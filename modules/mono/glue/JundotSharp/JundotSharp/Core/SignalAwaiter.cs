using System;
using System.Runtime.InteropServices;
using Jundot.NativeInterop;

namespace Jundot
{
    public class SignalAwaiter : IAwaiter<Variant[]>, IAwaitable<Variant[]>
    {
        private bool _completed;
        private Variant[] _result;
        private Action _continuation;

        public SignalAwaiter(JundotObject source, StringName signal, JundotObject target)
        {
            var awaiterGcHandle = CustomGCHandle.AllocStrong(this);
            using jundot_string_name signalSrc = NativeFuncs.jundotsharp_string_name_new_copy(
                (jundot_string_name)(signal?.NativeValue ?? default));
            NativeFuncs.jundotsharp_internal_signal_awaiter_connect(JundotObject.GetPtr(source), in signalSrc,
                JundotObject.GetPtr(target), GCHandle.ToIntPtr(awaiterGcHandle));
        }

        public bool IsCompleted => _completed;

        public void OnCompleted(Action continuation)
        {
            _continuation = continuation;
        }

        public Variant[] GetResult() => _result;

        public IAwaiter<Variant[]> GetAwaiter() => this;

        [UnmanagedCallersOnly]
        internal static unsafe void SignalCallback(IntPtr awaiterGCHandlePtr, jundot_variant** args, int argCount,
            jundot_bool* outAwaiterIsNull)
        {
            try
            {
                var awaiter = (SignalAwaiter)GCHandle.FromIntPtr(awaiterGCHandlePtr).Target;

                if (awaiter == null)
                {
                    *outAwaiterIsNull = jundot_bool.True;
                    return;
                }

                *outAwaiterIsNull = jundot_bool.False;

                awaiter._completed = true;

                if (argCount > 0)
                {
                    Variant[] signalArgs = new Variant[argCount];

                    for (int i = 0; i < argCount; i++)
                        signalArgs[i] = Variant.CreateCopyingBorrowed(*args[i]);

                    awaiter._result = signalArgs;
                }
                else
                {
                    awaiter._result = [];
                }

                awaiter._continuation?.Invoke();
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
                *outAwaiterIsNull = jundot_bool.False;
            }
        }
    }
}
