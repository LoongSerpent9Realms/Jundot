using JundotSite.Models;
using JundotSite.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace JundotSite.Pages.Admin;

public class UsersModel : AdminPageModel
{
    private readonly AuthService _authService;

    public UsersModel(AuthService authService)
    {
        _authService = authService;
    }

    public List<JundotSite.Models.User> Users { get; set; } = new();
    public string? SuccessMessage { get; set; }
    public string? ErrorMessage { get; set; }

    public IActionResult OnGet()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "用户管理";
        Users = _authService.GetAllUsersAsync().GetAwaiter().GetResult();
        return Page();
    }

    public IActionResult OnPostToggleActive(int userId)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var user = _authService.GetUserByIdAsync(userId).GetAwaiter().GetResult();
        if (user == null)
        {
            ErrorMessage = "用户不存在";
            Users = _authService.GetAllUsersAsync().GetAwaiter().GetResult();
            return Page();
        }

        // 防止禁用自己
        var currentUserId = HttpContext.Session.GetString("UserId");
        if (!string.IsNullOrEmpty(currentUserId) && int.Parse(currentUserId) == userId)
        {
            ErrorMessage = "不能禁用自己的账号";
            Users = _authService.GetAllUsersAsync().GetAwaiter().GetResult();
            return Page();
        }

        // 防止禁用管理员
        if (user.Role == UserRole.Admin)
        {
            ErrorMessage = "不能禁用管理员账号";
            Users = _authService.GetAllUsersAsync().GetAwaiter().GetResult();
            return Page();
        }

        var success = _authService.ToggleUserActiveAsync(userId).GetAwaiter().GetResult();
        if (success)
        {
            SuccessMessage = user.IsActive ? $"用户「{user.Username}」已禁用" : $"用户「{user.Username}」已启用";
        }
        else
        {
            ErrorMessage = "操作失败";
        }

        Users = _authService.GetAllUsersAsync().GetAwaiter().GetResult();
        return Page();
    }
}
