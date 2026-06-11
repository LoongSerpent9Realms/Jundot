// I18N.cs - Lightweight i18n for WinForms
// Translation files: lang.zh-CN.json / lang.en.json  (placed next to the exe)

using System.Text;
using System.Text.Json;

namespace JundotPackageBuilder;

public static class I18N
{
    private static readonly Dictionary<string, string> _dict = new();
    public static string CurrentLang { get; private set; } = "zh_CN";

    public static event Action? LanguageChanged;

    static I18N()
    {
        Load("zh_CN"); // default
    }

    public static void Load(string lang)
    {
        // Normalize: zh-CN → zh_CN, etc.
        lang = lang.Replace('-', '_');
        CurrentLang = lang;
        _dict.Clear();

        var exeDir = AppContext.BaseDirectory;
        var path = Path.Combine(exeDir, $"lang.{lang}.json");

        if (!File.Exists(path))
        {
            // fallback: try to create default English if missing
            if (lang == "en") CreateDefaultEnglish(path);
            else if (lang == "zh_CN") CreateDefaultChinese(path);
        }

        if (File.Exists(path))
        {
            try
            {
                var json = File.ReadAllText(path, Encoding.UTF8);
                var data = JsonSerializer.Deserialize<Dictionary<string, string>>(json);
                if (data != null)
                {
                    foreach (var kv in data)
                        _dict[kv.Key] = kv.Value;
                }
            }
            catch { /* ignore corrupt file */ }
        }

        LanguageChanged?.Invoke();
    }

    /// <summary>Get translated string by key. Falls back to key itself if missing.</summary>
    public static string T(string key) => _dict.TryGetValue(key, out var v) ? v : key;

    /// <summary>Save language preference to config.json next to exe.</summary>
    public static void SavePreference(string lang)
    {
        var cfgPath = Path.Combine(AppContext.BaseDirectory, "config.json");
        File.WriteAllText(cfgPath, JsonSerializer.Serialize(new { language = lang }), Encoding.UTF8);
    }

    /// <summary>Load language preference from config.json.</summary>
    public static string LoadPreference()
    {
        var cfgPath = Path.Combine(AppContext.BaseDirectory, "config.json");
        if (!File.Exists(cfgPath)) return "zh_CN";
        try
        {
            var json = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(File.ReadAllText(cfgPath, Encoding.UTF8));
            if (json != null && json.TryGetValue("language", out var el))
                return el.GetString() ?? "zh_CN";
        }
        catch { }
        return "zh_CN";
    }

    // ── Default Chinese strings ────────────────────────────────
    private static void CreateDefaultChinese(string path)
    {
        var dict = new Dictionary<string, string>
        {
            // Tabs
            ["Tab.BuildSettings"] = "构建设置",
            ["Tab.Version"] = "版本管理",
            ["Tab.Builds"] = "构建历史",
            ["Tab.Advanced"] = "高级",

            // Build Settings labels
            ["Label.Target"] = "构建目标:",
            ["Label.Platform"] = "平台:",
            ["Label.Architecture"] = "架构:",
            ["Label.Jobs"] = "并行任务数:",
            ["Label.ScriptLanguage"] = "脚本语言:",
            ["Label.CurrentVersion"] = "当前版本:",
            ["Label.Major"] = "主版本号:",
            ["Label.Minor"] = "次版本号:",
            ["Label.Patch"] = "修订号:",
            ["Label.Status"] = "状态后缀:",
            ["Label.MingwPrefix"] = "MinGW 前缀:",
            ["Label.PackageName"] = "包名:",
            ["Label.OutputDir"] = "输出目录:",
            ["Label.LogDir"] = "日志目录:",
            ["Label.ExtraSCons"] = "额外 SCons 参数:",
            ["Label.RepoRoot"] = "仓库根目录:",
            ["Label.UILanguage"] = "界面语言:",

            // Checkboxes
            ["Check.UseMinGW"] = "使用 MinGW",
            ["Check.WinOptDeps"] = "启用 Windows 可选依赖 (d3d12, accesskit, angle)",
            ["Check.SkipBuild"] = "跳过构建（仅打包）",
            ["Check.InstallSCons"] = "自动安装 SCons",
            ["Check.CleanDir"] = "构建前清理目录",
            ["Check.AutoVersion"] = "构建前自动更新版本号（patch +1）",
            ["Check.GenManifest"] = "生成更新清单 (update-manifest.json) —— 用于线上热更新系统",

            // Buttons
            ["Button.Browse"] = "浏览...",
            ["Button.Build"] = "▶ 构建",
            ["Button.Stop"] = "■ 停止",
            ["Button.Refresh"] = "↻ 刷新",
            ["Button.Launch"] = "▶ 启动",
            ["Button.OpenFolder"] = "📂 打开目录",
            ["Button.ViewLog"] = "📄 查看日志",
            ["Button.Delete"] = "🗑 删除",
            ["Button.ReloadVersion"] = "↻ 刷新版本",
            ["Button.SaveVersion"] = "💾 保存版本",
            ["Button.OpenVersionPy"] = "📄 编辑 version.py",

            // Builds tab columns
            ["Column.Name"] = "名称",
            ["Column.Version"] = "版本",
            ["Column.Type"] = "类型",
            ["Column.Arch"] = "架构",
            ["Column.Script"] = "脚本",
            ["Column.Lang"] = "语言",
            ["Column.Date"] = "日期",
            ["Column.Size"] = "大小",
            ["Column.Status"] = "状态",

            // Status messages
            ["Status.Ready"] = "就绪",
            ["Status.Building"] = "构建中...",
            ["Status.BuildSuccess"] = "构建成功完成！",
            ["Status.BuildFailed"] = "构建失败或被取消。",
            ["Status.NoRecord"] = "暂无构建记录。",

            // Language ComboBox items
            ["Lang.zh_CN"] = "中文 (zh_CN)",
            ["Lang.en"] = "English (en)",
            ["Lang.ja"] = "日本語 (ja)",
            ["Lang.ko"] = "한국어 (ko)",
            ["Lang.fr"] = "Français (fr)",
            ["Lang.de"] = "Deutsch (de)",
            ["Lang.es"] = "Español (es)",
            ["Lang.pt_BR"] = "Português (pt_BR)",
            ["Lang.ru"] = "Русский (ru)",

            // Misc
            ["Title"] = "Jundot 打包器",
            ["Menu.Settings"] = "设置 (&S)",
            ["Menu.Language"] = "界面语言",
            ["Dialog.SelectRepo"] = "选择 Jundot 仓库根目录",
            ["Dialog.SelectOutput"] = "选择输出目录",
            ["Dialog.SelectLog"] = "选择日志目录",
            ["Confirm.Delete"] = "删除构建:\n\n{0}\n\n将删除打包目录、zip 和日志。\nbin/ 中的 exe 将保留。\n\n确认继续？",
            ["Error.Launch"] = "无法找到可执行文件。可能已被移动或删除。",
            ["Error.LogNotFound"] = "构建日志未找到。",
            ["Error.FolderNotFound"] = "目录未找到。",
            ["Error.PythonNotFound"] = "未找到 Python。请安装 Python 后重试。",
            ["Error.SConsNotFound"] = "未找到 SCons。请安装 SCons (pip install scons) 后重试。",

            // Hints & Placeholders
            ["Hint.Jobs"] = "0 = 自动（CPU核心-1）",
            ["Placeholder.MingwPrefix"] = "MinGW 前缀 (如 C:\\msys64\\mingw64)",
            ["Placeholder.PackageName"] = "留空则自动生成",
            ["Placeholder.ExtraSCons"] = "如 verbose=yes dev_build=yes",
            ["Placeholder.RepoRoot"] = "自动从工具所在位置检测",
            ["Default.OutputDir"] = "artifacts/packages",
            ["Default.LogDir"] = "artifacts/logs",

            // Builds tab
            ["BuildCount"] = "{0} 个构建记录",
            ["Status.Missing"] = "缺失",
            ["Status.Ready"] = "就绪",

            // Version tab
            ["Hint.Version"] = "修改版本号后点击「保存版本」写入 version.py。\n若启用了自动更新，每次构建前会 patch +1，失败或取消会回滚。",

            // Column headers
            ["Col.Name"]    = "名称",
            ["Col.Version"] = "版本",
            ["Col.Type"]    = "类型",
            ["Col.Arch"]    = "架构",
            ["Col.Script"]  = "脚本",
            ["Col.Date"]    = "构建时间",
            ["Col.Size"]    = "大小",
            ["Col.Status"]  = "状态",

            // Language names
            ["Lang.zh_CN"] = "中文",
            ["Lang.en"]    = "English",

            // ── Update ─────────────────────────────────────────
            ["Update.Checking"] = "正在检查更新...",
            ["Update.NoUpdate"] = "当前已是最新版本。",
            ["Update.Available"] = "发现新版本！",
            ["Update.NewVersion"] = "新版本 {0} 已发布（当前版本 {1}）。\n是否立即下载并安装？",
            ["Update.Downloading"] = "正在下载更新 ({0}%)...",
            ["Update.Installing"] = "正在准备安装...",
            ["Update.InstallingDesc"] = "下载完成，即将关闭并自动安装更新。\n应用程序将在安装完成后自动重启。",
            ["Update.Failed"] = "更新检测失败: {0}",
            ["Update.Error"] = "更新出错",
            ["Update.Title"] = "自动更新",
            ["Update.ConfirmInstall"] = "安装更新",
            ["Update.DownloadFailed"] = "下载更新失败: {0}",
        };

        var opts = new JsonSerializerOptions { WriteIndented = true };
        File.WriteAllText(path, JsonSerializer.Serialize(dict, opts), Encoding.UTF8);
    }

    // ── Default English strings ────────────────────────────────
    private static void CreateDefaultEnglish(string path)
    {
        var dict = new Dictionary<string, string>
        {
            // Tabs
            ["Tab.BuildSettings"] = "Build Settings",
            ["Tab.Version"] = "Version",
            ["Tab.Builds"] = "Builds",
            ["Tab.Advanced"] = "Advanced",

            // Build Settings labels
            ["Label.Target"] = "Target:",
            ["Label.Platform"] = "Platform:",
            ["Label.Architecture"] = "Architecture:",
            ["Label.Jobs"] = "Jobs:",
            ["Label.ScriptLanguage"] = "Script Language:",
            ["Label.CurrentVersion"] = "Current Version:",
            ["Label.Major"] = "Major:",
            ["Label.Minor"] = "Minor:",
            ["Label.Patch"] = "Patch:",
            ["Label.Status"] = "Status:",
            ["Label.MingwPrefix"] = "MinGW Prefix:",
            ["Label.PackageName"] = "Package Name:",
            ["Label.OutputDir"] = "Output Dir:",
            ["Label.LogDir"] = "Log Dir:",
            ["Label.ExtraSCons"] = "Extra SCons Args:",
            ["Label.RepoRoot"] = "Repo Root:",
            ["Label.UILanguage"] = "UI Language:",

            // Checkboxes
            ["Check.UseMinGW"] = "Use MinGW",
            ["Check.WinOptDeps"] = "Enable Windows Optional Deps (d3d12, accesskit, angle)",
            ["Check.SkipBuild"] = "Skip Build (package only)",
            ["Check.InstallSCons"] = "Auto-install SCons",
            ["Check.CleanDir"] = "Clean before build",
            ["Check.AutoVersion"] = "Auto-increment version (patch +1) before build",
            ["Check.GenManifest"] = "Generate update manifest (update-manifest.json) — for online hot-update system",

            // Buttons
            ["Button.Browse"] = "Browse...",
            ["Button.Build"] = "▶ Build",
            ["Button.Stop"] = "■ Stop",
            ["Button.Refresh"] = "↻ Refresh",
            ["Button.Launch"] = "▶ Launch",
            ["Button.OpenFolder"] = "📂 Open Folder",
            ["Button.ViewLog"] = "📄 View Log",
            ["Button.Delete"] = "🗑 Delete",
            ["Button.ReloadVersion"] = "↻ Reload",
            ["Button.SaveVersion"] = "💾 Save Version",
            ["Button.OpenVersionPy"] = "📄 Edit version.py",

            // Builds tab columns
            ["Column.Name"] = "Name",
            ["Column.Version"] = "Version",
            ["Column.Type"] = "Type",
            ["Column.Arch"] = "Arch",
            ["Column.Script"] = "Script",
            ["Column.Lang"] = "Lang",
            ["Column.Date"] = "Date",
            ["Column.Size"] = "Size",
            ["Column.Status"] = "Status",

            // Status messages
            ["Status.Ready"] = "Ready",
            ["Status.Building"] = "Building...",
            ["Status.BuildSuccess"] = "Build completed successfully!",
            ["Status.BuildFailed"] = "Build failed or was cancelled.",
            ["Status.NoRecord"] = "No build records yet.",

            // Language ComboBox items
            ["Lang.zh_CN"] = "中文 (zh_CN)",
            ["Lang.en"] = "English (en)",
            ["Lang.ja"] = "日本語 (ja)",
            ["Lang.ko"] = "한국어 (ko)",
            ["Lang.fr"] = "Français (fr)",
            ["Lang.de"] = "Deutsch (de)",
            ["Lang.es"] = "Español (es)",
            ["Lang.pt_BR"] = "Português (pt_BR)",
            ["Lang.ru"] = "Русский (ru)",

            // Misc
            ["Title"] = "Jundot Package Builder",
            ["Menu.Settings"] = "&Settings",
            ["Menu.Language"] = "UI Language",
            ["Dialog.SelectRepo"] = "Select Jundot Repo Root",
            ["Dialog.SelectOutput"] = "Select Output Directory",
            ["Dialog.SelectLog"] = "Select Log Directory",
            ["Confirm.Delete"] = "Delete build:\n\n{0}\n\nThis will remove the package folder, zip, and logs.\nThe bin/ exe will be kept.\n\nContinue?",
            ["Error.Launch"] = "Executable not found. It may have been moved or deleted.",
            ["Error.LogNotFound"] = "Build log not found.",
            ["Error.FolderNotFound"] = "Folder not found.",
            ["Error.PythonNotFound"] = "Python was not found. Please install Python and retry.",
            ["Error.SConsNotFound"] = "SCons was not found. Please install SCons (pip install scons) and retry.",

            // Hints & Placeholders
            ["Hint.Jobs"] = "0 = auto (CPU - 1)",
            ["Placeholder.MingwPrefix"] = "MinGW prefix (e.g. C:\\msys64\\mingw64)",
            ["Placeholder.PackageName"] = "auto-generated if empty",
            ["Placeholder.ExtraSCons"] = "e.g. verbose=yes dev_build=yes",
            ["Placeholder.RepoRoot"] = "auto-detected from tool location",
            ["Default.OutputDir"] = "artifacts/packages",
            ["Default.LogDir"] = "artifacts/logs",

            // Builds tab
            ["BuildCount"] = "{0} build(s) found",
            ["Status.Missing"] = "Missing",
            ["Status.Ready"] = "Ready",

            // Version tab
            ["Hint.Version"] = "Edit version numbers, then click 'Save Version' to write to version.py.\nIf auto-update is enabled, patch increments before each build and rolls back on failure or cancellation.",

            // Column headers
            ["Col.Name"]    = "Name",
            ["Col.Version"] = "Version",
            ["Col.Type"]    = "Type",
            ["Col.Arch"]    = "Arch",
            ["Col.Script"]  = "Script",
            ["Col.Lang"]    = "Language",
            ["Col.Date"]    = "Build Date",
            ["Col.Size"]    = "Size",
            ["Col.Status"]  = "Status",

            // Language names
            ["Lang.zh_CN"] = "中文",
            ["Lang.en"]    = "English",

            // ── Update ─────────────────────────────────────────
            ["Update.Checking"] = "Checking for updates...",
            ["Update.NoUpdate"] = "You are running the latest version.",
            ["Update.Available"] = "New version available!",
            ["Update.NewVersion"] = "Version {0} is available (current: {1}).\nDownload and install now?",
            ["Update.Downloading"] = "Downloading update ({0}%)...",
            ["Update.Installing"] = "Preparing to install...",
            ["Update.InstallingDesc"] = "Download complete. The application will now close to install the update.\nIt will restart automatically once the update is installed.",
            ["Update.Failed"] = "Update check failed: {0}",
            ["Update.Error"] = "Update Error",
            ["Update.Title"] = "Auto Update",
            ["Update.ConfirmInstall"] = "Install Update",
            ["Update.DownloadFailed"] = "Download failed: {0}",
        };

        var opts = new JsonSerializerOptions { WriteIndented = true };
        File.WriteAllText(path, JsonSerializer.Serialize(dict, opts), Encoding.UTF8);
    }
}
