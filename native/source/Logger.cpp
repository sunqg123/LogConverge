#include <stdexcept>

#include "Logger.hpp"

#include "FileSink.hpp"

// 单调时钟，用于计算等待截止时间。
using Clock = std::chrono::steady_clock;
// 毫秒时长别名。
using Milliseconds = std::chrono::milliseconds;

namespace lc
{
thread_local bool in_callback = false;  // 当前线程是否正在回调宿主

// 校验文本指针、字节长度和 UTF-8 编码。
bool valid_text(LC_Text t) noexcept
{
  if (!t.data) {
    return t.length == 0;
  }
  // 拒绝非法 UTF-8、过长编码和代理码点。
  for (std::uint32_t i = 0; i < t.length;) {
    // 当前字节、解码码点、续字节数和最小合法码点。
    std::uint32_t c = t.data[i++], value = c, continuation = 0, minimum = 0;
    if (c < 0x80) {
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) {
      continuation = 1;
      value = c & 31;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      continuation = 2;
      value = c & 15;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      continuation = 3;
      value = c & 7;
      minimum = 0x10000;
    } else {
      return false;
    }
    if (continuation > t.length - i) {
      return false;
    }
    while (continuation--) {
      c = t.data[i++];
      if ((c & 0xc0) != 0x80) {
        return false;
      }
      value = (value << 6) | (c & 63);
    }
    if (value < minimum || value > 0x10ffff
        || (value >= 0xd800 && value <= 0xdfff))
    {
      return false;
    }
  }
  return true;
}

// 将调用方文本复制到 SDK 自有字符串。
std::string copy_text(LC_Text t)
{
  return t.length ? std::string(reinterpret_cast<const char*>(t.data), t.length)
                  : std::string {};
}

// 仅内部使用的定长字段头，不解析用户消息内容。
static std::string pack(const LC_Record& r)
{
  // 按分类、消息、异常排列的文本视图。
  const LC_Text fields[] = {r.category, r.message, r.exception};
  // 三段文本的字节数，用于内部解包。
  const std::uint32_t lengths[] = {
      r.category.length, r.message.length, r.exception.length};
  // 先写入长度头，再追加三段文本。
  std::string data(reinterpret_cast<const char*>(lengths), sizeof(lengths));
  data.reserve(sizeof(lengths) + r.category.length + r.message.length + r.exception.length);
  // 当前需要追加的文本字段。
  for (const auto& field : fields) {
    if (field.length) {
      data.append(reinterpret_cast<const char*>(field.data), field.length);
    }
  }
  return data;
}

// 将相对超时转换为统一截止时间。
static Clock::time_point deadline(std::uint32_t timeout)
{
  return timeout == LC_WAIT_FOREVER ? (Clock::time_point::max)()
                                    : Clock::now() + Milliseconds(timeout);
}

// 复制配置并准备输出目标。
Logger::Logger(const Options& o)
    : options_(o)
{
  if (o.enabled && o.minimum_level != LC_LEVEL_NONE) {
    sink_ = std::make_shared<FileSink>(*this, o);
  }
  options_.directory = {};
  options_.application = {};  // 不保留临时字节地址
}

Logger::~Logger() = default;  // Destroy 已等待所有后台资源结束

// 为实例创建独立的 spdlog 队列。
void Logger::start()
{
  if (!sink_) {
    return;
  }
  pool_ = std::make_shared<spdlog::details::thread_pool>(
      options_.queue_capacity, 1);
  try {
    backend_ = std::make_shared<spdlog::async_logger>(
        "LogConverge", sink_, pool_, spdlog::async_overflow_policy::block);
    backend_->set_level(spdlog::level::trace);
    backend_->set_error_handler([this](const std::string&)
                                { fault(LC_INTERNAL_ERROR); });
    controller_ = std::thread([this] { control(); });
    started_ = true;
  } catch (...) {
    pool_.reset();
    backend_.reset();  // 初始化失败时回收空线程池
    throw;
  }
}

// 初始化开关和级别在入队前生效。
std::uint32_t Logger::write(const LC_Record& r)
{
  if (closing_.load(std::memory_order_acquire)) {
    return LC_CLOSED;
  }
  if (!options_.enabled || options_.minimum_level == LC_LEVEL_NONE
      || r.level < options_.minimum_level)
  {
    return LC_FILTERED;
  }
  // 当前记录的文本总字节数。
  const auto size = static_cast<std::uint64_t>(r.category.length)
      + r.message.length + r.exception.length;
  if (size > options_.max_record_bytes || !valid_text(r.category)
      || !valid_text(r.message) || !valid_text(r.exception))
  {
    return LC_INVALID_ARGUMENT;
  }
  std::shared_ptr<spdlog::async_logger> backend; // 本次投递使用的日志器租约
  spdlog::details::thread_pool* pool = nullptr; // posting_ 保护的线程池
  {
    std::unique_lock<std::mutex> lock(mutex_); // 只保护状态和名额预留
    if (closing_) return LC_CLOSED;
    if (options_.overflow_policy == LC_BLOCK) {
      changed_.wait(lock, [this] { return closing_ || pending_ < options_.queue_capacity; });
      if (closing_) return LC_CLOSED;
    } else if (pending_ >= options_.queue_capacity) {
      return LC_DROPPED;
    }
    ++pending_;
    ++posting_;
    backend = backend_;
    pool = pool_.get();
  }
  try {
    const auto payload = pack(r); // 大块复制在状态锁外完成
    const spdlog::details::log_msg message(
        spdlog::log_clock::now(), {}, backend->name(),
        static_cast<spdlog::level::level_enum>(r.level),
        spdlog::string_view_t(payload.data(), payload.size()));
    // 已预留名额，不会因其他生产者抢占空位而阻塞。
    pool->post_log(std::move(backend), message, spdlog::async_overflow_policy::block);
  } catch (...) {
    backend.reset();
    finish_post(false);
    throw;
  }
  finish_post(true);
  return LC_OK;
}

// 复制失败时归还空位，成功时由消费者归还。
void Logger::finish_post(bool queued) noexcept
{
  std::lock_guard<std::mutex> lock(mutex_); // 投递和退出共用的计数锁
  if (!queued) --pending_;
  --posting_;
  if (closing_ || (!queued && options_.overflow_policy == LC_BLOCK)) changed_.notify_all();
}

// 关闭入口，后台安全排空和释放。
std::uint32_t Logger::destroy(std::uint32_t timeout)
{
  // 本次销毁等待的截止时间。
  const auto end = deadline(timeout);
  // 保护状态检查与队列投递。
  std::unique_lock<std::mutex> lock(mutex_);
  closing_ = true;
  changed_.notify_all();  // 同时唤醒满载等待者
  if (!started_) {
    sink_.reset();
    done_ = true;
  }
  if (!changed_.wait_until(lock, end, [this] { return done_; })) {
    return LC_TIMEOUT;
  }
  // 销毁后仍需返回的故障结果。
  const auto result = error_;
  lock.unlock();
  // 避免多次回收同一控制线程。
  std::lock_guard<std::mutex> join(join_mutex_);
  if (controller_.joinable()) {
    controller_.join();
  }
  return result;
}

// 致命故障停止接收，但仍回收后台资源。
void Logger::fault(std::uint32_t error) noexcept
{
  // 保护实例状态。
  std::lock_guard<std::mutex> lock(mutex_);
  error_ = error;
  closing_ = true;
  changed_.notify_all();
}

// 由 spdlog 工作线程串行处理。
void Logger::consume(const spdlog::details::log_msg& message) noexcept
{
  // 当前是否已发生致命输出故障。
  bool failed = false;
  {
    // 读取当前输出故障状态。
    std::lock_guard<std::mutex> lock(mutex_);
    --pending_; // spdlog 已取走本条记录，归还等待名额
    failed = error_ != LC_OK && error_ != LC_CALLBACK_ERROR;
    if (options_.overflow_policy == LC_BLOCK) {
      changed_.notify_all();  // 仅唤醒可能等待队列空位的生产者
    }
  }
  if (!failed) {
    try {
      if (sink_->emit(message) == LC_CALLBACK_ERROR) {
        // 保留首次宿主回调故障。
        std::lock_guard<std::mutex> lock(mutex_);
        if (error_ == LC_OK) {
          error_ = LC_CALLBACK_ERROR;
        }
      }
    } catch (const std::bad_alloc&) {
      fault(LC_OUT_OF_MEMORY);
    } catch (const std::filesystem::filesystem_error&) {
      fault(LC_IO_ERROR);
    } catch (const spdlog::spdlog_ex&) {
      fault(LC_IO_ERROR);
    } catch (...) {
      fault(LC_INTERNAL_ERROR);
    }
  }
}

// 协调自动刷新与有超时的销毁。
void Logger::control() noexcept
{
  try {
    for (;;) {
      // 等待刷新周期或销毁通知。
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait_for(lock,
                        Milliseconds(options_.flush_interval_ms),
                        [this] { return closing_.load(std::memory_order_acquire); });
      if (closing_) {
        break;
      }
      lock.unlock();
      sink_->flush();
    }
  } catch (const std::bad_alloc&) {
    fault(LC_OUT_OF_MEMORY);
  } catch (const spdlog::spdlog_ex&) {
    fault(LC_IO_ERROR);
  } catch (...) {
    fault(LC_INTERNAL_ERROR);
  }

  // 待排空并释放的线程池所有权。
  std::shared_ptr<spdlog::details::thread_pool> pool;
  {
    // 在锁内停止接收并移交线程池所有权。
    std::unique_lock<std::mutex> lock(mutex_);
    closing_ = true;
    changed_.wait(lock, [this] { return posting_ == 0; }); // 等待锁外投递结束后再回收队列
    pool = std::move(pool_);
    changed_.notify_all();
  }
  pool.reset();  // spdlog 排空队列并回收工作线程
  try {
    sink_->flush();
  } catch (const spdlog::spdlog_ex&) {
    fault(LC_IO_ERROR);
  } catch (...) {
    fault(LC_INTERNAL_ERROR);
  }
  backend_.reset();
  sink_.reset();  // 回调结束后才释放文件
  // 保护实例状态。
  std::lock_guard<std::mutex> lock(mutex_);
  done_ = true;
  changed_.notify_all();
}
}  // namespace lc
