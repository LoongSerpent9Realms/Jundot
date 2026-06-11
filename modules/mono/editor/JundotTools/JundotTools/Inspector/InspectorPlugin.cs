using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
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

            AddOdinLikeControls(jundotObject);
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

        private void AddOdinLikeControls(JundotObject jundotObject)
        {
            Type type = jundotObject.GetType();

            foreach (var attribute in EnumerateInfoBoxes(type))
            {
                AddCustomControl(new OdinLikeInspectorInfoBox(attribute));
            }

            foreach (var method in EnumerateButtonMethods(type))
            {
                var attribute = method.GetCustomAttribute<JundotEditorButtonAttribute>(inherit: true);
                if (attribute == null)
                {
                    continue;
                }

                if (method.ContainsGenericParameters || method.GetParameters().Length != 0)
                {
                    GD.PushWarning($"Ignoring [JundotEditorButton] on '{type.FullName}.{method.Name}'. Editor buttons must be non-generic methods with no parameters.");
                    continue;
                }

                AddCustomControl(new OdinLikeInspectorButton(jundotObject, method, attribute));
            }
        }

        private static IEnumerable<JundotEditorInfoBoxAttribute> EnumerateInfoBoxes(Type type)
        {
            foreach (var attribute in type.GetCustomAttributes<JundotEditorInfoBoxAttribute>(inherit: true))
            {
                yield return attribute;
            }

            foreach (var member in EnumerateInspectableMembers(type))
            {
                foreach (var attribute in member.GetCustomAttributes<JundotEditorInfoBoxAttribute>(inherit: true))
                {
                    yield return attribute;
                }
            }
        }

        private static IEnumerable<MemberInfo> EnumerateInspectableMembers(Type type)
        {
            Type? currentType = type;
            while (currentType != typeof(JundotObject) && currentType != null)
            {
                const BindingFlags flags = BindingFlags.DeclaredOnly | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

                foreach (var field in currentType.GetFields(flags))
                {
                    yield return field;
                }

                foreach (var property in currentType.GetProperties(flags))
                {
                    yield return property;
                }

                foreach (var method in currentType.GetMethods(flags).Where(method => !method.IsSpecialName))
                {
                    yield return method;
                }

                currentType = currentType.BaseType;
            }
        }

        private static IEnumerable<MethodInfo> EnumerateButtonMethods(Type type)
        {
            Type? currentType = type;
            while (currentType != typeof(JundotObject) && currentType != null)
            {
                const BindingFlags flags = BindingFlags.DeclaredOnly | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

                foreach (var method in currentType.GetMethods(flags))
                {
                    if (!method.IsSpecialName && method.IsDefined(typeof(JundotEditorButtonAttribute), inherit: true))
                    {
                        yield return method;
                    }
                }

                currentType = currentType.BaseType;
            }
        }
    }
}
