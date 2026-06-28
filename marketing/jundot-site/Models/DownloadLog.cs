using System.ComponentModel.DataAnnotations;

namespace JundotSite.Models;

public class DownloadLog
{
    public int Id { get; set; }

    [StringLength(50)]
    public string? VersionNumber { get; set; }

    [StringLength(100)]
    public string? Platform { get; set; }

    [StringLength(50)]
    public string? IpAddress { get; set; }

    [StringLength(200)]
    public string? UserAgent { get; set; }

    public DateTime DownloadedAt { get; set; } = DateTime.Now;
}
