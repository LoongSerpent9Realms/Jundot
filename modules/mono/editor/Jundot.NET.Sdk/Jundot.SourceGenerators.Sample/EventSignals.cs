namespace Jundot.SourceGenerators.Sample;

public partial class EventSignals : JundotObject
{
    [Signal]
    public delegate void MySignalEventHandler(string str, int num);
}
