#pragma once

#include "CoreMinimal.h"
#include "ISMPoolDataAsset.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ISMPhysicsDataAsset.generated.h"



UENUM(BlueprintType)
enum class EISMPhysicsFeedbackMode : uint8
{
    /** Fire separate one-shot events on convert and return */
    OneShot         UMETA(DisplayName = "One-Shot Events"),

    /** Fire continuous lifecycle event (start, complete/cancel) */
    Continuous      UMETA(DisplayName = "Continuous Lifecycle")
};

UENUM(BlueprintType)
enum class EISMPhysicsLifecycleResult : uint8
{
    /** Both return and destroy are considered "complete" */
    BothComplete    UMETA(DisplayName = "Both Complete"),

    /** Return = complete, destroy = cancel */
    ReturnComplete  UMETA(DisplayName = "Return Complete, Destroy Cancel"),

    /** Destroy = complete, return = cancel */
    DestroyComplete UMETA(DisplayName = "Destroy Complete, Return Cancel")
};

/**
 * Data asset defining physics behavior for ISM instances.
 * Extends UISMPoolDataAsset with physics-specific configuration.
 * 
 * This allows designers to configure:
 * - Physics properties (mass, damping, gravity)
 * - Collision settings
 * - Conversion thresholds (when to convert to physics)
 * - Resting detection (when to return to ISM)
 * - Material properties
 * 
 * Example usage:
 * - DA_Rock: Heavy mass, high friction, low damping
 * - DA_Bottle: Light mass, glass physics material, shatters easily
 * - DA_Twig: Very light, high damping, quick settle
 * 
 * All using the same AISMPhysicsActor class, just configured differently!
 */
UCLASS(BlueprintType)
class ISMRUNTIMEPHYSICS_API UISMPhysicsDataAsset : public UISMPoolDataAsset
{
    GENERATED_BODY()

public:
    // ===== Physics Properties =====
    
    /**
     * Mass of the physics actor in kilograms.
     * Set to 0 to auto-calculate from mesh volume and density.
     * 
     * Typical values:
     * - Twig: 0.1 - 0.5 kg
     * - Bottle: 0.5 - 2 kg
     * - Rock: 5 - 50 kg
     * - Barrel: 10 - 100 kg
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="1000.0",
              Tooltip="Mass in kg. 0 = auto-calculate from mesh."))
    float Mass = 0.0f;
    
    /**
     * Linear damping - resistance to linear motion.
     * Higher values make objects slow down faster.
     * 
     * Typical values:
     * - In air: 0.01 - 0.1
     * - In water: 1.0 - 5.0
     * - Quick settle (twigs): 2.0 - 10.0
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="10.0",
              Tooltip="Resistance to linear motion. Higher = faster slowdown."))
    float LinearDamping = 0.1f;
    
    /**
     * Angular damping - resistance to rotation.
     * Higher values make objects stop spinning faster.
     * 
     * Typical values:
     * - Normal objects: 0.05 - 0.5
     * - Quick settle: 1.0 - 5.0
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="10.0",
              Tooltip="Resistance to rotation. Higher = faster spin down."))
    float AngularDamping = 0.05f;
    
    /**
     * Enable gravity on this physics actor.
     * Disable for floating debris or zero-gravity scenarios.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics",
        meta=(Tooltip="Enable gravity simulation."))
    bool bEnableGravity = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics")
	FWalkableSlopeOverride WalkableSlopeOverride;
    
    /**
     * Physics material defining friction and restitution (bounciness).
     * Leave null to use mesh's default physics material.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics",
        meta=(Tooltip="Physics material for friction/bounce. Null = use mesh default."))
    TObjectPtr<UPhysicalMaterial> PhysicsMaterial = nullptr;
    
    // ===== Collision Settings =====
    
    /**
     * Collision profile to use for the physics actor.
     * Common presets:
     * - "PhysicsActor" - Standard physics object
     * - "Destructible" - Can be hit by projectiles
     * - "Debris" - Lightweight, ignores most collision
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision",
        meta=(Tooltip="Collision preset name. Common: PhysicsActor, Destructible, Debris."))
    FName CollisionPreset = "PhysicsActor";
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision")
    bool bCanEverEffectNavigation = false;

    /**
     * Enable collision between this and other physics actors.
     * Disable for better performance if actors don't need to collide with each other.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision",
        meta=(Tooltip="Allow collision with other physics actors. Disable for performance."))
    bool bEnablePhysicsCollision = true;
    
    // ===== Destruction Thresholds =====

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision|Destruction", meta = (
        EditCondition = "bEnablePhysicsCollision",
        Tooltip= "Enable if this ISM can be destroyed by impacts.  NOTE: PhysicsActor must have Enable Physics Collision = true to be destructable"))
    bool bIsDestructable = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision|Destruction",
        meta = (
            ClampMin = "0.0", UIMin = "0.0", UIMax = "50000.0",
            EditCondition = "bEnablePhysicsCollision && bIsDestructable", EditConditionHides,
            Tooltip = "Minimum impact force to convert to destroy instances/physics objects."))
    float DestructionForceThreshold = 800.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision|Destruction",
        meta = (EditCondition = "bEnablePhysicsCollision && bIsDestructable", EditConditionHides,
                Tooltip = "Destuction Theshold is multiplied by the object scale (should be mass?), so larger instances are stronger."))
	bool bDestructionThresholdIsScaled = true;

    //UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision|Cascade", 
    //    Tooltip = ("This enables ISMPhysicsActor to itself hit ISMRuntimeComponent instances"))
    //bool bEnableISMCascading = false;
    

    // ===== Conversion Thresholds =====

    /**
     * Minimum impact force required to trigger conversion from ISM to physics actor.
     * Lower values = more sensitive to impacts.
     * 
     * Typical values:
     * - Very sensitive (twigs): 10 - 50
     * - Normal (bottles, small props): 100 - 500
     * - Sturdy (barrels, crates): 500 - 2000
     * - Heavy (rocks, large props): 2000 - 10000
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conversion",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="10000.0",
              Tooltip="Minimum impact force to convert to physics. Lower = more sensitive."))
    float ConversionForceThreshold = 100.0f;
    
    // ===== Resting Detection =====
    
    /**
     * Velocity threshold (cm/s) below which actor is considered "at rest".
     * When velocity stays below this for RestingCheckDelay seconds, actor returns to ISM.
     * 
     * Typical values:
     * - Strict (quick return): 1 - 5 cm/s
     * - Normal: 5 - 10 cm/s  
     * - Loose (allow slow roll): 10 - 20 cm/s
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resting",
        meta=(ClampMin="0.1", UIMin="0.1", UIMax="100.0",
              Tooltip="Velocity threshold (cm/s) to consider actor at rest."))
    float RestingVelocityThreshold = 5.0f;
    
    /**
     * How long (seconds) velocity must stay below threshold before returning to ISM.
     * Prevents premature return if actor is slowly rolling/settling.
     * 
     * Typical values:
     * - Quick return (twigs): 0.1 - 0.5s
     * - Normal: 0.5 - 1.0s
     * - Patient (ensure fully settled): 1.0 - 2.0s
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resting",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="5.0",
              Tooltip="Time to stay below threshold before returning to ISM."))
    float RestingCheckDelay = 0.5f;
    
    /**
     * Check angular velocity in addition to linear velocity for resting.
     * Enable for objects that should stop spinning before returning (bottles, etc).
     * Disable for objects where spin doesn't matter (debris, etc).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resting",
        meta=(Tooltip="Also check if rotation has stopped before returning to ISM."))
    bool bCheckAngularVelocity = true;
    
    /**
     * Angular velocity threshold (rad/s) for resting check.
     * Only used if bCheckAngularVelocity is true.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resting",
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="10.0",
              EditCondition="bCheckAngularVelocity", EditConditionHides,
              Tooltip="Angular velocity threshold (rad/s) to consider actor at rest."))
    float RestingAngularThreshold = 0.1f;


    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resting|Debugging", meta = (Tooltip = "Log resting checks and state changes for debugging."))
	bool bLogRestingChecks = false;

    
    
    /**
     * Scale applied to the actor when converted.
     * Useful if ISM instance scale doesn't match 1:1 with actor mesh.
     * Typically leave at 1.0.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual",
        meta=(ClampMin="0.01", UIMin="0.1", UIMax="10.0",
              Tooltip="Scale multiplier when converting to actor. Usually 1.0."))
    float ActorScaleMultiplier = 1.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    bool bPhysicsActorCastShadows = true;


    // ===== Feedback Configuration =====

    /**
     * How to fire feedback events from physics actor lifecycle.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
    EISMPhysicsFeedbackMode FeedbackMode = EISMPhysicsFeedbackMode::OneShot;
    
    bool IsContinous() const { return FeedbackMode == EISMPhysicsFeedbackMode::Continuous;}
    // --- One-Shot Mode (separate convert/return events) ---

    /**
     * Feedback to play when instance converts to physics actor.
     * Only used in One-Shot mode.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback",
        meta = (EditCondition = "FeedbackMode == EISMPhysicsFeedbackMode::OneShot", EditConditionHides))
    FGameplayTag ConversionFeedback;

    /**
     * Feedback to play when physics actor returns to ISM.
     * Only used in One-Shot mode.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback",
        meta = (EditCondition = "FeedbackMode == EISMPhysicsFeedbackMode::OneShot", EditConditionHides))
    FGameplayTag ReturnFeedback;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback",
        meta = (EditCondition = "FeedbackMode == EISMPhysicsFeedbackMode::OneShot", EditConditionHides))
    FGameplayTag DestroyFeedback;

    // --- Continuous Mode (lifecycle event) ---

    /**
     * Feedback tag for continuous lifecycle event.
     * Fired with lifecycle stage in custom params.
     *
     * Examples:
     * - "Feedback.Physics.Lifecycle.Bottle" -> start loop on spawn, stop on despawn
     * - "Feedback.Physics.Lifecycle.Bomb" -> start fuse, complete on explode, cancel on disarm
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback",
        meta = (EditCondition = "FeedbackMode == EISMPhysicsFeedbackMode::Continuous", EditConditionHides))
    FGameplayTag LifecycleFeedbackTag;

    /**
     * How to interpret the lifecycle result.
     * Determines whether return/destroy are considered "complete" or "cancel".
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback",
        meta = (EditCondition = "FeedbackMode == EISMPhysicsFeedbackMode::Continuous", EditConditionHides))
    EISMPhysicsLifecycleResult LifecycleResult = EISMPhysicsLifecycleResult::BothComplete;


    

    
    // ===== Advanced Settings =====
    
    /**
     * Lock rotation on specific axes.
     * Useful for objects that should only rotate in certain directions.
     * Example: Logs that roll but don't tumble.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced", AdvancedDisplay,
        meta=(Tooltip="Lock rotation on specific axes."))
    bool bLockRotationX = false;
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced", AdvancedDisplay,
        meta=(Tooltip="Lock rotation on specific axes."))
    bool bLockRotationY = false;
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced", AdvancedDisplay,
        meta=(Tooltip="Lock rotation on specific axes."))
    bool bLockRotationZ = false;
    
    /**
     * Maximum angular velocity (rad/s).
     * Prevents objects from spinning unrealistically fast.
     * Set to 0 for no limit.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced", AdvancedDisplay,
        meta=(ClampMin="0.0", UIMin="0.0", UIMax="100.0",
              Tooltip="Max angular velocity (rad/s). 0 = no limit."))
    float MaxAngularVelocity = 0.0f;
    
    /**
     * Center of mass offset from actor origin.
     * Used to simulate off-center weight distribution.
     * Leave at zero for automatic center of mass.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced", AdvancedDisplay,
        meta=(Tooltip="Center of mass offset. Zero = automatic."))
    FVector CenterOfMassOffset = FVector::ZeroVector;

#if WITH_EDITOR
    /**
     * Validate physics configuration.
     * Called in editor when properties change.
     */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    /**
     * Get recommended pool configuration based on physics properties.
     * Heavier/slower objects need smaller pools than light/fast debris.
     */
    void GetRecommendedPoolSettings(int32& OutInitialSize, int32& OutGrowSize) const;


    void LogRestingCheck(const class AActor* Owner, float LinearVelocity, float AngularVelocity, bool passed);
};