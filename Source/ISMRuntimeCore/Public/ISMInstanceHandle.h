// ISMInstanceHandle.h
#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "ISMInstanceHandle.generated.h"

// Forward declarations
class AActor;
class UISMRuntimeComponent;

/**
 * Delegate called when a converted actor is being returned to ISM.
 * Allows custom cleanup/pooling logic.
 * @param ConvertedActor - The actor being released
 * @param bShouldDestroy - Set to true to destroy the actor, false to keep it (e.g., for pooling)
 */
DECLARE_DELEGATE_TwoParams(FOnReleaseConvertedActor, AActor* /*ConvertedActor*/, bool& /*bShouldDestroy*/);

/**
 * Delegate called when an instance is converted to an actor.
 * @param InstanceHandle - The handle that was converted
 * @param ConvertedActor - The newly created actor
 */
DECLARE_DELEGATE_TwoParams(FOnInstanceConverted, const struct FISMInstanceHandle& /*InstanceHandle*/, AActor* /*ConvertedActor*/);

/**
 * Delegate called when an instance is returned from actor to ISM.
 * @param InstanceHandle - The handle that was returned
 * @param FinalTransform - The final transform from the actor
 */
DECLARE_DELEGATE_TwoParams(FOnInstanceReturnedToISM, const struct FISMInstanceHandle& /*InstanceHandle*/, const FTransform& /*FinalTransform*/);

/**
 * Stable reference to an ISM instance that persists across the instance's lifetime.
 * Tracks conversion to/from actors and manages the lifecycle.
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

    /** If this instance has been converted to an actor, this holds the reference */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Runtime")
    TWeakObjectPtr<AActor> ConvertedActor;

    /** Cached custom data from when instance was converted (for restoration) */
    TArray<float> CachedCustomData;

    /** Cached transform from when instance was converted */
    FTransform CachedPreConversionTransform;

    FISMInstanceHandle() = default;

    /** Check if this handle is valid and the instance exists */
    bool IsValid() const;

    /** Check if this instance is currently converted to an actor */
    //UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    bool IsConvertedToActor() const;

    /** Get the converted actor (nullptr if not converted) */
    //UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    AActor* GetConvertedActor() const;

    /** Get the current world location (from ISM or converted actor) */
    FVector GetLocation() const;

    /** Get the current transform (from ISM or converted actor) */
    FTransform GetTransform() const;

    /**
     * Convert this instance to an actor.
     * @param ConversionContext - Context describing why/how to convert
     * @return The converted actor, or nullptr if conversion failed
     */
    AActor* ConvertToActor(const struct FISMConversionContext& ConversionContext);

    /**
     * Return this instance from actor back to ISM representation.
     * @param bDestroyActor - Whether to destroy the actor (false to keep for pooling)
     * @param bUpdateTransform - Whether to update ISM transform to match actor's final position
     * @return True if successfully returned to ISM
     */
    bool ReturnToISM(bool bDestroyActor = true, bool bUpdateTransform = true);

    /**
     * Set the converted actor reference (called by conversion system).
     * Caches instance data for later restoration.
     */
    void SetConvertedActor(AActor* Actor);

    /**
     * Clear the converted actor reference without returning to ISM.
     * Useful when the actor is destroyed externally.
     */
    void ClearConvertedActor();


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

/** Alias for cleaner code */
typedef FISMInstanceHandle FISMInstanceReference;