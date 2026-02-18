// Copyright Max Harris

using UnrealBuildTool;

public class ISMRuntimeDestruction : ModuleRules
{
    public ISMRuntimeDestruction(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "ISMRuntimeCore",
                "GameplayTags",
                "ISMRuntimePools",
                "GeometryCollectionEngine",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {

            }
        );
    }
}
