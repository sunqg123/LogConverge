#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>

#include "LogConverge.h"

namespace LogConverge
{
// C++ 日志级别。
enum class Level : std::uint32_t
{
  Trace = LC_LEVEL_TRACE,  // 跟踪
  Debug = LC_LEVEL_DEBUG,  // 调试
  Info = LC_LEVEL_INFO,  // 信息
  Information = LC_LEVEL_INFO,  // Info 的同义名称
  Warning = LC_LEVEL_WARNING,  // 警告
  Error = LC_LEVEL_ERROR,  // 错误
  Critical = LC_LEVEL_CRITICAL,  // 严重错误
  None = LC_LEVEL_NONE  // 禁用
};

// 构造 UTF-8 视图，不转换字符串编码。
inline LC_Text Text(std::string_view value) noexcept
{
  return {reinterpret_cast<const std::uint8_t*>(value.data()),
          value.size() <= UINT32_MAX ? static_cast<std::uint32_t>(value.size())
                                     : UINT32_MAX};
}

// 头文件外观；所有跨 DLL 调用仍使用 C ABI。
class Logger final
{
public:
  Logger() = default;  // 构造未初始化的日志入口
  Logger(const Logger&) = delete;  // 禁止复制句柄所有权
  Logger& operator=(const Logger&) = delete;  // 禁止复制赋值

  ~Logger() { Destroy(LC_WAIT_FOREVER); }  // 析构等待释放，禁止从回调内析构

  // 本次运行是否记录由宿主明确传入，不自动判断 Debug/Release。
  // directory 为 UTF-8 绝对目录，enabled 为本次运行开关。
  std::uint32_t Initialize(std::string_view directory, bool enabled) noexcept
  {
    const LC_InitOptions options {sizeof(LC_InitOptions),
                                  enabled ? 1u : 0u,
                                  Text(directory)};  // 原生初始化配置
    auto handle = handle_.load();  // 当前句柄副本
    const auto result =
        LogConverge_Initialize(&options, &handle);  // 初始化结果
    if (result == LC_OK) {
      handle_.store(handle);
    }
    return result;
  }

  // 写入日志，文本在调用返回前复制。
  // level 为级别，category 为分类，message 为正文，exception 为异常详情。
  std::uint32_t Write(Level level,
                      std::string_view category,
                      std::string_view message,
                      std::string_view exception = {}) noexcept
  {
    // 分类、正文和异常保持独立字段。
    LC_Record record {sizeof(LC_Record),
                      static_cast<std::uint32_t>(level),
                      Text(category),
                      Text(message),
                      Text(exception)};
    return LogConverge_Write(handle_.load(), &record);
  }

  // 排空并释放资源；超时保留实例，稍后重试。
  // timeout_ms 为等待毫秒数，LC_WAIT_FOREVER 表示无限等待。
  std::uint32_t Destroy(std::uint32_t timeout_ms = 3000) noexcept
  {
    auto handle = handle_.load();  // 本次销毁使用的句柄副本
    const auto result = LogConverge_Destroy(&handle, timeout_ms);  // 销毁结果
    if (!handle) {
      handle_.store(nullptr);
    }
    return result;
  }

private:
  std::atomic<LC_Handle*> handle_ {nullptr};  // 原生实例句柄
};
}  // namespace LogConverge
