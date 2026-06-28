using System;

namespace Jundot.SourceGenerators.Sample
{
    public partial class AllWriteOnly : JundotObject
    {
        private bool _writeOnlyBackingField = false;
        public bool WriteOnlyProperty { set => _writeOnlyBackingField = value; }
    }
}
