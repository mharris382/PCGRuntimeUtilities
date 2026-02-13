#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISMInteractable.generated.h"

UENUM(BlueprintType)
enum class EISMInteractionType : uint8
{
    None,
    Gather,     // Harvesting resources
    Use,        // Generic use interaction
    Open,       // Opening containers/doors
    Activate,   // Activating switches/mechanisms
    Custom      // Game-specific interaction
};

UINTERFACE(MinimalAPI, BlueprintType)
class UISMInteractable : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for ISM instances that can be interacted with
 */
class ISMRUNTIMECORE_API IISMInteractable
{
    GENERATED_BODY()
    
public:
    /** Check if this instance can be interacted with by the given actor */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Interaction")
    bool CanInteract(int32 InstanceIndex, AActor* Instigator) const;
    virtual bool CanInteract_Implementation(int32 InstanceIndex, AActor* Instigator) const { return true; }
    
    /** Perform the interaction */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Interaction")
    void OnInteract(int32 InstanceIndex, AActor* Instigator);
    virtual void OnInteract_Implementation(int32 InstanceIndex, AActor* Instigator) {}
    
    /** Get text to display for this interaction (e.g., "Press E to harvest") */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Interaction")
    FText GetInteractionText(int32 InstanceIndex) const;
    virtual FText GetInteractionText_Implementation(int32 InstanceIndex) const { return FText::GetEmpty(); }
    
    /** Get the type of interaction this instance supports */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Interaction")
    EISMInteractionType GetInteractionType(int32 InstanceIndex) const;
    virtual EISMInteractionType GetInteractionType_Implementation(int32 InstanceIndex) const { return EISMInteractionType::Use; }
};