using System.ComponentModel.DataAnnotations;

namespace JundotSite.Models;

public enum VerificationPurpose
{
    Register = 0,
    Login = 1
}

public class EmailVerificationCode
{
    public int Id { get; set; }

    [Required]
    [EmailAddress]
    public string Email { get; set; } = string.Empty;

    [Required]
    [StringLength(6)]
    public string Code { get; set; } = string.Empty;

    public VerificationPurpose Purpose { get; set; }

    public DateTime CreatedAt { get; set; } = DateTime.Now;

    public DateTime ExpiresAt { get; set; }

    public bool IsUsed { get; set; } = false;
}
