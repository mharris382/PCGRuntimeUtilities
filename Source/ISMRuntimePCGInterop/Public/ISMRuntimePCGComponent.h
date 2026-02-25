//// ISMRuntimePCGComponent.h
//#pragma once
//#include "CoreMinimal.h"
//#include "Components/ActorComponent.h"
//#include "ISMPCGDataChannel.h"
//#include "ISMPCGAttributeSchema.h"
//#include "GameplayTagContainer.h"
//#include "ISMRuntimePCGComponent.generated.h"
//
//class UPCGGraphInterface;
//class UISMRuntimeComponent;
//
//UENUM(BlueprintType)
//enum class EISMPCGExecutionMode : uint8
//{
//    /**
//     * Run graph once at BeginPlay (or after PCG generation completes).
//     * Results baked into ISM state. No further execution.
//     */
//    Precompute,
//
//    /**
//     * Run graph periodically during gameplay.
//     * Interval controlled by ExecutionInterval.
//     */
//    Periodic,
//
//    /**
//     * Only run when explicitly triggered via TriggerExecution().
//     * Caller controls when the graph fires.
//     */
//    OnDemand,
//
//    /**
//     * Run graph when any instance in the query radius changes state.
//     * Reactive pattern.
//     */
//    OnStateChange,
//};
//
//DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPCGGraphExecuted,
//    UISMRuntimePCGComponent*, Component,
//    const FISMPCGDataPacket&, ResultPacket);
//
///**
// * Actor component that attaches a PCG graph execution pipeline
// * to any actor that has ISMRuntimeComponents.
// *
// * This component is the "glue" - it knows which ISM Runtime components
// * to read from, which PCG graph to run, and what to do with the results.
// *
// * Other modules (physics, animation, etc.) attach this component
// * when they want PCG-driven behavior. They don't need to know anything
// * about how PCG interop works.
// */
//UCLASS(Blueprintable, ClassGroup=(ISMRuntime), meta=(BlueprintSpawnableComponent))
//class ISMRUNTIMEPCGINTEROP_API UISMRuntimePCGComponent : public UActorComponent
//{
//    GENERATED_BODY()
//
//public:
//    UISMRuntimePCGComponent();
//
//    // ===== Configuration =====
//
//    /** The PCG graph to execute */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Graph")
//    UPCGGraphInterface* PCGGraph = nullptr;
//
//    /** How the graph should be executed */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Execution")
//    EISMPCGExecutionMode ExecutionMode = EISMPCGExecutionMode::OnDemand;
//
//    /**
//     * For Periodic mode: how often to run the graph (seconds).
//     * Staggered across components to avoid frame spikes.
//     */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Execution",
//        meta=(EditCondition="ExecutionMode == EISMPCGExecutionMode::Periodic", ClampMin="0.1"))
//    float ExecutionInterval = 1.0f;
//
//    /** Attribute schema for translating between PCG and ISM */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Translation")
//    UISMPCGAttributeSchema* AttributeSchema = nullptr;
//
//    /**
//     * Specific ISM Runtime components to read from.
//     * If empty, auto-discovers all ISMRuntimeComponents on owning actor.
//     */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Sources")
//    TArray<UISMRuntimeComponent*> SourceComponents;
//
//    /**
//     * Tag filter: only process instances with these tags.
//     * Empty = all instances.
//     */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Sources")
//    FGameplayTagContainer InstanceTagFilter;
//
//    /**
//     * Spatial filter: only process instances within this radius of this component's owner.
//     * -1 = no filter.
//     */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Sources")
//    float SpatialFilterRadius = -1.0f;
//
//    /**
//     * What to do with PCG results.
//     * Callers can override these or subscribe to OnGraphExecuted
//     * to handle results manually.
//     */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Results")
//    bool bAutoApplyTransforms = false;
//
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Results")
//    bool bAutoApplyStates = true;
//
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Results")
//    bool bAutoApplyTags = true;
//
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG|Results")
//    bool bAutoApplyCustomData = true;
//
//    // ===== API =====
//
//    /**
//     * Manually trigger graph execution.
//     * Always works regardless of ExecutionMode.
//     * Async by default - subscribe to OnGraphExecuted for results.
//     */
//    UFUNCTION(BlueprintCallable, Category = "ISM PCG")
//    void TriggerExecution();
//
//    /**
//     * Synchronous version of TriggerExecution.
//     * Returns the result packet directly.
//     * WARNING: Blocks game thread. Use only for precompute.
//     */
//    UFUNCTION(BlueprintCallable, Category = "ISM PCG")
//    FISMPCGDataPacket TriggerExecutionSync();
//
//    /**
//     * Get the last result packet from the most recent execution.
//     */
//    UFUNCTION(BlueprintCallable, Category = "ISM PCG")
//    const FISMPCGDataPacket& GetLastResultPacket() const { return LastResultPacket; }
//
//    // ===== Events =====
//
//    /** Fires when graph execution completes (on game thread) */
//    UPROPERTY(BlueprintAssignable, Category = "ISM PCG|Events")
//    FOnPCGGraphExecuted OnGraphExecuted;
//
//    // ===== Lifecycle =====
//
//    virtual void BeginPlay() override;
//    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
//        FActorComponentTickFunction* ThisTickFunction) override;
//
//protected:
//    /** Cached result of last execution */
//    FISMPCGDataPacket LastResultPacket;
//
//    /** Time accumulator for Periodic mode */
//    float TimeSinceLastExecution = 0.0f;
//
//    /** Resolved source components (after auto-discovery) */
//    TArray<TWeakObjectPtr<UISMRuntimeComponent>> ResolvedSources;
//
//    void ResolveSourceComponents();
//    void ExecuteInternal(bool bSync);
//    void OnGraphComplete(const FISMPCGDataPacket& Result);
//
//    FISMQueryFilter BuildQueryFilter() const;
//};


/*
### Module Structure Summary
```
ISMRuntimePCGInterop/
├── ISMRuntimePCGInterop.Build.cs
│     Depends on: ISMRuntimeCore, PCGFramework (PCG module)
│
├── Public/
│   ├── ISMPCGDataChannel.h          FISMPCGInstancePoint, FISMPCGDataPacket
│   ├── ISMPCGAttributeSchema.h      UISMPCGAttributeSchema (data asset)
│   ├── ISMPCGBridge.h               UISMPCGBridge (static translation API)
│   ├── ISMRuntimePCGComponent.h     UISMRuntimePCGComponent (actor component)
│   └── PCGElements/
│       ├── PCGElement_ISMInput.h    PCG source node: instances → PCG points
│       └── PCGElement_ISMOutput.h   PCG sink node: PCG points → ISM changes
│
└── Private/
    ├── ISMPCGBridge.cpp
    ├── ISMRuntimePCGComponent.cpp
    └── PCGElements/
        ├── PCGElement_ISMInput.cpp
        └── PCGElement_ISMOutput.cpp

        */