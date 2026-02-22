// ISMInstanceHandleTests.cpp
#include "ISMInstanceHandle.h"
#include "ISMRuntimeComponent.h"
#include "ISMTestHelpers.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMInstanceHandleBasicTest,
    "ISMRuntime.Core.InstanceHandle.BasicOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMInstanceHandleBasicTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FISMTestHelpers::CreateTestWorld();
    UISMRuntimeComponent* Component = NewObject<UISMRuntimeComponent>();

    // ACT
    FISMInstanceHandle Handle = FISMInstanceHandle();
	Handle.Component = Component;
	Handle.InstanceIndex = 5;

    // ASSERT
    TestTrue("Handle should be valid", Handle.IsValid());
    TestEqual("Instance index should be 5", Handle.InstanceIndex, 5);
    TestFalse("Should not be converted to actor", Handle.IsConvertedToActor());
    TestNull("Converted actor should be null", Handle.GetConvertedActor());

	FISMTestHelpers::DestroyTestWorld(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMInstanceHandleConversionTest,
    "ISMRuntime.Core.InstanceHandle.Conversion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMInstanceHandleConversionTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = FISMTestHelpers::CreateTestWorld();
    UISMRuntimeComponent* Component = NewObject<UISMRuntimeComponent>();
	
    FISMInstanceHandle Handle = FISMInstanceHandle();
    Handle.Component = Component;
    Handle.InstanceIndex = 5;

    AActor* TestActor = World->SpawnActor<AActor>();
    
    // ACT
    Handle.SetConvertedActor(TestActor, 0);
    
    // ASSERT
    TestTrue("Handle should be converted", Handle.IsConvertedToActor());
    TestEqual("Converted actor should match", Handle.GetConvertedActor(), TestActor);
    
    // ACT - Return to ISM
    Handle.ClearConvertedActor();
    
    // ASSERT
    TestFalse("Handle should no longer be converted", Handle.IsConvertedToActor());
    TestNull("Converted actor should be null", Handle.GetConvertedActor());
    
    // Cleanup
    TestActor->Destroy();
    
    return true;
}