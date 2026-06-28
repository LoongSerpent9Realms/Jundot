using Jundot;
using System;
using System.IO;
using System.Linq;
using System.Xml.Linq;
using JundotTools.Internals;
using JundotTools.ProjectEditor;

namespace JundotTools
{
    public static class CsProjOperations
    {
        private const string JundotNuGetSourceKey = "Jundot";

        public static string GenerateGameProject(string dir, string name)
        {
            try
            {
                string guid = ProjectGenerator.GenAndSaveGameProject(dir, name);
                EnsureJundotNuGetConfig(dir);
                return guid;
            }
            catch (Exception e)
            {
                GD.PushError(e.ToString());
                return string.Empty;
            }
        }

        public static void EnsureJundotNuGetConfig(string dir)
        {
            try
            {
                string nupkgsPath = Path.Combine(JundotSharpDirs.DataEditorToolsDir, "nupkgs");
                string nugetConfigPath = Path.Combine(dir, "NuGet.config");

                XDocument document;
                if (File.Exists(nugetConfigPath))
                {
                    document = XDocument.Load(nugetConfigPath, LoadOptions.PreserveWhitespace);
                }
                else
                {
                    document = new XDocument(
                        new XDeclaration("1.0", "utf-8", null),
                        new XElement("configuration"));
                }

                XElement configuration = document.Element("configuration") ?? new XElement("configuration");
                if (configuration.Parent == null)
                {
                    document.RemoveNodes();
                    document.Add(configuration);
                }

                XElement packageSources = configuration.Element("packageSources") ??
                    new XElement("packageSources");
                if (packageSources.Parent == null)
                {
                    configuration.Add(packageSources);
                }

                XElement? jundotSource = packageSources.Elements("add")
                    .FirstOrDefault(element => string.Equals(
                        (string?)element.Attribute("key"),
                        JundotNuGetSourceKey,
                        StringComparison.OrdinalIgnoreCase));

                if (jundotSource == null)
                {
                    packageSources.Add(new XElement("add",
                        new XAttribute("key", JundotNuGetSourceKey),
                        new XAttribute("value", nupkgsPath)));
                }
                else
                {
                    jundotSource.SetAttributeValue("value", nupkgsPath);
                }

                document.Save(nugetConfigPath);
            }
            catch (Exception e)
            {
                GD.PushError(e.ToString());
                throw;
            }
        }
    }
}
