using System;

#nullable enable

namespace Jundot
{
    /// <summary>
    /// Adds an informational message to the editor inspector for a C# script type or member.
    /// </summary>
    [AttributeUsage(AttributeTargets.Class | AttributeTargets.Field | AttributeTargets.Property | AttributeTargets.Method,
        AllowMultiple = true, Inherited = true)]
    public sealed class JundotEditorInfoBoxAttribute : Attribute
    {
        /// <summary>
        /// Visual severity of an inspector info box.
        /// </summary>
        public enum Severity
        {
            /// <summary>
            /// Informational message.
            /// </summary>
            Info,

            /// <summary>
            /// Warning message.
            /// </summary>
            Warning,

            /// <summary>
            /// Error message.
            /// </summary>
            Error,
        }

        /// <summary>
        /// Message shown in the editor inspector.
        /// </summary>
        public string Message { get; }

        /// <summary>
        /// Visual severity of the message.
        /// </summary>
        public Severity Type { get; }

        /// <summary>
        /// Adds an informational message to the editor inspector for a C# script type or member.
        /// </summary>
        /// <param name="message">Message shown in the editor inspector.</param>
        /// <param name="type">Visual severity of the message.</param>
        public JundotEditorInfoBoxAttribute(string message, Severity type = Severity.Info)
        {
            Message = message;
            Type = type;
        }
    }
}
