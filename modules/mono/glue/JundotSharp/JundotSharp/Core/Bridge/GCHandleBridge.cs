using System;
using System.Runtime.InteropServices;
using Jundot.NativeInterop;

namespace Jundot.Bridge
{
    internal static class GCHandleBridge
    {
        [UnmanagedCallersOnly]
        internal static void FreeGCHandle(IntPtr gcHandlePtr)
        {
            try
            {
                CustomGCHandle.Free(GCHandle.FromIntPtr(gcHandlePtr));
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
            }
        }

        // Returns true, if releasing the provided handle is necessary for assembly unloading to succeed.
        // This check is not perfect and only intended to prevent things in JundotTools from being reloaded.
        [UnmanagedCallersOnly]
        internal static jundot_bool GCHandleIsTargetCollectible(IntPtr gcHandlePtr)
        {
            try
            {
                var target = GCHandle.FromIntPtr(gcHandlePtr).Target;

                if (target is Delegate @delegate)
                    return DelegateUtils.IsDelegateCollectible(@delegate).ToJundotBool();

                return target.GetType().IsCollectible.ToJundotBool();
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
                return jundot_bool.True;
            }
        }
    }
}
