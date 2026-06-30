using System.ComponentModel.DataAnnotations;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;
using JundotSite.Services;

namespace JundotSite.Pages.Admin;

[Microsoft.AspNetCore.Mvc.RequestFormLimits(ValueLengthLimit = 50 * 1024 * 1024, MultipartBodyLengthLimit = 50 * 1024 * 1024)]
public class DocsModel : AdminPageModel
{
    private readonly IWebHostEnvironment _environment;
    private readonly IConfiguration _configuration;
    private readonly IHttpClientFactory _httpClientFactory;
    private readonly ApplicationDbContext _context;

    public DocsModel(IWebHostEnvironment environment, IConfiguration configuration, IHttpClientFactory httpClientFactory, ApplicationDbContext context)
    {
        _environment = environment;
        _configuration = configuration;
        _httpClientFactory = httpClientFactory;
        _context = context;
    }

    public List<DocumentationPage> Documents { get; set; } = new();
    public List<DocVideo> DocVideos { get; set; } = new();
    public List<DocLanguageOption> SupportedLanguages { get; set; } = GetSupportedLanguages();
    public string CurrentDocId { get; set; } = "index";
    public string CurrentDocTitle { get; set; } = "开始使用";
    public string CurrentLanguage { get; set; } = "zh-CN";
    public string CurrentLanguageLabel => SupportedLanguages.First(l => l.Id == CurrentLanguage).Label;
    public string? SuccessMessage { get; set; }
    public string? ErrorMessage { get; set; }

    [BindProperty]
    public DocEditInput Input { get; set; } = new();

    [BindProperty]
    public DocGenerateInput GenerateInput { get; set; } = new();

    [BindProperty]
    public DocCreateInput CreateInput { get; set; } = new();

    public class DocEditInput
    {
        public string? DocId { get; set; } = "index";

        public string? Language { get; set; } = "zh-CN";

        public string? Html { get; set; } = string.Empty;
    }

    public class DocGenerateInput
    {
        public string? DocId { get; set; } = "index";

        public string? Language { get; set; } = "zh-CN";

        [StringLength(120)]
        public string? Title { get; set; } = string.Empty;

        [StringLength(160)]
        public string? Audience { get; set; } = string.Empty;

        public string? Prompt { get; set; } = string.Empty;
    }

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
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "文档管理";
        await LoadDocAsync(doc, NormalizeLanguage(lang));
        return Page();
    }

    public async Task<IActionResult> OnPostSaveAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "文档管理";
        Documents = GetDocuments();
        ModelState.Remove("GenerateInput.DocId");
        ModelState.Remove("GenerateInput.Language");
        ModelState.Remove("GenerateInput.Title");
        ModelState.Remove("GenerateInput.Audience");
        ModelState.Remove("GenerateInput.Prompt");
        ModelState.Remove("CreateInput.Slug");
        ModelState.Remove("CreateInput.Title");
        ModelState.Remove("CreateInput.Category");
        ModelState.Remove("CreateInput.Description");
        ModelState.Remove("CreateInput.Language");
        var language = NormalizeLanguage(Input.Language);

        if (string.IsNullOrWhiteSpace(Input.DocId) || string.IsNullOrWhiteSpace(Input.Html))
        {
            ErrorMessage = "请选择文档并填写文档 HTML。";
            await LoadDocAsync(Input.DocId ?? "index", language);
            return Page();
        }

        var doc = FindDoc(Input.DocId);
        if (doc == null)
        {
            ErrorMessage = "文档不存在。";
            await LoadDocAsync("index", language);
            return Page();
        }

        var path = GetSafeDocPath(doc, language);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await System.IO.File.WriteAllTextAsync(path, Input.Html);

        SuccessMessage = $"《{doc.Title}》({GetLanguageLabel(language)}) 已保存。";
        await LoadDocAsync(doc.Id, language);
        return Page();
    }

    public async Task<IActionResult> OnPostUploadImageAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var files = Request.Form.Files;
        if (files.Count == 0)
        {
            return new JsonResult(new { success = false, message = "没有选择文件。" });
        }

        var file = files[0];
        var allowedExtensions = new[] { ".jpg", ".jpeg", ".png", ".gif", ".webp", ".svg" };
        var ext = Path.GetExtension(file.FileName).ToLowerInvariant();
        if (!allowedExtensions.Contains(ext))
        {
            return new JsonResult(new { success = false, message = "不支持的图片格式。支持：JPG, PNG, GIF, WebP, SVG" });
        }

        if (file.Length > 50L * 1024 * 1024)
        {
            return new JsonResult(new { success = false, message = "图片大小不能超过 50MB。" });
        }

        var imagesDir = Path.Combine(_environment.WebRootPath, "images", "docs");
        Directory.CreateDirectory(imagesDir);

        var fileName = $"{Guid.NewGuid()}{ext}";
        var filePath = Path.Combine(imagesDir, fileName);

        using (var stream = new FileStream(filePath, FileMode.Create))
        {
            await file.CopyToAsync(stream);
        }

        var relativeUrl = $"/images/docs/{fileName}";
        return new JsonResult(new { success = true, url = relativeUrl });
    }

    public async Task<IActionResult> OnPostCreateAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "文档管理";
        Documents = GetDocuments();
        ModelState.Remove("Input.DocId");
        ModelState.Remove("Input.Language");
        ModelState.Remove("Input.Html");
        ModelState.Remove("GenerateInput.DocId");
        ModelState.Remove("GenerateInput.Language");
        ModelState.Remove("GenerateInput.Title");
        ModelState.Remove("GenerateInput.Audience");
        ModelState.Remove("GenerateInput.Prompt");

        var language = NormalizeLanguage(CreateInput.Language);
        var slug = DocumentationCatalog.NormalizeSlug(CreateInput.Slug);
        if (string.IsNullOrWhiteSpace(slug))
        {
            ErrorMessage = "请填写页面地址，只能使用英文、数字和短横线。";
            await LoadDocAsync(CurrentDocId, language);
            return Page();
        }

        if (FindDoc(slug) != null)
        {
            ErrorMessage = "这个页面地址已经存在。";
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

        SuccessMessage = $"《{title}》已创建。";
        await LoadDocAsync(slug, language);
        return Page();
    }

    public async Task<IActionResult> OnPostGenerateAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "文档管理";
        Documents = GetDocuments();
        ModelState.Remove("Input.DocId");
        ModelState.Remove("Input.Language");
        ModelState.Remove("Input.Html");
        ModelState.Remove("CreateInput.Slug");
        ModelState.Remove("CreateInput.Title");
        ModelState.Remove("CreateInput.Category");
        ModelState.Remove("CreateInput.Description");
        ModelState.Remove("CreateInput.Language");

        var language = NormalizeLanguage(GenerateInput.Language);
        var doc = FindDoc(GenerateInput.DocId ?? "index") ?? FindDoc("index")!;
        CurrentDocId = doc.Id;
        CurrentDocTitle = doc.Title;
        CurrentLanguage = language;

        if (string.IsNullOrWhiteSpace(GenerateInput.Prompt))
        {
            ErrorMessage = "请填写想生成什么内容。";
            await LoadDocAsync(doc.Id, language);
            return Page();
        }

        var draft = await GenerateDraftHtmlAsync(doc, GenerateInput);
        Input = new DocEditInput
        {
            DocId = doc.Id,
            Language = language,
            Html = draft
        };
        GenerateInput.DocId = doc.Id;
        GenerateInput.Language = language;
        SuccessMessage = "AI 文档草稿已生成。请检查后保存。";
        return Page();
    }

    private async Task LoadDocAsync(string docId, string language)
    {
        Documents = GetDocuments();
        SupportedLanguages = GetSupportedLanguages();
        DocVideos = await _context.DocVideos
            .Where(v => v.IsPublished)
            .OrderBy(v => v.SortOrder)
            .ToListAsync();
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

        GenerateInput = new DocGenerateInput
        {
            DocId = doc.Id,
            Language = CurrentLanguage,
            Title = doc.Title,
            Audience = CurrentLanguage == "en" ? "Jundot users and game developers" : "Jundot 用户和游戏开发者"
        };
    }

    private async Task<string> GenerateDraftHtmlAsync(DocumentationPage doc, DocGenerateInput input)
    {
        var aiDraft = await TryGenerateWithAiAsync(doc, input);
        return string.IsNullOrWhiteSpace(aiDraft) ? GenerateLocalDraftHtml(doc, input) : aiDraft;
    }

    private async Task<string?> TryGenerateWithAiAsync(DocumentationPage doc, DocGenerateInput input)
    {
        var endpoint = _configuration["DocumentationAI:Endpoint"];
        var apiKey = _configuration["DocumentationAI:ApiKey"];
        var model = _configuration["DocumentationAI:Model"] ?? "gpt-4.1-mini";
        var language = NormalizeLanguage(input.Language);
        var languageName = GetLanguageLabel(language);

        if (string.IsNullOrWhiteSpace(endpoint) || string.IsNullOrWhiteSpace(apiKey))
        {
            return null;
        }

        try
        {
            var client = _httpClientFactory.CreateClient();
            client.Timeout = TimeSpan.FromSeconds(60);
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", apiKey);

            var request = new
            {
                model,
                messages = new[]
                {
                    new
                    {
                        role = "system",
                        content = $"你是 Jundot Engine 官方文档写作助手。只输出可直接放入 Razor/HTML 文档片段的 HTML，不要输出 Markdown，不要包含 script。目标语言：{languageName}。文档必须围绕：用户用 AI 创建游戏项目；用户和 AI 细化项目；当前引擎能完成就不改引擎；不能完成时可在用户确认后改引擎；通用能力丰富主引擎；报错或性能问题由 AI 修复；用户验证后 AI 可以提交并上传 GitHub。"
                    },
                    new
                    {
                        role = "user",
                        content = $"目标文档：{doc.Title}\n面向对象：{input.Audience}\n管理员需求：{input.Prompt}\n请生成结构清晰、适合官网文档页的 {languageName} HTML。"
                    }
                },
                temperature = 0.4
            };

            var json = JsonSerializer.Serialize(request);
            using var content = new StringContent(json, Encoding.UTF8, "application/json");
            using var response = await client.PostAsync(endpoint, content);
            if (!response.IsSuccessStatusCode)
            {
                return null;
            }

            var responseJson = await response.Content.ReadAsStringAsync();
            using var docJson = JsonDocument.Parse(responseJson);
            var root = docJson.RootElement;
            var generated = root
                .GetProperty("choices")[0]
                .GetProperty("message")
                .GetProperty("content")
                .GetString();

            return string.IsNullOrWhiteSpace(generated) ? null : generated.Trim();
        }
        catch
        {
            return null;
        }
    }

    private static string GenerateLocalDraftHtml(DocumentationPage doc, DocGenerateInput input)
    {
        var title = string.IsNullOrWhiteSpace(input.Title) ? doc.Title : input.Title.Trim();
        var audience = string.IsNullOrWhiteSpace(input.Audience) ? "Jundot 用户" : input.Audience.Trim();
        var prompt = (input.Prompt ?? string.Empty).Trim();
        var language = NormalizeLanguage(input.Language);

        if (language == "en")
        {
            audience = string.IsNullOrWhiteSpace(input.Audience) ? "Jundot users" : input.Audience.Trim();
            return $@"<h1 id=""{HtmlId(title)}"">{Escape(title)}</h1>
<p>This page is written for {Escape(audience)} and explains {Escape(prompt)}.</p>

<div class=""docs-callout"">
    <div class=""docs-callout-title"">Jundot workflow</div>
    <p>Users can create a game project with AI, then refine the project together with the AI. If the current engine can complete the task, the AI changes the project only. If the engine cannot complete it, the AI can explain the need for an engine change and wait for user confirmation.</p>
</div>

<h2 id=""goal"">Goal</h2>
<ul>
    <li>Let AI participate in making the game, not just answer questions.</li>
    <li>Keep project changes, engine changes, and GitHub uploads clearly separated.</li>
    <li>Turn user-verified work into commits that can be reviewed and reused.</li>
</ul>

<h2 id=""flow"">Recommended flow</h2>
<ol>
    <li>The user describes a game idea, an error, or a performance problem.</li>
    <li>The AI reads project files, scenes, scripts, and logs.</li>
    <li>The AI decides whether the current engine is enough for the task.</li>
    <li>If it is enough, the AI edits the project and runs checks.</li>
    <li>If it is not enough, the AI explains why an engine change is needed and waits for confirmation.</li>
    <li>After user verification, the AI can prepare a commit and upload it to GitHub.</li>
</ol>

<h2 id=""engine-feedback"">Feeding useful engine work back</h2>
<p>If a project-specific engine capability is broadly useful, it should be cleaned up as a main-engine improvement. Consider scope, maintenance cost, validation, and fit with the Godot/Jundot architecture.</p>

<h2 id=""checklist"">Before publishing</h2>
<ul>
    <li>The user has run and verified the result.</li>
    <li>No API keys, private paths, or temporary files are committed.</li>
    <li>The commit message explains the benefit, scope, and validation result.</li>
    <li>Engine changes clearly state whether they are project-specific or reusable.</li>
</ul>";
        }

        return $@"<h1 id=""{HtmlId(title)}"">{Escape(title)}</h1>
<p>本文面向 {Escape(audience)}，说明 {Escape(prompt)}。</p>

<div class=""docs-callout"">
    <div class=""docs-callout-title"">Jundot 工作流</div>
    <p>用户可以使用 AI 创建游戏项目，并和 AI 一起细化项目。当前引擎能完成时，AI 优先修改项目；当前引擎不能完成时，AI 可以在用户确认后把引擎改成适合这个项目的形态。</p>
</div>

<h2 id=""goal"">目标</h2>
<ul>
    <li>让 AI 真正参与做游戏，而不只是回答问题。</li>
    <li>让项目改动、引擎改动和 GitHub 上传都有清晰边界。</li>
    <li>让用户验证过的修改可以被沉淀、提交和复用。</li>
</ul>

<h2 id=""flow"">推荐流程</h2>
<ol>
    <li>用户描述游戏想法、报错现场或性能问题。</li>
    <li>AI 读取项目、场景、脚本和日志上下文。</li>
    <li>AI 判断当前引擎是否足够完成需求。</li>
    <li>能完成时，AI 只修改项目并运行检查。</li>
    <li>不能完成时，AI 说明为什么需要改引擎，并等待用户确认。</li>
    <li>用户验证通过后，AI 可以整理提交并上传 GitHub。</li>
</ol>

<h2 id=""engine-feedback"">通用能力反哺主引擎</h2>
<p>如果某个项目中产生的引擎能力足够通用，应该整理为主引擎能力，而不是只停留在单个项目里。判断时需要考虑适用范围、维护成本、验证方式和与 Godot/Jundot 架构的一致性。</p>

<h2 id=""checklist"">发布前检查</h2>
<ul>
    <li>用户已经运行并验证结果。</li>
    <li>没有提交 API Key、私有路径或临时文件。</li>
    <li>提交说明写清楚了项目收益、修改范围和验证结果。</li>
    <li>如果是引擎改动，说明它是否足够通用。</li>
</ul>";
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

    private static string GetLanguageLabel(string language)
    {
        return GetSupportedLanguages().First(l => l.Id == NormalizeLanguage(language)).Label;
    }

    private static string Escape(string value)
    {
        return System.Net.WebUtility.HtmlEncode(value);
    }

    private static string HtmlId(string value)
    {
        var chars = value.ToLowerInvariant().Select(ch => char.IsLetterOrDigit(ch) ? ch : '-').ToArray();
        var id = new string(chars).Trim('-');
        return string.IsNullOrWhiteSpace(id) ? "ai-generated-doc" : id;
    }

    private static string GenerateEmptyDocHtml(string title, string language)
    {
        if (NormalizeLanguage(language) == "en")
        {
            return $@"<h1 id=""{HtmlId(title)}"">{Escape(title)}</h1>
<p>Write the page content here.</p>

<h2 id=""overview"">Overview</h2>
<p>Describe the goal, workflow, and verification steps for this topic.</p>";
        }

        return $@"<h1 id=""{HtmlId(title)}"">{Escape(title)}</h1>
<p>在这里编写页面内容。</p>

<h2 id=""overview"">概览</h2>
<p>说明这个主题的目标、流程和验证方式。</p>";
    }
}

public record DocLanguageOption(string Id, string Label);
