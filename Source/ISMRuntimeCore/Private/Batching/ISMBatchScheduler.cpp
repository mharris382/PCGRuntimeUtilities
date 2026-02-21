#include "Batching/ISMBatchScheduler.h"
#include "Batching/ISMBatchTransformer.h"
#include "Batching/ISMBatchTypes.h"
#include "Logging/LogMacros.h"
#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"


void UISMBatchScheduler::Initialize(UISMRuntimeSubsystem* InOwningSubsystem)
{
	if (bInitialized)
		return;

	bInitialized = true;
}

void UISMBatchScheduler::Deinitialize()
{
	bInitialized = false;
	UnregisterAllTransformers();
}

bool UISMBatchScheduler::RegisterTransformer(IISMBatchTransformer* Transformer)
{
	if (!Transformer)
		return false;

	if (IsTransformerRegistered(Transformer->GetTransformerName()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Transformer %s is already registered with the batch scheduler."), *Transformer->GetTransformerName().ToString());
		return false;
	}

	auto entry = FISMTransformerEntry();
	entry.Transformer = Transformer;
	entry.Name = Transformer->GetTransformerName();
	entry.Priority = Transformer->GetPriority();
	entry.bRegistered = true;
	RegisteredTransformers.Add(entry);
	return true;
}

void UISMBatchScheduler::UnregisterTransformer(FName TransformerName)
{
	if (!IsTransformerRegistered(TransformerName))
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot unregister transformer %s because it is not registered."), *TransformerName.ToString());
		return;
	}
}

void UISMBatchScheduler::UnregisterAllTransformers()
{
	for (const auto& Entry : RegisteredTransformers)
	{
		UnregisterTransformer(Entry.Name);
	}
	RegisteredTransformers.Empty();
}

bool UISMBatchScheduler::IsTransformerRegistered(FName TransformerName) const
{
	UE_LOG(LogTemp, Warning, TEXT("UISMBatchScheduler::IsTransformerRegistered is not implemented yet."));
	return false;
}

void UISMBatchScheduler::Tick(float DeltaTime)
{
}

int32 UISMBatchScheduler::GetInFlightChunkCount() const
{
	return InFlightChunks.Num();
}

int32 UISMBatchScheduler::GetPendingResultCount() const
{
	return ActiveCycles.Num();
}

TArray<FName> UISMBatchScheduler::GetTransformersWithOpenHandles() const
{
	return TArray<FName>();
}

void UISMBatchScheduler::OnHandleReleased(FISMBatchMutationResult&& Result, FIntVector CellCoords, TWeakObjectPtr<UISMRuntimeComponent> Component)
{
}

void UISMBatchScheduler::OnHandleAbandoned(FIntVector CellCoords, TWeakObjectPtr<UISMRuntimeComponent> Component)
{
}

void UISMBatchScheduler::DispatchDirtyTransformers()
{
}

void UISMBatchScheduler::DispatchTransformer(FISMTransformerEntry& Entry)
{
}

int32 UISMBatchScheduler::DispatchComponentChunks(IISMBatchTransformer* Transformer, UISMRuntimeComponent* Component, const FISMSnapshotRequest& Request, FName TransformerName)
{
	return int32();
}

FISMBatchSnapshot UISMBatchScheduler::BuildSnapshot(UISMRuntimeComponent* Component, FIntVector CellCoords, const TArray<int32>& InstanceIndices, EISMSnapshotField ReadMask) const
{
	return FISMBatchSnapshot();
}

void UISMBatchScheduler::DrainAndApplyResults()
{
}

bool UISMBatchScheduler::ApplyMutationResult(const FISMBatchMutationResult& Result)
{
	return false;
}

void UISMBatchScheduler::EnforceHandleTimeouts(double CurrentTime)
{
}

void UISMBatchScheduler::NotifyChunkResolved(FName TransformerName, bool bWasAbandoned)
{
}

FISMInFlightChunk& UISMBatchScheduler::TrackNewChunk(FName TransformerName, FISMMutationHandle&& Handle)
{
	// TODO: insert return statement here
	FISMInFlightChunk& NewChunk = InFlightChunks.AddDefaulted_GetRef();
	NewChunk.TransformerName = TransformerName;
	NewChunk.IssuedTimeSeconds = Handle.GetIssuedTime();
	NewChunk.Handle = MakeShared<FISMMutationHandle>(MoveTemp(Handle));
	return NewChunk;
}
