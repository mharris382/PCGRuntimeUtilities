#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Delegates/DelegateCombinations.h"
#include "ISMPhysicsResetTrigger.generated.h"

// Forward declarations
class UBoxComponent;
class AISMPhysicsActor;

/**
 * Simple volume trigger that instantly returns physics actors to ISM.
 * 
 * Use cases:
 * - Water volumes (objects sink and reset)
 * - Kill zones (out of bounds cleanup)
 * - Boundary volumes (prevent physics objects escaping playable area)
 * - Reset zones (designer-placed cleanup volumes)
 * 
 * Usage:
 * 1. Place in level
 * 2. Scale box component to desired volume
 * 3. Done! Physics actors touching volume will return to ISM
 * 
 * Features:
 * - Instant return (no delay)
 * - Optional transform update (ISM instance moved to reset position or kept at original)
 * - Tag filtering (only affect specific physics actors)
 * - Visual feedback (optional particle/sound on reset)
 */
UCLASS(Blueprintable)
class ISMRUNTIMEPHYSICS_API AISMPhysicsResetTrigger : public AActor
{
    GENERATED_BODY()

public:
    AISMPhysicsResetTrigger();

    // ===== Components =====
    
    /**
     * Box collision component defining the reset volume.
     * Scale this to desired size.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UBoxComponent> TriggerVolume;

    // ===== Configuration =====
    
    /**
     * Update ISM instance transform to match actor's position when returning.
     * 
     * True: ISM instance moves to where actor entered volume (useful for water)
     * False: ISM instance stays at original position (useful for kill zones)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reset Trigger")
    bool bUpdateTransformOnReset = true;
    
    /**
     * Only reset physics actors with ALL of these tags.
     * Empty = affect all physics actors.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reset Trigger|Filtering")
    FGameplayTagContainer RequiredTags;
    
    /**
     * Never reset physics actors with ANY of these tags.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reset Trigger|Filtering")
    FGameplayTagContainer ExcludedTags;
    
    // ===== Visual Feedback =====
    
    /**
     * Particle effect to spawn when actor is reset.
     * Leave null for no effect.
     */
    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reset Trigger|Feedback")
    //TObjectPtr<UParticleSystem> ResetParticleEffect;
    
    /**
     * Sound to play when actor is reset.
     * Leave null for silent reset.
     */
    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reset Trigger|Feedback")
    //TObjectPtr<USoundBase> ResetSound;

protected:
    // ===== Overlap Events =====
    
    /**
     * Called when actor begins overlapping the trigger volume.
     */
    UFUNCTION()
    void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
    
    /**
     * Check if actor should be reset by this trigger.
     * 
     * @param Actor - Actor to check
     * @return True if actor should be reset
     */
    bool ShouldResetActor(AActor* Actor) const;
    
    /**
     * Perform the reset on a physics actor.
     * 
     * @param PhysicsActor - Actor to reset
     */
    void ResetPhysicsActor(AISMPhysicsActor* PhysicsActor);
    
    /**
     * Play visual/audio feedback.
     * 
     * @param Location - Where to spawn feedback
     */
    void PlayResetFeedback(const FVector& Location);

public:
    // ===== Events =====
    
    /**
     * Called when a physics actor is reset by this trigger.
     * 
     * @param PhysicsActor - The actor that was reset
     */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActorReset, AActor*, PhysicsActor);

    
    UPROPERTY(BlueprintAssignable, Category = "Reset Trigger|Events")
    FOnActorReset OnActorReset;

    // ===== Debug =====
    
#if WITH_EDITORONLY_DATA
    /** Show debug visualization in editor */
    UPROPERTY(EditAnywhere, Category = "Debug")
    bool bShowDebugVisualization = true;
#endif
};