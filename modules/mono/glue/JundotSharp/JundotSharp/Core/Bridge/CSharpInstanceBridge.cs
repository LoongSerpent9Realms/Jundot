using System;
using System.Runtime.InteropServices;
using Jundot.NativeInterop;

namespace Jundot.Bridge
{
    internal static class CSharpInstanceBridge
    {
        [UnmanagedCallersOnly]
        internal static unsafe jundot_bool Call(IntPtr jundotObjectGCHandle, jundot_string_name* method,
            jundot_variant** args, int argCount, jundot_variant_call_error* refCallError, jundot_variant* ret)
        {
            try
            {
                var jundotObject = (JundotObject)GCHandle.FromIntPtr(jundotObjectGCHandle).Target;

                if (jundotObject == null)
                {
                    *ret = default;
                    (*refCallError).Error = jundot_variant_call_error_error.JUNDOT_CALL_ERROR_CALL_ERROR_INSTANCE_IS_NULL;
                    return jundot_bool.False;
                }

                bool methodInvoked = jundotObject.InvokeJundotClassMethod(CustomUnsafe.AsRef(method),
                    new NativeVariantPtrArgs(args, argCount), out jundot_variant retValue);

                if (!methodInvoked)
                {
                    *ret = default;
                    // This is important, as it tells Object::call that no method was called.
                    // Otherwise, it would prevent Object::call from calling native methods.
                    (*refCallError).Error = jundot_variant_call_error_error.JUNDOT_CALL_ERROR_CALL_ERROR_INVALID_METHOD;
                    return jundot_bool.False;
                }

                *ret = retValue;
                return jundot_bool.True;
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
                *ret = default;
                return jundot_bool.False;
            }
        }

        [UnmanagedCallersOnly]
        internal static unsafe jundot_bool Set(IntPtr jundotObjectGCHandle, jundot_string_name* name, jundot_variant* value)
        {
            try
            {
                var jundotObject = (JundotObject)GCHandle.FromIntPtr(jundotObjectGCHandle).Target;

                if (jundotObject == null)
                    throw new InvalidOperationException();

                if (jundotObject.SetJundotClassPropertyValue(CustomUnsafe.AsRef(name), CustomUnsafe.AsRef(value)))
                {
                    return jundot_bool.True;
                }

                if (!jundotObject.HasJundotClassMethod(JundotObject.MethodName._Set.NativeValue.DangerousSelfRef))
                {
                    return jundot_bool.False;
                }

                var nameManaged = StringName.CreateTakingOwnershipOfDisposableValue(
                    NativeFuncs.jundotsharp_string_name_new_copy(CustomUnsafe.AsRef(name)));

                Variant valueManaged = Variant.CreateCopyingBorrowed(*value);

                return jundotObject._Set(nameManaged, valueManaged).ToJundotBool();
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
                return jundot_bool.False;
            }
        }

        [UnmanagedCallersOnly]
        internal static unsafe jundot_bool Get(IntPtr jundotObjectGCHandle, jundot_string_name* name,
            jundot_variant* outRet)
        {
            try
            {
                var jundotObject = (JundotObject)GCHandle.FromIntPtr(jundotObjectGCHandle).Target;

                if (jundotObject == null)
                    throw new InvalidOperationException();

                // Properties
                if (jundotObject.GetJundotClassPropertyValue(CustomUnsafe.AsRef(name), out jundot_variant outRetValue))
                {
                    *outRet = outRetValue;
                    return jundot_bool.True;
                }

                // Signals
                if (jundotObject.HasJundotClassSignal(CustomUnsafe.AsRef(name)))
                {
                    jundot_signal signal = new jundot_signal(NativeFuncs.jundotsharp_string_name_new_copy(*name), jundotObject.GetInstanceId());
                    *outRet = VariantUtils.CreateFromSignalTakingOwnershipOfDisposableValue(signal);
                    return jundot_bool.True;
                }

                // Methods
                if (jundotObject.HasJundotClassMethod(CustomUnsafe.AsRef(name)))
                {
                    jundot_callable method = new jundot_callable(NativeFuncs.jundotsharp_string_name_new_copy(*name), jundotObject.GetInstanceId());
                    *outRet = VariantUtils.CreateFromCallableTakingOwnershipOfDisposableValue(method);
                    return jundot_bool.True;
                }

                if (!jundotObject.HasJundotClassMethod(JundotObject.MethodName._Get.NativeValue.DangerousSelfRef))
                {
                    return jundot_bool.False;
                }

                var nameManaged = StringName.CreateTakingOwnershipOfDisposableValue(
                    NativeFuncs.jundotsharp_string_name_new_copy(CustomUnsafe.AsRef(name)));

                Variant ret = jundotObject._Get(nameManaged);

                if (ret.VariantType == Variant.Type.Nil)
                {
                    *outRet = default;
                    return jundot_bool.False;
                }

                *outRet = ret.CopyNativeVariant();
                return jundot_bool.True;
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
                *outRet = default;
                return jundot_bool.False;
            }
        }

        [UnmanagedCallersOnly]
        internal static void CallDispose(IntPtr jundotObjectGCHandle, jundot_bool okIfNull)
        {
            try
            {
                var jundotObject = (JundotObject)GCHandle.FromIntPtr(jundotObjectGCHandle).Target;

                if (okIfNull.ToBool())
                    jundotObject?.Dispose();
                else
                    jundotObject!.Dispose();
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
            }
        }

        [UnmanagedCallersOnly]
        internal static unsafe void CallToString(IntPtr jundotObjectGCHandle, jundot_string* outRes, jundot_bool* outValid)
        {
            try
            {
                var self = (JundotObject)GCHandle.FromIntPtr(jundotObjectGCHandle).Target;

                if (self == null)
                {
                    *outRes = default;
                    *outValid = jundot_bool.False;
                    return;
                }

                var resultStr = self.ToString();

                if (resultStr == null)
                {
                    *outRes = default;
                    *outValid = jundot_bool.False;
                    return;
                }

                *outRes = Marshaling.ConvertStringToNative(resultStr);
                *outValid = jundot_bool.True;
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
                *outRes = default;
                *outValid = jundot_bool.False;
            }
        }

        [UnmanagedCallersOnly]
        internal static unsafe jundot_bool HasMethodUnknownParams(IntPtr jundotObjectGCHandle, jundot_string_name* method)
        {
            try
            {
                var jundotObject = (JundotObject)GCHandle.FromIntPtr(jundotObjectGCHandle).Target;

                if (jundotObject == null)
                    return jundot_bool.False;

                return jundotObject.HasJundotClassMethod(CustomUnsafe.AsRef(method)).ToJundotBool();
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
                return jundot_bool.False;
            }
        }

        [UnmanagedCallersOnly]
        internal static unsafe void SerializeState(
            IntPtr jundotObjectGCHandle,
            jundot_dictionary* propertiesState,
            jundot_dictionary* signalEventsState
        )
        {
            try
            {
                var jundotObject = (JundotObject)GCHandle.FromIntPtr(jundotObjectGCHandle).Target;

                if (jundotObject == null)
                    return;

                // Call OnBeforeSerialize

                // ReSharper disable once SuspiciousTypeConversion.Global
                if (jundotObject is ISerializationListener serializationListener)
                    serializationListener.OnBeforeSerialize();

                // Save instance state

                using var info = JundotSerializationInfo.CreateCopyingBorrowed(
                    *propertiesState, *signalEventsState);

                jundotObject.SaveJundotObjectData(info);
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
            }
        }

        [UnmanagedCallersOnly]
        internal static unsafe void DeserializeState(
            IntPtr jundotObjectGCHandle,
            jundot_dictionary* propertiesState,
            jundot_dictionary* signalEventsState
        )
        {
            try
            {
                var jundotObject = (JundotObject)GCHandle.FromIntPtr(jundotObjectGCHandle).Target;

                if (jundotObject == null)
                    return;

                // Restore instance state

                using var info = JundotSerializationInfo.CreateCopyingBorrowed(
                    *propertiesState, *signalEventsState);

                jundotObject.RestoreJundotObjectData(info);

                // Call OnAfterDeserialize

                // ReSharper disable once SuspiciousTypeConversion.Global
                if (jundotObject is ISerializationListener serializationListener)
                    serializationListener.OnAfterDeserialize();
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
            }
        }
    }
}
