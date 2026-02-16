#pragma once

#include "CoreMinimal.h"
#include "ISMInstanceDataAsset.h"
#include "ISMPoolDataAsset.generated.h"

/**
 * Data asset defining pooling behavior for ISM instances.
 * Extends base ISMInstanceDataAsset with pool configuration.
 *
 * This base pooling asset can be used directly or extended by module-specific
 * assets (e.g., UISMPhysicsDataAsset) to add additional properties.
 *
 * Design Philosophy:
 * - Data-driven: All pooling behavior configured without code
 * - Inheritance-friendly: Module-specific assets extend this
 * - Performance-focused: Separate pools per actor class
 * - Safe by default: Reasonable defaults, optional optimizations
 */
UCLASS(BlueprintType)
class ISMRUNTIMEPOOLS_API UISMPoolDataAsset : public UISMInstanceDataAsset
{
    GENERATED_BODY()

public:
    // ===== Core Configuration =====
    // These properties are required for the pool system to function.
    // Later, we'll add custom editor validation to enforce this.
    // TODO: Move StaticMesh from UISMInstanceDataAsset to "Core" category as well

    /**
     * Actor class to spawn for pool instances.
     * Should implement IISMPoolable interface for proper lifecycle management.
     *
     * QUESTION ANSWERED: Yes, we can validate at runtime that the class implements
     * IISMPoolable. Unfortunately, UPROPERTY meta "MustImplement" doesn't work reliably
     * for actor classes, so we'll add a validation function instead.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Core",
        meta = (Tooltip = "Actor class to spawn when instances are converted. Should implement IISMPoolable interface."))
    TSubclassOf<AActor> PooledActorClass;

    // ===== Pooling Behavior =====

    /**
     * Enable or disable pooling for this asset.
     *
     * QUESTION ANSWERED: Why allow disabling? Two reasons:
     * 1. Performance comparison: Test pooled vs non-pooled to measure actual gains
     * 2. Gradual rollout: Enable pooling incrementally across different asset types
     * 3. Debug mode: Easier to debug with real spawns vs pool retrieval
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling",
        meta = (Tooltip = "Enable pooling for this asset type. Disable for performance comparison or debugging."))
    bool bEnablePooling = true;

    /**
     * Number of actors to pre-spawn during pool initialization.
     * Higher values reduce runtime allocation but increase startup time and memory.
     *
     * Recommended values:
     * - Ambient effects (twigs, leaves): 20-50
     * - Gameplay objects (physics props): 10-20
     * - Rare events (explosions): 3-5
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling",
        meta = (ClampMin = "0", UIMin = "0", UIMax = "100",
            Tooltip = "Number of actors to pre-spawn. Higher = less runtime allocation, more startup cost.",
            EditCondition = "bEnablePooling", EditConditionHides))
    int32 InitialPoolSize = 10;

    /**
     * Number of actors to spawn when pool is exhausted.
     * Larger values reduce allocation frequency but waste memory if not all are used.
     *
     * Generally set to 20-50% of InitialPoolSize.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling",
        meta = (ClampMin = "1", UIMin = "1", UIMax = "50",
            Tooltip = "How many actors to spawn when pool runs out. Larger = fewer allocations, more memory.",
            EditCondition = "bEnablePooling", EditConditionHides))
    int32 PoolGrowSize = 5;

    /**
     * Maximum pool size (total actors spawned).
     * 0 = unlimited growth (use with caution!)
     *
     * Recommended to set a reasonable cap (2-3x InitialPoolSize) to prevent
     * runaway memory usage from temporary spikes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling",
        meta = (ClampMin = "0", UIMin = "0", UIMax = "500",
            Tooltip = "Maximum total pool size. 0 = unlimited (use carefully!).",
            EditCondition = "bEnablePooling", EditConditionHides))
    int32 MaxPoolSize = 0;

    /**
     * Allow the pool to destroy unused actors to save memory.
     *
     * When enabled, pools will destroy excess actors that haven't been used recently.
     * This is useful for pools that have temporary spikes but low average usage.
     *
     * Default: Disabled (better performance, stable memory usage)
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling",
        meta = (Tooltip = "Allow destroying unused actors to reclaim memory. Usually leave disabled for stable performance.",
            EditCondition = "bEnablePooling", EditConditionHides))
    bool bAllowPoolShrinking = false;

    /**
     * Percentage of pool that must be unused before shrinking occurs.
     * Only applies if bAllowPoolShrinking is true.
     *
     * 0.5 = 50% unused, 0.75 = 75% unused
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling",
        meta = (ClampMin = "0.1", ClampMax = "0.9", UIMin = "0.1", UIMax = "0.9",
            Tooltip = "Percentage of pool unused before shrinking (0.5 = 50% unused).",
            EditCondition = "bEnablePooling && bAllowPoolShrinking", EditConditionHides))
    float ShrinkThreshold = 0.5f;

    // ===== Validation =====

#if WITH_EDITOR
    /**
     * Validate that PooledActorClass implements IISMPoolable.
     * Called automatically in editor when asset is saved or modified.
     */
    //virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

    /**
     * Check if the configured actor class is valid for pooling.
     * Returns error message if invalid, empty string if valid.
     */
    //FString ValidatePoolConfiguration() const;
#endif

    /**
     * Runtime validation - check if actor class implements IISMPoolable.
     * Returns true if valid, false if not (with warning log).
     */
   // bool IsValidPoolConfiguration() const;
};