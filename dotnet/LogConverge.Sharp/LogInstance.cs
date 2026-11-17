using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;

namespace LogConverge.Sharp;

/// <summary>
/// 具有独立开关、目录和原生句柄的日志实例。
/// </summary>
public sealed unsafe class LogInstance
{
    /// <summary>
    /// 串行管理初始化和销毁。
    /// </summary>
    private readonly object Lifecycle = new object();
    /// <summary>
    /// 保护尚未结束的原生调用。
    /// </summary>
    private readonly ReaderWriterLockSlim Calls = new ReaderWriterLockSlim();
    /// <summary>
    /// 严格校验并编码 UTF-8 文本。
    /// </summary>
    private static readonly Encoding Utf8 = new UTF8Encoding(false, true);
    /// <summary>
    /// 当前原生实例句柄。
    /// </summary>
    private IntPtr handle;
    /// <summary>
    /// 状态：0 未初始化，1 启用，2 禁用，3 正在关闭。
    /// </summary>
    private int state;
    /// <summary>
    /// 退出时用于排空和释放的总等待毫秒数。
    /// </summary>
    private const int ExitTimeoutMilliseconds = 3000;
    /// <summary>
    /// 保护活动实例登记与退出通知。
    /// </summary>
    private static readonly object ActiveLock = new object();
    /// <summary>
    /// 正常退出时需要清理的实例，销毁完成后移除。
    /// </summary>
    private static readonly HashSet<LogInstance> Active = new HashSet<LogInstance>();
    /// <summary>
    /// 是否已注册进程退出处理，由登记锁保护。
    /// </summary>
    private static bool exitHandlerRegistered;
    /// <summary>
    /// 进程是否已经开始退出，退出后不再初始化。
    /// </summary>
    private static int exiting;

    /// <summary>
    /// 创建尚未初始化的独立日志实例。
    /// </summary>
    public LogInstance() { }

    /// <summary>
    /// 设置日志目录和本次运行开关；相对目录以程序目录为基准。
    /// </summary>
    /// <param name="directory">日志输出目录。</param>
    /// <param name="enabled">本次运行是否记录日志。</param>
    public LogResult Initialize(string directory, bool enabled)
    {
        lock (Lifecycle)
        {
            if (Volatile.Read(ref exiting) != 0) return LogResult.Closed;
            if (Volatile.Read(ref state) != 0) return LogResult.AlreadyInitialized;
            bool tracked = false; // 记录是否已登记到退出清理列表
            try
            {
                if (!enabled)
                {
                    if (!(tracked = TryTrack(this))) return LogResult.Closed;
                    Volatile.Write(ref state, 2);
                    return LogResult.Ok;
                }
                if (!NativeLibraryLoader.IsSupported)
                    return LogResult.UnsupportedPlatform;
                if (string.IsNullOrWhiteSpace(directory) || directory.IndexOf('\0') >= 0) return LogResult.InvalidArgument;
                // 解析后的绝对目录。
                var absolute = Path.GetFullPath(Path.IsPathRooted(directory) ? directory : Path.Combine(AppContext.BaseDirectory, directory));
                if (Utf8.GetByteCount(absolute) > 131072) return LogResult.InvalidArgument;
                byte[] bytes = Utf8.GetBytes(absolute); // 目录字节
                if (!(tracked = TryTrack(this))) return LogResult.Closed;
                fixed (byte* p = bytes) // 调用期间固定目录内存
                {
                    // 与 C++ 初始化结构体对应的配置。
                    var options = new NativeMethods.Options
                    {
                        Size = (uint)sizeof(NativeMethods.Options),
                        Enabled = 1,
                        Directory = new NativeMethods.Text { Data = p, Length = (uint)bytes.Length }
                    };
                    var result = NativeMethods.Initialize(ref options, ref handle); // 初始化结果
                    if (result == LogResult.Ok) Volatile.Write(ref state, 1);
                    return result;
                }
            }
            catch (Exception e)
            {
                return Failure(e);
            }
            finally
            {
                if (tracked && state == 0 && handle == IntPtr.Zero) Untrack(this);
            }
        }
    }

    /// <summary>
    /// 在退出前登记实例，确保原生初始化期间模块不会被卸载。
    /// </summary>
    private static bool TryTrack(LogInstance instance)
    {
        lock (ActiveLock)
        {
            if (exiting != 0) return false;
            if (!exitHandlerRegistered)
            {
                AppDomain.CurrentDomain.ProcessExit += OnProcessExit;
                exitHandlerRegistered = true;
            }
            Active.Add(instance);
            return true;
        }
    }

    /// <summary>
    /// 移除已释放的实例。
    /// </summary>
    private static void Untrack(LogInstance instance)
    {
        lock (ActiveLock) Active.Remove(instance);
    }

    /// <summary>
    /// 正常退出时排空所有实例，完成后卸载原生模块。
    /// </summary>
    private static void OnProcessExit(object? sender, EventArgs args)
    {
        try
        {
            Stopwatch timer = Stopwatch.StartNew();
            LogInstance[] instances;
            lock (ActiveLock)
            {
                if (exiting != 0) return;
                Volatile.Write(ref exiting, 1);
                instances = new LogInstance[Active.Count];
                Active.CopyTo(instances);
            }

            bool released = true;
            foreach (LogInstance instance in instances)
            {
                if (instance.Destroy(RemainingTimeout(ExitTimeoutMilliseconds, timer)) != LogResult.Ok)
                {
                    released = false;
                }
            }
            if (released) NativeMethods.ReleaseLibrary();
        }
        catch
        {
            // 退出清理不向宿主抛出异常。
        }
    }

    /// <summary>
    /// 写入一条日志；关闭时直接返回，异常详情由封装转换。
    /// </summary>
    /// <param name="level">日志级别。</param>
    /// <param name="category">模块或分类。</param>
    /// <param name="message">日志正文，null 视为空文本。</param>
    /// <param name="exception">可选异常对象。</param>
    public LogResult Write(LogLevel level, string category, string? message, Exception? exception = null)
    {
        int current = Volatile.Read(ref state); // 当前生命周期状态
        if (current != 1) return current == 3 ? LogResult.Closed : LogResult.Filtered;
        if (level == LogLevel.None) return LogResult.Filtered;
        if ((uint)level > (uint)LogLevel.Critical) return LogResult.InvalidArgument;
        bool entered = false; // 本次是否持有调用读锁
        byte[]? buffer = null; // 三段文本共用的租借缓冲
        try
        {
            string error = exception?.ToString() ?? string.Empty; // 完整异常文本
            category = category ?? string.Empty; message = message ?? string.Empty;
            int categoryBytes = Utf8.GetByteCount(category); // 分类字节数
            int messageBytes = Utf8.GetByteCount(message); // 正文字节数
            int errorBytes = Utf8.GetByteCount(error); // 异常字节数
            long size = (long)categoryBytes + messageBytes + errorBytes; // 文本总字节数
            if (size > 1048576) return LogResult.InvalidArgument;
            if (!(entered = Calls.TryEnterReadLock(0))) return LogResult.Closed;
            if (Volatile.Read(ref state) != 1 || Volatile.Read(ref exiting) != 0) return LogResult.Closed;
            buffer = ArrayPool<byte>.Shared.Rent((int)size);
            Utf8.GetBytes(category.AsSpan(), buffer.AsSpan(0, categoryBytes));
            Utf8.GetBytes(message.AsSpan(), buffer.AsSpan(categoryBytes, messageBytes));
            Utf8.GetBytes(error.AsSpan(), buffer.AsSpan(categoryBytes + messageBytes, errorBytes));
            fixed (byte* data = buffer) // 一次固定整个缓冲，字段只传各自范围
            {
                // 本次跨语言传递的日志记录。
                var record = new NativeMethods.Record
                {
                    Size = (uint)sizeof(NativeMethods.Record),
                    Level = (uint)level,
                    Category = new NativeMethods.Text { Data = data, Length = (uint)categoryBytes },
                    Message = new NativeMethods.Text { Data = data + categoryBytes, Length = (uint)messageBytes },
                    Exception = new NativeMethods.Text { Data = data + categoryBytes + messageBytes, Length = (uint)errorBytes }
                };
                return NativeMethods.Write(handle, ref record);
            }
        }
        catch (Exception e)
        {
            return Failure(e);
        }
        finally
        {
            if (buffer != null) ArrayPool<byte>.Shared.Return(buffer, clearArray: true);
            if (entered)
            {
                Calls.ExitReadLock();
            }
        }
    }

    /// <summary>
    /// 延迟构造消息；关闭时不调用消息工厂。
    /// </summary>
    /// <param name="level">日志级别。</param>
    /// <param name="category">模块或分类。</param>
    /// <param name="messageFactory">需要写入时才执行的消息工厂。</param>
    public LogResult WriteLazy(LogLevel level, string category, Func<string> messageFactory)
    {
        int current = Volatile.Read(ref state); // 调用工厂前检查状态
        if (current != 1) return current == 3 ? LogResult.Closed : LogResult.Filtered;
        if (Volatile.Read(ref exiting) != 0) return LogResult.Closed;
        if (level == LogLevel.None) return LogResult.Filtered;
        if ((uint)level > (uint)LogLevel.Critical || messageFactory == null) return LogResult.InvalidArgument;
        try
        {
            return Write(level, category, messageFactory());
        }
        catch (Exception e)
        {
            return Failure(e);
        }
    }

    /// <summary>
    /// 可选提前排空并释放；正常退出时自动调用，超时可重试。
    /// </summary>
    /// <param name="timeoutMilliseconds">等待毫秒数，-1 表示无限等待。</param>
    public LogResult Destroy(int timeoutMilliseconds = 3000)
    {
        if (timeoutMilliseconds < -1) return LogResult.InvalidArgument;
        // 生命周期锁、在途调用及原生排空共享同一超时预算。
        Stopwatch timer = Stopwatch.StartNew();
        bool lifecycleEntered = false; // 是否持有生命周期锁
        bool callsEntered = false; // 是否持有调用写锁
        try
        {
            if (!(lifecycleEntered = Monitor.TryEnter(Lifecycle, timeoutMilliseconds))) return LogResult.Timeout;
            if (Volatile.Read(ref state) == 0) return LogResult.Ok;
            Volatile.Write(ref state, 3); // 新的 Write 不再等待销毁操作
            if (!(callsEntered = Calls.TryEnterWriteLock(RemainingTimeout(timeoutMilliseconds, timer)))) return LogResult.Timeout;
            if (handle == IntPtr.Zero) { Volatile.Write(ref state, 0); return LogResult.Ok; }
            // 销毁结果；超时时原生句柄仍然有效。
            var result = NativeMethods.Destroy(ref handle, timeoutMilliseconds == -1
                ? uint.MaxValue : (uint)RemainingTimeout(timeoutMilliseconds, timer));
            if (handle == IntPtr.Zero) Volatile.Write(ref state, 0);
            return result;
        }
        catch (Exception e)
        {
            return Failure(e);
        }
        finally
        {
            if (callsEntered) Calls.ExitWriteLock();
            if (lifecycleEntered)
            {
                if (state == 0 && handle == IntPtr.Zero) Untrack(this);
                Monitor.Exit(Lifecycle);
            }
        }
    }

    /// <summary>
    /// 计算剩余等待时间，-1 保持无限等待的含义。
    /// </summary>
    private static int RemainingTimeout(int timeoutMilliseconds, Stopwatch timer) => timeoutMilliseconds == -1
        ? -1 : (int)Math.Max(0, timeoutMilliseconds - timer.ElapsedMilliseconds);

    /// <summary>
    /// 将加载、编码及内部异常转换为结果码。
    /// </summary>
    private static LogResult Failure(Exception error) => error switch
    {
        DllNotFoundException => LogResult.NativeLibraryNotFound,
        EntryPointNotFoundException => LogResult.NativeLibraryIncompatible,
        BadImageFormatException => LogResult.NativeLibraryLoadFailed,
        PlatformNotSupportedException => LogResult.UnsupportedPlatform,
        UnauthorizedAccessException => LogResult.IoError,
        EncoderFallbackException => LogResult.InvalidArgument,
        ArgumentException => LogResult.InvalidArgument,
        NotSupportedException => LogResult.InvalidArgument,
        PathTooLongException => LogResult.InvalidArgument,
        IOException => LogResult.IoError,
        TypeInitializationException { InnerException: { } inner } => Failure(inner),
        OutOfMemoryException => LogResult.OutOfMemory,
        _ => LogResult.InternalError
    };

    /// <summary>
    /// 输出跟踪日志。
    /// </summary>
    /// <param name="category">模块或分类。</param>
    /// <param name="message">日志正文。</param>
    public LogResult Trace(string category, string message) => Write(LogLevel.Trace, category, message);

    /// <summary>
    /// 输出调试日志。
    /// </summary>
    /// <param name="category">模块或分类。</param>
    /// <param name="message">日志正文。</param>
    public LogResult Debug(string category, string message) => Write(LogLevel.Debug, category, message);

    /// <summary>
    /// 输出信息日志。
    /// </summary>
    /// <param name="category">模块或分类。</param>
    /// <param name="message">日志正文。</param>
    public LogResult Info(string category, string message) => Write(LogLevel.Info, category, message);

    /// <summary>
    /// 输出警告日志。
    /// </summary>
    /// <param name="category">模块或分类。</param>
    /// <param name="message">日志正文。</param>
    public LogResult Warning(string category, string message) => Write(LogLevel.Warning, category, message);

    /// <summary>
    /// 输出错误日志及可选异常。
    /// </summary>
    /// <param name="category">模块或分类。</param>
    /// <param name="message">日志正文。</param>
    /// <param name="exception">可选异常对象。</param>
    public LogResult Error(string category, string message, Exception? exception = null) => Write(LogLevel.Error, category, message, exception);

    /// <summary>
    /// 输出严重错误及可选异常。
    /// </summary>
    /// <param name="category">模块或分类。</param>
    /// <param name="message">日志正文。</param>
    /// <param name="exception">可选异常对象。</param>
    public LogResult Critical(string category, string message, Exception? exception = null) => Write(LogLevel.Critical, category, message, exception);
}
