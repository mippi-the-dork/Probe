using UnrealBuildTool;

public class ProbeEditor : ModuleRules
{
    public ProbeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Slate", "SlateCore", "UnrealEd", "LevelEditor", "ToolMenus",
            "Projects", "ApplicationCore", "AssetRegistry", "AssetTools", "AssetDefinition", "ContentBrowser", "PropertyEditor", "EditorFramework", "Json"
        });

        // Slate list and combo widgets refer to EKeys symbols exported by InputCore.
        // This dependency must be linked into ProbeEditor, not just included transitively.
        PrivateDependencyModuleNames.Add("InputCore");
    }
}
