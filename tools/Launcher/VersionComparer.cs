using System.Text.RegularExpressions;

namespace JundotLauncher;

/// <summary>
/// Compares Jundot version strings and evaluates update eligibility
/// based on channel filtering and version precedence rules.
/// 
/// Version format: major.minor[.patch][-status]
/// Status priority (highest to lowest): stable > rc > beta > alpha > dev
/// </summary>
public static class VersionComparer
{
    /// <summary>Status priority scores (higher = more stable/preferred).</summary>
    private static readonly Dictionary<string, int> StatusPriority = new(StringComparer.OrdinalIgnoreCase)
    {
        ["stable"] = 50,
        ["rc"]     = 40,
        ["beta"]   = 30,
        ["alpha"]  = 20,
        ["dev"]    = 10,
        [""]       = 50 // no status = stable
    };

    // ═══════════════════════════════════════════════════════════
    //  PUBLIC API
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Determine if the manifest version is newer than the current installed version,
    /// considering channel filtering rules.
    /// </summary>
    /// <param name="currentVersion">Currently installed version string (e.g. "1.7.2-beta").</param>
    /// <param name="manifestVersion">Manifest version string.</param>
    /// <param name="manifestChannel">Manifest release channel (stable/beta/dev).</param>
    /// <param name="preferredChannel">User's preferred update channel.</param>
    /// <returns>True if an update should be offered.</returns>
    public static bool IsUpdateAvailable(
        string currentVersion,
        string manifestVersion,
        string manifestChannel,
        string preferredChannel)
    {
        if (!ChannelMatches(manifestChannel, preferredChannel))
            return false;

        var current = Parse(currentVersion);
        var target = Parse(manifestVersion);

        return Compare(target, current) > 0;
    }

    /// <summary>
    /// Compare two version strings.
    /// Returns positive if a > b, negative if a < b, zero if equal.
    /// </summary>
    public static int Compare(string a, string b)
    {
        return Compare(Parse(a), Parse(b));
    }

    /// <summary>
    /// Check if the manifest channel is eligible for the user's preferred channel.
    /// </summary>
    /// <param name="manifestChannel">Channel from manifest (stable/beta/dev).</param>
    /// <param name="preferredChannel">User's preferred channel.</param>
    /// <returns>True if the manifest should be offered under this channel setting.</returns>
    public static bool ChannelMatches(string manifestChannel, string preferredChannel)
    {
        return preferredChannel.ToLowerInvariant() switch
        {
            "stable" => manifestChannel == "stable",
            "beta" => manifestChannel is "stable" or "beta",
            "dev" => true, // dev channel sees everything
            _ => manifestChannel == "stable"
        };
    }

    /// <summary>
    /// Check if the current version meets the minimum version requirement.
    /// </summary>
    public static bool MeetsMinVersion(string currentVersion, string? minVersion)
    {
        if (string.IsNullOrEmpty(minVersion))
            return true;

        return Compare(Parse(currentVersion), Parse(minVersion)) >= 0;
    }

    /// <summary>
    /// Parse a Jundot version string into structured components.
    /// Handles: "1.7.2-beta", "1.7", "4", "1.7.2-beta.1", etc.
    /// </summary>
    public static VersionParts Parse(string version)
    {
        if (string.IsNullOrWhiteSpace(version))
            return new VersionParts();

        var result = new VersionParts();
        var v = version.Trim();

        // Split on first hyphen: numeric part + status part
        var hyphenIdx = v.IndexOf('-');
        var numericStr = hyphenIdx > 0 ? v[..hyphenIdx] : v;
        var statusStr = hyphenIdx > 0 ? v[(hyphenIdx + 1)..] : "stable";

        // Parse numeric: major.minor[.patch[.build]]
        var parts = numericStr.Split('.');
        if (parts.Length > 0 && int.TryParse(parts[0], out var m))
            result.Major = m;
        if (parts.Length > 1 && int.TryParse(parts[1], out var n))
            result.Minor = n;
        if (parts.Length > 2 && int.TryParse(parts[2], out var p))
            result.Patch = p;

        result.Status = statusStr;
        result.Full = v;

        return result;
    }

    // ═══════════════════════════════════════════════════════════
    //  INTERNAL COMPARISON
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Compare two parsed versions. Returns positive if a > b.
    /// Comparison order: major → minor → patch → status priority
    /// </summary>
    private static int Compare(VersionParts a, VersionParts b)
    {
        // Numeric comparison
        if (a.Major != b.Major) return a.Major.CompareTo(b.Major);
        if (a.Minor != b.Minor) return a.Minor.CompareTo(b.Minor);
        if (a.Patch != b.Patch) return a.Patch.CompareTo(b.Patch);

        // Status comparison: stable > rc > beta > alpha > dev
        var aPri = GetStatusPriority(a.Status);
        var bPri = GetStatusPriority(b.Status);
        return aPri.CompareTo(bPri);
    }

    /// <summary>Get numeric priority for a status label.</summary>
    public static int GetStatusPriority(string status)
    {
        if (string.IsNullOrEmpty(status))
            return StatusPriority["stable"];

        // Handle compound statuses like "beta.1" → extract base status
        var baseStatus = status.Split('.')[0].ToLowerInvariant();
        return StatusPriority.GetValueOrDefault(baseStatus, 0);
    }
}

/// <summary>Parsed Jundot version components.</summary>
public struct VersionParts
{
    public int Major { get; set; }
    public int Minor { get; set; }
    public int Patch { get; set; }
    public string Status { get; set; }
    public string Full { get; set; }

    public VersionParts()
    {
        Major = 0;
        Minor = 0;
        Patch = 0;
        Status = "";
        Full = "";
    }

    public override string ToString() => Full.Length > 0 ? Full : $"{Major}.{Minor}.{Patch}{(!string.IsNullOrEmpty(Status) ? $"-{Status}" : "")}";
}
