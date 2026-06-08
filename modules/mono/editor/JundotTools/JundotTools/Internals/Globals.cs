using Jundot;
using Jundot.NativeInterop;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace JundotTools.Internals
{
    public static class Globals
    {
        public static float EditorScale => Internal.jundot_icall_Globals_EditorScale();

        // ReSharper disable once UnusedMethodReturnValue.Global
        public static Variant GlobalDef(string setting, Variant defaultValue, bool restartIfChanged = false)
        {
            using jundot_string settingIn = Marshaling.ConvertStringToNative(setting);
            using jundot_variant defaultValueIn = defaultValue.CopyNativeVariant();
            Internal.jundot_icall_Globals_GlobalDef(settingIn, defaultValueIn, restartIfChanged,
                out jundot_variant result);
            return Variant.CreateTakingOwnershipOfDisposableValue(result);
        }

        // ReSharper disable once UnusedMethodReturnValue.Global
        public static Variant EditorDef(string setting, Variant defaultValue, bool restartIfChanged = false)
        {
            using jundot_string settingIn = Marshaling.ConvertStringToNative(setting);
            using jundot_variant defaultValueIn = defaultValue.CopyNativeVariant();
            Internal.jundot_icall_Globals_EditorDef(settingIn, defaultValueIn, restartIfChanged,
                out jundot_variant result);
            return Variant.CreateTakingOwnershipOfDisposableValue(result);
        }

        public static Shortcut EditorDefShortcut(string setting, string name, Key keycode = Key.None, bool physical = false)
        {
            using jundot_string settingIn = Marshaling.ConvertStringToNative(setting);
            using jundot_string nameIn = Marshaling.ConvertStringToNative(name);
            Internal.jundot_icall_Globals_EditorDefShortcut(settingIn, nameIn, keycode, physical.ToJundotBool(), out jundot_variant result);
            return (Shortcut)Variant.CreateTakingOwnershipOfDisposableValue(result);
        }

        public static Shortcut EditorGetShortcut(string setting)
        {
            using jundot_string settingIn = Marshaling.ConvertStringToNative(setting);
            Internal.jundot_icall_Globals_EditorGetShortcut(settingIn, out jundot_variant result);
            return (Shortcut)Variant.CreateTakingOwnershipOfDisposableValue(result);
        }

        public static void EditorShortcutOverride(string setting, string feature, Key keycode = Key.None, bool physical = false)
        {
            using jundot_string settingIn = Marshaling.ConvertStringToNative(setting);
            using jundot_string featureIn = Marshaling.ConvertStringToNative(feature);
            Internal.jundot_icall_Globals_EditorShortcutOverride(settingIn, featureIn, keycode, physical.ToJundotBool());
        }

        [SuppressMessage("ReSharper", "InconsistentNaming")]
        public static string TTR(this string text)
        {
            using jundot_string textIn = Marshaling.ConvertStringToNative(text);
            Internal.jundot_icall_Globals_TTR(textIn, out jundot_string dest);
            using (dest)
                return Marshaling.ConvertStringToManaged(dest);
        }
    }
}
