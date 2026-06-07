namespace GodotPackageBuilder;

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        if (!IsAiPackageBuilderSession(args))
        {
            MessageBox.Show(
                "Godot Package Builder is reserved for AI/developer automation sessions.",
                "Godot Package Builder",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
            return;
        }

        Application.Run(new MainForm());
    }

    private static bool IsAiPackageBuilderSession(string[] args)
    {
        if (args.Any(a => string.Equals(a, "--ai-package-builder", StringComparison.OrdinalIgnoreCase)))
            return true;

        var value = Environment.GetEnvironmentVariable("GODOT_AI_PACKAGE_BUILDER") ?? "";
        return value.Equals("1", StringComparison.OrdinalIgnoreCase) ||
               value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
               value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
               value.Equals("on", StringComparison.OrdinalIgnoreCase);
    }
}
