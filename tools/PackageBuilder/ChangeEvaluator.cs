using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace JundotPackageBuilder;

/// <summary>
/// Generates a dual-scope change evaluation report:
///   1. Source change report  — based on `git diff --name-status`
///   2. Package file report   — based on `update-manifest.json`
///
/// The combined markdown report is saved to artifacts/reports/.
/// </summary>
public static class ChangeEvaluator
{
	// ── Public API ──────────────────────────────────────────────

	/// <summary>
	/// Evaluate changes and write a combined report.
	/// Returns the path to the generated report, or null on failure.
	/// </summary>
	public static string? Evaluate(
		string repoRoot,
		string packageName,
		string version,
		string manifestPath,
		string? changelog = null)
	{
		var reportsDir = Path.Combine(repoRoot, "artifacts", "reports");
		Directory.CreateDirectory(reportsDir);

		var reportPath = Path.Combine(reportsDir, $"{packageName}-change-evaluation.md");

		var sb = new StringBuilder();
		sb.AppendLine($"# Change Evaluation Report");
		sb.AppendLine();
		sb.AppendLine($"**Package:** {packageName}");
		sb.AppendLine($"**Version:** {version}");
		sb.AppendLine($"**Generated:** {DateTime.UtcNow:yyyy-MM-dd HH:mm:ss} UTC");
		sb.AppendLine();

		// ── Section 1: Source changes ──
		sb.AppendLine("## 1. Source Changes (git diff)");
		sb.AppendLine();
		BuildSourceReport(repoRoot, sb);

		// ── Section 2: Package files ──
		sb.AppendLine("## 2. Package Files (manifest)");
		sb.AppendLine();
		BuildManifestReport(manifestPath, sb);

		// ── Section 3: Risk summary ──
		sb.AppendLine("## 3. Risk Summary");
		sb.AppendLine();
		BuildRiskSummary(repoRoot, sb);

		// ── Section 4: Changelog ──
		if (!string.IsNullOrWhiteSpace(changelog))
		{
			sb.AppendLine("## 4. Changelog");
			sb.AppendLine();
			sb.AppendLine(changelog);
			sb.AppendLine();
		}

		File.WriteAllText(reportPath, sb.ToString());
		return reportPath;
	}

	// ── Source report ───────────────────────────────────────────

	private static void BuildSourceReport(string repoRoot, StringBuilder sb)
	{
		var currentTag = GetCurrentVersionTag(repoRoot);
		var previousTag = FindPreviousVersionTag(repoRoot, currentTag);
		var head = RunGitCommand(repoRoot, "rev-parse", "--short", "HEAD").FirstOrDefault() ?? "HEAD";

		if (!string.IsNullOrEmpty(previousTag))
		{
			sb.AppendLine($"**Compared with previous version tag:** `{previousTag}` → `{head}`");
			if (!string.IsNullOrEmpty(currentTag))
				sb.AppendLine($"**Current version tag:** `{currentTag}`");
			sb.AppendLine();

			var commitLines = RunGitCommand(repoRoot, "log", "--pretty=format:%h\t%s", $"{previousTag}..HEAD");
			if (commitLines.Length > 0)
			{
				sb.AppendLine("### Commits since previous version");
				sb.AppendLine();
				foreach (var line in commitLines.Take(80))
				{
					var parts = line.Split('\t', 2);
					if (parts.Length == 2)
						sb.AppendLine($"- `{parts[0]}` {parts[1]}");
					else
						sb.AppendLine($"- {line}");
				}
				if (commitLines.Length > 80)
					sb.AppendLine($"- ... {commitLines.Length - 80} more commit(s)");
				sb.AppendLine();
			}

			var rangeDiffLines = RunGitCommand(repoRoot, "diff", "--name-status", $"{previousTag}..HEAD");
			AppendDiffTable(sb, rangeDiffLines, "Files changed since previous version");
		}

		var diffLines = RunGitCommand(repoRoot, "diff", "--name-status", "HEAD");
		if (diffLines.Length == 0 && string.IsNullOrEmpty(previousTag))
		{
			sb.AppendLine("> No source differences detected.");
			sb.AppendLine();
			return;
		}

		if (diffLines.Length > 0)
		{
			AppendDiffTable(sb, diffLines, "Uncommitted working tree changes");
		}
		else
		{
			sb.AppendLine("> No uncommitted working tree changes detected.");
			sb.AppendLine();
		}
	}

	private static void AppendDiffTable(StringBuilder sb, string[] diffLines, string title)
	{
		sb.AppendLine($"### {title}");
		sb.AppendLine();

		if (diffLines.Length == 0)
		{
			sb.AppendLine("> No file differences detected.");
			sb.AppendLine();
			return;
		}

		sb.AppendLine("| Status | File | Risk | Notes |");
		sb.AppendLine("|--------|------|------|-------|");

		foreach (var line in diffLines)
		{
			if (string.IsNullOrWhiteSpace(line)) continue;

			// git diff --name-status format: "M\tpath/to/file"
			var parts = line.Split('\t');
			var status = parts.Length > 0 ? parts[0].Trim() : "?";
			var path = parts.Length > 1 ? parts[1].Trim() : line.Trim();
			var risk = ClassifyRisk(path, status);
			var notes = GetStatusNotes(status);

			sb.AppendLine($"| {status} | `{path}` | {risk} | {notes} |");
		}

		sb.AppendLine();
		sb.AppendLine($"**Total files changed:** {diffLines.Length}");
		sb.AppendLine();
	}

	// ── Manifest report ─────────────────────────────────────────

	private static void BuildManifestReport(string manifestPath, StringBuilder sb)
	{
		if (!File.Exists(manifestPath))
		{
			sb.AppendLine("> No manifest file found. Run a build with `GenerateUpdateManifest = true` first.");
			sb.AppendLine();
			return;
		}

		try
		{
			var json = File.ReadAllText(manifestPath);
			var manifest = JsonSerializer.Deserialize<UpdateManifest>(json);
			if (manifest == null)
			{
				sb.AppendLine("> Could not parse manifest file.");
				sb.AppendLine();
				return;
			}

			sb.AppendLine($"- **Platform:** {manifest.Platform}");
			sb.AppendLine($"- **Target:** {manifest.Target}");
			sb.AppendLine($"- **Arch:** {manifest.Arch}");
			sb.AppendLine($"- **Package Size:** {FormatBytes(manifest.PackageSize)}");
			sb.AppendLine($"- **SHA256:** `{manifest.Sha256}`");
			sb.AppendLine($"- **Download URL:** {(string.IsNullOrEmpty(manifest.DownloadUrl) ? "(not set)" : manifest.DownloadUrl)}");
			sb.AppendLine();

			if (manifest.FileList is { Count: > 0 })
			{
				sb.AppendLine("| File | Size | SHA256 | Required |");
				sb.AppendLine("|------|------|--------|----------|");

				foreach (var file in manifest.FileList)
				{
					sb.AppendLine($"| `{file.Path}` | {FormatBytes(file.Size)} | `{file.Sha256[..Math.Min(12, file.Sha256.Length)]}...` | {(file.Required ? "Yes" : "No")} |");
				}

				sb.AppendLine();
				sb.AppendLine($"**Total files in package:** {manifest.FileList.Count}");
				sb.AppendLine();
			}
		}
		catch (Exception ex)
		{
			sb.AppendLine($"> Error reading manifest: {ex.Message}");
			sb.AppendLine();
		}
	}

	// ── Risk summary ────────────────────────────────────────────

	private static void BuildRiskSummary(string repoRoot, StringBuilder sb)
	{
		var currentTag = GetCurrentVersionTag(repoRoot);
		var previousTag = FindPreviousVersionTag(repoRoot, currentTag);
		var diffLines = !string.IsNullOrEmpty(previousTag)
			? RunGitCommand(repoRoot, "diff", "--name-only", $"{previousTag}..HEAD")
			: RunGitCommand(repoRoot, "diff", "--name-only", "HEAD");
		bool hasCoreChanged = false;
		bool hasEditorChanged = false;
		bool hasToolsChanged = false;

		foreach (var line in diffLines)
		{
			if (line.StartsWith("core/")) hasCoreChanged = true;
			else if (line.StartsWith("editor/")) hasEditorChanged = true;
			else if (line.StartsWith("tools/")) hasToolsChanged = true;
		}

		sb.AppendLine("| Category | Changed | Risk Level |");
		sb.AppendLine("|----------|---------|------------|");
		sb.AppendLine($"| Core engine (`core/`) | {(hasCoreChanged ? "Yes" : "No")} | {(hasCoreChanged ? "HIGH" : "Low")} |");
		sb.AppendLine($"| Editor (`editor/`) | {(hasEditorChanged ? "Yes" : "No")} | {(hasEditorChanged ? "MEDIUM" : "Low")} |");
		sb.AppendLine($"| Tools (`tools/`) | {(hasToolsChanged ? "Yes" : "No")} | {(hasToolsChanged ? "Low" : "Low")} |");
		sb.AppendLine();

		if (hasCoreChanged)
		{
			sb.AppendLine("> ⚠ **Warning:** Core engine files were modified. Thorough testing is recommended before publishing.");
			sb.AppendLine();
		}
	}

	// ── Helpers ─────────────────────────────────────────────────

	private static string[] RunGitCommand(string repoRoot, params string[] args)
	{
		try
		{
			var psi = new ProcessStartInfo("git")
			{
				WorkingDirectory = repoRoot,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				UseShellExecute = false,
				CreateNoWindow = true
			};
			foreach (var arg in args)
				psi.ArgumentList.Add(arg);

			using var proc = Process.Start(psi);
			if (proc == null) return Array.Empty<string>();

			proc.WaitForExit(5000);
			var output = proc.StandardOutput.ReadToEnd();
			return output.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
		}
		catch
		{
			return Array.Empty<string>();
		}
	}

	private static string? GetCurrentVersionTag(string repoRoot)
	{
		var version = ReadVersionString(repoRoot);
		return string.IsNullOrEmpty(version) ? null : $"v{version}";
	}

	private static string? FindPreviousVersionTag(string repoRoot, string? currentTag)
	{
		var tags = RunGitCommand(repoRoot, "tag", "--list", "v*", "--sort=-v:refname");
		if (tags.Length == 0)
			return null;

		if (string.IsNullOrEmpty(currentTag))
			return tags.FirstOrDefault();

		var currentVersion = ParseVersionTag(currentTag);
		foreach (var tag in tags)
		{
			if (string.Equals(tag, currentTag, StringComparison.OrdinalIgnoreCase))
				continue;

			var version = ParseVersionTag(tag);
			if (currentVersion != null && version != null && CompareVersions(version, currentVersion) >= 0)
				continue;

			return tag;
		}

		return tags.FirstOrDefault(t => !string.Equals(t, currentTag, StringComparison.OrdinalIgnoreCase));
	}

	private static string? ReadVersionString(string repoRoot)
	{
		var versionPy = Path.Combine(repoRoot, "version.py");
		if (!File.Exists(versionPy))
			return null;

		int? major = null, minor = null, patch = null;
		string status = "stable";
		foreach (var line in File.ReadLines(versionPy))
		{
			var trimmed = line.Trim();
			if (trimmed.StartsWith("major = ") && int.TryParse(trimmed["major = ".Length..], out var maj)) major = maj;
			else if (trimmed.StartsWith("minor = ") && int.TryParse(trimmed["minor = ".Length..], out var min)) minor = min;
			else if (trimmed.StartsWith("patch = ") && int.TryParse(trimmed["patch = ".Length..], out var pat)) patch = pat;
			else if (trimmed.StartsWith("status = ")) status = trimmed["status = ".Length..].Trim().Trim('"', '\'');
		}

		if (major == null || minor == null || patch == null)
			return null;

		return string.Equals(status, "stable", StringComparison.OrdinalIgnoreCase)
			? $"{major}.{minor}.{patch}"
			: $"{major}.{minor}.{patch}-{status}";
	}

	private static int[]? ParseVersionTag(string tag)
	{
		var text = tag.TrimStart('v', 'V');
		var dash = text.IndexOf('-');
		if (dash >= 0)
			text = text[..dash];

		var parts = text.Split('.');
		if (parts.Length < 3)
			return null;

		var values = new int[3];
		for (var i = 0; i < 3; i++)
		{
			if (!int.TryParse(parts[i], out values[i]))
				return null;
		}
		return values;
	}

	private static int CompareVersions(int[] left, int[] right)
	{
		for (var i = 0; i < Math.Min(left.Length, right.Length); i++)
		{
			var cmp = left[i].CompareTo(right[i]);
			if (cmp != 0)
				return cmp;
		}
		return left.Length.CompareTo(right.Length);
	}

	private static string ClassifyRisk(string path, string status)
	{
		if (status == "D") return "N/A (deleted)";
		if (path.StartsWith("core/")) return "HIGH";
		if (path.StartsWith("editor/")) return "MEDIUM";
		if (path.StartsWith("platform/")) return "HIGH";
		if (path.StartsWith("modules/")) return "MEDIUM";
		if (path.StartsWith("scene/")) return "MEDIUM";
		if (path.EndsWith(".h")) return "MEDIUM";
		return "Low";
	}

	private static string GetStatusNotes(string status) => status switch
	{
		"A" => "New file",
		"M" => "Modified",
		"D" => "Deleted",
		"R" => "Renamed",
		"C" => "Copied",
		_ => "Unknown"
	};

	private static string FormatBytes(long bytes)
	{
		return bytes switch
		{
			>= 1_048_576 => $"{bytes / 1_048_576.0:F1} MB",
			>= 1024 => $"{bytes / 1024.0:F1} KB",
			_ => $"{bytes} B"
		};
	}

	// ── Minimal manifest model for deserialization ──────────────

	private class UpdateManifest
	{
		[JsonPropertyName("package_name")] public string PackageName { get; set; } = "";
		[JsonPropertyName("platform")] public string Platform { get; set; } = "";
		[JsonPropertyName("target")] public string Target { get; set; } = "";
		[JsonPropertyName("arch")] public string Arch { get; set; } = "";
		[JsonPropertyName("package_size")] public long PackageSize { get; set; }
		[JsonPropertyName("sha256")] public string Sha256 { get; set; } = "";
		[JsonPropertyName("download_url")] public string DownloadUrl { get; set; } = "";
		[JsonPropertyName("files")] public List<FileEntry>? FileList { get; set; }
	}

	private class FileEntry
	{
		[JsonPropertyName("path")] public string Path { get; set; } = "";
		[JsonPropertyName("size")] public long Size { get; set; }
		[JsonPropertyName("sha256")] public string Sha256 { get; set; } = "";
		[JsonPropertyName("required")] public bool Required { get; set; } = true;
	}
}
