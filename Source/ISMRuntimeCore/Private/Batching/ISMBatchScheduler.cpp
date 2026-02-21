#include "Batching/ISMBatchScheduler.h"
#include "Batching/ISMBatchTransformer.h"
#include "Batching/ISMBatchTypes.h"
#include "Logging/LogMacros.h"
#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"


// ===== Init/Teardown =====

#pragma region INIT_AND_TEARDOWN

void UISMBatchScheduler::Initialize(UISMRuntimeSubsystem* InOwningSubsystem)
{
	OwningSubsystem = InOwningSubsystem;
	bInitialized = true;
}

void UISMBatchScheduler::Deinitialize()
{
	UnregisterAllTransformers();
	bInitialized = false;
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

	// Keep sorted by priority descending so DispatchDirtyTransformers processes
	// high-priority transformers first (Phase 2 conflict resolution depends on this)
	RegisteredTransformers.StableSort([](const FISMTransformerEntry& A, const FISMTransformerEntry& B)
		{
			return A.Priority > B.Priority;
		});

	return true;
}

void UISMBatchScheduler::UnregisterTransformer(FName TransformerName)
{
	// TODO Phase 2: abandon in-flight chunks for this transformer gracefully
	// For Phase 1, ProcessChunk is synchronous so there are never in-flight chunks
	// at unregister time during normal test flow.

	RegisteredTransformers.RemoveAll([&TransformerName](const FISMTransformerEntry& Entry)
		{
			return Entry.Name == TransformerName;
		});
}

void UISMBatchScheduler::UnregisterAllTransformers()
{
	RegisteredTransformers.Empty();
	InFlightChunks.Empty();
	ActiveCycles.Empty();
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

	// Phase 1 tick order:
	// 1. Drain any results that came in from the previous tick (or synchronously this tick)
	// 2. Dispatch dirty transformers (which in Phase 1 resolve synchronously, posting to queue)
	// 3. Drain again to catch results posted during dispatch this tick
	//
	// The double-drain is intentional for Phase 1 synchronous correctness:
	// ProcessChunk calls Handle.Release which posts to ReleasedResultQueue,
	// so we drain after dispatch to pick those up in the same tick.

	DrainAndApplyResults();
	DispatchDirtyTransformers();
	DrainAndApplyResults();


	// TODO Phase 2: EnforceHandleTimeouts(FPlatformTime::Seconds());
}


// ===== Dispatch =====

#pragma region DISPATCH

void UISMBatchScheduler::DispatchDirtyTransformers()
{
	for (FISMTransformerEntry& Entry : RegisteredTransformers)
	{
		if (!Entry.Transformer || !Entry.bRegistered) {
			continue;
		}
		if (!Entry.Transformer->IsDirty()) {
			continue;
		}
		DispatchTransformer(Entry);
		Entry.Transformer->ClearDirty();
	}
}

void UISMBatchScheduler::DispatchTransformer(FISMTransformerEntry& Entry)
{
	IISMBatchTransformer* Transformer = Entry.Transformer;
	FISMSnapshotRequest Request = Transformer->BuildRequest();

	TArray<TWeakObjectPtr<UISMRuntimeComponent>> Targets = Request.TargetComponents;
	if(Targets.IsEmpty())
	{
		// Phase 1: no-op if nothing specified. 
	    // Phase 2: walk subsystem's registered components here.
		return;
	}
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
	// Phase 1: single chunk containing ALL active instances on the component.
	// No spatial cell splitting, no MaxInstancesPerChunk enforcement.
	// TODO Phase 2: walk SpatialIndex cells, split oversized cells into sub-chunks.

	// Gather all active instance indices
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
	if (AllIndices.IsEmpty())
	{
		return 0;
	}

	if (!Component->SetBatchLocked(true))
	{
		return 0;
	}
	FISMBatchSnapshot Snapshot = BuildSnapshot(
		Component, FIntVector::ZeroValue, AllIndices, Request.ReadMask);

	const double IssuedTime = FPlatformTime::Seconds();

	// Track identity BEFORE issuing handle - one entry, no live handle stored
	TrackNewChunk(TransformerName, Component, FIntVector::ZeroValue, IssuedTime);
	TArray<FISMBatchSnapshot> IssuedSnapshots;
	IssuedSnapshots.Add(Snapshot); // copy - transformer owns its snapshot array separately

	Transformer->OnHandleIssued(IssuedSnapshots);

	// Issue exactly ONE handle - transformer owns it entirely
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
	// Phase 1: generation token is 0 (no slot-reuse tracking yet)
	Snapshot.ComponentGenerationToken = 0;

	Snapshot.Instances.Reserve(InstanceIndices.Num());

	for (int32 Idx : InstanceIndices)
	{
		FISMInstanceSnapshot& InstSnap = Snapshot.Instances.AddDefaulted_GetRef();
		InstSnap.InstanceIndex = Idx;

		if (EnumHasAnyFlags(ReadMask, EISMSnapshotField::Transform))
		{
			InstSnap.Transform = Component->GetInstanceTransform(Idx);
		}

		if (EnumHasAnyFlags(ReadMask, EISMSnapshotField::CustomData))
		{
			InstSnap.CustomData = Component->GetInstanceCustomData(Idx);
		}

		if (EnumHasAnyFlags(ReadMask, EISMSnapshotField::StateFlags))
		{
			InstSnap.StateFlags = Component->GetInstanceStateFlags(Idx);
		}
	}

	return Snapshot;
}

// ===== Drain & Apply =====

#pragma region DRAIN_AND_APPLY

void UISMBatchScheduler::DrainAndApplyResults()
{
	if (!bInitialized) return;
	// Drain released results
	{
		FISMBatchMutationResult Result;

		while (ReleasedResultQueue.Dequeue(Result))
		{
			UISMRuntimeComponent* Target = Result.TargetComponent.Get();
			if (!Target) {
				UE_LOG(LogTemp, Warning, TEXT("Received mutation result for a component that no longer exists. Discarding."));
				continue;  // <-- this guard is missing
			}
			ApplyMutationResult(Result);

			// Find which in-flight chunk this corresponds to and mark it resolved
			// Phase 1: component + cell is enough to identify the chunk
			for (FISMInFlightChunk& Chunk : InFlightChunks)
			{
				if (!Chunk.bReleased &&
					Chunk.TargetComponent == Result.TargetComponent &&
					Chunk.CellCoordinates == Result.CellCoordinates)
				{
					Chunk.bReleased = true;
					//Chunk.ResolvedResult = Result;
					NotifyChunkResolved(Chunk.TransformerName, false);
					break;
				}
			}
		}
	}

	// Drain abandoned chunks
	{
		FAbandonedChunkKey Key;
		while (AbandonedChunkQueue.Dequeue(Key))
		{
			for (FISMInFlightChunk& Chunk : InFlightChunks)
			{
				if (!Chunk.bReleased && !Chunk.bAbandoned &&
					Chunk.TargetComponent == Key.Component &&
					Chunk.CellCoordinates == Key.Cell)
				{
					Chunk.bAbandoned = true;
					Chunk.bReleased = true;    // treat as resolved for cycle tracking

					// Unlock the component - no writes happened
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

	// Remove fully resolved chunks
	InFlightChunks.RemoveAll([](const FISMInFlightChunk& C)
		{
			return C.bReleased;
		});
}



bool UISMBatchScheduler::ApplyMutationResult(const FISMBatchMutationResult& Result)
{
	UISMRuntimeComponent* Comp = Result.TargetComponent.Get();
	if (!Comp)
	{
		return false;
	}
	
	// Phase 1: no generation token validation (all tokens are 0)
  // TODO Phase 2: validate Result.ComponentGenerationToken against Comp's current token

	for (const FISMInstanceMutation& Mutation : Result.Mutations)
	{
		const int32 Idx = Mutation.InstanceIndex;

		// Core safety check: never write to a destroyed instance
		if (Comp->IsInstanceDestroyed(Idx))
		{
			continue;
		}

		// Apply transform if declared and present
		if (EnumHasAnyFlags(Result.WrittenFields, EISMSnapshotField::Transform))
		{
			if (Mutation.NewTransform.IsSet())
			{
				Comp->UpdateInstanceTransform(Idx, Mutation.NewTransform.GetValue(),
					true, false);
			}
		}

		// Apply custom data if declared and present
		if (EnumHasAnyFlags(Result.WrittenFields, EISMSnapshotField::CustomData))
		{
			if (Mutation.NewCustomData.IsSet())
			{
				Comp->SetInstanceCustomData(Idx, Mutation.NewCustomData.GetValue());
			}

			// Sparse slot overrides applied after full replacement
			for (const TTuple<int32, float>& SlotOverride : Mutation.CustomDataSlotOverrides)
			{
				Comp->SetInstanceCustomDataValue(Idx, SlotOverride.Key, SlotOverride.Value);
			}
		}

		// Apply state flags if declared and present
		if (EnumHasAnyFlags(Result.WrittenFields, EISMSnapshotField::StateFlags))
		{
			if (Mutation.NewStateFlags.IsSet())
			{
				// Apply each state flag individually to avoid clobbering unrelated flags
				const uint8 NewFlags = Mutation.NewStateFlags.GetValue();
				for (uint8 BitIdx = 0; BitIdx < 8; ++BitIdx)
				{
					const EISMInstanceState Flag = static_cast<EISMInstanceState>(1 << BitIdx);
					Comp->SetInstanceState(Idx, Flag, (NewFlags & (1 << BitIdx)) != 0);
				}
			}
		}
	}

	// Unlock the component now that writes are complete
	Comp->SetBatchLocked(false);

	return true;
}

#pragma endregion


// ===== Handle Callbacks (thread-safe - called from any thread) =====

void UISMBatchScheduler::OnHandleReleased(FISMBatchMutationResult&& Result, FIntVector CellCoords, TWeakObjectPtr<UISMRuntimeComponent> Component)
{
	// Stamp the cell/component onto the result for drain-side matching
	Result.CellCoordinates = CellCoords;
	Result.TargetComponent = Component;

	ReleasedResultQueue.Enqueue(MoveTemp(Result));
}

void UISMBatchScheduler::OnHandleAbandoned(FIntVector CellCoords, TWeakObjectPtr<UISMRuntimeComponent> Component)
{
	FAbandonedChunkKey Key;
	Key.Cell = CellCoords;
	Key.Component = Component;
	AbandonedChunkQueue.Enqueue(Key);
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

				// Fire OnRequestComplete on the game thread
				// Phase 1: we are always on the game thread here (synchronous)
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

	// Clean up completed cycles
	ActiveCycles.RemoveAll([](const FISMTransformerRequestCycle& C) { return C.bComplete; });
}

FISMInFlightChunk& UISMBatchScheduler::TrackNewChunk(
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




// ===== Statistics =====

int32 UISMBatchScheduler::GetInFlightChunkCount() const
{
	return InFlightChunks.Num();
}

int32 UISMBatchScheduler::GetPendingResultCount() const
{
	// TQueue doesn't expose a count - return in-flight as a proxy
	// TODO Phase 2: maintain a separate atomic counter
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

// ===== Phase 2 stubs =====

void UISMBatchScheduler::EnforceHandleTimeouts(double CurrentTime)
{
}


