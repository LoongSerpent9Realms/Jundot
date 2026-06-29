using System.Diagnostics;

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
	private enum CrashDialogAction { Close, Restart, Rollback }

	// ── CLI Arguments ────────────────────────────────────────
	private enum Command { Start, CheckOnly, Update, Rollback, Version, Help }

	private static Command _command = Command.Start;
	private static string _enginePath = "";
	private static string _channel = "stable";
	private static string _manifestUrl = "";
	internal static bool AssumeYes { get; private set; }

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
			if (string.IsNullOrWhiteSpace(state.CurrentVersion) || state.CurrentVersion == "0.0.0")
			{
				var detectedVersion = TryReadEngineVersion(_enginePath);
				if (!string.IsNullOrWhiteSpace(detectedVersion))
				{
					state.CurrentVersion = detectedVersion;
				}
			}

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

		int finalExitCode = 1;
		bool userWantsRestart;
		bool rollbackRestartRequested = false;

		do
		{
			userWantsRestart = false;
			if (rollbackRestartRequested)
			{
				rollbackRestartRequested = false;
				exes = Directory.GetFiles(engineDir, "jundot.*.editor.*.exe")
					.Where(f => !f.EndsWith(".console.exe"))
					.ToList();
				if (exes.Count == 0)
				{
					exes = Directory.GetFiles(engineDir, "jundot.*.editor.*.exe").ToList();
				}
				if (exes.Count == 0)
				{
					ConsoleUI.Error("回滚后找不到 Jundot 引擎可执行文件。");
					return 1;
				}
				exePath = exes.OrderByDescending(f => new FileInfo(f).Length).First();
				ConsoleUI.Success($"回滚后启动引擎: {Path.GetFileName(exePath)}");
			}

			// ── 准备 stderr 日志文件路径（用 cmd /c 包装以让 GUI 引擎的 stderr 落地） ──
			var stderrLogPath = Path.Combine(engineDir, "logs", $"engine-stderr-{DateTime.Now:yyyyMMdd-HHmmss}.log");
			try { Directory.CreateDirectory(Path.GetDirectoryName(stderrLogPath)!); } catch { }

			// 哨兵文件: 引擎正常运行时会创建并退出时清理。闪退时不会创建。
			var sentinelPath = Path.Combine(engineDir, ".engine-running");

			// 清理旧哨兵
			try { if (File.Exists(sentinelPath)) File.Delete(sentinelPath); } catch { }

			var launchedAt = DateTime.Now;
			Process? process = null;

			try
			{
				// 直接启动引擎（不使用任何包装，让 GUI 窗口正常显示）
				// 崩溃检测依赖: ExitCode + 运行时间 + crashlog 文件
				var psi = new System.Diagnostics.ProcessStartInfo(exePath)
				{
					WorkingDirectory = engineDir,
					UseShellExecute = true,      // 关键: 让 GUI 进程正常显示窗口
					CreateNoWindow = false,
					WindowStyle = System.Diagnostics.ProcessWindowStyle.Normal
				};

				process = System.Diagnostics.Process.Start(psi);
				ConsoleUI.Info($"[引擎监控] 引擎已启动: {Path.GetFileName(exePath)}");
			}
			catch (Exception ex)
			{
				ConsoleUI.Error($"启动引擎失败: {ex.Message}");
				// 即使启动失败也弹出崩溃对话框
				var action = ShowCrashDialog(BuildCrashInfo(exePath, engineDir, 1, "", new List<string>(), 0));
				if (action == CrashDialogAction.Rollback)
				{
					var rollbackOk = await RollbackAfterCrashAsync(engineDir);
					if (rollbackOk)
					{
						userWantsRestart = true;
						rollbackRestartRequested = true;
						continue;
					}
				}
				break;  // 启动失败不进入重启循环
			}

			// ── 等待进程退出 ──
			var waitStart = DateTime.Now;
			try
			{
				if (process != null)
				{
					await process.WaitForExitAsync();
					var waitDuration = (DateTime.Now - waitStart).TotalSeconds;
					ConsoleUI.Info($"[引擎监控] WaitForExit 完成，ExitCode={process.ExitCode}，等待耗时={waitDuration:F1}s");
				}
			}
			catch (Exception ex)
			{
				ConsoleUI.Error($"等待引擎退出时出错: {ex.Message}");
			}

			// ── 等待 stderr 日志写入完成 ──
			await Task.Delay(800);

			var runDuration = (DateTime.Now - launchedAt).TotalSeconds;
			finalExitCode = process?.ExitCode ?? 1;
			ConsoleUI.Info($"[引擎监控] runDuration={runDuration:F1}s, ExitCode={finalExitCode}");

			// ── 判断是否为"正常退出" ──
			// 简化: 任何非零 ExitCode 或运行时间过短，都视为崩溃
			bool isCrash = finalExitCode != 0 || runDuration < 3.0;
			ConsoleUI.Info($"[引擎监控] isCrash={isCrash} (ExitCode={finalExitCode}, sentinelExists={File.Exists(sentinelPath)})");

			if (!isCrash)
			{
				ConsoleUI.Success("引擎已正常退出。");
				return 0;
			}

			// ── 读取 crashlog（引擎崩溃时会在多个位置生成日志） ──
			var crashLogs = ScanCrashLogs(engineDir);
			ConsoleUI.Info($"[引擎监控] 检测到 {crashLogs.Count} 个 crashlog 文件");

			// ── 读取 stderr（如果存在的话） ──
			string capturedStderr = ReadStderrLog(stderrLogPath);
			if (!string.IsNullOrEmpty(capturedStderr))
				ConsoleUI.Info($"[引擎监控] stderr 日志已捕获 ({capturedStderr.Length} 字符)");

			// ── 清理哨兵 ──
			try { if (File.Exists(sentinelPath)) File.Delete(sentinelPath); } catch { }

			ConsoleUI.Error($"引擎{(finalExitCode != 0 ? $"以退出码 {finalExitCode}" : "异常")} 结束 (运行 {runDuration:F1} 秒)。正在准备崩溃报告...");

			var crashInfo = BuildCrashInfo(exePath, engineDir, finalExitCode, capturedStderr, crashLogs, runDuration);
			var crashAction = ShowCrashDialog(crashInfo);
			if (crashAction == CrashDialogAction.Restart)
			{
				userWantsRestart = true;
			}
			else if (crashAction == CrashDialogAction.Rollback)
			{
				var rollbackOk = await RollbackAfterCrashAsync(engineDir);
				userWantsRestart = rollbackOk;
				rollbackRestartRequested = rollbackOk;
			}
		}
		while (userWantsRestart);

		return finalExitCode;
	}

	private static string ReadStderrLog(string path)
	{
		try
		{
			if (!File.Exists(path)) return string.Empty;
			// 重试几次, 防止 cmd 重定向还在 flush
			for (int i = 0; i < 3; i++)
			{
				try
				{
					var content = File.ReadAllText(path);
					if (!string.IsNullOrEmpty(content) || i == 2) return content;
				}
				catch (IOException)
				{
					Thread.Sleep(200);
				}
			}
			return string.Empty;
		}
		catch
		{
			return string.Empty;
		}
	}

	private static async Task<bool> RollbackAfterCrashAsync(string engineDir)
	{
		ConsoleUI.Header("引擎闪退回滚");
		try
		{
			var state = new UpdateStateStore(engineDir);
			var latestBackup = state.GetLatestBackup();
			if (latestBackup == null)
			{
				ConsoleUI.Error("没有可用的上一版本备份，无法回滚。");
				return false;
			}

			ConsoleUI.Info($"准备回滚到上一版本: {latestBackup.Version}");
			var rollback = new RollbackManager(engineDir, state);
			var ok = await rollback.RollbackAsync(confirm: false);
			if (ok)
			{
				ConsoleUI.Success("已回滚到上一版本，即将重新启动引擎。");
			}
			return ok;
		}
		catch (Exception ex)
		{
			ConsoleUI.Error($"闪退回滚失败: {ex.Message}");
			return false;
		}
	}

	private static CrashInfo BuildCrashInfo(string exePath, string engineDir, int exitCode,
		string stderr, List<string> crashLogs, double runDuration)
	{
		return new CrashInfo
		{
			ExitCode = exitCode,
			Stderr = stderr,
			EngineExePath = exePath,
			EngineDir = engineDir,
			CrashLogFiles = crashLogs.Where(File.Exists).ToList(),
			EngineVersion = TryReadEngineVersion(engineDir),
			CrashTime = DateTime.Now,
			RunDurationSeconds = runDuration
		};
	}

	/// <summary>扫描引擎目录中常见命名模式的崩溃日志文件。</summary>
	private static List<string> ScanCrashLogs(string engineDir)
	{
		var candidates = new List<string>();
		try
		{
			var roots = new[] {
				engineDir,
				Path.Combine(engineDir, "logs"),
				Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
				Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData)
			};
			foreach (var root in roots.Where(Directory.Exists))
			{
				try
				{
					candidates.AddRange(Directory.EnumerateFiles(root, "*crash*", SearchOption.AllDirectories)
						.Take(20)
						.OrderByDescending(f => File.GetLastWriteTime(f))
						.Take(5));

					candidates.AddRange(Directory.EnumerateFiles(root, "*.log", SearchOption.AllDirectories)
						.OrderByDescending(f => File.GetLastWriteTime(f))
						.Take(5));
				}
				catch { }
			}
		}
		catch { }
		return candidates.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
	}

	/// <summary>从 .jundot-update-state.json 或本地 update-manifest.json 读取当前版本字符串（若存在）。</summary>
	private static string? TryReadEngineVersion(string engineDir)
	{
		try
		{
			var path = Path.Combine(engineDir, ".jundot-update-state.json");
			if (File.Exists(path))
			{
				var json = File.ReadAllText(path);
				// 轻量解析：兼容旧 PascalCase 和当前 snake_case 状态字段。
				var match = System.Text.RegularExpressions.Regex.Match(json,
					@"""(?:[Cc]urrent[Vv]ersion|current_version)""\s*:\s*""([^""]+)""");
				if (match.Success && !string.IsNullOrWhiteSpace(match.Groups[1].Value) && match.Groups[1].Value != "0.0.0")
				{
					return match.Groups[1].Value;
				}
			}

			var manifestPath = Path.Combine(engineDir, "update-manifest.json");
			if (!File.Exists(manifestPath)) return null;
			var manifestJson = File.ReadAllText(manifestPath);
			var manifestMatch = System.Text.RegularExpressions.Regex.Match(manifestJson,
				@"""version""\s*:\s*\{.*?""full""\s*:\s*""([^""]+)""",
				System.Text.RegularExpressions.RegexOptions.Singleline);
			return manifestMatch.Success ? manifestMatch.Groups[1].Value : null;
		}
		catch
		{
			return null;
		}
	}

	/// <summary>显示崩溃对话框，返回用户选择的恢复动作。</summary>
	private static CrashDialogAction ShowCrashDialog(CrashInfo info)
	{
		// 避免在非 Windows 环境尝试 GUI
		if (!OperatingSystem.IsWindows())
		{
			ConsoleUI.Error($"引擎以退出码 {info.ExitCode} 结束。");
			if (!string.IsNullOrWhiteSpace(info.Stderr))
			{
				ConsoleUI.Info("—— stderr ——");
				ConsoleUI.Info(info.Stderr.Length > 3000
					? info.Stderr.Substring(0, 3000) + "..."
					: info.Stderr);
			}
			return CrashDialogAction.Close;
		}

		try
		{
			return ShowMauiCrashDialog(info);
		}
		catch (FileNotFoundException ex)
		{
			// CrashDialog.exe 未找到，在控制台输出崩溃信息
			ConsoleUI.Error($"========================================");
			ConsoleUI.Error($"引擎发生崩溃 (退出码 {info.ExitCode})。");
			ConsoleUI.Error($"崩溃对话框程序未找到: {ex.Message}");
			ConsoleUI.Info($"stderr 输出 ({info.Stderr.Length} 字符):");
			ConsoleUI.Info(info.Stderr.Length > 2000
				? info.Stderr.Substring(0, 2000) + "..."
				: info.Stderr);
			ConsoleUI.Info($"完整日志: {info.CrashLogFiles.FirstOrDefault() ?? "(无)"}");
			ConsoleUI.Error($"========================================");
			return CrashDialogAction.Close;
		}
		catch (Exception ex)
		{
			// MAUI 对话框启动失败，降级到控制台输出
			ConsoleUI.Error($"显示崩溃弹窗失败: {ex.Message}");
			ConsoleUI.Info($"引擎退出码: {info.ExitCode}");
			if (!string.IsNullOrWhiteSpace(info.Stderr))
				ConsoleUI.Info(info.Stderr.Length > 3000
					? info.Stderr.Substring(0, 3000) + "..."
					: info.Stderr);
			return CrashDialogAction.Close;
		}
	}

	/// <summary>调用 MAUI 崩溃对话框进程，返回用户选择的恢复动作。</summary>
	private static CrashDialogAction ShowMauiCrashDialog(CrashInfo info)
	{
		var launcherDir = AppContext.BaseDirectory;

		// 打包后的目录结构: Tools/Launcher/ + Tools/CrashDialog/
		// 开发时的目录结构: tools/Launcher/bin/ + tools/JundotCrashDialog/bin/
		var candidatePaths = new[]
		{
            // 打包后: Tools/CrashDialog/JundotCrashDialog.exe (相对于 Launcher 目录)
            Path.Combine(launcherDir, "..", "CrashDialog", "JundotCrashDialog.exe"),
            // 开发时: 直接在 Launcher 输出目录（可能复制过来）
            Path.Combine(launcherDir, "JundotCrashDialog.exe"),
            // 开发时: 从构建输出目录查找
            Path.Combine(launcherDir, "..", "..", "..", "..", "JundotCrashDialog", "bin", "Release", "net10.0-windows10.0.19041.0", "win-x64", "JundotCrashDialog.exe"),
		};

		var crashDialogPath = candidatePaths.FirstOrDefault(File.Exists);
		if (crashDialogPath == null)
		{
			throw new FileNotFoundException("找不到崩溃对话框程序，尝试路径:\n  " + string.Join("\n  ", candidatePaths));
		}

		// stderr 已直接写在引擎目录的 logs/ 中, 对话框自行读取
		// 这里仍传一份 stderr 内联数据 (用于弹窗立即显示, 不必等对话框读文件)
		var args = new List<string>();
		args.Add($"--exit-code {info.ExitCode}");
		args.Add($"--engine-exe \"{info.EngineExePath}\"");
		args.Add($"--engine-dir \"{info.EngineDir}\"");
		args.Add($"--crash-time \"{info.CrashTime:O}\"");
		args.Add($"--run-duration {info.RunDurationSeconds:F1}");

		// 把 stderr 写入临时文件供 MAUI 对话框读取 (避免命令行长度限制)
		var stderrTempPath = Path.Combine(Path.GetTempPath(), $"jundot_stderr_{Guid.NewGuid():N}.txt");
		try
		{
			File.WriteAllText(stderrTempPath, info.Stderr ?? string.Empty);
			args.Add($"--stderr-file \"{stderrTempPath}\"");
		}
		catch
		{
			args.Add($"--stderr-file \"\"");
		}

		if (!string.IsNullOrEmpty(info.EngineVersion))
			args.Add($"--version \"{info.EngineVersion}\"");

		foreach (var logFile in info.CrashLogFiles)
			args.Add($"--crashlog \"{logFile}\"");

		try
		{
			var psi = new ProcessStartInfo(crashDialogPath, string.Join(" ", args))
			{
				UseShellExecute = true,
				WorkingDirectory = launcherDir
			};

			using var process = Process.Start(psi);
			if (process == null)
				return CrashDialogAction.Close;

			process.WaitForExit();

			// 退出码 100 表示用户选择重启
			return process.ExitCode switch
			{
				100 => CrashDialogAction.Restart,
				101 => CrashDialogAction.Rollback,
				_ => CrashDialogAction.Close
			};
		}
		finally
		{
			try { File.Delete(stderrTempPath); } catch { }
		}
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
			if (arg.StartsWith("--engine-path=", StringComparison.OrdinalIgnoreCase))
			{
				_enginePath = arg.Substring("--engine-path=".Length).Trim('"');
				continue;
			}
			else if (arg == "--yes" || arg == "-y")
			{
				AssumeYes = true;
				continue;
			}

			// Handle --channel value
			if (arg == "--channel" && i + 1 < args.Length)
			{
				_channel = args[++i];
				continue;
			}
			if (arg.StartsWith("--channel=", StringComparison.OrdinalIgnoreCase))
			{
				_channel = arg.Substring("--channel=".Length).Trim('"');
				continue;
			}

			// Handle --manifest-url value
			if (arg == "--manifest-url" && i + 1 < args.Length)
			{
				_manifestUrl = args[++i];
				continue;
			}
			if (arg.StartsWith("--manifest-url=", StringComparison.OrdinalIgnoreCase))
			{
				_manifestUrl = arg.Substring("--manifest-url=".Length).Trim('"');
				continue;
			}

			if (arg.StartsWith("--target=", StringComparison.OrdinalIgnoreCase))
			{
				// Accepted for forward compatibility with rollback callers that pass a target version.
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
