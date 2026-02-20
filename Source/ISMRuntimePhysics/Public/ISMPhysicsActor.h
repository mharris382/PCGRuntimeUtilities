#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ISMInstanceHandle.h"
#include "ISMRuntimeComponent.h"
//#include "Logging/LogMacros.h"
#include "Interfaces/ISMPoolable.h"
#include "GameplayTagAssetInterface.h"
#include "CustomData/ISMCustomDataMaterialProvider.h"
#include "ISMPhysicsActor.generated.h"

// Forward declarations
class UStaticMeshComponent;
class UISMPhysicsDataAsset;
struct FISMInstanceHandle;

/**
 * Pooled physics actor for ISM instance conversion.
 * 
 * This is a single, reusable actor class that handles ALL physics conversions.
 * Different behaviors are achieved through UISMPhysicsDataAsset configuration,
 * NOT by creating actor subclasses.
 * 
 * Features:
 * - Configures itself from data asset (mass, damping, materials, etc.)
 * - Tracks resting state and auto-returns to ISM when settled
 * - Plays audio feedback on conversion/return
 * - Handles custom scale, center of mass, rotation locks
 * - Zero allocations via pooling system
 * 
 * Lifecycle:
 * 1. OnPoolSpawned() - One-time setup when first added to pool
 * 2. OnRequestedFromPool(DataAsset, Handle) - Configure from data asset
 * 3. Simulate physics...
 * 4. OnReturnedToPool() - Reset state, return final transform
 * 5. Back to step 2 (recycled)
 * 
 * Usage (via pool subsystem):
 * ```cpp
 * UISMRuntimePoolSubsystem* Pools = World->GetSubsystem<UISMRuntimePoolSubsystem>();
 * AActor* PhysicsActor = Pools->RequestActor(AISMPhysicsActor::StaticClass(), PhysicsDataAsset, InstanceHandle);
 * ```
 */
UCLASS(Blueprintable)
class ISMRUNTIMEPHYSICS_API AISMPhysicsActor 
    : public AActor
    , public IISMPoolable
    , public IGameplayTagAssetInterface
    , public IISMCustomDataMaterialProvider
{
    GENERATED_BODY()

public:
    AISMPhysicsActor();

    // ===== Components =====
    
    /**
     * Root mesh component with physics enabled.
     * Configured from data asset when requested from pool.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MeshComponent;

    // ===== Actor Lifecycle =====
    
    virtual void Tick(float DeltaTime) override;

    // ===== Interfaces =====

#pragma region INTERFACES

    // ===== IISMCustomDataMaterialProvider Interface =====

    virtual void ApplyDMIToSlot_Implementation(int32 SlotIndex, UMaterialInstanceDynamic* DMI) override;
    virtual int32 GetMaterialSlotCount_Implementation() const override;
    virtual bool ShouldSkipSlot_Implementation(int32 SlotIndex) const override;

    // ===== IISMPoolable Interface =====

                    /**
     * Called when first spawned into pool.
     * Sets up basic actor state (hidden, no collision, etc).
     */
    virtual void OnPoolSpawned_Implementation() override;

    /**
     * Called when requested from pool for use.
     * Configures actor from data asset and instance handle.
     *
     * @param DataAsset - Physics configuration (must be UISMPhysicsDataAsset)
     * @param InstanceHandle - Reference to ISM instance being converted
     */
    virtual void OnRequestedFromPool_Implementation(UISMPoolDataAsset* DataAsset, const FISMInstanceHandle& InstanceHandle) override;

    /**
     * Called when being returned to pool.
     * Captures final transform and resets physics state.
     *
     * @param OutFinalTransform - Filled with actor's final transform
     * @param bUpdateInstanceTransform - Set to true to update ISM instance
     */
    virtual void OnReturnedToPool_Implementation(FTransform& OutFinalTransform, bool& bUpdateInstanceTransform) override;

    /**
     * Called when pool is destroyed.
     * Final cleanup before actor destruction.
     */
    virtual void OnPoolDestroyed_Implementation() override;



    // ===== IGameplayTagAssetInterface =====

public:

    /**
     * Get owned gameplay tags from the instance handle.
     * Returns tags from the ISM instance this actor was converted from.
     */
    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override
    {
        if (InstanceHandle.IsValid() && InstanceHandle.Component.IsValid())
        {
            TagContainer = InstanceHandle.Component.Get()->GetInstanceTags(InstanceHandle.InstanceIndex);
        }
    }

#pragma endregion


    // ===== Physics Configuration =====

#pragma region Config

public:

    /**
     * Apply physics properties from data asset to mesh component.
     *
     * @param PhysicsData - Data asset containing physics configuration
     */
    void ApplyPhysicsSettings(UISMPhysicsDataAsset* PhysicsData);

    /**
     * Apply collision settings from data asset.
     *
     * @param PhysicsData - Data asset containing collision configuration
     */
    void ApplyCollisionSettings(UISMPhysicsDataAsset* PhysicsData);

    /**
     * Apply visual settings (scale, materials) from data asset.
     *
     * @param PhysicsData - Data asset containing visual configuration
     */
    void ApplyVisualSettings(UISMPhysicsDataAsset* PhysicsData);


#pragma endregion

    // ===== Resting Detection =====

#pragma region RESTING_DETECTION


/**
 * Check if actor is currently at rest (velocity below threshold).
 * Considers both linear and angular velocity based on data asset settings.
 *
 * @return True if actor is at rest
 */
    UFUNCTION(BlueprintCallable, Category = "ISM Physics")
    bool IsAtRest() const;

    /**
     * Check if actor has been at rest for the required duration.
     * Only returns true after RestingCheckDelay seconds of rest.
     *
     * @return True if actor should return to ISM
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Physics")
    bool ShouldReturnToISM() const;

    /**
     * Manually trigger return to ISM.
     * Called automatically when resting, or manually via reset triggers.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Physics")
    void ReturnToISM();
#pragma endregion

    
    // ===== Feedback System Integration =====

#pragma region FEEDBACK

    /**
     * Fire conversion feedback via feedback subsystem.
     * Called when actor is requested from pool and configured.
     * Allows effects to be attached to this actor (trails, loops, etc).
     */
    void PlayConversionFeedback();

    /**
     * Fire return feedback via feedback subsystem.
     * Called when actor is being returned to pool.
     * Final chance for effects based on actor state (impact sounds, settle VFX, etc).
     */
    void PlayReturnFeedback();


	void PlayDestructionFeedback();

#pragma endregion


    // ===== Destruction =====
#pragma region DESTRUCTION

    protected:
        /**
         * Called when physics actor hits something.
         * Checks if impact force exceeds destruction threshold and destroys linked ISM instance if so.
         * Only active when bIsDestructable and bEnablePhysicsCollision are true in data asset.
         */
        UFUNCTION()
        void OnPhysicsActorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
            UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

        /**
         * Destroy the ISM instance this physics actor was converted from.
         * Called when impact force exceeds destruction threshold.
         * Triggers OnInstanceDestroyed event which feeds into feedback system.
         */
        void DestroyLinkedISMInstance();

    protected:
        bool ShouldDestroyOnImpact(float ImpactForce) const;
#pragma endregion



#pragma region ACCESSORS

        // ===== Accessors =====
public:
        /**
         * Get the instance handle this actor was converted from.
         * Invalid if actor is in pool or wasn't converted from ISM.
         */
        UFUNCTION(BlueprintPure, Category = "ISM Physics")
        const FISMInstanceHandle& GetInstanceHandle() const { return InstanceHandle; }



        void SetInstanceHandle(const struct FISMInstanceHandle& Handle);


        /**
         * Get the physics data asset used to configure this actor.
         * Null if actor is in pool or not configured yet.
         */
        UFUNCTION(BlueprintPure, Category = "ISM Physics")
        UISMPhysicsDataAsset* GetPhysicsData() const { return PhysicsData.Get(); }

        /**
         * Get current linear velocity magnitude.
         */
        UFUNCTION(BlueprintPure, Category = "ISM Physics")
        float GetLinearVelocityMagnitude() const;

        /**
         * Get current angular velocity magnitude.
         */
        UFUNCTION(BlueprintPure, Category = "ISM Physics")
        float GetAngularVelocityMagnitude() const;

        /**
         * Get time actor has been at rest.
         * Returns 0 if not currently at rest.
         */
        UFUNCTION(BlueprintPure, Category = "ISM Physics")
        float GetTimeAtRest() const { return TimeAtRest; }
#pragma endregion

    // ===== State Tracking =====

#pragma region STATETRACKING

protected:

    /** Reference to the ISM instance this actor was converted from */
    FISMInstanceHandle InstanceHandle;

    /** Physics configuration data asset */
    UPROPERTY()
    TWeakObjectPtr<UISMPhysicsDataAsset> PhysicsData;

    /** Time spent at rest (below velocity threshold) */
    float TimeAtRest = 0.0f;

    /** Was actor at rest last frame? (for detecting rest start) */
    bool bWasAtRestLastFrame = false;

    /** Time when actor was requested from pool (for lifetime tracking) */
    double TimeActivated = 0.0;

    // ===== Helper Functions =====

    /**
     * Reset all runtime state.
     * Called when returning to pool or initializing.
     */
    void ResetRuntimeState();

    /**
     * Update resting detection.
     * Called each tick to track rest duration.
     *
     * @param DeltaTime - Time since last tick
     */
    void UpdateRestingDetection(float DeltaTime);

    /**
     * Check if velocity is below threshold for resting.
     *
     * @param LinearVelocity - Current linear velocity magnitude
     * @param AngularVelocity - Current angular velocity magnitude
     * @return True if below thresholds
     */
    bool IsVelocityBelowThreshold(float LinearVelocity, float AngularVelocity) const;

    const FGameplayTagContainer GetInstanceTags()
    {
		return InstanceHandle.IsValid() ? InstanceHandle.GetInstanceTags() : FGameplayTagContainer();
    }

#pragma endregion

	

public:
    // ===== Debug Visualization =====
    
#if WITH_EDITORONLY_DATA
    /** Show debug info for this physics actor */
    UPROPERTY(EditAnywhere, Category = "Debug")
    bool bShowDebugInfo = false;
#endif

#if WITH_EDITOR
    /**
     * Draw debug information in editor viewport.
     * Shows velocity, rest state, etc.
     */
    void DrawDebugInfo() const;
#endif
};