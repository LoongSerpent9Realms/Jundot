using System.IO.Compression;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace JundotPackageBuilder;

/// <summary>
/// Auto-update checker: queries GitHub Releases for the latest version,
/// compares it with the running version, and if newer — downloads and
/// installs the update via a self-replacing batch script.
/// </summary>
public static class UpdateChecker
{
    // ── Configuration ──────────────────────────────────────────
    private const string Owner = "LoongSerpent9Realms";
    private const string Repo = "Jundot";
    private const string ApiUrl = $"https://api.github.com/repos/{Owner}/{Repo}/releases/latest";

    private static readonly HttpClient HttpClient = new()
    {
        DefaultRequestHeaders = { { "User-Agent", "JundotPackageBuilder-Updater/1.0" } },
        Timeout = TimeSpan.FromSeconds(30)
    };

    // ── Result types ───────────────────────────────────────────

    public sealed class UpdateCheckResult
    {
        public bool            HasUpdate     { get; set; }
        public string?         LatestVersion { get; set; }
        public string?         CurrentVersion { get; set; }
        public string?         DownloadUrl   { get; set; }
        public string?         ReleaseNotes  { get; set; }
        public long            AssetSize     { get; set; }
        public string?         Error         { get; set; }
    }

    // ═══════════════════════════════════════════════════════════
    //  PUBLIC API
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Check GitHub Releases for a newer version.
    /// </summary>
    /// <param name="currentVersion">Current running version (e.g. "1.0.0").</param>
    public static async Task<UpdateCheckResult> CheckForUpdateAsync(string currentVersion)
    {
        var result = new UpdateCheckResult { CurrentVersion = currentVersion };

        try
        {
            // ── 1. Fetch latest release info ──────────────────
            var release = await HttpClient.GetFromJsonAsync<GitHubRelease>(ApiUrl, JsonOptions);
            if (release == null || string.IsNullOrWhiteSpace(release.TagName))
            {
                result.Error = "Failed to parse GitHub release info.";
                return result;
            }

            var latestVersion = NormalizeVersion(release.TagName);
            result.LatestVersion = latestVersion;
            result.ReleaseNotes = release.Body ?? "";

            // ── 2. Compare versions ───────────────────────────
            if (!IsNewerVersion(currentVersion, latestVersion))
            {
                result.HasUpdate = false;
                return result;
            }

            // ── 3. Find download asset ────────────────────────
            // Prefer .zip first, then .exe matching "JundotPackageBuilder" or "PackageBuilder"
            var asset = FindBestAsset(release.Assets);
            if (asset == null)
            {
                result.Error = "No suitable download asset found in the latest release.";
                return result;
            }

            result.HasUpdate   = true;
            result.DownloadUrl = asset.BrowserDownloadUrl;
            result.AssetSize   = asset.Size;
        }
        catch (HttpRequestException ex)
        {
            result.Error = $"Network error: {ex.Message}";
        }
        catch (TaskCanceledException)
        {
            result.Error = "Request timed out.";
        }
        catch (Exception ex)
        {
            result.Error = $"Unexpected error: {ex.Message}";
        }

        return result;
    }

    /// <summary>
    /// Download the update and launch a self-replacing installer script,
    /// then signal the caller to exit.
    /// </summary>
    /// <param name="downloadUrl">Direct download URL of the asset.</param>
    /// <param name="currentExeDir">Directory containing the running exe.</param>
    /// <param name="progressCallback">Optional progress reporter (0-100).</param>
    /// <returns>The installer script path (for logging), or null on failure.</returns>
    public static async Task<string?> DownloadAndInstallAsync(
        string downloadUrl,
        string currentExeDir,
        Action<int>? progressCallback = null)
    {
        var tempDir = Path.Combine(Path.GetTempPath(), "JundotPackageBuilder_Update");
        var zipPath = Path.Combine(tempDir, "update.zip");
        var extractDir = Path.Combine(tempDir, "extracted");

        try
        {
            // ── 1. Prepare temp directories ───────────────────
            if (Directory.Exists(tempDir))
                Directory.Delete(tempDir, true);
            Directory.CreateDirectory(tempDir);
            Directory.CreateDirectory(extractDir);

            // ── 2. Download ───────────────────────────────────
            using var response = await HttpClient.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead);
            response.EnsureSuccessStatusCode();

            var totalBytes = response.Content.Headers.ContentLength ?? -1;
            using var stream = await response.Content.ReadAsStreamAsync();
            using var fileStream = File.Create(zipPath);

            var buffer = new byte[8192];
            long totalRead = 0;
            int bytesRead;
            int lastReported = -1;

            while ((bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length)) > 0)
            {
                await fileStream.WriteAsync(buffer, 0, bytesRead);
                totalRead += bytesRead;

                if (totalBytes > 0 && progressCallback != null)
                {
                    var pct = (int)(totalRead * 100 / totalBytes);
                    if (pct != lastReported)
                    {
                        lastReported = pct;
                        progressCallback(pct);
                    }
                }
            }

            progressCallback?.Invoke(100);

            // ── 3. Extract ────────────────────────────────────
            ZipFile.ExtractToDirectory(zipPath, extractDir, overwriteFiles: true);

            // ── 4. Write installer batch script ───────────────
            var currentExe = Environment.ProcessPath ?? Path.Combine(currentExeDir, "JundotPackageBuilder.exe");
            var batchPath = Path.Combine(tempDir, "update_install.bat");
            var batchContent = GenerateInstallerScript(extractDir, currentExeDir, currentExe, tempDir);
            await File.WriteAllTextAsync(batchPath, batchContent);

            // ── 5. Launch installer ───────────────────────────
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = "cmd.exe",
                Arguments = $"/C \"{batchPath}\"",
                UseShellExecute = true,
                CreateNoWindow = true,
                WindowStyle = System.Diagnostics.ProcessWindowStyle.Hidden
            });

            return batchPath;
        }
        catch
        {
            // Clean up temp on failure
            try { if (Directory.Exists(tempDir)) Directory.Delete(tempDir, true); }
            catch { /* best-effort */ }
            throw;
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  INTERNALS
    // ═══════════════════════════════════════════════════════════

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    /// <summary>Strip a leading 'v' prefix from version tags.</summary>
    private static string NormalizeVersion(string tag)
    {
        var v = tag.Trim();
        if (v.Length > 1 && (v[0] == 'v' || v[0] == 'V') && char.IsDigit(v[1]))
            v = v[1..];

        // Handle compound tags like "packagebuilder-1.2.3"
        var dashIdx = v.LastIndexOf('-');
        if (dashIdx > 0)
        {
            var after = v[(dashIdx + 1)..];
            if (after.All(c => char.IsDigit(c) || c == '.'))
                v = after;
        }

        return v;
    }

    /// <summary>
    /// Semantic version comparison: returns true if <paramref name="latest"/>
    /// is strictly greater than <paramref name="current"/>.
    /// </summary>
    private static bool IsNewerVersion(string current, string latest)
    {
        if (!TryParseSemVer(current, out var cMaj, out var cMin, out var cPatch))
            return !string.Equals(current, latest, StringComparison.OrdinalIgnoreCase);

        if (!TryParseSemVer(latest, out var lMaj, out var lMin, out var lPatch))
            return false;

        if (lMaj != cMaj) return lMaj > cMaj;
        if (lMin != cMin) return lMin > cMin;
        return lPatch > cPatch;
    }

    private static bool TryParseSemVer(string version, out int major, out int minor, out int patch)
    {
        major = minor = patch = 0;
        if (string.IsNullOrWhiteSpace(version)) return false;

        var parts = version.Trim().Split('.');
        if (parts.Length < 1) return false;

        return int.TryParse(parts[0], out major)
            && (parts.Length < 2 || int.TryParse(parts[1], out minor))
            && (parts.Length < 3 || int.TryParse(parts[2], out patch));
    }

    /// <summary>Pick the best asset: prefer .zip, then .exe matching known names.</summary>
    private static GitHubAsset? FindBestAsset(List<GitHubAsset>? assets)
    {
        if (assets == null || assets.Count == 0)
            return null;

        // Priority 1: zip files containing "PackageBuilder"
        var zip = assets.FirstOrDefault(a =>
            a.BrowserDownloadUrl.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) &&
            (a.Name.Contains("PackageBuilder", StringComparison.OrdinalIgnoreCase) ||
             a.Name.Contains("JundotPackageBuilder", StringComparison.OrdinalIgnoreCase)));
        if (zip != null) return zip;

        // Priority 2: any .zip
        zip = assets.FirstOrDefault(a =>
            a.BrowserDownloadUrl.EndsWith(".zip", StringComparison.OrdinalIgnoreCase));
        if (zip != null) return zip;

        // Priority 3: exe matching known names
        var exe = assets.FirstOrDefault(a =>
            a.BrowserDownloadUrl.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) &&
            (a.Name.Contains("PackageBuilder", StringComparison.OrdinalIgnoreCase) ||
             a.Name.Contains("Jundot", StringComparison.OrdinalIgnoreCase)));
        if (exe != null) return exe;

        // Last resort: first .exe
        return assets.FirstOrDefault(a =>
            a.BrowserDownloadUrl.EndsWith(".exe", StringComparison.OrdinalIgnoreCase));
    }

    /// <summary>
    /// Generate a batch script that replaces the running exe with the new version
    /// and restarts the application.
    /// </summary>
    private static string GenerateInstallerScript(
        string extractDir,
        string targetDir,
        string targetExe,
        string cleanupDir)
    {
        // Use a batch file with retry logic — waits for the old process to exit,
        // then copies files over and restarts.
        return $"""
@echo off
title Jundot PackageBuilder Updater
echo Updating Jundot PackageBuilder...

:: Wait for the old process to exit (up to 30 seconds)
set RETRIES=0
:waitloop
tasklist /FI "IMAGENAME eq {Path.GetFileName(targetExe)}" 2>NUL | find /I "{Path.GetFileName(targetExe)}" >NUL
if %ERRORLEVEL%==0 (
    if %RETRIES% LSS 30 (
        timeout /T 1 /NOBREAK >NUL
        set /A RETRIES+=1
        goto waitloop
    )
)

:: Copy new files over old ones
echo Installing update...
xcopy /E /Y /R "{extractDir}\\*" "{targetDir}\\" >NUL 2>&1

:: Restart the application
echo Starting updated PackageBuilder...
start "" "{targetExe}"

:: Clean up temp files
timeout /T 2 /NOBREAK >NUL
rmdir /S /Q "{cleanupDir}" 2>NUL
del "%~f0" >NUL 2>&1
""";
    }

    // ═══════════════════════════════════════════════════════════
    //  JSON MODELS
    // ═══════════════════════════════════════════════════════════

    private class GitHubRelease
    {
        [JsonPropertyName("tag_name")]
        public string? TagName { get; set; }

        [JsonPropertyName("body")]
        public string? Body { get; set; }

        [JsonPropertyName("assets")]
        public List<GitHubAsset>? Assets { get; set; }
    }

    private class GitHubAsset
    {
        [JsonPropertyName("name")]
        public string Name { get; set; } = "";

        [JsonPropertyName("browser_download_url")]
        public string BrowserDownloadUrl { get; set; } = "";

        [JsonPropertyName("size")]
        public long Size { get; set; }
    }
}
