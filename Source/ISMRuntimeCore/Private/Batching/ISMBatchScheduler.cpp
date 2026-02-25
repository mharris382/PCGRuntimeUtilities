#include "Batching/ISMBatchScheduler.h"
#include "Batching/ISMBatchTransformer.h"
#include "Batching/ISMBatchTypes.h"
#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogISMBatching);


// ============================================================
//  UISMBatchSchedulerBase
// ============================================================

#pragma region BASE

void UISMBatchSchedulerBase::Initialize(UISMRuntimeSubsystem* InOwningSubsystem)
{
    OwningSubsystem = InOwningSubsystem;
    bInitialized = true;
}

void UISMBatchSchedulerBase::Deinitialize()
{
    if (!bInitialized) return;
    bInitialized = false;
    UnregisterAllTransformers();
}

// ===== Transformer Registry =====

bool UISMBatchSchedulerBase::RegisterTransformer(IISMBatchTransformer* Transformer)
{
    if (!Transformer)
    {
        UE_LOG(LogISMBatching, Warning, TEXT("RegisterTransformer: null transformer passed."));
        return false;
    }

    const FName Name = Transformer->GetTransformerName();
    if (IsTransformerRegistered(Name))
    {
        UE_LOG(LogISMBatching, Warning, TEXT("RegisterTransformer: %s is already registered."), *Name.ToString());
        return false;
    }

    UE_LOG(LogISMBatching, Log, TEXT("RegisterTransformer: %s"), *Name.ToString());

    FISMTransformerEntry& Entry = RegisteredTransformers.AddDefaulted_GetRef();
    Entry.Transformer = Transformer;
    Entry.Name = Name;
    Entry.Priority = Transformer->GetPriority();
    Entry.bRegistered = true;

    RegisteredTransformers.StableSort([](const FISMTransformerEntry& A, const FISMTransformerEntry& B)
        {
            return A.Priority > B.Priority;
        });

    return true;
}

void UISMBatchSchedulerBase::UnregisterTransformer(FName TransformerName)
{
    if (RegisteredTransformers.Num() == 0) return;

    UE_LOG(LogISMBatching, Log, TEXT("UnregisterTransformer: %s"), *TransformerName.ToString());
    RegisteredTransformers.RemoveAll([&TransformerName](const FISMTransformerEntry& Entry)
        {
            return Entry.Name == TransformerName;
        });
}

void UISMBatchSchedulerBase::UnregisterAllTransformers()
{
    if (InFlightChunks.Num() > 0)
    {
        UE_LOG(LogISMBatching, Warning,
            TEXT("UnregisterAllTransformers: %d chunks still in-flight - clearing."), InFlightChunks.Num());
        InFlightChunks.Empty();
    }
    if (ActiveCycles.Num() > 0)
    {
        UE_LOG(LogISMBatching, Warning,
            TEXT("UnregisterAllTransformers: %d cycles still active - clearing."), ActiveCycles.Num());
        ActiveCycles.Empty();
    }

    if (RegisteredTransformers.Num() == 0) return;

    UE_LOG(LogISMBatching, Log, TEXT("UnregisterAllTransformers"));
    RegisteredTransformers.Empty();
}

bool UISMBatchSchedulerBase::IsTransformerRegistered(FName TransformerName) const
{
    return RegisteredTransformers.ContainsByPredicate([&TransformerName](const FISMTransformerEntry& Entry)
        {
            return Entry.Name == TransformerName;
        });
}

TArray<FName> UISMBatchSchedulerBase::GetTransformersWithOpenHandles() const
{
    TArray<FName> Result;
    for (const FISMInFlightChunk& Chunk : InFlightChunks)
    {
        if (!Chunk.bReleased)
            Result.AddUnique(Chunk.TransformerName);
    }
    return Result;
}

// ===== Dispatch =====

void UISMBatchSchedulerBase::DispatchDirtyTransformers()
{
    for (FISMTransformerEntry& Entry : RegisteredTransformers)
    {
        if (!Entry.Transformer || !Entry.bRegistered) continue;
        if (!Entry.Transformer->IsDirty()) continue;
        DispatchTransformer(Entry);
        Entry.Transformer->ClearDirty();
    }
}

void UISMBatchSchedulerBase::DispatchTransformer(FISMTransformerEntry& Entry)
{
    IISMBatchTransformer* Transformer = Entry.Transformer;
    FISMSnapshotRequest Request = Transformer->BuildRequest();

    TArray<TWeakObjectPtr<UISMRuntimeComponent>> Targets = Request.TargetComponents;
    if (Targets.IsEmpty()) return;

    int32 TotalChunks = 0;
    for (TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : Targets)
    {
        UISMRuntimeComponent* Comp = CompPtr.Get();
        if (!Comp) continue;
        TotalChunks += DispatchComponentChunks(Transformer, Comp, Request, Entry.Name);
    }

    if (TotalChunks > 0)
    {
        FISMTransformerRequestCycle& Cycle = ActiveCycles.AddDefaulted_GetRef();
        Cycle.TransformerName = Entry.Name;
        Cycle.TotalChunks = TotalChunks;
        Cycle.ResolvedChunks = 0;
        Cycle.bComplete = false;
    }
}

// ===== Snapshot =====

FISMBatchSnapshot UISMBatchSchedulerBase::BuildSnapshot(
    UISMRuntimeComponent* Component,
    FIntVector CellCoords,
    const TArray<int32>& InstanceIndices,
    EISMSnapshotField ReadMask) const
{
    FISMBatchSnapshot Snapshot;
    Snapshot.SourceComponent = Component;
    Snapshot.CellCoordinates = CellCoords;
    Snapshot.PopulatedFields = ReadMask;
    Snapshot.ComponentGenerationToken = 0;
    Snapshot.Instances.Reserve(InstanceIndices.Num());

    for (int32 Idx : InstanceIndices)
    {
        FISMInstanceSnapshot& InstSnap = Snapshot.Instances.AddDefaulted_GetRef();
        InstSnap.InstanceIndex = Idx;

        if (EnumHasAnyFlags(ReadMask, EISMSnapshotField::Transform))
            InstSnap.Transform = Component->GetInstanceTransform(Idx);

        if (EnumHasAnyFlags(ReadMask, EISMSnapshotField::CustomData))
            InstSnap.CustomData = Component->GetInstanceCustomData(Idx);

        if (EnumHasAnyFlags(ReadMask, EISMSnapshotField::StateFlags))
            InstSnap.StateFlags = Component->GetInstanceStateFlags(Idx);
    }

    return Snapshot;
}

// ===== Handle Construction =====

FISMMutationHandle UISMBatchSchedulerBase::MakeHandle(
    TWeakObjectPtr<UISMRuntimeComponent> Component,
    FIntVector CellCoords,
    uint32 GenerationToken,
    double IssuedTime) const
{
    // const_cast is safe here - the handle stores a weak pointer back to this
    // scheduler and calls virtual methods on it. MakeHandle is logically const
    // (it doesn't modify scheduler state), but the handle needs a non-const weak ptr.
    return FISMMutationHandle(
        TWeakObjectPtr<UISMBatchSchedulerBase>(const_cast<UISMBatchSchedulerBase*>(this)),
        Component,
        CellCoords,
        GenerationToken,
        IssuedTime);
}

// ===== Result Application =====

bool UISMBatchSchedulerBase::ApplyMutationResult(const FISMBatchMutationResult& Result)
{
    UISMRuntimeComponent* Comp = Result.TargetComponent.Get();
    if (!Comp) return false;

    for (const FISMInstanceMutation& Mutation : Result.Mutations)
    {
        const int32 Idx = Mutation.InstanceIndex;
        if (Comp->IsInstanceDestroyed(Idx)) continue;

        if (EnumHasAnyFlags(Result.WrittenFields, EISMSnapshotField::Transform))
        {
            if (Mutation.NewTransform.IsSet())
                Comp->UpdateInstanceTransform(Idx, Mutation.NewTransform.GetValue(), true, false);
        }

        if (EnumHasAnyFlags(Result.WrittenFields, EISMSnapshotField::CustomData))
        {
            if (Mutation.NewCustomData.IsSet())
                Comp->SetInstanceCustomData(Idx, Mutation.NewCustomData.GetValue());

            for (const TTuple<int32, float>& SlotOverride : Mutation.CustomDataSlotOverrides)
                Comp->SetInstanceCustomDataValue(Idx, SlotOverride.Key, SlotOverride.Value);
        }

        if (EnumHasAnyFlags(Result.WrittenFields, EISMSnapshotField::StateFlags))
        {
            if (Mutation.NewStateFlags.IsSet())
            {
                const uint8 NewFlags = Mutation.NewStateFlags.GetValue();
                for (uint8 BitIdx = 0; BitIdx < 8; ++BitIdx)
                {
                    const EISMInstanceState Flag = static_cast<EISMInstanceState>(1 << BitIdx);
                    Comp->SetInstanceState(Idx, Flag, (NewFlags & (1 << BitIdx)) != 0);
                }
            }
        }
    }

    Comp->SetBatchLocked(false);
    return true;
}

// ===== Cycle Tracking =====

void UISMBatchSchedulerBase::NotifyChunkResolved(FName TransformerName, bool bWasAbandoned)
{
    for (FISMTransformerRequestCycle& Cycle : ActiveCycles)
    {
        if (Cycle.TransformerName == TransformerName && !Cycle.bComplete)
        {
            Cycle.ResolvedChunks++;
            if (Cycle.ResolvedChunks >= Cycle.TotalChunks)
            {
                Cycle.bComplete = true;
                for (FISMTransformerEntry& Entry : RegisteredTransformers)
                {
                    if (Entry.Name == TransformerName && Entry.Transformer)
                    {
                        Entry.Transformer->OnRequestComplete();
                        break;
                    }
                }
            }
            break;
        }
    }
    ActiveCycles.RemoveAll([](const FISMTransformerRequestCycle& C) { return C.bComplete; });
}

FISMInFlightChunk& UISMBatchSchedulerBase::TrackNewChunk(
    FName TransformerName,
    TWeakObjectPtr<UISMRuntimeComponent> Component,
    FIntVector CellCoords,
    double IssuedTime)
{
    FISMInFlightChunk& Chunk = InFlightChunks.AddDefaulted_GetRef();
    Chunk.TransformerName = TransformerName;
    Chunk.TargetComponent = Component;
    Chunk.CellCoordinates = CellCoords;
    Chunk.IssuedTimeSeconds = IssuedTime;
    Chunk.bReleased = false;
    Chunk.bAbandoned = false;
    return Chunk;
}

#pragma endregion


// ============================================================
//  UISMBatchSchedulerSync
// ============================================================

#pragma region SYNC

void UISMBatchSchedulerSync::Tick(float DeltaTime)
{
    if (!bInitialized) return;
    if (RegisteredTransformers.Num() == 0) return;

    // No drain needed - results are applied inline in OnHandleReleased
    DispatchDirtyTransformers();
}

int32 UISMBatchSchedulerSync::DispatchComponentChunks(
    IISMBatchTransformer* Transformer,
    UISMRuntimeComponent* Component,
    const FISMSnapshotRequest& Request,
    FName TransformerName)
{
    TArray<int32> AllIndices;
    const int32 TotalCount = Component->GetInstanceCount();
    AllIndices.Reserve(TotalCount);
    for (int32 i = 0; i < TotalCount; ++i)
    {
        if (!Component->IsInstanceDestroyed(i) && !Component->IsInstanceConverted(i))
            AllIndices.Add(i);
    }
    if (AllIndices.IsEmpty()) return 0;

    // No batch lock - we are on the game thread and OnHandleReleased applies
    // results before ProcessChunk returns, so nothing can interleave.

    FISMBatchSnapshot Snapshot = BuildSnapshot(Component, FIntVector::ZeroValue, AllIndices, Request.ReadMask);
    Transformer->OnHandleIssued(Snapshot);

    FISMMutationHandle Handle = MakeHandle(Component, FIntVector::ZeroValue, 0, 0.0);
    Transformer->ProcessChunk(MoveTemp(Snapshot), MoveTemp(Handle));

    // ProcessChunk has returned which means Release() was called and
    // OnHandleReleased has already applied the mutations.
    // Notify cycle tracking so OnRequestComplete fires correctly.
    NotifyChunkResolved(TransformerName, false);

    return 1;
}

void UISMBatchSchedulerSync::OnHandleReleased(
    FISMBatchMutationResult&& Result,
    FIntVector CellCoords,
    TWeakObjectPtr<UISMRuntimeComponent> Component)
{
    // Sync path: apply immediately on the game thread, no staging needed
    Result.CellCoordinates = CellCoords;
    Result.TargetComponent = Component;
    ApplyMutationResult(Result);
}

void UISMBatchSchedulerSync::OnHandleAbandoned(
    FIntVector CellCoords,
    TWeakObjectPtr<UISMRuntimeComponent> Component)
{
    // Nothing to do for sync - component was never batch locked
}

#pragma endregion


// ============================================================
//  UISMBatchScheduler  (async)
// ============================================================

#pragma region ASYNC

void UISMBatchScheduler::Initialize(UISMRuntimeSubsystem* InOwningSubsystem)
{
    ThreadedState = MakeUnique<FThreadedState>();
    Super::Initialize(InOwningSubsystem);
}

void UISMBatchScheduler::Deinitialize()
{
    // Base sets bInitialized=false and clears transformers/chunks/cycles.
    // OnHandleReleased/Abandoned guard on bInitialized so no new posts
    // can arrive after Super::Deinitialize() returns.
    Super::Deinitialize();
    ThreadedState.Reset();
}

void UISMBatchScheduler::Tick(float DeltaTime)
{
    if (!bInitialized || !ThreadedState) return;
    if (RegisteredTransformers.Num() == 0 &&
        InFlightChunks.Num() == 0 &&
        ThreadedState->ReleasedResults.Num() == 0 &&
        ThreadedState->AbandonedChunks.Num() == 0) return;

    DrainAndApplyResults();
    DispatchDirtyTransformers();
    DrainAndApplyResults();
}

int32 UISMBatchScheduler::DispatchComponentChunks(
    IISMBatchTransformer* Transformer,
    UISMRuntimeComponent* Component,
    const FISMSnapshotRequest& Request,
    FName TransformerName)
{
    TArray<int32> AllIndices;
    const int32 TotalCount = Component->GetInstanceCount();
    AllIndices.Reserve(TotalCount);
    for (int32 i = 0; i < TotalCount; ++i)
    {
        if (!Component->IsInstanceDestroyed(i) && !Component->IsInstanceConverted(i))
            AllIndices.Add(i);
    }
    if (AllIndices.IsEmpty()) return 0;

    if (!Component->SetBatchLocked(true)) return 0;

    FISMBatchSnapshot Snapshot = BuildSnapshot(Component, FIntVector::ZeroValue, AllIndices, Request.ReadMask);

    const double IssuedTime = FPlatformTime::Seconds();
    TrackNewChunk(TransformerName, Component, FIntVector::ZeroValue, IssuedTime);

    Transformer->OnHandleIssued(Snapshot);

    FISMMutationHandle Handle = MakeHandle(Component, FIntVector::ZeroValue, 0, IssuedTime);
    Transformer->ProcessChunk(MoveTemp(Snapshot), MoveTemp(Handle));

    return 1;
}

void UISMBatchScheduler::OnHandleReleased(
    FISMBatchMutationResult&& Result,
    FIntVector CellCoords,
    TWeakObjectPtr<UISMRuntimeComponent> Component)
{
    if (!bInitialized || !ThreadedState) return;

    Result.CellCoordinates = CellCoords;
    Result.TargetComponent = Component;

    FScopeLock Lock(&ThreadedState->ReleasedResultsLock);
    ThreadedState->ReleasedResults.Add(MoveTemp(Result));
}

void UISMBatchScheduler::OnHandleAbandoned(
    FIntVector CellCoords,
    TWeakObjectPtr<UISMRuntimeComponent> Component)
{
    if (!bInitialized || !ThreadedState) return;

    FAbandonedChunkKey Key;
    Key.Cell = CellCoords;
    Key.Component = Component;

    FScopeLock Lock(&ThreadedState->AbandonedChunksLock);
    ThreadedState->AbandonedChunks.Add(Key);
}

void UISMBatchScheduler::DrainAndApplyResults()
{
    if (!bInitialized || !ThreadedState) return;
    if (ThreadedState->ReleasedResults.Num() == 0 &&
        ThreadedState->AbandonedChunks.Num() == 0) return;

    // --- Drain released results ---
    // Swap under lock so we hold the lock for minimum time.
    // Process the local copy with no lock held.
    TArray<FISMBatchMutationResult> LocalResults;
    {
        FScopeLock Lock(&ThreadedState->ReleasedResultsLock);
        LocalResults = MoveTemp(ThreadedState->ReleasedResults);
        ThreadedState->ReleasedResults.Reset();
    }

    for (const FISMBatchMutationResult& Result : LocalResults)
    {
        if (!Result.TargetComponent.IsValid() || Result.TargetComponent.IsStale())
        {
            UE_LOG(LogISMBatching, Warning, TEXT("DrainAndApplyResults: Discarding result with stale component"));
            continue;
        }

        UISMRuntimeComponent* Target = Result.TargetComponent.Get();
        if (!Target || !Target->IsValidLowLevel() || !IsValid(Target))
        {
            UE_LOG(LogISMBatching, Warning, TEXT("DrainAndApplyResults: Discarding result with invalid component"));
            continue;
        }

        ApplyMutationResult(Result);

        for (FISMInFlightChunk& Chunk : InFlightChunks)
        {
            if (!Chunk.bReleased &&
                Chunk.TargetComponent == Result.TargetComponent &&
                Chunk.CellCoordinates == Result.CellCoordinates)
            {
                Chunk.bReleased = true;
                NotifyChunkResolved(Chunk.TransformerName, false);
                break;
            }
        }
    }

    // --- Drain abandoned chunks ---
    if (ThreadedState->AbandonedChunks.Num() > 0)
    {
        TArray<FAbandonedChunkKey> LocalAbandoned;
        {
            FScopeLock Lock(&ThreadedState->AbandonedChunksLock);
            LocalAbandoned = MoveTemp(ThreadedState->AbandonedChunks);
            ThreadedState->AbandonedChunks.Reset();
        }

        UE_LOG(LogISMBatching, Warning,
            TEXT("DrainAndApplyResults: Processing %d abandoned chunks"), LocalAbandoned.Num());

        for (const FAbandonedChunkKey& Key : LocalAbandoned)
        {
            if (!Key.Component.IsValid())
            {
                UE_LOG(LogISMBatching, Warning, TEXT("DrainAndApplyResults: Abandoned entry has invalid component"));
                continue;
            }

            for (FISMInFlightChunk& Chunk : InFlightChunks)
            {
                if (!Chunk.bReleased && !Chunk.bAbandoned &&
                    Chunk.TargetComponent == Key.Component &&
                    Chunk.CellCoordinates == Key.Cell)
                {
                    Chunk.bAbandoned = true;
                    Chunk.bReleased = true;

                    if (UISMRuntimeComponent* Comp = Key.Component.Get())
                        Comp->SetBatchLocked(false);

                    NotifyChunkResolved(Chunk.TransformerName, true);
                    break;
                }
            }
        }
    }

    InFlightChunks.RemoveAll([](const FISMInFlightChunk& C) { return C.bReleased; });
}

void UISMBatchScheduler::EnforceHandleTimeouts(double CurrentTime)
{
    // TODO Phase 2
}

#pragma endregion