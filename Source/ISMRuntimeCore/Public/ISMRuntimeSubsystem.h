#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceHandle.h"
#include "ISMQueryFilter.h"
#include "CollisionQueryParams.h"
#include "ISMTraceResult.h"
#include "ISMRuntimeSubsystem.generated.h"

// Forward declarations
class UISMRuntimeComponent;
class UISMBatchSchedulerBase;





/**
 * Statistics for runtime monitoring
 */
USTRUCT(BlueprintType)
struct FISMRuntimeStats
{
    GENERATED_BODY()
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 RegisteredComponentCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TotalInstanceCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 ActiveInstanceCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 DestroyedInstanceCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float LastFrameProcessingTimeMs = 0.0f;
};


USTRUCT(BlueprintType)
struct FISMComponentMapping
{
    GENERATED_BODY()

    /** The ISM component to manage */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    UInstancedStaticMeshComponent* ISMComponent = nullptr;

    /** The runtime component class to use for this ISM */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    TSubclassOf<UISMRuntimeComponent> RuntimeComponentClass;

    /** Optional: Override spatial index cell size for this component */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime", meta = (ClampMin = "100.0"))
    float OverrideCellSize = 0.0f;

    /**
     * Primitive components that act as collision proxies for this ISM.
     * Traces hitting any of these will redirect to this ISM for instance resolution.
     * Use when the ISM itself has no per-instance collision — a single box collider
     * representing a crate pile, a vehicle body containing ISM cargo, etc.
     * Registered with the subsystem automatically on BeginPlay.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    TArray<UPrimitiveComponent*> CollisionProxies;
};



/**
 * World subsystem that coordinates all ISM runtime components.
 * Provides global queries, frame budget management, and component registration.
 */
UCLASS()
class ISMRUNTIMECORE_API UISMRuntimeSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
    
public:
    // ===== Subsystem Lifecycle =====
    
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

    // FTickableGameObject - required pure virtual
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UISMRuntimeSubsystem, STATGROUP_Tickables);
    }

    virtual bool IsTickable() const override;

    // The actual tick - drives the scheduler
    virtual void Tick(float DeltaTime) override;
    
	UISMBatchSchedulerBase* GetBatchScheduler() const { return BatchScheduler; }
    UISMBatchSchedulerBase* GetOrCreateBatchSchduler();

    UISMBatchSchedulerBase* BatchScheduler = nullptr;
	bool bBatchSchedulerInitialized = false;

    // ===== Component Registration =====
    
    /** Register a runtime component with this subsystem */
    bool RegisterRuntimeComponent(UISMRuntimeComponent* Component);
    
    /** Unregister a runtime component from this subsystem */
    void UnregisterRuntimeComponent(UISMRuntimeComponent* Component);
    
    /** Get all registered components */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    TArray<UISMRuntimeComponent*> GetAllComponents() const;
    
    /** Get components with specific tag */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    TArray<UISMRuntimeComponent*> GetComponentsWithTag(FGameplayTag Tag) const;
    
    /** Get components implementing a specific interface */
/*    template<typename InterfaceType>
    TArray<UISMRuntimeComponent*> GetComponentsWithInterface() const;*/


    /** 
     * If a runtime component already exists for this ISM, calls the callback immediately.
     * If not, registers the callback to fire when one is created.
     * Safe to call before or after ISMRuntimeActor::BeginPlay.
     */
    void RequestRuntimeComponent(UInstancedStaticMeshComponent* ISM,TFunction<void(UISMRuntimeComponent*)> Callback);
    
    // ===== Global Queries =====
    
    /** Query instances across all components within radius */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    TArray<FISMInstanceHandle> QueryInstancesInRadius(
        const FVector& Location,
        float Radius,
        const FISMQueryFilter& Filter) const;
    
    /** Query instances across all components within box */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    TArray<FISMInstanceHandle> QueryInstancesInBox(
        const FBox& Box,
        const FISMQueryFilter& Filter) const;
    
    /** Find nearest instance across all components */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    FISMInstanceHandle FindNearestInstance(
        const FVector& Location,
        const FISMQueryFilter& Filter,
        float MaxDistance = -1.0f) const;


    /** Find all instances across all components whose AABB overlaps the given box */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    TArray<FISMInstanceHandle> QueryInstancesOverlappingBox(
        const FBox& Box,
        const FISMQueryFilter& Filter) const;

    /** Find all instances whose AABB overlaps a given instance's AABB (cross-component) */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    TArray<FISMInstanceHandle> QueryInstancesOverlappingInstance(
        const FISMInstanceHandle& Handle,
        const FISMQueryFilter& Filter) const;
    
    /** Find component that owns a specific instance */
    UISMRuntimeComponent* FindComponentForInstance(const FISMInstanceReference& Instance) const;
    
    // ===== Statistics =====
    
    /** Get current runtime statistics */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    FISMRuntimeStats GetRuntimeStats() const;
    
    /** Refresh statistics (called automatically each frame) */
    void UpdateStatistics();
    
	UISMRuntimeComponent* FindComponentForISM(TWeakObjectPtr<UInstancedStaticMeshComponent> ISM) const;

protected:
    // ===== Component Storage =====
    
    /** All registered components */
    UPROPERTY()
    TArray<TWeakObjectPtr<UISMRuntimeComponent>> AllComponents;
    
    /** Components indexed by tag for fast lookup */
    TMap<FGameplayTag, TArray<TWeakObjectPtr<UISMRuntimeComponent>>> ComponentsByTag;
    
    // Populated by RegisterRuntimeComponent - fast lookup for RequestRuntimeComponent
    TMap<TWeakObjectPtr<UInstancedStaticMeshComponent>, TWeakObjectPtr<UISMRuntimeComponent>> ISMToRuntimeComponentMap;
    
    TMap<TWeakObjectPtr<UInstancedStaticMeshComponent>,TArray<TFunction<void(UISMRuntimeComponent*)>>> PendingRuntimeComponentCallbacks;

    /** Cached statistics */
    FISMRuntimeStats CachedStats;
    
    /** Frame number when stats were last updated */
    uint32 StatsUpdateFrame = 0;
    
    // ===== Helper Functions =====
    
    /** Clean up invalid component references */
    void CleanupInvalidComponents();
    
    /** Rebuild tag index for a component */
    void RebuildTagIndexForComponent(UISMRuntimeComponent* Component);

    /**
     * Called at the end of RegisterRuntimeComponent.
     * Checks PendingRuntimeComponentCallbacks for this ISM and fires any waiting callbacks.
     */
    void FirePendingCallbacksForISM(UInstancedStaticMeshComponent* ISM,UISMRuntimeComponent* RuntimeComponent);


    public:
    // ===== Component Redirect Registry =====

                  /**
                  * Register a primitive component as a redirect source for one or more
                  * ISM runtime components. When a trace hits this primitive, the subsystem
                  * will resolve candidates from the registered ISM components instead.
                  *
                  * Use case: a single collision box representing a cluster of ISM instances,
                  * a mesh collider for a foliage group, a vehicle body containing ISM cargo.
                  * 
				  * returns instance if registered successfully, or INDEX_NONE if the component is already registered as a redirect source
                  */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Redirect")
    void RegisterComponentRedirect(
        UPrimitiveComponent* PhysicsComponent,
        UISMRuntimeComponent* ISMComponent);

    /** Remove a specific ISM from a redirect source */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Redirect")
    void UnregisterComponentRedirect(
        UPrimitiveComponent* PhysicsComponent,
        UISMRuntimeComponent* ISMComponent);

    /** Remove all redirects for a physics component */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Redirect")
    void UnregisterAllRedirectsForComponent(UPrimitiveComponent* PhysicsComponent);

    private:
    // Private — managed entirely through the register/unregister API
    TMap<TWeakObjectPtr<UPrimitiveComponent>, TArray<TWeakObjectPtr<UISMRuntimeComponent>>> ComponentRedirectMap;

#pragma region ISM_AWARE_TRACES
               public:
                   // ===== ISM-Aware Traces =====
     
               /**
               * Line trace that resolves ISM instance handles directly.
               * Falls back to component redirect map if the hit component
               * is not an ISM but has been registered as a redirect source.
               */
                   bool LineTraceISM(const FVector& Start,
                       const FVector& End,
                       ECollisionChannel TraceChannel, 
                       FISMTraceResult& OutResult,
                       const FISMQueryFilter& Filter,
                       const FCollisionQueryParams& Params,
                       float RedirectSearchRadius) const;
                   
                   ///** Multi-trace variant — returns all hits sorted by distance */
                   bool LineTraceISMMulti(
                       const FVector& Start,
                       const FVector& End,
                       ECollisionChannel TraceChannel,
                       TArray<FISMTraceResult>& OutResults,
                       const FISMQueryFilter& Filter,
                       const FCollisionQueryParams& Params,
                       float RedirectSearchRadius) const;
     
                   /** Sphere sweep variant */
                   bool SweepISM(const FVector& Start,const FVector& End,float Radius, ECollisionChannel TraceChannel, struct FISMTraceResult& OutResult, const FISMQueryFilter& Filter,
                       const FCollisionQueryParams& Params,
                       float RedirectSearchRadius) const;
     
                   UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Trace")
                   bool LineTraceISM(const FVector& Start,
                       const FVector& End,
                       ECollisionChannel TraceChannel,
                       FISMTraceResult& OutResult,
                       const FISMQueryFilter& Filter,
                       float RedirectSearchRadius) const;

                   ///** Multi-trace variant — returns all hits sorted by distance */
                   UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Trace")
                   bool LineTraceISMMulti(
                       const FVector& Start,
                       const FVector& End,
                       ECollisionChannel TraceChannel,
                       TArray<FISMTraceResult>& OutResults,
                       const FISMQueryFilter& Filter,
                       float RedirectSearchRadius) const;

                   /** Sphere sweep variant */
                   UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Trace")
                   bool SweepISM(const FVector& Start, const FVector& End, float Radius, ECollisionChannel TraceChannel, struct FISMTraceResult& OutResult, const FISMQueryFilter& Filter,
                       float RedirectSearchRadius) const;
                    
              protected:
                  
                  /**
                   * Resolve a physics hit result to an ISM instance handle.
                   * Tries direct resolution first, then redirect lookup.
                   * Returns invalid handle if neither succeeds.
                   */
                  FISMTraceResult ResolveHitToISMHandle(
                      const FHitResult& Hit,
                      const FISMQueryFilter& Filter,
                      float RedirectSearchRadius) const;

                  /**
                   * Resolve redirect: given a hit on a proxy component, find the nearest
                   * ISM instance within RedirectSearchRadius of the impact point.
                   */
                  FISMTraceResult ResolveRedirectHit(
                      const FHitResult& Hit,
                      const TArray<TWeakObjectPtr<UISMRuntimeComponent>>& Candidates,
                      const FISMQueryFilter& Filter,
                      float RedirectSearchRadius) const;

                  /** Prune stale entries from ComponentRedirectMap and ISMToRuntimeMap */
                  void CleanupRedirectMap();





















#pragma endregion


                private:

					void InitializeBatchScheduler();
                  
};
/*
// Template implementation
template<typename InterfaceType>
TArray<UISMRuntimeComponent*> UISMRuntimeSubsystem::GetComponentsWithInterface() const
{
    TArray<UISMRuntimeComponent*> Result;
    
    for (const TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : AllComponents)
    {
        if (UISMRuntimeComponent* Comp = CompPtr.Get())
        {
            if (Comp->GetClass()->ImplementsInterface(InterfaceType::StaticClass()))
            {
                Result.Add(Comp);
            }
        }
    }
    
    return Result;
}*/