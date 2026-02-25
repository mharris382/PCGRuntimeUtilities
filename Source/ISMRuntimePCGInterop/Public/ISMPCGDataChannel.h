// ISMPCGDataChannel.h
// ISMRuntimePCGInterop Module
//
// Defines the data structures that cross the boundary between ISM Runtime and PCG.
// This is the module's foundational type — every other class is typed against these structs.
//
// Design principles:
//   - Packets are value types: copyable, ownable, safe to pass across async boundaries
//   - No raw pointers in the serializable payload; weak pointers for optional back-references
//   - Handles are preserved through the pipeline so results can be matched back to sources
//   - Payload maps use FName keys so PCG attribute names survive translation without loss
//
// Data flow:
//   ISM Runtime  →  FISMPCGInstancePoint  →  PCG graph  →  FISMPCGInstancePoint  →  ISM Runtime
//   (via ISMInput node or Bridge)                           (via ISMOutput node or Bridge)

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceHandle.h"
#include "ISMInstanceState.h"
#include "ISMPCGDataChannel.generated.h"

// Forward declarations
class UISMRuntimeComponent;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Describes the intended lifetime of a data packet and how its results should be applied.
 *
 * Persistent:    Results applied once and never overwritten by this source again.
 *                Used for precompute / load-time initialization patterns.
 *                Example: PCG spatial classifier bakes tags into instances at BeginPlay.
 *
 * Ephemeral:     Results applied then discarded. Next execution starts from fresh ISM state.
 *                Used for most runtime reactive patterns.
 *                Example: Periodic proximity check updates custom data each interval.
 *
 * Accumulating:  Results applied additively or as modifications to current state.
 *                Used for systems that layer changes over time.
 *                Example: Damage accumulation, heat map marking, territory spread.
 */
UENUM(BlueprintType)
enum class EISMPCGDataLifetime : uint8
{
    Persistent,
    Ephemeral,
    Accumulating,
};

/**
 * Determines behavior when a packet's source handle no longer resolves to a live instance.
 * Handles become stale when instances are destroyed or converted between packet capture and apply.
 */
UENUM(BlueprintType)
enum class EISMPCGStaleHandlePolicy : uint8
{
    /** Skip the point silently. Default — safe for all runtime use. */
    Skip,

    /** Skip and log a warning. Useful during development. */
    Warn,

    /** Ensure-fail in debug builds. Use only in controlled precompute contexts. */
    Assert,
};

/**
 * Controls which ISM fields the output node / bridge is permitted to write.
 * Prevents PCG graphs from accidentally clobbering fields they didn't intend to touch.
 *
 * These are flags — combine with | for multi-field writes.
 * Example: Write tags and custom data but never touch transforms.
 */
UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EISMPCGWriteMask : uint8
{
    None        = 0,
    Transforms  = 1 << 0,    // Update instance world transform from packet
    StateFlagsW = 1 << 1,    // Write EISMInstanceState bit flags  (W suffix avoids name collision)
    Tags        = 1 << 2,    // Add/remove gameplay tags
    CustomData  = 1 << 3,    // Write per-instance custom data slots
    All         = 0xFF,
};
ENUM_CLASS_FLAGS(EISMPCGWriteMask)

// ─────────────────────────────────────────────────────────────────────────────
// Core Point Type
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Represents a single ISM instance as a PCG-compatible data point.
 *
 * This struct is the unit of currency flowing through the translation layer.
 * It carries enough information to:
 *   - Identify the source instance (SourceHandle, SourceComponentId)
 *   - Reconstruct or update the instance's world state (Transform, StateFlags, Tags)
 *   - Drive material/shader parameters (CustomDataSlots)
 *   - Pass arbitrary data through PCG graphs without loss (FloatPayload, IntPayload, VectorPayload)
 *
 * When flowing ISM → PCG:
 *   Fields are populated from a live UISMRuntimeComponent instance.
 *   SourceHandle is always valid.
 *
 * When flowing PCG → ISM:
 *   SourceHandle must be valid for UpdateExisting write mode.
 *   SourceHandle may be invalid for SpawnNew write mode (handle assigned after spawn).
 *   PCG graph nodes may have added, removed, or modified any field.
 *
 * Payload maps (FloatPayload, IntPayload, VectorPayload):
 *   Keys are PCG attribute names. Values survive round-trips through PCG graphs unchanged
 *   unless a graph node explicitly modifies them. This allows arbitrary data to be threaded
 *   through a graph without custom PCG attribute accessors.
 *
 *   CustomDataSlots is separate because slot indices matter for GPU upload order.
 *   Named payloads are for logic inside PCG graphs; slots are for the renderer.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMEPCGINTEROP_API FISMPCGInstancePoint
{
    GENERATED_BODY()

    // ── Identity ──────────────────────────────────────────────────────────────

    /**
     * Handle to the source ISM instance.
     * Valid when this point was read from a live UISMRuntimeComponent.
     * May be invalid for points generated fresh inside a PCG graph.
     * Never assume validity — always check IsValid() before dereferencing.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Identity")
    FISMInstanceHandle SourceHandle;

    /**
     * Stable ID for the source component, usable even if the component pointer
     * becomes invalid. Populated from UISMRuntimeComponent's registered subsystem ID.
     * Used for routing results back to the correct component after async dispatch.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Identity")
    int32 SourceComponentId = INDEX_NONE;

    /**
     * Sequence index within the originating packet.
     * Preserved through PCG graph execution to allow order-dependent operations.
     * Not meaningful for points spawned inside a PCG graph (will be INDEX_NONE).
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Identity")
    int32 PacketSequenceIndex = INDEX_NONE;

    // ── World State ───────────────────────────────────────────────────────────

    /**
     * World transform of this instance.
     * When reading from ISM: the current transform from the ISM component.
     * When writing to ISM: applied only if EISMPCGWriteMask::Transforms is set.
     * PCG graph nodes may modify this freely (it's just an FTransform).
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|State")
    FTransform Transform;

    /**
     * State flags from FISMInstanceState at the time of capture.
     * Encoded as uint8 matching EISMInstanceState bit layout.
     * PCG graphs can read/write this as an integer attribute.
     * Applied back to ISM only if EISMPCGWriteMask::StateFlagsW is set.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|State")
    uint8 StateFlags = 0;

    /**
     * Gameplay tags on this instance at time of capture (component tags + per-instance tags).
     * Applied back to ISM only if EISMPCGWriteMask::Tags is set.
     *
     * Note on accumulation: when applying back, the bridge does NOT clear existing tags
     * before writing unless EISMPCGDataLifetime::Ephemeral is set with bClearTagsBeforeWrite.
     * Default behavior is additive.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|State")
    FGameplayTagContainer Tags;

    // ── Renderer Data ─────────────────────────────────────────────────────────

    /**
     * Per-instance custom data slots (maps to UInstancedStaticMeshComponent custom data).
     * Array index corresponds directly to custom data slot index.
     * Applied back to ISM only if EISMPCGWriteMask::CustomData is set.
     *
     * Kept as an indexed array (not a map) because slot order matters for GPU upload
     * and for consistency with ISM's own SetCustomDataValue(index, value) API.
     * An empty array means "no custom data" — not "zero all slots".
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Renderer")
    TArray<float> CustomDataSlots;

    // ── PCG Attribute Payload ─────────────────────────────────────────────────

    /**
     * Named float payload — maps to PCG float attributes by name.
     * Use for data that lives in PCG graph logic space, not the renderer.
     *
     * Examples:
     *   "Density"      — local instance density computed by PCG
     *   "DistToPlayer" — distance to player at time of capture
     *   "Weight"       — sampling weight for downstream PCG nodes
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Payload")
    TMap<FName, float> FloatPayload;

    /**
     * Named int payload — maps to PCG int32 attributes by name.
     *
     * Examples:
     *   "ClusterID"    — which spatial cluster this instance belongs to
     *   "LODGroup"     — LOD bucket index assigned by PCG spatial sampling
     *   "RegionIndex"  — streaming region index
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Payload")
    TMap<FName, int32> IntPayload;

    /**
     * Named vector payload — maps to PCG vector attributes by name.
     *
     * Examples:
     *   "NearestNeighborOffset" — offset to nearest same-type instance
     *   "SlopeNormal"           — terrain normal at instance position
     *   "WindDirection"         — local wind vector for animation
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Payload")
    TMap<FName, FVector> VectorPayload;

    // ── Helpers ───────────────────────────────────────────────────────────────

    /** True if this point has a valid back-reference to a live ISM instance. */
    bool HasValidSourceHandle() const { return SourceHandle.IsValid(); }

    /** True if this point has any payload data (useful for filtering no-op results). */
    bool HasPayload() const
    {
        return CustomDataSlots.Num() > 0
            || FloatPayload.Num() > 0
            || IntPayload.Num() > 0
            || VectorPayload.Num() > 0;
    }

    /** Get a float payload value by name, with fallback. */
    float GetFloat(FName Key, float Default = 0.0f) const
    {
        const float* Val = FloatPayload.Find(Key);
        return Val ? *Val : Default;
    }

    /** Get an int payload value by name, with fallback. */
    int32 GetInt(FName Key, int32 Default = 0) const
    {
        const int32* Val = IntPayload.Find(Key);
        return Val ? *Val : Default;
    }

    /** Get a vector payload value by name, with fallback. */
    FVector GetVector(FName Key, FVector Default = FVector::ZeroVector) const
    {
        const FVector* Val = VectorPayload.Find(Key);
        return Val ? *Val : Default;
    }

    /** Get custom data slot value by index, returns 0.0 if slot doesn't exist. */
    float GetCustomDataSlot(int32 SlotIndex) const
    {
        return CustomDataSlots.IsValidIndex(SlotIndex) ? CustomDataSlots[SlotIndex] : 0.0f;
    }

    /** Set custom data slot, expanding array if necessary. */
    void SetCustomDataSlot(int32 SlotIndex, float Value)
    {
        if (SlotIndex >= CustomDataSlots.Num())
        {
            CustomDataSlots.SetNumZeroed(SlotIndex + 1);
        }
        CustomDataSlots[SlotIndex] = Value;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Packet Type
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A batch of FISMPCGInstancePoints with metadata about the capture context.
 *
 * The packet is the unit of dispatch — the bridge, PCG nodes, and the component
 * all operate on packets rather than individual points. This allows:
 *   - Efficient bulk operations on the ISM side (batch transforms, batch custom data)
 *   - Clear ownership semantics across async boundaries (packet is copyable/moveable)
 *   - Staleness detection via CaptureTime and MaxAgeSeconds
 *   - Routing to the correct component after async dispatch via SourceComponentId
 *
 * Packets are intentionally not UObjects — they're plain structs that can be
 * created on the stack, stored in TArrays, and passed through lambdas without
 * GC involvement.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMEPCGINTEROP_API FISMPCGDataPacket
{
    GENERATED_BODY()

    // ── Points ────────────────────────────────────────────────────────────────

    /** All instance points in this batch. */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    TArray<FISMPCGInstancePoint> Points;

    // ── Source Metadata ───────────────────────────────────────────────────────

    /**
     * Weak reference to the component this packet was captured from.
     * May be null for packets assembled from multiple components or injected externally.
     * Check IsValid() before use — component may have been destroyed during async dispatch.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    TWeakObjectPtr<UISMRuntimeComponent> SourceComponent;

    /**
     * Stable ID of the source component (mirrors UISMRuntimeComponent's subsystem registration ID).
     * Valid even after SourceComponent pointer becomes stale.
     * Used to re-resolve the component via the subsystem after async dispatch.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    int32 SourceComponentId = INDEX_NONE;

    /**
     * Identifies what kind of data this packet carries.
     * Used for routing when multiple PCG graphs feed into the same apply step,
     * and for filtering at the ISMOutput node (e.g. only process "Physics.Impact" packets).
     *
     * Convention: use hierarchical tags matching your module's domain.
     * Examples: "ISMPCGChannel.Physics.Impact", "ISMPCGChannel.Animation.Density"
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    FGameplayTag ChannelTag;

    // ── Lifetime and Staleness ────────────────────────────────────────────────

    /**
     * World time (seconds) when this packet was captured from ISM state.
     * Used with MaxAgeSeconds to detect stale packets in async workflows.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    double CaptureTimeSeconds = 0.0;

    /**
     * If > 0, packets older than this value (seconds) are discarded without applying.
     * Set to 0 to disable staleness checking (always apply).
     * Typical values: 0.1–0.5s for fast reactive systems, 0 for precompute.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    float MaxAgeSeconds = 0.0f;

    /**
     * Intended lifetime of this packet's results.
     * Consumed by the bridge / batch transformer to determine apply behavior.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    EISMPCGDataLifetime DataLifetime = EISMPCGDataLifetime::Ephemeral;

    /**
     * What to do when a point's SourceHandle no longer resolves to a live instance.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    EISMPCGStaleHandlePolicy StaleHandlePolicy = EISMPCGStaleHandlePolicy::Skip;

    /**
     * Which ISM fields this packet is allowed to write when applied.
     * Acts as a safety mask — prevents PCG graphs from accidentally writing
     * fields the caller didn't intend to modify.
     *
     * Default: write state flags, tags, and custom data. Never write transforms.
     * Callers must explicitly opt in to transform writes.
     */
    UPROPERTY(BlueprintReadWrite, Category = "PCG|ISM|Packet")
    EISMPCGWriteMask WriteMask = EISMPCGWriteMask::StateFlagsW
                               | EISMPCGWriteMask::Tags
                               | EISMPCGWriteMask::CustomData;

    // ── Helpers ───────────────────────────────────────────────────────────────

    int32 Num() const { return Points.Num(); }
    bool IsEmpty() const { return Points.IsEmpty(); }

    /** True if this packet is still fresh enough to apply (respects MaxAgeSeconds). */
    bool IsFresh(double CurrentWorldTimeSeconds) const
    {
        if (MaxAgeSeconds <= 0.0f) return true;
        return (CurrentWorldTimeSeconds - CaptureTimeSeconds) <= MaxAgeSeconds;
    }

    /** True if this packet has a write mask flag set. */
    bool CanWrite(EISMPCGWriteMask Flag) const
    {
        return EnumHasAnyFlags(WriteMask, Flag);
    }

    /** Find a point by its source handle. Returns nullptr if not found. */
    const FISMPCGInstancePoint* FindByHandle(const FISMInstanceHandle& Handle) const
    {
        return Points.FindByPredicate([&Handle](const FISMPCGInstancePoint& P)
        {
            return P.SourceHandle == Handle;
        });
    }

    /** Find a point by its source instance index within a specific component. */
    const FISMPCGInstancePoint* FindByInstanceIndex(int32 InstanceIndex) const
    {
        return Points.FindByPredicate([InstanceIndex](const FISMPCGInstancePoint& P)
        {
            return P.SourceHandle.InstanceIndex == InstanceIndex;
        });
    }

    /** Reserve capacity for a known number of points (avoid reallocations during capture). */
    void Reserve(int32 Count) { Points.Reserve(Count); }

    /** Append another packet's points into this one. ChannelTag and metadata from this packet are preserved. */
    void AppendFrom(const FISMPCGDataPacket& Other)
    {
        Points.Append(Other.Points);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Attribute Name Constants
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Standard PCG attribute names used by the ISM interop layer.
 *
 * PCG graphs that want to read or write ISM fields by name should use these constants.
 * Custom attribute names are also valid — these are just the well-known names
 * that the ISMInput/ISMOutput nodes and the bridge recognize natively.
 */
namespace ISMPCGAttributes
{
    // Identity
    static const FName InstanceIndex    = TEXT("ISM.InstanceIndex");
    static const FName ComponentId      = TEXT("ISM.ComponentId");
    static const FName SequenceIndex    = TEXT("ISM.SequenceIndex");

    // State
    static const FName StateFlags       = TEXT("ISM.StateFlags");
    static const FName IsActive         = TEXT("ISM.IsActive");
    static const FName IsDestroyed      = TEXT("ISM.IsDestroyed");
    static const FName IsConverted      = TEXT("ISM.IsConverted");

    // Renderer
    static const FName CustomData0      = TEXT("ISM.CustomData.0");
    static const FName CustomData1      = TEXT("ISM.CustomData.1");
    static const FName CustomData2      = TEXT("ISM.CustomData.2");
    static const FName CustomData3      = TEXT("ISM.CustomData.3");
    // Additional slots accessed programmatically: FString::Printf(TEXT("ISM.CustomData.%d"), SlotIndex)

    // Spatial helpers (written by ISMInput, useful for PCG graph logic)
    static const FName DistanceToOrigin = TEXT("ISM.DistanceToOrigin");
    static const FName LocalDensity     = TEXT("ISM.LocalDensity");     // Populated if ISMInput has density enabled
}
