#pragma once

#include "CoreMinimal.h"
#include "ISMInstanceDataAsset.h"
#include "GameplayTagContainer.h"
#include "ISMResourceDataAsset.generated.h"

/**
 * Data asset for collectible/harvestable resources.
 * Extends base ISMInstanceDataAsset with resource-specific configuration.
 *
 * Use this to define different resource types (trees, rocks, plants, etc.)
 * without writing C++ code.
 */
UCLASS(BlueprintType)
class ISMRUNTIMERESOURCE_API UISMResourceDataAsset : public UISMInstanceDataAsset
{
    GENERATED_BODY()

public:
    // ===== Resource Identity =====

    /** Tags describing what type of resource this is */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Tags")
    FGameplayTagContainer ResourceTags;

    // ===== Collection Requirements =====

    /** What the collector must have to gather this resource */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Requirements")
    FGameplayTagQuery CollectorRequirements;

    /** Message shown when requirements aren't met */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Requirements")
    FText RequirementsFailureMessage;

    // ===== Collection Settings =====

    /** Base time to collect this resource (0 = instant) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Collection", meta = (ClampMin = "0.0"))
    float BaseCollectionTime = 2.0f;

    /** Can collection be interrupted? */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Collection")
    bool bCanInterruptCollection = true;

    /** Number of stages for collection (1 = single stage, >1 = multi-stage like tree chopping) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Collection", meta = (ClampMin = "1"))
    int32 CollectionStages = 1;

    /** Divide time across stages, or each stage takes full time? */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Collection", meta = (EditCondition = "CollectionStages > 1"))
    bool bDivideTimeAcrossStages = false;

    // ===== Tag-Based Modifiers =====

    /** Speed multipliers based on collector tags (multiplicative) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Modifiers", meta = (
        ToolTip = "Map of GameplayTag to speed multiplier. Example: Tool.IronAxe = 1.5 (50% faster)"))
    TMap<FGameplayTag, float> CollectorSpeedModifiers;

    /** Yield multipliers based on collector tags (multiplicative) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Modifiers", meta = (
        ToolTip = "Map of GameplayTag to yield multiplier. Example: Skill.Harvesting.Level5 = 2.0 (double resources)"))
    TMap<FGameplayTag, float> CollectorYieldModifiers;

    // ===== Visual/Audio Feedback =====

    /** VFX to spawn when collection completes */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Feedback")
    class UNiagaraSystem* CollectionVFX;

    /** Sound to play when collection completes */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Feedback")
    class USoundBase* CollectionSound;

    /** VFX to spawn per collection stage (for multi-stage resources) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Feedback", meta = (EditCondition = "CollectionStages > 1"))
    class UNiagaraSystem* StageVFX;

    /** Sound to play per collection stage */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource|Feedback", meta = (EditCondition = "CollectionStages > 1"))
    class USoundBase* StageSound;

    // ===== Helper Functions =====

    /** Get speed multiplier for specific collector tags */
    UFUNCTION(BlueprintPure, Category = "Resource")
    float GetSpeedMultiplier(const FGameplayTagContainer& CollectorTags) const;

    /** Get yield multiplier for specific collector tags */
    UFUNCTION(BlueprintPure, Category = "Resource")
    float GetYieldMultiplier(const FGameplayTagContainer& CollectorTags) const;
};