using Jundot;
using Jundot.Collections;

public partial class ExportDiagnostics_GD0107_OK : Node
{
    [Export]
    public Node NodeField;

    [Export]
    public Node[] SystemArrayOfNodesField;

    [Export]
    public Array<Node> JundotArrayOfNodesField;

    [Export]
    public Dictionary<Node, string> JundotDictionaryWithNodeAsKeyField;

    [Export]
    public Dictionary<string, Node> JundotDictionaryWithNodeAsValueField;

    [Export]
    public Node NodeProperty { get; set; }

    [Export]
    public Node[] SystemArrayOfNodesProperty { get; set; }

    [Export]
    public Array<Node> JundotArrayOfNodesProperty { get; set; }

    [Export]
    public Dictionary<Node, string> JundotDictionaryWithNodeAsKeyProperty { get; set; }

    [Export]
    public Dictionary<string, Node> JundotDictionaryWithNodeAsValueProperty { get; set; }
}

public partial class ExportDiagnostics_GD0107_KO : Resource
{
    [Export]
    public Node {|GD0107:NodeField|};

    [Export]
    public Node[] {|GD0107:SystemArrayOfNodesField|};

    [Export]
    public Array<Node> {|GD0107:JundotArrayOfNodesField|};

    [Export]
    public Dictionary<Node, string> {|GD0107:JundotDictionaryWithNodeAsKeyField|};

    [Export]
    public Dictionary<string, Node> {|GD0107:JundotDictionaryWithNodeAsValueField|};

    [Export]
    public Node {|GD0107:NodeProperty|} { get; set; }

    [Export]
    public Node[] {|GD0107:SystemArrayOfNodesProperty|} { get; set; }

    [Export]
    public Array<Node> {|GD0107:JundotArrayOfNodesProperty|} { get; set; }

    [Export]
    public Dictionary<Node, string> {|GD0107:JundotDictionaryWithNodeAsKeyProperty|} { get; set; }

    [Export]
    public Dictionary<string, Node> {|GD0107:JundotDictionaryWithNodeAsValueProperty|} { get; set; }
}
