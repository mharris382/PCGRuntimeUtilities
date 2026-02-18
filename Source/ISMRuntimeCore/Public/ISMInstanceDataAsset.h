#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Feedbacks/ISMFeedbackTags.h"
#include "ISMInstanceDataAsset.generated.h"



/**
 * Data asset defining properties and behavior for ISM instances.
 * Allows designers to configure instance behavior without code changes.
 */
UCLASS(BlueprintType)
class ISMRUNTIMECORE_API UISMInstanceDataAsset : public UDataAsset
{
    GENERATED_BODY()
    
public:
    // ===== Identification =====
    
    /** Display name for this instance type */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FText DisplayName;
    
    /** Description of this instance type */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta=(MultiLine=true))
    FText Description;
    
    // ===== Tags =====
    
    /** Default tags to apply to components using this data asset */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tags")
    FGameplayTagContainer DefaultTags;

    
     // ===== Feedback Configuration ===== (NEW SECTION)
    
    /**
     * Feedback tags for common ISM lifecycle events.
     * These override the component's default feedback tags.
     * Leave tags empty to use component defaults or disable feedback.
     * 
     * Example:
     * - OnSpawn = "Feedback.ISM.Spawn.Tree"
     * - OnDestroy = "Feedback.ISM.Destroy.Tree"
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags",
        meta=(ShowOnlyInnerProperties))
    FISMFeedbackTags FeedbackTags;
    
    
    // ===== Visual Settings =====
    
    /** Static mesh to use for instances */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    UStaticMesh* StaticMesh;


	UStaticMesh* GetStaticMesh() const { return StaticMesh; }


    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TArray<UMaterialInterface*> MaterialOverrides;

	TArray<UMaterialInterface*> GetMaterialOverrides() const { return MaterialOverrides; }
    
    /** Materials to override on instances (optional) */
   // UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
   // TArray<UMaterialInterface*> MaterialOverrides;
    
    // ===== Spatial Settings =====
    
    /** Recommended spatial index cell size for this instance type */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta=(ClampMin="100.0"))
    float RecommendedCellSize = 1000.0f;
    
    // ===== Custom Data =====
    
    /** Custom float parameters for module-specific use */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom")
    TMap<FName, float> CustomFloatParameters;
    
    /** Custom int parameters for module-specific use */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom")
    TMap<FName, int32> CustomIntParameters;
    
    /** Custom string parameters for module-specific use */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom")
    TMap<FName, FString> CustomStringParameters;
    
    // ===== Helper Functions =====
    
    UFUNCTION(BlueprintCallable, Category = "ISM Data")
    float GetFloatParameter(FName ParameterName, float DefaultValue = 0.0f) const
    {
        const float* Value = CustomFloatParameters.Find(ParameterName);
        return Value ? *Value : DefaultValue;
    }
    
    UFUNCTION(BlueprintCallable, Category = "ISM Data")
    int32 GetIntParameter(FName ParameterName, int32 DefaultValue = 0) const
    {
        const int32* Value = CustomIntParameters.Find(ParameterName);
        return Value ? *Value : DefaultValue;
    }
    
    UFUNCTION(BlueprintCallable, Category = "ISM Data")
    FString GetStringParameter(FName ParameterName, const FString& DefaultValue = "") const
    {
        const FString* Value = CustomStringParameters.Find(ParameterName);
        return Value ? *Value : DefaultValue;
    }

    // In UISMInstanceDataAsset

    /** Cached local-space bounds of the static mesh. Auto-populated when mesh is set. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bounds")
    FBox CachedLocalBounds;

    /** Padding added symmetrically to the cached bounds in all axes (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds", meta=(ClampMin="0.0"))
    float BoundsPadding = 0.0f;

    /** Additional per-axis padding for non-uniform adjustments */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds")
    FVector BoundsPaddingExtent = FVector::ZeroVector;

    /** Manually override the cached bounds instead of deriving from mesh */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds")
    bool bOverrideBounds = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds", meta=(EditCondition="bOverrideBounds"))
    FBox BoundsOverride;

    /** Get the effective local bounds (with padding applied) */
    FBox GetEffectiveLocalBounds() const;


    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debugging")
	FLinearColor DebugColor = FLinearColor::White;

    #if WITH_EDITOR

	UFUNCTION(BlueprintCallable, Category = "ISM Data", CallInEditor)
    void RefreshCachedBounds();
    /** Refresh cached bounds from the current StaticMesh. Called automatically when mesh changes. */
    
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

   
    #endif
};