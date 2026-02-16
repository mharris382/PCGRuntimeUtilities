#pragma once

#include "CoreMinimal.h"
#include "ISMRuntimeComponent.h"
#include "GameplayTagContainer.h"
#include "Delegates/DelegateCombinations.h"
#include "ISMResourceComponent.generated.h"

// Forward declarations
class UISMResourceDataAsset;

/**
 * Delegate called when a resource collection is validated.
 * Allows external systems to add custom validation logic.
 * @param Collector - The actor attempting to collect
 * @param Instance - The instance being collected
 * @param OutFailureReason - Set this to explain why collection failed
 * @return True if collection should proceed, false to block
 */
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FOnValidateResourceCollection,
    AActor* /*Collector*/,
    const FISMInstanceHandle& /*Instance*/,
    FText& /*OutFailureReason*/);

/**
 * Data about a completed collection event.
 * Passed to handlers to decide what to do with collected resources.
 */
USTRUCT(BlueprintType)
struct FResourceCollectionData
{
    GENERATED_BODY()
    
    /** The instance that was collected */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    FISMInstanceHandle Instance;
    
    /** Actor that collected this resource */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    AActor* Collector = nullptr;
    
    /** Transform where collection occurred */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    FTransform CollectionTransform;
    
    /** How long collection took (for stats/achievements) */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    float CollectionDuration = 0.0f;
    
    /** Yield multiplier applied (from collector tags) */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    float YieldMultiplier = 1.0f;
    
    /** Speed multiplier that was applied (for reference) */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    float SpeedMultiplier = 1.0f;
    
    /** Tags from the collector at time of collection */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    FGameplayTagContainer CollectorTags;
    
    /** Was this a multi-stage collection? If so, which stage completed? */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    int32 CompletedStage = 0;
    
    /** Total stages for this resource (1 = single stage) */
    UPROPERTY(BlueprintReadOnly, Category = "Collection")
    int32 TotalStages = 1;
    
    /** Custom data that can be set by game-specific logic */
    UPROPERTY(BlueprintReadWrite, Category = "Collection")
    TMap<FName, float> CustomData;
};

/**
 * Delegate called when resource collection completes.
 * This is where you decide what happens - spawn pickups, add to inventory, etc.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnResourceCollected, 
    UISMResourceComponent*, ResourceComponent,
    const FResourceCollectionData&, CollectionData);

/**
 * Native version for C++ binding
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnResourceCollectedNative,
    UISMResourceComponent* /*ResourceComponent*/,
    const FResourceCollectionData& /*CollectionData*/);

/**
 * Delegate for collection progress updates
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnResourceCollectionProgress,
    UISMResourceComponent*, ResourceComponent,
    const FISMInstanceHandle&, Instance,
    AActor*, Collector,
    float, Progress);

/**
 * Delegate for collection start/cancel
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnResourceCollectionEvent,
    UISMResourceComponent*, ResourceComponent,
    const FISMInstanceHandle&, Instance,
    AActor*, Collector);

/**
 * Tracks ongoing collection progress for an instance
 */
USTRUCT()
struct FResourceCollectionProgress
{
    GENERATED_BODY()
    
    /** Who is collecting this instance */
    UPROPERTY()
    TWeakObjectPtr<AActor> Collector;
    
    /** Progress (0-1) */
    float Progress = 0.0f;
    
    /** Time when collection started */
    float StartTime = 0.0f;
    
    /** Total time required (accounting for speed multipliers) */
    float RequiredTime = 0.0f;
    
    /** Current stage being collected (for multi-stage resources) */
    int32 CurrentStage = 0;
    
    /** Tags from collector at start of collection (cached) */
    FGameplayTagContainer CachedCollectorTags;
    
    /** Is this collection paused? */
    bool bIsPaused = false;
    
    /** Time spent paused (to adjust completion time) */
    float PausedTime = 0.0f;
};

/**
 * Component for managing collectible/harvestable ISM instances.
 * 
 * Features:
 * - Tag-based collection requirements
 * - Configurable collection time (instant or timed)
 * - Multi-stage collection support
 * - Speed/yield multipliers based on collector tags
 * - Event-driven resource handling (spawn pickups, add to inventory, etc.)
 * - Progress tracking for ongoing collections
 * 
 * Philosophy:
 * This component manages WHEN and IF collection happens, but not WHAT happens.
 * Use delegates/events to define what happens when resources are collected.
 */
UCLASS(Blueprintable, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMERESOURCE_API UISMResourceComponent : public UISMRuntimeComponent
{
    GENERATED_BODY()
    
public:
    UISMResourceComponent();
    
    // ===== Lifecycle =====
    
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    
    // ===== Resource Configuration =====
    
    /** Resource-specific data asset (optional - can use base InstanceData) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource")
    UISMResourceDataAsset* ResourceData;
    
    /** Tags describing what this resource is */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Tags")
    FGameplayTagContainer ResourceTags;
    
    /** Requirements that collector must meet (tag query) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Requirements")
    FGameplayTagQuery CollectorRequirements;
    
    /** Message shown when collector doesn't meet requirements */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Requirements")
    FText RequirementsFailureMessage;
    
    // ===== Collection Settings =====
    
    /** Base time to collect (0 = instant) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Collection", meta=(ClampMin="0.0"))
    float BaseCollectionTime = 2.0f;
    
    /** Can collection be interrupted? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Collection")
    bool bCanInterruptCollection = true;
    
    /** Reset progress on interrupt, or resume where left off? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Collection", meta=(EditCondition="bCanInterruptCollection"))
    bool bResetProgressOnInterrupt = false;
    
    /** Number of collection stages (1 = single stage, >1 = multi-stage like tree chopping) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Collection", meta=(ClampMin="1"))
    int32 CollectionStages = 1;
    
    /** Does each stage take the full collection time, or is time divided among stages? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Collection", meta=(EditCondition="CollectionStages > 1"))
    bool bDivideTimeAcrossStages = false;
    
    // ===== Tag-Based Modifiers =====
    
    /** Speed multipliers based on collector tags (multiplicative) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Modifiers")
    TMap<FGameplayTag, float> CollectorSpeedModifiers;
    
    /** Yield multipliers based on collector tags (multiplicative) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource|Modifiers")
    TMap<FGameplayTag, float> CollectorYieldModifiers;
    
    // ===== Collection API =====
    
    /**
     * Check if a collector can collect a specific instance.
     * @param CollectorTags - Tags from the collector
     * @param InstanceIndex - Instance to check
     * @param OutFailureReason - Populated with failure message if returns false
     */
    UFUNCTION(BlueprintCallable, Category = "Resource")
    bool CanCollectorGatherInstance(const FGameplayTagContainer& CollectorTags,
                                    int32 InstanceIndex,
                                    FText& OutFailureReason) const;
    
    /**
     * Start collecting an instance.
     * @param InstanceIndex - Instance to collect
     * @param Collector - Actor collecting the resource
     * @param CollectorTags - Tags from the collector (for requirements/modifiers)
     * @return True if collection started successfully
     */
    UFUNCTION(BlueprintCallable, Category = "Resource")
    bool StartCollection(int32 InstanceIndex, AActor* Collector, const FGameplayTagContainer& CollectorTags);
    
    /**
     * Cancel ongoing collection.
     * @param InstanceIndex - Instance being collected
     * @param bResetProgress - Force reset even if bResetProgressOnInterrupt is false
     */
    UFUNCTION(BlueprintCallable, Category = "Resource")
    void CancelCollection(int32 InstanceIndex, bool bResetProgress = false);
    
    /**
     * Complete collection immediately (skips time requirement).
     * Useful for instant collection or administrative commands.
     */
    UFUNCTION(BlueprintCallable, Category = "Resource")
    void CompleteCollectionImmediately(int32 InstanceIndex, AActor* Collector, const FGameplayTagContainer& CollectorTags);
    
    /**
     * Check if an instance is currently being collected.
     */
    UFUNCTION(BlueprintPure, Category = "Resource")
    bool IsInstanceBeingCollected(int32 InstanceIndex) const;
    
    /**
     * Get collection progress for an instance (0-1).
     * Returns 0 if not being collected.
     */
    UFUNCTION(BlueprintPure, Category = "Resource")
    float GetCollectionProgress(int32 InstanceIndex) const;
    
    /**
     * Get who is collecting an instance (nullptr if not being collected).
     */
    UFUNCTION(BlueprintPure, Category = "Resource")
    AActor* GetCollector(int32 InstanceIndex) const;
    
    // ===== Modifier Calculations =====
    
    /**
     * Calculate speed multiplier from collector tags.
     * @param CollectorTags - Tags from the collector
     * @return Multiplier (all matching modifiers multiplied together)
     */
    UFUNCTION(BlueprintPure, Category = "Resource|Modifiers")
    float CalculateSpeedMultiplier(const FGameplayTagContainer& CollectorTags) const;
    
    /**
     * Calculate yield multiplier from collector tags.
     * @param CollectorTags - Tags from the collector
     * @return Multiplier (all matching modifiers multiplied together)
     */
    UFUNCTION(BlueprintPure, Category = "Resource|Modifiers")
    float CalculateYieldMultiplier(const FGameplayTagContainer& CollectorTags) const;
    
    /**
     * Get the effective collection time for an instance (accounting for speed modifiers).
     * @param CollectorTags - Tags from the collector
     * @param Stage - Which stage (for multi-stage collections)
     */
    UFUNCTION(BlueprintPure, Category = "Resource")
    float GetEffectiveCollectionTime(const FGameplayTagContainer& CollectorTags, int32 Stage = 0) const;
    
    // ===== Per-Instance Overrides =====
    
    /**
     * Override collection requirements for a specific instance.
     * Useful for "ancient trees require special permission" scenarios.
     */
    UFUNCTION(BlueprintCallable, Category = "Resource")
    void SetInstanceRequirements(int32 InstanceIndex, const FGameplayTagQuery& Requirements);
    
    /**
     * Clear instance-specific requirements (falls back to component default).
     */
    UFUNCTION(BlueprintCallable, Category = "Resource")
    void ClearInstanceRequirements(int32 InstanceIndex);
    
    /**
     * Get effective requirements for a specific instance.
     */
    UFUNCTION(BlueprintPure, Category = "Resource")
    FGameplayTagQuery GetInstanceRequirements(int32 InstanceIndex) const;
    
    // ===== Events =====
    
    /** Called when collection starts */
    UPROPERTY(BlueprintAssignable, Category = "Resource|Events")
    FOnResourceCollectionEvent OnCollectionStarted;
    
    /** Called when collection completes - THIS IS WHERE YOU SPAWN PICKUPS/ADD TO INVENTORY */
    UPROPERTY(BlueprintAssignable, Category = "Resource|Events")
    FOnResourceCollected OnResourceCollected;
    
    /** Called during collection for progress updates */
    UPROPERTY(BlueprintAssignable, Category = "Resource|Events")
    FOnResourceCollectionProgress OnCollectionProgress;
    
    /** Called when collection is cancelled/interrupted */
    UPROPERTY(BlueprintAssignable, Category = "Resource|Events")
    FOnResourceCollectionEvent OnCollectionCancelled;
    
    /** Native C++ delegate for OnResourceCollected (no BP overhead) */
    FOnResourceCollectedNative OnResourceCollectedNative;
    
    /** External validation delegate (allows other systems to validate collection) */
    FOnValidateResourceCollection OnValidateCollection;
    
protected:
    // ===== Virtual Hooks =====
    
    /**
     * Called when validating collection requirements.
     * Override to add custom logic beyond tag queries.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Resource")
    bool ValidateCollectionRequirements(const FGameplayTagContainer& CollectorTags,
                                       int32 InstanceIndex,
                                       FText& OutFailureReason) const;
    virtual bool ValidateCollectionRequirements_Implementation(const FGameplayTagContainer& CollectorTags,
                                                               int32 InstanceIndex,
                                                               FText& OutFailureReason) const;
    
    /**
     * Called when collection starts (after validation).
     * Override for custom behavior (VFX, state changes, etc.).
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Resource")
    void OnCollectionStartedInternal(int32 InstanceIndex, AActor* Collector);
    virtual void OnCollectionStartedInternal_Implementation(int32 InstanceIndex, AActor* Collector);
    
    /**
     * Called when a collection stage completes (for multi-stage resources).
     * Override to add per-stage effects (tree shakes, damage appears, etc.).
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Resource")
    void OnCollectionStageCompleted(int32 InstanceIndex, AActor* Collector, int32 CompletedStage, int32 TotalStages);
    virtual void OnCollectionStageCompleted_Implementation(int32 InstanceIndex, AActor* Collector, int32 CompletedStage, int32 TotalStages);
    
    /**
     * Called when collection fully completes (before broadcasting OnResourceCollected).
     * Override to customize collection data or add final effects.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Resource")
    void OnCollectionCompletedInternal(int32 InstanceIndex, const FResourceCollectionData& CollectionData);
    virtual void OnCollectionCompletedInternal_Implementation(int32 InstanceIndex, const FResourceCollectionData& CollectionData);
    
    /**
     * Called when collection is cancelled.
     * Override for custom cleanup.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Resource")
    void OnCollectionCancelledInternal(int32 InstanceIndex, AActor* Collector);
    virtual void OnCollectionCancelledInternal_Implementation(int32 InstanceIndex, AActor* Collector);
    
    // ===== ISMRuntimeComponent Overrides =====
    
    virtual void BuildComponentTags() override;
    virtual void OnInstancePreDestroy(int32 InstanceIndex) override;
    
    // ===== Internal State =====
    
    /** Active collections (sparse - only instances being collected) */
    UPROPERTY()
    TMap<int32, FResourceCollectionProgress> ActiveCollections;
    
    /** Per-instance requirement overrides (sparse) */
    UPROPERTY()
    TMap<int32, FGameplayTagQuery> PerInstanceRequirements;
    
    // ===== Helper Functions =====
    
    /** Update a collection's progress (called from Tick) */
    void UpdateCollectionProgress(int32 InstanceIndex, FResourceCollectionProgress& Progress, float DeltaTime);
    
    /** Complete a collection (called when progress reaches 1.0 or CompleteCollectionImmediately) */
    void FinalizeCollection(int32 InstanceIndex, FResourceCollectionProgress& Progress);
    
    /** Build collection data struct for event broadcasting */
    FResourceCollectionData BuildCollectionData(int32 InstanceIndex, const FResourceCollectionProgress& Progress);
    
    /** Check tag query requirements */
    bool CheckTagRequirements(const FGameplayTagQuery& Query, const FGameplayTagContainer& CollectorTags, FText& OutFailureReason) const;
};