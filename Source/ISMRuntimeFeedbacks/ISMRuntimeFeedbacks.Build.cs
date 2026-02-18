// Copyright Max Harris

using UnrealBuildTool;

public class ISMRuntimeFeedbacks : ModuleRules
{
    public ISMRuntimeFeedbacks(ReadOnlyTargetRules Target) : base(Target)
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
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Niagara",
            }
        );
    }
}
