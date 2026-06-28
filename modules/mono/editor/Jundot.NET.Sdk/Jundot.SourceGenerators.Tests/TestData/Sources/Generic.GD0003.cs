using Jundot;

public partial class Generic<T> : JundotObject
{
    private int _field;
}

// Generic again but different generic parameters
public partial class {|GD0003:Generic|}<T, R> : JundotObject
{
    private int _field;
}

// Generic again but without generic parameters
public partial class {|GD0003:Generic|} : JundotObject
{
    private int _field;
}
