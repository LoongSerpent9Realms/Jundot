using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.EntityFrameworkCore;
using JundotSite.Data;
using JundotSite.Models;
using JundotSite.Services;

namespace JundotSite.Pages.Admin;

[Microsoft.AspNetCore.Mvc.RequestFormLimits(ValueLengthLimit = 1073741824, MultipartBodyLengthLimit = 1073741824)]
public class VideosModel : AdminPageModel
{
    private readonly ApplicationDbContext _context;
    private readonly IWebHostEnvironment _environment;

    public VideosModel(ApplicationDbContext context, IWebHostEnvironment environment)
    {
        _context = context;
        _environment = environment;
    }

    public List<DocVideo> Videos { get; set; } = new();
    public List<DocumentationPage> Documents { get; set; } = new();
    public List<string> Categories { get; set; } = new();
    public string? SuccessMessage { get; set; }
    public string? ErrorMessage { get; set; }

    [BindProperty]
    public VideoInput Input { get; set; } = new();

    public class VideoInput
    {
        public int? Id { get; set; }

        [Required]
        [StringLength(200)]
        public string Title { get; set; } = string.Empty;

        [StringLength(500)]
        public string? Description { get; set; }

        public int SourceType { get; set; } = 0;

        [Required]
        [StringLength(1000)]
        public string VideoUrl { get; set; } = string.Empty;

        [StringLength(1000)]
        public string? ThumbnailUrl { get; set; }

        [StringLength(100)]
        public string? DocId { get; set; }

        [StringLength(100)]
        public string? Category { get; set; }

        public int SortOrder { get; set; } = 0;

        public bool IsPublished { get; set; } = true;
    }

    public async Task<IActionResult> OnGetAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "视频管理";
        await LoadDataAsync();
        return Page();
    }

    public async Task<IActionResult> OnPostSaveAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "视频管理";

        if (string.IsNullOrWhiteSpace(Input.Title))
        {
            ErrorMessage = "请填写视频标题。";
            await LoadDataAsync();
            return Page();
        }

        if (string.IsNullOrWhiteSpace(Input.VideoUrl))
        {
            ErrorMessage = "请填写视频地址。";
            await LoadDataAsync();
            return Page();
        }

        DocVideo? video = null;
        if (Input.Id.HasValue && Input.Id.Value > 0)
        {
            video = await _context.DocVideos.FindAsync(Input.Id.Value);
            if (video == null)
            {
                ErrorMessage = "视频不存在。";
                await LoadDataAsync();
                return Page();
            }
        }
        else
        {
            video = new DocVideo();
            _context.DocVideos.Add(video);
        }

        video.Title = Input.Title.Trim();
        video.Description = Input.Description?.Trim();
        video.SourceType = Input.SourceType;
        video.VideoUrl = Input.VideoUrl.Trim();
        video.ThumbnailUrl = Input.ThumbnailUrl?.Trim();
        video.DocId = string.IsNullOrWhiteSpace(Input.DocId) ? null : Input.DocId.Trim();
        video.Category = string.IsNullOrWhiteSpace(Input.Category) ? null : Input.Category.Trim();
        video.SortOrder = Input.SortOrder;
        video.IsPublished = Input.IsPublished;
        video.UpdatedAt = DateTime.Now;

        await _context.SaveChangesAsync();

        SuccessMessage = Input.Id.HasValue && Input.Id.Value > 0
            ? $"视频《{video.Title}》已更新。"
            : $"视频《{video.Title}》已添加。";

        await LoadDataAsync();
        return Page();
    }

    public async Task<IActionResult> OnPostDeleteAsync(int id)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        ViewData["Title"] = "视频管理";

        var video = await _context.DocVideos.FindAsync(id);
        if (video == null)
        {
            ErrorMessage = "视频不存在。";
        }
        else
        {
            // If local file, try to delete it
            if (video.SourceType == 1 && !string.IsNullOrEmpty(video.VideoUrl))
            {
                var filePath = Path.Combine(_environment.WebRootPath, "videos", Path.GetFileName(video.VideoUrl));
                if (System.IO.File.Exists(filePath))
                {
                    try { System.IO.File.Delete(filePath); } catch { }
                }
            }

            _context.DocVideos.Remove(video);
            await _context.SaveChangesAsync();
            SuccessMessage = $"视频《{video.Title}》已删除。";
        }

        await LoadDataAsync();
        return Page();
    }

    public async Task<IActionResult> OnPostReorderAsync(List<int> videoIds)
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        if (videoIds == null || videoIds.Count == 0)
        {
            return new JsonResult(new { success = false, message = "没有排序数据。" });
        }

        for (int i = 0; i < videoIds.Count; i++)
        {
            var video = await _context.DocVideos.FindAsync(videoIds[i]);
            if (video != null)
            {
                video.SortOrder = i;
                video.UpdatedAt = DateTime.Now;
            }
        }

        await _context.SaveChangesAsync();
        return new JsonResult(new { success = true });
    }

    public async Task<IActionResult> OnPostUploadAsync()
    {
        var result = RequireLogin();
        if (result is not PageResult) return result;

        var files = Request.Form.Files;
        if (files.Count == 0)
        {
            return new JsonResult(new { success = false, message = "没有选择文件。" });
        }

        var file = files[0];
        var allowedExtensions = new[] { ".mp4", ".webm", ".ogg", ".mov", ".avi", ".mkv", ".wmv", ".flv" };

        var ext = Path.GetExtension(file.FileName).ToLowerInvariant();
        if (!allowedExtensions.Contains(ext))
        {
            return new JsonResult(new { success = false, message = "不支持的视频格式。支持：MP4, WebM, OGG, MOV, AVI, MKV, WMV, FLV" });
        }

        // Max 1GB
        if (file.Length > 1024L * 1024 * 1024)
        {
            return new JsonResult(new { success = false, message = "文件大小不能超过 1GB。" });
        }

        var videosDir = Path.Combine(_environment.WebRootPath, "videos");
        Directory.CreateDirectory(videosDir);

        var originalFileName = $"{Guid.NewGuid()}{ext}";
        var originalFilePath = Path.Combine(videosDir, originalFileName);

        // Save original file first
        using (var stream = new FileStream(originalFilePath, FileMode.Create))
        {
            await file.CopyToAsync(stream);
        }

        // Check if already compressed client-side
        var clientCompressed = Request.Form["clientCompressed"].ToString();
        var alreadyCompressed = clientCompressed.Equals("true", StringComparison.OrdinalIgnoreCase);

        // Try to compress to WebM (VP9) using ffmpeg (skip if already compressed client-side)
        var compressedFileName = $"{Guid.NewGuid()}.webm";
        var compressedFilePath = Path.Combine(videosDir, compressedFileName);
        var compressed = alreadyCompressed ? false : await TryCompressVideoAsync(originalFilePath, compressedFilePath);

        string finalUrl;
        string finalFileName;
        if (compressed)
        {
            // Compression succeeded: remove original, use compressed
            try { System.IO.File.Delete(originalFilePath); } catch { }
            finalUrl = $"/videos/{compressedFileName}";
            finalFileName = compressedFileName;
        }
        else
        {
            // Compression failed or ffmpeg not available: keep original
            try { System.IO.File.Delete(compressedFilePath); } catch { }
            finalUrl = $"/videos/{originalFileName}";
            finalFileName = originalFileName;
        }

        var fileInfo = new System.IO.FileInfo(compressed ? compressedFilePath : originalFilePath);
        return new JsonResult(new
        {
            success = true,
            url = finalUrl,
            fileName = finalFileName,
            fileSize = fileInfo.Length,
            compressed = compressed
        });
    }

    private static async Task<bool> TryCompressVideoAsync(string inputPath, string outputPath)
    {
        try
        {
            // Check if ffmpeg is available (with timeout)
            try
            {
                using var checkProcess = new Process
                {
                    StartInfo = new ProcessStartInfo
                    {
                        FileName = "ffmpeg",
                        Arguments = "-version",
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        UseShellExecute = false,
                        CreateNoWindow = true
                    }
                };
                checkProcess.Start();
                // Drain output to prevent deadlock
                _ = checkProcess.StandardOutput.ReadToEndAsync();
                _ = checkProcess.StandardError.ReadToEndAsync();
                
                var checkExited = await Task.WhenAny(
                    Task.Run(() => checkProcess.WaitForExit()),
                    Task.Delay(5000)
                );
                
                if (checkExited.IsCanceled || checkProcess.ExitCode != 0)
                {
                    return false;
                }
            }
            catch
            {
                // ffmpeg not installed or check failed
                return false;
            }

            // Compress to WebM (VP9) with CRF quality 32, cpu-used 4 for reasonable speed
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = "ffmpeg",
                    Arguments = $"-i \"{inputPath}\" -c:v libvpx-vp9 -crf 32 -b:v 0 -cpu-used 4 -c:a libopus -b:a 128k -y \"{outputPath}\"",
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true
                }
            };

            process.Start();
            // Drain both stdout and stderr asynchronously to prevent deadlock
            _ = process.StandardOutput.ReadToEndAsync();
            _ = process.StandardError.ReadToEndAsync();

            // Wait for exit with timeout (15 minutes)
            var exited = await Task.WhenAny(
                Task.Run(() => process.WaitForExit()),
                Task.Delay(TimeSpan.FromMinutes(15))
            );

            if (exited.IsCanceled)
            {
                // Timeout - kill the process
                try { process.Kill(entireProcessTree: true); } catch { }
                return false;
            }

            // Verify output file exists and has content
            var outputInfo = new System.IO.FileInfo(outputPath);
            return outputInfo.Exists && outputInfo.Length > 0;
        }
        catch
        {
            return false;
        }
    }

    private async Task LoadDataAsync()
    {
        Videos = await _context.DocVideos.OrderBy(v => v.SortOrder).ThenByDescending(v => v.CreatedAt).ToListAsync();
        Documents = DocumentationCatalog.GetPages(_environment.ContentRootPath);
        Categories = await _context.DocVideos
            .Where(v => v.Category != null && v.Category != "")
            .Select(v => v.Category!)
            .Distinct()
            .ToListAsync();
    }
}
