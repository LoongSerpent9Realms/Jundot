using System.Threading.Tasks;
using Xunit;

namespace Jundot.SourceGenerators.Tests;

public class AlcUnloadabilityAnalyzerTests
{
    [Fact]
    public async Task StrongGCHandleAnalyzerTest()
    {
        await CSharpAnalyzerVerifier<AlcUnloadabilityAnalyzer>.Verify("AlcUnloadabilityAnalyzer.GD0501.cs");
    }

    [Fact]
    public async Task StaticFieldAnalyzerTest()
    {
        await CSharpAnalyzerVerifier<AlcUnloadabilityAnalyzer>.Verify("AlcUnloadabilityAnalyzer.GD0502.cs");
    }

    [Fact]
    public async Task StaticEventSubscriptionAnalyzerTest()
    {
        await CSharpAnalyzerVerifier<AlcUnloadabilityAnalyzer>.Verify("AlcUnloadabilityAnalyzer.GD0503.cs");
    }

    [Fact]
    public async Task RuntimeScriptIsIgnoredTest()
    {
        await CSharpAnalyzerVerifier<AlcUnloadabilityAnalyzer>.Verify("AlcUnloadabilityAnalyzer.NoDiagnostic.cs");
    }
}
