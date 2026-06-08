using System.Security.Cryptography;
using System.Text;

namespace JundotLauncher;

/// <summary>
/// Evaluates whether the current machine is eligible for a grayscale update.
/// Supports two modes (both can be active simultaneously):
///   1. Percentage-based: hash(machine_id + seed) % 100 < percentage
///   2. Whitelist-based: machine_id is in the whitelist
/// 
/// Whitelist check takes priority — if the machine is whitelisted,
/// the update is always visible regardless of percentage.
/// </summary>
public class GrayscaleEvaluator
{
    private readonly string _machineId;
    private readonly string _defaultSeed;

    public GrayscaleEvaluator(string machineId, string defaultSeed = "jundot-grayscale-v1")
    {
        _machineId = machineId;
        _defaultSeed = defaultSeed;
    }

    // ═══════════════════════════════════════════════════════════
    //  PUBLIC API
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Evaluate whether the current machine should receive an update
    /// based on the manifest's grayscale configuration.
    /// </summary>
    /// <param name="grayscale">Grayscale config from the manifest, or null if no grayscale.</param>
    /// <returns>GrayscaleResult with eligibility and explanation.</returns>
    public GrayscaleResult Evaluate(GrayscaleConfigV1? grayscale)
    {
        // No grayscale config → update visible to everyone
        if (grayscale == null || !grayscale.Enabled)
            return GrayscaleResult.Eligible("灰度未启用，所有用户可见。");

        // ── 1. Whitelist check (highest priority) ─────────────
        if (grayscale.Whitelist.Count > 0)
        {
            foreach (var id in grayscale.Whitelist)
            {
                // Case-insensitive exact match
                if (string.Equals(id.Trim(), _machineId, StringComparison.OrdinalIgnoreCase))
                    return GrayscaleResult.Eligible("白名单匹配成功。");
            }
        }

        // ── 2. Percentage check ───────────────────────────────
        if (grayscale.Percentage >= 100)
            return GrayscaleResult.Eligible("灰度百分比=100%，全部可见。");

        if (grayscale.Percentage <= 0)
            return GrayscaleResult.NotEligible("灰度百分比=0%，全部不可见。");

        var seed = !string.IsNullOrEmpty(grayscale.MachineIdHashSeed)
            ? grayscale.MachineIdHashSeed
            : _defaultSeed;

        var hashValue = HashMachineId(_machineId, seed);
        var bucket = hashValue % 100;

        if (bucket < grayscale.Percentage)
            return GrayscaleResult.Eligible(
                $"灰度 {grayscale.Percentage}% 命中（bucket={bucket}）。");

        return GrayscaleResult.NotEligible(
            $"灰度 {grayscale.Percentage}% 未命中（bucket={bucket}）。");
    }

    // ═══════════════════════════════════════════════════════════
    //  HASH
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Compute a deterministic integer hash of (machine_id + seed).
    /// Uses SHA256 and takes the first 8 bytes as a 64-bit integer.
    /// Distribution is uniform enough for percentage-based bucketing.
    /// </summary>
    private static int HashMachineId(string machineId, string seed)
    {
        var input = $"{machineId}|{seed}|v1";
        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(input));

        // Take first 4 bytes as uint for a value in [0, 2^32)
        var value = BitConverter.ToUInt32(hash, 0);
        return (int)(value % 100);
    }
}

/// <summary>Result of a grayscale evaluation.</summary>
public class GrayscaleResult
{
    public bool IsEligible { get; private set; }
    public string Reason { get; private set; }

    private GrayscaleResult(bool eligible, string reason)
    {
        IsEligible = eligible;
        Reason = reason;
    }

    public static GrayscaleResult Eligible(string reason) => new(true, reason);
    public static GrayscaleResult NotEligible(string reason) => new(false, reason);

    public override string ToString() => IsEligible ? $"✓ {Reason}" : $"✗ {Reason}";
}
