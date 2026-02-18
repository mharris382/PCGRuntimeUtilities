// ISMRuntimeDebugger.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ISMRuntimeDebugger.generated.h"

class UISMRuntimeComponent;
class UISMRuntimeSubsystem;

/**
 * Which visual elements to draw per instance.
 * Kept as flags so you can mix and match cheaply.
 */
UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EISMDebugDrawFlags : uint8
{
    None            = 0,
    InstanceAABB    = 1 << 0,   // Per-instance world-space AABB box
    ComponentBounds = 1 << 1,   // Aggregate component bounds box
    InstanceCenter  = 1 << 2,   // Point at instance center
    InstanceIndex   = 1 << 3,   // Index label (expensive at scale, use with MaxLabelDistance)
    StateFlags      = 1 << 4,   // Color-code by active/destroyed/hidden state
};
ENUM_CLASS_FLAGS(EISMDebugDrawFlags)

/**
 * Per-component draw override. Lets you configure specific
 * components differently from the global defaults.
 */
USTRUCT(BlueprintType)
struct FISMDebugComponentOverride
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    UISMRuntimeComponent* Component = nullptr;

    /** Override the draw color for this component (ignores DataAsset DebugColor) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    FLinearColor ColorOverride = FLinearColor::White;

    /** Override draw flags for this component */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta=(Bitmask, BitmaskEnum="/Scripts/EISMRuntimeDebug.EISMDebugDrawFlags"))
    int32 FlagsOverride = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bUseColorOverride = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bUseFlagsOverride = false;
};

/**
 * Actor component that draws debug visualization for ISMRuntimeComponents.
 *
 * PERFORMANCE DESIGN:
 * - Only iterates instances once per tick, building a flat draw list
 * - Uses frustum culling to skip off-screen instances entirely
 * - MaxInstancesPerFrame budget cap prevents hitches on huge ISMs
 * - Depth-faded alpha reduces overdraw cost for dense scenes
 * - All drawing uses batched PDI line draws — no per-instance actor spawning
 *
 * USAGE:
 * Add to any actor in your level (or a dedicated debug actor).
 * Assign TargetComponents or leave empty to visualize all registered components.
 * Toggle bEnabled or call SetEnabled() at runtime.
 */
UCLASS(Blueprintable, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMEDEBUG_API UISMRuntimeDebugger : public UActorComponent
{
    GENERATED_BODY()

public:
    UISMRuntimeDebugger();

    // ===== Enable/Disable =====

    /** Master switch. Toggle without removing the component. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug")
    bool bEnabled = true;

    /** Only draw in editor (PIE + editor viewport). Automatically disabled in shipping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug")
    bool bEditorOnly = true;

    UFUNCTION(BlueprintCallable, Category = "ISM Debug")
    void SetEnabled(bool bNewEnabled) { bEnabled = bNewEnabled; }

    // ===== Target Selection =====

    /**
     * Specific components to visualize.
     * If empty, all components registered with the subsystem are drawn.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Targets")
    TArray<UISMRuntimeComponent*> TargetComponents;

    /** Per-component overrides for color and flags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Targets")
    TArray<FISMDebugComponentOverride> ComponentOverrides;

    // ===== Draw Settings =====

    /** What to draw for each instance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Draw",
        meta=(Bitmask, BitmaskEnum = "/Scripts/EISMRuntimeDebug.EISMDebugDrawFlags"))
    int32 DrawFlags = (int32)(EISMDebugDrawFlags::InstanceAABB | EISMDebugDrawFlags::ComponentBounds);

    /** Fallback color when DataAsset has no DebugColor set */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Draw")
    FLinearColor DefaultColor = FLinearColor(0.2f, 0.8f, 0.2f, 1.0f);

    /** Color used for destroyed instances when StateFlags draw mode is active */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Draw")
    FLinearColor DestroyedColor = FLinearColor(0.8f, 0.1f, 0.1f, 0.4f);

    /** Color used for hidden instances when StateFlags draw mode is active */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Draw")
    FLinearColor HiddenColor = FLinearColor(0.5f, 0.5f, 0.1f, 0.4f);

    /** Line thickness for AABB boxes */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Draw", meta=(ClampMin="0.5"))
    float LineThickness = 1.0f;

    /** Thickness for component aggregate bounds (slightly thicker to distinguish) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Draw", meta=(ClampMin="0.5"))
    float ComponentBoundsThickness = 3.0f;

    // ===== Culling & Performance =====

    /**
     * Only draw instances within this distance of the camera (cm).
     * -1 = no limit. Keep this tight on large ISMs.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Performance", meta=(ClampMin="-1.0"))
    float MaxDrawDistance = 5000.0f;

    /**
     * Hard cap on instances drawn per component per frame.
     * Prevents a single huge ISM from tanking debug performance.
     * -1 = no limit.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Performance")
    int32 MaxInstancesPerComponent = 500;

    /**
     * Index labels (InstanceIndex flag) are expensive to draw at scale.
     * They are suppressed beyond this distance regardless of MaxDrawDistance.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Performance", meta=(ClampMin="0.0"))
    float MaxLabelDistance = 1000.0f;

    /**
     * Only draw instances whose AABB is at least partially inside the camera frustum.
     * Highly recommended — disabling only makes sense for orthographic top-down debugging.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Performance")
    bool bFrustumCull = true;

    /**
     * If true, skip instances that are marked Destroyed in state flags.
     * Useful when you only care about live instances.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Debug|Performance")
    bool bSkipDestroyedInstances = false;

    // ===== Lifecycle =====

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // ===== Runtime API =====

    UFUNCTION(BlueprintCallable, Category = "ISM Debug")
    void AddTargetComponent(UISMRuntimeComponent* Component);

    UFUNCTION(BlueprintCallable, Category = "ISM Debug")
    void RemoveTargetComponent(UISMRuntimeComponent* Component);

    UFUNCTION(BlueprintCallable, Category = "ISM Debug")
    void ClearTargetComponents() { TargetComponents.Empty(); }

protected:
    // ===== Internal Draw Helpers =====

    /** Main per-frame draw loop */
    void DrawDebugForAllComponents(const FSceneView* View);

    /** Draw one component's instances */
    void DrawDebugForComponent(
        const UISMRuntimeComponent* Comp,
        const FLinearColor& Color,
        int32 ActiveFlags,
        const FConvexVolume& Frustum,
        const FVector& CameraLocation,
        UWorld* World) const;

    /** Draw a single instance AABB */
    void DrawInstanceAABB(
        const FBox& WorldBounds,
        const FLinearColor& Color,
        float Thickness,
        UWorld* World) const;

    /** Draw the aggregate component bounds */
    void DrawComponentBounds(
        const UISMRuntimeComponent* Comp,
        const FLinearColor& Color,
        UWorld* World) const;

    /** Resolve the effective color for a component */
    FLinearColor ResolveComponentColor(const UISMRuntimeComponent* Comp) const;

    /** Resolve the effective draw flags for a component */
    int32 ResolveComponentFlags(const UISMRuntimeComponent* Comp) const;

    /** Get the list of components to draw this frame */
    TArray<UISMRuntimeComponent*> GetComponentsToDraw() const;

    /** Get camera info for this frame */
    bool GetCameraInfo(FVector& OutLocation, FConvexVolume& OutFrustum) const;
};