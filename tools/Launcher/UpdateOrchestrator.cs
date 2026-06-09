namespace JundotLauncher;

/// <summary>
/// Orchestrates the complete update flow as a state machine.
/// Ties together ManifestFetcher, VersionComparer, GrayscaleEvaluator,
/// PackageDownloader, PackageVerifier, PackageInstaller, and RollbackManager.
/// 
/// States: IDLE → CHECKING → DOWNLOADING → VERIFYING → INSTALLING → COMPLETE
/// </summary>
public class UpdateOrchestrator
{
    public enum FlowState { Idle, Checking, Downloading, Verifying, Installing, Complete }

    private readonly string _engineDir;
    private readonly UpdateStateStore _state;
    private readonly ManifestFetcher _fetcher;
    private readonly PackageDownloader _downloader;
    private readonly PackageInstaller _installer;
    private readonly RollbackManager _rollback;
    private readonly GrayscaleEvaluator _grayscale;

    private FlowState _currentState = FlowState.Idle;
    public FlowState CurrentState => _currentState;

    // ─────────────────────────────────────────────────────────

    public UpdateOrchestrator(string engineDir, UpdateStateStore state)
    {
        _engineDir = engineDir;
        _state = state;

        _fetcher = new ManifestFetcher();
        _downloader = new PackageDownloader();
        _installer = new PackageInstaller(engineDir);
        _rollback = new RollbackManager(engineDir, state);
        _grayscale = new GrayscaleEvaluator(state.MachineId);
    }

    // ═══════════════════════════════════════════════════════════
    //  CHECK ONLY — 仅检查更新（不下载）
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Check for updates without downloading or installing.
    /// Returns the manifest if an update is available, null otherwise.
    /// </summary>
    public async Task<UpdateManifestV1?> CheckOnlyAsync(string? manifestUrl = null)
    {
        _currentState = FlowState.Checking;

        ConsoleUI.Step("检查更新...");

        // ── 1. Fetch manifest ──────────────────────────────
        var manifest = await FetchManifestAsync(manifestUrl);
        if (manifest == null)
        {
            ConsoleUI.Warning("无法获取更新清单，可能是网络问题。");
            _currentState = FlowState.Idle;
            return null;
        }

        // ── 2. Version comparison ──────────────────────────
        var currentVer = _state.CurrentVersion;
        var targetVer = manifest.Version?.Full ?? "0.0.0";

        if (!VersionComparer.IsUpdateAvailable(currentVer, targetVer, manifest.Channel, _state.UpdateChannel))
        {
            ConsoleUI.Success($"当前已是最新版本 ({currentVer})");
            _state.LastCheckTime = DateTime.Now;
            _currentState = FlowState.Complete;
            return null;
        }

        // ── 3. Check if user skipped this version ──────────
        if (_state.SkippedVersion == targetVer)
        {
            ConsoleUI.Info($"版本 {targetVer} 已被用户跳过。");
            _currentState = FlowState.Complete;
            return null;
        }

        // ── 4. Grayscale check ─────────────────────────────
        var grayscaleResult = _grayscale.Evaluate(manifest.Grayscale);
        ConsoleUI.PrintGrayscale(grayscaleResult.IsEligible, grayscaleResult.Reason);

        if (!grayscaleResult.IsEligible)
        {
            _currentState = FlowState.Complete;
            return null;
        }

        // ── 5. Check min version requirement ───────────────
        if (!VersionComparer.MeetsMinVersion(currentVer, manifest.MinVersion))
        {
            ConsoleUI.Warning($"需要最低版本 {manifest.MinVersion}，当前为 {currentVer}。请先更新到中间版本。");
            _currentState = FlowState.Complete;
            return null;
        }

        // ── 6. Show available update ─────────────────────
        ConsoleUI.PrintUpdateAvailable(targetVer, manifest.PackageSize, manifest.Channel);
        if (!string.IsNullOrEmpty(manifest.Changelog))
        {
            ConsoleUI.Info("变更日志:");
            foreach (var line in manifest.Changelog!.Split('\n'))
                ConsoleUI.Info($"  {line.Trim()}");
        }

        _state.LastCheckTime = DateTime.Now;
        _currentState = FlowState.Complete;
        return manifest;
    }

    // ═══════════════════════════════════════════════════════════
    //  FULL UPDATE — 完整更新流程
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Execute the full update flow: check → download → verify → install.
    /// </summary>
    /// <param name="manifestUrl">Optional override manifest URL.</param>
    /// <param name="ct">Cancellation token for user interruption.</param>
    /// <returns>True if update completed successfully.</returns>
    public async Task<bool> UpdateAsync(string? manifestUrl = null, CancellationToken ct = default)
    {
        // ── Phase 1: Check ─────────────────────────────────
        var manifest = await CheckOnlyAsync(manifestUrl);
        if (manifest == null)
        {
            return false; // No update or check failed
        }

        var targetVer = manifest.Version?.Full ?? "0.0.0";

        // Skip if mandatory? No, if we reach here it's available.
        var actionText = manifest.Mandatory ? "（强制更新）" : "";

        if (!ConsoleUI.AskYesNo($"是否更新到 {targetVer} {actionText}?", defaultYes: true))
        {
            ConsoleUI.Info("更新已取消。");
            if (!manifest.Mandatory)
            {
                // Allow user to skip non-mandatory updates
                if (ConsoleUI.AskYesNo("是否跳过此版本（不再提醒）？", defaultYes: false))
                {
                    _state.SkippedVersion = targetVer;
                }
            }
            return false;
        }

        // ── Phase 2: Download ──────────────────────────────
        _currentState = FlowState.Downloading;
        ConsoleUI.Header($"下载 Jundot {targetVer}");

        var downloadPath = Path.Combine(_engineDir, ".update-staging-download", $"{manifest.PackageName}.zip");
        var downloadDir = Path.GetDirectoryName(downloadPath)!;
        Directory.CreateDirectory(downloadDir);

        var progress = new Progress<DownloadProgress>(p =>
        {
            var eta = p.EstimatedRemaining.HasValue
                ? ConsoleUI.FormatTimeSpan(p.EstimatedRemaining.Value)
                : "—";
            ConsoleUI.ShowProgress("下载",
                p.Percent,
                $"{ConsoleUI.FormatBytes(p.BytesDownloaded)}/{ConsoleUI.FormatBytes(p.TotalBytes)} " +
                $"{ConsoleUI.FormatBytes((long)p.SpeedBytesPerSec)}/s 剩余 {eta}");
        });

        var downloaded = await _downloader.DownloadAsync(
            manifest.DownloadUrl, downloadPath, manifest.PackageSize, progress, ct);

        ConsoleUI.CompleteProgress();

        if (downloaded == null)
        {
            ConsoleUI.Error("下载失败。");
            _currentState = FlowState.Idle;
            return false;
        }

        // Save download progress to state
        _state.SetDownloadProgress(manifest.DownloadUrl, new FileInfo(downloadPath).Length);

        // ── Phase 3: Verify ────────────────────────────────
        _currentState = FlowState.Verifying;
        ConsoleUI.Step("校验完整性...");

        var verifyProgress = new Progress<double>(p =>
            ConsoleUI.ShowProgress("校验", p));
        var verified = await PackageVerifier.VerifySha256Async(downloadPath, manifest.Sha256, verifyProgress);
        ConsoleUI.CompleteProgress();

        if (!verified)
        {
            ConsoleUI.Error("校验失败，下载文件已损坏。正在删除...");
            try { File.Delete(downloadPath); } catch { }
            _state.ClearDownloadProgress();
            _currentState = FlowState.Idle;
            return false;
        }

        // ── Phase 4: Install ───────────────────────────────
        _currentState = FlowState.Installing;
        var installed = await _installer.InstallAsync(downloadPath, targetVer);

        if (!installed)
        {
            ConsoleUI.Error("安装失败。");
            _currentState = FlowState.Idle;
            return false;
        }

        // ── Phase 5: Finalize ──────────────────────────────
        // Clean up download
        try { File.Delete(downloadPath); } catch { }
        _state.ClearDownloadProgress();

        // Record backup
        var backupDir = Path.Combine(_engineDir, ".backup", _state.CurrentVersion);
        if (Directory.Exists(backupDir))
        {
            _state.RecordBackup(_state.CurrentVersion, backupDir);
        }

        // Update current version
        _state.CurrentVersion = targetVer;
        _state.SkippedVersion = null;

        _currentState = FlowState.Complete;
        ConsoleUI.Header($"更新完成！Jundot {targetVer} 已安装。");

        return true;
    }

    // ═══════════════════════════════════════════════════════════
    //  ROLLBACK
    // ═══════════════════════════════════════════════════════════

    /// <summary>Execute rollback to a previous version.</summary>
    public async Task<bool> RollbackAsync(string? targetVersion = null)
    {
        return await _rollback.RollbackAsync(targetVersion);
    }

    // ═══════════════════════════════════════════════════════════
    //  START — 启动前检查 + 启动引擎
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Full start flow: check for updates → offer to update → launch engine.
    /// This is the default "start" command behavior.
    /// </summary>
    public async Task<int> StartAsync(string? manifestUrl = null, CancellationToken ct = default)
    {
        // ── 1. Check for updates ────────────────────────────
        var manifest = await CheckOnlyAsync(manifestUrl);

        if (manifest != null)
        {
            var targetVer = manifest.Version?.Full ?? "0.0.0";

            if (manifest.Mandatory)
            {
                ConsoleUI.Warning("此更新为强制更新，必须安装后才能启动。");
                var updated = await UpdateAsync(manifestUrl, ct);
                if (!updated)
                {
                    ConsoleUI.Error("强制更新失败，无法启动。");
                    return 1;
                }
            }
            else if (ConsoleUI.AskYesNo($"是否立即更新到 {targetVer}？", defaultYes: true))
            {
                await UpdateAsync(manifestUrl, ct);
            }
        }

        // ── 2. Execute pending file replacements ────────────
        var pendingScript = Path.Combine(_engineDir, ".pending-replace.bat");
        if (File.Exists(pendingScript))
        {
            ConsoleUI.Info("执行待处理的文件替换...");
            try
            {
                var psi = new System.Diagnostics.ProcessStartInfo("cmd", $"/c \"{pendingScript}\"")
                {
                    WorkingDirectory = _engineDir,
                    UseShellExecute = true,
                    CreateNoWindow = true
                };
                var p = System.Diagnostics.Process.Start(psi);
                if (p != null)
                {
                    await p.WaitForExitAsync(ct);
                }
            }
            catch (Exception ex)
            {
                ConsoleUI.Warning($"待处理替换执行失败: {ex.Message}");
            }
        }

        // ── 3. Launch engine ────────────────────────────────
        return await Program.LaunchEngineStatic(_engineDir);
    }

    // ═══════════════════════════════════════════════════════════
    //  INTERNAL
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Fetch manifest from default URL or a user-provided override.
    /// Default: GitHub Releases latest download URL for Jundot repo.
    /// </summary>
    private async Task<UpdateManifestV1?> FetchManifestAsync(string? manifestUrl)
    {
        if (!string.IsNullOrEmpty(manifestUrl))
        {
            return await _fetcher.FetchAsync(manifestUrl);
        }

        // Default: try the local staging dir first (for testing),
        // then the GitHub Releases URL pattern
        var candidates = new[]
        {
            // For testing: look for manifest in artifacts/packages/
            Path.Combine(_engineDir, "update-manifest.json"),
        };

        // Try local file first (for dev/testing)
        foreach (var localPath in candidates)
        {
            if (File.Exists(localPath))
            {
                try
                {
                    var json = await File.ReadAllTextAsync(localPath);
                    var manifest = System.Text.Json.JsonSerializer.Deserialize<UpdateManifestV1>(json);
                    if (manifest != null && !string.IsNullOrEmpty(manifest.ManifestVersion))
                    {
                        ConsoleUI.Info($"从本地文件加载 manifest: {localPath}");
                        return manifest;
                    }
                }
                catch { /* fall through */ }
            }
        }

        // Default remote URL: GitHub Releases latest download
        const string owner = "LoongSerpent9Realms";
        const string repo = "Jundot";
        var remoteUrl = $"https://github.com/{owner}/{repo}/releases/latest/download/update-manifest.json";

        try
        {
            ConsoleUI.Info($"尝试从远程获取 manifest: {remoteUrl}");
            var remoteManifest = await _fetcher.FetchAsync(remoteUrl);
            if (remoteManifest != null)
            {
                return remoteManifest;
            }
        }
        catch
        {
            // Fall through to warning
        }

        ConsoleUI.Warning("未配置远程 manifest URL，且无法从默认 URL 获取。");
        ConsoleUI.Info("使用 --manifest-url 指定 manifest URL 重试。");
        return null;
    }
}
