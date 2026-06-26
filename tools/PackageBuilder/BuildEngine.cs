using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;

namespace JundotPackageBuilder;

/// <summary>
/// Configuration for a Jundot build + package run.
/// Mirrors the parameters of scripts/package-jundot.ps1.
/// </summary>
public class BuildConfig
{
	// ── Target / Platform / Arch ────────────────────────────────
	public string Target { get; set; } = "editor";          // editor | template_release | template_debug
	public string PlatformName { get; set; } = "windows";         // windows | linuxbsd | macos | android | ios | web
	public string Arch { get; set; } = "x86_64";          // x86_64 | x86_32 | arm64

	// ── Paths ───────────────────────────────────────────────────
	public string RepoRoot { get; set; } = "";                // Jundot source root (auto-detected if empty)
	public string OutputDir { get; set; } = "artifacts/packages";
	public string LogDir { get; set; } = "artifacts/logs";

	// ── Package naming ──────────────────────────────────────────
	public string PackageName { get; set; } = "";                // empty = auto-generate

	// ── Build control ───────────────────────────────────────────
	public int Jobs { get; set; } = 0;                 // 0 = Environment.ProcessorCount - 1
	public bool SkipBuild { get; set; } = false;
	public bool InstallSCons { get; set; } = false;
	public bool Mono { get; set; } = false;
	public bool UseMinGW { get; set; } = false;
	public string MingwPrefix { get; set; } = "";

	/// <summary>Enable d3d12, accesskit, angle on Windows.</summary>
	public bool EnableWindowsOptionalDeps { get; set; } = false;

	/// <summary>Remove staging dir before copying.</summary>
	public bool CleanPackageDir { get; set; } = false;

	/// <summary>Run scons -c before building to force full rebuild.</summary>
	public bool CleanBuild { get; set; } = false;

	/// <summary>Extra key=value args forwarded to SCons.</summary>
	public string ExtraSConsArgs { get; set; } = "";

	/// <summary>After a successful build + package, auto-increment the patch number in version.py.</summary>
	public bool AutoUpdateVersion { get; set; } = false;

	/// <summary>Generate update-manifest.json after packaging (for online hot-update system).</summary>
	public bool GenerateUpdateManifest { get; set; } = true;
}

/// <summary>
/// Build progress event data.
/// </summary>
public class BuildProgressEventArgs : EventArgs
{
	public string Message { get; init; } = "";
	public string MessageType { get; init; } = "info";  // info | step | warning | error | success | output
	/// <summary>Progress value 0.0�?.0, or null for indeterminate.</summary>
	public double? Progress { get; init; }
}

/// <summary>
/// Core build engine �?translates scripts/package-jundot.ps1 logic into C#.
/// </summary>
public class BuildEngine
{
	public event EventHandler<BuildProgressEventArgs>? ProgressChanged;

	private readonly BuildConfig _cfg;
	private CancellationToken _ct;
	private string _repoRoot = "";
	private string _packageRoot = "";
	private string _logRoot = "";
	private string _stagingDir = "";
	private string _zipPath = "";
	private string _version = "";
	private string _packageName = "";

	private string _pythonPath = "";
	private string _sconsCmd = "";
	private List<string> _sconsPrefix = new();

	private Process? _currentProcess;
	private readonly object _logLock = new();

	// Normalized target: "editor.dev" �?"editor" (for SCons + file pattern matching)
	private string _actualTarget = "editor";
	private string[]? _versionFileBackup;
	private bool _autoVersionApplied;

	/// <summary>爬取 Ninja/SCons 输出的编译进度�?/summary>
	private double? _ninjaTotal;
	private double _ninjaCurrent;

	/// <summary>Optional �?set to enable automatic build history recording.</summary>
	public BuildManager? BuildManager { get; set; }

	public BuildConfig Config => _cfg;
	public string Version => _version;
	public string PackageName => _packageName;

	public BuildEngine(BuildConfig cfg)
	{
		_cfg = cfg;
		_actualTarget = cfg.Target == "editor.dev" ? "editor" : cfg.Target;
	}

	// ══════════════════════════════════════════════════════════════�?    //  PUBLIC API
	// ══════════════════════════════════════════════════════════════�?
	public async Task<bool> RunAsync(CancellationToken ct)
	{
		_ct = ct;

		try
		{
			// ── 1. Resolve paths ────────────────────────────
			ResolvePaths();

			// ── 2. Read version ─────────────────────────────
			if (_cfg.AutoUpdateVersion)
			{
				var oldVer = GetJundotVersion();
				_versionFileBackup = ReadVersionFileLines();
				_version = UpdateVersionFile();
				_autoVersionApplied = true;
				Report($"Version auto-updated before build: {oldVer} -> {_version}", "success");
			}
			else
			{
				_version = GetJundotVersion();
			}
			Report($"Jundot version: {_version}", "info");
			InvalidateVersionDependentBuildOutputs(_version);

			// ── 3. Generate package name ────────────────────
			_packageName = BuildPackageName();
			Report($"Package name: {_packageName}", "info");

			_packageRoot = Path.GetFullPath(Path.Combine(_repoRoot, _cfg.OutputDir));
			_logRoot = Path.GetFullPath(Path.Combine(_repoRoot, _cfg.LogDir));
			_stagingDir = Path.Combine(_packageRoot, _packageName);
			_zipPath = Path.Combine(_packageRoot, $"{_packageName}.zip");

			// ── 4. Build (unless skipped) ───────────────────
			if (!_cfg.SkipBuild)
			{
				Report("", "step");
				Report("Checking build tools", "step");

				await FindPythonAsync();
				await FindSConsRunnerAsync();
				await AssertWindowsCompilerAsync();
				await CheckMonoToolsAsync();

				// Host platform guard: Jundot's SCons build can only build a
				// target platform on a matching host without a full cross-
				// compile toolchain. Surface a clear error early instead of
				// letting SCons fail with `Invalid target platform "linuxbsd"`.
				var hostPlatform = Environment.OSVersion.Platform.ToString().ToLowerInvariant();
				var isWindowsHost = OperatingSystem.IsWindows();
				var isMacOSHost = OperatingSystem.IsMacOS();
				var isLinuxHost = OperatingSystem.IsLinux();

				bool platformOk = _cfg.PlatformName switch
				{
					"windows" => isWindowsHost,
					"linuxbsd" => isLinuxHost,
					"macos" => isMacOSHost,
					"android" => isWindowsHost || isLinuxHost || isMacOSHost, // requires NDK
					"ios" => isMacOSHost,                                    // requires Xcode
					"web" => isWindowsHost || isLinuxHost || isMacOSHost,    // requires emscripten
					_ => true
				};

				if (!platformOk)
				{
					var hostDesc =
						  isWindowsHost ? "Windows"
						: isMacOSHost ? "macOS"
						: isLinuxHost ? "Linux"
						: hostPlatform;
					throw new InvalidOperationException(
						$"Target platform '{_cfg.PlatformName}' cannot be built on this {hostDesc} host. " +
						$"Switch the Platform dropdown to '{(isWindowsHost ? "windows" : isLinuxHost ? "linuxbsd" : "macos")}' " +
						$"or run the build on the matching host OS.");
				}

				Report("", "step");
				Report("Building Jundot", "step");
				await BuildAsync();
			}
			else
			{
				Report("Skipping build �?using existing files in bin/", "step");
				if (_cfg.AutoUpdateVersion)
					Report("Auto-version updated version.py, but Skip Build packages existing binaries; rebuild to embed the new engine version.", "warning");
			}

			// ── 5. Package ──────────────────────────────────
			Report("", "step");
			Report("Preparing package folders", "step");
			await PackageAsync();

			Report("", "success");
			Report($"Package created: {_zipPath}", "success");

			if (!_cfg.AutoUpdateVersion)
				Report("Auto-update version is disabled; version.py was not changed.", "info");

			SaveBuildRecord();
			_versionFileBackup = null;
			_autoVersionApplied = false;

			return true;
		}
		catch (OperationCanceledException)
		{
			RestoreVersionFileAfterFailedBuild();
			Report("Build cancelled by user.", "warning");
			return false;
		}
		catch (Exception ex)
		{
			RestoreVersionFileAfterFailedBuild();
			Report($"ERROR: {ex.Message}", "error");
			return false;
		}
	}

	/// <summary>Stop the currently running child process.</summary>
	public void Cancel()
	{
		try { _currentProcess?.Kill(entireProcessTree: true); } catch { /* best effort */ }
	}

	// ══════════════════════════════════════════════════════════════�?    //  PATH RESOLUTION
	// ══════════════════════════════════════════════════════════════�?
	private void ResolvePaths()
	{
		if (!string.IsNullOrWhiteSpace(_cfg.RepoRoot))
		{
			_repoRoot = Path.GetFullPath(_cfg.RepoRoot);
		}
		else
		{
			// Walk up from this assembly until we find SConstruct
			var dir = AppContext.BaseDirectory;
			while (dir != null && !File.Exists(Path.Combine(dir, "SConstruct")))
			{
				var parent = Path.GetDirectoryName(dir);
				if (parent == dir) break;
				dir = parent;
			}

			_repoRoot = dir ?? throw new Exception(
				"SConstruct not found. Set RepoRoot to the Jundot source tree, " +
				"or place this tool inside the Jundot source tree.");
		}

		if (!File.Exists(Path.Combine(_repoRoot, "SConstruct")))
			throw new Exception($"SConstruct not found at {_repoRoot}. Is this the Jundot source tree?");

		Report($"Repo root: {_repoRoot}", "info");
	}

	private string BuildPackageName()
	{
		if (!string.IsNullOrWhiteSpace(_cfg.PackageName))
			return _cfg.PackageName;

		var monoPart = _cfg.Mono ? "-mono" : "";
		var ts = DateTime.Now.ToString("yyyyMMdd-HHmmss");
		return $"jundot-{_version}-{_cfg.PlatformName}-{_cfg.Target}-{_cfg.Arch}{monoPart}-{ts}";
	}

	// ══════════════════════════════════════════════════════════════�?    //  VERSION
	// ══════════════════════════════════════════════════════════════�?
	private string GetJundotVersion()
	{
		var vf = Path.Combine(_repoRoot, "version.py");
		if (!File.Exists(vf))
			throw new Exception($"version.py not found at {vf}");

		var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		foreach (var line in File.ReadLines(vf))
		{
			var m = Regex.Match(line, @"^\s*([A-Za-z_]+)\s*=\s*""?([^""]+)""?\s*$");
			if (m.Success)
				values[m.Groups[1].Value] = m.Groups[2].Value;
		}

		var v = $"{values.GetValueOrDefault("major", "0")}.{values.GetValueOrDefault("minor", "0")}";
		var patch = values.GetValueOrDefault("patch", "");
		if (!string.IsNullOrEmpty(patch) && patch != "0")
			v += $".{patch}";

		var status = values.GetValueOrDefault("status", "");
		if (!string.IsNullOrEmpty(status))
			v += $"-{status}";

		return v;
	}

	private string[] ReadVersionFileLines()
	{
		var vf = Path.Combine(_repoRoot, "version.py");
		if (!File.Exists(vf))
			throw new Exception($"version.py not found at {vf}");

		return File.ReadAllLines(vf);
	}

	private void RestoreVersionFileAfterFailedBuild()
	{
		if (!_autoVersionApplied || _versionFileBackup == null)
			return;

		try
		{
			var vf = Path.Combine(_repoRoot, "version.py");
			File.WriteAllLines(vf, _versionFileBackup, System.Text.Encoding.UTF8);
			Report("Auto-updated version.py was restored because the build did not complete.", "warning");
		}
		catch (Exception ex)
		{
			Report($"Warning: failed to restore version.py after build failure: {ex.Message}", "warning");
		}
		finally
		{
			_versionFileBackup = null;
			_autoVersionApplied = false;
		}
	}

	private void InvalidateVersionDependentBuildOutputs(string oldVersion)
	{
		if (string.IsNullOrWhiteSpace(oldVersion))
			return;

		var objRoot = Path.Combine(_repoRoot, "bin", "obj");
		if (!Directory.Exists(objRoot))
			return;

		var deleted = 0;
		var oldVersionBytes = Encoding.UTF8.GetBytes(oldVersion);
		var oldVersionWideBytes = Encoding.Unicode.GetBytes(oldVersion);
		var versionNameBytes = Encoding.UTF8.GetBytes("Jundot Engine v");
		var versionNameWideBytes = Encoding.Unicode.GetBytes("Jundot Engine v");
		var extensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { ".obj", ".res", ".lib" };

		foreach (var file in Directory.EnumerateFiles(objRoot, "*", SearchOption.AllDirectories))
		{
			if (!extensions.Contains(Path.GetExtension(file)))
				continue;

			try
			{
				var bytes = File.ReadAllBytes(file);
				if (!ContainsBytes(bytes, oldVersionBytes) &&
					!ContainsBytes(bytes, oldVersionWideBytes) &&
					!ContainsBytes(bytes, versionNameBytes) &&
					!ContainsBytes(bytes, versionNameWideBytes))
					continue;

				File.Delete(file);
				deleted++;
			}
			catch (Exception ex)
			{
				Report($"Warning: failed to remove stale version build output '{file}': {ex.Message}", "warning");
			}
		}

		if (deleted > 0)
			Report($"Removed {deleted} stale build output(s) containing old engine version {oldVersion}.", "warning");
	}

	private static bool ContainsBytes(byte[] haystack, byte[] needle)
	{
		if (needle.Length == 0 || haystack.Length < needle.Length)
			return false;

		for (var i = 0; i <= haystack.Length - needle.Length; i++)
		{
			var matched = true;
			for (var j = 0; j < needle.Length; j++)
			{
				if (haystack[i + j] == needle[j])
					continue;

				matched = false;
				break;
			}

			if (matched)
				return true;
		}

		return false;
	}

	/// <summary>
	/// Increment the patch number in version.py.
	/// Returns the new version string after update.
	/// </summary>
	private string UpdateVersionFile()
	{
		var vf = Path.Combine(_repoRoot, "version.py");
		if (!File.Exists(vf))
			throw new Exception($"version.py not found at {vf}");

		var lines = File.ReadAllLines(vf).ToList();
		int newPatch = 0;

		for (int i = 0; i < lines.Count; i++)
		{
			var m = Regex.Match(lines[i], @"^(\s*patch(?:\s*:\s*[A-Za-z_][A-Za-z0-9_\.\[\]]*)?\s*=\s*)([""']?)(\d+)(\2)(.*)$");
			if (m.Success)
			{
				var oldPatch = int.Parse(m.Groups[3].Value);
				newPatch = oldPatch + 1;
				lines[i] = $"{m.Groups[1].Value}{m.Groups[2].Value}{newPatch}{m.Groups[4].Value}{m.Groups[5].Value}";
				break;
			}
		}

		if (newPatch == 0)
			throw new Exception("Could not find 'patch' field in version.py.");

		// Write back
		File.WriteAllLines(vf, lines, System.Text.Encoding.UTF8);

		// Re-read new version
		return GetJundotVersion();
	}

	// ══════════════════════════════════════════════════════════════�?    //  TOOL DETECTION
	// ══════════════════════════════════════════════════════════════�?
	private async Task FindPythonAsync()
	{
		// On Windows, prefer "py" launcher �?it is never the Store alias.
		// Fall back to "python"/"python3" but verify they are real Python.
		var candidates = new[] { "py", "python", "python3" };
		foreach (var name in candidates)
		{
			if (name == "py")
			{
				// "py" launcher: just check exit code (it's never the Store alias)
				if (await TestCommandAsync(name, "--version"))
				{
					_pythonPath = name;
					return;
				}
			}
			else
			{
				// "python"/"python3": must verify it's not the Microsoft Store alias
				if (await TestPythonCommandAsync(name))
				{
					_pythonPath = name;
					return;
				}
			}
		}

		throw new Exception(
			"Python was not found in PATH. Install Python, then run this tool again.");
	}

	private async Task FindSConsRunnerAsync()
	{
		// Try "python -m SCons"
		if (await TestCommandAsync(_pythonPath, "-m", "SCons", "--version"))
		{
			_sconsCmd = _pythonPath;
			_sconsPrefix = new List<string> { "-m", "SCons" };
			return;
		}

		// Try bare "scons"
		if (await TestCommandAsync("scons", "--version"))
		{
			_sconsCmd = "scons";
			_sconsPrefix = new List<string>();
			return;
		}

		// Optionally install
		if (_cfg.InstallSCons)
		{
			Report("Installing SCons via pip", "step");
			await RunAndReportAsync(_pythonPath, "-m", "pip", "install", "--user", "scons");

			if (await TestCommandAsync(_pythonPath, "-m", "SCons", "--version"))
			{
				_sconsCmd = _pythonPath;
				_sconsPrefix = new List<string> { "-m", "SCons" };
				return;
			}

			throw new Exception("SCons was installed but still cannot be started.");
		}

		throw new Exception(
			"SCons was not found. Install it with: pip install scons, or check Install SCons in the UI.");
	}

	private Version? GetSConsVersion()
	{
		try
		{
			var args = new List<string>(_sconsPrefix) { "--version" };
			var psi = new ProcessStartInfo(_sconsCmd, string.Join(" ", args))
			{
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				UseShellExecute = false,
				CreateNoWindow = true
			};
			ConfigureProcessOutput(psi);
			using var p = Process.Start(psi);
			if (p == null) return null;
			var output = p.StandardOutput.ReadToEnd();
			p.WaitForExit(5000);

			var m = Regex.Match(output, @"(?<![0-9])(\d+)\.(\d+)\.(\d+)(?![0-9])");
			if (m.Success)
				return new Version(int.Parse(m.Groups[1].Value), int.Parse(m.Groups[2].Value), int.Parse(m.Groups[3].Value));
		}
		catch { /* fall through */ }
		return null;
	}

	private async Task EnsureSConsForMsvcAsync(string msvcVersion)
	{
		if (msvcVersion != "14.5") return;

		var ver = GetSConsVersion();
		if (ver != null && ver >= new Version(4, 10, 1)) return;

		if (!_cfg.InstallSCons)
		{
			var reported = ver?.ToString() ?? "unknown";
			throw new Exception(
				$"Visual Studio 2026 requires SCons >= 4.10.1, but detected {reported}. Enable 'Install SCons' to upgrade.");
		}

		Report("Upgrading SCons for Visual Studio 2026", "step");
		await RunAndReportAsync(_pythonPath, "-m", "pip", "install", "--user", "--upgrade", "scons>=4.10.1");

		ver = GetSConsVersion();
		if (ver == null || ver < new Version(4, 10, 1))
		{
			var reported = ver?.ToString() ?? "unknown";
			throw new Exception($"SCons was upgraded but still reports {reported} instead of >= 4.10.1.");
		}
	}

	// ══════════════════════════════════════════════════════════════�?    //  WINDOWS COMPILER DETECTION
	// ══════════════════════════════════════════════════════════════�?
	private async Task AssertWindowsCompilerAsync()
	{
		if (_cfg.PlatformName != "windows") return;

		// Try MSVC first
		if (await ImportVisualStudioEnvironmentAsync())
			return;

		// Try MinGW
		var hasMinGW = _cfg.UseMinGW || !string.IsNullOrWhiteSpace(_cfg.MingwPrefix) ||
					   !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("MSYSTEM")) ||
					   !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("MINGW_PREFIX"));

		if (hasMinGW)
		{
			if (await TestMinGWAsync())
				return;

			PrintWindowsCompilerHelp();
			throw new Exception("MinGW was requested but gcc/clang could not be started.");
		}

		PrintWindowsCompilerHelp();
		throw new Exception("Windows C++ compiler is required before building Jundot.");
	}

	private void PrintWindowsCompilerHelp()
	{
		var help = @"
Windows C++ compiler was not found.
Install one of these toolchains, then run this tool again:

  Option A: Visual Studio 2022 Build Tools
    1. Install 'Desktop development with C++'.
    2. Include a Windows 10/11 SDK.

  Option B: MinGW-w64
    Set 'Use MinGW' and provide the MinGW prefix,
    or set MINGW_PREFIX environment variable.";

		Report(help, "warning");
	}

	private async Task<bool> ImportVisualStudioEnvironmentAsync()
	{
		if (_cfg.PlatformName != "windows" || _cfg.UseMinGW)
			return false;

		// 1. Already loaded? (e.g. running from VS Developer Command Prompt)
		if (FindCommand("cl") != null && !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("VSINSTALLDIR")))
		{
			Report("MSVC environment already loaded from parent process.", "info");
			return true;
		}

		// 2. Try cl.exe from PATH directly
		if (FindCommand("cl") != null)
		{
			Report("cl.exe found in PATH (using current environment).", "info");
			return true;
		}

		Report("Searching for Visual Studio installation", "step");

		// 3. Try common VS 2022 install paths directly
		if (TryImportVSFromCommonPaths())
			return true;

		// 4. Try vswhere
		if (await TryImportVSFromWhereAsync())
			return true;

		Report("Could not locate Visual Studio installation.", "warning");
		Report("Tried: dynamic scan of Program Files\\Microsoft Visual Studio\\* and vswhere.", "warning");
		return false;
	}

	private bool TryImportVSFromCommonPaths()
	{
		string[] basePaths =
		{
			Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),           // C:\Program Files
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),       // C:\Program Files (x86)
        };

		// Dynamically scan Microsoft Visual Studio directory tree for cl.exe
		// Supports all versions (2022, 2026, 18, etc.) and all editions (Community, Professional, Enterprise, BuildTools, Insiders, Preview)
		foreach (var basePath in basePaths)
		{
			var vsRoot = Path.Combine(basePath, "Microsoft Visual Studio");
			if (!Directory.Exists(vsRoot)) continue;

			// Walk version directories (2022, 2026, 18, etc.)
			foreach (var verDir in Directory.GetDirectories(vsRoot))
			{
				try
				{
					// Walk edition directories (Community, Professional, Insiders, etc.)
					foreach (var editionDir in Directory.GetDirectories(verDir))
					{
						var msvcBase = Path.Combine(editionDir, "VC", "Tools", "MSVC");
						if (!Directory.Exists(msvcBase)) continue;

						var msvcVersions = Directory.GetDirectories(msvcBase)
							.Select(d => new DirectoryInfo(d))
							.OrderByDescending(d => d.Name)
							.ToList();

						foreach (var msvcVer in msvcVersions)
						{
							var clPath = Path.Combine(msvcVer.FullName, "bin", "Hostx64", "x64", "cl.exe");
							if (!File.Exists(clPath)) continue;

							// ── Found cl.exe! Set up environment ──
							var binDir = Path.Combine(msvcVer.FullName, "bin", "Hostx64", "x64");
							var includeDir = Path.Combine(msvcVer.FullName, "include");
							var libDir = Path.Combine(msvcVer.FullName, "lib", "x64");

							Report($"Found Visual Studio at: {editionDir}", "info");
							Report($"Found cl.exe at: {clPath}", "info");

							// Find Windows SDK
							var sdkBase = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
								"Windows Kits", "10");
							string? sdkInclude = null, sdkLib = null, sdkVersion = null;

							if (Directory.Exists(sdkBase))
							{
								var inclBase = Path.Combine(sdkBase, "Include");
								if (Directory.Exists(inclBase))
								{
									var sdkVers = Directory.GetDirectories(inclBase)
										.Select(d => new DirectoryInfo(d))
										.OrderByDescending(d => d.Name)
										.FirstOrDefault();
									if (sdkVers != null)
									{
										sdkVersion = sdkVers.Name;
										sdkInclude = sdkVers.FullName;
										sdkLib = Path.Combine(sdkBase, "Lib", sdkVersion, "um", "x64");
									}
								}
							}

							// Prepend MSVC bin + SDK bin to PATH (rc.exe, mt.exe, etc.)
							var currentPath = Environment.GetEnvironmentVariable("PATH") ?? "";
							var pathAdditions = new List<string> { binDir };

							if (sdkVersion != null)
							{
								var sdkBinDir = Path.Combine(sdkBase, "bin", sdkVersion, "x64");
								if (Directory.Exists(sdkBinDir))
									pathAdditions.Add(sdkBinDir);
							}

							Environment.SetEnvironmentVariable("PATH",
								string.Join(Path.PathSeparator.ToString(), pathAdditions) + Path.PathSeparator + currentPath,
								EnvironmentVariableTarget.Process);

							var includes = new List<string> { includeDir };
							if (sdkInclude != null)
							{
								includes.Add(sdkInclude);
								includes.Add(Path.Combine(sdkInclude!, "ucrt"));
								includes.Add(Path.Combine(sdkInclude!, "shared"));
								includes.Add(Path.Combine(sdkInclude!, "um"));
								includes.Add(Path.Combine(sdkInclude!, "winrt"));
								includes.Add(Path.Combine(sdkInclude!, "cppwinrt"));
							}
							Environment.SetEnvironmentVariable("INCLUDE", string.Join(";", includes), EnvironmentVariableTarget.Process);

							var libs = new List<string> { libDir };
							if (sdkLib != null)
							{
								libs.Add(sdkLib);
								libs.Add(Path.Combine(Path.GetDirectoryName(sdkLib)!, "ucrt", "x64"));
							}
							Environment.SetEnvironmentVariable("LIB", string.Join(";", libs), EnvironmentVariableTarget.Process);
							Environment.SetEnvironmentVariable("LIBPATH", string.Join(";", libs), EnvironmentVariableTarget.Process);

							// Set VS detection vars
							Environment.SetEnvironmentVariable("VSINSTALLDIR", editionDir, EnvironmentVariableTarget.Process);
							Environment.SetEnvironmentVariable("VCToolsInstallDir", Path.Combine(editionDir, "VC", "Tools", "MSVC", msvcVer.Name) + "\\", EnvironmentVariableTarget.Process);
							Environment.SetEnvironmentVariable("VCToolsVersion", msvcVer.Name, EnvironmentVariableTarget.Process);
							if (sdkVersion != null)
								Environment.SetEnvironmentVariable("WindowsSDKLibVersion", $"{sdkVersion}\\", EnvironmentVariableTarget.Process);

							Report($"MSVC {msvcVer.Name} + Windows SDK {sdkVersion ?? "N/A"} configured.", "success");
							return true;
						}
					}
				}
				catch
				{
					// Skip directories we can't read
				}
			}
		}

		return false;
	}

	private async Task<bool> TryImportVSFromWhereAsync()
	{
		var vswhere = FindVsWhere();
		if (vswhere == null)
		{
			Report("vswhere.exe not found.", "info");
			return false;
		}

		Report($"Running vswhere: {vswhere}", "info");

		// Find VS install path
		var installPath = await RunAndCaptureAsync(vswhere,
			"-latest", "-products", "*",
			"-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
			"-property", "installationPath");

		if (string.IsNullOrWhiteSpace(installPath))
		{
			Report("vswhere returned no install path. Trying without workload filter.", "warning");
			installPath = await RunAndCaptureAsync(vswhere,
				"-latest", "-products", "*",
				"-property", "installationPath");

			if (string.IsNullOrWhiteSpace(installPath))
			{
				Report("vswhere still found nothing.", "warning");
				return false;
			}
		}

		installPath = installPath.Trim();
		Report($"Found VS at: {installPath}", "info");

		var devCmd = Path.Combine(installPath, "Common7", "Tools", "VsDevCmd.bat");
		if (!File.Exists(devCmd))
		{
			Report($"VsDevCmd.bat not found at expected path: {devCmd}", "warning");
			return false;
		}

		var targetArch = _cfg.Arch switch
		{
			"x86_32" => "x86",
			"arm64" => "arm64",
			_ => "x64"
		};

		Report($"Running VsDevCmd.bat -arch={targetArch}", "step");

		// Run VsDevCmd.bat and capture environment
		var escapedDevCmd = devCmd.Replace("\"", "\"\"");
		var psi = new ProcessStartInfo("cmd")
		{
			Arguments = $"/s /c \"\"{escapedDevCmd}\" -arch={targetArch} -host_arch=x64 >nul && set\"",
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			UseShellExecute = false,
			CreateNoWindow = true
		};
		ConfigureProcessOutput(psi);

		using var p = Process.Start(psi);
		if (p == null) return false;

		var output = await p.StandardOutput.ReadToEndAsync();
		var stderr = await p.StandardError.ReadToEndAsync();
		await p.WaitForExitAsync(_ct);

		if (p.ExitCode != 0)
		{
			if (!string.IsNullOrWhiteSpace(stderr))
				Report($"VsDevCmd.bat error: {stderr.Trim()}", "warning");
			Report($"VsDevCmd.bat exited with code {p.ExitCode}.", "warning");
			return false;
		}

		// Parse and apply environment variables
		foreach (var line in output.Split('\n'))
		{
			var trimmed = line.TrimEnd('\r', '\n');
			var sepIdx = trimmed.IndexOf('=');
			if (sepIdx > 0)
			{
				var name = trimmed[..sepIdx];
				var value = trimmed[(sepIdx + 1)..];
				Environment.SetEnvironmentVariable(name, value, EnvironmentVariableTarget.Process);
			}
		}

		var foundCl = FindCommand("cl");
		if (foundCl != null)
		{
			Report($"cl.exe found at: {foundCl}", "success");
			return true;
		}

		Report("cl.exe not found after loading VS environment.", "warning");
		return false;
	}

	private string? FindVsWhere()
	{
		var fromPath = FindCommand("vswhere");
		if (fromPath != null) return fromPath;

		var defaultPath = Path.Combine(
			Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
			"Microsoft Visual Studio", "Installer", "vswhere.exe");
		return File.Exists(defaultPath) ? defaultPath : null;
	}

	private async Task<bool> TestMinGWAsync()
	{
		var prefix = Environment.GetEnvironmentVariable("MINGW_PREFIX") ?? "";
		var binDir = !string.IsNullOrWhiteSpace(prefix) ? Path.Combine(prefix, "bin") : "";

		string? gcc = null, clang = null;
		if (!string.IsNullOrWhiteSpace(binDir))
		{
			gcc = Path.Combine(binDir, "gcc.exe");
			clang = Path.Combine(binDir, "clang.exe");
		}

		if (gcc != null && File.Exists(gcc) && await TestCommandAsync(gcc, "--version"))
			return true;
		if (clang != null && File.Exists(clang) && await TestCommandAsync(clang, "--version"))
			return true;
		if (string.IsNullOrWhiteSpace(binDir))
			return await TestCommandAsync("gcc", "--version") || await TestCommandAsync("clang", "--version");

		return false;
	}

	private string GetDetectedMsvcVersion()
	{
		if (_cfg.PlatformName != "windows" || _cfg.UseMinGW) return "";

		var toolsVer = Environment.GetEnvironmentVariable("VCToolsVersion") ?? "";
		if (toolsVer.StartsWith("14.5")) return "14.5";
		if (toolsVer.StartsWith("14.4")) return "14.4";
		if (toolsVer.StartsWith("14.3")) return "14.3";
		if (toolsVer.StartsWith("14.2")) return "14.2";

		var installDir = Environment.GetEnvironmentVariable("VSINSTALLDIR") ?? "";
		if (installDir.Contains("\\18\\")) return "14.5";

		return "";
	}

	private async Task CheckMonoToolsAsync()
	{
		if (!_cfg.Mono) return;

		if (FindCommand("dotnet") == null)
			throw new Exception("Mono builds require the .NET SDK. Install it and retry.");
	}

	// ══════════════════════════════════════════════════════════════�?    //  BUILD
	// ══════════════════════════════════════════════════════════════�?
	private async Task BuildAsync()
	{
		// Resolve MinGW prefix
		if (!string.IsNullOrWhiteSpace(_cfg.MingwPrefix))
		{
			var resolved = Path.GetFullPath(_cfg.MingwPrefix);
			if (!Directory.Exists(resolved))
				throw new Exception($"MinGW prefix not found: {resolved}");
			Environment.SetEnvironmentVariable("MINGW_PREFIX", resolved, EnvironmentVariableTarget.Process);
		}

		// Compute jobs
		var jobs = _cfg.Jobs;
		if (jobs <= 0)
		{
			// 释放全部 CPU 核心（留一个给系统），不再限制 editor �?4 线程
			jobs = Math.Max(1, Environment.ProcessorCount - 1);
		}

		// MSVC version detection
		var msvcVersion = GetDetectedMsvcVersion();
		var extraArgs = new List<string>();

		if (!string.IsNullOrWhiteSpace(msvcVersion) &&
			!HasArg(_cfg.ExtraSConsArgs, "msvc_version"))
		{
			await EnsureSConsForMsvcAsync(msvcVersion);
			extraArgs.Add($"msvc_version={msvcVersion}");

			if (msvcVersion == "14.5")
			{
				Report($"Detected Visual Studio 2026 / MSVC {Environment.GetEnvironmentVariable("VCToolsVersion")}; using msvc_version=14.5.", "warning");
				Report("Jundot requires SCons 4.10.1+ for Visual Studio 2026.", "warning");
			}
		}

		// Normalize editor.dev �?target=editor + dev_build=yes
		var isDevBuild = _cfg.Target == "editor.dev";

		// Build args
		var buildArgs = new List<string>(_sconsPrefix)
		{
			$"platform={_cfg.PlatformName}",
			$"target={_actualTarget}",
			$"arch={_cfg.Arch}",
			$"debug_symbols={(isDevBuild ? "yes" : "no")}",
			$"-j{jobs}"
		};
		if (isDevBuild)
			buildArgs.Add("dev_build=yes");
		buildArgs.AddRange(extraArgs);

		// Extra SCons args from UI
		if (!string.IsNullOrWhiteSpace(_cfg.ExtraSConsArgs))
		{
			buildArgs.AddRange(_cfg.ExtraSConsArgs.Split(' ', StringSplitOptions.RemoveEmptyEntries));
		}

		// ── 默认启用加速选项（用户可通过 ExtraSConsArgs 覆盖�?─────────
		AddIfMissing(buildArgs, "scu_build", "yes");           // 单编译单元：合并源文件减少编译次�?        AddIfMissing(buildArgs, "fast_unsafe", "yes");         // 隐式缓存 + 最大漂移：加速增量构�?        buildArgs.RemoveAll(a => a.StartsWith("ninja=", StringComparison.OrdinalIgnoreCase));
		AddIfMissing(buildArgs, "fast_unsafe", "yes");
		buildArgs.RemoveAll(a => a.StartsWith("ninja=", StringComparison.OrdinalIgnoreCase));
		buildArgs.Add("ninja=no");

		// 构建缓存目录（用户的项目 gitignore 中应已忽�?.scons_cache�?        var defaultCache = Path.Combine(_repoRoot, ".scons_cache");
		buildArgs.RemoveAll(a => a.StartsWith("cache_path=", StringComparison.OrdinalIgnoreCase));

		// MinGW
		if (_cfg.PlatformName == "windows" && _cfg.UseMinGW)
		{
			buildArgs.Add("use_mingw=yes");
			var mingwPrefix = Environment.GetEnvironmentVariable("MINGW_PREFIX");
			if (!string.IsNullOrWhiteSpace(mingwPrefix))
				buildArgs.Add($"mingw_prefix={mingwPrefix}");
		}

		// Optional Windows deps
		if (_cfg.PlatformName == "windows" && _cfg.EnableWindowsOptionalDeps)
		{
			Report("Checking Windows optional dependencies", "step");
			await InstallWindowsOptionalDepsAsync();
		}
		else if (_cfg.PlatformName == "windows" && !_cfg.EnableWindowsOptionalDeps)
		{
			AddIfMissing(buildArgs, "d3d12", "no");
			AddIfMissing(buildArgs, "accesskit", "no");
			AddIfMissing(buildArgs, "angle", "no");
			Report("Windows optional dependencies disabled: d3d12=no accesskit=no angle=no.", "warning");
			Report("Enable 'Windows Optional Deps' after installing those SDKs.", "warning");
		}

		// MSVC env vars import
		if (_cfg.PlatformName == "windows" && !_cfg.UseMinGW)
		{
			AddIfMissing(buildArgs, "import_env_vars",
				"PATH,INCLUDE,LIB,LIBPATH,VCToolsInstallDir,VCToolsVersion,VSINSTALLDIR,WindowsSdkDir,WindowsSDKLibVersion,UniversalCRTSdkDir,UCRTVersion");
		}

		// MSVC cxxflags
		if (_cfg.PlatformName == "windows" && !_cfg.UseMinGW)
		{
			AddIfMissing(buildArgs, "cxxflags", "/Zm500");
		}

		// SCons persists option values between invocations. Always set the
		// Mono module state explicitly so switching from Mono to a standard
		// build cannot keep producing stale `.mono` product names.
		buildArgs.RemoveAll(a => a.StartsWith("module_mono_enabled=", StringComparison.OrdinalIgnoreCase));
		buildArgs.Add($"module_mono_enabled={(_cfg.Mono ? "yes" : "no")}");

		var argsStr = string.Join(" ", buildArgs);
		Report($"SCons args: {argsStr}", "info");

		// ── Clean stale build artifacts from previous (failed) builds ──
		if (_cfg.CleanBuild)
		{
			Report("Cleaning previous build artifacts", "step");
			var cleanArgs = new List<string>(_sconsPrefix) { "-c" };
			cleanArgs.AddRange(buildArgs.Skip(_sconsPrefix.Count)); // reuse same platform/target/arch args
			try
			{
				var cleanLogPath = Path.Combine(_logRoot, $"{_packageName}-clean.log");
				await RunAndReportInDirAsync(_sconsCmd, _repoRoot, cleanLogPath, false, cleanArgs.ToArray());
				Report("Previous build artifacts cleaned.", "success");
			}
			catch (Exception ex)
			{
				Report($"Clean step warning (non-fatal): {ex.Message}", "warning");
			}
		}
		else
		{
			Report("Incremental build (skip clean) �?enable 'Clean Build' in Advanced tab for full rebuild.", "info");
		}

		// Create log directory
		Directory.CreateDirectory(_logRoot);
		var buildLogPath = Path.Combine(_logRoot, $"{_packageName}-build.log");

		// Run SCons build
		await RunAndReportInDirAsync(_sconsCmd, _repoRoot, buildLogPath, false, buildArgs.ToArray());

		// Mono post-build
		if (_cfg.Mono)
			await RunMonoPostBuildAsync(buildLogPath);
	}

	private async Task RunMonoPostBuildAsync(string buildLogPath)
	{
		Report("Preparing Mono assemblies", "step");

		var binDir = Path.Combine(_repoRoot, "bin");
		var pattern = GetProductPattern(monoBuild: true);
		var regex = new Regex(pattern, RegexOptions.IgnoreCase);
		var monoProducts = Directory.GetFiles(binDir)
			.Select(f => new FileInfo(f))
			.Where(f => regex.IsMatch(f.Name))
			.ToList();

		if (monoProducts.Count == 0)
			throw new Exception($"Mono build completed but no Mono executable matched '{pattern}' in {binDir}.");

		var monoExe = SelectJundotExecutable(monoProducts)!;
		Report($"Using Mono executable: {monoExe.FullName}", "info");

		// The --generate-mono-glue step requires the engine to load project data
		// (a .pck file) which only the editor target provides.  Template builds
		// (template_release / template_debug) use pre-built glue or don't need it.
		if (_actualTarget == "editor")
		{
			Report("Generating Mono glue (editor target)", "step");
			var glueLog = Path.Combine(_logRoot, $"{_packageName}-mono-glue.log");
			await RunAndReportInDirAsync(monoExe.FullName, _repoRoot, glueLog, false,
				new[] { "--headless", "--generate-mono-glue", "./modules/mono/glue" });
		}
		else
		{
			Report($"Skipping --generate-mono-glue for target '{_actualTarget}' (editor-only step).", "info");
		}

		// Build assemblies
		var asmLog = Path.Combine(_logRoot, $"{_packageName}-mono-assemblies.log");
		await RunAndReportInDirAsync(_pythonPath, _repoRoot, asmLog, false,
			new[] { "./modules/mono/build_scripts/build_assemblies.py",
			"--jundot-output-dir=./bin",
			$"--jundot-platform={_cfg.PlatformName}" });
	}

	// ══════════════════════════════════════════════════════════════�?    //  WINDOWS OPTIONAL DEPENDENCIES
	// ══════════════════════════════════════════════════════════════�?
	/// <summary>
	/// When EnableWindowsOptionalDeps is true, auto-detect whether the
	/// accesskit and d3d12 SDKs are installed. If they are missing,
	/// automatically run the corresponding install scripts.
	/// </summary>
	private async Task InstallWindowsOptionalDepsAsync()
	{
		var localAppData = Environment.GetEnvironmentVariable("LOCALAPPDATA")
			?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "AppData", "Local");
		var depsFolder = Path.Combine(localAppData, "Jundot", "build_deps");

		// ── AccessKit ────────────────────────────────────────
		var accesskitFolder = Path.Combine(depsFolder, "accesskit");
		if (!Directory.Exists(accesskitFolder))
		{
			Report("AccessKit SDK not found. Installing...", "warning");
			var script = Path.Combine(_repoRoot, "misc", "scripts", "install_accesskit.py");
			await RunAndReportInDirAsync(_pythonPath, _repoRoot, null, false, new[] { script });
			Report("AccessKit SDK installed successfully.", "success");
		}
		else
		{
			Report("AccessKit SDK already installed.", "info");
		}

		// ── D3D12 SDK ────────────────────────────────────────
		// Check for mesa-* directories (e.g. mesa-x86_64-msvc)
		string[] mesaDirs;
		try { mesaDirs = Directory.GetDirectories(depsFolder, "mesa-*"); }
		catch (DirectoryNotFoundException) { mesaDirs = Array.Empty<string>(); }
		var agilitySdkFolder = Path.Combine(depsFolder, "agility_sdk");

		if (mesaDirs.Length == 0 || !Directory.Exists(agilitySdkFolder))
		{
			Report("D3D12 SDK not found. Installing...", "warning");
			var script = Path.Combine(_repoRoot, "misc", "scripts", "install_d3d12_sdk_windows.py");
			await RunAndReportInDirAsync(_pythonPath, _repoRoot, null, false, new[] { script });
			Report("D3D12 SDK installed successfully.", "success");
		}
		else
		{
			Report("D3D12 SDK already installed.", "info");
		}
	}

	// ══════════════════════════════════════════════════════════════�?    //  PACKAGE
	// ══════════════════════════════════════════════════════════════�?
	private async Task PackageAsync()
	{
		Directory.CreateDirectory(_packageRoot);
		if (_cfg.CleanPackageDir && Directory.Exists(_stagingDir))
			Directory.Delete(_stagingDir, true);
		Directory.CreateDirectory(_stagingDir);

		// Collect products
		Report("Collecting build products", "step");
		var binDir = Path.Combine(_repoRoot, "bin");
		if (!Directory.Exists(binDir))
			throw new Exception("bin/ directory not found. Build first or uncheck Skip Build.");

		var pattern = GetProductPattern(monoBuild: _cfg.Mono);
		var regex = new Regex(pattern, RegexOptions.IgnoreCase);
		var products = Directory.GetFiles(binDir)
			.Select(f => new FileInfo(f))
			.Where(f => regex.IsMatch(f.Name))
			.Where(f => ShouldPackageProduct(f.Name))
			.ToList();

		if (products.Count == 0)
			throw new Exception($"No build products matched '{pattern}' in {binDir}.");

		foreach (var product in products)
		{
			var dest = Path.Combine(_stagingDir, product.Name);
			File.Copy(product.FullName, dest, overwrite: true);
			Report($"  Copied: {product.Name}", "output");
		}

		// Dev build: also copy PDB for crash report symbol resolution
		if (_cfg.Target == "editor.dev")
		{
			foreach (var product in products)
			{
				var pdbName = Path.ChangeExtension(product.Name, ".pdb");
				var pdbPath = Path.Combine(binDir, pdbName);
				if (File.Exists(pdbPath))
				{
					File.Copy(pdbPath, Path.Combine(_stagingDir, pdbName), overwrite: true);
					Report($"  Copied: {pdbName}", "output");
				}
			}
		}

		// Mono: copy JundotSharp
		if (_cfg.Mono)
		{
			var jundotSharpDir = Path.Combine(binDir, "JundotSharp");
			if (!Directory.Exists(jundotSharpDir))
				throw new Exception("Mono package requires JundotSharp/ in bin/. Build assemblies may have failed.");

			CopyDirectory(jundotSharpDir, Path.Combine(_stagingDir, "JundotSharp"));
			Report("  Copied: JundotSharp/", "output");
		}

		await CopyPackageBuilderAsync();
		// Launcher is intentionally not embedded for now.
		await CopyCrashDialogAsync();

		// Write manifest
		var manifestPath = Path.Combine(_stagingDir, "package-manifest.txt");
		var lines = new List<string>
		{
			$"Package: {_packageName}",
			$"Version: {_version}",
			$"Platform: {_cfg.PlatformName}",
			$"Target: {_cfg.Target}",
			$"Arch: {_cfg.Arch}",
			$"Created: {DateTime.Now:O}",
		};

		try
		{
			var commit = await RunAndCaptureAsync("git", "rev-parse", "--short", "HEAD");
			if (!string.IsNullOrWhiteSpace(commit))
				lines.Add($"Commit: {commit.Trim()}");
		}
		catch { /* git may not be available */ }

		lines.Add("");
		lines.Add("Files:");
		lines.AddRange(products.Select(p => $"  {p.Name}"));
		if (_cfg.Mono) lines.Add("  JundotSharp/");
		if (_actualTarget == "editor")
		{
			lines.Add("  Tools/PackageBuilder/");
			lines.Add("  Tools/CrashDialog/");
		}

		File.WriteAllLines(manifestPath, lines, System.Text.Encoding.UTF8);

		// Create zip �?prefer 7-Zip for multi-threaded compression (5-10x faster)
		Report("Creating zip package", "step");
		if (File.Exists(_zipPath))
			File.Delete(_zipPath);

		if (!await TryCompressWith7zAsync(_stagingDir, _zipPath))
		{
			Report("7-Zip not available, falling back to .NET ZipFile (single-threaded).", "warning");
			Report("Install 7-Zip (https://7-zip.org) for faster multi-threaded compression.", "warning");
			System.IO.Compression.ZipFile.CreateFromDirectory(_stagingDir, _zipPath);
		}

		// ── Generate update manifest for hot-update system ─────
		if (_cfg.GenerateUpdateManifest)
		{
			Report("Generating update manifest (update-manifest.json)", "step");
			var updateManifestPath = ManifestGenerator.Generate(_cfg, _version, _packageName, _zipPath, _stagingDir, _repoRoot);
			if (updateManifestPath != null)
			{
				Report($"  Manifest written: {updateManifestPath}", "success");
				Report($"  Manifest also at: {Path.Combine(_packageRoot, "manifest.json")}", "info");
			}
			else
			{
				Report("  Warning: Manifest generation failed (see debug output)", "warning");
			}
		}
	}

	// ══════════════════════════════════════════════════════════════�?    //  HELPERS
	// ══════════════════════════════════════════════════════════════�?
	private async Task CopyPackageBuilderAsync()
	{
		if (_cfg.PlatformName != "windows" || _actualTarget != "editor")
			return;

		var projectPath = Path.Combine(_repoRoot, "tools", "PackageBuilder", "PackageBuilder.csproj");
		if (!File.Exists(projectPath))
		{
			Report("PackageBuilder project was not found; skipping embedded AI package builder.", "warning");
			return;
		}

		var packageBuilderDir = Path.Combine(_stagingDir, "Tools", "PackageBuilder");
		if (Directory.Exists(packageBuilderDir))
			Directory.Delete(packageBuilderDir, true);
		Directory.CreateDirectory(packageBuilderDir);

		Report("Embedding AI package builder", "step");
		var logPath = Path.Combine(_logRoot, $"{_packageName}-package-builder.log");
		await RunAndReportInDirAsync("dotnet", _repoRoot, logPath, true, new[]
		{
			"publish",
			projectPath,
			"-c",
			"Release",
			"-o",
			packageBuilderDir,
			"--self-contained",
			"false",
			"/p:UseAppHost=true"
		});

		Report("  Copied: Tools/PackageBuilder/", "output");
	}

	private async Task CopyLauncherAsync()
	{
		if (_actualTarget != "editor")
			return;

		var projectPath = Path.Combine(_repoRoot, "tools", "Launcher", "Launcher.csproj");
		if (!File.Exists(projectPath))
		{
			Report("Launcher project was not found; skipping JundotLauncher.", "warning");
			return;
		}

		var launcherDir = Path.Combine(_stagingDir, "Tools", "Launcher");
		if (Directory.Exists(launcherDir))
			Directory.Delete(launcherDir, true);
		Directory.CreateDirectory(launcherDir);

		Report("Embedding JundotLauncher", "step");
		var logPath = Path.Combine(_logRoot, $"{_packageName}-launcher.log");
		await RunAndReportInDirAsync("dotnet", _repoRoot, logPath, true, new[]
		{
			"publish",
			projectPath,
			"-c",
			"Release",
			"-o",
			launcherDir,
			"--self-contained",
			"true",
			"/p:UseAppHost=true",
			"-r",
			"win-x64"
		});

		Report("  Copied: Tools/Launcher/", "output");
	}

	private async Task CopyCrashDialogAsync()
	{
		if (_cfg.PlatformName != "windows" || _actualTarget != "editor")
			return;

		var projectPath = Path.Combine(_repoRoot, "tools", "JundotCrashDialog", "JundotCrashDialog.csproj");
		if (!File.Exists(projectPath))
		{
			Report("JundotCrashDialog project was not found; skipping crash dialog.", "warning");
			return;
		}

		var crashDialogDir = Path.Combine(_stagingDir, "Tools", "CrashDialog");
		if (Directory.Exists(crashDialogDir))
			Directory.Delete(crashDialogDir, true);
		Directory.CreateDirectory(crashDialogDir);

		Report("Embedding JundotCrashDialog", "step");
		var logPath = Path.Combine(_logRoot, $"{_packageName}-crash-dialog.log");
		await RunAndReportInDirAsync("dotnet", _repoRoot, logPath, true, new[]
		{
			"publish",
			projectPath,
			"-c",
			"Release",
			"-o",
			crashDialogDir,
			"--self-contained",
			"true",
			"-r",
			"win-x64",
			"/p:UseAppHost=true"
		});

		Report("  Copied: Tools/CrashDialog/", "output");
	}

	private string GetProductPattern(bool monoBuild)
	{
		var platform = Regex.Escape(_cfg.PlatformName);
		// dev_build=yes appends `.dev` to editor binaries. A normal editor
		// package must not silently pick stale editor.dev artifacts from bin/.
		var target = Regex.Escape(_actualTarget);
		if (string.Equals(_cfg.Target, "editor.dev", StringComparison.OrdinalIgnoreCase))
			target += @"\.dev";
		var arch = Regex.Escape(_cfg.Arch);

		if (_cfg.PlatformName == "windows")
		{
			if (monoBuild)
				return $"^jundot\\.{platform}\\.{target}\\.{arch}(?:\\..*)?\\.mono(?:\\..*)?\\.exe$";
			return $"^jundot\\.{platform}\\.{target}\\.{arch}(?!.*\\.mono)(?:\\..+)?\\.exe$";
		}

		if (monoBuild)
			return $"^jundot\\.{platform}\\.{target}\\.{arch}(?:\\..*)?\\.mono(?:\\..*)?$";
		return $"^jundot\\.{platform}\\.{target}\\.{arch}(?:\\..+)?$";
	}

	private bool ShouldPackageProduct(string fileName)
	{
		if (fileName.Contains(".console", StringComparison.OrdinalIgnoreCase))
			return false;

		var isDevArtifact = fileName.Contains(".editor.dev.", StringComparison.OrdinalIgnoreCase);
		var wantsDevArtifact = string.Equals(_cfg.Target, "editor.dev", StringComparison.OrdinalIgnoreCase);
		if (isDevArtifact != wantsDevArtifact && string.Equals(_actualTarget, "editor", StringComparison.OrdinalIgnoreCase))
			return false;

		return true;
	}

	/// <summary>
	/// From a list of candidate Jundot binaries in bin/, pick the single exe
	/// that should actually be run (for --generate-mono-glue / as the primary
	/// binary in the record). Rule:
	///   - Prefer the non-.console, non-.dev GUI executable.
	///   - Next: pick non-.console (may be .dev).
	///   - Fall back to the first in the list.
	/// This fixes cases where the builder previously selected
	/// `jundot.windows.editor.dev.x86_64.mono.console.exe` just because it
	/// existed alongside the GUI editor binary.
	/// </summary>
	private static FileInfo SelectJundotExecutable(List<FileInfo> products)
	{
		if (products == null || products.Count == 0)
			return null!;

		// 1) Prefer GUI editor (not .console, not .dev)
		var guiEditor = products.FirstOrDefault(f =>
			!f.Name.Contains(".console", StringComparison.OrdinalIgnoreCase) &&
			!f.Name.Contains(".dev", StringComparison.OrdinalIgnoreCase));
		if (guiEditor != null) return guiEditor;

		// 2) Fall back to any non-console binary (e.g. editor.dev, template)
		var nonConsole = products.FirstOrDefault(f =>
			!f.Name.Contains(".console", StringComparison.OrdinalIgnoreCase));
		if (nonConsole != null) return nonConsole;

		// 3) Last resort: first in the list
		return products.First();
	}

	private static bool HasArg(string args, string name)
	{
		return Regex.IsMatch(args, $@"(^|\s){Regex.Escape(name)}=");
	}

	private static void AddIfMissing(List<string> args, string name, string value)
	{
		if (!args.Any(a => a.StartsWith($"{name}=")))
			args.Add($"{name}={value}");
	}

	private static void CopyDirectory(string sourceDir, string destDir)
	{
		Directory.CreateDirectory(destDir);
		foreach (var file in Directory.GetFiles(sourceDir))
			File.Copy(file, Path.Combine(destDir, Path.GetFileName(file)), overwrite: true);
		foreach (var dir in Directory.GetDirectories(sourceDir))
			CopyDirectory(dir, Path.Combine(destDir, Path.GetFileName(dir)));
	}

	/// <summary>
	/// Try to compress a directory using 7-Zip with multi-threaded compression.
	/// Returns true if 7-Zip was found and succeeded, false if 7-Zip is not available.
	/// </summary>
	private static async Task<bool> TryCompressWith7zAsync(string sourceDir, string zipPath)
	{
		var sevenZipPaths = new[]
		{
			@"C:\Program Files\7-Zip\7z.exe",
			@"C:\Program Files (x86)\7-Zip\7z.exe",
			"7z" // fallback to PATH
        };

		string? sevenZip = null;
		foreach (var p in sevenZipPaths)
		{
			if (p == "7z")
			{
				var found = FindCommand("7z");
				if (found != null) { sevenZip = "7z"; break; }
			}
			else if (File.Exists(p))
			{
				sevenZip = p;
				break;
			}
		}

		if (sevenZip == null)
			return false;

		try
		{
			// -mmt = multi-threaded, -mx5 = normal compression (good speed/ratio balance)
			var psi = new ProcessStartInfo(sevenZip, $"a -tzip \"{zipPath}\" \"{sourceDir}\\*\" -mx5 -mmt")
			{
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				UseShellExecute = false,
				CreateNoWindow = true
			};
			ConfigureProcessOutput(psi);

			using var p = Process.Start(psi);
			if (p == null) return false;

			await p.WaitForExitAsync();
			return p.ExitCode == 0;
		}
		catch
		{
			return false;
		}
	}

	private static string? FindCommand(string name)
	{
		// Check PATHEXT variants
		var pathExt = Environment.GetEnvironmentVariable("PATHEXT") ?? ".EXE;.CMD;.BAT";
		var extensions = pathExt.Split(';', StringSplitOptions.RemoveEmptyEntries);

		foreach (var dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator))
		{
			foreach (var ext in extensions)
			{
				var fullPath = Path.Combine(dir, name + ext);
				if (File.Exists(fullPath))
					return fullPath;
			}
		}

		// Check as-is (absolute or relative with extension)
		if (File.Exists(name)) return name;

		return null;
	}

	/// <summary>
	/// Test that a command is a real Python interpreter (not the Microsoft Store alias).
	/// The Store alias exits with code 0 but prints a "not found" message instead of a version string.
	/// </summary>
	private static async Task<bool> TestPythonCommandAsync(string cmd)
	{
		try
		{
			var psi = new ProcessStartInfo(cmd, "--version")
			{
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				UseShellExecute = false,
				CreateNoWindow = true
			};
			ConfigureProcessOutput(psi);
			using var p = Process.Start(psi);
			if (p == null) return false;

			var output = await p.StandardOutput.ReadToEndAsync();
			var error = await p.StandardError.ReadToEndAsync();
			await p.WaitForExitAsync();
			var combined = output + error;

			// Store alias prints "Python was not found" / "Microsoft Store" instead of a version string
			if (combined.Contains("Microsoft Store") || combined.Contains("was not found"))
				return false;

			// Real Python prints something like "Python 3.12.0"
			return p.ExitCode == 0 && combined.Contains("Python");
		}
		catch
		{
			return false;
		}
	}

	private static async Task<bool> TestCommandAsync(string cmd, params string[] args)
	{
		try
		{
			var psi = new ProcessStartInfo(cmd, string.Join(" ", args))
			{
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				UseShellExecute = false,
				CreateNoWindow = true
			};
			ConfigureProcessOutput(psi);
			using var p = Process.Start(psi);
			if (p == null) return false;
			await p.WaitForExitAsync();
			return p.ExitCode == 0;
		}
		catch
		{
			return false;
		}
	}

	private static async Task<string> RunAndCaptureAsync(string cmd, params string[] args)
	{
		var psi = new ProcessStartInfo(cmd, string.Join(" ", args))
		{
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			UseShellExecute = false,
			CreateNoWindow = true
		};
		ConfigureProcessOutput(psi);
		using var p = Process.Start(psi);
		if (p == null) throw new Exception($"Failed to start: {cmd}");
		var output = await p.StandardOutput.ReadToEndAsync();
		await p.WaitForExitAsync();
		return output.Trim();
	}

	private async Task RunAndReportAsync(string cmd, params string[] args)
	{
		// Overload 1: no log path
		await RunAndReportWithLogAsync(cmd, null, false, args);
	}

	private async Task RunAndReportInDirAsync(string cmd, string workingDirectory, string? logPath, bool forceUtf8 = false, string[]? args = null)
	{
		var argsList = args ?? Array.Empty<string>();
		var psi = new ProcessStartInfo(cmd, string.Join(" ", argsList))
		{
			WorkingDirectory = workingDirectory,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			UseShellExecute = false,
			CreateNoWindow = true
		};

		ConfigureProcessOutput(psi, forceUtf8);
		await RunProcessAndReportAsync(psi, logPath);
	}

	private async Task RunAndReportWithLogAsync(string cmd, string? logPath, bool forceUtf8 = false, params string[] args)
	{
		var psi = new ProcessStartInfo(cmd, string.Join(" ", args))
		{
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			UseShellExecute = false,
			CreateNoWindow = true
		};

		ConfigureProcessOutput(psi, forceUtf8);
		await RunProcessAndReportAsync(psi, logPath);
	}

	private async Task RunProcessAndReportAsync(ProcessStartInfo psi, string? logPath)
	{
		// Note: ConfigureProcessOutput should be called by the caller before this method.
		// This method assumes the encoding is already configured.

		_currentProcess = Process.Start(psi);
		if (_currentProcess == null)
			throw new Exception($"Failed to start: {psi.FileName}");

		StreamWriter? logWriter = null;
		if (logPath != null)
		{
			Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
			logWriter = new StreamWriter(logPath, append: false, Encoding.UTF8);
		}

		try
		{
			// Read stdout and stderr as console records. Ninja/SCons often update
			// progress with '\r' instead of '\n', so ReadLineAsync can hide output
			// until the process exits or is killed.
			var stdoutTask = ReadConsoleOutputAsync(_currentProcess.StandardOutput, "output", logWriter);
			var stderrTask = ReadConsoleOutputAsync(_currentProcess.StandardError, "warning", logWriter);

			await Task.WhenAll(stdoutTask, stderrTask);
			await _currentProcess.WaitForExitAsync(_ct);
		}
		finally
		{
			logWriter?.Dispose();
		}

		if (_currentProcess.ExitCode != 0)
		{
			var logInfo = logPath != null ? $"\nBuild log: {logPath}" : "";
			throw new Exception($"Process exited with code {_currentProcess.ExitCode}.{logInfo}");
		}
	}

	private static void ConfigureProcessOutput(ProcessStartInfo psi, bool forceUtf8 = false)
	{
		// Use the system's active ANSI code page (e.g. GBK/936 on Chinese Windows)
		// so that native tools like MSVC/cl.exe output Chinese correctly.
		// Falls back to UTF-8 on non-Windows, if the system codepage is unavailable,
		// or if forceUtf8 is true (for .NET tools that always output UTF-8).
		if (forceUtf8)
		{
			psi.StandardOutputEncoding = Encoding.UTF8;
			psi.StandardErrorEncoding = Encoding.UTF8;
		}
		else
		{
			try
			{
				var sysEncoding = Encoding.GetEncoding(0);
				psi.StandardOutputEncoding = sysEncoding;
				psi.StandardErrorEncoding = sysEncoding;
			}
			catch
			{
				psi.StandardOutputEncoding = Encoding.UTF8;
				psi.StandardErrorEncoding = Encoding.UTF8;
			}
		}

		// Do NOT set PYTHONUTF8/PYTHONIOENCODING here.
		// Let Python use its system default encoding (GBK on Chinese Windows),
		// which matches the ANSI code page set above. MSVC/cl.exe also outputs
		// GBK, so the whole pipeline stays consistent:
		//   MSVC(GBK) �?scons/Python(GBK) �?C# stdout reader(GBK) �?UI(Unicode)
		// Force Python unbuffered output so Ninja/SCons daemon output is visible
		// in real-time instead of being buffered until process exit.
		psi.Environment["PYTHONUNBUFFERED"] = "1";
		psi.Environment["DOTNET_CLI_UI_LANGUAGE"] = "zh-CN";
	}

	private async Task ReadConsoleOutputAsync(StreamReader reader, string msgType, StreamWriter? logWriter)
	{
		var ninjaProgress = new Regex(@"^\[(\d+)/(\d+)\].*$");
		var buffer = new StringBuilder();
		var readBuffer = new char[4096];
		string? lastPartialReport = null;

		try
		{
			while (true)
			{
				_ct.ThrowIfCancellationRequested();
				var read = await reader.ReadAsync(readBuffer, _ct);
				if (read == 0)
					break;

				for (var i = 0; i < read; i++)
				{
					var ch = readBuffer[i];
					if (ch == '\r' || ch == '\n')
					{
						var line = buffer.ToString();
						buffer.Clear();

						if (line.Length > 0)
						{
							WriteLogLine(logWriter, line);
							if (!string.Equals(line, lastPartialReport, StringComparison.Ordinal))
								ReportConsoleLine(line, msgType, ninjaProgress);
						}

						lastPartialReport = null;

						if (ch == '\r' && i + 1 < read && readBuffer[i + 1] == '\n')
							i++;
					}
					else
					{
						buffer.Append(ch);
					}
				}

				if (buffer.Length > 0)
				{
					var partial = buffer.ToString();
					if (ShouldReportPartialConsoleLine(partial) &&
						!string.Equals(partial, lastPartialReport, StringComparison.Ordinal))
					{
						ReportConsoleLine(partial, msgType, ninjaProgress);
						lastPartialReport = partial;
					}
				}
			}

			if (buffer.Length > 0)
			{
				var line = buffer.ToString();
				WriteLogLine(logWriter, line);
				if (!string.Equals(line, lastPartialReport, StringComparison.Ordinal))
					ReportConsoleLine(line, msgType, ninjaProgress);
			}
		}
		catch (OperationCanceledException) { throw; }
		catch (Exception ex)
		{
			Report($"Stream read error: {ex.Message}", "error");
		}
	}

	private void WriteLogLine(StreamWriter? logWriter, string line)
	{
		if (logWriter == null)
			return;

		lock (_logLock)
		{
			logWriter.WriteLine(line);
			logWriter.Flush();
		}
	}

	private void ReportConsoleLine(string line, string msgType, Regex ninjaProgress)
	{
		var match = ninjaProgress.Match(line);
		if (match.Success)
		{
			var current = double.Parse(match.Groups[1].Value);
			var total = double.Parse(match.Groups[2].Value);
			_ninjaTotal = total;
			_ninjaCurrent = current;

			Report(line, msgType, current / total);
			return;
		}

		Report(line, msgType);
	}

	private static bool ShouldReportPartialConsoleLine(string line)
	{
		return line.StartsWith("[", StringComparison.Ordinal) ||
			   line.Contains("Starting scons daemon", StringComparison.OrdinalIgnoreCase);
	}

	private async Task ReadLinesAsync(StreamReader reader, string msgType, StreamWriter? logWriter)
	{
		await ReadConsoleOutputAsync(reader, msgType, logWriter);
		if (reader.GetType() != null)
			return;

		// 匹配 Ninja �?[N/M] 进度前缀
		var ninjaProgress = new Regex(@"^\[(\d+)/(\d+)\].*$");

		try
		{
			while (true)
			{
				_ct.ThrowIfCancellationRequested();
				var line = await reader.ReadLineAsync();
				if (line == null) break;

				logWriter?.WriteLine(line);

				// 解析 Ninja 编译进度 [N/M]
				var match = ninjaProgress.Match(line);
				if (match.Success)
				{
					var current = double.Parse(match.Groups[1].Value);
					var total = double.Parse(match.Groups[2].Value);
					_ninjaTotal = total;
					_ninjaCurrent = current;

					Report(line, msgType, current / total);
					continue;
				}

				Report(line, msgType);
			}
		}
		catch (OperationCanceledException) { throw; }
		catch (Exception ex)
		{
			Report($"Stream read error: {ex.Message}", "error");
		}
	}

	// ══════════════════════════════════════════════════════════════�?    //  BUILD HISTORY
	// ══════════════════════════════════════════════════════════════�?
	private void SaveBuildRecord()
	{
		if (BuildManager == null) return;

		try
		{
			// Find the main exe in bin/
			var binDir = Path.Combine(_repoRoot, "bin");
			string exePath = "";
			if (Directory.Exists(binDir))
			{
				var pattern = GetProductPattern(monoBuild: _cfg.Mono);
				var regex = new Regex(pattern, RegexOptions.IgnoreCase);
				var exes = Directory.GetFiles(binDir, "*.exe")
					.Where(f => regex.IsMatch(Path.GetFileName(f)))
					.Where(f => ShouldPackageProduct(Path.GetFileName(f)))
					.ToList();

				// Prefer non-console, non-dev GUI exe (matches the actual
				// Jundot editor the user will run); fall back to any
				// non-console binary, then any file.
				var sortedExes = exes
					.OrderBy(f => f.Contains(".console") ? 2 : (f.Contains(".dev") ? 1 : 0))
					.ToList();
				var mainExe = sortedExes.FirstOrDefault();
				if (mainExe != null) exePath = mainExe;
			}

			var buildLogPath = Path.Combine(_logRoot, $"{_packageName}-build.log");
			if (!File.Exists(buildLogPath))
				buildLogPath = "";

			var record = new BuildRecord
			{
				PackageName = _packageName,
				Version = _version,
				Platform = _cfg.PlatformName,
				Target = _cfg.Target,
				Arch = _cfg.Arch,
				Mono = _cfg.Mono,
				ExePath = exePath,
				PackageDir = _stagingDir,
				ZipPath = _zipPath,
				BuildLogPath = buildLogPath,
				CreatedAt = DateTime.Now,
			};

			BuildManager.SaveRecord(record);
		}
		catch { /* best effort �?don't fail the build over history recording */ }
	}

	private void Report(string message, string type)
	{
		ProgressChanged?.Invoke(this, new BuildProgressEventArgs
		{
			Message = message,
			MessageType = type
		});
	}

	private void Report(string message, string type, double progress)
	{
		ProgressChanged?.Invoke(this, new BuildProgressEventArgs
		{
			Message = message,
			MessageType = type,
			Progress = progress
		});
	}
}
