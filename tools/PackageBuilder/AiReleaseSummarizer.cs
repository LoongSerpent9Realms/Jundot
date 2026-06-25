using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace JundotPackageBuilder;

/// <summary>
/// Summarizes the current build's version + changelog via an
/// AI chat completion API. Designed to work with the
/// MiMo-Code-jundot plugin (http://127.0.0.1:4096) as well as
/// any OpenAI-compatible endpoint.
///
/// The generated markdown body is then fed into
/// <see cref="GitHubReleasePublisher"/> so the resulting GitHub
/// Release has a human-friendly changelog automatically produced
/// from the raw commit / file / manifest data of this build.
/// </summary>
public class AiReleaseSummarizer
{
	private readonly PublishConfig _config;
	private readonly HttpClient _http;

	public AiReleaseSummarizer(PublishConfig config)
	{
		_config = config;
		_http = new HttpClient
		{
			Timeout = TimeSpan.FromMinutes(3)
		};
		_http.DefaultRequestHeaders.UserAgent.ParseAdd("Jundot-PackageBuilder/1.0");
	}

	// ── Public API ──────────────────────────────────────────────

	/// <summary>
	/// Produce an AI-summarized release body for the given build
	/// context. Returns null if summarization is disabled, missing
	/// credentials, or fails.
	/// </summary>
	public async Task<string?> SummarizeAsync(
		string version,
		string packageName,
		string changeEvaluationPath,
		string manifestPath,
		string rawChangelog,
		CancellationToken ct = default)
	{
		if (!_config.UseAiSummary)
		{
			LogInfo("AI summary disabled; skipping.");
			return null;
		}

		var apiKey = _config.GetAiApiKey();
		if (string.IsNullOrEmpty(apiKey))
		{
			LogError($"AI API key not set. Configure '{_config.AiTokenEnvVar}' or set API key in the publish config.");
			return null;
		}

		try
		{
			LogInfo($"Calling AI endpoint: {_config.AiBaseUrl} (model: {_config.AiModel})");

			var prompt = BuildPrompt(version, packageName, changeEvaluationPath, manifestPath, rawChangelog);

			var messages = new List<object>
			{
				new { role = "system", content = _config.GetAiSystemPrompt() },
				new { role = "user", content = prompt }
			};

			var payload = new
			{
				model = _config.AiModel,
				messages,
				temperature = _config.AiTemperature,
				max_tokens = _config.AiMaxTokens
			};

			var json = JsonSerializer.Serialize(payload);
			var request = new HttpRequestMessage(HttpMethod.Post, _config.AiBaseUrl.TrimEnd('/') + "/chat/completions")
			{
				Content = new StringContent(json, Encoding.UTF8, "application/json")
			};

			request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", apiKey);

			var response = await _http.SendAsync(request, ct);
			if (!response.IsSuccessStatusCode)
			{
				var errText = await response.Content.ReadAsStringAsync(ct);
				LogError($"AI request failed (HTTP {(int)response.StatusCode}): {errText}");
				return null;
			}

			var body = await response.Content.ReadAsStringAsync(ct);
			var doc = JsonDocument.Parse(body);

			var choice = doc.RootElement
				.GetProperty("choices")[0]
				.GetProperty("message")
				.GetProperty("content")
				.GetString();

			if (string.IsNullOrWhiteSpace(choice))
			{
				LogError("AI returned an empty summary.");
				return null;
			}

			LogInfo("AI summary generated successfully.");
			return choice.Trim();
		}
		catch (OperationCanceledException)
		{
			LogInfo("AI summary cancelled.");
			return null;
		}
		catch (Exception ex)
		{
			LogError($"AI summary failed: {ex.Message}");
			return null;
		}
	}

	// ── Prompt building ─────────────────────────────────────────

	private string BuildPrompt(
		string version,
		string packageName,
		string changeEvaluationPath,
		string manifestPath,
		string rawChangelog)
	{
		var sb = new StringBuilder();

		sb.AppendLine("You are the release notes writer for the Jundot engine (a Godot Engine fork).");
		sb.AppendLine("Based on the data below, produce a concise, well-structured GitHub Release body in English Markdown.");
		sb.AppendLine("Write what changed compared with the previous version. Prioritize commit subjects and source changes over package metadata.");
		sb.AppendLine("Do not invent features. Do not describe executable, zip, manifest, timestamp, hash, or package-size changes as product features.");
		sb.AppendLine("If the only detected changes are packaging metadata or build artifacts, say that no user-facing engine changes were detected.");
		sb.AppendLine("");
		sb.AppendLine("## Target output structure");
		sb.AppendLine("- A title line like `Release vX.Y.Z`.");
		sb.AppendLine("- `## What's Changed` section listing meaningful engine/editor/tool changes since the previous version.");
		sb.AppendLine("- `## Highlights` section for the 2-3 most important user-facing items, or `No user-facing highlights detected`.");
		sb.AppendLine("- `## Full Changelog` section with commit/file summaries from the source change evaluation.");
		sb.AppendLine("- `## Packaging` section for package artifacts, platform, manifest, and build timestamp details.");
		sb.AppendLine("- If there is a risk summary, include it under `## Risk Notes`.");
		sb.AppendLine("Do NOT wrap your reply in prose outside the Markdown body — output Markdown only.");
		sb.AppendLine("");
		sb.AppendLine("## Build metadata");
		sb.AppendLine($"- Version: {version}");
		sb.AppendLine($"- Package: {packageName}");
		sb.AppendLine($"- Platform: {_config.Owner}/{_config.Repo}");
		sb.AppendLine($"- Generated at (UTC): {DateTime.UtcNow:yyyy-MM-dd HH:mm:ss}");
		sb.AppendLine("");

		if (File.Exists(changeEvaluationPath))
		{
			sb.AppendLine("## Source change evaluation report");
			sb.AppendLine("```");
			try
			{
				sb.AppendLine(File.ReadAllText(changeEvaluationPath));
			}
			catch
			{
				sb.AppendLine("(unable to read evaluation report)");
			}
			sb.AppendLine("```");
			sb.AppendLine("");
		}

		if (File.Exists(manifestPath))
		{
			sb.AppendLine("## Package manifest (excerpt)");
			sb.AppendLine("```");
			try
			{
				var lines = File.ReadLines(manifestPath).Take(40);
				foreach (var l in lines) sb.AppendLine(l);
			}
			catch
			{
				sb.AppendLine("(unable to read manifest)");
			}
			sb.AppendLine("```");
			sb.AppendLine("");
		}

		if (!string.IsNullOrWhiteSpace(rawChangelog))
		{
			sb.AppendLine("## Raw user-provided changelog");
			sb.AppendLine("```");
			sb.AppendLine(rawChangelog);
			sb.AppendLine("```");
			sb.AppendLine("");
		}

		sb.AppendLine("Now produce the release body Markdown.");
		return sb.ToString();
	}

	// ── Logging ─────────────────────────────────────────────────

	public event EventHandler<string>? LogMessage;

	private void LogInfo(string msg) => LogMessage?.Invoke(this, $"[INFO] {msg}");
	private void LogError(string msg) => LogMessage?.Invoke(this, $"[ERROR] {msg}");
}
