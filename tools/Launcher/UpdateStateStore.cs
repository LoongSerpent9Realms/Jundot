using System.Text.Json;
using System.Text.Json.Serialization;

namespace JundotLauncher;

/// <summary>
/// Local persistent state for the update system.
/// Stored as .jundot-update-state.json in the engine directory.
/// Tracks current version, download progress, backup paths, and update preferences.
/// </summary>
public class UpdateStateStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    private readonly string _stateFilePath;
    private UpdateState _state = new();

    // ── Computed Properties ──────────────────────────────────

    /// <summary>Engine root directory path.</summary>
    public string EngineDir { get; }

    /// <summary>Path for backups (e.g., EngineDir/.backup/1.7.2-beta/).</summary>
    public string BackupDir => Path.Combine(EngineDir, ".backup");

    /// <summary>Path for staging directory during update.</summary>
    public string StagingDir => Path.Combine(EngineDir, ".update-staging");

    /// <summary>Get the currently stored update state.</summary>
    public UpdateState State => _state;

    // ─────────────────────────────────────────────────────────

    public UpdateStateStore(string engineDir)
    {
        EngineDir = engineDir;
        _stateFilePath = Path.Combine(engineDir, ".jundot-update-state.json");
        Load();
    }

    // ── Load / Save ──────────────────────────────────────────

    /// <summary>Load state from disk, or create default if not found.</summary>
    public void Load()
    {
        try
        {
            if (File.Exists(_stateFilePath))
            {
                var json = File.ReadAllText(_stateFilePath);
                _state = JsonSerializer.Deserialize<UpdateState>(json) ?? new UpdateState();
            }
        }
        catch
        {
            _state = new UpdateState();
        }
    }

    /// <summary>Persist current state to disk.</summary>
    public void Save()
    {
        try
        {
            var dir = Path.GetDirectoryName(_stateFilePath);
            if (dir != null && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);

            var json = JsonSerializer.Serialize(_state, JsonOptions);
            File.WriteAllText(_stateFilePath, json);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[WARN] Failed to save update state: {ex.Message}");
        }
    }

    // ── Convenience Accessors ────────────────────────────────

    /// <summary>Current installed version string.</summary>
    public string CurrentVersion
    {
        get => _state.CurrentVersion;
        set { _state.CurrentVersion = value; Save(); }
    }

    /// <summary>Last time we checked for updates.</summary>
    public DateTime LastCheckTime
    {
        get => _state.LastCheckTime;
        set { _state.LastCheckTime = value; Save(); }
    }

    /// <summary>Preferred update channel.</summary>
    public string UpdateChannel
    {
        get => _state.UpdateChannel;
        set { _state.UpdateChannel = value; Save(); }
    }

    /// <summary>Machine identifier for grayscale evaluation.</summary>
    public string MachineId
    {
        get
        {
            if (string.IsNullOrEmpty(_state.MachineId))
            {
                _state.MachineId = GenerateMachineId();
                Save();
            }
            return _state.MachineId;
        }
    }

    /// <summary>Whether to skip a specific version.</summary>
    public string? SkippedVersion
    {
        get => _state.SkippedVersion;
        set { _state.SkippedVersion = value; Save(); }
    }

    // ── Download Progress ────────────────────────────────────

    /// <summary>Record download progress for resumption.</summary>
    public void SetDownloadProgress(string url, long bytesDownloaded)
    {
        _state.DownloadUrl = url;
        _state.BytesDownloaded = bytesDownloaded;
        Save();
    }

    /// <summary>Clear download progress (download complete).</summary>
    public void ClearDownloadProgress()
    {
        _state.DownloadUrl = null;
        _state.BytesDownloaded = 0;
        Save();
    }

    /// <summary>Get download progress for given URL, or 0.</summary>
    public long GetDownloadProgress(string url)
    {
        return _state.DownloadUrl == url ? _state.BytesDownloaded : 0;
    }

    // ── Backup Management ────────────────────────────────────

    /// <summary>Record a backup after successful update.</summary>
    public void RecordBackup(string version, string backupPath)
    {
        var entry = new BackupEntry
        {
            Version = version,
            BackupPath = backupPath,
            CreatedAt = DateTime.Now
        };

        // Remove duplicate entries for same version
        _state.Backups.RemoveAll(b => b.Version == version);
        _state.Backups.Insert(0, entry);

        // Keep only most recent backups
        const int maxBackups = 5;
        while (_state.Backups.Count > maxBackups)
        {
            var stale = _state.Backups[^1];
            _state.Backups.RemoveAt(_state.Backups.Count - 1);

            // Clean up stale backup directory
            try { if (Directory.Exists(stale.BackupPath)) Directory.Delete(stale.BackupPath, true); }
            catch { /* best effort */ }
        }

        Save();
    }

    /// <summary>Get the most recent backup entry.</summary>
    public BackupEntry? GetLatestBackup()
    {
        return _state.Backups.FirstOrDefault();
    }

    // ── Machine ID Generation ─────────────────────────────

    /// <summary>
    /// Generate a stable machine identifier based on machine name +
    /// Windows SID (or fallback hash). Non-admin users can't forge
    /// the SID, making it reliable for grayscale evaluation.
    /// </summary>
    private static string GenerateMachineId()
    {
        try
        {
            var machineName = Environment.MachineName;
            var sid = GetCurrentUserSid();

            // Combine and hash
            var combined = $"{machineName}|{sid}|jundot-launcher-v1";
            var bytes = System.Security.Cryptography.SHA256.HashData(
                System.Text.Encoding.UTF8.GetBytes(combined));

            return Convert.ToHexString(bytes)[..16].ToLowerInvariant();
        }
        catch
        {
            return Guid.NewGuid().ToString("N")[..16];
        }
    }

    /// <summary>Get the current Windows user's SID string.</summary>
    private static string GetCurrentUserSid()
    {
        try
        {
            // Use whoami /user to get SID (works without admin)
            var psi = new System.Diagnostics.ProcessStartInfo("whoami", "/user")
            {
                RedirectStandardOutput = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            using var p = System.Diagnostics.Process.Start(psi);
            if (p == null) return Environment.UserName;

            var output = p.StandardOutput.ReadToEnd();
            p.WaitForExit(5000);

            // Parse output like "S-1-5-21-xxx-xxx-xxx-1001"
            foreach (var line in output.Split('\n'))
            {
                var trimmed = line.Trim();
                if (trimmed.StartsWith("S-1-"))
                    return trimmed;
            }

            return Environment.UserName;
        }
        catch
        {
            return Environment.UserName;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  DATA MODELS
// ═══════════════════════════════════════════════════════════════

/// <summary>Persistent state stored in .jundot-update-state.json.</summary>
public class UpdateState
{
    [JsonPropertyName("current_version")]
    public string CurrentVersion { get; set; } = "0.0.0";

    [JsonPropertyName("last_check_time")]
    public DateTime LastCheckTime { get; set; } = DateTime.MinValue;

    [JsonPropertyName("update_channel")]
    public string UpdateChannel { get; set; } = "stable";

    [JsonPropertyName("machine_id")]
    public string MachineId { get; set; } = "";

    [JsonPropertyName("skipped_version")]
    public string? SkippedVersion { get; set; }

    [JsonPropertyName("download_url")]
    public string? DownloadUrl { get; set; }

    [JsonPropertyName("bytes_downloaded")]
    public long BytesDownloaded { get; set; }

    [JsonPropertyName("backups")]
    public List<BackupEntry> Backups { get; set; } = new();
}

/// <summary>Record of a single backup created during an update.</summary>
public class BackupEntry
{
    [JsonPropertyName("version")]
    public string Version { get; set; } = "";

    [JsonPropertyName("backup_path")]
    public string BackupPath { get; set; } = "";

    [JsonPropertyName("created_at")]
    public DateTime CreatedAt { get; set; }
}
