#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ISMRuntimeSettings.generated.h"


UCLASS(Abstract, config = Game, defaultconfig)
class ISMRUNTIMECORE_API UISMRuntimeSettingsBase : public UDeveloperSettings
{
    GENERATED_BODY()

#if WITH_EDITOR

public:
    virtual FName GetCategoryName() const override { return TEXT("ISM Runtime"); }
#endif


};

/**
 * Project-wide settings for ISM Runtime system
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "ISM Runtime"))
class ISMRUNTIMECORE_API UISMRuntimeSettings : public UISMRuntimeSettingsBase
{
    GENERATED_BODY()
    
public:
    
    // ===== Performance =====

    UPROPERTY(config, EditAnywhere, Category = "Batching")
	bool bUseAsyncBatchScheduler = false;
    
    /** Default spatial index cell size in cm (10m = 1000cm) */
    UPROPERTY(config, EditAnywhere, Category = "Performance", meta=(ClampMin="100.0"))
    float DefaultSpatialCellSize = 1000.0f;
    
    /** Default maximum query results */
    UPROPERTY(config, EditAnywhere, Category = "Performance")
    int32 DefaultMaxQueryResults = 1000;
    
    // ===== Debug =====
    
    /** Enable debug visualization */
    UPROPERTY(config, EditAnywhere, Category = "Debug")
    bool bEnableDebugVisualization = false;
    
    /** Show spatial index grid */
    UPROPERTY(config, EditAnywhere, Category = "Debug")
    bool bShowSpatialIndexGrid = false;
    
    /** Show instance states */
    UPROPERTY(config, EditAnywhere, Category = "Debug")
    bool bShowInstanceStates = false;
    
    /** Log performance warnings */
    UPROPERTY(config, EditAnywhere, Category = "Debug")
    bool bLogPerformanceWarnings = true;
    
    // ===== Features =====
    
    /** Enable automatic tick optimization */
    UPROPERTY(config, EditAnywhere, Category = "Features")
    bool bAutoOptimizeTicking = true;
    
    /** Enable automatic statistics collection */
    UPROPERTY(config, EditAnywhere, Category = "Features")
    bool bCollectStatistics = true;
    
#if WITH_EDITOR
    virtual FText GetSectionText() const override
    {
        return NSLOCTEXT("ISMRuntime", "ISMRuntimeSettingsSection", "ISM Runtime");
    }

    virtual FText GetSectionDescription() const override
    {
        return NSLOCTEXT("ISMRuntime", "ISMRuntimeSettingsDescription",
            "Configure settings for the ISM Runtime system, including performance tuning and debug visualization options.");
    }
#endif
};