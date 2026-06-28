using JundotSite.Data;
using JundotSite.Models;
using Microsoft.EntityFrameworkCore;

namespace JundotSite.Services;

public class DocSnapshotService
{
    private readonly ApplicationDbContext _context;
    private readonly ILogger<DocSnapshotService> _logger;
    private const int MaxSnapshotsPerDoc = 10;

    public DocSnapshotService(ApplicationDbContext context, ILogger<DocSnapshotService> logger)
    {
        _context = context;
        _logger = logger;
    }

    public async Task<(bool Success, string Message)> CreateSnapshotAsync(
        string docId, 
        string docTitle, 
        string language, 
        string content, 
        int userId, 
        string username,
        string? snapshotName = null)
    {
        var existingSnapshots = await _context.DocSnapshots
            .Where(s => s.DocId == docId && s.Language == language)
            .OrderByDescending(s => s.CreatedAt)
            .ToListAsync();

        // 检查是否已达到上限
        if (existingSnapshots.Count >= MaxSnapshotsPerDoc)
        {
            // 删除最旧的存档
            var oldest = existingSnapshots.Last();
            _context.DocSnapshots.Remove(oldest);
            _logger.LogInformation("文档 {DocId} 已达存档上限，删除旧存档 ID {SnapshotId}", docId, oldest.Id);
        }

        // 生成存档名称
        var name = snapshotName;
        if (string.IsNullOrEmpty(name))
        {
            var nextVersion = existingSnapshots.Count + 1;
            name = $"v{nextVersion}";
        }

        var snapshot = new DocSnapshot
        {
            DocId = docId,
            DocTitle = docTitle,
            Language = language,
            Content = content,
            SnapshotName = name,
            CreatedAt = DateTime.Now,
            CreatedByUserId = userId,
            CreatedByUsername = username
        };

        _context.DocSnapshots.Add(snapshot);
        await _context.SaveChangesAsync();

        _logger.LogInformation("用户 {Username} 为文档 {DocId} 创建存档 {SnapshotName}", username, docId, name);
        return (true, $"存档「{name}」已创建");
    }

    public async Task<List<DocSnapshot>> GetSnapshotsAsync(string docId, string language = "zh-CN")
    {
        return await _context.DocSnapshots
            .Where(s => s.DocId == docId && s.Language == language)
            .OrderByDescending(s => s.CreatedAt)
            .ToListAsync();
    }

    public async Task<DocSnapshot?> GetSnapshotByIdAsync(int snapshotId)
    {
        return await _context.DocSnapshots.FindAsync(snapshotId);
    }

    public async Task<(bool Success, string Message)> RestoreSnapshotAsync(int snapshotId, int userId, string username)
    {
        var snapshot = await _context.DocSnapshots.FindAsync(snapshotId);
        if (snapshot == null)
        {
            return (false, "存档不存在");
        }

        // 读取当前文档内容，创建备份存档
        var docsRoot = Path.Combine(AppContext.BaseDirectory, "Pages", "Docs");
        var docPath = snapshot.Language == "zh-CN"
            ? Path.Combine(docsRoot, $"{snapshot.DocId}.cshtml")
            : Path.Combine(docsRoot, snapshot.Language, $"{snapshot.DocId}.cshtml");

        string currentContent = "";
        if (File.Exists(docPath))
        {
            currentContent = await File.ReadAllTextAsync(docPath);
            
            // 创建恢复前的备份
            var backupSnapshot = new DocSnapshot
            {
                DocId = snapshot.DocId,
                DocTitle = snapshot.DocTitle,
                Language = snapshot.Language,
                Content = currentContent,
                SnapshotName = $"auto_backup_{DateTime.Now:HHmmss}",
                CreatedAt = DateTime.Now,
                CreatedByUserId = userId,
                CreatedByUsername = username
            };
            
            // 先检查备份后是否超限
            var existingCount = await _context.DocSnapshots
                .Where(s => s.DocId == snapshot.DocId && s.Language == snapshot.Language)
                .CountAsync();

            if (existingCount < MaxSnapshotsPerDoc)
            {
                _context.DocSnapshots.Add(backupSnapshot);
            }
        }

        // 恢复存档内容
        Directory.CreateDirectory(Path.GetDirectoryName(docPath)!);
        await File.WriteAllTextAsync(docPath, snapshot.Content);

        _logger.LogInformation("用户 {Username} 恢复文档 {DocId} 至存档 {SnapshotName}", username, snapshot.DocId, snapshot.SnapshotName);
        return (true, $"已恢复到存档「{snapshot.SnapshotName}」");
    }

    public async Task<(bool Success, string Message)> DeleteSnapshotAsync(int snapshotId)
    {
        var snapshot = await _context.DocSnapshots.FindAsync(snapshotId);
        if (snapshot == null)
        {
            return (false, "存档不存在");
        }

        _context.DocSnapshots.Remove(snapshot);
        await _context.SaveChangesAsync();

        _logger.LogInformation("删除存档 ID {SnapshotId} ({DocId})", snapshotId, snapshot.DocId);
        return (true, "存档已删除");
    }

    public async Task<int> GetSnapshotCountAsync(string docId, string language = "zh-CN")
    {
        return await _context.DocSnapshots
            .Where(s => s.DocId == docId && s.Language == language)
            .CountAsync();
    }
}
