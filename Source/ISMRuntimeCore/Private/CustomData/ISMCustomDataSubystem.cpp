// ISMCustomDataSubsystem.cpp
#include "CustomData/ISMCustomDataSubsystem.h"

#include "ISMInstanceHandle.h"
#include "ISMRuntimeComponent.h"
#include "ISMInstanceDataAsset.h"
#include "CustomData/ISMCustomDataSchema.h"
#include "CustomData/ISMCustomDataConversionSystem.h"
#include "CustomData/ISMCustomDataMaterialProvider.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#endif

// ============================================================
//  UISMHotDMIPool
// ============================================================

void UISMHotDMIPool::Initialize(UMaterialInterface* InTemplate, int32 InPoolSize)
{
    SourceTemplate = InTemplate;
    Slots.SetNum(InPoolSize);

    for (FHotSlot& Slot : Slots)
    {
        if (SourceTemplate.Get())
        {
            Slot.DMI = UMaterialInstanceDynamic::Create(SourceTemplate.Get(), this);
        }
        Slot.bClaimed = false;
    }
}

UMaterialInstanceDynamic* UISMHotDMIPool::Acquire(bool bAllowTransientFallback, int32& OutSlotIndex)
{
    // Find first unclaimed slot
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        if (!Slots[i].bClaimed && Slots[i].DMI)
        {
            Slots[i].bClaimed = true;
            OutSlotIndex = i;
            return Slots[i].DMI;
        }
    }

    // Pool exhausted
    OutSlotIndex = INDEX_NONE;

    if (bAllowTransientFallback && SourceTemplate.Get())
    {
        // Create a transient DMI not tracked by the pool
        return UMaterialInstanceDynamic::Create(SourceTemplate.Get(), GetTransientPackage());
    }

    return nullptr;
}

void UISMHotDMIPool::Release(int32 SlotIndex)
{
    if (!Slots.IsValidIndex(SlotIndex))
    {
        return; // INDEX_NONE or out of range — transient, no-op
    }

    FHotSlot& Slot = Slots[SlotIndex];
    Slot.bClaimed = false;

    // Reset parameters to template defaults so next claimant starts clean
    if (Slot.DMI && SourceTemplate.Get())
    {
        Slot.DMI->CopyParameterOverrides(Cast<UMaterialInstance>(SourceTemplate.Get()));
        Slot.DMI->ClearParameterValues();
    }
}

int32 UISMHotDMIPool::GetActiveCount() const
{
    int32 Count = 0;
    for (const FHotSlot& Slot : Slots)
    {
        if (Slot.bClaimed) { ++Count; }
    }
    return Count;
}

// ============================================================
//  UISMCustomDataSubsystem — Lifecycle
// ============================================================

void UISMCustomDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Register world tick delegate for eviction and hot handle ticking
    WorldTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(
        this, &UISMCustomDataSubsystem::OnWorldTick);

#if WITH_EDITOR
    // Invalidate schema cache when developer settings change in editor
    if (UISMRuntimeDeveloperSettings* Settings =
        GetMutableDefault<UISMRuntimeDeveloperSettings>())
    {
        SettingsChangedHandle = Settings->OnSettingChanged().AddUObject(
            this, &UISMCustomDataSubsystem::OnDeveloperSettingsChanged);
    }
#endif
}

void UISMCustomDataSubsystem::Deinitialize()
{
    FWorldDelegates::OnWorldPostActorTick.Remove(WorldTickHandle);

#if WITH_EDITOR
    if (UISMRuntimeDeveloperSettings* Settings =
        GetMutableDefault<UISMRuntimeDeveloperSettings>())
    {
        Settings->OnSettingChanged().Remove(SettingsChangedHandle);
    }
#endif

    FlushSharedPool();
    FlushHotPools();

    Super::Deinitialize();
}

// ============================================================
//  Schema Resolution
// ============================================================

const FISMCustomDataSchema* UISMCustomDataSubsystem::ResolveSchema(FName SchemaName) const
{
    // Check cache first
    if (const FISMCustomDataSchema** Cached = SchemaCache.Find(SchemaName))
    {
        return *Cached;
    }

    // Resolve from settings (CDO — always valid)
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    const FISMCustomDataSchema* Schema = Settings->ResolveSchema(SchemaName);

    // Cache result (including nullptr, to avoid repeated failed lookups)
    SchemaCache.Add(SchemaName, Schema);

    return Schema;
}

const FISMCustomDataSchema* UISMCustomDataSubsystem::ResolveSchemaForInstance(
    const FISMInstanceHandle& InstanceHandle,
    FName& OutSchemaName) const
{
    OutSchemaName = NAME_None;

    UISMRuntimeComponent* Comp = InstanceHandle.Component.Get();
    if (!Comp)
    {
        return nullptr;
    }

    // Opt-out check
    if (Comp->InstanceData && !Comp->InstanceData->bUsePICDConversion)
    {
        return nullptr;
    }

    // Try explicit schema name from asset
    if (Comp->InstanceData && Comp->InstanceData->SchemaName != NAME_None)
    {
        const FISMCustomDataSchema* Schema = ResolveSchema(Comp->InstanceData->SchemaName);
        if (Schema)
        {
            OutSchemaName = Comp->InstanceData->SchemaName;
            return Schema;
        }
    }

    // Fall back to project default
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    if (Settings->DefaultSchemaName != NAME_None)
    {
        const FISMCustomDataSchema* Schema = ResolveSchema(Settings->DefaultSchemaName);
        if (Schema)
        {
            OutSchemaName = Settings->DefaultSchemaName;
            return Schema;
        }
    }

    return nullptr;
}

// ============================================================
//  Shared DMI Pool
// ============================================================

UMaterialInstanceDynamic* UISMCustomDataSubsystem::GetOrCreateDMI(
    UMaterialInterface* Template,
    const TArray<float>& CustomData,
    const FISMCustomDataSchema& Schema,
    int32 SlotIndex)
{
    if (!Template)
    {
        return nullptr;
    }

    const FISMMaterialSignature Sig = BuildSignature(Template, CustomData, Schema, SlotIndex);

    // Cache hit
    if (FISMPooledMaterial* Existing = SharedPool.Find(Sig))
    {
        Existing->LastUsedFrame = GFrameCounter;
        Existing->RefCount++;
        SharedPoolStats.CacheHits++;
        SharedPoolStats.TotalPooledDMIs = SharedPool.Num();
        return Existing->DMI;
    }

    // Cache miss — create new DMI
    SharedPoolStats.CacheMisses++;

    // Enforce max pool size via LRU eviction before adding
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    const int32 MaxSize = Settings->DefaultMaxSharedPoolSize;
    if (MaxSize > 0 && SharedPool.Num() >= MaxSize)
    {
        EvictLRUEntry();
    }

    UMaterialInstanceDynamic* NewDMI = CreateAndApplyDMI(Template, CustomData, Schema, SlotIndex);
    if (!NewDMI)
    {
        return nullptr;
    }

    FISMPooledMaterial& Entry = SharedPool.Add(Sig);
    Entry.DMI = NewDMI;
    Entry.LastUsedFrame = GFrameCounter;
    Entry.RefCount = 1;

    SharedPoolStats.TotalPooledDMIs = SharedPool.Num();

    return NewDMI;
}

void UISMCustomDataSubsystem::ReleaseDMI(
    UMaterialInterface* Template,
    const TArray<float>& CustomData,
    const FISMCustomDataSchema& Schema,
    int32 SlotIndex)
{
    if (!Template)
    {
        return;
    }

    const FISMMaterialSignature Sig = BuildSignature(Template, CustomData, Schema, SlotIndex);

    if (FISMPooledMaterial* Entry = SharedPool.Find(Sig))
    {
        Entry->RefCount = FMath::Max(0, Entry->RefCount - 1);
    }
}

void UISMCustomDataSubsystem::EvictStaleDMIs(int32 MaxAgeFrames)

{
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();

    const int32 EffectiveMaxAge = (MaxAgeFrames == -1)
        ? Settings->DefaultEvictionAgeFrames
        : MaxAgeFrames;

    TArray<FISMMaterialSignature> ToRemove;

    for (auto& Pair : SharedPool)
    {
        const FISMPooledMaterial& Entry = Pair.Value;

        // Never evict referenced entries
        if (Entry.RefCount > 0)
        {
            continue;
        }

        const uint32 Age = GFrameCounter - Entry.LastUsedFrame;
        if (EffectiveMaxAge == 0 || Age >= static_cast<uint32>(EffectiveMaxAge))
        {
            ToRemove.Add(Pair.Key);
        }
    }

    for (const FISMMaterialSignature& Sig : ToRemove)
    {
        SharedPool.Remove(Sig);
        SharedPoolStats.EvictedEntries++;
    }

    SharedPoolStats.TotalPooledDMIs = SharedPool.Num();
}

void UISMCustomDataSubsystem::FlushSharedPool()
{
    SharedPool.Empty();
    SharedPoolStats.TotalPooledDMIs = 0;
}

// ============================================================
//  Hot DMI Pool
// ============================================================

FISMHotDMIHandle UISMCustomDataSubsystem::AcquireHotDMI(
    FISMInstanceHandle& Handle,
    UMaterialInterface* Template,
    int32 SlotIndex,
    const FISMHotDMIRequest& Request)
{
    FISMHotDMIHandle HotHandle;
    HotHandle.InstanceHandle = &Handle;
    HotHandle.MaterialSlotIndex = SlotIndex;
    HotHandle.Request = Request;

    UISMHotDMIPool* Pool = GetOrCreateHotPool(Template);
    if (!Pool)
    {
        return HotHandle; // Inactive handle
    }

    int32 SlotIdx = INDEX_NONE;
    UMaterialInstanceDynamic* DMI = Pool->Acquire(Request.bAllowTransientFallback, SlotIdx);

    if (!DMI)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("ISMCustomDataSubsystem: Hot pool exhausted for template %s, no fallback available"),
            Template ? *Template->GetName() : TEXT("null"));
        return HotHandle;
    }

    HotHandle.HotDMI = DMI;
    HotHandle.PoolSlotIndex = SlotIdx;

    if (SlotIdx == INDEX_NONE)
    {
        // Transient fallback
        HotPoolStats.TransientFallbackCount++;
    }

    HotPoolStats.ActiveHotDMIs++;
    HotPoolStats.TotalHotPoolCapacity = 0;
    for (auto& Pair : HotPools)
    {
        if (Pair.Value) { HotPoolStats.TotalHotPoolCapacity += Pair.Value->GetPoolSize(); }
    }

    // Track for ticking
    ActiveHotHandles.Add(&HotHandle);

    return HotHandle;
}

void UISMCustomDataSubsystem::SurrenderHotDMI(FISMHotDMIHandle& HotHandle, UWorld* World)
{
    if (!HotHandle.IsActive())
    {
        return;
    }

    // Remove from active list
    ActiveHotHandles.Remove(&HotHandle);

    // Find the pool for this DMI's template to release the slot
    UMaterialInstanceDynamic* HotDMI = HotHandle.HotDMI.Get();
    if (HotDMI && HotHandle.PoolSlotIndex != INDEX_NONE)
    {
        // Find which pool owns this slot
        for (auto& Pair : HotPools)
        {
            if (Pair.Value && Pair.Value->GetTemplate())
            {
                // Release the slot — resets parameters to template defaults
                Pair.Value->Release(HotHandle.PoolSlotIndex);
                break;
            }
        }
    }

    HotPoolStats.ActiveHotDMIs = FMath::Max(0, HotPoolStats.ActiveHotDMIs - 1);

    // Resolve and apply the correct shared DMI for the final settled values
    FISMInstanceHandle* Handle = HotHandle.InstanceHandle;
    if (Handle && Handle->IsValid() && World)
    {
        // If the instance is currently a converted actor, apply the shared DMI
        if (Handle->ConvertedActor.IsValid())
        {
            FName SchemaName;
            const FISMCustomDataSchema* Schema = ResolveSchemaForInstance(*Handle, SchemaName);
            if (Schema && Handle->Component.IsValid() && Handle->Component->ManagedISMComponent)
            {
                UMaterialInterface* Template = Handle->Component->ManagedISMComponent->GetMaterial(
                    HotHandle.MaterialSlotIndex);

                if (Template)
                {
                    UMaterialInstanceDynamic* SharedDMI = GetOrCreateDMI(
                        Template,
                        Handle->CachedCustomData,
                        *Schema,
                        HotHandle.MaterialSlotIndex);

                    if (SharedDMI)
                    {
                        UISMCustomDataConversionSystem::ApplyToActor(
                            // Build a minimal result for this single slot
                            [&]() -> FISMCustomDataConversionResult {
                                FISMCustomDataConversionResult R;
                                R.bSuccess = true;
                                R.ResolvedSchema = Schema;
                                R.ResolvedSchemaName = SchemaName;
                                R.DMIsBySlot.Add(HotHandle.MaterialSlotIndex, SharedDMI);
                                return R;
                            }(),
                                Handle->ConvertedActor.Get());
                    }
                }
            }
        }
    }

    // Invalidate the handle
    HotHandle.HotDMI.Reset();
    HotHandle.PoolSlotIndex = INDEX_NONE;
    HotHandle.InstanceHandle = nullptr;
}

void UISMCustomDataSubsystem::TickHotHandles(float DeltaTime, UWorld* World)
{
    // Iterate backwards so we can safely remove surrendered handles
    for (int32 i = ActiveHotHandles.Num() - 1; i >= 0; --i)
    {
        FISMHotDMIHandle* Handle = ActiveHotHandles[i];
        if (!Handle || !Handle->IsActive())
        {
            ActiveHotHandles.RemoveAtSwap(i, EAllowShrinking::Yes);
            continue;
        }

        Handle->Tick(DeltaTime, World);

        // Surrender may have been triggered by Tick (Timed or AutoDetect settle)
        if (!Handle->IsActive())
        {
            ActiveHotHandles.RemoveAtSwap(i, EAllowShrinking::Yes);
        }
    }
}

void UISMCustomDataSubsystem::PrewarmHotPool(UMaterialInterface* Template, int32 PoolSize)
{
    if (!Template)
    {
        return;
    }

    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    const int32 EffectiveSize = (PoolSize > 0) ? PoolSize : Settings->DefaultHotPoolSizePerTemplate;

    UISMHotDMIPool* Pool = GetOrCreateHotPool(Template);
    if (Pool && Pool->GetPoolSize() < EffectiveSize)
    {
        // Re-initialize with larger size (simple approach — recreates all DMIs)
        Pool->Initialize(Template, EffectiveSize);
    }

    HotPoolStats.TotalHotPoolCapacity = 0;
    for (auto& Pair : HotPools)
    {
        if (Pair.Value) { HotPoolStats.TotalHotPoolCapacity += Pair.Value->GetPoolSize(); }
    }
}

void UISMCustomDataSubsystem::FlushHotPools()
{
    ActiveHotHandles.Empty();

    for (auto& Pair : HotPools)
    {
        if (Pair.Value)
        {
            Pair.Value->ConditionalBeginDestroy();
        }
    }

    HotPools.Empty();
    HotPoolStats = FISMHotPoolStats();
}

// ============================================================
//  Statistics
// ============================================================

void UISMCustomDataSubsystem::ResetStats()
{
    SharedPoolStats = FISMDMIPoolStats();
    SharedPoolStats.TotalPooledDMIs = SharedPool.Num();

    HotPoolStats.TotalSurrenders = 0;
    HotPoolStats.TransientFallbackCount = 0;
    HotPoolStats.AutoDetectSurrenders = 0;
    HotPoolStats.ManualSurrenders = 0;
    HotPoolStats.TimedSurrenders = 0;
    // Preserve ActiveHotDMIs and TotalHotPoolCapacity — those are live state
}

// ============================================================
//  Internal Helpers
// ============================================================

FISMMaterialSignature UISMCustomDataSubsystem::BuildSignature(
    UMaterialInterface* Template,
    const TArray<float>& CustomData,
    const FISMCustomDataSchema& Schema,
    int32 SlotIndex) const
{
    FISMMaterialSignature Sig;
    Sig.Template = Template;
    Sig.MappedValues = Schema.ExtractMappedValues(CustomData);
    return Sig;
}

UMaterialInstanceDynamic* UISMCustomDataSubsystem::CreateAndApplyDMI(
    UMaterialInterface* Template,
    const TArray<float>& CustomData,
    const FISMCustomDataSchema& Schema,
    int32 SlotIndex)
{
    UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(Template, this);
    if (!DMI)
    {
        return nullptr;
    }

    ApplyCustomDataToMaterial(DMI, CustomData, Schema, SlotIndex);
    return DMI;
}

void UISMCustomDataSubsystem::ApplyCustomDataToMaterial(
    UMaterialInstanceDynamic* DMI,
    const TArray<float>& CustomData,
    const FISMCustomDataSchema& Schema,
    int32 SlotIndex) const
{
    if (!DMI || CustomData.Num() == 0)
    {
        return;
    }

    for (const FISMCustomDataChannelDef& Channel : Schema.Channels)
    {
        // Check if any value in this channel's range is available
        if (Channel.DataIndex >= CustomData.Num())
        {
            continue;
        }

        if (Channel.bIsVector)
        {
            // Pack consecutive indices into FLinearColor
            FLinearColor Vec(0.f, 0.f, 0.f, 0.f);
            float* Components = &Vec.R;

            for (int32 c = 0; c < Channel.ComponentCount; ++c)
            {
                const int32 Idx = Channel.DataIndex + c;
                if (CustomData.IsValidIndex(Idx))
                {
                    Components[c] = CustomData[Idx];
                }
            }

            DMI->SetVectorParameterValue(Channel.ParameterName, Vec);
        }
        else
        {
            DMI->SetScalarParameterValue(Channel.ParameterName, CustomData[Channel.DataIndex]);
        }
    }
}

void UISMCustomDataSubsystem::EvictLRUEntry()
{
    FISMMaterialSignature OldestSig;
    uint32 OldestFrame = TNumericLimits<uint32>::Max();
    bool bFound = false;

    for (const auto& Pair : SharedPool)
    {
        // Only evict unreferenced entries
        if (Pair.Value.RefCount > 0) { continue; }

        if (Pair.Value.LastUsedFrame < OldestFrame)
        {
            OldestFrame = Pair.Value.LastUsedFrame;
            OldestSig = Pair.Key;
            bFound = true;
        }
    }

    if (bFound)
    {
        SharedPool.Remove(OldestSig);
        SharedPoolStats.EvictedEntries++;
        SharedPoolStats.TotalPooledDMIs = SharedPool.Num();
    }
}

UISMHotDMIPool* UISMCustomDataSubsystem::GetOrCreateHotPool(UMaterialInterface* Template)
{
    if (!Template)
    {
        return nullptr;
    }

    TWeakObjectPtr<UMaterialInterface> Key(Template);

    if (UISMHotDMIPool** Existing = HotPools.Find(Key))
    {
        return *Existing;
    }

    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    const int32 PoolSize = Settings->DefaultHotPoolSizePerTemplate;

    UISMHotDMIPool* NewPool = NewObject<UISMHotDMIPool>(this);
    NewPool->Initialize(Template, PoolSize);
    HotPools.Add(Key, NewPool);

    HotPoolStats.TotalHotPoolCapacity += PoolSize;

    return NewPool;
}

void FISMHotDMIHandle::Tick(float DeltaTime, UWorld* World)
{
    if (!IsActive())
    {
        return;
    }
/*    // Auto-detect settle: if the instance's cached custom data matches the request, surrender the hot DMI
    if (InstanceHandle && InstanceHandle->IsValid())
    {
        if (InstanceHandle->CachedCustomData.Num() > 0 &&
            InstanceHandle->CachedCustomData == Request.ExpectedCustomData)
        {
            UISMCustomDataSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<UISMCustomDataSubsystem>();
            if (Subsystem)
            {
                Subsystem->SurrenderHotDMI(*this, World);
                Subsystem->HotPoolStats.AutoDetectSurrenders++;
            }
        }
    }
    // Timed settle: if this handle has been active for longer than the configured duration, surrender it
    TimeActive += DeltaTime;
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    if (Settings->DefaultHotDMISettleTimeSeconds > 0.f &&
        TimeActive >= Settings->DefaultHotDMISettleTimeSeconds)
    {
        UISMCustomDataSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<UISMCustomDataSubsystem>();
        if (Subsystem)
        {
            Subsystem->SurrenderHotDMI(*this, World);
            Subsystem->HotPoolStats.TimedSurrenders++;
        }
    }*/
}

void UISMCustomDataSubsystem::OnWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
    if (!World || TickType == LEVELTICK_TimeOnly)
    {
        return;
    }

    // Tick hot DMI handles (AutoDetect + Timed settle)
    TickHotHandles(DeltaSeconds, World);

    // Auto-eviction on configured cadence
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    if (Settings->DefaultEvictionAgeFrames > 0)
    {
        const uint32 FramesSinceEviction = GFrameCounter - LastEvictionFrame;
        if (FramesSinceEviction >= static_cast<uint32>(Settings->DefaultEvictionAgeFrames))
        {
            EvictStaleDMIs(-1); // -1 = use project settings value
            LastEvictionFrame = GFrameCounter;
        }
    }
	
}

#if WITH_EDITOR
void UISMCustomDataSubsystem::OnDeveloperSettingsChanged(
    UObject* Settings,
    FPropertyChangedEvent& PropertyChangedEvent)
{
    // Schema pointers may have changed — clear cache so next resolve is fresh
    SchemaCache.Empty();
}
#endif

// ============================================================
//  FISMMaterialSignature
// ============================================================

bool FISMMaterialSignature::operator==(const FISMMaterialSignature& Other) const
{
    if (Template != Other.Template)
    {
        return false;
    }

    if (MappedValues.Num() != Other.MappedValues.Num())
    {
        return false;
    }

    for (int32 i = 0; i < MappedValues.Num(); ++i)
    {
        // Exact float comparison — values come from PICD which is set explicitly,
        // not computed, so NearlyEqual is unnecessary and would mask distinct signatures
        if (MappedValues[i] != Other.MappedValues[i])
        {
            return false;
        }
    }

    return true;
}

uint32 GetTypeHash(const FISMMaterialSignature& Sig)
{
    uint32 Hash = GetTypeHash(Sig.Template);

    for (float Val : Sig.MappedValues)
    {
        // Combine each float's bit pattern into the hash
        Hash = HashCombine(Hash, GetTypeHash(Val));
    }

    return Hash;
}