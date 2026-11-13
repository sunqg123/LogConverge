using LogConverge.Sharp;

/// <summary>
/// 演示日志类库的基本使用。
/// </summary>
internal static class Program
{
    /// <summary>
    /// 初始化并写入示例消息，退出时由封装库自动释放。
    /// </summary>
    private static int Main()
    {
        // 日志直接放在程序目录的 Logs 下，需要时修改这一行。
        string directory = Path.Combine(AppContext.BaseDirectory, "Logs");
        LogResult result = Log.Initialize(directory, enabled: true); // 本次运行启用日志
        if (result != LogResult.Ok)
        {
            Console.Error.WriteLine($"日志初始化失败：{result}");
            return 1;
        }

        Log.Trace("测试", "跟踪日志");
        Log.Debug("测试", "调试日志");
        Log.Info("测试", "程序启动成功");
        Log.Warning("测试", "警告日志");
        Log.Error("测试", "错误日志", new InvalidOperationException("测试异常"));
        Log.Critical("测试", "严重错误日志");

        Console.WriteLine($"日志目录：{directory}，退出时自动排空并释放。");
        return 0;
    }
}
