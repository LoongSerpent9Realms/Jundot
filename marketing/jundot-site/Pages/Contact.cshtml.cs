using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages;

public class ContactModel : PageModel
{
    private readonly ApplicationDbContext _context;

    public ContactModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public Dictionary<string, string> SiteContents { get; set; } = new();

    [BindProperty]
    public ContactInput Input { get; set; } = new();

    public bool IsSuccess { get; set; }

    public class ContactInput
    {
        [Required(ErrorMessage = "请输入您的姓名")]
        [StringLength(100, ErrorMessage = "姓名不能超过 100 个字符")]
        [Display(Name = "姓名")]
        public string Name { get; set; } = string.Empty;

        [Required(ErrorMessage = "请输入您的邮箱")]
        [EmailAddress(ErrorMessage = "请输入有效的邮箱地址")]
        [StringLength(200, ErrorMessage = "邮箱不能超过 200 个字符")]
        [Display(Name = "邮箱")]
        public string Email { get; set; } = string.Empty;

        [StringLength(100, ErrorMessage = "主题不能超过 100 个字符")]
        [Display(Name = "主题")]
        public string? Subject { get; set; }

        [Required(ErrorMessage = "请输入留言内容")]
        [Display(Name = "留言")]
        public string Message { get; set; } = string.Empty;
    }

    public async Task OnGetAsync()
    {
        ViewData["Title"] = "联系我们";

        var contents = await _context.SiteContents.ToListAsync();
        SiteContents = contents.ToDictionary(c => c.Key, c => c.Value);

        ViewData["FooterText"] = SiteContents.GetValueOrDefault("Footer_Text", "Jundot Engine - AI 辅助自动迭代的游戏引擎");
        ViewData["FooterCopyright"] = SiteContents.GetValueOrDefault("Footer_Copyright", "Jundot 基于 Godot Engine，遵循相关 MIT/Expat 许可与版权声明。");
    }

    public async Task<IActionResult> OnPostAsync()
    {
        var contents = await _context.SiteContents.ToListAsync();
        SiteContents = contents.ToDictionary(c => c.Key, c => c.Value);

        ViewData["FooterText"] = SiteContents.GetValueOrDefault("Footer_Text", "Jundot Engine - AI 辅助自动迭代的游戏引擎");
        ViewData["FooterCopyright"] = SiteContents.GetValueOrDefault("Footer_Copyright", "Jundot 基于 Godot Engine，遵循相关 MIT/Expat 许可与版权声明。");

        if (!ModelState.IsValid)
        {
            return Page();
        }

        var message = new ContactMessage
        {
            Name = Input.Name,
            Email = Input.Email,
            Subject = Input.Subject,
            Message = Input.Message
        };

        _context.ContactMessages.Add(message);
        await _context.SaveChangesAsync();

        IsSuccess = true;
        ModelState.Clear();
        Input = new ContactInput();

        return Page();
    }
}
