// ISMSelectionSet.cpp
#include "ISMSelectionSet.h"

#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "Engine/World.h"

UISMSelectionSet::UISMSelectionSet()
{
    PrimaryComponentTick.bCanEverTick = false;
    SelectedTag = FGameplayTag::RequestGameplayTag(FName("ISM.State.Selected"));
}

void UISMSelectionSet::SelectInstance(const FISMInstanceHandle& Handle, EISMSelectionMode Mode)
{
    if (!Handle.IsValid()) return;
    PruneInvalidHandles();

    switch (Mode)
    {
    case EISMSelectionMode::Replace:
        ApplySelectionTagBatch(SelectedHandles, false);
        SelectedHandles.Reset();
        SelectedHandles.Add(Handle);
        ApplySelectionTag(Handle, true);
        break;

    case EISMSelectionMode::Add:
        if (!IsSelected(Handle))
        {
            SelectedHandles.Add(Handle);
            ApplySelectionTag(Handle, true);
        }
        break;

    case EISMSelectionMode::Remove:
        if (SelectedHandles.RemoveSingle(Handle) > 0)
            ApplySelectionTag(Handle, false);
        break;

    case EISMSelectionMode::Toggle:
        if (IsSelected(Handle)) { SelectedHandles.RemoveSingle(Handle); ApplySelectionTag(Handle, false); }
        else { SelectedHandles.Add(Handle);          ApplySelectionTag(Handle, true); }
        break;
    }

    EnforceSelectionLimit();
    BroadcastSelectionChanged();
}

void UISMSelectionSet::SelectInstances(const TArray<FISMInstanceHandle>& Handles, EISMSelectionMode Mode)
{
    if (Handles.IsEmpty()) return;
    PruneInvalidHandles();

    if (Mode == EISMSelectionMode::Replace)
    {
        ApplySelectionTagBatch(SelectedHandles, false);
        SelectedHandles.Reset();
        for (const FISMInstanceHandle& H : Handles)
            if (H.IsValid()) SelectedHandles.Add(H);
        ApplySelectionTagBatch(SelectedHandles, true);
    }
    else
    {
        TArray<FISMInstanceHandle> ToAdd, ToRemove;
        for (const FISMInstanceHandle& H : Handles)
        {
            if (!H.IsValid()) continue;
            switch (Mode)
            {
            case EISMSelectionMode::Add:
                if (!IsSelected(H)) { SelectedHandles.Add(H); ToAdd.Add(H); }
                break;
            case EISMSelectionMode::Remove:
                if (SelectedHandles.RemoveSingle(H) > 0) ToRemove.Add(H);
                break;
            case EISMSelectionMode::Toggle:
                if (IsSelected(H)) { SelectedHandles.RemoveSingle(H); ToRemove.Add(H); }
                else { SelectedHandles.Add(H);          ToAdd.Add(H); }
                break;
            default: break;
            }
        }
        ApplySelectionTagBatch(ToAdd, true);
        ApplySelectionTagBatch(ToRemove, false);
    }

    EnforceSelectionLimit();
    BroadcastSelectionChanged();
}

void UISMSelectionSet::SelectInstancesInRadius(const FVector& Center, float Radius,
    const FISMQueryFilter& Filter, EISMSelectionMode Mode)
{
    UWorld* World = GetWorld();
    if (!World) return;
    UISMRuntimeSubsystem* Sub = World->GetSubsystem<UISMRuntimeSubsystem>();
    if (!Sub) return;
    TArray<FISMInstanceHandle> Found = Sub->QueryInstancesInRadius(Center, Radius, Filter);
    if (!Found.IsEmpty()) SelectInstances(Found, Mode);
}

void UISMSelectionSet::SelectInstancesInBox(const FBox& Box,
    const FISMQueryFilter& Filter, EISMSelectionMode Mode)
{
    UWorld* World = GetWorld();
    if (!World) return;
    UISMRuntimeSubsystem* Sub = World->GetSubsystem<UISMRuntimeSubsystem>();
    if (!Sub) return;
    TArray<FISMInstanceHandle> Found = Sub->QueryInstancesInBox(Box, Filter);
    if (!Found.IsEmpty()) SelectInstances(Found, Mode);
}

void UISMSelectionSet::DeselectInstance(const FISMInstanceHandle& Handle)
{
    SelectInstance(Handle, EISMSelectionMode::Remove);
}

void UISMSelectionSet::ClearSelection()
{
    if (SelectedHandles.IsEmpty()) return;
    ApplySelectionTagBatch(SelectedHandles, false);
    SelectedHandles.Reset();
    BroadcastSelectionChanged();
}

bool UISMSelectionSet::IsSelected(const FISMInstanceHandle& Handle) const
{
    return SelectedHandles.Contains(Handle);
}

TArray<FISMInstanceHandle> UISMSelectionSet::GetValidSelectedHandles() const
{
    TArray<FISMInstanceHandle> Valid;
    for (const FISMInstanceHandle& H : SelectedHandles)
        if (H.IsValid()) Valid.Add(H);
    return Valid;
}

void UISMSelectionSet::DestroySelected()
{
    PruneInvalidHandles();
    if (SelectedHandles.IsEmpty()) return;

    ApplySelectionTagBatch(SelectedHandles, false);
    for (const FISMInstanceHandle& H : SelectedHandles)
    {
        if (!H.IsValid()) continue;
        if (UISMRuntimeComponent* Comp = H.Component.Get())
            Comp->DestroyInstance(H.InstanceIndex);
    }
    SelectedHandles.Reset();
    BroadcastSelectionChanged();
}

TArray<AActor*> UISMSelectionSet::ConvertSelectedToActors(const FISMConversionContext& Context)
{
    PruneInvalidHandles();
    TArray<AActor*> Result;
    for (const FISMInstanceHandle& H : SelectedHandles)
    {
        if (!H.IsValid()) continue;
        if (UISMRuntimeComponent* Comp = H.Component.Get())
        {
            FISMInstanceHandle& MutableHandle = Comp->GetOrCreateHandle(H.InstanceIndex);
            if (AActor* Actor = MutableHandle.ConvertToActor(Context))
                Result.Add(Actor);
        }
    }
    return Result;
}

void UISMSelectionSet::ReturnSelectedToISM(bool bDestroyActors, bool bUpdateTransforms)
{
    PruneInvalidHandles();
    for (const FISMInstanceHandle& H : SelectedHandles)
    {
        if (!H.IsValid() || !H.IsConvertedToActor()) continue;
        if (UISMRuntimeComponent* Comp = H.Component.Get())
        {
            FISMInstanceHandle& MutableHandle = Comp->GetOrCreateHandle(H.InstanceIndex);
            MutableHandle.ReturnToISM(bDestroyActors, bUpdateTransforms);
        }
    }
}

// ============================================================
//  Protected Helpers
// ============================================================

void UISMSelectionSet::ApplySelectionTag(const FISMInstanceHandle& Handle, bool bAdd) const
{
    if (!SelectedTag.IsValid() || !Handle.IsValid()) return;
    if (UISMRuntimeComponent* Comp = Handle.Component.Get())
    {
        if (bAdd) Comp->AddInstanceTag(Handle.InstanceIndex, SelectedTag);
        else      Comp->RemoveInstanceTag(Handle.InstanceIndex, SelectedTag);
    }
}

void UISMSelectionSet::ApplySelectionTagBatch(const TArray<FISMInstanceHandle>& Handles, bool bAdd) const
{
    for (const FISMInstanceHandle& H : Handles)
        ApplySelectionTag(H, bAdd);
}

void UISMSelectionSet::PruneInvalidHandles()
{
    const int32 Before = SelectedHandles.Num();
    SelectedHandles.RemoveAll([](const FISMInstanceHandle& H) { return !H.IsValid(); });
    if (SelectedHandles.Num() != Before) BroadcastSelectionChanged();
}

void UISMSelectionSet::BroadcastSelectionChanged()
{
    OnSelectionChanged.Broadcast(SelectedHandles);
}

void UISMSelectionSet::EnforceSelectionLimit()
{
    if (MaxSelection <= 0) return;
    while (SelectedHandles.Num() > MaxSelection)
    {
        ApplySelectionTag(SelectedHandles[0], false);
        SelectedHandles.RemoveAt(0);
    }
}