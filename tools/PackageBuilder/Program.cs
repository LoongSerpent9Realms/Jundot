using System.Text;
using System.Text.Json;

namespace JundotPackageBuilder;

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        // Register code pages provider to support GBK/Shift-JIS/etc.
        // Required for correctly decoding MSVC/cl.exe output on non-English Windows.
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

        if (TryRunAiBuild(args))
            return;

        ApplicationConfiguration.Initialize();
        if (!IsAiPackageBuilderSession(args))
        {
            MessageBox.Show(
                "Jundot Package Builder is reserved for AI/developer automation sessions.",
                "Jundot Package Builder",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
            return;
        }

        Application.Run(new MainForm());
    }

    private static bool IsAiPackageBuilderSession(string[] args)
    {
        if (GetAiBuildRequestPath(args) != null)
            return true;

        if (args.Any(a => string.Equals(a, "--ai-package-builder", StringComparison.OrdinalIgnoreCase)))
            return true;

        var value = Environment.GetEnvironmentVariable("JUNDOT_AI_PACKAGE_BUILDER") ?? "";
        return value.Equals("1", StringComparison.OrdinalIgnoreCase) ||
               value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
               value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
               value.Equals("on", StringComparison.OrdinalIgnoreCase);
    }

    private static bool TryRunAiBuild(string[] args)
    {
        var requestPath = GetAiBuildRequestPath(args);
        if (requestPath == null)
            return false;

        RunAiBuildAsync(requestPath).GetAwaiter().GetResult();
        return true;
    }

    private static string? GetAiBuildRequestPath(string[] args)
    {
        for (var i = 0; i < args.Length; i++)
        {
            if (string.Equals(args[i], "--ai-build", StringComparison.OrdinalIgnoreCase))
                return i + 1 < args.Length ? args[i + 1] : "";
        }
        return null;
    }

    private static async Task RunAiBuildAsync(string requestPath)
    {
        var statusPath = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(requestPath)) ?? ".", "ai_build_status.json");
        try
        {
            if (string.IsNullOrWhiteSpace(requestPath) || !File.Exists(requestPath))
                throw new FileNotFoundException("AI build request file was not found.", requestPath);

            using var doc = JsonDocument.Parse(File.ReadAllText(requestPath, Encoding.UTF8));
            var root = doc.RootElement;
            var repoRoot = GetString(root, "repo_root", DetectRepoRoot());
            var cfg = new BuildConfig
            {
                RepoRoot = repoRoot,
                Target = GetString(root, "target", "editor"),
                PlatformName = GetString(root, "platform", "windows"),
                Arch = GetString(root, "arch", "x86_64"),
                SkipBuild = GetBool(root, "skip_build", false),
                Mono = GetBool(root, "mono", false),
                AutoUpdateVersion = GetBool(root, "auto_update_version", true),
                GenerateUpdateManifest = GetBool(root, "generate_update_manifest", true),
                Jobs = GetInt(root, "jobs", 0),
                OutputDir = GetString(root, "output_dir", "artifacts/packages"),
                LogDir = GetString(root, "log_dir", "artifacts/logs"),
                PackageName = GetString(root, "package_name", ""),
                ExtraSConsArgs = GetString(root, "extra_scons_args", "")
            };

            WriteStatus(statusPath, "running", "PackageBuilder started.", "", "", "");

            var manager = new BuildManager(cfg.RepoRoot);
            var engine = new BuildEngine(cfg) { BuildManager = manager };
            engine.ProgressChanged += (_, e) =>
            {
                if (!string.IsNullOrWhiteSpace(e.Message))
                    WriteStatus(statusPath, "running", e.Message, "", "", "");
            };

            var ok = await engine.RunAsync(CancellationToken.None);
            var record = manager.GetAllBuilds()
                .Where(r => string.Equals(r.PackageName, engine.PackageName, StringComparison.OrdinalIgnoreCase))
                .OrderByDescending(r => r.CreatedAt)
                .FirstOrDefault();

            if (ok && record != null)
            {
                var manifestPath = Path.Combine(record.PackageDir ?? "", "update-manifest.json");
                WriteStatus(statusPath, "success", "Package created.", record.ZipPath, manifestPath, record.BuildLogPath);
            }
            else
            {
                WriteStatus(statusPath, "failed", "Package build failed. Read the build log and fix the reported errors.", "", "", record?.BuildLogPath ?? "");
            }
        }
        catch (Exception ex)
        {
            WriteStatus(statusPath, "failed", ex.Message, "", "", "");
        }
    }

    private static string DetectRepoRoot()
    {
        var dir = AppContext.BaseDirectory;
        while (!string.IsNullOrEmpty(dir) && !File.Exists(Path.Combine(dir, "SConstruct")))
        {
            var parent = Path.GetDirectoryName(dir);
            if (string.IsNullOrEmpty(parent) || parent == dir)
                break;
            dir = parent;
        }
        return dir;
    }

    private static string GetString(JsonElement root, string name, string fallback)
    {
        return root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? fallback
            : fallback;
    }

    private static bool GetBool(JsonElement root, string name, bool fallback)
    {
        return root.TryGetProperty(name, out var value)
            ? value.ValueKind switch
            {
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                JsonValueKind.String => bool.TryParse(value.GetString(), out var parsed) ? parsed : fallback,
                _ => fallback
            }
            : fallback;
    }

    private static int GetInt(JsonElement root, string name, int fallback)
    {
        return root.TryGetProperty(name, out var value) && value.TryGetInt32(out var parsed) ? parsed : fallback;
    }

    private static void WriteStatus(string path, string state, string message, string zipPath, string manifestPath, string buildLogPath)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".");
        var status = new Dictionary<string, object?>
        {
            ["state"] = state,
            ["message"] = message,
            ["zip_path"] = zipPath,
            ["manifest_path"] = manifestPath,
            ["build_log_path"] = buildLogPath,
            ["updated_at"] = DateTimeOffset.Now.ToString("O")
        };
        File.WriteAllText(path, JsonSerializer.Serialize(status, new JsonSerializerOptions { WriteIndented = true }), Encoding.UTF8);
    }
}
