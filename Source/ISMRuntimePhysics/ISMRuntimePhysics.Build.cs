// Copyright Max Harris

using UnrealBuildTool;

public class ISMRuntimePhysics : ModuleRules
{
    public ISMRuntimePhysics(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "ISMRuntimeCore",
                "ISMRuntimePools",
                "GameplayTags",
                "PhysicsCore"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {

            }
        );
    }
}
