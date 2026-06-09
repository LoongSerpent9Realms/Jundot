using System.Text.Json;
using System.Text.Json.Serialization;

namespace JundotPackageBuilder;

/// <summary>
/// Configuration for publishing updates to GitHub Releases.
/// </summary>
public class PublishConfig
{
    [JsonPropertyName("owner")]
    public string Owner { get; set; } = "LoongSerpent9Realms";

    [JsonPropertyName("repo")]
    public string Repo { get; set; } = "Jundot";

    [JsonPropertyName("token_env_var")]
    public string TokenEnvVar { get; set; } = "GITHUB_TOKEN";

    [JsonPropertyName("release_tag")]
    public string ReleaseTag { get; set; } = "";

    [JsonPropertyName("release_name")]
    public string ReleaseName { get; set; } = "";

    [JsonPropertyName("release_body")]
    public string ReleaseBody { get; set; } = "";

    [JsonPropertyName("draft")]
    public bool Draft { get; set; } = true;

    [JsonPropertyName("prerelease")]
    public bool Prerelease { get; set; } = false;

    [JsonPropertyName("grayscale_percentage")]
    public int GrayscalePercentage { get; set; } = 100;

    [JsonPropertyName("mandatory")]
    public bool Mandatory { get; set; } = false;

    [JsonPropertyName("changelog")]
    public string? Changelog { get; set; }

    [JsonPropertyName("dry_run")]
    public bool DryRun { get; set; } = false;

    // ---- Helper ----

    public static PublishConfig Load(string repoRoot)
    {
        var path = Path.Combine(repoRoot, "artifacts", "publish-config.json");
        if (!File.Exists(path))
        {
            return new PublishConfig();
        }

        var json = File.ReadAllText(path);
        return JsonSerializer.Deserialize<PublishConfig>(json) ?? new PublishConfig();
    }

    public void Save(string repoRoot)
    {
        var dir = Path.Combine(repoRoot, "artifacts");
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, "publish-config.json");

        var options = new JsonSerializerOptions { WriteIndented = true };
        var json = JsonSerializer.Serialize(this, options);
        File.WriteAllText(path, json);
    }

    public string GetToken()
    {
        return Environment.GetEnvironmentVariable(TokenEnvVar) ?? "";
    }
}
