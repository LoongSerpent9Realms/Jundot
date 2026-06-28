using JundotSite.Models;
using JundotSite.Pages.Admin;
using JundotSite.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace JundotSite.Pages.Admin;

public class SnapshotsModel : AdminPageModel
{
    private readonly DocSnapshotService _snapshotService;
    private readonly IWebHostEnvironment _environment;

    public SnapshotsModel(DocSnapshotService snapshotService, IWebHostEnvironment environment)
    {
        _snapshotService = snapshotService;
        _environment = environment;
    }

    public List<DocumentationPage> Documents { get; set; } = new();
    public List<DocSnapshot> Snapshots { get; set; } = new();
    public string CurrentDocId { get; set; } = "index";
    public string CurrentDocTitle { get; set; } = "开始使用";
    public string CurrentLanguage { get; set; } = "zh-CN";
    public string CurrentDocContent { get; set; } = "";
    public bool HasSearched { get; set; } = false;

    public string? SuccessMessage { get; set; }
    public string? ErrorMessage { get; set; }

    public IActionResult OnGet(string docId = "index", string? lang = null)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "文档存档管理";
        CurrentDocId = docId;
        CurrentLanguage = NormalizeLanguage(lang);
        Documents = DocumentationCatalog.GetPages(_environment.ContentRootPath);

        var doc = Documents.FirstOrDefault(d => d.Id.Equals(CurrentDocId, StringComparison.OrdinalIgnoreCase)) ?? Documents.First();
        CurrentDocTitle = doc.Title;

        _ = LoadCurrentDocContentAsync(doc);
        _ = LoadSnapshotsAsync();

        return Page();
    }

    public async Task<IActionResult> OnPostCreate(string docId, string lang, string? snapshotName)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var userId = HttpContext.Session.GetString("UserId") ?? HttpContext.Session.GetString("AdminUserId") ?? "0";
        var username = HttpContext.Session.GetString("AdminUser") ?? HttpContext.Session.GetString("Username") ?? "Admin";

        CurrentDocId = docId;
        CurrentLanguage = NormalizeLanguage(lang);
        Documents = DocumentationCatalog.GetPages(_environment.ContentRootPath);

        var doc = Documents.FirstOrDefault(d => d.Id.Equals(CurrentDocId, StringComparison.OrdinalIgnoreCase)) ?? Documents.First();
        CurrentDocTitle = doc.Title;

        var content = await LoadCurrentDocContentAsync(doc);

        var (success, message) = await _snapshotService.CreateSnapshotAsync(
            docId, 
            doc.Title, 
            CurrentLanguage, 
            content, 
            int.Parse(userId), 
            username,
            snapshotName);

        if (success)
            SuccessMessage = message;
        else
            ErrorMessage = message;

        await LoadSnapshotsAsync();
        return Page();
    }

    public async Task<IActionResult> OnPostRestore(int snapshotId, string docId, string lang)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var userId = HttpContext.Session.GetString("UserId") ?? HttpContext.Session.GetString("AdminUserId") ?? "0";
        var username = HttpContext.Session.GetString("AdminUser") ?? HttpContext.Session.GetString("Username") ?? "Admin";

        var (success, message) = await _snapshotService.RestoreSnapshotAsync(snapshotId, int.Parse(userId), username);

        if (success)
            SuccessMessage = message;
        else
            ErrorMessage = message;

        CurrentDocId = docId;
        CurrentLanguage = NormalizeLanguage(lang);
        Documents = DocumentationCatalog.GetPages(_environment.ContentRootPath);

        var doc = Documents.FirstOrDefault(d => d.Id.Equals(CurrentDocId, StringComparison.OrdinalIgnoreCase)) ?? Documents.First();
        CurrentDocTitle = doc.Title;

        await LoadSnapshotsAsync();
        return Page();
    }

    public async Task<IActionResult> OnPostDelete(int snapshotId, string docId, string lang)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var (success, message) = await _snapshotService.DeleteSnapshotAsync(snapshotId);

        if (success)
            SuccessMessage = message;
        else
            ErrorMessage = message;

        CurrentDocId = docId;
        CurrentLanguage = NormalizeLanguage(lang);
        Documents = DocumentationCatalog.GetPages(_environment.ContentRootPath);

        var doc = Documents.FirstOrDefault(d => d.Id.Equals(CurrentDocId, StringComparison.OrdinalIgnoreCase)) ?? Documents.First();
        CurrentDocTitle = doc.Title;

        await LoadSnapshotsAsync();
        return Page();
    }

    private async Task<string> LoadCurrentDocContentAsync(DocumentationPage doc)
    {
        var docsRoot = Path.Combine(_environment.ContentRootPath, "Pages", "Docs");
        var path = CurrentLanguage == "zh-CN"
            ? Path.Combine(docsRoot, doc.FileName)
            : Path.Combine(docsRoot, CurrentLanguage, doc.FileName);

        CurrentDocContent = System.IO.File.Exists(path) ? await System.IO.File.ReadAllTextAsync(path) : "(文档为空)";
        return CurrentDocContent;
    }

    private async Task LoadSnapshotsAsync()
    {
        Snapshots = await _snapshotService.GetSnapshotsAsync(CurrentDocId, CurrentLanguage);
        HasSearched = true;
    }

    private static string NormalizeLanguage(string? language)
    {
        return string.IsNullOrWhiteSpace(language) || !language.Equals("en", StringComparison.OrdinalIgnoreCase)
            ? "zh-CN"
            : "en";
    }
}
