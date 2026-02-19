// ISMInstanceHandle.cpp
#include "ISMInstanceHandle.h"
#include "GameplayTagContainer.h"
#include "ISMRuntimeComponent.h"
#include "Interfaces/ISMConvertible.h"
#include "GameFramework/Actor.h"

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
    // If converted, get location from actor
    if (ConvertedActor.IsValid())
    {
        return ConvertedActor->GetActorLocation();
    }

    // Otherwise get from ISM
    if (UISMRuntimeComponent* Comp = Component.Get())
    {
        return Comp->GetInstanceLocation(InstanceIndex);
    }

    return FVector::ZeroVector;
}

FTransform FISMInstanceHandle::GetTransform() const
{
    // If converted, get transform from actor
    if (ConvertedActor.IsValid())
    {
        return ConvertedActor->GetActorTransform();
    }

    // Otherwise get from ISM
    if (UISMRuntimeComponent* Comp = Component.Get())
    {
        return Comp->GetInstanceTransform(InstanceIndex);
    }

    return FTransform::Identity;
}

FGameplayTagContainer FISMInstanceHandle::GetInstanceTags() const
{
    // If converted, get transform from actor
    //if (ConvertedActor.IsValid())
    //{
    //    return ConvertedActor->GetActorTransform();
    //}
    FGameplayTagContainer tags = FGameplayTagContainer();

    // Otherwise get from ISM
    if (UISMRuntimeComponent* Comp = Component.Get())
    {
		tags.AppendTags(Comp->GetInstanceTags(InstanceIndex));
    }
    return tags;
}

AActor* FISMInstanceHandle::ConvertToActor(const FISMConversionContext& ConversionContext)
{
    if (!IsValid())
    {
        return nullptr;
    }

    // Already converted?
    if (ConvertedActor.IsValid())
    {
        return ConvertedActor.Get();
    }

    UISMRuntimeComponent* Comp = Component.Get();
    if (!Comp)
    {
        return nullptr;
    }

    // Check if component implements conversion interface
    IISMConvertible* Convertible = Cast<IISMConvertible>(Comp);
    if (!Convertible)
    {
        return nullptr;
    }

    // Cache data before conversion
    CachedPreConversionTransform = Comp->GetInstanceTransform(InstanceIndex);
    //TODO: CachedCustomData = Comp->GetInstanceCustomData(InstanceIndex);

    // Perform conversion
    AActor* Actor = Convertible->ConvertInstance(InstanceIndex, ConversionContext);

    if (Actor)
    {
        SetConvertedActor(Actor);
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
        return false; // Not converted
    }

    UISMRuntimeComponent* Comp = Component.Get();
    if (!Comp)
    {
        return false;
    }

    // Get final transform from actor
    FTransform FinalTransform = Actor->GetActorTransform();

    // Call component's delegate to allow custom cleanup
    bool bShouldDestroyActor = bDestroyActor;
    Comp->OnReleaseConvertedActor.ExecuteIfBound(Actor, bShouldDestroyActor);

    // Update ISM instance
    if (bUpdateTransform)
    {
        Comp->UpdateInstanceTransform(InstanceIndex, FinalTransform);
    }

    // Restore custom data if available
    if (CachedCustomData.Num() > 0)
    {
        //TODO:Comp->SetInstanceCustomData(InstanceIndex, CachedCustomData);
    }

    // Show the instance (in case it was hidden during conversion)
    Comp->ShowInstance(InstanceIndex);

    // Remove conversion tag
    Comp->RemoveInstanceTag(InstanceIndex,
        FGameplayTag::RequestGameplayTag("ISM.State.Converting"));
    Comp->RemoveInstanceTag(InstanceIndex,
        FGameplayTag::RequestGameplayTag("ISM.State.Converted"));

    // Broadcast event
    Comp->OnInstanceReturnedToISM.ExecuteIfBound(*this, FinalTransform);

    // Clean up actor
    if (bShouldDestroyActor)
    {
        Actor->Destroy();
    }

    // Clear reference
    ConvertedActor.Reset();
    CachedCustomData.Empty();

    return true;
}

void FISMInstanceHandle::SetConvertedActor(AActor* Actor)
{
    ConvertedActor = Actor;

    if (UISMRuntimeComponent* Comp = Component.Get())
    {
        // Add tag to indicate conversion
        Comp->AddInstanceTag(InstanceIndex,
            FGameplayTag::RequestGameplayTag("ISM.State.Converted"));

       // //TODO: Optionally hide the ISM instance while converted
       // if (Comp->InstanceData && !Comp->InstanceData->bUseInvisiblePhysicsProxy)
       // {
       //     Comp->HideInstance(InstanceIndex);
       // }

        // Broadcast event
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
            FGameplayTag::RequestGameplayTag("ISM.State.Converted"));
    }
}