namespace JundotLauncher;

/// <summary>
/// JundotLauncher — Online Hot-Update System Entry Point
/// 
/// Usage:
///   JundotLauncher.exe start [--engine-path <dir>] [--channel <stable|beta|dev>]
///   JundotLauncher.exe check-only [--engine-path <dir>] [--channel <stable|beta|dev>]
///   JundotLauncher.exe update [--engine-path <dir>] [--manifest-url <url>]
///   JundotLauncher.exe rollback [--engine-path <dir>]
///   JundotLauncher.exe --version
///   JundotLauncher.exe --help
/// 
/// Exit codes:
///   0 — Success (no update needed, or operation completed)
///   1 — Error
///   2 — Update available but not applied (user skipped or check-only mode)
/// </summary>
public class Program
{
    // ── CLI Arguments ────────────────────────────────────────
    private enum Command { Start, CheckOnly, Update, Rollback, Version, Help }

    private static Command _command = Command.Start;
    private static string _enginePath = "";
    private static string _channel = "stable";
    private static string _manifestUrl = "";

    // Unused-param hint for future stages
    // ReSharper disable UnusedAutoPropertyAccessor.Local
    // ReSharper disable UnassignedGetOnlyAutoProperty

    public static async Task<int> Main(string[] args)
    {
        if (!ParseArgs(args))
            return 1;

        try
        {
            // ── Resolve engine path ───────────────────────────
            if (string.IsNullOrEmpty(_enginePath))
            {
                _enginePath = DetectEnginePath();
            }

            if (!Directory.Exists(_enginePath))
            {
                ConsoleUI.Error($"引擎目录不存在: {_enginePath}");
                ConsoleUI.Info("使用 --engine-path <dir> 指定路径，或将启动器放在引擎目录下。");
                return 1;
            }

            // ── Initialize state store ────────────────────────
            var state = new UpdateStateStore(_enginePath);
            if (!string.IsNullOrEmpty(_channel))
                state.UpdateChannel = _channel;

            // ── Dispatch command ──────────────────────────────
            switch (_command)
            {
                case Command.Version:
                    PrintVersionInfo(state);
                    return 0;

                case Command.Help:
                    PrintHelp();
                    return 0;

                case Command.CheckOnly:
                    return await RunCheckOnly(state);

                case Command.Rollback:
                    return await RunRollback(state);

                case Command.Update:
                    return await RunUpdate(state);

                case Command.Start:
                default:
                    return await RunStart(state);
            }
        }
        catch (Exception ex)
        {
            ConsoleUI.Error($"未处理的异常: {ex.Message}");
            ConsoleUI.Info(ex.StackTrace ?? "");
            return 1;
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  COMMAND HANDLERS (stubs for now, implemented in Step 8)
    // ═══════════════════════════════════════════════════════════

    private static async Task<int> RunStart(UpdateStateStore state)
    {
        ConsoleUI.Header("Jundot 启动器");
        ConsoleUI.PrintVersion(state.CurrentVersion, state.UpdateChannel);

        var orchestrator = new UpdateOrchestrator(_enginePath, state);
        return await orchestrator.StartAsync(_manifestUrl);
    }

    private static async Task<int> RunCheckOnly(UpdateStateStore state)
    {
        ConsoleUI.Header("Jundot 更新检查 (仅检查)");
        ConsoleUI.PrintVersion(state.CurrentVersion, state.UpdateChannel);

        var orchestrator = new UpdateOrchestrator(_enginePath, state);
        var manifest = await orchestrator.CheckOnlyAsync(_manifestUrl);

        if (manifest != null)
        {
            return 2; // Update available
        }
        return 0;
    }

    private static async Task<int> RunUpdate(UpdateStateStore state)
    {
        ConsoleUI.Header("Jundot 更新");

        var orchestrator = new UpdateOrchestrator(_enginePath, state);
        var success = await orchestrator.UpdateAsync(_manifestUrl);

        return success ? 0 : 1;
    }

    private static async Task<int> RunRollback(UpdateStateStore state)
    {
        ConsoleUI.Header("Jundot 回滚");

        var orchestrator = new UpdateOrchestrator(_enginePath, state);
        var success = await orchestrator.RollbackAsync();

        return success ? 0 : 1;
    }

    // ═══════════════════════════════════════════════════════════
    //  HELPERS
    // ═══════════════════════════════════════════════════════════

    /// <summary>Detect engine path from the launcher's own location.</summary>
    private static string DetectEnginePath()
    {
        // Default: launcher sits next to the engine executable
        var launcherDir = AppContext.BaseDirectory;

        // Walk up looking for a known engine file
        var dir = launcherDir;
        for (int i = 0; i < 3; i++)
        {
            var exes = Directory.GetFiles(dir, "jundot*.exe");
            if (exes.Any())
                return dir;

            var parent = Path.GetDirectoryName(dir);
            if (parent == null || parent == dir) break;
            dir = parent;
        }

        return launcherDir;
    }

    /// <summary>Launch the Jundot engine executable. Public for orchestrator use.</summary>
    public static async Task<int> LaunchEngineStatic(string engineDir)
    {
        // Find the main engine exe (prefer non-console for editor)
        var exes = Directory.GetFiles(engineDir, "jundot.*.editor.*.exe")
            .Where(f => !f.EndsWith(".console.exe"))
            .ToList();

        if (exes.Count == 0)
        {
            // Fallback: try console version
            exes = Directory.GetFiles(engineDir, "jundot.*.editor.*.exe").ToList();
        }

        if (exes.Count == 0)
        {
            ConsoleUI.Error("找不到 Jundot 引擎可执行文件。");
            ConsoleUI.Info($"搜索路径: {engineDir}");
            return 1;
        }

        var exePath = exes.OrderByDescending(f => new FileInfo(f).Length).First();
        ConsoleUI.Success($"启动引擎: {Path.GetFileName(exePath)}");

        try
        {
            var psi = new System.Diagnostics.ProcessStartInfo(exePath)
            {
                WorkingDirectory = engineDir,
                UseShellExecute = true
            };
            var process = System.Diagnostics.Process.Start(psi);
            if (process != null)
            {
                await process.WaitForExitAsync();
                return process.ExitCode;
            }
        }
        catch (Exception ex)
        {
            ConsoleUI.Error($"启动引擎失败: {ex.Message}");
        }

        return 1;
    }

    /// <summary>Print version and build info.</summary>
    private static void PrintVersionInfo(UpdateStateStore state)
    {
        Console.WriteLine($"JundotLauncher v1.0.0");
        Console.WriteLine($"协议版本: manifest-v1.0");
        Console.WriteLine($"引擎路径: {_enginePath}");
        Console.WriteLine($"当前版本: {state.CurrentVersion}");
        Console.WriteLine($"更新通道: {state.UpdateChannel}");
        Console.WriteLine($"机器标识: {state.MachineId}");
    }

    /// <summary>Print CLI help.</summary>
    private static void PrintHelp()
    {
        Console.WriteLine(@"
JundotLauncher — Jundot Engine 线上热更新启动器

用法:
  JundotLauncher.exe start      启动引擎（默认：检查更新→启动）
  JundotLauncher.exe check-only 仅检查更新（不下载）
  JundotLauncher.exe update     执行完整更新流程
  JundotLauncher.exe rollback   回滚到上一个版本
  JundotLauncher.exe --version  显示版本信息
  JundotLauncher.exe --help     显示此帮助

选项:
  --engine-path <dir>   引擎安装目录（默认：启动器所在目录）
  --channel <channel>    更新通道 (stable | beta | dev，默认：stable)
  --manifest-url <url>  直接指定 manifest URL（用于测试）

退出码:
  0  成功（无需更新或操作完成）
  1  错误
  2  有可用更新但未应用
");
    }

    // ═══════════════════════════════════════════════════════════
    //  ARGUMENT PARSING
    // ═══════════════════════════════════════════════════════════

    private static bool ParseArgs(string[] args)
    {
        var positional = new List<string>();

        for (int i = 0; i < args.Length; i++)
        {
            var arg = args[i];

            // Handle --engine-path value
            if (arg == "--engine-path" && i + 1 < args.Length)
            {
                _enginePath = args[++i];
                continue;
            }

            // Handle --channel value
            if (arg == "--channel" && i + 1 < args.Length)
            {
                _channel = args[++i];
                continue;
            }

            // Handle --manifest-url value
            if (arg == "--manifest-url" && i + 1 < args.Length)
            {
                _manifestUrl = args[++i];
                continue;
            }

            // Handle bare flags
            switch (arg)
            {
                case "--version":
                case "-v":
                    _command = Command.Version;
                    break;
                case "--help":
                case "-h":
                    _command = Command.Help;
                    break;
                default:
                    positional.Add(arg);
                    break;
            }
        }

        // Resolve command from positional args
        if (positional.Count > 0)
        {
            _command = positional[0].ToLowerInvariant() switch
            {
                "start" => Command.Start,
                "check-only" => Command.CheckOnly,
                "update" => Command.Update,
                "rollback" => Command.Rollback,
                "version" => Command.Version,
                "help" => Command.Help,
                _ => Command.Start
            };
        }

        return true;
    }
}
