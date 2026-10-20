using UnrealBuildTool;

public class ProbeEditor : ModuleRules
{
    public ProbeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Slate", "SlateCore", "UnrealEd", "LevelEditor", "ToolMenus",
            "Projects", "AssetRegistry", "AssetTools", "PropertyEditor", "EditorFramework"
        });
    }
}
