#pragma once

#include "CoreMinimal.h"
#include "ISMRuntimeComponent.h"
#include "ISMInstanceHandle.generated.h"

/**
 * Stable reference to an ISM instance that persists across the instance's lifetime.
 * Uses instance index directly for MVP (indices are stable because we hide instead of remove).
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMInstanceHandle
{
    GENERATED_BODY()
    
    /** The instance index within the component. Stable because we never compact the array. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Runtime")
    int32 InstanceIndex = INDEX_NONE;
    
    /** Weak pointer to the component that owns this instance */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Runtime")
    TWeakObjectPtr<UISMRuntimeComponent> Component;
    
    FISMInstanceHandle() = default;
    
    FISMInstanceHandle(UISMRuntimeComponent* InComponent, int32 InIndex)
        : InstanceIndex(InIndex), Component(InComponent)
    {
    }
    
    bool IsValid() const
    {
        return Component.IsValid() && InstanceIndex != INDEX_NONE;
    }
    
    /** Get the current world location of this instance */
    FVector GetLocation() const;
    
    /** Get the current transform of this instance */
    FTransform GetTransform() const;
    
    bool operator==(const FISMInstanceHandle& Other) const
    {
        return Component == Other.Component && InstanceIndex == Other.InstanceIndex;
    }
    
    bool operator!=(const FISMInstanceHandle& Other) const
    {
        return !(*this == Other);
    }
    
    friend uint32 GetTypeHash(const FISMInstanceHandle& Handle)
    {
        return HashCombine(GetTypeHash(Handle.Component), Handle.InstanceIndex);
    }
};

/** Alias for cleaner code - FISMInstanceReference and FISMInstanceHandle are the same for MVP */
typedef FISMInstanceHandle FISMInstanceReference;