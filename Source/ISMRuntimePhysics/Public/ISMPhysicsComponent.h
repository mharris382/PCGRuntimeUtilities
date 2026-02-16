#pragma once

#include "CoreMinimal.h"
#include "ISMRuntimeComponent.h"
#include "ISMPhysicsComponent.generated.h"

// Forward declarations
class UISMPhysicsDataAsset;
class UISMRuntimePoolSubsystem;
class AISMPhysicsActor;

/**
 * Query mode for detecting physics conversions.
 */
UENUM(BlueprintType)
enum class EPhysicsQueryMode : uint8
{
    /** Use ISM spatial queries (fast, doesn't require ISM collision) */
    ISMSpatial      UMETA(DisplayName = "ISM Spatial Query"),
    
    /** Use physics engine queries (accurate, requires ISM collision enabled) */
    PhysicsEngine   UMETA(DisplayName = "Physics Engine Query")
};

/**
 * Behavior when max concurrent actors limit is reached.
 */
UENUM(BlueprintType)
enum class EActorOverflowBehavior : uint8
{
    /** Don't convert new impacts, keep existing actors */
    IgnoreNew       UMETA(DisplayName = "Ignore New"),
    
    /** Return oldest converted actor to make room */
    ReturnOldest    UMETA(DisplayName = "Return Oldest"),
    
    /** Return farthest actor from player/camera */
    ReturnFarthest  UMETA(DisplayName = "Return Farthest")
};

/**
 * Runtime component for managing physics-enabled ISM instances.
 * Extends UISMRuntimeComponent with physics conversion capabilities.
 * 
 * Features:
 * - Automatic conversion from ISM to physics actors on impact
 * - Dual query modes (ISM spatial vs physics engine)
 * - Performance limiters (distance, count, lifetime)
 * - Automatic return to ISM when actors settle
 * - Integration with pool subsystem for zero allocations
 * 
 * Usage:
 * 1. Attach to actor with ISM component
 * 2. Assign ISMPhysicsDataAsset
 * 3. Set ManagedISMComponent reference
 * 4. Configure performance limits as needed
 * 5. Add ISMPhysicsInstigatorComponent to player/AI
 * 
 * The component handles everything automatically - no blueprint code needed!
 */
UCLASS(Blueprintable, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMEPHYSICS_API UISMPhysicsComponent : public UISMRuntimeComponent
{
    GENERATED_BODY()

public:
    UISMPhysicsComponent();

    // ===== Lifecycle =====
    
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ===== Configuration =====
    
    /**
     * Physics configuration data asset.
     * Must be UISMPhysicsDataAsset or subclass.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
    TObjectPtr<UISMPhysicsDataAsset> PhysicsData;
    
    /**
     * Query mode for detecting impacts.
     * 
     * ISMSpatial: Fast, works without ISM collision, good for ambient effects
     * PhysicsEngine: Accurate, requires ISM collision, good for gameplay
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Query")
    EPhysicsQueryMode QueryMode = EPhysicsQueryMode::ISMSpatial;
    
    // ===== Performance Limiters =====
    
    /**
     * Enable performance limiters.
     * Disable for gameplay-critical accuracy, enable for ambient effects.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Performance")
    bool bEnableLimiters = true;
    
    /**
     * Maximum distance from player camera to simulate physics.
     * 0 = unlimited.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Performance",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="10000.0", EditCondition="bEnableLimiters", EditConditionHides,
              Tooltip="Max simulation distance from camera (cm). 0 = unlimited."))
    float MaxSimulationDistance = 5000.0f;
    
    /**
     * Maximum concurrent physics actors.
     * 0 = unlimited (not recommended).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Performance",
        meta=(ClampMin="0", UIMin="0", UIMax="200", EditCondition="bEnableLimiters", EditConditionHides,
              Tooltip="Max concurrent physics actors. 0 = unlimited (use carefully!)."))
    int32 MaxConcurrentActors = 50;
    
    /**
     * What to do when max concurrent actors reached.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Performance",
        meta=(EditCondition="bEnableLimiters && MaxConcurrentActors > 0", EditConditionHides))
    EActorOverflowBehavior ActorOverflowBehavior = EActorOverflowBehavior::ReturnFarthest;
    
    /**
     * Maximum lifetime for physics actors (seconds).
     * Auto-return after this duration even if still moving.
     * 0 = unlimited.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Performance",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="300.0", EditCondition="bEnableLimiters", EditConditionHides,
              Tooltip="Max lifetime (seconds). 0 = unlimited."))
    float MaxLifetime = 30.0f;
    
    /**
     * Check distance/lifetime limits every N frames.
     * Higher = better performance, less responsive limits.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Performance",
        meta=(ClampMin="1", UIMin="1", UIMax="60", EditCondition="bEnableLimiters", EditConditionHides,
              Tooltip="Check limits every N frames. Higher = better performance."))
    int32 LimiterCheckInterval = 30;

    // ===== Conversion Management =====
    
    /**
     * Convert an instance to a physics actor.
     * 
     * @param InstanceIndex - Instance to convert
     * @param ImpactPoint - Where the impact occurred
     * @param ImpactNormal - Direction of impact
     * @param ImpactForce - Magnitude of impact force
     * @param Instigator - Actor that caused the impact (can be null)
     * @return The spawned physics actor, or nullptr if conversion failed
     */
    UFUNCTION(BlueprintCallable, Category = "Physics")
    AActor* ConvertInstanceToPhysics(int32 InstanceIndex, FVector ImpactPoint, FVector ImpactNormal, 
        float ImpactForce, AActor* Instigator = nullptr);
    
    /**
     * Return all converted actors back to ISM.
     * Useful for cleanup or reset.
     */
    UFUNCTION(BlueprintCallable, Category = "Physics")
    void ReturnAllToISM(bool bUpdateTransforms = true);
    
    /**
     * Get all currently converted physics actors.
     */
    UFUNCTION(BlueprintCallable, Category = "Physics")
    TArray<AActor*> GetActivePhysicsActors() const;
    
    /**
     * Get number of currently active physics actors.
     */
    UFUNCTION(BlueprintPure, Category = "Physics")
    int32 GetActivePhysicsActorCount() const { return ActivePhysicsActors.Num(); }
    
    /**
     * Check if at max concurrent actors limit.
     */
    UFUNCTION(BlueprintPure, Category = "Physics")
    bool IsAtMaxConcurrentActors() const;
    
    /**
     * Check if conversion should be allowed based on limiters.
     * 
     * @param InstanceIndex - Instance to check
     * @param ImpactForce - Force of the impact
     * @return True if conversion is allowed
     */
    bool ShouldAllowConversion(int32 InstanceIndex, float ImpactForce) const;

protected:
    // ===== Internal State =====
    
    /** Pool subsystem reference (cached on BeginPlay) */
    UPROPERTY()
    TWeakObjectPtr<UISMRuntimePoolSubsystem> PoolSubsystem;
    
    /** Currently active physics actors */
    UPROPERTY()
    TArray<TWeakObjectPtr<AISMPhysicsActor>> ActivePhysicsActors;
    
    /** Frame counter for limiter checks */
    int32 LimiterCheckFrameCounter = 0;
    
    /** Total conversions this session (for stats) */
    int32 TotalConversions = 0;
    
    /** Total returns this session (for stats) */
    int32 TotalReturns = 0;
    
    // ===== Lifecycle Hooks =====
    
    /** Initialize physics subsystem integration */
    virtual void OnInitializationComplete() override;
    
    /** Called when component is being destroyed */
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

    // ===== Conversion Helpers =====
    
    /**
     * Spawn a physics actor from the pool.
     * 
     * @param InstanceHandle - Handle to the instance being converted
     * @return Spawned actor, or nullptr if pool exhausted
     */
    AActor* SpawnPhysicsActorFromPool(const FISMInstanceHandle& InstanceHandle);
    
    /**
     * Handle overflow when max concurrent actors reached.
     * Returns an actor to pool based on overflow behavior.
     */
    void HandleActorOverflow();
    
    /**
     * Return oldest active actor to pool.
     */
    void ReturnOldestActor();
    
    /**
     * Return farthest active actor from player camera.
     */
    void ReturnFarthestActor();
    
    /**
     * Apply initial physics impulse to converted actor.
     * 
     * @param PhysicsActor - Actor to apply impulse to
     * @param ImpactPoint - Where to apply impulse
     * @param ImpactNormal - Direction of impulse
     * @param ImpactForce - Force magnitude
     */
    void ApplyConversionImpulse(AActor* PhysicsActor, FVector ImpactPoint, FVector ImpactNormal, float ImpactForce);

    // ===== Performance Limiters =====
    
    /**
     * Check and enforce distance limits.
     * Returns actors beyond MaxSimulationDistance.
     */
    void EnforceDistanceLimits();
    
    /**
     * Check and enforce lifetime limits.
     * Returns actors that have exceeded MaxLifetime.
     */
    void EnforceLifetimeLimits();
    
    /**
     * Get camera location for distance calculations.
     * Uses player camera if available, otherwise world origin.
     */
    FVector GetCameraLocation() const;

    // ===== Cleanup =====
    
    /**
     * Remove invalid weak pointers from active actors list.
     */
    void CleanupInvalidActors();
    
    /**
     * Register callback for when physics actor returns to pool.
     */
    void RegisterActorReturnCallback(AISMPhysicsActor* PhysicsActor);

public:
    // ===== Events =====
    
    /**
     * Called when an instance is converted to physics.
     * 
     * @param InstanceIndex - Index of converted instance
     * @param PhysicsActor - The spawned physics actor
     */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPhysicsConversion, int32, InstanceIndex, AActor*, PhysicsActor);
    UPROPERTY(BlueprintAssignable, Category = "Physics|Events")
    FOnPhysicsConversion OnPhysicsConversion;
    
    /**
     * Called when a physics actor returns to ISM.
     * 
     * @param InstanceIndex - Index of instance returned
     * @param FinalTransform - Final transform of actor
     */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPhysicsReturn, int32, InstanceIndex, FTransform, FinalTransform);
    UPROPERTY(BlueprintAssignable, Category = "Physics|Events")
    FOnPhysicsReturn OnPhysicsReturn;

    // ===== Debug =====
    
#if WITH_EDITORONLY_DATA
    /** Show debug visualization for active physics actors */
    UPROPERTY(EditAnywhere, Category = "Debug")
    bool bShowDebugInfo = false;
    
    /** Debug color for active physics actors */
    UPROPERTY(EditAnywhere, Category = "Debug", meta=(EditCondition="bShowDebugInfo"))
    FColor DebugColor = FColor::Green;
#endif

#if WITH_EDITOR
    /** Draw debug information in viewport */
    void DrawDebugVisualization() const;
#endif

    // ===== Statistics =====
    
    /**
     * Get statistics for this physics component.
     */
    UFUNCTION(BlueprintCallable, Category = "Physics")
    void GetPhysicsStats(int32& OutActiveActors, int32& OutTotalConversions, int32& OutTotalReturns) const
    {
        OutActiveActors = ActivePhysicsActors.Num();
        OutTotalConversions = TotalConversions;
        OutTotalReturns = TotalReturns;
    }
};