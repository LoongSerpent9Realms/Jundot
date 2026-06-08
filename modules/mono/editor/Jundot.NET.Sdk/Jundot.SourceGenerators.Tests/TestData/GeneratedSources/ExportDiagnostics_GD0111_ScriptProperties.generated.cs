using Jundot;
using Jundot.NativeInterop;

partial class ExportDiagnostics_GD0111
{
#pragma warning disable CS0109 // Disable warning about redundant 'new' keyword
    /// <summary>
    /// Cached StringNames for the properties and fields contained in this class, for fast lookup.
    /// </summary>
    public new class PropertyName : global::Jundot.Node.PropertyName {
        /// <summary>
        /// Cached name for the 'MyButtonGet' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyButtonGet = "MyButtonGet";
        /// <summary>
        /// Cached name for the 'MyButtonGetSet' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyButtonGetSet = "MyButtonGetSet";
        /// <summary>
        /// Cached name for the 'MyButtonGetWithBackingField' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyButtonGetWithBackingField = "MyButtonGetWithBackingField";
        /// <summary>
        /// Cached name for the 'MyButtonGetSetWithBackingField' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyButtonGetSetWithBackingField = "MyButtonGetSetWithBackingField";
        /// <summary>
        /// Cached name for the 'MyButtonOkWithCallableCreationExpression' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyButtonOkWithCallableCreationExpression = "MyButtonOkWithCallableCreationExpression";
        /// <summary>
        /// Cached name for the 'MyButtonOkWithImplicitCallableCreationExpression' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyButtonOkWithImplicitCallableCreationExpression = "MyButtonOkWithImplicitCallableCreationExpression";
        /// <summary>
        /// Cached name for the 'MyButtonOkWithCallableFromExpression' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyButtonOkWithCallableFromExpression = "MyButtonOkWithCallableFromExpression";
        /// <summary>
        /// Cached name for the '_backingField' field.
        /// </summary>
        public new static readonly global::Jundot.StringName @_backingField = "_backingField";
    }
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override bool SetJundotClassPropertyValue(in jundot_string_name name, in jundot_variant value)
    {
        if (name == PropertyName.@MyButtonGetSet) {
            this.@MyButtonGetSet = global::Jundot.NativeInterop.VariantUtils.ConvertTo<global::Jundot.Callable>(value);
            return true;
        }
        if (name == PropertyName.@MyButtonGetSetWithBackingField) {
            this.@MyButtonGetSetWithBackingField = global::Jundot.NativeInterop.VariantUtils.ConvertTo<global::Jundot.Callable>(value);
            return true;
        }
        if (name == PropertyName.@_backingField) {
            this.@_backingField = global::Jundot.NativeInterop.VariantUtils.ConvertTo<global::Jundot.Callable>(value);
            return true;
        }
        return base.SetJundotClassPropertyValue(name, value);
    }
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override bool GetJundotClassPropertyValue(in jundot_string_name name, out jundot_variant value)
    {
        if (name == PropertyName.@MyButtonGet) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@MyButtonGet);
            return true;
        }
        if (name == PropertyName.@MyButtonGetSet) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@MyButtonGetSet);
            return true;
        }
        if (name == PropertyName.@MyButtonGetWithBackingField) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@MyButtonGetWithBackingField);
            return true;
        }
        if (name == PropertyName.@MyButtonGetSetWithBackingField) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@MyButtonGetSetWithBackingField);
            return true;
        }
        if (name == PropertyName.@MyButtonOkWithCallableCreationExpression) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@MyButtonOkWithCallableCreationExpression);
            return true;
        }
        if (name == PropertyName.@MyButtonOkWithImplicitCallableCreationExpression) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@MyButtonOkWithImplicitCallableCreationExpression);
            return true;
        }
        if (name == PropertyName.@MyButtonOkWithCallableFromExpression) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@MyButtonOkWithCallableFromExpression);
            return true;
        }
        if (name == PropertyName.@_backingField) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@_backingField);
            return true;
        }
        return base.GetJundotClassPropertyValue(name, out value);
    }
    /// <summary>
    /// Get the property information for all the properties declared in this class.
    /// This method is used by Jundot to register the available properties in the editor.
    /// Do not call this method.
    /// </summary>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    internal new static global::System.Collections.Generic.List<global::Jundot.Bridge.PropertyInfo> GetJundotPropertyList()
    {
        var properties = new global::System.Collections.Generic.List<global::Jundot.Bridge.PropertyInfo>();
        properties.Add(new(type: (global::Jundot.Variant.Type)25, name: PropertyName.@_backingField, hint: (global::Jundot.PropertyHint)0, hintString: "", usage: (global::Jundot.PropertyUsageFlags)4096, exported: false));
        properties.Add(new(type: (global::Jundot.Variant.Type)25, name: PropertyName.@MyButtonOkWithCallableCreationExpression, hint: (global::Jundot.PropertyHint)39, hintString: "", usage: (global::Jundot.PropertyUsageFlags)4, exported: true));
        properties.Add(new(type: (global::Jundot.Variant.Type)25, name: PropertyName.@MyButtonOkWithImplicitCallableCreationExpression, hint: (global::Jundot.PropertyHint)39, hintString: "", usage: (global::Jundot.PropertyUsageFlags)4, exported: true));
        properties.Add(new(type: (global::Jundot.Variant.Type)25, name: PropertyName.@MyButtonOkWithCallableFromExpression, hint: (global::Jundot.PropertyHint)39, hintString: "", usage: (global::Jundot.PropertyUsageFlags)4, exported: true));
        return properties;
    }
#pragma warning restore CS0109
}
