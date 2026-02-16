#include "ISMResourceDebugComponent.h"
#include "ISMCollectorComponent.h"
#include "ISMResourceComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UISMResourceDebugComponent::UISMResourceDebugComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UISMResourceDebugComponent::BeginPlay()
{
    Super::BeginPlay();
    
    FindComponents();
    BindCollectorEvents();
    BindResourceEvents();
    
    UE_LOG(LogTemp, Warning, TEXT("=== ISMResource Debug Component Active on %s ==="), *GetOwner()->GetName());
    
    if (CachedCollector.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("  - Collector Found: Detection Mode = %d"), (int32)CachedCollector->DetectionMode);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("  - No Collector Component Found"));
    }
    
    if (CachedResourceComponents.Num() > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("  - Resource Components Found: %d"), CachedResourceComponents.Num());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("  - No Resource Components Found"));
    }
}

void UISMResourceDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    if (CachedCollector.IsValid())
    {
        DrawCollectorDebug();
        DrawCollectionProgress();
    }
    
    if (CachedResourceComponents.Num() > 0)
    {
        DrawResourceDebug();
    }
}

void UISMResourceDebugComponent::FindComponents()
{
    AActor* Owner = GetOwner();
    if (!Owner)
        return;
    
    // Find collector component
    CachedCollector = Owner->FindComponentByClass<UISMCollectorComponent>();
    
    // Find resource components
    TArray<UISMResourceComponent*> ResourceComps;
    Owner->GetComponents<UISMResourceComponent>(ResourceComps);
    
    for (UISMResourceComponent* Comp : ResourceComps)
    {
        CachedResourceComponents.Add(Comp);
    }
}

void UISMResourceDebugComponent::DrawCollectorDebug()
{
    UISMCollectorComponent* Collector = CachedCollector.Get();
    if (!Collector)
        return;
    
    UWorld* World = GetWorld();
    if (!World)
        return;
    
    FVector OwnerLocation = GetOwner()->GetActorLocation();
    
    // Draw detection mode visualization
    if (Collector->DetectionMode == ECollectionDetectionMode::Raycast && bShowRaycast)
    {
        // Get raycast line
        FVector Start = OwnerLocation;
        FVector End = Start + (GetOwner()->GetActorForwardVector() * Collector->RaycastRange);
        
        // Try to get from controller viewpoint if available
        if (APawn* Pawn = Cast<APawn>(GetOwner()))
        {
            if (AController* Controller = Pawn->GetController())
            {
                FVector ViewLocation;
                FRotator ViewRotation;
                Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
                Start = ViewLocation;
                End = Start + (ViewRotation.Vector() * Collector->RaycastRange);
            }
        }
        
        DrawDebugLine(World, Start, End, RaycastColor, false, -1.0f, 0, DebugLineThickness);
        DrawDebugSphere(World, End, 10.0f, 8, RaycastColor, false, -1.0f, 0, DebugLineThickness);
    }
    
    if (Collector->DetectionMode == ECollectionDetectionMode::Radius && bShowDetectionRadius)
    {
        DrawDebugSphere(World, OwnerLocation, Collector->DetectionRadius, 16, RaycastColor, false, -1.0f, 0, DebugLineThickness);
    }
    
    // Draw targeted instance
    if (bShowTargetedInstance && Collector->HasValidTarget())
    {
        FISMInstanceHandle Target = Collector->GetTargetedInstance();
        UISMResourceComponent* ResourceComp = Collector->GetTargetedResourceComponent();
        
        if (ResourceComp && Target.IsValid())
        {
            FVector TargetLocation = ResourceComp->GetInstanceLocation(Target.InstanceIndex);
            DrawDebugSphere(World, TargetLocation, 50.0f, 12, TargetedColor, false, -1.0f, 0, DebugLineThickness * 2);
            DrawDebugLine(World, OwnerLocation, TargetLocation, TargetedColor, false, -1.0f, 0, DebugLineThickness);
            
            // Draw text with distance
            float Distance = FVector::Dist(OwnerLocation, TargetLocation);
            FString DebugText = FString::Printf(TEXT("TARGETED\nDist: %.0fcm\nIndex: %d"), Distance, Target.InstanceIndex);
            DrawDebugString(World, TargetLocation + FVector(0, 0, 100), DebugText, nullptr, TargetedColor, 0.0f, true);
        }
    }
    
    // Draw collector tags
    if (bShowCollectorTags)
    {
        FGameplayTagContainer Tags = Collector->GetCollectorTags();
        if (Tags.Num() > 0)
        {
            FString TagString = TEXT("Collector Tags:\n");
            for (const FGameplayTag& Tag : Tags)
            {
                TagString += Tag.ToString() + TEXT("\n");
            }
            DrawDebugString(World, OwnerLocation + FVector(0, 0, 150), TagString, nullptr, FColor::White, 0.0f, true);
        }
        else
        {
            DrawDebugString(World, OwnerLocation + FVector(0, 0, 150), TEXT("No Collector Tags"), nullptr, FColor::Red, 0.0f, true);
        }
    }
}

void UISMResourceDebugComponent::DrawResourceDebug()
{
    UWorld* World = GetWorld();
    if (!World)
        return;
    
    for (TWeakObjectPtr<UISMResourceComponent>& ResourceCompPtr : CachedResourceComponents)
    {
        UISMResourceComponent* ResourceComp = ResourceCompPtr.Get();
        if (!ResourceComp || !ResourceComp->ManagedISMComponent)
            continue;
        
        int32 InstanceCount = ResourceComp->GetInstanceCount();
        
        for (int32 i = 0; i < InstanceCount; ++i)
        {
            if (ResourceComp->IsInstanceDestroyed(i))
                continue;
            
            if (!bShowActiveInstances && ResourceComp->IsInstanceActive(i))
                continue;
            
            if (!bShowAllInstances && !ResourceComp->IsInstanceActive(i))
                continue;
            
            FVector Location = ResourceComp->GetInstanceLocation(i);
            FColor Color = ResourceComp->IsInstanceActive(i) ? ActiveInstanceColor : InvalidColor;
            
            // Check if being collected
            if (ResourceComp->IsInstanceBeingCollected(i))
            {
                Color = CollectingColor;
            }
            
            DrawDebugSphere(World, Location, 30.0f, 8, Color, false, -1.0f, 0, 1.0f);
            
            // Draw instance info
            FString InfoText = FString::Printf(TEXT("Index: %d\n%s"), 
                i, 
                ResourceComp->IsInstanceActive(i) ? TEXT("Active") : TEXT("Inactive"));
            
            if (ResourceComp->IsInstanceBeingCollected(i))
            {
                float Progress = ResourceComp->GetCollectionProgress(i);
                InfoText += FString::Printf(TEXT("\nCollecting: %.0f%%"), Progress * 100.0f);
            }
            
            DrawDebugString(World, Location + FVector(0, 0, 60), InfoText, nullptr, Color, 0.0f, true, 0.8f);
        }
        
        // Draw resource component info
        if (bShowResourceRequirements)
        {
            FVector CompLocation = ResourceComp->GetOwner()->GetActorLocation();
            FString InfoText = FString::Printf(TEXT("Resource Component\nInstances: %d\nActive: %d"), 
                InstanceCount,
                ResourceComp->GetActiveInstanceCount());
            
            if (!ResourceComp->ResourceTags.IsEmpty())
            {
                InfoText += TEXT("\nTags:\n");
                for (const FGameplayTag& Tag : ResourceComp->ResourceTags)
                {
                    InfoText += Tag.ToString() + TEXT("\n");
                }
            }
            
            DrawDebugString(World, CompLocation + FVector(0, 0, 200), InfoText, nullptr, FColor::Cyan, 0.0f, true);
        }
    }
}

void UISMResourceDebugComponent::DrawCollectionProgress()
{
    if (!bShowCollectionProgress)
        return;
    
    UISMCollectorComponent* Collector = CachedCollector.Get();
    if (!Collector || !Collector->IsCollecting())
        return;
    
    UWorld* World = GetWorld();
    if (!World)
        return;
    
    FISMInstanceHandle Target = Collector->GetTargetedInstance();
    UISMResourceComponent* ResourceComp = Collector->GetTargetedResourceComponent();
    
    if (!ResourceComp || !Target.IsValid())
        return;
    
    float Progress = Collector->GetCollectionProgress();
    FVector Location = ResourceComp->GetInstanceLocation(Target.InstanceIndex);
    
    // Draw progress bar above instance
    FVector BarStart = Location + FVector(-50, 0, 120);
    FVector BarEnd = Location + FVector(50, 0, 120);
    FVector ProgressEnd = FMath::Lerp(BarStart, BarEnd, Progress);
    
    // Background
    DrawDebugLine(World, BarStart, BarEnd, FColor::Black, false, -1.0f, 0, 5.0f);
    // Progress
    DrawDebugLine(World, BarStart, ProgressEnd, CollectingColor, false, -1.0f, 0, 4.0f);
    
    // Percentage text
    FString ProgressText = FString::Printf(TEXT("Collecting: %.0f%%"), Progress * 100.0f);
    DrawDebugString(World, Location + FVector(0, 0, 140), ProgressText, nullptr, CollectingColor, 0.0f, true, 1.2f);
}

void UISMResourceDebugComponent::BindCollectorEvents()
{
    UISMCollectorComponent* Collector = CachedCollector.Get();
    if (!Collector)
        return;
    
    Collector->OnTargetChanged.AddDynamic(this, &UISMResourceDebugComponent::OnTargetChanged);
    Collector->OnCollectionStartedEvent.AddDynamic(this, &UISMResourceDebugComponent::OnCollectionStarted);
    Collector->OnCollectionProgressEvent.AddDynamic(this, &UISMResourceDebugComponent::OnCollectionProgress);
    Collector->OnCollectionCompletedEvent.AddDynamic(this, &UISMResourceDebugComponent::OnCollectionCompleted);
    Collector->OnCollectionFailedEvent.AddDynamic(this, &UISMResourceDebugComponent::OnCollectionFailed);
}

void UISMResourceDebugComponent::BindResourceEvents()
{
    for (TWeakObjectPtr<UISMResourceComponent>& ResourceCompPtr : CachedResourceComponents)
    {
        UISMResourceComponent* ResourceComp = ResourceCompPtr.Get();
        if (!ResourceComp)
            continue;
        
        // Resource components broadcast their own events
        // We already get updates through the collector
    }
}

// ===== Event Handlers =====

void UISMResourceDebugComponent::OnTargetChanged(const FISMInstanceHandle& NewTarget, UISMResourceComponent* ResourceComponent)
{
    if (!bLogDetection)
        return;
    
    if (NewTarget.IsValid() && ResourceComponent)
    {
        FVector Location = ResourceComponent->GetInstanceLocation(NewTarget.InstanceIndex);
        UE_LOG(LogTemp, Display, TEXT("[COLLECTOR] Target Changed: Instance %d at %s (Component: %s)"), 
            NewTarget.InstanceIndex,
            *Location.ToCompactString(),
            *ResourceComponent->GetOwner()->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Display, TEXT("[COLLECTOR] Target Cleared"));
    }
}

void UISMResourceDebugComponent::OnCollectionStarted(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent)
{
    if (!bLogCollection)
        return;
    
    UE_LOG(LogTemp, Warning, TEXT("[COLLECTION] Started on Instance %d (Component: %s)"), 
        Instance.InstanceIndex,
        ResourceComponent ? *ResourceComponent->GetOwner()->GetName() : TEXT("NULL"));
}

void UISMResourceDebugComponent::OnCollectionProgress(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent, float Progress)
{
    // Don't spam logs with progress updates
}

void UISMResourceDebugComponent::OnCollectionCompleted(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent)
{
    if (!bLogCollection)
        return;
    
    UE_LOG(LogTemp, Warning, TEXT("[COLLECTION] Completed on Instance %d (Component: %s)"), 
        Instance.InstanceIndex,
        ResourceComponent ? *ResourceComponent->GetOwner()->GetName() : TEXT("NULL"));
}

void UISMResourceDebugComponent::OnCollectionFailed(const FISMInstanceHandle& Instance, UISMResourceComponent* ResourceComponent, FText FailureReason)
{
    if (!bLogValidation)
        return;
    
    UE_LOG(LogTemp, Error, TEXT("[COLLECTION] Failed on Instance %d: %s"), 
        Instance.InstanceIndex,
        *FailureReason.ToString());
}

// ===== Manual Commands =====

void UISMResourceDebugComponent::PrintCollectorState()
{
    UISMCollectorComponent* Collector = CachedCollector.Get();
    if (!Collector)
    {
        UE_LOG(LogTemp, Warning, TEXT("No Collector Component"));
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("=== Collector State ==="));
    UE_LOG(LogTemp, Warning, TEXT("Detection Mode: %d"), (int32)Collector->DetectionMode);
    UE_LOG(LogTemp, Warning, TEXT("Raycast Range: %.0f"), Collector->RaycastRange);
    UE_LOG(LogTemp, Warning, TEXT("Detection Radius: %.0f"), Collector->DetectionRadius);
    UE_LOG(LogTemp, Warning, TEXT("Has Valid Target: %s"), Collector->HasValidTarget() ? TEXT("YES") : TEXT("NO"));
    UE_LOG(LogTemp, Warning, TEXT("Is Collecting: %s"), Collector->IsCollecting() ? TEXT("YES") : TEXT("NO"));
    
    if (Collector->HasValidTarget())
    {
        FISMInstanceHandle Target = Collector->GetTargetedInstance();
        UE_LOG(LogTemp, Warning, TEXT("  Target Instance: %d"), Target.InstanceIndex);
    }
    
    FGameplayTagContainer Tags = Collector->GetCollectorTags();
    UE_LOG(LogTemp, Warning, TEXT("Collector Tags: %d"), Tags.Num());
    for (const FGameplayTag& Tag : Tags)
    {
        UE_LOG(LogTemp, Warning, TEXT("  - %s"), *Tag.ToString());
    }
}

void UISMResourceDebugComponent::PrintResourceComponentState()
{
    for (TWeakObjectPtr<UISMResourceComponent>& ResourceCompPtr : CachedResourceComponents)
    {
        UISMResourceComponent* ResourceComp = ResourceCompPtr.Get();
        if (!ResourceComp)
            continue;
        
        UE_LOG(LogTemp, Warning, TEXT("=== Resource Component: %s ==="), *ResourceComp->GetOwner()->GetName());
        UE_LOG(LogTemp, Warning, TEXT("Total Instances: %d"), ResourceComp->GetInstanceCount());
        UE_LOG(LogTemp, Warning, TEXT("Active Instances: %d"), ResourceComp->GetActiveInstanceCount());
        UE_LOG(LogTemp, Warning, TEXT("Base Collection Time: %.2f"), ResourceComp->BaseCollectionTime);
        UE_LOG(LogTemp, Warning, TEXT("Collection Stages: %d"), ResourceComp->CollectionStages);
        
        UE_LOG(LogTemp, Warning, TEXT("Resource Tags: %d"), ResourceComp->ResourceTags.Num());
        for (const FGameplayTag& Tag : ResourceComp->ResourceTags)
        {
            UE_LOG(LogTemp, Warning, TEXT("  - %s"), *Tag.ToString());
        }
    }
}

void UISMResourceDebugComponent::PrintNearbyResources(float Radius)
{
    UWorld* World = GetWorld();
    if (!World)
        return;
    
    UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    if (!Subsystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("No ISMRuntimeSubsystem found"));
        return;
    }
    
    FVector OwnerLocation = GetOwner()->GetActorLocation();
    UE_LOG(LogTemp, Warning, TEXT("=== Nearby Resources (%.0fcm radius) ==="), Radius);
    
    TArray<UISMRuntimeComponent*> AllComponents = Subsystem->GetAllComponents();
    int32 TotalFound = 0;
    
    for (UISMRuntimeComponent* RuntimeComp : AllComponents)
    {
        UISMResourceComponent* ResourceComp = Cast<UISMResourceComponent>(RuntimeComp);
        if (!ResourceComp)
            continue;
        
        TArray<int32> NearbyInstances = ResourceComp->GetInstancesInRadius(OwnerLocation, Radius);
        
        if (NearbyInstances.Num() > 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("Component: %s - %d instances"), 
                *ResourceComp->GetOwner()->GetName(),
                NearbyInstances.Num());
            
            for (int32 Index : NearbyInstances)
            {
                FVector Location = ResourceComp->GetInstanceLocation(Index);
                float Distance = FVector::Dist(OwnerLocation, Location);
                UE_LOG(LogTemp, Warning, TEXT("  [%d] Dist: %.0fcm, Active: %s"), 
                    Index,
                    Distance,
                    ResourceComp->IsInstanceActive(Index) ? TEXT("YES") : TEXT("NO"));
            }
            
            TotalFound += NearbyInstances.Num();
        }
    }
    
    UE_LOG(LogTemp, Warning, TEXT("Total Nearby Instances: %d"), TotalFound);
}
