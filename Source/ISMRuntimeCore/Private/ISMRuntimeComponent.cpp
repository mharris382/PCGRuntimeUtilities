// Copyright (c) 2025 Max Harris
// Published by Procedural Architect
// ISMRuntimeComponent.cpp
#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "ISMInstanceDataAsset.h"
#include "GameplayTagContainer.h"
#include "Feedbacks/ISMFeedbackContext.h"
#include "Feedbacks/ISMFeedbackTags.h"
#include "Feedbacks/ISMFeedbackSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "GameplayTagsManager.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogISMRuntimeCore);

UISMRuntimeComponent::UISMRuntimeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false; // Only tick if needed

    bIsInitialized = false;
    TimeSinceLastTick = 0.0f;
}

void UISMRuntimeComponent::BeginPlay()
{
    Super::BeginPlay();

    // Auto-initialize on begin play
    if (!bIsInitialized)
    {
        InitializeInstances();
        RecalculateInstanceBounds();
    }
    
    // Register with subsystem
    RegisterWithSubsystem();
}

void UISMRuntimeComponent::EndPlay(const EEndPlayReason::Type EndReason)
{
    // Unregister from subsystem
    UnregisterFromSubsystem();

    // Return all converted instances
    ReturnAllConvertedInstances(true, false);

    // Clear all data
    InstanceHandles.Empty();
    InstanceStates.Empty();
    PerInstanceTags.Empty();
    SpatialIndex.Clear();

    Super::EndPlay(EndReason);
}

void UISMRuntimeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Tick interval optimization
    if (bEnableTickOptimization && TickInterval > 0.0f)
    {
        TimeSinceLastTick += DeltaTime;

        if (TimeSinceLastTick < TickInterval)
        {
            return; // Skip this tick
        }

        TimeSinceLastTick = 0.0f;
    }

    // Subclasses can override to add custom tick logic
}

bool UISMRuntimeComponent::InitializeInstances()
{
    if (bIsInitialized)
    {
        return true; // Already initialized
    }

    if (!ManagedISMComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeComponent: No ManagedISMComponent set on %s"), *GetOwner()->GetName());
        return false;
    }

    // Apply data asset settings if available
    if (InstanceData)
    {
        if (InstanceData->StaticMesh && !ManagedISMComponent->GetStaticMesh())
        {
            ManagedISMComponent->SetStaticMesh(InstanceData->StaticMesh);
        }

        // Apply material overrides
        for (int32 i = 0; i < InstanceData->MaterialOverrides.Num(); i++)
        {
            if (InstanceData->MaterialOverrides[i])
            {
                ManagedISMComponent->SetMaterial(i, InstanceData->MaterialOverrides[i]);
            }
        }

        // Use recommended cell size from data asset
        if (InstanceData->RecommendedCellSize > 0.0f)
        {
            SpatialIndexCellSize = InstanceData->RecommendedCellSize;
        }
    }

    // Build component tags (let subclasses add their specific tags)
    BuildComponentTags();

    // Initialize spatial index
    SpatialIndex = FISMSpatialIndex(SpatialIndexCellSize);

    // Index all existing instances
    int32 InstanceCount = ManagedISMComponent->GetInstanceCount();
    TArray<FVector> InstanceLocations;
    InstanceLocations.Reserve(InstanceCount);

    for (int32 i = 0; i < InstanceCount; i++)
    {
        FTransform InstanceTransform;
        ManagedISMComponent->GetInstanceTransform(i, InstanceTransform, true);
        if (ManagedISMComponent->NumCustomDataFloats > 0)
        {
            TArray<float> CustomData;
			CustomData.Reserve(ManagedISMComponent->NumCustomDataFloats);
			
			//InstanceCustomDataCache.Add(i, CustomData);
        }
        InstanceLocations.Add(InstanceTransform.GetLocation());

        // Initialize state for this instance
        FISMInstanceState& State = InstanceStates.Add(i);
        State.LastUpdateFrame = GFrameCounter;
        State.CachedTransform = InstanceTransform;
        State.bTransformCached = true;
    }

    // Build spatial index from all instances
    SpatialIndex.Rebuild(InstanceLocations);

    bIsInitialized = true;

    // Enable tick if needed
    if (TickInterval > 0.0f || !bEnableTickOptimization)
    {
        PrimaryComponentTick.SetTickFunctionEnable(true);
    }
    
    RecalculateInstanceBounds();

    if (!RegisterWithSubsystem())
        return false;

    // Notify subclasses
    OnInitializationComplete();

    UE_LOG(LogTemp, Log, TEXT("ISMRuntimeComponent: Initialized %d instances on %s"),
        InstanceCount, *GetOwner()->GetName());


    return true;
}

void UISMRuntimeComponent::BuildComponentTags()
{
    // Base implementation does nothing
    // Subclasses override to add their specific tags

    // Apply default tags from data asset
    if (InstanceData)
    {
        ISMComponentTags.AppendTags(InstanceData->DefaultTags);
    }
}

void UISMRuntimeComponent::OnInitializationComplete()
{
    // Base implementation does nothing
    // Subclasses can override for post-init logic
}

void UISMRuntimeComponent::DestroyInstance(int32 InstanceIndex, bool bUpdateBounds, bool bTriggerFeedbacks, const UActorComponent* InstigatorComponent)
{
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return;
    }
    
    // Get state
    FISMInstanceState* State = InstanceStates.Find(InstanceIndex);
    if (!State)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeComponent: No state found for instance %d"), InstanceIndex);
        return;
    }
    
    // Already destroyed?
    if (State->HasFlag(EISMInstanceState::Destroyed))
    {
        return;
    }
    
    // Cache location before destruction (for bounds check)
    FVector InstanceLocation = GetInstanceLocation(InstanceIndex);
    bool bWasOnBoundsEdge = IsLocationOnBoundsEdge(InstanceLocation);
    
    // Notify subclass
    OnInstancePreDestroy(InstanceIndex);
    
    // Mark as destroyed
    State->MarkDestroyed();
    
    // Add destroyed tag
    AddInstanceTag(InstanceIndex, FGameplayTag::RequestGameplayTag("ISM.State.Destroyed"));
    
    // Remove intact tag
    RemoveInstanceTag(InstanceIndex, FGameplayTag::RequestGameplayTag("ISM.State.Intact"));
    
    // Hide the instance (scale to zero - cheapest method)
    FTransform HiddenTransform;
    ManagedISMComponent->GetInstanceTransform(InstanceIndex, HiddenTransform, true);
    HiddenTransform.SetScale3D(FVector::ZeroVector);
    
    ManagedISMComponent->UpdateInstanceTransform(InstanceIndex, HiddenTransform, true, true);
    
    // Update cached transform
    State->CachedTransform = HiddenTransform;
    State->bTransformCached = true;
    
    // Broadcast events
    BroadcastDestruction(InstanceIndex);
    BroadcastStateChange(InstanceIndex);
    
    // Notify subclass
    OnInstancePostDestroy(InstanceIndex);
    
    // ★ EXPLICIT bounds update (only if requested AND instance was on edge)
    if (bUpdateBounds && bWasOnBoundsEdge)
    {
        RecalculateInstanceBounds(); // O(n) - but explicit!
    }
    else if (bBoundsValid && !bWasOnBoundsEdge)
    {
        // Instance was inside bounds, removal doesn't affect bounds
        // No recalculation needed - O(1)
    }

    if(bTriggerFeedbacks)
		TriggerFeedbackOnDestroyInternal(InstanceIndex, InstigatorComponent);
}

void UISMRuntimeComponent::HideInstance(int32 InstanceIndex, bool bUpdateBounds, bool bTriggerFeedbacks, const UActorComponent* InstigatorComponent)
{
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return;
    }
    
    FISMInstanceState* State = InstanceStates.Find(InstanceIndex);
    if (!State)
    {
        return;
    }
    
    // Already hidden?
    if (State->HasFlag(EISMInstanceState::Hidden))
    {
        return;
    }
    
    // Cache location before hiding
    FVector InstanceLocation = GetInstanceLocation(InstanceIndex);
    bool bWasOnBoundsEdge = IsLocationOnBoundsEdge(InstanceLocation);
    
    // Mark as hidden
    State->SetFlag(EISMInstanceState::Hidden, true);
    
    // Scale to zero
    FTransform HiddenTransform;
    ManagedISMComponent->GetInstanceTransform(InstanceIndex, HiddenTransform, true);
    HiddenTransform.SetScale3D(FVector::ZeroVector);
    
    ManagedISMComponent->UpdateInstanceTransform(InstanceIndex, HiddenTransform, true, true);
    
    State->CachedTransform = HiddenTransform;
    State->bTransformCached = true;
    
    BroadcastStateChange(InstanceIndex);
    
    // Explicit bounds update
    if (bUpdateBounds && bWasOnBoundsEdge)
    {
        RecalculateInstanceBounds();
    }

    if(bTriggerFeedbacks)
		TriggerFeedbackOnHideInternal(InstanceIndex, InstigatorComponent);
}

void UISMRuntimeComponent::ShowInstance(int32 InstanceIndex, bool bUpdateBounds, bool bTriggerFeedbacks, const UActorComponent* InstigatorComponent)
{
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return;
    }
    
    FISMInstanceState* State = InstanceStates.Find(InstanceIndex);
    if (!State)
    {
        return;
    }
    
    // Not hidden?
    if (!State->HasFlag(EISMInstanceState::Hidden))
    {
        return;
    }
    
    // Mark as not hidden
    State->SetFlag(EISMInstanceState::Hidden, false);
    
    // Restore scale (assume scale of 1,1,1 - could cache original scale if needed)
    FTransform VisibleTransform;
    ManagedISMComponent->GetInstanceTransform(InstanceIndex, VisibleTransform, true);
    VisibleTransform.SetScale3D(State->CachedTransform.GetScale3D());
    
    ManagedISMComponent->UpdateInstanceTransform(InstanceIndex, VisibleTransform, true, true);
    
    State->CachedTransform = VisibleTransform;
    State->bTransformCached = true;
    
    BroadcastStateChange(InstanceIndex);
    
    // Expand bounds to include newly shown instance (O(1))
    if (bUpdateBounds)
    {
        ExpandBoundsToInclude(VisibleTransform.GetLocation());
    }

    if(bTriggerFeedbacks)
		TriggerFeedbackOnShowInternal(InstanceIndex, InstigatorComponent);
}

void UISMRuntimeComponent::UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewTransform, 
    bool bUpdateSpatialIndex, bool bUpdateBounds, bool bTriggerFeedbacks, const UActorComponent* InstigatorComponent)
{
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return;
    }
    
    // Get old location for spatial index update
    FVector OldLocation = GetInstanceLocation(InstanceIndex);
    FVector NewLocation = NewTransform.GetLocation();
    
    // Update ISM
    ManagedISMComponent->UpdateInstanceTransform(InstanceIndex, NewTransform, true, true);
    UpdateInstanceWorldBounds(InstanceIndex, NewTransform);

    // Update cached state
    if (FISMInstanceState* State = InstanceStates.Find(InstanceIndex))
    {
        State->CachedTransform = NewTransform;
        State->bTransformCached = true;
        State->LastUpdateFrame = GFrameCounter;
    }
    
    // Update spatial index
    if (bUpdateSpatialIndex)
    {
        SpatialIndex.UpdateInstance(InstanceIndex, OldLocation, NewLocation);
    }
    
    // Update bounds
    if (bUpdateBounds)
    {
        bool bOldWasOnEdge = IsLocationOnBoundsEdge(OldLocation);
        
        if (bOldWasOnEdge)
        {
            // Old location was on edge, need full recalculation
            RecalculateInstanceBounds();
        }
        else
        {
            // Old location wasn't on edge, just expand to include new location
            ExpandBoundsToInclude(NewLocation);
        }
    }
    
    if(bTriggerFeedbacks)
		TriggerFeedbackOnTransformUpdateInternal(InstanceIndex, InstigatorComponent);
}

void UISMRuntimeComponent::BatchDestroyInstances(const TArray<int32>& InstanceIndices, bool bUpdateBounds, bool bTriggerFeedbacks, const UActorComponent* InstigatorComponent)
{
    if (InstanceIndices.Num() == 0)
    {
        return;
    }
    
    // Destroy all instances without bounds updates
    for (int32 Index : InstanceIndices)
    {
        DestroyInstance(Index, false); // Don't update bounds per-instance
    }
    
    // Single bounds recalculation at the end if requested
    if (bUpdateBounds)
    {
        RecalculateInstanceBounds();
    }
    if (bTriggerFeedbacks)
    {
		TriggerFeedbackBatchedOnDestroyInternal(InstanceIndices, InstigatorComponent);  
    }
}

int32 UISMRuntimeComponent::AddInstance(const FTransform& Transform, bool bUpdateBounds, bool bTriggerFeedbacks, const UActorComponent* InstigatorComponent)
{
    if (!ManagedISMComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("ISMRuntimeComponent: Cannot add instance - no managed ISM component"));
        return INDEX_NONE;
    }

    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeComponent: Adding instance before initialization on %s"),
            *GetOwner()->GetName());
    }

    // Add to ISM component
    int32 NewIndex = ManagedISMComponent->AddInstance(Transform);

    if (NewIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("ISMRuntimeComponent: Failed to add instance to ISM"));
        return INDEX_NONE;
    }

    // Initialize state for new instance
    InitializeNewInstance(NewIndex, Transform);

    // Add to spatial index
    SpatialIndex.AddInstance(NewIndex, Transform.GetLocation());

    // Update bounds (O(1) - just expand)
    if (bUpdateBounds)
    {
        ExpandBoundsToInclude(Transform.GetLocation());
    }

    if(bTriggerFeedbacks)
		TriggerFeedbackOnSpawnInternal(NewIndex, InstigatorComponent);

    // Notify subclass
    OnInstanceAdded(NewIndex, Transform);

    return NewIndex;
}

TArray<int32> UISMRuntimeComponent::BatchAddInstances(const TArray<FTransform>& Transforms, bool bUpdateBounds, bool bReturnInstances, bool bRegenerateNavigation, bool bTriggerFeedbacks, const UActorComponent* InstigatorComponent)
{
    TArray<int32> NewIndices;
    NewIndices.Reserve(Transforms.Num());
    
    if (!ManagedISMComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("ISMRuntimeComponent: Cannot add instances - no managed ISM component"));
        // Return array of INDEX_NONE
        for (int32 i = 0; i < Transforms.Num(); i++)
        {
            NewIndices.Add(INDEX_NONE);
        }
        return NewIndices;
    }

    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeComponent: Batch adding instances before initialization on %s"),
            *GetOwner()->GetName());
    }

    // Add all instances to ISM in batch (more efficient than individual adds)
    TArray<int32> AddedIndices = ManagedISMComponent->AddInstances(Transforms, bReturnInstances, true, bRegenerateNavigation);

    if (AddedIndices.Num() != Transforms.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("ISMRuntimeComponent: Batch add returned unexpected number of indices"));
        return NewIndices;
    }

    // Track bounds expansion
    FBox NewInstancesBounds(EForceInit::ForceInit);
    bool bHasValidInstances = false;

    // Initialize state and spatial index for each new instance
    for (int32 i = 0; i < AddedIndices.Num(); i++)
    {
        int32 NewIndex = AddedIndices[i];

        if (NewIndex == INDEX_NONE)
        {
            NewIndices.Add(INDEX_NONE);
            continue;
        }

        const FTransform& Transform = Transforms[i];

        // Initialize state
        InitializeNewInstance(NewIndex, Transform);

        // Add to spatial index
        SpatialIndex.AddInstance(NewIndex, Transform.GetLocation());

        // Track bounds
        if (bUpdateBounds)
        {
            if (bHasValidInstances)
            {
                NewInstancesBounds += Transform.GetLocation();
            }
            else
            {
                NewInstancesBounds = FBox(Transform.GetLocation(), Transform.GetLocation());
                bHasValidInstances = true;
            }
        }

        // Notify subclass
        OnInstanceAdded(NewIndex, Transform);

        NewIndices.Add(NewIndex);
    }

    // Update bounds once for all new instances (O(1))
    if (bUpdateBounds && bHasValidInstances)
    {
        if (bBoundsValid)
        {
            CachedInstanceBounds += NewInstancesBounds;
        }
        else
        {
            CachedInstanceBounds = NewInstancesBounds.ExpandBy(BoundsPadding);
            bBoundsValid = true;
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("ISMRuntimeComponent: Batch added %d instances"), NewIndices.Num());

    return NewIndices;
}

int32 UISMRuntimeComponent::AddInstanceWithCustomData(const FTransform& Transform, const TArray<float>& CustomData, bool bUpdateBounds)
{
    int32 NewIndex = AddInstance(Transform, bUpdateBounds);

    if (NewIndex != INDEX_NONE && CustomData.Num() > 0)
    {
        SetInstanceCustomData(NewIndex, CustomData);
    }

    return NewIndex;
}

void UISMRuntimeComponent::InitializeNewInstance(int32 InstanceIndex, const FTransform& Transform)
{
    // Create state entry
    FISMInstanceState& State = InstanceStates.Add(InstanceIndex);
    State.LastUpdateFrame = GFrameCounter;
    State.CachedTransform = Transform;
    State.bTransformCached = true;

    // Start as intact
    State.SetFlag(EISMInstanceState::Intact, true);

	UpdateInstanceWorldBounds(InstanceIndex, Transform);

    // Add default state tag
    AddInstanceTag(InstanceIndex, FGameplayTag::RequestGameplayTag("ISM.State.Intact"));

}

void UISMRuntimeComponent::OnInstanceAdded(int32 InstanceIndex, const FTransform& Transform)
{
    // Base implementation does nothing
    // Subclasses override for custom initialization
}




void UISMRuntimeComponent::RecalculateInstanceBounds()
{
    CachedInstanceBounds = FBox(EForceInit::ForceInit);
    
    if (!ManagedISMComponent)
    {
        bBoundsValid = false;
        return;
    }
    
    bool bHasActiveInstances = false;
    
    // O(n) operation - iterate all instances
    for (int32 i = 0; i < ManagedISMComponent->GetInstanceCount(); i++)
    {
        // Skip destroyed/inactive instances
        if (!IsInstanceActive(i))
        {
            continue;
        }
        
        FVector Location = GetInstanceLocation(i);
        
        if (bHasActiveInstances)
        {
            CachedInstanceBounds += Location;
        }
        else
        {
            CachedInstanceBounds = FBox(Location, Location);
            bHasActiveInstances = true;
        }
    }
    
    // Add padding to account for instance mesh size
    if (bHasActiveInstances)
    {
        CachedInstanceBounds = CachedInstanceBounds.ExpandBy(BoundsPadding);
        bBoundsValid = true;
    }
    else
    {
        bBoundsValid = false;
    }
}

void UISMRuntimeComponent::ExpandBoundsToInclude(const FVector& Location)
{
    if (bBoundsValid)
    {
        // O(1) - just expand existing bounds
        CachedInstanceBounds += Location;
    }
    else
    {
        // Initialize bounds with this location
        CachedInstanceBounds = FBox(Location, Location);
        CachedInstanceBounds = CachedInstanceBounds.ExpandBy(BoundsPadding);
        bBoundsValid = true;
    }
}

bool UISMRuntimeComponent::IsLocationOnBoundsEdge(const FVector& Location, float Tolerance) const
{
    if (!bBoundsValid)
    {
        return false; // No bounds = can't be on edge
    }
    
    // Check if location is within Tolerance of any face of the bounds
    FVector Min = CachedInstanceBounds.Min;
    FVector Max = CachedInstanceBounds.Max;
    
    // Check each axis
    bool bOnMinX = FMath::Abs(Location.X - Min.X) <= Tolerance;
    bool bOnMaxX = FMath::Abs(Location.X - Max.X) <= Tolerance;
    bool bOnMinY = FMath::Abs(Location.Y - Min.Y) <= Tolerance;
    bool bOnMaxY = FMath::Abs(Location.Y - Max.Y) <= Tolerance;
    bool bOnMinZ = FMath::Abs(Location.Z - Min.Z) <= Tolerance;
    bool bOnMaxZ = FMath::Abs(Location.Z - Max.Z) <= Tolerance;
    
    // On edge if on any face
    return bOnMinX || bOnMaxX || bOnMinY || bOnMaxY || bOnMinZ || bOnMaxZ;
}






bool UISMRuntimeComponent::IsInstanceDestroyed(int32 InstanceIndex) const
{
    const FISMInstanceState* State = InstanceStates.Find(InstanceIndex);
    return State && State->HasFlag(EISMInstanceState::Destroyed);
}

bool UISMRuntimeComponent::IsInstanceActive(int32 InstanceIndex) const
{
    const FISMInstanceState* State = InstanceStates.Find(InstanceIndex);
    return State && State->IsActive();
}

int32 UISMRuntimeComponent::GetInstanceCount() const
{
    return ManagedISMComponent ? ManagedISMComponent->GetInstanceCount() : 0;
}

int32 UISMRuntimeComponent::GetActiveInstanceCount() const
{
    int32 ActiveCount = 0;

    for (const auto& Pair : InstanceStates)
    {
        if (Pair.Value.IsActive())
        {
            ActiveCount++;
        }
    }

    return ActiveCount;
}

FTransform UISMRuntimeComponent::GetInstanceTransform(int32 InstanceIndex) const
{
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return FTransform::Identity;
    }

    // Check cache first
    if (const FISMInstanceState* State = InstanceStates.Find(InstanceIndex))
    {
        if (State->bTransformCached)
        {
            return State->CachedTransform;
        }
    }

    // Get from ISM
    FTransform Transform;
    ManagedISMComponent->GetInstanceTransform(InstanceIndex, Transform, true);

    return Transform;
}

FVector UISMRuntimeComponent::GetInstanceLocation(int32 InstanceIndex) const
{
    return GetInstanceTransform(InstanceIndex).GetLocation();
}



TArray<int32> UISMRuntimeComponent::GetInstancesInRadius(const FVector& Location, float Radius, bool bIncludeDestroyed) const
{
    TArray<int32> Results;
    SpatialIndex.QueryRadius(Location, Radius, Results);

    // Filter destroyed instances if requested
    if (!bIncludeDestroyed)
    {
        Results = Results.FilterByPredicate([this](int32 Index)
            {
                return IsInstanceActive(Index);
            });
    }

    return Results;
}

TArray<int32> UISMRuntimeComponent::GetInstancesInBox(const FBox& Box, bool bIncludeDestroyed) const
{
    TArray<int32> Results;
    SpatialIndex.QueryBox(Box, Results);

    // Filter destroyed instances if requested
    if (!bIncludeDestroyed)
    {
        Results = Results.FilterByPredicate([this](int32 Index)
            {
                return IsInstanceActive(Index);
            });
    }

    return Results;
}

int32 UISMRuntimeComponent::GetNearestInstance(const FVector& Location, float MaxDistance, bool bIncludeDestroyed) const
{
    // Build location array for FindNearestInstance
    TArray<FVector> InstanceLocations;
    int32 InstanceCount = GetInstanceCount();
    InstanceLocations.Reserve(InstanceCount);

    for (int32 i = 0; i < InstanceCount; i++)
    {
        // Skip destroyed instances if requested
        if (!bIncludeDestroyed && IsInstanceDestroyed(i))
        {
            InstanceLocations.Add(FVector(MAX_FLT, MAX_FLT, MAX_FLT)); // Push far away
        }
        else
        {
            InstanceLocations.Add(GetInstanceLocation(i));
        }
    }

    return SpatialIndex.FindNearestInstance(Location, InstanceLocations, MaxDistance);
}

TArray<int32> UISMRuntimeComponent::QueryInstances(const FVector& Location, float Radius, const FISMQueryFilter& Filter) const
{
    // Get candidates from spatial index
    TArray<int32> Candidates;
    SpatialIndex.QueryRadius(Location, Radius, Candidates);
	if (Candidates.Num() == 0)
    {
        return Candidates; 
    }

    // Create instance references and filter
    TArray<int32> Results;
    Results.Reserve(Candidates.Num());

    for (int32 Index : Candidates)
    {
        FISMInstanceReference Ref;
        Ref.Component = const_cast<UISMRuntimeComponent*>(this);
        Ref.InstanceIndex = Index;

        if (Filter.PassesFilter(Ref))
        {
            Results.Add(Index);

            // Check max results limit
            if (Filter.MaxResults > 0 && Results.Num() >= Filter.MaxResults)
            {
                break;
            }
        }
    }

    // Sort by distance if requested
    if (Filter.bSortByDistance && Results.Num() > 0)
    {
        Results.Sort([this, &Location](int32 A, int32 B)
            {
                float DistA = FVector::DistSquared(GetInstanceLocation(A), Location);
                float DistB = FVector::DistSquared(GetInstanceLocation(B), Location);
                return DistA < DistB;
            });
    }

    return Results;
}

// ===== Gameplay Tags =====

void UISMRuntimeComponent::AddInstanceTag(int32 InstanceIndex, FGameplayTag Tag)
{
    if (!IsValidInstanceIndex(InstanceIndex) || !Tag.IsValid())
    {
        return;
    }

    FGameplayTagContainer& InstanceTags = PerInstanceTags.FindOrAdd(InstanceIndex);

    if (!InstanceTags.HasTag(Tag))
    {
        InstanceTags.AddTag(Tag);
        BroadcastTagChange(InstanceIndex);
    }
}

void UISMRuntimeComponent::RemoveInstanceTag(int32 InstanceIndex, FGameplayTag Tag)
{
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return;
    }

    if (FGameplayTagContainer* InstanceTags = PerInstanceTags.Find(InstanceIndex))
    {
        if (InstanceTags->HasTag(Tag))
        {
            InstanceTags->RemoveTag(Tag);
            BroadcastTagChange(InstanceIndex);

            // Clean up empty containers
            if (InstanceTags->IsEmpty())
            {
                PerInstanceTags.Remove(InstanceIndex);
            }
        }
    }
}

bool UISMRuntimeComponent::InstanceHasTag(int32 InstanceIndex, FGameplayTag Tag) const
{
    return GetInstanceTags(InstanceIndex).HasTag(Tag);
}

FGameplayTagContainer UISMRuntimeComponent::GetInstanceTags(int32 InstanceIndex) const
{
    return GetEffectiveTagsForInstance(InstanceIndex);
}

FGameplayTagContainer UISMRuntimeComponent::GetEffectiveTagsForInstance(int32 InstanceIndex) const
{
    FGameplayTagContainer EffectiveTags = ISMComponentTags;

    if (const FGameplayTagContainer* InstanceTags = PerInstanceTags.Find(InstanceIndex))
    {
        EffectiveTags.AppendTags(*InstanceTags);
    }

    return EffectiveTags;
}

// ===== Custom Data =====

TArray<float> UISMRuntimeComponent::GetInstanceCustomData(int32 InstanceIndex) const
{
    TArray<float> CustomData;

    if (!ManagedISMComponent || !IsValidInstanceIndex(InstanceIndex))
    {
        return CustomData;
    }

    int32 NumCustomDataFloats = ManagedISMComponent->NumCustomDataFloats;
    if (NumCustomDataFloats == 0)
    {
        return CustomData;
    }

    CustomData.Reserve(NumCustomDataFloats);

    int32 StartIndex = InstanceIndex * NumCustomDataFloats;
    for (int32 i = 0; i < NumCustomDataFloats; i++)
    {
        int32 DataIndex = StartIndex + i;
        if (ManagedISMComponent->PerInstanceSMCustomData.IsValidIndex(DataIndex))
        {
            CustomData.Add(ManagedISMComponent->PerInstanceSMCustomData[DataIndex]);
        }
        else
        {
            CustomData.Add(0.0f);
        }
    }

    return CustomData;
}

void UISMRuntimeComponent::SetInstanceCustomData(int32 InstanceIndex, const TArray<float>& CustomData)
{
    if (!ManagedISMComponent || !IsValidInstanceIndex(InstanceIndex))
    {
        return;
    }

    int32 NumCustomDataFloats = ManagedISMComponent->NumCustomDataFloats;
    if (NumCustomDataFloats == 0)
    {
        return;
    }

    int32 StartIndex = InstanceIndex * NumCustomDataFloats;

    for (int32 i = 0; i < FMath::Min(CustomData.Num(), NumCustomDataFloats); i++)
    {
        int32 DataIndex = StartIndex + i;
        if (ManagedISMComponent->PerInstanceSMCustomData.IsValidIndex(DataIndex))
        {
            ManagedISMComponent->PerInstanceSMCustomData[DataIndex] = CustomData[i];
        }
    }

    ManagedISMComponent->MarkRenderStateDirty();

}

float UISMRuntimeComponent::GetInstanceCustomDataValue(int32 InstanceIndex, int32 DataIndex) const
{

    if (!ManagedISMComponent || !IsValidInstanceIndex(InstanceIndex))
    {
        return 0.0f;
    }

    int32 NumCustomDataFloats = ManagedISMComponent->NumCustomDataFloats;
    if (DataIndex < 0 || DataIndex >= NumCustomDataFloats)
    {
        return 0.0f;
    }

    int32 ArrayIndex = InstanceIndex * NumCustomDataFloats + DataIndex;

    if (!ManagedISMComponent->PerInstanceSMCustomData.IsValidIndex(ArrayIndex))
    {
        return 0.0f;
    }
    return ManagedISMComponent->PerInstanceSMCustomData[ArrayIndex];
}

void UISMRuntimeComponent::SetInstanceCustomDataValue(int32 InstanceIndex, int32 DataIndex, float Value)
{
    if (!ManagedISMComponent || !IsValidInstanceIndex(InstanceIndex))
    {
        return;
    }

    int32 NumCustomDataFloats = ManagedISMComponent->NumCustomDataFloats;
    if (DataIndex < 0 || DataIndex >= NumCustomDataFloats)
    {
        return;
    }

    int32 ArrayIndex = InstanceIndex * NumCustomDataFloats + DataIndex;

    if (ManagedISMComponent->PerInstanceSMCustomData.IsValidIndex(ArrayIndex))
    {
        ManagedISMComponent->PerInstanceSMCustomData[ArrayIndex] = Value;
        ManagedISMComponent->MarkRenderStateDirty();
    }
}

void UISMRuntimeComponent::SetCustomDataCount(int32 DesiredCount, bool bResetExisting, float DefaultValue)
{
    if (!ManagedISMComponent) return;
    if (DesiredCount <= 0)    return;

    const int32 CurrentCount = ManagedISMComponent->NumCustomDataFloats;

    if (bResetExisting)
    {
        // Rebuild from scratch at the requested size.
        // UInstancedStaticMeshComponent::SetNumCustomDataFloats clears all instance data.
        ManagedISMComponent->SetNumCustomDataFloats(DesiredCount);

        // Fill every instance with DefaultValue
        if (DefaultValue != 0.0f)
        {
            const int32 InstanceCount = ManagedISMComponent->GetInstanceCount();
            for (int32 i = 0; i < InstanceCount; ++i)
            {
                for (int32 SlotIdx = 0; SlotIdx < DesiredCount; ++SlotIdx)
                {
                    ManagedISMComponent->SetCustomDataValue(i, SlotIdx, DefaultValue, /*bMarkRenderStateDirty=*/false);
                }
            }
            ManagedISMComponent->MarkRenderStateDirty();
        }

        
        return;
    }

    // Non-destructive expand: only act if we are currently shorter than requested
    if (CurrentCount >= DesiredCount) return;

    // We need to expand from CurrentCount to DesiredCount.
    // UE doesn't have a "expand and preserve" call directly, so we:
    //   1. Capture existing data for all instances from our cache
    //   2. Resize the ISM
    //   3. Rewrite the existing data + fill new slots with DefaultValue

    const int32 InstanceCount = ManagedISMComponent->GetInstanceCount();

    // Step 1: snapshot existing data before resize clears it
    TMap<int32, TArray<float>> ExistingData;
    ExistingData.Reserve(ManagedISMComponent->GetInstanceCount());
	for (int i = 0; i < InstanceCount; ++i)
    {
		ExistingData.Add(i, GetInstanceCustomData(i));
    }

    // Step 2: resize - this resets all instance custom data to 0 internally
    ManagedISMComponent->SetNumCustomDataFloats(DesiredCount);

    // Step 3: rewrite preserved data + fill new slots
    for (int32 i = 0; i < InstanceCount; ++i)
    {
        const TArray<float>* OldData = ExistingData.Find(i);

        for (int32 SlotIdx = 0; SlotIdx < DesiredCount; ++SlotIdx)
        {
            float Value = DefaultValue;

            if (OldData && SlotIdx < OldData->Num())
            {
                // Preserve existing value
                Value = (*OldData)[SlotIdx];
            }

            ManagedISMComponent->SetCustomDataValue(i, SlotIdx, Value, false);
        }
    }

    ManagedISMComponent->MarkRenderStateDirty();
}




// ===== State Management (IISMStateProvider) =====

uint8 UISMRuntimeComponent::GetInstanceStateFlags(int32 InstanceIndex) const
{
    const FISMInstanceState* State = GetInstanceState(InstanceIndex);
    return State ? State->StateFlags : 0;
}

bool UISMRuntimeComponent::IsInstanceInState(int32 InstanceIndex, EISMInstanceState State) const
{
    const FISMInstanceState* InstanceState = GetInstanceState(InstanceIndex);
    return InstanceState && InstanceState->HasFlag(State);
}

void UISMRuntimeComponent::SetInstanceState(int32 InstanceIndex, EISMInstanceState State, bool bValue)
{
    FISMInstanceState* InstanceState = GetInstanceStateMutable(InstanceIndex);
    if (InstanceState)
    {
        InstanceState->SetFlag(State, bValue);
        BroadcastStateChange(InstanceIndex);
    }
}

const FISMInstanceState* UISMRuntimeComponent::GetInstanceState(int32 InstanceIndex) const
{
    return InstanceStates.Find(InstanceIndex);
}

FISMInstanceState* UISMRuntimeComponent::GetInstanceStateMutable(int32 InstanceIndex)
{
    return InstanceStates.Find(InstanceIndex);
}

#pragma region AABB_QUERIES

// ============================================================
//  Private helper — the single place WorldBounds gets written
// ============================================================

void UISMRuntimeComponent::UpdateInstanceWorldBounds(int32 InstanceIndex, const FTransform& Transform)
{
    // Hard gate — if the component opted out, never touch bounds data
    if (!bComputeInstanceAABBs)
    {
        return;
    }

    // Requires a DataAsset with valid cached local bounds
    if (!InstanceData)
    {
        return;
    }

    FBox LocalBounds = InstanceData->GetEffectiveLocalBounds();
    if (!LocalBounds.IsValid)
    {
        return;
    }

    // Transform the local box to world space.
    // FBox::TransformBy handles non-uniform scale correctly by transforming
    // all 8 corners and re-fitting, which is exactly what we want.
    FBox WorldBounds = LocalBounds.TransformBy(Transform);

    // Write into the instance state (creates entry if not present)
    FISMInstanceState& State = InstanceStates.FindOrAdd(InstanceIndex);
    State.WorldBounds = WorldBounds;
    State.bBoundsValid = true;
}


// ============================================================
//  Hook into existing lifecycle functions
//  These calls need to be added to the bodies of:
//    - InitializeNewInstance()
//    - UpdateInstanceTransform()
//  Since we can't modify those files here, the calls are shown
//  as standalone functions that delegate to UpdateInstanceWorldBounds.
// ============================================================

// Call this at the END of InitializeNewInstance(), after state is set up:
//   UpdateInstanceWorldBounds(InstanceIndex, Transform);

// Call this at the END of UpdateInstanceTransform(), after the ISM call:
//   UpdateInstanceWorldBounds(InstanceIndex, NewTransform);


// ============================================================
//  GetInstanceWorldBounds
// ============================================================

FBox UISMRuntimeComponent::GetInstanceWorldBounds(int32 InstanceIndex) const
{
    if (!bComputeInstanceAABBs)
    {
        return FBox(EForceInit::ForceInit);
    }

    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return FBox(EForceInit::ForceInit);
    }

    const FISMInstanceState* State = GetInstanceState(InstanceIndex);
    if (!State )
    {
        return FBox(EForceInit::ForceInit);
    }
	if (!State->bBoundsValid)
    {
        return FBox(EForceInit::ForceInit);
    }

    return State->WorldBounds;
}


// ============================================================
//  GetInstancesOverlappingBox
// ============================================================

TArray<int32> UISMRuntimeComponent::GetInstancesOverlappingBox(
    const FBox& Box,
    bool bIncludeDestroyed) const
{
    TArray<int32> Results;

    if (!bComputeInstanceAABBs)
    {
		UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeComponent: Cannot perform box query - AABB computation is disabled"));
        return Results;
    }

    // Expand the candidate search box by the max local extent so instances
    // whose centers are outside Box but whose AABBs overlap it are included
    // as candidates. The precise AABB intersection test is the second pass.
    FBox CandidateBox = Box;
    if (InstanceData)
    {
        FBox LocalBounds = InstanceData->GetEffectiveLocalBounds();
        if (LocalBounds.IsValid)
        {
            // GetExtent() is half-size. Expand by full extent to be safe
            // since scale can push the bounds further than the local extent.
            // A more precise expansion would track max scale, but this is
            // conservative and correct.
            FVector MaxExtent = LocalBounds.GetExtent();
            CandidateBox = Box.ExpandBy(MaxExtent);
        }
    }

    TArray<int32> Candidates = GetInstancesInBox(CandidateBox, bIncludeDestroyed);
	UE_LOG(LogTemp, Verbose, TEXT("ISMRuntimeComponent: Box query found %d candidates"), Candidates.Num());
    Results.Reserve(Candidates.Num());
    for (int32 CandidateIndex : Candidates)
    {
        const FISMInstanceState* State = GetInstanceState(CandidateIndex);
        if (!State || !State->bBoundsValid)
        {
			UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeComponent: No valid bounds for instance %d during box query"), CandidateIndex);
            continue;
        }

        if (State->WorldBounds.Intersect(Box))
        {
            Results.Add(CandidateIndex);
        }
    }

    return Results;
}


// ============================================================
//  GetInstancesOverlappingSphere
// ============================================================

TArray<int32> UISMRuntimeComponent::GetInstancesOverlappingSphere(
    const FVector& Center,
    float Radius,
    bool bIncludeDestroyed) const
{
    TArray<int32> Results;

    if (!bComputeInstanceAABBs || Radius <= 0.0f)
    {
        return Results;
    }

    float CandidateRadius = Radius;
    if (InstanceData)
    {
        FBox LocalBounds = InstanceData->GetEffectiveLocalBounds();
        if (LocalBounds.IsValid)
        {
            CandidateRadius += LocalBounds.GetExtent().Size();
        }
    }

    TArray<int32> Candidates = GetInstancesInRadius(Center, CandidateRadius, bIncludeDestroyed);

    Results.Reserve(Candidates.Num());
    for (int32 CandidateIndex : Candidates)
    {
        const FISMInstanceState* State = GetInstanceState(CandidateIndex);
        if (!State || !State->bBoundsValid)
        {
            continue;
        }

        float DistSq = State->WorldBounds.ComputeSquaredDistanceToPoint(Center);
        if (DistSq <= FMath::Square(Radius))
        {
            Results.Add(CandidateIndex);
        }
    }

    return Results;
}


// ============================================================
//  GetInstancesOverlappingInstance
// ============================================================

TArray<int32> UISMRuntimeComponent::GetInstancesOverlappingInstance(
    int32 InstanceIndex,
    bool bIncludeDestroyed) const
{
    TArray<int32> Results;

    if (!bComputeInstanceAABBs || !IsValidInstanceIndex(InstanceIndex))
    {
        return Results;
    }

    const FISMInstanceState* QueryState = GetInstanceState(InstanceIndex);
    if (!QueryState || !QueryState->bBoundsValid)
    {
        return Results;
    }

    FBox QueryBounds = QueryState->WorldBounds;

    // Use the box query to get candidates, then filter by AABB intersection
    TArray<int32> Candidates = GetInstancesOverlappingBox(QueryBounds, bIncludeDestroyed);

    Results.Reserve(Candidates.Num());

    for (int32 CandidateIndex : Candidates)
    {
        // Exclude the query instance itself
        if (CandidateIndex == InstanceIndex)
        {
            continue;
        }

        Results.Add(CandidateIndex);
    }

    return Results;
}


// ============================================================
//  DoInstancesOverlap
// ============================================================

bool UISMRuntimeComponent::DoInstancesOverlap(int32 IndexA, int32 IndexB) const
{
    if (!bComputeInstanceAABBs)
    {
        return false;
    }

    if (!IsValidInstanceIndex(IndexA) || !IsValidInstanceIndex(IndexB))
    {
        return false;
    }

    const FISMInstanceState* StateA = GetInstanceState(IndexA);
    const FISMInstanceState* StateB = GetInstanceState(IndexB);

    if (!StateA || !StateA->bBoundsValid || !StateB || !StateB->bBoundsValid)
    {
        return false;
    }

    return StateA->WorldBounds.Intersect(StateB->WorldBounds);
}


// ============================================================
//  DoesInstanceOverlapBox
// ============================================================

bool UISMRuntimeComponent::DoesInstanceOverlapBox(int32 InstanceIndex, const FBox& Box) const
{
    if (!bComputeInstanceAABBs)
    {
        return false;
    }

    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return false;
    }

    const FISMInstanceState* State = GetInstanceState(InstanceIndex);
    if (!State || !State->bBoundsValid)
    {
        return false;
    }

    return State->WorldBounds.Intersect(Box);
}


#pragma endregion


// ===== Conversion Tracking =====

FISMInstanceHandle UISMRuntimeComponent::GetInstanceHandle(int32 InstanceIndex)
{
    return GetOrCreateHandle(InstanceIndex);
}

FISMInstanceHandle& UISMRuntimeComponent::GetOrCreateHandle(int32 InstanceIndex)
{
    if (!InstanceHandles.Contains(InstanceIndex))
    {
        auto NewHandle = FISMInstanceHandle();
		NewHandle.Component = this;
		NewHandle.InstanceIndex = InstanceIndex;
        InstanceHandles.Add(InstanceIndex, NewHandle);
    }

    return InstanceHandles[InstanceIndex];
}

TArray<FISMInstanceHandle> UISMRuntimeComponent::GetConvertedInstances() const
{
    TArray<FISMInstanceHandle> ConvertedHandles;

    for (const auto& Pair : InstanceHandles)
    {
        if (Pair.Value.IsConvertedToActor())
        {
            ConvertedHandles.Add(Pair.Value);
        }
    }

    return ConvertedHandles;
}

void UISMRuntimeComponent::ReturnAllConvertedInstances(bool bDestroyActors, bool bUpdateTransforms)
{
    TArray<FISMInstanceHandle> ConvertedHandles = GetConvertedInstances();

    for (FISMInstanceHandle& Handle : ConvertedHandles)
    {
        Handle.ReturnToISM(bDestroyActors, bUpdateTransforms);
    }
}

bool UISMRuntimeComponent::IsInstanceConverted(int32 InstanceIndex) const
{
    if (const FISMInstanceHandle* Handle = InstanceHandles.Find(InstanceIndex))
    {
        return Handle->IsConvertedToActor();
    }

    return false;
}

// ===== Subsystem Integration =====

bool UISMRuntimeComponent::RegisterWithSubsystem()
{
    if (UWorld* World = GetWorld())
    {
        if (UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>())
        {
            bool added = Subsystem->RegisterRuntimeComponent(this);
            CachedSubsystem = Subsystem;
            return added;
        }
    }
    return false;
}

void UISMRuntimeComponent::UnregisterFromSubsystem()
{
    if (UISMRuntimeSubsystem* Subsystem = CachedSubsystem.Get())
    {
        Subsystem->UnregisterRuntimeComponent(this);
    }

    CachedSubsystem.Reset();
}

// ===== Helper Functions =====

bool UISMRuntimeComponent::IsValidInstanceIndex(int32 InstanceIndex) const
{
    return ManagedISMComponent &&
        InstanceIndex >= 0 &&
        InstanceIndex < ManagedISMComponent->GetInstanceCount();
}

void UISMRuntimeComponent::BroadcastStateChange(int32 InstanceIndex)
{
    OnInstanceStateChanged.Broadcast(this, InstanceIndex);
    OnInstanceStateChangedNative.Broadcast(this, InstanceIndex);
}

void UISMRuntimeComponent::BroadcastDestruction(int32 InstanceIndex)
{
    OnInstanceDestroyed.Broadcast(this, InstanceIndex);
    OnInstanceDestroyedNative.Broadcast(this, InstanceIndex);
}

void UISMRuntimeComponent::BroadcastTagChange(int32 InstanceIndex)
{
    OnInstanceTagsChanged.Broadcast(this, InstanceIndex);
}

void UISMRuntimeComponent::OnInstancePreDestroy(int32 InstanceIndex)
{
    // Base implementation does nothing
    // Subclasses override for custom pre-destroy logic
}

void UISMRuntimeComponent::OnInstancePostDestroy(int32 InstanceIndex)
{
    // Base implementation does nothing
    // Subclasses override for custom post-destroy logic
}



#pragma region FEEDBACKS

FISMFeedbackTags UISMRuntimeComponent::GetEffectiveFeedbackTags() const
{
    if (!InstanceData) return DefaultFeedbackTags;
    return DefaultFeedbackTags.OverrideWith(InstanceData->FeedbackTags);
}

bool UISMRuntimeComponent::TriggerInstanceFeedback(int32 InstanceIndex, FGameplayTag FeedbackTag, float Intensity)
{
    if (FeedbackTag.IsValid())
    {
        return false;
    }
    if (!IsValidInstanceIndex(InstanceIndex))
    {
        return false;
    }
    return TriggerFeedbackInternal(InstanceIndex, FeedbackTag, Intensity);
}

bool UISMRuntimeComponent::TriggerFeedbackInternal(int32 InstanceIndex, FGameplayTag FeedbackTag, float Intensity)
{
    if (UISMFeedbackSubsystem* FeedbackSubsystem = GetFeedbackSubsystem())
    {
        FISMFeedbackContext Context = FISMFeedbackContext::CreateFromInstance(FeedbackTag, this, InstanceIndex);
        Context.Intensity = Intensity;
        return FeedbackSubsystem->RequestFeedback(Context);
    }
    return false;
}

UISMFeedbackSubsystem* UISMRuntimeComponent::GetFeedbackSubsystem() const
{
    if (CachedFeedbackSubsystem.IsValid()) {
        return CachedFeedbackSubsystem.Get();
    }
    if (UWorld* World = GetWorld())
    {
        if (UISMFeedbackSubsystem* Subsystem = World->GetSubsystem<UISMFeedbackSubsystem>())
        {
            CachedFeedbackSubsystem = Subsystem;
            return Subsystem;
        }
    }
    return nullptr;
}



void UISMRuntimeComponent::TriggerFeedbackOnDestroyInternal(int InstanceIndex, const UActorComponent* Instigator)
{
    TriggerFeedbackInternal([&](const FISMFeedbackTags& Tags) { return Tags.OnDestroy; }, InstanceIndex, Instigator);
}

void UISMRuntimeComponent::TriggerFeedbackOnSpawnInternal(int InstanceIndex, const UActorComponent* Instigator)
{
    TriggerFeedbackInternal([&](const FISMFeedbackTags& Tags) { return Tags.OnSpawn; }, InstanceIndex, Instigator);
}

void UISMRuntimeComponent::TriggerFeedbackOnHideInternal(int InstanceIndex, const UActorComponent* Instigator)
{
    TriggerFeedbackInternal([&](const FISMFeedbackTags& Tags) { return Tags.OnHide; }, InstanceIndex, Instigator);
}

void UISMRuntimeComponent::TriggerFeedbackOnShowInternal(int InstanceIndex, const UActorComponent* Instigator)
{
    TriggerFeedbackInternal([&](const FISMFeedbackTags& Tags) { return Tags.OnShow; }, InstanceIndex, Instigator);
}

void UISMRuntimeComponent::TriggerFeedbackOnTransformUpdateInternal(int InstanceIndex, const UActorComponent* Instigator)
{
    TriggerFeedbackInternal([&](const FISMFeedbackTags& Tags) { return Tags.OnTransformUpdate; }, InstanceIndex, Instigator);
}

void UISMRuntimeComponent::TriggerFeedbackBatchedOnDestroyInternal(TArray<int> InstanceIndexes, const UActorComponent* Instigator)
{
    TriggerFeedbackBatchedInternal([&](const FISMFeedbackTags& Tags) { return Tags.GetBatchDestroyTag(); }, InstanceIndexes, Instigator);
}

void UISMRuntimeComponent::TriggerFeedbackBatchedOnSpawnInternal(TArray<int> InstanceIndexes, const UActorComponent* Instigator)
{
    TriggerFeedbackBatchedInternal([&](const FISMFeedbackTags& Tags) { return Tags.GetBatchSpawnTag(); }, InstanceIndexes, Instigator);
}




void UISMRuntimeComponent::TriggerFeedbackInternal(TFunctionRef<FGameplayTag(const FISMFeedbackTags&)> SelectTag, int InstanceIndex, const UActorComponent* Instigator)
{
    UISMFeedbackSubsystem* Subsystem = GetFeedbackSubsystem();
    if (!Subsystem) {
        return;
    }
    FISMFeedbackTags EffectiveTags = GetEffectiveFeedbackTags();
    FGameplayTag TargetTag = SelectTag(EffectiveTags);
    if (!TargetTag.IsValid()) {
        return;
    }
    FISMFeedbackParticipant InstigatorParticipant = FISMFeedbackParticipant::FromActorComponent(Instigator ? Instigator : this);
    Subsystem->RequestFeedback(FISMFeedbackContext::CreateFromInstance(TargetTag, this, InstanceIndex).WithInstigator(InstigatorParticipant));
}

void UISMRuntimeComponent::TriggerFeedbackBatchedInternal(TFunctionRef<FGameplayTag(const FISMFeedbackTags&)> SelectTag, TArray<int> InstanceIndexes, const UActorComponent* Instigator)
{
    if (InstanceIndexes.Num() == 0) {
        return;
    }
    UISMFeedbackSubsystem* Subsystem = GetFeedbackSubsystem();
    if (!Subsystem) {
        return;
    }

    FISMFeedbackTags EffectiveTags = GetEffectiveFeedbackTags();
    FGameplayTag TargetTag = SelectTag(EffectiveTags);
    if (!TargetTag.IsValid()) {
        return;
    }

    Subsystem->RequestFeedback(FISMFeedbackContext::CreateFromInstanceBatched(TargetTag, this, InstanceIndexes).WithInstigator(FISMFeedbackParticipant::FromActorComponent(Instigator ? Instigator : this)));

}

#pragma endregion
