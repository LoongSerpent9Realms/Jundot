using Jundot;
using JundotTools.Core;
using JundotTools.Export;
using JundotTools.Utils;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using JundotTools.Build;
using JundotTools.Ides;
using JundotTools.Ides.Rider;
using JundotTools.Inspector;
using JundotTools.Internals;
using JundotTools.ProjectEditor;
using JetBrains.Annotations;
using static JundotTools.Internals.Globals;
using Environment = System.Environment;
using File = JundotTools.Utils.File;
using OS = JundotTools.Utils.OS;
using Path = System.IO.Path;

namespace JundotTools
{
    public partial class JundotSharpEditor : EditorPlugin, ISerializationListener
    {
        public static class Settings
        {
            public const string ExternalEditor = "dotnet/editor/external_editor";
            public const string IdePathOptional = "dotnet/editor/ide_path_optional";
            public const string CustomExecPath = "dotnet/editor/custom_exec_path";
            public const string CustomExecPathArgs = "dotnet/editor/custom_exec_path_args";
            public const string VerbosityLevel = "dotnet/build/verbosity_level";
            public const string NoConsoleLogging = "dotnet/build/no_console_logging";
            public const string CreateBinaryLog = "dotnet/build/create_binary_log";
            public const string ProblemsLayout = "dotnet/build/problems_layout";
        }

#nullable disable
        private EditorSettings _editorSettings;

        private PopupMenu _menuPopup;

        private AcceptDialog _errorDialog;
        private ConfirmationDialog _confirmCreateSlnDialog;

        private Button _bottomPanelBtn;
        private Button _toolBarBuildButton;
        private Button _toolBarOpenIdeButton;
        private PopupMenu _openIdePopup;

        // TODO Use WeakReference once we have proper serialization.
        private WeakRef _exportPluginWeak;
        private WeakRef _inspectorPluginWeak;

        public JundotIdeManager JundotIdeManager { get; private set; }

        public MSBuildPanel MSBuildPanel { get; private set; }
#nullable enable

        public bool SkipBuildBeforePlaying { get; set; } = false;

        [UsedImplicitly]
        private bool CreateProjectSolutionIfNeeded()
        {
            if (!File.Exists(JundotSharpDirs.ProjectSlnPath) || !File.Exists(JundotSharpDirs.ProjectCsProjPath))
            {
                return CreateProjectSolution();
            }

            return true;
        }

        private bool CreateProjectSolution()
        {
            string? errorMessage = null;
            using (var pr = new EditorProgress("create_csharp_solution", "Generating solution...".TTR(), 2))
            {
                pr.Step("Generating C# project...".TTR());

                string csprojDir = Path.GetDirectoryName(JundotSharpDirs.ProjectCsProjPath)!;
                string slnDir = Path.GetDirectoryName(JundotSharpDirs.ProjectSlnPath)!;
                string name = JundotSharpDirs.ProjectAssemblyName;
                string guid = CsProjOperations.GenerateGameProject(csprojDir, name);

                if (guid.Length > 0)
                {
                    var solution = new DotNetSolution(name, slnDir);

                    var projectInfo = new DotNetSolution.ProjectInfo(guid,
                        Path.GetRelativePath(slnDir, JundotSharpDirs.ProjectCsProjPath),
                        new List<string> { "Debug", "ExportDebug", "ExportRelease" });

                    solution.AddNewProject(name, projectInfo);

                    try
                    {
                        solution.Save();
                    }
                    catch (IOException e)
                    {
                        errorMessage = "Failed to save solution. Exception message: ".TTR() + e.Message;
                    }
                }
                else
                {
                    errorMessage = "Failed to create C# project.".TTR();
                }
            }

            if (!string.IsNullOrEmpty(errorMessage))
            {
                ShowErrorDialog(errorMessage);
                return false;
            }

            _ShowDotnetFeatures();
            return true;
        }

        private void _ShowDotnetFeatures()
        {
            MSBuildPanel.Open();
            _toolBarBuildButton.Show();
        }

        private void _MenuOptionPressed(long id)
        {
            switch ((MenuOptions)id)
            {
                case MenuOptions.CreateSln:
                {
                    if (File.Exists(JundotSharpDirs.ProjectSlnPath) || File.Exists(JundotSharpDirs.ProjectCsProjPath))
                    {
                        ShowConfirmCreateSlnDialog();
                    }
                    else
                    {
                        CreateProjectSolution();
                    }
                    break;
                }
                case MenuOptions.OpenSln:
                    OpenProjectInExternalEditor(_editorSettings.GetSetting(Settings.ExternalEditor).As<ExternalEditorId>());
                    break;
                case MenuOptions.OpenSlnLocal:
                    OpenProjectInExternalEditor(_editorSettings.GetSetting(Settings.ExternalEditor).As<ExternalEditorId>(), useConfiguredPath: false);
                    break;
                case MenuOptions.OpenSlnInVisualStudio:
                    OpenProjectInExternalEditor(OS.IsMacOS ? ExternalEditorId.VisualStudioForMac : ExternalEditorId.VisualStudio);
                    break;
                case MenuOptions.OpenSlnInVsCode:
                    OpenProjectInExternalEditor(ExternalEditorId.VsCode);
                    break;
                case MenuOptions.OpenSlnInRider:
                    OpenProjectInExternalEditor(ExternalEditorId.Rider);
                    break;
                case MenuOptions.OpenSlnInFleet:
                    OpenProjectInExternalEditor(ExternalEditorId.Fleet);
                    break;
                case MenuOptions.OpenSlnInMonoDevelop:
                    OpenProjectInExternalEditor(ExternalEditorId.MonoDevelop);
                    break;
                default:
                    throw new ArgumentOutOfRangeException(nameof(id), id, "Invalid menu option");
            }
        }

        private bool EnsureProjectSolution()
        {
            if (File.Exists(JundotSharpDirs.ProjectSlnPath) && File.Exists(JundotSharpDirs.ProjectCsProjPath))
                return true;

            if (File.Exists(JundotSharpDirs.ProjectCsProjPath) && !File.Exists(JundotSharpDirs.ProjectSlnPath))
                return CreateSolutionForExistingProject();

            if (File.Exists(JundotSharpDirs.ProjectSlnPath) && !File.Exists(JundotSharpDirs.ProjectCsProjPath))
            {
                ShowErrorDialog("C# solution exists, but the C# project file is missing. Use Create C# solution to rebuild both files.".TTR());
                return false;
            }

            return CreateProjectSolution();
        }

        private bool CreateSolutionForExistingProject()
        {
            string? errorMessage = null;
            string slnDir = Path.GetDirectoryName(JundotSharpDirs.ProjectSlnPath)!;
            string name = JundotSharpDirs.ProjectAssemblyName;

            var solution = new DotNetSolution(name, slnDir);
            var projectInfo = new DotNetSolution.ProjectInfo(
                Guid.NewGuid().ToString().ToUpperInvariant(),
                Path.GetRelativePath(slnDir, JundotSharpDirs.ProjectCsProjPath),
                new List<string> { "Debug", "ExportDebug", "ExportRelease" });

            solution.AddNewProject(name, projectInfo);

            try
            {
                solution.Save();
            }
            catch (IOException e)
            {
                errorMessage = "Failed to save solution. Exception message: ".TTR() + e.Message;
            }

            if (!string.IsNullOrEmpty(errorMessage))
            {
                ShowErrorDialog(errorMessage);
                return false;
            }

            _ShowDotnetFeatures();
            return true;
        }

        private Error OpenProjectInExternalEditor(ExternalEditorId editorId, bool useConfiguredPath = true)
        {
            if (!EnsureProjectSolution())
                return Error.Failed;

            switch (editorId)
            {
                case ExternalEditorId.None:
                    ShowErrorDialog("No C# external editor is selected. Choose a specific editor from the C# menu or set one in Editor Settings.".TTR());
                    return Error.Unavailable;
                case ExternalEditorId.CustomEditor:
                {
                    string project = Path.GetDirectoryName(JundotSharpDirs.ProjectSlnPath)!;
                    var execCommand = _editorSettings.GetSetting(Settings.CustomExecPath).As<string>();
                    var execArgs = _editorSettings.GetSetting(Settings.CustomExecPathArgs).As<string>();
                    var args = new List<string>();

                    if (!string.IsNullOrEmpty(execArgs))
                    {
                        args.AddRange(ParseExternalEditorArgs(execArgs, project, JundotSharpDirs.ProjectSlnPath, -1, -1, out bool hasFileFlag));
                        if (!hasFileFlag)
                        {
                            args.Add(JundotSharpDirs.ProjectSlnPath);
                        }
                    }
                    else
                    {
                        args.Add(JundotSharpDirs.ProjectSlnPath);
                    }

                    OS.RunProcess(execCommand, args);
                    return Error.Ok;
                }
                case ExternalEditorId.VisualStudio:
                {
                    if (!OS.IsWindows)
                    {
                        ShowErrorDialog("Visual Studio project opening is only available on Windows. Use Visual Studio Code, Rider, Fleet, MonoDevelop, or a custom editor on this platform.".TTR());
                        return Error.Unavailable;
                    }

                    string configuredPath = GetConfiguredIdePath(editorId);
                    if (useConfiguredPath && !string.IsNullOrEmpty(configuredPath) && File.Exists(configuredPath))
                    {
                        OS.RunProcess(configuredPath, new[] { JundotSharpDirs.ProjectSlnPath });
                        return Error.Ok;
                    }

                    var args = new List<string>
                    {
                        Path.Combine(JundotSharpDirs.DataEditorToolsDir, "JundotTools.OpenVisualStudio.dll"),
                        JundotSharpDirs.ProjectSlnPath
                    };

                    string command = DotNetFinder.FindDotNetExe() ?? "dotnet";
                    OS.RunProcess(command, args);
                    return Error.Ok;
                }
                case ExternalEditorId.VisualStudioForMac:
                case ExternalEditorId.MonoDevelop:
                    _ = JundotIdeManager.LaunchIdeAsync(editorId);
                    return Error.Ok;
                case ExternalEditorId.VsCode:
                    return OpenProjectInVsCode(useConfiguredPath);
                case ExternalEditorId.Rider:
                case ExternalEditorId.Fleet:
                    RiderPathManager.InitializeIfNeeded(editorId);
                    RiderPathManager.OpenProject(editorId, JundotSharpDirs.ProjectSlnPath, useConfiguredPath);
                    return Error.Ok;
                default:
                    throw new ArgumentOutOfRangeException(nameof(editorId), editorId, "Invalid external editor");
            }
        }

        private string GetConfiguredIdePath(ExternalEditorId editorId)
        {
            if (!_editorSettings.HasSetting(Settings.IdePathOptional))
                return string.Empty;

            string configuredPath = _editorSettings.GetSetting(Settings.IdePathOptional).As<string>();
            if (string.IsNullOrEmpty(configuredPath) || !ConfiguredIdePathMatches(editorId, configuredPath))
                return string.Empty;

            return configuredPath;
        }

        private static bool ConfiguredIdePathMatches(ExternalEditorId editorId, string configuredPath)
        {
            string fileName = Path.GetFileName(configuredPath).ToLowerInvariant();

            switch (editorId)
            {
                case ExternalEditorId.VisualStudio:
                case ExternalEditorId.VisualStudioForMac:
                    return fileName.Contains("devenv", StringComparison.OrdinalIgnoreCase) ||
                           fileName.Contains("visualstudio", StringComparison.OrdinalIgnoreCase) ||
                           fileName.Contains("visual studio", StringComparison.OrdinalIgnoreCase);
                case ExternalEditorId.VsCode:
                    return VsCodeNames.Any(name => fileName.Contains(name, StringComparison.OrdinalIgnoreCase)) ||
                           fileName.Contains("code", StringComparison.OrdinalIgnoreCase);
                case ExternalEditorId.Rider:
                    return fileName.Contains("rider", StringComparison.OrdinalIgnoreCase);
                case ExternalEditorId.Fleet:
                    return fileName.Contains("fleet", StringComparison.OrdinalIgnoreCase);
                case ExternalEditorId.MonoDevelop:
                    return fileName.Contains("monodevelop", StringComparison.OrdinalIgnoreCase);
                case ExternalEditorId.CustomEditor:
                    return true;
                case ExternalEditorId.None:
                default:
                    return false;
            }
        }

        private Error OpenProjectInVsCode(bool useConfiguredPath = true)
        {
            string configuredPath = GetConfiguredIdePath(ExternalEditorId.VsCode);
            if (useConfiguredPath && !string.IsNullOrEmpty(configuredPath))
            {
                if (!File.Exists(configuredPath))
                {
                    GD.PushError($"Configured IDE path does not exist: {configuredPath}");
                    return Error.FileNotFound;
                }

                OS.RunProcess(configuredPath, new[] { Path.GetDirectoryName(JundotSharpDirs.ProjectSlnPath)! });
                return Error.Ok;
            }

            if (string.IsNullOrEmpty(_vsCodePath) || !File.Exists(_vsCodePath))
            {
                _vsCodePath = VsCodeNames.SelectFirstNotNull(OS.PathWhich, orElse: string.Empty);
            }

            var args = new List<string>();
            string command;
            bool macOSAppBundleInstalled = false;

            if (OS.IsMacOS)
            {
                const string vscodeBundleId = "com.microsoft.VSCode";
                macOSAppBundleInstalled = Internal.IsMacOSAppBundleInstalled(vscodeBundleId);

                if (macOSAppBundleInstalled)
                {
                    args.Add("-b");
                    args.Add(vscodeBundleId);
                    args.Add("-n");
                    args.Add("--wait-apps");
                    args.Add("--args");
                }

                if (!macOSAppBundleInstalled)
                {
                    const string vscodiumBundleId = "com.vscodium.codium";
                    macOSAppBundleInstalled = Internal.IsMacOSAppBundleInstalled(vscodiumBundleId);

                    if (macOSAppBundleInstalled)
                    {
                        args.Add("-b");
                        args.Add(vscodiumBundleId);
                        args.Add("-n");
                        args.Add("--wait-apps");
                        args.Add("--args");
                    }
                }

                if (!macOSAppBundleInstalled && string.IsNullOrEmpty(_vsCodePath))
                {
                    GD.PushError("Cannot find code editor: Visual Studio Code or VSCodium");
                    return Error.FileNotFound;
                }

                command = macOSAppBundleInstalled ? "/usr/bin/open" : _vsCodePath;
            }
            else
            {
                if (string.IsNullOrEmpty(_vsCodePath))
                {
                    GD.PushError("Cannot find code editor: Visual Studio Code or VSCodium");
                    return Error.FileNotFound;
                }

                command = _vsCodePath;
            }

            args.Add(Path.GetDirectoryName(JundotSharpDirs.ProjectSlnPath)!);
            OS.RunProcess(command, args);
            return Error.Ok;
        }

        private static List<string> ParseExternalEditorArgs(string execArgs, string project, string file, int line, int col, out bool hasFileFlag)
        {
            execArgs = execArgs.ReplaceN("{line}", line.ToString(CultureInfo.InvariantCulture));
            execArgs = execArgs.ReplaceN("{col}", col.ToString(CultureInfo.InvariantCulture));
            execArgs = execArgs.StripEdges(true, true);
            execArgs = execArgs.Replace("\\\\", "\\", StringComparison.Ordinal);

            var args = new List<string>();
            var from = 0;
            var numChars = 0;
            var insideQuotes = false;
            hasFileFlag = false;

            for (int i = 0; i < execArgs.Length; ++i)
            {
                if ((execArgs[i] == '"' && (i == 0 || execArgs[i - 1] != '\\')) && i != execArgs.Length - 1)
                {
                    if (!insideQuotes)
                    {
                        from++;
                    }
                    insideQuotes = !insideQuotes;
                }
                else if ((execArgs[i] == ' ' && !insideQuotes) || i == execArgs.Length - 1)
                {
                    if (i == execArgs.Length - 1 && !insideQuotes)
                    {
                        numChars++;
                    }

                    var arg = execArgs.Substr(from, numChars);
                    if (arg.Contains("{file}", StringComparison.OrdinalIgnoreCase))
                    {
                        hasFileFlag = true;
                    }

                    arg = arg.ReplaceN("{project}", project);
                    arg = arg.ReplaceN("{file}", file);
                    args.Add(arg);

                    from = i + 1;
                    numChars = 0;
                }
                else
                {
                    numChars++;
                }
            }

            return args;
        }

        private void BuildProjectPressed()
        {
            if (!File.Exists(JundotSharpDirs.ProjectCsProjPath))
            {
                if (!CreateProjectSolution())
                    return; // Failed to create project.
            }

            Instance.MSBuildPanel.BuildProject();
        }

        private enum MenuOptions
        {
            CreateSln,
            OpenSln,
            OpenSlnLocal,
            OpenSlnInVisualStudio,
            OpenSlnInVsCode,
            OpenSlnInRider,
            OpenSlnInFleet,
            OpenSlnInMonoDevelop,
        }

        public void ShowErrorDialog(string message, string title = "Error")
        {
            _errorDialog.Title = title;
            _errorDialog.DialogText = message;
            EditorInterface.Singleton.PopupDialogCentered(_errorDialog);
        }

        public void ShowConfirmCreateSlnDialog()
        {
            _confirmCreateSlnDialog.Title = "Create C# solution".TTR();
            _confirmCreateSlnDialog.DialogText = "C# solution already exists. This will override the existing C# project file, any manual changes will be lost.".TTR();
            EditorInterface.Singleton.PopupDialogCentered(_confirmCreateSlnDialog);
        }

        private static string _vsCodePath = string.Empty;

        private static readonly string[] VsCodeNames =
        {
            "code", "code-oss", "vscode", "vscode-oss", "visual-studio-code", "visual-studio-code-oss", "codium"
        };

        private void OpenSelectedIdePressed()
        {
            OpenProjectInExternalEditor(_editorSettings.GetSetting(Settings.ExternalEditor).As<ExternalEditorId>());
        }

        private void OpenIdeButtonGuiInput(InputEvent @event)
        {
            var mouseButton = @event as InputEventMouseButton;
            if (mouseButton == null)
                return;

            if (mouseButton.ButtonIndex != MouseButton.Right || !mouseButton.Pressed)
                return;

            _openIdePopup.Position = (Vector2I)(_toolBarOpenIdeButton.GetScreenPosition() + new Vector2(0, _toolBarOpenIdeButton.Size.Y));
            _openIdePopup.Popup();
        }

        [UsedImplicitly]
        public Error OpenInExternalEditor(Script script, int line, int col)
        {
            var editorId = _editorSettings.GetSetting(Settings.ExternalEditor).As<ExternalEditorId>();

            switch (editorId)
            {
                case ExternalEditorId.None:
                    // Not an error. Tells the caller to fallback to the global external editor settings or the built-in editor.
                    return Error.Unavailable;
                case ExternalEditorId.CustomEditor:
                {
                    string file = ProjectSettings.GlobalizePath(script.ResourcePath);
                    string project = ProjectSettings.GlobalizePath("res://");
                    // Since ProjectSettings.GlobalizePath replaces only "res:/", leaving a trailing slash, it is removed here.
                    project = project[..^1];
                    var execCommand = _editorSettings.GetSetting(Settings.CustomExecPath).As<string>();
                    var execArgs = _editorSettings.GetSetting(Settings.CustomExecPathArgs).As<string>();
                    var args = ParseExternalEditorArgs(execArgs, project, file, line, col, out bool hasFileFlag);

                    if (!hasFileFlag)
                    {
                        args.Add(file);
                    }

                    OS.RunProcess(execCommand, args);

                    break;
                }
                case ExternalEditorId.VisualStudio:
                {
                    string scriptPath = ProjectSettings.GlobalizePath(script.ResourcePath);

                    var args = new List<string>
                    {
                        Path.Combine(JundotSharpDirs.DataEditorToolsDir, "JundotTools.OpenVisualStudio.dll"),
                        JundotSharpDirs.ProjectSlnPath,
                        line >= 0 ? $"{scriptPath};{line + 1};{col + 1}" : scriptPath
                    };

                    string command = DotNetFinder.FindDotNetExe() ?? "dotnet";

                    try
                    {
                        if (Jundot.OS.IsStdOutVerbose())
                            Console.WriteLine(
                                $"Running: \"{command}\" {string.Join(" ", args.Select(a => $"\"{a}\""))}");

                        OS.RunProcess(command, args);
                    }
                    catch (Exception e)
                    {
                        GD.PushError(
                            $"Error when trying to run code editor: VisualStudio. Exception message: '{e.Message}'");
                    }

                    break;
                }
                case ExternalEditorId.VisualStudioForMac:
                    goto case ExternalEditorId.MonoDevelop;
                case ExternalEditorId.Rider:
                case ExternalEditorId.Fleet:
                {
                    string scriptPath = ProjectSettings.GlobalizePath(script.ResourcePath);
                    RiderPathManager.OpenFile(editorId, JundotSharpDirs.ProjectSlnPath, scriptPath, line + 1, col);
                    return Error.Ok;
                }
                case ExternalEditorId.MonoDevelop:
                {
                    string scriptPath = ProjectSettings.GlobalizePath(script.ResourcePath);

                    JundotIdeManager.LaunchIdeAsync().ContinueWith(launchTask =>
                    {
                        var editorPick = launchTask.Result;
                        if (line >= 0)
                            editorPick?.SendOpenFile(scriptPath, line + 1, col);
                        else
                            editorPick?.SendOpenFile(scriptPath);
                    });

                    break;
                }
                case ExternalEditorId.VsCode:
                {
                    string configuredPath = GetConfiguredIdePath(ExternalEditorId.VsCode);
                    bool usingConfiguredPath = !string.IsNullOrEmpty(configuredPath);
                    if (usingConfiguredPath && !File.Exists(configuredPath))
                    {
                        GD.PushError($"Configured IDE path does not exist: {configuredPath}");
                        return Error.FileNotFound;
                    }

                    if (!usingConfiguredPath && (string.IsNullOrEmpty(_vsCodePath) || !File.Exists(_vsCodePath)))
                    {
                        // Try to search it again if it wasn't found last time or if it was removed from its location
                        _vsCodePath = VsCodeNames.SelectFirstNotNull(OS.PathWhich, orElse: string.Empty);
                    }

                    var args = new List<string>();

                    bool macOSAppBundleInstalled = false;

                    if (OS.IsMacOS && !usingConfiguredPath)
                    {
                        // The package path is '/Applications/Visual Studio Code.app'
                        const string vscodeBundleId = "com.microsoft.VSCode";

                        macOSAppBundleInstalled = Internal.IsMacOSAppBundleInstalled(vscodeBundleId);

                        if (macOSAppBundleInstalled)
                        {
                            args.Add("-b");
                            args.Add(vscodeBundleId);

                            // The reusing of existing windows made by the 'open' command might not choose a window that is
                            // editing our folder. It's better to ask for a new window and let VSCode do the window management.
                            args.Add("-n");

                            // The open process must wait until the application finishes (which is instant in VSCode's case)
                            args.Add("--wait-apps");

                            args.Add("--args");
                        }

                        // Try VSCodium as a fallback if Visual Studio Code can't be found.
                        if (!macOSAppBundleInstalled)
                        {
                            const string VscodiumBundleId = "com.vscodium.codium";
                            macOSAppBundleInstalled = Internal.IsMacOSAppBundleInstalled(VscodiumBundleId);

                            if (macOSAppBundleInstalled)
                            {
                                args.Add("-b");
                                args.Add(VscodiumBundleId);

                                // The reusing of existing windows made by the 'open' command might not choose a window that is
                                // editing our folder. It's better to ask for a new window and let VSCode do the window management.
                                args.Add("-n");

                                // The open process must wait until the application finishes (which is instant in VSCode's case)
                                args.Add("--wait-apps");

                                args.Add("--args");
                            }
                        }
                    }

                    args.Add(Path.GetDirectoryName(JundotSharpDirs.ProjectSlnPath)!);

                    string scriptPath = ProjectSettings.GlobalizePath(script.ResourcePath);

                    if (line >= 0)
                    {
                        args.Add("-g");
                        args.Add($"{scriptPath}:{line + 1}:{col + 1}");
                    }
                    else
                    {
                        args.Add(scriptPath);
                    }

                    string command;

                    if (OS.IsMacOS && !usingConfiguredPath)
                    {
                        if (!macOSAppBundleInstalled && string.IsNullOrEmpty(_vsCodePath))
                        {
                            GD.PushError("Cannot find code editor: Visual Studio Code or VSCodium");
                            return Error.FileNotFound;
                        }

                        command = macOSAppBundleInstalled ? "/usr/bin/open" : _vsCodePath;
                    }
                    else
                    {
                        if (!usingConfiguredPath && string.IsNullOrEmpty(_vsCodePath))
                        {
                            GD.PushError("Cannot find code editor: Visual Studio Code or VSCodium");
                            return Error.FileNotFound;
                        }

                        command = usingConfiguredPath ? configuredPath : _vsCodePath;
                    }

                    try
                    {
                        OS.RunProcess(command, args);
                    }
                    catch (Exception e)
                    {
                        GD.PushError($"Error when trying to run code editor: Visual Studio Code or VSCodium. Exception message: '{e.Message}'");
                    }

                    break;
                }
                default:
                    throw new ArgumentOutOfRangeException();
            }

            return Error.Ok;
        }

        [UsedImplicitly]
        public bool OverridesExternalEditor()
        {
            return _editorSettings.GetSetting(Settings.ExternalEditor).As<ExternalEditorId>() != ExternalEditorId.None;
        }

        public override bool _Build()
        {
            return BuildManager.EditorBuildCallback();
        }

        private void ApplyNecessaryChangesToSolution()
        {
            try
            {
                // Migrate solution from old configuration names to: Debug, ExportDebug and ExportRelease
                DotNetSolution.MigrateFromOldConfigNames(JundotSharpDirs.ProjectSlnPath);

                var msbuildProject = ProjectUtils.Open(JundotSharpDirs.ProjectCsProjPath)
                                     ?? throw new InvalidOperationException("Cannot open C# project.");

                ProjectUtils.UpgradeProjectIfNeeded(msbuildProject, JundotSharpDirs.ProjectAssemblyName);

                if (msbuildProject.HasUnsavedChanges)
                {
                    // Save a copy of the project before replacing it
                    FileUtils.SaveBackupCopy(JundotSharpDirs.ProjectCsProjPath);

                    msbuildProject.Save();
                }
            }
            catch (Exception e)
            {
                GD.PushError(e.ToString());
            }
        }

        public override void _EnablePlugin()
        {
            base._EnablePlugin();

            ProjectSettings.SettingsChanged += JundotSharpDirs.DetermineProjectLocation;

            if (Instance != null)
                throw new InvalidOperationException();
            Instance = this;

            var dotNetSdkSearchVersion = Environment.Version;

            // First we try to find the .NET Sdk ourselves to make sure we get the
            // correct version first, otherwise pick the latest.
            if (DotNetFinder.TryFindDotNetSdk(dotNetSdkSearchVersion, out var sdkVersion, out string? sdkPath))
            {
                if (Jundot.OS.IsStdOutVerbose())
                    Console.WriteLine($"Found .NET Sdk version '{sdkVersion}': {sdkPath}");

                ProjectUtils.MSBuildLocatorRegisterMSBuildPath(sdkPath);
            }
            else
            {
                try
                {
                    ProjectUtils.MSBuildLocatorRegisterLatest(out sdkVersion, out sdkPath);
                    if (Jundot.OS.IsStdOutVerbose())
                        Console.WriteLine($"Found .NET Sdk version '{sdkVersion}': {sdkPath}");
                }
                catch (InvalidOperationException e)
                {
                    if (Jundot.OS.IsStdOutVerbose())
                        GD.PrintErr(e.ToString());
                    GD.PushError($".NET Sdk not found. The required version is '{dotNetSdkSearchVersion}'.");
                }
            }

            var editorBaseControl = EditorInterface.Singleton.GetBaseControl();

            _editorSettings = EditorInterface.Singleton.GetEditorSettings();

            _errorDialog = new AcceptDialog();
            _errorDialog.SetUnparentWhenInvisible(true);

            _confirmCreateSlnDialog = new ConfirmationDialog();
            _confirmCreateSlnDialog.SetUnparentWhenInvisible(true);
            _confirmCreateSlnDialog.Confirmed += () => CreateProjectSolution();

            MSBuildPanel = new MSBuildPanel();
            AddDock(MSBuildPanel);

            AddChild(new HotReloadAssemblyWatcher { Name = "HotReloadAssemblyWatcher" });

            _menuPopup = new PopupMenu
            {
                Name = "CSharpTools",
            };
            _menuPopup.Hide();

            AddToolSubmenuItem("C#", _menuPopup);

            _toolBarBuildButton = new Button
            {
                Flat = false,
                Icon = EditorInterface.Singleton.GetEditorTheme().GetIcon("BuildCSharp", "EditorIcons"),
                FocusMode = Control.FocusModeEnum.None,
                Shortcut = EditorDefShortcut("mono/build_solution", "Build Project".TTR(), (Key)KeyModifierMask.MaskAlt | Key.B),
                ShortcutInTooltip = true,
                ThemeTypeVariation = "RunBarButton",
            };
            EditorShortcutOverride("mono/build_solution", "macos", (Key)KeyModifierMask.MaskMeta | (Key)KeyModifierMask.MaskCtrl | Key.B);

            _toolBarBuildButton.Pressed += BuildProjectPressed;
            Internal.EditorPlugin_AddControlToEditorRunBar(_toolBarBuildButton);
            // Move Build button so it appears to the left of the Play button.
            _toolBarBuildButton.GetParent().MoveChild(_toolBarBuildButton, 0);

            _openIdePopup = new PopupMenu
            {
                Name = "OpenCSharpIdePopup",
            };
            _openIdePopup.AddItem("Open selected IDE".TTR(), (int)MenuOptions.OpenSln);
            _openIdePopup.AddItem("Open selected IDE locally".TTR(), (int)MenuOptions.OpenSlnLocal);
            _openIdePopup.IdPressed += _MenuOptionPressed;

            _toolBarOpenIdeButton = new Button
            {
                Flat = false,
                Icon = EditorInterface.Singleton.GetEditorTheme().GetIcon("CSharpScript", "EditorIcons"),
                FocusMode = Control.FocusModeEnum.None,
                TooltipText = "Open C# IDE".TTR(),
                ThemeTypeVariation = "RunBarButton",
            };
            _toolBarOpenIdeButton.Pressed += OpenSelectedIdePressed;
            _toolBarOpenIdeButton.GuiInput += OpenIdeButtonGuiInput;
            _toolBarOpenIdeButton.AddChild(_openIdePopup);
            Internal.EditorPlugin_AddControlToEditorRunBar(_toolBarOpenIdeButton);
            _toolBarOpenIdeButton.GetParent().MoveChild(_toolBarOpenIdeButton, 1);

            EditorInterface.Singleton.GetCommandPalette().AddCommand("Build C# project".TTR(), "dotnet/build_solution", Callable.From(BuildProjectPressed), _toolBarBuildButton.Shortcut.GetAsText());

            if (File.Exists(JundotSharpDirs.ProjectCsProjPath))
            {
                ApplyNecessaryChangesToSolution();
            }
            else
            {
                MSBuildPanel.Close();
                _toolBarBuildButton.Hide();
            }
            _menuPopup.AddItem("Create C# solution".TTR(), (int)MenuOptions.CreateSln);
            _menuPopup.AddSeparator();
            _menuPopup.AddItem("Open selected C# IDE".TTR(), (int)MenuOptions.OpenSln);
            _menuPopup.AddItem("Open selected C# IDE locally".TTR(), (int)MenuOptions.OpenSlnLocal);
            _menuPopup.AddItem("Open with Visual Studio".TTR(), (int)MenuOptions.OpenSlnInVisualStudio);
            _menuPopup.AddItem("Open with Visual Studio Code".TTR(), (int)MenuOptions.OpenSlnInVsCode);
            _menuPopup.AddItem("Open with JetBrains Rider".TTR(), (int)MenuOptions.OpenSlnInRider);
            _menuPopup.AddItem("Open with JetBrains Fleet".TTR(), (int)MenuOptions.OpenSlnInFleet);
            _menuPopup.AddItem("Open with MonoDevelop".TTR(), (int)MenuOptions.OpenSlnInMonoDevelop);

            _menuPopup.IdPressed += _MenuOptionPressed;

            // External editor settings
            EditorDef(Settings.ExternalEditor, Variant.From(ExternalEditorId.None));
            EditorDef(Settings.IdePathOptional, "");
            EditorDef(Settings.CustomExecPath, "");
            EditorDef(Settings.CustomExecPathArgs, "");
            EditorDef(Settings.VerbosityLevel, Variant.From(VerbosityLevelId.Normal));
            EditorDef(Settings.NoConsoleLogging, false);
            EditorDef(Settings.CreateBinaryLog, false);
            EditorDef(Settings.ProblemsLayout, Variant.From(BuildProblemsView.ProblemsLayout.Tree));

            string settingsHintStr = "Disabled";

            if (OS.IsWindows)
            {
                settingsHintStr += $",Visual Studio:{(int)ExternalEditorId.VisualStudio}" +
                                   $",MonoDevelop:{(int)ExternalEditorId.MonoDevelop}" +
                                   $",Visual Studio Code and VSCodium:{(int)ExternalEditorId.VsCode}" +
                                   $",JetBrains Rider:{(int)ExternalEditorId.Rider}" +
                                   $",JetBrains Fleet:{(int)ExternalEditorId.Fleet}" +
                                   $",Custom:{(int)ExternalEditorId.CustomEditor}";
            }
            else if (OS.IsMacOS)
            {
                settingsHintStr += $",Visual Studio:{(int)ExternalEditorId.VisualStudioForMac}" +
                                   $",MonoDevelop:{(int)ExternalEditorId.MonoDevelop}" +
                                   $",Visual Studio Code and VSCodium:{(int)ExternalEditorId.VsCode}" +
                                   $",JetBrains Rider:{(int)ExternalEditorId.Rider}" +
                                   $",JetBrains Fleet:{(int)ExternalEditorId.Fleet}" +
                                   $",Custom:{(int)ExternalEditorId.CustomEditor}";
            }
            else if (OS.IsUnixLike)
            {
                settingsHintStr += $",MonoDevelop:{(int)ExternalEditorId.MonoDevelop}" +
                                   $",Visual Studio Code and VSCodium:{(int)ExternalEditorId.VsCode}" +
                                   $",JetBrains Rider:{(int)ExternalEditorId.Rider}" +
                                   $",JetBrains Fleet:{(int)ExternalEditorId.Fleet}" +
                                   $",Custom:{(int)ExternalEditorId.CustomEditor}";
            }

            _editorSettings.AddPropertyInfo(new Jundot.Collections.Dictionary
            {
                ["type"] = (int)Variant.Type.Int,
                ["name"] = Settings.ExternalEditor,
                ["hint"] = (int)PropertyHint.Enum,
                ["hint_string"] = settingsHintStr
            });

            _editorSettings.AddPropertyInfo(new Jundot.Collections.Dictionary
            {
                ["type"] = (int)Variant.Type.String,
                ["name"] = Settings.IdePathOptional,
                ["hint"] = (int)PropertyHint.GlobalFile,
                ["hint_string"] = "",
            });

            _editorSettings.AddPropertyInfo(new Jundot.Collections.Dictionary
            {
                ["type"] = (int)Variant.Type.String,
                ["name"] = Settings.CustomExecPath,
                ["hint"] = (int)PropertyHint.GlobalFile,
            });

            _editorSettings.AddPropertyInfo(new Jundot.Collections.Dictionary
            {
                ["type"] = (int)Variant.Type.String,
                ["name"] = Settings.CustomExecPathArgs,
            });
            _editorSettings.SetInitialValue(Settings.CustomExecPathArgs, "{file}", false);

            var verbosityLevels = Enum.GetValues<VerbosityLevelId>().Select(level => $"{Enum.GetName(level)}:{(int)level}");
            _editorSettings.AddPropertyInfo(new Jundot.Collections.Dictionary
            {
                ["type"] = (int)Variant.Type.Int,
                ["name"] = Settings.VerbosityLevel,
                ["hint"] = (int)PropertyHint.Enum,
                ["hint_string"] = string.Join(",", verbosityLevels),
            });

            _editorSettings.AddPropertyInfo(new Jundot.Collections.Dictionary
            {
                ["type"] = (int)Variant.Type.Int,
                ["name"] = Settings.ProblemsLayout,
                ["hint"] = (int)PropertyHint.Enum,
                ["hint_string"] = "View as List,View as Tree",
            });

            OnSettingsChanged();
            _editorSettings.SettingsChanged += OnSettingsChanged;

            // Export plugin
            var exportPlugin = new ExportPlugin();
            AddExportPlugin(exportPlugin);
            _exportPluginWeak = WeakRef(exportPlugin);

            // Inspector plugin
            var inspectorPlugin = new InspectorPlugin();
            AddInspectorPlugin(inspectorPlugin);
            _inspectorPluginWeak = WeakRef(inspectorPlugin);

            // TranslationParser Plugin
            AddTranslationParserPlugin(new CsTranslationParserPlugin());

            BuildManager.Initialize();

            JundotIdeManager = new JundotIdeManager();
            AddChild(JundotIdeManager);
        }

        public override void _DisablePlugin()
        {
            base._DisablePlugin();

            _editorSettings.SettingsChanged -= OnSettingsChanged;
        }

        public override void _ExitTree()
        {
            _errorDialog?.QueueFree();
            _confirmCreateSlnDialog?.QueueFree();
        }

        private void OnSettingsChanged()
        {
            var changedSettings = _editorSettings.GetChangedSettings();
            if (changedSettings.Contains(Settings.VerbosityLevel))
            {
                // We want to force NoConsoleLogging to true when the VerbosityLevel is at Detailed or above.
                // At that point, there's so much info logged that it doesn't make sense to display it in
                // the tiny editor window, and it'd make the editor hang or crash anyway.
                var verbosityLevel = _editorSettings.GetSetting(Settings.VerbosityLevel).As<VerbosityLevelId>();
                var hideConsoleLog = (bool)_editorSettings.GetSetting(Settings.NoConsoleLogging);
                if (verbosityLevel >= VerbosityLevelId.Detailed && !hideConsoleLog)
                    _editorSettings.SetSetting(Settings.NoConsoleLogging, Variant.From(true));
            }

            if (changedSettings.Contains(Settings.ExternalEditor) && !changedSettings.Contains(RiderPathManager.EditorPathSettingName))
            {
                var editor = _editorSettings.GetSetting(Settings.ExternalEditor).As<ExternalEditorId>();
                if (editor != ExternalEditorId.Fleet && editor != ExternalEditorId.Rider)
                {
                    return;
                }

                RiderPathManager.InitializeIfNeeded(editor);
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                if (IsInstanceValid(_exportPluginWeak))
                {
                    // We need to dispose our export plugin before the editor destroys EditorSettings.
                    // Otherwise, if the GC disposes it at a later time, EditorExportPlatformAndroid
                    // will be freed after EditorSettings already was, and its device polling thread
                    // will try to access the EditorSettings singleton, resulting in null dereferencing.
                    (_exportPluginWeak.GetRef().AsJundotObject() as ExportPlugin)?.Dispose();

                    _exportPluginWeak.Dispose();
                }

                if (IsInstanceValid(_inspectorPluginWeak))
                {
                    (_inspectorPluginWeak.GetRef().AsJundotObject() as InspectorPlugin)?.Dispose();

                    _inspectorPluginWeak.Dispose();
                }

                JundotIdeManager?.Dispose();
            }

            base.Dispose(disposing);
        }

        public void OnBeforeSerialize()
        {
        }

        public void OnAfterDeserialize()
        {
            Instance = this;
        }

        // Singleton
#nullable disable
        public static JundotSharpEditor Instance { get; private set; }
#nullable enable

        [UsedImplicitly]
        private static IntPtr InternalCreateInstance(IntPtr unmanagedCallbacks, int unmanagedCallbacksSize)
        {
            Internal.Initialize(unmanagedCallbacks, unmanagedCallbacksSize);

            var populateConstructorMethod =
                AppDomain.CurrentDomain
                    .GetAssemblies()
                    .First(x => x.GetName().Name == "JundotSharpEditor")
                    .GetType("Jundot.EditorConstructors")?
                    .GetMethod("AddEditorConstructors",
                        BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public);

            if (populateConstructorMethod == null)
            {
                throw new MissingMethodException("Jundot.EditorConstructors",
                    "AddEditorConstructors");
            }

            populateConstructorMethod.Invoke(null, null);

            return new JundotSharpEditor().NativeInstance;
        }
    }
}
