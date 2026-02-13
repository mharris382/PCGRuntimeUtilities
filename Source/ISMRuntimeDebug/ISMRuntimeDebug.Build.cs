// Copyright Max Harris

using UnrealBuildTool;

public class ISMRuntimeDebug : ModuleRules
{
    public ISMRuntimeDebug(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "ISMRuntimeCore",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ISMRuntimePools",
                "ISMRuntimeSpatial",
                "ISMRuntimeResource",
                "ISMRuntimePhysics",
                "ISMRuntimeInteraction",
                "ISMRuntimeDamage",
            }
        );
    }
}
