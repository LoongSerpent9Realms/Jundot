using System.Runtime.InteropServices;
using Jundot;

[Tool]
public partial class AlcUnloadabilityGCHandleSample : Node
{
    public override void _Ready()
    {
        {|GD0501:GCHandle.Alloc(this)|};
        GCHandle.Alloc(this, GCHandleType.Weak);
        GCHandle.Alloc(this, GCHandleType.WeakTrackResurrection);
    }
}
