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

    /// <summary>Inline GitHub token. Leave empty to read from TokenEnvVar environment variable instead.</summary>
    [JsonPropertyName("token")]
    public string Token { get; set; } = "";

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

    // ── AI (MiMo-Code-jundot) summary settings ──────────────

    /// <summary>When true, call the AI to generate release body text.</summary>
    [JsonPropertyName("use_ai_summary")]
    public bool UseAiSummary { get; set; } = true;

    /// <summary>OpenAI-compatible base URL. Defaults to the MiMo-Code-jundot local plugin endpoint.</summary>
    [JsonPropertyName("ai_base_url")]
    public string AiBaseUrl { get; set; } = "http://127.0.0.1:4096/v1";

    /// <summary>Model name. Fallback: gpt-4.1.</summary>
    [JsonPropertyName("ai_model")]
    public string AiModel { get; set; } = "mimocode-jundot";

    /// <summary>Inline API key. Leave empty to read from AiTokenEnvVar.</summary>
    [JsonPropertyName("ai_api_key")]
    public string AiApiKey { get; set; } = "";

    /// <summary>Env var to read the AI API key from when AiApiKey is empty.</summary>
    [JsonPropertyName("ai_token_env_var")]
    public string AiTokenEnvVar { get; set; } = "MIMOCODE_API_KEY";

    [JsonPropertyName("ai_temperature")]
    public double AiTemperature { get; set; } = 0.3;

    [JsonPropertyName("ai_max_tokens")]
    public int AiMaxTokens { get; set; } = 1500;

    /// <summary>Optional override system prompt. Leave empty to use default release-notes prompt.</summary>
    [JsonPropertyName("ai_system_prompt")]
    public string AiSystemPrompt { get; set; } = "";

    // ---- helpers ----

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
        if (!string.IsNullOrWhiteSpace(Token))
            return Token;
        return Environment.GetEnvironmentVariable(TokenEnvVar) ?? "";
    }

    public string GetAiApiKey()
    {
        if (!string.IsNullOrWhiteSpace(AiApiKey))
            return AiApiKey;
        return Environment.GetEnvironmentVariable(AiTokenEnvVar) ?? "";
    }

    public string GetAiSystemPrompt()
    {
        if (!string.IsNullOrWhiteSpace(AiSystemPrompt))
            return AiSystemPrompt;
        return "You are a senior release notes engineer for the Jundot engine. " +
               "Write concise, well-structured Markdown suitable for a GitHub Release body. " +
               "Focus on user-facing changes and do NOT include boilerplate thank-you paragraphs unless the input explicitly contains contributor names.";
    }
}
