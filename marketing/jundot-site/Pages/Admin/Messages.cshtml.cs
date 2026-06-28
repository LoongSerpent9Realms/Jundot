using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages.Admin;

public class MessagesModel : AdminPageModel
{
    private readonly ApplicationDbContext _context;

    public MessagesModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public List<ContactMessage> Messages { get; set; } = new();
    public int TotalMessages { get; set; }
    public int UnreadMessages { get; set; }

    public string? SuccessMessage { get; set; }

    public async Task<IActionResult> OnGetAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "留言管理";
        await LoadDataAsync();
        return Page();
    }

    public async Task<IActionResult> OnPostMarkReadAsync(int id)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var message = await _context.ContactMessages.FindAsync(id);
        if (message != null)
        {
            message.IsRead = true;
            await _context.SaveChangesAsync();
            SuccessMessage = "已标记为已读！";
        }

        ViewData["Title"] = "留言管理";
        await LoadDataAsync();
        return Page();
    }

    public async Task<IActionResult> OnPostDeleteAsync(int id)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var message = await _context.ContactMessages.FindAsync(id);
        if (message != null)
        {
            _context.ContactMessages.Remove(message);
            await _context.SaveChangesAsync();
            SuccessMessage = "留言已删除！";
        }

        ViewData["Title"] = "留言管理";
        await LoadDataAsync();
        return Page();
    }

    private async Task LoadDataAsync()
    {
        Messages = await _context.ContactMessages
            .OrderByDescending(m => m.CreatedAt)
            .ToListAsync();
        TotalMessages = Messages.Count;
        UnreadMessages = Messages.Count(m => !m.IsRead);
    }
}
