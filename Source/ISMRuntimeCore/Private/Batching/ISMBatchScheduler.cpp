#include "Batching/ISMBatchScheduler.h"
#include "Batching/ISMBatchTransformer.h"
#include "Batching/ISMBatchTypes.h"
#include "Logging/LogMacros.h"
#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "Misc/ScopeLock.h"

// ===== Init/Teardown =====

#pragma region INIT_AND_TEARDOWN

void UISMBatchScheduler::Initialize(UISMRuntimeSubsystem* InOwningSubsystem)
{
	OwningSubsystem = InOwningSubsystem;
	bInitialized = true;
}

void UISMBatchScheduler::Deinitialize()
{
	if (!bInitialized) return;
	bInitialized = false;  // Set false FIRST before any cleanup


	UnregisterAllTransformers();

	// Discard any leftover results - no processing, just clear
	{
		FScopeLock ResultsLock(&ReleasedResultsLock);
		ReleasedResults.Empty();
	}
	{
		FScopeLock AbandonedLock(&AbandonedChunksLock);
		AbandonedChunks.Empty();
	}
}

bool UISMBatchScheduler::RegisterTransformer(IISMBatchTransformer* Transformer)
{
	if (!Transformer)
	{
		UE_LOG(LogTemp, Warning, TEXT("UISMBatchScheduler::RegisterTransformer - null transformer passed."));
		return false;
	}
	const FName Name = Transformer->GetTransformerName();
	if (IsTransformerRegistered(Name))
	{
		UE_LOG(LogTemp, Warning, TEXT("Transformer %s is already registered with the batch scheduler."), *Name.ToString());
		return false;
	}

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

void UISMBatchScheduler::UnregisterTransformer(FName TransformerName)
{
	if (RegisteredTransformers.Num() == 0)
		return;

	RegisteredTransformers.RemoveAll([&TransformerName](const FISMTransformerEntry& Entry)
		{
			return Entry.Name == TransformerName;
		});
}

void UISMBatchScheduler::UnregisterAllTransformers()
{

	if (InFlightChunks.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Unregistering all transformers while %d chunks are still in-flight. This may cause instability if those chunks resolve after this point."), InFlightChunks.Num());
		InFlightChunks.Empty();
	}
	if (ActiveCycles.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Unregistering all transformers while %d request cycles are still active. This may cause instability if those cycles resolve after this point."), ActiveCycles.Num());
		ActiveCycles.Empty();
	}

	if (RegisteredTransformers.Num() == 0) 
		return;
	RegisteredTransformers.Empty();
	
	
}

bool UISMBatchScheduler::IsTransformerRegistered(FName TransformerName) const
{
	return RegisteredTransformers.ContainsByPredicate([&TransformerName](const FISMTransformerEntry& Entry)
		{
			return Entry.Name == TransformerName;
		});
}

#pragma endregion


// ===== Tick =====

void UISMBatchScheduler::Tick(float DeltaTime)
{
	if (!bInitialized) return;

	DrainAndApplyResults();
	DispatchDirtyTransformers();
	DrainAndApplyResults();
}


// ===== Dispatch =====

#pragma region DISPATCH

void UISMBatchScheduler::DispatchDirtyTransformers()
{
	for (FISMTransformerEntry& Entry : RegisteredTransformers)
	{
		if (!Entry.Transformer || !Entry.bRegistered) continue;
		if (!Entry.Transformer->IsDirty()) continue;
		DispatchTransformer(Entry);
		Entry.Transformer->ClearDirty();
	}
}

void UISMBatchScheduler::DispatchTransformer(FISMTransformerEntry& Entry)
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

int32 UISMBatchScheduler::DispatchComponentChunks(IISMBatchTransformer* Transformer, UISMRuntimeComponent* Component, const FISMSnapshotRequest& Request, FName TransformerName)
{
	TArray<int32> AllIndices;
	const int32 TotalCount = Component->GetInstanceCount();
	AllIndices.Reserve(TotalCount);
	for (int32 i = 0; i < TotalCount; ++i)
	{
		if (!Component->IsInstanceDestroyed(i) && !Component->IsInstanceConverted(i))
		{
			AllIndices.Add(i);
		}
	}
	if (AllIndices.IsEmpty()) return 0;

	if (!Component->SetBatchLocked(true)) return 0;

	FISMBatchSnapshot Snapshot = BuildSnapshot(Component, FIntVector::ZeroValue, AllIndices, Request.ReadMask);

	const double IssuedTime = FPlatformTime::Seconds();
	TrackNewChunk(TransformerName, Component, FIntVector::ZeroValue, IssuedTime);

	Transformer->OnHandleIssued(Snapshot);

	FISMMutationHandle Handle(
		TWeakObjectPtr<UISMBatchScheduler>(this),
		TWeakObjectPtr<UISMRuntimeComponent>(Component),
		FIntVector::ZeroValue,
		0,
		IssuedTime);

	Transformer->ProcessChunk(MoveTemp(Snapshot), MoveTemp(Handle));

	return 1;
}

#pragma endregion


// ===== Snapshot =====

FISMBatchSnapshot UISMBatchScheduler::BuildSnapshot(UISMRuntimeComponent* Component, FIntVector CellCoords, const TArray<int32>& InstanceIndices, EISMSnapshotField ReadMask) const
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


// ===== Drain & Apply =====

#pragma region DRAIN_AND_APPLY

void UISMBatchScheduler::DrainAndApplyResults()
{
	if (!bInitialized) return;
	if (ReleasedResults.Num() == 0 && AbandonedChunks.Num() == 0) return;

	// --- Drain released results ---
	// Swap under lock so we hold the lock for the minimum possible time.
	// New results can be posted immediately after we release the lock.

	TArray<FISMBatchMutationResult> LocalResults;
	{
		FScopeLock Lock(&ReleasedResultsLock);
		LocalResults = MoveTemp(ReleasedResults);
		ReleasedResults.Reset();
	}

	if (ReleasedResults.Num() > 0)
	{
		for (int i = 0; i < LocalResults.Num(); ++i)
		{
			if (ReleasedResults.IsValidIndex(i) && ReleasedResults[i].TargetComponent.IsValid() && !ReleasedResults[i].TargetComponent.IsStale())
			{
				UISMRuntimeComponent* Target = ReleasedResults[i].TargetComponent.Get();
				if (!Target || !Target->IsValidLowLevel() || !IsValid(Target))
				{
					UE_LOG(LogISMRuntimeCore, Warning, TEXT("DrainAndApplyResults: Discarding result with stale component pointer"));
					continue;
				}

				ApplyMutationResult(ReleasedResults[i]);

				for (FISMInFlightChunk& Chunk : InFlightChunks)
				{
					if (!Chunk.bReleased &&
						Chunk.TargetComponent == ReleasedResults[i].TargetComponent &&
						Chunk.CellCoordinates == ReleasedResults[i].CellCoordinates)
					{
						Chunk.bReleased = true;
						NotifyChunkResolved(Chunk.TransformerName, false);
						break;
					}
				}
			}
		}
	}
	

	if (AbandonedChunks.Num() > 0)
	{
		UE_LOG(LogISMRuntimeCore, Warning, TEXT("DrainAndApplyResults: Processed %d abandoned chunks"), AbandonedChunks.Num());

		// --- Drain abandoned chunks ---
		TArray<FAbandonedChunkKey> LocalAbandoned;
		{
			FScopeLock Lock(&AbandonedChunksLock);
			LocalAbandoned = MoveTemp(AbandonedChunks);
			AbandonedChunks.Reset();
		}

		for (FAbandonedChunkKey& Key : LocalAbandoned)
		{
			if (!Key.Component.IsValid())
			{
				UE_LOG(LogISMRuntimeCore, Warning, TEXT("DrainAndApplyResults: Discarding abandoned entry with invalid component"));
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
					{
						Comp->SetBatchLocked(false);
					}

					NotifyChunkResolved(Chunk.TransformerName, true);
					break;
				}
			}
		}

	}

	// Remove resolved chunks
	InFlightChunks.RemoveAll([](const FISMInFlightChunk& C) { return C.bReleased; });
}

bool UISMBatchScheduler::ApplyMutationResult(const FISMBatchMutationResult& Result)
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
			{
				Comp->UpdateInstanceTransform(Idx, Mutation.NewTransform.GetValue(), true, false);
			}
		}

		if (EnumHasAnyFlags(Result.WrittenFields, EISMSnapshotField::CustomData))
		{
			if (Mutation.NewCustomData.IsSet())
			{
				Comp->SetInstanceCustomData(Idx, Mutation.NewCustomData.GetValue());
			}
			for (const TTuple<int32, float>& SlotOverride : Mutation.CustomDataSlotOverrides)
			{
				Comp->SetInstanceCustomDataValue(Idx, SlotOverride.Key, SlotOverride.Value);
			}
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

#pragma endregion


// ===== Handle Callbacks (thread-safe) =====

void UISMBatchScheduler::OnHandleReleased(FISMBatchMutationResult&& Result, FIntVector CellCoords, TWeakObjectPtr<UISMRuntimeComponent> Component)
{
	Result.CellCoordinates = CellCoords;
	Result.TargetComponent = Component;

	FScopeLock Lock(&ReleasedResultsLock);
	ReleasedResults.Add(MoveTemp(Result));
}

void UISMBatchScheduler::OnHandleAbandoned(FIntVector CellCoords, TWeakObjectPtr<UISMRuntimeComponent> Component)
{
	FAbandonedChunkKey Key;
	Key.Cell = CellCoords;
	Key.Component = Component;

	FScopeLock Lock(&AbandonedChunksLock);
	AbandonedChunks.Add(Key);
}


// ===== Cycle Tracking =====

void UISMBatchScheduler::NotifyChunkResolved(FName TransformerName, bool bWasAbandoned)
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

FISMInFlightChunk& UISMBatchScheduler::TrackNewChunk(FName TransformerName, TWeakObjectPtr<UISMRuntimeComponent> Component, FIntVector CellCoords, double IssuedTime)
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


// ===== Statistics =====

int32 UISMBatchScheduler::GetInFlightChunkCount() const
{
	return InFlightChunks.Num();
}

int32 UISMBatchScheduler::GetPendingResultCount() const
{
	return InFlightChunks.Num();
}

TArray<FName> UISMBatchScheduler::GetTransformersWithOpenHandles() const
{
	TArray<FName> Result;
	for (const FISMInFlightChunk& Chunk : InFlightChunks)
	{
		if (!Chunk.bReleased)
		{
			Result.AddUnique(Chunk.TransformerName);
		}
	}
	return Result;
}

void UISMBatchScheduler::EnforceHandleTimeouts(double CurrentTime)
{
	// TODO Phase 2
}