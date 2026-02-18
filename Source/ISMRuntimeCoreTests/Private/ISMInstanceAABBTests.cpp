// ISMInstanceAABBTests.cpp
// Tests for per-instance AABB caching and queries.
// All tests are expected to FAIL until implementation is complete.

#include "ISMRuntimeComponent.h"
#include "ISMInstanceDataAsset.h"
#include "ISMInstanceState.h"
#include "ISMQueryFilter.h"
#include "ISMRuntimeSubsystem.h"
#include "ISMTestHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

// ============================================================
//  SECTION 1: DataAsset Bounds Caching
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBDataAssetCacheTest,
    "ISMRuntime.Core.AABB.DataAsset.BoundsAreCachedFromMesh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBDataAssetCacheTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();

    // Load the engine's default cube - guaranteed to exist, known dimensions (50x50x50 half-extent)
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
        nullptr,
        TEXT("/Engine/BasicShapes/Cube.Cube")
    );
    TestNotNull("Cube mesh should load", CubeMesh);

    // ACT - Assign mesh, which should trigger bound caching
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->RefreshCachedBounds();

    // ASSERT - Cached bounds should be valid and non-zero
    FBox CachedBounds = DataAsset->CachedLocalBounds;
    TestTrue("Cached local bounds should be valid", CachedBounds.IsValid != 0);
    TestFalse("Cached bounds should not be zero-size",
        CachedBounds.GetSize().IsNearlyZero());

    // Bounds should be symmetric around origin for the cube
    FVector Center = CachedBounds.GetCenter();
    TestTrue("Cube local bounds should be centered near origin",
        Center.IsNearlyZero(1.0f));

    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBDataAssetPaddingTest,
    "ISMRuntime.Core.AABB.DataAsset.PaddingExpandsBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBDataAssetPaddingTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->RefreshCachedBounds();

    FBox UnpaddedBounds = DataAsset->GetEffectiveLocalBounds();

    // ACT - Apply uniform padding
    const float PaddingAmount = 25.0f;
    DataAsset->BoundsPadding = PaddingAmount;

    FBox PaddedBounds = DataAsset->GetEffectiveLocalBounds();

    // ASSERT - Each side should be expanded by padding amount
    FVector UnpaddedExtent = UnpaddedBounds.GetExtent();
    FVector PaddedExtent   = PaddedBounds.GetExtent();

    TestTrue("Padded X extent should be larger by padding",
        FMath::IsNearlyEqual(PaddedExtent.X, UnpaddedExtent.X + PaddingAmount, 0.1f));
    TestTrue("Padded Y extent should be larger by padding",
        FMath::IsNearlyEqual(PaddedExtent.Y, UnpaddedExtent.Y + PaddingAmount, 0.1f));
    TestTrue("Padded Z extent should be larger by padding",
        FMath::IsNearlyEqual(PaddedExtent.Z, UnpaddedExtent.Z + PaddingAmount, 0.1f));

    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBDataAssetNonUniformPaddingTest,
    "ISMRuntime.Core.AABB.DataAsset.NonUniformPaddingExpandsCorrectAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBDataAssetNonUniformPaddingTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->RefreshCachedBounds();

    FBox BaseBounds = DataAsset->GetEffectiveLocalBounds();

    // ACT - Apply non-uniform padding (Z only)
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->BoundsPaddingExtent = FVector(0.0f, 0.0f, 50.0f);

    FBox PaddedBounds = DataAsset->GetEffectiveLocalBounds();

    // ASSERT
    FVector BaseExtent   = BaseBounds.GetExtent();
    FVector PaddedExtent = PaddedBounds.GetExtent();

    TestTrue("X extent should be unchanged",
        FMath::IsNearlyEqual(PaddedExtent.X, BaseExtent.X, 0.1f));
    TestTrue("Y extent should be unchanged",
        FMath::IsNearlyEqual(PaddedExtent.Y, BaseExtent.Y, 0.1f));
    TestTrue("Z extent should be expanded by 50",
        FMath::IsNearlyEqual(PaddedExtent.Z, BaseExtent.Z + 50.0f, 0.1f));

    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBDataAssetOverrideTest,
    "ISMRuntime.Core.AABB.DataAsset.ManualOverrideReplacesComputedBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBDataAssetOverrideTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->RefreshCachedBounds();

    // ACT - Override with a known box
    FBox KnownBox(FVector(-100, -200, -300), FVector(100, 200, 300));
    DataAsset->bOverrideBounds = true;
    DataAsset->BoundsOverride  = KnownBox;

    FBox EffectiveBounds = DataAsset->GetEffectiveLocalBounds();

    // ASSERT - Should return the override, not the mesh bounds
    TestTrue("Override min should match",
        EffectiveBounds.Min.Equals(KnownBox.Min, 0.1f));
    TestTrue("Override max should match",
        EffectiveBounds.Max.Equals(KnownBox.Max, 0.1f));

    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBDataAssetNullMeshTest,
    "ISMRuntime.Core.AABB.DataAsset.NullMeshProducesInvalidBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBDataAssetNullMeshTest::RunTest(const FString& Parameters)
{
    // ARRANGE - DataAsset with no mesh assigned
    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    DataAsset->StaticMesh = nullptr;

    // ACT
    DataAsset->RefreshCachedBounds();
    FBox EffectiveBounds = DataAsset->GetEffectiveLocalBounds();

    // ASSERT - Should return an invalid/zero box, not crash
    TestFalse("Bounds should not be valid when mesh is null", EffectiveBounds.IsValid != 0);

    return true;
}


// ============================================================
//  SECTION 2: FISMInstanceState WorldBounds
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBStateDefaultInvalidTest,
    "ISMRuntime.Core.AABB.InstanceState.DefaultStateHasInvalidBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBStateDefaultInvalidTest::RunTest(const FString& Parameters)
{
    // A freshly-constructed FISMInstanceState should have invalid bounds
    // until the component writes them in.

    FISMInstanceState State;

    TestFalse("Default state bounds should be invalid", State.bBoundsValid);
    TestFalse("Default WorldBounds should be invalid", State.WorldBounds.IsValid!=0);

    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBStateOverlapTest,
    "ISMRuntime.Core.AABB.InstanceState.OverlapHelperWorks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBStateOverlapTest::RunTest(const FString& Parameters)
{
    FISMInstanceState State;
    State.WorldBounds = FBox(FVector(-50, -50, -50), FVector(50, 50, 50));
    State.bBoundsValid = true;

    FBox Overlapping   = FBox(FVector(0, 0, 0),     FVector(100, 100, 100));
    FBox NonOverlapping = FBox(FVector(200, 200, 200), FVector(300, 300, 300));

    TestTrue("Should overlap an intersecting box", State.OverlapsWith(Overlapping));
    TestFalse("Should not overlap a distant box",  State.OverlapsWith(NonOverlapping));

    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBStateContainsTest,
    "ISMRuntime.Core.AABB.InstanceState.ContainsPointWorks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBStateContainsTest::RunTest(const FString& Parameters)
{
    FISMInstanceState State;
    State.WorldBounds = FBox(FVector(-50, -50, -50), FVector(50, 50, 50));
    State.bBoundsValid = true;

    TestTrue("Origin should be inside bounds",      State.Contains(FVector::ZeroVector));
    TestFalse("Far point should be outside bounds", State.Contains(FVector(200, 0, 0)));

    return true;
}


// ============================================================
//  SECTION 3: ISMRuntimeComponent — WorldBounds computed on add/update
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBComputedOnAddTest,
    "ISMRuntime.Core.AABB.Component.BoundsComputedWhenInstanceAdded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBComputedOnAddTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // ACT - Add an instance at a known location
    FTransform Transform;
    Transform.SetLocation(FVector(500, 0, 0));
    int32 NewIndex = RuntimeComp->AddInstance(Transform);

    // ASSERT - WorldBounds should be valid immediately
    FBox InstanceBounds = RuntimeComp->GetInstanceWorldBounds(NewIndex);
    TestTrue("Instance bounds should be valid after add", InstanceBounds.IsValid!=0);
    TestTrue("Instance bounds should contain the instance location",
        InstanceBounds.IsInsideOrOn(FVector(500, 0, 0)));

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBUpdatedOnTransformChangeTest,
    "ISMRuntime.Core.AABB.Component.BoundsUpdatedWhenTransformChanges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBUpdatedOnTransformChangeTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    FTransform InitialTransform;
    InitialTransform.SetLocation(FVector(0, 0, 0));
    int32 Idx = RuntimeComp->AddInstance(InitialTransform);

    FBox InitialBounds = RuntimeComp->GetInstanceWorldBounds(Idx);

    // ACT - Move the instance far away
    FTransform NewTransform;
    NewTransform.SetLocation(FVector(10000, 0, 0));
    RuntimeComp->UpdateInstanceTransform(Idx, NewTransform);

    FBox UpdatedBounds = RuntimeComp->GetInstanceWorldBounds(Idx);

    // ASSERT - Bounds should have moved
    TestFalse("Bounds should have changed after transform update",
        InitialBounds.Min.Equals(UpdatedBounds.Min, 1.0f));
    TestTrue("Updated bounds should contain the new location",
        UpdatedBounds.IsInsideOrOn(FVector(10000, 0, 0)));
    TestFalse("Updated bounds should NOT contain the old location",
        UpdatedBounds.IsInsideOrOn(FVector(0, 0, 0)));

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBScaleAffectsBoundsTest,
    "ISMRuntime.Core.AABB.Component.ScaleCorrectlyAffectsBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBScaleAffectsBoundsTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // Add instance at scale 1
    FTransform Scale1Transform;
    Scale1Transform.SetLocation(FVector::ZeroVector);
    Scale1Transform.SetScale3D(FVector(1.0f));
    int32 Idx1 = RuntimeComp->AddInstance(Scale1Transform);

    // Add instance at scale 3
    FTransform Scale3Transform;
    Scale3Transform.SetLocation(FVector::ZeroVector);
    Scale3Transform.SetScale3D(FVector(3.0f));
    int32 Idx2 = RuntimeComp->AddInstance(Scale3Transform);

    FBox Bounds1 = RuntimeComp->GetInstanceWorldBounds(Idx1);
    FBox Bounds3 = RuntimeComp->GetInstanceWorldBounds(Idx2);

    // ASSERT - Scaled instance should have proportionally larger bounds
    FVector Extent1 = Bounds1.GetExtent();
    FVector Extent3 = Bounds3.GetExtent();

    TestTrue("Scale-3 instance X extent should be ~3x scale-1",
        FMath::IsNearlyEqual(Extent3.X, Extent1.X * 3.0f, 1.0f));
    TestTrue("Scale-3 instance Y extent should be ~3x scale-1",
        FMath::IsNearlyEqual(Extent3.Y, Extent1.Y * 3.0f, 1.0f));

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBInvalidIndexTest,
    "ISMRuntime.Core.AABB.Component.InvalidIndexReturnsInvalidBox",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBInvalidIndexTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeComponent* RuntimeComp = FISMTestHelpers::CreateTestComponent(World, 3);

    // ACT - Request bounds for an out-of-range index
    FBox Result = RuntimeComp->GetInstanceWorldBounds(9999);

    // ASSERT
    TestFalse("Out-of-range index should return invalid box", Result.IsValid!=0);

    World->DestroyWorld(false);
    return true;
}


// ============================================================
//  SECTION 4: Opt-Out Flag
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBOptOutSkipsComputeTest,
    "ISMRuntime.Core.AABB.OptOut.DisabledComponentSkipsBoundsCompute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBOptOutSkipsComputeTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = false;   // <-- OPT OUT
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // ACT
    FTransform T;
    T.SetLocation(FVector(100, 0, 0));
    int32 Idx = RuntimeComp->AddInstance(T);

    FBox Bounds = RuntimeComp->GetInstanceWorldBounds(Idx);

    // ASSERT - Bounds should be invalid because computation was skipped
    TestFalse("Opted-out component should return invalid bounds", Bounds.IsValid != 0);

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBOptOutDoesNotAffectOtherQueriesTest,
    "ISMRuntime.Core.AABB.OptOut.DisabledComponentStillAnswersSpatialQueries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBOptOutDoesNotAffectOtherQueriesTest::RunTest(const FString& Parameters)
{
    // Opt-out should only disable AABB computation.
    // Radius/box spatial queries (center-point based) must still work.

    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->RegisterComponent();

    for (int32 i = 0; i < 5; i++)
    {
        FTransform T;
        T.SetLocation(FVector(i * 100.0f, 0, 0));
        ISM->AddInstance(T);
    }

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->bComputeInstanceAABBs = false;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // ACT - Standard radius query should still work
    TArray<int32> Results = RuntimeComp->GetInstancesInRadius(FVector::ZeroVector, 250.0f);

    // ASSERT
    TestTrue("Spatial radius query should still work when AABB opt-out is set",
        Results.Num() > 0);
    TestTrue("Should find instance 0", Results.Contains(0));

    World->DestroyWorld(false);
    return true;
}


// ============================================================
//  SECTION 5: AABB Overlap Queries on ISMRuntimeComponent
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBOverlapBoxQueryTest,
    "ISMRuntime.Core.AABB.Query.OverlappingBox",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBOverlapBoxQueryTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    // Cube mesh has half-extent ~50. Place 3 instances well apart so AABBs don't touch.
    // Instance 0 at origin  (AABB: ~-50 to +50)
    // Instance 1 at X=500   (AABB: ~450 to 550)
    // Instance 2 at X=5000  (AABB far away)
    for (int32 i = 0; i < 3; i++)
    {
        FTransform T;
        T.SetLocation(FVector(i == 2 ? 5000.0f : i * 500.0f, 0, 0));
        ISM->AddInstance(T);
    }

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // ACT - Query box that covers only instances 0 and 1
    FBox QueryBox(FVector(-100, -100, -100), FVector(600, 100, 100));
    TArray<int32> Results = RuntimeComp->GetInstancesOverlappingBox(QueryBox);

    // ASSERT
    TestEqual("Should find exactly 2 instances", Results.Num(), 2);
    TestTrue("Should contain instance 0", Results.Contains(0));
    TestTrue("Should contain instance 1", Results.Contains(1));
    TestFalse("Should not contain instance 2", Results.Contains(2));

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBOverlapSphereQueryTest,
    "ISMRuntime.Core.AABB.Query.OverlappingSphere",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBOverlapSphereQueryTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    // 3 instances at known locations
    TArray<FVector> Locations = {
        FVector(0, 0, 0),
        FVector(1000, 0, 0),
        FVector(5000, 0, 0)
    };
    for (const FVector& Loc : Locations)
    {
        FTransform T;
        T.SetLocation(Loc);
        ISM->AddInstance(T);
    }

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // ACT - Sphere that overlaps instances 0 and 1 but not 2
    TArray<int32> Results = RuntimeComp->GetInstancesOverlappingSphere(FVector::ZeroVector, 1100.0f);

    // ASSERT
    TestTrue("Should contain instance 0", Results.Contains(0));
    TestTrue("Should contain instance 1", Results.Contains(1));
    TestFalse("Should not contain distant instance 2", Results.Contains(2));

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBInstanceOverlapInstanceTest,
    "ISMRuntime.Core.AABB.Query.InstanceOverlapsInstance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBInstanceOverlapInstanceTest::RunTest(const FString& Parameters)
{
    // ARRANGE - Two instances at the same location (guaranteed overlap),
    //           one far away (guaranteed no overlap).
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    // Instance 0 and 1 at origin (overlapping)
    FTransform AtOrigin;
    AtOrigin.SetLocation(FVector::ZeroVector);
    ISM->AddInstance(AtOrigin);
    ISM->AddInstance(AtOrigin);

    // Instance 2 far away
    FTransform FarAway;
    FarAway.SetLocation(FVector(10000, 0, 0));
    ISM->AddInstance(FarAway);

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // ACT
    TArray<int32> Overlapping = RuntimeComp->GetInstancesOverlappingInstance(0);

    // ASSERT
    TestFalse("Result should not include the query instance itself", Overlapping.Contains(0));
    TestTrue("Instance 1 should overlap instance 0",  Overlapping.Contains(1));
    TestFalse("Instance 2 should not overlap instance 0", Overlapping.Contains(2));

    // Also test the direct two-instance check
    TestTrue("DoInstancesOverlap(0,1) should be true",  RuntimeComp->DoInstancesOverlap(0, 1));
    TestFalse("DoInstancesOverlap(0,2) should be false", RuntimeComp->DoInstancesOverlap(0, 2));

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBDoesInstanceOverlapBoxTest,
    "ISMRuntime.Core.AABB.Query.DoesInstanceOverlapBox",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBDoesInstanceOverlapBoxTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    FTransform T;
    T.SetLocation(FVector::ZeroVector);
    ISM->AddInstance(T);

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    FBox OverlappingBox(FVector(-10, -10, -10), FVector(10, 10, 10));
    FBox NonOverlappingBox(FVector(500, 500, 500), FVector(600, 600, 600));

    TestTrue("Instance should overlap nearby box",       RuntimeComp->DoesInstanceOverlapBox(0, OverlappingBox));
    TestFalse("Instance should not overlap distant box", RuntimeComp->DoesInstanceOverlapBox(0, NonOverlappingBox));

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBDestroyedInstanceExcludedTest,
    "ISMRuntime.Core.AABB.Query.DestroyedInstancesExcludedByDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FISMAABBDestroyedInstanceExcludedTest::RunTest(const FString& Parameters)
{
    // ARRANGE - Overlapping instances, destroy one
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    FTransform T;
    T.SetLocation(FVector::ZeroVector);
    ISM->AddInstance(T);   // Index 0
    ISM->AddInstance(T);   // Index 1 — same spot, will be destroyed

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    RuntimeComp->DestroyInstance(1);

    // ACT - Overlap query on instance 0, default excludes destroyed
    TArray<int32> Results = RuntimeComp->GetInstancesOverlappingInstance(0, false);

    // ASSERT - Destroyed instance 1 should not appear
    TestFalse("Destroyed instance should not appear in default overlap results",
        Results.Contains(1));

    // ACT - With bIncludeDestroyed = true
    TArray<int32> ResultsWithDestroyed = RuntimeComp->GetInstancesOverlappingInstance(0, true);

    // ASSERT - Should now include it
    TestTrue("Destroyed instance should appear when bIncludeDestroyed is true",
        ResultsWithDestroyed.Contains(1));

    World->DestroyWorld(false);
    return true;
}


// ============================================================
//  SECTION 6: ISMQueryFilter AABB support
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBQueryFilterBoxTest,
    "ISMRuntime.Core.AABB.QueryFilter.AABBFilterExcludesNonOverlapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter

)

bool FISMAABBQueryFilterBoxTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->RefreshCachedBounds();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->SetStaticMesh(CubeMesh);
    ISM->RegisterComponent();

    for (int32 i = 0; i < 3; i++)
    {
        FTransform T;
        T.SetLocation(FVector(i * 1000.0f, 0, 0));
        ISM->AddInstance(T);
    }

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->InstanceData = DataAsset;
    RuntimeComp->bComputeInstanceAABBs = true;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    // ACT - Build a filter with an AABB constraint covering only instance 0
    FISMQueryFilter Filter;
    Filter.bFilterByAABB = true;
    Filter.AABBOverlapBox = FBox(FVector(-200, -200, -200), FVector(200, 200, 200));

    TArray<int32> Results = RuntimeComp->QueryInstances(FVector::ZeroVector, 10000.0f, Filter);

    // ASSERT
    TestTrue("Filter should include instance 0", Results.Contains(0));
    TestFalse("Filter should exclude instance 1", Results.Contains(1));
    TestFalse("Filter should exclude instance 2", Results.Contains(2));

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBQueryFilterExcludeUnavailableTest,
    "ISMRuntime.Core.AABB.QueryFilter.ExcludesOptOutComponentsWhenFlagSet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter

)

bool FISMAABBQueryFilterExcludeUnavailableTest::RunTest(const FString& Parameters)
{
    // When bExcludeIfAABBUnavailable is true, instances on opt-out components
    // should be excluded from AABB-filtered queries entirely.

    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->RegisterComponent();
    FTransform T;
    T.SetLocation(FVector::ZeroVector);
    ISM->AddInstance(T);

    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->bComputeInstanceAABBs = false; // no AABB data
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();

    FISMQueryFilter Filter;
    Filter.bFilterByAABB = true;
    Filter.AABBOverlapBox = FBox(FVector(-1000, -1000, -1000), FVector(1000, 1000, 1000));
    Filter.bExcludeIfAABBUnavailable = true;

    TArray<int32> Results = RuntimeComp->QueryInstances(FVector::ZeroVector, 5000.0f, Filter);

    TestEqual("Opt-out instances should be excluded when bExcludeIfAABBUnavailable is set",
        Results.Num(), 0);

    World->DestroyWorld(false);
    return true;
}


// ============================================================
//  SECTION 7: Cross-component queries on ISMRuntimeSubsystem
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBSubsystemOverlapBoxTest,
    "ISMRuntime.Core.AABB.Subsystem.CrossComponentOverlappingBox",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter

)

bool FISMAABBSubsystemOverlapBoxTest::RunTest(const FString& Parameters)
{
    // ARRANGE - Two components, instances spread across them
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    TestNotNull("Subsystem should exist", Subsystem);

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->RefreshCachedBounds();

    auto MakeComponent = [&](AActor* Owner, FVector Location) -> UISMRuntimeComponent*
    {
        UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Owner);
        ISM->SetStaticMesh(CubeMesh);
        ISM->RegisterComponent();
        FTransform T;
        T.SetLocation(Location);
        ISM->AddInstance(T);

        UISMRuntimeComponent* Comp = NewObject<UISMRuntimeComponent>(Owner);
        Comp->ManagedISMComponent = ISM;
        Comp->InstanceData = DataAsset;
        Comp->bComputeInstanceAABBs = true;
        Comp->RegisterComponent();
        Comp->InitializeInstances();
        return Comp;
    };

    AActor* Actor1 = World->SpawnActor<AActor>();
    AActor* Actor2 = World->SpawnActor<AActor>();

    UISMRuntimeComponent* CompA = MakeComponent(Actor1, FVector(0, 0, 0));
    UISMRuntimeComponent* CompB = MakeComponent(Actor2, FVector(5000, 0, 0));

    // ACT - Query a box that only covers CompA's instance
    FISMQueryFilter Filter;
    FBox QueryBox(FVector(-200, -200, -200), FVector(200, 200, 200));
    TArray<FISMInstanceHandle> Results = Subsystem->QueryInstancesOverlappingBox(QueryBox, Filter);

    // ASSERT
    TestEqual("Should find exactly 1 instance", Results.Num(), 1);
    if(Results.Num()>0)
        TestEqual("The result should be from CompA", Results[0].Component.Get(), CompA);

    World->DestroyWorld(false);
    return true;
}

// ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMAABBSubsystemOverlapInstanceCrossComponentTest,
    "ISMRuntime.Core.AABB.Subsystem.CrossComponentInstanceOverlap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter

)

bool FISMAABBSubsystemOverlapInstanceCrossComponentTest::RunTest(const FString& Parameters)
{
    // Tests QueryInstancesOverlappingInstance when the overlapping instances
    // live on different components.

    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    TestNotNull("Subsystem should exist", Subsystem);

    UISMInstanceDataAsset* DataAsset = NewObject<UISMInstanceDataAsset>();
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    DataAsset->StaticMesh = CubeMesh;
    DataAsset->BoundsPadding = 0.0f;
    DataAsset->RefreshCachedBounds();

    // CompA: instance at origin
    AActor* Actor1 = World->SpawnActor<AActor>();
    UInstancedStaticMeshComponent* ISM_A = NewObject<UInstancedStaticMeshComponent>(Actor1);
    ISM_A->SetStaticMesh(CubeMesh);
    ISM_A->RegisterComponent();
    FTransform TOrigin;
    TOrigin.SetLocation(FVector::ZeroVector);
    ISM_A->AddInstance(TOrigin);
    UISMRuntimeComponent* CompA = NewObject<UISMRuntimeComponent>(Actor1);
    CompA->ManagedISMComponent = ISM_A;
    CompA->InstanceData = DataAsset;
    CompA->bComputeInstanceAABBs = true;
    CompA->RegisterComponent();
    CompA->InitializeInstances();

    // CompB: instance also at origin (overlaps CompA's instance), another far away
    AActor* Actor2 = World->SpawnActor<AActor>();
    UInstancedStaticMeshComponent* ISM_B = NewObject<UInstancedStaticMeshComponent>(Actor2);
    ISM_B->SetStaticMesh(CubeMesh);
    ISM_B->RegisterComponent();
    ISM_B->AddInstance(TOrigin);              // index 0 - overlaps
    FTransform TFar;
    TFar.SetLocation(FVector(10000, 0, 0));
    ISM_B->AddInstance(TFar);                 // index 1 - does not overlap
    UISMRuntimeComponent* CompB = NewObject<UISMRuntimeComponent>(Actor2);
    CompB->ManagedISMComponent = ISM_B;
    CompB->InstanceData = DataAsset;
    CompB->bComputeInstanceAABBs = true;
    CompB->RegisterComponent();
    CompB->InitializeInstances();

    // ACT
    FISMInstanceHandle QueryHandle = CompA->GetInstanceHandle(0);
    FISMQueryFilter Filter;
    TArray<FISMInstanceHandle> Results = Subsystem->QueryInstancesOverlappingInstance(QueryHandle, Filter);

    // ASSERT
    // Should find CompB index 0, should NOT find CompB index 1, should NOT return the query handle itself
    bool bFoundOverlap = Results.ContainsByPredicate([&](const FISMInstanceHandle& H) {
        return H.Component == CompB && H.InstanceIndex == 0;
    });
    bool bFoundFar = Results.ContainsByPredicate([&](const FISMInstanceHandle& H) {
        return H.Component == CompB && H.InstanceIndex == 1;
    });
    bool bFoundSelf = Results.ContainsByPredicate([&](const FISMInstanceHandle& H) {
        return H.Component == CompA && H.InstanceIndex == 0;
    });

    TestTrue("Should find CompB instance 0 (overlaps)", bFoundOverlap);
    TestFalse("Should not find CompB instance 1 (far away)", bFoundFar);
    TestFalse("Should not return the query handle itself", bFoundSelf);

    World->DestroyWorld(false);
    return true;
}