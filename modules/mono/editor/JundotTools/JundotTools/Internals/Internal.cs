#pragma warning disable IDE1006 // Naming rule violation
// ReSharper disable InconsistentNaming

using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using Jundot;
using Jundot.NativeInterop;
using Jundot.SourceGenerators.Internal;
using JundotTools.IdeMessaging.Requests;

namespace JundotTools.Internals
{
    [GenerateUnmanagedCallbacks(typeof(InternalUnmanagedCallbacks))]
    internal static partial class Internal
    {
        public const string CSharpLanguageType = "CSharpScript";
        public const string CSharpLanguageExtension = ".cs";

        public static string FullExportTemplatesDir
        {
            get
            {
                jundot_icall_Internal_FullExportTemplatesDir(out jundot_string dest);
                using (dest)
                    return Marshaling.ConvertStringToManaged(dest);
            }
        }

        public static string SimplifyJundotPath(this string path) => Jundot.StringExtensions.SimplifyPath(path);

        public static bool IsMacOSAppBundleInstalled(string bundleId)
        {
            using jundot_string bundleIdIn = Marshaling.ConvertStringToNative(bundleId);
            return jundot_icall_Internal_IsMacOSAppBundleInstalled(bundleIdIn);
        }

        public static bool LipOCreateFile(string outputPath, string[] files)
        {
            using jundot_string outputPathIn = Marshaling.ConvertStringToNative(outputPath);
            using jundot_packed_string_array filesIn = Marshaling.ConvertSystemArrayToNativePackedStringArray(files);
            return jundot_icall_Internal_LipOCreateFile(outputPathIn, filesIn);
        }

        public static bool JundotIs32Bits() => jundot_icall_Internal_JundotIs32Bits();

        public static bool JundotIsRealTDouble() => jundot_icall_Internal_JundotIsRealTDouble();

        public static void JundotMainIteration() => jundot_icall_Internal_JundotMainIteration();

        public static bool IsAssembliesReloadingNeeded() => jundot_icall_Internal_IsAssembliesReloadingNeeded();

        public static void ReloadAssemblies(bool softReload) => jundot_icall_Internal_ReloadAssemblies(softReload);

        public static void EditorDebuggerNodeReloadScripts() => jundot_icall_Internal_EditorDebuggerNodeReloadScripts();

        public static bool ScriptEditorEdit(Resource resource, int line, int col, bool grabFocus = true) =>
            jundot_icall_Internal_ScriptEditorEdit(resource.NativeInstance, line, col, grabFocus);

        public static void EditorNodeShowScriptScreen() => jundot_icall_Internal_EditorNodeShowScriptScreen();

        public static void EditorRunPlay() => jundot_icall_Internal_EditorRunPlay();

        public static void EditorRunStop() => jundot_icall_Internal_EditorRunStop();

        public static void EditorPlugin_AddControlToEditorRunBar(Control control) =>
            jundot_icall_Internal_EditorPlugin_AddControlToEditorRunBar(control.NativeInstance);

        public static void ScriptEditorDebugger_ReloadScripts() =>
            jundot_icall_Internal_ScriptEditorDebugger_ReloadScripts();

        public static string[] CodeCompletionRequest(CodeCompletionRequest.CompletionKind kind,
            string scriptFile)
        {
            using jundot_string scriptFileIn = Marshaling.ConvertStringToNative(scriptFile);
            jundot_icall_Internal_CodeCompletionRequest((int)kind, scriptFileIn, out jundot_packed_string_array res);
            using (res)
                return Marshaling.ConvertNativePackedStringArrayToSystemArray(res);
        }

        #region Internal

        private static bool initialized = false;

        // ReSharper disable once ParameterOnlyUsedForPreconditionCheck.Global
        internal static unsafe void Initialize(IntPtr unmanagedCallbacks, int unmanagedCallbacksSize)
        {
            if (initialized)
                throw new InvalidOperationException("Already initialized.");
            initialized = true;

            if (unmanagedCallbacksSize != sizeof(InternalUnmanagedCallbacks))
                throw new ArgumentException("Unmanaged callbacks size mismatch.", nameof(unmanagedCallbacksSize));

            _unmanagedCallbacks = Unsafe.AsRef<InternalUnmanagedCallbacks>((void*)unmanagedCallbacks);
        }

        private partial struct InternalUnmanagedCallbacks
        {
        }

        /*
         * IMPORTANT:
         * The order of the methods defined in NativeFuncs must match the order
         * in the array defined at the bottom of 'editor/editor_internal_calls.cpp'.
         */

        public static partial void jundot_icall_JundotSharpDirs_ResMetadataDir(out jundot_string r_dest);

        public static partial void jundot_icall_JundotSharpDirs_MonoUserDir(out jundot_string r_dest);

        public static partial void jundot_icall_JundotSharpDirs_BuildLogsDirs(out jundot_string r_dest);

        public static partial void jundot_icall_JundotSharpDirs_DataEditorToolsDir(out jundot_string r_dest);

        public static partial void jundot_icall_JundotSharpDirs_CSharpProjectName(out jundot_string r_dest);

        public static partial void jundot_icall_EditorProgress_Create(in jundot_string task, in jundot_string label,
            int amount, bool canCancel);

        public static partial void jundot_icall_EditorProgress_Dispose(in jundot_string task);

        public static partial bool jundot_icall_EditorProgress_Step(in jundot_string task, in jundot_string state,
            int step,
            bool forceRefresh);

        private static partial void jundot_icall_Internal_FullExportTemplatesDir(out jundot_string dest);

        private static partial bool jundot_icall_Internal_IsMacOSAppBundleInstalled(in jundot_string bundleId);

        private static partial bool jundot_icall_Internal_LipOCreateFile(in jundot_string outputPath, in jundot_packed_string_array files);

        private static partial bool jundot_icall_Internal_JundotIs32Bits();

        private static partial bool jundot_icall_Internal_JundotIsRealTDouble();

        private static partial void jundot_icall_Internal_JundotMainIteration();

        private static partial bool jundot_icall_Internal_IsAssembliesReloadingNeeded();

        private static partial void jundot_icall_Internal_ReloadAssemblies(bool softReload);

        private static partial void jundot_icall_Internal_EditorDebuggerNodeReloadScripts();

        private static partial bool jundot_icall_Internal_ScriptEditorEdit(IntPtr resource, int line, int col,
            bool grabFocus);

        private static partial void jundot_icall_Internal_EditorNodeShowScriptScreen();

        private static partial void jundot_icall_Internal_EditorRunPlay();

        private static partial void jundot_icall_Internal_EditorRunStop();

        private static partial void jundot_icall_Internal_EditorPlugin_AddControlToEditorRunBar(IntPtr p_control);

        private static partial void jundot_icall_Internal_ScriptEditorDebugger_ReloadScripts();

        private static partial void jundot_icall_Internal_CodeCompletionRequest(int kind, in jundot_string scriptFile,
            out jundot_packed_string_array res);

        public static partial float jundot_icall_Globals_EditorScale();

        public static partial void jundot_icall_Globals_GlobalDef(in jundot_string setting, in jundot_variant defaultValue,
            bool restartIfChanged, out jundot_variant result);

        public static partial void jundot_icall_Globals_EditorDef(in jundot_string setting, in jundot_variant defaultValue,
            bool restartIfChanged, out jundot_variant result);

        public static partial void
            jundot_icall_Globals_EditorDefShortcut(in jundot_string setting, in jundot_string name, Key keycode, jundot_bool physical, out jundot_variant result);

        public static partial void
            jundot_icall_Globals_EditorGetShortcut(in jundot_string setting, out jundot_variant result);

        public static partial void
            jundot_icall_Globals_EditorShortcutOverride(in jundot_string setting, in jundot_string feature, Key keycode, jundot_bool physical);

        public static partial void jundot_icall_Globals_TTR(in jundot_string text, out jundot_string dest);

        public static partial void jundot_icall_Utils_OS_GetPlatformName(out jundot_string dest);

        public static partial bool jundot_icall_Utils_OS_UnixFileHasExecutableAccess(in jundot_string filePath);

        #endregion
    }
}
