using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;
using JundotSite.Services;

namespace JundotSite.Pages.Admin;

public class ReleasesModel : AdminPageModel
{
    private readonly ApplicationDbContext _context;
    private readonly GitHubReleaseService _gitHubService;
    private readonly IConfiguration _configuration;

    public ReleasesModel(ApplicationDbContext context, GitHubReleaseService gitHubService, IConfiguration configuration)
    {
        _context = context;
        _gitHubService = gitHubService;
        _configuration = configuration;
    }

    public List<ReleaseVersion> Releases { get; set; } = new();
<<<<<<< HEAD
    public List<EngineBranch> Branches { get; set; } = new();
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
    public List<GitHubRelease>? GitHubReleases { get; set; }

    [BindProperty]
    public ReleaseEditInput Input { get; set; } = new();

    public bool ShowForm { get; set; }
    public bool IsEditing { get; set; }
<<<<<<< HEAD

    [BindProperty]
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
    public int EditId { get; set; }

    public string? SuccessMessage { get; set; }
    public string? ErrorMessage { get; set; }

    public class ReleaseEditInput
    {
        [Required(ErrorMessage = "请输入版本号")]
        [StringLength(50)]
        public string VersionNumber { get; set; } = string.Empty;

        [Required(ErrorMessage = "请输入标题")]
        [StringLength(100)]
        public string Title { get; set; } = string.Empty;

        public string? Description { get; set; }

        [StringLength(500)]
        public string? DownloadUrl { get; set; }

        public bool IsPublished { get; set; }
        public bool IsBeta { get; set; }
        public DateTime ReleaseDate { get; set; } = DateTime.Now;
<<<<<<< HEAD

        public LicenseType LicenseType { get; set; } = LicenseType.MIT;

        public int? EngineBranchId { get; set; }
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
    }

    public async Task<IActionResult> OnGetAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "版本管理";
        Releases = await _context.ReleaseVersions
            .Include(r => r.Features)
<<<<<<< HEAD
            .Include(r => r.EngineBranch)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
        Branches = await _context.EngineBranches
            .OrderBy(b => b.Name)
            .ToListAsync();
=======
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
        return Page();
    }

    public async Task<IActionResult> OnGetCreateAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "新建版本";
        Releases = await _context.ReleaseVersions
            .Include(r => r.Features)
<<<<<<< HEAD
            .Include(r => r.EngineBranch)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
        Branches = await _context.EngineBranches
            .OrderBy(b => b.Name)
            .ToListAsync();
=======
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
        ShowForm = true;
        IsEditing = false;
        Input = new ReleaseEditInput { ReleaseDate = DateTime.Now };
        return Page();
    }

    public async Task<IActionResult> OnGetEditAsync(int id)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "编辑版本";
        Releases = await _context.ReleaseVersions
            .Include(r => r.Features)
<<<<<<< HEAD
            .Include(r => r.EngineBranch)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
        Branches = await _context.EngineBranches
            .OrderBy(b => b.Name)
            .ToListAsync();
=======
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a

        var release = await _context.ReleaseVersions.FindAsync(id);
        if (release == null)
        {
            return RedirectToPage();
        }

        EditId = id;
        Input = new ReleaseEditInput
        {
            VersionNumber = release.VersionNumber,
            Title = release.Title,
            Description = release.Description,
            DownloadUrl = release.DownloadUrl,
            IsPublished = release.IsPublished,
            IsBeta = release.IsBeta,
<<<<<<< HEAD
            ReleaseDate = release.ReleaseDate,
            LicenseType = release.LicenseType,
            EngineBranchId = release.EngineBranchId
=======
            ReleaseDate = release.ReleaseDate
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
        };
        ShowForm = true;
        IsEditing = true;

        return Page();
    }

    public async Task<IActionResult> OnPostCreateAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        if (!ModelState.IsValid)
        {
            ViewData["Title"] = "新建版本";
            Releases = await _context.ReleaseVersions
                .Include(r => r.Features)
<<<<<<< HEAD
                .Include(r => r.EngineBranch)
                .OrderByDescending(r => r.ReleaseDate)
                .ToListAsync();
            Branches = await _context.EngineBranches
                .OrderBy(b => b.Name)
                .ToListAsync();
=======
                .OrderByDescending(r => r.ReleaseDate)
                .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
            ShowForm = true;
            IsEditing = false;
            return Page();
        }

        var release = new ReleaseVersion
        {
            VersionNumber = Input.VersionNumber,
            Title = Input.Title,
            Description = Input.Description,
            DownloadUrl = Input.DownloadUrl,
            IsPublished = Input.IsPublished,
            IsBeta = Input.IsBeta,
<<<<<<< HEAD
            ReleaseDate = Input.ReleaseDate,
            LicenseType = Input.LicenseType,
            EngineBranchId = Input.EngineBranchId
=======
            ReleaseDate = Input.ReleaseDate
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
        };

        _context.ReleaseVersions.Add(release);
        await _context.SaveChangesAsync();

        SuccessMessage = "版本创建成功！";
        Releases = await _context.ReleaseVersions
            .Include(r => r.Features)
<<<<<<< HEAD
            .Include(r => r.EngineBranch)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
        Branches = await _context.EngineBranches
            .OrderBy(b => b.Name)
            .ToListAsync();
=======
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
        ShowForm = false;
        ViewData["Title"] = "版本管理";

        return Page();
    }

    public async Task<IActionResult> OnPostEditAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        if (!ModelState.IsValid)
        {
            ViewData["Title"] = "编辑版本";
            Releases = await _context.ReleaseVersions
                .Include(r => r.Features)
<<<<<<< HEAD
                .Include(r => r.EngineBranch)
                .OrderByDescending(r => r.ReleaseDate)
                .ToListAsync();
            Branches = await _context.EngineBranches
                .OrderBy(b => b.Name)
                .ToListAsync();
=======
                .OrderByDescending(r => r.ReleaseDate)
                .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
            ShowForm = true;
            IsEditing = true;
            return Page();
        }

        var release = await _context.ReleaseVersions.FindAsync(EditId);
        if (release == null)
        {
            return RedirectToPage();
        }

        release.VersionNumber = Input.VersionNumber;
        release.Title = Input.Title;
        release.Description = Input.Description;
        release.DownloadUrl = Input.DownloadUrl;
        release.IsPublished = Input.IsPublished;
        release.IsBeta = Input.IsBeta;
        release.ReleaseDate = Input.ReleaseDate;
<<<<<<< HEAD
        release.LicenseType = Input.LicenseType;
        release.EngineBranchId = Input.EngineBranchId;
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a

        await _context.SaveChangesAsync();

        SuccessMessage = "版本更新成功！";
        Releases = await _context.ReleaseVersions
            .Include(r => r.Features)
<<<<<<< HEAD
            .Include(r => r.EngineBranch)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
        Branches = await _context.EngineBranches
            .OrderBy(b => b.Name)
            .ToListAsync();
=======
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
        ShowForm = false;
        ViewData["Title"] = "版本管理";

        return Page();
    }

    public async Task<IActionResult> OnPostDeleteAsync(int id)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var release = await _context.ReleaseVersions.FindAsync(id);
        if (release != null)
        {
            _context.ReleaseVersions.Remove(release);
            await _context.SaveChangesAsync();
            SuccessMessage = "版本已删除！";
        }

        ViewData["Title"] = "版本管理";
        Releases = await _context.ReleaseVersions
            .Include(r => r.Features)
<<<<<<< HEAD
            .Include(r => r.EngineBranch)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
        Branches = await _context.EngineBranches
            .OrderBy(b => b.Name)
            .ToListAsync();
=======
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a

        return Page();
    }

    public async Task<IActionResult> OnPostSyncFromGitHubAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var owner = _configuration["GitHub:Owner"] ?? "LoongSerpent9Realms";
        var repo = _configuration["GitHub:Repo"] ?? "Jundot";

        var releases = await _gitHubService.GetReleasesAsync(owner, repo, 20);
        if (releases == null || releases.Count == 0)
        {
            ErrorMessage = "无法从 GitHub 获取 Release 信息，请检查网络连接或仓库配置。";
            ViewData["Title"] = "版本管理";
            Releases = await _context.ReleaseVersions
                .Include(r => r.Features)
                .OrderByDescending(r => r.ReleaseDate)
                .ToListAsync();
            return Page();
        }

        var syncedCount = 0;
        foreach (var ghRelease in releases)
        {
            var existing = await _context.ReleaseVersions
                .FirstOrDefaultAsync(r => r.VersionNumber == ghRelease.TagName);

            if (existing == null)
            {
                var newRelease = new ReleaseVersion
                {
                    VersionNumber = ghRelease.TagName,
                    Title = ghRelease.Name,
                    Description = ghRelease.Body,
                    DownloadUrl = ghRelease.HtmlUrl,
                    IsPublished = true,
                    IsBeta = ghRelease.Prerelease,
                    ReleaseDate = ghRelease.PublishedAt ?? DateTime.Now
                };
                _context.ReleaseVersions.Add(newRelease);
                syncedCount++;
            }
            else
            {
                existing.Title = ghRelease.Name;
                existing.Description = ghRelease.Body;
                existing.DownloadUrl = ghRelease.HtmlUrl;
                existing.IsBeta = ghRelease.Prerelease;
                if (ghRelease.PublishedAt.HasValue)
                {
                    existing.ReleaseDate = ghRelease.PublishedAt.Value;
                }
            }
        }

        await _context.SaveChangesAsync();

        SuccessMessage = $"从 GitHub 同步成功！新增 {syncedCount} 个版本，更新 {releases.Count - syncedCount} 个版本。";
        ViewData["Title"] = "版本管理";
        Releases = await _context.ReleaseVersions
            .Include(r => r.Features)
<<<<<<< HEAD
            .Include(r => r.EngineBranch)
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
        Branches = await _context.EngineBranches
            .OrderBy(b => b.Name)
            .ToListAsync();
=======
            .OrderByDescending(r => r.ReleaseDate)
            .ToListAsync();
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a

        return Page();
    }
}
