#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Delegates/DelegateCombinations.h"
#include "ISMInstanceHandle.h"
#include "ISMCollectorComponent.generated.h"

// Forward declarations
class UISMResourceComponent;
class UCameraComponent;

/**
 * How the collector detects collectible instances
 */
UENUM(BlueprintType)
enum class ECollectionDetectionMode : uint8
{
    /** Cast ray from camera/controller viewpoint */
    Raycast,

    /** Detect instances within a radius around the collector */
    Radius,

    /** Manual targeting - game code sets the target */
    Manual
};

/**
 * How collection is triggered
 */
UENUM(BlueprintType)
enum class ECollectionMode : uint8
{
    /** Instant collection on button press */
    Instant,

    /** Hold button for duration */
    Timed,

    /** Press once to start, automatically completes after duration */
    Progressive
};

/**
 * Delegate signatures for collector events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTargetChanged,
    const FISMInstanceHandle&, NewTarget,
    UISMResourceComponent*, ResourceComponent);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCollectionStarted,
    const FISMInstanceHandle&, Instance,
    UISMResourceComponent*, ResourceComponent);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCollectionProgress,
    const FISMInstanceHandle&, Instance,
    UISMResourceComponent*, ResourceComponent,
    float, Progress);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCollectionCompleted,
    const  FISMInstanceHandle&, Instance,
    UISMResourceComponent*, ResourceComponent);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCollectionFailed,
    const struct FISMInstanceHandle&, Instance,
    UISMResourceComponent*, ResourceComponent,
    FText, FailureReason);

/**
 * Component for actors that can collect resources from ISMResourceComponents.
 *
 * Features:
 * - Multiple detection modes (raycast, radius, manual)
 * - Configurable collection behavior (instant, timed, progressive)
 * - Tag-based validation
 * - Easy Blueprint integration
 * - Virtual hooks for customization
 *
 * Usage:
 * 1. Add to player/AI character
 * 2. Set detection mode and range
 * 3. Wire input to StartInteraction()/StopInteraction()
 * 4. Bind to events for UI updates
 *
 * Philosophy:
 * This component handles targeting and initiating collection.
 * It does NOT decide what happens when collection completes - that's
 * handled by ISMResourceComponent's OnResourceCollected event.
 */
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class ISMRUNTIMERESOURCE_API UISMCollectorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UISMCollectorComponent();

    // ===== Lifecycle =====

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ===== Detection Settings =====

    /** How to detect collectible instances */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Detection")
    ECollectionDetectionMode DetectionMode = ECollectionDetectionMode::Raycast;

    /** Detection range for raycast mode */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Detection", meta = (ClampMin = "10.0", EditCondition = "DetectionMode == ECollectionDetectionMode::Raycast"))
    float RaycastRange = 300.0f;

    /** Detection radius for radius mode */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Detection", meta = (ClampMin = "10.0", EditCondition = "DetectionMode == ECollectionDetectionMode::Radius"))
    float DetectionRadius = 200.0f;

    /** Trace channel for raycast detection */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Detection", meta = (EditCondition = "DetectionMode == ECollectionDetectionMode::Raycast"))
    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

    /** Should we trace complex collision? (slower but more accurate) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Detection", meta = (EditCondition = "DetectionMode == ECollectionDetectionMode::Raycast"))
    bool bTraceComplex = false;

    /** Camera component to use for raycast origin (auto-detected if null) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Detection", meta = (EditCondition = "DetectionMode == ECollectionDetectionMode::Raycast"))
    UCameraComponent* CameraComponent;

    // ===== Collection Behavior =====

    /** How collection is triggered */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Behavior")
    ECollectionMode CollectionMode = ECollectionMode::Timed;

    /** Should detection run every frame? (disable if using manual targeting) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Behavior")
    bool bAutoDetect = true;

    /** Throttle detection to reduce performance cost (0 = every frame) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Behavior", meta = (ClampMin = "0.0", EditCondition = "bAutoDetect"))
    float DetectionInterval = 0.1f;

    // ===== Tag Filtering =====

    /** Tags describing this collector's capabilities (e.g., "Tool.Axe", "Skill.Woodcutting.Level3") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Tags")
    FGameplayTagContainer CollectorTags;

    /** Query to filter which instances this collector can target (leave empty to target all) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Tags")
    FGameplayTagQuery TargetFilter;

    /** Only detect instances that pass validation? (false = detect but may fail to collect) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection|Tags")
    bool bOnlyDetectValidTargets = true;

    // ===== Input Integration =====

    /**
     * Call this from your input event to start interaction.
     * Handles both instant and timed collection modes.
     */
    UFUNCTION(BlueprintCallable, Category = "Collection")
    void StartInteraction();

    /**
     * Call this when input is released (for Timed mode).
     * Cancels ongoing collection if bCanInterruptCollection is true.
     */
    UFUNCTION(BlueprintCallable, Category = "Collection")
    void StopInteraction();

    // ===== Targeting API =====

    /**
     * Get the currently targeted instance (for UI prompts).
     */
    UFUNCTION(BlueprintPure, Category = "Collection")
    FISMInstanceHandle GetTargetedInstance() const { return TargetedInstance; }

    /**
     * Get the resource component of the targeted instance.
     */
    UFUNCTION(BlueprintPure, Category = "Collection")
    UISMResourceComponent* GetTargetedResourceComponent() const { return TargetedResourceComponent.Get(); }

    /**
     * Check if currently targeting a valid collectible instance.
     */
    UFUNCTION(BlueprintPure, Category = "Collection")
    bool HasValidTarget() const;

    /**
     * Manually set the target instance (for Manual detection mode or click-to-collect).
     */
    UFUNCTION(BlueprintCallable, Category = "Collection")
    void SetTargetInstance(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent);

    /**
     * Clear the current target.
     */
    UFUNCTION(BlueprintCallable, Category = "Collection")
    void ClearTarget();

    // ===== Collection State =====

    /**
     * Is currently collecting something?
     */
    UFUNCTION(BlueprintPure, Category = "Collection")
    bool IsCollecting() const { return bIsCollecting; }

    /**
     * Get collection progress (0-1).
     */
    UFUNCTION(BlueprintPure, Category = "Collection")
    float GetCollectionProgress() const;

    /**
     * Force cancel current collection.
     */
    UFUNCTION(BlueprintCallable, Category = "Collection")
    void CancelCollection();

    // ===== Tag Management =====

    /**
     * Add a tag to this collector (e.g., when equipping a tool).
     * Automatically updates target detection.
     */
    UFUNCTION(BlueprintCallable, Category = "Collection|Tags")
    void AddCollectorTag(FGameplayTag Tag);

    /**
     * Remove a tag from this collector (e.g., when unequipping a tool).
     */
    UFUNCTION(BlueprintCallable, Category = "Collection|Tags")
    void RemoveCollectorTag(FGameplayTag Tag);

    /**
     * Check if collector has a specific tag.
     */
    UFUNCTION(BlueprintPure, Category = "Collection|Tags")
    bool HasCollectorTag(FGameplayTag Tag) const { return CollectorTags.HasTag(Tag); }

    /**
     * Set all collector tags at once (replaces existing tags).
     */
    UFUNCTION(BlueprintCallable, Category = "Collection|Tags")
    void SetCollectorTags(const FGameplayTagContainer& NewTags);

    // ===== Events =====

    /** Called when targeted instance changes (for UI updates) */
    UPROPERTY(BlueprintAssignable, Category = "Collection|Events")
    FOnTargetChanged OnTargetChanged;

    /** Called when collection starts */
    UPROPERTY(BlueprintAssignable, Category = "Collection|Events")
    FOnCollectionStarted OnCollectionStartedEvent;

    /** Called during collection (for progress bars) */
    UPROPERTY(BlueprintAssignable, Category = "Collection|Events")
    FOnCollectionProgress OnCollectionProgressEvent;

    /** Called when collection completes */
    UPROPERTY(BlueprintAssignable, Category = "Collection|Events")
    FOnCollectionCompleted OnCollectionCompletedEvent;

    /** Called when collection fails validation */
    UPROPERTY(BlueprintAssignable, Category = "Collection|Events")
    FOnCollectionFailed OnCollectionFailedEvent;

public:
    // ===== Virtual Hooks for Customization =====

    /**
     * Get collector tags dynamically.
     * Override to pull tags from inventory, equipped items, character stats, etc.
     *
     * Default implementation returns CollectorTags property.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection|Tags")
    FGameplayTagContainer GetCollectorTags() const;
    virtual FGameplayTagContainer GetCollectorTags_Implementation() const;

    /**
     * Can this collector interact with this resource?
     * Override to add tool checks, skill requirements, inventory full checks, etc.
     *
     * @param Instance - The instance being checked
     * @param ResourceComp - The resource component
     * @param OutFailureReason - Set this to explain why collection failed
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection")
    bool CanCollectResource(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp,
        FText& OutFailureReason) const;
    virtual bool CanCollectResource_Implementation(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp,
        FText& OutFailureReason) const;

    /**
     * Get speed multiplier for collection.
     * Override to add stat bonuses, tool efficiency, buffs, etc.
     *
     * @return Multiplier (1.0 = normal, 2.0 = twice as fast, 0.5 = half speed)
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection")
    float GetCollectionSpeedMultiplier(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp) const;
    virtual float GetCollectionSpeedMultiplier_Implementation(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp) const;

    /**
     * Should this instance be considered during detection?
     * Override to add custom filtering (e.g., only detect if player has required tool equipped).
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection")
    bool ShouldConsiderInstance(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp) const;
    virtual bool ShouldConsiderInstance_Implementation(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp) const;

    /**
     * Called when a new instance is targeted (for UI updates, highlighting, etc.).
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection")
    void OnTargetChangedInternal(const FISMInstanceHandle& NewTarget, UISMResourceComponent* ResourceComp);
    virtual void OnTargetChangedInternal_Implementation(const FISMInstanceHandle& NewTarget,
        UISMResourceComponent* ResourceComp);

    /**
     * Called when collection starts (for animations, VFX, etc.).
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection")
    void OnCollectionStartedInternal(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComp);
    virtual void OnCollectionStartedInternal_Implementation(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp);

    /**
     * Called each frame during collection (for progress VFX, sounds, etc.).
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection")
    void OnCollectionProgressInternal(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp,
        float Progress);
    virtual void OnCollectionProgressInternal_Implementation(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp,
        float Progress);

    /**
     * Called when collection completes successfully.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection")
    void OnCollectionCompletedInternal(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComp);
    virtual void OnCollectionCompletedInternal_Implementation(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp);

    /**
     * Called when collection is cancelled/interrupted.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection")
    void OnCollectionCancelledInternal(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComp);
    virtual void OnCollectionCancelledInternal_Implementation(const FISMInstanceHandle& Instance,
        UISMResourceComponent* ResourceComp);

    /**
     * Called when collector tags change.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Collection|Tags")
    void OnCollectorTagsChangedInternal(const FGameplayTagContainer& NewTags);
    virtual void OnCollectorTagsChangedInternal_Implementation(const FGameplayTagContainer& NewTags);

    // ===== Internal State =====

    /** Currently targeted instance */
    FISMInstanceHandle TargetedInstance;

    /** Component owning the targeted instance */
    TWeakObjectPtr<UISMResourceComponent> TargetedResourceComponent;

    /** Is currently collecting? */
    bool bIsCollecting = false;

    /** Is interaction button held? (for Timed mode) */
    bool bIsInteractionHeld = false;

    /** Time accumulator for detection interval */
    float DetectionTimer = 0.0f;

    /** Auto-detected camera component */
    TWeakObjectPtr<UCameraComponent> CachedCamera;

    // ===== Helper Functions =====

    /** Run detection to find nearby collectible instance */
    void RunDetection();

    /** Perform raycast detection */
    FISMInstanceHandle DetectViaRaycast();

    /** Perform radius detection */
    FISMInstanceHandle DetectViaRadius();

    /** Update the current target */
    void UpdateTarget(const FISMInstanceHandle& NewTarget, UISMResourceComponent* NewResourceComp);

    /** Attempt to start collection on current target */
    bool TryStartCollection();

    /** Get raycast start and end points */
    void GetRaycastPoints(FVector& OutStart, FVector& OutEnd) const;

    /** Find camera component (auto-detect if not set) */
    UCameraComponent* FindCameraComponent();

    /** Bind to resource component collection events */
    void BindToResourceComponent(UISMResourceComponent* ResourceComp);

    /** Unbind from resource component */
    void UnbindFromResourceComponent();

    // ===== Resource Component Event Handlers =====

    UFUNCTION()
    void HandleCollectionProgress(UISMResourceComponent* Component,
        const FISMInstanceHandle& Instance,
        AActor* Collector,
        float Progress);

    UFUNCTION()
    void HandleCollectionCompleted(UISMResourceComponent* Component,
        const struct FResourceCollectionData& CollectionData);

    UFUNCTION()
    void HandleCollectionCancelled(UISMResourceComponent* Component,
        const FISMInstanceHandle& Instance,
        AActor* Collector);
};