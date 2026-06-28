using System.ComponentModel.DataAnnotations;
using JundotSite.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace JundotSite.Pages.User;

public class DocsModel : PageModel
{
    private readonly IWebHostEnvironment _environment;
    private readonly AuthService _authService;

    public DocsModel(IWebHostEnvironment environment, AuthService authService)
    {
        _environment = environment;
        _authService = authService;
    }

    public List<DocumentationPage> Documents { get; set; } = new();
    public List<DocLanguageOption> SupportedLanguages { get; set; } = GetSupportedLanguages();
    public string CurrentDocId { get; set; } = "index";
    public string CurrentDocTitle { get; set; } = "开始使用";
    public string CurrentLanguage { get; set; } = "zh-CN";
    public string CurrentLanguageLabel => SupportedLanguages.First(l => l.Id == CurrentLanguage).Label;
    public string? SuccessMessage { get; set; }
    public string? ErrorMessage { get; set; }

    [BindProperty]
    public DocEditInput Input { get; set; } = new();

    public class DocEditInput
    {
        public string? DocId { get; set; } = "index";
        public string? Language { get; set; } = "zh-CN";
        public string? Html { get; set; } = string.Empty;
    }

    public async Task<IActionResult> OnGetAsync(string doc = "index", string? lang = null)
    {
        // 检查用户是否已登录
        var userId = HttpContext.Session.GetString("UserId");
        if (string.IsNullOrEmpty(userId))
        {
            return RedirectToPage("/Login");
        }

        ViewData["Title"] = "文档管理";
        await LoadDocAsync(doc, NormalizeLanguage(lang));
        return Page();
    }

    public async Task<IActionResult> OnPostAsync()
    {
        var userId = HttpContext.Session.GetString("UserId");
        if (string.IsNullOrEmpty(userId))
        {
            return RedirectToPage("/Login");
        }

        ViewData["Title"] = "文档管理";
        var language = NormalizeLanguage(Input.Language);

        if (string.IsNullOrWhiteSpace(Input.DocId) || string.IsNullOrWhiteSpace(Input.Html))
        {
            ErrorMessage = "文档内容不能为空";
            await LoadDocAsync(Input.DocId ?? "index", language);
            return Page();
        }

        var doc = FindDoc(Input.DocId);
        if (doc == null)
        {
            ErrorMessage = "文档不存在";
            await LoadDocAsync("index", language);
            return Page();
        }

        var path = GetSafeDocPath(doc, language);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await System.IO.File.WriteAllTextAsync(path, Input.Html);

        SuccessMessage = $"《{doc.Title}》已保存";
        await LoadDocAsync(doc.Id, language);
        return Page();
    }

    private async Task LoadDocAsync(string docId, string language)
    {
        Documents = GetDocuments();
        SupportedLanguages = GetSupportedLanguages();
        var doc = FindDoc(docId) ?? FindDoc("index")!;
        CurrentDocId = doc.Id;
        CurrentDocTitle = doc.Title;
        CurrentLanguage = NormalizeLanguage(language);

        var path = GetSafeDocPath(doc, CurrentLanguage);
        var html = System.IO.File.Exists(path) ? await System.IO.File.ReadAllTextAsync(path) : "";

        Input = new DocEditInput
        {
            DocId = doc.Id,
            Language = CurrentLanguage,
            Html = html
        };
    }

    private DocumentationPage? FindDoc(string docId)
    {
        return GetDocuments().FirstOrDefault(d => d.Id.Equals(docId, StringComparison.OrdinalIgnoreCase));
    }

    private List<DocumentationPage> GetDocuments()
    {
        return DocumentationCatalog.GetPages(_environment.ContentRootPath);
    }

    private static List<DocLanguageOption> GetSupportedLanguages()
    {
        return new()
        {
            new("zh-CN", "简体中文"),
            new("en", "English")
        };
    }

    private static string NormalizeLanguage(string? language)
    {
        if (string.IsNullOrWhiteSpace(language))
        {
            return "zh-CN";
        }

        var match = GetSupportedLanguages()
            .FirstOrDefault(l => l.Id.Equals(language, StringComparison.OrdinalIgnoreCase));

        return match?.Id ?? "zh-CN";
    }

    private string GetSafeDocPath(DocumentationPage doc, string language)
    {
        var docsRoot = Path.GetFullPath(Path.Combine(_environment.ContentRootPath, "Pages", "Docs"));
        var normalizedLanguage = NormalizeLanguage(language);
        var path = normalizedLanguage == "zh-CN"
            ? Path.GetFullPath(Path.Combine(docsRoot, doc.FileName))
            : Path.GetFullPath(Path.Combine(docsRoot, normalizedLanguage, doc.FileName));

        if (!path.StartsWith(docsRoot, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("Invalid documentation path.");
        }
        return path;
    }
}

public record DocLanguageOption(string Id, string Label);
