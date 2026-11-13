namespace LogConverge.Sharp;

/// <summary>
/// 日志调用结果；不会以日志异常中断业务。
/// </summary>
public enum LogResult : uint
{
    /// <summary>
    /// 调用成功。
    /// </summary>
    Ok = 0,
    /// <summary>
    /// 当前未启用或未输出。
    /// </summary>
    Filtered = 1,
    /// <summary>
    /// 队列满载，本条被丢弃。
    /// </summary>
    Dropped = 2,
    /// <summary>
    /// 参数或文本无效。
    /// </summary>
    InvalidArgument = 3,
    /// <summary>
    /// 本次运行已初始化。
    /// </summary>
    AlreadyInitialized = 5,
    /// <summary>
    /// 正在关闭或已经关闭。
    /// </summary>
    Closed = 6,
    /// <summary>
    /// 排空等待超时，可稍后重试。
    /// </summary>
    Timeout = 7,
    /// <summary>
    /// 文件操作失败。
    /// </summary>
    IoError = 8,
    /// <summary>
    /// 原生回调失败。
    /// </summary>
    CallbackError = 9,
    /// <summary>
    /// 禁止重入。
    /// </summary>
    Reentrant = 10,
    /// <summary>
    /// 内存不足。
    /// </summary>
    OutOfMemory = 11,
    /// <summary>
    /// 内部操作失败。
    /// </summary>
    InternalError = 12,
    /// <summary>
    /// 未找到包内原生库。
    /// </summary>
    NativeLibraryNotFound = 100,
    /// <summary>
    /// 当前原生包不支持该平台。
    /// </summary>
    UnsupportedPlatform = 101,
    /// <summary>
    /// 原生入口与封装不匹配。
    /// </summary>
    NativeLibraryIncompatible = 102,
    /// <summary>
    /// 原生库格式或依赖不兼容。
    /// </summary>
    NativeLibraryLoadFailed = 103,
}
