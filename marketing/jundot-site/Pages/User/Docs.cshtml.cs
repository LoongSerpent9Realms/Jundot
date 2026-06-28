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
<<<<<<< HEAD
    public bool IsCurrentDocBuiltIn { get; set; } = true;
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
    public string? SuccessMessage { get; set; }
    public string? ErrorMessage { get; set; }

    [BindProperty]
    public DocEditInput Input { get; set; } = new();

<<<<<<< HEAD
    [BindProperty]
    public DocCreateInput CreateInput { get; set; } = new();

    [BindProperty]
    public string? CustomCss { get; set; } = string.Empty;

    public string ActiveTab { get; set; } = "content";

=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
    public class DocEditInput
    {
        public string? DocId { get; set; } = "index";
        public string? Language { get; set; } = "zh-CN";
        public string? Html { get; set; } = string.Empty;
    }

<<<<<<< HEAD
    public class DocCreateInput
    {
        [StringLength(80)]
        public string? Slug { get; set; } = string.Empty;

        [StringLength(120)]
        public string? Title { get; set; } = string.Empty;

        [StringLength(80)]
        public string? Category { get; set; } = "参考";

        [StringLength(180)]
        public string? Description { get; set; } = string.Empty;

        public string? Language { get; set; } = "zh-CN";
    }

    public async Task<IActionResult> OnGetAsync(string doc = "index", string? lang = null)
    {
=======
    public async Task<IActionResult> OnGetAsync(string doc = "index", string? lang = null)
    {
        // 检查用户是否已登录
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
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

<<<<<<< HEAD
    public async Task<IActionResult> OnPostCreateAsync()
    {
        var userId = HttpContext.Session.GetString("UserId");
        if (string.IsNullOrEmpty(userId))
        {
            return RedirectToPage("/Login");
        }

        ViewData["Title"] = "文档管理";
        Documents = GetDocuments();

        var language = NormalizeLanguage(CreateInput.Language);
        var slug = DocumentationCatalog.NormalizeSlug(CreateInput.Slug);
        if (string.IsNullOrWhiteSpace(slug))
        {
            ErrorMessage = "请填写页面地址，只能使用英文、数字和短横线";
            await LoadDocAsync(CurrentDocId, language);
            return Page();
        }

        if (FindDoc(slug) != null)
        {
            ErrorMessage = "这个页面地址已经存在";
            await LoadDocAsync(slug, language);
            return Page();
        }

        var title = string.IsNullOrWhiteSpace(CreateInput.Title) ? slug : CreateInput.Title.Trim();
        var category = string.IsNullOrWhiteSpace(CreateInput.Category) ? "参考" : CreateInput.Category.Trim();
        var description = string.IsNullOrWhiteSpace(CreateInput.Description) ? "自定义文档页面" : CreateInput.Description.Trim();
        var page = new DocumentationPage(slug, title, DocumentationCatalog.CreateCustomFileName(slug), description, category, false);
        var customPages = DocumentationCatalog.LoadCustomPages(_environment.ContentRootPath);
        customPages.Add(page);
        DocumentationCatalog.SaveCustomPages(_environment.ContentRootPath, customPages);

        var path = GetSafeDocPath(page, language);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await System.IO.File.WriteAllTextAsync(path, GenerateEmptyDocHtml(title, language));

        SuccessMessage = $"《{title}》已创建";
        await LoadDocAsync(slug, language);
        return Page();
    }

    public async Task<IActionResult> OnPostDeleteAsync(string docId)
    {
        var userId = HttpContext.Session.GetString("UserId");
        if (string.IsNullOrEmpty(userId))
        {
            return RedirectToPage("/Login");
        }

        ViewData["Title"] = "文档管理";

        if (string.IsNullOrWhiteSpace(docId))
        {
            ErrorMessage = "请选择要删除的文档";
            await LoadDocAsync("index", CurrentLanguage);
            return Page();
        }

        var doc = FindDoc(docId);
        if (doc == null)
        {
            ErrorMessage = "文档不存在";
            await LoadDocAsync("index", CurrentLanguage);
            return Page();
        }

        if (doc.IsBuiltIn)
        {
            ErrorMessage = "内置文档不能删除";
            await LoadDocAsync(docId, CurrentLanguage);
            return Page();
        }

        var customPages = DocumentationCatalog.LoadCustomPages(_environment.ContentRootPath);
        customPages.RemoveAll(p => p.Id.Equals(docId, StringComparison.OrdinalIgnoreCase));
        DocumentationCatalog.SaveCustomPages(_environment.ContentRootPath, customPages);

        foreach (var lang in GetSupportedLanguages())
        {
            var path = GetSafeDocPath(doc, lang.Id);
            if (System.IO.File.Exists(path))
            {
                System.IO.File.Delete(path);
            }
        }

        SuccessMessage = $"《{doc.Title}》已删除";
        await LoadDocAsync("index", CurrentLanguage);
        return Page();
    }

    public async Task<IActionResult> OnPostSaveCssAsync()
    {
        var userId = HttpContext.Session.GetString("UserId");
        if (string.IsNullOrEmpty(userId))
        {
            return RedirectToPage("/Login");
        }

        ViewData["Title"] = "文档管理";
        ActiveTab = "css";

        var cssPath = GetCustomCssPath();
        Directory.CreateDirectory(Path.GetDirectoryName(cssPath)!);
        await System.IO.File.WriteAllTextAsync(cssPath, CustomCss ?? string.Empty);

        SuccessMessage = "自定义CSS已保存";
        var language = NormalizeLanguage(Input.Language);
        await LoadDocAsync(Input.DocId ?? "index", language);
        ActiveTab = "css";
        return Page();
    }

    public async Task<IActionResult> OnPostResetCssAsync()
    {
        var userId = HttpContext.Session.GetString("UserId");
        if (string.IsNullOrEmpty(userId))
        {
            return RedirectToPage("/Login");
        }

        ViewData["Title"] = "文档管理";
        ActiveTab = "css";

        var cssPath = GetCustomCssPath();
        if (System.IO.File.Exists(cssPath))
        {
            System.IO.File.Delete(cssPath);
        }

        SuccessMessage = "自定义CSS已重置";
        var language = NormalizeLanguage(Input.Language);
        await LoadDocAsync(Input.DocId ?? "index", language);
        ActiveTab = "css";
        return Page();
    }

=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
    private async Task LoadDocAsync(string docId, string language)
    {
        Documents = GetDocuments();
        SupportedLanguages = GetSupportedLanguages();
        var doc = FindDoc(docId) ?? FindDoc("index")!;
        CurrentDocId = doc.Id;
        CurrentDocTitle = doc.Title;
        CurrentLanguage = NormalizeLanguage(language);
<<<<<<< HEAD
        IsCurrentDocBuiltIn = doc.IsBuiltIn;
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a

        var path = GetSafeDocPath(doc, CurrentLanguage);
        var html = System.IO.File.Exists(path) ? await System.IO.File.ReadAllTextAsync(path) : "";

<<<<<<< HEAD
        var cssPath = GetCustomCssPath();
        CustomCss = System.IO.File.Exists(cssPath) ? await System.IO.File.ReadAllTextAsync(cssPath) : "";

=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
        Input = new DocEditInput
        {
            DocId = doc.Id,
            Language = CurrentLanguage,
            Html = html
        };
<<<<<<< HEAD

        CreateInput = new DocCreateInput
        {
            Language = CurrentLanguage,
            Category = "参考"
        };
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
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
<<<<<<< HEAD

    private string GetCustomCssPath()
    {
        return Path.Combine(_environment.WebRootPath, "css", "custom-docs.css");
    }

    private string GenerateEmptyDocHtml(string title, string language)
    {
        var langLabel = language == "zh-CN" ? "简体中文" : "English";
        return $@"<h1 id=""{title.ToLower().Replace(" ", "-")}"">{title}</h1>

<p>这是一个新创建的文档页面。</p>

<div class=""docs-callout"">
    <div class=""docs-callout-title"">📝 关于本页面</div>
    <p>语言: {langLabel}</p>
</div>";
    }
}

public record DocLanguageOption(string Id, string Label);
=======
}

public record DocLanguageOption(string Id, string Label);
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
