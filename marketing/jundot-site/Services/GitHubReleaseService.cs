using System.Text.Json;
using System.Text.Json.Serialization;

namespace JundotSite.Services;

public class GitHubRelease
{
    [JsonPropertyName("tag_name")]
    public string TagName { get; set; } = string.Empty;

    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;

    [JsonPropertyName("body")]
    public string? Body { get; set; }

    [JsonPropertyName("prerelease")]
    public bool Prerelease { get; set; }

    [JsonPropertyName("published_at")]
    public DateTime? PublishedAt { get; set; }

    [JsonPropertyName("html_url")]
    public string HtmlUrl { get; set; } = string.Empty;

    [JsonPropertyName("assets")]
    public List<GitHubAsset>? Assets { get; set; }
}

public class GitHubAsset
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;

    [JsonPropertyName("browser_download_url")]
    public string BrowserDownloadUrl { get; set; } = string.Empty;

    [JsonPropertyName("size")]
    public long Size { get; set; }

    [JsonPropertyName("download_count")]
    public int DownloadCount { get; set; }
}

public class GitHubReleaseService
{
    private readonly HttpClient _httpClient;
    private readonly IConfiguration _configuration;

    public GitHubReleaseService(HttpClient httpClient, IConfiguration configuration)
    {
        _httpClient = httpClient;
        _configuration = configuration;
    }

    public async Task<List<GitHubRelease>?> GetReleasesAsync(string owner, string repo, int count = 10)
    {
        try
        {
            var url = $"https://api.github.com/repos/{owner}/{repo}/releases?per_page={count}";
            _httpClient.DefaultRequestHeaders.UserAgent.ParseAdd("JundotSite");

            var token = _configuration["GitHub:Token"];
            if (!string.IsNullOrEmpty(token))
            {
                _httpClient.DefaultRequestHeaders.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", token);
            }

            var response = await _httpClient.GetAsync(url);
            if (!response.IsSuccessStatusCode)
            {
                return null;
            }

            var content = await response.Content.ReadAsStringAsync();
            var options = new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true
            };
            var releases = JsonSerializer.Deserialize<List<GitHubRelease>>(content, options);
            return releases;
        }
        catch
        {
            return null;
        }
    }

    public async Task<GitHubRelease?> GetLatestReleaseAsync(string owner, string repo)
    {
        var releases = await GetReleasesAsync(owner, repo, 5);
        return releases?.FirstOrDefault(r => !r.Prerelease) ?? releases?.FirstOrDefault();
    }
}
