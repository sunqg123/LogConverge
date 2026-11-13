#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>

#include "Options.hpp"

namespace lc
{
extern thread_local bool in_callback;  // 阻止宿主回调递归
bool valid_text(LC_Text text) noexcept;  // 校验 UTF-8
std::string copy_text(LC_Text text);  // 复制调用方文本
class FileSink;  // 文件输出目标的前置声明

// spdlog 实例及 SDK 生命周期适配。
class Logger final
{
public:
  explicit Logger(const Options& options);  // 复制配置并准备输出目标
  ~Logger();  // 销毁后释放剩余成员资源
  Logger(const Logger&) = delete;  // 禁止复制线程和文件资源
  Logger& operator=(const Logger&) = delete;  // 禁止复制赋值
  void start();  // 创建 spdlog 线程池和控制线程
  std::uint32_t write(const LC_Record& record);  // 校验并投递
  std::uint32_t destroy(std::uint32_t timeout);  // 排空并等待释放
  void consume(const spdlog::details::log_msg& message) noexcept;  // sink 回调
private:
  void control() noexcept;  // 定时刷新与销毁协调
  void fault(std::uint32_t error) noexcept;  // 记录致命故障
  void finish_post(bool queued) noexcept; // 归还投递名额并通知销毁协调
  Options options_;  // 固定初始化配置
  std::mutex mutex_;  // 状态和投递锁
  std::mutex join_mutex_;  // 控制线程回收锁
  std::condition_variable changed_;  // 状态变化通知
  std::shared_ptr<FileSink> sink_;  // 文件及宿主回调适配器
  std::shared_ptr<spdlog::details::thread_pool> pool_;  // spdlog 有界队列
  std::shared_ptr<spdlog::async_logger> backend_;  // spdlog 异步日志器
  std::thread controller_;  // 刷新与超时销毁协调线程
  std::atomic<bool> closing_{false}; // 停止接收新记录
  std::size_t pending_ = 0; // 正在准备或等待消费的记录数
  std::size_t posting_ = 0; // 正在锁外复制和入队的调用数
  bool done_ = false;  // 后台资源已释放
  bool started_ = false;  // 后台资源已创建
  std::uint32_t error_ = LC_OK;  // 保留的诊断结果
};
}  // namespace lc
