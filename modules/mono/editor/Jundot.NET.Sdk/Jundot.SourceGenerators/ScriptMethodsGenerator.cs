using System.Collections.Generic;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Jundot.SourceGenerators
{
    [Generator]
    public class ScriptMethodsGenerator : ISourceGenerator
    {
        public void Initialize(GeneratorInitializationContext context)
        {
        }

        public void Execute(GeneratorExecutionContext context)
        {
            if (context.IsJundotSourceGeneratorDisabled("ScriptMethods"))
                return;

            INamedTypeSymbol[] jundotClasses = context
                .Compilation.SyntaxTrees
                .SelectMany(tree =>
                    tree.GetRoot().DescendantNodes()
                        .OfType<ClassDeclarationSyntax>()
                        .SelectJundotScriptClasses(context.Compilation)
                        // Report and skip non-partial classes
                        .Where(x =>
                        {
                            if (x.cds.IsPartial())
                            {
                                if (x.cds.IsNested() && !x.cds.AreAllOuterTypesPartial(out _))
                                {
                                    return false;
                                }

                                return true;
                            }
                            return false;
                        })
                        .Select(x => x.symbol)
                )
                .Distinct<INamedTypeSymbol>(SymbolEqualityComparer.Default)
                .ToArray();

            if (jundotClasses.Length > 0)
            {
                var typeCache = new MarshalUtils.TypeCache(context.Compilation);

                foreach (var jundotClass in jundotClasses)
                {
                    VisitJundotScriptClass(context, typeCache, jundotClass);
                }
            }
        }

        private class MethodOverloadEqualityComparer : IEqualityComparer<JundotMethodData>
        {
            public bool Equals(JundotMethodData x, JundotMethodData y)
                => x.ParamTypes.Length == y.ParamTypes.Length && x.Method.Name == y.Method.Name;

            public int GetHashCode(JundotMethodData obj)
            {
                unchecked
                {
                    return (obj.ParamTypes.Length.GetHashCode() * 397) ^ obj.Method.Name.GetHashCode();
                }
            }
        }

        private static void VisitJundotScriptClass(
            GeneratorExecutionContext context,
            MarshalUtils.TypeCache typeCache,
            INamedTypeSymbol symbol
        )
        {
            INamespaceSymbol namespaceSymbol = symbol.ContainingNamespace;
            string classNs = namespaceSymbol != null && !namespaceSymbol.IsGlobalNamespace ?
                namespaceSymbol.FullQualifiedNameOmitGlobal() :
                string.Empty;
            bool hasNamespace = classNs.Length != 0;

            bool isInnerClass = symbol.ContainingType != null;

            string uniqueHint = symbol.FullQualifiedNameOmitGlobal().SanitizeQualifiedNameForUniqueHint()
                                + "_ScriptMethods.generated";

            var source = new StringBuilder();

            source.Append("using Jundot;\n");
            source.Append("using Jundot.NativeInterop;\n");
            source.Append("\n");

            if (hasNamespace)
            {
                source.Append("namespace ");
                source.Append(classNs);
                source.Append(" {\n\n");
            }

            if (isInnerClass)
            {
                var containingType = symbol.ContainingType;
                AppendPartialContainingTypeDeclarations(containingType);

                void AppendPartialContainingTypeDeclarations(INamedTypeSymbol? containingType)
                {
                    if (containingType == null)
                        return;

                    AppendPartialContainingTypeDeclarations(containingType.ContainingType);

                    source.Append("partial ");
                    source.Append(containingType.GetDeclarationKeyword());
                    source.Append(" ");
                    source.Append(containingType.ToDisplayString(SymbolDisplayFormat.MinimallyQualifiedFormat));
                    source.Append("\n{\n");
                }
            }

            source.Append("partial class ");
            source.Append(symbol.ToDisplayString(SymbolDisplayFormat.MinimallyQualifiedFormat));
            source.Append("\n{\n");

            var members = symbol.GetMembers();

            var methodSymbols = members
                .Where(s => s.Kind == SymbolKind.Method && !s.IsImplicitlyDeclared)
                .Cast<IMethodSymbol>()
                .Where(m => m.MethodKind == MethodKind.Ordinary);

            var jundotClassMethods = methodSymbols.WhereHasJundotCompatibleSignature(typeCache)
                .Distinct(new MethodOverloadEqualityComparer())
                .ToArray();

            source.Append("#pragma warning disable CS0109 // Disable warning about redundant 'new' keyword\n");

            source.Append("    /// <summary>\n")
                .Append("    /// Cached StringNames for the methods contained in this class, for fast lookup.\n")
                .Append("    /// </summary>\n");

            source.Append(
                $"    public new class MethodName : {symbol.BaseType!.FullQualifiedNameIncludeGlobal()}.MethodName {{\n");

            // Generate cached StringNames for methods and properties, for fast lookup

            var distinctMethodNames = jundotClassMethods
                .Select(m => m.Method.Name)
                .Distinct()
                .ToArray();

            foreach (string methodName in distinctMethodNames)
            {
                source.Append("        /// <summary>\n")
                    .Append("        /// Cached name for the '")
                    .Append(methodName)
                    .Append("' method.\n")
                    .Append("        /// </summary>\n");

                source.Append("        public new static readonly global::Jundot.StringName @");
                source.Append(methodName);
                source.Append(" = \"");
                source.Append(methodName);
                source.Append("\";\n");
            }

            source.Append("    }\n"); // class JundotInternal

            // Generate GetJundotMethodList

            if (jundotClassMethods.Length > 0)
            {
                const string ListType = "global::System.Collections.Generic.List<global::Jundot.Bridge.MethodInfo>";

                source.Append("    /// <summary>\n")
                    .Append("    /// Get the method information for all the methods declared in this class.\n")
                    .Append("    /// This method is used by Jundot to register the available methods in the editor.\n")
                    .Append("    /// Do not call this method.\n")
                    .Append("    /// </summary>\n");

                source.Append("    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]\n");

                source.Append("    internal new static ")
                    .Append(ListType)
                    .Append(" GetJundotMethodList()\n    {\n");

                source.Append("        var methods = new ")
                    .Append(ListType)
                    .Append("(")
                    .Append(jundotClassMethods.Length)
                    .Append(");\n");

                foreach (var method in jundotClassMethods)
                {
                    var methodInfo = DetermineMethodInfo(method);
                    AppendMethodInfo(source, methodInfo);
                }

                source.Append("        return methods;\n");
                source.Append("    }\n");
            }

            source.Append("#pragma warning restore CS0109\n");

            // Generate InvokeJundotClassMethod

            if (jundotClassMethods.Length > 0)
            {
                source.Append("    /// <inheritdoc/>\n");
                source.Append("    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]\n");
                source.Append("    protected override bool InvokeJundotClassMethod(in jundot_string_name method, ");
                source.Append("NativeVariantPtrArgs args, out jundot_variant ret)\n    {\n");

                foreach (var method in jundotClassMethods)
                {
                    GenerateMethodInvoker(method, source);
                }

                source.Append("        return base.InvokeJundotClassMethod(method, args, out ret);\n");

                source.Append("    }\n");
            }

            // Generate InvokeJundotClassStaticMethod

            var jundotClassStaticMethods = jundotClassMethods.Where(m => m.Method.IsStatic).ToArray();

            if (jundotClassStaticMethods.Length > 0)
            {
                source.Append("#pragma warning disable CS0109 // Disable warning about redundant 'new' keyword\n");
                source.Append("    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]\n");
                source.Append("    internal new static bool InvokeJundotClassStaticMethod(in jundot_string_name method, ");
                source.Append("NativeVariantPtrArgs args, out jundot_variant ret)\n    {\n");

                foreach (var method in jundotClassStaticMethods)
                {
                    GenerateMethodInvoker(method, source);
                }

                source.Append("        ret = default;\n");
                source.Append("        return false;\n");
                source.Append("    }\n");

                source.Append("#pragma warning restore CS0109\n");
            }

            // Generate HasJundotClassMethod

            if (distinctMethodNames.Length > 0)
            {
                source.Append("    /// <inheritdoc/>\n");
                source.Append("    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]\n");
                source.Append("    protected override bool HasJundotClassMethod(in jundot_string_name method)\n    {\n");

                foreach (string methodName in distinctMethodNames)
                {
                    GenerateHasMethodEntry(methodName, source);
                }

                source.Append("        return base.HasJundotClassMethod(method);\n");

                source.Append("    }\n");
            }

            source.Append("}\n"); // partial class

            if (isInnerClass)
            {
                var containingType = symbol.ContainingType;

                while (containingType != null)
                {
                    source.Append("}\n"); // outer class

                    containingType = containingType.ContainingType;
                }
            }

            if (hasNamespace)
            {
                source.Append("\n}\n");
            }

            context.AddSource(uniqueHint, SourceText.From(source.ToString(), Encoding.UTF8));
        }

        private static void AppendMethodInfo(StringBuilder source, MethodInfo methodInfo)
        {
            source.Append("        methods.Add(new(name: MethodName.@")
                .Append(methodInfo.Name)
                .Append(", returnVal: ");

            AppendPropertyInfo(source, methodInfo.ReturnVal);

            source.Append(", flags: (global::Jundot.MethodFlags)")
                .Append((int)methodInfo.Flags)
                .Append(", arguments: ");

            if (methodInfo.Arguments is { Count: > 0 })
            {
                source.Append("new() { ");

                foreach (var param in methodInfo.Arguments)
                {
                    AppendPropertyInfo(source, param);

                    // C# allows colon after the last element
                    source.Append(", ");
                }

                source.Append(" }");
            }
            else
            {
                source.Append("null");
            }

            source.Append(", defaultArguments: null));\n");
        }

        private static void AppendPropertyInfo(StringBuilder source, PropertyInfo propertyInfo)
        {
            source.Append("new(type: (global::Jundot.Variant.Type)")
                .Append((int)propertyInfo.Type)
                .Append(", name: \"")
                .Append(propertyInfo.Name)
                .Append("\", hint: (global::Jundot.PropertyHint)")
                .Append((int)propertyInfo.Hint)
                .Append(", hintString: \"")
                .Append(propertyInfo.HintString)
                .Append("\", usage: (global::Jundot.PropertyUsageFlags)")
                .Append((int)propertyInfo.Usage)
                .Append(", exported: ")
                .Append(propertyInfo.Exported ? "true" : "false");
            if (propertyInfo.ClassName != null)
            {
                source.Append(", className: new global::Jundot.StringName(\"")
                    .Append(propertyInfo.ClassName)
                    .Append("\")");
            }
            source.Append(")");
        }

        private static MethodInfo DetermineMethodInfo(JundotMethodData method)
        {
            PropertyInfo returnVal;

            if (method.RetType != null)
            {
                returnVal = DeterminePropertyInfo(method.RetType.Value.MarshalType,
                    method.RetType.Value.TypeSymbol,
                    name: string.Empty);
            }
            else
            {
                returnVal = new PropertyInfo(VariantType.Nil, string.Empty, PropertyHint.None,
                    hintString: null, PropertyUsageFlags.Default, exported: false);
            }

            int paramCount = method.ParamTypes.Length;

            List<PropertyInfo>? arguments;

            if (paramCount > 0)
            {
                arguments = new(capacity: paramCount);

                for (int i = 0; i < paramCount; i++)
                {
                    arguments.Add(DeterminePropertyInfo(method.ParamTypes[i],
                        method.Method.Parameters[i].Type,
                        name: method.Method.Parameters[i].Name));
                }
            }
            else
            {
                arguments = null;
            }

            MethodFlags flags = MethodFlags.Default;

            if (method.Method.IsStatic)
            {
                flags |= MethodFlags.Static;
            }

            return new MethodInfo(method.Method.Name, returnVal, flags, arguments,
                defaultArguments: null);
        }

        private static PropertyInfo DeterminePropertyInfo(MarshalType marshalType, ITypeSymbol typeSymbol, string name)
        {
            var memberVariantType = MarshalUtils.ConvertMarshalTypeToVariantType(marshalType)!.Value;

            var propUsage = PropertyUsageFlags.Default;

            if (memberVariantType == VariantType.Nil)
                propUsage |= PropertyUsageFlags.NilIsVariant;

            string? className = null;
            if (memberVariantType == VariantType.Object && typeSymbol is INamedTypeSymbol namedTypeSymbol)
            {
                className = namedTypeSymbol.GetJundotScriptNativeClassName();
            }

            return new PropertyInfo(memberVariantType, name,
                PropertyHint.None, string.Empty, propUsage, className, exported: false);
        }

        private static void GenerateHasMethodEntry(
            string methodName,
            StringBuilder source
        )
        {
            source.Append("        ");
            source.Append("if (method == MethodName.@");
            source.Append(methodName);
            source.Append(") {\n           return true;\n        }\n");
        }

        private static void GenerateMethodInvoker(
            JundotMethodData method,
            StringBuilder source
        )
        {
            string methodName = method.Method.Name;

            source.Append("        if (method == MethodName.@");
            source.Append(methodName);
            source.Append(" && args.Count == ");
            source.Append(method.ParamTypes.Length);
            source.Append(") {\n");

            if (method.RetType != null)
                source.Append("            var callRet = ");
            else
                source.Append("            ");

            source.Append("@");
            source.Append(methodName);
            source.Append("(");

            for (int i = 0; i < method.ParamTypes.Length; i++)
            {
                if (i != 0)
                    source.Append(", ");

                source.AppendNativeVariantToManagedExpr(string.Concat("args[", i.ToString(), "]"),
                    method.ParamTypeSymbols[i], method.ParamTypes[i]);
            }

            source.Append(");\n");

            if (method.RetType != null)
            {
                source.Append("            ret = ");

                source.AppendManagedToNativeVariantExpr("callRet",
                    method.RetType.Value.TypeSymbol, method.RetType.Value.MarshalType);
                source.Append(";\n");

                source.Append("            return true;\n");
            }
            else
            {
                source.Append("            ret = default;\n");
                source.Append("            return true;\n");
            }

            source.Append("        }\n");
        }
    }
}
