using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages;

public class IndexModel : PageModel
{
    private readonly ApplicationDbContext _context;

    public IndexModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public Dictionary<string, string> SiteContents { get; set; } = new();
    public List<ReleaseVersion> PublishedReleases { get; set; } = new();
<<<<<<< HEAD
    public List<EngineBranch> FeaturedBranches { get; set; } = new();
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a

    public async Task OnGetAsync()
    {
        ViewData["Title"] = "首页";

        var contents = await _context.SiteContents.ToListAsync();
        SiteContents = contents.ToDictionary(c => c.Key, c => c.Value);

        ViewData["FooterText"] = SiteContents.GetValueOrDefault("Footer_Text", "Jundot Engine - AI 辅助自动迭代的游戏引擎");
        ViewData["FooterCopyright"] = SiteContents.GetValueOrDefault("Footer_Copyright", "Jundot 基于 Godot Engine，遵循相关 MIT/Expat 许可与版权声明。");

        PublishedReleases = await _context.ReleaseVersions
            .Include(r => r.Features)
            .Where(r => r.IsPublished)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
<<<<<<< HEAD

        FeaturedBranches = await _context.EngineBranches
            .Include(b => b.Features)
            .Where(b => b.IsPublished && b.IsFeatured)
            .OrderByDescending(b => b.CreatedAt)
            .ToListAsync();
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
    }
}
