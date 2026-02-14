// ISMTestHelpers.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Tests/AutomationCommon.h"

/**
 * Helper utilities for ISM Runtime tests
 */
class FISMTestHelpers
{
public:
    /** Create a test world with required subsystems */
    static UWorld* CreateTestWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
        WorldContext.SetCurrentWorld(World);
        
        World->InitializeActorsForPlay(FURL());
        World->BeginPlay();
        
        return World;
    }
    
    /** Destroy test world */
    static void DestroyTestWorld(UWorld* World)
    {
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }
    
    /** Create populated ISM component for testing */
    static UInstancedStaticMeshComponent* CreateTestISMComponent(
        UWorld* World,
        int32 NumInstances,
        float Spacing = 1000.0f)
    {
        AActor* TestActor = World->SpawnActor<AActor>();
        UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(TestActor);
        ISM->RegisterComponent();
        
        // Add instances in a grid
        for (int32 i = 0; i < NumInstances; i++)
        {
            FTransform Transform;
            Transform.SetLocation(FVector(
                (i % 10) * Spacing,
                (i / 10) * Spacing,
                0.0f
            ));
            ISM->AddInstance(Transform);
        }
        
        return ISM;
    }
    
    /** Generate random instances in a volume */
    static TArray<FVector> GenerateRandomLocations(
        int32 Count,
        const FBox& Bounds)
    {
        TArray<FVector> Locations;
        Locations.Reserve(Count);
        
        for (int32 i = 0; i < Count; i++)
        {
            FVector Location(
                FMath::FRandRange(Bounds.Min.X, Bounds.Max.X),
                FMath::FRandRange(Bounds.Min.Y, Bounds.Max.Y),
                FMath::FRandRange(Bounds.Min.Z, Bounds.Max.Z)
            );
            Locations.Add(Location);
        }
        
        return Locations;
    }
};

/** Scoped test world that auto-cleans up */
class FScopedTestWorld
{
public:
    FScopedTestWorld()
    {
        World = FISMTestHelpers::CreateTestWorld();
    }
    
    ~FScopedTestWorld()
    {
        FISMTestHelpers::DestroyTestWorld(World);
    }
    
    UWorld* GetWorld() const { return World; }
    
private:
    UWorld* World;
};