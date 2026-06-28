using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace JundotSite.Pages.Admin;

public class LoginModel : PageModel
{
    private readonly IConfiguration _configuration;

    public LoginModel(IConfiguration configuration)
    {
        _configuration = configuration;
    }

    [BindProperty]
    public LoginInput Input { get; set; } = new();

    public string? ErrorMessage { get; set; }

    public class LoginInput
    {
        [Required(ErrorMessage = "请输入用户名")]
        [Display(Name = "用户名")]
        public string Username { get; set; } = string.Empty;

        [Required(ErrorMessage = "请输入密码")]
        [DataType(DataType.Password)]
        [Display(Name = "密码")]
        public string Password { get; set; } = string.Empty;
    }

    public void OnGet()
    {
        ViewData["Title"] = "管理员登录";
    }

    public IActionResult OnPost()
    {
        ViewData["Title"] = "管理员登录";

        if (!ModelState.IsValid)
        {
            return Page();
        }

        var adminUsername = _configuration["AdminSettings:Username"];
        var adminPassword = _configuration["AdminSettings:Password"];

        if (string.IsNullOrEmpty(adminUsername) || string.IsNullOrEmpty(adminPassword))
        {
            ErrorMessage = "系统配置错误，请联系管理员";
            return Page();
        }

        var inputUsername = Input.Username.Trim();
        var inputPassword = Input.Password;

        if (inputUsername == adminUsername && inputPassword == adminPassword)
        {
            HttpContext.Session.SetString("IsAdmin", "true");
            HttpContext.Session.SetString("AdminUser", inputUsername);
            return RedirectToPage("/Admin/Index");
        }

        ErrorMessage = "用户名或密码错误";
        return Page();
    }
}
