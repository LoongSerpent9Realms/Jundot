using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages;

public class DownloadModel : PageModel
{
    private readonly ApplicationDbContext _context;

    public DownloadModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public async Task<IActionResult> OnGetAsync(string? version, string? platform, string? branch)
    {
        var latestRelease = await _context.ReleaseVersions
            .Where(r => r.IsPublished)
            .OrderByDescending(r => r.ReleaseDate)
            .FirstOrDefaultAsync();

        var downloadUrl = latestRelease?.DownloadUrl
            ?? "https://github.com/LoongSerpent9Realms/Jundot/releases";

        var versionNumber = version ?? latestRelease?.VersionNumber ?? "unknown";

        var log = new DownloadLog
        {
            VersionNumber = versionNumber,
            Platform = platform,
            IpAddress = HttpContext.Connection.RemoteIpAddress?.ToString(),
            UserAgent = Request.Headers["User-Agent"].ToString().Length > 200
                ? Request.Headers["User-Agent"].ToString().Substring(0, 200)
                : Request.Headers["User-Agent"].ToString()
        };

        _context.DownloadLogs.Add(log);

        if (latestRelease != null)
        {
            latestRelease.DownloadCount++;
        }

        if (!string.IsNullOrEmpty(branch))
        {
            var engineBranch = await _context.EngineBranches
                .FirstOrDefaultAsync(b => b.Slug == branch);
            if (engineBranch != null)
            {
                engineBranch.DownloadCount++;
            }
        }

        await _context.SaveChangesAsync();

        return Redirect(downloadUrl);
    }
}
