#pragma once
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>

#include <spdlog/details/file_helper.h>
#include <spdlog/sinks/base_sink.h>

#include "Options.hpp"

namespace lc
{
class Logger;  // 所属日志实例的前置声明

// 补充按日文件名、组合滚动和宿主回调。
class FileSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
  FileSink(Logger& owner,
           const Options& options);  // 保存所属实例并准备日志文件
  std::uint32_t emit(const spdlog::details::log_msg& message);  // 输出记录
protected:
  void sink_it_(
      const spdlog::details::log_msg& message) override;  // spdlog 工作入口
  void flush_() override;  // spdlog 文件缓冲刷新
private:
  void open_day(const std::string& day);  // 切换日期文件
  void roll();  // 大小滚动
  void cleanup();  // 清理历史文件
  Logger& owner_;  // 所属实例，生命周期由宿主管理
  Options options_;  // 不含临时文本指针的配置
  std::string application_;  // 已复制的应用名
  std::filesystem::path directory_;  // 日志绝对目录
  spdlog::details::file_helper file_;  // spdlog 文件资源
  std::string day_;  // 当前文件日期
  std::uint64_t file_size_ = 0;  // 当前文件字节数
  std::chrono::steady_clock::time_point roll_retry_after_{}; // 扫描失败后的下次滚动尝试时间
};
}  // namespace lc
