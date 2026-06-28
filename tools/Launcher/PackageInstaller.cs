using System.IO.Compression;
using System.Text;

namespace JundotLauncher;

/// <summary>
/// Installs a downloaded and verified package into the engine directory.
/// 
/// Flow:
///   1. Extract ZIP to staging directory (.update-staging/)
///   2. Backup current engine files to .backup/{version}/
///   3. Move staging files to engine directory
///   4. Handle locked files via .pending-replace.bat
/// </summary>
public class PackageInstaller
{
    private readonly string _engineDir;

    public PackageInstaller(string engineDir)
    {
        _engineDir = engineDir;
    }

    // ═══════════════════════════════════════════════════════════
    //  PUBLIC API
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Install a ZIP package into the engine directory.
    /// </summary>
    /// <param name="zipPath">Path to the downloaded & verified ZIP.</param>
    /// <param name="version">Version being installed (for backup naming).</param>
    /// <param name="progress">Optional progress reporter.</param>
    /// <returns>True on success.</returns>
    public async Task<bool> InstallAsync(
        string zipPath,
        string version,
        IProgress<double>? progress = null)
    {
        try
        {
            var stagingDir = Path.Combine(_engineDir, ".update-staging");
            var backupDir = Path.Combine(_engineDir, ".backup", version);

            // ── 1. Clean staging ──────────────────────────────
            if (Directory.Exists(stagingDir))
                Directory.Delete(stagingDir, true);
            Directory.CreateDirectory(stagingDir);

            // ── 2. Extract ZIP to staging ─────────────────────
            ConsoleUI.Step("解压更新包...");
            await Task.Run(() => ZipFile.ExtractToDirectory(zipPath, stagingDir, Encoding.UTF8, overwriteFiles: true));

            // ── 3. Clean excluded patterns ────────────────────
            CleanStaging(stagingDir);

            // ── 4. Backup current files ───────────────────────
            ConsoleUI.Step("备份当前版本...");
            await BackupCurrentAsync(backupDir, stagingDir);

            // ── 5. Replace files ──────────────────────────────
            ConsoleUI.Step("安装新版本...");
            var lockedFiles = await ReplaceFilesAsync(stagingDir);

            // ── 6. Handle locked files ────────────────────────
            if (lockedFiles.Count > 0)
            {
                var scriptPath = Path.Combine(_engineDir, ".pending-replace.bat");
                CreatePendingReplaceScript(scriptPath, lockedFiles);
                ConsoleUI.Warning($"{lockedFiles.Count} 个文件被锁定，将在下次重启时替换。");
                ConsoleUI.Info($"  待处理脚本: {scriptPath}");
            }

            // ── 7. Clean staging ──────────────────────────────
            try { Directory.Delete(stagingDir, true); }
            catch { ConsoleUI.Warning("无法完全清理临时目录，这不影响安装。"); }

            ConsoleUI.Success("安装完成！");

            return true;
        }
        catch (InvalidDataException ex)
        {
            ConsoleUI.Error($"ZIP 文件损坏: {ex.Message}");
            return false;
        }
        catch (UnauthorizedAccessException ex)
        {
            ConsoleUI.Error($"权限不足: {ex.Message}");
            ConsoleUI.Info("请以管理员身份运行启动器。");
            return false;
        }
        catch (Exception ex)
        {
            ConsoleUI.Error($"安装失败: {ex.Message}");
            return false;
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  INTERNAL
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Remove files/dirs from staging that should NOT replace existing ones.
    /// Preserves user data, configs, and cache.
    /// </summary>
    private void CleanStaging(string stagingDir)
    {
        var excludePatterns = new[]
        {
            "editor_data/editor_settings*.tres",  // User editor settings
            "editor_data/favorites",              // User favorites
            ".jundot-update-state.json",          // Update state
            ".pending-replace.bat",               // Our pending replace script
        };

        foreach (var pattern in excludePatterns)
        {
            var fullPattern = Path.Combine(stagingDir, pattern.Replace('/', Path.DirectorySeparatorChar));
            var dir = Path.GetDirectoryName(fullPattern);
            var filePattern = Path.GetFileName(fullPattern);
            if (dir != null && Directory.Exists(dir))
            {
                foreach (var match in Directory.GetFiles(dir, filePattern))
                {
                    try { File.Delete(match); } catch { /* best effort */ }
                }
            }
        }
    }

    /// <summary>
    /// Backup files that exist in the engine directory and will be replaced.
    /// </summary>
    private async Task BackupCurrentAsync(string backupDir, string stagingDir)
    {
        if (Directory.Exists(backupDir))
            Directory.Delete(backupDir, true);
        Directory.CreateDirectory(backupDir);

        var stagingFiles = Directory.GetFiles(stagingDir, "*", SearchOption.AllDirectories);
        var stagingRoot = Path.GetFullPath(stagingDir).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;

        int backedUp = 0;
        int skipped = 0;

        foreach (var stagingFile in stagingFiles)
        {
            var relativePath = stagingFile.StartsWith(stagingRoot, StringComparison.OrdinalIgnoreCase)
                ? stagingFile[stagingRoot.Length..]
                : Path.GetFileName(stagingFile);

            var engineFile = Path.Combine(_engineDir, relativePath);

            if (File.Exists(engineFile))
            {
                var backupPath = Path.Combine(backupDir, relativePath);
                var backupParent = Path.GetDirectoryName(backupPath);
                if (backupParent != null)
                    Directory.CreateDirectory(backupParent);

                await Task.Run(() => File.Copy(engineFile, backupPath, overwrite: true));
                backedUp++;
            }
            else
            {
                skipped++;
            }
        }

        ConsoleUI.Info($"  已备份 {backedUp} 个文件，{skipped} 个新文件无需备份");
    }

    /// <summary>
    /// Move files from staging to engine directory.
    /// Returns list of files that couldn't be replaced due to being locked.
    /// </summary>
    private async Task<List<PendingReplace>> ReplaceFilesAsync(string stagingDir)
    {
        var locked = new List<PendingReplace>();
        var stagingFiles = Directory.GetFiles(stagingDir, "*", SearchOption.AllDirectories);
        var stagingRoot = Path.GetFullPath(stagingDir).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;

        int replaced = 0;
        int skipped = 0;

        foreach (var stagingFile in stagingFiles)
        {
            var relativePath = stagingFile.StartsWith(stagingRoot, StringComparison.OrdinalIgnoreCase)
                ? stagingFile[stagingRoot.Length..]
                : Path.GetFileName(stagingFile);

            var destPath = Path.Combine(_engineDir, relativePath);
            var destDir = Path.GetDirectoryName(destPath);
            if (destDir != null)
                Directory.CreateDirectory(destDir);

            try
            {
                // Use atomic replace: delete old, move new
                if (File.Exists(destPath))
                    File.Delete(destPath);

                File.Move(stagingFile, destPath, overwrite: false);
                replaced++;
            }
            catch (IOException) when (IsFileLocked(destPath))
            {
                // File is locked — defer to next restart
                var tempPath = destPath + ".new";
                File.Copy(stagingFile, tempPath, overwrite: true);

                locked.Add(new PendingReplace
                {
                    TempPath = tempPath,
                    DestPath = destPath
                });
                skipped++;
            }
            catch (Exception ex)
            {
                ConsoleUI.Warning($"无法替换 {relativePath}: {ex.Message}");
                skipped++;
            }
        }

        ConsoleUI.Info($"  已替换 {replaced} 个文件，{skipped} 个被跳过");
        return locked;
    }

    /// <summary>
    /// Check if a file is locked by another process.
    /// </summary>
    private static bool IsFileLocked(string filePath)
    {
        if (!File.Exists(filePath)) return false;
        try
        {
            using var fs = File.Open(filePath, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
            return false;
        }
        catch (IOException)
        {
            return true;
        }
    }

    /// <summary>
    /// Create a batch script that replaces locked files after the current process exits.
    /// </summary>
    private static void CreatePendingReplaceScript(string scriptPath, List<PendingReplace> lockedFiles)
    {
        var sb = new StringBuilder();
        sb.AppendLine("@echo off");
        sb.AppendLine(":: JundotLauncher — Pending file replace script");
        sb.AppendLine(":: Auto-generated. Do not edit.");
        sb.AppendLine("echo Replacing locked files...");
        sb.AppendLine("timeout /t 2 /nobreak >nul");

        foreach (var item in lockedFiles)
        {
            sb.AppendLine($"echo   {Path.GetFileName(item.DestPath)}");
            sb.AppendLine($"move /Y \"{item.TempPath}\" \"{item.DestPath}\" 2>nul");
        }

        sb.AppendLine("echo Done.");
        sb.AppendLine("del \"%~f0\" 2>nul"); // Self-delete
        sb.AppendLine("exit /b 0");

        File.WriteAllText(scriptPath, sb.ToString(), Encoding.ASCII);
    }
}

/// <summary>Record of a file that couldn't be replaced immediately.</summary>
public class PendingReplace
{
    public string TempPath { get; init; } = "";
    public string DestPath { get; init; } = "";
}
