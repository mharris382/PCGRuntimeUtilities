#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ISMResourceDebugComponent.generated.h"

/**
 * Debug component for visualizing ISMResource system state.
 * Attach to collector or resource actors for real-time debugging.
 * 
 * Usage:
 * 1. Add to your character or resource actor
 * 2. Enable desired debug visualizations
 * 3. Check logs for detailed event tracking
 */
UCLASS(ClassGroup=(Debug), meta=(BlueprintSpawnableComponent))
class ISMRUNTIMERESOURCE_API UISMResourceDebugComponent : public UActorComponent
{
    GENERATED_BODY()
    
public:
    UISMResourceDebugComponent();
    
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void BeginPlay() override;
    
    // ===== Debug Visualization Settings =====
    
    /** Show raycast line for collector detection */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Collector")
    bool bShowRaycast = true;
    
    /** Show detection radius sphere */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Collector")
    bool bShowDetectionRadius = true;
    
    /** Show currently targeted instance */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Collector")
    bool bShowTargetedInstance = true;
    
    /** Show all instances managed by resource components */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Resource")
    bool bShowAllInstances = false;
    
    /** Show only active (collectable) instances */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Resource")
    bool bShowActiveInstances = true;
    
    /** Show collection progress */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Collection")
    bool bShowCollectionProgress = true;
    
    /** Show collector tags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Tags")
    bool bShowCollectorTags = true;
    
    /** Show resource requirements */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Tags")
    bool bShowResourceRequirements = true;
    
    // ===== Logging Settings =====
    
    /** Log detection events */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Logging")
    bool bLogDetection = true;
    
    /** Log collection events */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Logging")
    bool bLogCollection = true;
    
    /** Log validation failures */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Logging")
    bool bLogValidation = true;
    
    /** Log tag queries */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Logging")
    bool bLogTags = true;
    
    // ===== Visual Settings =====
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Visual")
    float DebugLineThickness = 2.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Visual")
    FColor RaycastColor = FColor::Yellow;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Visual")
    FColor TargetedColor = FColor::Green;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Visual")
    FColor ActiveInstanceColor = FColor::Cyan;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Visual")
    FColor CollectingColor = FColor::Orange;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug|Visual")
    FColor InvalidColor = FColor::Red;
    
    // ===== Manual Commands =====
    
    /** Print collector state to log */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void PrintCollectorState();
    
    /** Print resource component state to log */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void PrintResourceComponentState();
    
    /** Print all nearby resources */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void PrintNearbyResources(float Radius = 1000.0f);
    
protected:
    /** Auto-detected collector component */
    TWeakObjectPtr<class UISMCollectorComponent> CachedCollector;
    
    /** Auto-detected resource components */
    TArray<TWeakObjectPtr<class UISMResourceComponent>> CachedResourceComponents;
    
    /** Find components on owner */
    void FindComponents();
    
    /** Draw collector debug info */
    void DrawCollectorDebug();
    
    /** Draw resource debug info */
    void DrawResourceDebug();
    
    /** Draw collection progress */
    void DrawCollectionProgress();
    
    /** Bind to collector events */
    void BindCollectorEvents();
    
    /** Bind to resource events */
    void BindResourceEvents();
    
    // ===== Event Handlers =====
    
    UFUNCTION()
    void OnTargetChanged(const struct FISMInstanceHandle& NewTarget, class UISMResourceComponent* ResourceComponent);
    
    UFUNCTION()
    void OnCollectionStarted(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent);
    
    UFUNCTION()
    void OnCollectionProgress(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent, float Progress);
    
    UFUNCTION()
    void OnCollectionCompleted(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent);
    
    UFUNCTION()
    void OnCollectionFailed(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent, FText FailureReason);
};
