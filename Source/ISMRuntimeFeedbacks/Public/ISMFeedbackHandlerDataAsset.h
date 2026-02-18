// ISMFeedbackHandlerDataAsset.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ISMFeedbackHandler.h"
#include "ISMFeedbackHandlerDataAsset.generated.h"

/**
 * Top-level tag-to-handler mapping entry.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMEFEEDBACKS_API FTagHandlerEntry
{
    GENERATED_BODY()
    
    /** Feedback tag */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
    FGameplayTag Tag;
    
    /**
     * Handler for this tag.
     * Can be:
     * - Leaf handler (Audio, Niagara, Decal) → Executes directly
     * - Composite handler (SurfaceMatcher, IntensityMatcher) → Derives tag, delegates to children
     */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Mapping")
    TObjectPtr<UISMFeedbackHandler> Handler;
};

/**
 * Feedback handler database.
 * Top-level map: FeedbackTag → Handler (leaf or composite)
 * 
 * Three Usage Patterns:
 * 
 * 1. ONE-TO-ONE (Simple):
 *    Tag: "Feedback.UI.LevelUp"
 *    Handler: Audio (leaf) → Plays level up sound
 * 
 * 2. ONE-TO-MANY (Multiple outputs):
 *    Tag: "Feedback.Impact.Heavy"
 *    Handler: CompositeHandler containing:
 *      - Audio (leaf)
 *      - Niagara (leaf)
 *      - CameraShake (leaf)
 *    All execute in sequence
 * 
 * 3. MANY-TO-MANY (Context-dependent):
 *    Tag: "Feedback.Movement.Footstep"
 *    Handler: SurfaceMatcherHandler (composite):
 *      HandlerDB:
 *        "Surface.Wood" → Audio+VFX (wood)
 *        "Surface.Metal" → Audio+VFX (metal)
 *      DefaultHandler: GenericAudio
 *    
 *    Flow:
 *      Context.SurfaceType = Wood
 *      → SurfaceMatcher derives "Surface.Wood"
 *      → Looks up HandlerDB["Surface.Wood"]
 *      → Executes wood audio + VFX
 * 
 * Example Database Setup:
 * 
 * HandlerDB:
 *   [0] Level Up:
 *       Tag: "Feedback.UI.LevelUp"
 *       Handler: Audio (S_LevelUp)
 *   
 *   [1] Heavy Impact:
 *       Tag: "Feedback.Impact.Heavy"
 *       Handler: MultiHandler
 *         Children:
 *           - Audio (S_Impact_Heavy)
 *           - Niagara (NS_Impact_Heavy)
 *           - CameraShake (Shake_Heavy)
 *   
 *   [2] Footstep (Surface-Dependent):
 *       Tag: "Feedback.Movement.Footstep"
 *       Handler: SurfaceMatcherHandler
 *         SurfaceToTagMap:
 *           SurfaceType1 → "Surface.Wood"
 *           SurfaceType2 → "Surface.Metal"
 *           SurfaceType3 → "Surface.Concrete"
 *         HandlerDB:
 *           "Surface.Wood" → MultiHandler
 *             Children:
 *               - Audio (S_Footstep_Wood)
 *               - Niagara (NS_Dust_Small)
 *           "Surface.Metal" → MultiHandler
 *             Children:
 *               - Audio (S_Footstep_Metal)
 *               - Niagara (NS_Spark_Small)
 *           "Surface.Concrete" → MultiHandler
 *             Children:
 *               - Audio (S_Footstep_Concrete)
 *               - Niagara (NS_Dust_Small)
 *         DefaultHandler: Audio (S_Footstep_Generic)
 */
UCLASS(BlueprintType)
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackHandlerDataAsset : public UDataAsset
{
    GENERATED_BODY()
    
public:
    /** Database name */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Database")
    FText DatabaseName;
    
    /**
     * Top-level tag to handler map.
     * Maps feedback tags to handlers (leaf or composite).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handlers",
        meta=(TitleProperty="Tag"))
    TArray<FTagHandlerEntry> HandlerDB;
    
    /**
     * Priority for this database.
     * Higher priority databases checked first.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Database")
    int32 Priority = 50;
    
    // ===== Lookup Methods =====
    
    /**
     * Find handler for a feedback tag.
     * Returns nullptr if not found.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    UISMFeedbackHandler* FindHandler(FGameplayTag FeedbackTag) const;
    
    /**
     * Execute feedback for a tag.
     * Finds handler, then executes it.
     * 
     * @return True if handler found and executed
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    bool ExecuteFeedback(FGameplayTag FeedbackTag, const FISMFeedbackContext& Context, UObject* WorldContext);
    
    /**
     * Preload all handler assets.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    void PreloadAllHandlers();
};

/**
 * Multi-handler: Executes multiple handlers in sequence.
 * Use for one-to-many pattern (impact = sound + VFX + camera shake).
 * 
 * This is also a composite, but doesn't derive tags - just executes all children.
 */
UCLASS(BlueprintType, meta=(DisplayName="Multi Handler"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackMultiHandler : public UISMFeedbackHandler
{
    GENERATED_BODY()
    
public:
    /**
     * Child handlers to execute in sequence.
     * All execute with same context.
     */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Handlers")
    TArray<TObjectPtr<UISMFeedbackHandler>> Handlers;
    
    virtual bool Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext) override;
    virtual void PreloadAssets_Implementation() override;
};
