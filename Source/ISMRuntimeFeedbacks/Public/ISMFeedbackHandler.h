// ISMFeedbackHandler.h (COMPOSITE PATTERN)
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "Feedbacks/ISMFeedbackContext.h"
#include "ISMFeedbackHandler.generated.h"

/**
 * Base feedback handler interface.
 * Both leaf handlers (audio, VFX) and composite handlers (matchers) implement this.
 * 
 * Design Pattern: Composite
 * - Leaf Handlers: Execute feedback directly (play sound, spawn VFX)
 * - Composite Handlers: Derive new tag from context, delegate to child handlers
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackHandler : public UObject
{
    GENERATED_BODY()
    
public:
    /**
     * Execute feedback with the given context.
     * 
     * For Leaf Handlers: Execute directly (play sound, spawn VFX)
     * For Composite Handlers: Derive tag, lookup child, delegate
     * 
     * @param Context - Feedback context
     * @param WorldContext - World for spawning
     * @return True if handled
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Feedback")
    bool Execute(const FISMFeedbackContext& Context, UObject* WorldContext);
    virtual bool Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext) { return false; }
    
    /** Optional: Preload assets */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Feedback")
    void PreloadAssets();
    virtual void PreloadAssets_Implementation() {}


   // UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Feedback")
   // void InitializeHandler();
   // virtual void InitializeHandler_Implementation() {}
};

// ===== Leaf Handlers (Direct Execution) =====

/**
 * Leaf handler: Plays sound at location.
 * No children - just executes.
 */
UCLASS(BlueprintType, meta=(DisplayName="Audio Handler"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackHandler_Audio : public UISMFeedbackHandler
{
    GENERATED_BODY()
    
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TSoftObjectPtr<USoundBase> Sound;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float VolumeMultiplier = 1.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    float PitchMultiplier = 1.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bScaleVolumeByIntensity = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    USoundAttenuation* AttenuationSettings = nullptr;
    
    virtual bool Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext) override;
    virtual void PreloadAssets_Implementation() override;
};

/**
 * Leaf handler: Spawns Niagara system.
 * No children - just executes.
 */
UCLASS(BlueprintType, meta=(DisplayName="Niagara Handler"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackHandler_Niagara : public UISMFeedbackHandler
{
    GENERATED_BODY()
    
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
    TSoftObjectPtr<UNiagaraSystem> System;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
    bool bOrientToNormal = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
    float ScaleMultiplier = 1.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
    bool bPassContextAsParameters = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
    bool bAutoDestroy = true;
        
    virtual bool Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext) override;
    virtual void PreloadAssets_Implementation() override;
};

/**
 * Leaf handler: Spawns decal.
 */
UCLASS(BlueprintType, meta=(DisplayName="Decal Handler"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackHandler_Decal : public UISMFeedbackHandler
{
    GENERATED_BODY()
    
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
    UMaterialInterface* DecalMaterial = nullptr;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
    float Duration = 10.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
    bool bScaleByIntensity = true;
    
    virtual bool Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext) override;
};

/**
 * Leaf handler: Debug visualization.
 * Perfect for testing without assets.
 */
UCLASS(BlueprintType, meta=(DisplayName="Debug Handler"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackHandler_Debug : public UISMFeedbackHandler
{
    GENERATED_BODY()
    
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bLogToConsole = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawDebugSphere = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    float DrawDuration = 2.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawDebugArrow = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FColor DebugColor = FColor::Green;

    
    
    virtual bool Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext) override;
};



// ===== Composite Handlers (Tag Derivation + Delegation) =====

/**
 * Tag-to-handler mapping entry.
 * Used in composite handlers for child handler lookup.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMEFEEDBACKS_API FTagHandlerMatchEntry
{
    GENERATED_BODY()
    
    /** Derived tag (e.g., "Surface.Wood", "Intensity.Heavy") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
    FGameplayTag Tag;
    
    /** Handler to execute if this tag matches */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Mapping")
    TObjectPtr<UISMFeedbackHandler> Handler;
};

/**
 * Abstract composite handler base class.
 * 
 * Pattern:
 * 1. Derive a tag from context (implemented by subclasses)
 * 2. Lookup child handler by derived tag
 * 3. Delegate to child handler OR fallback to default
 * 
 * Example Flow:
 *   Context.PhysicalMaterial.SurfaceType = Wood
 *   → DeriveTag() returns "Surface.Wood"
 *   → Lookup HandlerDB["Surface.Wood"]
 *   → Found: WoodFootstepHandler
 *   → Delegate: WoodFootstepHandler->Execute(Context)
 */
UCLASS(Abstract, BlueprintType, meta=(DisplayName="Matcher Handler"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackMatcherHandler : public UISMFeedbackHandler
{
    GENERATED_BODY()
    
public:
    /**
     * Map of derived tag → child handler.
     * 
     * Example (Surface Matcher):
     *   "Surface.Wood" → WoodFootstepHandler
     *   "Surface.Metal" → MetalFootstepHandler
     *   "Surface.Concrete" → ConcreteFootstepHandler
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Matcher",
        meta=(TitleProperty="Tag"))
    TArray<FTagHandlerMatchEntry> HandlerDB;
    
    /**
     * Default handler if no match found.
     * Always executes if derived tag not in HandlerDB.
     */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Matcher")
    TObjectPtr<UISMFeedbackHandler> DefaultHandler = nullptr;
    
    /**
     * Derive a tag from context.
     * Implemented by subclasses (surface type, intensity, etc.)
     * 
     * @param Context - Feedback context
     * @return Derived tag (e.g., "Surface.Wood")
     */
    UFUNCTION(BlueprintNativeEvent, Category = "ISM Feedback")
    FGameplayTag DeriveTag(const FISMFeedbackContext& Context) const;
    virtual FGameplayTag DeriveTag_Implementation(const FISMFeedbackContext& Context) const { return FGameplayTag(); }
    
    /**
     * Execute: Derive tag → Lookup → Delegate
     */
    virtual bool Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext) override;
    
    virtual void PreloadAssets_Implementation() override;
    
protected:
    /** Find handler for derived tag */
    UISMFeedbackHandler* FindHandlerForTag(FGameplayTag DerivedTag) const;
};

/**
 * Concrete matcher: Surface type → Tag → Handler
 * 
 * Derives tag from Context.PhysicalMaterial.SurfaceType.
 * 
 * Example Configuration:
 *   SurfaceToTagMap:
 *     SurfaceType1 (Wood) → "Surface.Wood"
 *     SurfaceType2 (Metal) → "Surface.Metal"
 *     SurfaceType3 (Concrete) → "Surface.Concrete"
 *   
 *   HandlerDB:
 *     "Surface.Wood" → [WoodFootstepAudio, WoodDustVFX]
 *     "Surface.Metal" → [MetalFootstepAudio, MetalSparkVFX]
 *     "Surface.Concrete" → [ConcreteFootstepAudio, ConcreteDustVFX]
 *   
 *   DefaultHandler: GenericFootstepAudio
 * 
 * Flow:
 *   Context.PhysicalMaterial.SurfaceType = SurfaceType1 (Wood)
 *   → DeriveTag() → Lookup SurfaceToTagMap[SurfaceType1] → "Surface.Wood"
 *   → FindHandlerForTag("Surface.Wood") → WoodFootstepAudio + WoodDustVFX
 *   → Execute both
 */
UCLASS(BlueprintType, meta=(DisplayName="Surface Matcher Handler"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackSurfaceMatcherHandler : public UISMFeedbackMatcherHandler
{
    GENERATED_BODY()
    
public:
    /**
     * Map of ESurfaceType → GameplayTag.
     * 
     * Example:
     *   SurfaceType1 → "Surface.Wood"
     *   SurfaceType2 → "Surface.Metal"
     *   SurfaceType3 → "Surface.Concrete"
     * 
     * This avoids repeating surface type definitions.
     * Just map enum → tag once.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
    TMap<TEnumAsByte<EPhysicalSurface>, FGameplayTag> SurfaceToTagMap;
    
    /**
     * Derive tag from surface type.
     * Looks up Context.PhysicalMaterial.SurfaceType in SurfaceToTagMap.
     */
    virtual FGameplayTag DeriveTag_Implementation(const FISMFeedbackContext& Context) const override;
};

/**
 * Concrete matcher: Intensity threshold → Tag → Handler
 * 
 * Example Configuration:
 *   IntensityToTagMap:
 *     0.0-0.3 → "Intensity.Light"
 *     0.3-0.7 → "Intensity.Medium"
 *     0.7-1.0 → "Intensity.Heavy"
 *   
 *   HandlerDB:
 *     "Intensity.Light" → LightImpactHandler
 *     "Intensity.Medium" → MediumImpactHandler
 *     "Intensity.Heavy" → HeavyImpactHandler
 */
UCLASS(BlueprintType, meta=(DisplayName="Intensity Matcher Handler"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackIntensityMatcherHandler : public UISMFeedbackMatcherHandler
{
    GENERATED_BODY()
    
public:
    /** Intensity ranges → Tags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intensity")
    TMap<float, FGameplayTag> IntensityThresholds; // Key = min threshold, Value = tag
    
    virtual FGameplayTag DeriveTag_Implementation(const FISMFeedbackContext& Context) const override;
};