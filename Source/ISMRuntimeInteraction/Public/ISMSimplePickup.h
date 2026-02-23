// ISMSimplePickup.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceHandle.h"
#include "ISMPickupInterface.h"
#include "ISMSimplePickup.generated.h"

class UISMInteractionComponent;
class UPhysicsHandleComponent;

// ---------------------------------------------------------------
//  Minimal delegates — only what a simple pickup caller needs
// ---------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSimpleISMPickedUp,
    const FISMInstanceHandle&, Handle, AActor*, Actor);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSimpleISMDropped,
    const FISMInstanceHandle&, Handle, AActor*, Actor, FVector, FinalLocation);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSimpleISMThrown,
    const FISMInstanceHandle&, Handle, AActor*, Actor, FVector, ThrowVelocity);

/**
 * Lightweight pickup component. Facade over UISMInteractionComponent.
 *
 * Drop onto any actor — player pawn, VR motion controller, AI hand bone — 
 * wire up the three input notifications and you have full pickup/carry/drop/throw.
 *
 * For VR: attach to the motion controller scene component so GetComponentLocation()
 * and GetForwardVector() automatically reflect controller pose. Set TargetingMode
 * to SphereOverlap for proximity-based grab.
 *
 * For AI: call NotifyPickup / NotifyDrop / NotifyThrow from your behavior tree
 * tasks or AnimNotifies instead of input bindings.
 *
 * All complexity (ownership, selection, preview, possession tags) is handled
 * internally. If you outgrow this component, access the inner interactor via
 * GetInteractor() and use it directly.
 */
UCLASS(Blueprintable, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMEINTERACTION_API UISMSimplePickup : public USceneComponent
{
    GENERATED_BODY()

public:
    UISMSimplePickup();

    // ===== Configuration =====

    /** How far to search for pickups (cm). Raycast length or sphere radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    float PickupRange = 250.0f;

    /**
     * Targeting mode forwarded to the internal interactor.
     * Raycast = standard FPS/TPS. SphereOverlap = VR proximity grab.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    EISMTargetingMode TargetingMode = EISMTargetingMode::Raycast;

    /**
     * How the object is held while carried.
     * Kinematic: locked to hold offset, no physics. Good for most cases.
     * Physics: held via PhysicsHandle, collides while carried. Good for VR.
     * Attached: welded to CarrySocket on the owner. Good for VR controller attachment.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    EISMPickupHoldMode HoldMode = EISMPickupHoldMode::Kinematic;

    /**
     * Socket on the owner actor to attach held objects to.
     * Only used when HoldMode = Attached.
     * For VR: set this to your grip socket name (e.g. "GripPoint").
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    FName CarrySocket = NAME_None;

    /**
     * Offset applied to the carried object relative to this component's transform.
     * Only used when HoldMode = Kinematic.
     * Lets you position the held object in front of the hand without code.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    FTransform KinematicHoldOffset;

    /**
     * If true, dropping returns the actor to ISM representation.
     * If false, the actor stays as a world actor after drop.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    bool bReturnToISMOnDrop = false;

    /**
     * Throw velocity multiplier applied to the owner's velocity on throw.
     * 1.0 = inherit velocity exactly. 2.0 = double it.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup",
        meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float ThrowMultiplier = 1.5f;

    /**
     * Tag filter — only instances with these tags can be picked up.
     * Leave empty to allow any ISM instance.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    FGameplayTagContainer RequiredTags;

    /**
     * Tag written to the hovered instance for highlight materials.
     * Leave invalid to skip highlight writing.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    FGameplayTag HoverTag;

    /**
     * Possession tag written to the handle when this component picks up an instance.
     * Defaults to "ISM.Possession.Default" if not set.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple Pickup")
    FGameplayTag PossessionTag;

    // ===== Lifecycle =====

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // ===== Input API =====
    // Call these from input bindings, BT tasks, or AnimNotifies.

    /** Attempt to pick up the currently focused instance. No-op if nothing focused. */
    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    void NotifyPickup();

    /** Drop the currently held object. No-op if not holding. */
    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    void NotifyDrop();

    /** Throw the currently held object with velocity. No-op if not holding. */
    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    void NotifyThrow();

    // ===== State Queries =====

    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    bool IsHolding() const { return CarriedActor.IsValid(); }

    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    bool HasFocus() const { return FocusedHandle.IsValid(); }

    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    AActor* GetHeldActor() const { return CarriedActor.Get(); }

    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    const FISMInstanceHandle& GetHeldHandle() const { return CarriedHandle; }

    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    const FISMInstanceHandle& GetFocusedHandle() const { return FocusedHandle; }

    /**
     * Access the internal interactor for advanced use.
     * Use this if you need selection sets, preview contexts, or ownership queries.
     */
    UFUNCTION(BlueprintCallable, Category = "Simple Pickup")
    UISMInteractionComponent* GetInteractor() const { return Interactor; }

    // ===== Events =====

    UPROPERTY(BlueprintAssignable, Category = "Simple Pickup|Events")
    FOnSimpleISMPickedUp OnPickedUp;

    UPROPERTY(BlueprintAssignable, Category = "Simple Pickup|Events")
    FOnSimpleISMDropped OnDropped;

    UPROPERTY(BlueprintAssignable, Category = "Simple Pickup|Events")
    FOnSimpleISMThrown OnThrown;

protected:

    // ===== Internal =====

    /** The inner interactor — owns all targeting and conversion logic */
    UPROPERTY()
    UISMInteractionComponent* Interactor = nullptr;

    /** Currently focused handle (mirrors Interactor's focused handle) */
    FISMInstanceHandle FocusedHandle;

    /** Currently carried handle */
    FISMInstanceHandle CarriedHandle;

    /** Weak ref to the carried actor for tick-driven kinematic update */
    TWeakObjectPtr<AActor> CarriedActor;

    /** Tick: move kinematically held actor to match this component's transform + offset */
    void TickKinematicHold() const;

    /** Called by interactor delegate when pickup succeeds */
    UFUNCTION()
    void HandlePickupInitiated(const FISMInstanceHandle& Handle, AActor* Actor);

    /** Called by interactor delegate when release occurs */
    UFUNCTION()
    void HandleReleased(const FISMInstanceHandle& Handle, const FISMReleaseContext& ReleaseContext);

    /** Called by interactor delegate when focus changes */
    UFUNCTION()
    void HandleFocused(const FISMInstanceHandle& NewFocus, const FISMInstanceHandle& PreviousFocus);

    /** Called by interactor delegate when focus is lost */
    UFUNCTION()
    void HandleFocusLost(const FISMInstanceHandle& LostFocus);
};