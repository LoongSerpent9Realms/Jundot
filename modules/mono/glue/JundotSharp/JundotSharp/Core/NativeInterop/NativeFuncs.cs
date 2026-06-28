#pragma warning disable CA1707 // Identifiers should not contain underscores
#pragma warning disable IDE1006 // Naming rule violation
// ReSharper disable InconsistentNaming

using System;
using System.Runtime.CompilerServices;
using Jundot.SourceGenerators.Internal;


namespace Jundot.NativeInterop
{
    /*
     * IMPORTANT:
     * The order of the methods defined in NativeFuncs must match the order
     * in the array defined at the bottom of 'glue/runtime_interop.cpp'.
     */

    [GenerateUnmanagedCallbacks(typeof(UnmanagedCallbacks))]
    public static unsafe partial class NativeFuncs
    {
        private static bool initialized;

        // ReSharper disable once ParameterOnlyUsedForPreconditionCheck.Global
        public static void Initialize(IntPtr unmanagedCallbacks, int unmanagedCallbacksSize)
        {
            if (initialized)
                throw new InvalidOperationException("Already initialized.");
            initialized = true;

            if (unmanagedCallbacksSize != sizeof(UnmanagedCallbacks))
                throw new ArgumentException("Unmanaged callbacks size mismatch.", nameof(unmanagedCallbacksSize));

            _unmanagedCallbacks = Unsafe.AsRef<UnmanagedCallbacks>((void*)unmanagedCallbacks);
        }

        private partial struct UnmanagedCallbacks
        {
        }

        // Custom functions

        internal static partial jundot_bool jundotsharp_dotnet_module_is_initialized();

        public static partial IntPtr jundotsharp_method_bind_get_method(in jundot_string_name p_classname,
            in jundot_string_name p_methodname);

        public static partial IntPtr jundotsharp_method_bind_get_method_with_compatibility(
            in jundot_string_name p_classname, in jundot_string_name p_methodname, ulong p_hash);

        public static partial delegate* unmanaged<jundot_bool, IntPtr> jundotsharp_get_class_constructor(
            in jundot_string_name p_classname);

        public static partial IntPtr jundotsharp_engine_get_singleton(in jundot_string p_name);


        internal static partial Error jundotsharp_stack_info_vector_resize(
            ref DebuggingUtils.jundot_stack_info_vector p_stack_info_vector, int p_size);

        internal static partial void jundotsharp_stack_info_vector_destroy(
            ref DebuggingUtils.jundot_stack_info_vector p_stack_info_vector);

        internal static partial void jundotsharp_internal_editor_file_system_update_files(in jundot_packed_string_array p_script_paths);

        internal static partial void jundotsharp_internal_script_debugger_send_error(in jundot_string p_func,
            in jundot_string p_file, int p_line, in jundot_string p_err, in jundot_string p_descr,
            jundot_error_handler_type p_type, in DebuggingUtils.jundot_stack_info_vector p_stack_info_vector);

        internal static partial jundot_bool jundotsharp_internal_script_debugger_is_active();

        internal static partial IntPtr jundotsharp_internal_object_get_associated_gchandle(IntPtr ptr);

        internal static partial void jundotsharp_internal_object_disposed(IntPtr ptr, IntPtr gcHandleToFree);

        internal static partial void jundotsharp_internal_refcounted_disposed(IntPtr ptr, IntPtr gcHandleToFree,
            jundot_bool isFinalizer);

        internal static partial Error jundotsharp_internal_signal_awaiter_connect(IntPtr source,
            in jundot_string_name signal,
            IntPtr target, IntPtr awaiterHandlePtr);

        internal static partial void jundotsharp_internal_tie_native_managed_to_unmanaged(IntPtr gcHandleIntPtr,
            IntPtr unmanaged, in jundot_string_name nativeName, jundot_bool refCounted);

        internal static partial void jundotsharp_internal_tie_user_managed_to_unmanaged(IntPtr gcHandleIntPtr,
            IntPtr unmanaged, jundot_ref* scriptPtr, jundot_bool refCounted);

        internal static partial void jundotsharp_internal_tie_managed_to_unmanaged_with_pre_setup(
            IntPtr gcHandleIntPtr, IntPtr unmanaged);

        internal static partial IntPtr jundotsharp_internal_unmanaged_get_script_instance_managed(IntPtr p_unmanaged,
            out jundot_bool r_has_cs_script_instance);

        internal static partial IntPtr jundotsharp_internal_unmanaged_get_instance_binding_managed(IntPtr p_unmanaged);

        internal static partial IntPtr jundotsharp_internal_unmanaged_instance_binding_create_managed(IntPtr p_unmanaged,
            IntPtr oldGCHandlePtr);

        internal static partial void jundotsharp_internal_new_csharp_script(jundot_ref* r_dest);

        internal static partial jundot_bool jundotsharp_internal_script_load(in jundot_string p_path, jundot_ref* r_dest);

        internal static partial void jundotsharp_internal_reload_registered_script(IntPtr scriptPtr);

        internal static partial void jundotsharp_array_filter_jundot_objects_by_native(scoped in jundot_string_name p_native_name,
            scoped in jundot_array p_input, out jundot_array r_output);

        internal static partial void jundotsharp_array_filter_jundot_objects_by_non_native(scoped in jundot_array p_input,
            out jundot_array r_output);

        public static partial void jundotsharp_ref_new_from_ref_counted_ptr(out jundot_ref r_dest,
            IntPtr p_ref_counted_ptr);

        public static partial void jundotsharp_ref_destroy(ref jundot_ref p_instance);

        public static partial void jundotsharp_string_name_new_from_string(out jundot_string_name r_dest,
            scoped in jundot_string p_name);

        public static partial void jundotsharp_node_path_new_from_string(out jundot_node_path r_dest,
            scoped in jundot_string p_name);

        public static partial void
            jundotsharp_string_name_as_string(out jundot_string r_dest, scoped in jundot_string_name p_name);

        public static partial void jundotsharp_node_path_as_string(out jundot_string r_dest, scoped in jundot_node_path p_np);

        public static partial jundot_packed_byte_array jundotsharp_packed_byte_array_new_mem_copy(byte* p_src,
            int p_length);

        public static partial jundot_packed_int32_array jundotsharp_packed_int32_array_new_mem_copy(int* p_src,
            int p_length);

        public static partial jundot_packed_int64_array jundotsharp_packed_int64_array_new_mem_copy(long* p_src,
            int p_length);

        public static partial jundot_packed_float32_array jundotsharp_packed_float32_array_new_mem_copy(float* p_src,
            int p_length);

        public static partial jundot_packed_float64_array jundotsharp_packed_float64_array_new_mem_copy(double* p_src,
            int p_length);

        public static partial jundot_packed_vector2_array jundotsharp_packed_vector2_array_new_mem_copy(Vector2* p_src,
            int p_length);

        public static partial jundot_packed_vector3_array jundotsharp_packed_vector3_array_new_mem_copy(Vector3* p_src,
            int p_length);

        public static partial jundot_packed_vector4_array jundotsharp_packed_vector4_array_new_mem_copy(Vector4* p_src,
            int p_length);

        public static partial jundot_packed_color_array jundotsharp_packed_color_array_new_mem_copy(Color* p_src,
            int p_length);

        public static partial void jundotsharp_packed_string_array_add(ref jundot_packed_string_array r_dest,
            in jundot_string p_element);

        public static partial void jundotsharp_callable_new_with_delegate(IntPtr p_delegate_handle, IntPtr p_trampoline,
            IntPtr p_object, out jundot_callable r_callable);

        internal static partial jundot_bool jundotsharp_callable_get_data_for_marshalling(scoped in jundot_callable p_callable,
            out IntPtr r_delegate_handle, out IntPtr r_trampoline, out IntPtr r_object, out jundot_string_name r_name);

        internal static partial jundot_variant jundotsharp_callable_call(scoped in jundot_callable p_callable,
            jundot_variant** p_args, int p_arg_count, out jundot_variant_call_error p_call_error);

        internal static partial void jundotsharp_callable_call_deferred(in jundot_callable p_callable,
            jundot_variant** p_args, int p_arg_count);

        internal static partial Color jundotsharp_color_from_ok_hsl(float p_h, float p_s, float p_l, float p_alpha);

        internal static partial float jundotsharp_color_get_ok_hsl_h(in Color p_self);

        internal static partial float jundotsharp_color_get_ok_hsl_s(in Color p_self);

        internal static partial float jundotsharp_color_get_ok_hsl_l(in Color p_self);

        // GDNative functions

        // gdnative.h

        public static partial void jundotsharp_method_bind_ptrcall(IntPtr p_method_bind, IntPtr p_instance, void** p_args,
            void* p_ret);

        public static partial jundot_variant jundotsharp_method_bind_call(IntPtr p_method_bind, IntPtr p_instance,
            jundot_variant** p_args, int p_arg_count, out jundot_variant_call_error p_call_error);

        // variant.h

        public static partial void
            jundotsharp_variant_new_string_name(out jundot_variant r_dest, scoped in jundot_string_name p_s);

        public static partial void jundotsharp_variant_new_copy(out jundot_variant r_dest, scoped in jundot_variant p_src);

        public static partial void jundotsharp_variant_new_node_path(out jundot_variant r_dest, scoped in jundot_node_path p_np);

        public static partial void jundotsharp_variant_new_object(out jundot_variant r_dest, IntPtr p_obj);

        public static partial void jundotsharp_variant_new_transform2d(out jundot_variant r_dest, scoped in Transform2D p_t2d);

        public static partial void jundotsharp_variant_new_basis(out jundot_variant r_dest, scoped in Basis p_basis);

        public static partial void jundotsharp_variant_new_transform3d(out jundot_variant r_dest, scoped in Transform3D p_trans);

        public static partial void jundotsharp_variant_new_projection(out jundot_variant r_dest, scoped in Projection p_proj);

        public static partial void jundotsharp_variant_new_aabb(out jundot_variant r_dest, scoped in Aabb p_aabb);

        public static partial void jundotsharp_variant_new_dictionary(out jundot_variant r_dest,
            scoped in jundot_dictionary p_dict);

        public static partial void jundotsharp_variant_new_array(out jundot_variant r_dest, scoped in jundot_array p_arr);

        public static partial void jundotsharp_variant_new_packed_byte_array(out jundot_variant r_dest,
            scoped in jundot_packed_byte_array p_pba);

        public static partial void jundotsharp_variant_new_packed_int32_array(out jundot_variant r_dest,
            scoped in jundot_packed_int32_array p_pia);

        public static partial void jundotsharp_variant_new_packed_int64_array(out jundot_variant r_dest,
            scoped in jundot_packed_int64_array p_pia);

        public static partial void jundotsharp_variant_new_packed_float32_array(out jundot_variant r_dest,
            scoped in jundot_packed_float32_array p_pra);

        public static partial void jundotsharp_variant_new_packed_float64_array(out jundot_variant r_dest,
            scoped in jundot_packed_float64_array p_pra);

        public static partial void jundotsharp_variant_new_packed_string_array(out jundot_variant r_dest,
            scoped in jundot_packed_string_array p_psa);

        public static partial void jundotsharp_variant_new_packed_vector2_array(out jundot_variant r_dest,
            scoped in jundot_packed_vector2_array p_pv2a);

        public static partial void jundotsharp_variant_new_packed_vector3_array(out jundot_variant r_dest,
            scoped in jundot_packed_vector3_array p_pv3a);

        public static partial void jundotsharp_variant_new_packed_vector4_array(out jundot_variant r_dest,
            scoped in jundot_packed_vector4_array p_pv4a);

        public static partial void jundotsharp_variant_new_packed_color_array(out jundot_variant r_dest,
            scoped in jundot_packed_color_array p_pca);

        public static partial jundot_bool jundotsharp_variant_as_bool(scoped in jundot_variant p_self);

        public static partial Int64 jundotsharp_variant_as_int(scoped in jundot_variant p_self);

        public static partial double jundotsharp_variant_as_float(scoped in jundot_variant p_self);

        public static partial jundot_string jundotsharp_variant_as_string(scoped in jundot_variant p_self);

        public static partial Vector2 jundotsharp_variant_as_vector2(scoped in jundot_variant p_self);

        public static partial Vector2I jundotsharp_variant_as_vector2i(scoped in jundot_variant p_self);

        public static partial Rect2 jundotsharp_variant_as_rect2(scoped in jundot_variant p_self);

        public static partial Rect2I jundotsharp_variant_as_rect2i(scoped in jundot_variant p_self);

        public static partial Vector3 jundotsharp_variant_as_vector3(scoped in jundot_variant p_self);

        public static partial Vector3I jundotsharp_variant_as_vector3i(scoped in jundot_variant p_self);

        public static partial Transform2D jundotsharp_variant_as_transform2d(scoped in jundot_variant p_self);

        public static partial Vector4 jundotsharp_variant_as_vector4(scoped in jundot_variant p_self);

        public static partial Vector4I jundotsharp_variant_as_vector4i(scoped in jundot_variant p_self);

        public static partial Plane jundotsharp_variant_as_plane(scoped in jundot_variant p_self);

        public static partial Quaternion jundotsharp_variant_as_quaternion(scoped in jundot_variant p_self);

        public static partial Aabb jundotsharp_variant_as_aabb(scoped in jundot_variant p_self);

        public static partial Basis jundotsharp_variant_as_basis(scoped in jundot_variant p_self);

        public static partial Transform3D jundotsharp_variant_as_transform3d(scoped in jundot_variant p_self);

        public static partial Projection jundotsharp_variant_as_projection(scoped in jundot_variant p_self);

        public static partial Color jundotsharp_variant_as_color(scoped in jundot_variant p_self);

        public static partial jundot_string_name jundotsharp_variant_as_string_name(scoped in jundot_variant p_self);

        public static partial jundot_node_path jundotsharp_variant_as_node_path(scoped in jundot_variant p_self);

        public static partial Rid jundotsharp_variant_as_rid(scoped in jundot_variant p_self);

        public static partial jundot_callable jundotsharp_variant_as_callable(scoped in jundot_variant p_self);

        public static partial jundot_signal jundotsharp_variant_as_signal(scoped in jundot_variant p_self);

        public static partial jundot_dictionary jundotsharp_variant_as_dictionary(scoped in jundot_variant p_self);

        public static partial jundot_array jundotsharp_variant_as_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_byte_array jundotsharp_variant_as_packed_byte_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_int32_array jundotsharp_variant_as_packed_int32_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_int64_array jundotsharp_variant_as_packed_int64_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_float32_array jundotsharp_variant_as_packed_float32_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_float64_array jundotsharp_variant_as_packed_float64_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_string_array jundotsharp_variant_as_packed_string_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_vector2_array jundotsharp_variant_as_packed_vector2_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_vector3_array jundotsharp_variant_as_packed_vector3_array(scoped in jundot_variant p_self);

        public static partial jundot_packed_vector4_array jundotsharp_variant_as_packed_vector4_array(
            in jundot_variant p_self);

        public static partial jundot_packed_color_array jundotsharp_variant_as_packed_color_array(scoped in jundot_variant p_self);

        public static partial jundot_bool jundotsharp_variant_equals(scoped in jundot_variant p_a, scoped in jundot_variant p_b);

        // string.h

        public static partial void jundotsharp_string_new_with_utf16_chars(out jundot_string r_dest, char* p_contents);

        // string_name.h

        public static partial void jundotsharp_string_name_new_copy(out jundot_string_name r_dest,
            scoped in jundot_string_name p_src);

        // node_path.h

        public static partial void jundotsharp_node_path_new_copy(out jundot_node_path r_dest, scoped in jundot_node_path p_src);

        // array.h

        public static partial void jundotsharp_array_new(out jundot_array r_dest);

        public static partial void jundotsharp_array_new_copy(out jundot_array r_dest, scoped in jundot_array p_src);

        public static partial jundot_variant* jundotsharp_array_ptrw(ref jundot_array p_self);

        // dictionary.h

        public static partial void jundotsharp_dictionary_new(out jundot_dictionary r_dest);

        public static partial void jundotsharp_dictionary_new_copy(out jundot_dictionary r_dest,
            scoped in jundot_dictionary p_src);

        // destroy functions

        public static partial void jundotsharp_packed_byte_array_destroy(ref jundot_packed_byte_array p_self);

        public static partial void jundotsharp_packed_int32_array_destroy(ref jundot_packed_int32_array p_self);

        public static partial void jundotsharp_packed_int64_array_destroy(ref jundot_packed_int64_array p_self);

        public static partial void jundotsharp_packed_float32_array_destroy(ref jundot_packed_float32_array p_self);

        public static partial void jundotsharp_packed_float64_array_destroy(ref jundot_packed_float64_array p_self);

        public static partial void jundotsharp_packed_string_array_destroy(ref jundot_packed_string_array p_self);

        public static partial void jundotsharp_packed_vector2_array_destroy(ref jundot_packed_vector2_array p_self);

        public static partial void jundotsharp_packed_vector3_array_destroy(ref jundot_packed_vector3_array p_self);

        public static partial void jundotsharp_packed_vector4_array_destroy(ref jundot_packed_vector4_array p_self);

        public static partial void jundotsharp_packed_color_array_destroy(ref jundot_packed_color_array p_self);

        public static partial void jundotsharp_variant_destroy(ref jundot_variant p_self);

        public static partial void jundotsharp_string_destroy(ref jundot_string p_self);

        public static partial void jundotsharp_string_name_destroy(ref jundot_string_name p_self);

        public static partial void jundotsharp_node_path_destroy(ref jundot_node_path p_self);

        public static partial void jundotsharp_signal_destroy(ref jundot_signal p_self);

        public static partial void jundotsharp_callable_destroy(ref jundot_callable p_self);

        public static partial void jundotsharp_array_destroy(ref jundot_array p_self);

        public static partial void jundotsharp_dictionary_destroy(ref jundot_dictionary p_self);

        // Array

        public static partial int jundotsharp_array_add(ref jundot_array p_self, in jundot_variant p_item);

        public static partial int jundotsharp_array_add_range(ref jundot_array p_self, in jundot_array p_collection);

        public static partial int jundotsharp_array_binary_search(ref jundot_array p_self, int p_index, int p_count, in jundot_variant p_value);

        public static partial void jundotsharp_array_duplicate(scoped ref jundot_array p_self, jundot_bool p_deep, out jundot_array r_dest);

        public static partial void jundotsharp_array_fill(ref jundot_array p_self, in jundot_variant p_value);

        public static partial int jundotsharp_array_index_of(ref jundot_array p_self, in jundot_variant p_item, int p_index = 0);

        public static partial void jundotsharp_array_insert(ref jundot_array p_self, int p_index, in jundot_variant p_item);

        public static partial int jundotsharp_array_last_index_of(ref jundot_array p_self, in jundot_variant p_item, int p_index);

        public static partial void jundotsharp_array_make_read_only(ref jundot_array p_self);

        public static partial void jundotsharp_array_set_typed(
            ref jundot_array p_self,
            uint p_elem_type,
            in jundot_string_name p_elem_class_name,
            in jundot_ref p_elem_script);

        public static partial jundot_bool jundotsharp_array_is_typed(ref jundot_array p_self);

        public static partial void jundotsharp_array_max(scoped ref jundot_array p_self, out jundot_variant r_value);

        public static partial void jundotsharp_array_min(scoped ref jundot_array p_self, out jundot_variant r_value);

        public static partial void jundotsharp_array_pick_random(scoped ref jundot_array p_self, out jundot_variant r_value);

        public static partial jundot_bool jundotsharp_array_recursive_equal(ref jundot_array p_self, in jundot_array p_other);

        public static partial void jundotsharp_array_remove_at(ref jundot_array p_self, int p_index);

        public static partial Error jundotsharp_array_resize(ref jundot_array p_self, int p_new_size);

        public static partial void jundotsharp_array_reverse(ref jundot_array p_self);

        public static partial void jundotsharp_array_shuffle(ref jundot_array p_self);

        public static partial void jundotsharp_array_slice(scoped ref jundot_array p_self, int p_start, int p_end,
            int p_step, jundot_bool p_deep, out jundot_array r_dest);

        public static partial void jundotsharp_array_sort(ref jundot_array p_self);

        public static partial void jundotsharp_array_to_string(ref jundot_array p_self, out jundot_string r_str);

        public static partial void jundotsharp_packed_byte_array_compress(scoped in jundot_packed_byte_array p_src, int p_mode, out jundot_packed_byte_array r_dst);

        public static partial void jundotsharp_packed_byte_array_decompress(scoped in jundot_packed_byte_array p_src, long p_buffer_size, int p_mode, out jundot_packed_byte_array r_dst);

        public static partial void jundotsharp_packed_byte_array_decompress_dynamic(scoped in jundot_packed_byte_array p_src, long p_buffer_size, int p_mode, out jundot_packed_byte_array r_dst);

        // Dictionary

        public static partial jundot_bool jundotsharp_dictionary_try_get_value(scoped ref jundot_dictionary p_self,
            scoped in jundot_variant p_key,
            out jundot_variant r_value);

        public static partial void jundotsharp_dictionary_set_value(ref jundot_dictionary p_self, in jundot_variant p_key,
            in jundot_variant p_value);

        public static partial void jundotsharp_dictionary_keys(scoped ref jundot_dictionary p_self, out jundot_array r_dest);

        public static partial void jundotsharp_dictionary_values(scoped ref jundot_dictionary p_self, out jundot_array r_dest);

        public static partial int jundotsharp_dictionary_count(ref jundot_dictionary p_self);

        public static partial void jundotsharp_dictionary_key_value_pair_at(scoped ref jundot_dictionary p_self, int p_index,
            out jundot_variant r_key, out jundot_variant r_value);

        public static partial void jundotsharp_dictionary_add(ref jundot_dictionary p_self, in jundot_variant p_key,
            in jundot_variant p_value);

        public static partial void jundotsharp_dictionary_clear(ref jundot_dictionary p_self);

        public static partial jundot_bool jundotsharp_dictionary_contains_key(ref jundot_dictionary p_self,
            in jundot_variant p_key);

        public static partial void jundotsharp_dictionary_duplicate(scoped ref jundot_dictionary p_self, jundot_bool p_deep,
            out jundot_dictionary r_dest);

        public static partial void jundotsharp_dictionary_merge(ref jundot_dictionary p_self, in jundot_dictionary p_dictionary, jundot_bool p_overwrite);

        public static partial jundot_bool jundotsharp_dictionary_recursive_equal(ref jundot_dictionary p_self, in jundot_dictionary p_other);

        public static partial jundot_bool jundotsharp_dictionary_remove_key(ref jundot_dictionary p_self,
            in jundot_variant p_key);

        public static partial void jundotsharp_dictionary_make_read_only(ref jundot_dictionary p_self);

        public static partial void jundotsharp_dictionary_set_typed(
            ref jundot_dictionary p_self,
            uint p_key_type,
            in jundot_string_name p_key_class_name,
            in jundot_ref p_key_script,
            uint p_value_type,
            in jundot_string_name p_value_class_name,
            in jundot_ref p_value_script);

        public static partial jundot_bool jundotsharp_dictionary_is_typed_key(ref jundot_dictionary p_self);

        public static partial jundot_bool jundotsharp_dictionary_is_typed_value(ref jundot_dictionary p_self);

        public static partial uint jundotsharp_dictionary_get_typed_key_builtin(ref jundot_dictionary p_self);

        public static partial uint jundotsharp_dictionary_get_typed_value_builtin(ref jundot_dictionary p_self);

        public static partial void jundotsharp_dictionary_get_typed_key_class_name(ref jundot_dictionary p_self, out jundot_string_name r_dest);

        public static partial void jundotsharp_dictionary_get_typed_value_class_name(ref jundot_dictionary p_self, out jundot_string_name r_dest);

        public static partial void jundotsharp_dictionary_get_typed_key_script(ref jundot_dictionary p_self, out jundot_variant r_dest);

        public static partial void jundotsharp_dictionary_get_typed_value_script(ref jundot_dictionary p_self, out jundot_variant r_dest);

        public static partial void jundotsharp_dictionary_to_string(scoped ref jundot_dictionary p_self, out jundot_string r_str);

        // StringExtensions

        public static partial void jundotsharp_string_simplify_path(scoped in jundot_string p_self,
            out jundot_string r_simplified_path);

        public static partial void jundotsharp_string_capitalize(scoped in jundot_string p_self,
            out jundot_string r_capitalized);

        public static partial void jundotsharp_string_to_camel_case(scoped in jundot_string p_self,
            out jundot_string r_camel_case);

        public static partial void jundotsharp_string_to_pascal_case(scoped in jundot_string p_self,
            out jundot_string r_pascal_case);

        public static partial void jundotsharp_string_to_snake_case(scoped in jundot_string p_self,
            out jundot_string r_snake_case);

        public static partial void jundotsharp_string_to_kebab_case(scoped in jundot_string p_self,
            out jundot_string r_kebab_case);

        // NodePath

        public static partial void jundotsharp_node_path_get_as_property_path(in jundot_node_path p_self,
            ref jundot_node_path r_dest);

        public static partial void jundotsharp_node_path_get_concatenated_names(scoped in jundot_node_path p_self,
            out jundot_string r_names);

        public static partial void jundotsharp_node_path_get_concatenated_subnames(scoped in jundot_node_path p_self,
            out jundot_string r_subnames);

        public static partial void jundotsharp_node_path_get_name(scoped in jundot_node_path p_self, int p_idx,
            out jundot_string r_name);

        public static partial int jundotsharp_node_path_get_name_count(in jundot_node_path p_self);

        public static partial void jundotsharp_node_path_get_subname(scoped in jundot_node_path p_self, int p_idx,
            out jundot_string r_subname);

        public static partial int jundotsharp_node_path_get_subname_count(in jundot_node_path p_self);

        public static partial jundot_bool jundotsharp_node_path_is_absolute(in jundot_node_path p_self);

        public static partial jundot_bool jundotsharp_node_path_equals(in jundot_node_path p_self, in jundot_node_path p_other);

        public static partial int jundotsharp_node_path_hash(in jundot_node_path p_self);

        // GD, etc

        internal static partial void jundotsharp_bytes_to_var(scoped in jundot_packed_byte_array p_bytes,
            jundot_bool p_allow_objects,
            out jundot_variant r_ret);

        internal static partial void jundotsharp_convert(scoped in jundot_variant p_what, int p_type,
            out jundot_variant r_ret);

        internal static partial int jundotsharp_hash(in jundot_variant p_var);

        internal static partial IntPtr jundotsharp_instance_from_id(ulong p_instance_id);

        internal static partial void jundotsharp_print(in jundot_string p_what);

        public static partial void jundotsharp_print_rich(in jundot_string p_what);

        internal static partial void jundotsharp_printerr(in jundot_string p_what);

        internal static partial void jundotsharp_printraw(in jundot_string p_what);

        internal static partial void jundotsharp_prints(in jundot_string p_what);

        internal static partial void jundotsharp_printt(in jundot_string p_what);

        internal static partial float jundotsharp_randf();

        internal static partial uint jundotsharp_randi();

        internal static partial void jundotsharp_randomize();

        internal static partial double jundotsharp_randf_range(double from, double to);

        internal static partial double jundotsharp_randfn(double mean, double deviation);

        internal static partial int jundotsharp_randi_range(int from, int to);

        internal static partial uint jundotsharp_rand_from_seed(ulong seed, out ulong newSeed);

        internal static partial void jundotsharp_seed(ulong seed);

        internal static partial void jundotsharp_weakref(IntPtr p_obj, out jundot_ref r_weak_ref);

        internal static partial void jundotsharp_str_to_var(scoped in jundot_string p_str, out jundot_variant r_ret);

        internal static partial void jundotsharp_var_to_bytes(scoped in jundot_variant p_what, jundot_bool p_full_objects,
            out jundot_packed_byte_array r_bytes);

        internal static partial void jundotsharp_var_to_str(scoped in jundot_variant p_var, out jundot_string r_ret);

        internal static partial void jundotsharp_err_print_error(in jundot_string p_function, in jundot_string p_file, int p_line, in jundot_string p_error, in jundot_string p_message = default, jundot_bool p_editor_notify = jundot_bool.False, jundot_error_handler_type p_type = jundot_error_handler_type.ERR_HANDLER_ERROR);

        // Object

        public static partial void jundotsharp_object_to_string(IntPtr ptr, out jundot_string r_str);

        // Vector

        public static partial long jundotsharp_string_size(in jundot_string p_self);

        public static partial long jundotsharp_packed_byte_array_size(in jundot_packed_byte_array p_self);

        public static partial long jundotsharp_packed_int32_array_size(in jundot_packed_int32_array p_self);

        public static partial long jundotsharp_packed_int64_array_size(in jundot_packed_int64_array p_self);

        public static partial long jundotsharp_packed_float32_array_size(in jundot_packed_float32_array p_self);

        public static partial long jundotsharp_packed_float64_array_size(in jundot_packed_float64_array p_self);

        public static partial long jundotsharp_packed_string_array_size(in jundot_packed_string_array p_self);

        public static partial long jundotsharp_packed_vector2_array_size(in jundot_packed_vector2_array p_self);

        public static partial long jundotsharp_packed_vector3_array_size(in jundot_packed_vector3_array p_self);

        public static partial long jundotsharp_packed_vector4_array_size(in jundot_packed_vector4_array p_self);

        public static partial long jundotsharp_packed_color_array_size(in jundot_packed_color_array p_self);

        public static partial long jundotsharp_array_size(in jundot_array p_self);
    }
}
