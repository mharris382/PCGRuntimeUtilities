// ISMPickupInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISMInstanceHandle.h"
#include "Components/SceneComponent.h"
#include "ISMPickupInterface.generated.h"

/**
 * How the pickup is being held.
 * Determines physics behavior while carried.
 */
UENUM(BlueprintType)
enum class EISMPickupHoldMode : uint8
{
    /** Actor is kinematically held at a fixed offset from the holder */
    Kinematic,

    /** Actor is held via PhysicsHandle - retains physics, can collide while carried */
    Physics,

    /** Actor is attached to a socket on the holder (useful for VR controller attachment) */
    Attached
};

/**
 * Describes a pickup event - passed to the interface on pickup/drop/throw.
 * Keeps the interface methods thin while carrying all relevant context.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMPickupContext
{
    GENERATED_BODY()

    /** The actor performing the pickup (player, NPC, VR hand) */
    UPROPERTY(BlueprintReadWrite, Category = "Pickup")
    AActor* Instigator = nullptr;

    /** The actor performing the pickup (player, NPC, VR hand) */
    UPROPERTY(BlueprintReadWrite, Category = "Pickup")
    USceneComponent* InstigatorComponent = nullptr;

    /** Socket or attachment point name on the instigator (used for Attached hold mode) */
    UPROPERTY(BlueprintReadWrite, Category = "Pickup")
    FName AttachSocket = NAME_None;

    /** How the object will be held */
    UPROPERTY(BlueprintReadWrite, Category = "Pickup")
    EISMPickupHoldMode HoldMode = EISMPickupHoldMode::Kinematic;

    /** The ISM handle this actor was converted from. Valid for the duration of the carry. */
    UPROPERTY(BlueprintReadWrite, Category = "Pickup")
    FISMInstanceHandle SourceHandle;
};

/**
 * Describes a drop/throw event.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMReleaseContext
{
    GENERATED_BODY()

    /** The actor that was holding this object */
    UPROPERTY(BlueprintReadWrite, Category = "Release")
    AActor* Instigator = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Release")
	USceneComponent* InstigatorComponent = nullptr;

    /** World transform at the moment of release */
    UPROPERTY(BlueprintReadWrite, Category = "Release")
    FTransform ReleaseTransform;

    /** Velocity to apply on release (zero for a simple drop) */
    UPROPERTY(BlueprintReadWrite, Category = "Release")
    FVector ReleaseVelocity = FVector::ZeroVector;

    /** Angular velocity to apply on release */
    UPROPERTY(BlueprintReadWrite, Category = "Release")
    FVector ReleaseAngularVelocity = FVector::ZeroVector;

    /**
     * If true, the interaction module will attempt to return this actor
     * to ISM representation after release.
     * If false, the actor persists as a world actor (game handles its lifetime).
     */
    UPROPERTY(BlueprintReadWrite, Category = "Release")
    bool bReturnToISMOnRelease = false;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UISMPickupInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface implemented on actors that were converted from ISM instances
 * and can be picked up, carried, and released.
 *
 * This is the handoff contract between ISMRuntimeInteraction and the game's
 * carry/inventory/VR grip system. The module fires these events; the game
 * decides what to do with the actor while it is held.
 *
 * The interface is intentionally thin — it does not implement carry logic,
 * physics handles, or VR grip. Those are the game's responsibility.
 *
 * Implement on: AISMPhysicsActor subclasses, or any actor spawned by conversion.
 */
class ISMRUNTIMECORE_API IISMPickupInterface
{
    GENERATED_BODY()

public:
    /**
     * Called when this actor is picked up.
     * Implementor should configure physics state for the chosen hold mode
     * (e.g. disable gravity for kinematic, enable PhysicsHandle for physics hold).
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Pickup")
    void OnPickedUp(const FISMPickupContext& Context);
    virtual void OnPickedUp_Implementation(const FISMPickupContext& Context) {}

    /**
     * Called when this actor is dropped (zero release velocity).
     * Implementor should re-enable gravity and remove any carry constraints.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Pickup")
    void OnDropped(const FISMReleaseContext& Context);
    virtual void OnDropped_Implementation(const FISMReleaseContext& Context) {}

    /**
     * Called when this actor is thrown (non-zero release velocity).
     * Velocity is already in world space. Implementor applies it to their
     * physics component as appropriate.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Pickup")
    void OnThrown(const FISMReleaseContext& Context);
    virtual void OnThrown_Implementation(const FISMReleaseContext& Context) {}

    /**
     * Whether this actor can currently be picked up.
     * Allows the actor to refuse pickup based on its own state
     * (e.g. too heavy, already interacting, mid-destruction).
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Pickup")
    bool CanPickUp(AActor* Instigator, USceneComponent* InstigatorComponent) const;
    virtual bool CanPickUp_Implementation(AActor* Instigator, USceneComponent* InstigatorComponent) const { return true; }

    /**
     * Get the hold offset relative to the instigator's attach point.
     * Lets the actor define where it "wants" to be held rather than
     * having the carry system guess from the origin.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Pickup")
    FTransform GetHoldOffsetTransform() const;
    virtual FTransform GetHoldOffsetTransform_Implementation() const { return FTransform::Identity; }



    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Runtime|Pickup")
    FISMInstanceHandle GetSourceHandle() const;
    virtual FISMInstanceHandle GetSourceHandle_Implementation() const { return FISMInstanceHandle(); }
};