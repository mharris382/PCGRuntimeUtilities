#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ISMBatchTypes.generated.h"

// Forward declarations
class UISMRuntimeComponent;


// ============================================================
//  Field Mask
// ============================================================

/**
 * Flags declaring which per-instance data fields a transformer wants to read or write.
 * Used to minimize snapshot copy cost - only declared fields are populated.
 *
 * Read and write masks are declared separately on FISMSnapshotRequest.
 * The scheduler enforces that the transformer only writes fields it declared.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EISMSnapshotField : uint8
{
    None = 0,
    Transform = 1 << 0,   // Per-instance world transform
    CustomData = 1 << 1,   // Per-instance custom float data (material params)
    StateFlags = 1 << 2,   // EISMInstanceState bitmask
    // Tags     = 1 << 3,   // Reserved for V2 - tag mutation is more complex
};
ENUM_CLASS_FLAGS(EISMSnapshotField)


// ============================================================
//  Per-Instance Snapshot Data
// ============================================================

/**
 * Read-only snapshot of a single instance's data at the moment the snapshot was taken.
 * Fields are only populated if the corresponding bit was set in the request's ReadMask.
 *
 * Passed by value into async compute - the transformer owns this copy and can read it
 * freely on any thread without synchronization.
 */
 USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMInstanceSnapshot
{
    GENERATED_BODY()

    /** Index within the owning ISMRuntimeComponent. Always valid. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    int32 InstanceIndex = INDEX_NONE;

    /** World transform. Populated if ReadMask includes EISMSnapshotField::Transform. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    FTransform Transform;

    /** Per-instance custom float data. Populated if ReadMask includes EISMSnapshotField::CustomData. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    TArray<float> CustomData;

    /** Instance state bitmask. Populated if ReadMask includes EISMSnapshotField::StateFlags. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    uint8 StateFlags = 0;
};

// ============================================================
//  Batch Snapshot  (one spatial cell, one component)
// ============================================================

/**
 * Snapshot of all instances within a single spatial cell of a single ISMRuntimeComponent.
 * This is the unit of work handed to the transformer's ProcessChunk().
 *
 * One request may produce multiple chunks (one per occupied cell in the query bounds).
 * Each chunk is independently processable - the transformer can merge them client-side
 * if it needs cross-cell spatial awareness, but is not required to.
 *
 * Passed by value - the transformer owns the copy.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMBatchSnapshot
{
    GENERATED_BODY()

    /** The component this snapshot was taken from. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    TWeakObjectPtr<UISMRuntimeComponent> SourceComponent;

    /**
     * Generation token captured at snapshot time.
     * The scheduler validates this before applying results.
     * If the component has been modified (instances removed) since the snapshot,
     * the token will not match and the result is discarded as stale.
     */
    UPROPERTY( BlueprintReadOnly, Category = "ISM Batch")
    int32 ComponentGenerationToken = 0;

    /** Grid cell coordinates this snapshot covers (from the component's spatial index). */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    FIntVector CellCoordinates = FIntVector::ZeroValue;

    /** Fields that were copied into the instance snapshots. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    EISMSnapshotField PopulatedFields = EISMSnapshotField::None;

    /** Per-instance data for all active instances in this cell. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    TArray<FISMInstanceSnapshot> Instances;

    /** Whether this snapshot contains any instances. */
    bool IsEmpty() const { return Instances.Num() == 0; }

    /** Number of instances in this chunk. */
    int32 Num() const { return Instances.Num(); }
};


// ============================================================
//  Per-Instance Mutation
// ============================================================

/**
 * A single instance mutation returned by the transformer.
 * Only fields declared in the request's WriteMask should be set.
 * The scheduler silently ignores any fields not in the WriteMask (debug builds assert).
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMInstanceMutation
{
    GENERATED_BODY()

    /**
     * Index of the instance to mutate.
     * Must be an index that was present in the original FISMBatchSnapshot.
     * Returning an index not in the original snapshot causes the mutation to be discarded.
     */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    int32 InstanceIndex = INDEX_NONE;

    /**
     * New world transform. Applied if WriteMask includes EISMSnapshotField::Transform.
     * Leave at default (identity) if not writing transforms.
     */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    TOptional<FTransform> NewTransform;

    /**
     * New custom data. Applied if WriteMask includes EISMSnapshotField::CustomData.
     * Must be the same length as the original CustomData array or it is discarded.
     * Partial slot updates: use NewCustomDataSlots instead for fewer allocations.
     */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    TOptional<TArray<float>> NewCustomData;

    /**
     * Sparse custom data slot updates. Applied if WriteMask includes EISMSnapshotField::CustomData.
     * Preferred over NewCustomData when only a few slots change.
     * Format: (SlotIndex, NewValue). Applied after NewCustomData if both are set.
     */
    TArray<TTuple<int32, float>> CustomDataSlotOverrides;

    /**
     * New state flags bitmask. Applied if WriteMask includes EISMSnapshotField::StateFlags.
     */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    TOptional<uint8> NewStateFlags;
};


// ============================================================
//  Batch Mutation Result  (transformer's response to one chunk)
// ============================================================

/**
 * What the transformer returns after processing one FISMBatchSnapshot chunk.
 * The transformer may return fewer mutations than instances in the chunk -
 * instances not listed are left unchanged.
 *
 * The transformer may NOT add new instance indices not present in the original snapshot.
 * The transformer may NOT remove instances (V1 constraint - no structural changes).
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMBatchMutationResult
{
    GENERATED_BODY()

    /**
     * Token from the FISMBatchSnapshot this result corresponds to.
     * The scheduler matches this against the current component generation token
     * before applying. Stale results are silently discarded.
     */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    int32 ComponentGenerationToken = 0;

    /** The component this result targets. Must match the originating snapshot. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    TWeakObjectPtr<UISMRuntimeComponent> TargetComponent;

    /** Cell coordinates from the originating snapshot. For validation and logging. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    FIntVector CellCoordinates = FIntVector::ZeroValue;

    /**
     * Fields this result intends to write.
     * Must be a subset of the WriteMask declared in the original FISMSnapshotRequest.
     * The scheduler ignores writes to fields not declared here.
     */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Batch")
    EISMSnapshotField WrittenFields = EISMSnapshotField::None;

    /** The mutations to apply. May be empty (no-op for this chunk). */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    TArray<FISMInstanceMutation> Mutations;

    /** Convenience: whether there is anything to apply. */
    bool IsEmpty() const { return Mutations.IsEmpty(); }
};


// ============================================================
//  Snapshot Request
// ============================================================

/**
 * A transformer's declaration of what data it needs and where.
 * Submitted to UISMBatchScheduler::RegisterTransformer() or used
 * to drive the scheduler's chunk generation when a transformer signals it has work.
 *
 * The scheduler uses this to:
 *   1. Determine which components and cells to snapshot
 *   2. Know which fields to copy (minimizing allocation cost)
 *   3. Enforce that results only write declared fields
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMSnapshotRequest
{
    GENERATED_BODY()

    /**
     * Components to include in this snapshot pass.
     * Empty = all registered components (use with caution at scale).
     */
    TArray<TWeakObjectPtr<UISMRuntimeComponent>> TargetComponents;

    /**
     * Spatial bounds filter (world space).
     * Only cells overlapping this box are snapshotted.
     * Use FBox::BuildAABB or leave as InvertedBox to snapshot all cells.
     */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    FBox SpatialBounds = FBox(EForceInit::ForceInit);

    /** Fields to copy into each FISMInstanceSnapshot (read access). */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    EISMSnapshotField ReadMask = EISMSnapshotField::None;

    /**
     * Fields the transformer intends to write back.
     * The scheduler enforces that results only write declared fields.
     * WriteMask does not have to be a subset of ReadMask (e.g., write-only to CustomData).
     */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    EISMSnapshotField WriteMask = EISMSnapshotField::None;

    /**
     * Hard cap on instances per chunk.
     * 0 = use subsystem default (recommended).
     * If a cell exceeds this, the cell is split into sub-chunks.
     * This override exists for transformers that are known to be expensive per-instance.
     */
    UPROPERTY(BlueprintReadWrite, Category = "ISM Batch")
    int32 MaxInstancesPerChunkOverride = 0;

    /** Whether this request has a valid spatial bounds filter set. */
    bool HasSpatialBounds() const { return SpatialBounds.IsValid != 0; }
};