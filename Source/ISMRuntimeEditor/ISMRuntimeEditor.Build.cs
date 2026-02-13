// Copyright Max Harris

using UnrealBuildTool;

public class ISMRuntimeEditor : ModuleRules
{
    public ISMRuntimeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "ISMRuntimeCore",
                "UnrealEd",
                "ISMRuntimePools",
                "ISMRuntimeSpatial",
                "ISMRuntimeResource",
                "ISMRuntimePhysics",
                "ISMRuntimeInteraction",
                "ISMRuntimeDamage",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "UMG",
                "Slate",
                "SlateCore",
            }
        );
    }
}
