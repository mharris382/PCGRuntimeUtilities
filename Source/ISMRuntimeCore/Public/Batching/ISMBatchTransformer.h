#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISMBatchTypes.h"
#include "ISMBatchTransformer.generated.h"

// Forward declarations
class UISMBatchScheduler;


// ============================================================
//  Mutation Handle
// ============================================================

/**
 * A lease on a set of instance indices within a specific snapshot chunk.
 * Held by the transformer while async compute is in flight.
 *
 * While a handle is open:
 *   - The scheduler will not dispatch a second mutation request for the same
 *     (component, cell) pair to any other transformer with conflicting write masks.
 *   - The generation token from the snapshot is tracked - if the component is
 *     structurally modified before the handle is released, the result is flagged stale.
 *
 * V1 contract: explicit release only. The transformer MUST call Release() when done,
 * either with a result or empty-handed (abandon). Handles that exceed the scheduler's
 * timeout are force-released as "no change" and a warning is logged.
 *
 * Usage:
 *   // In ProcessChunk() - transformer receives handle, stores it, releases when future resolves
 *   Handle.Release(MyMutationResult);   // apply result
 *   Handle.Abandon();                   // no change, release lease
 */

struct ISMRUNTIMECORE_API FISMMutationHandle
{
    
    FISMMutationHandle() = default;

    // Non-copyable - a handle represents a unique lease
    FISMMutationHandle(const FISMMutationHandle&) = delete;
    FISMMutationHandle& operator=(const FISMMutationHandle&) = delete;

    // Movable
    FISMMutationHandle(FISMMutationHandle&& Other) noexcept;
    FISMMutationHandle& operator=(FISMMutationHandle&& Other) noexcept;

    ~FISMMutationHandle();

    /**
     * Submit the mutation result and release this lease.
     * The scheduler will apply the result on the next game thread tick
     * after validating the generation token.
     * Calling Release() on an already-released handle is a no-op (logs warning in debug).
     */
    void Release(FISMBatchMutationResult&& Result);

    /**
     * Abandon this lease without submitting any changes.
     * The chunk is left unchanged and the lease is freed.
     * Calling Abandon() on an already-released handle is a no-op (logs warning in debug).
     */
    void Abandon();

    /** Whether this handle is still open (not yet released or abandoned). */
    bool IsOpen() const { return bIsOpen && Scheduler.IsValid(); }

    /** The cell and component this handle covers. For transformer bookkeeping. */
    FIntVector GetCellCoordinates() const { return CellCoordinates; }
    TWeakObjectPtr<UISMRuntimeComponent> GetTargetComponent() const { return TargetComponent; }

    /** Generation token from the snapshot. Transformers can read this for their own staleness checks. */
    uint32 GetGenerationToken() const { return GenerationToken; }

    /** World time at which this handle was issued. Used by the scheduler for timeout enforcement. */
    double GetIssuedTime() const { return IssuedTimeSeconds; }

private:
    // Only the scheduler creates valid handles
    friend class UISMBatchScheduler;

    /** Internal constructor used by the scheduler */
    FISMMutationHandle(
        TWeakObjectPtr<UISMBatchScheduler> InScheduler,
        TWeakObjectPtr<UISMRuntimeComponent> InComponent,
        FIntVector InCellCoords,
        uint32 InGenerationToken,
        double InIssuedTime);

    TWeakObjectPtr<UISMBatchScheduler>    Scheduler;
    TWeakObjectPtr<UISMRuntimeComponent>  TargetComponent;
    FIntVector                            CellCoordinates = FIntVector::ZeroValue;
    uint32                                GenerationToken = 0;
    double                                IssuedTimeSeconds = 0.0;
    bool                                  bIsOpen = false;
};


// ============================================================
//  Transformer Interface
// ============================================================

UINTERFACE(MinimalAPI)
class UISMBatchTransformer : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for systems that want to perform async batched read/write operations
 * on ISMRuntimeComponent instance data.
 *
 * Implementations register with UISMBatchScheduler. Each tick the scheduler checks
 * registered transformers for pending work via IsDirty(), then drives the snapshot
 * and dispatch pipeline automatically.
 *
 * Two canonical implementations:
 *
 *   ISMRuntimeAnimation:
 *     - Sets dirty every tick (time-driven, continuous)
 *     - BuildRequest() returns spatial bounds around camera or all instances
 *     - ProcessChunk() evaluates animation curves against snapshot transforms
 *
 *   ISMRuntimePCGInterop:
 *     - Sets dirty flag in the PCG graph completion callback (event-driven, burst)
 *     - BuildRequest() returns the bounds that the PCG graph operated on
 *     - ProcessChunk() maps PCG output attributes back to instance mutations
 *
 * Threading contract:
 *   - BuildRequest()  : called on game thread
 *   - ProcessChunk()  : called on any thread (task graph) - no UObject access allowed
 *   - OnRequestComplete() : called on game thread after all chunks are applied
 *
 * V1 constraints:
 *   - Transformers cannot add or remove instances (structural changes forbidden)
 *   - Returned mutations must reference indices present in the original chunk
 *   - Handles must be explicitly released via Release() or Abandon()
 */
class ISMRUNTIMECORE_API IISMBatchTransformer
{
    GENERATED_BODY()

public:

    // ===== Registration Identity =====

    /**
     * Stable name for this transformer, used for logging, debugging, and collision detection.
     * Must be unique among transformers registered with the same scheduler.
     * Example: "ISMRuntimeAnimation.WindSway", "ISMRuntimePCGInterop.FoliageDensity"
     */
    virtual FName GetTransformerName() const = 0;

    /**
     * Priority used when two transformers have conflicting write masks on the same cell.
     * Higher value = higher priority (runs first, other transformer queues behind it).
     * Default 0. Most transformers should leave this at default.
     */
    virtual int32 GetPriority() const { return 0; }


    // ===== Dirty / Work-Ready Signal =====

    /**
     * Returns true if this transformer has work ready to process this tick.
     * Called on the game thread each scheduler tick.
     *
     * Continuous transformers (animation): return true every tick.
     * Event-driven transformers (PCG): return true only after receiving an external signal,
     *   then return false until the next signal arrives.
     *
     * The scheduler only calls BuildRequest() if this returns true,
     * so cheap dirty checks here keep idle transformers effectively free.
     */
    virtual bool IsDirty() const = 0;

    /**
     * Called by the scheduler after it has finished dispatching all chunks for this cycle.
     * Transformers should clear their dirty flag here (not in BuildRequest).
     * Called on the game thread.
     */
    virtual void ClearDirty() = 0;


    // ===== Request Building =====

    /**
     * Build the snapshot request for this processing cycle.
     * Called on the game thread only when IsDirty() returns true.
     *
     * The transformer declares:
     *   - Which components it wants to read/write
     *   - What spatial bounds to cover
     *   - Which fields it needs (read mask) and will write (write mask)
     *
     * The scheduler uses this to generate one FISMBatchSnapshot per occupied spatial cell
     * within the declared bounds, then calls ProcessChunk() for each.
     */
    virtual FISMSnapshotRequest BuildRequest() = 0;


    // ===== Chunk Processing =====

    /**
     * Process a single chunk (one spatial cell of one component).
     * Called on a background thread - NO UObject access is permitted here.
     * The snapshot is passed by value; the transformer owns this copy.
     *
     * The transformer MUST either:
     *   a) Call Handle.Release(result) with a FISMBatchMutationResult, or
     *   b) Call Handle.Abandon() to indicate no changes for this chunk.
     *
     * Failure to call either will result in the handle timing out, a warning being logged,
     * and the chunk being treated as abandoned by the scheduler.
     *
     * @param Chunk     - Read-only snapshot of one spatial cell's instances
     * @param Handle    - Lease object that must be released when processing is complete
     */
    virtual void ProcessChunk(FISMBatchSnapshot Chunk, FISMMutationHandle Handle) = 0;


    // ===== Completion Notification =====

    /**
     * Called on the game thread after all chunks from the current request cycle
     * have been processed and applied (or discarded as stale/abandoned).
     *
     * Use this to:
     *   - Trigger follow-up logic that depends on mutations being visible
     *   - Update internal state that requires knowing the cycle is complete
     *   - Kick off the next PCG graph execution if chaining graphs
     *
     * Not called if BuildRequest() produced zero chunks (nothing to process).
     */
    virtual void OnRequestComplete() {}

    virtual void OnHandleIssued(const TArray<FISMBatchSnapshot>& Snapshots) {}

    virtual void OnHandleChunksChanged(const TArray<FISMBatchSnapshot>& Snapshots) {}

    /** Called when the handle is released OR abandoned.
     *  Override to reset per-cycle state. Default is no-op. */
    virtual void OnHandleReleased() {}
};






