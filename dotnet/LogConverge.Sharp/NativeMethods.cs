using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace LogConverge.Sharp;

/// <summary>
/// 仅内部封送；业务代码不接触指针和编码。
/// </summary>
internal static unsafe class NativeMethods
{
    /// <summary>
    /// 首次启用时才释放资源并绑定原生入口。
    /// </summary>
    private static readonly Lazy<Bindings> Api = new Lazy<Bindings>(() => new Bindings(), LazyThreadSafetyMode.ExecutionAndPublication);
    /// <summary>
    /// UTF-8 字节视图，不持有调用方内存。
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct Text
    {
        /// <summary>
        /// 字节地址
        /// </summary>
        internal byte* Data;
        /// <summary>
        /// 字节数量
        /// </summary>
        internal uint Length;
    }
    /// <summary>
    /// 与原生初始化结构体保持相同布局。
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct Options
    {
        /// <summary>
        /// 结构体大小
        /// </summary>
        internal uint Size;
        /// <summary>
        /// 本次运行开关，0/1
        /// </summary>
        internal uint Enabled;
        /// <summary>
        /// 日志绝对目录
        /// </summary>
        internal Text Directory;
    }
    /// <summary>
    /// 与原生日志结构体保持相同布局。
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct Record
    {
        /// <summary>
        /// 结构体大小
        /// </summary>
        internal uint Size;
        /// <summary>
        /// 日志级别
        /// </summary>
        internal uint Level;
        /// <summary>
        /// 模块或分类
        /// </summary>
        internal Text Category;
        /// <summary>
        /// 原始消息
        /// </summary>
        internal Text Message;
        /// <summary>
        /// 可选异常详情
        /// </summary>
        internal Text Exception;
    }
    /// <summary>
    /// 创建原生实例，返回句柄由 Log 管理。
    /// </summary>
    internal static LogResult Initialize(ref Options options, ref IntPtr handle) => Api.Value.Initialize(ref options, ref handle);

    /// <summary>
    /// 原生返回前复制已经接收的记录。
    /// </summary>
    internal static LogResult Write(IntPtr handle, ref Record record) => Api.Value.Write(handle, ref record);

    /// <summary>
    /// 排空并释放实例，超时保留句柄。
    /// </summary>
    internal static LogResult Destroy(ref IntPtr handle, uint timeout) => Api.Value.Destroy(ref handle, timeout);

    /// <summary>
    /// 进程退出且实例已释放后，卸载模块并清理私有缓存。
    /// </summary>
    internal static void ReleaseLibrary()
    {
        if (Api.IsValueCreated) Api.Value.Release();
    }

    /// <summary>
    /// 初始化入口的调用约定与参数布局。
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate LogResult InitializeEntry(ref Options options, ref IntPtr handle);

    /// <summary>
    /// 写入入口的调用约定与参数布局。
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate LogResult WriteEntry(IntPtr handle, ref Record record);

    /// <summary>
    /// 销毁入口的调用约定与参数布局。
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate LogResult DestroyEntry(ref IntPtr handle, uint timeout);

    /// <summary>
    /// 持有原生模块和入口，整个运行期间只加载一次。
    /// </summary>
    private sealed class Bindings
    {
        /// <summary>
        /// 当前加载的模块句柄。
        /// </summary>
        private IntPtr module;
        /// <summary>
        /// 本次运行独占的临时资源目录。
        /// </summary>
        private readonly string cacheDirectory;
        /// <summary>
        /// 原生初始化入口。
        /// </summary>
        internal readonly InitializeEntry Initialize;
        /// <summary>
        /// 原生写入入口。
        /// </summary>
        internal readonly WriteEntry Write;
        /// <summary>
        /// 原生销毁入口。
        /// </summary>
        internal readonly DestroyEntry Destroy;

        /// <summary>
        /// 根据平台资源加载模块并绑定入口，失败时回收临时文件。
        /// </summary>
        internal Bindings()
        {
            module = NativeLibraryLoader.Load(out cacheDirectory);
            try
            {
                Initialize = Marshal.GetDelegateForFunctionPointer<InitializeEntry>(NativeLibraryLoader.GetExport(module, "LogConverge_Initialize"));
                Write = Marshal.GetDelegateForFunctionPointer<WriteEntry>(NativeLibraryLoader.GetExport(module, "LogConverge_Write"));
                Destroy = Marshal.GetDelegateForFunctionPointer<DestroyEntry>(NativeLibraryLoader.GetExport(module, "LogConverge_Destroy"));
            }
            catch
            {
                Release();
                throw;
            }
        }

        /// <summary>
        /// 仅在正常退出且没有在途调用时释放模块。
        /// </summary>
        internal void Release()
        {
            IntPtr value = Interlocked.Exchange(ref module, IntPtr.Zero); // 防止重复卸载
            if (value == IntPtr.Zero) return;
            NativeLibraryLoader.Free(value);
            NativeLibraryLoader.Cleanup(cacheDirectory);
        }
    }
}
