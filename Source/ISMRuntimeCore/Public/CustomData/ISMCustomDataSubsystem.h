#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CustomData/ISMCustomDataSchema.h"
#include "ISMCustomDataSubsystem.generated.h"

// Forward declarations
struct FISMInstanceHandle;
struct FISMCustomDataConversionResult;

// ============================================================
//  FISMMaterialSignature
// ============================================================

/**
 * Canonical pool key: template material + only the custom data values that
 * the schema maps to material parameters. Unmapped indices (physics mass,
 * AI state, etc.) are excluded so they don't fragment the pool.
 */
USTRUCT()
struct ISMRUNTIMECORE_API FISMMaterialSignature
{
    GENERATED_BODY()

    /** The material template this DMI was created from */
    UPROPERTY()
    TWeakObjectPtr<UMaterialInterface> Template;

    /**
     * Packed values of only the schema-mapped channels, in channel order.
     * Built by FISMCustomDataSchema::ExtractMappedValues().
     */
    TArray<float> MappedValues;

    bool operator==(const FISMMaterialSignature& Other) const;

    friend uint32 GetTypeHash(const FISMMaterialSignature& Sig);
};

// ============================================================
//  FISMPooledMaterial
// ============================================================

/** Single entry in the shared DMI pool with LRU and ref-count tracking. */
USTRUCT()
struct FISMPooledMaterial
{
    GENERATED_BODY()

    UPROPERTY()
    UMaterialInstanceDynamic* DMI = nullptr;

    /** GFrameCounter value when this entry was last requested */
    uint32 LastUsedFrame = 0;

    /** Live instance count using this DMI. Entry is eviction-eligible when 0. */
    int32 RefCount = 0;
};

// ============================================================
//  Statistics structs
// ============================================================

USTRUCT(BlueprintType)
struct FISMDMIPoolStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TotalPooledDMIs = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 CacheHits = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 CacheMisses = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 EvictedEntries = 0;

    float GetHitRate() const
    {
        const int32 Total = CacheHits + CacheMisses;
        return Total > 0 ? static_cast<float>(CacheHits) / static_cast<float>(Total) : 0.f;
    }
};

USTRUCT(BlueprintType)
struct FISMHotPoolStats
{
    GENERATED_BODY()

    /** DMIs currently claimed from hot pools */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 ActiveHotDMIs = 0;

    /** Total pre-warmed slots across all hot pools */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TotalHotPoolCapacity = 0;

    /** Handles that used a transient DMI because the pool was exhausted */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TransientFallbackCount = 0;

    /** Surrenders triggered by Timed settle mode */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TimedSurrenders = 0;

    /** Surrenders triggered by AutoDetect settle mode */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 AutoDetectSurrenders = 0;

    /** Surrenders triggered by explicit EndAnimation() call */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 ManualSurrenders = 0;

    /** Total surrenders (all modes combined) */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TotalSurrenders = 0;
};

// ============================================================
//  Hot DMI types
// ============================================================

/** Settle mode for FISMHotDMIHandle — controls when the handle auto-surrenders */
UENUM(BlueprintType)
enum class EISMAnimationSettleMode : uint8
{
    Manual,      // Explicit SurrenderHotDMI() call required
    AutoDetect,  // Surrender when mapped values have been stable for N frames
    Timed        // Surrender after a fixed duration
};

/** Request parameters passed to AcquireHotDMI */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMHotDMIRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hot DMI")
    EISMAnimationSettleMode SettleMode = EISMAnimationSettleMode::Manual;

    /** For AutoDetect: number of frames with no mapped-value changes before surrender */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hot DMI",
        meta = (EditCondition = "SettleMode == EISMAnimationSettleMode::AutoDetect"))
    int32 SettleFrameThreshold = 10;

    /** For Timed: seconds before automatic surrender */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hot DMI",
        meta = (EditCondition = "SettleMode == EISMAnimationSettleMode::Timed"))
    float SettleDuration = 1.f;

    /** If true, use a transient (non-pooled) DMI when all hot slots are claimed */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hot DMI")
    bool bAllowTransientFallback = true;
};

/**
 * An exclusive, writeable DMI handle for a currently-animating instance.
 * Returned by UISMCustomDataSubsystem::AcquireHotDMI.
 * Surrender via UISMCustomDataSubsystem::SurrenderHotDMI when animation
 * settles — the subsystem then resolves the correct shared pooled DMI.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMHotDMIHandle
{
    GENERATED_BODY()

    /** Back-pointer to the instance being animated (non-owning, not GC'd) */
    FISMInstanceHandle* InstanceHandle = nullptr;

    /** The exclusive DMI for this animation. Write material params directly to this. */
    UPROPERTY()
    TWeakObjectPtr<UMaterialInstanceDynamic> HotDMI;

    /** Which material slot this handle covers */
    UPROPERTY()
    int32 MaterialSlotIndex = 0;

    /**
     * Index into UISMHotDMIPool's slot array.
     * INDEX_NONE means this is a transient (non-pooled) DMI.
     */
    UPROPERTY()
    int32 PoolSlotIndex = INDEX_NONE;

    /** The request parameters that created this handle */
    FISMHotDMIRequest Request;

    // ===== Internal settle tracking =====

    float ElapsedTime = 0.f;
    int32 StableFrameCount = 0;
    TArray<float> LastMappedValues;

    /** True if this handle is currently live */
    bool IsActive() const { return HotDMI.IsValid(); }

    /**
     * Per-frame tick. Advances Timed/AutoDetect settle counters.
     * Calls SurrenderHotDMI internally when condition is met.
     * Called automatically by UISMCustomDataSubsystem::TickHotHandles.
     */
    void Tick(float DeltaTime, UWorld* World);
};

// ============================================================
//  UISMHotDMIPool
// ============================================================

/**
 * Fixed-size ring-buffer pool of pre-created DMIs for a single material template.
 * Owned by UISMCustomDataSubsystem — one pool per unique template.
 */
UCLASS()
class ISMRUNTIMECORE_API UISMHotDMIPool : public UObject
{
    GENERATED_BODY()

public:
    /** Initialize or re-initialize the pool with a template and slot count. */
    void Initialize(UMaterialInterface* InTemplate, int32 InPoolSize);

    /**
     * Claim a slot from the pool.
     * @param bAllowTransientFallback   If true and pool is full, create a non-tracked DMI
     * @param OutSlotIndex              INDEX_NONE if using transient fallback
     * @return                          Valid DMI, or nullptr if exhausted and no fallback
     */
    UMaterialInstanceDynamic* Acquire(bool bAllowTransientFallback, int32& OutSlotIndex);

    /**
     * Release a claimed slot. Resets DMI parameters to template defaults.
     * No-op for INDEX_NONE (transient fallbacks are not tracked).
     */
    void Release(int32 SlotIndex);

    /** Number of currently claimed slots */
    int32 GetActiveCount() const;

    /** Total slot capacity */
    int32 GetPoolSize() const { return Slots.Num(); }

    /** The template material all DMIs in this pool were created from */
    UMaterialInterface* GetTemplate() const { return SourceTemplate.Get(); }

private:
    struct FHotSlot
    {
        UPROPERTY()
        UMaterialInstanceDynamic* DMI = nullptr;
        bool bClaimed = false;
    };

    TWeakObjectPtr<UMaterialInterface> SourceTemplate;
    TArray<FHotSlot> Slots;
};

// ============================================================
//  UISMCustomDataSubsystem
// ============================================================

/**
 * Game instance subsystem owning the shared DMI pool and hot DMI pools.
 *
 * Lifetime: game instance. Pool survives level loads — desirable since
 * materials are project assets that don't change between levels.
 *
 * Two pools:
 *   Shared pool — maps (template + schema-mapped PICD values) → pooled DMI.
 *                 Instances with identical mapped values share one DMI.
 *   Hot pools   — per-template fixed-size ring buffers of exclusive DMIs
 *                 for instances whose values are changing this frame.
 *                 Surrendered when animation settles → transitions to shared pool.
 */
UCLASS()
class ISMRUNTIMECORE_API UISMCustomDataSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ===== Lifecycle =====

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ===== Schema Resolution =====

    /**
     * Resolve a schema by name from UISMRuntimeDeveloperSettings. Cached.
     * Passing NAME_None returns the project default schema (if configured).
     * Returns nullptr if name is not registered.
     */
    const FISMCustomDataSchema* ResolveSchema(FName SchemaName) const;

    /**
     * Resolve the schema for a given instance handle.
     * Resolution order: InstanceDataAsset::SchemaName → project default → nullptr.
     * Respects bUsePICDConversion opt-out on the data asset.
     *
     * @param OutSchemaName   Filled with the resolved registry key name on success
     */
    const FISMCustomDataSchema* ResolveSchemaForInstance(
        const FISMInstanceHandle& InstanceHandle,
        FName& OutSchemaName) const;

    // ===== Shared DMI Pool =====

    /**
     * Get or create a pooled DMI for the given template + custom data + schema.
     * Cache hits increment ref count. Misses create, parameterize, and cache a new DMI.
     *
     * @param Template      ISM's material for this slot (source of DMI template)
     * @param CustomData    Full PICD values for the instance
     * @param Schema        Defines which channels to apply and how to pack them
     * @param SlotIndex     Material slot index (used for schema ApplicableSlots check)
     * @return              Valid DMI, or nullptr if Template is null
     */
    UMaterialInstanceDynamic* GetOrCreateDMI(
        UMaterialInterface* Template,
        const TArray<float>& CustomData,
        const FISMCustomDataSchema& Schema,
        int32 SlotIndex = 0);

    /**
     * Decrement ref count for a pooled DMI.
     * Entry stays in pool until eviction — does not destroy immediately.
     */
    void ReleaseDMI(
        UMaterialInterface* Template,
        const TArray<float>& CustomData,
        const FISMCustomDataSchema& Schema,
        int32 SlotIndex = 0);

    /**
     * Evict unreferenced shared pool entries older than MaxAgeFrames.
     * Pass -1 to use DefaultEvictionAgeFrames from project settings.
     * Pass 0 to evict all unreferenced entries immediately.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Custom Data|Shared Pool")
    void EvictStaleDMIs(int32 MaxAgeFrames = -1);

    /** Flush entire shared pool. All DMIs recreated on next request. */
    UFUNCTION(BlueprintCallable, Category = "ISM Custom Data|Shared Pool")
    void FlushSharedPool();

    UFUNCTION(BlueprintCallable, Category = "ISM Custom Data|Shared Pool")
    FISMDMIPoolStats GetSharedPoolStats() const { return SharedPoolStats; }

    // ===== Hot DMI Pool =====

    /**
     * Acquire an exclusive DMI for an animating instance.
     * Claims a pre-warmed slot from the per-template hot pool (creates pool if needed).
     * Write material parameters directly to HotHandle.HotDMI each frame.
     *
     * @param Handle        The instance being animated
     * @param Template      Material template (should match ISM's slot material)
     * @param SlotIndex     Which material slot this animation covers
     * @param Request       Settle mode, threshold, and fallback config
     */
    FISMHotDMIHandle AcquireHotDMI(
        FISMInstanceHandle& Handle,
        UMaterialInterface* Template,
        int32 SlotIndex,
        const FISMHotDMIRequest& Request);

    /**
     * Surrender a hot DMI handle.
     * Releases the slot back to the pool, resolves the correct shared DMI
     * from the final settled values, and applies it to the converted actor.
     * Handle is invalidated (IsActive() returns false after this call).
     */
    void SurrenderHotDMI(FISMHotDMIHandle& HotHandle, UWorld* World);

    /**
     * Tick all active hot handles for Timed/AutoDetect auto-surrender.
     * Called automatically from OnWorldTick.
     */
    void TickHotHandles(float DeltaTime, UWorld* World);

    /**
     * Pre-warm a hot pool to avoid first-acquisition cost.
     * Called automatically on first AcquireHotDMI for a given template.
     * @param PoolSize   0 = use DefaultHotPoolSizePerTemplate from project settings
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Custom Data|Hot Pool")
    void PrewarmHotPool(UMaterialInterface* Template, int32 PoolSize = 0);

    /** Flush all hot pools and invalidate all active handles. */
    UFUNCTION(BlueprintCallable, Category = "ISM Custom Data|Hot Pool")
    void FlushHotPools();

    UFUNCTION(BlueprintCallable, Category = "ISM Custom Data|Hot Pool")
    FISMHotPoolStats GetHotPoolStats() const { return HotPoolStats; }

    // ===== Stats =====

    /** Reset all cumulative statistics counters (preserves live state). */
    UFUNCTION(BlueprintCallable, Category = "ISM Custom Data")
    void ResetStats();

private:
    // ===== Shared Pool =====

    TMap<FISMMaterialSignature, FISMPooledMaterial> SharedPool;
    FISMDMIPoolStats SharedPoolStats;
    uint32 LastEvictionFrame = 0;

    // ===== Schema Cache =====

    /** Points into UISMRuntimeDeveloperSettings CDO memory. Invalidated on settings change. */
    mutable TMap<FName, const FISMCustomDataSchema*> SchemaCache;

    // ===== Hot Pools =====

    UPROPERTY()
    TMap<TWeakObjectPtr<UMaterialInterface>, UISMHotDMIPool*> HotPools;

    /** Non-owning pointers to live FISMHotDMIHandles (stack/member lifetime) */
    TArray<FISMHotDMIHandle*> ActiveHotHandles;

    FISMHotPoolStats HotPoolStats;

    // ===== Delegate Handles =====

    FDelegateHandle WorldTickHandle;

#if WITH_EDITOR
    FDelegateHandle SettingsChangedHandle;
#endif

    // ===== Helpers =====

    FISMMaterialSignature BuildSignature(
        UMaterialInterface* Template,
        const TArray<float>& CustomData,
        const FISMCustomDataSchema& Schema,
        int32 SlotIndex) const;

    UMaterialInstanceDynamic* CreateAndApplyDMI(
        UMaterialInterface* Template,
        const TArray<float>& CustomData,
        const FISMCustomDataSchema& Schema,
        int32 SlotIndex);

    void ApplyCustomDataToMaterial(
        UMaterialInstanceDynamic* DMI,
        const TArray<float>& CustomData,
        const FISMCustomDataSchema& Schema,
        int32 SlotIndex) const;

    void EvictLRUEntry();

    UISMHotDMIPool* GetOrCreateHotPool(UMaterialInterface* Template);

    void OnWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

#if WITH_EDITOR
    void OnDeveloperSettingsChanged(UObject* Settings, struct FPropertyChangedEvent& PropertyChangedEvent);
#endif
};