using Jundot;
using Jundot.NativeInterop;

partial struct OuterClass
{
partial class NestedClass
{
#pragma warning disable CS0109 // Disable warning about redundant 'new' keyword
    /// <summary>
    /// Cached StringNames for the methods contained in this class, for fast lookup.
    /// </summary>
    public new class MethodName : global::Jundot.RefCounted.MethodName {
        /// <summary>
        /// Cached name for the '_Get' method.
        /// </summary>
        public new static readonly global::Jundot.StringName @_Get = "_Get";
    }
    /// <summary>
    /// Get the method information for all the methods declared in this class.
    /// This method is used by Jundot to register the available methods in the editor.
    /// Do not call this method.
    /// </summary>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    internal new static global::System.Collections.Generic.List<global::Jundot.Bridge.MethodInfo> GetJundotMethodList()
    {
        var methods = new global::System.Collections.Generic.List<global::Jundot.Bridge.MethodInfo>(1);
        methods.Add(new(name: MethodName.@_Get, returnVal: new(type: (global::Jundot.Variant.Type)0, name: "", hint: (global::Jundot.PropertyHint)0, hintString: "", usage: (global::Jundot.PropertyUsageFlags)131078, exported: false), flags: (global::Jundot.MethodFlags)1, arguments: new() { new(type: (global::Jundot.Variant.Type)21, name: "property", hint: (global::Jundot.PropertyHint)0, hintString: "", usage: (global::Jundot.PropertyUsageFlags)6, exported: false),  }, defaultArguments: null));
        return methods;
    }
#pragma warning restore CS0109
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override bool InvokeJundotClassMethod(in jundot_string_name method, NativeVariantPtrArgs args, out jundot_variant ret)
    {
        if (method == MethodName.@_Get && args.Count == 1) {
            var callRet = @_Get(global::Jundot.NativeInterop.VariantUtils.ConvertTo<global::Jundot.StringName>(args[0]));
            ret = global::Jundot.NativeInterop.VariantUtils.CreateFrom<global::Jundot.Variant>(callRet);
            return true;
        }
        return base.InvokeJundotClassMethod(method, args, out ret);
    }
    /// <inheritdoc/>
    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]
    protected override bool HasJundotClassMethod(in jundot_string_name method)
    {
        if (method == MethodName.@_Get) {
           return true;
        }
        return base.HasJundotClassMethod(method);
    }
}
}
