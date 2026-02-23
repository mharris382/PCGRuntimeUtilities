// ISMSimplePickup.cpp
#include "ISMSimplePickup.h"

#include "ISMInteractionComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "ISMQueryFilter.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

UISMSimplePickup::UISMSimplePickup()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    // Sane hold offset default — object appears 60cm in front, 10cm down
    KinematicHoldOffset.SetLocation(FVector(60.0f, 0.0f, -10.0f));
}

// ============================================================
//  Lifecycle
// ============================================================

void UISMSimplePickup::BeginPlay()
{
    Super::BeginPlay();

    // Create the inner interactor as a sibling component on the same owner
    Interactor = NewObject<UISMInteractionComponent>(GetOwner(),
        UISMInteractionComponent::StaticClass(), TEXT("ISMSimplePickup_Interactor"));

    // Attach it to this component so it inherits our world transform.
    // This means for VR the interactor origin is the controller tip automatically.
    Interactor->SetupAttachment(this);
    Interactor->RegisterComponent();

    // Forward our configuration
    Interactor->TargetingMode    = TargetingMode;
    Interactor->InteractionRange = PickupRange;
    Interactor->DefaultHoldMode  = HoldMode;
    Interactor->CarryAttachSocket = CarrySocket;
    Interactor->bReturnToISMOnDrop = bReturnToISMOnDrop;
    Interactor->FocusTag         = HoverTag;

    if (PossessionTag.IsValid())
    {
        Interactor->PossessionTagOverride = PossessionTag;
    }

    // Build tag filter
    if (!RequiredTags.IsEmpty())
    {
        Interactor->TargetFilter.RequiredTags = RequiredTags;
    }

    // Wire delegates — these are the only bridge between facade and inner interactor
    Interactor->OnPickupInitiated.AddDynamic(this, &UISMSimplePickup::HandlePickupInitiated);
    Interactor->OnReleased.AddDynamic(this,        &UISMSimplePickup::HandleReleased);
    Interactor->OnInstanceFocused.AddDynamic(this, &UISMSimplePickup::HandleFocused);
    Interactor->OnInstanceFocusLost.AddDynamic(this, &UISMSimplePickup::HandleFocusLost);
}

void UISMSimplePickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Drop cleanly — don't leave possessed handles dangling
    if (IsHolding())
    {
        NotifyDrop();
    }

    Super::EndPlay(EndPlayReason);
}

void UISMSimplePickup::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (IsHolding() && HoldMode == EISMPickupHoldMode::Kinematic)
    {
        TickKinematicHold();
    }
}

// ============================================================
//  Input API
// ============================================================

void UISMSimplePickup::NotifyPickup()
{
    if (!Interactor) return;

    // Already holding — ignore
    if (IsHolding()) return;

    Interactor->NotifyPickupPressed();
}

void UISMSimplePickup::NotifyDrop()
{
    if (!Interactor) return;
    if (!IsHolding()) return;

    Interactor->NotifyPickupReleased(false);
}

void UISMSimplePickup::NotifyThrow()
{
    if (!Interactor) return;
    if (!IsHolding()) return;

    Interactor->NotifyPickupReleased(true);
}

// ============================================================
//  Kinematic Hold Tick
// ============================================================

void UISMSimplePickup::TickKinematicHold() const
{
    AActor* Actor = CarriedActor.Get();
    if (!Actor) return;

    // Compose: this component's world transform * hold offset
    // This keeps the held object glued to the controller/hand in world space
    const FTransform WorldHoldTransform = KinematicHoldOffset * GetComponentTransform();

    // Apply hold offset from the carried actor itself if it implements the interface
    FTransform ActorHoldOffset = FTransform::Identity;
    if (Actor->GetClass()->ImplementsInterface(UISMPickupInterface::StaticClass()))
    {
        ActorHoldOffset = IISMPickupInterface::Execute_GetHoldOffsetTransform(Actor);
    }

    const FTransform FinalTransform = ActorHoldOffset * WorldHoldTransform;
    Actor->SetActorTransform(FinalTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

// ============================================================
//  Delegate Handlers
// ============================================================

void UISMSimplePickup::HandlePickupInitiated(const FISMInstanceHandle& Handle, AActor* Actor)
{
    CarriedHandle = Handle;
    CarriedActor  = Actor;

    OnPickedUp.Broadcast(Handle, Actor);
}

void UISMSimplePickup::HandleReleased(const FISMInstanceHandle& Handle,
    const FISMReleaseContext& ReleaseContext)
{
    AActor* Actor = CarriedActor.Get();

    if (ReleaseContext.ReleaseVelocity.IsNearlyZero())
    {
        OnDropped.Broadcast(Handle, Actor,
            Actor ? Actor->GetActorLocation() : ReleaseContext.ReleaseTransform.GetLocation());
    }
    else
    {
        OnThrown.Broadcast(Handle, Actor, ReleaseContext.ReleaseVelocity * ThrowMultiplier);
    }

    CarriedHandle = FISMInstanceHandle();
    CarriedActor.Reset();
}

void UISMSimplePickup::HandleFocused(const FISMInstanceHandle& NewFocus,
    const FISMInstanceHandle& PreviousFocus)
{
    FocusedHandle = NewFocus;
}

void UISMSimplePickup::HandleFocusLost(const FISMInstanceHandle& LostFocus)
{
    FocusedHandle = FISMInstanceHandle();
}