// ISMInstanceHandleTests.cpp
#include "ISMInstanceHandle.h"
#include "ISMRuntimeComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMInstanceHandleBasicTest,
    "ISMRuntime.Core.InstanceHandle.BasicOperations",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FISMInstanceHandleBasicTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = GetTestWorld();
    UISMRuntimeComponent* Component = NewObject<UISMRuntimeComponent>();
    
    // ACT
    FISMInstanceHandle Handle(Component, 5);
    
    // ASSERT
    TestTrue("Handle should be valid", Handle.IsValid());
    TestEqual("Instance index should be 5", Handle.InstanceIndex, 5);
    TestFalse("Should not be converted to actor", Handle.IsConvertedToActor());
    TestNull("Converted actor should be null", Handle.GetConvertedActor());
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMInstanceHandleConversionTest,
    "ISMRuntime.Core.InstanceHandle.Conversion",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FISMInstanceHandleConversionTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    UWorld* World = GetTestWorld();
    UISMRuntimeComponent* Component = NewObject<UISMRuntimeComponent>();
    FISMInstanceHandle Handle(Component, 0);
    
    AActor* TestActor = World->SpawnActor<AActor>();
    
    // ACT
    Handle.SetConvertedActor(TestActor);
    
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