#pragma once

#include "CoreMinimal.h"
#include "Batching/ISMBatchTransformer.h"
#include "ISMAnimationDataAsset.h"

// Forward declarations
class UISMRuntimeComponent;
class UISMAnimationDataAsset;
struct FRandomStream;

// Declares a log category symbol other .cpp files can reference
DECLARE_LOG_CATEGORY_EXTERN(LogISMRuntimeAnimation, Log, All);

/**
 * Per-instance animation state cached between frames.
 * Stored sparsely - only instances within animation range have an entry.
 * All fields are plain data, safe to read on background threads via snapshot.
 */
struct FISMAnimationInstanceState
{
    /** Accumulated phase per layer, carried between frames for continuity. */
    TArray<float> LayerPhases;

    /** Current falloff scalar [0,1] based on distance from reference location. */
    float FalloffScale = 1.0f;

    /** Whether this instance is currently within animation range. */
    bool bInRange = false;
};


/**
 * Encapsulates the parameters the transformer needs each tick that aren't
 * in the snapshot - world time, wind direction, reference location.
 * Written on the game thread by UISMAnimationComponent, read on the task thread
 * by ProcessChunk. Fields are atomic-safe plain types so no locking is needed.
 *
 * Written once per frame before the scheduler dispatches chunks.
 */
struct FISMAnimationFrameParams
{
    /** Elapsed world time in seconds. Used to advance animation phases. */
    float WorldTime = 0.0f;

    /** Delta time since last animation update. Used to advance phases smoothly. */
    float DeltaTime = 0.0f;

    /**
     * Reference location for distance-based culling and falloff.
     * Typically the player camera world position.
     */
    FVector ReferenceLocation = FVector::ZeroVector;

    /**
     * Normalized wind direction vector in world space.
     * Combined with each layer's WindInfluence to bias displacement direction.
     * Zero vector = no wind bias (omnidirectional animation).
     */
    FVector WindDirection = FVector::ZeroVector;

    /** Wind strength scalar, multiplied against layer amplitude for wind-influenced layers. */
    float WindStrength = 1.0f;
};

struct FISMInstanceCaptureData
{
    FTransform OriginalTransform;
	float Rand = 0.0f;
};

/**
 * Batch transformer that drives procedural per-instance animation on ISM components.
 *
 * Registered with UISMBatchScheduler by UISMAnimationComponent on BeginPlay.
 * Each scheduler tick:
 *   1. BuildRequest declares transform read + write masks and a distance-bounded spatial region
 *   2. Scheduler emits one chunk per spatial cell within range
 *   3. ProcessChunk evaluates animation layers per instance (background thread safe)
 *   4. Results applied to the ISMRuntimeComponent by the scheduler on the game thread
 *
 * Threading:
 *   - FrameParams is written on the game thread before dispatch (via UpdateFrameParams)
 *   - ProcessChunk reads FrameParams and AnimData by value/const ref - no locking needed
 *   - PerInstanceState is only read/written from ProcessChunk - chunks for the same
 *     component run sequentially in Phase 1, so no contention
 *
 * Lifetime:
 *   Owned by UISMAnimationComponent as a TSharedPtr.
 *   Unregistered from the scheduler before destruction.
 */
class ISMRUNTIMEANIMATION_API FISMAnimationTransformer : public IISMBatchTransformer
{
public:

    FISMAnimationTransformer(
        UISMRuntimeComponent* InTargetComponent,
        UISMAnimationDataAsset* InAnimData,
        FName InTransformerName);

    virtual ~FISMAnimationTransformer() override;


    // ===== IISMBatchTransformer =====

    virtual FName GetTransformerName() const override { return TransformerName; }


    void SetDirty();
    /**
     * Returns true every frame if the data asset has enabled layers and UpdateRate allows it.
     * Handles UpdateRateHz throttling internally via accumulated time.
     */
    virtual bool IsDirty() const override;

    /** Clears the dirty flag and resets the update rate accumulator if throttled. */
    virtual void ClearDirty() override;

    /**
     * Declares transform read + write masks.
     * Spatial bounds: sphere around FrameParams.ReferenceLocation with radius
     * equal to AnimData->MaxAnimationDistance (or unbounded if -1).
     */
    virtual FISMSnapshotRequest BuildRequest() override;

    /**
     * Evaluates all enabled animation layers for each instance in the chunk.
     * Applies distance falloff. Produces a transform mutation per animated instance.
     * Background thread safe - reads only snapshot data, FrameParams copy, and AnimData.
     *
     * Instances beyond MaxAnimationDistance are skipped (no mutation = no change).
     * Handle is always released (never abandoned) so OnRequestComplete fires reliably.
     */
    virtual void ProcessChunk(FISMBatchSnapshot Chunk, FISMMutationHandle Handle) override;

    /** Notifies the owning component that all chunks for this cycle have been applied. */
    virtual void OnRequestComplete() override;



    /** Called when the handle is released OR abandoned.
     *  Override to reset per-cycle state. Default is no-op. */
    virtual void OnHandleReleased() override;

    /** Called when the handle is issued, with the initial set of snapshots.
 *  Override to initialize per-cycle state from snapshot data (e.g. cache original transforms). */
    virtual void OnHandleIssued(const TArray<FISMBatchSnapshot>& Snapshots) override;

    /** Called if the snapshot set changes during the handle's lifetime (Phase 2: spatial chunks
     *  added/removed as cells enter/leave range). Default is no-op.
     *  Snapshots contains the FULL current set, not just the delta. */
    virtual void OnHandleChunksChanged(const TArray<FISMBatchSnapshot>& Snapshots) override;

    // ===== Frame Parameter Update =====

    /**
     * Called by UISMAnimationComponent each tick BEFORE the scheduler dispatches.
     * Safe to call on the game thread only.
     * Provides current time, delta, camera position, and wind state to the transformer.
     */
    void UpdateFrameParams(const FISMAnimationFrameParams& Params);


    // ===== Debug / Stats =====

    /** Number of instances animated in the most recently completed cycle. */
    int32 GetLastAnimatedInstanceCount() const { return LastAnimatedInstanceCount; }

    /** Number of instances skipped (out of range) in the most recently completed cycle. */
    int32 GetLastSkippedInstanceCount() const { return LastSkippedInstanceCount; }


private:

    void ResetCache();
    // ===== Animation Evaluation =====

    /**
     * Evaluate all enabled layers for a single instance and return the combined
     * delta transform to add to its base transform.
     * Called per-instance inside ProcessChunk - must be thread safe and allocation-free.
     *
     * @param BaseTransform     The instance's current world transform (from snapshot)
     * @param InstanceIndex     Instance index - used for index-based phase variation
     * @param FalloffScale      Distance falloff multiplier [0,1]
     * @param InFrameParams     Copy of frame params (captured at chunk dispatch time)
     */
    FTransform EvaluateLayers(
        const FTransform& BaseTransform,
        int32 InstanceIndex,
        float FalloffScale,
        const FISMAnimationFrameParams& InFrameParams) const;

    /**
     * Evaluate a single animation layer for one instance.
     * Returns the displacement vector or rotation delta contributed by this layer.
     *
     * @param Layer             The layer to evaluate
     * @param BaseTransform     Instance world transform
     * @param InstanceIndex     For index-based phase variation
     * @param InFrameParams     Frame context (time, wind, etc.)
     * @param OutTranslation    Additive world-space translation from this layer
     * @param OutRotation       Additive local-space rotation from this layer
     */
    void EvaluateLayer(
        const FISMAnimationLayer& Layer,
        const FTransform& BaseTransform,
        int32 InstanceIndex,
        const FISMAnimationFrameParams& InFrameParams,
        FVector& OutTranslation,
        FRotator& OutRotation) const;

    /**
     * Sample the configured waveform at a given phase [0, 1].
     * Returns a value in [-1, 1].
     */
    static float SampleWaveform(EISMAnimationWaveform Waveform, float Phase);

    /**
     * Compute the per-instance phase offset based on the layer's PhaseMode.
     * Result is in [0, 1] and is added to the base phase before waveform sampling.
     */
    static float ComputeInstancePhaseOffset(
        const FISMAnimationLayer& Layer,
        const FTransform& InstanceTransform,
        int32 InstanceIndex);


    // ===== State =====

    /** The component this transformer writes to. */
    TWeakObjectPtr<UISMRuntimeComponent> TargetComponent;

    /** Animation configuration. Const after construction - safe to read on any thread. */
    TWeakObjectPtr<UISMAnimationDataAsset> AnimData;

    /** Stable name for scheduler registration. */
    FName TransformerName;

    /**
     * Current frame parameters. Written on game thread via UpdateFrameParams(),
     * captured by value into ProcessChunk tasks.
     * Plain struct - no locking needed for single-writer single-reader pattern.
     */
    FISMAnimationFrameParams FrameParams;

    // ===== Dirty / Update Rate =====

    /** Time accumulated since the last animation update. Used for UpdateRateHz throttling. */
    float TimeSinceLastUpdate = 0.0f;

    /** Whether the transformer has work ready this tick. */
    bool bDirtyFlag = false;

    // ===== Stats (written from game thread after each cycle) =====

    int32 LastAnimatedInstanceCount = 0;
    int32 LastSkippedInstanceCount = 0;

    /** Accumulators updated during ProcessChunk, committed to Last* on OnRequestComplete. */
    int32 CycleAnimatedCount = 0;
    int32 CycleSkippedCount = 0;


	FISMInstanceCaptureData CaptureInstanceData(FISMInstanceSnapshot InstanceSnapshot) const;
    
    FRandomStream RandomStream;
    TMap<int32, FISMInstanceCaptureData> OriginalData;
	bool bOriginalTransformsInitialized = false;
};