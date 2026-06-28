using System.Text.Json;

namespace JundotPackageBuilder;

/// <summary>
/// Represents a single Jundot build record — persisted to .build-history.json
/// and displayed in the Builds tab.
/// </summary>
public class BuildRecord
{
    public string Id          { get; set; } = Guid.NewGuid().ToString("N")[..12];
    public string PackageName { get; set; } = "";
    public string Version     { get; set; } = "";
    public string Platform    { get; set; } = "";
    public string Target      { get; set; } = "";
    public string Arch        { get; set; } = "";
    public bool   Mono        { get; set; } = false;
    public DateTime CreatedAt { get; set; } = DateTime.Now;

    /// <summary>Path to the main executable (bin/ or staging dir).</summary>
    public string ExePath       { get; set; } = "";

    /// <summary>Path to the staging directory (if packaged).</summary>
    public string PackageDir    { get; set; } = "";

    /// <summary>Path to the zip (if packaged).</summary>
    public string ZipPath       { get; set; } = "";

    /// <summary>Main build log path.</summary>
    public string BuildLogPath  { get; set; } = "";

    /// <summary>Git commit hash (if available).</summary>
    public string Commit        { get; set; } = "";

    /// <summary>Human-readable summary line for the list view.</summary>
    public string Summary => $"{Version} {Target} {Arch}{(Mono ? " Mono" : "")}";

    /// <summary>Whether the exe file actually exists on disk.</summary>
    public bool ExeExists => File.Exists(ExePath);

    /// <summary>File size of the exe in MB.</summary>
    public string SizeDisplay
    {
        get
        {
            try
            {
                if (!File.Exists(ExePath)) return "N/A";
                var size = new FileInfo(ExePath).Length;
                return size switch
                {
                    >= 1024 * 1024 * 1024 => $"{size / (1024.0 * 1024 * 1024):F2} GB",
                    >= 1024 * 1024        => $"{size / (1024.0 * 1024):F1} MB",
                    >= 1024              => $"{size / 1024:F0} KB",
                    _                    => $"{size} B"
                };
            }
            catch { return "N/A"; }
        }
    }
}

/// <summary>
/// Persisted build history — stored as JSON in artifacts/packages/.build-history.json.
/// </summary>
public class BuildHistory
{
    public List<BuildRecord> Records { get; set; } = new();
    public List<string> HiddenExePaths { get; set; } = new();
    public DateTime LastScanned { get; set; }
}
