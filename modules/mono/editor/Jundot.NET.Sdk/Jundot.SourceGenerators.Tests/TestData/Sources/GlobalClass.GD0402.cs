using Jundot;

// This works because it inherits from JundotObject and it doesn't have any generic type parameter.
[GlobalClass]
public partial class CustomGlobalClass : JundotObject
{

}

// This raises a GD0402 diagnostic error: global classes can't have any generic type parameter
[GlobalClass]
public partial class {|GD0402:CustomGlobalClass|}<T> : JundotObject
{

}
