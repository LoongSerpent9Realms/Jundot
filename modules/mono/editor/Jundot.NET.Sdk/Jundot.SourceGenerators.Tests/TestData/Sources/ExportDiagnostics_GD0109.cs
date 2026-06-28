using Jundot;
using Jundot.Collections;

[Tool]
public partial class ExportDiagnostics_GD0109 : Node
{
    [Export, ExportToolButton("")]
    public Callable {|GD0109:MyButton|} => new Callable();
}
