using System;
using System.Runtime.CompilerServices;
using Jundot;
using Jundot.NativeInterop;

namespace JundotTools.Internals
{
    public class EditorProgress : IDisposable
    {
        public string Task { get; }

        public EditorProgress(string task, string label, int amount, bool canCancel = false)
        {
            Task = task;
            using jundot_string taskIn = Marshaling.ConvertStringToNative(task);
            using jundot_string labelIn = Marshaling.ConvertStringToNative(label);
            Internal.jundot_icall_EditorProgress_Create(taskIn, labelIn, amount, canCancel);
        }

        ~EditorProgress()
        {
            // Should never rely on the GC to dispose EditorProgress.
            // It should be disposed immediately when the task finishes.
            GD.PushError("EditorProgress disposed by the Garbage Collector");
            Dispose();
        }

        public void Dispose()
        {
            using jundot_string taskIn = Marshaling.ConvertStringToNative(Task);
            Internal.jundot_icall_EditorProgress_Dispose(taskIn);
            GC.SuppressFinalize(this);
        }

        public void Step(string state, int step = -1, bool forceRefresh = true)
        {
            using jundot_string taskIn = Marshaling.ConvertStringToNative(Task);
            using jundot_string stateIn = Marshaling.ConvertStringToNative(state);
            Internal.jundot_icall_EditorProgress_Step(taskIn, stateIn, step, forceRefresh);
        }

        public bool TryStep(string state, int step = -1, bool forceRefresh = true)
        {
            using jundot_string taskIn = Marshaling.ConvertStringToNative(Task);
            using jundot_string stateIn = Marshaling.ConvertStringToNative(state);
            return Internal.jundot_icall_EditorProgress_Step(taskIn, stateIn, step, forceRefresh);
        }
    }
}
