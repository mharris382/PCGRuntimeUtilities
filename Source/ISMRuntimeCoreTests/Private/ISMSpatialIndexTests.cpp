// ISMSpatialIndexTests.cpp
#include "ISMSpatialIndex.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMSpatialIndexBasicTest,
    "ISMRuntime.Core.SpatialIndex.BasicOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMSpatialIndexBasicTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    FISMSpatialIndex SpatialIndex(1000.0f); // 10m cells
    
    // ACT - Add some instances
    SpatialIndex.AddInstance(0, FVector(0, 0, 0));
    SpatialIndex.AddInstance(1, FVector(500, 0, 0));
    SpatialIndex.AddInstance(2, FVector(5000, 0, 0));
    
    // ASSERT - Verify cell count
    TestEqual("Should have 2 cells", SpatialIndex.GetCellCount(), 2);
    TestEqual("Should have 3 total instances", SpatialIndex.GetTotalInstances(), 3);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMSpatialIndexQueryTest,
    "ISMRuntime.Core.SpatialIndex.RadiusQuery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMSpatialIndexQueryTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    FISMSpatialIndex SpatialIndex(1000.0f);
    
    // Create a cluster of instances
    SpatialIndex.AddInstance(0, FVector(0, 0, 0));
    SpatialIndex.AddInstance(1, FVector(100, 0, 0));
    SpatialIndex.AddInstance(2, FVector(200, 0, 0));
    SpatialIndex.AddInstance(3, FVector(5000, 0, 0)); // Far away
    
    // ACT - Query near origin
    TArray<int32> Results;
    SpatialIndex.QueryRadius(FVector(0, 0, 0), 500.0f, Results);
    
    // ASSERT
    TestEqual("Should find 3 nearby instances", Results.Num(), 3);
    TestTrue("Should contain instance 0", Results.Contains(0));
    TestTrue("Should contain instance 1", Results.Contains(1));
    TestTrue("Should contain instance 2", Results.Contains(2));
    TestFalse("Should NOT contain far instance 3", Results.Contains(3));
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMSpatialIndexRemoveTest,
    "ISMRuntime.Core.SpatialIndex.RemoveInstance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMSpatialIndexRemoveTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    FISMSpatialIndex SpatialIndex(1000.0f);
    FVector Location(0, 0, 0);
    
    SpatialIndex.AddInstance(0, Location);
    SpatialIndex.AddInstance(1, Location);
    
    // ACT - Remove one instance
    SpatialIndex.RemoveInstance(0, Location);
    
    // ASSERT
    TestEqual("Should have 1 instance after removal", SpatialIndex.GetTotalInstances(), 1);
    
    TArray<int32> Results;
    SpatialIndex.QueryRadius(Location, 100.0f, Results);
    TestEqual("Query should return 1 instance", Results.Num(), 1);
    TestEqual("Remaining instance should be 1", Results[0], 1);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMSpatialIndexUpdateTest,
    "ISMRuntime.Core.SpatialIndex.UpdateInstance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMSpatialIndexUpdateTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    FISMSpatialIndex SpatialIndex(1000.0f);
    FVector OldLocation(0, 0, 0);
    FVector NewLocation(5000, 0, 0);
    
    SpatialIndex.AddInstance(0, OldLocation);
    
    // ACT - Move instance to different cell
    SpatialIndex.UpdateInstance(0, OldLocation, NewLocation);
    
    // ASSERT - Old location shouldn't find it
    TArray<int32> OldResults;
    SpatialIndex.QueryRadius(OldLocation, 500.0f, OldResults);
    TestEqual("Old location should find nothing", OldResults.Num(), 0);
    
    // New location should find it
    TArray<int32> NewResults;
    SpatialIndex.QueryRadius(NewLocation, 500.0f, NewResults);
    TestEqual("New location should find instance", NewResults.Num(), 1);
    TestEqual("Found instance should be 0", NewResults[0], 0);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMSpatialIndexEdgeCaseTest,
    "ISMRuntime.Core.SpatialIndex.EdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMSpatialIndexEdgeCaseTest::RunTest(const FString& Parameters)
{
    // ARRANGE
    FISMSpatialIndex SpatialIndex(1000.0f);
    
    // ACT & ASSERT - Query empty index
    TArray<int32> EmptyResults;
    SpatialIndex.QueryRadius(FVector::ZeroVector, 1000.0f, EmptyResults);
    TestEqual("Empty index should return no results", EmptyResults.Num(), 0);
    
    // ACT & ASSERT - Add duplicate instance
    SpatialIndex.AddInstance(0, FVector::ZeroVector);
    SpatialIndex.AddInstance(0, FVector::ZeroVector); // Same index
    
    TArray<int32> DuplicateResults;
    SpatialIndex.QueryRadius(FVector::ZeroVector, 100.0f, DuplicateResults);
    
    // Note: We allow duplicates in the test - implementation should handle this gracefully
    TestTrue("Should handle duplicate adds", DuplicateResults.Num() >= 1);
    
    // ACT & ASSERT - Remove non-existent instance
    SpatialIndex.RemoveInstance(999, FVector::ZeroVector);
    // Shouldn't crash - that's the test
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMSpatialIndexCellBoundaryTest,
    "ISMRuntime.Core.SpatialIndex.CellBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMSpatialIndexCellBoundaryTest::RunTest(const FString& Parameters)
{
    // ARRANGE - Cell size 1000, so cells are: [-1000,0], [0,1000], [1000,2000], etc.
    FISMSpatialIndex SpatialIndex(1000.0f);
    
    // ACT - Add instances at cell boundaries
    SpatialIndex.AddInstance(0, FVector(0, 0, 0));      // Cell [0,0,0]
    SpatialIndex.AddInstance(1, FVector(999, 0, 0));    // Cell [0,0,0]
    SpatialIndex.AddInstance(2, FVector(1000, 0, 0));   // Cell [1,0,0]
    SpatialIndex.AddInstance(3, FVector(1001, 0, 0));   // Cell [1,0,0]
    
    // ASSERT - Query that spans boundary
    TArray<int32> Results;
    SpatialIndex.QueryRadius(FVector(500, 0, 0), 600.0f, Results);
    
    // Should find instances on both sides of boundary
    TestTrue("Should find at least 3 instances near boundary", Results.Num() >= 3);
    
    return true;
}
///------------------------------------------------------
// ISMSpatialIndexTests.cpp (complex tests)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMSpatialIndexPerformanceTest,
    "ISMRuntime.Core.SpatialIndex.Performance.LargeDataset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter
)

bool FISMSpatialIndexPerformanceTest::RunTest(const FString& Parameters)
{
    // ARRANGE - Large dataset
    FISMSpatialIndex SpatialIndex(1000.0f);
    const int32 NumInstances = 10000;
    
    // ACT - Add many instances
    double AddStartTime = FPlatformTime::Seconds();
    
    for (int32 i = 0; i < NumInstances; i++)
    {
        // Random distribution
        FVector Location(
            FMath::FRandRange(-10000.0f, 10000.0f),
            FMath::FRandRange(-10000.0f, 10000.0f),
            FMath::FRandRange(-1000.0f, 1000.0f)
        );
        SpatialIndex.AddInstance(i, Location);
    }
    
    double AddTime = FPlatformTime::Seconds() - AddStartTime;
    
    // ASSERT - Should be fast
    TestTrue(FString::Printf(TEXT("Adding %d instances took %.2fms (should be <100ms)"), 
        NumInstances, AddTime * 1000.0f),
        AddTime < 0.1); // 100ms threshold
    
    // ACT - Query performance
    double QueryStartTime = FPlatformTime::Seconds();
    
    TArray<int32> Results;
    SpatialIndex.QueryRadius(FVector::ZeroVector, 1000.0f, Results);
    
    double QueryTime = FPlatformTime::Seconds() - QueryStartTime;
    
    // ASSERT - Query should be very fast
    TestTrue(FString::Printf(TEXT("Query took %.2fms (should be <5ms)"), 
        QueryTime * 1000.0f),
        QueryTime < 0.005); // 5ms threshold
    
    AddInfo(FString::Printf(TEXT("Performance: %d instances, %.2fms add, %.2fms query, %d results"),
        NumInstances, AddTime * 1000.0f, QueryTime * 1000.0f, Results.Num()));
    
    return true;
}
