#include "ISMPhysicsInstigatorComponent.h"
#include "ISMPhysicsComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UISMPhysicsInstigatorComponent::UISMPhysicsInstigatorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

// ===== Lifecycle =====

void UISMPhysicsInstigatorComponent::BeginPlay()
{
    Super::BeginPlay();
    
    // Cache root primitive component
    if (AActor* Owner = GetOwner())
    {
        CachedRootPrimitive = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
        LastPosition = GetComponentLocation();
    }
    
    // Refresh physics components cache
    RefreshPhysicsComponentsCache();
    
    // Bind collision events if we have a root primitive
    BindCollisionEvents();
    
    UE_LOG(LogTemp, Log, TEXT("UISMPhysicsInstigatorComponent::BeginPlay - Instigator initialized (Radius: %.1f, UpdateInterval: %.2fs)"),
        QueryRadius, UpdateInterval);
}

void UISMPhysicsInstigatorComponent::EndPlay(const EEndPlayReason::Type EndReason)
{
    // Unbind collision events
    UnbindCollisionEvents();
    
    Super::EndPlay(EndReason);
}

void UISMPhysicsInstigatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    if (!bIsEnabled)
    {
        return;
    }
    
    // Update cached velocity
    UpdateVelocity(DeltaTime);
    
    // Check if we should update this frame
    TimeSinceLastUpdate += DeltaTime;
    if (TimeSinceLastUpdate < UpdateInterval)
    {
        return;
    }
    
    TimeSinceLastUpdate = 0.0f;
    
    // Check minimum velocity threshold
    const float VelocityMagnitude = CachedVelocity.Size();
    if (VelocityMagnitude < MinimumVelocity)
    {
        return;
    }
    
    // Perform instigator update
    PerformInstigatorUpdate();
    
    // Periodically refresh component cache
    if (GFrameCounter - ComponentCacheUpdateFrame > ComponentCacheRefreshInterval)
    {
        RefreshPhysicsComponentsCache();
    }
    
#if WITH_EDITOR
    if (bShowDebugSphere || bShowDebugLines)
    {
        DrawDebugInfo();
    }
#endif
}

// ===== Manual Triggering =====

void UISMPhysicsInstigatorComponent::TriggerPhysicsAtLocation(FVector WorldLocation, float Radius, float Force)
{
    TArray<UISMPhysicsComponent*> PhysicsComponents = GetPhysicsComponents();
    
    for (UISMPhysicsComponent* PhysicsComponent : PhysicsComponents)
    {
        if (!PhysicsComponent)
        {
            continue;
        }
        
        // Query instances within radius
        TArray<int32> NearbyInstances = PhysicsComponent->GetInstancesInRadius(WorldLocation, Radius, false);
        
        // Try to convert each instance
        for (int32 InstanceIndex : NearbyInstances)
        {
            // Check filter
            if (!PassesFilter(PhysicsComponent, InstanceIndex))
            {
                continue;
            }
            
            // Get instance location for impact point
            const FVector InstanceLocation = PhysicsComponent->GetInstanceLocation(InstanceIndex);
            const FVector ImpactNormal = (InstanceLocation - WorldLocation).GetSafeNormal();
            
            // Trigger conversion
            if (PhysicsComponent->ConvertInstanceToPhysics(InstanceIndex, InstanceLocation, ImpactNormal, Force, GetOwner()))
            {
                OnInstigatorTriggered.Broadcast(PhysicsComponent, InstanceIndex, Force);
            }
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("UISMPhysicsInstigatorComponent::TriggerPhysicsAtLocation - Triggered at %s with radius %.1f and force %.1f"),
        *WorldLocation.ToString(), Radius, Force);
}

void UISMPhysicsInstigatorComponent::TriggerSingleInstance(UISMPhysicsComponent* PhysicsComponent, 
    int32 InstanceIndex, FVector ImpactPoint, float Force)
{
    if (!PhysicsComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsInstigatorComponent::TriggerSingleInstance - PhysicsComponent is null"));
        return;
    }
    
    // Check filter
    if (!PassesFilter(PhysicsComponent, InstanceIndex))
    {
        return;
    }
    
    // Calculate impact normal (from owner to instance)
    const FVector InstanceLocation = PhysicsComponent->GetInstanceLocation(InstanceIndex);
    const FVector OwnerLocation = GetOwner() ? GetOwner()->GetActorLocation() : ImpactPoint;
    const FVector ImpactNormal = (InstanceLocation - OwnerLocation).GetSafeNormal();
    
    // Trigger conversion
    if (PhysicsComponent->ConvertInstanceToPhysics(InstanceIndex, ImpactPoint, ImpactNormal, Force, GetOwner()))
    {
        OnInstigatorTriggered.Broadcast(PhysicsComponent, InstanceIndex, Force);
        
        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsInstigatorComponent::TriggerSingleInstance - Triggered instance %d with force %.1f"),
            InstanceIndex, Force);
    }
}

// ===== Query & Conversion =====

void UISMPhysicsInstigatorComponent::PerformInstigatorUpdate()
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }
    
	const FVector Location = GetComponentLocation();
    TArray<UISMPhysicsComponent*> PhysicsComponents = GetPhysicsComponents();
    
    // Calculate impact force once
    const float ImpactForce = CalculateImpactForce();
    
    // Query each physics component
    for (UISMPhysicsComponent* PhysicsComponent : PhysicsComponents)
    {
        if (!PhysicsComponent)
        {
            continue;
        }
        
        // Find nearby instances
        TArray<int32> NearbyInstances;
        QueryComponent(PhysicsComponent, NearbyInstances);
        
        // Try to convert nearby instances
        for (int32 InstanceIndex : NearbyInstances)
        {
            // Check filter
            if (!PassesFilter(PhysicsComponent, InstanceIndex))
            {
                continue;
            }
            
            // Get instance location for impact calculations
            const FVector InstanceLocation = PhysicsComponent->GetInstanceLocation(InstanceIndex);
			const float DistanceSqr = FVector::DistSquared(Location, InstanceLocation);
            if (DistanceSqr > FMath::Square(QueryRadius))
            {
                UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsInstigatorComponent::PerformInstigatorUpdate - Instance %d is outside of query radius (Distance: %.1f)"),
					InstanceIndex, FMath::Sqrt(DistanceSqr));
                continue; // Skip if outside of radius (shouldn't happen if query is correct, but just in case)
			}
            const FVector ImpactNormal = CachedVelocity.GetSafeNormal();
            
            // Trigger conversion
            if (PhysicsComponent->ConvertInstanceToPhysics(InstanceIndex, InstanceLocation, ImpactNormal, ImpactForce, Owner))
            {
                OnInstigatorTriggered.Broadcast(PhysicsComponent, InstanceIndex, ImpactForce);
                
                UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsInstigatorComponent::PerformInstigatorUpdate - Converted instance %d (Force: %.1f)"),
                    InstanceIndex, ImpactForce);
            }
        }
    }
}

void UISMPhysicsInstigatorComponent::RefreshPhysicsComponentsCache()
{
    CachedPhysicsComponents.Empty();
    
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    
    // Get runtime subsystem
    UISMRuntimeSubsystem* RuntimeSubsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    if (!RuntimeSubsystem)
    {
        return;
    }
    
    // Get all components and filter for physics components
    TArray<UISMRuntimeComponent*> AllComponents = RuntimeSubsystem->GetAllComponents();
    
    for (UISMRuntimeComponent* Component : AllComponents)
    {
        if (UISMPhysicsComponent* PhysicsComponent = Cast<UISMPhysicsComponent>(Component))
        {
            CachedPhysicsComponents.Add(PhysicsComponent);
        }
    }
    
    ComponentCacheUpdateFrame = GFrameCounter;
    
    UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsInstigatorComponent::RefreshPhysicsComponentsCache - Cached %d physics components"),
        CachedPhysicsComponents.Num());
}

TArray<UISMPhysicsComponent*> UISMPhysicsInstigatorComponent::GetPhysicsComponents()
{
    TArray<UISMPhysicsComponent*> Result;
    
    for (const TWeakObjectPtr<UISMPhysicsComponent>& CompPtr : CachedPhysicsComponents)
    {
        if (UISMPhysicsComponent* Comp = CompPtr.Get())
        {
            Result.Add(Comp);
        }
    }
    
    return Result;
}

void UISMPhysicsInstigatorComponent::QueryComponent(UISMPhysicsComponent* PhysicsComponent, TArray<int32>& OutInstances)
{
    if (!PhysicsComponent)
    {
        return;
    }
    
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return;
    }
    
    const FVector OwnerLocation = GetComponentLocation();
    
    // Query instances within radius
    OutInstances = PhysicsComponent->GetInstancesInRadius(OwnerLocation, QueryRadius, false);
}

float UISMPhysicsInstigatorComponent::CalculateImpactForce() const
{
    if (!bUseVelocityBasedForce)
    {
        // Use constant force
        return ImpactForceMultiplier * 100.0f; // Base force of 100
    }
    
    // Calculate force from velocity and mass: F = m * v
    const float Mass = GetOwnerMassForCalculation();
    const float VelocityMagnitude = CachedVelocity.Size();
    const float Force = Mass * VelocityMagnitude * ImpactForceMultiplier;
    
    return Force;
}

void UISMPhysicsInstigatorComponent::UpdateVelocity(float DeltaTime)
{
    AActor* Owner = GetOwner();
    if (!Owner || DeltaTime <= 0.0f)
    {
        CachedVelocity = FVector::ZeroVector;
        return;
    }
    
    // Try to get velocity from primitive component first
    if (CachedRootPrimitive.IsValid())
    {
        if (CachedRootPrimitive->IsSimulatingPhysics())
        {
            CachedVelocity = CachedRootPrimitive->GetPhysicsLinearVelocity();
            LastPosition = Owner->GetActorLocation();
            return;
        }
    }
    
    // Fall back to manual calculation
    const FVector CurrentPosition = Owner->GetActorLocation();
    CachedVelocity = (CurrentPosition - LastPosition) / DeltaTime;
    LastPosition = CurrentPosition;
}

FVector UISMPhysicsInstigatorComponent::GetOwnerVelocity() const
{
    return CachedVelocity;
}

float UISMPhysicsInstigatorComponent::GetOwnerMassForCalculation() const
{
    // Use configured mass if set
    if (OwnerMass > 0.0f)
    {
        return OwnerMass;
    }
    
    // Try to get mass from root primitive
    if (CachedRootPrimitive.IsValid())
    {
        const float PrimitiveMass = CachedRootPrimitive->GetMass();
        if (PrimitiveMass > 0.0f)
        {
            return PrimitiveMass;
        }
    }
    
    // Default mass for characters/actors without physics
    return 80.0f; // ~average human mass in kg
}

bool UISMPhysicsInstigatorComponent::PassesFilter(UISMPhysicsComponent* PhysicsComponent, int32 InstanceIndex) const
{
    if (!PhysicsComponent)
    {
        return false;
    }
    
    // Get instance tags
    const FGameplayTagContainer InstanceTags = PhysicsComponent->GetInstanceTags(InstanceIndex);
    
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
    
    return true;
}

// ===== Collision-Based Detection =====

void UISMPhysicsInstigatorComponent::OnOwnerHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
    UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (!bIsEnabled)
    {
        return;
    }
    
    // This is primarily for physics query mode
    // ISM components will have collision enabled, and we can detect direct hits
    
    // For now, this is a placeholder - ISM spatial mode doesn't rely on collision events
    // Physics engine mode could use this to detect direct hits on ISM instances
    
    UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsInstigatorComponent::OnOwnerHit - Hit detected"));
}

void UISMPhysicsInstigatorComponent::BindCollisionEvents()
{
    if (CachedRootPrimitive.IsValid())
    {
        // Bind to hit event
        if (!CachedRootPrimitive->OnComponentHit.IsBound())
        {
            CachedRootPrimitive->OnComponentHit.AddDynamic(this, &UISMPhysicsInstigatorComponent::OnOwnerHit);
            UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsInstigatorComponent::BindCollisionEvents - Bound to OnComponentHit"));
        }
    }
}

void UISMPhysicsInstigatorComponent::UnbindCollisionEvents()
{
    if (CachedRootPrimitive.IsValid())
    {
        CachedRootPrimitive->OnComponentHit.RemoveDynamic(this, &UISMPhysicsInstigatorComponent::OnOwnerHit);
        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsInstigatorComponent::UnbindCollisionEvents - Unbound from OnComponentHit"));
    }
}

// ===== Debug Visualization =====

#if WITH_EDITOR

void UISMPhysicsInstigatorComponent::DrawDebugInfo() const
{
    UWorld* World = GetWorld();
    AActor* Owner = GetOwner();
    if (!World || !Owner)
    {
        return;
    }
    
    const FVector OwnerLocation = GetComponentLocation();
    
    float r = GetQueryRadius();
    // Draw query radius sphere
    if (bShowDebugSphere)
    {
        DrawDebugSphere(World, OwnerLocation,r, 16, FColor::Green, false, -1.0f, 0, 2.0f);
    }
    
    // Draw lines to nearby instances
    if (bShowDebugLines)
    {
        TArray<UISMPhysicsComponent*> PhysicsComponents = const_cast<UISMPhysicsInstigatorComponent*>(this)->GetPhysicsComponents();
        
        for (UISMPhysicsComponent* PhysicsComponent : PhysicsComponents)
        {
            if (!PhysicsComponent)
            {
                continue;
            }
            
            // Get nearby instances
            TArray<int32> NearbyInstances = PhysicsComponent->GetInstancesInRadius(OwnerLocation, r, false);
            
            for (int32 InstanceIndex : NearbyInstances)
            {
                const FVector InstanceLocation = PhysicsComponent->GetInstanceLocation(InstanceIndex);
                const FColor LineColor = PassesFilter(PhysicsComponent, InstanceIndex) ? FColor::Yellow : FColor::Red;
                
                DrawDebugLine(World, OwnerLocation, InstanceLocation, LineColor, false, -1.0f, 0, 1.0f);
            }
        }
    }
    
    // Draw velocity vector
    const float VelocityMagnitude = CachedVelocity.Size();
    if (VelocityMagnitude > MinimumVelocity)
    {
        const FVector VelocityEnd = OwnerLocation + CachedVelocity.GetSafeNormal() * 200.0f;
        DrawDebugDirectionalArrow(World, OwnerLocation, VelocityEnd, 30.0f, FColor::Cyan, false, -1.0f, 0, 3.0f);
    }
    
    // Draw debug text
    const FString DebugText = FString::Printf(
        TEXT("Velocity: %.1f cm/s\nForce: %.1f\nComponents: %d"),
        VelocityMagnitude,
        CalculateImpactForce(),
        CachedPhysicsComponents.Num()
    );
    
    DrawDebugString(World, OwnerLocation + FVector(0, 0, 120), DebugText, nullptr, FColor::White, -1.0f, true);
}

#endif // WITH_EDITOR

// ===== Utility =====


void UISMPhysicsInstigatorComponent::SetInstigatorEnabled(bool bEnabled)
{
    if (bIsEnabled == bEnabled)
    {
        return;
    }
    
    bIsEnabled = bEnabled;
    
    UE_LOG(LogTemp, Log, TEXT("UISMPhysicsInstigatorComponent::SetInstigatorEnabled - %s"),
        bIsEnabled ? TEXT("Enabled") : TEXT("Disabled"));
}

float UISMPhysicsInstigatorComponent::GetQueryRadius() const
{
    return QueryRadius;
}


