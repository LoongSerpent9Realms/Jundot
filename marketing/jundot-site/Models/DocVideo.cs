using System.ComponentModel.DataAnnotations;

namespace JundotSite.Models;

public class DocVideo
{
    public int Id { get; set; }

    [Required]
    [StringLength(200)]
    public string Title { get; set; } = string.Empty;

    [StringLength(500)]
    public string? Description { get; set; }

    /// <summary>
    /// 0 = ExternalUrl, 1 = LocalUpload
    /// </summary>
    public int SourceType { get; set; } = 0;

    /// <summary>
    /// External URL (YouTube, Bilibili, etc.) or local file path relative to /videos/
    /// </summary>
    [Required]
    [StringLength(1000)]
    public string VideoUrl { get; set; } = string.Empty;

    /// <summary>
    /// Optional thumbnail image URL
    /// </summary>
    [StringLength(1000)]
    public string? ThumbnailUrl { get; set; }

    /// <summary>
    /// Associated documentation page ID (e.g. "quickstart", "ai-assistant")
    /// Can be null for unassigned videos
    /// </summary>
    [StringLength(100)]
    public string? DocId { get; set; }

    /// <summary>
    /// Video category for grouping (e.g. "教程", "演示", "入门")
    /// </summary>
    [StringLength(100)]
    public string? Category { get; set; }

    /// <summary>
    /// Sort order within a doc or category
    /// </summary>
    public int SortOrder { get; set; } = 0;

    /// <summary>
    /// Whether the video is visible on the front-end
    /// </summary>
    public bool IsPublished { get; set; } = true;

    public DateTime CreatedAt { get; set; } = DateTime.Now;

    public DateTime UpdatedAt { get; set; } = DateTime.Now;
}
