#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ISMRuntimeActorPool.h"
#include "ISMRuntimePoolSubsystem.generated.h"

// Forward declarations
class UISMPoolDataAsset;
class UISMRuntimeComponent;
struct FISMInstanceHandle;

/**
 * Configuration for automatic pool cleanup behavior.
 */
USTRUCT(BlueprintType)
struct FISMPoolCleanupConfig
{
    GENERATED_BODY()

    /** Enable automatic cleanup of stale pools */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool Cleanup")
    bool bEnableStalePoolCleanup = true;

    /** Number of frames a pool can be unused before considered stale */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool Cleanup",
        meta = (EditCondition = "bEnableStalePoolCleanup", ClampMin = "100"))
    int32 StaleFrameThreshold = 1000;

    /** How often to check for stale pools (in seconds) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool Cleanup",
        meta = (EditCondition = "bEnableStalePoolCleanup", ClampMin = "1.0"))
    float CleanupCheckInterval = 60.0f;
};

/**
 * Global statistics for all pools in the world.
 */
USTRUCT(BlueprintType)
struct FISMGlobalPoolStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Global Pool Stats")
    int32 TotalPools = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Global Pool Stats")
    int32 TotalActorsSpawned = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Global Pool Stats")
    int32 TotalActiveActors = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Global Pool Stats")
    int32 TotalAvailableActors = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Global Pool Stats")
    int32 TotalLeakedActors = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Global Pool Stats")
    float AverageUtilization = 0.0f;
};

/**
 * World subsystem that manages all actor pools.
 *
 * Responsibilities:
 * - Create and manage pools for different actor classes
 * - Provide global access to pooled actors
 * - Track statistics across all pools
 * - Cleanup stale/unused pools
 * - Integration point for ISMRuntimeComponent
 *
 * Architecture:
 * - One pool per unique TSubclassOf<AActor>
 * - Pools are created on-demand when first requested
 * - Components request pools via this subsystem
 * - Subsystem handles pool lifecycle (creation, cleanup)
 *
 * Usage:
 * ```cpp
 * UISMRuntimePoolSubsystem* PoolSubsystem = World->GetSubsystem<UISMRuntimePoolSubsystem>();
 * AActor* Actor = PoolSubsystem->RequestActor(MyActorClass, MyDataAsset, InstanceHandle);
 * // Use actor...
 * PoolSubsystem->ReturnActor(Actor, FinalTransform, true);
 * ```
 */
UCLASS()
class ISMRUNTIMEPOOLS_API UISMRuntimePoolSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // ===== Subsystem Lifecycle =====

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

    // ===== Pool Management =====

    /**
     * Get existing pool for an actor class, or create a new one if it doesn't exist.
     *
     * @param ActorClass - The class of actor to pool
     * @param Config - Configuration for the pool (used only if creating new pool)
     * @return Pointer to the pool, or nullptr if creation failed
     */
    FISMRuntimeActorPool* GetOrCreatePool(TSubclassOf<AActor> ActorClass, UISMPoolDataAsset* Config);

    /**
     * Get existing pool for an actor class.
     *
     * @param ActorClass - The class of actor to find
     * @return Pointer to the pool, or nullptr if pool doesn't exist
     */
    FISMRuntimeActorPool* GetPool(TSubclassOf<AActor> ActorClass);

    /**
     * Check if a pool exists for an actor class.
     *
     * @param ActorClass - The class to check
     * @return True if pool exists
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    bool HasPool(TSubclassOf<AActor> ActorClass) const;

    /**
     * Destroy a specific pool, cleaning up all its actors.
     *
     * @param ActorClass - The class whose pool to destroy
     * @return True if pool was found and destroyed
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    bool DestroyPool(TSubclassOf<AActor> ActorClass);

    /**
     * Destroy all pools in the subsystem.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    void DestroyAllPools();

    // ===== Actor Request/Return =====

    /**
     * Request an actor from the pool for a specific actor class.
     *
     * If no pool exists, one will be created using the provided config.
     * If the pool is exhausted, it may grow automatically based on configuration.
     *
     * @param ActorClass - Class of actor to request
     * @param DataAsset - Data asset to configure the actor (passed to IISMPoolable::OnRequestedFromPool)
     * @param InstanceHandle - Instance handle to pass to actor (may be invalid if not from ISM conversion)
     * @return Actor ready for use, or nullptr if request failed
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    AActor* RequestActor(TSubclassOf<AActor> ActorClass, UISMPoolDataAsset* DataAsset, const FISMInstanceHandle& InstanceHandle);

    /**
     * Return an actor to its pool.
     *
     * Automatically finds the correct pool based on actor class.
     *
     * @param Actor - Actor to return
     * @param OutFinalTransform - Filled with actor's final transform if applicable
     * @param bUpdateTransform - Set to true if the final transform should be captured
     * @return True if successfully returned to pool
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    bool ReturnActor(AActor* Actor, FTransform& OutFinalTransform, bool& bUpdateTransform);

    /**
     * Find which pool an actor belongs to.
     *
     * @param Actor - Actor to search for
     * @return Pool containing the actor, or nullptr if not found in any pool
     */
    FISMRuntimeActorPool* FindPoolForActor(AActor* Actor);

    // ===== Statistics =====

    /**
     * Get statistics for a specific pool.
     *
     * @param ActorClass - Class whose pool stats to retrieve
     * @param OutStats - Filled with pool statistics
     * @return True if pool exists and stats retrieved
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    bool GetPoolStats(TSubclassOf<AActor> ActorClass, FISMPoolStats& OutStats) const;

    /**
     * Get global statistics for all pools.
     *
     * @return Aggregated statistics across all pools
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    FISMGlobalPoolStats GetGlobalStats() const;

    /**
     * Get statistics for all pools as a map.
     *
     * @return Map of actor class to pool statistics
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    TMap<TSubclassOf<AActor>, FISMPoolStats> GetAllPoolStats() const;

    /**
     * Check if any pools have detected leaks.
     *
     * @param OutLeakedPools - Filled with classes that have leaked actors
     * @return True if any leaks detected
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    bool DetectLeaks(TArray<TSubclassOf<AActor>>& OutLeakedPools) const;

    // ===== Cleanup & Maintenance =====

    /**
     * Clean up stale pools that haven't been used recently.
     * Called automatically based on cleanup configuration.
     *
     * @return Number of pools destroyed
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    int32 CleanupStalePools();

    /**
     * Shrink all pools that are below their utilization threshold.
     *
     * @return Total number of actors destroyed across all pools
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    int32 ShrinkAllPools();

    /**
     * Get/set cleanup configuration.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    const FISMPoolCleanupConfig& GetCleanupConfig() const { return CleanupConfig; }

    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    void SetCleanupConfig(const FISMPoolCleanupConfig& NewConfig);

    // ===== Component Integration =====

    /**
     * Register a runtime component with the subsystem.
     * Components should call this during BeginPlay if they use pooling.
     *
     * @param Component - Component to register
     */
    void RegisterComponent(UISMRuntimeComponent* Component);

    /**
     * Unregister a runtime component from the subsystem.
     * Components should call this during EndPlay.
     *
     * @param Component - Component to unregister
     */
    void UnregisterComponent(UISMRuntimeComponent* Component);

    /**
     * Get all registered components.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools")
    TArray<UISMRuntimeComponent*> GetRegisteredComponents() const;

protected:
    // ===== Internal State =====

    /** Map of actor class to its pool */
    UPROPERTY()
    TMap<TSubclassOf<AActor>, FISMRuntimeActorPool> ActorPools;

    /** Registered components that use pooling */
    UPROPERTY()
    TArray<TWeakObjectPtr<UISMRuntimeComponent>> RegisteredComponents;

    /** Cleanup configuration */
    UPROPERTY()
    FISMPoolCleanupConfig CleanupConfig;

    /** Timer handle for periodic cleanup checks */
    FTimerHandle CleanupTimerHandle;

    /** Cached global stats (updated when queried) */
    mutable FISMGlobalPoolStats CachedGlobalStats;

    /** Frame when global stats were last calculated */
    mutable uint64 GlobalStatsUpdateFrame = 0;

    // ===== Helper Functions =====

    /**
     * Start automatic cleanup timer.
     */
    void StartCleanupTimer();

    /**
     * Stop automatic cleanup timer.
     */
    void StopCleanupTimer();

    /**
     * Timer callback for periodic cleanup.
     */
    void OnCleanupTimer();

    /**
     * Update cached global statistics.
     */
    void UpdateGlobalStats() const;

    /**
     * Validate pool configuration before creating a pool.
     *
     * @param Config - Configuration to validate
     * @return True if valid
     */
    bool ValidatePoolConfig(UISMPoolDataAsset* Config) const;

    /**
     * Log pool creation for debugging.
     */
    void LogPoolCreation(TSubclassOf<AActor> ActorClass, const FISMPoolStats& Stats) const;

    /**
     * Log pool destruction for debugging.
     */
    void LogPoolDestruction(TSubclassOf<AActor> ActorClass, const FISMPoolStats& Stats) const;

public:
    // ===== Debug/Testing =====

#if WITH_EDITOR
    /**
     * Print detailed statistics for all pools to log.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools|Debug")
    void PrintPoolStatistics() const;

    /**
     * Validate all pools are in a consistent state.
     * Used for debugging and testing.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Pools|Debug")
    bool ValidateAllPools() const;
#endif
};