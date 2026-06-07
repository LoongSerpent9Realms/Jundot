using System.Text.Json;
using System.Text.RegularExpressions;

namespace GodotPackageBuilder;

/// <summary>
/// Discovers, tracks, and manages Godot builds across bin/ and artifacts/.
/// Maintains a persistent history in artifacts/packages/.build-history.json.
/// </summary>
public class BuildManager
{
    private readonly string _repoRoot;
    private readonly string _historyPath;
    private readonly string _binDir;
    private readonly string _packagesDir;
    private readonly string _logsDir;
    private BuildHistory _history = new();

    public BuildManager(string repoRoot)
    {
        _repoRoot = repoRoot;
        _binDir = Path.Combine(repoRoot, "bin");
        _packagesDir = Path.Combine(repoRoot, "artifacts", "packages");
        _logsDir = Path.Combine(repoRoot, "artifacts", "logs");
        _historyPath = Path.Combine(_packagesDir, ".build-history.json");
        LoadHistory();
    }

    // ═══════════════════════════════════════════════════════════════
    //  PUBLIC API
    // ═══════════════════════════════════════════════════════════════

    /// <summary>Scan disk and merge with history. Returns all known builds, newest first.</summary>
    public List<BuildRecord> GetAllBuilds()
    {
        var discovered = new List<BuildRecord>();

        // 1. Scan bin/ for raw exes
        if (Directory.Exists(_binDir))
            discovered.AddRange(ScanBinDirectory());

        // 2. Scan artifacts/packages/ for packaged builds
        if (Directory.Exists(_packagesDir))
            discovered.AddRange(ScanPackagesDirectory());

        discovered = discovered
            .Where(r => string.IsNullOrEmpty(r.ExePath) || !IsHiddenExePath(r.ExePath))
            .ToList();

        // 3. Merge with history: prefer disk records, keep historical records that reference existing files
        var merged = MergeWithHistory(discovered);

        // 4. Sort by CreatedAt descending, newest first
        merged.Sort((a, b) => b.CreatedAt.CompareTo(a.CreatedAt));

        return merged;
    }

    /// <summary>Save a build record to history (called after successful build).</summary>
    public void SaveRecord(BuildRecord record)
    {
        if (!string.IsNullOrEmpty(record.ExePath))
            _history.HiddenExePaths.RemoveAll(p => SamePath(p, record.ExePath));

        // Remove any existing record with the same Id or same ExePath
        _history.Records.RemoveAll(r => r.Id == record.Id || (!string.IsNullOrEmpty(r.ExePath) && r.ExePath == record.ExePath));
        _history.Records.Insert(0, record);
        _history.LastScanned = DateTime.Now;
        SaveHistory();
    }

    /// <summary>Delete a build: removes staging dir, zip, logs, and history entry.</summary>
    public void DeleteBuild(BuildRecord record, bool keepExe = false)
    {
        var exePath = record.ExePath;

        // Delete staging directory
        if (!string.IsNullOrEmpty(record.PackageDir) && Directory.Exists(record.PackageDir))
        {
            try { Directory.Delete(record.PackageDir, true); } catch { }
        }

        // Delete zip
        if (!string.IsNullOrEmpty(record.ZipPath) && File.Exists(record.ZipPath))
        {
            try { File.Delete(record.ZipPath); } catch { }
        }

        // Delete logs
        if (!string.IsNullOrEmpty(record.BuildLogPath) && File.Exists(record.BuildLogPath))
        {
            try { File.Delete(record.BuildLogPath); } catch { }
        }

        // Also delete companion logs (mono-glue, mono-assemblies)
        var logDir = Path.GetDirectoryName(record.BuildLogPath);
        var logBase = Path.GetFileNameWithoutExtension(record.BuildLogPath);
        if (!string.IsNullOrEmpty(logDir) && !string.IsNullOrEmpty(logBase) && Directory.Exists(logDir))
        {
            foreach (var f in Directory.GetFiles(logDir, logBase + "*"))
            {
                try { File.Delete(f); } catch { }
            }
        }

        if (keepExe && string.IsNullOrEmpty(record.PackageDir) && !string.IsNullOrEmpty(exePath) && File.Exists(exePath) && !IsHiddenExePath(exePath))
            _history.HiddenExePaths.Add(NormalizePath(exePath));

        // Remove from history
        _history.Records.RemoveAll(r => r.Id == record.Id || (!string.IsNullOrEmpty(r.ExePath) && SamePath(r.ExePath, exePath)));
        SaveHistory();
    }

    /// <summary>Find the most recent build matching the current config.</summary>
    public BuildRecord? FindMatchingBuild(string platform, string target, string arch, bool mono)
    {
        return GetAllBuilds().FirstOrDefault(b =>
            b.Platform == platform &&
            b.Target == target &&
            b.Arch == arch &&
            b.Mono == mono &&
            b.ExeExists);
    }

    // ═══════════════════════════════════════════════════════════════
    //  SCANNING
    // ═══════════════════════════════════════════════════════════════

    private List<BuildRecord> ScanBinDirectory()
    {
        var records = new List<BuildRecord>();
        var seenKeys = new HashSet<string>(); // dedupe by platform+target+arch+mono

        foreach (var exePath in Directory.GetFiles(_binDir, "godot.*.exe"))
        {
            var fileName = Path.GetFileName(exePath);

            // Skip .console.exe variants — they are just console-flavored copies of the same editor
            if (fileName.Contains(".console.") || fileName.EndsWith(".console.exe"))
                continue;

            var parsed = ParseExeFileName(fileName);
            if (parsed == null) continue;

            // Dedupe: only keep first exe per unique (platform, target, arch, mono) combo
            var key = $"{parsed.Value.Platform}|{parsed.Value.Target}|{parsed.Value.Arch}|{parsed.Value.Mono}";
            if (!seenKeys.Add(key)) continue;

            var record = new BuildRecord
            {
                Platform = parsed.Value.Platform,
                Target = parsed.Value.Target,
                Arch = parsed.Value.Arch,
                Mono = parsed.Value.Mono,
                ExePath = exePath,
                CreatedAt = File.GetCreationTime(exePath),
            };

            // Try to find matching log
            var matchingLogs = FindMatchingLogs(record.Platform, record.Target, record.Arch, record.Mono);
            if (matchingLogs.Count > 0)
            {
                var bestLog = matchingLogs.OrderByDescending(File.GetCreationTime).First();
                record.BuildLogPath = bestLog;

                var logName = Path.GetFileNameWithoutExtension(bestLog);
                var versionMatch = Regex.Match(logName, @"^godot-([^-]+)-");
                if (versionMatch.Success)
                    record.Version = versionMatch.Groups[1].Value;

                var tsMatch = Regex.Match(logName, @"(\d{8}-\d{6})$");
                if (tsMatch.Success && DateTime.TryParseExact(tsMatch.Groups[1].Value, "yyyyMMdd-HHmmss", null,
                        System.Globalization.DateTimeStyles.None, out var ts))
                    record.CreatedAt = ts;

                record.PackageName = logName;
            }

            // Fallback: read version from version.py
            if (string.IsNullOrEmpty(record.Version) || record.Version == "?")
                record.Version = ReadVersionFromPy() ?? "?";

            if (string.IsNullOrEmpty(record.PackageName))
                record.PackageName = $"godot-{record.Version}-{record.Platform}-{record.Target}-{record.Arch}";

            records.Add(record);
        }

        return records;
    }

    /// <summary>Read version string from version.py in the repo root.</summary>
    private string? ReadVersionFromPy()
    {
        var vf = Path.Combine(_repoRoot, "version.py");
        if (!File.Exists(vf)) return null;

        try
        {
            var major = "0"; var minor = "0"; var patch = "0"; var status = "";
            foreach (var line in File.ReadLines(vf))
            {
                var m = Regex.Match(line, @"^\s*([A-Za-z_]+)\s*=\s*""?([^""\n]+)""?\s*$");
                if (!m.Success) continue;
                switch (m.Groups[1].Value)
                {
                    case "major": major = m.Groups[2].Value; break;
                    case "minor": minor = m.Groups[2].Value; break;
                    case "patch": patch = m.Groups[2].Value; break;
                    case "status": status = m.Groups[2].Value; break;
                }
            }
            return FormatGodotVersion(major, minor, patch, status);
        }
        catch { return null; }
    }

    private static string FormatGodotVersion(string major, string minor, string patch, string status)
    {
        var version = $"{major}.{minor}";
        if (!string.IsNullOrWhiteSpace(patch) && patch != "0")
            version += $".{patch}";

        if (!string.IsNullOrWhiteSpace(status))
            version += $"-{status}";

        return version;
    }

    private List<BuildRecord> ScanPackagesDirectory()
    {
        var records = new List<BuildRecord>();

        foreach (var dir in Directory.GetDirectories(_packagesDir))
        {
            var dirName = Path.GetFileName(dir);
            if (dirName.StartsWith(".")) continue; // skip .build-history.json

            var manifestPath = Path.Combine(dir, "package-manifest.txt");
            if (!File.Exists(manifestPath)) continue;

            var manifest = ParseManifest(manifestPath);
            if (manifest == null) continue;

            var exeFiles = Directory.GetFiles(dir, "*.exe");
            var mainExe = exeFiles.FirstOrDefault(f => !f.Contains(".console")) ?? exeFiles.FirstOrDefault();

            var record = new BuildRecord
            {
                PackageName = dirName,
                Platform = manifest.GetValueOrDefault("Platform", "?"),
                Target = manifest.GetValueOrDefault("Target", "?"),
                Arch = manifest.GetValueOrDefault("Arch", "?"),
                Version = manifest.GetValueOrDefault("Version", "?"),
                Mono = false, // will be detected from exe
                ExePath = mainExe ?? "",
                PackageDir = dir,
                ZipPath = Path.Combine(_packagesDir, $"{dirName}.zip"),
                Commit = manifest.GetValueOrDefault("Commit", ""),
                CreatedAt = Directory.GetCreationTime(dir),
            };

            // Detect Mono from exe name
            if (mainExe != null && mainExe.Contains(".mono"))
                record.Mono = true;

            // Try parse created time from manifest
            if (manifest.TryGetValue("Created", out var createdStr) &&
                DateTime.TryParse(createdStr, out var created))
                record.CreatedAt = created;

            // Find matching logs
            var matchingLogs = FindMatchingLogs(record.Platform, record.Target, record.Arch, record.Mono);
            if (matchingLogs.Count > 0)
                record.BuildLogPath = matchingLogs.OrderByDescending(File.GetCreationTime).First();

            records.Add(record);
        }

        return records;
    }

    private List<string> FindMatchingLogs(string platform, string target, string arch, bool mono)
    {
        if (!Directory.Exists(_logsDir)) return new List<string>();

        var results = new List<string>();
        foreach (var logFile in Directory.GetFiles(_logsDir, "*.log"))
        {
            var name = Path.GetFileNameWithoutExtension(logFile);
            // Log name format: godot-VERSION-PLATFORM-TARGET-ARCH[-mono]-TIMESTAMP[-build|-mono-glue|-mono-assemblies]
            if (name.Contains(platform) && name.Contains(target) && name.Contains(arch))
            {
                var isMonoLog = name.Contains("-mono");
                // Match build logs only (skip mono-glue, mono-assemblies)
                if (!name.EndsWith("-build")) continue;
                if (isMonoLog == mono)
                    results.Add(logFile);
            }
        }

        return results;
    }

    private static Dictionary<string, string>? ParseManifest(string path)
    {
        try
        {
            var dict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in File.ReadLines(path))
            {
                var sepIdx = line.IndexOf(':');
                if (sepIdx <= 0) continue;
                var key = line[..sepIdx].Trim();
                var value = line[(sepIdx + 1)..].Trim();
                dict[key] = value;
            }
            return dict;
        }
        catch { return null; }
    }

    /// <summary>
    /// Parse a Godot exe filename like:
    ///   godot.windows.editor.x86_64.exe
    ///   godot.windows.editor.x86_64.console.exe
    ///   godot.windows.editor.x86_64.mono.exe
    ///   godot.windows.editor.x86_64.mono.console.exe
    /// </summary>
    private static (string Platform, string Target, string Arch, bool Mono)? ParseExeFileName(string fileName)
    {
        // Remove .exe
        var name = Path.GetFileNameWithoutExtension(fileName);
        // Remove .console if present
        name = name.Replace(".console", "");

        var pattern = @"^godot\.(\w+)\.(\w+)\.(\w+)(?:\.(.+))?(?:\.mono)?$";
        var match = Regex.Match(name, pattern);
        if (!match.Success) return null;

        var platform = match.Groups[1].Value;
        var target = match.Groups[2].Value;
        var arch = match.Groups[3].Value;
        var mono = fileName.Contains(".mono.") || fileName.EndsWith(".mono.exe");

        return (platform, target, arch, mono);
    }

    // ═══════════════════════════════════════════════════════════════
    //  HISTORY PERSISTENCE
    // ═══════════════════════════════════════════════════════════════

    private void LoadHistory()
    {
        try
        {
            if (File.Exists(_historyPath))
            {
                var json = File.ReadAllText(_historyPath);
                _history = JsonSerializer.Deserialize<BuildHistory>(json) ?? new BuildHistory();
            }
        }
        catch { _history = new BuildHistory(); }
    }

    private void SaveHistory()
    {
        try
        {
            Directory.CreateDirectory(_packagesDir);
            var json = JsonSerializer.Serialize(_history, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(_historyPath, json);
        }
        catch { /* best effort */ }
    }

    private bool IsHiddenExePath(string path)
    {
        return _history.HiddenExePaths.Any(hidden => SamePath(hidden, path));
    }

    private static bool SamePath(string left, string right)
    {
        if (string.IsNullOrWhiteSpace(left) || string.IsNullOrWhiteSpace(right))
            return false;

        return string.Equals(NormalizePath(left), NormalizePath(right), StringComparison.OrdinalIgnoreCase);
    }

    private static string NormalizePath(string path)
    {
        try { return Path.GetFullPath(path); }
        catch { return path.Trim(); }
    }

    private List<BuildRecord> MergeWithHistory(List<BuildRecord> discovered)
    {
        var result = new List<BuildRecord>();
        var seenExes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        // Add all discovered records
        foreach (var d in discovered)
        {
            if (!string.IsNullOrEmpty(d.ExePath) && !seenExes.Add(d.ExePath))
                continue;

            // If this same build exists in history, preserve the Id and any extra metadata
            var histMatch = _history.Records.FirstOrDefault(h =>
                !string.IsNullOrEmpty(h.ExePath) && h.ExePath == d.ExePath);
            if (histMatch != null)
            {
                d.Id = histMatch.Id;
                if (string.IsNullOrEmpty(d.Commit)) d.Commit = histMatch.Commit;
                if (string.IsNullOrEmpty(d.Version) || d.Version == "?") d.Version = histMatch.Version;
            }

            result.Add(d);
        }

        return result;
    }
}
