using System.Diagnostics;
using System.IO;
using System.Text;

namespace JundotCrashDialog;

public static class CrashLogWriter
{
    public static string WriteCrashLog(CrashInfo info)
    {
        try
        {
            var fileName = $"jundot-crash-{info.CrashTime:yyyyMMdd-HHmmss}.txt";
            var baseDir = !string.IsNullOrWhiteSpace(info.EngineDir)
                ? info.EngineDir
                : Path.GetTempPath();
            var dir = Path.Combine(baseDir, "logs");
            Directory.CreateDirectory(dir);
            var path = Path.Combine(dir, fileName);

            var sb = new StringBuilder();
            sb.AppendLine("========== Jundot Engine Crash Report ==========");
            sb.AppendLine($"时间     : {info.CrashTime:yyyy-MM-dd HH:mm:ss}");
            sb.AppendLine($"引擎     : {info.EngineExePath}");
            sb.AppendLine($"退出码   : {info.ExitCode}");
            sb.AppendLine($"运行时间 : {info.RunDurationSeconds:F2} 秒");
            if (!string.IsNullOrEmpty(info.EngineVersion))
                sb.AppendLine($"版本     : {info.EngineVersion}");
            sb.AppendLine($"日志文件 : {path}");
            sb.AppendLine();
            sb.AppendLine("========== 引擎 stderr 输出 ==========");
            sb.AppendLine(string.IsNullOrEmpty(info.Stderr) ? "(未捕获到 stderr 输出)" : info.Stderr);
            sb.AppendLine();

            if (info.CrashLogFiles.Count > 0)
            {
                sb.AppendLine("========== 检测到的 crashlog 文件 ==========");
                foreach (var f in info.CrashLogFiles)
                {
                    sb.AppendLine($"→ {f}");
                    try
                    {
                        sb.AppendLine($"--- {Path.GetFileName(f)} ---");
                        sb.AppendLine(ReadLogTail(f));
                        sb.AppendLine();
                    }
                    catch (Exception ex)
                    {
                        sb.AppendLine($"[无法读取: {ex.Message}]");
                    }
                }
            }

            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
            return path;
        }
        catch
        {
            return string.Empty;
        }
    }

    private static string ReadLogTail(string path)
    {
        const int maxChars = 120_000;

        var text = File.ReadAllText(path);
        if (text.Length <= maxChars)
            return text;

        return $"[Log truncated to last {maxChars} characters]\n" + text.Substring(text.Length - maxChars);
    }

    public static void OpenLogFile(string logPath)
    {
        if (!File.Exists(logPath)) return;
        try
        {
            Process.Start(new ProcessStartInfo(logPath) { UseShellExecute = true });
        }
        catch { }
    }
}
