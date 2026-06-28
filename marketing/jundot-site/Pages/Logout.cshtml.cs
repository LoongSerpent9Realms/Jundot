using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace JundotSite.Pages;

public class LogoutModel : PageModel
{
    public IActionResult OnGet()
    {
        return OnPost();
    }

    public IActionResult OnPost()
    {
        // 清除所有 Session
        HttpContext.Session.Clear();

        return RedirectToPage("/Index");
    }
}
