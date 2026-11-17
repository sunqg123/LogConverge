using System;

namespace LogConverge.Sharp;

/// <summary>
/// 应用默认日志实例的静态入口。
/// </summary>
public static class Log
{
    /// <summary>
    /// 静态入口使用的默认实例。
    /// </summary>
    private static readonly LogInstance Default = new LogInstance();

    /// <summary>
    /// 初始化应用默认日志实例。
    /// </summary>
    public static LogResult Initialize(string directory, bool enabled) => Default.Initialize(directory, enabled);

    /// <summary>
    /// 写入应用默认日志。
    /// </summary>
    public static LogResult Write(LogLevel level, string category, string? message, Exception? exception = null) => Default.Write(level, category, message, exception);

    /// <summary>
    /// 延迟构造并写入应用默认日志。
    /// </summary>
    public static LogResult WriteLazy(LogLevel level, string category, Func<string> messageFactory) => Default.WriteLazy(level, category, messageFactory);

    /// <summary>
    /// 提前排空并释放应用默认日志实例。
    /// </summary>
    public static LogResult Destroy(int timeoutMilliseconds = 3000) => Default.Destroy(timeoutMilliseconds);

    /// <summary>
    /// 写入应用默认的跟踪日志。
    /// </summary>
    public static LogResult Trace(string category, string message) => Default.Trace(category, message);

    /// <summary>
    /// 写入应用默认的调试日志。
    /// </summary>
    public static LogResult Debug(string category, string message) => Default.Debug(category, message);

    /// <summary>
    /// 写入应用默认的信息日志。
    /// </summary>
    public static LogResult Info(string category, string message) => Default.Info(category, message);

    /// <summary>
    /// 写入应用默认的警告日志。
    /// </summary>
    public static LogResult Warning(string category, string message) => Default.Warning(category, message);

    /// <summary>
    /// 写入应用默认的错误日志。
    /// </summary>
    public static LogResult Error(string category, string message, Exception? exception = null) => Default.Error(category, message, exception);

    /// <summary>
    /// 写入应用默认的严重错误日志。
    /// </summary>
    public static LogResult Critical(string category, string message, Exception? exception = null) => Default.Critical(category, message, exception);
}
