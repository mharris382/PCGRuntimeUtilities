// ISMPreviewContext.cpp
#include "ISMPreviewContext.h"

#include "ISMRuntimeComponent.h"
#include "ISMSelectionSet.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"

UISMPreviewContext::UISMPreviewContext()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ============================================================
//  BeginPreview  (Destroy / Move)
// ============================================================

bool UISMPreviewContext::BeginPreview(UISMSelectionSet* InSelectionSet, EISMPreviewType InPreviewType)
{
    if (!InSelectionSet || InSelectionSet->IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPreviewContext::BeginPreview - selection set is null or empty"));
        return false;
    }

    if (InPreviewType == EISMPreviewType::Placement)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UISMPreviewContext::BeginPreview - use BeginPlacementPreview for Placement type"));
        return false;
    }

    if (PreviewState == EISMPreviewState::Pending)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPreviewContext::BeginPreview - already pending"));
        return false;
    }

    PreviewType = InPreviewType;
    SourceSelectionSet = InSelectionSet;
    PreviewHandles = InSelectionSet->GetValidSelectedHandles();

    if (PreviewHandles.IsEmpty())
    {
        return false;
    }

    // Convert each selected instance to a preview actor
    for (const FISMInstanceHandle& Handle : PreviewHandles)
    {
        AActor* PreviewActor = SpawnPreviewActorForHandle(Handle);
        PreviewActors.Add(PreviewActor); // nullptr entries kept — parallel array
        if (PreviewActor)
        {
            ApplyPreviewMaterial(PreviewActor);
        }
    }

    // For destroy preview, hide the source instances (they're now represented by preview actors)
    // For move preview, same — source is hidden, preview is at destination
    for (const FISMInstanceHandle& Handle : PreviewHandles)
    {
        if (Handle.IsValid())
        {
            if (UISMRuntimeComponent* Comp = Handle.Component.Get())
            {
                Comp->HideInstance(Handle.InstanceIndex);
            }
        }
    }

    PreviewState = EISMPreviewState::Pending;
    return true;
}

// ============================================================
//  BeginPlacementPreview
// ============================================================

bool UISMPreviewContext::BeginPlacementPreview(UISMRuntimeComponent* TargetComponent,
    const FTransform& InitialTransform)
{
    if (!TargetComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPreviewContext::BeginPlacementPreview - null TargetComponent"));
        return false;
    }

    if (PreviewState == EISMPreviewState::Pending)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPreviewContext::BeginPlacementPreview - already pending"));
        return false;
    }

    PreviewType = EISMPreviewType::Placement;

    // Add a hidden reserved instance to the spatial index so AABB queries
    // can discover the pending placement as a candidate occupant
    const int32 ReservedIndex = TargetComponent->AddInstance(InitialTransform, true);
    if (ReservedIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UISMPreviewContext::BeginPlacementPreview - failed to reserve instance slot"));
        return false;
    }

    // Mark it reserved/hidden so it doesn't render but remains in spatial index
    TargetComponent->SetInstanceState(ReservedIndex, EISMInstanceState::Hidden, true);
    TargetComponent->HideInstance(ReservedIndex);

    PlacementHandle = TargetComponent->GetInstanceHandle(ReservedIndex);

    // Spawn a single preview actor at the initial transform
    UWorld* World = GetWorld();
    if (World && PreviewActorClass)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        AActor* PreviewActor = World->SpawnActor<AActor>(
            PreviewActorClass, InitialTransform, Params);

        PreviewActors.Add(PreviewActor);

        if (PreviewActor)
        {
            ApplyPreviewMaterial(PreviewActor);
        }
    }
    else
    {
        PreviewActors.Add(nullptr);
    }

    PreviewState = EISMPreviewState::Pending;
    return true;
}

// ============================================================
//  UpdatePlacementTransform
// ============================================================

void UISMPreviewContext::UpdatePlacementTransform(const FTransform& NewTransform)
{
    if (!EnsurePending(TEXT("UpdatePlacementTransform"))) return;
    if (PreviewType != EISMPreviewType::Placement) return;

    // Move the preview actor
    if (PreviewActors.Num() > 0 && PreviewActors[0].IsValid())
    {
        PreviewActors[0]->SetActorTransform(NewTransform);
    }

    // Update the reserved instance transform in the spatial index
    // so AABB queries see the current pending location
    if (PlacementHandle.IsValid())
    {
        if (UISMRuntimeComponent* Comp = PlacementHandle.Component.Get())
        {
            Comp->UpdateInstanceTransform(PlacementHandle.InstanceIndex, NewTransform,
                true,  // update spatial index
                false  // don't recalculate full bounds
            );
        }
    }
}

// ============================================================
//  Confirm
// ============================================================

void UISMPreviewContext::Confirm()
{
    if (!EnsurePending(TEXT("Confirm"))) return;

    switch (PreviewType)
    {
    case EISMPreviewType::Destroy:
    {
        // Destroy source instances — they were already hidden, now make it permanent
        for (const FISMInstanceHandle& Handle : PreviewHandles)
        {
            if (!Handle.IsValid()) continue;
            if (UISMRuntimeComponent* Comp = Handle.Component.Get())
            {
                Comp->DestroyInstance(Handle.InstanceIndex);
            }
        }
        CleanupPreviewActors();
        break;
    }

    case EISMPreviewType::Placement:
    {
        // Unhide the reserved instance — it becomes a real permanent instance
        if (PlacementHandle.IsValid())
        {
            if (UISMRuntimeComponent* Comp = PlacementHandle.Component.Get())
            {
                Comp->SetInstanceState(
                    PlacementHandle.InstanceIndex, EISMInstanceState::Hidden, false);
                Comp->ShowInstance(PlacementHandle.InstanceIndex);
            }
        }
        CleanupPreviewActors();
        break;
    }

    case EISMPreviewType::Move:
    {
        // Update source instance transforms to match preview actor final positions
        for (int32 i = 0; i < PreviewHandles.Num(); ++i)
        {
            const FISMInstanceHandle& Handle = PreviewHandles[i];
            if (!Handle.IsValid()) continue;

            FTransform FinalTransform = Handle.GetTransform();
            if (PreviewActors.IsValidIndex(i) && PreviewActors[i].IsValid())
            {
                FinalTransform = PreviewActors[i]->GetActorTransform();
            }

            if (UISMRuntimeComponent* Comp = Handle.Component.Get())
            {
                Comp->UpdateInstanceTransform(Handle.InstanceIndex, FinalTransform);
                Comp->ShowInstance(Handle.InstanceIndex);
            }
        }
        CleanupPreviewActors();
        break;
    }
    }

    PreviewState = EISMPreviewState::Confirmed;
    OnPreviewResolved.Broadcast(PreviewType, PreviewState, PreviewHandles);
}

// ============================================================
//  Cancel
// ============================================================

void UISMPreviewContext::Cancel()
{
    if (!EnsurePending(TEXT("Cancel"))) return;

    switch (PreviewType)
    {
    case EISMPreviewType::Destroy:
    case EISMPreviewType::Move:
    {
        // Restore hidden source instances
        for (const FISMInstanceHandle& Handle : PreviewHandles)
        {
            if (!Handle.IsValid()) continue;
            if (UISMRuntimeComponent* Comp = Handle.Component.Get())
            {
                Comp->ShowInstance(Handle.InstanceIndex);
            }
        }
        CleanupPreviewActors();
        break;
    }

    case EISMPreviewType::Placement:
    {
        // Remove the reserved instance entirely — it was never confirmed
        if (PlacementHandle.IsValid())
        {
            if (UISMRuntimeComponent* Comp = PlacementHandle.Component.Get())
            {
                Comp->DestroyInstance(PlacementHandle.InstanceIndex);
            }
            PlacementHandle = FISMInstanceHandle();
        }
        CleanupPreviewActors();
        break;
    }
    }

    PreviewState = EISMPreviewState::Cancelled;
    OnPreviewResolved.Broadcast(PreviewType, PreviewState, PreviewHandles);
}

// ============================================================
//  State Queries
// ============================================================

const TArray<AActor*> UISMPreviewContext::GetPreviewActors() const
{
    TArray<AActor*> Result;
    for (const TWeakObjectPtr<AActor>& Ptr : PreviewActors)
    {
        if (Ptr.IsValid()) Result.Add(Ptr.Get());
    }
    return Result;
}

// ============================================================
//  Internal Helpers
// ============================================================

AActor* UISMPreviewContext::SpawnPreviewActorForHandle(const FISMInstanceHandle& Handle)
{
    if (!Handle.IsValid() || !PreviewActorClass) return nullptr;

    UWorld* World = GetWorld();
    if (!World) return nullptr;

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    return World->SpawnActor<AActor>(PreviewActorClass, Handle.GetTransform(), Params);
}

void UISMPreviewContext::CleanupPreviewActors()
{
    for (TWeakObjectPtr<AActor>& Ptr : PreviewActors)
    {
        if (Ptr.IsValid())
        {
            Ptr->Destroy();
        }
    }
    PreviewActors.Reset();
}

void UISMPreviewContext::ApplyPreviewMaterial(AActor* PreviewActor) const
{
    if (!PreviewActor) return;

    // Write the preview flag into the actor's dynamic material instances
    // via per-instance custom data on any static mesh components present.
    // The material reads this value to activate ghost/translucent appearance.
    TArray<UStaticMeshComponent*> MeshComps;
    PreviewActor->GetComponents<UStaticMeshComponent>(MeshComps);

    for (UStaticMeshComponent* MeshComp : MeshComps)
    {
        if (!MeshComp) continue;

        // Create a dynamic material instance per slot and set the preview parameter
        for (int32 SlotIdx = 0; SlotIdx < MeshComp->GetNumMaterials(); ++SlotIdx)
        {
            UMaterialInterface* Mat = MeshComp->GetMaterial(SlotIdx);
            if (!Mat) continue;

            UMaterialInstanceDynamic* DMI = Cast<UMaterialInstanceDynamic>(Mat);
            if (!DMI)
            {
                DMI = MeshComp->CreateAndSetMaterialInstanceDynamic(SlotIdx);
            }

            if (DMI)
            {
                // "PreviewMode" is the expected scalar parameter name in the material
                DMI->SetScalarParameterValue(FName("PreviewMode"), PreviewMaterialDataValue);
            }
        }
    }
}

bool UISMPreviewContext::EnsurePending(const FString& OperationName) const
{
    if (PreviewState != EISMPreviewState::Pending)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UISMPreviewContext::%s - preview is not in Pending state (current: %d)"),
            *OperationName, static_cast<int32>(PreviewState));
        return false;
    }
    return true;
}