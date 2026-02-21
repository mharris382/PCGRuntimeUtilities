// Copyright Max Harris

using UnrealBuildTool;

public class ISMRuntimePCGInterop : ModuleRules
{
    public ISMRuntimePCGInterop(ReadOnlyTargetRules Target) : base(Target)
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
                "PCG",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {

            }
        );
    }
}
