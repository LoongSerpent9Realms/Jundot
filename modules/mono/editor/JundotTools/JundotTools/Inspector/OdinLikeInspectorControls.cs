using System;
using System.Reflection;
using System.Threading.Tasks;
using Jundot;
using JundotTools.Internals;

namespace JundotTools.Inspector
{
    internal partial class OdinLikeInspectorButton : Button
    {
        private readonly JundotObject _target;
        private readonly MethodInfo _method;
        private readonly string? _iconName;

        public OdinLikeInspectorButton(JundotObject target, MethodInfo method, JundotEditorButtonAttribute attribute)
        {
            _target = target;
            _method = method;
            _iconName = attribute.Icon;

            Text = string.IsNullOrEmpty(attribute.Text) ? HumanizeIdentifier(method.Name) : attribute.Text;
            TooltipText = attribute.Tooltip ?? string.Empty;
            SizeFlagsHorizontal = SizeFlags.ExpandFill;

            Pressed += InvokeMethod;
        }

        public override void _Ready()
        {
            if (!string.IsNullOrEmpty(_iconName))
            {
                Icon = GetThemeIcon(_iconName, "EditorIcons");
            }
        }

        private async void InvokeMethod()
        {
            try
            {
                object? result = _method.Invoke(_target, null);
                if (result is Task task)
                {
                    await task;
                }
            }
            catch (TargetInvocationException e) when (e.InnerException != null)
            {
                GD.PushError($"Exception while invoking editor button '{_method.Name}':\n{e.InnerException}");
            }
            catch (Exception e)
            {
                GD.PushError($"Exception while invoking editor button '{_method.Name}':\n{e}");
            }
        }

        private static string HumanizeIdentifier(string identifier)
        {
            if (string.IsNullOrEmpty(identifier))
            {
                return identifier;
            }

            var result = new System.Text.StringBuilder(identifier.Length + 8);
            for (int i = 0; i < identifier.Length; i++)
            {
                char c = identifier[i];
                if (i > 0 && char.IsUpper(c) && !char.IsUpper(identifier[i - 1]))
                {
                    result.Append(' ');
                }
                else if (c == '_')
                {
                    result.Append(' ');
                    continue;
                }

                result.Append(c);
            }

            return result.ToString();
        }
    }

    internal partial class OdinLikeInspectorInfoBox : HBoxContainer
    {
        private readonly JundotEditorInfoBoxAttribute _attribute;

        public OdinLikeInspectorInfoBox(JundotEditorInfoBoxAttribute attribute)
        {
            _attribute = attribute;
        }

        public override void _Ready()
        {
            SetAnchorsPreset(LayoutPreset.TopWide);

            var iconTexture = GetThemeIcon(GetIconName(), "EditorIcons");
            var icon = new TextureRect
            {
                Texture = iconTexture,
                ExpandMode = TextureRect.ExpandModeEnum.FitWidthProportional,
                CustomMinimumSize = iconTexture.GetSize(),
            };
            icon.SizeFlagsVertical = SizeFlags.ShrinkCenter;

            var label = new Label
            {
                Text = _attribute.Message.TTR(),
                AutowrapMode = TextServer.AutowrapMode.WordSmart,
                CustomMinimumSize = new Vector2(100f, 0f),
            };
            label.AddThemeColorOverride("font_color", GetThemeColor(GetColorName(), "Editor"));
            label.SizeFlagsHorizontal = SizeFlags.Fill | SizeFlags.Expand;

            AddChild(icon);
            AddChild(label);
        }

        private string GetIconName()
        {
            return _attribute.Type switch
            {
                JundotEditorInfoBoxAttribute.Severity.Warning => "StatusWarning",
                JundotEditorInfoBoxAttribute.Severity.Error => "StatusError",
                _ => "NodeInfo",
            };
        }

        private string GetColorName()
        {
            return _attribute.Type switch
            {
                JundotEditorInfoBoxAttribute.Severity.Warning => "warning_color",
                JundotEditorInfoBoxAttribute.Severity.Error => "error_color",
                _ => "font_color",
            };
        }
    }
}
