using JundotSite.Data;
using JundotSite.Models;
using JundotSite.Services;
using Microsoft.EntityFrameworkCore;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddRazorPages();
builder.Services.AddDbContext<ApplicationDbContext>(options =>
    options.UseSqlite(builder.Configuration.GetConnectionString("DefaultConnection")));

builder.Services.AddSession(options =>
{
    options.IdleTimeout = TimeSpan.FromHours(2);
    options.Cookie.HttpOnly = true;
    options.Cookie.IsEssential = true;
});

builder.Services.AddHttpClient<GitHubReleaseService>();
builder.Services.AddScoped<GitHubReleaseService>();

builder.Services.AddSingleton<EmailService>();
builder.Services.AddScoped<AuthService>();
builder.Services.AddScoped<DocSnapshotService>();

var app = builder.Build();

using (var scope = app.Services.CreateScope())
{
    var dbContext = scope.ServiceProvider.GetRequiredService<ApplicationDbContext>();
    dbContext.Database.EnsureCreated();
    await DbInitializer.MigrateAsync(dbContext);
    await DbInitializer.SeedDataAsync(dbContext);

    // 应用启动时自动从 GitHub 同步最新版本
    try
    {
        var gitHubService = scope.ServiceProvider.GetRequiredService<GitHubReleaseService>();
        var owner = builder.Configuration["GitHub:Owner"] ?? "LoongSerpent9Realms";
        var repo = builder.Configuration["GitHub:Repo"] ?? "Jundot";

        var releases = await gitHubService.GetReleasesAsync(owner, repo, 10);
        if (releases != null && releases.Count > 0)
        {
            foreach (var ghRelease in releases)
            {
                var existing = await dbContext.ReleaseVersions
                    .FirstOrDefaultAsync(r => r.VersionNumber == ghRelease.TagName);

                var supportedPlatforms = DbInitializer.ExtractPlatformsFromAssets(ghRelease.Assets);

                if (existing == null)
                {
                    var newRelease = new ReleaseVersion
                    {
                        VersionNumber = ghRelease.TagName,
                        Title = ghRelease.Name,
                        Description = ghRelease.Body,
                        DownloadUrl = ghRelease.HtmlUrl,
                        IsPublished = true,
                        IsBeta = ghRelease.Prerelease,
                        ReleaseDate = ghRelease.PublishedAt ?? DateTime.Now,
                        SupportedPlatforms = supportedPlatforms
                    };
                    dbContext.ReleaseVersions.Add(newRelease);
                }
                else
                {
                    existing.Title = ghRelease.Name;
                    existing.Description = ghRelease.Body;
                    existing.DownloadUrl = ghRelease.HtmlUrl;
                    existing.IsBeta = ghRelease.Prerelease;
                    existing.SupportedPlatforms = supportedPlatforms;
                    if (ghRelease.PublishedAt.HasValue)
                    {
                        existing.ReleaseDate = ghRelease.PublishedAt.Value;
                    }
                }
            }
            await dbContext.SaveChangesAsync();
            Console.WriteLine($"[Jundot] 已从 GitHub 同步 {releases.Count} 个版本");
        }
    }
    catch (Exception ex)
    {
        Console.WriteLine($"[Jundot] 从 GitHub 同步版本失败: {ex.Message}");
    }
}

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error");
    app.UseHsts();
}

app.UseHttpsRedirection();
app.UseStaticFiles();

app.UseRouting();

app.UseSession();

app.UseAuthorization();

var assetStorePublicBaseUrl = builder.Configuration["AssetStore:PublicBaseUrl"] ?? "https://jundot.loongst.com";

app.MapGet("/api/v1", () => Results.Json(new
{
    name = "Jundot Asset Store",
    version = "1"
}));

app.MapGet("/api/v1/tags/", () => Results.Json(new[]
{
    new { display_name = "Audio", slug = "audio" },
    new { display_name = "Integration", slug = "integration" },
    new { display_name = "Wwise", slug = "wwise" }
}));

app.MapGet("/api/v1/licenses/", () => Results.Json(new[]
{
    new { type = "MIT" }
}));

app.MapGet("/api/v1/search/query/", (HttpContext context) =>
{
    var query = context.Request.Query["filter"].ToString();
    var hits = new List<object>();
    if (string.IsNullOrWhiteSpace(query) || "jundot wwise audio audiokinetic integration".Contains(query, StringComparison.OrdinalIgnoreCase))
    {
        hits.Add(new { asset = AssetStoreApi.CreateWwiseAsset(assetStorePublicBaseUrl) });
    }

    return Results.Json(new
    {
        count = hits.Count,
        hits
    });
});

app.MapGet("/api/v1/assets/{slug}", (string slug) =>
{
    if (!string.Equals(slug, AssetStoreApi.WwiseSlug, StringComparison.OrdinalIgnoreCase))
    {
        return Results.NotFound();
    }

    return Results.Json(AssetStoreApi.CreateWwiseAsset(assetStorePublicBaseUrl, includeDetails: true));
});

app.MapGet("/api/v1/releases/{publisher}/{asset}/", (string publisher, string asset) =>
{
    if (!string.Equals(asset, AssetStoreApi.WwiseSlug, StringComparison.OrdinalIgnoreCase))
    {
        return Results.NotFound();
    }

    return Results.Json(new[]
    {
        new
        {
            download_url = AssetStoreApi.AbsoluteUrl(assetStorePublicBaseUrl, "/packages/jundot-wwise-addon.zip"),
            version = "0.1.0",
            stable = true,
            min_jundot_version = "4.0.0",
            max_jundot_version = (string?)null,
            changes_bbcode = "Initial Jundot Wwise project addon. Imports an autoload wrapper and generated SoundBanks folder."
        }
    });
});
app.MapStaticAssets();
app.MapRazorPages()
   .WithStaticAssets();

app.Run();

public static class AssetStoreApi
{
    public const string WwiseSlug = "jundot-wwise";

    public static string AbsoluteUrl(string publicBaseUrl, string path)
    {
        return publicBaseUrl.TrimEnd('/') + "/" + path.TrimStart('/');
    }

    public static object CreateWwiseAsset(string publicBaseUrl, bool includeDetails = false)
    {
        var asset = new Dictionary<string, object?>
        {
            ["name"] = "Jundot Wwise",
            ["slug"] = WwiseSlug,
            ["store_url"] = AbsoluteUrl(publicBaseUrl, "/api/v1/assets/" + WwiseSlug),
            ["license_type"] = "MIT",
            ["license_url"] = "https://opensource.org/license/mit/",
            ["reviews_score"] = 0,
            ["thumbnail"] = "",
            ["publisher"] = new
            {
                name = "Jundot",
                slug = "jundot",
                verified = true
            }
        };

        if (includeDetails)
        {
            asset["body_bbcode"] = "[b]Jundot Wwise[/b]\n\nImports the project-side helper for Wwise-enabled Jundot builds. Enable the plugin, place generated SoundBanks under res://Audio/GeneratedSoundBanks/Windows/, then call the JundotWwise autoload.\n\nNote: this addon requires an editor/runtime build that includes the Wwise engine module.";
            asset["source"] = "https://github.com/LoongSerpent9Realms/Jundot";
            asset["tags"] = new[]
            {
                new { display_name = "Audio", slug = "audio" },
                new { display_name = "Integration", slug = "integration" },
                new { display_name = "Wwise", slug = "wwise" }
            };
            asset["media"] = Array.Empty<string>();
        }

        return asset;
    }
}

public static class DbInitializer
{
    public static async Task MigrateAsync(ApplicationDbContext context)
    {
        var connection = context.Database.GetDbConnection();
        await connection.OpenAsync();

        using var cmd = connection.CreateCommand();
        cmd.CommandText = "PRAGMA table_info(ReleaseVersions)";
        using var reader = await cmd.ExecuteReaderAsync();

        var columns = new HashSet<string>();
        while (await reader.ReadAsync())
        {
            columns.Add(reader["name"].ToString()!);
        }
        await reader.CloseAsync();

        if (!columns.Contains("SupportedPlatforms"))
        {
            using var alterCmd = connection.CreateCommand();
            alterCmd.CommandText = "ALTER TABLE ReleaseVersions ADD COLUMN SupportedPlatforms TEXT";
            await alterCmd.ExecuteNonQueryAsync();
            Console.WriteLine("[Jundot] 数据库迁移完成：添加 SupportedPlatforms 列");
        }

        if (!columns.Contains("LicenseType"))
        {
            using var alterCmd = connection.CreateCommand();
            alterCmd.CommandText = "ALTER TABLE ReleaseVersions ADD COLUMN LicenseType INTEGER NOT NULL DEFAULT 0";
            await alterCmd.ExecuteNonQueryAsync();
            Console.WriteLine("[Jundot] 数据库迁移完成：添加 LicenseType 列");
        }

        if (!columns.Contains("EngineBranchId"))
        {
            using var alterCmd = connection.CreateCommand();
            alterCmd.CommandText = "ALTER TABLE ReleaseVersions ADD COLUMN EngineBranchId INTEGER NULL";
            await alterCmd.ExecuteNonQueryAsync();
            Console.WriteLine("[Jundot] 数据库迁移完成：添加 EngineBranchId 列");
        }

        using var tableCheckCmd = connection.CreateCommand();
        tableCheckCmd.CommandText = "SELECT name FROM sqlite_master WHERE type='table' AND name='EngineBranches'";
        var branchesTable = await tableCheckCmd.ExecuteScalarAsync();
        if (branchesTable == null)
        {
            using var createBranchesCmd = connection.CreateCommand();
            createBranchesCmd.CommandText = @"CREATE TABLE EngineBranches (
                Id INTEGER PRIMARY KEY AUTOINCREMENT,
                Name TEXT NOT NULL,
                Slug TEXT NOT NULL UNIQUE,
                Description TEXT NULL,
                ThumbnailUrl TEXT NULL,
                Genre INTEGER NOT NULL DEFAULT 99,
                LicenseType INTEGER NOT NULL DEFAULT 0,
                LicenseName TEXT NULL,
                LicenseUrl TEXT NULL,
                IsPublished INTEGER NOT NULL DEFAULT 0,
                IsFeatured INTEGER NOT NULL DEFAULT 0,
                CreatedAt TEXT NOT NULL,
                UpdatedAt TEXT NOT NULL,
                DownloadCount INTEGER NOT NULL DEFAULT 0
            )";
            await createBranchesCmd.ExecuteNonQueryAsync();
            Console.WriteLine("[Jundot] 数据库迁移完成：创建 EngineBranches 表");
        }

        tableCheckCmd.CommandText = "SELECT name FROM sqlite_master WHERE type='table' AND name='BranchFeatures'";
        var branchFeaturesTable = await tableCheckCmd.ExecuteScalarAsync();
        if (branchFeaturesTable == null)
        {
            using var createFeaturesCmd = connection.CreateCommand();
            createFeaturesCmd.CommandText = @"CREATE TABLE BranchFeatures (
                Id INTEGER PRIMARY KEY AUTOINCREMENT,
                Title TEXT NOT NULL,
                Description TEXT NULL,
                Icon TEXT NULL,
                SortOrder INTEGER NOT NULL DEFAULT 0,
                EngineBranchId INTEGER NOT NULL,
                FOREIGN KEY (EngineBranchId) REFERENCES EngineBranches(Id) ON DELETE CASCADE
            )";
            await createFeaturesCmd.ExecuteNonQueryAsync();
            Console.WriteLine("[Jundot] 数据库迁移完成：创建 BranchFeatures 表");
        }

        // 检查 Users 表是否存在
        tableCheckCmd.CommandText = "SELECT name FROM sqlite_master WHERE type='table' AND name='Users'";
        var usersTable = await tableCheckCmd.ExecuteScalarAsync();
        if (usersTable == null)
        {
            using var createUsersCmd = connection.CreateCommand();
            createUsersCmd.CommandText = @"CREATE TABLE Users (
                Id INTEGER PRIMARY KEY AUTOINCREMENT,
                Username TEXT NOT NULL UNIQUE,
                Email TEXT NOT NULL UNIQUE,
                PasswordHash TEXT NOT NULL,
                Role INTEGER NOT NULL DEFAULT 0,
                IsActive INTEGER NOT NULL DEFAULT 0,
                CreatedAt TEXT NOT NULL,
                LastLoginAt TEXT NULL
            )";
            await createUsersCmd.ExecuteNonQueryAsync();
            Console.WriteLine("[Jundot] 数据库迁移完成：创建 Users 表");
        }

        // 检查 DocSnapshots 表是否存在
        tableCheckCmd.CommandText = "SELECT name FROM sqlite_master WHERE type='table' AND name='DocSnapshots'";
        var snapshotsTable = await tableCheckCmd.ExecuteScalarAsync();
        if (snapshotsTable == null)
        {
            using var createSnapshotsCmd = connection.CreateCommand();
            createSnapshotsCmd.CommandText = @"CREATE TABLE DocSnapshots (
                Id INTEGER PRIMARY KEY AUTOINCREMENT,
                DocId TEXT NOT NULL,
                DocTitle TEXT NULL,
                Language TEXT NOT NULL DEFAULT 'zh-CN',
                Content TEXT NOT NULL,
                SnapshotName TEXT NULL,
                CreatedAt TEXT NOT NULL,
                CreatedByUserId INTEGER NOT NULL,
                CreatedByUsername TEXT NULL
            )";
            await createSnapshotsCmd.ExecuteNonQueryAsync();
            
            using var createIndexCmd = connection.CreateCommand();
            createIndexCmd.CommandText = "CREATE INDEX IX_DocSnapshots_DocId ON DocSnapshots(DocId)";
            await createIndexCmd.ExecuteNonQueryAsync();
            
            Console.WriteLine("[Jundot] 数据库迁移完成：创建 DocSnapshots 表");
        }

        // 检查 EmailVerificationCodes 表是否存在
        tableCheckCmd.CommandText = "SELECT name FROM sqlite_master WHERE type='table' AND name='EmailVerificationCodes'";
        var codesTable = await tableCheckCmd.ExecuteScalarAsync();
        if (codesTable == null)
        {
            using var createCodesCmd = connection.CreateCommand();
            createCodesCmd.CommandText = @"CREATE TABLE EmailVerificationCodes (
                Id INTEGER PRIMARY KEY AUTOINCREMENT,
                Email TEXT NOT NULL,
                Code TEXT NOT NULL,
                Purpose INTEGER NOT NULL,
                CreatedAt TEXT NOT NULL,
                ExpiresAt TEXT NOT NULL,
                IsUsed INTEGER NOT NULL DEFAULT 0
            )";
            await createCodesCmd.ExecuteNonQueryAsync();
            
            using var createIndexCmd = connection.CreateCommand();
            createIndexCmd.CommandText = "CREATE INDEX IX_EmailVerificationCodes_Email_Code_Purpose ON EmailVerificationCodes(Email, Code, Purpose)";
            await createIndexCmd.ExecuteNonQueryAsync();
            
            Console.WriteLine("[Jundot] 数据库迁移完成：创建 EmailVerificationCodes 表");
        }

        await connection.CloseAsync();
    }

    public static async Task SeedDataAsync(ApplicationDbContext context)
    {
        if (!context.SiteContents.Any())
        {
            var contents = new List<SiteContent>
            {
                new() { Key = "Hero_Title", Value = "Jundot Engine", Description = "首页主标题" },
                new() { Key = "Hero_Subtitle", Value = "基于 Godot 的 AI 辅助自动迭代引擎", Description = "首页副标题" },
                new() { Key = "Hero_Description", Value = "把游戏开发中的对话、日志分析、脚本自检、构建反馈、补丁迭代和打包发布放到一个可审核的闭环里。你仍然掌控方向，AI 负责把重复排查和迭代步骤推进得更快。", Description = "首页描述文字" },
                new() { Key = "Features_Title", Value = "不是把 AI 放在旁边，而是放进开发现场。", Description = "功能区标题" },
                new() { Key = "Features_Description", Value = "Jundot 保留 Godot 的编辑器、2D/3D、导出和开源生态，同时把 AI 辅助工作流接入引擎内部。它适合想快速试错、持续修复、减少重复排查成本的独立开发者和小团队。", Description = "功能区描述" },
                new() { Key = "Workflow_Title", Value = "从想法到可测试版本，流程更短。", Description = "工作流标题" },
                new() { Key = "Workflow_Description", Value = "Jundot 的核心不是替代开发者，而是把原本分散在聊天、终端、日志、编辑器和打包工具之间的步骤串起来。", Description = "工作流描述" },
                new() { Key = "CTA_Title", Value = "开始试用 Jundot。", Description = "行动号召标题" },
                new() { Key = "CTA_Description", Value = "下载预览版，打开项目，在编辑器里让 AI 帮你分析、修改、检查和迭代。当前仍是 beta，建议先用于测试项目和原型项目。", Description = "行动号召描述" },
                new() { Key = "Footer_Text", Value = "Jundot Engine - AI 辅助自动迭代的游戏引擎", Description = "页脚文字" },
                new() { Key = "Footer_Copyright", Value = "Jundot 基于 Godot Engine，遵循相关 MIT/Expat 许可与版权声明。", Description = "页脚版权信息" }
            };
            context.SiteContents.AddRange(contents);
        }

        EngineBranch? coreBranch = null;
        if (!context.EngineBranches.Any())
        {
            coreBranch = new EngineBranch
            {
                Name = "核心通用版",
                Slug = "core",
                Description = "Jundot Engine 核心版本，基于 Godot 的 AI 辅助自动迭代引擎，适合所有类型的游戏开发。包含完整的 2D/3D 能力、AI 对话模块、热更新系统。",
                Genre = GameGenre.Other,
                LicenseType = LicenseType.MIT,
                LicenseName = "MIT License",
                LicenseUrl = "https://opensource.org/licenses/MIT",
                IsPublished = true,
                IsFeatured = true,
                CreatedAt = new DateTime(2026, 6, 1),
                UpdatedAt = new DateTime(2026, 6, 1),
                Features = new List<BranchFeature>
                {
                    new() { Title = "完整 2D/3D 支持", Description = "保留 Godot 全部 2D 和 3D 渲染与物理能力。", Icon = "🎮", SortOrder = 1 },
                    new() { Title = "AI 辅助开发", Description = "编辑器内 AI 对话、问题分析、代码生成与修复。", Icon = "🤖", SortOrder = 2 },
                    new() { Title = "热更新系统", Description = "引擎级热更新，快速迭代和分发新功能。", Icon = "🔥", SortOrder = 3 },
                    new() { Title = "MIT 开源", Description = "完全开源，游戏成果归你所有。", Icon = "📜", SortOrder = 4 }
                }
            };

            var action2dBranch = new EngineBranch
            {
                Name = "2D 横版动作版",
                Slug = "action-2d",
                Description = "专为 2D 横版动作游戏优化的引擎分支。内置平台跳跃、角色控制器、攻击判定、敌人 AI 等模板，快速搭建动作游戏。",
                Genre = GameGenre.Action2D,
                LicenseType = LicenseType.MIT,
                LicenseName = "MIT License",
                LicenseUrl = "https://opensource.org/licenses/MIT",
                IsPublished = true,
                IsFeatured = true,
                CreatedAt = new DateTime(2026, 6, 10),
                UpdatedAt = new DateTime(2026, 6, 10),
                Features = new List<BranchFeature>
                {
                    new() { Title = "平台跳跃控制器", Description = "开箱即用的 2D 平台角色控制器，支持跳跃、二段跳、墙壁滑行。", Icon = "🏃", SortOrder = 1 },
                    new() { Title = "攻击判定系统", Description = "近战/远程攻击判定框架，支持连击、蓄力、无敌帧。", Icon = "⚔️", SortOrder = 2 },
                    new() { Title = "敌人 AI 模板", Description = "巡逻、追击、攻击等多种敌人行为模板。", Icon = "👾", SortOrder = 3 },
                    new() { Title = "关卡编辑器", Description = "内置瓦片地图和关卡快速编辑工具。", Icon = "🗺️", SortOrder = 4 }
                }
            };

            var rpgBranch = new EngineBranch
            {
                Name = "RPG 角色扮演版",
                Slug = "rpg",
                Description = "专为角色扮演游戏打造的引擎分支。包含对话系统、任务系统、背包系统、技能树、战斗系统等 RPG 核心模块。",
                Genre = GameGenre.RPG,
                LicenseType = LicenseType.MIT,
                LicenseName = "MIT License",
                LicenseUrl = "https://opensource.org/licenses/MIT",
                IsPublished = true,
                IsFeatured = true,
                CreatedAt = new DateTime(2026, 6, 15),
                UpdatedAt = new DateTime(2026, 6, 15),
                Features = new List<BranchFeature>
                {
                    new() { Title = "对话系统", Description = "可视化对话编辑器，支持分支对话、条件判断。", Icon = "💬", SortOrder = 1 },
                    new() { Title = "任务系统", Description = "主线/支线任务框架，支持任务追踪和奖励。", Icon = "📋", SortOrder = 2 },
                    new() { Title = "背包与装备", Description = "物品系统、装备系统、背包管理界面。", Icon = "🎒", SortOrder = 3 },
                    new() { Title = "回合制战斗", Description = "可扩展的回合制战斗系统框架。", Icon = "⚔️", SortOrder = 4 }
                }
            };

            var puzzleBranch = new EngineBranch
            {
                Name = "解谜益智版",
                Slug = "puzzle",
                Description = "专注解谜益智游戏的引擎分支。提供物理交互、机关系统、关卡管理、提示系统等解谜游戏核心功能。",
                Genre = GameGenre.Puzzle,
                LicenseType = LicenseType.MIT,
                LicenseName = "MIT License",
                LicenseUrl = "https://opensource.org/licenses/MIT",
                IsPublished = true,
                IsFeatured = false,
                CreatedAt = new DateTime(2026, 6, 20),
                UpdatedAt = new DateTime(2026, 6, 20),
                Features = new List<BranchFeature>
                {
                    new() { Title = "物理交互", Description = "丰富的 2D/3D 物理交互组件。", Icon = "🧩", SortOrder = 1 },
                    new() { Title = "机关系统", Description = "开关、传送门、移动平台等机关组件库。", Icon = "🔧", SortOrder = 2 },
                    new() { Title = "关卡管理", Description = "关卡解锁、进度保存、星级评价系统。", Icon = "⭐", SortOrder = 3 },
                    new() { Title = "提示系统", Description = "可配置的玩家提示和帮助系统。", Icon = "💡", SortOrder = 4 }
                }
            };

            context.EngineBranches.AddRange(coreBranch, action2dBranch, rpgBranch, puzzleBranch);

            if (!context.ReleaseVersions.Any())
            {
                var release = new ReleaseVersion
                {
                    VersionNumber = "1.7.4 beta",
                    Title = "Jundot Engine 1.7.4 beta",
                    Description = "首个公开测试版本",
                    DownloadUrl = "https://github.com/LoongSerpent9Realms/Jundot/releases",
                    IsPublished = true,
                    IsBeta = true,
                    ReleaseDate = new DateTime(2026, 6, 1),
                    LicenseType = LicenseType.MIT,
                    Features = new List<ReleaseFeature>
                    {
                        new() { Title = "基于 Godot 4.6.3", Description = "沿用成熟的 Godot 架构、编辑器体验、2D/3D 能力和导出流程。" },
                        new() { Title = "内置 AI Chat 模块", Description = "包含编辑器对话入口、Skill 系统、记忆系统、安全确认和工具调用可视化。" },
                        new() { Title = "图形化打包工具", Description = "配置平台、架构、版本、日志、更新检测和多平台发布，不必每次手写命令。" }
                    }
                };
                coreBranch.Releases.Add(release);
            }
        }

        await context.SaveChangesAsync();
    }

    public static string? ExtractPlatformsFromAssets(List<GitHubAsset>? assets)
    {
        if (assets == null || assets.Count == 0)
            return null;

        var platforms = new HashSet<string>();
        foreach (var asset in assets)
        {
            var name = asset.Name.ToLower();
            if (name.Contains("windows")) platforms.Add("Windows");
            else if (name.Contains("win")) platforms.Add("Windows");
            else if (name.Contains("macos")) platforms.Add("macOS");
            else if (name.Contains("mac")) platforms.Add("macOS");
            else if (name.Contains("linux")) platforms.Add("Linux");
            else if (name.Contains("ubuntu")) platforms.Add("Linux");
            else if (name.Contains("debian")) platforms.Add("Linux");
            else if (name.Contains("android")) platforms.Add("Android");
            else if (name.Contains("ios")) platforms.Add("iOS");
            else if (name.Contains("web")) platforms.Add("Web");
            else if (name.Contains("html5")) platforms.Add("Web");
        }
        return platforms.Count > 0 ? string.Join(" · ", platforms.OrderBy(p => p)) : null;
    }
}
