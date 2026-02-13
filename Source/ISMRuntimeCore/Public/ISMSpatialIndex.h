#pragma once

#include "CoreMinimal.h"

/**
 * Simple spatial hash for fast instance queries.
 * Divides world into uniform grid cells and stores instance indices per cell.
 */
class ISMRUNTIMECORE_API FISMSpatialIndex
{
public:
    /**
     * Constructor
     * @param InCellSize Size of each grid cell in world units (default 1000cm = 10m)
     *                   Rule of thumb: Use 2x your typical query radius
     */
    explicit FISMSpatialIndex(float InCellSize = 1000.0f);
    
    /** Add an instance to the spatial index */
    void AddInstance(int32 InstanceIndex, const FVector& Location);
    
    /** Remove an instance from the spatial index */
    void RemoveInstance(int32 InstanceIndex, const FVector& Location);
    
    /** Update an instance's location in the spatial index */
    void UpdateInstance(int32 InstanceIndex, const FVector& OldLocation, const FVector& NewLocation);
    
    /** Query instances within a sphere */
    void QueryRadius(const FVector& Center, float Radius, TArray<int32>& OutInstances) const;
    
    /** Query instances within a box */
    void QueryBox(const FBox& Box, TArray<int32>& OutInstances) const;
    
    /** Find the nearest instance to a location */
    int32 FindNearestInstance(const FVector& Location, const TArray<FVector>& InstanceLocations, float MaxDistance = -1.0f) const;
    
    /** Clear all data from the index */
    void Clear();
    
    /** Rebuild the entire index from scratch (useful after bulk changes) */
    void Rebuild(const TArray<FVector>& InstanceLocations);
    
    // Debug functions
    int32 GetCellCount() const { return Cells.Num(); }
    int32 GetTotalInstances() const;
    float GetCellSize() const { return CellSize; }
    void DebugDraw(class UWorld* World, float Duration = 0.0f) const;
    
private:
    /** Convert world location to cell coordinate */
    FIntVector WorldLocationToCell(const FVector& Location) const;
    
    /** Get world bounds of a cell */
    FBox GetCellBounds(const FIntVector& CellCoord) const;
    
    /** Size of each cell in world units */
    float CellSize;
    
    /** Map of cell coordinates to instance indices in that cell */
    TMap<FIntVector, TArray<int32>> Cells;
};