using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using Jundot.NativeInterop;

#nullable enable

namespace Jundot
{
    internal static class DisposablesTracker
    {
        [UnmanagedCallersOnly]
        internal static void OnJundotShuttingDown()
        {
            try
            {
                OnJundotShuttingDownImpl();
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
            }
        }

        private static void OnJundotShuttingDownImpl()
        {
            bool isStdoutVerbose;

            try
            {
                isStdoutVerbose = OS.IsStdOutVerbose();
            }
            catch (ObjectDisposedException)
            {
                // OS singleton already disposed. Maybe OnUnloading was called twice.
                isStdoutVerbose = false;
            }

            if (isStdoutVerbose)
                GD.Print("Unloading: Disposing tracked instances...");

            // Dispose Jundot Objects first, and only then dispose other disposables
            // like StringName, NodePath, Jundot.Collections.Array/Dictionary, etc.
            // The Jundot Object Dispose() method may need any of the later instances.

            foreach (WeakReference<JundotObject> item in JundotObjectInstances.Keys)
            {
                if (item.TryGetTarget(out JundotObject? self))
                    self.Dispose();
            }

            foreach (WeakReference<IDisposable> item in OtherInstances.Keys)
            {
                if (item.TryGetTarget(out IDisposable? self))
                    self.Dispose();
            }

            if (isStdoutVerbose)
                GD.Print("Unloading: Finished disposing tracked instances.");
        }

        private static ConcurrentDictionary<WeakReference<JundotObject>, byte> JundotObjectInstances { get; } =
            new();

        private static ConcurrentDictionary<WeakReference<IDisposable>, byte> OtherInstances { get; } =
            new();

        public static WeakReference<JundotObject> RegisterJundotObject(JundotObject jundotObject)
        {
            var weakReferenceToSelf = new WeakReference<JundotObject>(jundotObject);
            JundotObjectInstances.TryAdd(weakReferenceToSelf, 0);
            return weakReferenceToSelf;
        }

        public static WeakReference<IDisposable> RegisterDisposable(IDisposable disposable)
        {
            var weakReferenceToSelf = new WeakReference<IDisposable>(disposable);
            OtherInstances.TryAdd(weakReferenceToSelf, 0);
            return weakReferenceToSelf;
        }

        public static void UnregisterJundotObject(JundotObject jundotObject, WeakReference<JundotObject> weakReferenceToSelf)
        {
            if (!JundotObjectInstances.TryRemove(weakReferenceToSelf, out _))
                throw new ArgumentException("Jundot Object not registered.", nameof(weakReferenceToSelf));
        }

        public static void UnregisterDisposable(WeakReference<IDisposable> weakReference)
        {
            if (!OtherInstances.TryRemove(weakReference, out _))
                throw new ArgumentException("Disposable not registered.", nameof(weakReference));
        }
    }
}
