// ISMInstanceHandle.cpp
#include "ISMInstanceHandle.h"

#include "ISMRuntimeComponent.h"
#include "Interfaces/ISMConvertible.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Logging/LogMacros.h"
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

    // Perform the conversion via the component's implementation
    AActor* Actor = Convertible->ConvertInstance(InstanceIndex, ConversionContext);

    if (Actor)
    {
        SetConvertedActor(Actor);

        // Resolve and apply pooled DMIs based on cached custom data
        // UISMCustomDataConversionSystem handles schema resolution and pool lookup
        UWorld* World = Comp->GetWorld();
        ApplyCustomDataMaterialsToActor(Actor, Comp->GetWorld());
        
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
                        Sub->ReleaseDMI(Template, GetCustomDataFromISM(), *Schema, SlotIdx);
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
	Comp->SetInstanceCustomData(InstanceIndex, NewData);
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
	Comp->SetInstanceCustomDataValue(InstanceIndex, DataIndex, Value);

    // 3. If currently a converted actor, refresh materials
    if (ConvertedActor.IsValid() && World)
    {
        ApplyCustomDataMaterialsToActor(ConvertedActor.Get(), World);
    }
}

float FISMInstanceHandle::ReadCustomDataValue(int32 DataIndex) const
{
    if (!IsValid())
    {
		//UE_LOG(LogISMRuntime, Warning, TEXT("Attempted to read custom data from invalid ISM instance handle"));
        return -6969696.0f;
    }

    UISMRuntimeComponent* Comp = Component.Get();
    if (!Comp)
    {
        return -6969696.0f;
    }
	return Comp->GetInstanceCustomDataValue(InstanceIndex, DataIndex);
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

TArray<float> FISMInstanceHandle::GetCustomDataFromISM() const
{
    if (!IsValid())
    {
        return TArray<float>();
    }

    UISMRuntimeComponent* Comp = Component.Get();
    if (!Comp)
    {
        return TArray<float>();
    }
	return Comp->GetInstanceCustomData(InstanceIndex);
}

// ============================================================
//  Internal
// ============================================================

void FISMInstanceHandle::ApplyCustomDataMaterialsToActor(AActor* Actor, UWorld* World) const
{
    if (!Actor || !World)
    {
        return;
    }
	UE_LOG(LogTemp, Log, TEXT("Applying custom data materials to actor %s"), *Actor->GetName());
    // Delegate full resolution to the conversion system — it handles
    // schema lookup, pool signature building, DMI creation, and application
    UISMCustomDataConversionSystem::RefreshActorMaterials(*this, Actor, World);
}