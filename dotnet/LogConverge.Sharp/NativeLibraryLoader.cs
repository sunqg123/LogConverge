using System;
using System.IO;
using System.Runtime.InteropServices;

namespace LogConverge.Sharp;

/// <summary>
/// 按平台从程序集释放原生资源，宿主目录不产生动态库。
/// </summary>
internal static class NativeLibraryLoader
{
    /// <summary>
    /// 当前运行平台的资源标识。
    /// </summary>
    private static readonly string RuntimeId = GetRuntimeId();
    /// <summary>
    /// 当前平台的原生库文件名称。
    /// </summary>
    private static readonly string FileName = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "LogConverge.dll" : RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "libLogConverge.dylib" : "libLogConverge.so";
    /// <summary>
    /// 当前平台对应的程序集资源名称。
    /// </summary>
    private static readonly string ResourceName = "LogConverge.Native." + RuntimeId + "." + FileName;
    /// <summary>
    /// 当前平台是否已包含对应资源。
    /// </summary>
    internal static bool IsSupported => RuntimeId.Length != 0 && typeof(NativeLibraryLoader).Assembly.GetManifestResourceInfo(ResourceName) != null;

    /// <summary>
    /// 获取平台和架构，缺少对应原生库时不伪装为支持。
    /// </summary>
    private static string GetRuntimeId()
    {
        string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "win" : RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ? "linux" : RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "osx" : "";
        string architecture = RuntimeInformation.ProcessArchitecture switch
        {
            Architecture.X64 => "x64",
            Architecture.X86 => "x86",
            Architecture.Arm64 => "arm64",
            Architecture.Arm => "arm",
            _ => ""
        };
        return platform.Length == 0 || architecture.Length == 0 ? "" : platform + "-" + architecture;
    }

    /// <summary>
    /// 创建独占缓存并从资源复制动态库，再按绝对路径加载。
    /// </summary>
    internal static IntPtr Load(out string cacheDirectory)
    {
        cacheDirectory = "";
        using Stream resource = typeof(NativeLibraryLoader).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new DllNotFoundException("未嵌入当前平台的原生资源：" + RuntimeId);
        string directory = Path.Combine(Path.GetTempPath(), "LogConverge", Guid.NewGuid().ToString("N")); // 独占目录，避免多进程覆盖
        try
        {
            Directory.CreateDirectory(directory);
            string path = Path.Combine(directory, FileName); // 始终使用绝对路径，不依赖 PATH 或宿主目录
            using (var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None)) resource.CopyTo(file);
            IntPtr module = LoadModule(path);
            cacheDirectory = directory;
            return module;
        }
        catch
        {
            Cleanup(directory);
            throw;
        }
    }

    /// <summary>
    /// 清理本次释放的文件，不触碰其他进程的缓存。
    /// </summary>
    internal static void Cleanup(string directory)
    {
        try
        {
            File.Delete(Path.Combine(directory, FileName));
            Directory.Delete(directory);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

#if NET10_0_OR_GREATER
    /// <summary>
    /// 使用运行时平台加载器打开原生库。
    /// </summary>
    private static IntPtr LoadModule(string path) => NativeLibrary.Load(path);
    /// <summary>
    /// 获取原生导出函数地址。
    /// </summary>
    internal static IntPtr GetExport(IntPtr module, string name) => NativeLibrary.GetExport(module, name);
    /// <summary>
    /// 释放原生模块引用。
    /// </summary>
    internal static void Free(IntPtr module) => NativeLibrary.Free(module);
#else
    /// <summary>
    /// 在 netstandard 目标下绑定宿主提供的 NativeLibrary API。
    /// </summary>
    private static class RuntimeApi
    {
        /// <summary>
        /// 宿主运行时的原生加载类型。
        /// </summary>
        private static readonly Type Type = typeof(Marshal).Assembly.GetType("System.Runtime.InteropServices.NativeLibrary")
            ?? System.Type.GetType("System.Runtime.InteropServices.NativeLibrary, System.Runtime.InteropServices")
            ?? throw new PlatformNotSupportedException("宿主未提供 NativeLibrary 加载能力。");
        /// <summary>
        /// 根据路径加载模块。
        /// </summary>
        internal static readonly Func<string, IntPtr> Load = (Func<string, IntPtr>)Delegate.CreateDelegate(typeof(Func<string, IntPtr>), Type.GetMethod("Load", new[] { typeof(string) })!);
        /// <summary>
        /// 读取导出地址。
        /// </summary>
        internal static readonly Func<IntPtr, string, IntPtr> Export = (Func<IntPtr, string, IntPtr>)Delegate.CreateDelegate(typeof(Func<IntPtr, string, IntPtr>),
            Type.GetMethod("GetExport", new[] { typeof(IntPtr), typeof(string) })!);
        /// <summary>
        /// 回收模块引用。
        /// </summary>
        internal static readonly Action<IntPtr> Release = (Action<IntPtr>)Delegate.CreateDelegate(typeof(Action<IntPtr>), Type.GetMethod("Free", new[] { typeof(IntPtr) })!);
    }
    /// <summary>
    /// 使用宿主平台加载器打开模块。
    /// </summary>
    private static IntPtr LoadModule(string path) => RuntimeApi.Load(path);
    /// <summary>
    /// 获取导出函数地址。
    /// </summary>
    internal static IntPtr GetExport(IntPtr module, string name) => RuntimeApi.Export(module, name);
    /// <summary>
    /// 释放模块引用。
    /// </summary>
    internal static void Free(IntPtr module) => RuntimeApi.Release(module);
#endif
}
