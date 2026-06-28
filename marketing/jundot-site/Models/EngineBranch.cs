using System.ComponentModel.DataAnnotations;

namespace JundotSite.Models;

public enum LicenseType
{
    MIT = 0,
    Commercial = 1,
    GPL = 2,
    Apache = 3,
    Custom = 99
}

public enum GameGenre
{
    Action2D = 0,
    Action3D = 1,
    RPG = 2,
    Puzzle = 3,
    Simulation = 4,
    Strategy = 5,
    Adventure = 6,
    Platformer = 7,
    Shooter = 8,
    Racing = 9,
    Sports = 10,
    Casual = 11,
    Other = 99
}

public class EngineBranch
{
    public int Id { get; set; }

    [Required]
    [StringLength(100)]
    public string Name { get; set; } = string.Empty;

    [Required]
    [StringLength(100)]
    public string Slug { get; set; } = string.Empty;

    public string? Description { get; set; }

    [StringLength(500)]
    public string? ThumbnailUrl { get; set; }

    public GameGenre Genre { get; set; } = GameGenre.Other;

    public LicenseType LicenseType { get; set; } = LicenseType.MIT;

    [StringLength(200)]
    public string? LicenseName { get; set; }

    public string? LicenseUrl { get; set; }

    public bool IsPublished { get; set; } = false;

    public bool IsFeatured { get; set; } = false;

    public DateTime CreatedAt { get; set; } = DateTime.Now;

    public DateTime UpdatedAt { get; set; } = DateTime.Now;

    public int DownloadCount { get; set; } = 0;

    public List<BranchFeature> Features { get; set; } = new();

    public List<ReleaseVersion> Releases { get; set; } = new();
}

public class BranchFeature
{
    public int Id { get; set; }

    [Required]
    [StringLength(100)]
    public string Title { get; set; } = string.Empty;

    public string? Description { get; set; }

    [StringLength(50)]
    public string? Icon { get; set; }

    public int SortOrder { get; set; } = 0;

    public int EngineBranchId { get; set; }

    public EngineBranch? EngineBranch { get; set; }
}
