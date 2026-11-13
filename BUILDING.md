# 构建与安装

## Windows x64

需要 CMake 3.21 或更新版本、Ninja、MSVC C++ 工具链及 Windows SDK。
首次配置会获取固定版本 spdlog 1.15.3 并验证 SHA-256；可使用 `native/build/dependency-cache/spdlog-1.15.3.tar.gz` 本地缓存，或通过 CMake 的 `FETCHCONTENT_SOURCE_DIR_SPDLOG` 指定离线源码。
spdlog/fmt 编入 LogConverge.dll，消费者不需要安装它们。安装目录同时包含第三方声明和许可证。

在 Visual Studio / Build Tools 提供的 **x64 Native Tools Command Prompt** 中运行。它会设置编译器及 Windows SDK 环境，普通 CMD 若没有初始化该环境，将找不到 `cl`。

```cmd
cd /d D:\GithubServer\LogConverge\native
cmake --preset windows-debug
cmake --build --preset windows-debug
```

以下命令均在 `native/` 目录执行。Release 构建：

```cmd
cmake --preset windows-release
cmake --build --preset windows-release
```

库和公共示例兼容 C++17、C++20。C++20 可在独立构建目录验证，保留已有构建配置：

```cmd
cmake --preset windows-debug -B build/windows-cpp20 -DCMAKE_CXX_STANDARD=20 -DLOGCONVERGE_BUILD_EXAMPLES=ON
cmake --build build/windows-cpp20
```

预设默认生成动态库。Release DLL 直接输出到 `dotnet/LogConverge.Sharp/3lib/win-x64/LogConverge.dll`；Debug DLL 留在 `native/build/windows-debug/bin/Debug/`。中间文件、符号、导入库和 C++ 示例保留在对应的 native/build 构建目录。

## VS Code

安装 C/C++、CMake Tools 扩展。在上述开发者命令提示符中进入 `native/` 后运行 `code .`，选择 `windows-debug` 或 `windows-release` 预设。

若打开整个仓库，在 CMake Tools 设置中将 `cmake.sourceDirectory` 指向 `${workspaceFolder}/native`。

## Visual Studio 2026（C#）

打开 `dotnet/LogConverge.Sharp.sln`，需要安装 .NET 10 SDK 及对应的 .NET 开发组件。

- `LogConverge.Sharp`：需要开发和发布 NuGet 的 C# 类库，内部通过 P/Invoke 调用原生 SDK。
- `LogConverge.Sharp.Tests`：简单的控制台使用示例，Program.cs 只初始化和写入各级别日志，正常退出时自动释放。

先在 VS Code 中构建 C++ Release 库，使 3lib/win-x64/LogConverge.dll 就绪。然后在 VS 中选择 `LogConverge.Sharp.Tests` 为启动项目，按 F5 直接运行，无需填写 DLL 路径。
测试通过 ProjectReference 获取编译后的 LogConverge.Sharp.dll，类库构建将 3lib/<RID>/ 下的原生库嵌入程序集资源。测试代码只使用 Log.Initialize、Log.Info、Log.Error 等公开入口，不编写 P/Invoke 或手动加载动态库。
运行目录只部署托管程序集，不复制独立原生 DLL 或生成 win-x64 文件夹；NuGet 包消费及 publish 使用相同方式。首次启用日志时，封装自行释放资源到独占临时目录并加载，正常退出且实例释放成功后卸载并清理。
C# 解决方案只构建托管项目，原生 SDK 继续使用 VS Code + CMake。仅编译 C# 类库不需要先生成原生 DLL，运行验收和打包时需要。

命令行也可构建整个 C# 解决方案：

```cmd
dotnet build dotnet/LogConverge.Sharp.sln -c Debug
```

## 安装 SDK

以 Release 为例，将原生库、头文件和 CMake 包配置安装到本地目录：

```cmd
cmake --install build/windows-release --prefix prefix
```

C++ 消费者通过安装目录使用：

```cmake
find_package(LogConverge CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE LogConverge::LogConverge)
```

配置消费者时用 `-DCMAKE_PREFIX_PATH=<安装目录绝对路径>` 指定位置。

## 示例运行

在仓库根目录、已初始化 x64 MSVC 环境的命令行中运行（C# 示例需要 .NET 10 SDK）：

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-native.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-native.ps1 -Configuration Release
```

脚本构建 DLL 和示例、执行示例、验证安装，并运行 C# 基本使用示例。它通过 NativeLibraryPath 构建属性选择嵌入 Debug 或 Release 原生资源；示例不接触原生绑定。脚本不生成 NuGet 包。

也可在仓库根目录只运行 C# 示例：

```cmd
dotnet run --project dotnet/LogConverge.Sharp.Tests
```

手动构建示例时，从 `native/` 执行：

```cmd
cmake --preset windows-release -DLOGCONVERGE_BUILD_EXAMPLES=ON
cmake --build --preset windows-release
build\windows-release\LogConvergeExample.exe D:\AppLogs\Example
```

Windows 原生库使用静态 MSVC 运行库；本机 Release DLL 依赖检查仅发现系统 `KERNEL32.dll`，不需要额外复制 MSVC C++ 运行时 DLL。未来其他平台仍需单独检查依赖。

## 其他构建方式

核心 CMake 文件保留跨平台构建能力，平台预设和实际验证后续补充。使用本机工具链时，可从 `native/` 目录直接配置：

```sh
cmake -S . -B build/local -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/local --config Release
```

仅 C++ 消费者需要原生开发工具链，NuGet 消费者使用随包预编译动态库。
Release 动态库直接输出到 `dotnet/LogConverge.Sharp/3lib/<RID>/`，例如 win-x64、linux-x64、osx-arm64，其下不再增加 native 或配置层，也不放 .gitkeep。其他配置留在构建目录。Linux 和 macOS 当前仅预留目录，需使用对应工具链实际构建；musl 等变体工具链需通过 `-DLOGCONVERGE_RUNTIME_ID=linux-musl-x64` 指定 RID。预留目录由 C# 项目的 Folder 项维护，VS 中可见。

C++ 示例由宿主构建模式选择开关：Debug 记录，Release 不记录。C# 示例显式启用日志，便于直接查看输出。

C# 示例只初始化一次、写入各级别日志和异常，由封装在正常退出时自动释放，不需要手动 Destroy。路径固定为 `Path.Combine(AppContext.BaseDirectory, "Logs")`，Debug 构建直接写到 `dotnet/LogConverge.Sharp.Tests/bin/Debug/net10.0/Logs/yyyyMMdd.txt`，当天重复运行追加到同一文件。需要修改目录时，只改 Program.cs 的 directory 变量。

## NuGet 打包与独立消费者

在 x64 MSVC 开发者命令行、仓库根目录执行：

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\pack.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-package.ps1
```

pack 脚本生成 artifacts/packages/LogConverge.Sharp.0.7.0.nupkg，包含已嵌入原生资源的 AnyCPU 托管程序集、XML 文档和第三方声明/许可证。
构建封装使用 .NET 10 SDK，同时生成 netstandard2.1 和 net10.0 两个目标。原生库固定 Release，托管消费者无需 C++ 编译器或源码。
打包读取 C# 类库项目内的 3lib/<RID>/ 动态库并嵌入程序集，不再包含 runtimes 原生资产或 buildTransitive 配置。当前 3lib/win-x64 已有匹配的 Release DLL 时可使用 pack.ps1 -SkipNativeBuild。打包不会自动发布到公共源。

verify-package 会在 artifacts/consumer-* 中临时生成仅含 PackageReference 的消费者，不维护单独的示例项目。它使用复制的 nupkg、清空其他源的 NuGet.Config 和独立包缓存验证 restore/build/run、关闭开关、win-x64 publish 后运行及 netstandard2.1 消费者编译。
消费者正常退出后由父进程检查完整日志数量，同时验证手动提前释放、重复初始化、释放后重新初始化及退出阶段禁止重建实例；检查构建和发布输出没有独立原生 DLL，并核对正常退出后的临时资源清理。
所有验收产物保留在 artifacts/consumer-*，不要求用户复制 DLL 或修改 PATH。
