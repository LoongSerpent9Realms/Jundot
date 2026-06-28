using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace JundotPackageBuilder;

/// <summary>
/// Generates an update-manifest.json file conforming to
/// scripts/update-manifest-schema.json during the package phase.
/// 
/// Called by BuildEngine right after the ZIP is created.
/// </summary>
public static class ManifestGenerator
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    /// <summary>
    /// Generate update-manifest.json for the just-built package.
    /// </summary>
    /// <param name="cfg">Build configuration used for this package.</param>
    /// <param name="version">Human-readable version string (e.g. "1.7.2-beta").</param>
    /// <param name="packageName">Package identifier (e.g. "jundot-1.7.2-beta-windows-editor-x86_64-20260608-120000").</param>
    /// <param name="zipPath">Path to the finished ZIP file.</param>
    /// <param name="stagingDir">Staging directory containing extracted package files.</param>
    /// <param name="repoRoot">Jundot repository root (for git commit lookup).</param>
    /// <returns>Path to the written manifest JSON, or null on failure.</returns>
    public static string? Generate(
        BuildConfig cfg,
        string version,
        string packageName,
        string zipPath,
        string stagingDir,
        string repoRoot)
    {
        try
        {
            // ── 1. Compute SHA256 of ZIP ────────────────────────
            string sha256, sha1;
            long zipSize;
            ComputeHashes(zipPath, out sha256, out sha1, out zipSize);

            // ── 2. Parse version components ──────────────────────
            var ver = ParseVersion(version);

            // ── 3. Get git commit ────────────────────────────────
            var commit = TryGetGitCommit(repoRoot);

            // ── 4. Collect file list from staging dir ────────────
            var files = CollectFileManifest(stagingDir);

            // ── 5. Build manifest object ─────────────────────────
            var manifest = new UpdateManifest
            {
                ManifestVersion = "1.0",
                PackageName = packageName,
                VersionInfo = ver,
                Platform = cfg.PlatformName,
                Target = cfg.Target,
                Arch = cfg.Arch,
                Mono = cfg.Mono,
                DownloadUrl = "", // Filled after GitHub Release upload
                PackageSize = zipSize,
                Sha256 = sha256,
                Sha1 = sha1,
                ReleaseDate = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"),
                Channel = ver.Status switch
                {
                    "stable" => "stable",
                    "beta" => "beta",
                    "rc" => "beta",
                    _ => "dev"
                },
                Changelog = "",
                Mandatory = false,
                MinVersion = $"{ver.Major}.{ver.Minor}.0",
                RollbackTo = "", // Consumer fills after publishing
                Grayscale = new GrayscaleConfig
                {
                    Enabled = false,
                    Percentage = 100,
                    Whitelist = new List<string>(),
                    MachineIdHashSeed = "jundot-grayscale-v1"
                },
                FileList = files
            };

            // ── 6. Serialize to staging dir ─────────────────────
            var manifestPath = Path.Combine(stagingDir, "update-manifest.json");
            var json = JsonSerializer.Serialize(manifest, JsonOptions);
            File.WriteAllText(manifestPath, json, Encoding.UTF8);

            // ── 7. Also copy to package root (next to the ZIP) ──
            var packageRoot = Path.GetDirectoryName(zipPath)!;
            var externalManifestPath = Path.Combine(packageRoot, "manifest.json");
            File.WriteAllText(externalManifestPath, json, Encoding.UTF8);

            return manifestPath;
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[ManifestGenerator] Failed: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Compute SHA-256 and SHA-1 of a file using streaming (memory-efficient).
    /// </summary>
    private static void ComputeHashes(
        string filePath,
        out string sha256Hex,
        out string sha1Hex,
        out long fileSize)
    {
        using var stream = File.OpenRead(filePath);
        fileSize = stream.Length;

        using var sha256 = SHA256.Create();
        using var sha1 = System.Security.Cryptography.SHA1.Create();

        // Compute both hashes from the file
        var hash256 = sha256.ComputeHash(stream);

        stream.Position = 0;
        var hash1 = sha1.ComputeHash(stream);

        sha256Hex = BitConverter.ToString(hash256).Replace("-", "").ToLowerInvariant();
        sha1Hex = BitConverter.ToString(hash1).Replace("-", "").ToLowerInvariant();
    }

    /// <summary>
    /// Parse a Jundot version string like "1.7.2-beta" into structured components.
    /// </summary>
    private static VersionInfo ParseVersion(string version)
    {
        var result = new VersionInfo();

        // Split on first hyphen: "1.7.2" + "beta"
        var hyphenIdx = version.IndexOf('-');
        var numericPart = hyphenIdx > 0 ? version[..hyphenIdx] : version;
        var statusPart = hyphenIdx > 0 ? version[(hyphenIdx + 1)..] : "stable";

        var parts = numericPart.Split('.');
        if (parts.Length > 0 && int.TryParse(parts[0], out var major))
            result.Major = major;
        if (parts.Length > 1 && int.TryParse(parts[1], out var minor))
            result.Minor = minor;
        if (parts.Length > 2 && int.TryParse(parts[2], out var patch))
            result.Patch = patch;

        result.Status = statusPart;
        result.Full = version;

        return result;
    }

    /// <summary>
    /// Try to get the current git HEAD short commit hash.
    /// </summary>
    private static string? TryGetGitCommit(string repoRoot)
    {
        try
        {
            var psi = new System.Diagnostics.ProcessStartInfo("git", "rev-parse --short HEAD")
            {
                WorkingDirectory = repoRoot,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            using var p = System.Diagnostics.Process.Start(psi);
            if (p == null) return null;
            var output = p.StandardOutput.ReadToEnd().Trim();
            p.WaitForExit(5000);
            return p.ExitCode == 0 && output.Length >= 7 ? output : null;
        }
        catch
        {
            return null;
        }
    }

    /// <summary>
    /// Walk the staging directory and build a file manifest (path + sha256 + size).
    /// Skips directories and the manifest files themselves.
    /// </summary>
    private static List<FileEntry> CollectFileManifest(string stagingDir)
    {
        var entries = new List<FileEntry>();
        var stagingDirFull = Path.GetFullPath(stagingDir).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;

        foreach (var filePath in Directory.GetFiles(stagingDir, "*", SearchOption.AllDirectories))
        {
            // Skip our own manifest files
            var fileName = Path.GetFileName(filePath);
            if (fileName == "update-manifest.json" || fileName == "package-manifest.txt")
                continue;

            var relativePath = filePath.StartsWith(stagingDirFull, StringComparison.OrdinalIgnoreCase)
                ? filePath[stagingDirFull.Length..]
                : filePath;

            // Normalize path separators to forward slash
            relativePath = relativePath.Replace('\\', '/');

            var fileInfo = new FileInfo(filePath);

            // Compute per-file SHA256
            using var stream = File.OpenRead(filePath);
            using var sha256 = SHA256.Create();
            var hash = sha256.ComputeHash(stream);
            var sha256Hex = BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();

            entries.Add(new FileEntry
            {
                Path = relativePath,
                Sha256 = sha256Hex,
                Size = fileInfo.Length,
                Required = IsRequiredFile(relativePath)
            });
        }

        return entries;
    }

    /// <summary>
    /// Determine if a file is considered "required" for the package.
    /// The main exe + core DLLs are required; optional assets are not.
    /// </summary>
    private static bool IsRequiredFile(string relativePath)
    {
        var lower = relativePath.ToLowerInvariant();

        // Main executables
        if (lower.EndsWith(".exe")) return true;
        if (lower.EndsWith(".dll")) return true;

        // Optional files
        if (lower.EndsWith(".pdb")) return false;
        if (lower.EndsWith(".txt")) return false;
        if (lower.EndsWith(".json")) return false;
        if (lower.EndsWith(".md")) return false;
        if (lower.EndsWith(".log")) return false;

        // Config/editor data is required
        if (lower.Contains("editor_data")) return true;
        if (lower == "_sc_") return true;

        return true; // Default to required for unknown files
    }
}

// ═══════════════════════════════════════════════════════════════
//  MANIFEST DATA MODELS
// ═══════════════════════════════════════════════════════════════

/// <summary>Serialization model matching update-manifest-schema.json.</summary>
public class UpdateManifest
{
    [JsonPropertyName("manifest_version")]
    public string ManifestVersion { get; set; } = "1.0";

    [JsonPropertyName("package_name")]
    public string PackageName { get; set; } = "";

    [JsonPropertyName("version")]
    public VersionInfo VersionInfo { get; set; } = new();

    [JsonPropertyName("platform")]
    public string Platform { get; set; } = "";

    [JsonPropertyName("target")]
    public string Target { get; set; } = "";

    [JsonPropertyName("arch")]
    public string Arch { get; set; } = "";

    [JsonPropertyName("mono")]
    public bool Mono { get; set; }

    [JsonPropertyName("download_url")]
    public string DownloadUrl { get; set; } = "";

    [JsonPropertyName("package_size")]
    public long PackageSize { get; set; }

    [JsonPropertyName("sha256")]
    public string Sha256 { get; set; } = "";

    [JsonPropertyName("sha1")]
    public string? Sha1 { get; set; }

    [JsonPropertyName("release_date")]
    public string ReleaseDate { get; set; } = "";

    [JsonPropertyName("channel")]
    public string Channel { get; set; } = "stable";

    [JsonPropertyName("changelog")]
    public string? Changelog { get; set; }

    [JsonPropertyName("mandatory")]
    public bool Mandatory { get; set; }

    [JsonPropertyName("min_version")]
    public string? MinVersion { get; set; }

    [JsonPropertyName("rollback_to")]
    public string? RollbackTo { get; set; }

    [JsonPropertyName("grayscale")]
    public GrayscaleConfig Grayscale { get; set; } = new();

    [JsonPropertyName("files")]
    public List<FileEntry>? FileList { get; set; }
}

public class VersionInfo
{
    [JsonPropertyName("major")]
    public int Major { get; set; }

    [JsonPropertyName("minor")]
    public int Minor { get; set; }

    [JsonPropertyName("patch")]
    public int Patch { get; set; }

    [JsonPropertyName("status")]
    public string Status { get; set; } = "stable";

    [JsonPropertyName("full")]
    public string Full { get; set; } = "";

    [JsonPropertyName("commit")]
    public string? Commit { get; set; }
}

public class GrayscaleConfig
{
    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; }

    [JsonPropertyName("percentage")]
    public int Percentage { get; set; } = 100;

    [JsonPropertyName("whitelist")]
    public List<string> Whitelist { get; set; } = new();

    [JsonPropertyName("machine_id_hash_seed")]
    public string MachineIdHashSeed { get; set; } = "jundot-grayscale-v1";
}

public class FileEntry
{
    [JsonPropertyName("path")]
    public string Path { get; set; } = "";

    [JsonPropertyName("sha256")]
    public string Sha256 { get; set; } = "";

    [JsonPropertyName("size")]
    public long Size { get; set; }

    [JsonPropertyName("required")]
    public bool Required { get; set; } = true;
}
