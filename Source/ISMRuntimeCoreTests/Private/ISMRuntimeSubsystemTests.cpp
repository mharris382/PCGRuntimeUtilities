// ISMRuntimeSubsystemTests.cpp
#include "ISMRuntimeSubsystem.h"
#include "ISMRuntimeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Tests/AutomationEditorCommon.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeSubsystemBasicTest,
    "ISMRuntime.Core.Subsystem.Registration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeSubsystemBasicTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    
    TestNotNull("Subsystem should exist", Subsystem);
    
    AActor* TestActor = World->SpawnActor<AActor>();
    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
    ISM->RegisterComponent();
    
    FTransform Transform;
    ISM->AddInstance(Transform);
    
    UISMRuntimeComponent* RuntimeComp = NewObject<UISMRuntimeComponent>(TestActor);
    RuntimeComp->ManagedISMComponent = ISM;
    RuntimeComp->RegisterComponent();
    
    
    // ACT
    RuntimeComp->InitializeInstances();
    
    // ASSERT
    TArray<UISMRuntimeComponent*> AllComponents = Subsystem->GetAllComponents();
    TestEqual("Should have 1 registered component", AllComponents.Num(), 1);
    TestEqual("Component should match", AllComponents[0], RuntimeComp);
    
    FISMRuntimeStats Stats = Subsystem->GetRuntimeStats();
    TestEqual("Stats should show 1 component", Stats.RegisteredComponentCount, 1);
    TestEqual("Stats should show 1 instance", Stats.TotalInstanceCount, 1);
    
    // Cleanup
    World->DestroyWorld(false);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMRuntimeSubsystemQueryTest,
    "ISMRuntime.Core.Subsystem.GlobalQuery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMRuntimeSubsystemQueryTest::RunTest(const FString& Parameters)
{
    // ARRANGE - Create multiple components
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
    
    // Component 1 - Trees at origin
    AActor* Actor1 = World->SpawnActor<AActor>();
    UInstancedStaticMeshComponent* ISM1 = NewObject<UInstancedStaticMeshComponent>(Actor1);
    ISM1->RegisterComponent();
    
    for (int32 i = 0; i < 5; i++)
    {
        FTransform Transform;
        Transform.SetLocation(FVector(i * 100.0f, 0, 0));
        ISM1->AddInstance(Transform);
    }
    
    UISMRuntimeComponent* TreeComp = NewObject<UISMRuntimeComponent>(Actor1);
    TreeComp->ManagedISMComponent = ISM1;
    TreeComp->ISMComponentTags.AddTag(FGameplayTag::RequestGameplayTag("ISM.Type.Vegetation.Tree"));
    TreeComp->RegisterComponent();
    TreeComp->InitializeInstances();
    
    // Component 2 - Rocks far away
    AActor* Actor2 = World->SpawnActor<AActor>();
    UInstancedStaticMeshComponent* ISM2 = NewObject<UInstancedStaticMeshComponent>(Actor2);
    ISM2->RegisterComponent();
    
    for (int32 i = 0; i < 3; i++)
    {
        FTransform Transform;
        Transform.SetLocation(FVector(10000.0f + i * 100.0f, 0, 0));
        ISM2->AddInstance(Transform);
    }
    
    UISMRuntimeComponent* RockComp = NewObject<UISMRuntimeComponent>(Actor2);
    RockComp->ManagedISMComponent = ISM2;
    RockComp->ISMComponentTags.AddTag(FGameplayTag::RequestGameplayTag("ISM.Type.Rock"));
    RockComp->RegisterComponent();
    RockComp->InitializeInstances();
    
    // ACT - Query near origin
    FISMQueryFilter Filter;
    TArray<FISMInstanceReference> Results = Subsystem->QueryInstancesInRadius(
        FVector::ZeroVector, 
        500.0f, 
        Filter
    );
    
    // ASSERT
    TestTrue("Should find tree instances", Results.Num() > 0);
    TestTrue("All results should be trees", 
        Results.FilterByPredicate([TreeComp](const FISMInstanceReference& Ref)
        {
            return Ref.Component == TreeComp;
        }).Num() == Results.Num()
    );
    
    // ACT - Query with tag filter
    FISMQueryFilter RockFilter;
    RockFilter.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag("ISM.Type.Rock"));
    
    TArray<FISMInstanceReference> RockResults = Subsystem->QueryInstancesInRadius(
        FVector(10000, 0, 0),
        500.0f,
        RockFilter
    );
    
    // ASSERT
    TestEqual("Should find 3 rocks", RockResults.Num(), 3);
    
    // Cleanup
    World->DestroyWorld(false);
    
    return true;
}