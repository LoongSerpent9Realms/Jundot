using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages;

public class BranchDetailModel : PageModel
{
    private readonly ApplicationDbContext _context;

    public BranchDetailModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public Dictionary<string, string> SiteContents { get; set; } = new();
    public EngineBranch? Branch { get; set; }
    public List<ReleaseVersion> BranchReleases { get; set; } = new();

    public async Task<IActionResult> OnGetAsync(string slug)
    {
        var contents = await _context.SiteContents.ToListAsync();
        SiteContents = contents.ToDictionary(c => c.Key, c => c.Value);

        ViewData["FooterText"] = SiteContents.GetValueOrDefault("Footer_Text", "Jundot Engine - AI 辅助自动迭代的游戏引擎");
        ViewData["FooterCopyright"] = SiteContents.GetValueOrDefault("Footer_Copyright", "Jundot 基于 Godot Engine，遵循相关 MIT/Expat 许可与版权声明。");

        Branch = await _context.EngineBranches
            .Include(b => b.Features)
            .FirstOrDefaultAsync(b => b.Slug == slug && b.IsPublished);

        if (Branch == null)
        {
            return NotFound();
        }

        ViewData["Title"] = $"{Branch.Name} - Jundot Engine";

        BranchReleases = await _context.ReleaseVersions
            .Include(r => r.Features)
            .Where(r => r.EngineBranchId == Branch.Id && r.IsPublished)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();

        return Page();
    }

    public string GetGenreDisplayName(GameGenre genre)
    {
        return genre switch
        {
            GameGenre.Action2D => "2D 动作",
            GameGenre.Action3D => "3D 动作",
            GameGenre.RPG => "角色扮演",
            GameGenre.Puzzle => "解谜益智",
            GameGenre.Simulation => "模拟经营",
            GameGenre.Strategy => "策略游戏",
            GameGenre.Adventure => "冒险游戏",
            GameGenre.Platformer => "平台跳跃",
            GameGenre.Shooter => "射击游戏",
            GameGenre.Racing => "赛车竞速",
            GameGenre.Sports => "体育游戏",
            GameGenre.Casual => "休闲游戏",
            GameGenre.Other => "通用/其他",
            _ => "其他"
        };
    }

    public string GetLicenseDisplayName(LicenseType license)
    {
        return license switch
        {
            LicenseType.MIT => "MIT License",
            LicenseType.Commercial => "商业授权",
            LicenseType.GPL => "GPL",
            LicenseType.Apache => "Apache",
            LicenseType.Custom => "自定义许可",
            _ => "未知"
        };
    }
}
