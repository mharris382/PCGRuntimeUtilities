// ISMInteractionComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceHandle.h"
#include "ISMQueryFilter.h"
#include "Interfaces/ISMPickupInterface.h"
#include "ISMSelectionSet.h"
#include "Delegates/DelegateCombinations.h"
#include "ISMInteractionComponent.generated.h"

class UISMRuntimeComponent;
class UISMPreviewContext;
class UISMRuntimeSubsystem;

DECLARE_LOG_CATEGORY_EXTERN(LogISMRuntimeInteraction, Log, All);

// ---------------------------------------------------------------
//  Delegates
// ---------------------------------------------------------------

/** Fired when the focused (hovered/targeted) instance changes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnISMInstanceFocused, const FISMInstanceHandle&, NewFocus, const FISMInstanceHandle&, PreviousFocus);

/** Fired when focus is lost entirely */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnISMInstanceFocusLost, const FISMInstanceHandle&, LostFocus);

/** Fired when the player presses the interact input (game binds this) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnISMInteractPressed, const FISMInstanceHandle&, FocusedHandle);

/** Fired when the player presses the pickup input */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnISMPickupInitiated,const FISMInstanceHandle&, Handle,AActor*, ConvertedActor);

/** Fired when a carried actor is released */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnISMReleased, const FISMInstanceHandle&, Handle, const FISMReleaseContext&, ReleaseContext);

// ---------------------------------------------------------------
//  Targeting mode
// ---------------------------------------------------------------

/**
 * How this component finds the targeted instance each frame.
 * Attach the component to camera/controller for raycast, to a
 * motion controller tip for sphere overlap (VR).
 */
UENUM(BlueprintType)
enum class EISMTargetingMode : uint8
{
    /**
     * Single raycast from this component's location along its forward vector.
     * Standard first/third person targeting.
     */
    Raycast,

    /**
     * Sphere overlap at this component's location.
     * Nearest overlapping instance wins. Good for VR controller proximity.
     */
    SphereOverlap
};

/**
 * Scene component that provides ISM instance targeting, selection management,
 * and pickup/release orchestration.
 *
 * Attach to: camera boom, VR motion controller, or player pawn directly.
 * Being a SceneComponent (not ActorComponent) means the targeting origin and
 * direction can be positioned correctly for VR or third-person without code changes.
 *
 * WHAT THIS COMPONENT OWNS:
 *   - Per-frame instance targeting (raycast or sphere, resolved via spatial index)
 *   - Focused instance tracking + highlight tag writes
 *   - Selection set reference and selection input routing
 *   - Pickup initiation: converts focused instance, fires handoff delegates
 *   - Release/throw orchestration: applies velocity, optionally returns to ISM
 *   - Preview context creation for destroy/placement workflows
 *
 * WHAT THIS COMPONENT DOES NOT OWN:
 *   - Carry physics / PhysicsHandle (game responsibility)
 *   - VR grip implementation (game responsibility)
 *   - Inventory / crafting / resource logic (game responsibility)
 *   - Input bindings (caller invokes NotifyInteractPressed etc.)
 */
UCLASS(Blueprintable, ClassGroup = (ISMRuntime), meta = (BlueprintSpawnableComponent))
class ISMRUNTIMEINTERACTION_API UISMInteractionComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UISMInteractionComponent();

    // ===== Targeting Configuration =====

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Targeting")
    EISMTargetingMode TargetingMode = EISMTargetingMode::Raycast;

    /**
     * Maximum interaction distance (cm).
     * For Raycast: max trace length.
     * For SphereOverlap: sphere radius.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Targeting", meta = (ClampMin = "1.0"))
    float InteractionRange = 300.0f;

    /**
     * Filter applied every frame during targeting.
     * Use RequiredTags to limit interaction to specific instance types
     * (e.g. only interactable trees, not decorative foliage).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Targeting")
    FISMQueryFilter TargetFilter;

    /**
     * Trace channel used for the interaction raycast.
     * Only relevant for Raycast targeting mode.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Targeting")
    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

    /**
     * Tag written to the currently focused instance.
     * Drive your hover/highlight material from this tag.
     * Removed automatically when focus changes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Targeting")
    FGameplayTag FocusTag;

    // ===== Selection =====

    /**
     * The selection set this component manages.
     * If null, a default selection set is created automatically on BeginPlay.
     * Assign an external one to share selection state across components (e.g. split-screen).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Selection")
    UISMSelectionSet* SelectionSet;

    /**
     * If true, pressing the interact input on a focused instance
     * adds it to the selection set rather than firing OnInteractPressed.
     * Toggle this at runtime to switch between interact and select modes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Selection")
    bool bSelectOnInteract = false;

    // ===== Pickup Configuration =====

    /**
     * Hold mode applied when a pickup is initiated via NotifyPickupPressed.
     * Can be overridden per-pickup by the game before calling NotifyPickupPressed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Pickup")
    EISMPickupHoldMode DefaultHoldMode = EISMPickupHoldMode::Kinematic;

    /**
     * Socket name on the owner actor (or a child component) to attach
     * carried actors to. Used when DefaultHoldMode is Attached.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Pickup")
    FName CarryAttachSocket = NAME_None;

    /**
     * If true, dropping a carried object will attempt to return it to ISM
     * representation rather than leaving it as a world actor.
     * Individual releases can override this via FISMReleaseContext.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Pickup")
    bool bReturnToISMOnDrop = false;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Possession")
    FGameplayTag PossessionTagOverride;

    // ===== Preview Configuration =====

    /**
     * Default preview actor class for destroy/placement previews.
     * Can be overridden per-operation through UISMPreviewContext.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Interaction|Preview")
    TSubclassOf<AActor> DefaultPreviewActorClass;

    // ===== Lifecycle =====

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // ===== Input Notification API =====
    // The game calls these from its input bindings.
    // The component does not bind input itself — keeping it input-system agnostic
    // means it works with Enhanced Input, legacy input, and VR input equally.

    /** Call when the player presses the interact button */
    UFUNCTION(BlueprintCallable, Category = "ISM Interaction|Input")
    void NotifyInteractPressed();

    /** Call when the player presses the pickup button */
    UFUNCTION(BlueprintCallable, Category = "ISM Interaction|Input")
    void NotifyPickupPressed();

    /**
     * Call when the player releases the pickup button (drop).
     * @param bThrow    If true, applies owner velocity as throw velocity
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Interaction|Input")
    void NotifyPickupReleased(bool bThrow = false);

    /** Call when the player presses the select button */
    UFUNCTION(BlueprintCallable, Category = "ISM Interaction|Input")
    void NotifySelectPressed();

    // ===== State Queries =====

    UFUNCTION(BlueprintCallable, Category = "ISM Interaction")
    bool HasFocus() const { return FocusedHandle.IsValid(); }

    UFUNCTION(BlueprintCallable, Category = "ISM Interaction")
    const FISMInstanceHandle& GetFocusedHandle() const { return FocusedHandle; }

    UFUNCTION(BlueprintCallable, Category = "ISM Interaction")
    bool IsCarrying() const { return CarriedHandle.IsValid(); }

    UFUNCTION(BlueprintCallable, Category = "ISM Interaction")
    const FISMInstanceHandle& GetCarriedHandle() const { return CarriedHandle; }

    UFUNCTION(BlueprintCallable, Category = "ISM Interaction")
    AActor* GetCarriedActor() const { return CarriedActor.Get(); }

    UFUNCTION(BlueprintCallable, Category = "ISM Interaction")
    UISMSelectionSet* GetSelectionSet() const { return SelectionSet; }

    // ===== Preview API =====

    /**
     * Begin a destroy preview for the current selection.
     * Returns the created preview context. Caller confirms or cancels.
     * Returns null if selection is empty or a preview is already active.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Interaction|Preview")
    UISMPreviewContext* BeginDestroyPreview();

    /**
     * Begin a placement preview for a new instance.
     * @param TargetComponent   Component to add the reserved instance to
     * @param InitialTransform  Starting world transform
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Interaction|Preview")
    UISMPreviewContext* BeginPlacementPreview(
        UISMRuntimeComponent* TargetComponent,
        const FTransform& InitialTransform);

    /** Get the currently active preview context (null if none) */
    UFUNCTION(BlueprintCallable, Category = "ISM Interaction|Preview")
    UISMPreviewContext* GetActivePreview() const { return ActivePreview; }

    // ===== Events =====

    UPROPERTY(BlueprintAssignable, Category = "ISM Interaction|Events")
    FOnISMInstanceFocused OnInstanceFocused;

    UPROPERTY(BlueprintAssignable, Category = "ISM Interaction|Events")
    FOnISMInstanceFocusLost OnInstanceFocusLost;

    UPROPERTY(BlueprintAssignable, Category = "ISM Interaction|Events")
    FOnISMInteractPressed OnInteractPressed;

    UPROPERTY(BlueprintAssignable, Category = "ISM Interaction|Events")
    FOnISMPickupInitiated OnPickupInitiated;

    UPROPERTY(BlueprintAssignable, Category = "ISM Interaction|Events")
    FOnISMReleased OnReleased;

protected:



    // ===== Internal State =====

    /** Currently focused instance (hovered/targeted) */
    FISMInstanceHandle FocusedHandle;

    /** Currently carried instance handle */
    FISMInstanceHandle CarriedHandle;

    /** The actor currently being carried (weak — game owns the actor) */
    TWeakObjectPtr<AActor> CarriedActor;

    /** Active preview context (null when no preview is running) */
    UPROPERTY()
    UISMPreviewContext* ActivePreview = nullptr;

    /** Cached subsystem reference */
    TWeakObjectPtr<UISMRuntimeSubsystem> CachedSubsystem;

    // ===== Internal Targeting =====

    /**
     * Run the targeting query for this frame.
     * Returns the best candidate handle, or an invalid handle if nothing found.
     */
    FISMInstanceHandle ResolveTargetThisFrame() const;

    /** Raycast targeting implementation */
    FISMInstanceHandle ResolveByRaycast() const;

    /** Sphere overlap targeting implementation */
    FISMInstanceHandle ResolveBySphereOverlap() const;

    /**
     * Update the focused handle. Handles tag add/remove and delegate firing.
     * Safe to call with the same handle as current focus — no-ops cleanly.
     */
    void SetFocusedHandle(const FISMInstanceHandle& NewHandle);

    /** Clear focused handle and fire FocusLost delegate */
    void ClearFocus();

    // ===== Internal Pickup =====

    /**
     * Initiate carry of the given handle.
     * Converts to actor, fires OnPickupInitiated, calls IISMPickupInterface::OnPickedUp.
     */
    void InitiatePickup(const FISMInstanceHandle& Handle);

    /**
     * Release the currently carried actor.
     * Builds FISMReleaseContext, calls IISMPickupInterface::OnDropped or OnThrown,
     * fires OnReleased. Optionally returns actor to ISM.
     */
    void ReleaseCarried(bool bThrow);
};