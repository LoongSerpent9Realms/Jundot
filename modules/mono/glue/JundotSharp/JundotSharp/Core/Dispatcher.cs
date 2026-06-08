using System;
using System.Runtime.InteropServices;
using Jundot.NativeInterop;

namespace Jundot
{
    public static class Dispatcher
    {
        internal static JundotTaskScheduler DefaultJundotTaskScheduler;

        internal static void InitializeDefaultJundotTaskScheduler()
        {
            DefaultJundotTaskScheduler?.Dispose();
            DefaultJundotTaskScheduler = new JundotTaskScheduler();
        }

        public static JundotSynchronizationContext SynchronizationContext => DefaultJundotTaskScheduler.Context;
    }
}
