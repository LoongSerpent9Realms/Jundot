using System.Text.Json;
using System.Text.RegularExpressions;

namespace JundotSite.Services;

public static class DocumentationCatalog
{
    private static readonly List<DocumentationPage> BuiltInPages = new()
    {
        new("index", "开始使用", "_Index.cshtml", "Jundot 核心流程和文档入口", "入门", true),
        new("installation", "安装指南", "_Installation.cshtml", "下载、启动、AI 后端配置", "入门", true),
        new("quickstart", "快速入门", "_Quickstart.cshtml", "用 AI 创建并细化项目", "入门", true),
        new("current-version", "现版本说明", "_CurrentVersion.cshtml", "Jundot Engine 0.3.32 alpha 的功能入口和使用流程", "入门", true),
        new("scripting-api", "脚本 API 与 Godot 文档", "_ScriptingApi.cshtml", "Godot API 关系和速查", "脚本开发", true),
        new("ai-assistant", "AI 助手", "_AiAssistant.cshtml", "创建项目、修复问题和改引擎", "核心功能", true),
        new("hot-update", "更新与发布", "_HotUpdate.cshtml", "GitHub、Package Builder 与 Launcher", "核心功能", true),
        new("troubleshooting", "常见问题", "_Troubleshooting.cshtml", "安装、AI、脚本和更新排错", "参考", true)
    };

    public static List<DocumentationPage> GetPages(string contentRootPath)
    {
        var customPages = LoadCustomPages(contentRootPath);
        return BuiltInPages
            .Concat(customPages.Where(page => BuiltInPages.All(b => !b.Id.Equals(page.Id, StringComparison.OrdinalIgnoreCase))))
            .ToList();
    }

    public static DocumentationPage? FindPage(string contentRootPath, string? docId)
    {
        var normalized = string.IsNullOrWhiteSpace(docId) ? "index" : docId.Trim();
        return GetPages(contentRootPath).FirstOrDefault(page => page.Id.Equals(normalized, StringComparison.OrdinalIgnoreCase));
    }

    public static Dictionary<string, List<DocumentationPage>> GetCategories(string contentRootPath)
    {
        return GetPages(contentRootPath)
            .GroupBy(page => string.IsNullOrWhiteSpace(page.Category) ? "其他" : page.Category)
            .ToDictionary(group => group.Key, group => group.ToList());
    }

    public static List<DocumentationPage> LoadCustomPages(string contentRootPath)
    {
        var manifestPath = GetManifestPath(contentRootPath);
        if (!File.Exists(manifestPath))
        {
            return new();
        }

        try
        {
            var json = File.ReadAllText(manifestPath);
            return JsonSerializer.Deserialize<List<DocumentationPage>>(json, JsonOptions) ?? new();
        }
        catch
        {
            return new();
        }
    }

    public static void SaveCustomPages(string contentRootPath, List<DocumentationPage> pages)
    {
        var manifestPath = GetManifestPath(contentRootPath);
        Directory.CreateDirectory(Path.GetDirectoryName(manifestPath)!);
        var customPages = pages.Where(page => !page.IsBuiltIn).ToList();
        File.WriteAllText(manifestPath, JsonSerializer.Serialize(customPages, JsonOptions));
    }

    public static string GetManifestPath(string contentRootPath)
    {
        return Path.Combine(contentRootPath, "Pages", "Docs", "docs-manifest.json");
    }

    public static string NormalizeSlug(string? value)
    {
        var slug = Regex.Replace((value ?? string.Empty).Trim().ToLowerInvariant(), @"[^a-z0-9\-]+", "-");
        slug = Regex.Replace(slug, @"-{2,}", "-").Trim('-');
        return slug.Length > 80 ? slug[..80].Trim('-') : slug;
    }

    public static string CreateCustomFileName(string slug)
    {
        return Path.Combine("custom", $"{slug}.cshtml");
    }

    private static JsonSerializerOptions JsonOptions => new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };
}

public record DocumentationPage(
    string Id,
    string Title,
    string FileName,
    string Description,
    string Category,
    bool IsBuiltIn);
