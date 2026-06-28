using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;

namespace JundotSite.Pages;

public class AboutModel : PageModel
{
    private readonly ApplicationDbContext _context;

    public AboutModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public Dictionary<string, string> SiteContents { get; set; } = new();

    public async Task OnGetAsync()
    {
        ViewData["Title"] = "关于我们";

        var contents = await _context.SiteContents.ToListAsync();
        SiteContents = contents.ToDictionary(c => c.Key, c => c.Value);

        ViewData["FooterText"] = SiteContents.GetValueOrDefault("Footer_Text", "Jundot Engine - AI 辅助自动迭代的游戏引擎");
        ViewData["FooterCopyright"] = SiteContents.GetValueOrDefault("Footer_Copyright", "Jundot 基于 Godot Engine，遵循相关 MIT/Expat 许可与版权声明。");
    }
}
