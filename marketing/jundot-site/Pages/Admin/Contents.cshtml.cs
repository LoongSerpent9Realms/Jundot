using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages.Admin;

public class ContentsModel : AdminPageModel
{
    private readonly ApplicationDbContext _context;

    public ContentsModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public List<SiteContent> Contents { get; set; } = new();

    [BindProperty]
    public int EditId { get; set; }

    [BindProperty]
    public ContentEditInput Input { get; set; } = new();

    public bool ShowEditForm { get; set; }

    public string? SuccessMessage { get; set; }

    public class ContentEditInput
    {
        [Required]
        [StringLength(100)]
        public string Key { get; set; } = string.Empty;

        [Required]
        public string Value { get; set; } = string.Empty;

        [StringLength(200)]
        public string? Description { get; set; }
    }

    public async Task<IActionResult> OnGetAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "内容管理";
        Contents = await _context.SiteContents.OrderBy(c => c.Key).ToListAsync();
        return Page();
    }

    public async Task<IActionResult> OnGetEditAsync(int id)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "编辑内容";
        Contents = await _context.SiteContents.OrderBy(c => c.Key).ToListAsync();

        var content = await _context.SiteContents.FindAsync(id);
        if (content == null)
        {
            return RedirectToPage();
        }

        EditId = id;
        Input = new ContentEditInput
        {
            Key = content.Key,
            Value = content.Value,
            Description = content.Description
        };
        ShowEditForm = true;

        return Page();
    }

    public async Task<IActionResult> OnPostEditAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        if (!ModelState.IsValid)
        {
            ViewData["Title"] = "编辑内容";
            Contents = await _context.SiteContents.OrderBy(c => c.Key).ToListAsync();
            ShowEditForm = true;
            return Page();
        }

        var content = await _context.SiteContents.FindAsync(EditId);
        if (content == null)
        {
            return RedirectToPage();
        }

        content.Key = Input.Key;
        content.Value = Input.Value;
        content.Description = Input.Description;
        content.UpdatedAt = DateTime.Now;

        await _context.SaveChangesAsync();

        SuccessMessage = "内容更新成功！";
        Contents = await _context.SiteContents.OrderBy(c => c.Key).ToListAsync();
        ShowEditForm = false;
        ViewData["Title"] = "内容管理";

        return Page();
    }
}
