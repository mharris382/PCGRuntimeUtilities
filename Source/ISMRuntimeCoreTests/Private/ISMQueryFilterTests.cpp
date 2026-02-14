// ISMQueryFilterTests.cpp
#include "ISMQueryFilter.h"
#include "ISMRuntimeComponent.h"
#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMQueryFilterTagTest,
    "ISMRuntime.Core.QueryFilter.GameplayTags",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FISMQueryFilterTagTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    FISMQueryFilter Filter;
    Filter.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag("ISM.Type.Tree"));
    Filter.ExcludedTags.AddTag(FGameplayTag::RequestGameplayTag("ISM.State.Destroyed"));
    
    UISMRuntimeComponent* Component = NewObject<UISMRuntimeComponent>();
    Component->ISMComponentTags.AddTag(FGameplayTag::RequestGameplayTag("ISM.Type.Tree"));
    
    // ACT & ASSERT - Component passes filter
    TestTrue("Component should pass filter", Filter.PassesComponentFilter(Component));
    
    // ACT & ASSERT - Component with excluded tag fails
    Component->ISMComponentTags.AddTag(FGameplayTag::RequestGameplayTag("ISM.State.Destroyed"));
    TestFalse("Component with excluded tag should fail", Filter.PassesComponentFilter(Component));
    
    return true;
}