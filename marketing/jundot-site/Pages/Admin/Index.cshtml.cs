using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;

namespace JundotSite.Pages.Admin;

public class IndexModel : AdminPageModel
{
    private readonly ApplicationDbContext _context;

    public IndexModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public int TotalReleases { get; set; }
    public int TotalDownloads { get; set; }
    public int TotalMessages { get; set; }
    public int UnreadMessages { get; set; }
    public int TotalContents { get; set; }

    public List<JundotSite.Models.ReleaseVersion> RecentReleases { get; set; } = new();
    public List<JundotSite.Models.ContactMessage> RecentMessages { get; set; } = new();

    public async Task<IActionResult> OnGetAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "仪表盘";

        TotalReleases = await _context.ReleaseVersions.CountAsync();
        TotalDownloads = await _context.DownloadLogs.CountAsync();
        TotalMessages = await _context.ContactMessages.CountAsync();
        UnreadMessages = await _context.ContactMessages.Where(m => !m.IsRead).CountAsync();
        TotalContents = await _context.SiteContents.CountAsync();

        RecentReleases = await _context.ReleaseVersions
            .OrderByDescending(r => r.ReleaseDate)
            .Take(5)
            .ToListAsync();

        RecentMessages = await _context.ContactMessages
            .OrderByDescending(m => m.CreatedAt)
            .Take(5)
            .ToListAsync();

        return Page();
    }
}
