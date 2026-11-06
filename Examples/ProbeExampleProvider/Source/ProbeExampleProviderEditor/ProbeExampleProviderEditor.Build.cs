using UnrealBuildTool;

public class ProbeExampleProviderEditor : ModuleRules
{
    public ProbeExampleProviderEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Projects" });
        // Deliberately no testing-system dependency: this module compiles independently.
    }
}
