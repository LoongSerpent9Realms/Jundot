using System;
using Jundot;

[Tool]
public partial class AlcUnloadabilityStaticEventSample : Node
{
    public override void _Ready()
    {
        GlobalEvents.Reloaded {|GD0503:+=|} OnReloaded;
    }

    private static void OnReloaded()
    {
    }
}

public static class GlobalEvents
{
    public static event Action? Reloaded;
}
