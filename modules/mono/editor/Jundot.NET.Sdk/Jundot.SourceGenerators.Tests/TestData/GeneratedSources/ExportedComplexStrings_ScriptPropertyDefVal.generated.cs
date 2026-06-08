partial class ExportedComplexStrings
{
#pragma warning disable CS0109 // Disable warning about redundant 'new' keyword
#if TOOLS
    /// <summary>
    /// Get the default values for all properties declared in this class.
    /// This method is used by Jundot to determine the value that will be
    /// used by the inspector when resetting properties.
    /// Do not call this method.
    /// </summary>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    internal new static global::System.Collections.Generic.Dictionary<global::Jundot.StringName, global::Jundot.Variant> GetJundotPropertyDefaultValues()
    {
        var values = new global::System.Collections.Generic.Dictionary<global::Jundot.StringName, global::Jundot.Variant>(5);
        string __PropertyInterpolated1_default_value = $"The quick brown fox jumps over {(global::Jundot.GD.VarToStr($"the lazy {(global::Jundot.Engine.GetVersionInfo())} do"))}g.";
        values.Add(PropertyName.@PropertyInterpolated1, global::Jundot.Variant.From<string>(__PropertyInterpolated1_default_value));
        string ___fieldInterpolated1_default_value = $"The quick brown fox jumps over ({(global::Jundot.Engine.GetVersionInfo())})";
        values.Add(PropertyName.@_fieldInterpolated1, global::Jundot.Variant.From<string>(___fieldInterpolated1_default_value));
        string ___fieldInterpolated2_default_value = $"The quick brown fox jumps over ({(global::Jundot.Engine.GetVersionInfo()["major"]),0:G}) the lazy dog.";
        values.Add(PropertyName.@_fieldInterpolated2, global::Jundot.Variant.From<string>(___fieldInterpolated2_default_value));
        string ___fieldInterpolated3_default_value = $"{(((int)global::Jundot.Engine.GetVersionInfo()["major"])  * -1    * -1):G} the lazy dog.";
        values.Add(PropertyName.@_fieldInterpolated3, global::Jundot.Variant.From<string>(___fieldInterpolated3_default_value));
        string ___fieldInterpolated4_default_value = $"{(":::fff,,}<,<}},,}]")}";
        values.Add(PropertyName.@_fieldInterpolated4, global::Jundot.Variant.From<string>(___fieldInterpolated4_default_value));
        return values;
    }
#endif // TOOLS
#pragma warning restore CS0109
}
