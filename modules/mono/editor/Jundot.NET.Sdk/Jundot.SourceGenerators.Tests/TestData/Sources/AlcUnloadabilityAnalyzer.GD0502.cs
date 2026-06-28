using System;
using System.Threading;
using System.Threading.Tasks;
using Jundot;

[Tool]
public partial class AlcUnloadabilityStaticFieldSample : Node
{
    private static Node {|GD0502:_cachedNode|};
    private static Action? {|GD0502:_cachedAction|};
    private static Thread? {|GD0502:_thread|};
    private static Task<int>? {|GD0502:_task|};

    private static string? _safeString;
}
