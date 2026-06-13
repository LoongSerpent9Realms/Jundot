using System.ComponentModel;
using System.Reflection;

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

        // Row 2: Info label
        var infoLabel = new Label
        {
            Text = "This tool wraps scripts/package-jundot.ps1 into a GUI.\n" +
                   "All settings mirror the original PowerShell script parameters.\n\n" +
                   "Requirements:\n" +
                   "  • Python 3.x with SCons\n" +
                   "  • Visual Studio 2022 (MSVC) or MinGW-w64 (Windows)\n" +
                   "  • .NET SDK (for C# / Mono builds)",
            AutoSize = true,
            Font = new Font("Segoe UI", 9f),
            ForeColor = Color.Gray,
            Padding = new Padding(0, 12, 0, 0)
        };
        layout.Controls.Add(infoLabel, 1, 2);
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

        // Persist config before building
        SaveConfig();

        _isRunning = true;
        SetRunningState(true);
        _rtbConsole.Clear();

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

        _btnLaunch.Enabled = singleSelection && record?.ExeExists == true;
        _btnOpenFolder.Enabled = hasSelection;
        _btnViewLog.Enabled = singleSelection && !string.IsNullOrEmpty(record?.BuildLogPath) && File.Exists(record.BuildLogPath);
        _btnDeleteBuild.Enabled = hasSelection;
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
