// ISMSpatialIndex.h
#pragma once

#include "CoreMinimal.h"

/**
 * Simple spatial hash for fast instance queries.
 * Divides world into uniform grid cells and stores instance indices per cell.
 *
 * Thread Safety: NOT thread-safe. Assumes single-threaded access.
 * Performance: O(1) add/remove, O(k) query where k = instances in overlapping cells
 */
class ISMRUNTIMECORE_API FISMSpatialIndex
{
public:
    /**
     * Constructor
     * @param InCellSize Size of each grid cell in world units (default 1000cm = 10m)
     * Rule of thumb: Use 2x your typical query radius
     */
    explicit FISMSpatialIndex(float InCellSize = 1000.0f);

    /**
     * Add an instance to the spatial index.
     * Time Complexity: O(1) average case
     * @param InstanceIndex The instance index to add
     * @param Location World location of the instance
     */
    void AddInstance(int32 InstanceIndex, const FVector& Location);

    /**
     * Remove an instance from the spatial index.
     * Time Complexity: O(n) where n = instances in the cell (typically small)
     * @param InstanceIndex The instance index to remove
     * @param Location World location of the instance (must match location used in AddInstance)
     */
    void RemoveInstance(int32 InstanceIndex, const FVector& Location);

    /**
     * Update an instance's location in the spatial index.
     * Only performs work if the instance moved to a different cell.
     * Time Complexity: O(n) where n = instances in the cell
     * @param InstanceIndex The instance index to update
     * @param OldLocation Previous world location
     * @param NewLocation New world location
     */
    void UpdateInstance(int32 InstanceIndex, const FVector& OldLocation, const FVector& NewLocation);

    /**
     * Query instances within a sphere.
     * Returns ALL instances in cells that overlap the sphere (may include false positives).
     * Time Complexity: O(k*c) where k = cells overlapped, c = avg instances per cell
     * @param Center Center of the query sphere
     * @param Radius Radius of the query sphere
     * @param OutInstances Array to populate with instance indices (will be cleared first)
     */
    void QueryRadius(const FVector& Center, float Radius, TArray<int32>& OutInstances) const;

    /**
     * Query instances within a box.
     * Returns ALL instances in cells that overlap the box (may include false positives).
     * Time Complexity: O(k*c) where k = cells overlapped, c = avg instances per cell
     * @param Box The query box
     * @param OutInstances Array to populate with instance indices (will be cleared first)
     */
    void QueryBox(const FBox& Box, TArray<int32>& OutInstances) const;

    /**
     * Find the nearest instance to a location.
     * WARNING: This requires checking actual distances, so can be expensive.
     * Time Complexity: O(n) where n = instances in nearby cells
     * @param Location Query location
     * @param InstanceLocations Array of actual instance locations (indexed by instance index)
     * @param MaxDistance Maximum search distance (-1 for unlimited)
     * @return Instance index of nearest instance, or INDEX_NONE if none found
     */
    int32 FindNearestInstance(
        const FVector& Location,
        const TArray<FVector>& InstanceLocations,
        float MaxDistance = -1.0f) const;

    /**
     * Clear all data from the index.
     * Time Complexity: O(n) where n = total instances
     */
    void Clear();

    /**
     * Rebuild the entire index from scratch.
     * Useful after bulk changes or when loading saved data.
     * Time Complexity: O(n) where n = total instances
     * @param InstanceLocations Array of all instance locations
     */
    void Rebuild(const TArray<FVector>& InstanceLocations);

    // ===== Debug / Statistics =====

    /** Get number of cells currently allocated */
    int32 GetCellCount() const { return Cells.Num(); }

    /** Get total number of instance references stored (may have duplicates) */
    int32 GetTotalInstances() const;

    /** Get the cell size */
    float GetCellSize() const { return CellSize; }

    /**
     * Get average instances per cell (for tuning cell size).
     * Lower is better - aim for 10-50 instances per cell.
     */
    float GetAverageInstancesPerCell() const;

    /**
     * Get maximum instances in any single cell.
     * Very high values (>100) suggest cell size is too large.
     */
    int32 GetMaxInstancesPerCell() const;

    /**
     * Debug draw the spatial grid.
     * Only works in editor builds.
     */
    void DebugDraw(class UWorld* World, float Duration = 0.0f, bool bShowInstanceCounts = true) const;

private:
    /**
     * Convert world location to cell coordinate.
     * Uses floor division to ensure consistent cell assignment.
     */
    FIntVector WorldLocationToCell(const FVector& Location) const;

    /**
     * Get world-space bounds of a cell.
     * Useful for debug visualization.
     */
    FBox GetCellBounds(const FIntVector& CellCoord) const;

    /** Size of each cell in world units (centimeters) */
    float CellSize;

    /**
     * Map of cell coordinates to instance indices in that cell.
     * Using TMap<FIntVector, TArray<int32>> instead of unordered_map for:
     * - Better integration with Unreal's memory allocators
     * - Easier debugging (shows nicely in debugger)
     * - Predictable performance
     */
    TMap<FIntVector, TArray<int32>> Cells;
};