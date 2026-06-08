using Jundot;

// This works because it inherits from JundotObject.
[GlobalClass]
public partial class CustomGlobalClass1 : JundotObject
{

}

// This works because it inherits from an object that inherits from JundotObject
[GlobalClass]
public partial class CustomGlobalClass2 : Node
{

}

// This raises a GD0401 diagnostic error: global classes must inherit from JundotObject
[GlobalClass]
public partial class {|GD0401:CustomGlobalClass3|}
{

}
