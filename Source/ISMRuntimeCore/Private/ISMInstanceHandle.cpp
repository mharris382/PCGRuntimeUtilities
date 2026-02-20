// ISMInstanceHandle.cpp
#include "ISMInstanceHandle.h"

#include "ISMRuntimeComponent.h"
#include "Interfaces/ISMConvertible.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"

#include "CustomData/ISMCustomDataSchema.h"
#include "CustomData/ISMCustomDataSubsystem.h"
#include "CustomData/ISMCustomDataMaterialProvider.h"
#include "CustomData/ISMCustomDataConversionSystem.h"

// ============================================================
//  State Queries
// ============================================================

bool FISMInstanceHandle::IsValid() const
{
    return Component.IsValid() && InstanceIndex != INDEX_NONE;
}

bool FISMInstanceHandle::IsConvertedToActor() const
{
    return ConvertedActor.IsValid();
}

AActor* FISMInstanceHandle::GetConvertedActor() const
{
    return ConvertedActor.Get();
}

FVector FISMInstanceHandle::GetLocation() const
{
    if (ConvertedActor.IsValid())
    {
        return ConvertedActor->GetActorLocation();
    }

    if (UISMRuntimeComponent* Comp = Component.Get())
    {
        return Comp->GetInstanceLocation(InstanceIndex);
    }

    return FVector::ZeroVector;
}

FTransform FISMInstanceHandle::GetTransform() const
{
    if (ConvertedActor.IsValid())
    {
        return ConvertedActor->GetActorTransform();
    }

    if (UISMRuntimeComponent* Comp = Component.Get())
    {
        return Comp->GetInstanceTransform(InstanceIndex);
    }

    return FTransform::Identity;
}

FGameplayTagContainer FISMInstanceHandle::GetInstanceTags() const
{
    FGameplayTagContainer Tags;

    if (UISMRuntimeComponent* Comp = Component.Get())
    {
        Tags.AppendTags(Comp->GetInstanceTags(InstanceIndex));
    }

    return Tags;
}

// ============================================================
//  Conversion
// ============================================================

AActor* FISMInstanceHandle::ConvertToActor(const FISMConversionContext& ConversionContext)
{
    if (!IsValid())
    {
        return nullptr;
    }

    // Already converted — return existing actor
    if (ConvertedActor.IsValid())
    {
        return ConvertedActor.Get();
    }

    UISMRuntimeComponent* Comp = Component.Get();
    if (!Comp)
    {
        return nullptr;
    }

    IISMConvertible* Convertible = Cast<IISMConvertible>(Comp);
    if (!Convertible)
    {
        return nullptr;
    }

    // Cache state before conversion so ReturnToISM can restore it
    CachedPreConversionTransform = Comp->GetInstanceTransform(InstanceIndex);
    CachedCustomData = Comp->GetInstanceCustomData(InstanceIndex);

    // Perform the conversion via the component's implementation
    AActor* Actor = Convertible->ConvertInstance(InstanceIndex, ConversionContext);

    if (Actor)
    {
        SetConvertedActor(Actor);

        // Resolve and apply pooled DMIs based on cached custom data
        // UISMCustomDataConversionSystem handles schema resolution and pool lookup
        UWorld* World = Comp->GetWorld();
        if (World && CachedCustomData.Num() > 0)
        {
            UISMCustomDataConversionSystem::ResolveAndApply(*this, Actor, World);
        }
    }

    return Actor;
}

bool FISMInstanceHandle::ReturnToISM(bool bDestroyActor, bool bUpdateTransform)
{
    if (!IsValid())
    {
        return false;
    }

    AActor* Actor = ConvertedActor.Get();
    if (!Actor)
    {
        return false;
    }

    UISMRuntimeComponent* Comp = Component.Get();
    if (!Comp)
    {
        return false;
    }

    const FTransform FinalTransform = Actor->GetActorTransform();

    // Allow component delegate to override destroy decision (e.g. for pooling)
    bool bShouldDestroyActor = bDestroyActor;
    Comp->OnReleaseConvertedActor.ExecuteIfBound(Actor, bShouldDestroyActor);

    // Update ISM transform to match final actor position
    if (bUpdateTransform)
    {
        Comp->UpdateInstanceTransform(InstanceIndex, FinalTransform);
    }

    // Restore custom data to ISM PICD.
    // CachedCustomData is always current because WriteCustomData/WriteCustomDataValue
    // keep it in sync even while converted, so this restores the final state correctly.
    if (CachedCustomData.Num() > 0)
    {
        Comp->SetInstanceCustomData(InstanceIndex, CachedCustomData);
    }

    // Release the DMI reference in the shared pool (decrements ref count)
    UWorld* World = Comp->GetWorld();
    if (World)
    {
        UGameInstance* GI = World->GetGameInstance();
        if (UISMCustomDataSubsystem* Sub = GI ? GI->GetSubsystem<UISMCustomDataSubsystem>() : nullptr)
        {
            FName SchemaName;
            if (const FISMCustomDataSchema* Schema = Sub->ResolveSchemaForInstance(*this, SchemaName))
            {
                // Release per slot
                const int32 NumSlots = Comp->ManagedISMComponent
                    ? Comp->ManagedISMComponent->GetNumMaterials()
                    : 1;

                for (int32 SlotIdx = 0; SlotIdx < NumSlots; ++SlotIdx)
                {
                    if (!Schema->AppliesToSlot(SlotIdx)) { continue; }

                    UMaterialInterface* Template = Comp->ManagedISMComponent
                        ? Comp->ManagedISMComponent->GetMaterial(SlotIdx)
                        : nullptr;

                    if (Template)
                    {
                        Sub->ReleaseDMI(Template, CachedCustomData, *Schema, SlotIdx);
                    }
                }
            }
        }
    }

    // Unhide the ISM instance
    Comp->ShowInstance(InstanceIndex);

    // Clear conversion state tags
    Comp->RemoveInstanceTag(InstanceIndex,
        FGameplayTag::RequestGameplayTag(FName("ISM.State.Converting")));
    Comp->RemoveInstanceTag(InstanceIndex,
        FGameplayTag::RequestGameplayTag(FName("ISM.State.Converted")));

    // Broadcast return event before destroying actor
    Comp->OnInstanceReturnedToISM.ExecuteIfBound(*this, FinalTransform);

    if (bShouldDestroyActor)
    {
        Actor->Destroy();
    }

    ConvertedActor.Reset();
    CachedCustomData.Empty();

    return true;
}

void FISMInstanceHandle::SetConvertedActor(AActor* Actor)
{
    ConvertedActor = Actor;

    if (UISMRuntimeComponent* Comp = Component.Get())
    {
        Comp->AddInstanceTag(InstanceIndex,
            FGameplayTag::RequestGameplayTag(FName("ISM.State.Converted")));

        Comp->OnInstanceConvertedToActor.ExecuteIfBound(*this, Actor);
    }
}

void FISMInstanceHandle::ClearConvertedActor()
{
    ConvertedActor.Reset();
    CachedCustomData.Empty();

    if (UISMRuntimeComponent* Comp = Component.Get())
    {
        Comp->RemoveInstanceTag(InstanceIndex,
            FGameplayTag::RequestGameplayTag(FName("ISM.State.Converted")));
    }
}

// ============================================================
//  Custom Data Write-Through
// ============================================================

void FISMInstanceHandle::WriteCustomData(const TArray<float>& NewData, UWorld* World)
{
    if (!IsValid())
    {
        return;
    }

    UISMRuntimeComponent* Comp = Component.Get();
    if (!Comp)
    {
        return;
    }

    // 1. Update ISM PICD (source of truth, always written even if instance is hidden)
    Comp->SetInstanceCustomData(InstanceIndex, NewData);

    // 2. Keep CachedCustomData in sync
    CachedCustomData = NewData;

    // 3. If currently a converted actor, refresh its materials
    if (ConvertedActor.IsValid() && World)
    {
        ApplyCustomDataMaterialsToActor(ConvertedActor.Get(), World);
    }
}

void FISMInstanceHandle::WriteCustomDataValue(int32 DataIndex, float Value, UWorld* World)
{
    if (!IsValid())
    {
        return;
    }

    UISMRuntimeComponent* Comp = Component.Get();
    if (!Comp)
    {
        return;
    }

    // Ensure CachedCustomData is large enough
    if (DataIndex >= CachedCustomData.Num())
    {
        CachedCustomData.SetNumZeroed(DataIndex + 1);
    }

    // Early out if value didn't change — avoids pool lookup and material update
    if (FMath::IsNearlyEqual(CachedCustomData[DataIndex], Value))
    {
        return;
    }

    // 1. Write to ISM PICD
    Comp->SetInstanceCustomDataValue(InstanceIndex, DataIndex, Value);

    // 2. Update cache
    CachedCustomData[DataIndex] = Value;

    // 3. If currently a converted actor, refresh materials
    if (ConvertedActor.IsValid() && World)
    {
        ApplyCustomDataMaterialsToActor(ConvertedActor.Get(), World);
    }
}

float FISMInstanceHandle::ReadCustomDataValue(int32 DataIndex) const
{
    if (CachedCustomData.IsValidIndex(DataIndex))
    {
        return CachedCustomData[DataIndex];
    }

    return 0.f;
}

bool FISMInstanceHandle::RefreshConvertedActorMaterials(UWorld* World)
{
    if (!ConvertedActor.IsValid() || !World)
    {
        return false;
    }

    ApplyCustomDataMaterialsToActor(ConvertedActor.Get(), World);
    return true;
}

// ============================================================
//  Internal
// ============================================================

void FISMInstanceHandle::ApplyCustomDataMaterialsToActor(AActor* Actor, UWorld* World) const
{
    if (!Actor || !World || CachedCustomData.Num() == 0)
    {
        return;
    }

    // Delegate full resolution to the conversion system — it handles
    // schema lookup, pool signature building, DMI creation, and application
    UISMCustomDataConversionSystem::RefreshActorMaterials(*this, Actor, World);
}