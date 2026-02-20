// ISMInstanceHandle.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Delegates/DelegateCombinations.h"
#include "ISMInstanceHandle.generated.h"

// Forward declarations
class AActor;
class UWorld;
class UISMRuntimeComponent;
class UISMCustomDataSubsystem;
struct FISMCustomDataSchema;
struct FISMConversionContext;

/**
 * Delegate called when a converted actor is being returned to ISM.
 * @param ConvertedActor - The actor being released
 * @param bShouldDestroy - Set to true to destroy, false to keep (e.g. for pooling)
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
 * Stable reference to an ISM instance that persists across its lifetime.
 * Tracks conversion to/from actors and manages the instance lifecycle.
 *
 * Custom data write-through:
 *   Use WriteCustomData / WriteCustomDataValue to update PICD values.
 *   These write to both the ISM PICD and CachedCustomData, and if the instance
 *   is currently a converted actor, immediately resolve and apply the correct
 *   pooled DMI so the actor's visual state stays in sync.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMInstanceHandle
{
    GENERATED_BODY()

    /** The instance index within the component. Stable — array is never compacted. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Runtime")
    int32 InstanceIndex = INDEX_NONE;

    /** Weak pointer to the component that owns this instance */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Runtime")
    TWeakObjectPtr<UISMRuntimeComponent> Component;

    /** If this instance has been converted to an actor, this holds the reference */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Runtime")
    TWeakObjectPtr<AActor> ConvertedActor;

    /**
     * Canonical custom data for this instance.
     * Always kept in sync with ISM PICD via WriteCustomData / WriteCustomDataValue.
     * Read by the DMI pool and conversion system — this is the source of truth
     * for converted actors since PICD on a hidden ISM instance can't be queried cheaply.
     */
    TArray<float> CachedCustomData;

    /** Transform cached at conversion time, used to restore ISM position on return. */
    FTransform CachedPreConversionTransform;

    FISMInstanceHandle() = default;

    // ===== State Queries =====

    /** True if handle points to a valid component and instance index */
    bool IsValid() const;

    /** True if this instance is currently represented as an actor */
    bool IsConvertedToActor() const;

    /** Get the converted actor, or nullptr if not converted */
    AActor* GetConvertedActor() const;

    /** Get current world location (from actor if converted, otherwise from ISM) */
    FVector GetLocation() const;

    /** Get current world transform (from actor if converted, otherwise from ISM) */
    FTransform GetTransform() const;

    /** Get all gameplay tags for this instance (component tags + per-instance tags) */
    FGameplayTagContainer GetInstanceTags() const;

    // ===== Conversion =====

    /**
     * Convert this instance to an actor via the component's IISMConvertible implementation.
     * Caches transform and custom data before conversion for later restoration.
     * No-op if already converted — returns the existing actor.
     */
    AActor* ConvertToActor(const FISMConversionContext& ConversionContext);

    /**
     * Return this instance from actor back to ISM representation.
     * Restores PICD from CachedCustomData (write-through keeps it current).
     * Releases the DMI ref count in the shared pool.
     * @param bDestroyActor      Whether to destroy the actor (false = caller manages lifetime)
     * @param bUpdateTransform   Whether to update ISM transform to actor's final position
     */
    bool ReturnToISM(bool bDestroyActor = true, bool bUpdateTransform = true);

    /**
     * Set the converted actor reference. Called by ConvertToActor after the actor
     * is successfully spawned. Fires OnInstanceConvertedToActor delegate.
     */
    void SetConvertedActor(AActor* Actor);

    /**
     * Clear the converted actor reference without going through ReturnToISM.
     * Use when the actor was destroyed externally (e.g. killed by damage system).
     */
    void ClearConvertedActor();

    // ===== Custom Data Write-Through =====

    /**
     * Write the full custom data array with write-through behavior.
     *
     * Always writes to:
     *   1. ISM PICD (source of truth, even if instance is hidden/converted)
     *   2. CachedCustomData (keeps handle in sync)
     *
     * If currently converted to an actor, also:
     *   3. Resolves the correct pooled DMI via UISMCustomDataSubsystem
     *   4. Applies it to the actor via IISMCustomDataMaterialProvider (or direct fallback)
     *
     * @param NewData   Full replacement custom data array
     * @param World     World context for subsystem access
     */
    void WriteCustomData(const TArray<float>& NewData, UWorld* World);

    /**
     * Write a single custom data channel with write-through behavior.
     * More efficient than WriteCustomData when only one value changes.
     * Early-outs if the value is unchanged (avoids pool lookup and material update).
     *
     * @param DataIndex     The channel index to update
     * @param Value         The new float value
     * @param World         World context for subsystem access
     */
    void WriteCustomDataValue(int32 DataIndex, float Value, UWorld* World);

    /**
     * Read-only access to the current canonical custom data.
     * Reads from CachedCustomData — does not touch the ISM component.
     */
    const TArray<float>& ReadCustomData() const { return CachedCustomData; }

    /**
     * Read a single custom data channel value.
     * Returns 0.f if DataIndex is out of range.
     */
    float ReadCustomDataValue(int32 DataIndex) const;

    /**
     * Force re-application of current custom data to the converted actor's materials.
     * Use if the pool was flushed or the actor's mesh component was replaced.
     *
     * @param World     World context for subsystem access
     * @return          True if materials were successfully re-applied
     */
    bool RefreshConvertedActorMaterials(UWorld* World);

    // ===== Operators =====

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

private:
    /**
     * Internal: resolve and apply DMIs to a converted actor from CachedCustomData.
     * Called by WriteCustomData, WriteCustomDataValue, and RefreshConvertedActorMaterials.
     * No-op if not currently converted.
     */
    void ApplyCustomDataMaterialsToActor(AActor* Actor, UWorld* World) const;
};

/** Alias for cleaner code at call sites that treat handles as stable references */
typedef FISMInstanceHandle FISMInstanceReference;