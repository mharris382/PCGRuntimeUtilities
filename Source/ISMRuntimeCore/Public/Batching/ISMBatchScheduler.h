#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Batching/ISMBatchTypes.h"
#include "Batching/ISMBatchTransformer.h"
#include "ISMBatchScheduler.generated.h"


// Forward declarations
class UISMRuntimeComponent;
class UISMRuntimeSubsystem;


// ============================================================
//  Scheduler Settings
// ============================================================

/**
 * Configuration for the batch mutation scheduler.
 * Set on UISMBatchScheduler before any transformers are registered.
 * Can be overridden per-project via project settings (future work).
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMBatchSchedulerSettings
{
    GENERATED_BODY()

    /**
     * Hard cap on instances per snapshot chunk.
     * If a spatial cell contains more instances than this, it is split into sub-chunks.
     * Lower values = smaller async tasks = better parallelism but more scheduling overhead.
     * Default: 2048 (generous - most cells will be well under this).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Performance", meta = (ClampMin = "64"))
    int32 MaxInstancesPerChunk = 2048;

    /**
     * Seconds before an open FISMMutationHandle is force-released as abandoned.
     * Protects against transformers that stall (e.g., PCG graph hangs).
     * A warning is logged when a handle times out.
     * Default: 5.0s (very generous - normal async work should complete in <1 frame).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Safety", meta = (ClampMin = "0.5"))
    float HandleTimeoutSeconds = 5.0f;

    /**
     * Maximum number of chunks that can be in-flight simultaneously across all transformers.
     * Prevents the task graph from being flooded when many transformers fire at once.
     * New chunks queue behind in-flight ones and are dispatched as slots free up.
     * Default: 32.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Performance", meta = (ClampMin = "1"))
    int32 MaxConcurrentChunks = 32;

    /**
     * Whether to log warnings when a handle times out.
     * Disable in shipping builds if timeout warnings are expected and benign.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Debug")
    bool bWarnOnHandleTimeout = true;

    /**
     * Whether to assert (debug builds) when a mutation result contains an index
     * not present in the original snapshot.
     * Always discards invalid indices regardless of this setting.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Debug")
    bool bAssertOnInvalidMutationIndex = true;
};


// ============================================================
//  Internal Tracking Structs
// ============================================================

/** Registration entry for a transformer */
struct FISMTransformerEntry
{
    IISMBatchTransformer* Transformer = nullptr;
    FName                 Name;
    int32                 Priority = 0;
    bool                  bRegistered = false;
};

/**
 * Tracks a single in-flight chunk: the open handle and the future result.
 * The scheduler checks these each tick and applies completed results.
 */
struct FISMInFlightChunk
{
    FName                                TransformerName;
    TWeakObjectPtr<UISMRuntimeComponent> TargetComponent;  // identity for matching
    FIntVector                           CellCoordinates;  // identity for matching
    double                               IssuedTimeSeconds = 0.0;
    bool                                 bReleased = false;
    bool                                 bAbandoned = false;
};

/**
 * Tracks all in-flight chunks for a single transformer request cycle.
 * When all chunks are resolved, OnRequestComplete() is called.
 */
struct FISMTransformerRequestCycle
{
    FName  TransformerName;
    int32  TotalChunks = 0;
    int32  ResolvedChunks = 0;
    bool   bComplete = false;
};


// ============================================================
//  Scheduler
// ============================================================

/**
 * Manages the async batch mutation pipeline for all registered ISM batch transformers.
 *
 * Owned and ticked by UISMRuntimeSubsystem. Not intended to be created directly.
 *
 * Each tick:
 *   1. Poll registered transformers for dirty state (IsDirty())
 *   2. For dirty transformers, call BuildRequest() on the game thread
 *   3. Walk the spatial cells of each target component, emit FISMBatchSnapshot chunks
 *   4. Dispatch ProcessChunk() calls to the task graph (one task per chunk)
 *   5. Each tick, drain completed chunks (handles that have been Released/Abandoned)
 *   6. Apply validated mutation results to ISMRuntimeComponents on the game thread
 *   7. When all chunks for a transformer cycle are resolved, call OnRequestComplete()
 *   8. Enforce handle timeouts - force-abandon stale open handles with a warning
 *
 * Threading:
 *   All public methods are game-thread only unless explicitly noted.
 *   ProcessChunk() runs on the task graph - no scheduler state is touched from there.
 *   Handle::Release() and Handle::Abandon() are thread-safe (they post results back
 *   via a lock-free queue that the scheduler drains on the game thread each tick).
 */
UCLASS()
class ISMRUNTIMECORE_API UISMBatchScheduler : public UObject
{
    GENERATED_BODY()

public:

    // ===== Initialization =====

    /** Called by UISMRuntimeSubsystem during Initialize(). */
    void Initialize(UISMRuntimeSubsystem* InOwningSubsystem);

    /** Called by UISMRuntimeSubsystem during Deinitialize(). Abandons all open handles. */
    void Deinitialize();

    /** Configuration. Set before registering transformers for best results. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch")
    FISMBatchSchedulerSettings Settings;


    // ===== Transformer Registration =====

    /**
     * Register a transformer with the scheduler.
     * The transformer must remain valid for the lifetime of its registration.
     * Safe to call at any time including during BeginPlay of the owning module.
     *
     * @param Transformer   - The transformer to register (must not be nullptr)
     * @return true if registration succeeded, false if a transformer with the same name
     *         is already registered.
     */
    bool RegisterTransformer(IISMBatchTransformer* Transformer);

    /**
     * Unregister a transformer.
     * Any in-flight chunks for this transformer are abandoned gracefully.
     * Safe to call from EndPlay.
     *
     * @param TransformerName - The name returned by the transformer's GetTransformerName()
     */
    void UnregisterTransformer(FName TransformerName);

    /**
     * Unregister all transformers. Called during Deinitialize().
     */
    void UnregisterAllTransformers();

    /** Check if a transformer with the given name is registered. */
    bool IsTransformerRegistered(FName TransformerName) const;

    /** Get number of currently registered transformers. */
    int32 GetRegisteredTransformerCount() const { return RegisteredTransformers.Num(); }

	bool HasPendingWork() const { return GetRegisteredTransformerCount() > 0; }
    


    // ===== Tick =====

    /**
     * Drive the scheduler pipeline for one frame.
     * Called by UISMRuntimeSubsystem::Tick().
     * Game thread only.
     */
    void Tick(float DeltaTime);


    // ===== Statistics / Debug =====

    /** Number of chunks currently in flight (handles open, waiting for transformer). */
    int32 GetInFlightChunkCount() const;

    /** Number of pending results waiting to be applied this tick. */
    int32 GetPendingResultCount() const;

    /**
     * Get the name of every transformer that currently has open handles.
     * Useful for diagnosing stalls.
     */
    TArray<FName> GetTransformersWithOpenHandles() const;


private:

    // ===== Handle Lifecycle (called by FISMMutationHandle internals) =====
    // These are called from any thread - implementations must be thread-safe.

    friend struct FISMMutationHandle;

    /**
     * Called by FISMMutationHandle::Release().
     * Posts the result to a thread-safe queue for application on the game thread.
     */
    void OnHandleReleased(FISMBatchMutationResult&& Result, FIntVector CellCoords,
        TWeakObjectPtr<UISMRuntimeComponent> Component);

    /**
     * Called by FISMMutationHandle::Abandon().
     * Posts an abandon signal so the scheduler knows the chunk is done.
     */
    void OnHandleAbandoned(FIntVector CellCoords, TWeakObjectPtr<UISMRuntimeComponent> Component);


    // ===== Internal Pipeline =====

    /** Step 1-4: Poll dirty transformers, build requests, emit and dispatch chunks. */
    void DispatchDirtyTransformers();

    /**
     * For a single dirty transformer: walk its requested components and spatial bounds,
     * emit one FISMBatchSnapshot per occupied cell (respecting MaxInstancesPerChunk),
     * and dispatch each to the task graph via ProcessChunk().
     */
    void DispatchTransformer(FISMTransformerEntry& Entry);

    /**
     * Build all snapshot chunks for one component given a request.
     * Splits oversized cells into sub-chunks if needed.
     * Returns the number of chunks dispatched.
     */
    int32 DispatchComponentChunks(
        IISMBatchTransformer* Transformer,
        UISMRuntimeComponent* Component,
        const FISMSnapshotRequest& Request,
        FName TransformerName);

    /**
     * Build a single FISMBatchSnapshot from a component, cell, and field mask.
     * Called on the game thread before the async task is launched.
     */
    FISMBatchSnapshot BuildSnapshot(
        UISMRuntimeComponent* Component,
        FIntVector CellCoords,
        const TArray<int32>& InstanceIndices,
        EISMSnapshotField ReadMask) const;

    /** Step 5-7: Drain the released-handle queue, apply results, fire completion callbacks. */
    void DrainAndApplyResults();

    /**
     * Apply a single validated mutation result to its target component.
     * Game thread only.
     * Returns true if the result was applied, false if it was discarded (stale token etc).
     */
    bool ApplyMutationResult(const FISMBatchMutationResult& Result);

    /** Step 8: Walk open handles and force-abandon any that have exceeded the timeout. */
    void EnforceHandleTimeouts(double CurrentTime);

    /**
     * Mark a transformer's current request cycle as having one more resolved chunk.
     * When all chunks are resolved, calls OnRequestComplete() and clears the cycle entry.
     */
    void NotifyChunkResolved(FName TransformerName, bool bWasAbandoned);

    /** Create and register a new in-flight chunk entry. Returns a weak ref for tracking. */
    FISMInFlightChunk& TrackNewChunk(FName TransformerName,
        TWeakObjectPtr<UISMRuntimeComponent> Component,
        FIntVector CellCoords,
        double IssuedTime);


    // ===== State =====

    /** Owning subsystem reference */
    TWeakObjectPtr<UISMRuntimeSubsystem> OwningSubsystem;

    /** Registered transformers, sorted by priority (high to low). */
    TArray<FISMTransformerEntry> RegisteredTransformers;

    /** Currently in-flight chunks awaiting transformer results. */
    TArray<FISMInFlightChunk> InFlightChunks;

    /** Active request cycles (one per transformer with chunks in flight). */
    TArray<FISMTransformerRequestCycle> ActiveCycles;

    /**
     * Thread-safe queue of released results from FISMMutationHandle::Release().
     * Drained on the game thread each tick.
     */
    TQueue<FISMBatchMutationResult, EQueueMode::Mpsc> ReleasedResultQueue;

    /**
     * Thread-safe queue of abandoned chunk identifiers from FISMMutationHandle::Abandon().
     * Drained on the game thread each tick.
     */
    struct FAbandonedChunkKey { FIntVector Cell; TWeakObjectPtr<UISMRuntimeComponent> Component; };
    TQueue<FAbandonedChunkKey, EQueueMode::Mpsc> AbandonedChunkQueue;

    /** Whether the scheduler has been initialized. */
    bool bInitialized = false;
};