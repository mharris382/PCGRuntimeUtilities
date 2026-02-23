// ISMInteractionComponent.cpp
#include "ISMInteractionComponent.h"

#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "ISMPreviewContext.h"
#include "ISMSelectionSet.h"
#include "Interfaces/ISMPickupInterface.h"
#include "Interfaces/ISMInteractable.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"

#include "GameFramework/Actor.h"
#include "Logging/LogMacros.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY(LogISMRuntimeInteraction);

// ============================================================
//  Constructor
// ============================================================

UISMInteractionComponent::UISMInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

// ============================================================
//  Lifecycle
// ============================================================

void UISMInteractionComponent::BeginPlay()
{
    Super::BeginPlay();

    // Cache subsystem
    if (UWorld* World = GetWorld())
    {
        CachedSubsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    }

    // Create a default selection set if one wasn't assigned
    if (!SelectionSet)
    {
        SelectionSet = NewObject<UISMSelectionSet>(GetOwner(),
            UISMSelectionSet::StaticClass(), TEXT("ISMSelectionSet_Default"));
        SelectionSet->RegisterComponent();
    }
}

void UISMInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Release any carried object cleanly before we go away
    if (IsCarrying())
    {
        ReleaseCarried(false);
    }

    // Cancel any active preview
    if (ActivePreview && ActivePreview->IsPending())
    {
        ActivePreview->Cancel();
    }

    ClearFocus();

    Super::EndPlay(EndPlayReason);
}

void UISMInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Skip targeting while carrying — the focused instance isn't meaningful
    // while hands are full. Games can override this by subclassing.
    if (IsCarrying())
    {
        return;
    }

    FISMInstanceHandle NewTarget = ResolveTargetThisFrame();

    if (NewTarget != FocusedHandle)
    {
        SetFocusedHandle(NewTarget);
    }
}

// ============================================================
//  Input Notifications
// ============================================================

void UISMInteractionComponent::NotifyInteractPressed()
{
    if (!FocusedHandle.IsValid())
    {
        return;
    }

    // Guard: don't interact with instances mid-conversion
    if (UISMRuntimeComponent* Comp = FocusedHandle.Component.Get())
    {
        if (Comp->IsInstanceInState(FocusedHandle.InstanceIndex, EISMInstanceState::Converting))
        {
            return;
        }
    }

    if (bSelectOnInteract)
    {
        NotifySelectPressed();
        return;
    }

    OnInteractPressed.Broadcast(FocusedHandle);

    // Forward to IISMInteractable if the component implements it
    if (UISMRuntimeComponent* Comp = FocusedHandle.Component.Get())
    {
        if (Comp->GetClass()->ImplementsInterface(UISMInteractable::StaticClass()))
        {
            IISMInteractable::Execute_OnInteract(Comp, FocusedHandle.InstanceIndex, GetOwner());
        }
    }
}

void UISMInteractionComponent::NotifyPickupPressed()
{
    // If already carrying, pressing pickup again does nothing (must release first)
    if (IsCarrying())
    {
        return;
    }

    if (!FocusedHandle.IsValid())
    {
        return;
    }

    // Respect ownership — don't pick up instances already possessed by someone else
    if (FocusedHandle.IsPossessed())
    {
        AActor* Owner = GetOwner();
        if (!FocusedHandle.IsPossessedBy(Owner))
        {
            // Instance is possessed by a different actor — refuse pickup
            return;
        }
    }

    InitiatePickup(FocusedHandle);
}

void UISMInteractionComponent::NotifyPickupReleased(bool bThrow)
{
    if (!IsCarrying())
    {
        return;
    }

    ReleaseCarried(bThrow);
}

void UISMInteractionComponent::NotifySelectPressed()
{
    if (!FocusedHandle.IsValid() || !SelectionSet)
    {
        return;
    }

    SelectionSet->SelectInstance(FocusedHandle, EISMSelectionMode::Toggle);
}

// ============================================================
//  Preview API
// ============================================================

UISMPreviewContext* UISMInteractionComponent::BeginDestroyPreview()
{
    if (!SelectionSet || SelectionSet->IsEmpty())
    {
        return nullptr;
    }

    // Only one preview at a time
    if (ActivePreview && ActivePreview->IsPending())
    {
        return ActivePreview;
    }

    UISMPreviewContext* Preview = NewObject<UISMPreviewContext>(GetOwner());
    Preview->PreviewActorClass = DefaultPreviewActorClass;

    if (Preview->BeginPreview(SelectionSet, EISMPreviewType::Destroy))
    {
        ActivePreview = Preview;
        return ActivePreview;
    }

    return nullptr;
}

UISMPreviewContext* UISMInteractionComponent::BeginPlacementPreview(
    UISMRuntimeComponent* TargetComponent, const FTransform& InitialTransform)
{
    if (!TargetComponent)
    {
        return nullptr;
    }

    if (ActivePreview && ActivePreview->IsPending())
    {
        return ActivePreview;
    }

    UISMPreviewContext* Preview = NewObject<UISMPreviewContext>(GetOwner());
    Preview->PreviewActorClass = DefaultPreviewActorClass;

    if (Preview->BeginPlacementPreview(TargetComponent, InitialTransform))
    {
        ActivePreview = Preview;
        return ActivePreview;
    }

    return nullptr;
}

// ============================================================
//  Internal Targeting
// ============================================================


FISMInstanceHandle UISMInteractionComponent::ResolveTargetThisFrame() const
{
    switch (TargetingMode)
    {
    case EISMTargetingMode::Raycast:      return ResolveByRaycast();
    case EISMTargetingMode::SphereOverlap: return ResolveBySphereOverlap();
    default:                               return FISMInstanceHandle();
    }
}

FISMInstanceHandle UISMInteractionComponent::ResolveByRaycast() const
{
    auto Fail = [&](FString msg) -> FISMInstanceHandle
        {
            UE_LOG(LogISMRuntimeInteraction, Verbose,
                TEXT("UISMInteractionComponent::ResolveByRaycast(): Raycast target failed: %s"), *msg);
            return FISMInstanceHandle();
        };

    UWorld* World = GetWorld();
    if (!World)
    {
        return Fail(TEXT("no world"));
    }

    UISMRuntimeSubsystem* Subsystem = CachedSubsystem.Get();
    if (!Subsystem)
    {
        return Fail(TEXT("no subsystem"));
    }

    const FVector Start = GetComponentLocation();
    const FVector End = Start + GetForwardVector() * InteractionRange;

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());

    if (!World->LineTraceSingleByChannel(Hit, Start, End, TraceChannel, Params))
    {
        return Fail(TEXT("no hit"));
    }

    if (!Hit.Component.IsValid())
    {
        return Fail(TEXT("hit invalid component"));
    }

    // -------------------------------------------------------
    // Pass 1: did we hit a converted ISM actor?
    // Converted instances have their ISM hidden — the raycast
    // hits the actor mesh instead, so we resolve via the handle
    // the actor carries rather than the ISM component.
    // -------------------------------------------------------
    if (AActor* HitActor = Hit.GetActor())
    {
        if (HitActor->GetClass()->ImplementsInterface(UISMPickupInterface::StaticClass()))
        {
            FISMInstanceHandle ActorHandle =
                IISMPickupInterface::Execute_GetSourceHandle(HitActor);

            if (ActorHandle.IsValid())
            {
                UE_LOG(LogISMRuntimeInteraction, Verbose,
                    TEXT("UISMInteractionComponent::ResolveByRaycast(): "
                        "Resolved via converted actor handle (index %d)"),
                    ActorHandle.InstanceIndex);
                return ActorHandle;
            }
        }
    }

    // -------------------------------------------------------
    // Pass 2: did we hit an ISM instance directly?
    // -------------------------------------------------------
    UInstancedStaticMeshComponent* HitISM = Cast<UInstancedStaticMeshComponent>(Hit.Component.Get());
    if (!HitISM || Hit.Item == INDEX_NONE)
    {
        return Fail(TEXT("hit non-ISM or invalid instance index"));
    }

    UISMRuntimeComponent* OwningRuntime = Subsystem->FindComponentForISM(HitISM);
    if (!OwningRuntime)
    {
        return Fail(TEXT("no owning runtime component"));
    }

    FISMInstanceHandle Candidate = OwningRuntime->GetInstanceHandle(Hit.Item);
    if (!Candidate.IsValid())
    {
        return Fail(TEXT("invalid instance handle"));
    }

    // Tag filter check — instances possessed by others can be excluded
    if (TargetFilter.RequiredTags.IsValid() || TargetFilter.ExcludedTags.IsValid())
    {
        FGameplayTagContainer InstanceTags = OwningRuntime->GetInstanceTags(Hit.Item);
        if (!TargetFilter.RequiredTags.IsEmpty() && !InstanceTags.HasAll(TargetFilter.RequiredTags))
        {
            return Fail(TEXT("instance missing required tags"));
        }
        if (!TargetFilter.ExcludedTags.IsEmpty() && InstanceTags.HasAny(TargetFilter.ExcludedTags))
        {
            return Fail(TEXT("instance has excluded tags"));
        }
    }

    return Candidate;
}

FISMInstanceHandle UISMInteractionComponent::ResolveBySphereOverlap() const
{
    auto Fail = [&](FString msg) -> FISMInstanceHandle
        {
            UE_LOG(LogISMRuntimeInteraction, Verbose, TEXT("UISMInteractionComponent::ResolveBySphereOverlap(): Raycast target failed: %s"), *msg);
            return FISMInstanceHandle();
        };

    UISMRuntimeSubsystem* Subsystem = CachedSubsystem.Get();
    if (!Subsystem)
    {
		return Fail(TEXT("no subsystem"));
    }

    const FVector Origin = GetComponentLocation();

    // Delegate to the subsystem's radius query — it searches all registered components
    TArray<FISMInstanceHandle> Candidates =
        Subsystem->QueryInstancesInRadius(Origin, InteractionRange, TargetFilter);

    if (Candidates.IsEmpty())
    {
		return Fail(TEXT("no candidates found"));
    }

    // Pick the nearest valid candidate that passes ownership check
    FISMInstanceHandle Best;
    float BestDistSq = FLT_MAX;

    for (const FISMInstanceHandle& Candidate : Candidates)
    {
        if (!Candidate.IsValid())
        {
            continue;
        }

        // Skip instances possessed by a different actor
        if (Candidate.IsPossessed() && !Candidate.IsPossessedBy(GetOwner()))
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(Origin, Candidate.GetLocation());
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            Best = Candidate;
        }
    }

    return Best;
}

// ============================================================
//  Focus Management
// ============================================================

void UISMInteractionComponent::SetFocusedHandle(const FISMInstanceHandle& NewHandle)
{
    // No change
    if (NewHandle == FocusedHandle)
    {
        return;
    }

    // Remove focus tag from previous instance
    if (FocusedHandle.IsValid() && FocusTag.IsValid())
    {
        if (UISMRuntimeComponent* Comp = FocusedHandle.Component.Get())
        {
            Comp->RemoveInstanceTag(FocusedHandle.InstanceIndex, FocusTag);
        }
    }

    const FISMInstanceHandle Previous = FocusedHandle;
    FocusedHandle = NewHandle;

    if (FocusedHandle.IsValid())
    {
        // Write focus tag to new instance
        if (FocusTag.IsValid())
        {
            if (UISMRuntimeComponent* Comp = FocusedHandle.Component.Get())
            {
                Comp->AddInstanceTag(FocusedHandle.InstanceIndex, FocusTag);
            }
        }

        OnInstanceFocused.Broadcast(FocusedHandle, Previous);
    }
    else
    {
        // Focus fully lost
        if (Previous.IsValid())
        {
            OnInstanceFocusLost.Broadcast(Previous);
        }
    }
}

void UISMInteractionComponent::ClearFocus()
{
    SetFocusedHandle(FISMInstanceHandle());
}

// ============================================================
//  Pickup / Release
// ============================================================

void UISMInteractionComponent::InitiatePickup(const FISMInstanceHandle& Handle)
{
    if (!Handle.IsValid())
    {
        return;
    }

    UISMRuntimeComponent* Comp = Handle.Component.Get();
    if (!Comp)
    {
        return;
    }

    // Guard: already converting
    if (Comp->IsInstanceInState(Handle.InstanceIndex, EISMInstanceState::Converting))
    {
        return;
    }

    // Check the converted actor can actually be picked up before we do anything
    // If already converted (e.g. from a prior partial interaction), check CanPickUp
    if (AActor* Existing = Handle.GetConvertedActor())
    {
        if (Existing->GetClass()->ImplementsInterface(UISMPickupInterface::StaticClass()))
        {
            if (!IISMPickupInterface::Execute_CanPickUp(Existing, GetOwner(), this))
            {
                return;
            }
        }
    }

    // Build conversion context
    FISMConversionContext ConversionContext;
    ConversionContext.Reason = EISMConversionReason::Interaction;
    ConversionContext.Instigator = GetOwner();
    ConversionContext.ImpactPoint = GetComponentLocation();

    // Convert the instance — handle manages actor lifetime
    // We need a mutable handle reference from the component
    FISMInstanceHandle& MutableHandle = Comp->GetOrCreateHandle(Handle.InstanceIndex);
    AActor* ConvertedActor = MutableHandle.ConvertToActor(ConversionContext);

    if (!ConvertedActor)
    {
        return;
    }

    // Write possession onto the handle — tag identifies this pawn/controller
    // Use the owner's class name as a fallback tag if no possession tag is configured.
    // Games should override PossessionTag to something meaningful.
    FGameplayTag PossessionTag = PossessionTagOverride.IsValid()
        ? PossessionTagOverride
        : FGameplayTag::RequestGameplayTag(FName("ISM.Possession.Default"));

    MutableHandle.SetPossessor(PossessionTag, GetOwner());

    // Store carried state
    CarriedHandle = MutableHandle;
    CarriedActor = ConvertedActor;

    // If Attached mode, set attachment data on the handle
    if (DefaultHoldMode == EISMPickupHoldMode::Attached && CarryAttachSocket != NAME_None)
    {
        if (USceneComponent* AttachRoot = GetOwner()->GetRootComponent())
        {
            MutableHandle.SetAttachment(AttachRoot, CarryAttachSocket);
        }
    }

    // Notify the actor it has been picked up — it configures its own physics
    if (ConvertedActor->GetClass()->ImplementsInterface(UISMPickupInterface::StaticClass()))
    {
        FISMPickupContext PickupContext;
        PickupContext.Instigator = GetOwner();
        PickupContext.AttachSocket = CarryAttachSocket;
        PickupContext.HoldMode = DefaultHoldMode;
        PickupContext.SourceHandle = CarriedHandle;

        IISMPickupInterface::Execute_OnPickedUp(ConvertedActor, PickupContext);
    }

    // Broadcast so game systems can react (inventory UI, carry component setup, etc.)
    OnPickupInitiated.Broadcast(CarriedHandle, ConvertedActor);

    // Clear focus — hands are full
    ClearFocus();
}

void UISMInteractionComponent::ReleaseCarried(bool bThrow)
{
    if (!IsCarrying())
    {
        return;
    }

    AActor* Actor = CarriedActor.Get();

    // Build the release context
    FISMReleaseContext ReleaseContext;
    ReleaseContext.Instigator = GetOwner();
    ReleaseContext.bReturnToISMOnRelease = bReturnToISMOnDrop;

    if (Actor)
    {
        ReleaseContext.ReleaseTransform = Actor->GetActorTransform();
    }

    if (bThrow)
    {
        // Inherit owner velocity as throw velocity
        if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
        {
            if (UPawnMovementComponent* MoveComp =
                OwnerPawn->FindComponentByClass<UPawnMovementComponent>())
            {
                ReleaseContext.ReleaseVelocity = MoveComp->Velocity;
            }
        }
    }

    // Clear attachment if one was set
    if (CarriedHandle.IsAttached())
    {
        // Get mutable handle from component to clear attachment state
        if (UISMRuntimeComponent* Comp = CarriedHandle.Component.Get())
        {
            FISMInstanceHandle& MutableHandle = Comp->GetOrCreateHandle(CarriedHandle.InstanceIndex);
            MutableHandle.ClearAttachment();
        }
    }

    // Notify the actor — it handles its own physics re-enable, socket detach, etc.
    if (Actor && Actor->GetClass()->ImplementsInterface(UISMPickupInterface::StaticClass()))
    {
        if (bThrow && !ReleaseContext.ReleaseVelocity.IsNearlyZero())
        {
            IISMPickupInterface::Execute_OnThrown(Actor, ReleaseContext);
        }
        else
        {
            IISMPickupInterface::Execute_OnDropped(Actor, ReleaseContext);
        }
    }

    // Clear possession from the handle
    if (UISMRuntimeComponent* Comp = CarriedHandle.Component.Get())
    {
        FISMInstanceHandle& MutableHandle = Comp->GetOrCreateHandle(CarriedHandle.InstanceIndex);
        MutableHandle.ClearPossessor();
    }

    // Broadcast before we clear state so listeners have valid handle data
    OnReleased.Broadcast(CarriedHandle, ReleaseContext);

    // Optionally return to ISM
    if (ReleaseContext.bReturnToISMOnRelease)
    {
        if (UISMRuntimeComponent* Comp = CarriedHandle.Component.Get())
        {
            FISMInstanceHandle& MutableHandle = Comp->GetOrCreateHandle(CarriedHandle.InstanceIndex);
            MutableHandle.ReturnToISM(true, true);
        }
    }

    // Clear local carried state
    CarriedHandle = FISMInstanceHandle();
    CarriedActor.Reset();
}