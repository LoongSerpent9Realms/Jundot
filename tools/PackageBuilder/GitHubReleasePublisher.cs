using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using System.Security.Cryptography;

namespace JundotPackageBuilder;

/// <summary>
/// Uploads a built package (zip + manifest) to a GitHub Release.
///
/// Workflow:
///   1. Create or find a GitHub Release (by tag)
///   2. Upload the zip as a release asset
///   3. Backfill `download_url` into the manifest
///   4. Re-upload the updated manifest
///
/// Supports a dry-run mode that prints what would happen without
/// making any network calls.
/// </summary>
public class GitHubReleasePublisher
{
    private readonly PublishConfig _config;
    private readonly HttpClient _http;
    private readonly string _repoRoot;

    public GitHubReleasePublisher(string repoRoot, PublishConfig config)
    {
        _repoRoot = repoRoot;
        _config = config;

        _http = new HttpClient
        {
            DefaultRequestHeaders =
            {
                { "User-Agent", "JundotPackageBuilder-Publisher/1.0" },
                { "Accept", "application/vnd.github+json" },
                { "X-GitHub-Api-Version", "2022-11-28" }
            },
            Timeout = TimeSpan.FromMinutes(5)
        };

        var token = config.GetToken();
        if (!string.IsNullOrEmpty(token))
        {
            _http.DefaultRequestHeaders.Authorization =
                new AuthenticationHeaderValue("Bearer", token);
        }
    }

    // ── Public API ──────────────────────────────────────────────

    /// <summary>
    /// Publish a built package to GitHub Releases.
    /// </summary>
    /// <returns>True on success, false on failure.</returns>
    public async Task<bool> PublishAsync(
        string packageName,
        string version,
        string zipPath,
        string manifestPath,
        CancellationToken ct = default)
    {
        if (!File.Exists(zipPath))
        {
            LogError($"Zip file not found: {zipPath}");
            return false;
        }

        if (!File.Exists(manifestPath))
        {
            LogError($"Manifest file not found: {manifestPath}");
            return false;
        }

        if (_config.DryRun)
        {
            return DryRunPublish(packageName, version, zipPath, manifestPath);
        }

        var token = _config.GetToken();
        if (string.IsNullOrEmpty(token))
        {
            LogError($"GitHub token not set. Set the {_config.TokenEnvVar} environment variable.");
            return false;
        }

        try
        {
            // Step 1: Find or create the release
            var releaseTag = string.IsNullOrEmpty(_config.ReleaseTag)
                ? $"v{version}"
                : _config.ReleaseTag;

            var releaseName = string.IsNullOrEmpty(_config.ReleaseName)
                ? $"Jundot {version}"
                : _config.ReleaseName;

            LogInfo($"Publishing {packageName} to GitHub Releases (tag: {releaseTag})...");

            var release = await FindOrCreateReleaseAsync(releaseTag, releaseName, ct);
            if (release == null)
            {
                LogError("Failed to find or create release.");
                return false;
            }

            var uploadUrl = release.UploadUrl.Replace("{?name,label}", "");
            LogInfo($"Release ID: {release.Id}, upload URL: {uploadUrl}");

            // Step 2: Upload the zip asset
            var zipName = Path.GetFileName(zipPath);
            var zipDownloadUrl = await UploadAssetAsync(uploadUrl, zipPath, zipName, "application/zip", ct);
            if (string.IsNullOrEmpty(zipDownloadUrl))
            {
                LogError("Failed to upload zip asset.");
                return false;
            }

            LogInfo($"Zip uploaded: {zipDownloadUrl}");

            // Step 3: Backfill download_url in manifest
            var manifestJson = await File.ReadAllTextAsync(manifestPath, ct);
            var manifestNode = JsonNode.Parse(manifestJson);
            if (manifestNode != null)
            {
                manifestNode["download_url"] = zipDownloadUrl;
                manifestNode["release_date"] = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ");
                manifestNode["changelog"] = _config.Changelog ?? "";
                manifestNode["mandatory"] = _config.Mandatory;

                if (manifestNode["grayscale"] is JsonObject gs)
                {
                    gs["percentage"] = _config.GrayscalePercentage;
                    gs["enabled"] = _config.GrayscalePercentage < 100;
                }

                var updatedManifest = manifestNode.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
                await File.WriteAllTextAsync(manifestPath, updatedManifest, ct);
            }

            // Step 4: Re-upload the updated manifest
            var manifestName = Path.GetFileName(manifestPath);
            var manifestDownloadUrl = await UploadAssetAsync(uploadUrl, manifestPath, manifestName, "application/json", ct);
            if (!string.IsNullOrEmpty(manifestDownloadUrl))
            {
                LogInfo($"Manifest updated: {manifestDownloadUrl}");
            }

            LogInfo($"Publish complete: {release.HtmlUrl}");
            return true;
        }
        catch (OperationCanceledException)
        {
            LogInfo("Publish cancelled.");
            return false;
        }
        catch (Exception ex)
        {
            LogError($"Publish failed: {ex.Message}");
            return false;
        }
    }

    // ── Dry-run ─────────────────────────────────────────────────

    private bool DryRunPublish(string packageName, string version, string zipPath, string manifestPath)
    {
        var zipInfo = new FileInfo(zipPath);
        var manifestInfo = new FileInfo(manifestPath);

        LogInfo("=== DRY RUN ===");
        LogInfo($"Package: {packageName}");
        LogInfo($"Version: {version}");
        LogInfo($"Repository: {_config.Owner}/{_config.Repo}");
        LogInfo($"Release Tag: {(string.IsNullOrEmpty(_config.ReleaseTag) ? $"v{version}" : _config.ReleaseTag)}");
        LogInfo($"Draft: {_config.Draft}");
        LogInfo($"Prerelease: {_config.Prerelease}");
        LogInfo($"Grayscale: {_config.GrayscalePercentage}%");
        LogInfo($"Mandatory: {_config.Mandatory}");
        LogInfo($"");
        LogInfo($"Files to upload:");
        LogInfo($"  - {zipPath} ({FormatBytes(zipInfo.Length)})");
        LogInfo($"  - {manifestPath} ({FormatBytes(manifestInfo.Length)})");
        LogInfo($"");
        LogInfo($"Token env var: {_config.TokenEnvVar}");
        LogInfo($"Token set: {(string.IsNullOrEmpty(_config.GetToken()) ? "NO" : "YES")}");
        LogInfo("=== End dry run ===");
        return true;
    }

    // ── GitHub API helpers ──────────────────────────────────────

    private async Task<GitHubRelease?> FindOrCreateReleaseAsync(
        string tag, string name, CancellationToken ct)
    {
        var apiBase = $"https://api.github.com/repos/{_config.Owner}/{_config.Repo}/releases";

        // Try to find an existing release by tag
        try
        {
            var getUrl = $"{apiBase}/tags/{Uri.EscapeDataString(tag)}";
            var response = await _http.GetAsync(getUrl, ct);

            if (response.IsSuccessStatusCode)
            {
                var json = await response.Content.ReadAsStringAsync(ct);
                return JsonSerializer.Deserialize<GitHubRelease>(json);
            }
        }
        catch
        {
            // Not found, will create
        }

        // Create a new release
        var body = _config.ReleaseBody;
        if (string.IsNullOrEmpty(body))
        {
            body = $"Automated build of Jundot {name}.";
        }

        var createPayload = new
        {
            tag_name = tag,
            name,
            body,
            draft = _config.Draft,
            prerelease = _config.Prerelease
        };

        var content = new StringContent(
            JsonSerializer.Serialize(createPayload),
            Encoding.UTF8,
            "application/json");

        var createResponse = await _http.PostAsync(apiBase, content, ct);

        if (!createResponse.IsSuccessStatusCode)
        {
            var errorBody = await createResponse.Content.ReadAsStringAsync(ct);
            LogError($"Failed to create release (HTTP {(int)createResponse.StatusCode}): {errorBody}");
            return null;
        }

        var createJson = await createResponse.Content.ReadAsStringAsync(ct);
        return JsonSerializer.Deserialize<GitHubRelease>(createJson);
    }

    private async Task<string?> UploadAssetAsync(
        string uploadUrl, string filePath, string fileName, string contentType, CancellationToken ct)
    {
        var fileBytes = await File.ReadAllBytesAsync(filePath, ct);
        var url = $"{uploadUrl}?name={Uri.EscapeDataString(fileName)}";

        using var content = new ByteArrayContent(fileBytes);
        content.Headers.ContentType = new MediaTypeHeaderValue(contentType);

        // GitHub API may return 422 if asset already exists; try to delete first
        var response = await _http.PostAsync(url, content, ct);

        if (!response.IsSuccessStatusCode)
        {
            var errorBody = await response.Content.ReadAsStringAsync(ct);
            LogError($"Failed to upload {fileName} (HTTP {(int)response.StatusCode}): {errorBody}");
            return null;
        }

        var json = await response.Content.ReadAsStringAsync(ct);
        var asset = JsonSerializer.Deserialize<GitHubAsset>(json);
        return asset?.BrowserDownloadUrl;
    }

    // ── Logging ─────────────────────────────────────────────────

    public event EventHandler<string>? LogMessage;

    private void LogInfo(string msg) => LogMessage?.Invoke(this, $"[INFO] {msg}");
    private void LogError(string msg) => LogMessage?.Invoke(this, $"[ERROR] {msg}");

    // ── Helpers ─────────────────────────────────────────────────

    private static string FormatBytes(long bytes) => bytes switch
    {
        >= 1_048_576 => $"{bytes / 1_048_576.0:F1} MB",
        >= 1024      => $"{bytes / 1024.0:F1} KB",
        _            => $"{bytes} B"
    };

    // ── GitHub API response models ──────────────────────────────

    private class GitHubRelease
    {
        [JsonPropertyName("id")]          public long   Id        { get; set; }
        [JsonPropertyName("tag_name")]    public string TagName   { get; set; } = "";
        [JsonPropertyName("name")]        public string Name      { get; set; } = "";
        [JsonPropertyName("html_url")]    public string HtmlUrl   { get; set; } = "";
        [JsonPropertyName("upload_url")]  public string UploadUrl { get; set; } = "";
    }

    private class GitHubAsset
    {
        [JsonPropertyName("id")]                    public long   Id                { get; set; }
        [JsonPropertyName("name")]                  public string Name              { get; set; } = "";
        [JsonPropertyName("browser_download_url")]  public string BrowserDownloadUrl { get; set; } = "";
    }
}
