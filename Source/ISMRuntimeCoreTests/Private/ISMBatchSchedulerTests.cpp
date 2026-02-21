// ISMBatchSchedulerTests.cpp
// Phase 1 - Core async loop validation
//
// Tests covered:
//   1. ProcessChunk is called when a dirty transformer is ticked
//   2. A released mutation result is applied to the component
//   3. An abandoned handle writes nothing to the component
//   4. A mutation targeting a destroyed instance index is silently skipped
//
// Phase 2+ concerns (spatial chunking, slot-reuse locking, timeouts, priority)
// are explicitly out of scope here. Each test uses a single component with all
// instances treated as one chunk.
//
// Test infrastructure:
//   - FISMTestTransformer  : synchronous mock transformer, configurable per-test
//   - FISMBatchTestFixture : sets up a minimal world, component, and scheduler

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"


#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

#include "ISMTestHelpers.h"

#include "Batching/ISMBatchScheduler.h"
#include "Batching/ISMBatchTransformer.h"
#include "Batching/ISMBatchTypes.h"

#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"

// ============================================================
//  Test flags
// ============================================================




// ============================================================
//  Mock Transformer
// ============================================================

/**
 * Synchronous test transformer.
 * ProcessChunk resolves immediately on the calling thread - no actual async work.
 * Configure ResultBuilder before ticking to control what the transformer returns.
 * Inspect ReceivedChunks after ticking to verify the scheduler called it correctly.
 */
struct FISMTestTransformer : public IISMBatchTransformer
{
    // ----- Configuration (set before Tick) -----

    /** Which component to target. Set before ticking. */
    TWeakObjectPtr<UISMRuntimeComponent> TargetComponent;

    /** Fields to read and write. Defaults to CustomData only for simplicity. */
    EISMSnapshotField ReadMask  = EISMSnapshotField::CustomData;
    EISMSnapshotField WriteMask = EISMSnapshotField::CustomData;

    /**
     * Called once per chunk received. Return the result to submit, or an empty
     * FISMBatchMutationResult with no mutations to simulate abandonment via Release.
     * Leave null to Abandon the handle instead of Releasing.
     */
    TFunction<FISMBatchMutationResult(const FISMBatchSnapshot&)> ResultBuilder;

    // ----- Inspection (read after Tick) -----

    TArray<FISMBatchSnapshot> ReceivedChunks;
    int32 AbandonCount  = 0;
    int32 ReleaseCount  = 0;

    // ----- IISMBatchTransformer -----

    virtual FName GetTransformerName() const override
    {
        return FName("ISMBatchTest.MockTransformer");
    }

    virtual bool IsDirty() const override { return bDirty; }

    virtual void ClearDirty() override { bDirty = false; }

    virtual FISMSnapshotRequest BuildRequest() override
    {
        FISMSnapshotRequest Request;
        if (TargetComponent.IsValid())
        {
            Request.TargetComponents.Add(TargetComponent);
        }
        Request.ReadMask  = ReadMask;
        Request.WriteMask = WriteMask;
        // No spatial bounds = snapshot all cells (Phase 1: treated as one chunk)
        return Request;
    }

    virtual void ProcessChunk(FISMBatchSnapshot Chunk, FISMMutationHandle Handle) override
    {
        ReceivedChunks.Add(Chunk);

        if (ResultBuilder)
        {
            FISMBatchMutationResult Result = ResultBuilder(Chunk);
            Handle.Release(MoveTemp(Result));
            ReleaseCount++;
        }
        else
        {
            Handle.Abandon();
            AbandonCount++;
        }
    }

    // ----- Test helpers -----

    void SetDirty() { bDirty = true; }
    void Reset()
    {
        ReceivedChunks.Reset();
        AbandonCount = 0;
        ReleaseCount = 0;
        bDirty       = true;
        ResultBuilder = nullptr;
    }

private:
    bool bDirty = true;
};


// ============================================================
//  Test Fixture
// ============================================================

/**
 * Sets up the minimal objects needed for each test:
 *   - A transient UWorld (editor world, no game mode overhead)
 *   - A UISMRuntimeComponent with a UInstancedStaticMeshComponent
 *   - A UISMBatchScheduler initialized against a mock subsystem reference
 *
 * All objects are created with the transient package as outer so they get
 * cleaned up by GC without needing manual destruction.
 *
 * Usage:
 *   FISMBatchTestFixture F;
 *   F.AddInstances(3);
 *   // run test against F.Scheduler, F.RuntimeComponent
 */
struct FISMBatchTestFixture
{
    UWorld*                  World            = nullptr;
    UISMRuntimeSubsystem*    Subsystem        = nullptr;
    AActor*                  OwnerActor       = nullptr;
    UInstancedStaticMeshComponent* ISMComp   = nullptr;
    UISMRuntimeComponent*    RuntimeComponent = nullptr;
    UISMBatchScheduler*      Scheduler        = nullptr;

    FISMBatchTestFixture()
    {
        // Create a minimal transient world
        World = UWorld::CreateWorld(EWorldType::Game, false);
        check(World);

        // Get or create the subsystem - UWorldSubsystem instances are owned by the world
        Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>();
        check(Subsystem);

        Scheduler = Subsystem->GetBatchScheduler();
        check(Scheduler);

        // Spawn a minimal actor to own our components
        FActorSpawnParameters SpawnParams;
        SpawnParams.ObjectFlags = RF_Transient;
        OwnerActor = World->SpawnActor<AActor>(SpawnParams);
        check(OwnerActor);

        // Create the raw ISM component
        ISMComp = NewObject<UInstancedStaticMeshComponent>(OwnerActor, NAME_None, RF_Transient);
        OwnerActor->AddInstanceComponent(ISMComp);
        ISMComp->RegisterComponent();

        // Create the runtime component, point it at the ISM
        RuntimeComponent = NewObject<UISMRuntimeComponent>(OwnerActor, NAME_None, RF_Transient);
        RuntimeComponent->ManagedISMComponent = ISMComp;
        OwnerActor->AddInstanceComponent(RuntimeComponent);
        RuntimeComponent->RegisterComponent();
        RuntimeComponent->InitializeInstances();
    }

    ~FISMBatchTestFixture()
    {
        if (World)
        {
            World->DestroyWorld(false);
            World = nullptr;
        }
    }

    /**
     * Add N instances at arbitrary transforms and optionally set custom data.
     * CustomDataValue is written to slot 0 of each instance.
     * Returns the indices of the added instances.
     */
    TArray<int32> AddInstances(int32 Count, float CustomDataValue = 0.0f)
    {
        TArray<FTransform> Transforms;
        for (int32 i = 0; i < Count; ++i)
        {
            Transforms.Add(FTransform(FVector(i * 100.0f, 0.0f, 0.0f)));
        }
        TArray<int32> Indices = RuntimeComponent->BatchAddInstances(Transforms, false, true);

        if (CustomDataValue != 0.0f)
        {
            for (int32 Idx : Indices)
            {
                RuntimeComponent->SetInstanceCustomDataValue(Idx, 0, CustomDataValue);
            }
        }

        return Indices;
    }

    /** Tick the subsystem one frame (drives the scheduler). */
    void Tick(float DeltaTime = 0.016f)
    {
        Subsystem->Tick(DeltaTime);
    }
};


// ============================================================
//  Test 1: ProcessChunk is called when a dirty transformer ticks
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMBatch_Test_ProcessChunkCalled,
    "ISMRuntime.Batch.Phase1.ProcessChunkCalledOnDirtyTick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter)

bool FISMBatch_Test_ProcessChunkCalled::RunTest(const FString& Parameters)
{
    // ----- Arrange -----
    FISMBatchTestFixture F;
    F.AddInstances(3);

    FISMTestTransformer Transformer;
    Transformer.TargetComponent = F.RuntimeComponent;
    // No ResultBuilder = Abandon, but we only care that ProcessChunk was called

    F.Scheduler->RegisterTransformer(&Transformer);

    // ----- Act -----
    F.Tick();

    // ----- Assert -----

    // ProcessChunk must have been called exactly once (Phase 1: one chunk per component)
    TestEqual(TEXT("ProcessChunk called once"), Transformer.ReceivedChunks.Num(), 1);

    // The chunk must reference the correct component
    TestTrue(TEXT("Chunk references correct component"),
        Transformer.ReceivedChunks[0].SourceComponent == F.RuntimeComponent);

    // The chunk must contain all 3 instances
    TestEqual(TEXT("Chunk contains all instances"),
        Transformer.ReceivedChunks[0].Instances.Num(), 3);

    // After dispatch, dirty should be cleared
    TestFalse(TEXT("Dirty cleared after dispatch"), Transformer.IsDirty());

    // A second tick with dirty=false should NOT call ProcessChunk again
    F.Tick();
    TestEqual(TEXT("ProcessChunk not called when clean"), Transformer.ReceivedChunks.Num(), 1);

    F.Scheduler->UnregisterTransformer(Transformer.GetTransformerName());
    return true;
}


// ============================================================
//  Test 2: Released mutation result is applied to the component
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMBatch_Test_MutationApplied,
    "ISMRuntime.Batch.Phase1.ReleasedMutationIsApplied",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter)

bool FISMBatch_Test_MutationApplied::RunTest(const FString& Parameters)
{
    // ----- Arrange -----
    FISMBatchTestFixture F;
    TArray<int32> Indices = F.AddInstances(3, /*CustomDataValue=*/1.0f);

    const float ExpectedValue = 99.0f;

    FISMTestTransformer Transformer;
    Transformer.TargetComponent = F.RuntimeComponent;
    Transformer.ReadMask        = EISMSnapshotField::CustomData;
    Transformer.WriteMask       = EISMSnapshotField::CustomData;

    // ResultBuilder: set custom data slot 0 to ExpectedValue on all instances
    Transformer.ResultBuilder = [&](const FISMBatchSnapshot& Chunk) -> FISMBatchMutationResult
    {
        FISMBatchMutationResult Result;
        Result.TargetComponent = Chunk.SourceComponent;
        Result.WrittenFields   = EISMSnapshotField::CustomData;

        for (const FISMInstanceSnapshot& InstSnap : Chunk.Instances)
        {
            FISMInstanceMutation Mutation;
            Mutation.InstanceIndex = InstSnap.InstanceIndex;

            TArray<float> NewData = InstSnap.CustomData;
            if (NewData.Num() > 0)
                NewData[0] = ExpectedValue;
            else
                NewData.Add(ExpectedValue);

            Mutation.NewCustomData = NewData;
            Result.Mutations.Add(Mutation);
        }
        return Result;
    };

    F.Scheduler->RegisterTransformer(&Transformer);

    // ----- Act -----
    F.Tick();

    // ----- Assert -----

    // Every instance should now have ExpectedValue in slot 0
    for (int32 Idx : Indices)
    {
        const float Actual = F.RuntimeComponent->GetInstanceCustomDataValue(Idx, 0);
        TestEqual(
            FString::Printf(TEXT("Instance %d custom data slot 0 equals expected value"), Idx),
            Actual,
            ExpectedValue);
    }

    // Handle was released (not abandoned)
    TestEqual(TEXT("Handle released once"), Transformer.ReleaseCount, 1);
    TestEqual(TEXT("Handle not abandoned"), Transformer.AbandonCount, 0);

    F.Scheduler->UnregisterTransformer(Transformer.GetTransformerName());
    return true;
}


// ============================================================
//  Test 3: Abandoned handle writes nothing to the component
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMBatch_Test_AbandonWritesNothing,
    "ISMRuntime.Batch.Phase1.AbandonedHandleWritesNothing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter)

bool FISMBatch_Test_AbandonWritesNothing::RunTest(const FString& Parameters)
{
    // ----- Arrange -----
    FISMBatchTestFixture F;
    const float OriginalValue = 5.0f;
    TArray<int32> Indices = F.AddInstances(3, OriginalValue);

    FISMTestTransformer Transformer;
    Transformer.TargetComponent = F.RuntimeComponent;
    // ResultBuilder left null = ProcessChunk calls Handle.Abandon()

    F.Scheduler->RegisterTransformer(&Transformer);

    // ----- Act -----
    F.Tick();

    // ----- Assert -----

    // ProcessChunk was called (abandon doesn't mean it wasn't dispatched)
    TestEqual(TEXT("ProcessChunk called"), Transformer.ReceivedChunks.Num(), 1);
    TestEqual(TEXT("Handle was abandoned"), Transformer.AbandonCount, 1);
    TestEqual(TEXT("Handle not released"), Transformer.ReleaseCount, 0);

    // Data must be unchanged
    for (int32 Idx : Indices)
    {
        const float Actual = F.RuntimeComponent->GetInstanceCustomDataValue(Idx, 0);
        TestEqual(
            FString::Printf(TEXT("Instance %d data unchanged after abandon"), Idx),
            Actual,
            OriginalValue);
    }

    F.Scheduler->UnregisterTransformer(Transformer.GetTransformerName());
    return true;
}


// ============================================================
//  Test 4: Mutation targeting a destroyed instance is silently skipped
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FISMBatch_Test_DestroyedInstanceSkipped,
    "ISMRuntime.Batch.Phase1.DestroyedInstanceMutationSkipped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProgramContext | EAutomationTestFlags::ProductFilter)

bool FISMBatch_Test_DestroyedInstanceSkipped::RunTest(const FString& Parameters)
{
    // ----- Arrange -----
    FISMBatchTestFixture F;
    const float OriginalValue = 3.0f;
    const float MutatedValue  = 77.0f;
    TArray<int32> Indices = F.AddInstances(3, OriginalValue);

    // Instance at index 1 will be destroyed between snapshot and apply.
    // We simulate this by having the transformer destroy it inside ProcessChunk,
    // before releasing the handle, which is the worst-case race condition.
    const int32 DestroyedIndex = Indices[1];

    FISMTestTransformer Transformer;
    Transformer.TargetComponent = F.RuntimeComponent;
    Transformer.WriteMask       = EISMSnapshotField::CustomData;

    Transformer.ResultBuilder = [&](const FISMBatchSnapshot& Chunk) -> FISMBatchMutationResult
    {
        // Simulate instance being destroyed mid-flight
        // (In production this would happen from gameplay code on the game thread,
        //  but for Phase 1 testing synchronously this is equivalent)
        F.RuntimeComponent->DestroyInstance(DestroyedIndex);

        FISMBatchMutationResult Result;
        Result.TargetComponent = Chunk.SourceComponent;
        Result.WrittenFields   = EISMSnapshotField::CustomData;

        // Attempt to mutate ALL instances including the destroyed one
        for (const FISMInstanceSnapshot& InstSnap : Chunk.Instances)
        {
            FISMInstanceMutation Mutation;
            Mutation.InstanceIndex = InstSnap.InstanceIndex;

            TArray<float> NewData = { MutatedValue };
            Mutation.NewCustomData = NewData;
            Result.Mutations.Add(Mutation);
        }
        return Result;
    };

    F.Scheduler->RegisterTransformer(&Transformer);

    // ----- Act -----
    F.Tick();

    // ----- Assert -----

    // Instances 0 and 2 should have the mutated value
    TestEqual(TEXT("Instance 0 mutated"),
        F.RuntimeComponent->GetInstanceCustomDataValue(Indices[0], 0), MutatedValue);

    TestEqual(TEXT("Instance 2 mutated"),
        F.RuntimeComponent->GetInstanceCustomDataValue(Indices[2], 0), MutatedValue);

    // Instance 1 (destroyed) must NOT have been mutated - value stays at original
    // We verify via the state flag (it's destroyed) and that custom data wasn't touched.
    TestTrue(TEXT("Instance 1 is destroyed"),
        F.RuntimeComponent->IsInstanceDestroyed(DestroyedIndex));

    TestEqual(TEXT("Destroyed instance data not mutated"),
        F.RuntimeComponent->GetInstanceCustomDataValue(DestroyedIndex, 0), OriginalValue);

    F.Scheduler->UnregisterTransformer(Transformer.GetTransformerName());
    return true;
}