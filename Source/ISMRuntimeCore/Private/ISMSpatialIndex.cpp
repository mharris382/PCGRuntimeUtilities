// Copyright (c) 2025 Max Harris
// Published by Procedural Architect

#include "ISMSpatialIndex.h"

FISMSpatialIndex::FISMSpatialIndex(float InCellSize)
{
}

void FISMSpatialIndex::AddInstance(int32 InstanceIndex, const FVector& Location)
{
}

void FISMSpatialIndex::RemoveInstance(int32 InstanceIndex, const FVector& Location)
{
}

void FISMSpatialIndex::UpdateInstance(int32 InstanceIndex, const FVector& OldLocation, const FVector& NewLocation)
{
}

void FISMSpatialIndex::QueryRadius(const FVector& Center, float Radius, TArray<int32>& OutInstances) const
{
}

void FISMSpatialIndex::QueryBox(const FBox& Box, TArray<int32>& OutInstances) const
{
}

int32 FISMSpatialIndex::FindNearestInstance(const FVector& Location, const TArray<FVector>& InstanceLocations, float MaxDistance) const
{
	return int32();
}

void FISMSpatialIndex::Clear()
{
}

void FISMSpatialIndex::Rebuild(const TArray<FVector>& InstanceLocations)
{
}

int32 FISMSpatialIndex::GetTotalInstances() const
{
	return int32();
}

void FISMSpatialIndex::DebugDraw(UWorld* World, float Duration) const
{
}

FIntVector FISMSpatialIndex::WorldLocationToCell(const FVector& Location) const
{
	return FIntVector();
}

FBox FISMSpatialIndex::GetCellBounds(const FIntVector& CellCoord) const
{
	return FBox();
}
