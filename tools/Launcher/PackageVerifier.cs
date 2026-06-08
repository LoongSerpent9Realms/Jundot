using System.Security.Cryptography;

namespace JundotLauncher;

/// <summary>
/// Verifies the integrity of downloaded packages using SHA-256.
/// Supports streaming hash computation to avoid loading entire files into memory.
/// </summary>
public class PackageVerifier
{
    // ── Public API ───────────────────────────────────────────

    /// <summary>
    /// Verify that a file's SHA-256 matches the expected hash.
    /// </summary>
    /// <param name="filePath">Path to the file to verify.</param>
    /// <param name="expectedSha256">Expected SHA-256 hex string (64 lowercase hex chars).</param>
    /// <param name="progress">Optional progress reporter.</param>
    /// <returns>True if verification passes, false otherwise.</returns>
    public static async Task<bool> VerifySha256Async(
        string filePath,
        string expectedSha256,
        IProgress<double>? progress = null)
    {
        if (!File.Exists(filePath))
        {
            ConsoleUI.Error($"文件不存在: {filePath}");
            return false;
        }

        if (string.IsNullOrEmpty(expectedSha256))
        {
            ConsoleUI.Warning("Manifest 中缺少 SHA256 值，跳过校验。");
            return true;
        }

        expectedSha256 = expectedSha256.Trim().ToLowerInvariant();

        if (expectedSha256.Length != 64)
        {
            ConsoleUI.Warning($"SHA256 格式异常（长度={expectedSha256.Length}，期望64），跳过校验。");
            return true;
        }

        var fileSize = new FileInfo(filePath).Length;

        try
        {
            var actual = await ComputeSha256Async(filePath, fileSize, progress);

            if (string.Equals(actual, expectedSha256, StringComparison.OrdinalIgnoreCase))
            {
                ConsoleUI.Success($"SHA256 校验通过");
                return true;
            }

            ConsoleUI.Error($"SHA256 校验失败！");
            ConsoleUI.Error($"  期望: {expectedSha256}");
            ConsoleUI.Error($"  实际: {actual}");
            return false;
        }
        catch (Exception ex)
        {
            ConsoleUI.Error($"校验过程异常: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Verify all files listed in the manifest's files[] array.
    /// </summary>
    /// <param name="installDir">Directory where files were extracted.</param>
    /// <param name="files">File entries from the manifest.</param>
    /// <returns>True if all required files pass verification.</returns>
    public static async Task<bool> VerifyFilesAsync(
        string installDir,
        List<FileEntryV1>? files,
        IProgress<double>? progress = null)
    {
        if (files == null || files.Count == 0)
        {
            ConsoleUI.Info("文件清单为空，跳过逐文件校验。");
            return true;
        }

        var required = files.Where(f => f.Required).ToList();
        if (required.Count == 0)
        {
            ConsoleUI.Info("无必需文件需要校验。");
            return true;
        }

        int passed = 0;
        int failed = 0;

        for (int i = 0; i < required.Count; i++)
        {
            var file = required[i];
            var filePath = Path.Combine(installDir, file.Path.Replace('/', Path.DirectorySeparatorChar));

            if (!File.Exists(filePath))
            {
                ConsoleUI.Error($"必需文件缺失: {file.Path}");
                failed++;
                continue;
            }

            if (string.IsNullOrEmpty(file.Sha256))
            {
                passed++;
                continue;
            }

            var actual = await ComputeSha256Async(filePath, file.Size);
            if (string.Equals(actual, file.Sha256.Trim(), StringComparison.OrdinalIgnoreCase))
            {
                passed++;
            }
            else
            {
                ConsoleUI.Error($"文件校验失败: {file.Path}");
                ConsoleUI.Error($"  期望: {file.Sha256}");
                ConsoleUI.Error($"  实际: {actual}");
                failed++;
            }

            progress?.Report((double)(i + 1) / required.Count * 100);
        }

        if (failed > 0)
        {
            ConsoleUI.Error($"{failed} 个文件校验失败，{passed} 个通过。");
            return false;
        }

        ConsoleUI.Success($"全部 {passed} 个文件校验通过。");
        return true;
    }

    // ── Internal ─────────────────────────────────────────────

    /// <summary>
    /// Compute SHA-256 hash of a file using streaming (memory-efficient for large files).
    /// </summary>
    private static async Task<string> ComputeSha256Async(
        string filePath,
        long fileSize,
        IProgress<double>? progress = null)
    {
        using var stream = File.OpenRead(filePath);
        using var sha256 = SHA256.Create();

        var buffer = new byte[81920]; // 80 KB
        long totalRead = 0;
        int bytesRead;

        while ((bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length)) > 0)
        {
            sha256.TransformBlock(buffer, 0, bytesRead, null, 0);
            totalRead += bytesRead;

            if (progress != null && fileSize > 0)
            {
                progress.Report((double)totalRead / fileSize * 100);
            }
        }

        sha256.TransformFinalBlock(Array.Empty<byte>(), 0, 0);

        var hash = sha256.Hash!;
        return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
    }
}
