namespace JundotLauncher;

/// <summary>
/// Manages backup retention and rollback operations.
/// 
/// Backups are stored in {engineDir}/.backup/{version}/
/// Kept by the UpdateStateStore's backup list.
/// </summary>
public class RollbackManager
{
    private readonly string _engineDir;
    private readonly UpdateStateStore _state;

    public RollbackManager(string engineDir, UpdateStateStore state)
    {
        _engineDir = engineDir;
        _state = state;
    }

    // ═══════════════════════════════════════════════════════════
    //  PUBLIC API
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Rollback to the most recent backup.
    /// </summary>
    /// <param name="targetVersion">Specific backup version to restore, or null for latest.</param>
    /// <returns>True on success.</returns>
    public async Task<bool> RollbackAsync(string? targetVersion = null, bool confirm = true)
    {
        BackupEntry? backup;

        if (targetVersion != null)
        {
            backup = _state.State.Backups.FirstOrDefault(
                b => string.Equals(b.Version, targetVersion, StringComparison.OrdinalIgnoreCase));
        }
        else
        {
            backup = _state.GetLatestBackup();
        }

        if (backup == null)
        {
            ConsoleUI.Error("没有可用的备份。");
            return false;
        }

        if (!Directory.Exists(backup.BackupPath))
        {
            ConsoleUI.Error($"备份目录不存在: {backup.BackupPath}");
            return false;
        }

        ConsoleUI.Header($"回滚到 {backup.Version}");
        ConsoleUI.Info($"备份路径: {backup.BackupPath}");
        ConsoleUI.Info($"创建时间: {backup.CreatedAt:yyyy-MM-dd HH:mm:ss}");

        if (confirm && !ConsoleUI.AskYesNo("确认回滚？当前版本将被替换。", defaultYes: false))
        {
            ConsoleUI.Info("回滚已取消。");
            return false;
        }

        try
        {
            // ── 1. Create a safety backup of current state ─────
            var safetyBackupDir = Path.Combine(_engineDir, ".backup", $"_pre-rollback-{DateTime.Now:yyyyMMddHHmmss}");
            ConsoleUI.Step("创建安全备份...");
            await QuickBackupAsync(safetyBackupDir);

            // ── 2. Restore files from backup ───────────────────
            ConsoleUI.Step("还原文件...");
            var restored = await RestoreFromBackupAsync(backup.BackupPath);

            // ── 3. Update state ────────────────────────────────
            _state.CurrentVersion = backup.Version;
            _state.SkippedVersion = null;

            ConsoleUI.Success($"回滚完成！已恢复到版本 {backup.Version}");
            ConsoleUI.Info($"  还原了 {restored} 个文件");

            return true;
        }
        catch (Exception ex)
        {
            ConsoleUI.Error($"回滚失败: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Clean up old backups, keeping only the most recent N.
    /// </summary>
    public void CleanupOldBackups(int keepCount = 3)
    {
        var backups = _state.State.Backups.OrderByDescending(b => b.CreatedAt).ToList();

        if (backups.Count <= keepCount) return;

        var toRemove = backups.Skip(keepCount).ToList();
        foreach (var backup in toRemove)
        {
            try
            {
                if (Directory.Exists(backup.BackupPath))
                    Directory.Delete(backup.BackupPath, true);
            }
            catch { /* best effort */ }
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  INTERNAL
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Quick backup: copy all engine files (except .backup/ .update-staging/ temp files).
    /// </summary>
    private async Task QuickBackupAsync(string backupDir)
    {
        Directory.CreateDirectory(backupDir);

        var excludeDirs = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            ".backup", ".update-staging", ".git", "JundotSharp", "Tools"
        };

        var excludeExts = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            ".download", ".log", ".tmp", ".json" // Don't backup update state in quick mode
        };

        var files = Directory.GetFiles(_engineDir, "*", SearchOption.AllDirectories)
            .Where(f =>
            {
                var dir = Path.GetDirectoryName(f) ?? "";
                var relDir = Path.GetRelativePath(_engineDir, dir);
                var topDir = relDir.Split(Path.DirectorySeparatorChar)[0];

                if (excludeDirs.Contains(topDir)) return false;

                var ext = Path.GetExtension(f);
                if (excludeExts.Contains(ext)) return false;

                return true;
            });

        foreach (var file in files)
        {
            var relativePath = Path.GetRelativePath(_engineDir, file);
            var dest = Path.Combine(backupDir, relativePath);
            var destDir = Path.GetDirectoryName(dest);
            if (destDir != null)
                Directory.CreateDirectory(destDir);

            await Task.Run(() => File.Copy(file, dest, overwrite: true));
        }
    }

    /// <summary>
    /// Restore all files from a backup directory into the engine directory.
    /// </summary>
    private async Task<int> RestoreFromBackupAsync(string backupDir)
    {
        var files = Directory.GetFiles(backupDir, "*", SearchOption.AllDirectories);
        var backupRoot = Path.GetFullPath(backupDir).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        int count = 0;

        foreach (var file in files)
        {
            var relativePath = file.StartsWith(backupRoot, StringComparison.OrdinalIgnoreCase)
                ? file[backupRoot.Length..]
                : Path.GetFileName(file);

            var dest = Path.Combine(_engineDir, relativePath);
            var destDir = Path.GetDirectoryName(dest);
            if (destDir != null)
                Directory.CreateDirectory(destDir);

            await Task.Run(() => File.Copy(file, dest, overwrite: true));
            count++;
        }

        return count;
    }
}
