#pragma once

#include "CoreMinimal.h"
#include "ISMRuntimeActorPool.generated.h"


// Forward declarations
class AActor;
class UISMPoolDataAsset;
class UWorld;

/**
 * Statistics for a single actor pool.
 * Used for monitoring, debugging, and optimization.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMEPOOLS_API FISMPoolStats
{
    GENERATED_BODY()

    /** Total number of actors spawned in this pool */
    UPROPERTY(BlueprintReadOnly, Category = "Pool Stats")
    int32 TotalActors = 0;

    /** Number of actors currently in use (not available) */
    UPROPERTY(BlueprintReadOnly, Category = "Pool Stats")
    int32 ActiveActors = 0;

    /** Number of actors available for use */
    UPROPERTY(BlueprintReadOnly, Category = "Pool Stats")
    int32 AvailableActors = 0;

    /** Highest number of concurrent active actors ever */
    UPROPERTY(BlueprintReadOnly, Category = "Pool Stats")
    int32 PeakActiveActors = 0;

    /** Total number of times actors were requested from this pool */
    UPROPERTY(BlueprintReadOnly, Category = "Pool Stats")
    int32 TotalRequests = 0;

    /** Total number of times actors were returned to this pool */
    UPROPERTY(BlueprintReadOnly, Category = "Pool Stats")
    int32 TotalReturns = 0;

    /** Number of times the pool had to grow (ran out of available actors) */
    UPROPERTY(BlueprintReadOnly, Category = "Pool Stats")
    int32 GrowCount = 0;

    /** Number of times the pool shrank (destroyed unused actors) */
    UPROPERTY(BlueprintReadOnly, Category = "Pool Stats")
    int32 ShrinkCount = 0;

    /** Frame number when this pool was last accessed (for stale pool cleanup) */
    uint64 LastAccessFrame = 0;

    /** Time when pool was created */
    double CreationTime = 0.0;

    /** Check if there's a leak (more requests than returns) */
    bool HasLeak() const { return TotalRequests > TotalReturns; }

    /** Get number of leaked actors */
    int32 GetLeakCount() const { return TotalRequests - TotalReturns; }

    /** Check if pool is currently exhausted (no available actors) */
    bool IsExhausted() const { return AvailableActors == 0; }

    /** Get pool utilization (0.0 to 1.0) */
    float GetUtilization() const { return TotalActors > 0 ? (float)ActiveActors / TotalActors : 0.0f; }
};

/**
 * Pool manager for a single actor class.
 *
 * This is a non-UObject struct that handles the actual pooling logic.
 * One FISMRuntimeActorPool exists per unique actor class in the subsystem.
 *
 * Responsibilities:
 * - Pre-warming: Spawn initial actors
 * - Request handling: Return available actors or spawn new ones
 * - Return handling: Reset actors and mark as available
 * - Growth: Spawn additional actors when exhausted
 * - Shrinking: Destroy unused actors to reclaim memory
 * - Statistics: Track usage for monitoring and optimization
 *
 * Thread Safety: This struct is NOT thread-safe. All operations must occur on game thread.
 */
USTRUCT()
struct ISMRUNTIMEPOOLS_API FISMRuntimeActorPool
{
    GENERATED_BODY()

    // ===== Configuration =====

    /** The class of actor this pool manages */
    TSubclassOf<AActor> ActorClass;

    /** Configuration from data asset */
    TWeakObjectPtr<UISMPoolDataAsset> PoolConfig;

    /** World this pool belongs to */
    TWeakObjectPtr<UWorld> OwningWorld;

    // ===== Actor Storage =====

    /** Actors ready to be used */
    TArray<TWeakObjectPtr<AActor>> AvailableActors;

    /** Actors currently in use (not in pool) */
    TArray<TWeakObjectPtr<AActor>> ActiveActors;


    TSet<AActor*> AllActors;

    // ===== Statistics =====

    /** Runtime statistics for this pool */
    FISMPoolStats Stats;

    // ===== Constructor & Initialization =====

    FISMRuntimeActorPool() = default;

    /**
     * Initialize the pool with configuration and world context.
     *
     * @param InActorClass - The class of actor to pool
     * @param InConfig - Data asset containing pool configuration
     * @param InWorld - World to spawn actors in
     */
    void Initialize(TSubclassOf<AActor> InActorClass, UISMPoolDataAsset* InConfig, UWorld* InWorld);

    /**
     * Pre-spawn initial actors to populate the pool.
     * Called automatically during initialization.
     *
     * @return Number of actors successfully spawned
     */
    int32 PreWarm();

    // ===== Core Pool Operations =====

    /**
     * Request an actor from the pool.
     *
     * Returns an available actor if one exists, otherwise spawns a new one
     * (if within MaxPoolSize limit). Automatically calls IISMPoolable::OnRequestedFromPool
     * if the actor implements the interface.
     *
     * @param DataAsset - Data asset to pass to actor's OnRequestedFromPool (can be more specific than PoolConfig)
     * @param InstanceHandle - Optional instance handle to pass to actor
     * @return Actor ready for use, or nullptr if pool exhausted and can't grow
     */
    AActor* RequestActor(UISMPoolDataAsset* DataAsset, const struct FISMInstanceHandle& InstanceHandle);

    /**
     * Return an actor to the pool.
     *
     * Resets the actor to a clean state and marks it as available for reuse.
     * Automatically calls IISMPoolable::OnReturnedToPool if the actor implements the interface.
     *
     * @param Actor - The actor to return
     * @param OutFinalTransform - Filled with actor's final transform if bUpdateTransform is true
     * @param bUpdateTransform - Whether to capture the actor's final transform
     * @return True if successfully returned, false if actor wasn't from this pool
     */
    bool ReturnActor(AActor* Actor, FTransform& OutFinalTransform, bool& bUpdateTransform);

    /**
     * Check if an actor belongs to this pool.
     *
     * @param Actor - Actor to check
     * @return True if actor is in this pool (active or available)
     */
    bool ContainsActor(AActor* Actor) const;

    // ===== Pool Management =====

    /**
     * Grow the pool by spawning additional actors.
     * Called automatically when RequestActor finds no available actors.
     *
     * @param Count - Number of actors to spawn (defaults to PoolGrowSize from config)
     * @return Number of actors successfully spawned
     */
    int32 GrowPool(int32 Count = -1);

    /**
     * Shrink the pool by destroying unused actors.
     * Only destroys available actors, never active ones.
     * Only executes if bAllowPoolShrinking is true and utilization below threshold.
     *
     * @param bForceImmediate - Ignore threshold and destroy excess actors immediately
     * @return Number of actors destroyed
     */
    int32 ShrinkPool(bool bForceImmediate = false);

    /**
     * Clean up the entire pool, destroying all actors.
     * Called when pool is no longer needed or during world cleanup.
     * Calls IISMPoolable::OnPoolDestroyed on all actors before destruction.
     */
    void Cleanup();

    // ===== Queries =====

    /** Get current pool statistics */
    const FISMPoolStats& GetStats() const { return Stats; }

    /** Check if pool is currently valid and operational */
    bool IsValid() const;

    /** Check if pool can grow (hasn't reached MaxPoolSize) */
    bool CanGrow() const;

    /** Check if pool should shrink (based on utilization threshold) */
    bool ShouldShrink() const;

    /** Get the number of actors that would be destroyed if shrunk now */
    int32 GetShrinkCandidateCount() const;

private:
    // ===== Internal Helpers =====

    /**
     * Spawn a single actor for this pool.
     *
     * @param bIsPreWarm - True if spawning during pre-warm, false if spawning during growth
     * @return Spawned actor, or nullptr if spawn failed
     */
    AActor* SpawnPoolActor(bool bIsPreWarm);

    /**
     * Reset an actor to clean state for return to pool.
     * Uses IISMPoolable interface if available, otherwise applies default reset.
     *
     * @param Actor - Actor to reset
     */
    void ResetActor(AActor* Actor);

    /**
     * Apply default reset behavior for actors that don't implement IISMPoolable.
     * - Teleport to world origin
     * - Disable collision
     * - Hide all components
     * - Stop physics
     */
    void ApplyDefaultReset(AActor* Actor);

    /**
     * Update statistics after a request/return operation.
     */
    void UpdateStats();

    /**
     * Remove invalid weak pointers from actor arrays.
     * Called periodically to clean up destroyed actors.
     */
    void CleanupInvalidActors();

    /**
     * Check if pool configuration allows the requested operation.
     * Logs warnings if configuration is invalid.
     */
    bool ValidateOperation(const FString& OperationName) const;
};