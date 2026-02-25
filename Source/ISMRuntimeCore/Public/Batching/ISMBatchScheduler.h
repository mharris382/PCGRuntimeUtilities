#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Batching/ISMBatchTypes.h"
#include "Logging/LogMacros.h"
#include "Batching/ISMBatchTransformer.h"
#include "ISMBatchScheduler.generated.h"

// Forward declarations
class UISMRuntimeComponent;
class UISMRuntimeSubsystem;

DECLARE_LOG_CATEGORY_EXTERN(LogISMBatching, Log, All);


// ============================================================
//  Scheduler Settings
// ============================================================

USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMBatchSchedulerSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Performance", meta = (ClampMin = "64"))
    int32 MaxInstancesPerChunk = 2048;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Safety", meta = (ClampMin = "0.5"))
    float HandleTimeoutSeconds = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Performance", meta = (ClampMin = "1"))
    int32 MaxConcurrentChunks = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Debug")
    bool bWarnOnHandleTimeout = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch|Debug")
    bool bAssertOnInvalidMutationIndex = true;
};


// ============================================================
//  Internal Tracking Structs  (shared by both implementations)
// ============================================================

struct FISMTransformerEntry
{
    IISMBatchTransformer* Transformer = nullptr;
    FName                 Name;
    int32                 Priority = 0;
    bool                  bRegistered = false;
};

struct FISMInFlightChunk
{
    FName                                TransformerName;
    TWeakObjectPtr<UISMRuntimeComponent> TargetComponent;
    FIntVector                           CellCoordinates;
    double                               IssuedTimeSeconds = 0.0;
    bool                                 bReleased = false;
    bool                                 bAbandoned = false;
};

struct FISMTransformerRequestCycle
{
    FName  TransformerName;
    int32  TotalChunks = 0;
    int32  ResolvedChunks = 0;
    bool   bComplete = false;
};


// ============================================================
//  Base Scheduler
//  Owns: transformer registry, dispatch, snapshot, cycle tracking,
//        result application, handle construction.
//  Does NOT own: result staging, tick drain strategy.
// ============================================================

UCLASS(Abstract)
class ISMRUNTIMECORE_API UISMBatchSchedulerBase : public UObject
{
    GENERATED_BODY()

public:

    virtual void Initialize(UISMRuntimeSubsystem* InOwningSubsystem);
    virtual void Deinitialize();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch")
    FISMBatchSchedulerSettings Settings;

    // ===== Transformer Registry =====

    bool  RegisterTransformer(IISMBatchTransformer* Transformer);
    void  UnregisterTransformer(FName TransformerName);
    void  UnregisterAllTransformers();
    bool  IsTransformerRegistered(FName TransformerName) const;
    int32 GetRegisteredTransformerCount() const { return RegisteredTransformers.Num(); }
    bool  HasPendingWork() const { return GetRegisteredTransformerCount() > 0; }

    // ===== Tick =====

    virtual void Tick(float DeltaTime) PURE_VIRTUAL(UISMBatchSchedulerBase::Tick, );

    // ===== Stats =====

    virtual int32 GetInFlightChunkCount() const { return InFlightChunks.Num(); }
    virtual int32 GetPendingResultCount()  const { return InFlightChunks.Num(); }
    TArray<FName> GetTransformersWithOpenHandles() const;

protected:

    // ===== Handle Callbacks =====
    //
    // Called by FISMMutationHandle::Release() and ::Abandon().
    // Subclasses decide what "receiving a result" means:
    //   Sync:  apply immediately on this thread
    //   Async: post to thread-safe staging area, apply on next tick drain

    friend struct FISMMutationHandle;

    virtual void OnHandleReleased(FISMBatchMutationResult&& Result, FIntVector CellCoords,
        TWeakObjectPtr<UISMRuntimeComponent> Component) PURE_VIRTUAL(UISMBatchSchedulerBase::OnHandleReleased, );

    virtual void OnHandleAbandoned(FIntVector CellCoords,
        TWeakObjectPtr<UISMRuntimeComponent> Component) PURE_VIRTUAL(UISMBatchSchedulerBase::OnHandleAbandoned, );

    // ===== Dispatch (shared) =====

    void DispatchDirtyTransformers();
    void DispatchTransformer(FISMTransformerEntry& Entry);

    /**
     * Builds the snapshot for one component and calls ProcessChunk on the transformer.
     * Subclasses provide the correct handle type via the friend relationship.
     * Sync:  no batch lock, results applied before function returns.
     * Async: batch locks the component, results staged for next tick drain.
     */
    virtual int32 DispatchComponentChunks(
        IISMBatchTransformer* Transformer,
        UISMRuntimeComponent* Component,
        const FISMSnapshotRequest& Request,
        FName TransformerName) PURE_VIRTUAL(UISMBatchSchedulerBase::DispatchComponentChunks, return 0;);

    FISMBatchSnapshot BuildSnapshot(
        UISMRuntimeComponent* Component,
        FIntVector CellCoords,
        const TArray<int32>& InstanceIndices,
        EISMSnapshotField ReadMask) const;

    // ===== Result Application (shared) =====

    bool ApplyMutationResult(const FISMBatchMutationResult& Result);

    // ===== Cycle Tracking (shared) =====

    void NotifyChunkResolved(FName TransformerName, bool bWasAbandoned);

    FISMInFlightChunk& TrackNewChunk(
        FName TransformerName,
        TWeakObjectPtr<UISMRuntimeComponent> Component,
        FIntVector CellCoords,
        double IssuedTime);

    // ===== Handle Construction (shared) =====
    //
    // Both subclasses construct handles through this helper so the handle
    // always stores a TWeakObjectPtr<UISMBatchSchedulerBase> and calls
    // back through the virtual OnHandleReleased/Abandoned.

    FISMMutationHandle MakeHandle(
        TWeakObjectPtr<UISMRuntimeComponent> Component,
        FIntVector CellCoords,
        uint32 GenerationToken,
        double IssuedTime) const;

    // ===== State =====

    TWeakObjectPtr<UISMRuntimeSubsystem> OwningSubsystem;
    TArray<FISMTransformerEntry>         RegisteredTransformers;
    TArray<FISMInFlightChunk>            InFlightChunks;
    TArray<FISMTransformerRequestCycle>  ActiveCycles;
    bool                                 bInitialized = false;
};


// ============================================================
//  Synchronous Scheduler
//
//  ProcessChunk completes before DispatchComponentChunks returns.
//  OnHandleReleased applies mutations immediately on the game thread.
//  No locking, no staging, no drain loop.
//  Safe default for all current use cases.
// ============================================================

UCLASS()
class ISMRUNTIMECORE_API UISMBatchSchedulerSync : public UISMBatchSchedulerBase
{
    GENERATED_BODY()

public:

    virtual void Tick(float DeltaTime) override;

protected:

    virtual void OnHandleReleased(FISMBatchMutationResult&& Result, FIntVector CellCoords,
        TWeakObjectPtr<UISMRuntimeComponent> Component) override;

    virtual void OnHandleAbandoned(FIntVector CellCoords,
        TWeakObjectPtr<UISMRuntimeComponent> Component) override;

    virtual int32 DispatchComponentChunks(
        IISMBatchTransformer* Transformer,
        UISMRuntimeComponent* Component,
        const FISMSnapshotRequest& Request,
        FName TransformerName) override;
};


// ============================================================
//  Async Scheduler
//
//  ProcessChunk may hand work to a background thread.
//  OnHandleReleased posts results to a thread-safe staging area.
//  Results are applied on the game thread during the next Tick drain.
//
//  FCriticalSection lives in FThreadedState (heap-allocated) to avoid
//  UObject CDO construction corrupting the mutex handle and poisoning
//  every TArray stored after it in the object layout.
// ============================================================

UCLASS()
class ISMRUNTIMECORE_API UISMBatchScheduler : public UISMBatchSchedulerBase
{
    GENERATED_BODY()

public:

    virtual void Initialize(UISMRuntimeSubsystem* InOwningSubsystem) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;

protected:

    virtual void OnHandleReleased(FISMBatchMutationResult&& Result, FIntVector CellCoords,
        TWeakObjectPtr<UISMRuntimeComponent> Component) override;

    virtual void OnHandleAbandoned(FIntVector CellCoords,
        TWeakObjectPtr<UISMRuntimeComponent> Component) override;

    virtual int32 DispatchComponentChunks(
        IISMBatchTransformer* Transformer,
        UISMRuntimeComponent* Component,
        const FISMSnapshotRequest& Request,
        FName TransformerName) override;

private:

    struct FAbandonedChunkKey
    {
        FIntVector                           Cell;
        TWeakObjectPtr<UISMRuntimeComponent> Component;
    };

    void DrainAndApplyResults();
    void EnforceHandleTimeouts(double CurrentTime); // TODO Phase 2

    // See class comment above for why these live in a TUniquePtr
    struct FThreadedState
    {
        FCriticalSection                ReleasedResultsLock;
        TArray<FISMBatchMutationResult> ReleasedResults;

        FCriticalSection                AbandonedChunksLock;
        TArray<FAbandonedChunkKey>      AbandonedChunks;
    };

    TUniquePtr<FThreadedState> ThreadedState;
};