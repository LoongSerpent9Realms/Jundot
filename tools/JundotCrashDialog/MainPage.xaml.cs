using Microsoft.Maui.Controls;
using System.Text;
using System.Text.Json;

namespace JundotCrashDialog;

public partial class MainPage : ContentPage
{
    public CrashInfo CrashInfo { get; private set; }
    public bool UserRequestedRestart { get; private set; }
    public bool UserRequestedRollback { get; private set; }

    public MainPage(CrashInfo info)
    {
        InitializeComponent();
        CrashInfo = info;
        UserRequestedRestart = false;
        UserRequestedRollback = false;
        PopulateContent();
    }

    private void PopulateContent()
    {
        var hasLog = !string.IsNullOrEmpty(CrashInfo.LogOutputPath);
        var hasStderr = !string.IsNullOrWhiteSpace(CrashInfo.Stderr);
        var hasCrashlog = CrashInfo.CrashLogFiles.Count > 0;

        // 根据运行时间给出不同的闪退提示
        var durationHint = CrashInfo.RunDurationSeconds switch
        {
            < 1.0 => "(启动后立即退出, 可能是 DLL 缺失 / 配置错误 / 闪退)",
            < 5.0 => $"(运行 {CrashInfo.RunDurationSeconds:F1} 秒后退出, 可能是初始化失败)",
            _ => $"(已运行 {CrashInfo.RunDurationSeconds:F1} 秒后异常退出)"
        };

        SummaryLabel.Text = $"引擎进程以退出码 {CrashInfo.ExitCode} 结束 {durationHint}。";

        // 崩溃摘要
        var summary = new StringBuilder();
        summary.AppendLine($"时间       : {CrashInfo.CrashTime:yyyy-MM-dd HH:mm:ss}");
        summary.AppendLine($"退出码     : {CrashInfo.ExitCode}");
        summary.AppendLine($"运行时间   : {CrashInfo.RunDurationSeconds:F2} 秒");
        summary.AppendLine($"引擎路径   : {CrashInfo.EngineExePath}");
        if (!string.IsNullOrEmpty(CrashInfo.EngineVersion))
            summary.AppendLine($"版本       : {CrashInfo.EngineVersion}");
        if (hasCrashlog)
            summary.AppendLine($"crashlog   : {CrashInfo.CrashLogFiles.Count} 个文件");
        summary.AppendLine($"日志已保存 : {(hasLog ? CrashInfo.LogOutputPath : "(写入失败)")}");
        CrashSummary.Text = summary.ToString();

        // stderr
        StderrOutput.Text = string.IsNullOrWhiteSpace(CrashInfo.Stderr)
            ? "(未捕获到 stderr 输出)"
            : Truncate(CrashInfo.Stderr, 6000);

        // crashlog 列表
        if (CrashInfo.CrashLogFiles.Count > 0)
        {
            var logList = new StringBuilder();
            foreach (var f in CrashInfo.CrashLogFiles)
                logList.AppendLine($"  • {f}");
            CrashLogList.Text = logList.ToString();
        }
        else
        {
            CrashLogList.Text = "(未检测到 crashlog 文件)";
        }

        // 底部提示
        FooterLabel.Text = string.IsNullOrEmpty(CrashInfo.LogOutputPath)
            ? "提示: 无法写入日志文件，请检查磁盘空间与权限。"
            : $"完整日志已写入: {CrashInfo.LogOutputPath}";

        var rollbackAvailable = HasRollbackBackup(CrashInfo.EngineDir);
        RollbackButton.IsEnabled = rollbackAvailable;
        RollbackButton.Opacity = rollbackAvailable ? 1.0 : 0.45;
        RollbackButton.Text = rollbackAvailable ? "回到上一版本" : "无可用备份";
    }

    private static string Truncate(string text, int maxChars)
    {
        if (string.IsNullOrEmpty(text) || text.Length <= maxChars) return text;
        return text.Substring(0, maxChars) + "\n...(已截断，完整内容见日志文件)";
    }

    private void OnViewLogClicked(object sender, EventArgs e)
    {
        CrashLogWriter.OpenLogFile(CrashInfo.LogOutputPath);
    }

    private void OnRestartClicked(object sender, EventArgs e)
    {
        UserRequestedRestart = true;
        UserRequestedRollback = false;
        Application.Current?.Quit();
    }

    private void OnRollbackClicked(object sender, EventArgs e)
    {
        UserRequestedRollback = true;
        UserRequestedRestart = false;
        Application.Current?.Quit();
    }

    private void OnCloseClicked(object sender, EventArgs e)
    {
        UserRequestedRestart = false;
        UserRequestedRollback = false;
        Application.Current?.Quit();
    }

    private static bool HasRollbackBackup(string engineDir)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(engineDir))
                return false;

            var statePath = Path.Combine(engineDir, ".jundot-update-state.json");
            if (!File.Exists(statePath))
                return false;

            using var doc = JsonDocument.Parse(File.ReadAllText(statePath));
            if (!doc.RootElement.TryGetProperty("backups", out var backups) || backups.ValueKind != JsonValueKind.Array)
                return false;

            foreach (var backup in backups.EnumerateArray())
            {
                if (!backup.TryGetProperty("backup_path", out var pathValue))
                    continue;

                var path = pathValue.GetString();
                if (!string.IsNullOrWhiteSpace(path) && Directory.Exists(path))
                    return true;
            }

            return false;
        }
        catch
        {
            return false;
        }
    }
}
