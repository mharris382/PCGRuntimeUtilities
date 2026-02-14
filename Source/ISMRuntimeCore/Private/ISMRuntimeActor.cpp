// ISMRuntimeActor.cpp
#include "ISMRuntimeActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ISMInstanceHandle.h"
#include "ISMQueryFilter.h"
#include "Engine/World.h"

AISMRuntimeActor::AISMRuntimeActor()
{
    PrimaryActorTick.bCanEverTick = false;
    PrimaryActorTick.bStartWithTickEnabled = false;
    
    // Create bounds box component
    BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
    RootComponent = BoundsBox;
    
    BoundsBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BoundsBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    BoundsBox->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
    BoundsBox->SetBoxExtent(FVector(1000.0f)); // Default size
    BoundsBox->SetHiddenInGame(true);
    
#if WITH_EDITORONLY_DATA
    BoundsBox->bDrawOnlyIfSelected = true;
    BoundsBox->ShapeColor = FColor::Cyan;
#endif
    
    // Default to base runtime component class
    DefaultRuntimeComponentClass = UISMRuntimeComponent::StaticClass();
    
    CachedWorldBounds = FBox(EForceInit::ForceInit);
}

void AISMRuntimeActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    
#if WITH_EDITOR
    if (bAutoCreateRuntimeComponents)
    {
        // In editor, recreate components on construction for immediate feedback
        RecreateRuntimeComponents();
    }
#endif
}

void AISMRuntimeActor::BeginPlay()
{
    Super::BeginPlay();
    
    if (bAutoCreateRuntimeComponents)
    {
        CreateRuntimeComponents();
    }
    
    if (bAutoUpdateBounds)
    {
        RecalculateBounds();
    }
}

void AISMRuntimeActor::EndPlay(const EEndPlayReason::Type EndReason)
{
    DestroyRuntimeComponents();
    
    Super::EndPlay(EndReason);
}

#if WITH_EDITOR
void AISMRuntimeActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    
    FName PropertyName = PropertyChangedEvent.GetPropertyName();
    
    if (PropertyName == GET_MEMBER_NAME_CHECKED(AISMRuntimeActor, bAutoUpdateBounds) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AISMRuntimeActor, BoundsPadding))
    {
        if (bAutoUpdateBounds)
        {
            RecalculateBounds();
        }
    }
    else if (PropertyName == GET_MEMBER_NAME_CHECKED(AISMRuntimeActor, DefaultRuntimeComponentClass) ||
             PropertyName == GET_MEMBER_NAME_CHECKED(AISMRuntimeActor, CustomMappings))
    {
        if (bAutoCreateRuntimeComponents)
        {
            RecreateRuntimeComponents();
        }
    }
}
#endif

int32 AISMRuntimeActor::CreateRuntimeComponents()
{
    // Clean up any existing runtime components first
    DestroyRuntimeComponents();
    
    TArray<UInstancedStaticMeshComponent*> ISMComponents = FindAllISMComponents();
    
    if (ISMComponents.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeActor: No ISM components found on %s"), *GetName());
        return 0;
    }
    
    int32 CreatedCount = 0;
    
    for (UInstancedStaticMeshComponent* ISM : ISMComponents)
    {
        if (!ISM || ISM->GetInstanceCount() == 0)
        {
            continue;
        }
        
        // Determine which component class to use
        TSubclassOf<UISMRuntimeComponent> ComponentClass = GetRuntimeComponentClassForISM(ISM);
        
        if (!ComponentClass)
        {
            UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeActor: No runtime component class for ISM %s"), 
                *ISM->GetName());
            continue;
        }
        
        // Create runtime component
        UISMRuntimeComponent* RuntimeComp = CreateRuntimeComponentForISM(ISM, ComponentClass);
        
        if (RuntimeComp)
        {
            CreatedCount++;
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("ISMRuntimeActor: Created %d runtime components on %s"), 
        CreatedCount, *GetName());
    
    return CreatedCount;
}

void AISMRuntimeActor::DestroyRuntimeComponents()
{
    for (UISMRuntimeComponent* RuntimeComp : CreatedRuntimeComponents)
    {
        if (RuntimeComp)
        {
            RuntimeComp->DestroyComponent();
        }
    }
    
    CreatedRuntimeComponents.Empty();
}

void AISMRuntimeActor::RecreateRuntimeComponents()
{
    DestroyRuntimeComponents();
    CreateRuntimeComponents();
}

TArray<UISMRuntimeComponent*> AISMRuntimeActor::GetRuntimeComponents() const
{
    TArray<UISMRuntimeComponent*> ValidComponents;
    
    for (UISMRuntimeComponent* Comp : CreatedRuntimeComponents)
    {
        if (Comp)
        {
            ValidComponents.Add(Comp);
        }
    }
    
    return ValidComponents;
}

UISMRuntimeComponent* AISMRuntimeActor::GetRuntimeComponentForISM(UInstancedStaticMeshComponent* ISMComponent) const
{
    for (UISMRuntimeComponent* RuntimeComp : CreatedRuntimeComponents)
    {
        if (RuntimeComp && RuntimeComp->ManagedISMComponent == ISMComponent)
        {
            return RuntimeComp;
        }
    }
    
    return nullptr;
}

void AISMRuntimeActor::RecalculateBounds()
{
    CachedWorldBounds = CalculateCombinedBounds();
    bBoundsCalculated = true;
    
    UpdateBoundsVisualization();
}

bool AISMRuntimeActor::IsLocationInBounds(const FVector& WorldLocation, float Tolerance) const
{
    if (!bBoundsCalculated)
    {
        return true; // If bounds not calculated, don't filter
    }
    
    return CachedWorldBounds.ExpandBy(Tolerance).IsInside(WorldLocation);
}

bool AISMRuntimeActor::DoesSphereOverlapBounds(const FVector& Center, float Radius) const
{
    if (!bBoundsCalculated)
    {
        return true; // If bounds not calculated, don't filter
    }
    
    // Check if sphere overlaps AABB
    FVector ClosestPoint = CachedWorldBounds.GetClosestPointTo(Center);
    float DistanceSquared = FVector::DistSquared(Center, ClosestPoint);
    
    return DistanceSquared <= (Radius * Radius);
}

TArray<FISMInstanceHandle> AISMRuntimeActor::QueryInstancesInRadius(
    const FVector& Location,
    float Radius,
    const FISMQueryFilter& Filter) const
{
    TArray<FISMInstanceReference> Results;
    
    // Early out if query doesn't overlap our bounds
    if (bBoundsCalculated && !DoesSphereOverlapBounds(Location, Radius))
    {
        return Results; // Empty - query outside our bounds
    }
    
    // Query all our runtime components
    for (UISMRuntimeComponent* RuntimeComp : CreatedRuntimeComponents)
    {
        if (!RuntimeComp)
        {
            continue;
        }
        
        // Pre-filter by component
        if (!Filter.PassesComponentFilter(RuntimeComp))
        {
            continue;
        }
        
        // Query this component
        TArray<int32> InstanceIndices = RuntimeComp->QueryInstances(Location, Radius, Filter);
        
        // Convert to references
        for (int32 Index : InstanceIndices)
        {
            FISMInstanceReference Ref;
            Ref.Component = RuntimeComp;
            Ref.InstanceIndex = Index;
            Results.Add(Ref);
        }
    }
    
    return Results;
}

TArray<UInstancedStaticMeshComponent*> AISMRuntimeActor::FindAllISMComponents() const
{
    TArray<UInstancedStaticMeshComponent*> ISMComponents;
    
    TArray<UActorComponent*> Components;
    GetComponents(UInstancedStaticMeshComponent::StaticClass(), Components);
    
    for (UActorComponent* Comp : Components)
    {
        if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Comp))
        {
            // Skip the bounds box
            //if (ISM != BoundsBox)
            //{
            ISMComponents.Add(ISM);
            //}
        }
    }
    
    return ISMComponents;
}

TSubclassOf<UISMRuntimeComponent> AISMRuntimeActor::GetRuntimeComponentClassForISM(
    UInstancedStaticMeshComponent* ISMComponent) const
{
    if (!ISMComponent)
    {
        return nullptr;
    }
    
    // Check custom mappings first
    for (const FISMComponentMapping& Mapping : CustomMappings)
    {
        if (Mapping.ISMComponent == ISMComponent && Mapping.RuntimeComponentClass)
        {
            return Mapping.RuntimeComponentClass;
        }
    }
    
    // Fall back to default
    return DefaultRuntimeComponentClass;
}

UISMRuntimeComponent* AISMRuntimeActor::CreateRuntimeComponentForISM(
    UInstancedStaticMeshComponent* ISMComponent,
    TSubclassOf<UISMRuntimeComponent> ComponentClass)
{
    if (!ISMComponent || !ComponentClass)
    {
        return nullptr;
    }
    
    // Create the component
    FName ComponentName = FName(*FString::Printf(TEXT("RuntimeComp_%s"), *ISMComponent->GetName()));
    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(
        this,
        ComponentClass,
        ComponentName,
        RF_Transient
    );
    
    if (!RuntimeComp)
    {
        UE_LOG(LogTemp, Error, TEXT("ISMRuntimeActor: Failed to create runtime component for %s"), 
            *ISMComponent->GetName());
        return nullptr;
    }
    
    // Configure the runtime component
    RuntimeComp->ManagedISMComponent = ISMComponent;
    
    // Check for custom cell size override
    for (const FISMComponentMapping& Mapping : CustomMappings)
    {
        if (Mapping.ISMComponent == ISMComponent && Mapping.OverrideCellSize > 0.0f)
        {
            RuntimeComp->SpatialIndexCellSize = Mapping.OverrideCellSize;
            break;
        }
    }
    
    // Register the component
    RuntimeComp->RegisterComponent();
    
    // Initialize
    if (!RuntimeComp->InitializeInstances())
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeActor: Failed to initialize runtime component for %s"), 
            *ISMComponent->GetName());
        RuntimeComp->DestroyComponent();
        return nullptr;
    }
    
    // Track it
    CreatedRuntimeComponents.Add(RuntimeComp);
    
    UE_LOG(LogTemp, Verbose, TEXT("ISMRuntimeActor: Created %s for ISM %s with %d instances"),
        *ComponentClass->GetName(),
        *ISMComponent->GetName(),
        ISMComponent->GetInstanceCount());
    
    return RuntimeComp;
}

void AISMRuntimeActor::UpdateBoundsVisualization()
{
    if (!BoundsBox || !bBoundsCalculated)
    {
        return;
    }
    
    // Convert world bounds to local bounds
    FVector BoundsCenter = CachedWorldBounds.GetCenter();
    FVector BoundsExtent = CachedWorldBounds.GetExtent();
    
    // Update box component
    BoundsBox->SetWorldLocation(BoundsCenter);
    BoundsBox->SetBoxExtent(BoundsExtent);
}

FBox AISMRuntimeActor::CalculateCombinedBounds() const
{
    FBox CombinedBounds(EForceInit::ForceInit);
    bool bHasAnyInstances = false;
    
    TArray<UInstancedStaticMeshComponent*> ISMComponents = FindAllISMComponents();
    
    for (UInstancedStaticMeshComponent* ISM : ISMComponents)
    {
        if (!ISM || ISM->GetInstanceCount() == 0)
        {
            continue;
        }
        
        // Get bounds of all instances in this ISM
        for (int32 i = 0; i < ISM->GetInstanceCount(); i++)
        {
            FTransform InstanceTransform;
            if (ISM->GetInstanceTransform(i, InstanceTransform, true))
            {
                FVector Location = InstanceTransform.GetLocation();
                
                if (bHasAnyInstances)
                {
                    CombinedBounds += Location;
                }
                else
                {
                    CombinedBounds = FBox(Location, Location);
                    bHasAnyInstances = true;
                }
            }
        }
    }
    
    // Add padding
    if (bHasAnyInstances)
    {
        CombinedBounds = CombinedBounds.ExpandBy(BoundsPadding);
    }
    else
    {
        // No instances - use actor location with default size
        FVector ActorLoc = GetActorLocation();
        CombinedBounds = FBox(ActorLoc - FVector(1000), ActorLoc + FVector(1000));
    }
    
    return CombinedBounds;
}