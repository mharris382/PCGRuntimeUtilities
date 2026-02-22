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
//  Internal Tracking Structs
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
//  Scheduler
// ============================================================

UCLASS()
class ISMRUNTIMECORE_API UISMBatchScheduler : public UObject
{
    GENERATED_BODY()

public:

    void Initialize(UISMRuntimeSubsystem* InOwningSubsystem);
    void Deinitialize();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Batch")
    FISMBatchSchedulerSettings Settings;

    bool RegisterTransformer(IISMBatchTransformer* Transformer);
    void UnregisterTransformer(FName TransformerName);
    void UnregisterAllTransformers();
    bool IsTransformerRegistered(FName TransformerName) const;
    int32 GetRegisteredTransformerCount() const { return RegisteredTransformers.Num(); }
    bool HasPendingWork() const { return GetRegisteredTransformerCount() > 0; }

    void Tick(float DeltaTime);

    int32 GetInFlightChunkCount() const;
    int32 GetPendingResultCount() const;
    TArray<FName> GetTransformersWithOpenHandles() const;


private:

    // Key for abandoned chunk identification
    struct FAbandonedChunkKey
    {
        FIntVector Cell;
        TWeakObjectPtr<UISMRuntimeComponent> Component;
    };

    friend struct FISMMutationHandle;

    void OnHandleReleased(FISMBatchMutationResult&& Result, FIntVector CellCoords,
        TWeakObjectPtr<UISMRuntimeComponent> Component);
    void OnHandleAbandoned(FIntVector CellCoords, TWeakObjectPtr<UISMRuntimeComponent> Component);

    void DispatchDirtyTransformers();
    void DispatchTransformer(FISMTransformerEntry& Entry);
    int32 DispatchComponentChunks(
        IISMBatchTransformer* Transformer,
        UISMRuntimeComponent* Component,
        const FISMSnapshotRequest& Request,
        FName TransformerName);
    FISMBatchSnapshot BuildSnapshot(
        UISMRuntimeComponent* Component,
        FIntVector CellCoords,
        const TArray<int32>& InstanceIndices,
        EISMSnapshotField ReadMask) const;
    void DrainAndApplyResults();
    bool ApplyMutationResult(const FISMBatchMutationResult& Result);
    void EnforceHandleTimeouts(double CurrentTime);
    void NotifyChunkResolved(FName TransformerName, bool bWasAbandoned);
    FISMInFlightChunk& TrackNewChunk(FName TransformerName,
        TWeakObjectPtr<UISMRuntimeComponent> Component,
        FIntVector CellCoords,
        double IssuedTime);

    // ===== State =====

    TWeakObjectPtr<UISMRuntimeSubsystem> OwningSubsystem;
    TArray<FISMTransformerEntry> RegisteredTransformers;
    TArray<FISMInFlightChunk> InFlightChunks;
    TArray<FISMTransformerRequestCycle> ActiveCycles;

    /**
     * Released results - replaced TQueue to avoid heap-allocated linked list nodes
     * which were becoming corrupted under heavy physics load.
     * Swap-and-process pattern: lock briefly to swap, process without holding lock.
     */
    FCriticalSection ReleasedResultsLock;
    TArray<FISMBatchMutationResult> ReleasedResults;

    FCriticalSection AbandonedChunksLock;
    TArray<FAbandonedChunkKey> AbandonedChunks;

    bool bInitialized = false;
};