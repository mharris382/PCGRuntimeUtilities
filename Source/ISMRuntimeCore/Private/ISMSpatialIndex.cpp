// Copyright (c) 2025 Max Harris
// Published by Procedural Architect

// ISMSpatialIndex.cpp
#include "ISMSpatialIndex.h"
#include "DrawDebugHelpers.h"

FISMSpatialIndex::FISMSpatialIndex(float InCellSize)
    : CellSize(InCellSize)
{
    ensure(CellSize > 0.0f);

    // Reserve some space to reduce allocations
    Cells.Reserve(64);
}

void FISMSpatialIndex::AddInstance(int32 InstanceIndex, const FVector& Location)
{
    FIntVector CellCoord = WorldLocationToCell(Location);

    // Get or create cell
    TArray<int32>& Cell = Cells.FindOrAdd(CellCoord);

    // Avoid duplicates (shouldn't happen in normal usage, but be safe)
    Cell.AddUnique(InstanceIndex);
}

void FISMSpatialIndex::RemoveInstance(int32 InstanceIndex, const FVector& Location)
{
    FIntVector CellCoord = WorldLocationToCell(Location);

    if (TArray<int32>* Cell = Cells.Find(CellCoord))
    {
        // Remove the instance
        Cell->Remove(InstanceIndex);

        // Clean up empty cells to save memory
        if (Cell->Num() == 0)
        {
            Cells.Remove(CellCoord);
        }
    }
}

void FISMSpatialIndex::UpdateInstance(int32 InstanceIndex, const FVector& OldLocation, const FVector& NewLocation)
{
    FIntVector OldCell = WorldLocationToCell(OldLocation);
    FIntVector NewCell = WorldLocationToCell(NewLocation);

    // Only update if cell changed (common case: instance moves within same cell)
    if (OldCell != NewCell)
    {
        RemoveInstance(InstanceIndex, OldLocation);
        AddInstance(InstanceIndex, NewLocation);
    }
}

void FISMSpatialIndex::QueryRadius(const FVector& Center, float Radius, TArray<int32>& OutInstances) const
{
    OutInstances.Reset();

    // Calculate AABB of the sphere
    FVector Min = Center - FVector(Radius);
    FVector Max = Center + FVector(Radius);

    FIntVector MinCell = WorldLocationToCell(Min);
    FIntVector MaxCell = WorldLocationToCell(Max);

    // Reserve approximate space
    int32 EstimatedResults = (MaxCell.X - MinCell.X + 1) *
        (MaxCell.Y - MinCell.Y + 1) *
        (MaxCell.Z - MinCell.Z + 1) * 10; // Assume ~10 per cell
    OutInstances.Reserve(EstimatedResults);

    // Iterate overlapping cells
    for (int32 X = MinCell.X; X <= MaxCell.X; X++)
    {
        for (int32 Y = MinCell.Y; Y <= MaxCell.Y; Y++)
        {
            for (int32 Z = MinCell.Z; Z <= MaxCell.Z; Z++)
            {
                FIntVector CellCoord(X, Y, Z);

                if (const TArray<int32>* Cell = Cells.Find(CellCoord))
                {
                    OutInstances.Append(*Cell);
                }
            }
        }
    }

    // Note: Results may include instances outside the sphere.
    // Caller should do precise distance check if needed.
}

void FISMSpatialIndex::QueryBox(const FBox& Box, TArray<int32>& OutInstances) const
{
    OutInstances.Reset();

    FIntVector MinCell = WorldLocationToCell(Box.Min);
    FIntVector MaxCell = WorldLocationToCell(Box.Max);

    // Reserve approximate space
    int32 EstimatedResults = (MaxCell.X - MinCell.X + 1) *
        (MaxCell.Y - MinCell.Y + 1) *
        (MaxCell.Z - MinCell.Z + 1) * 10;
    OutInstances.Reserve(EstimatedResults);

    for (int32 X = MinCell.X; X <= MaxCell.X; X++)
    {
        for (int32 Y = MinCell.Y; Y <= MaxCell.Y; Y++)
        {
            for (int32 Z = MinCell.Z; Z <= MaxCell.Z; Z++)
            {
                if (const TArray<int32>* Cell = Cells.Find(FIntVector(X, Y, Z)))
                {
                    OutInstances.Append(*Cell);
                }
            }
        }
    }
}

int32 FISMSpatialIndex::FindNearestInstance(
    const FVector& Location,
    const TArray<FVector>& InstanceLocations,
    float MaxDistance) const
{
    // Start with a reasonable search radius
    float SearchRadius = MaxDistance > 0.0f ? MaxDistance : CellSize * 2.0f;

    int32 NearestIndex = INDEX_NONE;
    float NearestDistSq = MaxDistance > 0.0f ? (MaxDistance * MaxDistance) : MAX_FLT;

    // Expand search if needed
    const int32 MaxSearchIterations = 5; // Prevent infinite loops
    int32 SearchIteration = 0;

    while (NearestIndex == INDEX_NONE && SearchIteration < MaxSearchIterations)
    {
        TArray<int32> CandidateIndices;
        QueryRadius(Location, SearchRadius, CandidateIndices);

        // Check actual distances
        for (int32 CandidateIndex : CandidateIndices)
        {
            if (!InstanceLocations.IsValidIndex(CandidateIndex))
            {
                continue;
            }

            float DistSq = FVector::DistSquared(Location, InstanceLocations[CandidateIndex]);

            if (DistSq < NearestDistSq)
            {
                NearestDistSq = DistSq;
                NearestIndex = CandidateIndex;
            }
        }

        // If we found something or we're at max distance, stop
        if (NearestIndex != INDEX_NONE || (MaxDistance > 0.0f && SearchRadius >= MaxDistance))
        {
            break;
        }

        // Expand search radius
        SearchRadius *= 2.0f;
        SearchIteration++;
    }

    return NearestIndex;
}

void FISMSpatialIndex::Clear()
{
    Cells.Empty();
}

void FISMSpatialIndex::Rebuild(const TArray<FVector>& InstanceLocations)
{
    Clear();

    // Reserve space based on instance count
    Cells.Reserve(FMath::Max(64, InstanceLocations.Num() / 10));

    for (int32 i = 0; i < InstanceLocations.Num(); i++)
    {
        AddInstance(i, InstanceLocations[i]);
    }
}

int32 FISMSpatialIndex::GetTotalInstances() const
{
    int32 Total = 0;
    for (const auto& Pair : Cells)
    {
        Total += Pair.Value.Num();
    }
    return Total;
}

float FISMSpatialIndex::GetAverageInstancesPerCell() const
{
    if (Cells.Num() == 0)
    {
        return 0.0f;
    }

    return static_cast<float>(GetTotalInstances()) / static_cast<float>(Cells.Num());
}

int32 FISMSpatialIndex::GetMaxInstancesPerCell() const
{
    int32 MaxCount = 0;

    for (const auto& Pair : Cells)
    {
        MaxCount = FMath::Max(MaxCount, Pair.Value.Num());
    }

    return MaxCount;
}

FIntVector FISMSpatialIndex::WorldLocationToCell(const FVector& Location) const
{
    // Use floor to ensure negative coordinates work correctly
    // E.g., location -50 with cell size 100 should be cell -1, not 0
    return FIntVector(
        FMath::FloorToInt(Location.X / CellSize),
        FMath::FloorToInt(Location.Y / CellSize),
        FMath::FloorToInt(Location.Z / CellSize)
    );
}

FBox FISMSpatialIndex::GetCellBounds(const FIntVector& CellCoord) const
{
    FVector Min(
        CellCoord.X * CellSize,
        CellCoord.Y * CellSize,
        CellCoord.Z * CellSize
    );

    FVector Max = Min + FVector(CellSize);

    return FBox(Min, Max);
}

void FISMSpatialIndex::DebugDraw(UWorld* World, float Duration, bool bShowInstanceCounts) const
{
#if ENABLE_DRAW_DEBUG
    if (!World)
    {
        return;
    }

    for (const auto& Pair : Cells)
    {
        const FIntVector& CellCoord = Pair.Key;
        const TArray<int32>& Instances = Pair.Value;

        FBox CellBox = GetCellBounds(CellCoord);

        // Color based on instance density
        // Green = few instances (good)
        // Yellow = medium
        // Red = many instances (might want smaller cells)
        float Density = FMath::Clamp(Instances.Num() / 50.0f, 0.0f, 1.0f);
        FColor Color = FLinearColor::LerpUsingHSV(
            FLinearColor::Green,
            FLinearColor::Red,
            Density
        ).ToFColor(true);

        // Draw cell bounds
        DrawDebugBox(
            World,
            CellBox.GetCenter(),
            CellBox.GetExtent(),
            Color,
            false,
            Duration,
            0,
            2.0f
        );

        // Optionally draw instance count
        if (bShowInstanceCounts)
        {
            DrawDebugString(
                World,
                CellBox.GetCenter(),
                FString::Printf(TEXT("%d"), Instances.Num()),
                nullptr,
                FColor::White,
                Duration,
                true,
                1.2f
            );
        }
    }
#endif
}