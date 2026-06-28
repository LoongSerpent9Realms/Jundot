using System;

#nullable enable

namespace Jundot
{
    /// <summary>
    /// Adds a clickable button for the annotated parameterless method in the editor inspector.
    /// </summary>
    [AttributeUsage(AttributeTargets.Method, AllowMultiple = false, Inherited = true)]
    public sealed class JundotEditorButtonAttribute : Attribute
    {
        /// <summary>
        /// Optional label for the generated inspector button. When omitted, the method name is humanized.
        /// </summary>
        public string? Text { get; }

        /// <summary>
        /// Optional tooltip for the generated inspector button.
        /// </summary>
        public string? Tooltip { get; init; }

        /// <summary>
        /// Optional icon name fetched from the <c>EditorIcons</c> theme type.
        /// </summary>
        public string? Icon { get; init; }

        /// <summary>
        /// Adds a clickable button for the annotated parameterless method in the editor inspector.
        /// </summary>
        /// <param name="text">Optional label for the generated inspector button.</param>
        public JundotEditorButtonAttribute(string? text = null)
        {
            Text = text;
        }
    }
}
