using System.ComponentModel.DataAnnotations;
using JundotSite.Models;
using JundotSite.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace JundotSite.Pages;

public class LoginModel : PageModel
{
    private readonly AuthService _authService;

    public LoginModel(AuthService authService)
    {
        _authService = authService;
    }

    [BindProperty]
    public LoginInput Input { get; set; } = new();

    [BindProperty]
    public string VerificationCode { get; set; } = string.Empty;

    public int Step { get; set; } = 1;

    public string? ErrorMessage { get; set; }
    public string? SuccessMessage { get; set; }

    public class LoginInput
    {
        [Required(ErrorMessage = "请输入邮箱")]
        [EmailAddress(ErrorMessage = "请输入有效的邮箱地址")]
        public string Email { get; set; } = string.Empty;

        [Required(ErrorMessage = "请输入密码")]
        public string Password { get; set; } = string.Empty;
    }

    public void OnGet()
    {
        ViewData["Title"] = "用户登录";
    }

    public async Task<IActionResult> OnPostPassword()
    {
        ViewData["Title"] = "用户登录";

        if (!ModelState.IsValid)
        {
            return Page();
        }

        var (success, message, user) = await _authService.LoginWithPasswordAsync(Input.Email, Input.Password);

        if (!success || user == null)
        {
            ErrorMessage = message;
            return Page();
        }

        // 设置用户 Session
        HttpContext.Session.SetString("UserId", user.Id.ToString());
        HttpContext.Session.SetString("Username", user.Username);
        HttpContext.Session.SetString("UserEmail", user.Email);
        HttpContext.Session.SetString("UserRole", user.Role.ToString());
        HttpContext.Session.Remove("IsAdmin"); // 确保不混淆

        return RedirectToPage("/Docs");
    }

    public async Task<IActionResult> OnPostSendCode()
    {
        ViewData["Title"] = "用户登录";

        if (string.IsNullOrWhiteSpace(Input.Email))
        {
            ErrorMessage = "请输入邮箱";
            return Page();
        }

        // 存储邮箱到临时 cookie
        HttpContext.Session.SetString("PendingLoginEmail", Input.Email.ToLower().Trim());

        var (success, message) = await _authService.SendVerificationCodeAsync(Input.Email.ToLower().Trim(), VerificationPurpose.Login);

        if (!success)
        {
            ErrorMessage = message;
            return Page();
        }

        Step = 2;
        SuccessMessage = "验证码已发送到您的邮箱";
        return Page();
    }

    public async Task<IActionResult> OnPostResendCode()
    {
        ViewData["Title"] = "用户登录";

        var pendingEmail = HttpContext.Session.GetString("PendingLoginEmail");
        if (string.IsNullOrEmpty(pendingEmail))
        {
            ErrorMessage = "请重新输入邮箱";
            Step = 1;
            return Page();
        }

        Input.Email = pendingEmail;

        var (success, message) = await _authService.SendVerificationCodeAsync(pendingEmail, VerificationPurpose.Login);

        if (!success)
        {
            ErrorMessage = message;
            Step = 2;
            return Page();
        }

        Step = 2;
        SuccessMessage = "验证码已重新发送到您的邮箱";
        return Page();
    }

    public async Task<IActionResult> OnPostVerifyCode()
    {
        ViewData["Title"] = "用户登录";

        if (string.IsNullOrEmpty(VerificationCode) || VerificationCode.Length != 6)
        {
            ErrorMessage = "请输入6位验证码";
            Step = 2;
            return Page();
        }

        var pendingEmail = HttpContext.Session.GetString("PendingLoginEmail");
        if (string.IsNullOrEmpty(pendingEmail))
        {
            ErrorMessage = "登录信息已过期，请重新尝试";
            Step = 1;
            return Page();
        }

        Input.Email = pendingEmail;

        var (success, message, user) = await _authService.VerifyCodeAsync(pendingEmail, VerificationCode.Trim(), VerificationPurpose.Login);

        if (!success || user == null)
        {
            ErrorMessage = message;
            Step = 2;
            return Page();
        }

        HttpContext.Session.Remove("PendingLoginEmail");

        // 设置用户 Session
        HttpContext.Session.SetString("UserId", user.Id.ToString());
        HttpContext.Session.SetString("Username", user.Username);
        HttpContext.Session.SetString("UserEmail", user.Email);
        HttpContext.Session.SetString("UserRole", user.Role.ToString());
        HttpContext.Session.Remove("IsAdmin");

        return RedirectToPage("/Docs");
    }
}
