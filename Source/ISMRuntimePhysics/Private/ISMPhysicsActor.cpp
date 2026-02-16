#include "ISMPhysicsActor.h"
#include "ISMPhysicsDataAsset.h"
#include "ISMInstanceHandle.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

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

void AISMPhysicsActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (!PhysicsData.IsValid())
    {
        return;
    }
    
    // Update resting detection
    UpdateRestingDetection(DeltaTime);
    
    // Auto-return if settled
    if (ShouldReturnToISM())
    {
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
    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::OnPoolSpawned - %s"), *GetName());
    
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
    
    // Record activation time
    TimeActivated = GetWorld()->GetTimeSeconds();
    
    // Play conversion sound
    PlayConversionSound();
    
    UE_LOG(LogTemp, Log, TEXT("AISMPhysicsActor::OnRequestedFromPool - Actor configured and activated"));
}

void AISMPhysicsActor::OnReturnedToPool_Implementation(FTransform& OutFinalTransform, bool& bUpdateInstanceTransform)
{
    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::OnReturnedToPool - %s"), *GetName());
    
    // Capture final transform
    OutFinalTransform = GetActorTransform();
    bUpdateInstanceTransform = true; // Usually want to update ISM position to match where actor settled
    
    // Play return sound
    PlayReturnSound();
    
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
    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::OnPoolDestroyed - %s"), *GetName());
    
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
    
    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::ApplyPhysicsSettings - Mass: %.2f, LinearDamping: %.2f, AngularDamping: %.2f"),
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
    
    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::ApplyCollisionSettings - Preset: %s, PhysicsCollision: %s"),
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
    
    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::ApplyVisualSettings - Scale: %.2f"),
        PhysicsDataAsset->ActorScaleMultiplier);
}

// ===== Resting Detection =====

bool AISMPhysicsActor::IsAtRest() const
{
    if (!MeshComponent || !PhysicsData.IsValid())
    {
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
    
    UE_LOG(LogTemp, Verbose, TEXT("AISMPhysicsActor::ReturnToISM - Returning to ISM"));
    
    // Call the handle's return function
    // This will trigger OnReturnedToPool and handle pool return
    InstanceHandle.ReturnToISM(true, true); // Destroy actor=true, UpdateTransform=true
}

// ===== Audio Feedback =====

void AISMPhysicsActor::PlayConversionSound()
{
    if (!PhysicsData.IsValid() || !PhysicsData->ConversionSound)
    {
        return;
    }
    
    UGameplayStatics::PlaySoundAtLocation(
        this,
        PhysicsData->ConversionSound,
        GetActorLocation()
    );
}

void AISMPhysicsActor::PlayReturnSound()
{
    if (!PhysicsData.IsValid() || !PhysicsData->ReturnSound)
    {
        return;
    }
    
    UGameplayStatics::PlaySoundAtLocation(
        this,
        PhysicsData->ReturnSound,
        GetActorLocation()
    );
}

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
    
    return bLinearBelowThreshold && bAngularBelowThreshold;
}

// ===== Debug Visualization =====

#if WITH_EDITOR

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

#endif // WITH_EDITOR