using System.Net;
using System.Net.Http.Headers;

namespace JundotLauncher;

/// <summary>
/// Downloads a package ZIP with HTTP Range-based resume support.
/// 
/// Resume logic:
///   1. Check if a .download temp file already exists
///   2. If yes → send HEAD to get total size, then GET with Range: bytes=X-
///   3. If no → start fresh GET
/// 
/// Progress is reported via <see cref="IProgress{T}"/>.
/// </summary>
public class PackageDownloader
{
    // Default buffer: 64 KB
    private const int BufferSize = 65536;

    // Max retries
    private const int MaxRetries = 3;
    private static readonly TimeSpan RetryDelayBase = TimeSpan.FromSeconds(2);

    private readonly HttpClient _http;
    private readonly int _timeoutSeconds;

    public PackageDownloader(HttpClient? httpClient = null, int timeoutSeconds = 300)
    {
        _http = httpClient ?? new HttpClient(new HttpClientHandler
        {
            AllowAutoRedirect = true
        });
        _http.Timeout = TimeSpan.FromSeconds(timeoutSeconds);
        _http.DefaultRequestHeaders.UserAgent.ParseAdd(
            "JundotLauncher/1.0 (Windows)");
        _timeoutSeconds = timeoutSeconds;
    }

    // ═══════════════════════════════════════════════════════════
    //  PUBLIC API
    // ═══════════════════════════════════════════════════════════

    /// <summary>
    /// Download a file with resume support.
    /// </summary>
    /// <param name="url">Download URL.</param>
    /// <param name="outputPath">Final output file path (e.g., "update.zip").</param>
    /// <param name="expectedSize">Expected total size from manifest (for validation).</param>
    /// <param name="progress">Progress reporter — receives (percent, bytesDownloaded, totalBytes).</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>The path to the downloaded file, or null on failure.</returns>
    public async Task<string?> DownloadAsync(
        string url,
        string outputPath,
        long expectedSize,
        IProgress<DownloadProgress>? progress,
        CancellationToken ct)
    {
        var tempPath = outputPath + ".download";
        long existingBytes = 0;
        long totalBytes = expectedSize;

        // ── Check for existing partial download ───────────────
        if (File.Exists(tempPath))
        {
            existingBytes = new FileInfo(tempPath).Length;

            // If already complete and matches expected size, skip download
            if (expectedSize > 0 && existingBytes >= expectedSize)
            {
                ConsoleUI.Success("文件已完整，跳过下载。");
                File.Move(tempPath, outputPath, overwrite: true);
                return outputPath;
            }

            // If temp file is larger than expected, it's corrupt — restart
            if (expectedSize > 0 && existingBytes > expectedSize)
            {
                ConsoleUI.Warning("临时文件大小异常，重新下载。");
                File.Delete(tempPath);
                existingBytes = 0;
            }
        }

        int retry = 0;
        while (retry <= MaxRetries)
        {
            try
            {
                using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
                cts.CancelAfter(TimeSpan.FromSeconds(_timeoutSeconds));

                var request = new HttpRequestMessage(HttpMethod.Get, url);

                // ── Range request for resume ──────────────────
                if (existingBytes > 0)
                {
                    request.Headers.Range = new RangeHeaderValue(existingBytes, null);
                    ConsoleUI.Info($"从 {existingBytes:N0} 字节处继续下载（断点续传）");
                }

                // Get server's total size via HEAD first if needed
                if (totalBytes <= 0 && existingBytes > 0)
                {
                    totalBytes = await GetContentLengthAsync(url, cts.Token);
                }

                var response = await _http.SendAsync(request,
                    HttpCompletionOption.ResponseHeadersRead, cts.Token);

                // Handle resume response
                if (existingBytes > 0)
                {
                    if (response.StatusCode == HttpStatusCode.PartialContent)
                    {
                        // Server supports Range — continue
                        var contentRange = response.Content.Headers.ContentRange;
                        if (contentRange?.Length != null)
                            totalBytes = contentRange.Length.Value;
                    }
                    else if (response.StatusCode == HttpStatusCode.OK)
                    {
                        // Server doesn't support Range — restart
                        ConsoleUI.Warning("服务器不支持断点续传，从头下载。");
                        File.Delete(tempPath);
                        existingBytes = 0;
                    }
                    else if (response.StatusCode == HttpStatusCode.RequestedRangeNotSatisfiable)
                    {
                        // File already fully downloaded
                        ConsoleUI.Success("文件已完整下载。");
                        File.Move(tempPath, outputPath, overwrite: true);
                        return outputPath;
                    }
                }

                response.EnsureSuccessStatusCode();

                // Get total size if not yet known
                if (totalBytes <= 0)
                {
                    totalBytes = response.Content.Headers.ContentLength ?? 0;
                }

                // ── Download stream ───────────────────────────
                using var responseStream = await response.Content.ReadAsStreamAsync(cts.Token);
                using var fileStream = new FileStream(tempPath,
                    existingBytes > 0 ? FileMode.Append : FileMode.Create,
                    FileAccess.Write, FileShare.None, BufferSize, useAsync: true);

                var buffer = new byte[BufferSize];
                var totalDownloaded = existingBytes;
                var lastReportTime = DateTime.MinValue;
                var lastReportBytes = existingBytes;
                var startTime = DateTime.UtcNow;

                int bytesRead;
                while ((bytesRead = await responseStream.ReadAsync(buffer, 0, buffer.Length, cts.Token)) > 0)
                {
                    await fileStream.WriteAsync(buffer, 0, bytesRead, cts.Token);
                    totalDownloaded += bytesRead;

                    // Report progress at most 4 times per second
                    var now = DateTime.UtcNow;
                    if ((now - lastReportTime).TotalMilliseconds >= 250)
                    {
                        var elapsed = now - startTime;
                        var speed = elapsed.TotalSeconds > 0
                            ? (totalDownloaded - lastReportBytes) / (now - lastReportTime).TotalSeconds
                            : 0;

                        var percent = totalBytes > 0
                            ? (double)totalDownloaded / totalBytes * 100
                            : 0;

                        progress?.Report(new DownloadProgress
                        {
                            Percent = Math.Min(percent, 100),
                            BytesDownloaded = totalDownloaded,
                            TotalBytes = totalBytes,
                            SpeedBytesPerSec = speed,
                            Elapsed = elapsed
                        });

                        lastReportTime = now;
                        lastReportBytes = totalDownloaded;
                    }
                }

                await fileStream.FlushAsync(cts.Token);

                // ── Size validation ───────────────────────────
                if (expectedSize > 0 && totalDownloaded < expectedSize)
                {
                    // Incomplete download — will retry
                    throw new IOException(
                        $"下载不完整: 期望 {expectedSize:N0} 字节，实际 {totalDownloaded:N0} 字节。");
                }

                // ── Rename temp to final ──────────────────────
                File.Move(tempPath, outputPath, overwrite: true);

                var totalElapsed = DateTime.UtcNow - startTime;
                ConsoleUI.Success($"下载完成 ({ConsoleUI.FormatBytes(totalDownloaded)}, " +
                                 $"耗时 {ConsoleUI.FormatTimeSpan(totalElapsed)})");

                return outputPath;
            }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested)
            {
                // Timeout — retry
                retry++;
                if (retry > MaxRetries)
                {
                    ConsoleUI.Error($"下载超时，已达最大重试次数 {MaxRetries}。");
                    return null;
                }
                ConsoleUI.Warning($"下载超时（第 {retry}/{MaxRetries} 次重试）...");
                await Task.Delay(RetryDelayBase * retry, ct);
            }
            catch (HttpRequestException ex)
            {
                retry++;
                if (retry > MaxRetries)
                {
                    ConsoleUI.Error($"下载失败: {ex.Message}");
                    return null;
                }
                var delay = RetryDelayBase * Math.Pow(2, retry - 1);
                ConsoleUI.Warning($"网络错误（第 {retry}/{MaxRetries} 次重试，{delay.TotalSeconds:F0}s 后重试）...");
                await Task.Delay(delay, ct);
            }
            catch (IOException ex)
            {
                ConsoleUI.Error($"I/O 错误: {ex.Message}");
                // Keep temp file for resume
                return null;
            }
            catch (Exception ex)
            {
                ConsoleUI.Error($"下载异常: {ex.Message}");
                return null;
            }
        }

        return null;
    }

    // ── Helpers ──────────────────────────────────────────────

    /// <summary>Send HEAD request to get Content-Length.</summary>
    private async Task<long> GetContentLengthAsync(string url, CancellationToken ct)
    {
        try
        {
            var request = new HttpRequestMessage(HttpMethod.Head, url);
            var response = await _http.SendAsync(request, ct);
            return response.Content.Headers.ContentLength ?? 0;
        }
        catch
        {
            return 0;
        }
    }
}

/// <summary>Progress data reported during download.</summary>
public class DownloadProgress
{
    public double Percent { get; init; }
    public long BytesDownloaded { get; init; }
    public long TotalBytes { get; init; }
    public double SpeedBytesPerSec { get; init; }
    public TimeSpan Elapsed { get; init; }

    /// <summary>Estimated remaining time, or null if speed is 0.</summary>
    public TimeSpan? EstimatedRemaining =>
        SpeedBytesPerSec > 0 && TotalBytes > BytesDownloaded
            ? TimeSpan.FromSeconds((TotalBytes - BytesDownloaded) / SpeedBytesPerSec)
            : null;
}
