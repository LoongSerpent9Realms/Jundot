using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace JundotSite.Pages.Admin;

public class AdminPageModel : PageModel
{
    protected bool IsAdminLoggedIn()
    {
        return HttpContext.Session.GetString("IsAdmin") == "true";
    }

    protected IActionResult RequireLogin()
    {
        if (!IsAdminLoggedIn())
        {
            return RedirectToPage("/Admin/Login");
        }
        return Page();
    }
}
