#include "ISMPhysicsComponent.h"
#include "ISMPhysicsDataAsset.h"
#include "ISMInstanceDataAsset.h"
#include "ISMPhysicsActor.h"
#include "ISMRuntimePoolSubsystem.h"
#include "ISMInstanceHandle.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

UISMPhysicsComponent::UISMPhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}


namespace
{
    void LogGeminiCurseWarning(const FString& ComponentName)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsComponent '%s' - Gemini Curse is enabled! This allows multiple physics actors per instance but can cause visual and gameplay issues. Use with caution."), *ComponentName);
	}
}

// ===== Lifecycle =====

void UISMPhysicsComponent::BeginPlay()
{
    Super::BeginPlay();
    
    // Get pool subsystem
    if (UWorld* World = GetWorld())
    {
        PoolSubsystem = World->GetSubsystem<UISMRuntimePoolSubsystem>();
        
        if (!PoolSubsystem.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("UISMPhysicsComponent::BeginPlay - Failed to get pool subsystem!"));
        }
    }
    
    // Validate configuration
    if (!PhysicsData)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsComponent::BeginPlay - PhysicsData not set on %s!"), 
            *GetOwner()->GetName());
    }
    
    UE_LOG(LogTemp, Log, TEXT("UISMPhysicsComponent::BeginPlay - Physics component initialized (Query Mode: %s, Limiters: %s)"),
        QueryMode == EPhysicsQueryMode::ISMSpatial ? TEXT("ISM Spatial") : TEXT("Physics Engine"),
        bEnableLimiters ? TEXT("Enabled") : TEXT("Disabled"));
}

void UISMPhysicsComponent::EndPlay(const EEndPlayReason::Type EndReason)
{
    // Return all actors to pool
    ReturnAllToISM(true);
    
    Super::EndPlay(EndReason);
}

void UISMPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    if (!bEnableLimiters)
    {
        return;
    }
    
    // Increment frame counter
    LimiterCheckFrameCounter++;
    
    // Only check limiters every N frames
    if (LimiterCheckFrameCounter >= LimiterCheckInterval)
    {
        LimiterCheckFrameCounter = 0;
        
        // Enforce distance limits
        if (MaxSimulationDistance > 0.0f)
        {
            EnforceDistanceLimits();
        }
        
        // Enforce lifetime limits
        if (MaxLifetime > 0.0f)
        {
            EnforceLifetimeLimits();
        }
        
        // Cleanup invalid actors
        CleanupInvalidActors();
    }
    
#if WITH_EDITOR
    if (bShowDebugInfo)
    {
        DrawDebugVisualization();
    }
#endif
}

void UISMPhysicsComponent::OnInstanceDataAssigned()
{
    if(!InstanceData)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsComponent::OnInstanceDataAssigned - Assigned null InstanceData on %s"), *GetOwner()->GetName());
        return;
	}
	check(InstanceData);
	PhysicsData = Cast<UISMPhysicsDataAsset>(InstanceData);
    if(!PhysicsData)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsComponent::OnInstanceDataAssigned - Assigned InstanceData is not a UISMPhysicsDataAsset on %s"), *GetOwner()->GetName());
	}
    check(PhysicsData);
}

void UISMPhysicsComponent::OnInitializationComplete()
{
    Super::OnInitializationComplete();
    
    // Additional physics-specific initialization
    UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::OnInitializationComplete - %s"), *GetOwner()->GetName());
}

void UISMPhysicsComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    // Cleanup all physics actors
    ReturnAllToISM(false);
    
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

// ===== Conversion Management =====

AActor* UISMPhysicsComponent::ConvertInstanceToPhysics(int32 InstanceIndex, FVector ImpactPoint,
    FVector ImpactNormal, float ImpactForce, AActor* Instigator)
{
    // Validate instance
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsComponent::ConvertInstanceToPhysics - Invalid instance index %d"), InstanceIndex);
        return nullptr;
    }

    if (bApplyGeminiCurse)
        LogGeminiCurseWarning(GetOwner()->GetName());

    // Check if instance is already converted (ignore safety check if Gemini Curse enabled, as multiple actors per instance is allowed in that case)
    if (!bApplyGeminiCurse && IsInstanceConverted(InstanceIndex))
    {
        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::ConvertInstanceToPhysics - Instance %d already converted"), InstanceIndex);
        return nullptr;
    }

    // Check if conversion should be allowed
    if (!ShouldAllowConversion(InstanceIndex, ImpactForce))
    {
        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::ConvertInstanceToPhysics - Conversion denied by limiters"));
        return nullptr;
    }

    // Handle overflow if at max concurrent actors
    if (IsAtMaxConcurrentActors())
    {
        HandleActorOverflow();
    }

    // Get instance handle
    FISMInstanceHandle Handle = GetInstanceHandle(InstanceIndex);
    if (!Handle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("UISMPhysicsComponent::ConvertInstanceToPhysics - Failed to get instance handle"));
        return nullptr;
    }
    
    if (bApplyGeminiCurse && Handle.GetConvertedActor())
        HandlePrevPooledActorIfGeminiCursed(Handle);

    // Spawn physics actor from pool
    AISMPhysicsActor* PhysicsActor = SpawnPhysicsActorFromPool(Handle);
    if (!PhysicsActor)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMPhysicsComponent::ConvertInstanceToPhysics - Failed to spawn physics actor"));
        return nullptr;
    }
    
    if(!bApplyGeminiCurse)
    {
        // Hide the ISM instance (visual is now represented by physics actor)
        HideInstance(InstanceIndex, false); // Don't update bounds for performance

        // Mark the handle as converted (this tracks conversion state)
        Handle.SetConvertedActor(PhysicsActor);
    }
    
    // Apply conversion impulse
    ApplyConversionImpulse(PhysicsActor, ImpactPoint, ImpactNormal, ImpactForce);
    
    // Track the actor
    if (AISMPhysicsActor* TypedActor = Cast<AISMPhysicsActor>(PhysicsActor))
    {
        ActivePhysicsActors.Add(TypedActor);
        RegisterActorReturnCallback(TypedActor);
    }
    
    // Update stats
    TotalConversions++;
    
    // Broadcast event
    OnPhysicsConversion.Broadcast(InstanceIndex, PhysicsActor);
    
    UE_LOG(LogTemp, Log, TEXT("UISMPhysicsComponent::ConvertInstanceToPhysics - Converted instance %d (Active: %d, Total: %d)"),
        InstanceIndex, ActivePhysicsActors.Num(), TotalConversions);
    
    return PhysicsActor;
}

void UISMPhysicsComponent::ReturnAllToISM(bool bUpdateTransforms)
{
    UE_LOG(LogTemp, Log, TEXT("UISMPhysicsComponent::ReturnAllToISM - Returning %d actors"), ActivePhysicsActors.Num());
    
    // Return all active physics actors
    for (const TWeakObjectPtr<AISMPhysicsActor>& ActorPtr : ActivePhysicsActors)
    {
        if (AISMPhysicsActor* Actor = ActorPtr.Get())
        {
            Actor->ReturnToISM();
        }
    }
    
    // Clear the array
    ActivePhysicsActors.Empty();
}

TArray<AActor*> UISMPhysicsComponent::GetActivePhysicsActors() const
{
    TArray<AActor*> Result;
    
    for (const TWeakObjectPtr<AISMPhysicsActor>& ActorPtr : ActivePhysicsActors)
    {
        if (AISMPhysicsActor* Actor = ActorPtr.Get())
        {
            Result.Add(Actor);
        }
    }
    
    return Result;
}

bool UISMPhysicsComponent::IsAtMaxConcurrentActors() const
{
    if (!bEnableLimiters || MaxConcurrentActors <= 0)
    {
        return false;
    }
    
    return ActivePhysicsActors.Num() >= MaxConcurrentActors;
}

bool UISMPhysicsComponent::ShouldAllowConversion(int32 InstanceIndex, float ImpactForce) const
{
    // Check force threshold
    if (PhysicsData && ImpactForce < PhysicsData->ConversionForceThreshold)
    {
        return false;
    }
    
    // Check if instance is destroyed
    if (IsInstanceDestroyed(InstanceIndex))
    {
        return false;
    }
    
    // Distance check (if limiters enabled)
    if (bEnableLimiters && MaxSimulationDistance > 0.0f)
    {
        const FVector InstanceLocation = GetInstanceLocation(InstanceIndex);
        const FVector CameraLocation = GetCameraLocation();
        const float Distance = FVector::Dist(InstanceLocation, CameraLocation);
        
        if (Distance > MaxSimulationDistance)
        {
            return false;
        }
    }
    
    return true;
}

// ===== Conversion Helpers =====

AISMPhysicsActor* UISMPhysicsComponent::SpawnPhysicsActorFromPool(const FISMInstanceHandle& InstanceHandle)
{
    if (!PoolSubsystem.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("UISMPhysicsComponent::SpawnPhysicsActorFromPool - Pool subsystem invalid"));
        return nullptr;
    }
    
    if (!PhysicsData)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMPhysicsComponent::SpawnPhysicsActorFromPool - PhysicsData not set"));
        return nullptr;
    }
    
    // Get pooled actor class from data asset
    TSubclassOf<AActor> ActorClass = PhysicsData->PooledActorClass;
    if (!ActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMPhysicsComponent::SpawnPhysicsActorFromPool - PooledActorClass not set in data asset"));
        return nullptr;
    }
	//TODO: verify that the class is of subtype ISMPhysicsActor for safety
    
    // Request actor from pool
    AActor* Actor = PoolSubsystem->RequestActor(ActorClass, PhysicsData, InstanceHandle);
    if (!Actor)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsComponent::SpawnPhysicsActorFromPool - Pool exhausted or failed to spawn"));
    }
	AISMPhysicsActor* ISMActor = Cast<AISMPhysicsActor>(Actor);
    if (!ISMActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsComponent::Spawned Actor was not of Type ISMPhysicsActor!"));
    }
	ISMActor->SetInstanceHandle(InstanceHandle);
    return ISMActor;
}

void UISMPhysicsComponent::HandleActorOverflow()
{
    if (ActivePhysicsActors.Num() == 0)
    {
        return;
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::HandleActorOverflow - Handling overflow (Behavior: %d)"), 
        static_cast<int32>(ActorOverflowBehavior));
    
    switch (ActorOverflowBehavior)
    {
        case EActorOverflowBehavior::IgnoreNew:
            // Do nothing - just don't allow new conversions
            break;
            
        case EActorOverflowBehavior::ReturnOldest:
            ReturnOldestActor();
            break;
            
        case EActorOverflowBehavior::ReturnFarthest:
            ReturnFarthestActor();
            break;
    }
}

void UISMPhysicsComponent::ReturnOldestActor()
{
    if (ActivePhysicsActors.Num() == 0)
    {
        return;
    }
    
    // First actor in array is oldest (since we add to end)
    if (AISMPhysicsActor* OldestActor = ActivePhysicsActors[0].Get())
    {
        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::ReturnOldestActor - Returning oldest actor"));
        OldestActor->ReturnToISM();
    }
}

void UISMPhysicsComponent::ReturnFarthestActor()
{
    if (ActivePhysicsActors.Num() == 0)
    {
        return;
    }
    
    const FVector CameraLocation = GetCameraLocation();
    
    AISMPhysicsActor* FarthestActor = nullptr;
    float FarthestDistanceSq = 0.0f;
    
    for (const TWeakObjectPtr<AISMPhysicsActor>& ActorPtr : ActivePhysicsActors)
    {
        if (AISMPhysicsActor* Actor = ActorPtr.Get())
        {
            const float DistanceSq = FVector::DistSquared(Actor->GetActorLocation(), CameraLocation);
            if (DistanceSq > FarthestDistanceSq)
            {
                FarthestDistanceSq = DistanceSq;
                FarthestActor = Actor;
            }
        }
    }
    
    if (FarthestActor)
    {
        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::ReturnFarthestActor - Returning farthest actor (Distance: %.1f)"),
            FMath::Sqrt(FarthestDistanceSq));
        FarthestActor->ReturnToISM();
    }
}

void UISMPhysicsComponent::ApplyConversionImpulse(AActor* PhysicsActor, FVector ImpactPoint, 
    FVector ImpactNormal, float ImpactForce)
{
    if (!PhysicsActor)
    {
        return;
    }
    
    // Get root primitive component
    UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(PhysicsActor->GetRootComponent());
    if (!RootPrimitive || !RootPrimitive->IsSimulatingPhysics())
    {
        return;
    }
    
    // Calculate impulse vector
    const FVector ImpulseDirection = ImpactNormal.GetSafeNormal();
    const FVector Impulse = ImpulseDirection * ImpactForce;
    
    // Apply impulse at impact point
    RootPrimitive->AddImpulseAtLocation(Impulse, ImpactPoint);
    
    UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::ApplyConversionImpulse - Applied impulse: %.1f at %s"),
        ImpactForce, *ImpactPoint.ToString());
}

// ===== Performance Limiters =====

void UISMPhysicsComponent::EnforceDistanceLimits()
{
    if (ActivePhysicsActors.Num() == 0)
    {
        return;
    }
    
    const FVector CameraLocation = GetCameraLocation();
    const float MaxDistanceSq = MaxSimulationDistance * MaxSimulationDistance;
    
    TArray<AISMPhysicsActor*> ActorsToReturn;
    
    for (const TWeakObjectPtr<AISMPhysicsActor>& ActorPtr : ActivePhysicsActors)
    {
        if (AISMPhysicsActor* Actor = ActorPtr.Get())
        {
            const float DistanceSq = FVector::DistSquared(Actor->GetActorLocation(), CameraLocation);
            if (DistanceSq > MaxDistanceSq)
            {
                ActorsToReturn.Add(Actor);
            }
        }
    }
    
    // Return actors beyond distance
    for (AISMPhysicsActor* Actor : ActorsToReturn)
    {
        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::EnforceDistanceLimits - Returning actor beyond max distance"));
        Actor->ReturnToISM();
    }
}

void UISMPhysicsComponent::EnforceLifetimeLimits()
{
    if (ActivePhysicsActors.Num() == 0)
    {
        return;
    }
    
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    
    const double CurrentTime = World->GetTimeSeconds();
    TArray<AISMPhysicsActor*> ActorsToReturn;
    
    for (const TWeakObjectPtr<AISMPhysicsActor>& ActorPtr : ActivePhysicsActors)
    {
        if (AISMPhysicsActor* Actor = ActorPtr.Get())
        {
            // Check if actor has exceeded max lifetime
            // We can't access TimeActivated directly, but we can use a workaround
            // by tracking when we added the actor (or rely on the actor's internal tracking)
            
            // For now, we'll assume the actor handles its own lifetime tracking
            // and will return itself via ReturnToISM when appropriate
            
            // This is a placeholder - in a real implementation, you might want to
            // track activation time in a map or have the actor expose its activation time
        }
    }
}

FVector UISMPhysicsComponent::GetCameraLocation() const
{
    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
            {
                return CameraManager->GetCameraLocation();
            }
        }
    }
    
    // Fallback to world origin
    return FVector::ZeroVector;
}

// ===== Cleanup =====

void UISMPhysicsComponent::CleanupInvalidActors()
{
    // Remove null/invalid actors from tracking
    ActivePhysicsActors.RemoveAll([](const TWeakObjectPtr<AISMPhysicsActor>& ActorPtr)
    {
        return !ActorPtr.IsValid();
    });
}

void UISMPhysicsComponent::RegisterActorReturnCallback(AISMPhysicsActor* PhysicsActor)
{
    if (!PhysicsActor)
    {
        return;
    }
    // Bind to the instance handle's return delegate
    const FISMInstanceHandle& Handle = PhysicsActor->GetInstanceHandle();
    if (Handle.IsValid())
    {
        if (!bApplyGeminiCurse)
        {
            OnInstanceReturnedToISM.BindLambda([this, InstanceIndex = Handle.InstanceIndex](const FISMInstanceHandle& ReturnedHandle, const FTransform& FinalTransform)
                {
                    if (ReturnedHandle.InstanceIndex == InstanceIndex)
                    {
                        ShowInstance(InstanceIndex, false); // Unhide the ISM instance (if it was hidden)
                        // Update stats
                        TotalReturns++;

                        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::OnInstanceReturnedToISM - Instance %d returned to ISM (Active: %d, Total Returns: %d)"),
                            InstanceIndex, ActivePhysicsActors.Num(), TotalReturns);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("UISMPhysicsComponent::OnInstanceReturnedToISM - Returned instance index %d does not match expected %d"),
                            ReturnedHandle.InstanceIndex, InstanceIndex);
                    }
                });
        }
        else 
        {
			LogGeminiCurseWarning(GetOwner()->GetName());
            
            OnInstanceReturnedToISM.BindLambda([this, InstanceIndex = Handle.InstanceIndex](const FISMInstanceHandle& ReturnedHandle, const FTransform& FinalTransform)
                {
                    if (ReturnedHandle.InstanceIndex == InstanceIndex)
                    {
                        LogGeminiCurseWarning(GetOwner()->GetName());

						AddInstance(FinalTransform, false); // Add a new instance to the ISM (since we are duplicating instead of removing/restoring)
                        
                        // Update stats
                        TotalReturns++;

                        UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::OnInstanceReturnedToISM - Instance %d returned to ISM (Active: %d, Total Returns: %d)"),
                            InstanceIndex, ActivePhysicsActors.Num(), TotalReturns);
                    }
                });
        }
    }
    // When actor returns to ISM, we need to remove it from our tracking
    // This is handled automatically via the instance handle's delegates
    // The actor will call ReturnToISM on its handle, which triggers the pool return
    
    // We could bind to OnInstanceReturnedToISM delegate here if needed
    // For now, we rely on CleanupInvalidActors to remove returned actors
}

// ===== Debug Visualization =====

#if WITH_EDITOR

void UISMPhysicsComponent::DrawDebugVisualization() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    
    // Draw all active physics actors
    for (const TWeakObjectPtr<AISMPhysicsActor>& ActorPtr : ActivePhysicsActors)
    {
        if (const AISMPhysicsActor* Actor = ActorPtr.Get())
        {
            const FVector Location = Actor->GetActorLocation();
            
            // Draw sphere at actor location
            DrawDebugSphere(World, Location, 50.0f, 12, DebugColor, false, -1.0f, 0, 2.0f);
            
            // Draw line to camera
            const FVector CameraLocation = GetCameraLocation();
            DrawDebugLine(World, Location, CameraLocation, FColor::Cyan, false, -1.0f, 0, 1.0f);
        }
    }
    
    // Draw distance limit sphere (if enabled)
    if (bEnableLimiters && MaxSimulationDistance > 0.0f)
    {
        const FVector CameraLocation = GetCameraLocation();
        DrawDebugSphere(World, CameraLocation, MaxSimulationDistance, 32, FColor::Orange, false, -1.0f, 0, 1.0f);
    }
    
    // Draw stats text
    const FString StatsText = FString::Printf(
        TEXT("Active: %d\nTotal Conversions: %d\nTotal Returns: %d\nQuery Mode: %s"),
        ActivePhysicsActors.Num(),
        TotalConversions,
        TotalReturns,
        QueryMode == EPhysicsQueryMode::ISMSpatial ? TEXT("ISM Spatial") : TEXT("Physics Engine")
    );
    
    const FVector OwnerLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
    DrawDebugString(World, OwnerLocation + FVector(0, 0, 200), StatsText, nullptr, FColor::Yellow, -1.0f, true);
}

#endif // WITH_EDITOR



void UISMPhysicsComponent::HandlePrevPooledActorIfGeminiCursed(const FISMInstanceHandle& Handle)
{
    //lets just return the actor to the pool immediately, since we are allowing multiple actors per instance, this will just be a visual refresh and won't cause gameplay issues
    if (AActor* Actor = Handle.GetConvertedActor())
    {
        if (AISMPhysicsActor* PhysicsActor = Cast<AISMPhysicsActor>(Actor))
        {
            UE_LOG(LogTemp, Verbose, TEXT("UISMPhysicsComponent::OnActorWillConvert_GeminiCursed - Returning existing actor for instance %d before converting new one"), Handle.InstanceIndex);
            PhysicsActor->ReturnToISM();
        }
    }
}
