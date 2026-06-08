using System;
using System.Runtime.InteropServices;
using Jundot.Bridge;

// ReSharper disable InconsistentNaming

namespace Jundot.NativeInterop
{
    internal static class InteropUtils
    {
        public static JundotObject UnmanagedGetManaged(IntPtr unmanaged)
        {
            // The native pointer may be null
            if (unmanaged == IntPtr.Zero)
                return null;

            IntPtr gcHandlePtr;
            jundot_bool hasCsScriptInstance;

            // First try to get the tied managed instance from a CSharpInstance script instance

            gcHandlePtr = NativeFuncs.jundotsharp_internal_unmanaged_get_script_instance_managed(
                unmanaged, out hasCsScriptInstance);

            if (gcHandlePtr != IntPtr.Zero)
                return (JundotObject)GCHandle.FromIntPtr(gcHandlePtr).Target;

            // Otherwise, if the object has a CSharpInstance script instance, return null

            if (hasCsScriptInstance.ToBool())
                return null;

            // If it doesn't have a CSharpInstance script instance, try with native instance bindings

            gcHandlePtr = NativeFuncs.jundotsharp_internal_unmanaged_get_instance_binding_managed(unmanaged);

            object target = gcHandlePtr != IntPtr.Zero ? GCHandle.FromIntPtr(gcHandlePtr).Target : null;

            if (target != null)
                return (JundotObject)target;

            // If the native instance binding GC handle target was collected, create a new one

            gcHandlePtr = NativeFuncs.jundotsharp_internal_unmanaged_instance_binding_create_managed(
                unmanaged, gcHandlePtr);

            return gcHandlePtr != IntPtr.Zero ? (JundotObject)GCHandle.FromIntPtr(gcHandlePtr).Target : null;
        }

        public static void TieManagedToUnmanaged(JundotObject managed, IntPtr unmanaged,
            StringName nativeName, bool refCounted, Type type, Type nativeType)
        {
            var gcHandle = refCounted ?
                CustomGCHandle.AllocWeak(managed) :
                CustomGCHandle.AllocStrong(managed, type);

            if (type == nativeType)
            {
                var nativeNameSelf = (jundot_string_name)nativeName.NativeValue;
                NativeFuncs.jundotsharp_internal_tie_native_managed_to_unmanaged(
                    GCHandle.ToIntPtr(gcHandle), unmanaged, nativeNameSelf, refCounted.ToJundotBool());
            }
            else
            {
                unsafe
                {
                    // We don't dispose `script` ourselves here.
                    // `tie_user_managed_to_unmanaged` does it for us to avoid another P/Invoke call.
                    jundot_ref script;
                    ScriptManagerBridge.GetOrLoadOrCreateScriptForType(type, &script);

                    // IMPORTANT: This must be called after GetOrCreateScriptBridgeForType
                    NativeFuncs.jundotsharp_internal_tie_user_managed_to_unmanaged(
                        GCHandle.ToIntPtr(gcHandle), unmanaged, &script, refCounted.ToJundotBool());
                }
            }
        }

        public static void TieManagedToUnmanagedWithPreSetup(JundotObject managed, IntPtr unmanaged,
            Type type, Type nativeType)
        {
            if (type == nativeType)
                return;

            var strongGCHandle = CustomGCHandle.AllocStrong(managed);
            NativeFuncs.jundotsharp_internal_tie_managed_to_unmanaged_with_pre_setup(
                GCHandle.ToIntPtr(strongGCHandle), unmanaged);
        }

        public static JundotObject EngineGetSingleton(string name)
        {
            using jundot_string src = Marshaling.ConvertStringToNative(name);
            return UnmanagedGetManaged(NativeFuncs.jundotsharp_engine_get_singleton(src));
        }
    }
}
