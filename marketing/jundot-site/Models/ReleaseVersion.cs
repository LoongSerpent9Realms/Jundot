using System.ComponentModel.DataAnnotations;

namespace JundotSite.Models;

public class ReleaseVersion
{
    public int Id { get; set; }

    [Required]
    [StringLength(50)]
    public string VersionNumber { get; set; } = string.Empty;

    [Required]
    [StringLength(100)]
    public string Title { get; set; } = string.Empty;

    public string? Description { get; set; }

    [StringLength(500)]
    public string? DownloadUrl { get; set; }

    public bool IsPublished { get; set; } = false;

    public bool IsBeta { get; set; } = true;

    public DateTime ReleaseDate { get; set; } = DateTime.Now;

    public int DownloadCount { get; set; } = 0;

    public string? SupportedPlatforms { get; set; }

<<<<<<< HEAD
    public LicenseType LicenseType { get; set; } = LicenseType.MIT;

    public int? EngineBranchId { get; set; }

    public EngineBranch? EngineBranch { get; set; }

=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
    public List<ReleaseFeature> Features { get; set; } = new();
}

public class ReleaseFeature
{
    public int Id { get; set; }

    [Required]
    [StringLength(100)]
    public string Title { get; set; } = string.Empty;

    public string? Description { get; set; }

    public int ReleaseVersionId { get; set; }

    public ReleaseVersion? ReleaseVersion { get; set; }
}
