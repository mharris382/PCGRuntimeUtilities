#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ISMSpatialIndex.h"
#include "Feedbacks/ISMFeedbackTags.h"
#include "ISMInstanceHandle.h"
#include "Delegates/DelegateCombinations.h"
#include "Interfaces/ISMStateProvider.h"
#include "GameplayTagAssetInterface.h"
#include "ISMRuntimeComponent.generated.h"

// Forward declarations
class UInstancedStaticMeshComponent;
class UISMInstanceDataAsset;

struct FISMQueryFilter;
struct FISMInstanceState;
struct FISMFeedbackParticipant;

/**
 * Delegate signatures for instance events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInstanceStateChanged, class UISMRuntimeComponent*, Component, int32, InstanceIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInstanceDestroyed, class UISMRuntimeComponent*, Component, int32, InstanceIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInstanceTagsChanged, class UISMRuntimeComponent*, Component, int32, InstanceIndex);

//DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnReleaseConvertedActor, class UISMRuntimeComponent*, Component, int32, InstanceIndex, AActor*, ReleasedActor);
//DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInstanceConverted, class UISMRuntimeComponent*, Component, int32, InstanceIndex, AActor*, ConvertedActor);
//DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInstanceReturnedToISM, class UISMRuntimeComponent*, Component, int32, InstanceIndex);


DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInstanceStateChangedNative, class UISMRuntimeComponent*, int32);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInstanceDestroyedNative, class UISMRuntimeComponent*, int32);

/**
 * Base component for managing ISM instances at runtime with gameplay features.
 * Provides spatial indexing, state tracking, tagging, and event system.
 * 
 * All specialized ISM features (damage, resources, physics) inherit from this.
 */
UCLASS(Blueprintable, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMECORE_API UISMRuntimeComponent 
    : public UActorComponent
    , public IISMStateProvider
    , public IGameplayTagAssetInterface
{
    GENERATED_BODY()
    
public:
    UISMRuntimeComponent();
    
    // ===== Lifecycle =====
    
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    
    // ===== Component Configuration =====
    
#pragma region COMPONENTS
                    /** The ISM component this runtime component manages */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    UInstancedStaticMeshComponent* ManagedISMComponent;

    /** Data asset defining behavior and properties for instances */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    UISMInstanceDataAsset* InstanceData;

    /** Cell size for spatial indexing (defaults to 1000cm = 10m) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime|Performance", meta = (ClampMin = "100.0"))
    float SpatialIndexCellSize = 1000.0f;

#pragma endregion

    


    // ===== Gameplay Tags =====
    
#pragma region GAMEPLAY_TAGS
                    /** Tags applied to ALL instances managed by this component */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime|Tags")
    FGameplayTagContainer ISMComponentTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime|Tags", meta = (Tooltip="Default Feedback Tags used for ISM Runtime Feedbacks, which is used when the InstanceDataAsset doesn't provide any Feedback Tags"))
    FISMFeedbackTags DefaultFeedbackTags;

    /** Per-instance tags (sparse - only instances with unique tags) */
    UPROPERTY()
    TMap<int32, FGameplayTagContainer> PerInstanceTags;

    /** Add a tag to a specific instance */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Tags")
    void AddInstanceTag(int32 InstanceIndex, FGameplayTag Tag);

    /** Remove a tag from a specific instance */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Tags")
    void RemoveInstanceTag(int32 InstanceIndex, FGameplayTag Tag);

    /** Check if a specific instance has a tag */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Tags")
    bool InstanceHasTag(int32 InstanceIndex, FGameplayTag Tag) const;

    /** Get all tags for a specific instance (component + instance-specific) */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Tags")
    FGameplayTagContainer GetInstanceTags(int32 InstanceIndex) const;

    /** Check if component has a tag */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Tags")
    bool HasTag(FGameplayTag Tag) const { return ISMComponentTags.HasTag(Tag); }

    /** Check if component has all tags */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Tags")
    bool HasAllTags(const FGameplayTagContainer& Tags) const { return ISMComponentTags.HasAll(Tags); }

    /** Check if component has any of the tags */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Tags")
    bool HasAnyTag(const FGameplayTagContainer& Tags) const { return ISMComponentTags.HasAny(Tags); }



#pragma endregion

#pragma region IGameplayTagAssetInterface

public:
    // ===== IGameplayTagAssetInterface =====

    /**
     * Get owned gameplay tags (component-level tags).
     * Note: This returns component tags, not per-instance tags.
     * Use GetInstanceTags(InstanceIndex) for per-instance tags.
     */
    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override
    {
        TagContainer.AppendTags(ISMComponentTags);
    }

    // Optional: Add helper to get tags for specific instance
    void GetOwnedGameplayTagsForInstance(FGameplayTagContainer& TagContainer, int32 InstanceIndex) const
    {
        TagContainer.AppendTags(ISMComponentTags);
        if (const FGameplayTagContainer* InstanceTags = PerInstanceTags.Find(InstanceIndex))
        {
            TagContainer.AppendTags(*InstanceTags);
        }
    }

#pragma endregion

    // ===== Instance Management =====
    
#pragma region INSTANCE_MANAGEMENT

                    /** Initialize all instances (called automatically on BeginPlay) */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    virtual bool InitializeInstances();


    bool IsValidInstanceIndex(int32 InstanceIndex) const;


    /**
     * Hide an instance without destroying it.
     * @param InstanceIndex The instance to hide
     * @param bUpdateBounds Whether to recalculate bounds after hiding (expensive O(n) operation)
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    virtual void HideInstance(int32 InstanceIndex, bool bUpdateBounds = false, 
        bool bTriggerFeedbacks = true, const UActorComponent* InstigatorComponent = nullptr);

    /**
     * Show a previously hidden instance.
     * @param InstanceIndex The instance to show
     * @param bUpdateBounds Whether to recalculate bounds after showing (expensive O(n) operation)
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    virtual void ShowInstance(int32 InstanceIndex, bool bUpdateBounds = false, 
        bool bTriggerFeedbacks = true, const UActorComponent* InstigatorComponent = nullptr);


    
            /**
             * Update instance transform.
             * @param InstanceIndex The instance to update
             * @param NewTransform New transform for the instance
             * @param bUpdateSpatialIndex Whether to update spatial index (default true)
             * @param bUpdateBounds Whether to recalculate bounds (expensive O(n) operation, default false)
             */
            UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
            virtual void UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewTransform, 
                bool bUpdateSpatialIndex = true, bool bUpdateBounds = false, 
                bool bTriggerFeedbacks = true, const UActorComponent* InstigatorComponent = nullptr);
        
          /**
            * Destroy an instance (hides it and marks as destroyed, but index remains valid).
            * @param InstanceIndex The instance to destroy
            * @param bUpdateBounds Whether to recalculate bounds after destruction (expensive O(n) operation)
            */
            UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
            virtual void DestroyInstance(int32 InstanceIndex, bool bUpdateBounds = false,
                bool bTriggerFeedbacks = true, const UActorComponent* InstigatorComponent = nullptr);

            /**
             * Batch destroy multiple instances efficiently.
             * Only recalculates bounds once at the end if requested.
             * @param InstanceIndices Array of instance indices to destroy
             * @param bUpdateBounds Whether to recalculate bounds after all destructions (one O(n) operation)
             */
            UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
            void BatchDestroyInstances(const TArray<int32>& InstanceIndices, bool bUpdateBounds = false,
                bool bTriggerFeedbacks = true, const UActorComponent* InstigatorComponent = nullptr);

            //
           // ===== Instance Creation =====

           /**
            * Add a new instance to the managed ISM component.
            * Automatically initializes state, adds to spatial index, and optionally updates bounds.
            * @param Transform World transform for the new instance
            * @param bUpdateBounds Whether to expand bounds to include new instance (O(1) operation)
            * @return Index of the newly added instance, or INDEX_NONE if failed
            */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        int32 AddInstance(const FTransform& Transform, bool bUpdateBounds = true,
            bool bTriggerFeedbacks = true, const UActorComponent* InstigatorComponent = nullptr);

        /**
         * Add multiple instances efficiently in a batch.
         * Much faster than calling AddInstance in a loop.
         * @param Transforms Array of world transforms for new instances
         * @param bUpdateBounds Whether to update bounds after adding all instances (single O(1) operation)
         * @return Array of indices for newly added instances (INDEX_NONE for any that failed)
         */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        TArray<int32> BatchAddInstances(const TArray<FTransform>& Transforms, bool bUpdateBounds = true,
            bool bShouldReturnIndices = true, bool bUpdateNavigation = false, 
            bool bTriggerFeedbacks = true, const UActorComponent* InstigatorComponent = nullptr);

        /**
         * Add instances with custom data.
         * Useful for setting per-instance material parameters.
         * @param Transform World transform for the new instance
         * @param CustomData Per-instance custom data for materials
         * @param bUpdateBounds Whether to expand bounds to include new instance
         * @return Index of the newly added instance, or INDEX_NONE if failed
         */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        int32 AddInstanceWithCustomData(const FTransform& Transform, const TArray<float>& CustomData, bool bUpdateBounds = true);




        // ===== Instance Queries =====

        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        bool IsInstanceDestroyed(int32 InstanceIndex) const;
    
        /** Check if an instance is active (not destroyed, collected, or hidden) */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        bool IsInstanceActive(int32 InstanceIndex) const;
    
        /** Get total number of instances (including destroyed ones) */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        int32 GetInstanceCount() const;
    
        /** Get number of active instances (excludes destroyed) */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        int32 GetActiveInstanceCount() const;
    
        // ===== Transform Access =====
    
        /** Get instance transform */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        FTransform GetInstanceTransform(int32 InstanceIndex) const;
    
        /** Get instance world location */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
        FVector GetInstanceLocation(int32 InstanceIndex) const;
#pragma endregion

    protected:
        /**
         * Initialize state for a newly added instance.
         * Called automatically by AddInstance.
         */
        virtual void InitializeNewInstance(int32 InstanceIndex, const FTransform& Transform);

        /**
         * Hook for subclasses to customize new instance initialization.
         * Called after state is initialized but before spatial index update.
         */
        virtual void OnInstanceAdded(int32 InstanceIndex, const FTransform& Transform);

        
    

    // ===== Spatial Queries =====
    
#pragma region SPATIAL_QUERIES
public:
    
    /** Find instances within radius of a location */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    TArray<int32> GetInstancesInRadius(const FVector& Location, float Radius, bool bIncludeDestroyed = false) const;

    /** Find instances within a box */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    TArray<int32> GetInstancesInBox(const FBox& Box, bool bIncludeDestroyed = false) const;

    /** Find the nearest instance to a location */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    int32 GetNearestInstance(const FVector& Location, float MaxDistance = -1.0f, bool bIncludeDestroyed = false) const;

    /** Query instances with advanced filter */
    TArray<int32> QueryInstances(const FVector& Location, float Radius, const FISMQueryFilter& Filter) const;

    // ===== State Management (IISMStateProvider) =====

    virtual uint8 GetInstanceStateFlags(int32 InstanceIndex) const override;
    virtual bool IsInstanceInState(int32 InstanceIndex, EISMInstanceState State) const override;
    virtual void SetInstanceState(int32 InstanceIndex, EISMInstanceState State, bool bValue) override;
    virtual const FISMInstanceState* GetInstanceState(int32 InstanceIndex) const override;

    /** Get mutable instance state (non-const version) */
    FISMInstanceState* GetInstanceStateMutable(int32 InstanceIndex);

#pragma endregion

#pragma region AABB_QUERIES

    // Get world AABB for a specific instance. Returns invalid box if opt-out or index invalid.
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|AABB")
    FBox GetInstanceWorldBounds(int32 InstanceIndex) const;

    // Find all instances whose AABB overlaps the given box.
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|AABB")
    TArray<int32> GetInstancesOverlappingBox(const FBox& Box, bool bIncludeDestroyed = false) const;

    // Find all instances whose AABB overlaps the given sphere (center + radius).
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|AABB")
    TArray<int32> GetInstancesOverlappingSphere(const FVector& Center, float Radius, bool bIncludeDestroyed = false) const;

    // Find all instances whose AABB overlaps another instance's AABB.
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|AABB")
    TArray<int32> GetInstancesOverlappingInstance(int32 InstanceIndex, bool bIncludeDestroyed = false) const;

    // Check if two specific instances overlap each other.
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|AABB")
    bool DoInstancesOverlap(int32 IndexA, int32 IndexB) const;

    // Check if a specific instance overlaps a given world box.
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|AABB")
    bool DoesInstanceOverlapBox(int32 InstanceIndex, const FBox& Box) const;

private:
        // Recomputes and stores WorldBounds in state. No-op if bComputeInstanceAABBs = false.
        void UpdateInstanceWorldBounds(int32 InstanceIndex, const FTransform& Transform);

#pragma endregion

    // ===== Conversion Tracking =====

#pragma region CONVERSION_TRACKING

public:
                    /** Get a handle for an instance */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    FISMInstanceHandle GetInstanceHandle(int32 InstanceIndex);

    /** Get all currently converted instances */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    TArray<FISMInstanceHandle> GetConvertedInstances() const;

    /** Return all converted instances back to ISM */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    void ReturnAllConvertedInstances(bool bDestroyActors = true, bool bUpdateTransforms = true);

    /** Check if an instance is currently converted */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    bool IsInstanceConverted(int32 InstanceIndex) const;

#pragma endregion
    
    // ===== Custom Data =====

    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Custom Data")
    TArray<float> GetInstanceCustomData(int32 InstanceIndex) const;

    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Custom Data")
    void SetInstanceCustomData(int32 InstanceIndex, const TArray<float>& CustomData);

    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Custom Data")
    float GetInstanceCustomDataValue(int32 InstanceIndex, int32 DataIndex) const;

    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Custom Data")
    void SetInstanceCustomDataValue(int32 InstanceIndex, int32 DataIndex, float Value);

    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Custom Data")
    void SetCustomDataCount(int32 DesiredCount, bool bResetExisting = false, float DefaultValue = 1.0);

    // ===== Events =====
#pragma region EVENTS

    /** Called when instance state changes */
    UPROPERTY(BlueprintAssignable, Category = "ISM Runtime|Events")
    FOnInstanceStateChanged OnInstanceStateChanged;

    /** Called when instance is destroyed */
    UPROPERTY(BlueprintAssignable, Category = "ISM Runtime|Events")
    FOnInstanceDestroyed OnInstanceDestroyed;

    /** Called when instance tags change */
    UPROPERTY(BlueprintAssignable, Category = "ISM Runtime|Events")
    FOnInstanceTagsChanged OnInstanceTagsChanged;

    /** Native C++ delegates (no Blueprint overhead) */
    FOnInstanceStateChangedNative OnInstanceStateChangedNative;
    FOnInstanceDestroyedNative OnInstanceDestroyedNative;


    /** Called when releasing a converted actor back to ISM (allows pooling) */
    FOnReleaseConvertedActor OnReleaseConvertedActor;

    /** Called when an instance is converted to an actor */
    FOnInstanceConverted OnInstanceConvertedToActor;

    /** Called when an instance is returned from actor to ISM */
    FOnInstanceReturnedToISM OnInstanceReturnedToISM;

#pragma endregion


    
    // ===== Performance Settings =====
    
    UPROPERTY(EditAnywhere, Category = "ISM Runtime|Performance")
    bool bEnableTickOptimization = true;
    
    UPROPERTY(EditAnywhere, Category = "ISM Runtime|Performance", meta=(EditCondition="bEnableTickOptimization"))
    float TickInterval = 0.0f; // 0 = every frame


    // ===== Accessors =====
	bool IsISMInitialized() const { return bIsInitialized; }


    void SetInstanceDataAsset(UISMInstanceDataAsset* NewDataAsset)
    {
		InstanceData = NewDataAsset;
        if(InstanceData)
        {
            OnInstanceDataAssigned();
		}
    }
    
protected:
    // ===== Subclass Hooks =====

#pragma region SUBCLASS_HOOKS

    virtual void OnInstanceDataAssigned() {}

/** Override to add component-specific tags during initialization */
    virtual void BuildComponentTags();

    /** Called after initialization is complete */
    virtual void OnInitializationComplete();

    /** Called when an instance is about to be destroyed */
    virtual void OnInstancePreDestroy(int32 InstanceIndex);

    /** Called after an instance has been destroyed */
    virtual void OnInstancePostDestroy(int32 InstanceIndex);

#pragma endregion

    
    // ===== Subsystem Integration =====
    
#pragma region SUBSYSTEM_INTEGRATION

                    /** Register this component with the runtime subsystem */
    virtual bool RegisterWithSubsystem();

    /** Unregister this component from the runtime subsystem */
    virtual void UnregisterFromSubsystem();

#pragma endregion

#pragma region INTERNAL_STATE
    
    // ===== Internal State =====

/** Spatial index for fast queries */
    FISMSpatialIndex SpatialIndex;

    /** Per-instance state (sparse map - only active instances) */
    UPROPERTY()
    TMap<int32, FISMInstanceState> InstanceStates;

    /** Cached subsystem reference */
    TWeakObjectPtr<class UISMRuntimeSubsystem> CachedSubsystem;

    /** Whether this component has been initialized */
    bool bIsInitialized = false;

    /** Time accumulator for tick interval */
    float TimeSinceLastTick = 0.0f;

    /** Map of instance index to handle (for tracking conversions) */
    TMap<int32, FISMInstanceHandle> InstanceHandles;
#pragma endregion


    
    // ===== Helper Functions =====
    
    /** Get effective tags for an instance (component + per-instance) */
    FGameplayTagContainer GetEffectiveTagsForInstance(int32 InstanceIndex) const;
    
    /** Validate instance index */
    
    
    /** Broadcast state change event */
    void BroadcastStateChange(int32 InstanceIndex);
    
    /** Broadcast destruction event */
    void BroadcastDestruction(int32 InstanceIndex);
    
    /** Broadcast tag change event */
    void BroadcastTagChange(int32 InstanceIndex);


    /** Get or create a handle for an instance */
    FISMInstanceHandle& GetOrCreateHandle(int32 InstanceIndex);



    // ===== Bounds Management =====

#pragma region BOUNDS

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime|Performance")
    bool bComputeInstanceAABBs = true;

    /**
     * Recalculate bounds from all active instances.
     * O(n) operation - use sparingly!
     * Called automatically during initialization.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Bounds")
    void RecalculateInstanceBounds();

    /**
     * Incrementally expand bounds to include a new location.
     * O(1) operation - safe to call frequently.
     * Does NOT shrink bounds if location is inside existing bounds.
     */
    void ExpandBoundsToInclude(const FVector& Location);

    /**
     * Invalidate bounds cache, forcing recalculation on next query.
     * Use when you know bounds have changed but don't want to recalculate immediately.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Bounds")
    void InvalidateBounds() { bBoundsValid = false; }


    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Bounds")
    bool IsBoundsValid() const { return bBoundsValid; }

protected:
    /** Cached bounds of all active instances */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Runtime|Bounds")
    FBox CachedInstanceBounds;

    /** Whether cached bounds are currently valid */
    bool bBoundsValid = false;

    /** Padding to add to bounds (accounts for instance size/scale) */
    UPROPERTY(EditAnywhere, Category = "ISM Runtime|Performance", meta = (ClampMin = "0.0"))
    float BoundsPadding = 100.0f;

    /**
     * Check if a location is on the edge of the bounds.
     * Used to determine if removing an instance requires full bounds recalculation.
     */
    bool IsLocationOnBoundsEdge(const FVector& Location, float Tolerance = 10.0f) const;
#pragma endregion



    
public:
    /**
     * Whether this component is currently held by an open batch mutation handle.
     * While locked, destroyed instance slots are not recycled - their indices are
     * reserved until the handle is released.
     */
    bool IsBatchLocked() const { return bBatchLocked; }
	bool SetBatchLocked(bool bLocked) 
    {
        bBatchLocked = bLocked;
        return true;
    }
private:
    bool bBatchLocked = false;





#pragma region FEEDBACKS

        // ===== Feedback Helpers ===== (NEW SECTION)
    public:

        /**
         * Get the effective feedback tags for this component.
         * Merges component defaults with instance data overrides.
         */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Feedback")
        FISMFeedbackTags GetEffectiveFeedbackTags() const;

        /**
         * Manually trigger feedback for an instance.
         * Useful for custom events or testing.
         *
         * @param InstanceIndex The instance to trigger feedback for
         * @param FeedbackTag The feedback tag to trigger
         * @param Intensity Feedback intensity (0-1)
         */
        UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Feedback")
        bool TriggerInstanceFeedback(int32 InstanceIndex, FGameplayTag FeedbackTag, float Intensity = 1.0f);

protected:
    // ===== Feedback Implementation ===== (NEW SECTION)

    /**
     * Internal helper to trigger feedback for an instance.
     * Called by lifecycle methods when bTriggerFeedback=true.
     */
    bool TriggerFeedbackInternal(int32 InstanceIndex, FGameplayTag FeedbackTag, float Intensity = 1.0f);

    /**
     * Get the feedback subsystem (cached for performance).
     */
    class UISMFeedbackSubsystem* GetFeedbackSubsystem() const;

    /** Cached feedback subsystem reference */
    mutable TWeakObjectPtr<class UISMFeedbackSubsystem> CachedFeedbackSubsystem;

    private:
        void TriggerFeedbackOnDestroyInternal(int InstanceIndex, const UActorComponent* Instigator);
        void TriggerFeedbackOnSpawnInternal(int InstanceIndex, const UActorComponent* Instigator);
        void TriggerFeedbackOnHideInternal(int InstanceIndex, const UActorComponent* Instigator);
        void TriggerFeedbackOnShowInternal(int InstanceIndex, const UActorComponent* Instigator);
        void TriggerFeedbackOnTransformUpdateInternal(int InstanceIndex, const UActorComponent* Instigator);
        void TriggerFeedbackBatchedOnSpawnInternal(TArray<int> InstanceIndexes, const UActorComponent* Instigator);
        void TriggerFeedbackBatchedOnDestroyInternal(TArray<int> InstanceIndexes, const UActorComponent* Instigator);

        void TriggerFeedbackInternal(TFunctionRef<FGameplayTag(const FISMFeedbackTags&)> SelectTag, int InstanceIndex, const UActorComponent* Instigator);
        void TriggerFeedbackBatchedInternal(TFunctionRef<FGameplayTag(const FISMFeedbackTags&)> SelectTag, TArray<int> InstanceIndexes, const UActorComponent* Instigator);

#pragma endregion

};