# LogConverge.Sharp

C# 日志 SDK，提供静态入口与独立实例。原生 spdlog 内核负责异步文件输出。目前随包分发 Windows x64 动态库。

项目源码：[GitHub · LogConverge](https://github.com/sunqg123/LogConverge)。

```csharp
using LogConverge.Sharp;

var result = Log.Initialize(Path.Combine(AppContext.BaseDirectory, "Logs"), true);
if (result == LogResult.Ok)
{
    Log.Debug("App", "开始加载");
    Log.Info("Network", "连接成功");
    Log.Warning("Playback", "缓冲不足");
    // Log.Error("Playback", "播放失败", exception);
}
```

Debug/发布版本是否记录由宿主传入的 bool 决定，SDK 不根据自身构建模式推断。

同一进程内的类库或组件可以持有自己的 `LogInstance`。

```csharp
var playbackLog = new LogInstance();
if (playbackLog.Initialize(Path.Combine("Logs", "Playback"), enabled: true) == LogResult.Ok)
{
    playbackLog.Info("Playback", "播放组件已启动");
}
```

各实例拥有自己的开关、目录和原生句柄。静态 `Log` 使用应用的默认实例。每个启用的实例应使用独占目录。组件提前卸载时可调用自己的 `Destroy()`，进程正常退出时 SDK 会统一清理仍在使用的实例。
未初始化或传 false 时不编码消息、不调用原生库、不创建目录或线程。
相对目录以 AppContext.BaseDirectory 为基准；也可传 D:/Logs 等绝对目录。
文件追加到 yyyyMMdd.txt，后台默认每秒刷新；正常退出时自动排空并释放，业务代码只需初始化和写日志。
退出处理只注册一次，所有实例共享 3 秒等待预算。强制结束、崩溃或断电无法保证执行退出回调。

Trace、Debug、Info、Warning、Error、Critical 和 Write 均返回 LogResult；日志故障不向业务抛异常。
Error/Critical 接受 Exception 并转换完整异常详情。Write 的 null 消息视为空文本；WriteLazy 接受 Func<string> 延迟构造，关闭时不执行消息工厂。
队列满载返回 Dropped，初始化失败和原生加载失败有对应结果码。Destroy 是可选的提前释放入口，超时保留实例可重试；手动释放后正常退出也可安全处理。
配置仅由 Initialize 指定，不提供可变 IsEnabled、状态查询、Flush 或 Shutdown。

托管程序集提供 netstandard2.1 和 net10.0 目标；实际运行验证为 Windows x64 + .NET 10。
netstandard2.1 程序集也已在 .NET 10 宿主运行验证，不代表 Unity、IL2CPP、AOT、裁剪、单文件发布或其他操作系统已通过验证。
本项目的 Release 原生库直接放在 3lib/<RID>/ 下，并作为资源嵌入 LogConverge.Sharp.dll；当前已包含 3lib/win-x64/LogConverge.dll。
正常 PackageReference 和 publish 不会在宿主目录复制原生 DLL 或生成 win-x64 文件夹，不包含 buildTransitive。消费者不需要复制 DLL、修改 PATH 或安装 C++ 编译器。
首次启用时，封装按进程平台和架构选择资源，在自身管理的独占临时目录释放并按绝对路径加载。正常退出、实例释放成功后卸载并清理；退出超时或异常终止可能留下临时缓存。
SDK 使用 MIT 许可证，许可证、第三方声明和原始许可证随包提供。
