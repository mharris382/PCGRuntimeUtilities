#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISMConvertible.generated.h"

UENUM(BlueprintType)
enum class EISMConversionReason : uint8
{
    None,
    Physics,        // Convert due to physics impact
    Destruction,    // Convert due to destruction
    Interaction,    // Convert due to player interaction
    Gameplay        // Convert for gameplay reasons
};

USTRUCT(BlueprintType)
struct FISMConversionContext
{
    GENERATED_BODY()
    
    UPROPERTY(BlueprintReadWrite, Category = "Conversion")
    EISMConversionReason Reason = EISMConversionReason::None;
    
    UPROPERTY(BlueprintReadWrite, Category = "Conversion")
    FVector ImpactPoint = FVector::ZeroVector;
    
    UPROPERTY(BlueprintReadWrite, Category = "Conversion")
    FVector ImpactNormal = FVector::UpVector;
    
    UPROPERTY(BlueprintReadWrite, Category = "Conversion")
    float ImpactForce = 0.0f;
    
    UPROPERTY(BlueprintReadWrite, Category = "Conversion")
    AActor* Instigator = nullptr;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UISMConvertible : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for ISM instances that can be converted to other representations (e.g., physics actors)
 */
class ISMRUNTIMECORE_API IISMConvertible
{
    GENERATED_BODY()
    
public:
    /** Should this instance be converted based on the given context? */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Conversion")
    bool ShouldConvert(int32 InstanceIndex, const FISMConversionContext& Context) const;
    virtual bool ShouldConvert_Implementation(int32 InstanceIndex, const FISMConversionContext& Context) const { return false; }
    
    /** Convert this instance to another representation (e.g., physics actor) */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Conversion")
    AActor* ConvertInstance(int32 InstanceIndex, const FISMConversionContext& Context);
    virtual AActor* ConvertInstance_Implementation(int32 InstanceIndex, const FISMConversionContext& Context) { return nullptr; }
    
    /** Called after successful conversion */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Conversion")
    void OnInstanceConverted(int32 InstanceIndex, AActor* ConvertedActor);
    virtual void OnInstanceConverted_Implementation(int32 InstanceIndex, AActor* ConvertedActor) {}
};