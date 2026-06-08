using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace Jundot.SourceGenerators
{
    [Generator]
    public class JundotPluginsInitializerGenerator : ISourceGenerator
    {
        public void Initialize(GeneratorInitializationContext context)
        {
        }

        public void Execute(GeneratorExecutionContext context)
        {
            if (context.IsJundotToolsProject() || context.IsJundotSourceGeneratorDisabled("JundotPluginsInitializer"))
                return;

            string source =
                @"using System;
using System.Runtime.InteropServices;
using Jundot.Bridge;
using Jundot.NativeInterop;

namespace JundotPlugins.Game
{
    internal static partial class Main
    {
        [UnmanagedCallersOnly(EntryPoint = ""jundotsharp_game_main_init"")]
        private static jundot_bool InitializeFromGameProject(IntPtr jundotDllHandle, IntPtr outManagedCallbacks,
            IntPtr unmanagedCallbacks, int unmanagedCallbacksSize)
        {
            try
            {
                DllImportResolver dllImportResolver = new JundotDllImportResolver(jundotDllHandle).OnResolveDllImport;

                var coreApiAssembly = typeof(global::Jundot.JundotObject).Assembly;

                NativeLibrary.SetDllImportResolver(coreApiAssembly, dllImportResolver);

                NativeFuncs.Initialize(unmanagedCallbacks, unmanagedCallbacksSize);

                ManagedCallbacks.Create(outManagedCallbacks);

                ScriptManagerBridge.LookupScriptsInAssembly(typeof(global::JundotPlugins.Game.Main).Assembly);

                return jundot_bool.True;
            }
            catch (Exception e)
            {
                global::System.Console.Error.WriteLine(e);
                return false.ToJundotBool();
            }
        }
    }
}
";

            context.AddSource("JundotPlugins.Game.generated",
                SourceText.From(source, Encoding.UTF8));
        }
    }
}
