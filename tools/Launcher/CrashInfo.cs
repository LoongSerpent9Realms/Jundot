namespace JundotLauncher;

/// <summary>
/// 崩溃报告数据结构：包含退出码、捕获的 stderr、crashlog 路径、引擎版本等。
/// </summary>
public class CrashInfo
{
    public int ExitCode { get; set; }

    /// <summary>从引擎 stderr 捕获的原始文本。</summary>
    public string Stderr { get; set; } = string.Empty;

    /// <summary>引擎可执行文件路径。</summary>
    public string EngineExePath { get; set; } = string.Empty;

    /// <summary>引擎所在目录。</summary>
    public string EngineDir { get; set; } = string.Empty;

    /// <summary>检测到的 crashlog 文件路径列表。</summary>
    public List<string> CrashLogFiles { get; set; } = new();

    /// <summary>当前引擎版本（若能从 update state 获取）。</summary>
    public string? EngineVersion { get; set; }

    /// <summary>崩溃时间。</summary>
    public DateTime CrashTime { get; set; } = DateTime.Now;

    /// <summary>引擎运行持续时长 (秒), 0 表示未启动成功。</summary>
    public double RunDurationSeconds { get; set; }
}
