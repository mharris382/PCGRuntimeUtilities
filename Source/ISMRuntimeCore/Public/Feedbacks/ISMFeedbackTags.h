// ISMFeedbackTags.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ISMFeedbackTags.generated.h"

/**
 * Configuration struct holding feedback tags for common ISM lifecycle events.
 * 
 * Design Philosophy:
 * - Data-driven feedback assignment via gameplay tags
 * - Designer-friendly configuration in data assets
 * - Per-operation granularity (spawn, destroy, hide, show, etc.)
 * - Fallback hierarchy: Instance Data Asset → Component Defaults → None
 * - Empty tags mean "no feedback" for that operation
 * 
 * Usage Pattern:
 * 
 *   // In ISMInstanceDataAsset:
 *   FeedbackTags.OnSpawn = FGameplayTag::RequestGameplayTag("Feedback.Tree.Spawn");
 *   FeedbackTags.OnDestroy = FGameplayTag::RequestGameplayTag("Feedback.Tree.Destroy");
 *   
 *   // In ISMRuntimeComponent:
 *   DestroyInstance(InstanceIndex); // Automatically triggers OnDestroy feedback
 *   
 *   // Manual control:
 *   DestroyInstance(InstanceIndex, false, false); // bShouldTriggerFeedback = false
 * 
 * Recommended Tag Hierarchy:
 * - Feedback.ISM.Spawn.{ObjectType}       (e.g., Feedback.ISM.Spawn.Tree)
 * - Feedback.ISM.Destroy.{ObjectType}     (e.g., Feedback.ISM.Destroy.Rock)
 * - Feedback.ISM.Hide.{ObjectType}        (e.g., Feedback.ISM.Hide.Foliage)
 * - Feedback.ISM.Show.{ObjectType}        (e.g., Feedback.ISM.Show.Foliage)
 * - Feedback.ISM.Update.{ObjectType}      (e.g., Feedback.ISM.Update.Debris)
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMFeedbackTags
{
    GENERATED_BODY()
    
    // ===== Lifecycle Events =====
    
    /**
     * Feedback tag for when instances are spawned/added.
     * Triggered by: AddInstance, BatchAddInstances
     * 
     * Use cases:
     * - Spawn VFX (dust puff, magic sparkle)
     * - Spawn audio (pop, whoosh)
     * 
     * Example: Feedback.ISM.Spawn.Tree
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Lifecycle", 
        meta=(Categories="Feedback"))
    FGameplayTag OnSpawn;
    
    /**
     * Feedback tag for when instances are destroyed.
     * Triggered by: DestroyInstance, BatchDestroyInstances
     * 
     * Use cases:
     * - Destruction VFX (explosion, debris)
     * - Destruction audio (crash, shatter)
     * 
     * Example: Feedback.ISM.Destroy.Rock
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Lifecycle",
        meta=(Categories="Feedback"))
    FGameplayTag OnDestroy;
    
    /**
     * Feedback tag for when instances are hidden.
     * Triggered by: HideInstance
     * 
     * Use cases:
     * - Subtle disappear VFX (fade, dissolve)
     * - Disappear audio (whoosh out)
     * 
     * Example: Feedback.ISM.Hide.Foliage
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Lifecycle",
        meta=(Categories="Feedback"))
    FGameplayTag OnHide;
    
    /**
     * Feedback tag for when instances are shown.
     * Triggered by: ShowInstance
     * 
     * Use cases:
     * - Appear VFX (fade in, materialize)
     * - Appear audio (whoosh in)
     * 
     * Example: Feedback.ISM.Show.Foliage
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Lifecycle",
        meta=(Categories="Feedback"))
    FGameplayTag OnShow;
    
    /**
     * Feedback tag for when instance transforms are updated.
     * Triggered by: UpdateInstanceTransform
     * 
     * Use cases:
     * - Movement audio (scrape, slide)
     * - Movement VFX (dust trail)
     * - Generally only used for significant/animated transforms
     * 
     * Example: Feedback.ISM.Update.Debris
     * 
     * Note: Be careful with this - transform updates can be very frequent!
     * Consider using intensity/distance thresholds before triggering.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Lifecycle",
        meta=(Categories="Feedback"))
    FGameplayTag OnTransformUpdate;
    
    // ===== Batch Events =====

protected:
    UPROPERTY(EditAnywhere, Category = "Feedback|Batch")
	bool bUseSeparateFeedbackForBatched = true;

    /**
     * Feedback tag for batch spawn operations.
     * Triggered by: BatchAddInstances (only if this tag is set, overrides OnSpawn)
     * 
     * Use cases:
     * - Large-scale spawn events (forest generation, explosion debris field)
     * - Single audio event instead of hundreds
     * - Aggregate VFX for performance
     * 
     * Example: Feedback.ISM.Spawn.Batch.Forest
     * 
     * Behavior:
     * - If set: Triggers once for entire batch (ignores OnSpawn)
     * - If empty: Falls back to OnSpawn for each instance
     */
    UPROPERTY(EditAnywhere, Category = "Feedback|Batch",
        meta = (EditCondition = "bUseSeparateFeedbackForBatched", EditConditionHides))
    FGameplayTag OnBatchSpawn;
    
    /**
     * Feedback tag for batch destroy operations.
     * Triggered by: BatchDestroyInstances (only if this tag is set, overrides OnDestroy)
     * 
     * Use cases:
     * - Mass destruction (building collapse, area clearing)
     * - Single audio event instead of hundreds
     * - Aggregate VFX for performance
     * 
     * Example: Feedback.ISM.Destroy.Batch.Building
     * 
     * Behavior:
     * - If set: Triggers once for entire batch (ignores OnDestroy)
     * - If empty: Falls back to OnDestroy for each instance
     */
    UPROPERTY(EditAnywhere, Category = "Feedback|Batch",
        meta = (EditCondition = "bUseSeparateFeedbackForBatched", EditConditionHides))
    FGameplayTag OnBatchDestroy;
    
    // ===== Helper Functions =====
public:
    /** Check if any lifecycle tags are set */
    bool HasAnyTags() const
    {
        return OnSpawn.IsValid() || 
               OnDestroy.IsValid() || 
               OnHide.IsValid() || 
               OnShow.IsValid() || 
               OnTransformUpdate.IsValid() ||
               OnBatchSpawn.IsValid() ||
               OnBatchDestroy.IsValid();
    }
    
    /** Check if a specific operation has a feedback tag */
    bool HasSpawnFeedback() const { return OnSpawn.IsValid(); }
    bool HasDestroyFeedback() const { return OnDestroy.IsValid(); }
    bool HasHideFeedback() const { return OnHide.IsValid(); }
    bool HasShowFeedback() const { return OnShow.IsValid(); }
    bool HasTransformUpdateFeedback() const { return OnTransformUpdate.IsValid(); }
    bool HasBatchSpawnFeedback() const { return GetBatchSpawnTag().IsValid(); }
    bool HasBatchDestroyFeedback() const { return GetBatchDestroyTag().IsValid(); }

	FGameplayTag GetBatchSpawnTag() const { return bUseSeparateFeedbackForBatched ? OnBatchSpawn : OnSpawn; }
	FGameplayTag GetBatchDestroyTag() const { return bUseSeparateFeedbackForBatched ? OnBatchDestroy : OnDestroy; }
    
    /**
     * Merge with another tag set, using this as the base and other as overrides.
     * Non-empty tags in 'other' will override empty tags in this struct.
     * 
     * Usage: ComponentTags.MergeWith(DataAssetTags);
     * Result: Data asset tags override component defaults where specified
     */
    FISMFeedbackTags OverrideWith(const FISMFeedbackTags& Other) const
    {
        FISMFeedbackTags Merged = *this;
        
        if (Other.HasSpawnFeedback()) Merged.OnSpawn = Other.OnSpawn;
        if (Other.HasDestroyFeedback()) Merged.OnDestroy = Other.OnDestroy;
        if (Other.HasHideFeedback()) Merged.OnHide = Other.OnHide;
        if (Other.HasShowFeedback()) Merged.OnShow = Other.OnShow;
        if (Other.HasTransformUpdateFeedback()) Merged.OnTransformUpdate = Other.OnTransformUpdate;
        if (Other.HasBatchSpawnFeedback()) Merged.OnBatchSpawn = Other.OnBatchSpawn;
        if (Other.HasBatchDestroyFeedback()) Merged.OnBatchDestroy = Other.OnBatchDestroy;
        
        return Merged;
    }
    
    /** Create default feedback tags for common object types (helper for quick setup) */
    static FISMFeedbackTags CreateDefaultForObjectType(const FString& ObjectType)
    {
        FISMFeedbackTags Tags;
        
        // Create tags with standard hierarchy
        Tags.OnSpawn = FGameplayTag::RequestGameplayTag(*FString::Printf(TEXT("Feedback.ISM.Spawn.%s"), *ObjectType));
        Tags.OnDestroy = FGameplayTag::RequestGameplayTag(*FString::Printf(TEXT("Feedback.ISM.Destroy.%s"), *ObjectType));
        Tags.OnHide = FGameplayTag::RequestGameplayTag(*FString::Printf(TEXT("Feedback.ISM.Hide.%s"), *ObjectType));
        Tags.OnShow = FGameplayTag::RequestGameplayTag(*FString::Printf(TEXT("Feedback.ISM.Show.%s"), *ObjectType));
        
        return Tags;
    }
};
