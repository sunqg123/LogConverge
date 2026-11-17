# SDK 使用约定

## 初始化只需要路径和开关

```cpp
#include <LogConverge/LogConverge.hpp>

LogConverge::Logger log;
const bool enabled = true; // Debug 通常启用，发布版本通常传 false
if (log.Initialize("D:/AppLogs/MyApplication", enabled) == LC_OK) {
    log.Write(LogConverge::Level::Info, "Network", "连接成功");
    log.Write(LogConverge::Level::Debug, "App", "调试信息");
    log.Write(LogConverge::Level::Error, "Playback", "播放失败", "HostException: detail");
    auto result = log.Destroy(3000);
}
```

C++ 接口接收 char 类型的 UTF-8 字节。公共头使用 UTF-8 BOM，纯 ASCII 调用方不需要增加编码选项；源码直接包含中文窄字符串时，MSVC 需启用 /utf-8 来保证执行字符集。C++20 的 u8 字符串为 char8_t，使用时应明确转换成 UTF-8 字节视图，不能用 path.string() 代替 UTF-8 转换。

开关表示本次运行是否记录日志，不只是是否写文件。false 时不创建输出目录、文件、异步队列或后台线程，Write 返回 Filtered。
true 时接收 Trace、Debug、Info/Information、Warning、Error、Critical 全部有效级别，按传入级别保存。
SDK 不根据自身 DLL 的 Debug/Release 构建判断宿主模式：宿主 Debug 配 Release DLL，只要传 true 也会记录。
C++ 示例在宿主 Debug 构建传 true，在宿主 Release 构建传 false；这只是示例的应用策略。
初始化后不提供运行时开关修改。修改策略需先 Destroy 再 Initialize。

## C ABI

公共头文件：`native/include/LogConverge/LogConverge.h`。仅三个导出：

```c
uint32_t LogConverge_Initialize(const LC_InitOptions* options, LC_Handle** handle);
uint32_t LogConverge_Write(LC_Handle* handle, const LC_Record* record);
uint32_t LogConverge_Destroy(LC_Handle** handle, uint32_t timeout_ms);
```

Initialize 接收 LC_InitOptions 结构体：struct_size 由封装填写，业务字段只有 directory 和 enabled；handle 是原生实例输出，由 C++/C# 封装管理。
C++ 外观使用 bool，C ABI 使用 uint32_t 的 0/1，避免不同语言的 bool 封送宽度差异，其他值返回 InvalidArgument。
启用时目录必须为绝对 UTF-8 路径，由宿主确定可写位置；关闭时允许空路径。
重复初始化非 NULL 句柄返回 AlreadyInitialized，保留原实例；初始化失败不发布新句柄。
句柄以指针宽度传递单调分配的标识，不代表可访问的对象地址，调用方不得解引用。本次 SDK 加载期间，销毁后的标识不会分配给新实例，旧句柄返回 Closed；对旧句柄调用 Destroy 也会将其置空。注册表仍以共享租约保护在途调用。
原生初始化使用精简结构体。LC_Record 保留 struct_size、level、category、message、exception，已去掉 correlation_id。旧消费者需更新绑定并重新编译。
应用名、队列、滚动等采用内置默认值；当前公开入口不支持自定义回调或高级配置。

Windows 调用约定为 cdecl，结构体采用默认本机对齐，不用 Pack=1。
所有函数返回 LC_Result；C++ 异常被转换为结果码，不跨越 C ABI。
初始化填写 struct_size=sizeof(LC_InitOptions)，记录填写 struct_size=sizeof(LC_Record)。不提供版本和状态查询。

## 内置行为

| 项目 | 默认行为 |
|---|---|
| 总开关 | 未初始化时关闭；初始化时显式传入 |
| 最低级别 | Trace，启用时记录全部有效级别 |
| 文件目录 | 初始化时由宿主提供 |
| 文件名称 | 记录本地日期的 yyyyMMdd.txt，追加写入 |
| 队列 | 准备及排队总容量 4096 条，不含正在写入的一条；满载返回 Dropped |
| 刷新 | 后台默认每秒刷新文件缓冲 |
| 单条上限 | 分类、消息、异常合计 1 MiB UTF-8 字节 |
| 大小滚动 | 10 MiB；单条记录不拆分 |
| 历史保留 | 尽量保留最多 10 个 SDK 日志文件，包含当前文件；占用导致无法清理时可暂时超出 |
| 应用标签 | Application |

日期切换由记录时间触发。超过大小限制后，归档为 yyyyMMdd.000001.txt 等，继续写当天基础文件。
初始化、跨日和大小滚动时按日期和序号清理，保留当前文件。
历史扫描、状态读取或删除失败时跳过清理，不停止后续日志。大小滚动的归档编号扫描失败时保留当前文件继续追加，间隔一秒重试，期间可能超过大小上限；不退回已有归档编号。当前日志文件的打开、写入或实际归档失败仍会反馈 I/O 故障。
仅 8 位日期.txt 和 8 位日期.数字.txt 形状的普通文件参与清理，不跟随符号链接；请给实例独占目录，不混放其他同名文件。
首版支持单进程多线程，不支持多个实例或进程同时操作同一目录。

## 文本和所有权

LC_Text 是 UTF-8 字节指针和长度，长度不含终止符；空文本可用 NULL。
Write 返回前，spdlog 队列已取得被接收记录的独立副本；随后可释放、修改或解除固定调用方内存。
文本必须是合法 UTF-8。消息允许 NUL、换行和特殊字符，路径不允许 NUL。
分类和异常分别传递；不会从 Event=、逗号或冒号推断字段。
异常由宿主转换成 UTF-8 详情，C# 封装使用 Exception.ToString() 保留完整异常信息。
文本为 UTF-8 无 BOM，记录含本地毫秒时间、级别、应用标签、分类和生产者线程 ID。
每条记录一行，换行保存为 \n，NUL 保存为 \x00，反斜杠和方括号也转义。

## 自动刷新和退出

日常只调用 Write。后台持续写入并定时刷新；不要逐条等待磁盘写入。
原生调用方退出时调用 Destroy，正常返回表示已停止接收、排空已接收记录、刷新关闭文件并回收后台线程。C# 封装会在进程正常退出时自动完成该操作。
超时返回 Timeout，保留句柄和仍在使用的资源；宿主稍后再次 Destroy，不能提前卸载 DLL。
C++ 外观析构会无限等待释放；有界退出应显式 Destroy 并处理超时。
只有一个宿主管理实例生命周期。Write 可使用句柄副本与 Destroy 并发；销毁完成后旧句柄失效。
文件缓冲刷新不代表 fsync 或断电持久性保证。

初始化 I/O 失败直接返回错误。后台致命 I/O 故障停止接收，后续 Write 返回 Closed，Destroy 返回保留的错误。
满载通过 Write 的 Dropped 返回值反馈，调用方可自行累计，不提供状态查询。

## 实现与验证边界

内核使用固定版本 spdlog 1.15.3，实际异步队列、记录副本和工作线程均由其提供；文件操作使用 spdlog file_helper，格式化使用随附 fmt。
SDK 适配按日名称、组合滚动、历史扫描及有超时的销毁；另一个控制线程负责定时刷新和安全回收，不另建日志队列。
校验、打包和 spdlog 投递在状态锁外完成；在锁内预留容量，销毁等待在途投递结束再回收线程池。C# 使用单个池化缓冲编码分类、正文和异常，返回前清空并归还；数组池可缓存容量，不承诺常驻内存必然下降。GetByteCount 仍用于严格校验和确定字节范围，不承诺包含业务字符串/异常构造的全链路零分配。
依赖以 header-only 编入 LogConverge.dll，不需要单独分发 spdlog.dll；许可证见 [第三方声明](native/THIRD_PARTY_NOTICES.md)。
Windows x64 Debug/Release、C# 公开入口及本地 NuGet 消费者的 build/run/publish 已验证。原生互操作封装在类库内部，测试项目通过正常引用调用类库。netstandard2.1 已验证编译及 .NET 10 宿主运行；Linux/macOS、真实午夜跨日、磁盘耗尽、长稳、引擎插件、单文件、AOT 和裁剪尚未验证。

## C# 封装与发布

命名空间为 LogConverge.Sharp，静态入口 Log 提供 Initialize、Trace、Debug、Info、Warning、Error、Critical、Write、Destroy。
LogInstance 提供相同的实例方法。各实例独立保存开关、目录和原生句柄，静态 Log 使用默认实例。每个启用的实例应使用独占目录。
Error/Critical 接受 Exception；Write 的 null 消息视为空文本。延迟消息使用独立的 WriteLazy(level, category, factory)，关闭时不调用消息工厂，避免 null 与字符串重载混淆。原先使用 Write(..., () => message) 的代码需改为 WriteLazy(..., () => message)。
所有调用返回 LogResult，日志失败不抛给业务；必须检查 Initialize 结果才能确认启用成功，写入满载返回 Dropped。
C# 相对路径以 AppContext.BaseDirectory 为基准；原生 ABI 仍要求宿主提供绝对目录。
传 false 时封装不调用原生初始化，因此关闭场景不依赖原生 DLL 加载；重复 Initialize 仍返回 AlreadyInitialized，Destroy 后可以重新初始化。
初始化阶段仅注册一次 ProcessExit 处理。正常退出时停止接收并排空所有已登记的实例，它们共享 3 秒等待预算，包含生命周期锁、在途调用和原生排空。退出开始后不允许重新初始化。
Destroy 是可选的提前释放入口，仍支持手动释放后重新初始化，超时保留实例可重试。托管封装保护在途调用，销毁开始后新写入返回 Closed，完成后返回 Filtered。
退出处理不抛出异常；强制结束、崩溃、断电或超时无法保证剩余日志完整写入。没有可变 IsEnabled 或 MinimumLevel 属性。
托管程序集 AnyCPU 便于复用，但启用时检查操作系统和进程架构；当前只有 Windows x64 原生库，其他组合返回 UnsupportedPlatform。
库缺失、格式不兼容或入口不匹配分别返回 NativeLibraryNotFound、NativeLibraryLoadFailed、NativeLibraryIncompatible。
源码原生资源统一存放在 3lib/<RID>/；构建时以 LogConverge.Native.<RID>.<文件名> 嵌入托管程序集。包内 lib/{tfm}/ 只分发托管程序集和 XML，另附第三方声明和许可证，不使用独立 runtimes 资产或 buildTransitive。
引用和发布输出不产生独立原生 DLL 或 win-x64 目录。首次启用时按进程平台和架构选择资源，释放到独占临时目录后按绝对路径加载、绑定原生入口。正常退出且实例释放成功后卸载模块并删除本次缓存；退出超时保留模块供在途调用完成，强制终止可能留下缓存。手动 Destroy 后保留模块以支持重新初始化。
