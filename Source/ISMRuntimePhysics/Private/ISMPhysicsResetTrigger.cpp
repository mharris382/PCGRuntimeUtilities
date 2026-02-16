#include "ISMPhysicsResetTrigger.h"
#include "ISMPhysicsActor.h"
#include "Components/BoxComponent.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceHandle.h"
#include "ISMRuntimeComponent.h"
#include "Particles/ParticleSystem.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

AISMPhysicsResetTrigger::AISMPhysicsResetTrigger()
{
    PrimaryActorTick.bCanEverTick = false;
    
    // Create trigger volume
    TriggerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerVolume"));
    RootComponent = TriggerVolume;
    
    // Configure for overlap detection
    TriggerVolume->SetBoxExtent(FVector(500.0f, 500.0f, 500.0f)); // Default 10m cube
    TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    TriggerVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    TriggerVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    TriggerVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
    TriggerVolume->SetGenerateOverlapEvents(true);
    
    // Bind overlap event
    TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &AISMPhysicsResetTrigger::OnTriggerBeginOverlap);
    
#if WITH_EDITORONLY_DATA
    // Make visible in editor
    TriggerVolume->bHiddenInGame = true;
    TriggerVolume->SetVisibility(true);
#endif
}

// ===== Overlap Events =====

void AISMPhysicsResetTrigger::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!OtherActor)
    {
        return;
    }
    
    // Check if this is a physics actor
    AISMPhysicsActor* PhysicsActor = Cast<AISMPhysicsActor>(OtherActor);
    if (!PhysicsActor)
    {
        return;
    }
    
    // Check if we should reset this actor
    if (!ShouldResetActor(PhysicsActor))
    {
        return;
    }
    
    // Reset the actor
    ResetPhysicsActor(PhysicsActor);
}

bool AISMPhysicsResetTrigger::ShouldResetActor(AActor* Actor) const
{
    if (!Actor)
    {
        return false;
    }
    
    // For physics actors, check their instance tags
    if (AISMPhysicsActor* PhysicsActor = Cast<AISMPhysicsActor>(Actor))
    {
        // Get the instance handle
        const FISMInstanceHandle& Handle = PhysicsActor->GetInstanceHandle();
        if (!Handle.IsValid() || !Handle.Component.IsValid())
        {
            return false;
        }
        
        // Get instance tags from component
        const FGameplayTagContainer InstanceTags = Handle.Component->GetInstanceTags(Handle.InstanceIndex);
        
        // Check required tags
        if (!RequiredTags.IsEmpty())
        {
            if (!InstanceTags.HasAll(RequiredTags))
            {
                return false;
            }
        }
        
        // Check excluded tags
        if (!ExcludedTags.IsEmpty())
        {
            if (InstanceTags.HasAny(ExcludedTags))
            {
                return false;
            }
        }
    }
    
    return true;
}

void AISMPhysicsResetTrigger::ResetPhysicsActor(AISMPhysicsActor* PhysicsActor)
{
    if (!PhysicsActor)
    {
        return;
    }
    
    const FVector ActorLocation = PhysicsActor->GetActorLocation();
    
    // Play feedback at current location (before reset)
    PlayResetFeedback(ActorLocation);
    
    // Return to ISM
    // The bUpdateTransformOnReset flag is handled by the actor's OnReturnedToPool implementation
    // For now, we always update transform - could extend ISMInstanceHandle to support this flag
    PhysicsActor->ReturnToISM();
    
    // Broadcast event
    OnActorReset.Broadcast(PhysicsActor);
    
    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsResetTrigger::ResetPhysicsActor - Reset actor %s at %s"),
        *PhysicsActor->GetName(), *ActorLocation.ToString());
}

void AISMPhysicsResetTrigger::PlayResetFeedback(const FVector& Location)
{
    // Spawn particle effect
    //if (ResetParticleEffect)
    //{
    //    UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ResetParticleEffect, Location);
    //}
    //
    //// Play sound
    //if (ResetSound)
    //{
    //    UGameplayStatics::PlaySoundAtLocation(this, ResetSound, Location);
    //}
}