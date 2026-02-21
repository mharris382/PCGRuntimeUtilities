#include "Batching/ISMBatchTransformer.h"

FISMMutationHandle::FISMMutationHandle(FISMMutationHandle&& Other) noexcept
{
	Scheduler = Other.Scheduler;
	TargetComponent = Other.TargetComponent;
	CellCoordinates = Other.CellCoordinates;
	GenerationToken = Other.GenerationToken;
	IssuedTimeSeconds = Other.IssuedTimeSeconds;
	bIsOpen = Other.bIsOpen;

}

FISMMutationHandle& FISMMutationHandle::operator=(FISMMutationHandle&& Other) noexcept
{
	// TODO: insert return statement here
	return *this;
}

FISMMutationHandle::~FISMMutationHandle()
{
}

void FISMMutationHandle::Release(FISMBatchMutationResult&& Result)
{

}

void FISMMutationHandle::Abandon()
{
}
