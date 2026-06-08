using Jundot;
using Jundot.NativeInterop;

partial class AbstractGenericNode<T>
{
#pragma warning disable CS0109 // Disable warning about redundant 'new' keyword
    /// <summary>
    /// Cached StringNames for the properties and fields contained in this class, for fast lookup.
    /// </summary>
    public new class PropertyName : global::Jundot.Node.PropertyName {
        /// <summary>
        /// Cached name for the 'MyArray' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyArray = "MyArray";
    }
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override bool SetJundotClassPropertyValue(in jundot_string_name name, in jundot_variant value)
    {
        if (name == PropertyName.@MyArray) {
            this.@MyArray = global::Jundot.NativeInterop.VariantUtils.ConvertToArray<T>(value);
            return true;
        }
        return base.SetJundotClassPropertyValue(name, value);
    }
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override bool GetJundotClassPropertyValue(in jundot_string_name name, out jundot_variant value)
    {
        if (name == PropertyName.@MyArray) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFromArray(this.@MyArray);
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
        properties.Add(new(type: (global::Jundot.Variant.Type)28, name: PropertyName.@MyArray, hint: (global::Jundot.PropertyHint)0, hintString: "", usage: (global::Jundot.PropertyUsageFlags)4102, exported: true));
        return properties;
    }
#pragma warning restore CS0109
}
