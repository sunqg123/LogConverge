#ifndef LOGCONVERGE_H
#define LOGCONVERGE_H
#include <stdint.h>

#include "LogConverge/LogConverge_export.hpp"

#if defined(_WIN32)
#  define LC_CALL __cdecl  // Windows 调用约定
#else
#  define LC_CALL
#endif
#ifdef __cplusplus
#  define LC_NOEXCEPT noexcept  // C++ 异常不跨越导出边界
extern "C"
{
#else
#  define LC_NOEXCEPT
#endif

#define LC_WAIT_FOREVER UINT32_MAX  // 无限等待

// 日志级别；ABI 字段使用 uint32_t 保存枚举值。
enum LC_Level
{
  LC_LEVEL_TRACE = 0,  // 跟踪
  LC_LEVEL_DEBUG = 1,  // 调试
  LC_LEVEL_INFO = 2,  // 信息
  LC_LEVEL_WARNING = 3,  // 警告
  LC_LEVEL_ERROR = 4,  // 错误
  LC_LEVEL_CRITICAL = 5,  // 严重错误
  LC_LEVEL_NONE = 6  // 禁用
};
typedef enum LC_Level LC_Level;  // 兼容纯 C 的类型名称

// 调用结果。
enum LC_Result
{
  LC_OK = 0,  // 成功
  LC_FILTERED = 1,  // 未启用或被过滤
  LC_DROPPED = 2,  // 满载丢弃
  LC_INVALID_ARGUMENT = 3,  // 参数无效
  // 4 为保留值，避免改变现有结果码。
  LC_ALREADY_INITIALIZED = 5,  // 已初始化
  LC_CLOSED = 6,  // 实例已关闭
  LC_TIMEOUT = 7,  // 等待超时
  LC_IO_ERROR = 8,  // 文件操作失败
  LC_CALLBACK_ERROR = 9,  // 宿主回调失败
  LC_REENTRANT = 10,  // 禁止回调重入
  LC_OUT_OF_MEMORY = 11,  // 内存不足
  LC_INTERNAL_ERROR = 12  // 内部错误
};
typedef enum LC_Result LC_Result;  // 兼容纯 C 的类型名称

typedef struct LC_Handle LC_Handle;  // 不透明实例标识，不可解引用

// UTF-8 字节视图；长度不含终止符。
typedef struct LC_Text
{
  const uint8_t* data;  // 字节地址，空文本可为 NULL
  uint32_t length;  // 字节数
} LC_Text;

// 单条日志；Write 返回前复制已接收的数据。
typedef struct LC_Record
{
  uint32_t struct_size;  // 结构体大小
  uint32_t level;  // LC_Level 级别
  LC_Text category;  // 模块或分类
  LC_Text message;  // 原始消息
  LC_Text exception;  // 可选异常详情
} LC_Record;

// 初始化配置，布局信息由语言封装填写。
typedef struct LC_InitOptions
{
  uint32_t struct_size;  // 结构体大小
  uint32_t enabled;  // 本次运行开关，0/1
  LC_Text directory;  // 日志绝对目录
} LC_InitOptions;

// 路径和本次运行开关；enabled 为 0/1，*handle 必须为 NULL。
// options 为初始化配置，handle 接收创建的实例句柄。
LOGCONVERGE_EXPORT uint32_t LC_CALL LogConverge_Initialize(
    const LC_InitOptions* options, LC_Handle** handle) LC_NOEXCEPT;
// 写入一条日志；空句柄表示未启用。
// handle 为日志实例，record 为调用期间有效的记录。
LOGCONVERGE_EXPORT uint32_t LC_CALL
LogConverge_Write(LC_Handle* handle, const LC_Record* record) LC_NOEXCEPT;
// 停止接收、排空并释放；超时保留句柄，完成后置空。
// handle 为待销毁句柄的地址，timeout_ms 为等待毫秒数。
LOGCONVERGE_EXPORT uint32_t LC_CALL
LogConverge_Destroy(LC_Handle** handle, uint32_t timeout_ms) LC_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
