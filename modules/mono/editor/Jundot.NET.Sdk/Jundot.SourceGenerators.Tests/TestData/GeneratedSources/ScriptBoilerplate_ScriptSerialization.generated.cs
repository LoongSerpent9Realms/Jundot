using Jundot;
using Jundot.NativeInterop;

partial class ScriptBoilerplate
{
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override void SaveJundotObjectData(global::Jundot.Bridge.JundotSerializationInfo info)
    {
        base.SaveJundotObjectData(info);
        info.AddProperty(PropertyName.@_nodePath, global::Jundot.Variant.From<global::Jundot.NodePath>(this.@_nodePath));
        info.AddProperty(PropertyName.@_velocity, global::Jundot.Variant.From<int>(this.@_velocity));
    }
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override void RestoreJundotObjectData(global::Jundot.Bridge.JundotSerializationInfo info)
    {
        base.RestoreJundotObjectData(info);
        if (info.TryGetProperty(PropertyName.@_nodePath, out var _value__nodePath))
            this.@_nodePath = _value__nodePath.As<global::Jundot.NodePath>();
        if (info.TryGetProperty(PropertyName.@_velocity, out var _value__velocity))
            this.@_velocity = _value__velocity.As<int>();
    }
}
