using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages.Admin;

public class DownloadsModel : AdminPageModel
{
    private readonly ApplicationDbContext _context;

    public DownloadsModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public List<DownloadLog> DownloadLogs { get; set; } = new();
    public int TotalDownloads { get; set; }
    public Dictionary<string, int> DownloadsByVersion { get; set; } = new();
    public Dictionary<string, int> DownloadsByPlatform { get; set; } = new();
    public int DownloadsToday { get; set; }
    public int DownloadsThisWeek { get; set; }
    public int DownloadsThisMonth { get; set; }

    public async Task<IActionResult> OnGetAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "下载统计";

        TotalDownloads = await _context.DownloadLogs.CountAsync();
        DownloadLogs = await _context.DownloadLogs
            .OrderByDescending(d => d.DownloadedAt)
            .Take(100)
            .ToListAsync();

        DownloadsByVersion = await _context.DownloadLogs
            .Where(d => d.VersionNumber != null)
            .GroupBy(d => d.VersionNumber!)
            .Select(g => new { Version = g.Key, Count = g.Count() })
            .OrderByDescending(g => g.Count)
            .ToDictionaryAsync(g => g.Version, g => g.Count);

        DownloadsByPlatform = await _context.DownloadLogs
            .Where(d => d.Platform != null)
            .GroupBy(d => d.Platform!)
            .Select(g => new { Platform = g.Key, Count = g.Count() })
            .OrderByDescending(g => g.Count)
            .ToDictionaryAsync(g => g.Platform, g => g.Count);

        var today = DateTime.Today;
        DownloadsToday = await _context.DownloadLogs
            .Where(d => d.DownloadedAt >= today)
            .CountAsync();

        var weekAgo = today.AddDays(-7);
        DownloadsThisWeek = await _context.DownloadLogs
            .Where(d => d.DownloadedAt >= weekAgo)
            .CountAsync();

        var monthAgo = today.AddMonths(-1);
        DownloadsThisMonth = await _context.DownloadLogs
            .Where(d => d.DownloadedAt >= monthAgo)
            .CountAsync();

        return Page();
    }
}
