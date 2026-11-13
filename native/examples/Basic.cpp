#include <filesystem>
#include <iostream>
#include <string>

#include <LogConverge/LogConverge.hpp>

// 解析目录参数并演示日志生命周期。
int main(int argc, char** argv)
{
  if (argc != 2) {
    std::cerr << "Usage: LogConvergeExample <log-directory>\n";
    return 1;
  }
  // 宿主明确解析命令行路径。
  const std::string argument = argv[1]; // 命令行中的 UTF-8 目录
#if defined(__cpp_lib_char8_t)
  const std::filesystem::path input(std::u8string(argument.begin(), argument.end())); // C++20 路径
#else
  const auto input = std::filesystem::u8path(argument); // C++17 路径
#endif
  const auto utf8_directory = std::filesystem::absolute(input).u8string(); // 绝对目录的 UTF-8 表示
  // C++17/20 均按 UTF-8 字节传入，不使用系统代码页转换。
  const std::string directory(reinterpret_cast<const char*>(utf8_directory.data()),
                              utf8_directory.size());
  LogConverge::Logger log; // 当前应用的日志实例
  // 这是宿主示例的选择，SDK 不判断宿主的构建模式。
#if defined(NDEBUG)
  constexpr bool enabled = false; // 示例在 Release 模式关闭日志
#else
  constexpr bool enabled = true; // 示例在 Debug 模式启用日志
#endif
  if (log.Initialize(directory, enabled) != LC_OK)
  {
    return 2;
  }
  log.Write(LogConverge::Level::Info, "Network", "连接成功");
  log.Write(LogConverge::Level::Debug, "App", "调试消息\n第二行");
  log.Write(LogConverge::Level::Error,
            "Playback",
            "播放失败",
            "HostException: detail");
  return log.Destroy() == LC_OK ? 0 : 4;
}
