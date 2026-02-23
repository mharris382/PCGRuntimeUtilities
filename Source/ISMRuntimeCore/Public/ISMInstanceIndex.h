// ISMInstanceIndex.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ISMRuntimeComponent.h"
#include "ISMInstanceHandle.h"
#include "ISMInstanceIndex.generated.h"



/**
 * Query descriptor for multi-index intersection.
 * Pairs an index with the tag key to query within it.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMIndexQuery
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "ISM Index")
    UISMInstanceIndex* Index = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "ISM Index")
    FGameplayTag Key;

    FISMIndexQuery() = default;
    FISMIndexQuery(UISMInstanceIndex* InIndex, FGameplayTag InKey)
        : Index(InIndex), Key(InKey) {}
};

/**
 * Opt-in secondary index over ISM instance handles, keyed by gameplay tag.
 *
 * Maintains a TMap<FGameplayTag, TSet<FISMInstanceHandle>> that is kept in sync
 * with one or more UISMRuntimeComponents via their existing delegates.
 * Zero impact on components that don't register an index.
 *
 * Subclasses determine what gets indexed and under which key:
 *   UISMOwnershipIndex  — keys by owner tag
 *   UISMPossessionIndex — keys by possessor tag
 *   UISMStateIndex      — keys by instance state flags mapped to tags
 *   UISMTagIndex        — keys by per-instance gameplay tags
 *
 * Designed for fast set-based queries and intersection with spatial results.
 */
UCLASS(Abstract, Blueprintable, ClassGroup=(ISMRuntime),
    meta=(BlueprintSpawnableComponent))
class ISMRUNTIMECORE_API UISMInstanceIndex : public UActorComponent
{
    GENERATED_BODY()

public:
    UISMInstanceIndex();

    // ===== Registration =====

    /**
     * Subscribe this index to a runtime component.
     * Immediately builds the index from existing instances,
     * then keeps it in sync via delegates.
     * Safe to call on multiple components — indexes are merged.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    void RegisterWithComponent(UISMRuntimeComponent* Component);

    /** Unsubscribe from a component and remove its handles from the index */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    void UnregisterFromComponent(UISMRuntimeComponent* Component);

    /** Unsubscribe from all components and clear the index */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    void UnregisterAll();

    // ===== Single-Index Queries =====

    /** Get all handles filed under a specific tag key. O(1) lookup, O(n) copy. */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    TArray<FISMInstanceHandle> GetHandlesForTag(FGameplayTag Key) const;

    /**
     * Get handles present under ALL of the given keys.
     * Intersects sets starting from the smallest — O(min set size).
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    TArray<FISMInstanceHandle> GetHandlesForAllTags(
        const FGameplayTagContainer& Keys) const;

    /**
     * Get handles present under ANY of the given keys.
     * Union of sets — O(sum of set sizes).
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    TArray<FISMInstanceHandle> GetHandlesForAnyTag(
        const FGameplayTagContainer& Keys) const;

    /** O(1) membership test */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    bool IsHandleIndexed(FGameplayTag Key, const FISMInstanceHandle& Handle) const;

    /** How many handles are filed under this key */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    int32 GetCountForTag(FGameplayTag Key) const;

    /** All keys that currently have at least one handle */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    TArray<FGameplayTag> GetActiveKeys() const;

    /** Total handles across all keys (handles filed under multiple keys counted once per key) */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    int32 GetTotalIndexedCount() const;

    // ===== Spatial Intersection =====

    /**
     * Filter a set of spatial candidates through this index's key.
     * Use after GetInstancesInRadius / GetInstancesInBox to apply logical filtering.
     * O(k) where k = spatial candidate count.
     *
     * @param Key               The logical key to filter by
     * @param SpatialCandidates Instance indices from a spatial query
     * @param Component         The component those indices belong to
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    TArray<FISMInstanceHandle> IntersectWithSpatial(
        FGameplayTag Key,
        const TArray<int32>& SpatialCandidates,
        UISMRuntimeComponent* Component) const;

    // ===== Multi-Index Intersection (Static) =====

    /**
     * Intersect two indexes at given keys.
     * Iterates the smaller set, probes the larger. O(min(|A|, |B|)).
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Index",
        meta=(DefaultToSelf="IndexA"))
    static TArray<FISMInstanceHandle> Intersect(
        const UISMInstanceIndex* IndexA, FGameplayTag KeyA,
        const UISMInstanceIndex* IndexB, FGameplayTag KeyB);

    /**
     * Intersect N indexes, each at a specified key.
     * Automatically sorts by ascending set size before iterating
     * so the most selective filter always runs first.
     * O(min set size) probes against all other sets.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    static TArray<FISMInstanceHandle> IntersectAll(
        const TArray<FISMIndexQuery>& Queries);

    /**
     * Intersect spatial candidates with multiple logical indexes.
     * Spatial result is treated as the first (potentially large) candidate set.
     * Each logical index filters it down further. O(k * n_indexes).
     */
    static TArray<FISMInstanceHandle> IntersectSpatialWithIndexes(
        const TArray<int32>& SpatialCandidates,
        UISMRuntimeComponent* Component,
        const TArray<FISMIndexQuery>& Queries);

    // ===== Maintenance =====

    /** Force full rebuild from all registered components */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    void RebuildIndex();

    /** Remove any handles whose component or instance is no longer valid */
    UFUNCTION(BlueprintCallable, Category = "ISM Index")
    void PruneStaleHandles();

protected:
    // ===== Subclass Interface =====

    /**
     * Called when a handle should be considered for indexing.
     * Subclass determines which key(s) to file it under via AddToKey.
     */
    virtual void OnHandleChanged(const FISMInstanceHandle& Handle) {}

    /**
     * Called when a handle must be removed from the index entirely.
     * Default calls RemoveFromAllKeys — override for targeted removal.
     */
    virtual void OnHandleRemoved(const FISMInstanceHandle& Handle)
    {
        RemoveFromAllKeys(Handle);
    }

    /** File a handle under a key. Creates the key set if needed. */
    void AddToKey(FGameplayTag Key, const FISMInstanceHandle& Handle);

    /** Remove a handle from a specific key. Removes the key if empty. */
    void RemoveFromKey(FGameplayTag Key, const FISMInstanceHandle& Handle);

    /** Remove a handle from every key it appears in. */
    void RemoveFromAllKeys(const FISMInstanceHandle& Handle);

    /** Get the const TSet for a key, or nullptr if key doesn't exist */
    const TSet<FISMInstanceHandle>* GetSetForKey(FGameplayTag Key) const;

private:
    // ===== Index Storage =====

    /** Primary index: tag key → set of handles */
    TMap<FGameplayTag, TSet<FISMInstanceHandle>> Index;

    /**
     * Reverse map: handle → set of keys it's filed under.
     * Makes RemoveFromAllKeys O(keys per handle) instead of O(total keys).
     */
    TMap<FISMInstanceHandle, TSet<FGameplayTag>> HandleToKeys;

    // ===== Subscription Tracking =====

    UPROPERTY()
    TArray<TWeakObjectPtr<UISMRuntimeComponent>> RegisteredComponents;

    struct FComponentDelegateHandles
    {
        FDelegateHandle StateChanged;
        FDelegateHandle Destroyed;
        FDelegateHandle TagChanged;
        FDelegateHandle OwnershipChanged;
        FDelegateHandle PossessionChanged;
        FDelegateHandle AttachmentChanged;
    };
    TMap<UISMRuntimeComponent*, FComponentDelegateHandles> DelegateHandles;

    // ===== Delegate Callbacks =====

    void HandleStateChanged(UISMRuntimeComponent* Component, int32 InstanceIndex);
    void HandleDestroyed(UISMRuntimeComponent* Component, int32 InstanceIndex);
    void HandleTagChanged(UISMRuntimeComponent* Component, int32 InstanceIndex);
    void HandleOwnershipChanged(UISMRuntimeComponent* Component, int32 InstanceIndex);
    void HandlePossessionChanged(UISMRuntimeComponent* Component, int32 InstanceIndex);
    void HandleAttachmentChanged(UISMRuntimeComponent* Component, int32 InstanceIndex);

    /** Build index entries for a single handle — calls OnHandleChanged */
    void IndexHandle(UISMRuntimeComponent* Component, int32 InstanceIndex);
};


// ================================================================
//  Concrete Index Subclasses
// ================================================================

/**
 * Keys handles by their owner tag.
 * Query: "what instances does Faction.PlayerTeam.Alpha own?"
 * Syncs from: BroadcastOwnershipChange
 */
UCLASS(BlueprintType, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMECORE_API UISMOwnershipIndex : public UISMInstanceIndex
{
    GENERATED_BODY()
protected:
    virtual void OnHandleChanged(const FISMInstanceHandle& Handle) override
    {
        RemoveFromAllKeys(Handle);
        if (Handle.IsOwned())
        {
            AddToKey(Handle.GetOwnerTag(), Handle);
        }
    }
};

/**
 * Keys handles by their possessor tag.
 * Query: "what is Player.Hand.Left currently holding?"
 * Syncs from: BroadcastPossessionChange
 */
UCLASS(BlueprintType, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMECORE_API UISMPossessionIndex : public UISMInstanceIndex
{
    GENERATED_BODY()
protected:
    virtual void OnHandleChanged(const FISMInstanceHandle& Handle) override
    {
        RemoveFromAllKeys(Handle);
        if (Handle.IsPossessed())
        {
            AddToKey(Handle.GetPossessorTag(), Handle);
        }
    }
};

/**
 * Keys handles by EISMInstanceState flags, mapped to gameplay tags.
 * Query: "what instances are currently destroyed / hidden / converting?"
 *
 * Default tag mapping:
 *   Intact     → ISM.State.Intact
 *   Damaged    → ISM.State.Damaged
 *   Destroyed  → ISM.State.Destroyed
 *   Hidden     → ISM.State.Hidden
 *   Converting → ISM.State.Converting
 *
 * Override StateTagMap to customize.
 */
UCLASS(BlueprintType, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMECORE_API UISMStateIndex : public UISMInstanceIndex
{
    GENERATED_BODY()

public:
    UISMStateIndex();

    /**
     * Mapping from state flags to gameplay tags.
     * Populated with defaults in constructor. Override in BP or C++ subclass.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM State Index")
    TMap<EISMInstanceState, FGameplayTag> StateTagMap;

protected:
    virtual void OnHandleChanged(const FISMInstanceHandle& Handle) override;
};

/**
 * Keys handles by their per-instance gameplay tags.
 * One handle can appear under multiple keys (one per tag it carries).
 * Query: "what instances have the ISM.Resource.Wood tag?"
 * Syncs from: OnInstanceTagsChanged
 */
UCLASS(BlueprintType, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMECORE_API UISMTagIndex : public UISMInstanceIndex
{
    GENERATED_BODY()
protected:
    virtual void OnHandleChanged(const FISMInstanceHandle& Handle) override
    {
        RemoveFromAllKeys(Handle);

        if (UISMRuntimeComponent* Comp = Handle.Component.Get())
        {
            FGameplayTagContainer Tags = Comp->GetInstanceTags(Handle.InstanceIndex);
            Tags.GetGameplayTagArray(TagBuffer);

            for (const FGameplayTag& Tag : TagBuffer)
            {
                AddToKey(Tag, Handle);
            }
        }
    }

private:
    // Reusable buffer — avoids allocation per call
    mutable TArray<FGameplayTag> TagBuffer;
};