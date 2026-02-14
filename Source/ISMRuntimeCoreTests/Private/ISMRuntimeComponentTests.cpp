// ISMRuntimeComponentTests.cpp
#include "ISMRuntimeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "ISMTestHelpers.h"
#include "Engine/World.h"
#include "Tests/AutomationEditorCommon.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentBasicTest,
    "ISMRuntime.Core.Component.BasicOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentBasicTest::RunTest(const FString& Parameters)
{
    // ARRANGE - Create test world and actor
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();
    
    // Create ISM component
    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->RegisterComponent();
    
    // Add some instances
    for (int32 i = 0; i < 10; i++)
    {
        FTransform Transform;
        Transform.SetLocation(FVector(i * 100.0f, 0, 0));
        ISM->AddInstance(Transform);
    }
    
    // Create runtime component
    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->RegisterComponent();
    
    // ACT - Initialize
    RuntimeComp->InitializeInstances();
    
    // ASSERT
    TestEqual("Should have 10 instances", RuntimeComp->GetInstanceCount(), 10);
    TestEqual("All should be active initially", RuntimeComp->GetActiveInstanceCount(), 10);
    TestTrue("Should be initialized", RuntimeComp->IsISMInitialized());
    
    // Cleanup
    World->DestroyWorld(false);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentDestroyTest,
    "ISMRuntime.Core.Component.DestroyInstance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentDestroyTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();
    
    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->RegisterComponent();
    
    FTransform Transform;
    ISM->AddInstance(Transform);
    ISM->AddInstance(Transform);
    ISM->AddInstance(Transform);
    
    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->RegisterComponent();
    RuntimeComp->InitializeInstances();
    
    // ACT - Destroy instance 1
    RuntimeComp->DestroyInstance(1);
    
    // ASSERT
    TestEqual("Should still have 3 instances total", RuntimeComp->GetInstanceCount(), 3);
    TestEqual("Should have 2 active instances", RuntimeComp->GetActiveInstanceCount(), 2);
    TestTrue("Instance 0 should be active", RuntimeComp->IsInstanceActive(0));
    TestFalse("Instance 1 should be destroyed", RuntimeComp->IsInstanceActive(1));
    TestTrue("Instance 1 should be marked destroyed", RuntimeComp->IsInstanceDestroyed(1));
    TestTrue("Instance 2 should be active", RuntimeComp->IsInstanceActive(2));
    
    // Cleanup
    World->DestroyWorld(false);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentQueryTest,
    "ISMRuntime.Core.Component.SpatialQuery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentQueryTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();
    
    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->RegisterComponent();
    
    // Add instances in a line
    for (int32 i = 0; i < 10; i++)
    {
        FTransform Transform;
        Transform.SetLocation(FVector(i * 1000.0f, 0, 0));
        ISM->AddInstance(Transform);
    }
    
    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->SpatialIndexCellSize = 1000.0f;
    RuntimeComp->RegisterComponent();
    bool InitWasSuccessful = RuntimeComp->InitializeInstances();
	TestTrue("Initialization should succeed", InitWasSuccessful);
    
    // ACT - Query near origin
    TArray<int32> Results = RuntimeComp->GetInstancesInRadius(FVector::ZeroVector, 1500.0f);
    
    // ASSERT
    TestTrue("Should find at least 2 instances", Results.Num() >= 2);
    TestTrue("Should contain instance 0", Results.Contains(0));
    TestTrue("Should contain instance 1", Results.Contains(1));
    
    // ACT - Query far away
    TArray<int32> FarResults = RuntimeComp->GetInstancesInRadius(FVector(9000, 0, 0), 500.0f);
    
    // ASSERT
    TestTrue("Should find instance 9", FarResults.Contains(9));
    TestFalse("Should not find instance 0", FarResults.Contains(0));
    
    // Cleanup
    World->DestroyWorld(false);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentTagTest,
    "ISMRuntime.Core.Component.GameplayTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentTagTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* TestActor = World->SpawnActor<AActor>();
    
    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->RegisterComponent();
    
    FTransform Transform;
    ISM->AddInstance(Transform);
    
    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->RegisterComponent();
    bool InitWasSuccessful = RuntimeComp->InitializeInstances();
    TestTrue("Initialization should succeed", InitWasSuccessful);

    // ACT - Add component tag
    FGameplayTag TreeTag = FGameplayTag::RequestGameplayTag("ISM.Type.Vegetation.Tree");
    RuntimeComp->ISMComponentTags.AddTag(TreeTag);
    
    // ACT - Add instance-specific tag
    FGameplayTag DamagedTag = FGameplayTag::RequestGameplayTag("ISM.State.Damaged");
    RuntimeComp->AddInstanceTag(0, DamagedTag);
    
    // ASSERT
    TestTrue("Component should have tree tag", RuntimeComp->HasTag(TreeTag));
    TestTrue("Instance should have tree tag (inherited)", RuntimeComp->InstanceHasTag(0, TreeTag));
    TestTrue("Instance should have damaged tag", RuntimeComp->InstanceHasTag(0, DamagedTag));
    
    FGameplayTagContainer InstanceTags = RuntimeComp->GetInstanceTags(0);
    TestTrue("Instance tags should contain both tags", 
        InstanceTags.HasTag(TreeTag) && InstanceTags.HasTag(DamagedTag));
    
    // Cleanup
    World->DestroyWorld(false);
    
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentBoundsPerformanceTest,
    "ISMRuntime.Core.Component.BoundsPerformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentBoundsPerformanceTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeComponent* RuntimeComp = FISMTestHelpers::CreateTestComponent(World, 1000);

    TestNotNull("Component should exist", RuntimeComp);

    // ACT - Destroy 100 instances WITHOUT bounds updates (should be fast)
    double StartTime = FPlatformTime::Seconds();

    for (int32 i = 0; i < 100; i++)
    {
        RuntimeComp->DestroyInstance(i, false); // Explicit: no bounds update
    }

    double DestroyTime = FPlatformTime::Seconds() - StartTime;

    // ASSERT - Should be very fast (O(1) per destruction)
    TestTrue(FString::Printf(TEXT("100 destructions took %.2fms (should be <5ms)"),
        DestroyTime * 1000.0f),
        DestroyTime < 0.005);

    // ACT - Single bounds recalculation
    StartTime = FPlatformTime::Seconds();
    RuntimeComp->RecalculateInstanceBounds();
    double BoundsTime = FPlatformTime::Seconds() - StartTime;

    // ASSERT - Single O(n) should still be fast
    TestTrue(FString::Printf(TEXT("Bounds recalc took %.2fms (should be <10ms)"),
        BoundsTime * 1000.0f),
        BoundsTime < 0.01);

    // Total time should be much less than 100 individual recalculations
    AddInfo(FString::Printf(TEXT("Total time: %.2fms (vs ~1000ms if auto-updating bounds)"),
        (DestroyTime + BoundsTime) * 1000.0f));

    // Cleanup
    World->DestroyWorld(false);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentBatchDestroyTest,
    "ISMRuntime.Core.Component.BatchDestroy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentBatchDestroyTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeComponent* RuntimeComp = FISMTestHelpers::CreateTestComponent(World, 1000);

    TArray<int32> IndicesToDestroy;
    for (int32 i = 0; i < 100; i++)
    {
        IndicesToDestroy.Add(i);
    }

    // ACT - Batch destroy
    double StartTime = FPlatformTime::Seconds();
    RuntimeComp->BatchDestroyInstances(IndicesToDestroy, true);
    double BatchTime = FPlatformTime::Seconds() - StartTime;

    // ASSERT
    TestEqual("Should have 900 active instances", RuntimeComp->GetActiveInstanceCount(), 900);
	TestTrue("Bounds should be valid", RuntimeComp->IsBoundsValid());

    TestTrue(FString::Printf(TEXT("Batch destroy took %.2fms (should be <10ms)"),
        BatchTime * 1000.0f),
        BatchTime < 0.01);

    // Cleanup
    World->DestroyWorld(false);

    return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentAddInstanceTest,
    "ISMRuntime.Core.Component.AddInstance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentAddInstanceTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeComponent* RuntimeComp = FISMTestHelpers::CreateTestComponent(World, 0); // Start empty

    // ACT - Add single instance
    FTransform Transform;
    Transform.SetLocation(FVector(100, 200, 0));

    int32 NewIndex = RuntimeComp->AddInstance(Transform, true);

    // ASSERT
    TestTrue("New index should be valid", NewIndex != INDEX_NONE);
    TestEqual("Should have 1 instance", RuntimeComp->GetInstanceCount(), 1);
    TestEqual("Should have 1 active instance", RuntimeComp->GetActiveInstanceCount(), 1);
    TestTrue("Bounds should be valid", RuntimeComp->IsBoundsValid());

    FVector InstanceLocation = RuntimeComp->GetInstanceLocation(NewIndex);
    TestEqual("Location should match", InstanceLocation, Transform.GetLocation());

    // Cleanup
    World->DestroyWorld(false);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentBatchAddTest,
    "ISMRuntime.Core.Component.BatchAddInstances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentBatchAddTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeComponent* RuntimeComp = FISMTestHelpers::CreateTestComponent(World, 0);

    TArray<FTransform> Transforms;
    for (int32 i = 0; i < 100; i++)
    {
        FTransform Transform;
        Transform.SetLocation(FVector(i * 100.0f, 0, 0));
        Transforms.Add(Transform);
    }

    // ACT
    double StartTime = FPlatformTime::Seconds();
    TArray<int32> NewIndices = RuntimeComp->BatchAddInstances(Transforms, true);
    double Duration = FPlatformTime::Seconds() - StartTime;

    // ASSERT
    TestEqual("Should return 100 indices", NewIndices.Num(), 100);
    TestEqual("Should have 100 instances", RuntimeComp->GetInstanceCount(), 100);
    TestEqual("All should be active", RuntimeComp->GetActiveInstanceCount(), 100);

    // Check no failures
    int32 FailureCount = NewIndices.FilterByPredicate([](int32 Index) {
        return Index == INDEX_NONE;
        }).Num();
    TestEqual("Should have no failures", FailureCount, 0);

    // Should be fast
    TestTrue(FString::Printf(TEXT("Batch add took %.2fms (should be <20ms)"),
        Duration * 1000.0f),
        Duration < 0.02);

    AddInfo(FString::Printf(TEXT("Added 100 instances in %.2fms"), Duration * 1000.0f));

    // Cleanup
    World->DestroyWorld(false);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeComponentAddPerformanceTest,
    "ISMRuntime.Core.Component.AddPerformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeComponentAddPerformanceTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeComponent* RuntimeComp = FISMTestHelpers::CreateTestComponent(World, 0);

    TArray<FTransform> Transforms;
    for (int32 i = 0; i < 1000; i++)
    {
        FTransform Transform;
        Transform.SetLocation(FVector(
            FMath::FRandRange(-5000.0f, 5000.0f),
            FMath::FRandRange(-5000.0f, 5000.0f),
            0
        ));
        Transforms.Add(Transform);
    }

    // ACT - Test batch add
    double BatchStart = FPlatformTime::Seconds();
    TArray<int32> BatchIndices = RuntimeComp->BatchAddInstances(Transforms, true);
    double BatchDuration = FPlatformTime::Seconds() - BatchStart;

    // ASSERT - Batch should be very fast
    TestTrue(FString::Printf(TEXT("Batch add 1000 instances: %.2fms (should be <50ms)"),
        BatchDuration * 1000.0f),
        BatchDuration < 0.05);

    // Compare to individual adds (slower)
    UISMRuntimeComponent* RuntimeComp2 = FISMTestHelpers::CreateTestComponent(World, 0);

    double IndividualStart = FPlatformTime::Seconds();
    for (const FTransform& Transform : Transforms)
    {
        RuntimeComp2->AddInstance(Transform, false); // Don't update bounds per-add
    }
    RuntimeComp2->RecalculateInstanceBounds(); // Update once at end
    double IndividualDuration = FPlatformTime::Seconds() - IndividualStart;

    // Batch should be faster
    AddInfo(FString::Printf(TEXT("Batch: %.2fms, Individual: %.2fms, Speedup: %.1fx"),
        BatchDuration * 1000.0f,
        IndividualDuration * 1000.0f,
        IndividualDuration / BatchDuration));

    // Cleanup
    World->DestroyWorld(false);

    return true;
}