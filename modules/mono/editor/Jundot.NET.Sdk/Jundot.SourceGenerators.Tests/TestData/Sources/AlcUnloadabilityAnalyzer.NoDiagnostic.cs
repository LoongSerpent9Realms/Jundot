using System;
using System.Runtime.InteropServices;
using Jundot;

public partial class RuntimeOnlyAnalyzerSample : Node
{
    private static Node _cachedNode;

    public override void _Ready()
    {
        GCHandle.Alloc(this);
        GlobalEvents.Reloaded += OnReloaded;
    }

    private static void OnReloaded()
    {
    }
}

public static class GlobalEvents
{
    public static event Action? Reloaded;
}
