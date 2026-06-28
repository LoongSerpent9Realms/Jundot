using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;

namespace JundotSite.Pages.Admin;

public class BranchesModel : AdminPageModel
{
    private readonly ApplicationDbContext _context;

    public BranchesModel(ApplicationDbContext context)
    {
        _context = context;
    }

    public List<EngineBranch> Branches { get; set; } = new();

    [BindProperty]
    public BranchEditInput Input { get; set; } = new();

    public bool ShowForm { get; set; }
    public bool IsEditing { get; set; }

    [BindProperty]
    public int EditId { get; set; }

    public string? SuccessMessage { get; set; }
    public string? ErrorMessage { get; set; }

    public class BranchEditInput
    {
        [Required(ErrorMessage = "请输入分支名称")]
        [StringLength(100)]
        public string Name { get; set; } = string.Empty;

        [Required(ErrorMessage = "请输入分支标识")]
        [StringLength(100)]
        public string Slug { get; set; } = string.Empty;

        public string? Description { get; set; }

        [StringLength(500)]
        public string? ThumbnailUrl { get; set; }

        public GameGenre Genre { get; set; } = GameGenre.Other;

        public LicenseType LicenseType { get; set; } = LicenseType.MIT;

        [StringLength(200)]
        public string? LicenseName { get; set; }

        public string? LicenseUrl { get; set; }

        public bool IsPublished { get; set; }
        public bool IsFeatured { get; set; }
    }

    public async Task<IActionResult> OnGetAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "分支管理";
        Branches = await _context.EngineBranches
            .Include(b => b.Features)
            .Include(b => b.Releases)
            .OrderByDescending(b => b.IsFeatured)
            .ThenByDescending(b => b.CreatedAt)
            .ToListAsync();
        return Page();
    }

    public async Task<IActionResult> OnGetCreateAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "新建分支";
        Branches = await _context.EngineBranches
            .Include(b => b.Features)
            .OrderByDescending(b => b.CreatedAt)
            .ToListAsync();
        ShowForm = true;
        IsEditing = false;
        Input = new BranchEditInput();
        return Page();
    }

    public async Task<IActionResult> OnGetEditAsync(int id)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "编辑分支";
        Branches = await _context.EngineBranches
            .Include(b => b.Features)
            .OrderByDescending(b => b.CreatedAt)
            .ToListAsync();

        var branch = await _context.EngineBranches.FindAsync(id);
        if (branch == null)
        {
            return RedirectToPage();
        }

        EditId = id;
        Input = new BranchEditInput
        {
            Name = branch.Name,
            Slug = branch.Slug,
            Description = branch.Description,
            ThumbnailUrl = branch.ThumbnailUrl,
            Genre = branch.Genre,
            LicenseType = branch.LicenseType,
            LicenseName = branch.LicenseName,
            LicenseUrl = branch.LicenseUrl,
            IsPublished = branch.IsPublished,
            IsFeatured = branch.IsFeatured
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
            ViewData["Title"] = "新建分支";
            Branches = await _context.EngineBranches
                .Include(b => b.Features)
                .OrderByDescending(b => b.CreatedAt)
                .ToListAsync();
            ShowForm = true;
            IsEditing = false;
            return Page();
        }

        var existing = await _context.EngineBranches
            .FirstOrDefaultAsync(b => b.Slug == Input.Slug);
        if (existing != null)
        {
            ErrorMessage = "分支标识已存在，请使用不同的标识。";
            ViewData["Title"] = "新建分支";
            Branches = await _context.EngineBranches
                .Include(b => b.Features)
                .OrderByDescending(b => b.CreatedAt)
                .ToListAsync();
            ShowForm = true;
            IsEditing = false;
            return Page();
        }

        var branch = new EngineBranch
        {
            Name = Input.Name,
            Slug = Input.Slug,
            Description = Input.Description,
            ThumbnailUrl = Input.ThumbnailUrl,
            Genre = Input.Genre,
            LicenseType = Input.LicenseType,
            LicenseName = Input.LicenseName,
            LicenseUrl = Input.LicenseUrl,
            IsPublished = Input.IsPublished,
            IsFeatured = Input.IsFeatured,
            CreatedAt = DateTime.Now,
            UpdatedAt = DateTime.Now
        };

        _context.EngineBranches.Add(branch);
        await _context.SaveChangesAsync();

        SuccessMessage = "分支创建成功！";
        Branches = await _context.EngineBranches
            .Include(b => b.Features)
            .Include(b => b.Releases)
            .OrderByDescending(b => b.IsFeatured)
            .ThenByDescending(b => b.CreatedAt)
            .ToListAsync();
        ShowForm = false;
        ViewData["Title"] = "分支管理";

        return Page();
    }

    public async Task<IActionResult> OnPostEditAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        if (!ModelState.IsValid)
        {
            ViewData["Title"] = "编辑分支";
            Branches = await _context.EngineBranches
                .Include(b => b.Features)
                .OrderByDescending(b => b.CreatedAt)
                .ToListAsync();
            ShowForm = true;
            IsEditing = true;
            return Page();
        }

        var branch = await _context.EngineBranches.FindAsync(EditId);
        if (branch == null)
        {
            return RedirectToPage();
        }

        var existing = await _context.EngineBranches
            .FirstOrDefaultAsync(b => b.Slug == Input.Slug && b.Id != EditId);
        if (existing != null)
        {
            ErrorMessage = "分支标识已存在，请使用不同的标识。";
            ViewData["Title"] = "编辑分支";
            Branches = await _context.EngineBranches
                .Include(b => b.Features)
                .OrderByDescending(b => b.CreatedAt)
                .ToListAsync();
            ShowForm = true;
            IsEditing = true;
            return Page();
        }

        branch.Name = Input.Name;
        branch.Slug = Input.Slug;
        branch.Description = Input.Description;
        branch.ThumbnailUrl = Input.ThumbnailUrl;
        branch.Genre = Input.Genre;
        branch.LicenseType = Input.LicenseType;
        branch.LicenseName = Input.LicenseName;
        branch.LicenseUrl = Input.LicenseUrl;
        branch.IsPublished = Input.IsPublished;
        branch.IsFeatured = Input.IsFeatured;
        branch.UpdatedAt = DateTime.Now;

        await _context.SaveChangesAsync();

        SuccessMessage = $"分支更新成功！当前状态：已发布={branch.IsPublished}，推荐展示={branch.IsFeatured}";
        Branches = await _context.EngineBranches
            .Include(b => b.Features)
            .Include(b => b.Releases)
            .OrderByDescending(b => b.IsFeatured)
            .ThenByDescending(b => b.CreatedAt)
            .ToListAsync();
        ShowForm = false;
        ViewData["Title"] = "分支管理";

        return Page();
    }

    public async Task<IActionResult> OnPostDeleteAsync(int id)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var branch = await _context.EngineBranches.FindAsync(id);
        if (branch != null)
        {
            _context.EngineBranches.Remove(branch);
            await _context.SaveChangesAsync();
            SuccessMessage = "分支已删除！";
        }

        ViewData["Title"] = "分支管理";
        Branches = await _context.EngineBranches
            .Include(b => b.Features)
            .Include(b => b.Releases)
            .OrderByDescending(b => b.IsFeatured)
            .ThenByDescending(b => b.CreatedAt)
            .ToListAsync();

        return Page();
    }
}
