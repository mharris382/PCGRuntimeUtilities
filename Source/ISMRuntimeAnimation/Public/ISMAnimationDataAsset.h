#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveFloat.h"
#include "ISMAnimationDataAsset.generated.h"

/**
 * The mathematical function used to drive animation displacement.
 */
UENUM(BlueprintType)
enum class EISMAnimationWaveform : uint8
{
    Sine,           // Smooth oscillation - good for gentle sway, bobbing
    Perlin,         // Smooth noise - good for organic, non-repeating motion
    Triangle,       // Linear back-and-forth - good for mechanical motion
    Square,         // Snap between two states - good for flickering, glitching
};

/**
 * Which axes the animation displacement is applied to.
 * Combined as flags - e.g. X | Z for horizontal sway + vertical bob.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EISMAnimationAxis : uint8
{
    None = 0,
    X = 1 << 0,
    Y = 1 << 1,
    Z = 1 << 2,
};
ENUM_CLASS_FLAGS(EISMAnimationAxis)

/**
 * Controls how per-instance variation is introduced to avoid all instances
 * moving in perfect lockstep (the "marching army" problem).
 */
    UENUM(BlueprintType)
    enum class EISMAnimationPhaseMode : uint8
{
    /** All instances use the same phase - synchronized motion. */
    Synchronized,

    /**
     * Phase offset derived from instance world position.
     * Instances in different locations get naturally offset phase.
     * Best for foliage, environmental objects.
     */
    PositionBased,

    /**
     * Phase offset derived from instance index.
     * Uniform distribution regardless of spatial layout.
     * Best for UI elements, procedural grids.
     */
    IndexBased,
};


/**
 * A single animation layer. Multiple layers can be stacked in one data asset
 * to produce complex compound motion (e.g. slow main sway + fast tip flutter).
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMEANIMATION_API FISMAnimationLayer
{
    GENERATED_BODY()

    /** Whether this layer is active. Allows toggling layers without removing them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
    bool bEnabled = true;

    /** Mathematical function driving this layer's displacement. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
    EISMAnimationWaveform Waveform = EISMAnimationWaveform::Sine;

    /** Which axes this layer displaces. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer",
        meta = (Bitmask, BitmaskEnum = "/Script/ISMRuntimeAnimation.EISMAnimationAxis"))
    int32 ActiveAxes = static_cast<int32>(EISMAnimationAxis::X) | static_cast<int32>(EISMAnimationAxis::Y);

    /**
     * Peak displacement in cm per active axis.
     * For rotation-based animation, treat this as degrees.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.0"))
    float Amplitude = 5.0f;

    /** Cycles per second. Higher = faster oscillation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.001"))
    float Frequency = 0.5f;

    /**
     * Fixed phase offset in [0, 1] range applied before per-instance variation.
     * Use to offset multiple layers relative to each other.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PhaseOffset = 0.0f;

    /** How per-instance phase variation is calculated. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
    EISMAnimationPhaseMode PhaseMode = EISMAnimationPhaseMode::PositionBased;

    /**
     * Scale of per-instance phase variation.
     * 0 = all instances perfectly synchronized.
     * 1 = full variation (instances spread evenly across one full cycle).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PhaseVariation = 0.7f;

    /**
     * Whether displacement is applied as transform translation or rotation.
     * Translation: instances bob/sway in world space.
     * Rotation: instances pivot around their local origin (better for foliage).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
    bool bApplyAsRotation = false;

    /**
     * Wind direction influence weight for this layer [0, 1].
     * 0 = layer ignores wind direction entirely (omnidirectional).
     * 1 = layer is fully aligned to the animation component's wind vector.
     * Only meaningful if the owning ISMAnimationComponent has a wind vector set.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer|Wind",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WindInfluence = 0.5f;
};


/**
 * Data asset defining the complete animation configuration for an ISM animation transformer.
 * Assign to a UISMAnimationComponent to drive procedural instance animation.
 *
 * Supports multiple stacked animation layers for compound motion.
 * All parameters are readable on background threads (plain data, no UObject access).
 */
UCLASS(BlueprintType)
class ISMRUNTIMEANIMATION_API UISMAnimationDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:

    // ===== Layers =====

    /**
     * Animation layers, evaluated and summed each frame.
     * Order does not matter - all layers are additive.
     * Maximum recommended: 3-4 layers before per-instance cost becomes noticeable.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
    TArray<FISMAnimationLayer> Layers;


    // ===== Distance Falloff =====

    /**
     * Maximum distance from the reference location (usually player camera) at which
     * instances are animated. Instances beyond this distance are skipped entirely.
     * -1 = no distance limit (animate all instances regardless of distance).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "-1.0"))
    float MaxAnimationDistance = 5000.0f;

    /**
     * Distance at which animation begins fading out, as a fraction of MaxAnimationDistance.
     * e.g. 0.7 means falloff begins at 70% of max distance and reaches zero at 100%.
     * Only used if MaxAnimationDistance > 0.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FalloffStartFraction = 0.7f;

    /**
     * Curve controlling the falloff shape from FalloffStartFraction to MaxAnimationDistance.
     * X axis: 0 = falloff start, 1 = max distance.
     * Y axis: 0 = no animation, 1 = full animation.
     * If not set, linear falloff is used.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance")
    UCurveFloat* FalloffCurve = nullptr;


    // ===== Update Rate =====

    /**
     * How many times per second this animation updates.
     * 0 = every frame (default, smoothest).
     * Lower values save CPU at the cost of animation smoothness.
     * Recommended minimum for visible foliage: 15 Hz.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0.0"))
    float UpdateRateHz = 0.0f;

    // ===== Update Rate =====


    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Random")
	int32 RandomSeed = 12345;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Random", meta=(UIMin="0.0", UIMax="1.0"))
	FVector2D RandomRange = FVector2D(0.0f, 1.0f);

    // ===== Helpers =====

	float GetRandomRangeMin() const { return RandomRange.X; }
	float GetRandomRangeMax() const { return RandomRange.Y; }

    /** Whether any layers are enabled. */
    bool HasEnabledLayers() const
    {
        for (const FISMAnimationLayer& Layer : Layers)
        {
            if (Layer.bEnabled) return true;
        }
        return false;
    }

    /**
     * Evaluate the combined amplitude falloff scalar for a given distance.
     * Returns 1.0 within the inner range, 0.0 beyond MaxAnimationDistance,
     * and a curve-interpolated value in between.
     * Safe to call on any thread.
     */
    float EvaluateFalloff(float Distance) const;
};