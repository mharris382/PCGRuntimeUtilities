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
    
#pragma region CUSTOM_DATA
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
#pragma endregion




    // ===== PICD Conversion =====

#pragma region PICD_Conversion
  
    /**
    * Whether this instance type should attempt PICD-to-DMI conversion
    * when the instance is converted to a physics/gameplay actor.
    *
    * When true, the conversion system will:
    *   1. Resolve SchemaName against the global registry in UISMRuntimeDeveloperSettings
    *   2. Use the ISM's own material as the DMI template
    *   3. Build a pool signature from the instance's PICD values
    *   4. Get or create a pooled DMI and offer it to the converted actor
    *      via IISMCustomDataMaterialProvider (actor makes the final decision)
    *
    * When false, converted actors receive the raw ISM material with no DMI wrapping.
    * Default: true — opt-out rather than opt-in, since PICD conversion is the
    * common case for any project using per-instance custom data.
    */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|PICD Conversion",
        meta = (DisplayName = "Use PICD Conversion",
            ToolTip = "When enabled, per-instance custom data is translated to DMI \n parameters when this instance converts to a physics actor. \n The ISM's material is used directly as the DMI template."))
    bool bUsePICDConversion = true;

    /**
     * The PICD schema to use for DMI parameter mapping.
     *
     * Must match a key registered in UISMRuntimeDeveloperSettings::SchemaRegistry.
     * Leave empty (NAME_None) to use the project default schema.
     *
     * The schema defines which PICD indices map to which material parameter names.
     * Any material whose parameter names match the schema is automatically compatible —
     * no per-material registration needed.
     *
     * Editor: displayed as a dropdown populated from the project schema registry.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|PICD Conversion",
        meta = (EditCondition = "bUsePICDConversion",
            DisplayName = "Schema",
            ToolTip = "The PICD channel mapping schema. Leave empty to use the \n project default schema defined in ISM Runtime project settings.",
            GetOptions = "GetAvailableSchemaNames"))
    FName SchemaName = NAME_None;

    
    /**
 * Resolve the active schema for this asset.
 * Returns the explicitly set schema if SchemaName is set,
 * otherwise falls back to the project default.
 * Returns nullptr if bUsePICDConversion is false or no schema is resolvable.
 */
    const struct FISMCustomDataSchema* ResolveSchema() const;

    /**
     * Get the effective schema name (explicit or default).
     * Returns NAME_None if bUsePICDConversion is false or no default is configured.
     */
    FName GetEffectiveSchemaName() const;

#if WITH_EDITOR
    /**
     * Called by the GetOptions meta specifier to populate the SchemaName dropdown.
     * Returns all schema names registered in UISMRuntimeDeveloperSettings.
     */
    UFUNCTION()
    TArray<FName> GetAvailableSchemaNames() const;
#endif


#pragma endregion


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