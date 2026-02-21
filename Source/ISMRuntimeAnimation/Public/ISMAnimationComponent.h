#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ISMAnimationTransformer.h"
#include "ISMAnimationComponent.generated.h"

// Forward declarations
class UISMAnimationDataAsset;
class UISMRuntimeComponent;
class UInstancedStaticMeshComponent;
class UISMBatchScheduler;
class AActor;

/**
 * Actor component that drives procedural per-instance animation on an ISM component
 * via the ISMRuntimeCore batch mutation pipeline.
 *
 * Setup (in editor):
 *   1. Add UISMAnimationComponent to any actor that has an ISM component
 *   2. Set TargetISM to the ISM component you want to animate
 *   3. Set AnimationData to a configured UISMAnimationDataAsset
 *   4. Optionally set WindDirection and WindStrength for directional effects
 *
 * Runtime behavior:
 *   - On BeginPlay, calls UISMRuntimeSubsystem::RequestRuntimeComponent(TargetISM)
 *   - When the runtime component is available (immediately if already registered,
 *     deferred if ISMRuntimeActor hasn't run BeginPlay yet), creates and registers
 *     an FISMAnimationTransformer with the batch scheduler
 *   - Each tick, updates frame parameters (time, delta, camera, wind) on the transformer
 *   - On EndPlay, unregisters the transformer and releases it
 *
 * Multiple UISMAnimationComponents can target different ISM components on the same actor,
 * or different actors entirely. Each gets its own transformer and scheduler registration.
 *
 * Wind:
 *   Wind direction and strength can be set directly on this component for simple cases,
 *   or driven externally each frame via SetWindParams() for dynamic weather systems.
 */
UCLASS(Blueprintable, ClassGroup = (ISMRuntime), meta = (BlueprintSpawnableComponent))
class ISMRUNTIMEANIMATION_API UISMAnimationComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    UISMAnimationComponent();

    // ===== Configuration =====

    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Animation")
    //TObjectPtr<AActor> TargetActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Animation")
    FName AnimationName = NAME_None;
    /**
     * The ISM component to animate.
     * The subsystem will provide the corresponding UISMRuntimeComponent automatically.
     * Can be on this actor or any other actor in the world.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Animation")
    UInstancedStaticMeshComponent* TargetISM = nullptr;

    /**
     * Animation configuration data asset.
     * Defines layers, amplitudes, frequencies, falloff, and update rate.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Animation")
    UISMAnimationDataAsset* AnimationData = nullptr;

    /**
     * Normalized wind direction in world space.
     * Combined with each layer's WindInfluence to bias displacement.
     * Set via SetWindParams() for dynamic control, or leave as static config.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Animation|Wind")
    FVector WindDirection = FVector(1.0f, 0.0f, 0.0f);

    /**
     * Wind strength scalar. Multiplies the amplitude of wind-influenced layers.
     * 0 = no wind effect. 1 = full configured amplitude. Values > 1 amplify beyond asset config.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Animation|Wind",
        meta = (ClampMin = "0.0"))
    float WindStrength = 1.0f;

    /**
     * If true, use the player camera location as the reference point for distance falloff.
     * If false, use this component's owner's location instead.
     * Camera-relative is correct for most foliage use cases.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Animation")
    bool bUseCameraAsReferenceLocation = true;
     
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Animation", meta = (EditCondition = "!bUseCameraAsReferenceLocation", EditConditionHides))
    AActor* ReferenceLocationActor; 

    // ===== Lifecycle =====

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;


    // ===== Runtime Control =====

    /**
     * Update wind parameters at runtime.
     * Safe to call every frame from a wind system or weather manager.
     * Takes effect on the next scheduler dispatch.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Animation|Wind")
    void SetWindParams(FVector InWindDirection, float InWindStrength);

    /**
     * Pause or resume animation. When paused, the transformer's IsDirty returns false
     * and no snapshot/apply work is done. Instances hold their last animated position.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Animation")
    void SetAnimationPaused(bool bPaused);

    /** Whether animation is currently paused. */
    UFUNCTION(BlueprintPure, Category = "ISM Animation")
    bool IsAnimationPaused() const { return bAnimationPaused; }

    /**
     * Whether the transformer has been successfully created and registered.
     * False until RequestRuntimeComponent callback fires.
     */
    UFUNCTION(BlueprintPure, Category = "ISM Animation")
    bool IsAnimationActive() const { return Transformer.IsValid(); }

    // ===== Debug =====

    /** Number of instances animated in the last completed cycle. */
    UFUNCTION(BlueprintPure, Category = "ISM Animation|Debug")
    int32 GetLastAnimatedInstanceCount() const;

    /** Number of instances skipped (out of range) in the last completed cycle. */
    UFUNCTION(BlueprintPure, Category = "ISM Animation|Debug")
    int32 GetLastSkippedInstanceCount() const;


private:

    // ===== Internal =====

    /**
     * Called when the runtime component is available (either immediately or deferred).
     * Creates the transformer and registers it with the scheduler.
     */
    void OnRuntimeComponentReady(UISMRuntimeComponent* RuntimeComponent);

    /** Build the frame params struct from current component state. */
    FISMAnimationFrameParams BuildFrameParams(float DeltaTime) const;

    /** Get the reference location for distance falloff (camera or owner). */
    FVector GetReferenceLocation() const;


    // ===== State =====

    /** The transformer owned by this component. Null until OnRuntimeComponentReady fires. */
    TSharedPtr<FISMAnimationTransformer> Transformer;

    /**
     * Cached scheduler reference. Grabbed from subsystem on BeginPlay.
     * Stored to avoid subsystem lookup on every tick.
     */
    TWeakObjectPtr<UISMBatchScheduler> CachedScheduler;

    /** Whether animation is paused (transformer IsDirty always returns false). */
    bool bAnimationPaused = false;

    /**
     * Whether we are waiting for the runtime component callback.
     * Used to guard against duplicate registration if BeginPlay is called twice.
     */
    bool bWaitingForRuntimeComponent = false;
};