#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ISMSpatialIndex.h"
#include "ISMInstanceHandle.h"
#include "Delegates/DelegateCombinations.h"
#include "Interfaces/ISMStateProvider.h"
#include "ISMRuntimeComponent.generated.h"

// Forward declarations
class UInstancedStaticMeshComponent;
class UISMInstanceDataAsset;

struct FISMQueryFilter;
struct FISMInstanceState;

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
UCLASS(Abstract, Blueprintable, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMECORE_API UISMRuntimeComponent : public UActorComponent, public IISMStateProvider
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

    // ===== Instance Management =====
    
#pragma region INSTANCE_MANAGEMENT

                    /** Initialize all instances (called automatically on BeginPlay) */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    virtual void InitializeInstances();

    /** Destroy an instance (hides it and marks as destroyed, but index remains valid) */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    virtual void DestroyInstance(int32 InstanceIndex);

    /** Hide an instance without destroying it */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    virtual void HideInstance(int32 InstanceIndex);

    /** Show a previously hidden instance */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    virtual void ShowInstance(int32 InstanceIndex);

    /** Check if an instance is destroyed */
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

    /** Update instance transform */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    virtual void UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewTransform, bool bUpdateSpatialIndex = true);
#pragma endregion

    
    // ===== Spatial Queries =====
    
#pragma region SPATIAL_QUERIES
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


    // ===== Conversion Tracking =====

#pragma region CONVERSION_TRACKING

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
    
protected:
    // ===== Subclass Hooks =====

#pragma region SUBCLASS_HOOKS

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
    virtual void RegisterWithSubsystem();

    /** Unregister this component from the runtime subsystem */
    virtual void UnregisterFromSubsystem();

#pragma endregion

    
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

    
    // ===== Helper Functions =====
    
    /** Get effective tags for an instance (component + per-instance) */
    FGameplayTagContainer GetEffectiveTagsForInstance(int32 InstanceIndex) const;
    
    /** Validate instance index */
    bool IsValidInstanceIndex(int32 InstanceIndex) const;
    
    /** Broadcast state change event */
    void BroadcastStateChange(int32 InstanceIndex);
    
    /** Broadcast destruction event */
    void BroadcastDestruction(int32 InstanceIndex);
    
    /** Broadcast tag change event */
    void BroadcastTagChange(int32 InstanceIndex);



    /** Get or create a handle for an instance */
    FISMInstanceHandle& GetOrCreateHandle(int32 InstanceIndex);
};