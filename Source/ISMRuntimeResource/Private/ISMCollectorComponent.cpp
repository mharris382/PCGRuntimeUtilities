#include "ISMCollectorComponent.h"
#include "ISMResourceComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UISMCollectorComponent::UISMCollectorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UISMCollectorComponent::BeginPlay()
{
    Super::BeginPlay();

    // Auto-detect camera if not set
    if (!CameraComponent && DetectionMode == ECollectionDetectionMode::Raycast)
    {
        CachedCamera = FindCameraComponent();
    }
    else if (CameraComponent)
    {
        CachedCamera = CameraComponent;
    }
}

void UISMCollectorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Run detection if enabled
    if (bAutoDetect && DetectionMode != ECollectionDetectionMode::Manual)
    {
        DetectionTimer += DeltaTime;

        if (DetectionTimer >= DetectionInterval)
        {
            DetectionTimer = 0.0f;
            RunDetection();
        }
    }
}

// ===== Input Integration =====

void UISMCollectorComponent::StartInteraction()
{
    bIsInteractionHeld = true;

    // If already collecting, do nothing
    if (bIsCollecting)
        return;

    // Attempt to start collection
    TryStartCollection();
}

void UISMCollectorComponent::StopInteraction()
{
    bIsInteractionHeld = false;

    // For Timed mode, cancel collection when button is released
    if (CollectionMode == ECollectionMode::Timed && bIsCollecting)
    {
        CancelCollection();
    }
}

// ===== Targeting API =====

bool UISMCollectorComponent::HasValidTarget() const
{
    return TargetedInstance.IsValid() && TargetedResourceComponent.IsValid();
}

void UISMCollectorComponent::SetTargetInstance(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent)
{
    UpdateTarget(Instance, ResourceComponent);
}

void UISMCollectorComponent::ClearTarget()
{
    UpdateTarget(FISMInstanceHandle(), nullptr);
}

// ===== Collection State =====

float UISMCollectorComponent::GetCollectionProgress() const
{
    if (!bIsCollecting || !TargetedResourceComponent.IsValid())
        return 0.0f;

    return TargetedResourceComponent->GetCollectionProgress(TargetedInstance.InstanceIndex);
}

void UISMCollectorComponent::CancelCollection()
{
    if (!bIsCollecting || !TargetedResourceComponent.IsValid())
        return;

    TargetedResourceComponent->CancelCollection(TargetedInstance.InstanceIndex);
    bIsCollecting = false;

    OnCollectionCancelledInternal(TargetedInstance, TargetedResourceComponent.Get());
}

// ===== Tag Management =====

void UISMCollectorComponent::AddCollectorTag(FGameplayTag Tag)
{
    if (!Tag.IsValid())
        return;

    CollectorTags.AddTag(Tag);
    OnCollectorTagsChangedInternal(CollectorTags);

    // Re-validate current target if tags changed
    if (HasValidTarget())
    {
        FText FailureReason;
        if (!CanCollectResource(TargetedInstance, TargetedResourceComponent.Get(), FailureReason))
        {
            // Target no longer valid, clear it
            ClearTarget();
        }
    }
}

void UISMCollectorComponent::RemoveCollectorTag(FGameplayTag Tag)
{
    if (!Tag.IsValid())
        return;

    CollectorTags.RemoveTag(Tag);
    OnCollectorTagsChangedInternal(CollectorTags);

    // Re-validate current target
    if (HasValidTarget())
    {
        FText FailureReason;
        if (!CanCollectResource(TargetedInstance, TargetedResourceComponent.Get(), FailureReason))
        {
            ClearTarget();
        }
    }
}

void UISMCollectorComponent::SetCollectorTags(const FGameplayTagContainer& NewTags)
{
    CollectorTags = NewTags;
    OnCollectorTagsChangedInternal(CollectorTags);

    // Re-validate current target
    if (HasValidTarget())
    {
        FText FailureReason;
        if (!CanCollectResource(TargetedInstance, TargetedResourceComponent.Get(), FailureReason))
        {
            ClearTarget();
        }
    }
}

// ===== Virtual Hook Implementations =====

FGameplayTagContainer UISMCollectorComponent::GetCollectorTags_Implementation() const
{
    return CollectorTags;
}

bool UISMCollectorComponent::CanCollectResource_Implementation(const FISMInstanceHandle& Instance,
    UISMResourceComponent* ResourceComp,
    FText& OutFailureReason) const
{
    if (!ResourceComp)
    {
        OutFailureReason = NSLOCTEXT("ISMCollector", "InvalidResource", "Invalid resource");
        return false;
    }

    // Get current collector tags (may be dynamic)
    FGameplayTagContainer CurrentCollectorTags = GetCollectorTags();

    // Check if resource requirements are met
    if (!ResourceComp->CanCollectorGatherInstance(CurrentCollectorTags, Instance.InstanceIndex, OutFailureReason))
    {
        return false;
    }

    return true;
}

float UISMCollectorComponent::GetCollectionSpeedMultiplier_Implementation(const FISMInstanceHandle& Instance,
    UISMResourceComponent* ResourceComp) const
{
    if (!ResourceComp)
        return 1.0f;

    // Get speed multiplier from resource component's tag-based modifiers
    FGameplayTagContainer CurrentCollectorTags = GetCollectorTags();
    return ResourceComp->CalculateSpeedMultiplier(CurrentCollectorTags);
}

bool UISMCollectorComponent::ShouldConsiderInstance_Implementation(const FISMInstanceHandle& Instance,
    UISMResourceComponent* ResourceComp) const
{
    if (!ResourceComp)
        return false;

    // Check if we have a target filter
    if (!TargetFilter.IsEmpty())
    {
        // Get combined tags (component + resource tags)
        FGameplayTagContainer InstanceTags = ResourceComp->ISMComponentTags;
        InstanceTags.AppendTags(ResourceComp->ResourceTags);

        // Check if instance matches our filter
        if (!TargetFilter.Matches(InstanceTags))
        {
            return false;
        }
    }

    // If we only want valid targets, pre-check requirements
    if (bOnlyDetectValidTargets)
    {
        FText FailureReason;
        return CanCollectResource(Instance, ResourceComp, FailureReason);
    }

    return true;
}

void UISMCollectorComponent::OnTargetChangedInternal_Implementation(const FISMInstanceHandle& NewTarget,
    UISMResourceComponent* ResourceComp)
{
    // Override in subclasses for highlighting, VFX, etc.
}

void UISMCollectorComponent::OnCollectionStartedInternal_Implementation(const FISMInstanceHandle& Instance,
    UISMResourceComponent* ResourceComp)
{
    // Override in subclasses for animations, VFX, etc.
}

void UISMCollectorComponent::OnCollectionProgressInternal_Implementation(const FISMInstanceHandle& Instance,
    UISMResourceComponent* ResourceComp,
    float Progress)
{
    // Override in subclasses for progress VFX, sounds, etc.
}

void UISMCollectorComponent::OnCollectionCompletedInternal_Implementation(const FISMInstanceHandle& Instance,
    UISMResourceComponent* ResourceComp)
{
    // Override in subclasses for completion effects
}

void UISMCollectorComponent::OnCollectionCancelledInternal_Implementation(const FISMInstanceHandle& Instance,
    UISMResourceComponent* ResourceComp)
{
    // Override in subclasses for cancellation effects
}

void UISMCollectorComponent::OnCollectorTagsChangedInternal_Implementation(const FGameplayTagContainer& NewTags)
{
    // Override in subclasses for tag change reactions
}

// ===== Helper Functions =====

void UISMCollectorComponent::RunDetection()
{
    FISMInstanceHandle NewTarget;
    UISMResourceComponent* NewResourceComp = nullptr;

    switch (DetectionMode)
    {
    case ECollectionDetectionMode::Raycast:
        NewTarget = DetectViaRaycast();
        break;

    case ECollectionDetectionMode::Radius:
        NewTarget = DetectViaRadius();
        break;

    case ECollectionDetectionMode::Manual:
    default:
        // Manual mode - don't auto-detect
        return;
    }

    // Check if target changed
    if (NewTarget != TargetedInstance)
    {
        UpdateTarget(NewTarget, NewResourceComp);
    }
}

FISMInstanceHandle UISMCollectorComponent::DetectViaRaycast()
{
    // Get raycast points
    FVector Start, End;
    GetRaycastPoints(Start, End);

    // Perform line trace
    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.bTraceComplex = bTraceComplex;
    QueryParams.AddIgnoredActor(GetOwner());

    bool bHit = GetWorld()->LineTraceSingleByChannel(
        HitResult,
        Start,
        End,
        TraceChannel,
        QueryParams
    );

    if (!bHit)
        return FISMInstanceHandle();

    // Check if we hit an ISM component
    UInstancedStaticMeshComponent* ISMComp = Cast<UInstancedStaticMeshComponent>(HitResult.Component.Get());
    if (!ISMComp)
        return FISMInstanceHandle();

    // Find the resource component managing this ISM
    UWorld* World = GetWorld();
    if (!World)
        return FISMInstanceHandle();

    UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    if (!Subsystem)
        return FISMInstanceHandle();

    // Find resource component for this ISM
    TArray<UISMRuntimeComponent*> AllComponents = Subsystem->GetAllComponents();
    for (UISMRuntimeComponent* RuntimeComp : AllComponents)
    {
        UISMResourceComponent* ResourceComp = Cast<UISMResourceComponent>(RuntimeComp);
        if (!ResourceComp)
            continue;

        if (ResourceComp->ManagedISMComponent == ISMComp)
        {
            // Found the resource component, get instance index from hit
            int32 InstanceIndex = HitResult.Item;

            FISMInstanceHandle Handle;
            Handle.InstanceIndex = InstanceIndex;
            Handle.Component = ResourceComp;

            // Check if we should consider this instance
            if (ShouldConsiderInstance(Handle, ResourceComp))
            {
                // Cache the resource component for UpdateTarget
                TargetedResourceComponent = ResourceComp;
                return Handle;
            }
        }
    }

    return FISMInstanceHandle();
}

FISMInstanceHandle UISMCollectorComponent::DetectViaRadius()
{
    UWorld* World = GetWorld();
    if (!World)
        return FISMInstanceHandle();

    UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    if (!Subsystem)
        return FISMInstanceHandle();

    FVector OwnerLocation = GetOwner()->GetActorLocation();

    // Query all resource components
    TArray<UISMRuntimeComponent*> AllComponents = Subsystem->GetAllComponents();

    FISMInstanceHandle BestHandle;
    float BestDistance = DetectionRadius;
    UISMResourceComponent* BestResourceComp = nullptr;

    for (UISMRuntimeComponent* RuntimeComp : AllComponents)
    {
        UISMResourceComponent* ResourceComp = Cast<UISMResourceComponent>(RuntimeComp);
        if (!ResourceComp)
            continue;

        // Query instances in radius
        TArray<int32> NearbyInstances = ResourceComp->GetInstancesInRadius(OwnerLocation, DetectionRadius);

        for (int32 InstanceIndex : NearbyInstances)
        {
            FISMInstanceHandle Handle;
            Handle.InstanceIndex = InstanceIndex;
            Handle.Component = ResourceComp;

            // Check if we should consider this instance
            if (!ShouldConsiderInstance(Handle, ResourceComp))
                continue;

            // Find closest
            FVector InstanceLocation = ResourceComp->GetInstanceLocation(InstanceIndex);
            float Distance = FVector::Dist(OwnerLocation, InstanceLocation);

            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                BestHandle = Handle;
                BestResourceComp = ResourceComp;
            }
        }
    }

    if (BestHandle.IsValid())
    {
        TargetedResourceComponent = BestResourceComp;
    }

    return BestHandle;
}

void UISMCollectorComponent::UpdateTarget(const FISMInstanceHandle& NewTarget, UISMResourceComponent* NewResourceComp)
{
    // If target didn't actually change, return
    if (NewTarget == TargetedInstance && NewResourceComp == TargetedResourceComponent.Get())
        return;

    // Unbind from old resource component
    if (TargetedResourceComponent.IsValid())
    {
        UnbindFromResourceComponent();
    }

    // Update target
    FISMInstanceHandle OldTarget = TargetedInstance;
    TargetedInstance = NewTarget;
    TargetedResourceComponent = NewResourceComp;

    // Bind to new resource component
    if (NewResourceComp)
    {
        BindToResourceComponent(NewResourceComp);
    }

    // Call hooks
    OnTargetChangedInternal(NewTarget, NewResourceComp);
    OnTargetChanged.Broadcast(NewTarget, NewResourceComp);

    UE_LOG(LogTemp, Verbose, TEXT("Collector target changed: %s -> %s"),
        OldTarget.IsValid() ? TEXT("Valid") : TEXT("None"),
        NewTarget.IsValid() ? TEXT("Valid") : TEXT("None"));
}

bool UISMCollectorComponent::TryStartCollection()
{
    if (!HasValidTarget())
    {
        UE_LOG(LogTemp, Warning, TEXT("[ISMCollector] Cannot start collection: No valid target"));
        return false;
    }

    UISMResourceComponent* ResourceComp = TargetedResourceComponent.Get();
    if (!ResourceComp)
    {
        UE_LOG(LogTemp, Error, TEXT("[ISMCollector] Cannot start collection: ResourceComponent is null"));
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("[ISMCollector] Attempting to start collection on instance %d"), TargetedInstance.InstanceIndex);

    // Validate collection
    FText FailureReason;
    if (!CanCollectResource(TargetedInstance, ResourceComp, FailureReason))
    {
        // Broadcast failure event
        OnCollectionFailedEvent.Broadcast(TargetedInstance, ResourceComp, FailureReason);

        UE_LOG(LogTemp, Warning, TEXT("[ISMCollector] Collection validation failed: %s"), *FailureReason.ToString());
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("[ISMCollector] Validation passed, getting collector tags..."));

    // Get current collector tags
    FGameplayTagContainer CurrentCollectorTags = GetCollectorTags();

    UE_LOG(LogTemp, Display, TEXT("[ISMCollector] Collector has %d tags"), CurrentCollectorTags.Num());
    for (const FGameplayTag& Tag : CurrentCollectorTags)
    {
        UE_LOG(LogTemp, Display, TEXT("  - %s"), *Tag.ToString());
    }

    // Start collection on resource component
    UE_LOG(LogTemp, Display, TEXT("[ISMCollector] Calling ResourceComponent->StartCollection..."));
    if (ResourceComp->StartCollection(TargetedInstance.InstanceIndex, GetOwner(), CurrentCollectorTags))
    {
        bIsCollecting = true;

        // Call hooks
        OnCollectionStartedInternal(TargetedInstance, ResourceComp);
        OnCollectionStartedEvent.Broadcast(TargetedInstance, ResourceComp);

        UE_LOG(LogTemp, Log, TEXT("[ISMCollector] ✓ Collection started successfully on instance %d"), TargetedInstance.InstanceIndex);
        return true;
    }

    UE_LOG(LogTemp, Error, TEXT("[ISMCollector] ✗ ResourceComponent->StartCollection returned false"));
    return false;
}

void UISMCollectorComponent::GetRaycastPoints(FVector& OutStart, FVector& OutEnd) const
{
    // Try to get view point from controller
    if (APawn* Pawn = Cast<APawn>(GetOwner()))
    {
        if (AController* Controller = Pawn->GetController())
        {
            FVector ViewLocation;
            FRotator ViewRotation;
            Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

            OutStart = ViewLocation;
            OutEnd = OutStart + (ViewRotation.Vector() * RaycastRange);
            return;
        }
    }

    // Fall back to camera component
    if (UCameraComponent* Camera = CachedCamera.Get())
    {
        OutStart = Camera->GetComponentLocation();
        OutEnd = OutStart + (Camera->GetForwardVector() * RaycastRange);
        return;
    }

    // Last resort: use actor location and forward
    OutStart = GetOwner()->GetActorLocation();
    OutEnd = OutStart + (GetOwner()->GetActorForwardVector() * RaycastRange);
}

UCameraComponent* UISMCollectorComponent::FindCameraComponent()
{
    // Check owner for camera component
    if (AActor* Owner = GetOwner())
    {
        if (UCameraComponent* Camera = Owner->FindComponentByClass<UCameraComponent>())
        {
            return Camera;
        }

        // Check controller's view target if this is a pawn
        if (APawn* Pawn = Cast<APawn>(Owner))
        {
            if (AController* Controller = Pawn->GetController())
            {
                if (AActor* ViewTarget = Controller->GetViewTarget())
                {
                    if (UCameraComponent* Camera = ViewTarget->FindComponentByClass<UCameraComponent>())
                    {
                        return Camera;
                    }
                }
            }
        }
    }

    return nullptr;
}

void UISMCollectorComponent::BindToResourceComponent(UISMResourceComponent* ResourceComp)
{
    if (!ResourceComp)
        return;

    ResourceComp->OnCollectionProgress.AddDynamic(this, &UISMCollectorComponent::HandleCollectionProgress);
    ResourceComp->OnResourceCollected.AddDynamic(this, &UISMCollectorComponent::HandleCollectionCompleted);
    ResourceComp->OnCollectionCancelled.AddDynamic(this, &UISMCollectorComponent::HandleCollectionCancelled);
}

void UISMCollectorComponent::UnbindFromResourceComponent()
{
    if (UISMResourceComponent* ResourceComp = TargetedResourceComponent.Get())
    {
        ResourceComp->OnCollectionProgress.RemoveDynamic(this, &UISMCollectorComponent::HandleCollectionProgress);
        ResourceComp->OnResourceCollected.RemoveDynamic(this, &UISMCollectorComponent::HandleCollectionCompleted);
        ResourceComp->OnCollectionCancelled.RemoveDynamic(this, &UISMCollectorComponent::HandleCollectionCancelled);
    }
}

// ===== Resource Component Event Handlers =====

void UISMCollectorComponent::HandleCollectionProgress(UISMResourceComponent* Component,
    const FISMInstanceHandle& Instance,
    AActor* Collector,
    float Progress)
{
    // Only handle if we're the collector
    if (Collector != GetOwner())
        return;

    // Broadcast progress
    OnCollectionProgressInternal(Instance, Component, Progress);
    OnCollectionProgressEvent.Broadcast(Instance, Component, Progress);
}

void UISMCollectorComponent::HandleCollectionCompleted(UISMResourceComponent* Component,
    const FResourceCollectionData& CollectionData)
{
    // Only handle if we're the collector
    if (CollectionData.Collector != GetOwner())
        return;

    bIsCollecting = false;

    // Call hooks
    OnCollectionCompletedInternal(CollectionData.Instance, Component);
    OnCollectionCompletedEvent.Broadcast(CollectionData.Instance, Component);

    // Clear target since instance was collected
    ClearTarget();
}

void UISMCollectorComponent::HandleCollectionCancelled(UISMResourceComponent* Component,
    const FISMInstanceHandle& Instance,
    AActor* Collector)
{
    // Only handle if we're the collector
    if (Collector != GetOwner())
        return;

    bIsCollecting = false;
}