partial class ExportedFields
{
#pragma warning disable CS0109 // Disable warning about redundant 'new' keyword
#if TOOLS
    /// <summary>
    /// Get the default values for all properties declared in this class.
    /// This method is used by Jundot to determine the value that will be
    /// used by the inspector when resetting properties.
    /// Do not call this method.
    /// </summary>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    internal new static global::System.Collections.Generic.Dictionary<global::Jundot.StringName, global::Jundot.Variant> GetJundotPropertyDefaultValues()
    {
        var values = new global::System.Collections.Generic.Dictionary<global::Jundot.StringName, global::Jundot.Variant>(62);
        bool ___fieldBoolean_default_value = true;
        values.Add(PropertyName.@_fieldBoolean, global::Jundot.Variant.From<bool>(___fieldBoolean_default_value));
        char ___fieldChar_default_value = 'f';
        values.Add(PropertyName.@_fieldChar, global::Jundot.Variant.From<char>(___fieldChar_default_value));
        sbyte ___fieldSByte_default_value = 10;
        values.Add(PropertyName.@_fieldSByte, global::Jundot.Variant.From<sbyte>(___fieldSByte_default_value));
        short ___fieldInt16_default_value = 10;
        values.Add(PropertyName.@_fieldInt16, global::Jundot.Variant.From<short>(___fieldInt16_default_value));
        int ___fieldInt32_default_value = 10;
        values.Add(PropertyName.@_fieldInt32, global::Jundot.Variant.From<int>(___fieldInt32_default_value));
        long ___fieldInt64_default_value = -10_000;
        values.Add(PropertyName.@_fieldInt64, global::Jundot.Variant.From<long>(___fieldInt64_default_value));
        byte ___fieldByte_default_value = 10;
        values.Add(PropertyName.@_fieldByte, global::Jundot.Variant.From<byte>(___fieldByte_default_value));
        ushort ___fieldUInt16_default_value = 10;
        values.Add(PropertyName.@_fieldUInt16, global::Jundot.Variant.From<ushort>(___fieldUInt16_default_value));
        uint ___fieldUInt32_default_value = 10;
        values.Add(PropertyName.@_fieldUInt32, global::Jundot.Variant.From<uint>(___fieldUInt32_default_value));
        ulong ___fieldUInt64_default_value = 10;
        values.Add(PropertyName.@_fieldUInt64, global::Jundot.Variant.From<ulong>(___fieldUInt64_default_value));
        float ___fieldSingle_default_value = 10;
        values.Add(PropertyName.@_fieldSingle, global::Jundot.Variant.From<float>(___fieldSingle_default_value));
        double ___fieldDouble_default_value = 10;
        values.Add(PropertyName.@_fieldDouble, global::Jundot.Variant.From<double>(___fieldDouble_default_value));
        string ___fieldString_default_value = "foo";
        values.Add(PropertyName.@_fieldString, global::Jundot.Variant.From<string>(___fieldString_default_value));
        float ___fieldStaticImport_default_value = global::Jundot.Mathf.RadToDeg(2  * global::Jundot.Mathf.Pi);
        values.Add(PropertyName.@_fieldStaticImport, global::Jundot.Variant.From<float>(___fieldStaticImport_default_value));
        global::Jundot.Vector2 ___fieldVector2_default_value = new(10f, 10f);
        values.Add(PropertyName.@_fieldVector2, global::Jundot.Variant.From<global::Jundot.Vector2>(___fieldVector2_default_value));
        global::Jundot.Vector2I ___fieldVector2I_default_value = global::Jundot.Vector2I.Up;
        values.Add(PropertyName.@_fieldVector2I, global::Jundot.Variant.From<global::Jundot.Vector2I>(___fieldVector2I_default_value));
        global::Jundot.Rect2 ___fieldRect2_default_value = new(new global::Jundot.Vector2(10f, 10f), new global::Jundot.Vector2(10f, 10f));
        values.Add(PropertyName.@_fieldRect2, global::Jundot.Variant.From<global::Jundot.Rect2>(___fieldRect2_default_value));
        global::Jundot.Rect2I ___fieldRect2I_default_value = new(new global::Jundot.Vector2I(10, 10), new global::Jundot.Vector2I(10, 10));
        values.Add(PropertyName.@_fieldRect2I, global::Jundot.Variant.From<global::Jundot.Rect2I>(___fieldRect2I_default_value));
        global::Jundot.Transform2D ___fieldTransform2D_default_value = global::Jundot.Transform2D.Identity;
        values.Add(PropertyName.@_fieldTransform2D, global::Jundot.Variant.From<global::Jundot.Transform2D>(___fieldTransform2D_default_value));
        global::Jundot.Vector3 ___fieldVector3_default_value = new(10f, 10f, 10f);
        values.Add(PropertyName.@_fieldVector3, global::Jundot.Variant.From<global::Jundot.Vector3>(___fieldVector3_default_value));
        global::Jundot.Vector3I ___fieldVector3I_default_value = global::Jundot.Vector3I.Back;
        values.Add(PropertyName.@_fieldVector3I, global::Jundot.Variant.From<global::Jundot.Vector3I>(___fieldVector3I_default_value));
        global::Jundot.Basis ___fieldBasis_default_value = new global::Jundot.Basis(global::Jundot.Quaternion.Identity);
        values.Add(PropertyName.@_fieldBasis, global::Jundot.Variant.From<global::Jundot.Basis>(___fieldBasis_default_value));
        global::Jundot.Quaternion ___fieldQuaternion_default_value = new global::Jundot.Quaternion(global::Jundot.Basis.Identity);
        values.Add(PropertyName.@_fieldQuaternion, global::Jundot.Variant.From<global::Jundot.Quaternion>(___fieldQuaternion_default_value));
        global::Jundot.Transform3D ___fieldTransform3D_default_value = global::Jundot.Transform3D.Identity;
        values.Add(PropertyName.@_fieldTransform3D, global::Jundot.Variant.From<global::Jundot.Transform3D>(___fieldTransform3D_default_value));
        global::Jundot.Vector4 ___fieldVector4_default_value = new(10f, 10f, 10f, 10f);
        values.Add(PropertyName.@_fieldVector4, global::Jundot.Variant.From<global::Jundot.Vector4>(___fieldVector4_default_value));
        global::Jundot.Vector4I ___fieldVector4I_default_value = global::Jundot.Vector4I.One;
        values.Add(PropertyName.@_fieldVector4I, global::Jundot.Variant.From<global::Jundot.Vector4I>(___fieldVector4I_default_value));
        global::Jundot.Projection ___fieldProjection_default_value = global::Jundot.Projection.Identity;
        values.Add(PropertyName.@_fieldProjection, global::Jundot.Variant.From<global::Jundot.Projection>(___fieldProjection_default_value));
        global::Jundot.Aabb ___fieldAabb_default_value = new global::Jundot.Aabb(10f, 10f, 10f, new global::Jundot.Vector3(1f, 1f, 1f));
        values.Add(PropertyName.@_fieldAabb, global::Jundot.Variant.From<global::Jundot.Aabb>(___fieldAabb_default_value));
        global::Jundot.Color ___fieldColor_default_value = global::Jundot.Colors.Aquamarine;
        values.Add(PropertyName.@_fieldColor, global::Jundot.Variant.From<global::Jundot.Color>(___fieldColor_default_value));
        global::Jundot.Plane ___fieldPlane_default_value = global::Jundot.Plane.PlaneXZ;
        values.Add(PropertyName.@_fieldPlane, global::Jundot.Variant.From<global::Jundot.Plane>(___fieldPlane_default_value));
        global::Jundot.Callable ___fieldCallable_default_value = new global::Jundot.Callable(global::Jundot.Engine.GetMainLoop(), "_process");
        values.Add(PropertyName.@_fieldCallable, global::Jundot.Variant.From<global::Jundot.Callable>(___fieldCallable_default_value));
        global::Jundot.Signal ___fieldSignal_default_value = new global::Jundot.Signal(global::Jundot.Engine.GetMainLoop(), "property_list_changed");
        values.Add(PropertyName.@_fieldSignal, global::Jundot.Variant.From<global::Jundot.Signal>(___fieldSignal_default_value));
        global::ExportedFields.MyEnum ___fieldEnum_default_value = global::ExportedFields.MyEnum.C;
        values.Add(PropertyName.@_fieldEnum, global::Jundot.Variant.From<global::ExportedFields.MyEnum>(___fieldEnum_default_value));
        global::ExportedFields.MyFlagsEnum ___fieldFlagsEnum_default_value = global::ExportedFields.MyFlagsEnum.C;
        values.Add(PropertyName.@_fieldFlagsEnum, global::Jundot.Variant.From<global::ExportedFields.MyFlagsEnum>(___fieldFlagsEnum_default_value));
        byte[] ___fieldByteArray_default_value = { 0, 1, 2, 3, 4, 5, 6  };
        values.Add(PropertyName.@_fieldByteArray, global::Jundot.Variant.From<byte[]>(___fieldByteArray_default_value));
        int[] ___fieldInt32Array_default_value = { 0, 1, 2, 3, 4, 5, 6  };
        values.Add(PropertyName.@_fieldInt32Array, global::Jundot.Variant.From<int[]>(___fieldInt32Array_default_value));
        long[] ___fieldInt64Array_default_value = { 0, 1, 2, 3, 4, 5, 6  };
        values.Add(PropertyName.@_fieldInt64Array, global::Jundot.Variant.From<long[]>(___fieldInt64Array_default_value));
        float[] ___fieldSingleArray_default_value = { 0f, 1f, 2f, 3f, 4f, 5f, 6f  };
        values.Add(PropertyName.@_fieldSingleArray, global::Jundot.Variant.From<float[]>(___fieldSingleArray_default_value));
        double[] ___fieldDoubleArray_default_value = { 0d, 1d, 2d, 3d, 4d, 5d, 6d  };
        values.Add(PropertyName.@_fieldDoubleArray, global::Jundot.Variant.From<double[]>(___fieldDoubleArray_default_value));
        string[] ___fieldStringArray_default_value = { "foo", "bar"  };
        values.Add(PropertyName.@_fieldStringArray, global::Jundot.Variant.From<string[]>(___fieldStringArray_default_value));
        string[] ___fieldStringArrayEnum_default_value = { "foo", "bar"  };
        values.Add(PropertyName.@_fieldStringArrayEnum, global::Jundot.Variant.From<string[]>(___fieldStringArrayEnum_default_value));
        global::Jundot.Vector2[] ___fieldVector2Array_default_value = { global::Jundot.Vector2.Up, global::Jundot.Vector2.Down, global::Jundot.Vector2.Left, global::Jundot.Vector2.Right   };
        values.Add(PropertyName.@_fieldVector2Array, global::Jundot.Variant.From<global::Jundot.Vector2[]>(___fieldVector2Array_default_value));
        global::Jundot.Vector3[] ___fieldVector3Array_default_value = { global::Jundot.Vector3.Up, global::Jundot.Vector3.Down, global::Jundot.Vector3.Left, global::Jundot.Vector3.Right   };
        values.Add(PropertyName.@_fieldVector3Array, global::Jundot.Variant.From<global::Jundot.Vector3[]>(___fieldVector3Array_default_value));
        global::Jundot.Color[] ___fieldColorArray_default_value = { global::Jundot.Colors.Aqua, global::Jundot.Colors.Aquamarine, global::Jundot.Colors.Azure, global::Jundot.Colors.Beige   };
        values.Add(PropertyName.@_fieldColorArray, global::Jundot.Variant.From<global::Jundot.Color[]>(___fieldColorArray_default_value));
        global::Jundot.JundotObject[] ___fieldJundotObjectOrDerivedArray_default_value = { null  };
        values.Add(PropertyName.@_fieldJundotObjectOrDerivedArray, global::Jundot.Variant.CreateFrom(___fieldJundotObjectOrDerivedArray_default_value));
        global::Jundot.StringName[] ___fieldStringNameArray_default_value = { "foo", "bar"  };
        values.Add(PropertyName.@_fieldStringNameArray, global::Jundot.Variant.From<global::Jundot.StringName[]>(___fieldStringNameArray_default_value));
        global::Jundot.NodePath[] ___fieldNodePathArray_default_value = { "foo", "bar"  };
        values.Add(PropertyName.@_fieldNodePathArray, global::Jundot.Variant.From<global::Jundot.NodePath[]>(___fieldNodePathArray_default_value));
        global::Jundot.Rid[] ___fieldRidArray_default_value = { default, default, default  };
        values.Add(PropertyName.@_fieldRidArray, global::Jundot.Variant.From<global::Jundot.Rid[]>(___fieldRidArray_default_value));
        int[] ___fieldEmptyInt32Array_default_value = global::System.Array.Empty<int>();
        values.Add(PropertyName.@_fieldEmptyInt32Array, global::Jundot.Variant.From<int[]>(___fieldEmptyInt32Array_default_value));
        int[] ___fieldArrayFromList_default_value = new global::System.Collections.Generic.List<int>(global::System.Array.Empty<int>()).ToArray();
        values.Add(PropertyName.@_fieldArrayFromList, global::Jundot.Variant.From<int[]>(___fieldArrayFromList_default_value));
        global::Jundot.Variant ___fieldVariant_default_value = "foo";
        values.Add(PropertyName.@_fieldVariant, global::Jundot.Variant.From<global::Jundot.Variant>(___fieldVariant_default_value));
        global::Jundot.JundotObject ___fieldJundotObjectOrDerived_default_value = default;
        values.Add(PropertyName.@_fieldJundotObjectOrDerived, global::Jundot.Variant.From<global::Jundot.JundotObject>(___fieldJundotObjectOrDerived_default_value));
        global::Jundot.Texture ___fieldJundotResourceTexture_default_value = default;
        values.Add(PropertyName.@_fieldJundotResourceTexture, global::Jundot.Variant.From<global::Jundot.Texture>(___fieldJundotResourceTexture_default_value));
        global::Jundot.Texture ___fieldJundotResourceTextureWithInitializer_default_value = new()  { ResourceName  = ""   };
        values.Add(PropertyName.@_fieldJundotResourceTextureWithInitializer, global::Jundot.Variant.From<global::Jundot.Texture>(___fieldJundotResourceTextureWithInitializer_default_value));
        global::Jundot.StringName ___fieldStringName_default_value = new global::Jundot.StringName("foo");
        values.Add(PropertyName.@_fieldStringName, global::Jundot.Variant.From<global::Jundot.StringName>(___fieldStringName_default_value));
        global::Jundot.NodePath ___fieldNodePath_default_value = new global::Jundot.NodePath("foo");
        values.Add(PropertyName.@_fieldNodePath, global::Jundot.Variant.From<global::Jundot.NodePath>(___fieldNodePath_default_value));
        global::Jundot.Rid ___fieldRid_default_value = default;
        values.Add(PropertyName.@_fieldRid, global::Jundot.Variant.From<global::Jundot.Rid>(___fieldRid_default_value));
        global::Jundot.Collections.Dictionary ___fieldJundotDictionary_default_value = new()  { { "foo", 10  }, { global::Jundot.Vector2.Up, global::Jundot.Colors.Chocolate   }  };
        values.Add(PropertyName.@_fieldJundotDictionary, global::Jundot.Variant.From<global::Jundot.Collections.Dictionary>(___fieldJundotDictionary_default_value));
        global::Jundot.Collections.Array ___fieldJundotArray_default_value = new()  { "foo", 10, global::Jundot.Vector2.Up, global::Jundot.Colors.Chocolate   };
        values.Add(PropertyName.@_fieldJundotArray, global::Jundot.Variant.From<global::Jundot.Collections.Array>(___fieldJundotArray_default_value));
        global::Jundot.Collections.Dictionary<string, bool> ___fieldJundotGenericDictionary_default_value = new()  { { "foo", true  }, { "bar", false  }  };
        values.Add(PropertyName.@_fieldJundotGenericDictionary, global::Jundot.Variant.CreateFrom(___fieldJundotGenericDictionary_default_value));
        global::Jundot.Collections.Array<int> ___fieldJundotGenericArray_default_value = new()  { 0, 1, 2, 3, 4, 5, 6  };
        values.Add(PropertyName.@_fieldJundotGenericArray, global::Jundot.Variant.CreateFrom(___fieldJundotGenericArray_default_value));
        long[] ___fieldEmptyInt64Array_default_value = global::System.Array.Empty<long>();
        values.Add(PropertyName.@_fieldEmptyInt64Array, global::Jundot.Variant.From<long[]>(___fieldEmptyInt64Array_default_value));
        return values;
    }
#endif // TOOLS
#pragma warning restore CS0109
}
