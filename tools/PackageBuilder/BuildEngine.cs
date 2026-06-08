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
    public string Target       { get; set; } = "editor";          // editor | template_release | template_debug
    public string PlatformName { get; set; } = "windows";         // windows | linuxbsd | macos | android | ios | web
    public string Arch         { get; set; } = "x86_64";          // x86_64 | x86_32 | arm64

    // ── Paths ───────────────────────────────────────────────────
    public string RepoRoot     { get; set; } = "";                // Jundot source root (auto-detected if empty)
    public string OutputDir    { get; set; } = "artifacts/packages";
    public string LogDir       { get; set; } = "artifacts/logs";

    // ── Package naming ──────────────────────────────────────────
    public string PackageName  { get; set; } = "";                // empty = auto-generate

    // ── Build control ───────────────────────────────────────────
    public int    Jobs         { get; set; } = 0;                 // 0 = Environment.ProcessorCount - 1
    public bool   SkipBuild    { get; set; } = false;
    public bool   InstallSCons { get; set; } = false;
    public bool   Mono         { get; set; } = false;
    public bool   UseMinGW     { get; set; } = false;
    public string MingwPrefix  { get; set; } = "";

    /// <summary>Enable d3d12, accesskit, angle on Windows.</summary>
    public bool   EnableWindowsOptionalDeps { get; set; } = false;

    /// <summary>Remove staging dir before copying.</summary>
    public bool   CleanPackageDir { get; set; } = false;

    /// <summary>Extra key=value args forwarded to SCons.</summary>
    public string ExtraSConsArgs { get; set; } = "";

    /// <summary>Editor UI language locale (e.g. zh_CN, en). Defaults to Chinese.</summary>
    public string Language { get; set; } = "zh_CN";

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
    public string Message   { get; init; } = "";
    public string MessageType { get; init; } = "info";  // info | step | warning | error | success | output
}

/// <summary>
/// Core build engine — translates scripts/package-jundot.ps1 logic into C#.
/// </summary>
public class BuildEngine
{
    public event EventHandler<BuildProgressEventArgs>? ProgressChanged;

    private readonly BuildConfig _cfg;
    private CancellationToken    _ct;
    private string               _repoRoot = "";
    private string               _packageRoot = "";
    private string               _logRoot = "";
    private string               _stagingDir = "";
    private string               _zipPath = "";
    private string               _version = "";
    private string               _packageName = "";

    private string _pythonPath = "";
    private string _sconsCmd   = "";
    private List<string> _sconsPrefix = new();

    private Process? _currentProcess;

    /// <summary>Optional — set to enable automatic build history recording.</summary>
    public BuildManager? BuildManager { get; set; }

    public BuildConfig Config => _cfg;
    public string Version => _version;
    public string PackageName => _packageName;

    public BuildEngine(BuildConfig cfg)
    {
        _cfg = cfg;
    }

    // ═══════════════════════════════════════════════════════════════
    //  PUBLIC API
    // ═══════════════════════════════════════════════════════════════

    public async Task<bool> RunAsync(CancellationToken ct)
    {
        _ct = ct;

        try
        {
            // ── 1. Resolve paths ────────────────────────────
            ResolvePaths();

            // ── 2. Read version ─────────────────────────────
            _version = GetJundotVersion();
            Report($"Jundot version: {_version}", "info");

            // ── 3. Generate package name ────────────────────
            _packageName = BuildPackageName();
            Report($"Package name: {_packageName}", "info");

            _packageRoot = Path.GetFullPath(Path.Combine(_repoRoot, _cfg.OutputDir));
            _logRoot     = Path.GetFullPath(Path.Combine(_repoRoot, _cfg.LogDir));
            _stagingDir  = Path.Combine(_packageRoot, _packageName);
            _zipPath     = Path.Combine(_packageRoot, $"{_packageName}.zip");

            // ── 4. Build (unless skipped) ───────────────────
            if (!_cfg.SkipBuild)
            {
                Report("", "step");
                Report("Checking build tools", "step");

                await FindPythonAsync();
                await FindSConsRunnerAsync();
                await AssertWindowsCompilerAsync();
                await CheckMonoToolsAsync();

                Report("", "step");
                Report("Building Jundot", "step");
                await BuildAsync();
            }
            else
            {
                Report("Skipping build — using existing files in bin/", "step");
            }

            // ── 5. Package ──────────────────────────────────
            Report("", "step");
            Report("Preparing package folders", "step");
            await PackageAsync();

            Report("", "success");
            Report($"Package created: {_zipPath}", "success");

            // ── 6. Apply language preset ─────────────────────
            ApplyLanguagePreset();

            // ── 7. Auto-update version ──────────────────────
            if (_cfg.AutoUpdateVersion)
            {
                var newVer = UpdateVersionFile();
                Report($"Version auto-updated for next build: {_version} -> {newVer}", "success");
                Report($"Version auto-updated: {_version} → {newVer}", "success");
            }

            // ── 8. Record build history ─────────────────────
            if (!_cfg.AutoUpdateVersion)
                Report("Auto-update version is disabled; version.py was not changed.", "info");

            SaveBuildRecord();

            return true;
        }
        catch (OperationCanceledException)
        {
            Report("Build cancelled by user.", "warning");
            return false;
        }
        catch (Exception ex)
        {
            Report($"ERROR: {ex.Message}", "error");
            return false;
        }
    }

    /// <summary>Stop the currently running child process.</summary>
    public void Cancel()
    {
        try { _currentProcess?.Kill(entireProcessTree: true); } catch { /* best effort */ }
    }

    // ═══════════════════════════════════════════════════════════════
    //  PATH RESOLUTION
    // ═══════════════════════════════════════════════════════════════

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

    // ═══════════════════════════════════════════════════════════════
    //  VERSION
    // ═══════════════════════════════════════════════════════════════

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

    // ═══════════════════════════════════════════════════════════════
    //  TOOL DETECTION
    // ═══════════════════════════════════════════════════════════════

    private async Task FindPythonAsync()
    {
        // On Windows, prefer "py" launcher — it is never the Store alias.
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

    // ═══════════════════════════════════════════════════════════════
    //  WINDOWS COMPILER DETECTION
    // ═══════════════════════════════════════════════════════════════

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

    // ═══════════════════════════════════════════════════════════════
    //  BUILD
    // ═══════════════════════════════════════════════════════════════

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
            jobs = Math.Max(1, Environment.ProcessorCount - 1);
            if (_cfg.PlatformName == "windows" && _cfg.Target == "editor")
                jobs = Math.Min(jobs, 4);
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

        // Build args
        var buildArgs = new List<string>(_sconsPrefix)
        {
            $"platform={_cfg.PlatformName}",
            $"target={_cfg.Target}",
            $"arch={_cfg.Arch}",
            "debug_symbols=no",
            $"-j{jobs}"
        };
        buildArgs.AddRange(extraArgs);

        // Extra SCons args from UI
        if (!string.IsNullOrWhiteSpace(_cfg.ExtraSConsArgs))
        {
            buildArgs.AddRange(_cfg.ExtraSConsArgs.Split(' ', StringSplitOptions.RemoveEmptyEntries));
        }

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
            AddIfMissing(buildArgs, "cxxflags", "/Zm200");
        }

        // Mono
        if (_cfg.Mono)
        {
            AddIfMissing(buildArgs, "module_mono_enabled", "yes");
        }

        var argsStr = string.Join(" ", buildArgs);
        Report($"SCons args: {argsStr}", "info");

        // Create log directory
        Directory.CreateDirectory(_logRoot);
        var buildLogPath = Path.Combine(_logRoot, $"{_packageName}-build.log");

        // Run SCons build
        await RunAndReportInDirAsync(_sconsCmd, _repoRoot, buildLogPath, buildArgs.ToArray());

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

        // Generate mono glue
        var glueLog = Path.Combine(_logRoot, $"{_packageName}-mono-glue.log");
        await RunAndReportInDirAsync(monoExe.FullName, _repoRoot, glueLog,
            new[] { "--headless", "--generate-mono-glue", "./modules/mono/glue" });

        // Build assemblies
        var asmLog = Path.Combine(_logRoot, $"{_packageName}-mono-assemblies.log");
        await RunAndReportInDirAsync(_pythonPath, _repoRoot, asmLog,
            new[] { "./modules/mono/build_scripts/build_assemblies.py",
            "--jundot-output-dir=./bin",
            $"--jundot-platform={_cfg.PlatformName}" });
    }

    // ═══════════════════════════════════════════════════════════════
    //  WINDOWS OPTIONAL DEPENDENCIES
    // ═══════════════════════════════════════════════════════════════

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
            await RunAndReportInDirAsync(_pythonPath, _repoRoot, null, new[] { script });
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
            await RunAndReportInDirAsync(_pythonPath, _repoRoot, null, new[] { script });
            Report("D3D12 SDK installed successfully.", "success");
        }
        else
        {
            Report("D3D12 SDK already installed.", "info");
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  PACKAGE
    // ═══════════════════════════════════════════════════════════════

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
            .ToList();

        if (products.Count == 0)
            throw new Exception($"No build products matched '{pattern}' in {binDir}.");

        foreach (var product in products)
        {
            var dest = Path.Combine(_stagingDir, product.Name);
            File.Copy(product.FullName, dest, overwrite: true);
            Report($"  Copied: {product.Name}", "output");
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
        if (_cfg.PlatformName == "windows" && _cfg.Target == "editor") lines.Add("  Tools/PackageBuilder/");

        File.WriteAllLines(manifestPath, lines, System.Text.Encoding.UTF8);

        // Create zip
        Report("Creating zip package", "step");
        if (File.Exists(_zipPath))
            File.Delete(_zipPath);

        System.IO.Compression.ZipFile.CreateFromDirectory(_stagingDir, _zipPath);

        // ── Generate update manifest for hot-update system ─────
        if (_cfg.GenerateUpdateManifest)
        {
            Report("Generating update manifest (update-manifest.json)", "step");
            var updateManifestPath = ManifestGenerator.Generate(_cfg, _version, _packageName, _zipPath, _stagingDir, _repoRoot);
            if (updateManifestPath != null)
            {
                Report($"  Manifest written: {updateManifestPath}", "success");
                Report($"  Manifest also at: {Path.Combine(_packageRoot, $"{_packageName}-manifest.json")}", "info");
            }
            else
            {
                Report("  Warning: Manifest generation failed (see debug output)", "warning");
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  HELPERS
    // ═══════════════════════════════════════════════════════════════

    private async Task CopyPackageBuilderAsync()
    {
        if (_cfg.PlatformName != "windows" || _cfg.Target != "editor")
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
        await RunAndReportInDirAsync("dotnet", _repoRoot, logPath, new[]
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

    private string GetProductPattern(bool monoBuild)
    {
        var platform = Regex.Escape(_cfg.PlatformName);
        var target = Regex.Escape(_cfg.Target);
        var arch = Regex.Escape(_cfg.Arch);

        if (_cfg.PlatformName == "windows")
        {
            if (monoBuild)
                return $"^jundot\\.{platform}\\.{target}\\.{arch}(\\..*)?\\.mono(\\..*)?\\.exe$";
            return $"^jundot\\.{platform}\\.{target}\\.{arch}(?!.*\\.mono)(\\..+)?\\.exe$";
        }

        if (monoBuild)
            return $"^jundot\\.{platform}\\.{target}\\.{arch}(\\..*)?\\.mono(\\..*)?$";
        return $"^jundot\\.{platform}\\.{target}\\.{arch}(\\..+)?$";
    }

    private static FileInfo SelectJundotExecutable(List<FileInfo> products)
    {
        var consoleExe = products.FirstOrDefault(f => f.Name.Contains(".console.exe"));
        return consoleExe ?? products.First();
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
        await RunAndReportWithLogAsync(cmd, null, args);
    }

    private async Task RunAndReportInDirAsync(string cmd, string workingDirectory, string? logPath, string[] args)
    {
        var psi = new ProcessStartInfo(cmd, string.Join(" ", args))
        {
            WorkingDirectory = workingDirectory,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };

        await RunProcessAndReportAsync(psi, logPath);
    }

    private async Task RunAndReportWithLogAsync(string cmd, string? logPath, params string[] args)
    {
        var psi = new ProcessStartInfo(cmd, string.Join(" ", args))
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };

        await RunProcessAndReportAsync(psi, logPath);
    }

    private async Task RunProcessAndReportAsync(ProcessStartInfo psi, string? logPath)
    {
        ConfigureProcessOutput(psi);

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
            // Read stdout and stderr line by line
            var stdoutTask = ReadLinesAsync(_currentProcess.StandardOutput, "output", logWriter);
            var stderrTask = ReadLinesAsync(_currentProcess.StandardError, "warning", logWriter);

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

    private static void ConfigureProcessOutput(ProcessStartInfo psi)
    {
        psi.StandardOutputEncoding = Encoding.UTF8;
        psi.StandardErrorEncoding = Encoding.UTF8;
        psi.Environment["PYTHONUTF8"] = "1";
        psi.Environment["PYTHONIOENCODING"] = "utf-8";
        psi.Environment["DOTNET_CLI_UI_LANGUAGE"] = "zh-CN";
    }

    private async Task ReadLinesAsync(StreamReader reader, string msgType, StreamWriter? logWriter)
    {
        try
        {
            while (true)
            {
                _ct.ThrowIfCancellationRequested();
                var line = await reader.ReadLineAsync();
                if (line == null) break;

                logWriter?.WriteLine(line);
                Report(line, msgType);
            }
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex)
        {
            Report($"Stream read error: {ex.Message}", "error");
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  BUILD HISTORY
    // ═══════════════════════════════════════════════════════════════

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
                    .ToList();

                // Prefer non-console exe, then any
                var mainExe = exes.FirstOrDefault(f => !f.Contains(".console")) ?? exes.FirstOrDefault();
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
                Language = NormalizeEditorLanguage(_cfg.Language),
                CreatedAt = DateTime.Now,
            };

            BuildManager.SaveRecord(record);
        }
        catch { /* best effort — don't fail the build over history recording */ }
    }

    /// <summary>
    /// Write editor settings to pre-set the UI language in the built Jundot editor.
    /// Creates _sc_ (self-contained mode marker) and editor_data/editor_settings-4.tres
    /// both in bin/ and in the staging package directory.
    /// </summary>
    public static string NormalizeEditorLanguage(string language)
    {
        var normalized = (language ?? "").Trim().Replace('-', '_');

        return normalized switch
        {
            "" => "",
            "zh" or "zh_CN" or "zh_SG" or "zh_Hans_CN" or "zh_Hans_SG" => "zh_Hans",
            "zh_TW" or "zh_HK" or "zh_MO" or "zh_Hant_TW" or "zh_Hant_HK" or "zh_Hant_MO" => "zh_Hant",
            _ => normalized
        };
    }

    private void ApplyLanguagePreset()
    {
        var editorLanguage = NormalizeEditorLanguage(_cfg.Language);
        if (string.IsNullOrEmpty(editorLanguage) || editorLanguage == "en")
            return; // English is the default — no preset needed

        try
        {
            var settingsContent = $@"[gd_resource type=""EditorSettings"" format=3]

[resource]
interface/editor/localization/editor_language = ""{editorLanguage}""
interface/editor/editor_language = ""{editorLanguage}""
";

            // Write to bin/ so the editor starts with the selected language
            var binDir = Path.Combine(_repoRoot, "bin");
            if (Directory.Exists(binDir))
            {
                // Self-contained mode marker
                File.WriteAllText(Path.Combine(binDir, "_sc_"), "");

                // Editor settings
                var editorDataDir = Path.Combine(binDir, "editor_data");
                Directory.CreateDirectory(editorDataDir);
                File.WriteAllText(Path.Combine(editorDataDir, "editor_settings-4.tres"), settingsContent);
            }

            // Also write to staging package dir
            if (Directory.Exists(_stagingDir))
            {
                File.WriteAllText(Path.Combine(_stagingDir, "_sc_"), "");
                var editorDataDir = Path.Combine(_stagingDir, "editor_data");
                Directory.CreateDirectory(editorDataDir);
                File.WriteAllText(Path.Combine(editorDataDir, "editor_settings-4.tres"), settingsContent);
            }

            Report($"Editor language preset: {editorLanguage}", "info");
        }
        catch (Exception ex)
        {
            Report($"Warning: Failed to write language preset: {ex.Message}", "warning");
        }
    }

    private void Report(string message, string type)
    {
        ProgressChanged?.Invoke(this, new BuildProgressEventArgs
        {
            Message = message,
            MessageType = type
        });
    }
}
