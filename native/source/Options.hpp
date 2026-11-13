#pragma once
#include <cstdint>

#include "LogConverge/LogConverge.h"

namespace lc
{
// 队列满载策略。
enum LC_Overflow
{
  LC_DROP_NEWEST = 0,  // 丢弃新记录
  LC_BLOCK = 1  // 等待空位，销毁时唤醒
};

// 回调事件；文本仅在本次回调期间有效。
typedef struct LC_Event
{
  uint32_t struct_size;  // 结构体大小
  uint32_t level;  // LC_Level 级别
  int64_t timestamp_unix_ms;  // Unix 毫秒时间
  uint64_t thread_id;  // 产生日志的线程
  LC_Text application;  // 应用名称
  LC_Text category;  // 模块或分类
  LC_Text message;  // 原始消息
  LC_Text exception;  // 可选异常详情
} LC_Event;

// spdlog 工作线程调用；返回 LC_OK，宿主异常须自行捕获。
typedef uint32_t(LC_CALL* LC_Callback)(void* context, const LC_Event* event);

// 内核默认配置，不跨语言传递。
struct Options
{
  bool enabled = false;  // 本次运行开关
  std::uint32_t minimum_level = LC_LEVEL_TRACE;  // 启用后接收全部有效级别
  bool file_enabled = true;  // 默认写入文件
  std::uint32_t queue_capacity = 4096;  // 等待队列容量
  std::uint32_t overflow_policy = LC_DROP_NEWEST;  // 满载丢弃新记录
  std::uint32_t flush_interval_ms = 1000;  // 自动刷新间隔
  std::uint32_t max_record_bytes = 1048576;  // 单条文本上限
  std::uint32_t max_files = 10;  // 历史文件保留数量
  std::uint64_t max_file_bytes = 10485760;  // 单文件大小上限
  LC_Text directory {};  // 宿主传入的绝对目录
  LC_Text application {reinterpret_cast<const std::uint8_t*>("Application"),
                       11};  // 默认应用标签
  LC_Callback callback = nullptr;  // 预留内部回调
  void* callback_context = nullptr;  // 预留回调上下文
};
}  // namespace lc
