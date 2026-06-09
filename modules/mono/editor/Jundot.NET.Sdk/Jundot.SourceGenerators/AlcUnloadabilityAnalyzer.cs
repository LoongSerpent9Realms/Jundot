using System.Collections.Immutable;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Diagnostics;

namespace Jundot.SourceGenerators
{
    [DiagnosticAnalyzer(LanguageNames.CSharp)]
    public sealed class AlcUnloadabilityAnalyzer : DiagnosticAnalyzer
    {
        public override ImmutableArray<DiagnosticDescriptor> SupportedDiagnostics =>
            ImmutableArray.Create(
                Common.AlcUnloadabilityStrongGCHandleRule,
                Common.AlcUnloadabilityStaticFieldRule,
                Common.AlcUnloadabilityStaticEventSubscriptionRule);

        public override void Initialize(AnalysisContext context)
        {
            context.ConfigureGeneratedCodeAnalysis(GeneratedCodeAnalysisFlags.None);
            context.EnableConcurrentExecution();
            context.RegisterSyntaxNodeAction(AnalyzeInvocation, SyntaxKind.InvocationExpression);
            context.RegisterSyntaxNodeAction(AnalyzeField, SyntaxKind.FieldDeclaration);
            context.RegisterSyntaxNodeAction(AnalyzeAssignment, SyntaxKind.AddAssignmentExpression);
        }

        private static void AnalyzeInvocation(SyntaxNodeAnalysisContext context)
        {
            var invocation = (InvocationExpressionSyntax)context.Node;

            if (!IsInsideToolJundotClass(context))
                return;

            if (context.SemanticModel.GetSymbolInfo(invocation).Symbol is not IMethodSymbol methodSymbol)
                return;

            if (methodSymbol.Name != "Alloc" ||
                methodSymbol.ContainingType?.FullQualifiedNameOmitGlobal() != "System.Runtime.InteropServices.GCHandle")
            {
                return;
            }

            if (IsWeakGCHandleAllocation(context, invocation))
                return;

            context.ReportDiagnostic(Diagnostic.Create(
                Common.AlcUnloadabilityStrongGCHandleRule,
                invocation.GetLocation(),
                invocation.Expression.ToString()));
        }

        private static void AnalyzeField(SyntaxNodeAnalysisContext context)
        {
            var fieldDeclaration = (FieldDeclarationSyntax)context.Node;

            if (!IsInsideToolJundotClass(context))
                return;

            if (!fieldDeclaration.Modifiers.Any(SyntaxKind.StaticKeyword))
                return;

            foreach (var variable in fieldDeclaration.Declaration.Variables)
            {
                if (context.SemanticModel.GetDeclaredSymbol(variable) is not IFieldSymbol fieldSymbol)
                    continue;

                if (fieldSymbol.IsConst)
                    continue;

                if (!IsUnloadabilitySensitiveStaticFieldType(fieldSymbol.Type))
                    continue;

                context.ReportDiagnostic(Diagnostic.Create(
                    Common.AlcUnloadabilityStaticFieldRule,
                    variable.Identifier.GetLocation(),
                    fieldSymbol.Name));
            }
        }

        private static void AnalyzeAssignment(SyntaxNodeAnalysisContext context)
        {
            var assignment = (AssignmentExpressionSyntax)context.Node;

            if (!IsInsideToolJundotClass(context))
                return;

            if (context.SemanticModel.GetSymbolInfo(assignment.Left).Symbol is not IEventSymbol { IsStatic: true })
                return;

            context.ReportDiagnostic(Diagnostic.Create(
                Common.AlcUnloadabilityStaticEventSubscriptionRule,
                assignment.OperatorToken.GetLocation(),
                assignment.Left.ToString()));
        }

        private static bool IsInsideToolJundotClass(SyntaxNodeAnalysisContext context)
        {
            var classDeclaration = context.Node.AncestorsAndSelf().OfType<ClassDeclarationSyntax>().FirstOrDefault();
            if (classDeclaration == null)
                return false;

            if (context.SemanticModel.GetDeclaredSymbol(classDeclaration) is not INamedTypeSymbol classSymbol)
                return false;

            if (!classSymbol.InheritsFrom("JundotSharp", JundotClasses.JundotObject))
                return false;

            return classSymbol.GetAttributes().Any(a => a.AttributeClass?.IsJundotToolAttribute() ?? false);
        }

        private static bool IsWeakGCHandleAllocation(SyntaxNodeAnalysisContext context, InvocationExpressionSyntax invocation)
        {
            SeparatedSyntaxList<ArgumentSyntax> arguments = invocation.ArgumentList.Arguments;
            if (arguments.Count < 2)
                return false;

            SymbolInfo symbolInfo = context.SemanticModel.GetSymbolInfo(arguments[1].Expression);
            if (symbolInfo.Symbol is not IFieldSymbol fieldSymbol)
                return false;

            if (fieldSymbol.ContainingType?.FullQualifiedNameOmitGlobal() != "System.Runtime.InteropServices.GCHandleType")
                return false;

            return fieldSymbol.Name is "Weak" or "WeakTrackResurrection";
        }

        private static bool IsUnloadabilitySensitiveStaticFieldType(ITypeSymbol typeSymbol)
        {
            if (typeSymbol.InheritsFrom("JundotSharp", JundotClasses.JundotObject))
                return true;

            if (typeSymbol.TypeKind == TypeKind.Delegate)
                return true;

            INamedTypeSymbol? namedType = typeSymbol as INamedTypeSymbol;
            string? typeName = namedType?.OriginalDefinition.FullQualifiedNameOmitGlobal() ??
                typeSymbol.FullQualifiedNameOmitGlobal();

            return typeName is "System.Threading.Thread" or
                "System.Threading.Tasks.Task" or
                "System.Threading.Tasks.Task<TResult>";
        }
    }
}
