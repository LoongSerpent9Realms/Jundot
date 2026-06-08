using Jundot;

namespace NamespaceA
{
    partial class SameName : JundotObject
    {
        private int _field;
    }
}

// SameName again but different namespace
namespace NamespaceB
{
    partial class {|GD0003:SameName|} : JundotObject
    {
        private int _field;
    }
}
