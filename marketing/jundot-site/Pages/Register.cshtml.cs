using System.ComponentModel.DataAnnotations;
using JundotSite.Models;
using JundotSite.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace JundotSite.Pages;

public class RegisterModel : PageModel
{
    private readonly AuthService _authService;

    public RegisterModel(AuthService authService)
    {
        _authService = authService;
    }

    [BindProperty]
    public RegisterInput Input { get; set; } = new();

    [BindProperty]
    public string? VerificationCode { get; set; }

    public int Step { get; set; } = 1;

    public string? ErrorMessage { get; set; }
    public string? SuccessMessage { get; set; }

    public class RegisterInput
    {
        [Required(ErrorMessage = "请输入用户名")]
        [StringLength(50, MinimumLength = 2, ErrorMessage = "用户名长度2-50个字符")]
        public string Username { get; set; } = string.Empty;

        [Required(ErrorMessage = "请输入邮箱")]
        [EmailAddress(ErrorMessage = "请输入有效的邮箱地址")]
        public string Email { get; set; } = string.Empty;

        [Required(ErrorMessage = "请输入密码")]
        [StringLength(100, MinimumLength = 6, ErrorMessage = "密码至少6位")]
        public string Password { get; set; } = string.Empty;

        [Required(ErrorMessage = "请确认密码")]
        [Compare("Password", ErrorMessage = "两次输入的密码不一致")]
        public string ConfirmPassword { get; set; } = string.Empty;
    }

    public void OnGet()
    {
        ViewData["Title"] = "用户注册";
    }

    public async Task<IActionResult> OnPostSendCode()
    {
        ViewData["Title"] = "用户注册";

        if (!ModelState.IsValid)
        {
            return Page();
        }

        // 验证密码确认
        if (Input.Password != Input.ConfirmPassword)
        {
            ErrorMessage = "两次输入的密码不一致";
            return Page();
        }

        // 存储注册信息到临时cookie
        var registerData = new
        {
            Username = Input.Username.Trim(),
            Email = Input.Email.ToLower().Trim(),
            Password = Input.Password
        };

        HttpContext.Session.SetString("PendingRegister", System.Text.Json.JsonSerializer.Serialize(registerData));

        // 发送验证码
        var (success, message) = await _authService.SendVerificationCodeAsync(Input.Email.ToLower().Trim(), VerificationPurpose.Register);

        if (!success)
        {
            ErrorMessage = message;
            return Page();
        }

        Step = 2;
        SuccessMessage = "验证码已发送到您的邮箱，请查收";
        return Page();
    }

    public async Task<IActionResult> OnPostResendCode()
    {
        ViewData["Title"] = "用户注册";

        if (string.IsNullOrEmpty(Input.Email))
        {
            ErrorMessage = "请重新填写邮箱";
            Step = 1;
            return Page();
        }

        var (success, message) = await _authService.SendVerificationCodeAsync(Input.Email.ToLower().Trim(), VerificationPurpose.Register);

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
        ViewData["Title"] = "用户注册";

        if (string.IsNullOrEmpty(VerificationCode) || VerificationCode.Length != 6)
        {
            ErrorMessage = "请输入6位验证码";
            Step = 2;
            return Page();
        }

        var pendingRegisterJson = HttpContext.Session.GetString("PendingRegister");
        if (string.IsNullOrEmpty(pendingRegisterJson))
        {
            ErrorMessage = "注册信息已过期，请重新注册";
            Step = 1;
            return Page();
        }

        var pendingData = System.Text.Json.JsonSerializer.Deserialize<System.Text.Json.JsonElement>(pendingRegisterJson);
        var email = pendingData.GetProperty("Email").GetString() ?? "";
        var username = pendingData.GetProperty("Username").GetString() ?? "";
        var password = pendingData.GetProperty("Password").GetString() ?? "";

        Input.Username = username;
        Input.Email = email;
        Input.Password = password;

        // 先验证验证码
        var (verifySuccess, verifyMessage, user) = await _authService.VerifyCodeAsync(email, VerificationCode.Trim(), VerificationPurpose.Register);

        if (!verifySuccess)
        {
            ErrorMessage = verifyMessage;
            Step = 2;
            return Page();
        }

        // 验证码成功后，设置密码
        if (user != null)
        {
            await _authService.SetUserPasswordAsync(user.Id, password);
        }

        HttpContext.Session.Remove("PendingRegister");
        SuccessMessage = "注册成功！请使用邮箱和密码登录";
        return RedirectToPage("/Login", new { registered = true });
    }
}
