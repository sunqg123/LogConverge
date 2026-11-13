# 第三方组件

LogConverge 0.7 使用以下组件，不向公共 C ABI 暴露其类型：

| 组件 | 版本 | 许可证 | 用途 |
|---|---|---|---|
| spdlog | 1.15.3 | MIT | 异步队列、工作线程、sink 和文件操作 |
| fmt | spdlog 1.15.3 附带版本 | MIT | 日志格式化 |

来源：[spdlog v1.15.3](https://github.com/gabime/spdlog/tree/v1.15.3)。
版本和源码压缩包 SHA-256 固定在 `CMakeLists.txt`，依赖以 header-only 方式编入 LogConverge 原生库。

原始许可证完整保存在 `licenses/spdlog.LICENSE`、`licenses/fmt.LICENSE`，随 SDK 安装。
NuGet 包已包含本声明和上述许可证。
