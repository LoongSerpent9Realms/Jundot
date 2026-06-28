using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages;

public class BranchesModel : PageModel
{
    private readonly ApplicationDbContext _context;

    public BranchesModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public Dictionary<string, string> SiteContents { get; set; } = new();
    public List<EngineBranch> PublishedBranches { get; set; } = new();
    public List<EngineBranch> FeaturedBranches { get; set; } = new();
    public Dictionary<GameGenre, List<EngineBranch>> BranchesByGenre { get; set; } = new();

    public string? CurrentGenre { get; set; }
    public string? CurrentLicense { get; set; }

    public async Task OnGetAsync(string? genre, string? license)
    {
        ViewData["Title"] = "引擎分支 - Jundot Engine";

        var contents = await _context.SiteContents.ToListAsync();
        SiteContents = contents.ToDictionary(c => c.Key, c => c.Value);

        ViewData["FooterText"] = SiteContents.GetValueOrDefault("Footer_Text", "Jundot Engine - AI 辅助自动迭代的游戏引擎");
        ViewData["FooterCopyright"] = SiteContents.GetValueOrDefault("Footer_Copyright", "Jundot 基于 Godot Engine，遵循相关 MIT/Expat 许可与版权声明。");

        var query = _context.EngineBranches
            .Include(b => b.Features)
            .Include(b => b.Releases.Where(r => r.IsPublished))
            .Where(b => b.IsPublished)
            .AsQueryable();

        if (!string.IsNullOrEmpty(genre))
        {
            if (Enum.TryParse<GameGenre>(genre, true, out var genreEnum))
            {
                query = query.Where(b => b.Genre == genreEnum);
                CurrentGenre = genre;
            }
        }

        if (!string.IsNullOrEmpty(license))
        {
            if (Enum.TryParse<LicenseType>(license, true, out var licenseEnum))
            {
                query = query.Where(b => b.LicenseType == licenseEnum);
                CurrentLicense = license;
            }
        }

        PublishedBranches = await query
            .OrderByDescending(b => b.IsFeatured)
            .ThenByDescending(b => b.CreatedAt)
            .ToListAsync();

        FeaturedBranches = PublishedBranches.Where(b => b.IsFeatured).ToList();

        BranchesByGenre = PublishedBranches
            .GroupBy(b => b.Genre)
            .ToDictionary(g => g.Key, g => g.ToList());
    }

    public static string GetGenreDisplayName(GameGenre genre)
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

    public static string GetLicenseDisplayName(LicenseType license)
    {
        return license switch
        {
            LicenseType.MIT => "MIT",
            LicenseType.Commercial => "商业授权",
            LicenseType.GPL => "GPL",
            LicenseType.Apache => "Apache",
            LicenseType.Custom => "自定义",
            _ => "未知"
        };
    }
}
