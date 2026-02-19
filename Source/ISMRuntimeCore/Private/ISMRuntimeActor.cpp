// ISMRuntimeActor.cpp

#include "ISMRuntimeActor.h"
#include "Feedbacks/ISMFeedbackTags.h"
#include "ISMRuntimeComponent.h"
#include "ISMInstanceDataAsset.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"

AISMRuntimeActor::AISMRuntimeActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create bounds box
    BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
    RootComponent = BoundsBox;
    BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoundsBox->SetHiddenInGame(true);

    bAutoUpdateBounds = true;
    BoundsPadding = 500.0f;
}


void AISMRuntimeActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    if (bAutoCreateRuntimeComponents)
    {
#if WITH_EDITOR
        // In editor, we don't create runtime components during construction
        // They'll be created on BeginPlay
#endif
    }
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


FISMFeedbackTags AISMRuntimeActor::DefaultFeedbackTagsFor(UISMRuntimeComponent* component, FISMMeshComponentMapping mapping)
{
    if (!component)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ISMRuntimeActor] DefaultFeedbackTagsFor called with null component"));
        return FISMFeedbackTags();
    }
    auto tags = bSetActorDefaultFeedbackTags ? ActorDefaultFeedbackTags : FISMFeedbackTags();
    tags = mapping.bSetComponentDefaultFeedbackTags ? tags.OverrideWith(mapping.ComponentDefaultFeedbackTags) : tags;
    return component->DefaultFeedbackTags = tags.OverrideWith(component->DefaultFeedbackTags);

}


#if WITH_EDITOR
void AISMRuntimeActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    FName PropertyName = PropertyChangedEvent.GetPropertyName();

    if (PropertyName == GET_MEMBER_NAME_CHECKED(AISMRuntimeActor, MeshMappings))
    {
        // Mesh mappings changed - might want to refresh in editor
    }
}
#endif

int32 AISMRuntimeActor::CreateRuntimeComponents()
{
    // Clean up any existing components first
    DestroyRuntimeComponents();

    // Find all ISM components
    TArray<UInstancedStaticMeshComponent*> ISMComponents = FindAllISMComponents();

    UE_LOG(LogTemp, Display, TEXT("[ISMRuntimeActor] Found %d ISM components on %s"),
        ISMComponents.Num(), *GetName());

    int32 CreatedCount = 0;

    for (UInstancedStaticMeshComponent* ISMComp : ISMComponents)
    {
        if (!ISMComp || ISMComp->GetInstanceCount() == 0)
            continue;

        // Get the mesh
        UStaticMesh* Mesh = ISMComp->GetStaticMesh();
        if (!Mesh)
        {
            UE_LOG(LogTemp, Warning, TEXT("[ISMRuntimeActor] ISM component %s has no mesh, skipping"),
                *ISMComp->GetName());
            continue;
        }

        // Find mapping for this mesh
        const FISMMeshComponentMapping* MeshMapping = FindMeshMapping(Mesh);
        if (!MeshMapping)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[ISMRuntimeActor] No mapping found for mesh %s, skipping"),
                *Mesh->GetName());
            continue;
        }

        if (!MeshMapping->RuntimeComponentClass)
        {
            UE_LOG(LogTemp, Warning, TEXT("[ISMRuntimeActor] Mesh mapping for %s has no component class, skipping"),
                *Mesh->GetName());
            continue;
        }

        UE_LOG(LogTemp, Display, TEXT("[ISMRuntimeActor] Creating %s for mesh %s"),
            *MeshMapping->RuntimeComponentClass->GetClassPathName().ToString(),
            *Mesh->GetName());

		for (int i = 0; i < MeshMapping->InstanceDataAssets.Num(); i++)
        {
            UISMInstanceDataAsset* DataAsset = MeshMapping->InstanceDataAssets[i];
            if (!DataAsset)
            {
                UE_LOG(LogTemp, Warning, TEXT("[ISMRuntimeActor] Null data asset in mapping for mesh %s, skipping"),
                    *Mesh->GetName());
                continue;
            }
			
            if (DataAsset->GetStaticMesh() != Mesh)
            {
                UE_LOG(LogTemp, Warning, TEXT("[ISMRuntimeActor] Data asset %s mesh does not match ISM mesh %s, skipping"),
                    *DataAsset->GetName(), *Mesh->GetName());
                continue;
            }

            UISMRuntimeComponent* RuntimeComp = CreateRuntimeComponentForISM(
                ISMComp,
                MeshMapping->RuntimeComponentClass,
                MeshMapping,
                i);
            if (RuntimeComp)
            {
                CreatedRuntimeComponents.Add(RuntimeComp);
                CreatedCount++;
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[ISMRuntimeActor] Created %d runtime components on %s"),
        CreatedCount, *GetName());

    return CreatedCount;
}

void AISMRuntimeActor::DestroyRuntimeComponents()
{
    for (UISMRuntimeComponent* Comp : CreatedRuntimeComponents)
    {
        if (Comp && !Comp->IsBeingDestroyed())
        {
            Comp->DestroyComponent();
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
    TArray<UISMRuntimeComponent*> Result;

    for (TWeakObjectPtr<UISMRuntimeComponent> CompPtr : CreatedRuntimeComponents)
    {
        if (UISMRuntimeComponent* Comp = CompPtr.Get())
        {
            Result.Add(Comp);
        }
    }

    return Result;
}

UISMRuntimeComponent* AISMRuntimeActor::GetRuntimeComponentForISM(UInstancedStaticMeshComponent* ISMComponent) const
{
    for (UISMRuntimeComponent* Comp : CreatedRuntimeComponents)
    {
        if (Comp && Comp->ManagedISMComponent == ISMComponent)
        {
            return Comp;
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
        return false;

    return CachedWorldBounds.ExpandBy(Tolerance).IsInside(WorldLocation);
}

bool AISMRuntimeActor::DoesSphereOverlapBounds(const FVector& Center, float Radius) const
{
    if (!bBoundsCalculated)
        return false;

    FBox SphereBox = FBox::BuildAABB(Center, FVector(Radius));
    return CachedWorldBounds.Intersect(SphereBox);
}

TArray<FISMInstanceHandle> AISMRuntimeActor::QueryInstancesInRadius(
    const FVector& Location,
    float Radius,
    const FISMQueryFilter& Filter) const
{
    TArray<FISMInstanceHandle> Results;

    // Quick bounds check
    if (!DoesSphereOverlapBounds(Location, Radius))
        return Results;

    // Query each runtime component
    for (UISMRuntimeComponent* Comp : CreatedRuntimeComponents)
    {
        if (!Comp)
            continue;

        TArray<int32> Instances = Comp->QueryInstances(Location, Radius, Filter);

        for (int32 InstanceIndex : Instances)
        {
            FISMInstanceHandle Handle;
            Handle.InstanceIndex = InstanceIndex;
            Handle.Component = Comp;
            Results.Add(Handle);
        }
    }

    return Results;
}

// ===== Helper Functions =====

TArray<UInstancedStaticMeshComponent*> AISMRuntimeActor::FindAllISMComponents() const
{
    TArray<UInstancedStaticMeshComponent*> ISMComponents;
    GetComponents<UInstancedStaticMeshComponent>(ISMComponents);
    for (AActor* Parent : RuntimeComponentParents)
    {
        if (Parent)
        {
            TArray<UInstancedStaticMeshComponent*> ParentISMComponents;
            Parent->GetComponents<UInstancedStaticMeshComponent>(ParentISMComponents);
            ISMComponents.Append(ParentISMComponents);
        }
	}
    return ISMComponents;
}

const FISMMeshComponentMapping* AISMRuntimeActor::FindMeshMapping(UStaticMesh* Mesh) const
{
    if (!Mesh)
        return nullptr;

    for (const FISMMeshComponentMapping& Mapping : MeshMappings)
    {
		for (const UISMInstanceDataAsset* DataAsset : Mapping.InstanceDataAssets)
        {
			UStaticMesh* DataAssetMesh = DataAsset->GetStaticMesh();
			if (DataAssetMesh == Mesh)
            {
                return &Mapping;
            }
        }
        }

    return nullptr;
}

TSubclassOf<UISMRuntimeComponent> AISMRuntimeActor::GetRuntimeComponentClassForISM(
    UInstancedStaticMeshComponent* ISMComponent) const
{
    if (!ISMComponent)
        return nullptr;

    UStaticMesh* Mesh = ISMComponent->GetStaticMesh();
    const FISMMeshComponentMapping* Mapping = FindMeshMapping(Mesh);

    return Mapping ? Mapping->RuntimeComponentClass : nullptr;
}

UISMRuntimeComponent* AISMRuntimeActor::CreateRuntimeComponentForISM(
    UInstancedStaticMeshComponent* ISMComponent,
    TSubclassOf<UISMRuntimeComponent> ComponentClass,
    const FISMMeshComponentMapping* MeshMapping, 
    const int DataInstanceIndex)
{
    if (!ISMComponent || !ComponentClass)
        return nullptr;

    // Generate unique name for the runtime component
    FName CompName = MakeUniqueObjectName(this, ComponentClass,
        FName(*FString::Printf(TEXT("RuntimeComp_%s"), *ISMComponent->GetName())));

	AActor* ISMOwner = ISMComponent->GetOwner(); 
    // Create the component
    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(
        ISMOwner,          // or should this actor be the owner of the runtime component, regardless of where the ISM is?
        ComponentClass,
        CompName,
        RF_Transient
    );

    if (!RuntimeComp)
    {
        UE_LOG(LogTemp, Error, TEXT("[ISMRuntimeActor] Failed to create runtime component"));
        return nullptr;
    }

    // Configure the component
    RuntimeComp->ManagedISMComponent = ISMComponent;


    // Assign Feedbacks from Actor
	FISMFeedbackTags t = DefaultFeedbackTagsFor(RuntimeComp, *MeshMapping);
	RuntimeComp->DefaultFeedbackTags = t;

    //compine Gameplay Tags defaults
    auto tags = RuntimeComp->ISMComponentTags;
	tags.AppendTags(DefaultTags);
	tags.AppendTags(MeshMapping->DefaultComponentTags);

    // Apply mesh mapping settings
    if (MeshMapping)
    {
        if (MeshMapping->OverrideCellSize > 0.0f)
        {
            RuntimeComp->SpatialIndexCellSize = MeshMapping->OverrideCellSize;
        }

        if (MeshMapping->InstanceDataAssets[DataInstanceIndex])
        {
			RuntimeComp->SetInstanceDataAsset(MeshMapping->InstanceDataAssets[DataInstanceIndex]);
        }
    }

    // Register and initialize
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // Track for cleanup
    CreatedRuntimeComponents.Add(RuntimeComp);

    UE_LOG(LogTemp, Display, TEXT("[ISMRuntimeActor] Created %s for ISM %s (%d instances)"),
        *RuntimeComp->GetName(),
        *ISMComponent->GetName(),
        ISMComponent->GetInstanceCount());

    return RuntimeComp;
}

void AISMRuntimeActor::UpdateBoundsVisualization()
{
    if (!BoundsBox || !bBoundsCalculated)
        return;

    FVector Center = CachedWorldBounds.GetCenter();
    FVector Extent = CachedWorldBounds.GetExtent() + FVector(BoundsPadding);

    BoundsBox->SetWorldLocation(Center);
    BoundsBox->SetBoxExtent(Extent);
}

FBox AISMRuntimeActor::CalculateCombinedBounds() const
{
    FBox CombinedBounds(ForceInit);
    bool bHasValidBounds = false;

    for (UISMRuntimeComponent* Comp : CreatedRuntimeComponents)
    {
        if (!Comp || !Comp->ManagedISMComponent)
            continue;

        FBox CompBounds = Comp->ManagedISMComponent->CalcBounds(Comp->ManagedISMComponent->GetComponentTransform()).GetBox();

        if (bHasValidBounds)
        {
            CombinedBounds += CompBounds;
        }
        else
        {
            CombinedBounds = CompBounds;
            bHasValidBounds = true;
        }
    }

    return CombinedBounds;
}
