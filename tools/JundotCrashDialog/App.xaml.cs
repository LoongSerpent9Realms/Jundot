using Microsoft.Extensions.DependencyInjection;

namespace JundotCrashDialog;

public partial class App : Application
{
	public static bool RestartRequested { get; set; }

	public App()
	{
		InitializeComponent();
		RestartRequested = false;
	}

	protected override Window CreateWindow(IActivationState? activationState)
	{
		var crashInfo = ParseCommandLineArgs();
		var mainPage = new MainPage(crashInfo);
		
		mainPage.Disappearing += (s, e) =>
		{
			RestartRequested = mainPage.UserRequestedRestart;
			Environment.ExitCode = RestartRequested ? 100 : 0;
		};
		
		return new Window(mainPage);
	}

	private CrashInfo ParseCommandLineArgs()
	{
		var args = Environment.GetCommandLineArgs();
		var info = new CrashInfo();

		for (int i = 0; i < args.Length; i++)
		{
			switch (args[i])
			{
				case "--exit-code" when i + 1 < args.Length:
					if (int.TryParse(args[++i], out var code))
						info.ExitCode = code;
					break;
				case "--engine-exe" when i + 1 < args.Length:
					info.EngineExePath = args[++i];
					break;
				case "--engine-dir" when i + 1 < args.Length:
					info.EngineDir = args[++i];
					break;
				case "--version" when i + 1 < args.Length:
					info.EngineVersion = args[++i];
					break;
				case "--crash-time" when i + 1 < args.Length:
					if (DateTime.TryParse(args[++i], out var time))
						info.CrashTime = time;
					break;
				case "--stderr-file" when i + 1 < args.Length:
					var stderrPath = args[++i];
					if (File.Exists(stderrPath))
						info.Stderr = File.ReadAllText(stderrPath);
					break;
				case "--run-duration" when i + 1 < args.Length:
					if (double.TryParse(args[++i], System.Globalization.NumberStyles.Float,
						System.Globalization.CultureInfo.InvariantCulture, out var duration))
						info.RunDurationSeconds = duration;
					break;
				case "--crashlog" when i + 1 < args.Length:
					info.CrashLogFiles.Add(args[++i]);
					break;
				case "--crash-info" when i + 1 < args.Length:
					var crashInfoPath = args[++i];
					if (File.Exists(crashInfoPath))
					{
						var crashInfoContent = File.ReadAllText(crashInfoPath);
						ParseCrashInfoFile(crashInfoContent, info);
					}
					break;
			}
		}

		PopulateDerivedCrashInfo(info);
		DiscoverEngineLogFiles(info);
		info.LogOutputPath = CrashLogWriter.WriteCrashLog(info);
		return info;
	}

	private static void PopulateDerivedCrashInfo(CrashInfo info)
	{
		if (string.IsNullOrWhiteSpace(info.EngineDir) && !string.IsNullOrWhiteSpace(info.EngineExePath))
		{
			try
			{
				var dir = Path.GetDirectoryName(info.EngineExePath);
				if (!string.IsNullOrWhiteSpace(dir))
					info.EngineDir = dir;
			}
			catch
			{
				// Best-effort metadata only.
			}
		}
	}

	private static void DiscoverEngineLogFiles(CrashInfo info)
	{
		var candidates = new List<string>();

		if (!string.IsNullOrWhiteSpace(info.EngineDir))
			AddRecentLogsFromDirectory(Path.Combine(info.EngineDir, "logs"), info.CrashTime, candidates);

		var appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
		if (!string.IsNullOrWhiteSpace(appData))
		{
			AddRecentLogsFromDirectory(Path.Combine(appData, "Jundot", "logs"), info.CrashTime, candidates);
			AddRecentLogsFromDirectory(Path.Combine(appData, "Jundot", "app_userdata"), info.CrashTime, candidates, recursive: true);
			AddRecentLogsFromDirectory(Path.Combine(appData, "Godot", "logs"), info.CrashTime, candidates);
			AddRecentLogsFromDirectory(Path.Combine(appData, "Godot", "app_userdata"), info.CrashTime, candidates, recursive: true);
		}

		foreach (var path in candidates
			.Where(File.Exists)
			.Distinct(StringComparer.OrdinalIgnoreCase)
			.OrderByDescending(File.GetLastWriteTime)
			.Take(8))
		{
			if (!info.CrashLogFiles.Contains(path, StringComparer.OrdinalIgnoreCase))
				info.CrashLogFiles.Add(path);
		}
	}

	private static void AddRecentLogsFromDirectory(string directory, DateTime crashTime, List<string> candidates, bool recursive = false)
	{
		try
		{
			if (!Directory.Exists(directory))
				return;

			var searchOption = recursive ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly;
			var crashWindowStart = crashTime.AddMinutes(-30);
			var crashWindowEnd = crashTime.AddMinutes(10);

			foreach (var file in Directory.EnumerateFiles(directory, "*.*", searchOption))
			{
				var extension = Path.GetExtension(file);
				if (!extension.Equals(".log", StringComparison.OrdinalIgnoreCase) &&
						!extension.Equals(".txt", StringComparison.OrdinalIgnoreCase))
					continue;

				var name = Path.GetFileName(file);
				if (name.StartsWith("jundot-crash-", StringComparison.OrdinalIgnoreCase))
					continue;

				var lastWriteTime = File.GetLastWriteTime(file);
				if (lastWriteTime >= crashWindowStart && lastWriteTime <= crashWindowEnd)
					candidates.Add(file);
			}
		}
		catch
		{
			// Log discovery should never prevent the crash dialog from opening.
		}
	}

	/// <summary>Parses the crash info file written by the C++ crash handler.</summary>
	private static void ParseCrashInfoFile(string content, CrashInfo info)
	{
		if (string.IsNullOrWhiteSpace(content)) return;

		foreach (var line in content.Split('\n', StringSplitOptions.RemoveEmptyEntries))
		{
			var trimmed = line.Trim();
			if (trimmed.StartsWith("Engine:", StringComparison.OrdinalIgnoreCase))
				info.EngineExePath = trimmed.Substring("Engine:".Length).Trim();
			else if (trimmed.StartsWith("EngineDir:", StringComparison.OrdinalIgnoreCase))
				info.EngineDir = trimmed.Substring("EngineDir:".Length).Trim();
			else if (trimmed.StartsWith("Version:", StringComparison.OrdinalIgnoreCase))
				info.EngineVersion = trimmed.Substring("Version:".Length).Trim();
			else if (trimmed.StartsWith("Hash:", StringComparison.OrdinalIgnoreCase))
				info.EngineVersion = (info.EngineVersion ?? "") + " (" + trimmed.Substring("Hash:".Length).Trim() + ")";
			else if (trimmed.StartsWith("CrashTime:", StringComparison.OrdinalIgnoreCase))
			{
				if (DateTime.TryParse(trimmed.Substring("CrashTime:".Length).Trim(), out var crashTime))
					info.CrashTime = crashTime;
			}
		}
	}
}
