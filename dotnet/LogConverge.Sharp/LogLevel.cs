namespace LogConverge.Sharp;

/// <summary>
/// 日志级别。
/// </summary>
public enum LogLevel : uint
{
    /// <summary>
    /// 跟踪。
    /// </summary>
    Trace = 0,
    /// <summary>
    /// 调试。
    /// </summary>
    Debug = 1,
    /// <summary>
    /// 信息。
    /// </summary>
    Info = 2,
    /// <summary>
    /// Info 的同义名称。
    /// </summary>
    Information = 2,
    /// <summary>
    /// 警告。
    /// </summary>
    Warning = 3,
    /// <summary>
    /// 错误。
    /// </summary>
    Error = 4,
    /// <summary>
    /// 严重错误。
    /// </summary>
    Critical = 5,
    /// <summary>
    /// 禁用级别。
    /// </summary>
    None = 6,
}
