using System;

namespace Jundot.SourceGenerators.Sample;

public partial class NestedClass : JundotObject
{
    public partial class NestedClass2 : JundotObject
    {
        public partial class NestedClass3 : JundotObject
        {
            [Signal]
            public delegate void MySignalEventHandler(string str, int num);

            [Export] private String _fieldString = "foo";
            [Export] private String PropertyString { get; set; } = "foo";

            private void Method()
            {
            }
        }
    }
}
