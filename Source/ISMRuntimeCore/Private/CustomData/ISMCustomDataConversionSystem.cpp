// ISMCustomDataConversionSystem.cpp
#include "CustomData/ISMCustomDataConversionSystem.h"

#include "ISMInstanceHandle.h"
#include "ISMRuntimeComponent.h"
#include "ISMInstanceDataAsset.h"
#include "CustomData/ISMCustomDataSchema.h"
#include "CustomData/ISMCustomDataSubsystem.h"
#include "CustomData/ISMCustomDataMaterialProvider.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

// ============================================================
//  Public API
// ============================================================

FISMCustomDataConversionResult UISMCustomDataConversionSystem::ResolveAndApply(
    const FISMInstanceHandle& InstanceHandle,
    AActor* ConvertedActor,
    UWorld* World)
{
    FISMCustomDataConversionResult Result = ResolveDMIs(InstanceHandle, World);

    if (Result.bSuccess && ConvertedActor)
    {
        ApplyToActor(Result, ConvertedActor);
    }

    return Result;
}

FISMCustomDataConversionResult UISMCustomDataConversionSystem::ResolveDMIs(
    const FISMInstanceHandle& InstanceHandle,
    UWorld* World)
{
    FISMCustomDataConversionResult Result;

    // Fast path: check if we should even attempt conversion
    FString SkipReason;
    if (!ShouldAttemptConversion(InstanceHandle, &SkipReason))
    {
        Result.SkipReason = SkipReason;
        return Result;
    }

    UISMRuntimeComponent* Comp = InstanceHandle.Component.Get();
    if (!Comp || !Comp->ManagedISMComponent)
    {
        Result.SkipReason = TEXT("No managed ISM component found on runtime component");
        return Result;
    }

    // Get subsystem
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    UISMCustomDataSubsystem* Sub = GI ? GI->GetSubsystem<UISMCustomDataSubsystem>() : nullptr;
    if (!Sub)
    {
        Result.SkipReason = TEXT("UISMCustomDataSubsystem not available");
        return Result;
    }

    // Resolve schema
    FName SchemaName;
    const FISMCustomDataSchema* Schema = ResolveSchema(InstanceHandle, SchemaName);
    if (!Schema)
    {
        Result.SkipReason = TEXT("No schema resolved — check InstanceDataAsset SchemaName or project default");
        return Result;
    }

    Result.ResolvedSchema = Schema;
    Result.ResolvedSchemaName = SchemaName;

    // Get applicable slots
    const TArray<int32> ApplicableSlots = GetApplicableSlots(*Schema, Comp);

    bool bAnyCacheHit = true; // will be false if any slot misses

    for (int32 SlotIdx : ApplicableSlots)
    {
        UMaterialInterface* Template = Comp->ManagedISMComponent->GetMaterial(SlotIdx);
        if (!Template)
        {
            continue;
        }

        bool bCacheHit = false;
        UMaterialInstanceDynamic* DMI = ResolvePooledDMI(
            Template,
            InstanceHandle.CachedCustomData,
            *Schema,
            SlotIdx,
            Sub,
            bCacheHit);

        if (DMI)
        {
            Result.DMIsBySlot.Add(SlotIdx, DMI);
            if (!bCacheHit) { bAnyCacheHit = false; }
        }
    }

    Result.bCacheHit = bAnyCacheHit;
    Result.bSuccess = Result.DMIsBySlot.Num() > 0;

    if (!Result.bSuccess)
    {
        Result.SkipReason = TEXT("No DMIs could be resolved — check material slot count and schema ApplicableSlots");
    }

    return Result;
}

void UISMCustomDataConversionSystem::ApplyToActor(
    const FISMCustomDataConversionResult& Result,
    AActor* ConvertedActor)
{
    if (!ConvertedActor || !Result.bSuccess)
    {
        return;
    }

    for (const TTuple<int32, UMaterialInstanceDynamic*>& Entry : Result.DMIsBySlot)
    {
        ApplyDMIToActorSlot(ConvertedActor, Entry.Key, Entry.Value);
    }
}

void UISMCustomDataConversionSystem::RefreshActorMaterials(
    const FISMInstanceHandle& InstanceHandle,
    AActor* ConvertedActor,
    UWorld* World)
{
    if (!ConvertedActor || !World || !InstanceHandle.IsValid())
    {
        return;
    }

    // Re-run the full resolution — pool will return existing DMIs for unchanged
    // signatures (cache hit), or create new ones if mapped values changed
    const FISMCustomDataConversionResult Result = ResolveDMIs(InstanceHandle, World);

    if (Result.bSuccess)
    {
        ApplyToActor(Result, ConvertedActor);
    }
}

bool UISMCustomDataConversionSystem::ShouldAttemptConversion(
    const FISMInstanceHandle& InstanceHandle,
    FString* OutSkipReason)
{
    if (!InstanceHandle.IsValid())
    {
        if (OutSkipReason) { *OutSkipReason = TEXT("Instance handle is not valid"); }
        return false;
    }

    UISMRuntimeComponent* Comp = InstanceHandle.Component.Get();
    if (!Comp)
    {
        if (OutSkipReason) { *OutSkipReason = TEXT("Component is null"); }
        return false;
    }

    // Check InstanceDataAsset opt-out flag
    if (Comp->InstanceData && !Comp->InstanceData->bUsePICDConversion)
    {
        if (OutSkipReason) { *OutSkipReason = TEXT("bUsePICDConversion is false on InstanceDataAsset"); }
        return false;
    }

    // Must have custom data to convert
    if (InstanceHandle.CachedCustomData.Num() == 0)
    {
        if (OutSkipReason) { *OutSkipReason = TEXT("CachedCustomData is empty — no PICD values to map"); }
        return false;
    }

    return true;
}

// ============================================================
//  Private Helpers
// ============================================================

const FISMCustomDataSchema* UISMCustomDataConversionSystem::ResolveSchema(
    const FISMInstanceHandle& InstanceHandle,
    FName& OutSchemaName)
{
    OutSchemaName = NAME_None;

    UISMRuntimeComponent* Comp = InstanceHandle.Component.Get();
    if (!Comp)
    {
        return nullptr;
    }

    // Try InstanceDataAsset first
    if (Comp->InstanceData)
    {
        const FName ExplicitName = Comp->InstanceData->SchemaName;
        const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();

        if (ExplicitName != NAME_None && Settings->HasSchema(ExplicitName))
        {
            OutSchemaName = ExplicitName;
            return Settings->ResolveSchema(ExplicitName);
        }
    }

    // Fall back to project default
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    if (const FISMCustomDataSchema* Default = Settings->GetDefaultSchema())
    {
        OutSchemaName = Settings->DefaultSchemaName;
        return Default;
    }

    return nullptr;
}

TArray<int32> UISMCustomDataConversionSystem::GetApplicableSlots(
    const FISMCustomDataSchema& Schema,
    UISMRuntimeComponent* Comp)
{
    TArray<int32> Result;

    if (!Comp || !Comp->ManagedISMComponent)
    {
        return Result;
    }

    const int32 NumSlots = Comp->ManagedISMComponent->GetNumMaterials();

    // Empty ApplicableSlots means all slots
    if (Schema.ApplicableSlots.Num() == 0)
    {
        for (int32 i = 0; i < NumSlots; ++i)
        {
            Result.Add(i);
        }
    }
    else
    {
        for (int32 SlotIdx : Schema.ApplicableSlots)
        {
            if (SlotIdx < NumSlots)
            {
                Result.Add(SlotIdx);
            }
        }
    }

    return Result;
}

UMaterialInstanceDynamic* UISMCustomDataConversionSystem::ResolvePooledDMI(
    UMaterialInterface* Template,
    const TArray<float>& CustomData,
    const FISMCustomDataSchema& Schema,
    int32 SlotIndex,
    UISMCustomDataSubsystem* Subsystem,
    bool& bOutCacheHit)
{
    if (!Template || !Subsystem)
    {
        bOutCacheHit = false;
        return nullptr;
    }

    // Track pool size before to detect cache hit vs miss
    const FISMDMIPoolStats StatsBefore = Subsystem->GetSharedPoolStats();

    UMaterialInstanceDynamic* DMI = Subsystem->GetOrCreateDMI(
        Template, CustomData, Schema, SlotIndex);

    const FISMDMIPoolStats StatsAfter = Subsystem->GetSharedPoolStats();
    bOutCacheHit = (StatsAfter.CacheHits > StatsBefore.CacheHits);

    return DMI;
}

bool UISMCustomDataConversionSystem::ApplyDMIToActorSlot(
    AActor* Actor,
    int32 SlotIndex,
    UMaterialInstanceDynamic* DMI)
{
    if (!Actor || !DMI)
    {
        return false;
    }

    // Try the interface first — actor gets final say
    if (Actor->GetClass()->ImplementsInterface(UISMCustomDataMaterialProvider::StaticClass()))
    {
        IISMCustomDataMaterialProvider* Provider =
            Cast<IISMCustomDataMaterialProvider>(Actor);

        // Check if actor wants to skip this slot
        if (Provider && IISMCustomDataMaterialProvider::Execute_ShouldSkipSlot(Actor, SlotIndex))
        {
            return false;
        }

        // Offer the DMI to the actor
        if (Provider)
        {
            IISMCustomDataMaterialProvider::Execute_ApplyDMIToSlot(Actor, SlotIndex, DMI);
            return true;
        }
    }

    // Fallback: apply directly to first mesh component
    return ApplyDMIToFirstMeshComponent(Actor, SlotIndex, DMI);
}

bool UISMCustomDataConversionSystem::ApplyDMIToFirstMeshComponent(
    AActor* Actor,
    int32 SlotIndex,
    UMaterialInstanceDynamic* DMI)
{
    if (!Actor || !DMI)
    {
        return false;
    }

    // Find first mesh component with enough material slots
    TArray<UMeshComponent*> MeshComponents;
    Actor->GetComponents<UMeshComponent>(MeshComponents);

    for (UMeshComponent* MeshComp : MeshComponents)
    {
        if (MeshComp && SlotIndex < MeshComp->GetNumMaterials())
        {
            MeshComp->SetMaterial(SlotIndex, DMI);
            return true;
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("ISMCustomDataConversionSystem: No mesh component with slot %d found on actor %s"),
        SlotIndex,
        *Actor->GetName());

    return false;
}