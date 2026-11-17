# LogConverge

C++ / spdlog 日志内核，配套 C# 静态入口、独立日志实例与 NuGet 包。当前验证 Windows x64 + .NET 10。

## C# 使用

引用 `LogConverge.Sharp`，业务代码不需要编写 P/Invoke 或复制原生 DLL：

```csharp
using LogConverge.Sharp;

var result = Log.Initialize(Path.Combine(AppContext.BaseDirectory, "Logs"), enabled: true);
if (result == LogResult.Ok)
{
    Log.Debug("App", "开始加载配置");
    Log.Info("Network", "连接成功");
    Log.Warning("Playback", "缓冲不足");
    Log.WriteLazy(LogLevel.Debug, "App", () => "需要写入时再构造消息");
    // Log.Error("Playback", "播放失败", exception);
}
```

初始化只接收目录和本次运行开关。Debug 通常传 true，发布版本通常传 false；SDK 不自行判断宿主构建模式。
关闭时 C# 在编码和跨语言调用前直接返回，不创建日志输出资源。
支持 Trace、Debug、Info、Warning、Error、Critical；异常通过 Exception.ToString() 保留完整详情。
日志追加到 yyyyMMdd.txt，后台每秒刷新。软件正常退出时由 C# 封装自动排空并释放，业务代码无需调用 Destroy。
自动退出清理最多等待 3 秒；Destroy 保留为可选的提前释放入口。没有 Flush、Shutdown 或独立状态查询。
各方法返回 LogResult；初始化失败、满载丢弃和 I/O 故障可通过返回值观察，不向业务抛出日志异常。
Write 的 null 消息按空文本写入；延迟消息使用 WriteLazy，关闭时不执行消息工厂。

同一进程里的类库可以创建并持有自己的 `LogInstance`，使用实例的 `Initialize` 和 `Info` 等方法。每个实例单独设置开关、目录和生命周期。给各实例分配不同目录，避免同时滚动同一个文件。

本地包：`artifacts/packages/LogConverge.Sharp.0.7.1.nupkg`。
从仓库根目录执行下面命令，将 `path\to\YourApp.csproj` 替换为业务项目路径：

```cmd
dotnet add path\to\YourApp.csproj package LogConverge.Sharp --version 0.7.1 --source .\artifacts\packages
```

托管程序集为 AnyCPU，目标为 netstandard2.1、net10.0；原生库作为资源嵌入 LogConverge.Sharp.dll。
引用和发布后，程序目录不产生独立的原生 DLL 或 win-x64 文件夹，也不需要 buildTransitive。首次启用时，封装按平台释放到私有临时目录并加载；正常退出、实例释放成功后卸载并清理缓存。
两个目标程序集均已在 Windows x64 + .NET 10 宿主运行验证；Unity、Godot、AOT、单文件、裁剪及其他平台尚未验证。

## 源码目录

- `dotnet/LogConverge.Sharp.sln`：使用 Visual Studio 2026 打开，包含 C# 类库和使用示例。
- `native/`：C++ 内核、C ABI、C++ 外观和示例。
- `dotnet/LogConverge.Sharp/3lib/`：按 RID 直接存放 Release 动态库，如 win-x64/LogConverge.dll；当前已构建 Windows x64。
- `dotnet/LogConverge.Sharp/`：C# 静态入口、内部封送及 NuGet 打包。
- `dotnet/LogConverge.Sharp.Tests/`：简单使用示例，F5 直接运行，日志写入程序目录下的 Logs/yyyyMMdd.txt。
- `artifacts/`：NuGet 包及包验收产物，不存放 SDK 源码。
- `scripts/pack.ps1`：构建原生库并打包。
- `scripts/verify-native.ps1`：原生构建、安装和行为验收。
- `scripts/verify-package.ps1`：临时生成消费者，通过独立缓存、本地包源验证 build/run/publish。

构建步骤见 [BUILDING.md](BUILDING.md)，接口约定见 [NATIVE_API.md](NATIVE_API.md)，验证范围见 [VALIDATION.md](VALIDATION.md)。

## 许可证

SDK 使用 [MIT 许可证](LICENSE)。包内包含许可证、标签和更新说明；NuGet 仓库和项目链接指向 [GitHub 仓库](https://github.com/sunqg123/LogConverge)。
第三方原始许可证随包提供，见 [第三方声明](native/THIRD_PARTY_NOTICES.md)。
