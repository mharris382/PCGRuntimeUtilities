#include "ISMPhysicsActor.h"
#include "ISMPhysicsDataAsset.h"
#include "ISMInstanceHandle.h"
#include "Components/StaticMeshComponent.h"
#include "Logging/LogMacros.h"
#include "ISMRuntimeComponent.h"
#include "ISMRuntimePoolSubsystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Kismet/GameplayStatics.h"
#include "Feedbacks/ISMFeedbackSubsystem.h"
#include "Feedbacks/ISMFeedbackContext.h"

#include "DrawDebugHelpers.h"


DEFINE_LOG_CATEGORY(LogISMRuntimePhysics);

AISMPhysicsActor::AISMPhysicsActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false; // Disabled when in pool
    
    // Create mesh component
    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhysicsMesh"));
    RootComponent = MeshComponent;
    
    // Configure for physics
    MeshComponent->SetSimulatePhysics(false); // Disabled when in pool
    MeshComponent->SetEnableGravity(false);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetVisibility(false);
    
    // Make transient so we don't save pooled actors
    SetFlags(RF_Transient);
}

//void AISMPhysicsActor::BeginPlay()
//{
//	ApplyIgnorePawnCollision(this);
//}

// ===== IISMCustomDataMaterialProvider Interface =====

void AISMPhysicsActor::ApplyDMIToSlot_Implementation(int32 SlotIndex, UMaterialInstanceDynamic* DMI)
{
    if (!DMI)
    {
        UE_LOG(LogISMRuntimePhysics, Warning, TEXT("ApplyDMIToSlot: null DMI for slot %d on %s — skipping"), SlotIndex, *GetName());
        return;
    }

    // Guard whatever is on line 48 — likely a mesh component or material array access
    UMeshComponent* Mesh = MeshComponent; // or however you get it
    if (!Mesh)
    {
        UE_LOG(LogISMRuntimePhysics, Warning, TEXT("ApplyDMIToSlot: null mesh on %s — skipping slot %d"), *GetName(), SlotIndex);
        return;
    }
    
    if (SlotIndex >= Mesh->GetNumMaterials())
    {
        UE_LOG(LogISMRuntimePhysics, Warning, TEXT("ApplyDMIToSlot: slot %d out of range (%d slots) on %s"), SlotIndex, Mesh->GetNumMaterials(), *GetName());
        return;
    }

	UE_LOG(LogISMRuntimePhysics, Verbose, TEXT("AISMPhysicsActor::ApplyDMIToSlot - Slot: %d, DMI: %s"), SlotIndex, *GetNameSafe(DMI));
    if (MeshComponent && SlotIndex < MeshComponent->GetNumMaterials())
    {
        MeshComponent->SetMaterial(SlotIndex, DMI);
    }
	
}

int32 AISMPhysicsActor::GetMaterialSlotCount_Implementation() const
{
    return MeshComponent ? MeshComponent->GetNumMaterials() : 1;
}

bool AISMPhysicsActor::ShouldSkipSlot_Implementation(int32 SlotIndex) const
{
    return false; // Accept all slots
}

void AISMPhysicsActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (!PhysicsData.IsValid())
    {
		UE_LOG(LogTemp, Warning, TEXT("[%s] Tick - No valid physics data!"), *GetName());
        return;
    }
    
    // DIAGNOSTIC LOGGING - Remove after debugging
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  

    // Update resting detection
    UpdateRestingDetection(DeltaTime);
    
    // Auto-return if settled
    if (ShouldReturnToISM())
    {
        UE_LOG(LogTemp, Log, TEXT("[%s] RETURNING TO ISM - Settled for %.2fs"),
            *GetName(), TimeAtRest);
        ReturnToISM();
    }
    
#if WITH_EDITOR
    if (bShowDebugInfo)
    {
        DrawDebugInfo();
    }
#endif
}

// ===== IISMPoolable Interface =====

void AISMPhysicsActor::OnPoolSpawned_Implementation()
{
    UE_LOG(LogISMRuntimePhysics, Verbose, TEXT("AISMPhysicsActor::OnPoolSpawned - %s"), *GetName());
    
    // One-time initialization when first added to pool
    ResetRuntimeState();
    
    // Ensure actor is in clean state
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    SetActorTickEnabled(false);
    
    if (MeshComponent)
    {
        MeshComponent->SetSimulatePhysics(false);
        MeshComponent->SetVisibility(false);
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AISMPhysicsActor::OnRequestedFromPool_Implementation(UISMPoolDataAsset* DataAsset, const FISMInstanceHandle& InInstanceHandle)
{
    PoolActivationCount++; 

    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::OnRequestedFromPool - %s"), *GetName());
    
    // Validate data asset
    UISMPhysicsDataAsset* PhysicsDataAsset = Cast<UISMPhysicsDataAsset>(DataAsset);
    if (!PhysicsDataAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("AISMPhysicsActor::OnRequestedFromPool - DataAsset is not UISMPhysicsDataAsset!"));
        return;
    }
    
    // Store references
    PhysicsData = PhysicsDataAsset;
    InstanceHandle = InInstanceHandle;
    
    // Reset runtime state
    ResetRuntimeState();
    
    // Get instance transform from handle
    if (InstanceHandle.IsValid())
    {
        const FTransform InstanceTransform = InstanceHandle.GetTransform();
        SetActorTransform(InstanceTransform);
    }
    
    // Configure from data asset
    ApplyPhysicsSettings(PhysicsDataAsset);
    ApplyCollisionSettings(PhysicsDataAsset);
    ApplyVisualSettings(PhysicsDataAsset);
    
    // Enable actor
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    SetActorTickEnabled(true);
    
    // Enable physics simulation
    if (MeshComponent)
    {
        MeshComponent->SetVisibility(true);
        MeshComponent->SetSimulatePhysics(true);
    }

    if (InstanceHandle.IsValid())
    {
        InstanceHandle.RefreshConvertedActorMaterials(GetWorld());
    }
    
    // Record activation time
    TimeActivated = GetWorld()->GetTimeSeconds();
    
    // Play conversion sound
    PlayConversionFeedback();
    
    UE_LOG(LogISMRuntimePhysics, Log, TEXT("AISMPhysicsActor::OnRequestedFromPool - Actor configured and activated"));
}

void AISMPhysicsActor::OnReturnedToPool_Implementation(FTransform& OutFinalTransform, bool& bUpdateInstanceTransform)
{
    UE_LOG(LogISMRuntimePhysics, Verbose, TEXT("AISMPhysicsActor::OnReturnedToPool - %s"), *GetName());
    
    // Capture final transform
    OutFinalTransform = GetActorTransform();
    bUpdateInstanceTransform = true; // Usually want to update ISM position to match where actor settled
    
    // Play return sound
    PlayReturnFeedback();
    
    // Stop physics
    if (MeshComponent)
    {
        MeshComponent->SetSimulatePhysics(false);
        MeshComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
        MeshComponent->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    }
    
    // Hide actor
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    SetActorTickEnabled(false);
    
    if (MeshComponent)
    {
        MeshComponent->SetVisibility(false);
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    
    // Clear references
    InstanceHandle = FISMInstanceHandle();
    PhysicsData.Reset();
    
    // Reset state
    ResetRuntimeState();
}

void AISMPhysicsActor::OnPoolDestroyed_Implementation()
{
    UE_LOG(LogISMRuntimePhysics, Verbose, TEXT("AISMPhysicsActor::OnPoolDestroyed - %s"), *GetName());
    
    // Final cleanup before destruction
    // Nothing special needed - default cleanup is fine
}

// ===== Physics Configuration =====

void AISMPhysicsActor::ApplyPhysicsSettings(UISMPhysicsDataAsset* PhysicsDataAsset)
{
    if (!MeshComponent || !PhysicsDataAsset)
    {
        return;
    }
    
    // Set mesh from data asset
    if (PhysicsDataAsset->StaticMesh)
    {
        MeshComponent->SetStaticMesh(PhysicsDataAsset->StaticMesh);
    }
    
    // Apply material overrides
    const TArray<UMaterialInterface*>& MaterialOverrides = PhysicsDataAsset->GetMaterialOverrides();
    for (int32 i = 0; i < MaterialOverrides.Num(); ++i)
    {
        if (MaterialOverrides[i])
        {
            MeshComponent->SetMaterial(i, MaterialOverrides[i]);
        }
    }
    
    // Set mass
    if (PhysicsDataAsset->Mass > 0.0f)
    {
        MeshComponent->SetMassOverrideInKg(NAME_None, PhysicsDataAsset->Mass, true);
    }
    else
    {
        // Auto-calculate mass from mesh
        MeshComponent->SetMassOverrideInKg(NAME_None, 0.0f, false);
    }
    
    // Set damping
    MeshComponent->SetLinearDamping(PhysicsDataAsset->LinearDamping);
    MeshComponent->SetAngularDamping(PhysicsDataAsset->AngularDamping);
    
    // Set gravity
    MeshComponent->SetEnableGravity(PhysicsDataAsset->bEnableGravity);
    
    // Set physics material
    if (PhysicsDataAsset->PhysicsMaterial)
    {
        MeshComponent->SetPhysMaterialOverride(PhysicsDataAsset->PhysicsMaterial);
    }
    
    // Advanced settings
    if (PhysicsDataAsset->MaxAngularVelocity > 0.0f)
    {
        MeshComponent->SetPhysicsMaxAngularVelocityInRadians(PhysicsDataAsset->MaxAngularVelocity);
    }
    
    // Lock rotation axes
    MeshComponent->BodyInstance.bLockXRotation = PhysicsDataAsset->bLockRotationX;
    MeshComponent->BodyInstance.bLockYRotation = PhysicsDataAsset->bLockRotationY;
    MeshComponent->BodyInstance.bLockZRotation = PhysicsDataAsset->bLockRotationZ;
    
    // Center of mass offset
    if (!PhysicsDataAsset->CenterOfMassOffset.IsNearlyZero())
    {
        MeshComponent->BodyInstance.COMNudge = PhysicsDataAsset->CenterOfMassOffset;
        //MeshComponent->BodyInstance.bOverrideCenterOfMass = true;
    }
    else
    {
        //MeshComponent->BodyInstance.bOverrideCenterOfMass = false;
    }
    
    UE_LOG(LogISMRuntimePhysics, Verbose, TEXT("AISMPhysicsActor::ApplyPhysicsSettings - Mass: %.2f, LinearDamping: %.2f, AngularDamping: %.2f"),
        PhysicsDataAsset->Mass, PhysicsDataAsset->LinearDamping, PhysicsDataAsset->AngularDamping);
}

void AISMPhysicsActor::ApplyCollisionSettings(UISMPhysicsDataAsset* PhysicsDataAsset)
{
    if (!MeshComponent || !PhysicsDataAsset)
    {
        return;
    }
    
    // Set collision preset
    if (!PhysicsDataAsset->CollisionPreset.IsNone())
    {
        MeshComponent->SetCollisionProfileName(PhysicsDataAsset->CollisionPreset);
    }
    
    // Enable/disable physics collision
    if (!PhysicsDataAsset->bEnablePhysicsCollision)
    {
        // Disable collision with other physics objects
        MeshComponent->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
    }
	MeshComponent->SetCanEverAffectNavigation(PhysicsDataAsset->bCanEverEffectNavigation);
	MeshComponent->SetWalkableSlopeOverride(PhysicsDataAsset->WalkableSlopeOverride);
	MeshComponent->SetNotifyRigidBodyCollision(PhysicsDataAsset->bIsDestructable);
    
    UE_LOG(LogISMRuntimePhysics, Verbose, TEXT("AISMPhysicsActor::ApplyCollisionSettings - Preset: %s, PhysicsCollision: %s"),
        *PhysicsDataAsset->CollisionPreset.ToString(), PhysicsDataAsset->bEnablePhysicsCollision ? TEXT("Enabled") : TEXT("Disabled"));
}

void AISMPhysicsActor::ApplyVisualSettings(UISMPhysicsDataAsset* PhysicsDataAsset)
{
    if (!MeshComponent || !PhysicsDataAsset)
    {
        return;
    }
    
    // Apply scale multiplier
    if (!FMath::IsNearlyEqual(PhysicsDataAsset->ActorScaleMultiplier, 1.0f))
    {
        const FVector CurrentScale = GetActorScale3D();
        SetActorScale3D(CurrentScale * PhysicsDataAsset->ActorScaleMultiplier);
    }

	MeshComponent->SetCastShadow(PhysicsDataAsset->bPhysicsActorCastShadows);
    
    UE_LOG(LogISMRuntimePhysics, Verbose, TEXT("AISMPhysicsActor::ApplyVisualSettings - Scale: %.2f"),
        PhysicsDataAsset->ActorScaleMultiplier);
}

#pragma region RESTING_DETECTION

// ===== Resting Detection =====

bool AISMPhysicsActor::IsAtRest() const
{
    if (!MeshComponent || !PhysicsData.IsValid())
    {
		UE_LOG(LogISMRuntimePhysics, Warning, TEXT("AISMPhysicsActor::IsAtRest - Missing MeshComponent or PhysicsData!"));
        return false;
    }

    const float LinearVelocity = GetLinearVelocityMagnitude();
    const float AngularVelocity = GetAngularVelocityMagnitude();

    return IsVelocityBelowThreshold(LinearVelocity, AngularVelocity);
}

bool AISMPhysicsActor::ShouldReturnToISM() const
{
    if (!PhysicsData.IsValid())
    {
		UE_LOG(LogISMRuntimePhysics, Warning, TEXT("AISMPhysicsActor::ShouldReturnToISM - No valid physics data!"));
        return false;
    }

    // Check if at rest for required duration
    const bool bAtRest = IsAtRest();
    const bool bLongEnough = TimeAtRest >= PhysicsData->RestingCheckDelay;

    return bAtRest && bLongEnough;
}

void AISMPhysicsActor::ReturnToISM()
{
    // This is called either:
    // 1. Automatically when actor settles (from Tick)
    // 2. Manually via reset triggers or limiters
    // 3. Via pool cleanup

    // The actual return logic is handled by the pool subsystem
    // through the component that owns this actor's handle

    if (!InstanceHandle.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("AISMPhysicsActor::ReturnToISM - Invalid instance handle!"));
        return;
    }

    // Check we are still the actor this handle was assigned to
    if (InstanceHandle.CachedActorActivationCount != PoolActivationCount)
    {
        UE_LOG(LogISMRuntimePhysics, Warning, TEXT("[%s] ReturnToISM: stale activation count, ignoring"), *GetName());
        ReturnSelfToPool();
        return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::ReturnToISM - Returning to ISM"));

    // Call the handle's return function
    // This will trigger OnReturnedToPool and handle pool return
    InstanceHandle.ReturnToISM(true, true); // Destroy actor=true, UpdateTransform=true

    ReturnSelfToPool();
}


void AISMPhysicsActor::ReturnSelfToPool()
{
    if (UWorld* World = GetWorld())
    {
        if (UISMRuntimePoolSubsystem* Pool = World->GetSubsystem<UISMRuntimePoolSubsystem>())
        {
            FTransform FinalTransform;
            bool bUpdateTransform = false;
            Pool->ReturnActor(this, FinalTransform, bUpdateTransform);
        }
    }
}
#pragma endregion


#pragma region FEEDBACKS
namespace
{
    bool PlayFeedback(FGameplayTag tag, AISMPhysicsActor* actor, EISMFeedbackMessageType MessageType, TFunctionRef<void(FISMFeedbackContext& context)> SetupContext)
    {
        if (!tag.IsValid())
            return false;
        if (!actor)
            return false;

        UWorld* World = actor->GetWorld();
        if (!World)
            return false; // Changed from true to false - no world means can't play feedback

        UISMFeedbackSubsystem* FeedbackSys = World->GetSubsystem<UISMFeedbackSubsystem>();
        if (!FeedbackSys)
            return false;

        FISMFeedbackContext Context = FISMFeedbackContext::CreateFromPrimitive(tag, actor->MeshComponent, nullptr, MessageType, 0.0f);
        SetupContext(Context);
        return FeedbackSys->RequestFeedback(Context);
    }

    EISMFeedbackMessageType GetMessageTypeStart(UISMPhysicsDataAsset* PhysicsData)
    {
        if (!PhysicsData)
            return EISMFeedbackMessageType::ONE_SHOT;

        return PhysicsData->FeedbackMode == EISMPhysicsFeedbackMode::Continuous
            ? EISMFeedbackMessageType::STARTED  // Should be STARTED, not COMPLETED!
            : EISMFeedbackMessageType::ONE_SHOT;
    }

    EISMFeedbackMessageType GetMessageTypeEnding(UISMPhysicsDataAsset* PhysicsData, bool bIsForDestructionEvent)
    {
        if (!PhysicsData)
            return EISMFeedbackMessageType::ONE_SHOT;

        switch (PhysicsData->FeedbackMode)
        {
        case EISMPhysicsFeedbackMode::OneShot:
            return EISMFeedbackMessageType::ONE_SHOT;

        case EISMPhysicsFeedbackMode::Continuous:
        {
            // For continuous mode, determine complete vs interrupted
            switch (PhysicsData->LifecycleResult)
            {
            case EISMPhysicsLifecycleResult::BothComplete:
                return EISMFeedbackMessageType::COMPLETED;

            case EISMPhysicsLifecycleResult::ReturnComplete:
                // Return = complete, destroy = interrupted
                return bIsForDestructionEvent
                    ? EISMFeedbackMessageType::INTERRUPTED
                    : EISMFeedbackMessageType::COMPLETED;

            case EISMPhysicsLifecycleResult::DestroyComplete:
                // Destroy = complete, return = interrupted
                return bIsForDestructionEvent
                    ? EISMFeedbackMessageType::COMPLETED
                    : EISMFeedbackMessageType::INTERRUPTED;

            default:
                return EISMFeedbackMessageType::COMPLETED;
            }
        }

        default:
            return EISMFeedbackMessageType::ONE_SHOT;
        }
        // REMOVED: Unreachable return statement here
    }
}

void AISMPhysicsActor::PlayConversionFeedback()
{
    if (!PhysicsData.IsValid())
        return;

    EISMFeedbackMessageType MessageType = GetMessageTypeStart(PhysicsData.Get());
    FGameplayTag tag = PhysicsData->IsContinous()
        ? PhysicsData->LifecycleFeedbackTag
        : PhysicsData->ConversionFeedback;

    PlayFeedback(tag, this, MessageType, [](FISMFeedbackContext& Context)
        {
            Context.Normal = FVector::UpVector; // Spawning upward, not down
        });

    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::PlayConversionFeedback - Fired tag %s"), *tag.ToString());
}

void AISMPhysicsActor::PlayReturnFeedback()
{
    if (!PhysicsData.IsValid())
        return;

    EISMFeedbackMessageType MessageType = GetMessageTypeEnding(PhysicsData.Get(), false);
    FGameplayTag tag = PhysicsData->IsContinous()
        ? PhysicsData->LifecycleFeedbackTag
        : PhysicsData->ReturnFeedback;

    PlayFeedback(tag, this, MessageType, [this](FISMFeedbackContext& Context)
        {
            Context.Normal = FVector::DownVector;
            Context.Velocity = MeshComponent ? MeshComponent->GetPhysicsLinearVelocity() : FVector::ZeroVector;
            Context.Intensity = GetLinearVelocityMagnitude() / 1000.0f; // Normalize velocity
        });

    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::PlayReturnFeedback - Fired tag %s"), *tag.ToString());
}

void AISMPhysicsActor::PlayDestructionFeedback()
{
    if (!PhysicsData.IsValid())
        return;

    EISMFeedbackMessageType MessageType = GetMessageTypeEnding(PhysicsData.Get(), true);
    FGameplayTag tag = PhysicsData->IsContinous()
        ? PhysicsData->LifecycleFeedbackTag
        : PhysicsData->DestroyFeedback;

    PlayFeedback(tag, this, MessageType, [this](FISMFeedbackContext& Context)
        {
            Context.Velocity = MeshComponent ? MeshComponent->GetPhysicsLinearVelocity() : FVector::ZeroVector;
            Context.Intensity = 1.0f; // Max intensity for destruction
        });

    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::PlayDestructionFeedback - Fired tag %s"), *tag.ToString());
}
#pragma endregion


#pragma region DESTRUCTION


void AISMPhysicsActor::OnPhysicsActorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!PhysicsData.IsValid() || !InstanceHandle.IsValid())
    {
        return;
    }

    // Calculate impact force magnitude
    const float ImpactForce = NormalImpulse.Size();

    // Check if this impact should trigger destruction
   // if (PhysicsData->bIsDestructable && ShouldDestroyOnImpact(ImpactForce) && PhysicsData->TestDestructionQueryForInstance(InstanceHandle))
   // {
   //     UE_LOG(LogTemp, Log, TEXT("AISMPhysicsActor::OnPhysicsHit - %s hit with force %.1f, triggering destruction"),
   //         *GetName(), ImpactForce);
   //
   //     // Get the component that owns this instance
   //     if (UISMRuntimeComponent* Component = InstanceHandle.Component.Get())
   //     {
   //         // Destroy the instance (this fires OnInstanceDestroyed -> feedback)
   //         Component->DestroyInstance(InstanceHandle.InstanceIndex, false);
   //
   //         // Return this physics actor to pool immediately (no need to simulate further)
   //         // Note: We DON'T call ReturnToISM because the instance is destroyed, not hidden
   //         // The pool system will handle cleanup via the handle
   //     }
   // }
}

bool AISMPhysicsActor::ShouldDestroyOnImpact(float ImpactForce) const
{
    if (!PhysicsData.IsValid() || !PhysicsData->bIsDestructable)
    {
        return false;
    }

    float DestructionThreshold = PhysicsData->DestructionForceThreshold;

    // Scale threshold by actor scale if configured
    if (PhysicsData->bDestructionThresholdIsScaled)
    {
        const float ActorScale = GetActorScale3D().GetMax(); // Use largest scale component
        DestructionThreshold *= ActorScale;
    }

    return ImpactForce >= DestructionThreshold;
}

void AISMPhysicsActor::DestroyLinkedISMInstance()
{
    if (!InstanceHandle.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("AISMPhysicsActor::DestroyLinkedISMInstance - Invalid instance handle"));
        return;
    }

    UISMRuntimeComponent* Component = InstanceHandle.Component.Get();
    if (!Component)
    {
        UE_LOG(LogTemp, Warning, TEXT("AISMPhysicsActor::DestroyLinkedISMInstance - Component no longer valid"));
        return;
    }

    // Destroy the instance permanently (triggers OnInstanceDestroyed event)
    Component->DestroyInstance(InstanceHandle.InstanceIndex, false);

    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::DestroyLinkedISMInstance - Destroyed instance %d in component %s"),
        InstanceHandle.InstanceIndex, *Component->GetName());
}

#pragma endregion


void AISMPhysicsActor::SetInstanceHandle(const FISMInstanceHandle& Handle)
{
    InstanceHandle = Handle;
    InstanceHandle.SetConvertedActor(this, GetPoolActivationCount());
	InstanceHandle.RefreshConvertedActorMaterials(GetWorld());
    const FString CompName = Handle.Component.IsValid() ? Handle.Component.Get()->GetName() : TEXT("NULL");
    UE_LOG(LogTemp, Log, TEXT("PhysicsActor %s set instance handle: Component=%s, Index=%d"), *GetName(), *CompName, Handle.InstanceIndex);
}



#pragma region ACCESSORS

// ===== Accessors =====

float AISMPhysicsActor::GetLinearVelocityMagnitude() const
{
    if (!MeshComponent)
    {
        return 0.0f;
    }

    return MeshComponent->GetPhysicsLinearVelocity().Size();
}

float AISMPhysicsActor::GetAngularVelocityMagnitude() const
{
    if (!MeshComponent)
    {
        return 0.0f;
    }

    return MeshComponent->GetPhysicsAngularVelocityInRadians().Size();
}
#pragma endregion

#pragma region HELPER_FUNCTIONS

// ===== Helper Functions =====

void AISMPhysicsActor::ResetRuntimeState()
{
    TimeAtRest = 0.0f;
    bWasAtRestLastFrame = false;
    TimeActivated = 0.0;
}

void AISMPhysicsActor::UpdateRestingDetection(float DeltaTime)
{
    if (!PhysicsData.IsValid())
    {
        return;
    }

    const bool bAtRest = IsAtRest();

    if (bAtRest)
    {
        if (bWasAtRestLastFrame)
        {
            // Continue accumulating rest time
            TimeAtRest += DeltaTime;
        }
        else
        {
            // Just started resting
            TimeAtRest = DeltaTime;
            bWasAtRestLastFrame = true;
        }
    }
    else
    {
        // Not at rest - reset timer
        TimeAtRest = 0.0f;
        bWasAtRestLastFrame = false;
    }
}

bool AISMPhysicsActor::IsVelocityBelowThreshold(float LinearVelocity, float AngularVelocity) const
{
    if (!PhysicsData.IsValid())
    {
        return false;
    }

    // Check linear velocity
    const bool bLinearBelowThreshold = LinearVelocity <= PhysicsData->RestingVelocityThreshold;

    // Check angular velocity if required
    bool bAngularBelowThreshold = true;
    if (PhysicsData->bCheckAngularVelocity)
    {
        bAngularBelowThreshold = AngularVelocity <= PhysicsData->RestingAngularThreshold;
    }
    PhysicsData->LogRestingCheck(GetOwner(), LinearVelocity, AngularVelocity, bLinearBelowThreshold && bAngularBelowThreshold);
    return bLinearBelowThreshold && bAngularBelowThreshold;
}
#pragma endregion



#if WITH_EDITOR

#pragma region DEBUG_VISUALIZATION

// ===== Debug Visualization =====
void AISMPhysicsActor::DrawDebugInfo() const
{
    if (!PhysicsData.IsValid())
    {
        return;
    }

    const FVector Location = GetActorLocation();
    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Draw velocity vector
    const FVector LinearVelocity = MeshComponent ? MeshComponent->GetPhysicsLinearVelocity() : FVector::ZeroVector;
    const float VelocityMagnitude = LinearVelocity.Size();

    if (VelocityMagnitude > 0.1f)
    {
        const FVector VelocityEnd = Location + LinearVelocity.GetSafeNormal() * 100.0f;
        DrawDebugDirectionalArrow(World, Location, VelocityEnd, 20.0f, FColor::Yellow, false, -1.0f, 0, 2.0f);
    }

    // Draw rest state
    const FColor StateColor = IsAtRest() ? FColor::Green : FColor::Red;
    DrawDebugSphere(World, Location, 50.0f, 12, StateColor, false, -1.0f, 0, 2.0f);

    // Draw debug text
    const FString DebugText = FString::Printf(
        TEXT("Vel: %.1f cm/s\nAngular: %.2f rad/s\nRest Time: %.2fs\nActive Time: %.1fs"),
        VelocityMagnitude,
        GetAngularVelocityMagnitude(),
        TimeAtRest,
        World->GetTimeSeconds() - TimeActivated
    );

    DrawDebugString(World, Location + FVector(0, 0, 100), DebugText, nullptr, FColor::White, -1.0f, true);

    // Draw threshold sphere
    const float ThresholdRadius = PhysicsData->RestingVelocityThreshold;
    DrawDebugSphere(World, Location, ThresholdRadius, 12, FColor::Cyan, false, -1.0f, 0, 1.0f);
}
#pragma endregion


#endif // WITH_EDITOR