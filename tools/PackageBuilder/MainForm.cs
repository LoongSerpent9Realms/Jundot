using System.ComponentModel;
using System.Reflection;
using System.Text;

namespace JundotPackageBuilder;

public partial class MainForm : Form
{
    private const int DefaultBottomPanelHeight = 220;
    private const int MinBottomPanelHeight = 160;
    private const int MinTopPanelHeight = 280;

    // ── Controls ──────────────────────────────────────────────
    private SplitContainer _splitMain = null!;

    // Navigation bar (replaces TabControl)
    private FlowLayoutPanel _navBar = null!;
    private Button _btnNavSettings = null!;
    private Button _btnNavVersion = null!;
    private Button _btnNavBuilds = null!;
    private Button _btnNavAdvanced = null!;
    private Panel _contentPanel = null!;
    private Control[]? _pages;
    private int _currentPageIndex;

    // Build Settings tab
    private ComboBox _cbTarget = null!;
    private ComboBox _cbPlatform = null!;
    private ComboBox _cbArch = null!;
    private NumericUpDown _numJobs = null!;
    private ComboBox _cbScriptLang = null!;

    private CheckBox _chkUseMinGW = null!;
    private CheckBox _chkWinOptDeps = null!;
    private CheckBox _chkSkipBuild = null!;
    private CheckBox _chkInstallSCons = null!;
    private CheckBox _chkCleanDir = null!;
    private CheckBox _chkCleanBuild = null!;
    private TextBox _txtMingwPrefix = null!;
    private TextBox _txtPackageName = null!;
    private TextBox _txtOutputDir = null!;
    private TextBox _txtLogDir = null!;
    private TextBox _txtExtraSCons = null!;
    private TextBox _txtRepoRoot = null!;
    private Button _btnBrowseRepo = null!;
    private Button _btnBrowseOutput = null!;
    private Button _btnBrowseLog = null!;

    // Version tab
    private TextBox _txtVerMajor = null!;
    private TextBox _txtVerMinor = null!;
    private TextBox _txtVerPatch = null!;
    private TextBox _txtVerStatus = null!;
    private Label _lblVerDisplay = null!;
    private CheckBox _chkAutoVersion = null!;
    private CheckBox _chkGenManifest = null!;

    // Builds tab
    private ListView _lvBuilds = null!;
    private Button _btnLaunch = null!;
    private Button _btnOpenFolder = null!;
    private Button _btnViewLog = null!;
    private Button _btnDeleteBuild = null!;
    private Button _btnRefreshBuilds = null!;
    private Label _lblBuildCount = null!;
    private Button _btnPublishToGitHub = null!;
    private Button _btnGenerateAiSummary = null!;

    // Console output
    private RichTextBox _rtbConsole = null!;

    // Bottom bar
    private Panel _bottomBar = null!;
    private Button _btnBuild = null!;
    private Button _btnCancel = null!;
    private ProgressBar _progressBar = null!;
    private Label _lblStatus = null!;

    // ── State ─────────────────────────────────────────────────
    private BuildEngine? _engine;
    private BuildManager? _buildManager;
    private CancellationTokenSource? _cts;
	private Label navSeparator = null!;
	private TableLayoutPanel bottomLayout = null!;
	private bool _isRunning;
    private bool _splitterInitialized;

    public MainForm()
    {
        SetTitleWithVersion();
        Size = new Size(960, 720);
        MinimumSize = new Size(800, 600);
        StartPosition = FormStartPosition.CenterScreen;
        Icon = SystemIcons.Shield;

        InitializeComponent();
        InitNavigation();
        LoadLanguagePreference();
        ApplyBottomBarText();
        LoadDefaults();
        RefreshVersionPanel();
        RefreshBuildList();

        // Save config on close
        FormClosing += (s, e) => SaveConfig();

        // Auto-check for updates after form is fully loaded
        Shown += OnShown;
    }

    // ── Language Switch ─────────────────────────────────────
    private void LoadLanguagePreference()
    {
        var lang = I18N.LoadPreference();
        I18N.Load(lang);
        I18N.LanguageChanged += OnLanguageChanged;
    }

    private void OnLanguageChanged()
    {
        ApplyLanguage();
    }

    private void ApplyLanguage()
    {
        // Update form title
        SetTitleWithVersion();
        ApplyBottomBarText();

        // Update all controls recursively
        foreach (var c in GetAllControls(this))
        {
            if (c.Tag is string key && key.StartsWith("i18n:"))
            {
                var realKey = key["i18n:".Length..];
                c.Text = I18N.T(realKey);
            }
        }

        // Update ListView columns
        if (_lvBuilds != null)
        {
            _lvBuilds.Columns[0].Text = I18N.T("Col.Name");
            _lvBuilds.Columns[1].Text = I18N.T("Col.Version");
            _lvBuilds.Columns[2].Text = I18N.T("Col.Type");
            _lvBuilds.Columns[3].Text = I18N.T("Col.Arch");
            _lvBuilds.Columns[4].Text = I18N.T("Col.Script");
            _lvBuilds.Columns[5].Text = I18N.T("Col.Lang");
            _lvBuilds.Columns[6].Text = I18N.T("Col.Date");
            _lvBuilds.Columns[7].Text = I18N.T("Col.Size");
            _lvBuilds.Columns[8].Text = I18N.T("Col.Status");
        }

        // Refresh build list to update status texts
        RefreshBuildList();
    }

    private static IEnumerable<Control> GetAllControls(Control parent)
    {
        foreach (Control c in parent.Controls)
        {
            yield return c;
            foreach (var child in GetAllControls(c))
                yield return child;
        }
    }

	private void InitializeComponent()
	{
		_splitMain = new SplitContainer();
		_navBar = new FlowLayoutPanel();
		navSeparator = new Label();
		_contentPanel = new Panel();
		bottomLayout = new TableLayoutPanel();
		_rtbConsole = new RichTextBox();
		_bottomBar = new Panel();
		_btnBuild = new Button();
		_btnCancel = new Button();
		_progressBar = new ProgressBar();
		_lblStatus = new Label();
		((ISupportInitialize)_splitMain).BeginInit();
		_splitMain.Panel1.SuspendLayout();
		_splitMain.Panel2.SuspendLayout();
		_splitMain.SuspendLayout();
		bottomLayout.SuspendLayout();
		_bottomBar.SuspendLayout();
		SuspendLayout();
		// 
		// _splitMain
		// 
		_splitMain.Dock = DockStyle.Fill;
		_splitMain.Location = new Point(0, 0);
		_splitMain.Name = "_splitMain";
		_splitMain.Orientation = Orientation.Horizontal;
		_splitMain.FixedPanel = FixedPanel.Panel2;
		// 
		// _splitMain.Panel1
		// 
		_splitMain.Panel1.Controls.Add(_contentPanel);
		_splitMain.Panel1.Controls.Add(navSeparator);
		_splitMain.Panel1.Controls.Add(_navBar);
		// 
		// _splitMain.Panel2
		// 
		_splitMain.Panel2.Controls.Add(bottomLayout);
		_splitMain.Size = new Size(150, 100);
		_splitMain.TabIndex = 1;
		// 
		// _navBar
		// 
		_navBar.Dock = DockStyle.Top;
		_navBar.FlowDirection = FlowDirection.LeftToRight;
		_navBar.Height = 50;
		_navBar.Location = new Point(0, 0);
		_navBar.Name = "_navBar";
		_navBar.Padding = new Padding(12, 8, 12, 4);
		_navBar.Size = new Size(200, 100);
		_navBar.TabIndex = 0;
		_navBar.WrapContents = false;
		// 
		// navSeparator
		// 
		navSeparator.BackColor = Color.FromArgb(220, 220, 220);
		navSeparator.Dock = DockStyle.Top;
		navSeparator.Height = 2;
		navSeparator.Location = new Point(0, 0);
		navSeparator.Name = "navSeparator";
		navSeparator.Size = new Size(100, 23);
		navSeparator.TabIndex = 1;
		// 
		// _contentPanel
		// 
		_contentPanel.Dock = DockStyle.Fill;
		_contentPanel.Location = new Point(0, 0);
		_contentPanel.Name = "_contentPanel";
		_contentPanel.Size = new Size(200, 100);
		_contentPanel.TabIndex = 2;
		// 
		// bottomLayout
		// 
		bottomLayout.Dock = DockStyle.Fill;
		bottomLayout.ColumnCount = 1;
		bottomLayout.RowCount = 2;
		bottomLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
		bottomLayout.Controls.Add(_rtbConsole, 0, 0);
		bottomLayout.Controls.Add(_bottomBar, 0, 1);
		bottomLayout.Location = new Point(0, 0);
		bottomLayout.Name = "bottomLayout";
		bottomLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
		bottomLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48F));
		bottomLayout.Size = new Size(200, 100);
		bottomLayout.TabIndex = 0;
		// 
		// _rtbConsole
		// 
		_rtbConsole.Dock = DockStyle.Fill;
		_rtbConsole.Location = new Point(3, 3);
		_rtbConsole.Name = "_rtbConsole";
		_rtbConsole.Size = new Size(100, 46);
		_rtbConsole.TabIndex = 0;
		_rtbConsole.BackColor = Color.FromArgb(18, 18, 18);
		_rtbConsole.ForeColor = Color.FromArgb(230, 230, 230);
		_rtbConsole.BorderStyle = BorderStyle.FixedSingle;
		_rtbConsole.Font = new Font("Consolas", 9.5f);
		_rtbConsole.ReadOnly = true;
		_rtbConsole.Text = "";
		// 
		// _bottomBar
		// 
		_bottomBar.Dock = DockStyle.Fill;
		_bottomBar.Controls.Add(_btnBuild);
		_bottomBar.Controls.Add(_btnCancel);
		_bottomBar.Controls.Add(_progressBar);
		_bottomBar.Controls.Add(_lblStatus);
		_bottomBar.Location = new Point(3, 55);
		_bottomBar.Name = "_bottomBar";
		_bottomBar.Size = new Size(194, 42);
		_bottomBar.TabIndex = 1;
		// 
		// _btnBuild
		// 
		_btnBuild.FlatAppearance.BorderSize = 0;
		_btnBuild.Location = new Point(12, 10);
		_btnBuild.Name = "_btnBuild";
		_btnBuild.Size = new Size(120, 28);
		_btnBuild.TabIndex = 0;
		_btnBuild.Tag = "i18n:Button.Build";
		_btnBuild.Click += BtnBuild_Click;
		// 
		// _btnCancel
		// 
		_btnCancel.Location = new Point(140, 10);
		_btnCancel.Name = "_btnCancel";
		_btnCancel.Size = new Size(100, 28);
		_btnCancel.TabIndex = 1;
		_btnCancel.Tag = "i18n:Button.Stop";
		_btnCancel.Enabled = false;
		_btnCancel.Click += BtnCancel_Click;
		// 
		// _progressBar
		// 
		_progressBar.Location = new Point(250, 12);
		_progressBar.Name = "_progressBar";
		_progressBar.Size = new Size(200, 22);
		_progressBar.TabIndex = 2;
		_progressBar.Visible = false;
		// 
		// _lblStatus
		// 
		_lblStatus.Anchor = AnchorStyles.Right | AnchorStyles.Top | AnchorStyles.Left;
		_lblStatus.Location = new Point(460, 12);
		_lblStatus.Name = "_lblStatus";
		_lblStatus.Size = new Size(200, 23);
		_lblStatus.TabIndex = 3;
		_lblStatus.TextAlign = ContentAlignment.MiddleRight;
		// 
		// MainForm
		// 
		ClientSize = new Size(960, 720);
		Controls.Add(_splitMain);
		Name = "MainForm";
		Shown += (s, e) => EnsureBottomPanelHeight(true);
		SizeChanged += (s, e) => EnsureBottomPanelHeight(false);
		_splitMain.Panel1.ResumeLayout(false);
		_splitMain.Panel2.ResumeLayout(false);
		((ISupportInitialize)_splitMain).EndInit();
		_splitMain.ResumeLayout(false);
		bottomLayout.ResumeLayout(false);
		_bottomBar.ResumeLayout(false);
		ResumeLayout(false);
		PerformLayout();
	}

    private void ApplyBottomBarText()
    {
        if (_btnBuild == null || _btnCancel == null || _lblStatus == null)
            return;

        _btnBuild.Text = _isRunning ? I18N.T("Status.Building") : I18N.T("Button.Build");
        _btnCancel.Text = I18N.T("Button.Stop");
        _lblStatus.Text = I18N.T("Status.Ready");
    }

	// ── Navigation Helpers ──────────────────────────────────────

    private void EnsureBottomPanelHeight(bool forceDefault)
    {
        if (_splitMain.Height <= 0)
            return;

        if (_splitMain.Height <= MinTopPanelHeight + MinBottomPanelHeight + _splitMain.SplitterWidth)
            return;

        _splitMain.Panel1MinSize = MinTopPanelHeight;
        _splitMain.Panel2MinSize = MinBottomPanelHeight;

        var minDistance = MinTopPanelHeight;
        var maxDistance = _splitMain.Height - MinBottomPanelHeight - _splitMain.SplitterWidth;
        if (maxDistance < minDistance)
            return;

        var maxBottomHeight = _splitMain.Height - minDistance - _splitMain.SplitterWidth;
        var targetBottomHeight = Math.Min(DefaultBottomPanelHeight, maxBottomHeight);
        var currentBottomHeight = _splitMain.Panel2.Height;

        if (!forceDefault && _splitterInitialized && currentBottomHeight >= MinBottomPanelHeight)
            return;

        _splitterInitialized = true;
        var targetDistance = _splitMain.Height - targetBottomHeight - _splitMain.SplitterWidth;
        _splitMain.SplitterDistance = Math.Clamp(targetDistance, minDistance, maxDistance);
    }

	private Button CreateNavButton(string i18nKey, int index)
    {
        var btn = new Button
        {
            Text = I18N.T(i18nKey),
            Tag = $"i18n:{i18nKey}",
            Size = new Size(120, 36),
            FlatStyle = FlatStyle.Flat,
            Font = new Font("Segoe UI", 10.5f),
            TextAlign = ContentAlignment.MiddleCenter,
            Margin = new Padding(6, 0, 6, 0)
        };
        btn.FlatAppearance.BorderSize = 0;
        btn.Click += (s, e) => SwitchToPage(index);
        return btn;
    }

    private void ApplyDefaultNavStyle(Button btn)
    {
        btn.BackColor = Color.Transparent;
        btn.ForeColor = Color.FromArgb(64, 64, 64);
    }

    private void ApplyActiveNavStyle(Button btn)
    {
        btn.BackColor = Color.FromArgb(0, 120, 212);
        btn.ForeColor = Color.White;
    }

    private void SwitchToPage(int index)
    {
        if (_pages == null || index < 0 || index >= _pages.Length) return;

        _contentPanel.Controls.Clear();
        var page = _pages[index];
        page.Dock = DockStyle.Fill;
        _contentPanel.Controls.Add(page);
        _currentPageIndex = index;

        // Update button styles
        var buttons = new[] { _btnNavSettings, _btnNavVersion, _btnNavBuilds, _btnNavAdvanced };
        for (int i = 0; i < buttons.Length; i++)
        {
            if (i == index)
                ApplyActiveNavStyle(buttons[i]);
            else
                ApplyDefaultNavStyle(buttons[i]);
        }

        // Refresh data when switching to data-dependent pages
        if (index == 1) // Version
            RefreshVersionPanel();
        else if (index == 2) // Builds
            RefreshBuildList();
    }

    private void InitNavigation()
    {
        // Create navigation buttons
        _btnNavSettings  = CreateNavButton("Tab.BuildSettings", 0);
        _btnNavVersion   = CreateNavButton("Tab.Version", 1);
        _btnNavBuilds    = CreateNavButton("Tab.Builds", 2);
        _btnNavAdvanced  = CreateNavButton("Tab.Advanced", 3);

        _navBar.Controls.AddRange(new Control[] {
            _btnNavSettings, _btnNavVersion, _btnNavBuilds, _btnNavAdvanced
        });

        // Create all content pages (controls must exist before LoadDefaults uses them)
        _pages = new Control[] {
            BuildSettingsPage(),
            VersionPage(),
            BuildsPage(),
            AdvancedPage()
        };

        // Show the first page
        SwitchToPage(0);
    }

    // ═══════════════════════════════════════════════════════════
    //  BUILD SETTINGS TAB
    // ═══════════════════════════════════════════════════════════

    private Control BuildSettingsPage()
    {
        var wrapper = new Panel { AutoScroll = true, Dock = DockStyle.Fill };
        var layout = new TableLayoutPanel
        {
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowOnly,
            Dock = DockStyle.Top,
            ColumnCount = 3,
            RowCount = 16,
            Padding = new Padding(12, 12, 12, 4)
        };
        for (int i = 0; i < 16; i++)
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 160)); // label
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 70));   // input
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100)); // extra/browse
        wrapper.Controls.Add(layout);

        int row = 0;

        // Row 0: Target
        AddLabel(layout, "Label.Target", row);
        _cbTarget = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        _cbTarget.Items.AddRange(new[] { "editor", "editor.dev", "template_release", "template_debug" });
        _cbTarget.SelectedIndex = 0;
        layout.Controls.Add(_cbTarget, 1, row);

        // Row 1: Platform
        row++;
        AddLabel(layout, "Label.Platform", row);
        _cbPlatform = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        _cbPlatform.Items.AddRange(new[] { "windows", "linuxbsd", "macos", "android", "ios", "web" });
        _cbPlatform.SelectedIndex = 0;
        _cbPlatform.SelectedIndexChanged += (s, e) => UpdateMinGWVisibility();
        layout.Controls.Add(_cbPlatform, 1, row);

        // Row 2: Architecture
        row++;
        AddLabel(layout, "Label.Architecture", row);
        _cbArch = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        _cbArch.Items.AddRange(new[] { "x86_64", "x86_32", "arm64" });
        _cbArch.SelectedIndex = 0;
        layout.Controls.Add(_cbArch, 1, row);

        // Row 3: Jobs
        row++;
        AddLabel(layout, "Label.Jobs", row);
        _numJobs = new NumericUpDown { Minimum = 0, Maximum = 64, Value = 0 };
        layout.Controls.Add(_numJobs, 1, row);
        AddHint(layout, row, "Hint.Jobs");

        // Row 4: Script Language
        row++;
        AddLabel(layout, "Label.ScriptLanguage", row);
        _cbScriptLang = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        _cbScriptLang.Items.AddRange(new[] { "GDScript", "C# (Mono)" });
        _cbScriptLang.SelectedIndex = 0;
        layout.Controls.Add(_cbScriptLang, 1, row);

        // Row 5: MinGW
        row++;
        AddLabel(layout, "Label.MingwPrefix", row);
        var mingwPanel = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        _chkUseMinGW = new CheckBox
        {
            Text = I18N.T("Check.UseMinGW"),
            Tag  = "i18n:Check.UseMinGW",
            AutoSize = true
        };
        _chkUseMinGW.CheckedChanged += (s, e) => _txtMingwPrefix.Enabled = _chkUseMinGW.Checked;
        _txtMingwPrefix = new TextBox
        {
            Width = 240,
            Enabled = false,
            PlaceholderText = I18N.T("Placeholder.MingwPrefix")
        };
        mingwPanel.Controls.Add(_chkUseMinGW);
        mingwPanel.Controls.Add(_txtMingwPrefix);
        layout.Controls.Add(mingwPanel, 1, row);

        // Row 6: Win Optional Deps
        row++;
        AddLabel(layout, "", row);
        _chkWinOptDeps = new CheckBox
        {
            Text = "Enable Windows Optional Deps (d3d12, accesskit, angle)",
            AutoSize = true
        };
        layout.Controls.Add(_chkWinOptDeps, 1, row);

        // Row 7: Skip Build
        row++;
        AddLabel(layout, "", row);
        _chkSkipBuild = new CheckBox { Text = "Skip Build (use existing bin/)", AutoSize = true };
        _chkSkipBuild.CheckedChanged += (s, e) => EnableBuildOptions(!_chkSkipBuild.Checked);
        layout.Controls.Add(_chkSkipBuild, 1, row);

        // Row 8: Install SCons / Clean
        row++;
        AddLabel(layout, "", row);
        var optsPanel = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        _chkInstallSCons = new CheckBox { Text = "Auto-install SCons", AutoSize = true };
        _chkCleanDir = new CheckBox { Text = "Clean Package Dir", AutoSize = true };
        _chkCleanBuild = new CheckBox { Text = "Clean Build", AutoSize = true };
        optsPanel.Controls.Add(_chkInstallSCons);
        optsPanel.Controls.Add(_chkCleanDir);
        optsPanel.Controls.Add(_chkCleanBuild);
        layout.Controls.Add(optsPanel, 1, row);

        // Row 9: Package Name
        row++;
        AddLabel(layout, "Label.PackageName", row);
        _txtPackageName = new TextBox
        {
            Dock = DockStyle.Fill,
            PlaceholderText = I18N.T("Placeholder.PackageName")
        };
        layout.Controls.Add(_txtPackageName, 1, row);

        // Row 10: Output Dir
        row++;
        AddLabel(layout, "Label.OutputDir", row);
        _txtOutputDir = new TextBox { Dock = DockStyle.Fill, Text = I18N.T("Default.OutputDir") };
        layout.Controls.Add(_txtOutputDir, 1, row);
        _btnBrowseOutput = new Button { Text = I18N.T("Button.Browse"), Tag = "i18n:Button.Browse", Width = 80 };
        _btnBrowseOutput.Click += (s, e) => BrowseFolder(_txtOutputDir);
        layout.Controls.Add(_btnBrowseOutput, 2, row);

        // Row 11: Log Dir
        row++;
        AddLabel(layout, "Label.LogDir", row);
        _txtLogDir = new TextBox { Dock = DockStyle.Fill, Text = I18N.T("Default.LogDir") };
        layout.Controls.Add(_txtLogDir, 1, row);
        _btnBrowseLog = new Button { Text = I18N.T("Button.Browse"), Tag = "i18n:Button.Browse", Width = 80 };
        _btnBrowseLog.Click += (s, e) => BrowseFolder(_txtLogDir);
        layout.Controls.Add(_btnBrowseLog, 2, row);

        // Row 12: Extra SCons Args
        row++;
        AddLabel(layout, "Label.ExtraSCons", row);
        _txtExtraSCons = new TextBox
        {
            Dock = DockStyle.Fill,
            PlaceholderText = I18N.T("Placeholder.ExtraSCons")
        };
        layout.Controls.Add(_txtExtraSCons, 1, row);



        // Row 14: Auto Update Version
        row++;
        AddLabel(layout, "", row);
        _chkAutoVersion = new CheckBox
        {
            Text = I18N.T("Check.AutoVersion"),
            Tag = "i18n:Check.AutoVersion",
            AutoSize = true
        };
        layout.Controls.Add(_chkAutoVersion, 1, row);

        // Row 15: Generate Update Manifest
        row++;
        AddLabel(layout, "", row);
        _chkGenManifest = new CheckBox
        {
            Text = I18N.T("Check.GenManifest"),
            Tag = "i18n:Check.GenManifest",
            AutoSize = true,
            Checked = true
        };
        layout.Controls.Add(_chkGenManifest, 1, row);

        return wrapper;
    }

    // ═══════════════════════════════════════════════════════════
    //  VERSION MANAGEMENT TAB
    // ═══════════════════════════════════════════════════════════

    private Control VersionPage()
    {
        var wrapper = new Panel { AutoScroll = true, Dock = DockStyle.Fill };
        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 3,
            Padding = new Padding(12, 12, 12, 4)
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        int row = 0;

        // Row 0: Current Version (read-only display)
        AddLabel(layout, "Label.CurrentVersion", row);
        _lblVerDisplay = new Label
        {
            Text = "—",
            AutoSize = true,
            Font = new Font("Segoe UI", 12f, FontStyle.Bold),
            ForeColor = Color.FromArgb(0, 120, 212),
            Padding = new Padding(0, 6, 0, 6)
        };
        layout.Controls.Add(_lblVerDisplay, 1, row);
        layout.SetColumnSpan(_lblVerDisplay, 2);

        // Row 1: separator
        row++;
        var sep1 = new Label { Text = "", Height = 8 };
        layout.Controls.Add(sep1, 0, row);

        // Row 2: Major
        row++;
        AddLabel(layout, "Label.Major", row);
        _txtVerMajor = new TextBox { Width = 80, TextAlign = HorizontalAlignment.Center };
        layout.Controls.Add(_txtVerMajor, 1, row);

        // Row 3: Minor
        row++;
        AddLabel(layout, "Label.Minor", row);
        _txtVerMinor = new TextBox { Width = 80, TextAlign = HorizontalAlignment.Center };
        layout.Controls.Add(_txtVerMinor, 1, row);

        // Row 4: Patch
        row++;
        AddLabel(layout, "Label.Patch", row);
        _txtVerPatch = new TextBox { Width = 80, TextAlign = HorizontalAlignment.Center };
        layout.Controls.Add(_txtVerPatch, 1, row);

        // Row 5: Status
        row++;
        AddLabel(layout, "Label.Status", row);
        _txtVerStatus = new TextBox { Width = 200 };
        layout.Controls.Add(_txtVerStatus, 1, row);
        layout.SetColumnSpan(_txtVerStatus, 2);

        // Row 6: separator
        row++;
        var sep2 = new Label { Text = "", Height = 16 };
        layout.Controls.Add(sep2, 0, row);

        // Row 7: Action buttons
        row++;
        AddLabel(layout, "", row);
        var btnPanel = new FlowLayoutPanel { AutoSize = true, WrapContents = false };

        var btnReload = new Button
        {
            Text = I18N.T("Button.ReloadVersion"),
            Tag = "i18n:Button.ReloadVersion",
            Size = new Size(120, 32),
            FlatStyle = FlatStyle.Flat
        };
        btnReload.Click += (s, e) => RefreshVersionPanel();
        btnPanel.Controls.Add(btnReload);

        var btnSave = new Button
        {
            Text = I18N.T("Button.SaveVersion"),
            Tag = "i18n:Button.SaveVersion",
            Size = new Size(120, 32),
            FlatStyle = FlatStyle.Flat,
            BackColor = Color.FromArgb(0, 120, 212),
            ForeColor = Color.White
        };
        btnSave.FlatAppearance.BorderSize = 0;
        btnSave.Click += (s, e) => SaveVersionFile();
        btnPanel.Controls.Add(btnSave);

        var btnOpenPy = new Button
        {
            Text = I18N.T("Button.OpenVersionPy"),
            Tag = "i18n:Button.OpenVersionPy",
            Size = new Size(130, 32),
            FlatStyle = FlatStyle.Flat
        };
        btnOpenPy.Click += (s, e) =>
        {
            var vf = GetVersionPyPath();
            if (File.Exists(vf))
            {
                try { System.Diagnostics.Process.Start("notepad", $"\"{vf}\""); }
                catch { /* ignore */ }
            }
        };
        btnPanel.Controls.Add(btnOpenPy);

        layout.Controls.Add(btnPanel, 1, row);
        layout.SetColumnSpan(btnPanel, 2);

        // Row 8: Info
        row++;
        var infoLabel = new Label
        {
            Text = I18N.T("Hint.Version"),
            Tag = "i18n:Hint.Version",
            AutoSize = true,
            Font = new Font("Segoe UI", 9f),
            ForeColor = Color.Gray,
            Padding = new Padding(0, 12, 0, 0)
        };
        layout.Controls.Add(infoLabel, 1, row);
        layout.SetColumnSpan(infoLabel, 2);

        wrapper.Controls.Add(layout);

        return wrapper;
    }

    private string GetVersionPyPath()
    {
        var root = _txtRepoRoot.Text.Trim();
        if (!string.IsNullOrEmpty(root) && File.Exists(Path.Combine(root, "version.py")))
            return Path.Combine(root, "version.py");
        return "";
    }

    private void RefreshVersionPanel()
    {
        var vf = GetVersionPyPath();
        if (string.IsNullOrEmpty(vf))
        {
            _lblVerDisplay.Text = "version.py 未找到";
            _txtVerMajor.Text = "";
            _txtVerMinor.Text = "";
            _txtVerPatch.Text = "";
            _txtVerStatus.Text = "";
            return;
        }

        try
        {
            var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in File.ReadLines(vf))
            {
                var m = System.Text.RegularExpressions.Regex.Match(line, @"^\s*([A-Za-z_]+)\s*=\s*""?([^""\n]+)""?\s*$");
                if (m.Success)
                    values[m.Groups[1].Value] = m.Groups[2].Value;
            }

            var major = values.GetValueOrDefault("major", "0");
            var minor = values.GetValueOrDefault("minor", "0");
            var patch = values.GetValueOrDefault("patch", "0");
            var status = values.GetValueOrDefault("status", "");

            _txtVerMajor.Text = major;
            _txtVerMinor.Text = minor;
            _txtVerPatch.Text = patch;
            _txtVerStatus.Text = status;

            _lblVerDisplay.Text = FormatJundotVersion(major, minor, patch, status);
        }
        catch (Exception ex)
        {
            _lblVerDisplay.Text = $"错误: {ex.Message}";
        }
    }

    private static string FormatJundotVersion(string major, string minor, string patch, string status)
    {
        var version = $"{major}.{minor}";
        if (!string.IsNullOrWhiteSpace(patch) && patch != "0")
            version += $".{patch}";

        if (!string.IsNullOrWhiteSpace(status))
            version += $"-{status}";

        return version;
    }

    private void SaveVersionFile()
    {
        var vf = GetVersionPyPath();
        if (string.IsNullOrEmpty(vf))
        {
            MessageBox.Show("version.py not found. Set the repo root first.", "Error",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        if (!int.TryParse(_txtVerMajor.Text.Trim(), out _) ||
            !int.TryParse(_txtVerMinor.Text.Trim(), out _) ||
            !int.TryParse(_txtVerPatch.Text.Trim(), out _))
        {
            MessageBox.Show("Major, minor, and patch must be integers.", "Invalid Input",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        try
        {
            var major = _txtVerMajor.Text.Trim();
            var minor = _txtVerMinor.Text.Trim();
            var patch = _txtVerPatch.Text.Trim();
            var status = _txtVerStatus.Text.Trim();

            var lines = File.ReadAllLines(vf).ToList();
            for (int i = 0; i < lines.Count; i++)
            {
                var m = System.Text.RegularExpressions.Regex.Match(lines[i], @"^(\s*)([A-Za-z_]+)(\s*=\s*).*$");
                if (!m.Success) continue;

                var key = m.Groups[2].Value;
                var indent = m.Groups[1].Value;
                var eq = m.Groups[3].Value;

                if (key == "major")
                    lines[i] = $"{indent}{key}{eq}{major}";
                else if (key == "minor")
                    lines[i] = $"{indent}{key}{eq}{minor}";
                else if (key == "patch")
                    lines[i] = $"{indent}{key}{eq}{patch}";
                else if (key == "status")
                    lines[i] = $"{indent}{key}{eq}\"{status}\"";
            }

            File.WriteAllLines(vf, lines, System.Text.Encoding.UTF8);
            RefreshVersionPanel();
            ReinitBuildManager();

            UpdateStatus($"Version saved: {_lblVerDisplay.Text}", Color.Green);
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to save version.py:\n{ex.Message}", "Error",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  ADVANCED TAB
    // ═══════════════════════════════════════════════════════════

    private Control AdvancedPage()
    {
        var wrapper = new Panel { AutoScroll = true, Dock = DockStyle.Fill };
        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 3,
            Padding = new Padding(12, 12, 12, 4)
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));

        // Row 0: Repo Root
        AddLabel(layout, "Label.RepoRoot", 0);
        _txtRepoRoot = new TextBox
        {
            Dock = DockStyle.Fill,
            PlaceholderText = I18N.T("Placeholder.RepoRoot")
        };
        layout.Controls.Add(_txtRepoRoot, 1, 0);
        _btnBrowseRepo = new Button { Text = I18N.T("Button.Browse"), Tag = "i18n:Button.Browse", Width = 80 };
        _btnBrowseRepo.Click += (s, e) =>
        {
            BrowseFolder(_txtRepoRoot);
            ReinitBuildManager();
        };
        layout.Controls.Add(_btnBrowseRepo, 2, 0);

        // Row 1: UI Language
        AddLabel(layout, "Label.UILanguage", 1);
        var cbAppLang = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
        var appLangKeys = new[] { "zh_CN", "en", "ja", "ko", "fr", "de", "es", "pt_BR", "ru" };
        int currentIdx = 0;
        for (int i = 0; i < appLangKeys.Length; i++)
        {
            var display = I18N.T($"Lang.{appLangKeys[i]}");
            cbAppLang.Items.Add(display);
            if (appLangKeys[i] == I18N.CurrentLang)
                currentIdx = i;
        }
        cbAppLang.SelectedIndex = currentIdx;
        cbAppLang.Tag = "i18n:Label.UILanguage";
        cbAppLang.SelectedIndexChanged += (s, e) =>
        {
            var lang = appLangKeys[cbAppLang.SelectedIndex];
            if (lang == I18N.CurrentLang) return;
            I18N.Load(lang);
            I18N.SavePreference(lang);
        };
        layout.Controls.Add(cbAppLang, 1, 1);

        // Row 2: AI / Release settings section
        var aiSection = new Panel
        {
            AutoSize = true,
            Padding = new Padding(0, 16, 0, 4)
        };
        var aiTitle = new Label
        {
            Text = "── AI Release Summary + GitHub Publish ──",
            AutoSize = true,
            Font = new Font("Segoe UI", 10f, FontStyle.Bold),
            ForeColor = Color.FromArgb(80, 80, 80),
            Margin = new Padding(0, 0, 0, 8)
        };
        aiSection.Controls.Add(aiTitle);
        layout.Controls.Add(aiSection, 1, 2);
        layout.SetColumnSpan(aiSection, 2);

        var aiLayout = new TableLayoutPanel
        {
            AutoSize = true,
            ColumnCount = 3,
            Padding = new Padding(0, 4, 0, 0)
        };
        aiLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 140));
        aiLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        aiLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));

        // Use AI summary
        var chkUseAi = new CheckBox { Text = "Use AI to generate release summary", AutoSize = true, Checked = true };
        chkUseAi.CheckedChanged += (s, e) =>
        {
            // Update saved config immediately on edit
            if (string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()) || !Directory.Exists(_txtRepoRoot.Text.Trim()))
                return;
            var cfg = PublishConfig.Load(_txtRepoRoot.Text.Trim());
            cfg.UseAiSummary = chkUseAi.Checked;
            cfg.Save(_txtRepoRoot.Text.Trim());
        };
        aiLayout.Controls.Add(chkUseAi, 1, 0);

        // AI Base URL
        var txtAiBase = new TextBox { Dock = DockStyle.Fill, PlaceholderText = "http://127.0.0.1:4096/v1" };
        txtAiBase.Leave += (s, e) =>
        {
            if (string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()) || !Directory.Exists(_txtRepoRoot.Text.Trim()))
                return;
            var cfg = PublishConfig.Load(_txtRepoRoot.Text.Trim());
            cfg.AiBaseUrl = txtAiBase.Text.Trim();
            cfg.Save(_txtRepoRoot.Text.Trim());
        };
        var lblBase = new Label { Text = "AI Base URL", AutoSize = true, ForeColor = Color.Gray };
        aiLayout.Controls.Add(lblBase, 0, 1);
        aiLayout.Controls.Add(txtAiBase, 1, 1);

        // AI Model
        var txtAiModel = new TextBox { Dock = DockStyle.Fill, PlaceholderText = "mimocode-jundot / gpt-4.1-mini" };
        txtAiModel.Leave += (s, e) =>
        {
            if (string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()) || !Directory.Exists(_txtRepoRoot.Text.Trim()))
                return;
            var cfg = PublishConfig.Load(_txtRepoRoot.Text.Trim());
            cfg.AiModel = txtAiModel.Text.Trim();
            cfg.Save(_txtRepoRoot.Text.Trim());
        };
        var lblModel = new Label { Text = "AI Model", AutoSize = true, ForeColor = Color.Gray };
        aiLayout.Controls.Add(lblModel, 0, 2);
        aiLayout.Controls.Add(txtAiModel, 1, 2);

        // AI API Key
        var txtAiKey = new TextBox { Dock = DockStyle.Fill, PasswordChar = '*', PlaceholderText = "(optional, or set MIMOCODE_API_KEY env var)" };
        txtAiKey.Leave += (s, e) =>
        {
            if (string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()) || !Directory.Exists(_txtRepoRoot.Text.Trim()))
                return;
            var cfg = PublishConfig.Load(_txtRepoRoot.Text.Trim());
            cfg.AiApiKey = txtAiKey.Text.Trim();
            cfg.Save(_txtRepoRoot.Text.Trim());
        };
        var lblKey = new Label { Text = "AI API Key", AutoSize = true, ForeColor = Color.Gray };
        aiLayout.Controls.Add(lblKey, 0, 3);
        aiLayout.Controls.Add(txtAiKey, 1, 3);

        // GitHub Owner/Repo
        var txtGhOwner = new TextBox { Dock = DockStyle.Fill, PlaceholderText = "LoongSerpent9Realms" };
        var txtGhRepo = new TextBox { Dock = DockStyle.Fill, PlaceholderText = "Jundot" };
        void PersistGh()
        {
            if (string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()) || !Directory.Exists(_txtRepoRoot.Text.Trim()))
                return;
            var cfg = PublishConfig.Load(_txtRepoRoot.Text.Trim());
            cfg.Owner = string.IsNullOrWhiteSpace(txtGhOwner.Text) ? "LoongSerpent9Realms" : txtGhOwner.Text.Trim();
            cfg.Repo = string.IsNullOrWhiteSpace(txtGhRepo.Text) ? "Jundot" : txtGhRepo.Text.Trim();
            cfg.Save(_txtRepoRoot.Text.Trim());
        }
        txtGhOwner.Leave += (s, e) => PersistGh();
        txtGhRepo.Leave += (s, e) => PersistGh();
        var lblOwner = new Label { Text = "GitHub Owner", AutoSize = true, ForeColor = Color.Gray };
        var lblRepo = new Label { Text = "GitHub Repo", AutoSize = true, ForeColor = Color.Gray };
        aiLayout.Controls.Add(lblOwner, 0, 4);
        aiLayout.Controls.Add(txtGhOwner, 1, 4);
        aiLayout.Controls.Add(lblRepo, 0, 5);
        aiLayout.Controls.Add(txtGhRepo, 1, 5);

        var chkDraft = new CheckBox { Text = "Create GitHub release as Draft", AutoSize = true, Checked = false };
        var chkPrerelease = new CheckBox { Text = "Mark as Pre-release", AutoSize = true };
        void PersistReleaseFlags()
        {
            if (string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()) || !Directory.Exists(_txtRepoRoot.Text.Trim()))
                return;
            var cfg = PublishConfig.Load(_txtRepoRoot.Text.Trim());
            cfg.Draft = chkDraft.Checked;
            cfg.Prerelease = chkPrerelease.Checked;
            cfg.Save(_txtRepoRoot.Text.Trim());
        }
        chkDraft.CheckedChanged += (s, e) => PersistReleaseFlags();
        chkPrerelease.CheckedChanged += (s, e) => PersistReleaseFlags();

        aiLayout.Controls.Add(new Label { Text = "Release Flags", AutoSize = true, ForeColor = Color.Gray }, 0, 6);
        var flagsPanel = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
        flagsPanel.Controls.Add(chkDraft);
        flagsPanel.Controls.Add(chkPrerelease);
        aiLayout.Controls.Add(flagsPanel, 1, 6);

        // GitHub Token (password input) + Test button
        var tokenLayout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 3,
            RowCount = 1
        };
        tokenLayout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        tokenLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        tokenLayout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));

        var txtGhToken = new TextBox
        {
            Dock = DockStyle.Fill,
            PasswordChar = '*',
            PlaceholderText = "(ghp_... token) or leave blank to use GITHUB_TOKEN env var"
        };
        var lblToken = new Label { Text = "GitHub Token", AutoSize = true, ForeColor = Color.Gray, Margin = new Padding(0, 6, 0, 0) };

        var btnTestToken = new Button
        {
            Text = "Test",
            AutoSize = true,
            Margin = new Padding(4, 4, 0, 0)
        };

        async void BtnTestToken_Click(object? sender, EventArgs e)
        {
            var root = _txtRepoRoot.Text.Trim();
            if (string.IsNullOrEmpty(root) || !Directory.Exists(root))
            {
                MessageBox.Show("Set the repo root first.", "Missing Repo Root",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            btnTestToken.Enabled = false;
            btnTestToken.Text = "Testing...";
            try
            {
                // Persist the token first, then validate
                if (!string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()))
                {
                    var cfg = PublishConfig.Load(root);
                    cfg.Token = txtGhToken.Text.Trim();
                    cfg.Save(root);
                }

                var cfg2 = PublishConfig.Load(root);
                var publisher = new GitHubReleasePublisher(root, cfg2);
                var (ok, message) = await publisher.ValidateTokenAsync(CancellationToken.None);

                if (ok)
                {
                    AppendConsole($"[GitHub] Token OK — {message}", "success");
                    MessageBox.Show(message, "Token Valid",
                        MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                else
                {
                    AppendConsole($"[GitHub] Token failed: {message}", "error");
                    MessageBox.Show(message, "Token Invalid",
                        MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
            catch (Exception ex)
            {
                AppendConsole($"[GitHub] Test connection error: {ex.Message}", "error");
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                btnTestToken.Enabled = true;
                btnTestToken.Text = "Test";
            }
        }

        btnTestToken.Click += BtnTestToken_Click;

        void PersistGhToken()
        {
            if (string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()) || !Directory.Exists(_txtRepoRoot.Text.Trim()))
                return;
            var cfg = PublishConfig.Load(_txtRepoRoot.Text.Trim());
            cfg.Token = txtGhToken.Text.Trim();
            cfg.Save(_txtRepoRoot.Text.Trim());
        }
        txtGhToken.Leave += (s, e) => PersistGhToken();

        tokenLayout.Controls.Add(lblToken, 0, 0);
        tokenLayout.Controls.Add(txtGhToken, 1, 0);
        tokenLayout.Controls.Add(btnTestToken, 2, 0);

        aiLayout.Controls.Add(new Label { Text = "", AutoSize = true }, 0, 7); // spacer
        aiLayout.Controls.Add(tokenLayout, 1, 7);

        // Load current config into inputs
        void LoadPublishInputs()
        {
            if (string.IsNullOrEmpty(_txtRepoRoot.Text.Trim()) || !Directory.Exists(_txtRepoRoot.Text.Trim()))
                return;
            var cfg = PublishConfig.Load(_txtRepoRoot.Text.Trim());
            chkUseAi.Checked = cfg.UseAiSummary;
            txtAiBase.Text = cfg.AiBaseUrl;
            txtAiModel.Text = cfg.AiModel;
            txtAiKey.Text = cfg.AiApiKey;
            txtGhOwner.Text = cfg.Owner;
            txtGhRepo.Text = cfg.Repo;
            txtGhToken.Text = cfg.Token;
            chkDraft.Checked = cfg.Draft;
            chkPrerelease.Checked = cfg.Prerelease;
        }
        _txtRepoRoot.TextChanged += (s, e) => LoadPublishInputs();
        // Re-read on first paint
        LoadPublishInputs();

        layout.Controls.Add(aiLayout, 1, 3);
        layout.SetColumnSpan(aiLayout, 2);

        // Row 4: Info label
        var infoLabel = new Label
        {
            Text = "This tool wraps scripts/package-jundot.ps1 into a GUI.\n" +
                   "All settings mirror the original PowerShell script parameters.\n\n" +
                   "Requirements:\n" +
                   "  • Python 3.x with SCons\n" +
                   "  • Visual Studio 2022 (MSVC) or MinGW-w64 (Windows)\n" +
                   "  • .NET SDK (for C# / Mono builds)\n\n" +
                   "Publishing:\n" +
                   "  • Paste your GitHub token into the 'GitHub Token' field above,\n" +
                   "    or leave it blank to read from the GITHUB_TOKEN environment var.\n" +
                   "    Click 'Test' to verify the token has the 'repo' scope needed to\n" +
                   "    create releases and upload assets.\n" +
                   "  • 'AI Summary' uses MiMo-Code-jundot or any OpenAI-compatible\n" +
                   "    endpoint to auto-write release notes from build metadata.",
            AutoSize = true,
            Font = new Font("Segoe UI", 9f),
            ForeColor = Color.Gray,
            Padding = new Padding(0, 12, 0, 0)
        };
        layout.Controls.Add(infoLabel, 1, 4);
        layout.SetColumnSpan(infoLabel, 2);

        wrapper.Controls.Add(layout);
        return wrapper;
    }

    // ═══════════════════════════════════════════════════════════
    //  UI HELPERS
    // ═══════════════════════════════════════════════════════════

    private static void AddLabel(TableLayoutPanel layout, string text, int row)
    {
        var key = text;
        var label = new Label
        {
            Text = I18N.T(text),
            Tag = "i18n:" + text,
            TextAlign = ContentAlignment.MiddleLeft,
            AutoSize = true,
            Font = new Font("Segoe UI", 9f),
            Padding = new Padding(0, 6, 12, 6)
        };
        layout.Controls.Add(label, 0, row);
    }

    private static void AddHint(TableLayoutPanel layout, int row, string text)
    {
        var label = new Label
        {
            Text = I18N.T(text),
            Tag = "i18n:" + text,
            TextAlign = ContentAlignment.MiddleLeft,
            AutoSize = true,
            Font = new Font("Segoe UI", 8f),
            ForeColor = Color.Gray
        };
        layout.Controls.Add(label, 2, row);
    }

    private void BrowseFolder(TextBox target)
    {
        using var dlg = new FolderBrowserDialog
        {
            Description = "Select folder",
            ShowNewFolderButton = true
        };

        if (!string.IsNullOrWhiteSpace(target.Text) && Directory.Exists(target.Text))
            dlg.SelectedPath = target.Text;

        if (dlg.ShowDialog(this) == DialogResult.OK)
            target.Text = dlg.SelectedPath;
    }

    private void EnableBuildOptions(bool enabled)
    {
        _cbTarget.Enabled = enabled;
        _cbPlatform.Enabled = enabled;
        _cbArch.Enabled = enabled;
        _numJobs.Enabled = enabled;
        _cbScriptLang.Enabled = enabled;
        _chkUseMinGW.Enabled = enabled;
        _chkWinOptDeps.Enabled = enabled;
        _chkInstallSCons.Enabled = enabled;
        _chkCleanBuild.Enabled = enabled;
        _txtMingwPrefix.Enabled = enabled && _chkUseMinGW.Checked;
        _txtExtraSCons.Enabled = enabled;
    }

    private void UpdateMinGWVisibility()
    {
        var isWindows = _cbPlatform.SelectedItem?.ToString() == "windows";
        _chkUseMinGW.Visible = isWindows;
        _chkWinOptDeps.Visible = isWindows;
    }

    private void LoadDefaults()
    {
        // Try to detect the Jundot repo root
        var dir = AppContext.BaseDirectory;
        while (dir != null && !File.Exists(Path.Combine(dir, "SConstruct")))
        {
            var parent = Path.GetDirectoryName(dir);
            if (parent == dir) break;
            dir = parent;
        }

        if (dir != null && File.Exists(Path.Combine(dir, "SConstruct")))
        {
            _txtRepoRoot.Text = dir;
            _txtRepoRoot.PlaceholderText = dir;
            ReinitBuildManager();
        }

        // Restore last saved build config
        LoadConfig();
    }

    /// <summary>Get the path for build config persistence.</summary>
    private string GetConfigPath()
    {
        var root = _txtRepoRoot.Text.Trim();
        if (!string.IsNullOrEmpty(root) && Directory.Exists(root))
            return Path.Combine(root, ".build-config.json");
        // Fallback to app directory
        return Path.Combine(AppContext.BaseDirectory, ".build-config.json");
    }

    /// <summary>Save current UI settings to a JSON config file.</summary>
    private void SaveConfig()
    {
        try
        {
            var cfg = new BuildConfig
            {
                Target = _cbTarget.SelectedItem?.ToString() ?? "editor",
                PlatformName = _cbPlatform.SelectedItem?.ToString() ?? "windows",
                Arch = _cbArch.SelectedItem?.ToString() ?? "x86_64",
                Jobs = (int)_numJobs.Value,
                Mono = _cbScriptLang.SelectedIndex == 1,
                UseMinGW = _chkUseMinGW.Checked,
                EnableWindowsOptionalDeps = _chkWinOptDeps.Checked,
                SkipBuild = _chkSkipBuild.Checked,
                InstallSCons = _chkInstallSCons.Checked,
                CleanPackageDir = _chkCleanDir.Checked,
                CleanBuild = _chkCleanBuild.Checked,
                MingwPrefix = _txtMingwPrefix.Text.Trim(),
                PackageName = _txtPackageName.Text.Trim(),
                OutputDir = _txtOutputDir.Text.Trim(),
                LogDir = _txtLogDir.Text.Trim(),
                ExtraSConsArgs = _txtExtraSCons.Text.Trim(),
                RepoRoot = _txtRepoRoot.Text.Trim(),
                AutoUpdateVersion = _chkAutoVersion?.Checked ?? false,
                GenerateUpdateManifest = _chkGenManifest?.Checked ?? true
            };

            var json = System.Text.Json.JsonSerializer.Serialize(cfg, new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(GetConfigPath(), json, System.Text.Encoding.UTF8);
        }
        catch { /* best effort */ }
    }

    /// <summary>Load saved config and populate UI controls.</summary>
    private void LoadConfig()
    {
        try
        {
            var path = GetConfigPath();
            if (!File.Exists(path)) return;

            var json = File.ReadAllText(path, System.Text.Encoding.UTF8);
            var cfg = System.Text.Json.JsonSerializer.Deserialize<BuildConfig>(json);
            if (cfg == null) return;

            // Populate UI controls from saved config
            _cbTarget.SelectedItem = cfg.Target;
            _cbPlatform.SelectedItem = cfg.PlatformName;
            _cbArch.SelectedItem = cfg.Arch;
            _numJobs.Value = Math.Clamp(cfg.Jobs, 0, 64);
            _cbScriptLang.SelectedIndex = cfg.Mono ? 1 : 0;
            _chkUseMinGW.Checked = cfg.UseMinGW;
            _chkWinOptDeps.Checked = cfg.EnableWindowsOptionalDeps;
            _chkSkipBuild.Checked = cfg.SkipBuild;
            _chkInstallSCons.Checked = cfg.InstallSCons;
            _chkCleanDir.Checked = cfg.CleanPackageDir;
            _chkCleanBuild.Checked = cfg.CleanBuild;
            _txtMingwPrefix.Text = cfg.MingwPrefix;
            _txtPackageName.Text = cfg.PackageName;
            _txtOutputDir.Text = cfg.OutputDir;
            _txtLogDir.Text = cfg.LogDir;
            _txtExtraSCons.Text = cfg.ExtraSConsArgs;
            _txtRepoRoot.Text = cfg.RepoRoot;
            if (_chkAutoVersion != null)
                _chkAutoVersion.Checked = cfg.AutoUpdateVersion;
            if (_chkGenManifest != null)
                _chkGenManifest.Checked = cfg.GenerateUpdateManifest;

            // Re-init build manager if repo root changed
            ReinitBuildManager();
        }
        catch { /* best effort */ }
    }

    private void ReinitBuildManager()
    {
        var root = _txtRepoRoot.Text.Trim();
        if (!string.IsNullOrEmpty(root) && Directory.Exists(root) && File.Exists(Path.Combine(root, "SConstruct")))
        {
            _buildManager = new BuildManager(root);
            if (_currentPageIndex == 2)
                RefreshBuildList();
            if (_currentPageIndex == 1)
                RefreshVersionPanel();
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  BUILD LOGIC
    // ═══════════════════════════════════════════════════════════

    private async void BtnBuild_Click(object? sender, EventArgs e)
    {
        if (_isRunning) return;

        var cfg = new BuildConfig
        {
            Target = _cbTarget.SelectedItem?.ToString() ?? "editor",
            PlatformName = _cbPlatform.SelectedItem?.ToString() ?? "windows",
            Arch = _cbArch.SelectedItem?.ToString() ?? "x86_64",
            Jobs = (int)_numJobs.Value,
            Mono = _cbScriptLang.SelectedIndex == 1,
            UseMinGW = _chkUseMinGW.Checked,
            EnableWindowsOptionalDeps = _chkWinOptDeps.Checked,
            SkipBuild = _chkSkipBuild.Checked,
            InstallSCons = _chkInstallSCons.Checked,
            CleanPackageDir = _chkCleanDir.Checked,
            CleanBuild = _chkCleanBuild.Checked,
            MingwPrefix = _txtMingwPrefix.Text.Trim(),
            PackageName = _txtPackageName.Text.Trim(),
            OutputDir = _txtOutputDir.Text.Trim(),
            LogDir = _txtLogDir.Text.Trim(),
            ExtraSConsArgs = _txtExtraSCons.Text.Trim(),
            RepoRoot = _txtRepoRoot.Text.Trim(),
            AutoUpdateVersion = _chkAutoVersion?.Checked ?? false,
            GenerateUpdateManifest = _chkGenManifest?.Checked ?? true
        };

        // Pre-flight: target platform must match the host OS. Skip if the
        // user said "Skip build" — they may be packaging existing binaries
        // (but we still let them try).
        if (!cfg.SkipBuild)
        {
            var platformMismatch = cfg.PlatformName switch
            {
                "linuxbsd" => !OperatingSystem.IsLinux(),
                "macos" => !OperatingSystem.IsMacOS(),
                "windows" => !OperatingSystem.IsWindows(),
                "ios" => !OperatingSystem.IsMacOS(),
                _ => false
            };
            if (platformMismatch)
            {
                var host = OperatingSystem.IsWindows() ? "Windows"
                          : OperatingSystem.IsMacOS() ? "macOS"
                          : OperatingSystem.IsLinux() ? "Linux"
                          : "unknown";
                var msg = $"Target platform '{cfg.PlatformName}' cannot be built on this {host} host.\n\n" +
                          $"Switch Platform to 'windows' (or 'linuxbsd' / 'macos') or run on the matching OS.";
                AppendConsole(msg, "error");
                UpdateStatus("Build cancelled: platform mismatch.", Color.OrangeRed);
                MessageBox.Show(msg, "Platform Mismatch", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
        }

        // Persist config before building
        SaveConfig();

        _isRunning = true;
        SetRunningState(true);
        _rtbConsole.Clear();

        _cts = new CancellationTokenSource();
        _engine = new BuildEngine(cfg);
        _engine.BuildManager = _buildManager;
        _engine.ProgressChanged += OnBuildProgress;

        var success = await Task.Run(() => _engine.RunAsync(_cts.Token));

        _engine.ProgressChanged -= OnBuildProgress;
        SetRunningState(false);
        _isRunning = false;

        if (success)
        {
            RefreshBuildList();
            RefreshVersionPanel();
        }

        UpdateStatus(success ? "Build completed successfully!" : "Build failed or was cancelled.",
            success ? Color.Green : Color.OrangeRed);
    }

    private void BtnCancel_Click(object? sender, EventArgs e)
    {
        if (!_isRunning) return;
        _engine?.Cancel();
        _cts?.Cancel();
        UpdateStatus("Cancelling...", Color.Orange);
    }

    private void SetRunningState(bool running)
    {
        _btnBuild.Enabled = !running;
        _btnCancel.Enabled = running;
        _progressBar.Visible = running;
        _lblStatus.Visible = running;

        if (!running)
        {
            _btnBuild.BackColor = Color.FromArgb(0, 120, 212);
            _btnBuild.ForeColor = Color.White;
            _btnBuild.Text = I18N.T("Button.Build");
            _lblStatus.Text = "";
        }
        else
        {
            _btnBuild.BackColor = SystemColors.Control;
            _btnBuild.ForeColor = SystemColors.ControlText;
            _btnBuild.Text = I18N.T("Status.Building");

            // 初始为不确定进度（等待实际进度数据）
            _progressBar.Style = ProgressBarStyle.Marquee;
            _lblStatus.Text = "Initializing...";
            _lblStatus.ForeColor = Color.Gray;
        }
    }

    private void UpdateStatus(string text, Color color)
    {
        if (InvokeRequired)
        {
            Invoke(() => UpdateStatus(text, color));
            return;
        }

        _lblStatus.Text = text;
        _lblStatus.ForeColor = color;
    }

    private void OnBuildProgress(object? sender, BuildProgressEventArgs e)
    {
        if (InvokeRequired)
        {
            BeginInvoke(() => OnBuildProgress(sender, e));
            return;
        }

        if (e.Progress.HasValue)
        {
            _progressBar.Style = ProgressBarStyle.Continuous;
            _progressBar.Value = (int)Math.Round(e.Progress.Value * 100);
            _lblStatus.Text = $"{(int)Math.Round(e.Progress.Value * 100)}%";
        }
        else
        {
            // 无精确进度时显示当前阶段
            switch (e.MessageType)
            {
                case "step":
                    _lblStatus.Text = e.Message;
                    _lblStatus.ForeColor = Color.FromArgb(88, 166, 255);
                    break;
                case "warning":
                    _lblStatus.Text = e.Message;
                    _lblStatus.ForeColor = Color.FromArgb(210, 153, 34);
                    break;
                case "error":
                    _lblStatus.Text = e.Message;
                    _lblStatus.ForeColor = Color.FromArgb(255, 123, 114);
                    break;
                case "success":
                    _lblStatus.Text = e.Message;
                    _lblStatus.ForeColor = Color.FromArgb(63, 185, 80);
                    break;
                case "output":
                    // Ninja 输出行：提取 [N/M] 或文件名做状态提示
                    if (!e.Progress.HasValue && e.Message.StartsWith("["))
                    {
                        _lblStatus.Text = e.Message.Length > 60
                            ? e.Message.Substring(0, 60) + "..."
                            : e.Message;
                        _lblStatus.ForeColor = Color.FromArgb(201, 209, 217);
                    }
                    break;
            }
        }

        AppendConsole(e.Message, e.MessageType);
    }

    // ═══════════════════════════════════════════════════════════
    //  CONSOLE OUTPUT
    // ═══════════════════════════════════════════════════════════

    private void AppendConsole(string text, string type)
    {
        var color = type switch
        {
            "step" => Color.FromArgb(88, 166, 255),
            "success" => Color.FromArgb(63, 185, 80),
            "error" => Color.FromArgb(255, 123, 114),
            "warning" => Color.FromArgb(210, 153, 34),
            "info" => Color.FromArgb(201, 209, 217),
            _ => _rtbConsole.ForeColor
        };

        // Bold for step/success headers
        var fontStyle = type is "step" or "success" or "error" ? FontStyle.Bold : FontStyle.Regular;

        _rtbConsole.SelectionStart = _rtbConsole.TextLength;
        _rtbConsole.SelectionLength = 0;
        _rtbConsole.SelectionColor = color;
        _rtbConsole.SelectionFont = new Font(_rtbConsole.Font, fontStyle);
        _rtbConsole.AppendText(text + Environment.NewLine);
        _rtbConsole.ScrollToCaret();
    }

    // ═══════════════════════════════════════════════════════════
    //  BUILDS TAB
    // ═══════════════════════════════════════════════════════════

    private Control BuildsPage()
    {
        var panel = new Panel { Dock = DockStyle.Fill };

        // ── Toolbar ───────────────────────────────────────────
        var toolbar = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 36,
            Padding = new Padding(12, 4, 12, 0),
            WrapContents = false
        };

        _btnRefreshBuilds = new Button
        {
            Text = I18N.T("Button.Refresh"),
            Tag  = "i18n:Button.Refresh",
            Size = new Size(90, 28),
            FlatStyle = FlatStyle.Flat
        };
        _btnRefreshBuilds.Click += (s, e) => RefreshBuildList();
        toolbar.Controls.Add(_btnRefreshBuilds);

        _lblBuildCount = new Label
        {
            Text = "",
            AutoSize = true,
            Font = new Font("Segoe UI", 9f),
            ForeColor = Color.Gray,
            Padding = new Padding(12, 6, 0, 0)
        };
        toolbar.Controls.Add(_lblBuildCount);

        // ── ListView ──────────────────────────────────────────
        _lvBuilds = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            FullRowSelect = true,
            MultiSelect = true,
            GridLines = true,
            Font = new Font("Segoe UI", 9f)
        };
        _lvBuilds.Columns.Add(I18N.T("Col.Name"), 220);
        _lvBuilds.Columns.Add(I18N.T("Col.Version"), 80);
        _lvBuilds.Columns.Add(I18N.T("Col.Type"), 65);
        _lvBuilds.Columns.Add(I18N.T("Col.Arch"), 55);
        _lvBuilds.Columns.Add(I18N.T("Col.Script"), 75);
        _lvBuilds.Columns.Add(I18N.T("Col.Date"), 135);
        _lvBuilds.Columns.Add(I18N.T("Col.Size"), 70);
        _lvBuilds.Columns.Add(I18N.T("Col.Status"), 65);
        _lvBuilds.DoubleClick += LvBuilds_DoubleClick;
        _lvBuilds.MouseClick += LvBuilds_MouseClick;
        _lvBuilds.KeyDown += LvBuilds_KeyDown;
        _lvBuilds.SelectedIndexChanged += (s, e) => UpdateBuildActionButtons();

        // ── Action Buttons ────────────────────────────────────
        var actionBar = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            Height = 44,
            Padding = new Padding(12, 6, 12, 4),
            WrapContents = false
        };

        _btnLaunch = new Button
        {
            Text = I18N.T("Button.Launch"),
            Tag  = "i18n:Button.Launch",
            Size = new Size(100, 30),
            Enabled = false,
            FlatStyle = FlatStyle.Flat,
            BackColor = Color.FromArgb(0, 120, 212),
            ForeColor = Color.White
        };
        _btnLaunch.FlatAppearance.BorderSize = 0;
        _btnLaunch.Click += BtnLaunch_Click;
        actionBar.Controls.Add(_btnLaunch);

        _btnOpenFolder = new Button
        {
            Text = I18N.T("Button.OpenFolder"),
            Tag  = "i18n:Button.OpenFolder",
            Size = new Size(110, 30),
            Enabled = false,
            FlatStyle = FlatStyle.Flat
        };
        _btnOpenFolder.Click += BtnOpenFolder_Click;
        actionBar.Controls.Add(_btnOpenFolder);

        _btnViewLog = new Button
        {
            Text = I18N.T("Button.ViewLog"),
            Tag  = "i18n:Button.ViewLog",
            Size = new Size(100, 30),
            Enabled = false,
            FlatStyle = FlatStyle.Flat
        };
        _btnViewLog.Click += BtnViewLog_Click;
        actionBar.Controls.Add(_btnViewLog);

        _btnDeleteBuild = new Button
        {
            Text = I18N.T("Button.Delete"),
            Tag  = "i18n:Button.Delete",
            Size = new Size(90, 30),
            Enabled = false,
            FlatStyle = FlatStyle.Flat,
            ForeColor = Color.OrangeRed
        };
        _btnDeleteBuild.Click += BtnDeleteBuild_Click;
        actionBar.Controls.Add(_btnDeleteBuild);

        // ── AI + GitHub Release buttons ───────────────
        _btnGenerateAiSummary = new Button
        {
            Text = "AI Summary",
            Tag  = "i18n:Button.AiSummary",
            Size = new Size(110, 30),
            Enabled = false,
            FlatStyle = FlatStyle.Flat,
            BackColor = Color.FromArgb(120, 70, 200),
            ForeColor = Color.White
        };
        _btnGenerateAiSummary.FlatAppearance.BorderSize = 0;
        _btnGenerateAiSummary.Click += BtnGenerateAiSummary_Click;
        actionBar.Controls.Add(_btnGenerateAiSummary);

        _btnPublishToGitHub = new Button
        {
            Text = "Publish GitHub Release",
            Tag  = "i18n:Button.PublishToGitHub",
            Size = new Size(180, 30),
            Enabled = false,
            FlatStyle = FlatStyle.Flat,
            BackColor = Color.FromArgb(46, 160, 67),
            ForeColor = Color.White
        };
        _btnPublishToGitHub.FlatAppearance.BorderSize = 0;
        _btnPublishToGitHub.Click += BtnPublishToGitHub_Click;
        actionBar.Controls.Add(_btnPublishToGitHub);

        // ── Layout ────────────────────────────────────────────
        panel.Controls.Add(_lvBuilds);
        panel.Controls.Add(toolbar);
        panel.Controls.Add(actionBar);

        return panel;
    }

    // ──  Build List Logic  ─────────────────────────────────────

    private void RefreshBuildList()
    {
        if (_buildManager == null) return;

        _lvBuilds.Items.Clear();
        List<BuildRecord> builds;
        try
        {
            builds = _buildManager.GetAllBuilds();
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to scan builds:\n{ex.Message}", "Build Scan Error",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        foreach (var b in builds)
        {
            var nameDisplay = b.PackageName;
            // Prettify: jundot-4.7-beta-windows-editor-x86_64-20260606-142153-build → Jundot 4.7-beta Editor (x86_64)
            if (nameDisplay.StartsWith("jundot-"))
            {
                var parts = nameDisplay.Split('-');
                if (parts.Length >= 4)
                {
                    var verPart = parts[1];
                    var statusPart = "";
                    if (parts.Length >= 5 && parts[2] != "windows" && parts[2] != "linux" && parts[2] != "macos" && parts[2] != "android" && parts[2] != "ios" && parts[2] != "web")
                    {
                        // Has status like "beta"
                        statusPart = $"-{parts[2]}";
                        nameDisplay = $"Jundot {verPart}{statusPart} {char.ToUpper(parts[3][0]) + parts[3][1..]} ({parts[4]})";
                    }
                    else
                    {
                        nameDisplay = $"Jundot {verPart} {char.ToUpper(parts[2][0]) + parts[2][1..]} ({parts[3]})";
                    }
                    if (b.Mono) nameDisplay += " [Mono]";
                }
            }

            var item = new ListViewItem(nameDisplay)
            {
                Tag = b,
                BackColor = b.ExeExists ? Color.White : Color.FromArgb(255, 240, 240)
            };
            item.SubItems.Add(b.Version);
            item.SubItems.Add(b.Target);
            item.SubItems.Add(b.Arch);
            item.SubItems.Add(b.Mono ? "C# (Mono)" : "GDScript");
            item.SubItems.Add(b.CreatedAt.ToString("yyyy-MM-dd HH:mm:ss"));
            item.SubItems.Add(b.SizeDisplay);
            item.SubItems.Add(b.ExeExists ? I18N.T("Status.Ready") : I18N.T("Status.Missing"));
            _lvBuilds.Items.Add(item);
        }

        _lblBuildCount.Text = string.Format(I18N.T("BuildCount"), _lvBuilds.Items.Count);
        UpdateBuildActionButtons();
    }

    private void UpdateBuildActionButtons()
    {
        var selectedCount = _lvBuilds.SelectedItems.Count;
        var hasSelection = selectedCount > 0;
        var singleSelection = selectedCount == 1;
        var record = singleSelection ? (BuildRecord?)_lvBuilds.SelectedItems[0].Tag : null;
        var hasSinglePackage = singleSelection && !string.IsNullOrEmpty(record?.ZipPath) && File.Exists(record.ZipPath);

        // Multi-select publishing: every selected build must have a .zip file
        var allHavePackages = hasSelection && SelectedBuilds
            .All(r => !string.IsNullOrEmpty(r.ZipPath) && File.Exists(r.ZipPath));

        // Launch: only enable for editor targets. Template targets need
        // a .pck (project data) file to run – they are game runtimes, not
        // standalone tools.
        var isEditorTarget = string.Equals(record?.Target, "editor", StringComparison.OrdinalIgnoreCase) ||
                             string.Equals(record?.Target, "editor.dev", StringComparison.OrdinalIgnoreCase);
        _btnLaunch.Enabled = singleSelection && record?.ExeExists == true && isEditorTarget;
        _btnOpenFolder.Enabled = hasSelection;
        _btnViewLog.Enabled = singleSelection && !string.IsNullOrEmpty(record?.BuildLogPath) && File.Exists(record.BuildLogPath);
        _btnDeleteBuild.Enabled = hasSelection;
        _btnGenerateAiSummary.Enabled = hasSinglePackage;
        _btnPublishToGitHub.Enabled = allHavePackages;

        if (_btnPublishToGitHub.Enabled && selectedCount > 1)
        {
            var versions = SelectedBuilds
                .Select(r => r.Version)
                .Where(v => !string.IsNullOrEmpty(v))
                .Distinct()
                .ToList();

            _btnPublishToGitHub.Text = versions.Count == 1
                ? $"Publish {selectedCount} to GitHub (v{versions[0]})"
                : $"Publish {selectedCount} to GitHub";
        }
        else
        {
            _btnPublishToGitHub.Text = "Publish GitHub Release";
        }
    }

    private BuildRecord? SelectedBuild =>
        _lvBuilds.SelectedItems.Count > 0 ? (BuildRecord?)_lvBuilds.SelectedItems[0].Tag : null;

    private List<BuildRecord> SelectedBuilds =>
        _lvBuilds.SelectedItems
            .Cast<ListViewItem>()
            .Select(item => (BuildRecord?)item.Tag)
            .Where(record => record != null)
            .Cast<BuildRecord>()
            .ToList();

    // ──  Event Handlers  ────────────────────────────────────────

    private void LvBuilds_DoubleClick(object? sender, EventArgs e)
    {
        LaunchSelectedBuild();
    }

    private void LvBuilds_MouseClick(object? sender, MouseEventArgs e)
    {
        UpdateBuildActionButtons();

        if (e.Button == MouseButtons.Right && SelectedBuilds.Count > 0)
        {
            var ctxMenu = new ContextMenuStrip();
            ctxMenu.Items.Add("▶ Launch", null, (s, a) => LaunchSelectedBuild());
            ctxMenu.Items.Add("📂 Open Folder", null, (s, a) => OpenBuildFolder());
            ctxMenu.Items.Add("📄 View Log", null, (s, a) => ViewBuildLog());
            ctxMenu.Items.Add(new ToolStripSeparator());
            ctxMenu.Items.Add("🗑 Delete", null, (s, a) => DeleteSelectedBuild());
            ctxMenu.Show(_lvBuilds, e.Location);
        }
    }

    private void LvBuilds_KeyDown(object? sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Enter)
            LaunchSelectedBuild();
        else if (e.KeyCode == Keys.Delete)
            DeleteSelectedBuild();
    }

    private void BtnLaunch_Click(object? sender, EventArgs e) => LaunchSelectedBuild();
    private void BtnOpenFolder_Click(object? sender, EventArgs e) => OpenBuildFolder();
    private void BtnViewLog_Click(object? sender, EventArgs e) => ViewBuildLog();
    private void BtnDeleteBuild_Click(object? sender, EventArgs e) => DeleteSelectedBuild();
    private async void BtnGenerateAiSummary_Click(object? sender, EventArgs e) => await GenerateAiReleaseSummaryAsync();
    private async void BtnPublishToGitHub_Click(object? sender, EventArgs e) => await PublishSelectedBuildToGitHubAsync();

    // ──  AI Release Summary + GitHub Publish  ─────────────

    private async Task GenerateAiReleaseSummaryAsync()
    {
        var record = SelectedBuild;
        if (record == null || string.IsNullOrEmpty(record.ZipPath) || !File.Exists(record.ZipPath))
        {
            MessageBox.Show("Select a packaged build first (one that has a .zip and a manifest).",
                "Cannot Generate Summary", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        var repoRoot = _txtRepoRoot.Text.Trim();
        if (string.IsNullOrEmpty(repoRoot) || !Directory.Exists(repoRoot))
        {
            MessageBox.Show("Repo root not configured. Switch to the Advanced tab and set it.",
                "Missing Repo Root", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        var publishConfig = PublishConfig.Load(repoRoot);
        if (!publishConfig.UseAiSummary)
        {
            MessageBox.Show("AI summary is disabled in publish-config.json. Enable UseAiSummary and try again.",
                "AI Disabled", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        AppendConsole("--- AI Release Summary ---", "info");
        AppendConsole($"Target: {record.PackageName}  ({record.Version})", "info");
        publishConfig.ReleaseBody = "";

        var manifestPath = Path.ChangeExtension(record.ZipPath, ".json");
        if (!File.Exists(manifestPath))
        {
            // Fallback: look for manifest.json next to the zip, or in the package dir.
            var dir = Path.GetDirectoryName(record.ZipPath) ?? "";
            manifestPath = Path.Combine(dir, "manifest.json");
        }

        var evaluationPath = Path.Combine(repoRoot, "artifacts", "reports",
            $"{record.PackageName}-change-evaluation.md");
        evaluationPath = ChangeEvaluator.Evaluate(
            repoRoot,
            record.PackageName,
            record.Version ?? "?",
            manifestPath,
            publishConfig.Changelog) ?? evaluationPath;

        try
        {
            var summarizer = new AiReleaseSummarizer(publishConfig);
            summarizer.LogMessage += (s, e) => AppendConsole(e, "info");

            var body = await summarizer.SummarizeAsync(
                record.Version ?? "?",
                record.PackageName,
                evaluationPath,
                manifestPath,
                publishConfig.Changelog ?? "");

            if (string.IsNullOrWhiteSpace(body))
            {
                AppendConsole("AI returned an empty summary. Check endpoint / key.", "warning");
                return;
            }
            if (!BodyMentionsVersion(body, record.Version ?? ""))
            {
                AppendConsole($"AI summary did not mention current version {record.Version}; discarded to avoid stale release notes.", "warning");
                return;
            }

            // Persist into the publish config's ReleaseBody so the publish step re-uses it.
            publishConfig.ReleaseBody = body;
            publishConfig.Save(repoRoot);

            AppendConsole("AI summary generated and saved. Use 'Publish GitHub Release' next.", "success");

            // Also show it to the user in a read-only dialog.
            var previewForm = new Form
            {
                Text = "AI Generated Release Body",
                Size = new Size(780, 540),
                StartPosition = FormStartPosition.CenterParent
            };
            var tb = new TextBox
            {
                Multiline = true,
                ReadOnly = true,
                ScrollBars = ScrollBars.Both,
                Dock = DockStyle.Fill,
                Text = body,
                Font = new Font("Consolas", 10f)
            };
            previewForm.Controls.Add(tb);
            previewForm.ShowDialog(this);
        }
        catch (Exception ex)
        {
            AppendConsole($"AI summary failed: {ex.Message}", "error");
            MessageBox.Show(ex.Message, "AI Summary Error",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private async Task PublishSelectedBuildToGitHubAsync()
    {
        var records = SelectedBuilds;
        if (records.Count == 0)
        {
            MessageBox.Show("Select one or more packaged builds first.",
                "Cannot Publish", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        var invalid = records.FirstOrDefault(r =>
            string.IsNullOrEmpty(r.ZipPath) || !File.Exists(r.ZipPath));
        if (invalid != null)
        {
            MessageBox.Show($"Build '{invalid.PackageName} ({invalid.Version})' has no .zip file on disk.",
                "Cannot Publish", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        var repoRoot = _txtRepoRoot.Text.Trim();
        if (string.IsNullOrEmpty(repoRoot) || !Directory.Exists(repoRoot))
        {
            MessageBox.Show("Repo root not configured. Switch to the Advanced tab and set it.",
                "Missing Repo Root", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        var publishConfig = PublishConfig.Load(repoRoot);
        if (!string.IsNullOrWhiteSpace(publishConfig.ReleaseBody))
        {
            var selectedVersions = records.Select(r => r.Version ?? "").Where(v => !string.IsNullOrWhiteSpace(v)).Distinct().ToList();
            if (selectedVersions.Count != 1 || !BodyMentionsVersion(publishConfig.ReleaseBody, selectedVersions[0]))
            {
                AppendConsole("Saved release body belongs to another version; clearing it before publish.", "warning");
                publishConfig.ReleaseBody = "";
                publishConfig.Save(repoRoot);
            }
        }

        // Resolve manifest + evaluation paths for every record
        var manifestByRecord = new Dictionary<BuildRecord, string>();
        var evalByRecord = new Dictionary<BuildRecord, string>();
        foreach (var r in records)
        {
            var manifestPath = Path.ChangeExtension(r.ZipPath, ".json");
            if (!File.Exists(manifestPath))
            {
                var dir = Path.GetDirectoryName(r.ZipPath) ?? "";
                manifestPath = Path.Combine(dir, "manifest.json");
            }
            manifestByRecord[r] = manifestPath;

            var evaluationPath = Path.Combine(repoRoot, "artifacts", "reports",
                $"{r.PackageName}-change-evaluation.md");
            evalByRecord[r] = evaluationPath;
        }

        // Check: warn if multiple versions are selected (they cannot be collapsed into a single release).
        var versions = records
            .Select(r => r.Version ?? "?")
            .Distinct()
            .ToList();

        var askText = new StringBuilder();
        askText.AppendLine("Publish the following builds to a single GitHub Release?");
        askText.AppendLine();
        askText.AppendLine($"  Repo:    {publishConfig.Owner}/{publishConfig.Repo}");
        askText.AppendLine($"  Draft:   {(publishConfig.Draft ? "YES (not published)" : "NO (published immediately)")}");
        askText.AppendLine($"  Pre-release: {(publishConfig.Prerelease ? "YES" : "NO")}");
        if (versions.Count == 1)
        {
            var tag = string.IsNullOrEmpty(publishConfig.ReleaseTag)
                ? "v" + versions[0]
                : publishConfig.ReleaseTag;
            askText.AppendLine($"  Tag:     {tag}");
        }
        else
        {
            askText.AppendLine($"  ⚠ Versions: {string.Join(", ", versions)} — will be merged under one release.");
        }
        askText.AppendLine($"  AI Summary: {(publishConfig.UseAiSummary ? "ON" : "OFF")}");
        askText.AppendLine();
        askText.AppendLine("  Builds:");
        foreach (var r in records.Take(15))
            askText.AppendLine($"    - {r.PackageName} ({r.Version}) {r.Platform}/{r.Arch}");
        if (records.Count > 15)
            askText.AppendLine($"    (... plus {records.Count - 15} more)");
        askText.AppendLine();
        askText.Append("Proceed?");

        var ask = MessageBox.Show(
            askText.ToString(),
            "Confirm Release",
            MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2);

        if (ask != DialogResult.Yes) return;

        AppendConsole("--- Publish to GitHub Release ---", "info");
        AppendConsole($"Selected {records.Count} build(s).", "info");

        try
        {
            var publisher = new GitHubReleasePublisher(repoRoot, publishConfig);
            publisher.LogMessage += (s, e) => AppendConsole(e, "info");

            var ct = CancellationToken.None;

            var args = records.Select(r => (
                PackageName:       r.PackageName,
                Version:           r.Version ?? "1.0.0",
                ZipPath:           r.ZipPath,
                ManifestPath:      manifestByRecord[r],
                Platform:          r.Platform ?? "win",
                Arch:              r.Arch ?? "x64",
                ChangeEvaluationPath: evalByRecord[r]
            ));

            var ok = await publisher.PublishManyAsync(args, aiOverrideBody: null, ct);

            if (ok)
            {
                UpdateStatus("Release published successfully", Color.Green);
                AppendConsole($"Release posted to https://github.com/{publishConfig.Owner}/{publishConfig.Repo}/releases", "success");
            }
            else
            {
                UpdateStatus("Release publish failed (see console)", Color.OrangeRed);
            }
        }
        catch (Exception ex)
        {
            AppendConsole($"Publish failed: {ex.Message}", "error");
            MessageBox.Show(ex.Message, "Publish Error",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private static bool BodyMentionsVersion(string body, string version)
    {
        if (string.IsNullOrWhiteSpace(body) || string.IsNullOrWhiteSpace(version))
            return false;
        return body.Contains(version, StringComparison.OrdinalIgnoreCase) ||
               body.Contains($"v{version}", StringComparison.OrdinalIgnoreCase);
    }

    private void LaunchSelectedBuild()
    {
        if (_lvBuilds.SelectedItems.Count != 1) return;

        var record = SelectedBuild;
        if (record == null || string.IsNullOrEmpty(record.ExePath) || !File.Exists(record.ExePath))
        {
            MessageBox.Show("Executable not found on disk. It may have been moved or deleted.",
                "Cannot Launch", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        // Template targets (template_release, template_debug) are game
        // runtimes that need a project .pck file to run. They are not
        // standalone executables.
        var target = (record.Target ?? "").ToLowerInvariant();
        if (target.StartsWith("template_"))
        {
            MessageBox.Show(
                $"'{record.Target}' is a game runtime template and cannot be launched standalone.\n\n" +
                $"It requires a .pck project data file. Build 'editor' instead for a standalone editor,\n" +
                $"or export a project using this template to produce a runnable game.",
                "Cannot Launch Template", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        try
        {
            var psi = new System.Diagnostics.ProcessStartInfo(record.ExePath)
            {
                WorkingDirectory = Path.GetDirectoryName(record.ExePath) ?? "",
                UseShellExecute = true
            };
            System.Diagnostics.Process.Start(psi);
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to launch: {ex.Message}", "Error",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void OpenBuildFolder()
    {
        var records = SelectedBuilds;
        if (records.Count == 0) return;

        var openedAny = false;
        foreach (var record in records)
        {
            var dir = !string.IsNullOrEmpty(record.PackageDir) && Directory.Exists(record.PackageDir)
                ? record.PackageDir
                : Path.GetDirectoryName(record.ExePath);

            if (!string.IsNullOrEmpty(dir) && Directory.Exists(dir))
            {
                try
                {
                    System.Diagnostics.Process.Start("explorer", $"\"{dir}\"");
                    openedAny = true;
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Failed to open folder: {ex.Message}", "Error");
                }
            }
        }

        if (!openedAny)
        {
            MessageBox.Show("Folder not found.", "Cannot Open", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }

    private void ViewBuildLog()
    {
        if (_lvBuilds.SelectedItems.Count != 1) return;

        var record = SelectedBuild;
        if (record == null || string.IsNullOrEmpty(record.BuildLogPath) || !File.Exists(record.BuildLogPath))
        {
            MessageBox.Show("Build log not found.", "Cannot View", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        try
        {
            System.Diagnostics.Process.Start("notepad", $"\"{record.BuildLogPath}\"");
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to open log: {ex.Message}", "Error");
        }
    }

    private void DeleteSelectedBuild()
    {
        var records = SelectedBuilds;
        if (records.Count == 0) return;

        var msg = records.Count == 1
            ? $"Delete build:\n\n{records[0].PackageName}\n\n"
            : $"Delete {records.Count} builds:\n\n{string.Join("\n", records.Select(r => $"  - {r.PackageName}"))}\n\n";
        msg += "This will remove the package folder, zip, and logs.\n" +
               "The bin/ executable will be kept.\n\nContinue?";

        var result = MessageBox.Show(msg, "Confirm Delete",
            MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2);

        if (result != DialogResult.Yes) return;

        try
        {
            foreach (var record in records)
            {
                _buildManager?.DeleteBuild(record, keepExe: true);
            }
            RefreshBuildList();
            UpdateStatus(records.Count == 1 ? $"Deleted: {records[0].PackageName}" : $"Deleted {records.Count} builds", Color.Orange);
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to delete: {ex.Message}", "Error",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  AUTO UPDATE
    // ═══════════════════════════════════════════════════════════

    /// <summary>Get the current application version from the assembly.</summary>
    private static string GetAppVersion()
    {
        try
        {
            var ver = Assembly.GetExecutingAssembly().GetName().Version;
            return ver != null ? $"{ver.Major}.{ver.Minor}.{ver.Build}" : "1.0.0";
        }
        catch
        {
            return "1.0.0";
        }
    }

    /// <summary>Set the window title to include the version number.</summary>
    private void SetTitleWithVersion()
    {
        var baseTitle = I18N.T("Title");
        var version = GetAppVersion();
        Text = $"{baseTitle}  v{version}";
    }

    /// <summary>
    /// Called when the form is first shown. Kicks off the background
    /// update check (non-blocking, silent on error).
    /// </summary>
    private async void OnShown(object? sender, EventArgs e)
    {
        // Only check once per session
        Shown -= OnShown;

        try
        {
            await CheckForUpdatesAsync();
        }
        catch
        {
            // Silently ignore — update check failures should not disturb the user
        }
    }

    /// <summary>
    /// Background update check flow:
    /// 1. Query GitHub Releases for latest version
    /// 2. If newer, show dialog asking whether to install
    /// 3. If confirmed, download and trigger self-replacing install
    /// </summary>
    private async Task CheckForUpdatesAsync()
    {
        var currentVersion = GetAppVersion();

        // Write a brief note to the console
        AppendConsole(I18N.T("Update.Checking"), "info");

        var result = await UpdateChecker.CheckForUpdateAsync(currentVersion);

        if (result.HasUpdate && !string.IsNullOrEmpty(result.DownloadUrl))
        {
            // Show update dialog
            var message = string.Format(
                I18N.T("Update.NewVersion"),
                result.LatestVersion ?? "?",
                currentVersion);
            var caption = I18N.T("Update.Available");

            var dialogResult = MessageBox.Show(
                this,
                message,
                caption,
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Information,
                MessageBoxDefaultButton.Button1);

            if (dialogResult != DialogResult.Yes)
                return;

            // Download and install
            await DownloadAndApplyUpdateAsync(result.DownloadUrl);
        }
        else if (!string.IsNullOrEmpty(result.Error))
        {
            // Log the error to console but don't bother the user
            AppendConsole(
                string.Format(I18N.T("Update.Failed"), result.Error),
                "warning");
        }
        // else: no update — nothing to do (silent)
    }

    /// <summary>Download the update package and trigger self-replacing install.</summary>
    private async Task DownloadAndApplyUpdateAsync(string downloadUrl)
    {
        var exeDir = AppContext.BaseDirectory;

        try
        {
            // Show progress in console
            UpdateStatus(I18N.T("Update.Downloading").Replace("{0}", "0"), Color.DodgerBlue);

            await UpdateChecker.DownloadAndInstallAsync(
                downloadUrl,
                exeDir,
                progress =>
                {
                    // Update status on UI thread
                    if (InvokeRequired)
                    {
                        BeginInvoke(() => UpdateStatus(
                            I18N.T("Update.Downloading").Replace("{0}", progress.ToString()),
                            Color.DodgerBlue));
                    }
                    else
                    {
                        UpdateStatus(
                            I18N.T("Update.Downloading").Replace("{0}", progress.ToString()),
                            Color.DodgerBlue);
                    }
                });

            // Notify the user
            AppendConsole(I18N.T("Update.InstallingDesc"), "success");
            UpdateStatus(I18N.T("Update.Installing"), Color.Green);

            // Give the user a moment to see the message, then exit
            await Task.Delay(1500);
            Application.Exit();
        }
        catch (Exception ex)
        {
            var msg = string.Format(I18N.T("Update.DownloadFailed"), ex.Message);
            AppendConsole(msg, "error");
            MessageBox.Show(this, msg, I18N.T("Update.Error"),
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
