#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "FileSink.hpp"

#include <spdlog/fmt/chrono.h>

#include "Logger.hpp"

// 文件系统命名空间别名。
namespace fs = std::filesystem;

namespace lc
{
// 受管文件名只含 ASCII，避免路径编码转换影响历史扫描。
static std::string managed_name(const fs::path& path)
{
  const auto name = path.filename().native();  // 平台原生文件名
  if (name.size() < 12 || name.size() > 23) {
    return {};
  }
  std::string text;  // 可参与日期格式检查的 ASCII 字节
  text.reserve(name.size());
  for (auto character : name) {
    if (static_cast<std::uint32_t>(character) > 127) {
      return {};
    }
    text.push_back(static_cast<char>(character));
  }
  return text;
}

// 转义边界字符，保持一条记录一行。
static std::string escape(const LC_Text& text)
{
  // 转义后的输出文本。
  std::string out;
  // 控制字符转义所用的十六进制字符。
  constexpr char hex[] = "0123456789ABCDEF";
  for (std::uint32_t i = 0; i < text.length; ++i) {
    // 当前待转义的字节。
    const auto c = text.data[i];
    switch (c) {
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '[':
        out += "\\[";
        break;
      case ']':
        out += "\\]";
        break;
      default:
        if (c < 32 || c == 127) {
          out += "\\x";
          out += hex[c >> 4];
          out += hex[c & 15];
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

// 识别 SDK 管理的日期文件，其他文件不参与清理。
static bool managed_file(const std::string& name)
{
  if (name.size() < 12 || name.compare(name.size() - 4, 4, ".txt") != 0) {
    return false;
  }
  for (std::size_t i = 0; i < 8; ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return false;
    }
  }
  if (name.size() == 12) {
    return true;
  }
  if (name[8] != '.' || name.size() < 14 || name.size() > 23) {
    return false;
  }
  for (std::size_t i = 9; i < name.size() - 4; ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return false;
    }
  }
  return true;
}

// 准备绝对目录和当天文件。
FileSink::FileSink(Logger& owner, const Options& o)
    : owner_(owner)
    , options_(o)
    , application_(copy_text(o.application))
{
  options_.directory = {};
  options_.application = {};  // 不保存外部文本指针
  if (!o.file_enabled) {
    return;
  }
  const auto directory = copy_text(o.directory);  // 已校验的 UTF-8 目录字节
#if defined(__cpp_lib_char8_t)
  directory_ = fs::path(std::u8string(directory.begin(), directory.end()))
                   .lexically_normal();
#else
  directory_ = fs::u8path(directory).lexically_normal();
#endif
  if (!directory_.is_absolute()) {
    throw std::invalid_argument("absolute directory required");
  }
  fs::create_directories(directory_);
  // 当前本地时间，用于初始化日期文件。
  const auto tm = spdlog::details::os::localtime();
  open_day(spdlog::fmt_lib::format("{:%Y%m%d}", tm));
}

// 接收 spdlog 的异步记录。
void FileSink::sink_it_(const spdlog::details::log_msg& message)
{
  owner_.consume(message);  // spdlog 已持有 sink 锁
}

// 刷新文件缓冲，不承诺断电持久化。
void FileSink::flush_()
{
  if (!day_.empty()) {
    file_.flush();
  }
}

// 按日期追加打开，失败时不保留旧句柄。
void FileSink::open_day(const std::string& day)
{
  if (!day_.empty()) {
    file_.flush();
  }
  // 目标日期副本，防止清空成员时失效。
  const auto selected_day = day;
  file_.close();
  day_.clear();
  file_.open((directory_ / (selected_day + ".txt")).native());
  day_ = selected_day;
  file_size_ = file_.size();
  roll_retry_after_ = {};
  cleanup();
}

// 当前文件归档后重新打开当天文件。
void FileSink::roll()
{
  // 滚动前的日志日期。
  const auto day = day_;
  const auto now = std::chrono::steady_clock::now(); // 避免扫描失败后每条日志都重复扫描
  if (now < roll_retry_after_) return;
  // 当天已有归档的最大序号。
  std::uint64_t index = 0;
  std::error_code error; // 扫描失败时保留当前文件继续追加
  fs::directory_iterator it(directory_, error), end;
  if (error) {
    roll_retry_after_ = now + std::chrono::seconds(1);
    return;
  }
  while (it != end) {
    const auto& entry = *it; // 当前扫描到的目录条目
    const auto name =
        managed_name(entry.path());  // 仅使用受管格式的 ASCII 文件名
    if (managed_file(name) && name.size() > 12 && name.compare(0, 8, day) == 0)
    {
      index = (std::max)(index,
                         static_cast<std::uint64_t>(
                             std::stoull(name.substr(9, name.size() - 13))));
    }
    it.increment(error);
    if (error) {
      roll_retry_after_ = now + std::chrono::seconds(1);
      return;
    }
  }
  if (index >= 9999999999ULL) {
    throw fs::filesystem_error("archive counter exhausted", directory_, std::make_error_code(std::errc::value_too_large));
  }
  // 下一份归档文件名。
  const auto archive = spdlog::fmt_lib::format("{}.{:06}.txt", day, index + 1);
  file_.flush();
  file_.close();
  day_.clear();
  fs::rename(directory_ / (day + ".txt"), directory_ / archive);
  open_day(day);
}

// 按日期及归档序号清理，保留当前文件。
void FileSink::cleanup()
{
  if (!options_.max_files) {
    return;
  }

  // 历史文件排序键。
  struct File
  {
    fs::path path;  // 文件位置
    std::string day;  // 日期
    std::uint64_t part;  // 归档序号
  };

  // 需要按日期及序号排序的历史文件。
  std::vector<File> files;
  std::error_code error;  // 清理失败不影响继续写入
  fs::directory_iterator it(directory_, error), end;  // 当前扫描位置和结束标记
  if (error) {
    return;
  }
  while (it != end) {
    const auto& entry = *it;  // 当前目录条目
    std::error_code status_error;  // 当前条目的状态查询结果
    const auto status = entry.symlink_status(status_error);  // 不跟随符号链接
    if (!status_error && fs::is_regular_file(status)) {
      const auto name =
          managed_name(entry.path());  // 非 ASCII 文件不属于受管日志
      if (managed_file(name)) {
        // 当天基础文件排在同日归档之后。
        const auto part = name.size() == 12
            ? (std::numeric_limits<std::uint64_t>::max)()
            : static_cast<std::uint64_t>(
                  std::stoull(name.substr(9, name.size() - 13)));
        files.push_back({entry.path(), name.substr(0, 8), part});
      }
    }
    it.increment(error);
    if (error) {
      return;  // 扫描不完整时暂不清理
    }
  }
  // 按日期、归档序号升序排列，优先清理最旧文件。
  std::sort(files.begin(),
            files.end(),
            [](const File& a, const File& b)
            { return a.day == b.day ? a.part < b.part : a.day < b.day; });
  // 尚未删除的受管文件数量。
  auto remaining = files.size();
  // 当前正在写入的基础文件。
  const auto active = directory_ / (day_ + ".txt");
  // 按清理顺序检查的历史文件。
  for (const auto& file : files) {
    if (remaining <= options_.max_files) {
      break;
    }
    if (file.path != active && fs::remove(file.path, error)) {
      --remaining;
    }
  }
}

// 解包明确字段，使用 spdlog/fmt 格式化及写入。
std::uint32_t FileSink::emit(const spdlog::details::log_msg& message)
{
  std::uint32_t lengths[3] {};  // 内部字段长度，不对外暴露
  if (message.payload.size() < sizeof(lengths)) {
    throw std::logic_error("record header");
  }
  std::memcpy(lengths, message.payload.data(), sizeof(lengths));
  // 长度头与三段文本的总字节数。
  std::uint64_t total = sizeof(lengths);
  // 累加各字段声明的长度。
  for (auto length : lengths) {
    total += length;
  }
  if (total != message.payload.size()) {
    throw std::logic_error("record length");
  }
  LC_Text fields[3] {};  // 分类、消息、异常
  // 指向下一段文本的字节地址。
  auto data = reinterpret_cast<const std::uint8_t*>(message.payload.data()
                                                    + sizeof(lengths));
  for (std::size_t i = 0; i < 3; ++i) {
    fields[i] = {data, lengths[i]};
    data += lengths[i];
  }
  // 记录生产时间对应的 Unix 毫秒数。
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             message.time.time_since_epoch())
                             .count();
  if (options_.file_enabled) {
    // 记录生产时间对应的本地时间。
    const auto tm = spdlog::details::os::localtime(
        spdlog::log_clock::to_time_t(message.time));
    // 当前记录所属的本地日期。
    const auto day = spdlog::fmt_lib::format("{:%Y%m%d}", tm);
    if (day != day_) {
      open_day(day);
    }
    // 日志级别对应的显示名称。
    constexpr const char* levels[] = {
        "Trace", "Debug", "Info", "Warning", "Error", "Critical"};
    // 已复制应用标签的文本视图。
    const LC_Text app {
        reinterpret_cast<const std::uint8_t*>(application_.data()),
        static_cast<std::uint32_t>(application_.size())};
    // 格式化后的单行日志正文。
    auto text = spdlog::fmt_lib::format(
        "{:%Y-%m-%d %H:%M:%S}.{:03} [{}] [{}] [{}] [Thread={}]",
        tm,
        timestamp % 1000,
        levels[message.level],
        escape(app),
        escape(fields[0]),
        message.thread_id);
    text += " " + escape(fields[1]);
    if (fields[2].length) {
      text += spdlog::fmt_lib::format(" [Exception={}]", escape(fields[2]));
    }
    text += '\n';
    if (options_.max_file_bytes && file_size_
        && (file_size_ >= options_.max_file_bytes
            || text.size() > options_.max_file_bytes - file_size_))
    {
      roll();
    }
    // 交给 spdlog 文件接口的写入缓冲。
    spdlog::memory_buf_t buffer;
    buffer.append(text.data(), text.data() + text.size());
    file_.write(buffer);
    file_size_ += text.size();
  }
  if (!options_.callback) {
    return LC_OK;
  }
  // 当前记录对应的宿主回调事件。
  const LC_Event event {
      sizeof(LC_Event),
      static_cast<std::uint32_t>(message.level),
      timestamp,
      static_cast<std::uint64_t>(message.thread_id),
      {reinterpret_cast<const std::uint8_t*>(application_.data()),
       static_cast<std::uint32_t>(application_.size())},
      fields[0],
      fields[1],
      fields[2]};
  // 宿主回调返回值。
  std::uint32_t result;
  in_callback = true;
  try {
    result = options_.callback(options_.callback_context, &event);
  } catch (...) {
    result = LC_CALLBACK_ERROR;
  }
  in_callback = false;
  return result == LC_OK ? LC_OK : LC_CALLBACK_ERROR;
}
}  // namespace lc
