#pragma warning disable CA1707 // Identifiers should not contain underscores
#pragma warning disable IDE1006 // Naming rule violation
// ReSharper disable InconsistentNaming

namespace Jundot.NativeInterop
{
    public static partial class NativeFuncs
    {
        public static jundot_variant jundotsharp_variant_new_copy(scoped in jundot_variant src)
        {
            switch (src.Type)
            {
                case Variant.Type.Nil:
                    return default;
                case Variant.Type.Bool:
                    return new jundot_variant() { Bool = src.Bool, Type = Variant.Type.Bool };
                case Variant.Type.Int:
                    return new jundot_variant() { Int = src.Int, Type = Variant.Type.Int };
                case Variant.Type.Float:
                    return new jundot_variant() { Float = src.Float, Type = Variant.Type.Float };
                case Variant.Type.Vector2:
                    return new jundot_variant() { Vector2 = src.Vector2, Type = Variant.Type.Vector2 };
                case Variant.Type.Vector2I:
                    return new jundot_variant() { Vector2I = src.Vector2I, Type = Variant.Type.Vector2I };
                case Variant.Type.Rect2:
                    return new jundot_variant() { Rect2 = src.Rect2, Type = Variant.Type.Rect2 };
                case Variant.Type.Rect2I:
                    return new jundot_variant() { Rect2I = src.Rect2I, Type = Variant.Type.Rect2I };
                case Variant.Type.Vector3:
                    return new jundot_variant() { Vector3 = src.Vector3, Type = Variant.Type.Vector3 };
                case Variant.Type.Vector3I:
                    return new jundot_variant() { Vector3I = src.Vector3I, Type = Variant.Type.Vector3I };
                case Variant.Type.Vector4:
                    return new jundot_variant() { Vector4 = src.Vector4, Type = Variant.Type.Vector4 };
                case Variant.Type.Vector4I:
                    return new jundot_variant() { Vector4I = src.Vector4I, Type = Variant.Type.Vector4I };
                case Variant.Type.Plane:
                    return new jundot_variant() { Plane = src.Plane, Type = Variant.Type.Plane };
                case Variant.Type.Quaternion:
                    return new jundot_variant() { Quaternion = src.Quaternion, Type = Variant.Type.Quaternion };
                case Variant.Type.Color:
                    return new jundot_variant() { Color = src.Color, Type = Variant.Type.Color };
                case Variant.Type.Rid:
                    return new jundot_variant() { Rid = src.Rid, Type = Variant.Type.Rid };
            }

            jundotsharp_variant_new_copy(out jundot_variant ret, src);
            return ret;
        }

        public static jundot_string_name jundotsharp_string_name_new_copy(scoped in jundot_string_name src)
        {
            if (src.IsEmpty)
                return default;
            jundotsharp_string_name_new_copy(out jundot_string_name ret, src);
            return ret;
        }

        public static jundot_node_path jundotsharp_node_path_new_copy(scoped in jundot_node_path src)
        {
            if (src.IsEmpty)
                return default;
            jundotsharp_node_path_new_copy(out jundot_node_path ret, src);
            return ret;
        }

        public static jundot_array jundotsharp_array_new()
        {
            jundotsharp_array_new(out jundot_array ret);
            return ret;
        }

        public static jundot_array jundotsharp_array_new_copy(scoped in jundot_array src)
        {
            jundotsharp_array_new_copy(out jundot_array ret, src);
            return ret;
        }

        public static jundot_dictionary jundotsharp_dictionary_new()
        {
            jundotsharp_dictionary_new(out jundot_dictionary ret);
            return ret;
        }

        public static jundot_dictionary jundotsharp_dictionary_new_copy(scoped in jundot_dictionary src)
        {
            jundotsharp_dictionary_new_copy(out jundot_dictionary ret, src);
            return ret;
        }

        public static jundot_string_name jundotsharp_string_name_new_from_string(string name)
        {
            using jundot_string src = Marshaling.ConvertStringToNative(name);
            jundotsharp_string_name_new_from_string(out jundot_string_name ret, src);
            return ret;
        }

        public static jundot_node_path jundotsharp_node_path_new_from_string(string name)
        {
            using jundot_string src = Marshaling.ConvertStringToNative(name);
            jundotsharp_node_path_new_from_string(out jundot_node_path ret, src);
            return ret;
        }
    }
}
