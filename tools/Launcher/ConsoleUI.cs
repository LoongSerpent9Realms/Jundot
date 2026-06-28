namespace JundotLauncher;

/// <summary>
/// Minimal console UI with colored output, progress bar, and status updates.
/// Works on Windows (supports UTF-8 and ANSI escape codes via Windows Terminal / ConPTY).
/// </summary>
public static class ConsoleUI
{
    private static readonly object _lock = new();
    private static int _progressBarWidth = 40;

    static ConsoleUI()
    {
        try
        {
            Console.OutputEncoding = System.Text.Encoding.UTF8;
        }
        catch
        {
            // Fall back to system default encoding
        }
    }

    // ── Status Messages ──────────────────────────────────────

    public static void Header(string text)
    {
        lock (_lock)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine($"═══ {text} ═══");
            Console.ResetColor();
        }
    }

    public static void Info(string text)
    {
        lock (_lock)
        {
            Console.ForegroundColor = ConsoleColor.Gray;
            Console.WriteLine($"  {text}");
            Console.ResetColor();
        }
    }

    public static void Success(string text)
    {
        lock (_lock)
        {
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"  ✓ {text}");
            Console.ResetColor();
        }
    }

    public static void Warning(string text)
    {
        lock (_lock)
        {
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine($"  ⚠ {text}");
            Console.ResetColor();
        }
    }

    public static void Error(string text)
    {
        lock (_lock)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.Error.WriteLine($"  ✗ {text}");
            Console.ResetColor();
        }
    }

    /// <summary>Print a step header with separator.</summary>
    public static void Step(string text)
    {
        lock (_lock)
        {
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine($"  ▶ {text}");
            Console.ResetColor();
        }
    }

    /// <summary>Print current version info.</summary>
    public static void PrintVersion(string version, string channel)
    {
        lock (_lock)
        {
            Console.ForegroundColor = ConsoleColor.White;
            Console.Write("  当前版本: ");
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(version);
            Console.ForegroundColor = ConsoleColor.White;
            Console.Write("  更新通道: ");
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine(channel);
            Console.ResetColor();
        }
    }

    /// <summary>Print new version available notification.</summary>
    public static void PrintUpdateAvailable(string newVersion, long sizeBytes, string channel)
    {
        lock (_lock)
        {
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"  ★ 新版本可用: {newVersion} ({channel})");
            Console.ForegroundColor = ConsoleColor.Gray;
            Console.WriteLine($"    包大小: {FormatBytes(sizeBytes)}");
            Console.ResetColor();
        }
    }

    /// <summary>Print grayscale status.</summary>
    public static void PrintGrayscale(bool eligible, string reason)
    {
        lock (_lock)
        {
            if (eligible)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"  ✓ 灰度检查通过");
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine($"  - 灰度检查未通过: {reason}");
            }
            Console.ResetColor();
        }
    }

    // ── Progress Bar ─────────────────────────────────────────

    /// <summary>Draw/update a progress bar on the current line.</summary>
    public static void ShowProgress(string label, double percent, string? extraInfo = null)
    {
        lock (_lock)
        {
            var filled = (int)(percent / 100.0 * _progressBarWidth);
            var empty = _progressBarWidth - filled;

            var bar = new string('█', filled) + new string('░', empty);
            var pctStr = $"{percent,5:F1}%";

            var info = extraInfo != null ? $" {extraInfo}" : "";

            Console.Write($"\r  {label} [{bar}] {pctStr}{info}");
        }
    }

    /// <summary>Complete the progress bar with a newline.</summary>
    public static void CompleteProgress()
    {
        lock (_lock)
        {
            Console.WriteLine();
        }
    }

    // ── Interactive Prompts ──────────────────────────────────

    /// <summary>Ask user yes/no question. Returns true for yes.</summary>
    public static bool AskYesNo(string question, bool defaultYes = true)
    {
        lock (_lock)
        {
            var hint = defaultYes ? "[Y/n]" : "[y/N]";
            Console.Write($"  {question} {hint}: ");
            var key = Console.ReadKey(intercept: false);
            Console.WriteLine();

            if (key.Key == ConsoleKey.Y) return true;
            if (key.Key == ConsoleKey.N) return false;

            return defaultYes;
        }
    }

    // ── Helpers ──────────────────────────────────────────────

    public static string FormatBytes(long bytes)
    {
        string[] sizes = { "B", "KB", "MB", "GB" };
        double len = bytes;
        int order = 0;
        while (len >= 1024 && order < sizes.Length - 1)
        {
            order++;
            len /= 1024;
        }
        return $"{len:0.##} {sizes[order]}";
    }

    public static string FormatTimeSpan(TimeSpan ts)
    {
        if (ts.TotalHours >= 1)
            return $"{(int)ts.TotalHours}h {ts.Minutes}m";
        if (ts.TotalMinutes >= 1)
            return $"{ts.Minutes}m {ts.Seconds}s";
        return $"{ts.Seconds}s";
    }
}
