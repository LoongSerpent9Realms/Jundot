using System.ComponentModel.DataAnnotations;

namespace JundotSite.Models;

public class DocSnapshot
{
    public int Id { get; set; }

    [Required]
    [StringLength(100)]
    public string DocId { get; set; } = string.Empty;

    [StringLength(200)]
    public string? DocTitle { get; set; }

    [StringLength(10)]
    public string Language { get; set; } = "zh-CN";

    public string Content { get; set; } = string.Empty;

    [StringLength(100)]
    public string? SnapshotName { get; set; }

    public DateTime CreatedAt { get; set; } = DateTime.Now;

    public int CreatedByUserId { get; set; }

    public string? CreatedByUsername { get; set; }
}
