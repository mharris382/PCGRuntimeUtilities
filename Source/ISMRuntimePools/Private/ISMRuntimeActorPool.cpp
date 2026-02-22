#include "ISMRuntimeActorPool.h"
#include "Interfaces/ISMPoolable.h"
#include "ISMPoolDataAsset.h"
#include "ISMInstanceHandle.h"
#include "Logging/LogMacros.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TimerManager.h"

// ===== Initialization =====

void FISMRuntimeActorPool::Initialize(TSubclassOf<AActor> InActorClass, UISMPoolDataAsset* InConfig, UWorld* InWorld)
{
    if (!InActorClass || !InConfig || !InWorld)
    {
        UE_LOG(LogTemp, Error, TEXT("FISMRuntimeActorPool::Initialize - Invalid parameters! ActorClass=%s, Config=%s, World=%s"),
            InActorClass ? *InActorClass->GetName() : TEXT("nullptr"),
            InConfig ? *InConfig->GetName() : TEXT("nullptr"),
            InWorld ? *InWorld->GetName() : TEXT("nullptr"));
        return;
    }

    ActorClass = InActorClass;
    PoolConfig = InConfig;
    OwningWorld = InWorld;

    // Initialize statistics
    Stats = FISMPoolStats();
    Stats.CreationTime = InWorld->GetTimeSeconds();

    UE_LOG(LogTemp, Log, TEXT("FISMRuntimeActorPool::Initialize - Pool created for %s with initial size %d"),
        *ActorClass->GetName(), InConfig->InitialPoolSize);
}

int32 FISMRuntimeActorPool::PreWarm()
{
    if (!ValidateOperation(TEXT("PreWarm")))
    {
        return 0;
    }

    const int32 TargetSize = PoolConfig->InitialPoolSize;
    int32 SpawnedCount = 0;

    const double StartTime = FPlatformTime::Seconds();

    for (int32 i = 0; i < TargetSize; ++i)
    {
        if (AActor* Actor = SpawnPoolActor(true))
        {
            AvailableActors.Add(Actor);
            SpawnedCount++;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::PreWarm - Failed to spawn actor %d/%d for class %s"),
                i + 1, TargetSize, *ActorClass->GetName());
        }
    }

    const double ElapsedTime = (FPlatformTime::Seconds() - StartTime) * 1000.0; // Convert to ms

    Stats.TotalActors = SpawnedCount;
    Stats.AvailableActors = SpawnedCount;

    UE_LOG(LogTemp, Log, TEXT("FISMRuntimeActorPool::PreWarm - Spawned %d actors for %s in %.2fms"),
        SpawnedCount, *ActorClass->GetName(), ElapsedTime);

    return SpawnedCount;
}

// ===== Core Pool Operations =====

AActor* FISMRuntimeActorPool::RequestActor(UISMPoolDataAsset* DataAsset, const FISMInstanceHandle& InstanceHandle)
{
    if (!ValidateOperation(TEXT("RequestActor")))
    {
        return nullptr;
    }

    // Update last access frame
    if (UWorld* World = OwningWorld.Get())
    {
        Stats.LastAccessFrame = GFrameCounter;
    }

    AActor* Actor = nullptr;

    // Try to get an available actor
    if (AvailableActors.Num() > 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("FISMRuntimeActorPool::RequestActor - %d available actors in pool for %s"), AvailableActors.Num(), *ActorClass->GetName());
        // Get from end for better cache performance
        TWeakObjectPtr<AActor> ActorPtr = AvailableActors.Pop();
        Actor = ActorPtr.Get();

        if (!Actor)
        {
            // Actor was destroyed externally, try next
            CleanupInvalidActors();
			UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::RequestActor - Found null actor in available list, cleaning up and trying again"));
            return RequestActor(DataAsset, InstanceHandle); // Recursive call
        }
    }
    else
    {
        // Pool exhausted - try to grow
        if (CanGrow())
        {
            UE_LOG(LogTemp, Verbose, TEXT("FISMRuntimeActorPool::RequestActor - Pool exhausted, growing by %d actors"),
                PoolConfig->PoolGrowSize);

            const int32 Grown = GrowPool();
            Stats.GrowCount++;

            if (Grown > 0 && AvailableActors.Num() > 0)
            {
                TWeakObjectPtr<AActor> ActorPtr = AvailableActors.Pop();
                Actor = ActorPtr.Get();
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::RequestActor - Pool exhausted and cannot grow! MaxPoolSize=%d"), PoolConfig->MaxPoolSize);
            if (ActiveActors.Num() > 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::RequestActor - Active actors:"));
                for (const TWeakObjectPtr<AActor>& ActorPtr : ActiveActors)
                {
                    if (AActor* ActiveActor = ActorPtr.Get())
                    {
                        UE_LOG(LogTemp, Warning, TEXT("  - %s"), *ActiveActor->GetName());
						return ActiveActor; // Return an active actor as a last resort (potentially unsafe)
                    }
				}
            }
            return nullptr;
        }
    }

    if (!Actor)
    {
        UE_LOG(LogTemp, Error, TEXT("FISMRuntimeActorPool::RequestActor - Failed to get or spawn actor"));
        return nullptr;
    }

    // Move to active list
    ActiveActors.Add(Actor);

    // Update stats
    Stats.TotalRequests++;
    Stats.ActiveActors = ActiveActors.Num();
    Stats.AvailableActors = AvailableActors.Num();
    Stats.PeakActiveActors = FMath::Max(Stats.PeakActiveActors, Stats.ActiveActors);

    // Call IISMPoolable::OnRequestedFromPool if implemented
    if (IISMPoolable* Poolable = Cast<IISMPoolable>(Actor))
    {
        Poolable->Execute_OnRequestedFromPool(Actor, DataAsset, InstanceHandle);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("FISMRuntimeActorPool::RequestActor - Actor %s does not implement IISMPoolable"),
            *Actor->GetName());
    }

    return Actor;
}

bool FISMRuntimeActorPool::ReturnActor(AActor* Actor, FTransform& OutFinalTransform, bool& bUpdateTransform)
{
    if (!Actor)
    {
        UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::ReturnActor - Null actor provided"));
        return false;
    }

    if (!ValidateOperation(TEXT("ReturnActor")))
    {
		UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::ReturnActor - Invalid pool state, cannot return actor"));
        return false;
    }
	UE_LOG(LogTemp, Verbose, TEXT("FISMRuntimeActorPool::ReturnActor - Returning actor %s to pool"), *Actor->GetName());
	if (!ActiveActors.Contains(Actor) )
    {
        if (!AvailableActors.Contains(Actor))
        {
            UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::ReturnActor - Actor %s is not active or availble??"), *Actor->GetName());
            // Update stats
            Stats.TotalReturns++;
            Stats.ActiveActors = ActiveActors.Num();
            Stats.AvailableActors = AvailableActors.Num();
			AvailableActors.Add(Actor);
        }
        UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::ReturnActor - Actor %s is already returned to the pool"),*Actor->GetName());
        return false;
    }

	if (!ContainsActor(Actor))
    {
        UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::ReturnActor - Actor %s does not belong to this pool!"), *Actor->GetName());
        return false;
    }

    // Update last access frame
    Stats.LastAccessFrame = GFrameCounter;

    // Initialize return parameters
    OutFinalTransform = FTransform::Identity;
    bUpdateTransform = false;

    // Call IISMPoolable::OnReturnedToPool if implemented
    if (IISMPoolable* Poolable = Cast<IISMPoolable>(Actor))
    {
        Poolable->Execute_OnReturnedToPool(Actor, OutFinalTransform, bUpdateTransform);
    }

    // Reset actor to clean state
    ResetActor(Actor);

    // Move from active to available
    ActiveActors.Remove(Actor); // Don't shrink array for performance
    AvailableActors.Add(Actor);

    // Update stats
    Stats.TotalReturns++;
    Stats.ActiveActors = ActiveActors.Num();
    Stats.AvailableActors = AvailableActors.Num();

    // Check for leaks periodically (every 100 returns)
    if (Stats.TotalReturns % 100 == 0 && Stats.HasLeak())
    {
        UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::ReturnActor - Potential leak detected! Requests=%d, Returns=%d, Leaked=%d"),
            Stats.TotalRequests, Stats.TotalReturns, Stats.GetLeakCount());
    }

    return true;
}

bool FISMRuntimeActorPool::ContainsActor(AActor* Actor) const
{
    if (!Actor)
    {
        return false;
    }
    return AllActors.Contains(Actor);
}

// ===== Pool Management =====

int32 FISMRuntimeActorPool::GrowPool(int32 Count)
{
    if (!ValidateOperation(TEXT("GrowPool")))
    {
        return 0;
    }

    if (!CanGrow())
    {
        UE_LOG(LogTemp, Warning, TEXT("FISMRuntimeActorPool::GrowPool - Cannot grow, MaxPoolSize reached (%d)"),
            PoolConfig->MaxPoolSize);
        return 0;
    }

    // Use configured grow size if not specified
    if (Count <= 0)
    {
        Count = PoolConfig->PoolGrowSize;
    }

    // Respect MaxPoolSize limit
    const int32 CurrentTotal = Stats.TotalActors;
    const int32 MaxSize = PoolConfig->MaxPoolSize;
    if (MaxSize > 0 && CurrentTotal + Count > MaxSize)
    {
        Count = MaxSize - CurrentTotal;
        if (Count <= 0)
        {
            return 0;
        }
    }

    int32 SpawnedCount = 0;
    for (int32 i = 0; i < Count; ++i)
    {
        if (AActor* Actor = SpawnPoolActor(false))
        {
            AvailableActors.Add(Actor);
            SpawnedCount++;
        }
    }

    // Update stats
    Stats.TotalActors += SpawnedCount;
    Stats.AvailableActors = AvailableActors.Num();

    if (SpawnedCount > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("FISMRuntimeActorPool::GrowPool - Grew pool by %d actors (Total: %d)"),
            SpawnedCount, Stats.TotalActors);
    }

    return SpawnedCount;
}

int32 FISMRuntimeActorPool::ShrinkPool(bool bForceImmediate)
{
    if (!ValidateOperation(TEXT("ShrinkPool")))
    {
        return 0;
    }

    // Check if shrinking is allowed
    if (!PoolConfig->bAllowPoolShrinking && !bForceImmediate)
    {
        return 0;
    }

    // Check if we should shrink based on utilization
    if (!bForceImmediate && !ShouldShrink())
    {
        return 0;
    }

    const int32 TargetDestroyCount = GetShrinkCandidateCount();
    if (TargetDestroyCount <= 0)
    {
        return 0;
    }

    int32 DestroyedCount = 0;

    // Only destroy available actors, never active ones
    for (int32 i = AvailableActors.Num() - 1; i >= 0 && DestroyedCount < TargetDestroyCount; --i)
    {
        if (AActor* Actor = AvailableActors[i].Get())
        {
            // Call cleanup before destroying
            if (IISMPoolable* Poolable = Cast<IISMPoolable>(Actor))
            {
                Poolable->Execute_OnPoolDestroyed(Actor);
            }

            Actor->Destroy();
            DestroyedCount++;
        }

        AvailableActors.RemoveAt(i);
    }

    // Update stats
    Stats.TotalActors -= DestroyedCount;
    Stats.AvailableActors = AvailableActors.Num();
    Stats.ShrinkCount++;

    if (DestroyedCount > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("FISMRuntimeActorPool::ShrinkPool - Destroyed %d unused actors (Total: %d)"),
            DestroyedCount, Stats.TotalActors);
    }

    return DestroyedCount;
}

void FISMRuntimeActorPool::Cleanup()
{
    UE_LOG(LogTemp, Log, TEXT("FISMRuntimeActorPool::Cleanup - Destroying pool with %d actors"), Stats.TotalActors);

    // Destroy all actors (both active and available)
    auto DestroyActors = [](TArray<TWeakObjectPtr<AActor>>& Actors)
        {
            for (TWeakObjectPtr<AActor>& ActorPtr : Actors)
            {
                if (AActor* Actor = ActorPtr.Get())
                {
                    // Call cleanup notification
                    if (IISMPoolable* Poolable = Cast<IISMPoolable>(Actor))
                    {
                        Poolable->Execute_OnPoolDestroyed(Actor);
                    }

                    Actor->Destroy();
                }
            }
            Actors.Empty();
        };

    DestroyActors(ActiveActors);
    DestroyActors(AvailableActors);

    // Reset stats
    Stats = FISMPoolStats();
}

// ===== Queries =====

bool FISMRuntimeActorPool::IsValid() const
{
    return ActorClass != nullptr &&
        PoolConfig.IsValid() &&
        OwningWorld.IsValid();
}

bool FISMRuntimeActorPool::CanGrow() const
{
    if (!PoolConfig.IsValid())
    {
        return false;
    }

    const int32 MaxSize = PoolConfig->MaxPoolSize;
    if (MaxSize <= 0)
    {
        return true; // Unlimited growth
    }

    return Stats.TotalActors < MaxSize;
}

bool FISMRuntimeActorPool::ShouldShrink() const
{
    if (!PoolConfig.IsValid() || !PoolConfig->bAllowPoolShrinking)
    {
        return false;
    }

    const float Utilization = Stats.GetUtilization();
    const float UnusedPercentage = 1.0f - Utilization;

    return UnusedPercentage >= PoolConfig->ShrinkThreshold;
}

int32 FISMRuntimeActorPool::GetShrinkCandidateCount() const
{
    if (!PoolConfig.IsValid())
    {
        return 0;
    }

    // Calculate how many actors to destroy to reach target utilization
    const int32 TargetAvailable = FMath::CeilToInt(Stats.TotalActors * (1.0f - PoolConfig->ShrinkThreshold));
    const int32 ExcessCount = Stats.AvailableActors - TargetAvailable;

    return FMath::Max(0, ExcessCount);
}

// ===== Internal Helpers =====

AActor* FISMRuntimeActorPool::SpawnPoolActor(bool bIsPreWarm)
{
    UWorld* World = OwningWorld.Get();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("FISMRuntimeActorPool::SpawnPoolActor - World is invalid"));
        return nullptr;
    }

    if (!ActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("FISMRuntimeActorPool::SpawnPoolActor - ActorClass is null"));
        return nullptr;
    }

    // Spawn at world origin, hidden
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.bDeferConstruction = false;
    SpawnParams.ObjectFlags |= RF_Transient; // Don't save pooled actors

    AActor* Actor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
	
    if (!Actor)
    {
        UE_LOG(LogTemp, Error, TEXT("FISMRuntimeActorPool::SpawnPoolActor - Failed to spawn actor of class %s"),
            *ActorClass->GetName());
        return nullptr;
    }

    AllActors.Add(Actor);

    // Apply default reset to ensure clean state
    ApplyDefaultReset(Actor);

    // Call OnPoolSpawned if actor implements IISMPoolable
    if (IISMPoolable* Poolable = Cast<IISMPoolable>(Actor))
    {
        Poolable->Execute_OnPoolSpawned(Actor);
    }

    return Actor;
}

void FISMRuntimeActorPool::ResetActor(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    // If actor implements IISMPoolable, it handled its own reset in OnReturnedToPool
    // Just apply default reset for safety
    if (!Actor->Implements<UISMPoolable>())
    {
        ApplyDefaultReset(Actor);
    }
    else
    {
        // Still apply some default cleanup even for IISMPoolable actors
        // This ensures basic state is clean even if implementation forgot something
        Actor->SetActorHiddenInGame(true);
        Actor->SetActorEnableCollision(false);
    }
}

void FISMRuntimeActorPool::ApplyDefaultReset(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    // Teleport to world origin
    Actor->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::ResetPhysics);
    Actor->SetActorRotation(FRotator::ZeroRotator, ETeleportType::ResetPhysics);
    Actor->SetActorScale3D(FVector::OneVector);

    // Disable rendering
    Actor->SetActorHiddenInGame(true);

    // Disable collision
    Actor->SetActorEnableCollision(false);

    // Stop physics if it has a root component
    if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
    {
        if (RootPrimitive->IsSimulatingPhysics())
        {
            RootPrimitive->SetSimulatePhysics(false);
            RootPrimitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
            RootPrimitive->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        }
    }

    // Disable ticking to save CPU
    Actor->SetActorTickEnabled(false);
}

void FISMRuntimeActorPool::UpdateStats()
{
    Stats.ActiveActors = ActiveActors.Num();
    Stats.AvailableActors = AvailableActors.Num();
    Stats.TotalActors = Stats.ActiveActors + Stats.AvailableActors;
}

void FISMRuntimeActorPool::CleanupInvalidActors()
{
    // Remove null weak pointers from both arrays
    AvailableActors.RemoveAll([](const TWeakObjectPtr<AActor>& ActorPtr)
        {
            return !ActorPtr.IsValid();
        });

    ActiveActors.RemoveAll([](const TWeakObjectPtr<AActor>& ActorPtr)
        {
            return !ActorPtr.IsValid();
        });

    // Update stats after cleanup
    UpdateStats();
}

bool FISMRuntimeActorPool::ValidateOperation(const FString& OperationName) const
{
    if (!ActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("FISMRuntimeActorPool::%s - ActorClass is null"), *OperationName);
        return false;
    }

    if (!PoolConfig.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("FISMRuntimeActorPool::%s - PoolConfig is invalid"), *OperationName);
        return false;
    }

    if (!OwningWorld.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("FISMRuntimeActorPool::%s - OwningWorld is invalid"), *OperationName);
        return false;
    }

    return true;
}