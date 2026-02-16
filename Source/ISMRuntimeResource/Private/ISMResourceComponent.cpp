#include "ISMResourceComponent.h"
#include "ISMResourceDataAsset.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceHandle.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UISMResourceComponent::UISMResourceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UISMResourceComponent::BeginPlay()
{
    Super::BeginPlay();

    // Load settings from ResourceData if available
    if (ResourceData)
    {
        // ResourceData properties override component properties
        if (!ResourceData->ResourceTags.IsEmpty())
        {
            ResourceTags.AppendTags(ResourceData->ResourceTags);
        }

        if (!ResourceData->CollectorRequirements.IsEmpty())
        {
            CollectorRequirements = ResourceData->CollectorRequirements;
        }

        if (!ResourceData->RequirementsFailureMessage.IsEmpty())
        {
            RequirementsFailureMessage = ResourceData->RequirementsFailureMessage;
        }

        // Load collection settings
        BaseCollectionTime = ResourceData->BaseCollectionTime;
        bCanInterruptCollection = ResourceData->bCanInterruptCollection;
        CollectionStages = ResourceData->CollectionStages;

        // Load modifiers
        CollectorSpeedModifiers = ResourceData->CollectorSpeedModifiers;
        CollectorYieldModifiers = ResourceData->CollectorYieldModifiers;
    }
}

void UISMResourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Update all active collections
    TArray<int32> CompletedInstances;

    for (auto& Pair : ActiveCollections)
    {
        int32 InstanceIndex = Pair.Key;
        FResourceCollectionProgress& Progress = Pair.Value;

        // Skip paused collections
        if (Progress.bIsPaused)
            continue;

        // Check if collector is still valid
        if (!Progress.Collector.IsValid())
        {
            CancelCollection(InstanceIndex);
            continue;
        }

        // Update progress
        UpdateCollectionProgress(InstanceIndex, Progress, DeltaTime);

        // Check if completed
        if (Progress.Progress >= 1.0f)
        {
            CompletedInstances.Add(InstanceIndex);
        }
    }

    // Finalize completed collections
    for (int32 InstanceIndex : CompletedInstances)
    {
        if (FResourceCollectionProgress* Progress = ActiveCollections.Find(InstanceIndex))
        {
            FinalizeCollection(InstanceIndex, *Progress);
        }
    }
}

void UISMResourceComponent::BuildComponentTags()
{
    Super::BuildComponentTags();

    // Add resource tags to component tags
    ISMComponentTags.AppendTags(ResourceTags);
}

void UISMResourceComponent::OnInstancePreDestroy(int32 InstanceIndex)
{
    Super::OnInstancePreDestroy(InstanceIndex);

    // Cancel any active collection on this instance
    if (ActiveCollections.Contains(InstanceIndex))
    {
        CancelCollection(InstanceIndex);
    }
}

// ===== Collection API Implementation =====

bool UISMResourceComponent::CanCollectorGatherInstance(const FGameplayTagContainer& CollectorTags,
    int32 InstanceIndex,
    FText& OutFailureReason) const
{
    // Validate instance
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        OutFailureReason = NSLOCTEXT("ISMResource", "InvalidInstance", "Invalid instance");
        return false;
    }

    // Check if already destroyed or collected
    if (IsInstanceDestroyed(InstanceIndex))
    {
        OutFailureReason = NSLOCTEXT("ISMResource", "AlreadyCollected", "Already collected");
        return false;
    }

    // Check if currently being collected by someone else
    if (IsInstanceBeingCollected(InstanceIndex))
    {
        OutFailureReason = NSLOCTEXT("ISMResource", "BeingCollected", "Someone else is collecting this");
        return false;
    }

    // Get requirements (per-instance override or component default)
    FGameplayTagQuery Requirements = GetInstanceRequirements(InstanceIndex);

    // Check tag requirements
    if (!Requirements.IsEmpty())
    {
        if (!CheckTagRequirements(Requirements, CollectorTags, OutFailureReason))
        {
            // Use custom failure message if provided
            if (!RequirementsFailureMessage.IsEmpty())
            {
                OutFailureReason = RequirementsFailureMessage;
            }
            return false;
        }
    }

    // Allow custom validation via blueprint
    return ValidateCollectionRequirements(CollectorTags, InstanceIndex, OutFailureReason);
}

bool UISMResourceComponent::StartCollection(int32 InstanceIndex, AActor* Collector, const FGameplayTagContainer& CollectorTags)
{
    if (!Collector)
    {
        UE_LOG(LogTemp, Warning, TEXT("StartCollection called with null Collector"));
        return false;
    }

    // Validate collection
    FText FailureReason;
    if (!CanCollectorGatherInstance(CollectorTags, InstanceIndex, FailureReason))
    {
        UE_LOG(LogTemp, Verbose, TEXT("Collection blocked: %s"), *FailureReason.ToString());
        return false;
    }

    // Check if external validation delegate blocks this
    if (OnValidateCollection.IsBound())
    {
        FISMInstanceHandle Handle = GetInstanceHandle(InstanceIndex);
        if (!OnValidateCollection.Execute(Collector, Handle, FailureReason))
        {
            UE_LOG(LogTemp, Verbose, TEXT("Collection blocked by external validator: %s"), *FailureReason.ToString());
            return false;
        }
    }

    // Calculate effective collection time
    float EffectiveTime = GetEffectiveCollectionTime(CollectorTags, 0);

    // For instant collection, complete immediately
    if (EffectiveTime <= 0.0f)
    {
        CompleteCollectionImmediately(InstanceIndex, Collector, CollectorTags);
        return true;
    }

    // Create progress tracker
    FResourceCollectionProgress& Progress = ActiveCollections.Add(InstanceIndex);
    Progress.Collector = Collector;
    Progress.Progress = 0.0f;
    Progress.StartTime = GetWorld()->GetTimeSeconds();
    Progress.RequiredTime = EffectiveTime;
    Progress.CurrentStage = 0;
    Progress.CachedCollectorTags = CollectorTags;
    Progress.bIsPaused = false;
    Progress.PausedTime = 0.0f;

    // Call internal hook
    OnCollectionStartedInternal(InstanceIndex, Collector);

    // Broadcast event
    FISMInstanceHandle Handle = GetInstanceHandle(InstanceIndex);
    OnCollectionStarted.Broadcast(this, Handle, Collector);

    UE_LOG(LogTemp, Verbose, TEXT("Collection started on instance %d by %s (time: %.2fs)"),
        InstanceIndex, *Collector->GetName(), EffectiveTime);

    return true;
}

void UISMResourceComponent::CancelCollection(int32 InstanceIndex, bool bResetProgress)
{
    FResourceCollectionProgress* Progress = ActiveCollections.Find(InstanceIndex);
    if (!Progress)
        return;

    AActor* Collector = Progress->Collector.Get();

    // Decide whether to keep or reset progress
    bool bShouldResetProgress = bResetProgress || bResetProgressOnInterrupt;

    if (bShouldResetProgress)
    {
        // Remove from active collections
        ActiveCollections.Remove(InstanceIndex);

        UE_LOG(LogTemp, Verbose, TEXT("Collection cancelled on instance %d (progress reset)"), InstanceIndex);
    }
    else
    {
        // Just pause it
        Progress->bIsPaused = true;

        UE_LOG(LogTemp, Verbose, TEXT("Collection paused on instance %d (progress: %.2f)"),
            InstanceIndex, Progress->Progress);
    }

    // Call internal hook
    OnCollectionCancelledInternal(InstanceIndex, Collector);

    // Broadcast event
    FISMInstanceHandle Handle = GetInstanceHandle(InstanceIndex);
    OnCollectionCancelled.Broadcast(this, Handle, Collector);
}

void UISMResourceComponent::CompleteCollectionImmediately(int32 InstanceIndex, AActor* Collector, const FGameplayTagContainer& CollectorTags)
{
    // Create temporary progress for completion
    FResourceCollectionProgress Progress;
    Progress.Collector = Collector;
    Progress.Progress = 1.0f;
    Progress.StartTime = GetWorld()->GetTimeSeconds();
    Progress.RequiredTime = 0.0f;
    Progress.CurrentStage = CollectionStages - 1;
    Progress.CachedCollectorTags = CollectorTags;

    FinalizeCollection(InstanceIndex, Progress);
}

bool UISMResourceComponent::IsInstanceBeingCollected(int32 InstanceIndex) const
{
    const FResourceCollectionProgress* Progress = ActiveCollections.Find(InstanceIndex);
    return Progress && !Progress->bIsPaused;
}

float UISMResourceComponent::GetCollectionProgress(int32 InstanceIndex) const
{
    const FResourceCollectionProgress* Progress = ActiveCollections.Find(InstanceIndex);
    return Progress ? Progress->Progress : 0.0f;
}

AActor* UISMResourceComponent::GetCollector(int32 InstanceIndex) const
{
    const FResourceCollectionProgress* Progress = ActiveCollections.Find(InstanceIndex);
    return Progress ? Progress->Collector.Get() : nullptr;
}

// ===== Modifier Calculations =====

float UISMResourceComponent::CalculateSpeedMultiplier(const FGameplayTagContainer& CollectorTags) const
{
    float Multiplier = 1.0f;

    for (const auto& Pair : CollectorSpeedModifiers)
    {
        if (CollectorTags.HasTag(Pair.Key))
        {
            Multiplier *= Pair.Value;
        }
    }

    return FMath::Max(Multiplier, 0.01f); // Prevent zero or negative
}

float UISMResourceComponent::CalculateYieldMultiplier(const FGameplayTagContainer& CollectorTags) const
{
    float Multiplier = 1.0f;

    for (const auto& Pair : CollectorYieldModifiers)
    {
        if (CollectorTags.HasTag(Pair.Key))
        {
            Multiplier *= Pair.Value;
        }
    }

    return FMath::Max(Multiplier, 0.0f); // Prevent negative
}

float UISMResourceComponent::GetEffectiveCollectionTime(const FGameplayTagContainer& CollectorTags, int32 Stage) const
{
    float BaseTime = BaseCollectionTime;

    // If dividing time across stages, reduce per-stage time
    if (CollectionStages > 1 && bDivideTimeAcrossStages)
    {
        BaseTime /= CollectionStages;
    }

    // Apply speed multiplier
    float SpeedMult = CalculateSpeedMultiplier(CollectorTags);

    return BaseTime / SpeedMult;
}

// ===== Per-Instance Overrides =====

void UISMResourceComponent::SetInstanceRequirements(int32 InstanceIndex, const FGameplayTagQuery& Requirements)
{
    if (!IsValidInstanceIndex(InstanceIndex))
        return;

    PerInstanceRequirements.Add(InstanceIndex, Requirements);
}

void UISMResourceComponent::ClearInstanceRequirements(int32 InstanceIndex)
{
    PerInstanceRequirements.Remove(InstanceIndex);
}

FGameplayTagQuery UISMResourceComponent::GetInstanceRequirements(int32 InstanceIndex) const
{
    // Check for per-instance override
    if (const FGameplayTagQuery* Override = PerInstanceRequirements.Find(InstanceIndex))
    {
        return *Override;
    }

    // Fall back to component default
    return CollectorRequirements;
}

// ===== Virtual Hook Implementations =====

bool UISMResourceComponent::ValidateCollectionRequirements_Implementation(const FGameplayTagContainer& CollectorTags,
    int32 InstanceIndex,
    FText& OutFailureReason) const
{
    // Default: passed tag query = valid
    return true;
}

void UISMResourceComponent::OnCollectionStartedInternal_Implementation(int32 InstanceIndex, AActor* Collector)
{
    // Override in subclasses for custom behavior
}

void UISMResourceComponent::OnCollectionStageCompleted_Implementation(int32 InstanceIndex, AActor* Collector, int32 CompletedStage, int32 TotalStages)
{
    // Override in subclasses for per-stage effects
}

void UISMResourceComponent::OnCollectionCompletedInternal_Implementation(int32 InstanceIndex, const FResourceCollectionData& CollectionData)
{
    // Override in subclasses for custom completion logic
}

void UISMResourceComponent::OnCollectionCancelledInternal_Implementation(int32 InstanceIndex, AActor* Collector)
{
    // Override in subclasses for custom cancellation logic
}

// ===== Helper Functions =====

void UISMResourceComponent::UpdateCollectionProgress(int32 InstanceIndex, FResourceCollectionProgress& Progress, float DeltaTime)
{
    // Update progress
    float OldProgress = Progress.Progress;
    Progress.Progress += (DeltaTime / Progress.RequiredTime);

    // Check for stage completion (multi-stage only)
    if (CollectionStages > 1)
    {
        float ProgressPerStage = 1.0f / CollectionStages;
        int32 NewStage = FMath::FloorToInt(Progress.Progress / ProgressPerStage);

        if (NewStage > Progress.CurrentStage && NewStage < CollectionStages)
        {
            Progress.CurrentStage = NewStage;
            OnCollectionStageCompleted(InstanceIndex, Progress.Collector.Get(), NewStage, CollectionStages);

            UE_LOG(LogTemp, Verbose, TEXT("Collection stage %d/%d completed on instance %d"),
                NewStage, CollectionStages, InstanceIndex);
        }
    }

    // Broadcast progress update (throttle to avoid spam)
    static float LastProgressBroadcastTime = 0.0f;
    float CurrentTime = GetWorld()->GetTimeSeconds();
    if (CurrentTime - LastProgressBroadcastTime > 0.1f) // Update UI at most 10 times per second
    {
        FISMInstanceHandle Handle = GetInstanceHandle(InstanceIndex);
        OnCollectionProgress.Broadcast(this, Handle, Progress.Collector.Get(), Progress.Progress);
        LastProgressBroadcastTime = CurrentTime;
    }
}

void UISMResourceComponent::FinalizeCollection(int32 InstanceIndex, FResourceCollectionProgress& Progress)
{
    // Build collection data
    FResourceCollectionData CollectionData = BuildCollectionData(InstanceIndex, Progress);

    // Call internal hook
    OnCollectionCompletedInternal(InstanceIndex, CollectionData);

    // Mark instance as collected (destroyed state)
    DestroyInstance(InstanceIndex);

    // Remove from active collections
    ActiveCollections.Remove(InstanceIndex);

    // Broadcast events - THIS IS WHERE GAME LOGIC HAPPENS
    OnResourceCollected.Broadcast(this, CollectionData);
    OnResourceCollectedNative.Broadcast(this, CollectionData);

    UE_LOG(LogTemp, Log, TEXT("Collection completed on instance %d by %s (duration: %.2fs, yield: %.2fx)"),
        InstanceIndex,
        CollectionData.Collector ? *CollectionData.Collector->GetName() : TEXT("Unknown"),
        CollectionData.CollectionDuration,
        CollectionData.YieldMultiplier);
}

FResourceCollectionData UISMResourceComponent::BuildCollectionData(int32 InstanceIndex, const FResourceCollectionProgress& Progress) 
{
    FResourceCollectionData Data;
    Data.Instance = GetOrCreateHandle(InstanceIndex);
    Data.Collector = Progress.Collector.Get();
    Data.CollectionTransform = GetInstanceTransform(InstanceIndex);
    Data.CollectionDuration = GetWorld()->GetTimeSeconds() - Progress.StartTime - Progress.PausedTime;
    Data.YieldMultiplier = CalculateYieldMultiplier(Progress.CachedCollectorTags);
    Data.SpeedMultiplier = CalculateSpeedMultiplier(Progress.CachedCollectorTags);
    Data.CollectorTags = Progress.CachedCollectorTags;
    Data.CompletedStage = Progress.CurrentStage;
    Data.TotalStages = CollectionStages;

    return Data;
}

bool UISMResourceComponent::CheckTagRequirements(const FGameplayTagQuery& Query, const FGameplayTagContainer& CollectorTags, FText& OutFailureReason) const
{
    if (Query.IsEmpty())
        return true;

    if (!Query.Matches(CollectorTags))
    {
        OutFailureReason = NSLOCTEXT("ISMResource", "TagRequirementsFailed", "You don't meet the requirements");
        return false;
    }

    return true;
}