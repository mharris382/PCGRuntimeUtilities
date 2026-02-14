// Copyright (c) 2025 Max Harris
// Published by Procedural Architect

#include "ISMRuntimeComponent.h"
#include "ISMInstanceHandle.h"

UISMRuntimeComponent::UISMRuntimeComponent()
{
}

#pragma region LIFECYCLE

void UISMRuntimeComponent::BeginPlay()
{
}

void UISMRuntimeComponent::EndPlay(const EEndPlayReason::Type EndReason)
{
}

void UISMRuntimeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
}

#pragma endregion


#pragma region FEATURE_PER_INSTANCE_TAGS

void UISMRuntimeComponent::AddInstanceTag(int32 InstanceIndex, FGameplayTag Tag)
{
}

void UISMRuntimeComponent::RemoveInstanceTag(int32 InstanceIndex, FGameplayTag Tag)
{
}

bool UISMRuntimeComponent::InstanceHasTag(int32 InstanceIndex, FGameplayTag Tag) const
{
	return false;
}

FGameplayTagContainer UISMRuntimeComponent::GetInstanceTags(int32 InstanceIndex) const
{
	return FGameplayTagContainer();
}

#pragma endregion


#pragma region INSTANCE_MANAGEMENT

void UISMRuntimeComponent::InitializeInstances()
{
}

void UISMRuntimeComponent::DestroyInstance(int32 InstanceIndex)
{
	HideInstance(InstanceIndex);
}

void UISMRuntimeComponent::HideInstance(int32 InstanceIndex)
{
}

void UISMRuntimeComponent::ShowInstance(int32 InstanceIndex)
{
}

bool UISMRuntimeComponent::IsInstanceDestroyed(int32 InstanceIndex) const
{
	return false;
}

bool UISMRuntimeComponent::IsInstanceActive(int32 InstanceIndex) const
{
	return false;
}

int32 UISMRuntimeComponent::GetInstanceCount() const
{
	return int32();
}

int32 UISMRuntimeComponent::GetActiveInstanceCount() const
{
	return int32();
}
#pragma endregion

#pragma region TRANSFORM_ACCESS

FTransform UISMRuntimeComponent::GetInstanceTransform(int32 InstanceIndex) const
{
	return FTransform();
}

FVector UISMRuntimeComponent::GetInstanceLocation(int32 InstanceIndex) const
{
	return FVector();
}

void UISMRuntimeComponent::UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewTransform, bool bUpdateSpatialIndex)
{
}

#pragma endregion


#pragma region SPATIAL_QUERIES

TArray<int32> UISMRuntimeComponent::GetInstancesInRadius(const FVector& Location, float Radius, bool bIncludeDestroyed) const
{
	return TArray<int32>();
}

TArray<int32> UISMRuntimeComponent::GetInstancesInBox(const FBox& Box, bool bIncludeDestroyed) const
{
	return TArray<int32>();
}

int32 UISMRuntimeComponent::GetNearestInstance(const FVector& Location, float MaxDistance, bool bIncludeDestroyed) const
{
	return int32();
}

TArray<int32> UISMRuntimeComponent::QueryInstances(const FVector& Location, float Radius, const FISMQueryFilter& Filter) const
{
	return TArray<int32>();
}

#pragma endregion


#pragma region STATE_MANAGEMENT

uint8 UISMRuntimeComponent::GetInstanceStateFlags(int32 InstanceIndex) const
{
	return uint8();
}

bool UISMRuntimeComponent::IsInstanceInState(int32 InstanceIndex, EISMInstanceState State) const
{
	return false;
}

void UISMRuntimeComponent::SetInstanceState(int32 InstanceIndex, EISMInstanceState State, bool bValue)
{
}

const FISMInstanceState* UISMRuntimeComponent::GetInstanceState(int32 InstanceIndex) const
{
	return nullptr;
}

FISMInstanceState* UISMRuntimeComponent::GetInstanceStateMutable(int32 InstanceIndex)
{
	return nullptr;
}

FISMInstanceHandle UISMRuntimeComponent::GetInstanceHandle(int32 InstanceIndex)
{
	return FISMInstanceHandle();
}

TArray<FISMInstanceHandle> UISMRuntimeComponent::GetConvertedInstances() const
{
	return TArray<FISMInstanceHandle>();
}

void UISMRuntimeComponent::ReturnAllConvertedInstances(bool bDestroyActors, bool bUpdateTransforms)
{
}

bool UISMRuntimeComponent::IsInstanceConverted(int32 InstanceIndex) const
{
	return false;
}

#pragma endregion


#pragma region INSTANCE_HANDLES

FISMInstanceHandle UISMRuntimeComponent::GetInstanceHandle(int32 InstanceIndex)
{
    return GetOrCreateHandle(InstanceIndex);
}

FISMInstanceHandle& UISMRuntimeComponent::GetOrCreateHandle(int32 InstanceIndex)
{
    if (!InstanceHandles.Contains(InstanceIndex))
    {
        InstanceHandles.Add(InstanceIndex, FISMInstanceHandle(this, InstanceIndex));
    }

    return InstanceHandles[InstanceIndex];
}

TArray<FISMInstanceHandle> UISMRuntimeComponent::GetConvertedInstances() const
{
    TArray<FISMInstanceHandle> ConvertedHandles;

    for (const auto& Pair : InstanceHandles)
    {
        if (Pair.Value.IsConvertedToActor())
        {
            ConvertedHandles.Add(Pair.Value);
        }
    }

    return ConvertedHandles;
}

void UISMRuntimeComponent::ReturnAllConvertedInstances(bool bDestroyActors, bool bUpdateTransforms)
{
    TArray<FISMInstanceHandle> ConvertedHandles = GetConvertedInstances();

    for (FISMInstanceHandle& Handle : ConvertedHandles)
    {
        Handle.ReturnToISM(bDestroyActors, bUpdateTransforms);
    }
}

bool UISMRuntimeComponent::IsInstanceConverted(int32 InstanceIndex) const
{
    if (const FISMInstanceHandle* Handle = InstanceHandles.Find(InstanceIndex))
    {
        return Handle->IsConvertedToActor();
    }

    return false;
}
#pragma endregion


#pragma region SUBCLASS_HOOKS


void UISMRuntimeComponent::BuildComponentTags()
{
}

void UISMRuntimeComponent::OnInitializationComplete()
{
}

void UISMRuntimeComponent::OnInstancePreDestroy(int32 InstanceIndex)
{
}

void UISMRuntimeComponent::OnInstancePostDestroy(int32 InstanceIndex)
{
}

#pragma endregion


#pragma region SUBSYSTEM_INTEGRATION

void UISMRuntimeComponent::RegisterWithSubsystem()
{
}

void UISMRuntimeComponent::UnregisterFromSubsystem()
{
}

#pragma endregion


#pragma region HELPERS

FGameplayTagContainer UISMRuntimeComponent::GetEffectiveTagsForInstance(int32 InstanceIndex) const
{
	return ISMComponentTags;
}

bool UISMRuntimeComponent::IsValidInstanceIndex(int32 InstanceIndex) const
{
	return InstanceStates.Contains(InstanceIndex);
}

void UISMRuntimeComponent::BroadcastStateChange(int32 InstanceIndex)
{
	OnInstanceStateChanged.Broadcast(this, InstanceIndex);
}

void UISMRuntimeComponent::BroadcastDestruction(int32 InstanceIndex)
{
	OnInstanceDestroyed.Broadcast(this, InstanceIndex);
}

void UISMRuntimeComponent::BroadcastTagChange(int32 InstanceIndex)
{
	OnInstanceTagsChanged.Broadcast(this, InstanceIndex);
}


#pragma endregion

