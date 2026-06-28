using Jundot;
using Jundot.NativeInterop;

partial class ExportDiagnostics_GD0109
{
#pragma warning disable CS0109 // Disable warning about redundant 'new' keyword
    /// <summary>
    /// Cached StringNames for the properties and fields contained in this class, for fast lookup.
    /// </summary>
    public new class PropertyName : global::Jundot.Node.PropertyName {
        /// <summary>
        /// Cached name for the 'MyButton' property.
        /// </summary>
        public new static readonly global::Jundot.StringName @MyButton = "MyButton";
    }
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override bool GetJundotClassPropertyValue(in jundot_string_name name, out jundot_variant value)
    {
        if (name == PropertyName.@MyButton) {
            value = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Callable>(this.@MyButton);
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
        return properties;
    }
#pragma warning restore CS0109
}
