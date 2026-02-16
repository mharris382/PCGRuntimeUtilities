#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISMPoolable.generated.h"

// Forward declarations
class UISMPoolDataAsset;
struct FISMInstanceHandle;

UINTERFACE(MinimalAPI, BlueprintType)
class UISMPoolable : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for actors that can be pooled and reused.
 *
 * Actors implementing this interface can be spawned into a pool, requested for use,
 * and returned to the pool for reuse. This eliminates allocation overhead.
 *
 * The interface provides lifecycle hooks with context data (data asset, instance handle)
 * to enable data-driven configuration without requiring actor subclasses.
 */
class ISMRUNTIMEPOOLS_API IISMPoolable
{
    GENERATED_BODY()

public:

    /**
     * Called when this actor is first spawned into the pool during pre-warm.
     * Use this for one-time initialization that doesn't need to be repeated on each request.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Poolable")
    void OnPoolSpawned();
    virtual void OnPoolSpawned_Implementation() {}

    /**
     * Called when this actor is requested from the pool for use.
     *
     * @param DataAsset - The data asset containing configuration for this instance
     * @param InstanceHandle - Handle to the ISM instance being converted (may be invalid if not from conversion)
     *
     * Use this to configure the actor from the data asset (e.g., set physics properties,
     * materials, collision settings). This allows one pooled actor class to handle
     * many different configurations.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Poolable")
    void OnRequestedFromPool(UISMPoolDataAsset* DataAsset, const FISMInstanceHandle& InstanceHandle);
    virtual void OnRequestedFromPool_Implementation(UISMPoolDataAsset* DataAsset, const FISMInstanceHandle& InstanceHandle) {}

    /**
     * Called when this actor is being returned to the pool.
     *
     * @param OutFinalTransform - Set this to the actor's final transform if it should update the ISM instance
     * @param bUpdateInstanceTransform - Set to true if the ISM instance should be updated with final transform
     *
     * Use this to reset the actor to a clean state (stop physics, clear velocities,
     * hide visual components, clear any runtime state). You can also return final
     * transform data to update the ISM instance.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Poolable")
    void OnReturnedToPool(FTransform& OutFinalTransform, bool& bUpdateInstanceTransform);
    virtual void OnReturnedToPool_Implementation(FTransform& OutFinalTransform, bool& bUpdateInstanceTransform)
    {
        OutFinalTransform = FTransform::Identity;
        bUpdateInstanceTransform = false;
    }

    /**
     * Called when the pool is being destroyed (usually on world cleanup).
     * Use this for final cleanup of any persistent resources.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Poolable")
    void OnPoolDestroyed();
    virtual void OnPoolDestroyed_Implementation() {}
};