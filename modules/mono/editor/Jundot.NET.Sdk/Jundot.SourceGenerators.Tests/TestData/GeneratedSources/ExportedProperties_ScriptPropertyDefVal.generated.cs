partial class ExportedProperties
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
        var values = new global::System.Collections.Generic.Dictionary<global::Jundot.StringName, global::Jundot.Variant>(71);
        string __NotGenerateComplexLamdaProperty_default_value = default;
        values.Add(PropertyName.@NotGenerateComplexLamdaProperty, global::Jundot.Variant.From<string>(__NotGenerateComplexLamdaProperty_default_value));
        string __NotGenerateLamdaNoFieldProperty_default_value = default;
        values.Add(PropertyName.@NotGenerateLamdaNoFieldProperty, global::Jundot.Variant.From<string>(__NotGenerateLamdaNoFieldProperty_default_value));
        string __NotGenerateComplexReturnProperty_default_value = default;
        values.Add(PropertyName.@NotGenerateComplexReturnProperty, global::Jundot.Variant.From<string>(__NotGenerateComplexReturnProperty_default_value));
        string __NotGenerateReturnsProperty_default_value = default;
        values.Add(PropertyName.@NotGenerateReturnsProperty, global::Jundot.Variant.From<string>(__NotGenerateReturnsProperty_default_value));
        string __FullPropertyString_default_value = "FullPropertyString";
        values.Add(PropertyName.@FullPropertyString, global::Jundot.Variant.From<string>(__FullPropertyString_default_value));
        string __FullPropertyString_Complex_default_value = new string("FullPropertyString_Complex")   + global::System.Convert.ToInt32("1");
        values.Add(PropertyName.@FullPropertyString_Complex, global::Jundot.Variant.From<string>(__FullPropertyString_Complex_default_value));
        float __FullPropertyStaticImport_default_value = global::Jundot.Mathf.Pi;
        values.Add(PropertyName.@FullPropertyStaticImport, global::Jundot.Variant.From<float>(__FullPropertyStaticImport_default_value));
        string __LamdaPropertyString_default_value = "LamdaPropertyString";
        values.Add(PropertyName.@LamdaPropertyString, global::Jundot.Variant.From<string>(__LamdaPropertyString_default_value));
        float __LambdaPropertyStaticImport_default_value = global::Jundot.Mathf.Tau;
        values.Add(PropertyName.@LambdaPropertyStaticImport, global::Jundot.Variant.From<float>(__LambdaPropertyStaticImport_default_value));
        string __PrimaryCtorParameter_default_value = default;
        values.Add(PropertyName.@PrimaryCtorParameter, global::Jundot.Variant.From<string>(__PrimaryCtorParameter_default_value));
        float __ConstantMath_default_value = 2  * global::Jundot.Mathf.Pi;
        values.Add(PropertyName.@ConstantMath, global::Jundot.Variant.From<float>(__ConstantMath_default_value));
        float __ConstantMathStaticImport_default_value = global::Jundot.Mathf.RadToDeg(2  * global::Jundot.Mathf.Pi);
        values.Add(PropertyName.@ConstantMathStaticImport, global::Jundot.Variant.From<float>(__ConstantMathStaticImport_default_value));
        string __StaticStringAddition_default_value = string.Empty   + string.Empty;
        values.Add(PropertyName.@StaticStringAddition, global::Jundot.Variant.From<string>(__StaticStringAddition_default_value));
        bool __PropertyBoolean_default_value = true;
        values.Add(PropertyName.@PropertyBoolean, global::Jundot.Variant.From<bool>(__PropertyBoolean_default_value));
        char __PropertyChar_default_value = 'f';
        values.Add(PropertyName.@PropertyChar, global::Jundot.Variant.From<char>(__PropertyChar_default_value));
        sbyte __PropertySByte_default_value = 10;
        values.Add(PropertyName.@PropertySByte, global::Jundot.Variant.From<sbyte>(__PropertySByte_default_value));
        short __PropertyInt16_default_value = 10;
        values.Add(PropertyName.@PropertyInt16, global::Jundot.Variant.From<short>(__PropertyInt16_default_value));
        int __PropertyInt32_default_value = 10;
        values.Add(PropertyName.@PropertyInt32, global::Jundot.Variant.From<int>(__PropertyInt32_default_value));
        long __PropertyInt64_default_value = -10_000;
        values.Add(PropertyName.@PropertyInt64, global::Jundot.Variant.From<long>(__PropertyInt64_default_value));
        byte __PropertyByte_default_value = 10;
        values.Add(PropertyName.@PropertyByte, global::Jundot.Variant.From<byte>(__PropertyByte_default_value));
        ushort __PropertyUInt16_default_value = 10;
        values.Add(PropertyName.@PropertyUInt16, global::Jundot.Variant.From<ushort>(__PropertyUInt16_default_value));
        uint __PropertyUInt32_default_value = 10;
        values.Add(PropertyName.@PropertyUInt32, global::Jundot.Variant.From<uint>(__PropertyUInt32_default_value));
        ulong __PropertyUInt64_default_value = 10;
        values.Add(PropertyName.@PropertyUInt64, global::Jundot.Variant.From<ulong>(__PropertyUInt64_default_value));
        float __PropertySingle_default_value = 10;
        values.Add(PropertyName.@PropertySingle, global::Jundot.Variant.From<float>(__PropertySingle_default_value));
        double __PropertyDouble_default_value = 10;
        values.Add(PropertyName.@PropertyDouble, global::Jundot.Variant.From<double>(__PropertyDouble_default_value));
        string __PropertyString_default_value = "foo";
        values.Add(PropertyName.@PropertyString, global::Jundot.Variant.From<string>(__PropertyString_default_value));
        global::Jundot.Vector2 __PropertyVector2_default_value = new(10f, 10f);
        values.Add(PropertyName.@PropertyVector2, global::Jundot.Variant.From<global::Jundot.Vector2>(__PropertyVector2_default_value));
        global::Jundot.Vector2I __PropertyVector2I_default_value = global::Jundot.Vector2I.Up;
        values.Add(PropertyName.@PropertyVector2I, global::Jundot.Variant.From<global::Jundot.Vector2I>(__PropertyVector2I_default_value));
        global::Jundot.Rect2 __PropertyRect2_default_value = new(new global::Jundot.Vector2(10f, 10f), new global::Jundot.Vector2(10f, 10f));
        values.Add(PropertyName.@PropertyRect2, global::Jundot.Variant.From<global::Jundot.Rect2>(__PropertyRect2_default_value));
        global::Jundot.Rect2I __PropertyRect2I_default_value = new(new global::Jundot.Vector2I(10, 10), new global::Jundot.Vector2I(10, 10));
        values.Add(PropertyName.@PropertyRect2I, global::Jundot.Variant.From<global::Jundot.Rect2I>(__PropertyRect2I_default_value));
        global::Jundot.Transform2D __PropertyTransform2D_default_value = global::Jundot.Transform2D.Identity;
        values.Add(PropertyName.@PropertyTransform2D, global::Jundot.Variant.From<global::Jundot.Transform2D>(__PropertyTransform2D_default_value));
        global::Jundot.Vector3 __PropertyVector3_default_value = new(10f, 10f, 10f);
        values.Add(PropertyName.@PropertyVector3, global::Jundot.Variant.From<global::Jundot.Vector3>(__PropertyVector3_default_value));
        global::Jundot.Vector3I __PropertyVector3I_default_value = global::Jundot.Vector3I.Back;
        values.Add(PropertyName.@PropertyVector3I, global::Jundot.Variant.From<global::Jundot.Vector3I>(__PropertyVector3I_default_value));
        global::Jundot.Basis __PropertyBasis_default_value = new global::Jundot.Basis(global::Jundot.Quaternion.Identity);
        values.Add(PropertyName.@PropertyBasis, global::Jundot.Variant.From<global::Jundot.Basis>(__PropertyBasis_default_value));
        global::Jundot.Quaternion __PropertyQuaternion_default_value = new global::Jundot.Quaternion(global::Jundot.Basis.Identity);
        values.Add(PropertyName.@PropertyQuaternion, global::Jundot.Variant.From<global::Jundot.Quaternion>(__PropertyQuaternion_default_value));
        global::Jundot.Transform3D __PropertyTransform3D_default_value = global::Jundot.Transform3D.Identity;
        values.Add(PropertyName.@PropertyTransform3D, global::Jundot.Variant.From<global::Jundot.Transform3D>(__PropertyTransform3D_default_value));
        global::Jundot.Vector4 __PropertyVector4_default_value = new(10f, 10f, 10f, 10f);
        values.Add(PropertyName.@PropertyVector4, global::Jundot.Variant.From<global::Jundot.Vector4>(__PropertyVector4_default_value));
        global::Jundot.Vector4I __PropertyVector4I_default_value = global::Jundot.Vector4I.One;
        values.Add(PropertyName.@PropertyVector4I, global::Jundot.Variant.From<global::Jundot.Vector4I>(__PropertyVector4I_default_value));
        global::Jundot.Projection __PropertyProjection_default_value = global::Jundot.Projection.Identity;
        values.Add(PropertyName.@PropertyProjection, global::Jundot.Variant.From<global::Jundot.Projection>(__PropertyProjection_default_value));
        global::Jundot.Aabb __PropertyAabb_default_value = new global::Jundot.Aabb(10f, 10f, 10f, new global::Jundot.Vector3(1f, 1f, 1f));
        values.Add(PropertyName.@PropertyAabb, global::Jundot.Variant.From<global::Jundot.Aabb>(__PropertyAabb_default_value));
        global::Jundot.Color __PropertyColor_default_value = global::Jundot.Colors.Aquamarine;
        values.Add(PropertyName.@PropertyColor, global::Jundot.Variant.From<global::Jundot.Color>(__PropertyColor_default_value));
        global::Jundot.Plane __PropertyPlane_default_value = global::Jundot.Plane.PlaneXZ;
        values.Add(PropertyName.@PropertyPlane, global::Jundot.Variant.From<global::Jundot.Plane>(__PropertyPlane_default_value));
        global::Jundot.Callable __PropertyCallable_default_value = new global::Jundot.Callable(global::Jundot.Engine.GetMainLoop(), "_process");
        values.Add(PropertyName.@PropertyCallable, global::Jundot.Variant.From<global::Jundot.Callable>(__PropertyCallable_default_value));
        global::Jundot.Signal __PropertySignal_default_value = new global::Jundot.Signal(global::Jundot.Engine.GetMainLoop(), "Propertylist_changed");
        values.Add(PropertyName.@PropertySignal, global::Jundot.Variant.From<global::Jundot.Signal>(__PropertySignal_default_value));
        global::ExportedProperties.MyEnum __PropertyEnum_default_value = global::ExportedProperties.MyEnum.C;
        values.Add(PropertyName.@PropertyEnum, global::Jundot.Variant.From<global::ExportedProperties.MyEnum>(__PropertyEnum_default_value));
        global::ExportedProperties.MyFlagsEnum __PropertyFlagsEnum_default_value = global::ExportedProperties.MyFlagsEnum.C;
        values.Add(PropertyName.@PropertyFlagsEnum, global::Jundot.Variant.From<global::ExportedProperties.MyFlagsEnum>(__PropertyFlagsEnum_default_value));
        byte[] __PropertyByteArray_default_value = { 0, 1, 2, 3, 4, 5, 6  };
        values.Add(PropertyName.@PropertyByteArray, global::Jundot.Variant.From<byte[]>(__PropertyByteArray_default_value));
        int[] __PropertyInt32Array_default_value = { 0, 1, 2, 3, 4, 5, 6  };
        values.Add(PropertyName.@PropertyInt32Array, global::Jundot.Variant.From<int[]>(__PropertyInt32Array_default_value));
        long[] __PropertyInt64Array_default_value = { 0, 1, 2, 3, 4, 5, 6  };
        values.Add(PropertyName.@PropertyInt64Array, global::Jundot.Variant.From<long[]>(__PropertyInt64Array_default_value));
        float[] __PropertySingleArray_default_value = { 0f, 1f, 2f, 3f, 4f, 5f, 6f  };
        values.Add(PropertyName.@PropertySingleArray, global::Jundot.Variant.From<float[]>(__PropertySingleArray_default_value));
        double[] __PropertyDoubleArray_default_value = { 0d, 1d, 2d, 3d, 4d, 5d, 6d  };
        values.Add(PropertyName.@PropertyDoubleArray, global::Jundot.Variant.From<double[]>(__PropertyDoubleArray_default_value));
        string[] __PropertyStringArray_default_value = { "foo", "bar"  };
        values.Add(PropertyName.@PropertyStringArray, global::Jundot.Variant.From<string[]>(__PropertyStringArray_default_value));
        string[] __PropertyStringArrayEnum_default_value = { "foo", "bar"  };
        values.Add(PropertyName.@PropertyStringArrayEnum, global::Jundot.Variant.From<string[]>(__PropertyStringArrayEnum_default_value));
        global::Jundot.Vector2[] __PropertyVector2Array_default_value = { global::Jundot.Vector2.Up, global::Jundot.Vector2.Down, global::Jundot.Vector2.Left, global::Jundot.Vector2.Right   };
        values.Add(PropertyName.@PropertyVector2Array, global::Jundot.Variant.From<global::Jundot.Vector2[]>(__PropertyVector2Array_default_value));
        global::Jundot.Vector3[] __PropertyVector3Array_default_value = { global::Jundot.Vector3.Up, global::Jundot.Vector3.Down, global::Jundot.Vector3.Left, global::Jundot.Vector3.Right   };
        values.Add(PropertyName.@PropertyVector3Array, global::Jundot.Variant.From<global::Jundot.Vector3[]>(__PropertyVector3Array_default_value));
        global::Jundot.Color[] __PropertyColorArray_default_value = { global::Jundot.Colors.Aqua, global::Jundot.Colors.Aquamarine, global::Jundot.Colors.Azure, global::Jundot.Colors.Beige   };
        values.Add(PropertyName.@PropertyColorArray, global::Jundot.Variant.From<global::Jundot.Color[]>(__PropertyColorArray_default_value));
        global::Jundot.JundotObject[] __PropertyJundotObjectOrDerivedArray_default_value = { null  };
        values.Add(PropertyName.@PropertyJundotObjectOrDerivedArray, global::Jundot.Variant.CreateFrom(__PropertyJundotObjectOrDerivedArray_default_value));
        global::Jundot.StringName[] __field_StringNameArray_default_value = { "foo", "bar"  };
        values.Add(PropertyName.@field_StringNameArray, global::Jundot.Variant.From<global::Jundot.StringName[]>(__field_StringNameArray_default_value));
        global::Jundot.NodePath[] __field_NodePathArray_default_value = { "foo", "bar"  };
        values.Add(PropertyName.@field_NodePathArray, global::Jundot.Variant.From<global::Jundot.NodePath[]>(__field_NodePathArray_default_value));
        global::Jundot.Rid[] __field_RidArray_default_value = { default, default, default  };
        values.Add(PropertyName.@field_RidArray, global::Jundot.Variant.From<global::Jundot.Rid[]>(__field_RidArray_default_value));
        global::Jundot.Variant __PropertyVariant_default_value = "foo";
        values.Add(PropertyName.@PropertyVariant, global::Jundot.Variant.From<global::Jundot.Variant>(__PropertyVariant_default_value));
        global::Jundot.JundotObject __PropertyJundotObjectOrDerived_default_value = default;
        values.Add(PropertyName.@PropertyJundotObjectOrDerived, global::Jundot.Variant.From<global::Jundot.JundotObject>(__PropertyJundotObjectOrDerived_default_value));
        global::Jundot.Texture __PropertyJundotResourceTexture_default_value = default;
        values.Add(PropertyName.@PropertyJundotResourceTexture, global::Jundot.Variant.From<global::Jundot.Texture>(__PropertyJundotResourceTexture_default_value));
        global::Jundot.Texture __PropertyJundotResourceTextureWithInitializer_default_value = new()  { ResourceName  = ""   };
        values.Add(PropertyName.@PropertyJundotResourceTextureWithInitializer, global::Jundot.Variant.From<global::Jundot.Texture>(__PropertyJundotResourceTextureWithInitializer_default_value));
        global::Jundot.StringName __PropertyStringName_default_value = new global::Jundot.StringName("foo");
        values.Add(PropertyName.@PropertyStringName, global::Jundot.Variant.From<global::Jundot.StringName>(__PropertyStringName_default_value));
        global::Jundot.NodePath __PropertyNodePath_default_value = new global::Jundot.NodePath("foo");
        values.Add(PropertyName.@PropertyNodePath, global::Jundot.Variant.From<global::Jundot.NodePath>(__PropertyNodePath_default_value));
        global::Jundot.Rid __PropertyRid_default_value = default;
        values.Add(PropertyName.@PropertyRid, global::Jundot.Variant.From<global::Jundot.Rid>(__PropertyRid_default_value));
        global::Jundot.Collections.Dictionary __PropertyJundotDictionary_default_value = new()  { { "foo", 10  }, { global::Jundot.Vector2.Up, global::Jundot.Colors.Chocolate   }  };
        values.Add(PropertyName.@PropertyJundotDictionary, global::Jundot.Variant.From<global::Jundot.Collections.Dictionary>(__PropertyJundotDictionary_default_value));
        global::Jundot.Collections.Array __PropertyJundotArray_default_value = new()  { "foo", 10, global::Jundot.Vector2.Up, global::Jundot.Colors.Chocolate   };
        values.Add(PropertyName.@PropertyJundotArray, global::Jundot.Variant.From<global::Jundot.Collections.Array>(__PropertyJundotArray_default_value));
        global::Jundot.Collections.Dictionary<string, bool> __PropertyJundotGenericDictionary_default_value = new()  { { "foo", true  }, { "bar", false  }  };
        values.Add(PropertyName.@PropertyJundotGenericDictionary, global::Jundot.Variant.CreateFrom(__PropertyJundotGenericDictionary_default_value));
        global::Jundot.Collections.Array<int> __PropertyJundotGenericArray_default_value = new()  { 0, 1, 2, 3, 4, 5, 6  };
        values.Add(PropertyName.@PropertyJundotGenericArray, global::Jundot.Variant.CreateFrom(__PropertyJundotGenericArray_default_value));
        return values;
    }
#endif // TOOLS
#pragma warning restore CS0109
}
