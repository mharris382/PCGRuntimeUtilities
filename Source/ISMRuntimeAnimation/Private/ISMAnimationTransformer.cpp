#include "ISMAnimationTransformer.h"
#include "ISMAnimationDataAsset.h"
#include "Math/UnrealMathUtility.h"
#include "Curves/CurveFloat.h"
#include "Logging/LogMacros.h"
#include "ISMRuntimeComponent.h"

// Defines storage for the category (exactly once per module)
DEFINE_LOG_CATEGORY(LogISMRuntimeAnimation);

// ============================================================
//  Construction / Destruction
// ============================================================

FISMAnimationTransformer::FISMAnimationTransformer(
    UISMRuntimeComponent* InTargetComponent,
    UISMAnimationDataAsset* InAnimData,
    FName InTransformerName)
    : TargetComponent(InTargetComponent)
    , AnimData(InAnimData)
    , TransformerName(InTransformerName)
    , bDirtyFlag(true)
{
}

FISMAnimationTransformer::~FISMAnimationTransformer()
{
}

// ============================================================
//  IISMBatchTransformer
// ============================================================

bool FISMAnimationTransformer::IsDirty() const
{
    if (bDirtyFlag) return true;

    // Handle UpdateRateHz throttling.
    // TimeSinceLastUpdate is advanced in UpdateFrameParams.
    // If UpdateRateHz == 0, always dirty (every frame).
    const UISMAnimationDataAsset* Data = AnimData.Get();
    if (!Data || Data->UpdateRateHz <= 0.0f) return true;

    const float RequiredInterval = 1.0f / Data->UpdateRateHz;
    return TimeSinceLastUpdate >= RequiredInterval;
}

void FISMAnimationTransformer::ClearDirty()
{
    bDirtyFlag = false;
    TimeSinceLastUpdate = 0.0f;
}

void FISMAnimationTransformer::SetDirty()
{
    bDirtyFlag = true;
}


void FISMAnimationTransformer::ResetCache()
{
    bOriginalTransformsInitialized = false;
    OriginalData.Reset();
}

FISMSnapshotRequest FISMAnimationTransformer::BuildRequest()
{
    FISMSnapshotRequest Request;

    if (TargetComponent.IsValid())
    {
        Request.TargetComponents.Add(TargetComponent);
    }
    
    // We read transforms to compute displaced positions,
    // and write transforms back with animation applied.
    Request.ReadMask = EISMSnapshotField::Transform;
    Request.WriteMask = EISMSnapshotField::Transform;

    // Spatial bounds: sphere around reference location limited to animation distance.
  // If MaxAnimationDistance < 0, leave bounds invalid = snapshot all cells.
    const UISMAnimationDataAsset* Data = AnimData.Get();
    if (Data && Data->MaxAnimationDistance > 0.0f)
    {
        const FVector Center = FrameParams.ReferenceLocation;
        const float   Radius = Data->MaxAnimationDistance;
        Request.SpatialBounds = FBox::BuildAABB(Center, FVector(Radius));
    }

    return Request;
}

void FISMAnimationTransformer::ProcessChunk(FISMBatchSnapshot Chunk, FISMMutationHandle Handle)
{
    const UISMAnimationDataAsset* Data = AnimData.Get();
    if (!Data || !Data->HasEnabledLayers())
    {
		UE_LOG(LogISMRuntimeAnimation, Warning, TEXT("Transformer %s has no valid animation data or enabled layers - abandoning handle."), *TransformerName.ToString());
        Handle.Abandon();
        return;
    }

  

    // Capture frame params by value - this copy is what makes the function thread safe.
    // The game thread may update FrameParams again before this task finishes,
    // but we work from our local snapshot so there's no race.
    const FISMAnimationFrameParams LocalParams = FrameParams;

	FISMBatchMutationResult Result;
	Result.TargetComponent = TargetComponent;
	Result.WrittenFields = EISMSnapshotField::Transform;
	Result.Mutations.Reserve(Chunk.Instances.Num());

    int32 AnimatedCount = 0;
    int32 SkippedCount = 0;

    for (const FISMInstanceSnapshot& InstSnap : Chunk.Instances)
    {
        const FISMInstanceCaptureData* data = OriginalData.Find(InstSnap.InstanceIndex);
        if (!data) continue;
		
        // Distance falloff - skip or scale based on distance from reference
        const float Distance = FVector::Dist(
            data->OriginalTransform.GetLocation(),
            LocalParams.ReferenceLocation);

        const float Falloff = Data->EvaluateFalloff(Distance);

        if (FMath::IsNearlyZero(Falloff))
        {
            SkippedCount++;
            continue;
        }

        // Evaluate all enabled layers and accumulate displacement
        const FTransform AnimatedTransform = EvaluateLayers(data->OriginalTransform, InstSnap.InstanceIndex, Falloff, LocalParams);

        FISMInstanceMutation Mutation;
        Mutation.InstanceIndex = InstSnap.InstanceIndex;
        Mutation.NewTransform = AnimatedTransform;
        Result.Mutations.Add(Mutation);
        AnimatedCount++;
    }

    // Update cycle stats atomically (Phase 2: multiple chunks may run concurrently)
	CycleAnimatedCount += AnimatedCount;
	CycleSkippedCount += SkippedCount;

    Handle.Release(MoveTemp(Result));
}

void FISMAnimationTransformer::OnRequestComplete()
{
    // Commit cycle stats to the readable Last* values on the game thread
    LastAnimatedInstanceCount = CycleAnimatedCount;
    LastSkippedInstanceCount = CycleSkippedCount;
    CycleAnimatedCount = 0;
    CycleSkippedCount = 0;
}


void FISMAnimationTransformer::OnHandleReleased()
{
    ResetCache();
}

void FISMAnimationTransformer::OnHandleIssued(const TArray<FISMBatchSnapshot>& Snapshots)
{
	if (Snapshots.Num() == 0)
    {
        UE_LOG(LogISMRuntimeAnimation, Warning, TEXT("Transformer %s issued with empty snapshot array - abandoning handle."), *TransformerName.ToString());
        return;
    }
	if (Snapshots.Num() > 1)
    {
        UE_LOG(LogISMRuntimeAnimation, Warning, TEXT("Transformer %s issued with multiple snapshots - but this is not yet implemented. Extra snapshots will be ignored."), *TransformerName.ToString());
    }
	FISMBatchSnapshot Chunk = Snapshots[0]; // We only ever request 1 chunk per handle, so this is safe.
    if (!bOriginalTransformsInitialized)
    {
        RandomStream.Initialize(AnimData->RandomSeed);
        OriginalData.Reserve(Chunk.Instances.Num());
        for (const FISMInstanceSnapshot& InstSnap : Chunk.Instances)
        {
            OriginalData.Add(InstSnap.InstanceIndex, CaptureInstanceData(InstSnap));
        }
        bOriginalTransformsInitialized = true;
    }
}

void FISMAnimationTransformer::OnHandleChunksChanged(const TArray<FISMBatchSnapshot>& Snapshots)
{
	UE_LOG(LogISMRuntimeAnimation, Warning, TEXT("Transformer %s had updated chunk during handle lifetime - but this is not yet implemented."), *TransformerName.ToString());
}

void FISMAnimationTransformer::UpdateFrameParams(const FISMAnimationFrameParams& Params)
{
    // Advance update rate accumulator before storing new params
    TimeSinceLastUpdate += Params.DeltaTime;

    // Mark dirty based on accumulated time vs required interval
    const UISMAnimationDataAsset* Data = AnimData.Get();
    if (!Data || Data->UpdateRateHz <= 0.0f)
    {
        bDirtyFlag = true;
    }
    else
    {
        const float RequiredInterval = 1.0f / Data->UpdateRateHz;
        if (TimeSinceLastUpdate >= RequiredInterval)
        {
            bDirtyFlag = true;
        }
    }

    FrameParams = Params;
}

FTransform FISMAnimationTransformer::EvaluateLayers(const FTransform& BaseTransform, int32 InstanceIndex, float FalloffScale, const FISMAnimationFrameParams& InFrameParams) const
{
    const UISMAnimationDataAsset* Data = AnimData.Get();
    if (!Data) return BaseTransform;

    FVector   TotalTranslation = FVector::ZeroVector;
    FRotator  TotalRotation = FRotator::ZeroRotator;

    for (const FISMAnimationLayer& Layer : Data->Layers)
    {
        if (!Layer.bEnabled) continue;

        FVector  LayerTranslation = FVector::ZeroVector;
        FRotator LayerRotation = FRotator::ZeroRotator;

        EvaluateLayer(Layer, BaseTransform, InstanceIndex, InFrameParams,
            LayerTranslation, LayerRotation);

        // Apply falloff to this layer's contribution
        TotalTranslation += LayerTranslation * FalloffScale;
        TotalRotation.Pitch += LayerRotation.Pitch * FalloffScale;
        TotalRotation.Yaw += LayerRotation.Yaw * FalloffScale;
        TotalRotation.Roll += LayerRotation.Roll * FalloffScale;
    }

    // Build the displaced transform
    FTransform Result = BaseTransform;

    if (TotalTranslation != FVector::ZeroVector)
    {
        Result.SetLocation(BaseTransform.GetLocation() + TotalTranslation);
    }

    if (!TotalRotation.IsNearlyZero())
    {
        // Apply rotation in local space so instances pivot around their own origin
        const FQuat AdditiveRot = TotalRotation.Quaternion();
        const FQuat CombinedRot = BaseTransform.GetRotation() * AdditiveRot;
        Result.SetRotation(CombinedRot);
    }

    return Result;
}

void FISMAnimationTransformer::EvaluateLayer(const FISMAnimationLayer& Layer, const FTransform& BaseTransform, int32 InstanceIndex, const FISMAnimationFrameParams& InFrameParams, FVector& OutTranslation, FRotator& OutRotation) const
{
    // Compute this instance's phase: base time * frequency + fixed offset + per-instance variation
    const float BasePhase = InFrameParams.WorldTime * Layer.Frequency;
    const float InstanceOffset = ComputeInstancePhaseOffset(Layer, BaseTransform, InstanceIndex);
    const float TotalPhase = BasePhase + Layer.PhaseOffset + InstanceOffset;

    // Sample the waveform - result in [-1, 1]
    const float WaveValue = SampleWaveform(Layer.Waveform, TotalPhase);

    // Scale by amplitude
    float Displacement = WaveValue * Layer.Amplitude;

    // Apply wind influence: blend displacement direction toward wind direction
    // WindInfluence 0 = displacement on all active axes equally
    // WindInfluence 1 = displacement purely along wind direction
    FVector DisplacementVector = FVector::ZeroVector;

    const bool bHasX = EnumHasAnyFlags(static_cast<EISMAnimationAxis>(Layer.ActiveAxes), EISMAnimationAxis::X);
    const bool bHasY = EnumHasAnyFlags(static_cast<EISMAnimationAxis>(Layer.ActiveAxes), EISMAnimationAxis::Y);
    const bool bHasZ = EnumHasAnyFlags(static_cast<EISMAnimationAxis>(Layer.ActiveAxes), EISMAnimationAxis::Z);

    // Base displacement spreads evenly across active axes
    FVector BaseDisplacement = FVector::ZeroVector;
    if (bHasX) BaseDisplacement.X = Displacement;
    if (bHasY) BaseDisplacement.Y = Displacement;
    if (bHasZ) BaseDisplacement.Z = Displacement;

    if (Layer.WindInfluence > 0.0f && !InFrameParams.WindDirection.IsNearlyZero())
    {
        // Wind-aligned displacement: scalar displacement projected onto wind direction,
        // scaled by wind strength
        const FVector WindDisplacement =
            InFrameParams.WindDirection *
            Displacement *
            InFrameParams.WindStrength;

        DisplacementVector = FMath::Lerp(BaseDisplacement, WindDisplacement, Layer.WindInfluence);
    }
    else
    {
        DisplacementVector = BaseDisplacement;
    }

    // Output as either translation or rotation
    if (Layer.bApplyAsRotation)
    {
        // Convert displacement magnitude to degrees of rotation
        // X displacement -> Roll, Y displacement -> Pitch, Z displacement -> Yaw
        OutRotation.Roll += DisplacementVector.X;
        OutRotation.Pitch += DisplacementVector.Y;
        OutRotation.Yaw += DisplacementVector.Z;
    }
    else
    {
        OutTranslation += DisplacementVector;
    }
}

float FISMAnimationTransformer::SampleWaveform(EISMAnimationWaveform Waveform, float Phase)
{
    // Normalize phase to [0, 1] by taking fractional part
    const float NormalizedPhase = FMath::Frac(Phase);

    switch (Waveform)
    {
    case EISMAnimationWaveform::Sine:
        // Standard sine, scaled to [-1, 1]
        return FMath::Sin(NormalizedPhase * TWO_PI);

    case EISMAnimationWaveform::Triangle:
        // Linear ramp up then down: 0->1->-1->0 over one cycle
        if (NormalizedPhase < 0.25f)
            return NormalizedPhase * 4.0f;
        else if (NormalizedPhase < 0.75f)
            return 1.0f - (NormalizedPhase - 0.25f) * 4.0f;
        else
            return -1.0f + (NormalizedPhase - 0.75f) * 4.0f;

    case EISMAnimationWaveform::Square:
        // Snap between +1 and -1 at the midpoint
        return NormalizedPhase < 0.5f ? 1.0f : -1.0f;

    case EISMAnimationWaveform::Perlin:
        // Perlin noise sampled along a 1D path through 2D noise space.
        // Using the phase as both X and Y offset creates a smooth looping-ish pattern
        // without a hard seam (though it won't perfectly loop - acceptable for foliage).
        // Result from FMath::PerlinNoise2D is in [-1, 1].
        return FMath::PerlinNoise2D(FVector2D(NormalizedPhase * 3.7f, NormalizedPhase * 1.3f));

    default:
        return 0.0f;
    }
}

float FISMAnimationTransformer::ComputeInstancePhaseOffset(const FISMAnimationLayer& Layer, const FTransform& InstanceTransform, int32 InstanceIndex)
{
    if (FMath::IsNearlyZero(Layer.PhaseVariation)) return 0.0f;

    float RawOffset = 0.0f;

    switch (Layer.PhaseMode)
    {
    case EISMAnimationPhaseMode::Synchronized:
        RawOffset = 0.0f;
        break;

    case EISMAnimationPhaseMode::PositionBased:
    {
        // Derive offset from world position so spatially close instances
        // have similar phase (natural clustering effect for foliage)
        const FVector Loc = InstanceTransform.GetLocation();
        // Scale down position to avoid very large phase values;
        // the magic divisor just controls spatial frequency of the variation pattern
        RawOffset = (Loc.X * 0.00731f + Loc.Y * 0.01137f + Loc.Z * 0.00419f);
        break;
    }

    case EISMAnimationPhaseMode::IndexBased:
        // Distribute phases evenly using golden ratio to avoid clustering
        // Golden ratio gives good distribution for any number of instances
        RawOffset = InstanceIndex * 0.618033988749f;
        break;
    }

    // Scale variation amount and wrap to [0, 1]
    return FMath::Frac(RawOffset * Layer.PhaseVariation);
}

FISMInstanceCaptureData FISMAnimationTransformer::CaptureInstanceData(FISMInstanceSnapshot InstanceSnapshot) const
{
    auto data = FISMInstanceCaptureData();
	data.OriginalTransform = InstanceSnapshot.Transform;
	data.Rand = RandomStream.FRandRange(AnimData->GetRandomRangeMin(), AnimData->GetRandomRangeMax());
    return data;
}


// ============================================================
//  UISMAnimationDataAsset helpers
// ============================================================

float UISMAnimationDataAsset::EvaluateFalloff(float Distance) const
{
    if (MaxAnimationDistance <= 0.0f)
    {
        // No distance limit - full animation regardless of distance
        return 1.0f;
    }

    if (Distance >= MaxAnimationDistance)
    {
        return 0.0f;
    }

    const float FalloffStart = MaxAnimationDistance * FalloffStartFraction;

    if (Distance <= FalloffStart)
    {
        return 1.0f;
    }

    // Remap distance within the falloff band to [0, 1]
    const float FalloffRange = MaxAnimationDistance - FalloffStart;
    const float FalloffProgress = (Distance - FalloffStart) / FalloffRange;

    if (FalloffCurve)
    {
        // Curve X: 0 = start of falloff, 1 = max distance
        // Curve Y: 1 = full animation, 0 = no animation
        return FMath::Clamp(FalloffCurve->GetFloatValue(FalloffProgress), 0.0f, 1.0f);
    }

    // Default: linear falloff from 1 to 0 over the falloff band
    return FMath::Clamp(1.0f - FalloffProgress, 0.0f, 1.0f);
}