using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;
using JundotSite.Services;

namespace JundotSite.Pages;

public class DocsModel : PageModel
{
    private readonly ApplicationDbContext _context;
    private readonly IWebHostEnvironment _environment;
    public string CurrentDoc { get; set; } = string.Empty;
    public string CurrentLanguage { get; set; } = "zh-CN";
    public string CurrentLanguageLabel => SupportedLanguages.First(l => l.Id == CurrentLanguage).Label;
    public string? CurrentSection { get; set; }
    public string CurrentDocHtml { get; set; } = string.Empty;
    public List<DocLanguageOption> SupportedLanguages { get; } = GetSupportedLanguages();
    public List<DocVideo> DocVideos { get; set; } = new();

    public DocsModel(ApplicationDbContext context, IWebHostEnvironment environment)
    {
        _context = context;
        _environment = environment;
    }

    public async Task OnGetAsync(string? doc = "index", string? section = null, string? lang = null)
    {
        var normalizedDoc = NormalizeDoc(doc);
        CurrentDoc = normalizedDoc;
        CurrentSection = section;
        CurrentLanguage = NormalizeLanguage(lang);

        ViewData["Title"] = GetDocTitle(normalizedDoc);
        CurrentDocHtml = await LoadDocHtmlAsync(normalizedDoc, CurrentLanguage);

        // Load published videos associated with this doc
        DocVideos = await _context.DocVideos
            .Where(v => v.DocId == normalizedDoc && v.IsPublished)
            .OrderBy(v => v.SortOrder)
            .ThenByDescending(v => v.CreatedAt)
            .ToListAsync();
    }

    private string GetDocTitle(string doc)
    {
        return DocumentationCatalog.FindPage(_environment.ContentRootPath, doc)?.Title ?? "文档";
    }

    public Dictionary<string, List<DocumentationPage>> GetDocCategories()
    {
        return DocumentationCatalog.GetCategories(_environment.ContentRootPath);
    }

    private async Task<string> LoadDocHtmlAsync(string doc, string language)
    {
        var page = DocumentationCatalog.FindPage(_environment.ContentRootPath, doc);
        if (page == null)
        {
            return "<h1>文档</h1><p>选择左侧的文档开始阅读</p>";
        }

        var docsRoot = Path.Combine(_environment.ContentRootPath, "Pages", "Docs");
        var fullPath = GetDocPath(docsRoot, page.FileName, language);
        var rootPath = Path.GetFullPath(docsRoot);

        if (!fullPath.StartsWith(rootPath, StringComparison.OrdinalIgnoreCase))
        {
            return "<h1>文档未找到</h1><p>当前文档文件不存在。</p>";
        }

        if (!System.IO.File.Exists(fullPath) && language != "zh-CN")
        {
            fullPath = GetDocPath(docsRoot, page.FileName, "zh-CN");
        }

        if (!System.IO.File.Exists(fullPath))
        {
            return "<h1>文档未找到</h1><p>当前文档文件不存在。</p>";
        }

        return await System.IO.File.ReadAllTextAsync(fullPath);
    }

    private static string GetDocPath(string docsRoot, string fileName, string language)
    {
        return language == "zh-CN"
            ? Path.GetFullPath(Path.Combine(docsRoot, fileName))
            : Path.GetFullPath(Path.Combine(docsRoot, language, fileName));
    }

    private static string NormalizeDoc(string? doc)
    {
        if (string.IsNullOrWhiteSpace(doc))
        {
            return "index";
        }

        return doc.Trim();
    }

    private static string NormalizeLanguage(string? language)
    {
        if (string.IsNullOrWhiteSpace(language))
        {
            return "zh-CN";
        }

        return GetSupportedLanguages().Any(l => l.Id.Equals(language, StringComparison.OrdinalIgnoreCase))
            ? GetSupportedLanguages().First(l => l.Id.Equals(language, StringComparison.OrdinalIgnoreCase)).Id
            : "zh-CN";
    }

    private static List<DocLanguageOption> GetSupportedLanguages()
    {
        return new()
        {
            new("zh-CN", "简体中文", "zh-CN"),
            new("en", "English", "en")
        };
    }
}
public record DocLanguageOption(string Id, string Label, string HtmlLang);
