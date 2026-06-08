using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace JundotLauncher;

/// <summary>
/// Fetches and caches the update-manifest.json from a remote server.
/// Supports GitHub Releases URL, ETag/Last-Modified conditional requests.
/// </summary>
public class ManifestFetcher
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    private readonly HttpClient _http;
    private readonly string _cacheDir;
    private readonly int _timeoutSeconds;

    // ─────────────────────────────────────────────────────────

    public ManifestFetcher(HttpClient? httpClient = null, string? cacheDir = null, int timeoutSeconds = 15)
    {
        _http = httpClient ?? CreateDefaultHttpClient();
        _http.Timeout = TimeSpan.FromSeconds(timeoutSeconds);
        _timeoutSeconds = timeoutSeconds;

        _cacheDir = cacheDir ?? Path.Combine(Path.GetTempPath(), "jundot-launcher-cache");
    }

    // ── Public API ───────────────────────────────────────────

    /// <summary>
    /// Fetch the latest update manifest from a given URL.
    /// Supports ETag-based caching: if the server returns 304 Not Modified,
    /// the cached manifest is returned.
    /// </summary>
    /// <param name="manifestUrl">Full URL to update-manifest.json.</param>
    /// <param name="useCache">Whether to use ETag/Last-Modified cache.</param>
    /// <returns>The parsed manifest, or null on failure.</returns>
    public async Task<UpdateManifestV1?> FetchAsync(string manifestUrl, bool useCache = true)
    {
        try
        {
            var request = new HttpRequestMessage(HttpMethod.Get, manifestUrl);

            // ── Conditional cache headers ─────────────────────
            if (useCache)
            {
                var cacheFile = GetCacheFilePath(manifestUrl);
                if (File.Exists(cacheFile))
                {
                    var cacheData = File.ReadAllText(cacheFile);
                    var cacheEntry = JsonSerializer.Deserialize<CacheEntry>(cacheData, JsonOptions);
                    if (cacheEntry != null)
                    {
                        if (!string.IsNullOrEmpty(cacheEntry.ETag))
                            request.Headers.IfNoneMatch.Add(new EntityTagHeaderValue(cacheEntry.ETag));
                        if (!string.IsNullOrEmpty(cacheEntry.LastModified))
                            request.Headers.IfModifiedSince = DateTimeOffset.Parse(cacheEntry.LastModified);
                    }
                }
            }

            // ── Send request ──────────────────────────────────
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(_timeoutSeconds));
            var response = await _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cts.Token);

            // 304 Not Modified → use cache
            if (response.StatusCode == HttpStatusCode.NotModified && useCache)
            {
                var cacheFile = GetCacheFilePath(manifestUrl);
                if (File.Exists(cacheFile))
                {
                    var cached = JsonSerializer.Deserialize<UpdateManifestV1>(
                        File.ReadAllText(cacheFile), JsonOptions);
                    if (cached != null) return cached;
                }
            }

            response.EnsureSuccessStatusCode();

            // ── Read body ─────────────────────────────────────
            var body = await response.Content.ReadAsStringAsync(cts.Token);

            // ── Parse manifest ────────────────────────────────
            var manifest = JsonSerializer.Deserialize<UpdateManifestV1>(body, JsonOptions);
            if (manifest == null)
                throw new Exception("Manifest JSON 解析失败：返回了 null。");

            // Validate required fields
            if (string.IsNullOrEmpty(manifest.ManifestVersion))
                throw new Exception("Manifest 缺少 manifest_version 字段。");
            if (string.IsNullOrEmpty(manifest.Version?.Full))
                throw new Exception("Manifest 缺少 version.full 字段。");

            // ── Cache the manifest ────────────────────────────
            if (useCache)
            {
                var cacheEntry = new CacheEntry();
                var etag = response.Headers.ETag?.Tag;
                if (!string.IsNullOrEmpty(etag))
                    cacheEntry.ETag = etag;
                var lastModified = response.Content.Headers.LastModified;
                if (lastModified.HasValue)
                    cacheEntry.LastModified = lastModified.Value.ToString("R");

                var cacheFile = GetCacheFilePath(manifestUrl);
                Directory.CreateDirectory(Path.GetDirectoryName(cacheFile)!);
                File.WriteAllText(cacheFile, body);
                File.WriteAllText(cacheFile + ".meta",
                    JsonSerializer.Serialize(cacheEntry, JsonOptions));
            }

            return manifest;
        }
        catch (TaskCanceledException)
        {
            ConsoleUI.Warning($"Manifest 请求超时（{_timeoutSeconds}秒）");
            return null;
        }
        catch (HttpRequestException ex)
        {
            ConsoleUI.Warning($"网络错误: {ex.Message}");
            return null;
        }
        catch (JsonException ex)
        {
            ConsoleUI.Warning($"Manifest JSON 解析错误: {ex.Message}");
            return null;
        }
        catch (Exception ex)
        {
            ConsoleUI.Warning($"Manifest 拉取异常: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Try to fetch from multiple URL candidates (e.g., GitHub Releases API first, then direct URL).
    /// Returns the first successful result, or null if all fail.
    /// </summary>
    public async Task<UpdateManifestV1?> FetchFromCandidatesAsync(string[] urls, bool useCache = true)
    {
        foreach (var url in urls)
        {
            var result = await FetchAsync(url, useCache);
            if (result != null)
                return result;
        }
        return null;
    }

    // ── Cache ────────────────────────────────────────────────

    private string GetCacheFilePath(string url)
    {
        var hash = Convert.ToHexString(
            System.Security.Cryptography.SHA256.HashData(
                System.Text.Encoding.UTF8.GetBytes(url)))
            .ToLowerInvariant()[..16];
        return Path.Combine(_cacheDir, $"manifest-{hash}.json");
    }

    // ── Default HttpClient ───────────────────────────────────

    private static HttpClient CreateDefaultHttpClient()
    {
        var handler = new HttpClientHandler
        {
            AllowAutoRedirect = true,
            AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate
        };

        var client = new HttpClient(handler);
        client.DefaultRequestHeaders.UserAgent.ParseAdd(
            $"JundotLauncher/1.0 (Windows; +https://jundotengine.org)");
        client.DefaultRequestHeaders.Accept.Add(
            new MediaTypeWithQualityHeaderValue("application/json"));
        client.DefaultRequestHeaders.AcceptEncoding.Add(
            new StringWithQualityHeaderValue("gzip"));
        client.DefaultRequestHeaders.AcceptEncoding.Add(
            new StringWithQualityHeaderValue("deflate"));

        return client;
    }
}

// ═══════════════════════════════════════════════════════════════
//  MANIFEST DATA MODEL (mirrors update-manifest-schema.json)
// ═══════════════════════════════════════════════════════════════

/// <summary>Deserialized update manifest matching the v1 schema.</summary>
public class UpdateManifestV1
{
    [JsonPropertyName("manifest_version")]
    public string ManifestVersion { get; set; } = "";

    [JsonPropertyName("package_name")]
    public string PackageName { get; set; } = "";

    [JsonPropertyName("version")]
    public ManifestVersionV1? Version { get; set; }

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
    public GrayscaleConfigV1? Grayscale { get; set; }

    [JsonPropertyName("files")]
    public List<FileEntryV1>? Files { get; set; }
}

public class ManifestVersionV1
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

public class GrayscaleConfigV1
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

public class FileEntryV1
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

/// <summary>ETag/Last-Modified cache metadata.</summary>
public class CacheEntry
{
    [JsonPropertyName("etag")]
    public string? ETag { get; set; }

    [JsonPropertyName("last_modified")]
    public string? LastModified { get; set; }
}
