// ISMInstanceIndex.cpp
#include "ISMInstanceIndex.h"

#include "ISMRuntimeComponent.h"
#include "ISMInstanceState.h"

DEFINE_LOG_CATEGORY_STATIC(LogISMIndex, Log, All);

// ============================================================
//  Constructor
// ============================================================

UISMInstanceIndex::UISMInstanceIndex()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ============================================================
//  Registration
// ============================================================

void UISMInstanceIndex::RegisterWithComponent(UISMRuntimeComponent* Component)
{
    if (!Component)
    {
        UE_LOG(LogISMIndex, Warning,
            TEXT("UISMInstanceIndex::RegisterWithComponent — null component"));
        return;
    }

    // Avoid double registration
    for (const TWeakObjectPtr<UISMRuntimeComponent>& Existing : RegisteredComponents)
    {
        if (Existing.Get() == Component) return;
    }

    RegisteredComponents.Add(Component);

    // Subscribe to all relevant delegates
    FComponentDelegateHandles Handles;

    Handles.StateChanged = Component->OnInstanceStateChangedNative.AddUObject(
        this, &UISMInstanceIndex::HandleStateChanged);

    Handles.Destroyed = Component->OnInstanceDestroyedNative.AddUObject(
        this, &UISMInstanceIndex::HandleDestroyed);

    Handles.TagChanged = Component->OnInstanceTagsChangedNative.AddLambda(
        [this](UISMRuntimeComponent* Comp, int32 Idx)
        {
            HandleTagChanged(Comp, Idx);
        });

    // Ownership/possession/attachment — only bind if the component exposes them
    // These are native multicast delegates on the component
    Handles.OwnershipChanged = Component->OnInstanceOwnerChangedNative.AddUObject(
        this, &UISMInstanceIndex::HandleOwnershipChanged);

    Handles.PossessionChanged = Component->OnInstancePossessionChangedNative.AddUObject(
        this, &UISMInstanceIndex::HandlePossessionChanged);

    Handles.AttachmentChanged = Component->OnInstanceAttachmentChangedNative.AddUObject(
        this, &UISMInstanceIndex::HandleAttachmentChanged);

    DelegateHandles.Add(Component, Handles);

    // Build initial index from existing instances
    const int32 Count = Component->GetInstanceCount();
    for (int32 i = 0; i < Count; ++i)
    {
        if (!Component->IsInstanceDestroyed(i))
        {
            IndexHandle(Component, i);
        }
    }

    UE_LOG(LogISMIndex, Verbose,
        TEXT("UISMInstanceIndex: registered with %s (%d existing instances indexed)"),
        *Component->GetName(), Count);
}

void UISMInstanceIndex::UnregisterFromComponent(UISMRuntimeComponent* Component)
{
    if (!Component) return;

    // Unbind delegates
    if (FComponentDelegateHandles* Handles = DelegateHandles.Find(Component))
    {
        Component->OnInstanceStateChangedNative.Remove(Handles->StateChanged);
        Component->OnInstanceDestroyedNative.Remove(Handles->Destroyed);
        // Tag/ownership delegates are dynamic — remove via handle not supported
        // on DECLARE_DYNAMIC_MULTICAST_DELEGATE, so we remove the lambda-bound ones
        // by clearing the entire delegate if it was the only subscriber, or accept
        // slight overhead. For production, migrate to native delegates for all events.
        Component->OnInstanceOwnerChangedNative.Remove(Handles->OwnershipChanged);
        Component->OnInstancePossessionChangedNative.Remove(Handles->PossessionChanged);
        Component->OnInstanceAttachmentChangedNative.Remove(Handles->AttachmentChanged);

        DelegateHandles.Remove(Component);
    }

    RegisteredComponents.RemoveAll([Component](const TWeakObjectPtr<UISMRuntimeComponent>& Ptr)
    {
        return Ptr.Get() == Component;
    });

    // Remove all handles that belonged to this component
    for (auto It = HandleToKeys.CreateIterator(); It; ++It)
    {
        if (It.Key().Component.Get() == Component)
        {
            // Remove from forward index
            for (const FGameplayTag& Key : It.Value())
            {
                if (TSet<FISMInstanceHandle>* Set = Index.Find(Key))
                {
                    Set->Remove(It.Key());
                    if (Set->IsEmpty()) Index.Remove(Key);
                }
            }
            It.RemoveCurrent();
        }
    }
}

void UISMInstanceIndex::UnregisterAll()
{
    TArray<UISMRuntimeComponent*> ToUnregister;
    for (const TWeakObjectPtr<UISMRuntimeComponent>& Ptr : RegisteredComponents)
    {
        if (UISMRuntimeComponent* Comp = Ptr.Get())
        {
            ToUnregister.Add(Comp);
        }
    }
    for (UISMRuntimeComponent* Comp : ToUnregister)
    {
        UnregisterFromComponent(Comp);
    }
    Index.Reset();
    HandleToKeys.Reset();
}

// ============================================================
//  Single-Index Queries
// ============================================================

TArray<FISMInstanceHandle> UISMInstanceIndex::GetHandlesForTag(FGameplayTag Key) const
{
    if (const TSet<FISMInstanceHandle>* Set = Index.Find(Key))
    {
        return Set->Array();
    }
    return {};
}

TArray<FISMInstanceHandle> UISMInstanceIndex::GetHandlesForAllTags(
    const FGameplayTagContainer& Keys) const
{
    if (Keys.IsEmpty()) return {};

    // Collect the TSet pointers, sort ascending by size
    TArray<const TSet<FISMInstanceHandle>*> Sets;
    for (const FGameplayTag& Key : Keys)
    {
        if (const TSet<FISMInstanceHandle>* Set = Index.Find(Key))
        {
            Sets.Add(Set);
        }
        else
        {
            // One key has zero entries — intersection is empty
            return {};
        }
    }

    Sets.Sort([](const TSet<FISMInstanceHandle>& A, const TSet<FISMInstanceHandle>& B)
    {
        return A.Num() < B.Num();
    });

    // Iterate smallest, probe all others
    TArray<FISMInstanceHandle> Result;
    for (const FISMInstanceHandle& Handle : *Sets[0])
    {
        bool bInAll = true;
        for (int32 i = 1; i < Sets.Num(); ++i)
        {
            if (!Sets[i]->Contains(Handle))
            {
                bInAll = false;
                break;
            }
        }
        if (bInAll) Result.Add(Handle);
    }
    return Result;
}

TArray<FISMInstanceHandle> UISMInstanceIndex::GetHandlesForAnyTag(
    const FGameplayTagContainer& Keys) const
{
    TSet<FISMInstanceHandle> Union;
    for (const FGameplayTag& Key : Keys)
    {
        if (const TSet<FISMInstanceHandle>* Set = Index.Find(Key))
        {
            Union.Append(*Set);
        }
    }
    return Union.Array();
}

bool UISMInstanceIndex::IsHandleIndexed(FGameplayTag Key,
    const FISMInstanceHandle& Handle) const
{
    if (const TSet<FISMInstanceHandle>* Set = Index.Find(Key))
    {
        return Set->Contains(Handle);
    }
    return false;
}

int32 UISMInstanceIndex::GetCountForTag(FGameplayTag Key) const
{
    if (const TSet<FISMInstanceHandle>* Set = Index.Find(Key))
    {
        return Set->Num();
    }
    return 0;
}

TArray<FGameplayTag> UISMInstanceIndex::GetActiveKeys() const
{
    TArray<FGameplayTag> Keys;
    Index.GetKeys(Keys);
    return Keys;
}

int32 UISMInstanceIndex::GetTotalIndexedCount() const
{
    return HandleToKeys.Num();
}

// ============================================================
//  Spatial Intersection
// ============================================================

TArray<FISMInstanceHandle> UISMInstanceIndex::IntersectWithSpatial(
    FGameplayTag Key,
    const TArray<int32>& SpatialCandidates,
    UISMRuntimeComponent* Component) const
{
    const TSet<FISMInstanceHandle>* Set = Index.Find(Key);
    if (!Set || Set->IsEmpty() || SpatialCandidates.IsEmpty())
    {
        return {};
    }

    TArray<FISMInstanceHandle> Result;
    Result.Reserve(FMath::Min(SpatialCandidates.Num(), Set->Num()));

    for (int32 InstanceIndex : SpatialCandidates)
    {
        FISMInstanceHandle Candidate = Component->GetInstanceHandle(InstanceIndex);
        if (Set->Contains(Candidate))
        {
            Result.Add(Candidate);
        }
    }
    return Result;
}

// ============================================================
//  Multi-Index Intersection (Static)
// ============================================================

TArray<FISMInstanceHandle> UISMInstanceIndex::Intersect(
    const UISMInstanceIndex* IndexA, FGameplayTag KeyA,
    const UISMInstanceIndex* IndexB, FGameplayTag KeyB)
{
    if (!IndexA || !IndexB) return {};

    const TSet<FISMInstanceHandle>* SetA = IndexA->GetSetForKey(KeyA);
    const TSet<FISMInstanceHandle>* SetB = IndexB->GetSetForKey(KeyB);

    if (!SetA || !SetB) return {};

    // Always iterate the smaller set
    const TSet<FISMInstanceHandle>* Small = SetA->Num() <= SetB->Num() ? SetA : SetB;
    const TSet<FISMInstanceHandle>* Large = SetA->Num() <= SetB->Num() ? SetB : SetA;

    TArray<FISMInstanceHandle> Result;
    for (const FISMInstanceHandle& Handle : *Small)
    {
        if (Large->Contains(Handle))
        {
            Result.Add(Handle);
        }
    }
    return Result;
}

TArray<FISMInstanceHandle> UISMInstanceIndex::IntersectAll(
    const TArray<FISMIndexQuery>& Queries)
{
    if (Queries.IsEmpty()) return {};

    // Gather valid set pointers paired with their sizes for sorting
    TArray<TPair<const TSet<FISMInstanceHandle>*, int32>> Sets;
    for (const FISMIndexQuery& Q : Queries)
    {
        if (!Q.Index) continue;
        const TSet<FISMInstanceHandle>* Set = Q.Index->GetSetForKey(Q.Key);
        if (!Set)
        {
            // One query has zero results — whole intersection is empty
            return {};
        }
        Sets.Add({ Set, Set->Num() });
    }

    if (Sets.IsEmpty()) return {};

    // Sort ascending by set size — smallest first = most selective filter first
    Sets.Sort([](const TPair<const TSet<FISMInstanceHandle>*, int32>& A,
                 const TPair<const TSet<FISMInstanceHandle>*, int32>& B)
    {
        return A.Value < B.Value;
    });

    TArray<FISMInstanceHandle> Result;
    for (const FISMInstanceHandle& Handle : *Sets[0].Key)
    {
        bool bInAll = true;
        for (int32 i = 1; i < Sets.Num(); ++i)
        {
            if (!Sets[i].Key->Contains(Handle))
            {
                bInAll = false;
                break;
            }
        }
        if (bInAll) Result.Add(Handle);
    }
    return Result;
}

TArray<FISMInstanceHandle> UISMInstanceIndex::IntersectSpatialWithIndexes(
    const TArray<int32>& SpatialCandidates,
    UISMRuntimeComponent* Component,
    const TArray<FISMIndexQuery>& Queries)
{
    if (SpatialCandidates.IsEmpty() || !Component) return {};

    // Gather index sets, bail early if any is empty
    TArray<const TSet<FISMInstanceHandle>*> Sets;
    for (const FISMIndexQuery& Q : Queries)
    {
        if (!Q.Index) continue;
        const TSet<FISMInstanceHandle>* Set = Q.Index->GetSetForKey(Q.Key);
        if (!Set || Set->IsEmpty()) return {};
        Sets.Add(Set);
    }

    // Spatial candidates are the outer loop — O(k * n_indexes)
    TArray<FISMInstanceHandle> Result;
    for (int32 InstanceIndex : SpatialCandidates)
    {
        FISMInstanceHandle Candidate = Component->GetInstanceHandle(InstanceIndex);
        if (!Candidate.IsValid()) continue;

        bool bPassesAll = true;
        for (const TSet<FISMInstanceHandle>* Set : Sets)
        {
            if (!Set->Contains(Candidate))
            {
                bPassesAll = false;
                break;
            }
        }
        if (bPassesAll) Result.Add(Candidate);
    }
    return Result;
}

// ============================================================
//  Maintenance
// ============================================================

void UISMInstanceIndex::RebuildIndex()
{
    Index.Reset();
    HandleToKeys.Reset();

    for (const TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : RegisteredComponents)
    {
        UISMRuntimeComponent* Comp = CompPtr.Get();
        if (!Comp) continue;

        const int32 Count = Comp->GetInstanceCount();
        for (int32 i = 0; i < Count; ++i)
        {
            if (!Comp->IsInstanceDestroyed(i))
            {
                IndexHandle(Comp, i);
            }
        }
    }
}

void UISMInstanceIndex::PruneStaleHandles()
{
    TArray<FISMInstanceHandle> ToRemove;
    for (const auto& Pair : HandleToKeys)
    {
        if (!Pair.Key.IsValid())
        {
            ToRemove.Add(Pair.Key);
        }
    }
    for (const FISMInstanceHandle& Handle : ToRemove)
    {
        RemoveFromAllKeys(Handle);
    }
}

// ============================================================
//  Protected Helpers
// ============================================================

void UISMInstanceIndex::AddToKey(FGameplayTag Key, const FISMInstanceHandle& Handle)
{
    if (!Key.IsValid() || !Handle.IsValid()) return;

    Index.FindOrAdd(Key).Add(Handle);
    HandleToKeys.FindOrAdd(Handle).Add(Key);
}

void UISMInstanceIndex::RemoveFromKey(FGameplayTag Key, const FISMInstanceHandle& Handle)
{
    if (TSet<FISMInstanceHandle>* Set = Index.Find(Key))
    {
        Set->Remove(Handle);
        if (Set->IsEmpty()) Index.Remove(Key);
    }

    if (TSet<FGameplayTag>* Keys = HandleToKeys.Find(Handle))
    {
        Keys->Remove(Key);
        if (Keys->IsEmpty()) HandleToKeys.Remove(Handle);
    }
}

void UISMInstanceIndex::RemoveFromAllKeys(const FISMInstanceHandle& Handle)
{
    TSet<FGameplayTag>* Keys = HandleToKeys.Find(Handle);
    if (!Keys) return;

    for (const FGameplayTag& Key : *Keys)
    {
        if (TSet<FISMInstanceHandle>* Set = Index.Find(Key))
        {
            Set->Remove(Handle);
            if (Set->IsEmpty()) Index.Remove(Key);
        }
    }
    HandleToKeys.Remove(Handle);
}

const TSet<FISMInstanceHandle>* UISMInstanceIndex::GetSetForKey(FGameplayTag Key) const
{
    return Index.Find(Key);
}

// ============================================================
//  Private: Index a single handle
// ============================================================

void UISMInstanceIndex::IndexHandle(UISMRuntimeComponent* Component, int32 InstanceIndex)
{
    FISMInstanceHandle Handle = Component->GetInstanceHandle(InstanceIndex);
    if (Handle.IsValid())
    {
        OnHandleChanged(Handle);
    }
}

// ============================================================
//  Private: Delegate Callbacks
// ============================================================

void UISMInstanceIndex::HandleStateChanged(UISMRuntimeComponent* Component, int32 InstanceIndex)
{
    IndexHandle(Component, InstanceIndex);
}

void UISMInstanceIndex::HandleDestroyed(UISMRuntimeComponent* Component, int32 InstanceIndex)
{
    FISMInstanceHandle Handle = Component->GetInstanceHandle(InstanceIndex);
    OnHandleRemoved(Handle);
}

void UISMInstanceIndex::HandleTagChanged(UISMRuntimeComponent* Component, int32 InstanceIndex)
{
    IndexHandle(Component, InstanceIndex);
}

void UISMInstanceIndex::HandleOwnershipChanged(UISMRuntimeComponent* Component, int32 InstanceIndex)
{
    IndexHandle(Component, InstanceIndex);
}

void UISMInstanceIndex::HandlePossessionChanged(UISMRuntimeComponent* Component, int32 InstanceIndex)
{
    IndexHandle(Component, InstanceIndex);
}

void UISMInstanceIndex::HandleAttachmentChanged(UISMRuntimeComponent* Component, int32 InstanceIndex)
{
    IndexHandle(Component, InstanceIndex);
}

// ============================================================
//  UISMStateIndex
// ============================================================

UISMStateIndex::UISMStateIndex()
{
    // Default state → tag mapping
    StateTagMap.Add(EISMInstanceState::Intact,
        FGameplayTag::RequestGameplayTag("ISM.State.Intact"));
    StateTagMap.Add(EISMInstanceState::Damaged,
        FGameplayTag::RequestGameplayTag("ISM.State.Damaged"));
    StateTagMap.Add(EISMInstanceState::Destroyed,
        FGameplayTag::RequestGameplayTag("ISM.State.Destroyed"));
    StateTagMap.Add(EISMInstanceState::Hidden,
        FGameplayTag::RequestGameplayTag("ISM.State.Hidden"));
    StateTagMap.Add(EISMInstanceState::Converting,
        FGameplayTag::RequestGameplayTag("ISM.State.Converting"));
    StateTagMap.Add(EISMInstanceState::Collected,
        FGameplayTag::RequestGameplayTag("ISM.State.Collected"));
}

void UISMStateIndex::OnHandleChanged(const FISMInstanceHandle& Handle)
{
    RemoveFromAllKeys(Handle);

    UISMRuntimeComponent* Comp = Handle.Component.Get();
    if (!Comp) return;

    const FISMInstanceState* State = Comp->GetInstanceState(Handle.InstanceIndex);
    if (!State) return;

    for (const auto& Pair : StateTagMap)
    {
        if (State->HasFlag(Pair.Key) && Pair.Value.IsValid())
        {
            AddToKey(Pair.Value, Handle);
        }
    }
}