namespace JundotCrashDialog;

public class CrashInfo
{
    public int ExitCode { get; set; }
    public string Stderr { get; set; } = string.Empty;
    public string EngineExePath { get; set; } = string.Empty;
    public string EngineDir { get; set; } = string.Empty;
    public List<string> CrashLogFiles { get; set; } = new();
    public string? EngineVersion { get; set; }
    public DateTime CrashTime { get; set; } = DateTime.Now;
    public string LogOutputPath { get; set; } = string.Empty;
    public double RunDurationSeconds { get; set; }
}
