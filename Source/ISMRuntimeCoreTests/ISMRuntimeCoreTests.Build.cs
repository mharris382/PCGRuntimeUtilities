// ISMRuntimeCoreTests.Build.cs
using UnrealBuildTool;

public class ISMRuntimeCoreTests : ModuleRules
{
    public ISMRuntimeCoreTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        // Test module dependencies
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "ISMRuntimeCore"
        });
        
        // Only compile in editor builds
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
            });
        }
    }
}