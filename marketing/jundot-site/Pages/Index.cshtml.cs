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
    public List<EngineBranch> FeaturedBranches { get; set; } = new();

    public async Task OnGetAsync()
    {
        SiteContents = await _context.SiteContents
            .ToDictionaryAsync(s => s.Key, s => s.Value);

        PublishedReleases = await _context.ReleaseVersions
            .Where(r => r.IsPublished)
            .OrderByDescending(r => r.ReleaseDate)
            .Take(5)
            .ToListAsync();

        FeaturedBranches = await _context.EngineBranches
            .Where(b => b.IsPublished && b.IsFeatured)
            .OrderBy(b => b.Name)
            .ToListAsync();
    }
}
