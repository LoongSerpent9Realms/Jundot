using Jundot;
using Jundot.NativeInterop;

partial struct OuterClass
{
partial class NestedClass
{
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override void SaveJundotObjectData(global::Jundot.Bridge.JundotSerializationInfo info)
    {
        base.SaveJundotObjectData(info);
    }
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override void RestoreJundotObjectData(global::Jundot.Bridge.JundotSerializationInfo info)
    {
        base.RestoreJundotObjectData(info);
    }
}
}
