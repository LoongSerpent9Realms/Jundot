using System;
using System.Collections.Generic;
using Jundot;
using JundotTools.Build;
using JundotTools.Utils;

namespace JundotTools.Inspector
{
    public partial class InspectorPlugin : EditorInspectorPlugin
    {
        public override bool _CanHandle(JundotObject jundotObject)
        {
            if (jundotObject == null)
            {
                return false;
            }

            foreach (var script in EnumerateScripts(jundotObject))
            {
                if (script is CSharpScript)
                {
                    return true;
                }
            }
            return false;
        }

        public override void _ParseBegin(JundotObject jundotObject)
        {
            foreach (var script in EnumerateScripts(jundotObject))
            {
                if (script is not CSharpScript)
                    continue;

                string scriptPath = script.ResourcePath;

                if (string.IsNullOrEmpty(scriptPath))
                {
                    // Generic types used empty paths in older versions of Jundot
                    // so we assume your project is out of sync.
                    AddCustomControl(new InspectorOutOfSyncWarning());
                    break;
                }

                if (scriptPath.StartsWith("csharp://"))
                {
                    // This is a virtual path used by generic types, extract the real path.
                    var scriptPathSpan = scriptPath.AsSpan("csharp://".Length);
                    scriptPathSpan = scriptPathSpan[..scriptPathSpan.IndexOf(':')];
                    scriptPath = $"res://{scriptPathSpan}";
                }

                if (File.GetLastWriteTime(scriptPath) > BuildManager.LastValidBuildDateTime)
                {
                    AddCustomControl(new InspectorOutOfSyncWarning());
                    break;
                }
            }
        }

        private static IEnumerable<Script> EnumerateScripts(JundotObject jundotObject)
        {
            var script = jundotObject.GetScript().As<Script>();
            while (script != null)
            {
                yield return script;
                script = script.GetBaseScript();
            }
        }
    }
}
