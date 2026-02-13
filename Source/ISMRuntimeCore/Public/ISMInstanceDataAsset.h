#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
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
    
    // ===== Visual Settings =====
    
    /** Static mesh to use for instances */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    UStaticMesh* StaticMesh;
    
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
};