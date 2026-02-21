#include "Batching/ISMBatchTransformer.h"
#include "Batching/ISMBatchScheduler.h"
#include "ISMRuntimeComponent.h"

FISMMutationHandle::FISMMutationHandle(
    TWeakObjectPtr<UISMBatchScheduler> InScheduler,
    TWeakObjectPtr<UISMRuntimeComponent> InComponent,
    FIntVector InCellCoords,
    uint32 InGenerationToken,
    double InIssuedTime)
    : Scheduler(InScheduler)
    , TargetComponent(InComponent)
    , CellCoordinates(InCellCoords)
    , GenerationToken(InGenerationToken)
    , IssuedTimeSeconds(InIssuedTime)
    , bIsOpen(true)
{
}

FISMMutationHandle::FISMMutationHandle(FISMMutationHandle&& Other) noexcept
    : Scheduler(MoveTemp(Other.Scheduler))
    , TargetComponent(MoveTemp(Other.TargetComponent))
    , CellCoordinates(Other.CellCoordinates)
    , GenerationToken(Other.GenerationToken)
    , IssuedTimeSeconds(Other.IssuedTimeSeconds)
    , bIsOpen(Other.bIsOpen)
{
	// Source handle is now closed - it transferred ownership
    Other.bIsOpen = false;
}

FISMMutationHandle& FISMMutationHandle::operator=(FISMMutationHandle&& Other) noexcept
{
    if (this != &Other)
    {
        // If we still have an open handle, abandon it before taking the new one
        if (bIsOpen)
        {
            Abandon();
        }

        Scheduler = MoveTemp(Other.Scheduler);
        TargetComponent = MoveTemp(Other.TargetComponent);
        CellCoordinates = Other.CellCoordinates;
        GenerationToken = Other.GenerationToken;
        IssuedTimeSeconds = Other.IssuedTimeSeconds;
        bIsOpen = Other.bIsOpen;

        Other.bIsOpen = false;
    }
    return *this;
}

FISMMutationHandle::~FISMMutationHandle()
{
	if(bIsOpen)
	{
		UE_LOG(LogTemp, Warning,TEXT("FISMMutationHandle destroyed while still open. Abandoning automatically."));
		Abandon();
	}
}

void FISMMutationHandle::Release(FISMBatchMutationResult&& Result)
{
    if (!bIsOpen)
    {
        UE_LOG(LogTemp, Warning, TEXT("FISMMutationHandle::Release called on already-released handle. Ignored."));
        return;
    }

    bIsOpen = false;

    if (UISMBatchScheduler* Sched = Scheduler.Get())
    {
        Sched->OnHandleReleased(MoveTemp(Result), CellCoordinates, TargetComponent);
    }
}

void FISMMutationHandle::Abandon()
{
    if (!bIsOpen)
    {
        UE_LOG(LogTemp, Warning, TEXT("FISMMutationHandle::Abandon called on already-released handle. Ignored."));
        return;
    }

    bIsOpen = false;

    if (UISMBatchScheduler* Sched = Scheduler.Get())
    {
        Sched->OnHandleAbandoned(CellCoordinates, TargetComponent);
    }
}
