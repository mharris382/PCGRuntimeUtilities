#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Delegates/DelegateCombinations.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "ISMPhysicsInstigatorComponent.generated.h"

// Forward declarations
class UISMPhysicsComponent;
class UPrimitiveComponent;

/**
 * Simple plug-and-play component for triggering physics conversions.
 * Attach to player, AI, or physics objects to enable them to knock over ISM instances.
 * 
 * Features:
 * - Automatic detection via movement queries or collision events
 * - Configurable force calculation
 * - Performance throttling
 * - Works with both query modes (ISM spatial and physics engine)
 * 
 * Usage:
 * 1. Add to player character or AI
 * 2. Configure QueryRadius and ImpactForceMultiplier
 * 3. Done! Actor will now trigger physics on ISM instances
 * 
 * No blueprint code required - completely automatic.
 */
UCLASS(Blueprintable, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMEPHYSICS_API UISMPhysicsInstigatorComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UISMPhysicsInstigatorComponent();

    // ===== Lifecycle =====
    
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ===== Configuration =====
    
    /**
     * Radius for detecting nearby ISM instances (cm).
     * Larger = detect more instances, worse performance.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator")
    float QueryRadius = 100.0f;
    
    /**
     * Multiplier for calculated impact force.
     * Higher = stronger impacts, more conversions.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator")
    float ImpactForceMultiplier = 1.0f;
    
    /**
     * How often to check for nearby instances (seconds).
     * 0 = every frame (expensive!), higher = better performance.
     * 
     * Recommended: 0.1 - 0.2 for fast movement, 0.5 for slow movement
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator|Performance")
    float UpdateInterval = 0.1f;
    
    /**
     * Calculate impact force from owner's velocity.
     * If false, uses a constant force based on ImpactForceMultiplier.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator")
    bool bUseVelocityBasedForce = true;
    
    /**
     * Mass of the owner for force calculations (kg).
     * Only used if bUseVelocityBasedForce is true.
     * Set to 0 to auto-detect from root primitive component.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator",
        meta=(ClampMin="0.0", EditCondition="bUseVelocityBasedForce", EditConditionHides))
    float OwnerMass = 0.0f;
    
    /**
     * Minimum velocity (cm/s) required to trigger conversions.
     * Prevents impacts when standing still.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="1000.0",
              Tooltip="Min velocity (cm/s) to trigger physics."))
    float MinimumVelocity = 10.0f;
    
    ///**
    // * Enable debug visualization of instigator queries and impacts.
    // * Shows query radius and impact points in the world.
	// */
    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator|Debugging")
    //bool bEnableInstigatorVisualization = false;

    /**
     * Only trigger physics on instances with these tags.
     * Empty = affect all instances.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator|Filtering")
    FGameplayTagContainer RequiredTags;
    
    /**
     * Never trigger physics on instances with these tags.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instigator|Filtering")
    FGameplayTagContainer ExcludedTags;

    // ===== Manual Triggering =====
    
    /**
     * Manually trigger physics conversions at a specific location.
     * Useful for explosions, projectile impacts, etc.
     * 
     * @param WorldLocation - Center point for query
     * @param Radius - Radius to search for instances
     * @param Force - Force to apply
     */
    UFUNCTION(BlueprintCallable, Category = "Instigator")
    void TriggerPhysicsAtLocation(FVector WorldLocation, float Radius, float Force);
    
    /**
     * Manually trigger a single instance conversion.
     * 
     * @param PhysicsComponent - Component managing the instance
     * @param InstanceIndex - Index of instance to convert
     * @param ImpactPoint - Where to apply force
     * @param Force - Force magnitude
     */
    UFUNCTION(BlueprintCallable, Category = "Instigator")
    void TriggerSingleInstance(UISMPhysicsComponent* PhysicsComponent, int32 InstanceIndex, 
        FVector ImpactPoint, float Force);

protected:
    // ===== Internal State =====
    
    /** Time accumulator for update interval */
    float TimeSinceLastUpdate = 0.0f;
    
    /** Cached owner velocity (for force calculations) */
    FVector CachedVelocity = FVector::ZeroVector;
    
    /** Last frame's position (for velocity calculation) */
    FVector LastPosition = FVector::ZeroVector;
    
    /** Cached root primitive component */
    UPROPERTY()
    TWeakObjectPtr<UPrimitiveComponent> CachedRootPrimitive;
    
    /** Cached list of physics components in world (refreshed periodically) */
    UPROPERTY()
    TArray<TWeakObjectPtr<UISMPhysicsComponent>> CachedPhysicsComponents;
    
    /** Frame when cached components were last updated */
    int32 ComponentCacheUpdateFrame = 0;
    
    /** How often to refresh component cache (frames) */
    static constexpr int32 ComponentCacheRefreshInterval = 300; // Every 5 seconds at 60fps

    // ===== Query & Conversion =====
    
    /**
     * Perform physics instigator update.
     * Finds nearby instances and triggers conversions.
     */
    void PerformInstigatorUpdate();
    
    /**
     * Find all physics components in the world.
     * Results are cached for performance.
     */
    void RefreshPhysicsComponentsCache();
    
    /**
     * Get all physics components (uses cache).
     */
    TArray<UISMPhysicsComponent*> GetPhysicsComponents();
    
    /**
     * Query a single physics component for nearby instances.
     * 
     * @param PhysicsComponent - Component to query
     * @param OutInstances - Filled with nearby instance indices
     */
    void QueryComponent(UISMPhysicsComponent* PhysicsComponent, TArray<int32>& OutInstances);
    
    /**
     * Calculate impact force based on owner velocity and mass.
     * 
     * @return Calculated force magnitude
     */
    float CalculateImpactForce() const;
    
    /**
     * Update cached velocity.
     * Called each tick to track owner movement.
     */
    void UpdateVelocity(float DeltaTime);
    
    /**
     * Get owner velocity.
     * Tries primitive component first, falls back to manual calculation.
     */
    FVector GetOwnerVelocity() const;
    
    /**
     * Get owner mass for force calculations.
     * Uses configured value or auto-detects from root primitive.
     */
    float GetOwnerMassForCalculation() const;
    
    /**
     * Check if instance passes filter criteria (tags).
     * 
     * @param PhysicsComponent - Component owning the instance
     * @param InstanceIndex - Instance to check
     * @return True if instance should be affected
     */
    bool PassesFilter(UISMPhysicsComponent* PhysicsComponent, int32 InstanceIndex) const;

    // ===== Collision-Based Detection =====
    
    /**
     * Handle collision event (for physics query mode).
     * Automatically triggers conversions on ISM instances hit by owner.
     */
    UFUNCTION()
    void OnOwnerHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
        FVector NormalImpulse, const FHitResult& Hit);
    
    /**
     * Bind to owner's collision events.
     */
    void BindCollisionEvents();
    
    /**
     * Unbind from owner's collision events.
     */
    void UnbindCollisionEvents();

public:
    // ===== Events =====
    
    /**
     * Called when this instigator triggers a physics conversion.
     * 
     * @param PhysicsComponent - Component that was triggered
     * @param InstanceIndex - Instance that was converted
     * @param Force - Force that was applied
     */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInstigatorTriggered, UISMPhysicsComponent*, PhysicsComponent, 
        int32, InstanceIndex, float, Force);

    UPROPERTY(BlueprintAssignable, Category = "Instigator|Events")
    FOnInstigatorTriggered OnInstigatorTriggered;

    // ===== Debug =====
    
#if WITH_EDITORONLY_DATA
    /** Show debug sphere for query radius */
    UPROPERTY(EditAnywhere, Category = "Debug")
    bool bShowDebugSphere = false;
    
    /** Show debug lines for detected instances */
    UPROPERTY(EditAnywhere, Category = "Debug")
    bool bShowDebugLines = false;
#endif

#if WITH_EDITOR
    /** Draw debug visualization */
    void DrawDebugInfo() const;
#endif

    // ===== Utility =====
    
    /**
     * Enable or disable the instigator.
     * Useful for temporarily disabling physics interactions.
     */
    UFUNCTION(BlueprintCallable, Category = "Instigator")
    void SetInstigatorEnabled(bool bEnabled);
    
    /**
     * Check if instigator is currently enabled.
     */
    UFUNCTION(BlueprintPure, Category = "Instigator")
    bool IsInstigatorEnabled() const { return bIsEnabled; }

    float GetQueryRadius() const;

private:
    /** Whether instigator is currently active */
    bool bIsEnabled = true;
};