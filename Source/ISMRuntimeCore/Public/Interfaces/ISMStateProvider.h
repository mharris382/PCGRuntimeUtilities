#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISMInstanceState.h"
#include "ISMStateProvider.generated.h"

UINTERFACE(MinimalAPI)
class UISMStateProvider : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for components that provide instance state information
 */
class ISMRUNTIMECORE_API IISMStateProvider
{
    GENERATED_BODY()
    
public:
    /** Get the current state flags for an instance */
    virtual uint8 GetInstanceStateFlags(int32 InstanceIndex) const = 0;
    
    /** Check if instance has a specific state flag */
    virtual bool IsInstanceInState(int32 InstanceIndex, EISMInstanceState State) const = 0;
    
    /** Set a state flag value for an instance */
    virtual void SetInstanceState(int32 InstanceIndex, EISMInstanceState State, bool bValue) = 0;
    
    /** Get the full state struct for an instance */
    virtual const FISMInstanceState* GetInstanceState(int32 InstanceIndex) const = 0;
};