// ISMFeedbackContext.cpp

#include "Feedbacks/ISMFeedbackContext.h"
#include "ISMRuntimeComponent.h"
#include "Logging/LogMacros.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace
{
    static UPhysicalMaterial* GetPhysicalMaterialFromPrimitive(const UPrimitiveComponent* PrimComp)
    {
        if (PrimComp)
        {
            return PrimComp->GetBodyInstance()
                ? PrimComp->GetBodyInstance()->GetSimplePhysicalMaterial()
                : nullptr;
        }
        return nullptr;
    }

    static UPhysicalMaterial* GetPhysicalMaterialFromComponent(const UActorComponent* Component)
    {
        if (const USceneComponent* SceneComp = Cast<USceneComponent>(Component))
        {
			auto PrimComp = Cast<UPrimitiveComponent>(SceneComp);
			return GetPhysicalMaterialFromPrimitive(PrimComp);
        }
        return nullptr;
	}
}

// ===== FISMFeedbackParticipant Static Constructors =====

FISMFeedbackParticipant FISMFeedbackParticipant::FromISMComponent(const UISMRuntimeComponent* ISMComp, int32 InstanceIndex)
{
    FISMFeedbackParticipant Participant;
    
    if (!ISMComp)
    {
        return Participant;
    }
    
    // Set component and actor references
    Participant.ParticipantComponent = const_cast<UISMRuntimeComponent*>(ISMComp);
    Participant.Participant = ISMComp->GetOwner();
    
    // Get transform - either instance-specific or actor transform
    if (InstanceIndex != INDEX_NONE && ISMComp->IsValidInstanceIndex(InstanceIndex))
    {
        Participant.ParticipantTransform = ISMComp->GetInstanceTransform(InstanceIndex);
    }
    else if (Participant.Participant.IsValid())
    {
        Participant.ParticipantTransform = Participant.Participant->GetActorTransform();
    }
    
    // Get tags from component
    Participant.ParticipantTags = ISMComp->ISMComponentTags;
    
    // Add instance-specific tags if available
    if (InstanceIndex != INDEX_NONE)
    {
        FGameplayTagContainer InstanceTags = ISMComp->GetInstanceTags(InstanceIndex);
        Participant.ParticipantTags.AppendTags(InstanceTags);
    }
    
    // Get physical material from managed ISM component
    if (ISMComp->ManagedISMComponent)
        Participant.ParticipantPhysicalMaterial = GetPhysicalMaterialFromPrimitive(ISMComp->ManagedISMComponent);
    
    return Participant;
}

FISMFeedbackParticipant FISMFeedbackParticipant::FromActorComponent(const UActorComponent* ActorComp)
{
    FISMFeedbackParticipant Participant;
    
    if (!ActorComp)
    {
        return Participant;
    }
    
    // Set component and actor references
    Participant.ParticipantComponent = const_cast<UActorComponent*>(ActorComp);
    Participant.Participant = ActorComp->GetOwner();
    
    // Get transform - try SceneComponent first, fallback to actor transform
    if (const USceneComponent* SceneComp = Cast<USceneComponent>(ActorComp))
    {
        Participant.ParticipantTransform = SceneComp->GetComponentTransform();
        
        // Try to get physical material from PrimitiveComponent
        if (const UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(SceneComp))
        {
            Participant.ParticipantPhysicalMaterial = PrimComp->GetBodyInstance()
                ? PrimComp->GetBodyInstance()->GetSimplePhysicalMaterial()
                : nullptr;
        }
    }
    else if (Participant.Participant.IsValid())
    {
        Participant.ParticipantTransform = Participant.Participant->GetActorTransform();
    }
    
    // Note: ActorComponent doesn't have native tag support
    // Subclasses can populate ParticipantTags manually if needed
    
    return Participant;
}

// ===== FISMFeedbackContext Static Constructors =====

TArray<FTransform> FISMFeedbackContext::GetTransformsForBatchedInstances() const
{
    if (!IsBatched())
        return TArray<FTransform>();

	UISMRuntimeComponent* ISMComp = GetISMComponentFromSubject();
    if(!ISMComp)
		return TArray<FTransform>();
    
    auto res = TArray<FTransform>();
    res.Reserve(BatchedInstanceIndices.Num());

	for (int i = 0; i < BatchedInstanceIndices.Num(); i++)
    {
		res.Add(ISMComp->GetInstanceTransform(BatchedInstanceIndices[i]));
    }

    return res;
}

TArray<FISMFeedbackBatchedInstanceInfo> FISMFeedbackContext::GetBatchedInstanceInfo(bool bWithGameplayTags, int customDataIndexes) const
{
    if (!IsBatched())
        return TArray<FISMFeedbackBatchedInstanceInfo>();

	UISMRuntimeComponent* ISMComp = GetISMComponentFromSubject();
    if(!ISMComp)
		return TArray<FISMFeedbackBatchedInstanceInfo>();
    
    auto res = TArray<FISMFeedbackBatchedInstanceInfo>();
    res.Reserve(BatchedInstanceIndices.Num());

    for (int i = 0; i < BatchedInstanceIndices.Num(); i++)
    {
		res.Add(FISMFeedbackBatchedInstanceInfo::GetInstanceInfo(ISMComp, BatchedInstanceIndices[i], bWithGameplayTags, customDataIndexes));
    }
    return res;
}

FISMFeedbackBatchedInstanceInfo FISMFeedbackBatchedInstanceInfo::GetInstanceInfo(const UISMRuntimeComponent* ISMComp, int32 InstanceIndex, bool bWithTags, int customDataIndexes)
{
    if (!ISMComp)
        return FISMFeedbackBatchedInstanceInfo();
    if (ISMComp->IsValidInstanceIndex(InstanceIndex) == false)
        return FISMFeedbackBatchedInstanceInfo();

    auto info = FISMFeedbackBatchedInstanceInfo();
    info.InstanceIndex = InstanceIndex;
    info.InstanceTransform = ISMComp->GetInstanceTransform(InstanceIndex);
    if (bWithTags)
        info.InstanceTags = ISMComp->GetInstanceTags(InstanceIndex);
    if (customDataIndexes > 0)
    {
		info.PerInstanceCustomData.Reserve(customDataIndexes);
        for (int i = 0; i < customDataIndexes; i++)
        {
            info.PerInstanceCustomData.Add(ISMComp->GetInstanceCustomDataValue(InstanceIndex, i));
        }
	}

    return info;
}

UISMRuntimeComponent* FISMFeedbackContext::GetISMComponentFromSubject() const
{
    if(!Subject.IsValid())
        return nullptr;
	return Cast<UISMRuntimeComponent>(Subject.ParticipantComponent.Get());
}

FISMFeedbackContext FISMFeedbackContext::CreateFromInstance(
    FGameplayTag FeedbackTag, 
    const UISMRuntimeComponent* ISMComp, 
    int32 InstanceIndex)
{
    FISMFeedbackContext Context;
    
    if (!ISMComp)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateFromInstance: ISMComp is null"));
        return Context;
    }
    
    if (!ISMComp->ManagedISMComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateFromInstance: ManagedISMComponent is null"));
        return Context;
    }
    
    if (!ISMComp->IsValidInstanceIndex(InstanceIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateFromInstance: Invalid instance index %d"), InstanceIndex);
        return Context;
    }
    
    // Set primary identification
    Context.FeedbackTag = FeedbackTag;
    
    // Create subject participant from ISM component
    Context.Subject = FISMFeedbackParticipant::FromISMComponent(ISMComp, InstanceIndex);
    
    // Populate spatial data from instance transform
    FTransform InstanceTransform = ISMComp->GetInstanceTransform(InstanceIndex);
    Context.Location = InstanceTransform.GetLocation();
    Context.Rotation = InstanceTransform.Rotator();
    Context.Scale = InstanceTransform.GetScale3D().X; // Assume uniform scale
    
    // Copy instance tags to context tags
    Context.ContextTags = ISMComp->GetInstanceTags(InstanceIndex);
    
    // Set static mesh reference
    if (ISMComp->ManagedISMComponent)
    {
        Context.StaticMesh = ISMComp->ManagedISMComponent->GetStaticMesh();
    }
    
    // Set physical material from managed ISM
    if (ISMComp->ManagedISMComponent)
    {
		Context.PhysicalMaterial = Context.Subject.ParticipantPhysicalMaterial.Get();
    }
    
    return Context;
}


FISMFeedbackContext FISMFeedbackContext::CreateFromInstanceBatched(
    FGameplayTag FeedbackTag,
    const UISMRuntimeComponent* ISMComp,
    TArray<int32> InstanceIndexes)
{
	if (InstanceIndexes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateFromInstanceBatched: InstanceIndexes array is empty"));
        return FISMFeedbackContext();
    }
	return FISMFeedbackContext::CreateFromInstance(FeedbackTag, ISMComp, InstanceIndexes[0]).WithBatchedIndexes(InstanceIndexes);
}

FISMFeedbackContext FISMFeedbackContext::CreateFromHitResult(
    FGameplayTag FeedbackTag,
    const UActorComponent* InstigatorComp,
    const FHitResult& HitResult)
{
    FISMFeedbackContext Context;
    
    // Set primary identification
    Context.FeedbackTag = FeedbackTag;
    
    // Create instigator participant if provided
    if (InstigatorComp)
    {
        Context.Instigator = FISMFeedbackParticipant::FromActorComponent(InstigatorComp);
    }
    
    // Create subject participant from hit component
    if (HitResult.Component.IsValid())
    {
        Context.Subject = FISMFeedbackParticipant::FromActorComponent(HitResult.Component.Get());
    }
    
    // Populate spatial data from hit result
    Context.Location = HitResult.ImpactPoint;
    Context.Normal = HitResult.ImpactNormal;
    
    // Calculate velocity if we have impact normal and a moving instigator
    if (InstigatorComp)
    {
        if (const USceneComponent* SceneComp = Cast<USceneComponent>(InstigatorComp))
        {
            Context.Velocity = SceneComp->GetComponentVelocity();
        }
    }
    
    // Set physical material from hit
    if (HitResult.PhysMaterial.IsValid())
    {
        Context.PhysicalMaterial = HitResult.PhysMaterial.Get();
        
        // Also set on subject participant
        Context.Subject.ParticipantPhysicalMaterial = HitResult.PhysMaterial.Get();
    }
    
    // Set static mesh if we hit a static mesh component
    if (const UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(HitResult.Component.Get()))
    {
        Context.StaticMesh = StaticMeshComp->GetStaticMesh();
    }
    
    // Calculate impact intensity from velocity (if available)
    if (Context.Velocity.SizeSquared() > 0.0f)
    {
        // Simple intensity calculation based on velocity magnitude
        // Projects should customize this based on their needs
        const float MaxExpectedVelocity = 1000.0f; // 10 m/s
        Context.Intensity = FMath::Clamp(Context.Velocity.Size() / MaxExpectedVelocity, 0.0f, 1.0f);
    }
    
    return Context;
}


