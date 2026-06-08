using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Jundot.SourceGenerators
{
    [Generator]
    public class ScriptPathAttributeGenerator : ISourceGenerator
    {
        public void Execute(GeneratorExecutionContext context)
        {
            if (context.IsJundotSourceGeneratorDisabled("ScriptPathAttribute"))
                return;

            if (context.IsJundotToolsProject())
                return;

            // NOTE: NotNullWhen diagnostics don't work on projects targeting .NET Standard 2.0
            // ReSharper disable once ReplaceWithStringIsNullOrEmpty
            if (!context.TryGetGlobalAnalyzerProperty("JundotProjectDirBase64", out string? jundotProjectDir) || jundotProjectDir!.Length == 0)
            {
                if (!context.TryGetGlobalAnalyzerProperty("JundotProjectDir", out jundotProjectDir) || jundotProjectDir!.Length == 0)
                {
                    throw new InvalidOperationException("Property 'JundotProjectDir' is null or empty.");
                }
            }
            else
            {
                // Workaround for https://github.com/dotnet/roslyn/issues/51692
                jundotProjectDir = Encoding.UTF8.GetString(Convert.FromBase64String(jundotProjectDir));
            }

            Dictionary<INamedTypeSymbol, IEnumerable<ClassDeclarationSyntax>> jundotClasses = context
                .Compilation.SyntaxTrees
                .SelectMany(tree =>
                    tree.GetRoot().DescendantNodes()
                        .OfType<ClassDeclarationSyntax>()
                        // Ignore inner classes
                        .Where(cds => !cds.IsNested())
                        .SelectJundotScriptClasses(context.Compilation)
                        // Report and skip non-partial classes
                        .Where(x =>
                        {
                            if (x.cds.IsPartial())
                                return true;
                            return false;
                        })
                )
                .Where(x =>
                    // Ignore classes whose name is not the same as the file name
                    Path.GetFileNameWithoutExtension(x.cds.SyntaxTree.FilePath) == x.symbol.Name)
                .GroupBy<(ClassDeclarationSyntax cds, INamedTypeSymbol symbol), INamedTypeSymbol>(x => x.symbol, SymbolEqualityComparer.Default)
                .ToDictionary<IGrouping<INamedTypeSymbol, (ClassDeclarationSyntax cds, INamedTypeSymbol symbol)>, INamedTypeSymbol, IEnumerable<ClassDeclarationSyntax>>(g => g.Key, g => g.Select(x => x.cds), SymbolEqualityComparer.Default);

            var usedPaths = new HashSet<string>();
            foreach (var jundotClass in jundotClasses)
            {
                VisitJundotScriptClass(context, jundotProjectDir, usedPaths,
                    symbol: jundotClass.Key,
                    classDeclarations: jundotClass.Value);
            }

            if (jundotClasses.Count <= 0)
                return;

            AddScriptTypesAssemblyAttr(context, jundotClasses);
        }

        private static void VisitJundotScriptClass(
            GeneratorExecutionContext context,
            string jundotProjectDir,
            HashSet<string> usedPaths,
            INamedTypeSymbol symbol,
            IEnumerable<ClassDeclarationSyntax> classDeclarations
        )
        {
            var attributes = new StringBuilder();

            // Remember syntax trees for which we already added an attribute, to prevent unnecessary duplicates.
            var attributedTrees = new List<SyntaxTree>();

            foreach (var cds in classDeclarations)
            {
                if (attributedTrees.Contains(cds.SyntaxTree))
                    continue;

                attributedTrees.Add(cds.SyntaxTree);

                if (attributes.Length != 0)
                    attributes.Append("\n");

                string scriptPath = RelativeToDir(cds.SyntaxTree.FilePath, jundotProjectDir);
                if (!usedPaths.Add(scriptPath))
                {
                    context.ReportDiagnostic(Diagnostic.Create(
                        Common.MultipleClassesInJundotScriptRule,
                        cds.Identifier.GetLocation(),
                        symbol.Name
                    ));
                    return;
                }

                attributes.Append(@"[ScriptPathAttribute(""res://");
                attributes.Append(scriptPath);
                attributes.Append(@""")]");
            }

            INamespaceSymbol namespaceSymbol = symbol.ContainingNamespace;
            string classNs = namespaceSymbol != null && !namespaceSymbol.IsGlobalNamespace ?
                namespaceSymbol.FullQualifiedNameOmitGlobal() :
                string.Empty;
            bool hasNamespace = classNs.Length != 0;

            string uniqueHint = symbol.FullQualifiedNameOmitGlobal().SanitizeQualifiedNameForUniqueHint()
                             + "_ScriptPath.generated";

            var source = new StringBuilder();

            // using Jundot;
            // namespace {classNs} {
            //     {attributesBuilder}
            //     partial class {className} { }
            // }

            source.Append("using Jundot;\n");

            if (hasNamespace)
            {
                source.Append("namespace ");
                source.Append(classNs);
                source.Append(" {\n\n");
            }

            source.Append(attributes);
            source.Append("\npartial class ");
            source.Append(symbol.ToDisplayString(SymbolDisplayFormat.MinimallyQualifiedFormat));
            source.Append("\n{\n}\n");

            if (hasNamespace)
            {
                source.Append("\n}\n");
            }

            context.AddSource(uniqueHint, SourceText.From(source.ToString(), Encoding.UTF8));
        }

        private static void AddScriptTypesAssemblyAttr(GeneratorExecutionContext context,
            Dictionary<INamedTypeSymbol, IEnumerable<ClassDeclarationSyntax>> jundotClasses)
        {
            var sourceBuilder = new StringBuilder();

            sourceBuilder.Append("[assembly:");
            sourceBuilder.Append(JundotClasses.AssemblyHasScriptsAttr);
            sourceBuilder.Append("(new System.Type[] {");

            bool first = true;

            foreach (var jundotClass in jundotClasses)
            {
                var qualifiedName = jundotClass.Key.ToDisplayString(
                    NullableFlowState.NotNull, SymbolDisplayFormat.FullyQualifiedFormat
                        .WithGenericsOptions(SymbolDisplayGenericsOptions.None));
                if (!first)
                    sourceBuilder.Append(", ");
                first = false;
                sourceBuilder.Append("typeof(");
                sourceBuilder.Append(qualifiedName);
                if (jundotClass.Key.IsGenericType)
                    sourceBuilder.Append($"<{new string(',', jundotClass.Key.TypeParameters.Count() - 1)}>");
                sourceBuilder.Append(")");
            }

            sourceBuilder.Append("})]\n");

            context.AddSource("AssemblyScriptTypes.generated",
                SourceText.From(sourceBuilder.ToString(), Encoding.UTF8));
        }

        public void Initialize(GeneratorInitializationContext context)
        {
        }

        private static string RelativeToDir(string path, string dir)
        {
            // Make sure the directory ends with a path separator
            dir = Path.Combine(dir, " ").TrimEnd();

            if (Path.DirectorySeparatorChar == '\\')
                dir = dir.Replace("/", "\\") + "\\";

            var fullPath = new Uri(Path.GetFullPath(path), UriKind.Absolute);
            var relRoot = new Uri(Path.GetFullPath(dir), UriKind.Absolute);

            // MakeRelativeUri converts spaces to %20, hence why we need UnescapeDataString
            return Uri.UnescapeDataString(relRoot.MakeRelativeUri(fullPath).ToString());
        }
    }
}
