#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>

#include "Logger.hpp"

namespace
{
// 允许已取得租约的 Write 与 Destroy 并发结束。
struct Registry
{
  std::mutex mutex;  // 注册表锁
  std::unordered_map<std::uintptr_t, std::shared_ptr<lc::Logger>>
      entries;  // 活跃标识及实例租约
  std::uintptr_t next_id = 1;  // 单调分配，不复用已销毁的标识
};

// 注册表独立于业务实例。
Registry& registry()
{
  static auto* value = new Registry;  // 避免 DLL 卸载时隐式等待线程
  return *value;
}

// 获取租约，防止并发销毁导致悬空访问。
std::shared_ptr<lc::Logger> acquire(LC_Handle* handle)
{
  auto& r = registry();  // 全局实例注册表
  std::lock_guard<std::mutex> lock(r.mutex);  // 保护句柄查找
  const auto found = r.entries.find(
      reinterpret_cast<std::uintptr_t>(handle));  // 不解引用不透明句柄
  return found == r.entries.end() ? nullptr : found->second;
}

// 所有 C++ 异常在 ABI 内转换为结果码。
template<class F>
std::uint32_t protect(F&& action) noexcept
{
  try {
    return action();
  } catch (const std::bad_alloc&) {
    return LC_OUT_OF_MEMORY;
  } catch (const std::filesystem::filesystem_error&) {
    return LC_IO_ERROR;
  } catch (const spdlog::spdlog_ex&) {
    return LC_IO_ERROR;
  } catch (const std::invalid_argument&) {
    return LC_INVALID_ARGUMENT;
  } catch (...) {
    return LC_INTERNAL_ERROR;
  }
}

// 路径禁止嵌入空字符。
bool no_nul(LC_Text text)
{
  for (std::uint32_t i = 0; i < text.length; ++i) {
    if (!text.data[i]) {
      return false;
    }
  }
  return true;
}
}  // namespace

// 校验参数并创建实例。
uint32_t LC_CALL LogConverge_Initialize(const LC_InitOptions* input,
                                        LC_Handle** out) noexcept
{
  if (lc::in_callback) {
    return LC_REENTRANT;
  }
  return protect(
      [&]() -> uint32_t
      {
        if (!input || input->struct_size != sizeof(LC_InitOptions) || !out) {
          return LC_INVALID_ARGUMENT;
        }
        const auto directory = input->directory;  // 调用方目录视图
        const auto enabled = input->enabled;  // 本次运行开关
        if (enabled > 1 || directory.length > 131072
            || !lc::valid_text(directory) || !no_nul(directory))
        {
          return LC_INVALID_ARGUMENT;
        }
        if (*out) {
          return LC_ALREADY_INITIALIZED;
        }
        if (enabled && !directory.length) {
          return LC_INVALID_ARGUMENT;
        }
        lc::Options options;  // 内核配置，其他项使用默认值
        options.enabled = enabled != 0;
        options.directory = directory;
        auto logger = std::make_shared<lc::Logger>(options);  // 共享日志实例
        auto& r = registry();  // 全局注册表
        std::uintptr_t id = 0;  // 本次初始化取得的唯一标识
        // 注册锁只覆盖句柄发布，不覆盖线程启动。
        {
          std::lock_guard<std::mutex> lock(r.mutex);  // 标识分配和登记原子完成
          if (r.next_id > static_cast<std::uintptr_t>(
                  (std::numeric_limits<std::intptr_t>::max)()))
          {
            return LC_OUT_OF_MEMORY;
          }
          id = r.next_id++;  // 即使后续启动失败也不重复使用
          r.entries.emplace(id, logger);
        }
        try {
          logger->start();
        } catch (...) {
          std::lock_guard<std::mutex> lock(r.mutex);
          r.entries.erase(id);
          throw;
        }
        // 指针宽度仅用于 ABI 传递，句柄不是可访问的对象地址。
        *out = reinterpret_cast<LC_Handle*>(id);
        return LC_OK;
      });
}

// 将日志交给 spdlog 队列。
uint32_t LC_CALL LogConverge_Write(LC_Handle* h,
                                   const LC_Record* record) noexcept
{
  if (lc::in_callback) {
    return LC_REENTRANT;
  }
  if (!h) {
    return LC_FILTERED;
  }
  if (!record || record->struct_size != sizeof(LC_Record)
      || record->level >= LC_LEVEL_NONE)
  {
    return LC_INVALID_ARGUMENT;
  }
  // 租约保持实例存活，防止并发销毁。
  return protect(
      [&]() -> uint32_t
      {
        const auto logger = acquire(h);
        return logger ? logger->write(*record) : LC_CLOSED;
      });
}

// 统一排空、等待和释放句柄。
uint32_t LC_CALL LogConverge_Destroy(LC_Handle** h, uint32_t timeout) noexcept
{
  if (lc::in_callback) {
    return LC_REENTRANT;
  }
  return protect(
      [&]() -> uint32_t
      {
        if (!h) {
          return LC_INVALID_ARGUMENT;
        }
        if (!*h) {
          return LC_OK;
        }
        const auto logger = acquire(*h);  // 本次销毁持有的实例租约
        if (!logger) {
          *h = nullptr;
          return LC_CLOSED;
        }
        const auto result = logger->destroy(timeout);  // 排空及资源回收结果
        if (result != LC_TIMEOUT) {
          auto& r = registry();  // 待移除句柄的注册表
          std::lock_guard<std::mutex> lock(r.mutex);  // 保护句柄注销
          r.entries.erase(reinterpret_cast<std::uintptr_t>(*h));
          *h = nullptr;  // 已进入的调用仍持有安全租约
        }
        return result;
      });
}
