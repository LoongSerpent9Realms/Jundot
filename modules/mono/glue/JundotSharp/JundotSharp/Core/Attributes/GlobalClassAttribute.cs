using System;

#nullable enable

namespace Jundot
{
    /// <summary>
    /// Exposes the target class as a global script class to Jundot Engine.
    /// </summary>
    [AttributeUsage(AttributeTargets.Class)]
    public sealed class GlobalClassAttribute : Attribute { }
}
