#include "ISMRuntimePoolSubsystem.h"
#include "ISMPoolDataAsset.h"
#include "Interfaces/ISMPoolable.h"
#include "ISMRuntimeComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

// ===== Subsystem Lifecycle =====

void UISMRuntimePoolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::Initialize - Pool subsystem initialized for world %s"),
        *GetWorld()->GetName());

    // Start automatic cleanup timer if enabled
    if (CleanupConfig.bEnableStalePoolCleanup)
    {
        StartCleanupTimer();
    }
}

void UISMRuntimePoolSubsystem::Deinitialize()
{
    UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::Deinitialize - Destroying all pools"));

    // Stop cleanup timer
    StopCleanupTimer();

    // Destroy all pools
    DestroyAllPools();

    // Clear registered components
    RegisteredComponents.Empty();

    Super::Deinitialize();
}

bool UISMRuntimePoolSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    // Support game and PIE worlds, not editor preview worlds
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

// ===== Pool Management =====

FISMRuntimeActorPool* UISMRuntimePoolSubsystem::GetOrCreatePool(TSubclassOf<AActor> ActorClass, UISMPoolDataAsset* Config)
{
    if (!ActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::GetOrCreatePool - ActorClass is null"));
        return nullptr;
    }

    if (!Config)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::GetOrCreatePool - Config is null"));
        return nullptr;
    }

    // Check if pooling is enabled for this asset
    if (!Config->bEnablePooling)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMRuntimePoolSubsystem::GetOrCreatePool - Pooling disabled for %s"),
            *Config->GetName());
        return nullptr;
    }

    // Check if pool already exists
    if (FISMRuntimeActorPool* ExistingPool = ActorPools.Find(ActorClass))
    {
        return ExistingPool;
    }

    // Validate configuration
    if (!ValidatePoolConfig(Config))
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::GetOrCreatePool - Invalid pool configuration for %s"),
            *ActorClass->GetName());
        return nullptr;
    }

    // Create new pool
    FISMRuntimeActorPool NewPool;
    NewPool.Initialize(ActorClass, Config, GetWorld());

    // Pre-warm the pool
    const int32 SpawnedCount = NewPool.PreWarm();
    if (SpawnedCount == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMRuntimePoolSubsystem::GetOrCreatePool - Failed to pre-warm pool for %s"),
            *ActorClass->GetName());
    }

    // Add to map
    FISMRuntimeActorPool& AddedPool = ActorPools.Add(ActorClass, MoveTemp(NewPool));

    // Log creation
    LogPoolCreation(ActorClass, AddedPool.GetStats());

    return &AddedPool;
}

FISMRuntimeActorPool* UISMRuntimePoolSubsystem::GetPool(TSubclassOf<AActor> ActorClass)
{
    if (!ActorClass)
    {
        return nullptr;
    }

    return ActorPools.Find(ActorClass);
}

bool UISMRuntimePoolSubsystem::HasPool(TSubclassOf<AActor> ActorClass) const
{
    return ActorClass && ActorPools.Contains(ActorClass);
}

bool UISMRuntimePoolSubsystem::DestroyPool(TSubclassOf<AActor> ActorClass)
{
    if (!ActorClass)
    {
        return false;
    }

    FISMRuntimeActorPool* Pool = ActorPools.Find(ActorClass);
    if (!Pool)
    {
        return false;
    }

    // Log destruction before cleanup
    LogPoolDestruction(ActorClass, Pool->GetStats());

    // Cleanup the pool
    Pool->Cleanup();

    // Remove from map
    ActorPools.Remove(ActorClass);

    return true;
}

void UISMRuntimePoolSubsystem::DestroyAllPools()
{
    UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::DestroyAllPools - Destroying %d pools"), ActorPools.Num());

    // Cleanup all pools
    for (auto& Pair : ActorPools)
    {
        LogPoolDestruction(Pair.Key, Pair.Value.GetStats());
        Pair.Value.Cleanup();
    }

    // Clear the map
    ActorPools.Empty();
}

// ===== Actor Request/Return =====

AActor* UISMRuntimePoolSubsystem::RequestActor(TSubclassOf<AActor> ActorClass, UISMPoolDataAsset* DataAsset, const FISMInstanceHandle& InstanceHandle)
{
    if (!ActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::RequestActor - ActorClass is null"));
        return nullptr;
    }

    if (!DataAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::RequestActor - DataAsset is null"));
        return nullptr;
    }

    // Get or create pool
    FISMRuntimeActorPool* Pool = GetOrCreatePool(ActorClass, DataAsset);
    if (!Pool)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::RequestActor - Failed to get or create pool for %s"),
            *ActorClass->GetName());
        return nullptr;
    }

    // Request actor from pool
    AActor* Actor = Pool->RequestActor(DataAsset, InstanceHandle);

    if (!Actor)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMRuntimePoolSubsystem::RequestActor - Pool exhausted for %s"),
            *ActorClass->GetName());
    }

    return Actor;
}

bool UISMRuntimePoolSubsystem::ReturnActor(AActor* Actor, FTransform& OutFinalTransform, bool& bUpdateTransform)
{
    if (!Actor)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMRuntimePoolSubsystem::ReturnActor - Actor is null"));
        return false;
    }

    // Find which pool this actor belongs to
    FISMRuntimeActorPool* Pool = FindPoolForActor(Actor);
    if (!Pool)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMRuntimePoolSubsystem::ReturnActor - Could not find pool for actor %s"),
            *Actor->GetName());
        return false;
    }

    // Return to pool
    return Pool->ReturnActor(Actor, OutFinalTransform, bUpdateTransform);
}

FISMRuntimeActorPool* UISMRuntimePoolSubsystem::FindPoolForActor(AActor* Actor)
{
    if (!Actor)
    {
        return nullptr;
    }

    UE_LOG(LogTemp, Warning, TEXT("FindPoolForActor: Looking for %s (Class: %s), Pools: %d"),
        *Actor->GetName(), *Actor->GetClass()->GetName(), ActorPools.Num());

    for (auto& Pair : ActorPools)
    {
        UE_LOG(LogTemp, Warning, TEXT("  Pool key class: %s, ContainsActor: %s"),
            *Pair.Key->GetName(),
            Pair.Value.ContainsActor(Actor) ? TEXT("YES") : TEXT("NO"));
    }

    // Search all pools for this actor
    for (auto& Pair : ActorPools)
    {
        if (Pair.Value.ContainsActor(Actor))
        {
            return &Pair.Value;
        }
    }

    return nullptr;
}

// ===== Statistics =====

bool UISMRuntimePoolSubsystem::GetPoolStats(TSubclassOf<AActor> ActorClass, FISMPoolStats& OutStats) const
{
    if (!ActorClass)
    {
        return false;
    }

    const FISMRuntimeActorPool* Pool = ActorPools.Find(ActorClass);
    if (!Pool)
    {
        return false;
    }

    OutStats = Pool->GetStats();
    return true;
}

FISMGlobalPoolStats UISMRuntimePoolSubsystem::GetGlobalStats() const
{
    // Use cached stats if they're from the current frame
    if (GlobalStatsUpdateFrame == GFrameCounter)
    {
        return CachedGlobalStats;
    }

    // Update cached stats
    UpdateGlobalStats();

    return CachedGlobalStats;
}

TMap<TSubclassOf<AActor>, FISMPoolStats> UISMRuntimePoolSubsystem::GetAllPoolStats() const
{
    TMap<TSubclassOf<AActor>, FISMPoolStats> AllStats;

    for (const auto& Pair : ActorPools)
    {
        AllStats.Add(Pair.Key, Pair.Value.GetStats());
    }

    return AllStats;
}

bool UISMRuntimePoolSubsystem::DetectLeaks(TArray<TSubclassOf<AActor>>& OutLeakedPools) const
{
    OutLeakedPools.Empty();

    for (const auto& Pair : ActorPools)
    {
        if (Pair.Value.GetStats().HasLeak())
        {
            OutLeakedPools.Add(Pair.Key);
        }
    }

    return OutLeakedPools.Num() > 0;
}

// ===== Cleanup & Maintenance =====

int32 UISMRuntimePoolSubsystem::CleanupStalePools()
{
    if (!CleanupConfig.bEnableStalePoolCleanup)
    {
        return 0;
    }

    const uint64 CurrentFrame = GFrameCounter;
    const uint64 FrameThreshold = CleanupConfig.StaleFrameThreshold;

    TArray<TSubclassOf<AActor>> PoolsToDestroy;

    // Find stale pools
    for (const auto& Pair : ActorPools)
    {
        const FISMPoolStats& Stats = Pair.Value.GetStats();
        const uint64 FramesSinceLastAccess = CurrentFrame - Stats.LastAccessFrame;
        if(Pair.Value.ActiveActors.Num() > 0)
        {
            continue; // Skip pools that are still active
		}
        if (FramesSinceLastAccess > FrameThreshold)
        {
            PoolsToDestroy.Add(Pair.Key);
        }
    }

    // Destroy stale pools
    for (TSubclassOf<AActor> ActorClass : PoolsToDestroy)
    {
        UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::CleanupStalePools - Destroying stale pool for %s"),
            *ActorClass->GetName());
        DestroyPool(ActorClass);
    }

    return PoolsToDestroy.Num();
}

int32 UISMRuntimePoolSubsystem::ShrinkAllPools()
{
    int32 TotalDestroyed = 0;

    for (auto& Pair : ActorPools)
    {
        const int32 Destroyed = Pair.Value.ShrinkPool(false);
        TotalDestroyed += Destroyed;
    }

    if (TotalDestroyed > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::ShrinkAllPools - Destroyed %d unused actors across all pools"),
            TotalDestroyed);
    }

    return TotalDestroyed;
}

void UISMRuntimePoolSubsystem::SetCleanupConfig(const FISMPoolCleanupConfig& NewConfig)
{
    const bool bWasEnabled = CleanupConfig.bEnableStalePoolCleanup;
    const bool bIsEnabled = NewConfig.bEnableStalePoolCleanup;

    CleanupConfig = NewConfig;

    // Restart timer if settings changed
    if (bWasEnabled != bIsEnabled)
    {
        if (bIsEnabled)
        {
            StartCleanupTimer();
        }
        else
        {
            StopCleanupTimer();
        }
    }
    else if (bIsEnabled)
    {
        // Interval might have changed, restart timer
        StopCleanupTimer();
        StartCleanupTimer();
    }
}

// ===== Component Integration =====

void UISMRuntimePoolSubsystem::RegisterComponent(UISMRuntimeComponent* Component)
{
    if (!Component)
    {
        return;
    }

    RegisteredComponents.AddUnique(Component);

    UE_LOG(LogTemp, Verbose, TEXT("UISMRuntimePoolSubsystem::RegisterComponent - Registered component %s (Total: %d)"),
        *Component->GetName(), RegisteredComponents.Num());
}

void UISMRuntimePoolSubsystem::UnregisterComponent(UISMRuntimeComponent* Component)
{
    if (!Component)
    {
        return;
    }

    RegisteredComponents.Remove(Component);

    UE_LOG(LogTemp, Verbose, TEXT("UISMRuntimePoolSubsystem::UnregisterComponent - Unregistered component %s (Total: %d)"),
        *Component->GetName(), RegisteredComponents.Num());
}

TArray<UISMRuntimeComponent*> UISMRuntimePoolSubsystem::GetRegisteredComponents() const
{
    TArray<UISMRuntimeComponent*> ValidComponents;

    for (const TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : RegisteredComponents)
    {
        if (UISMRuntimeComponent* Comp = CompPtr.Get())
        {
            ValidComponents.Add(Comp);
        }
    }

    return ValidComponents;
}

// ===== Helper Functions =====

void UISMRuntimePoolSubsystem::StartCleanupTimer()
{
    if (UWorld* World = GetWorld())
    {
        if (!CleanupTimerHandle.IsValid())
        {
            World->GetTimerManager().SetTimer(
                CleanupTimerHandle,
                this,
                &UISMRuntimePoolSubsystem::OnCleanupTimer,
                CleanupConfig.CleanupCheckInterval,
                true // Loop
            );

            UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::StartCleanupTimer - Started cleanup timer (Interval: %.1fs)"),
                CleanupConfig.CleanupCheckInterval);
        }
    }
}

void UISMRuntimePoolSubsystem::StopCleanupTimer()
{
    if (UWorld* World = GetWorld())
    {
        if (CleanupTimerHandle.IsValid())
        {
            World->GetTimerManager().ClearTimer(CleanupTimerHandle);
            CleanupTimerHandle.Invalidate();

            UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::StopCleanupTimer - Stopped cleanup timer"));
        }
    }
}

void UISMRuntimePoolSubsystem::OnCleanupTimer()
{
    UE_LOG(LogTemp, Verbose, TEXT("UISMRuntimePoolSubsystem::OnCleanupTimer - Running periodic cleanup"));

    // Cleanup stale pools
    const int32 DestroyedPools = CleanupStalePools();

    // Optionally shrink pools
    const int32 DestroyedActors = ShrinkAllPools();

    if (DestroyedPools > 0 || DestroyedActors > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::OnCleanupTimer - Destroyed %d pools, %d actors"),
            DestroyedPools, DestroyedActors);
    }
}

void UISMRuntimePoolSubsystem::UpdateGlobalStats() const
{
    FISMGlobalPoolStats Stats;
    Stats.TotalPools = ActorPools.Num();

    int32 TotalUtilizationSum = 0;
    int32 PoolsWithActors = 0;

    for (const auto& Pair : ActorPools)
    {
        const FISMPoolStats& PoolStats = Pair.Value.GetStats();

        Stats.TotalActorsSpawned += PoolStats.TotalActors;
        Stats.TotalActiveActors += PoolStats.ActiveActors;
        Stats.TotalAvailableActors += PoolStats.AvailableActors;
        Stats.TotalLeakedActors += PoolStats.GetLeakCount();

        if (PoolStats.TotalActors > 0)
        {
            TotalUtilizationSum += FMath::RoundToInt(PoolStats.GetUtilization() * 100.0f);
            PoolsWithActors++;
        }
    }

    // Calculate average utilization
    if (PoolsWithActors > 0)
    {
        Stats.AverageUtilization = TotalUtilizationSum / (float)PoolsWithActors / 100.0f;
    }

    // Cache results
    CachedGlobalStats = Stats;
    GlobalStatsUpdateFrame = GFrameCounter;
}

bool UISMRuntimePoolSubsystem::ValidatePoolConfig(UISMPoolDataAsset* Config) const
{
    if (!Config)
    {
        return false;
    }

    // Check if actor class is set
    if (!Config->PooledActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::ValidatePoolConfig - PooledActorClass is not set in %s"),
            *Config->GetName());
        return false;
    }

    // Check if actor class implements IISMPoolable (warning, not error)
    if (!Config->PooledActorClass->ImplementsInterface(UISMPoolable::StaticClass()))
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMRuntimePoolSubsystem::ValidatePoolConfig - Actor class %s does not implement IISMPoolable interface. Default reset will be used."),
            *Config->PooledActorClass->GetName());
    }

    // Validate pool sizes
    if (Config->InitialPoolSize < 0)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::ValidatePoolConfig - InitialPoolSize cannot be negative (%d)"),
            Config->InitialPoolSize);
        return false;
    }

    if (Config->PoolGrowSize <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::ValidatePoolConfig - PoolGrowSize must be positive (%d)"),
            Config->PoolGrowSize);
        return false;
    }

    if (Config->MaxPoolSize > 0 && Config->InitialPoolSize > Config->MaxPoolSize)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMRuntimePoolSubsystem::ValidatePoolConfig - InitialPoolSize (%d) exceeds MaxPoolSize (%d)"),
            Config->InitialPoolSize, Config->MaxPoolSize);
    }

    return true;
}

void UISMRuntimePoolSubsystem::LogPoolCreation(TSubclassOf<AActor> ActorClass, const FISMPoolStats& Stats) const
{
    UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::LogPoolCreation - Created pool for %s with %d actors"),
        *ActorClass->GetName(), Stats.TotalActors);
}

void UISMRuntimePoolSubsystem::LogPoolDestruction(TSubclassOf<AActor> ActorClass, const FISMPoolStats& Stats) const
{
    UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::LogPoolDestruction - Destroying pool for %s (Total: %d, Active: %d, Requests: %d, Returns: %d)"),
        *ActorClass->GetName(), Stats.TotalActors, Stats.ActiveActors, Stats.TotalRequests, Stats.TotalReturns);

    if (Stats.HasLeak())
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMRuntimePoolSubsystem::LogPoolDestruction - Pool has %d leaked actors!"),
            Stats.GetLeakCount());
    }
}

// ===== Debug/Testing =====

#if WITH_EDITOR

void UISMRuntimePoolSubsystem::PrintPoolStatistics() const
{
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    UE_LOG(LogTemp, Log, TEXT("ISM Pool Subsystem Statistics"));
    UE_LOG(LogTemp, Log, TEXT("========================================"));

    const FISMGlobalPoolStats GlobalStats = GetGlobalStats();

    UE_LOG(LogTemp, Log, TEXT("Global Stats:"));
    UE_LOG(LogTemp, Log, TEXT("  Total Pools: %d"), GlobalStats.TotalPools);
    UE_LOG(LogTemp, Log, TEXT("  Total Actors: %d"), GlobalStats.TotalActorsSpawned);
    UE_LOG(LogTemp, Log, TEXT("  Active Actors: %d"), GlobalStats.TotalActiveActors);
    UE_LOG(LogTemp, Log, TEXT("  Available Actors: %d"), GlobalStats.TotalAvailableActors);
    UE_LOG(LogTemp, Log, TEXT("  Leaked Actors: %d"), GlobalStats.TotalLeakedActors);
    UE_LOG(LogTemp, Log, TEXT("  Average Utilization: %.1f%%"), GlobalStats.AverageUtilization * 100.0f);
    UE_LOG(LogTemp, Log, TEXT(""));

    UE_LOG(LogTemp, Log, TEXT("Individual Pools:"));
    for (const auto& Pair : ActorPools)
    {
        const FISMPoolStats& Stats = Pair.Value.GetStats();
        const FString ClassName = Pair.Key->GetName();

        UE_LOG(LogTemp, Log, TEXT("  %s:"), *ClassName);
        UE_LOG(LogTemp, Log, TEXT("    Total: %d | Active: %d | Available: %d"),
            Stats.TotalActors, Stats.ActiveActors, Stats.AvailableActors);
        UE_LOG(LogTemp, Log, TEXT("    Peak: %d | Utilization: %.1f%%"),
            Stats.PeakActiveActors, Stats.GetUtilization() * 100.0f);
        UE_LOG(LogTemp, Log, TEXT("    Requests: %d | Returns: %d | Leaked: %d"),
            Stats.TotalRequests, Stats.TotalReturns, Stats.GetLeakCount());
        UE_LOG(LogTemp, Log, TEXT("    Grows: %d | Shrinks: %d"),
            Stats.GrowCount, Stats.ShrinkCount);

        if (Stats.HasLeak())
        {
            UE_LOG(LogTemp, Warning, TEXT("    ⚠ LEAK DETECTED!"));
        }

        UE_LOG(LogTemp, Log, TEXT(""));
    }

    UE_LOG(LogTemp, Log, TEXT("========================================"));
}

bool UISMRuntimePoolSubsystem::ValidateAllPools() const
{
    bool bAllValid = true;

    UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::ValidateAllPools - Validating %d pools"), ActorPools.Num());

    for (const auto& Pair : ActorPools)
    {
        const FISMRuntimeActorPool& Pool = Pair.Value;
        const FString ClassName = Pair.Key->GetName();

        // Check pool is valid
        if (!Pool.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("  Pool %s is invalid!"), *ClassName);
            bAllValid = false;
            continue;
        }

        const FISMPoolStats& Stats = Pool.GetStats();

        // Check for leaks
        if (Stats.HasLeak())
        {
            UE_LOG(LogTemp, Warning, TEXT("  Pool %s has %d leaked actors"),
                *ClassName, Stats.GetLeakCount());
        }

        // Check stats consistency
        const int32 ExpectedTotal = Stats.ActiveActors + Stats.AvailableActors;
        if (Stats.TotalActors != ExpectedTotal)
        {
            UE_LOG(LogTemp, Error, TEXT("  Pool %s has inconsistent stats! Total=%d, Expected=%d"),
                *ClassName, Stats.TotalActors, ExpectedTotal);
            bAllValid = false;
        }
    }

    if (bAllValid)
    {
        UE_LOG(LogTemp, Log, TEXT("UISMRuntimePoolSubsystem::ValidateAllPools - All pools valid ✓"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("UISMRuntimePoolSubsystem::ValidateAllPools - Validation failed ✗"));
    }

    return bAllValid;
}

#endif // WITH_EDITOR