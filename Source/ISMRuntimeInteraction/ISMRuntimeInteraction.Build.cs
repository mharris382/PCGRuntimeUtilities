// Copyright Max Harris

using UnrealBuildTool;

public class ISMRuntimeInteraction : ModuleRules
{
    public ISMRuntimeInteraction(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "ISMRuntimeCore",
                "ISMRuntimeSpatial",
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
