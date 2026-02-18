// ISMFeedbackHandler.cpp
#include "ISMFeedbackHandler.h"
#include "Feedbacks/ISMFeedbackContext.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/DecalComponent.h"
#include "Logging/LogMacros.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

//DEFINE_LOG_CATEGORY_STATIC(LogTemp, Log, All);

// ===== Leaf Handler: Audio =====

bool UISMFeedbackHandler_Audio::Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext)
{
    // Load sound if needed
    USoundBase* LoadedSound = Sound.LoadSynchronous();
    if (!LoadedSound)
    {
        UE_LOG(LogTemp, Warning, TEXT("Audio Handler: Sound not set or failed to load"));
        return false;
    }

    // Get world
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return false;
    }

    // Calculate volume
    float FinalVolume = VolumeMultiplier;
    if (bScaleVolumeByIntensity)
    {
        FinalVolume *= Context.Intensity;
    }

    // Calculate pitch
    float FinalPitch = PitchMultiplier;
    //if (bScalePitchByScale)
    //{
    //    FinalPitch *= Context.Scale;
    //}

    // Play sound
    UGameplayStatics::PlaySoundAtLocation(
        World,
        LoadedSound,
        Context.Location,
        FinalVolume,
        FinalPitch,
        0.0f, // Start time
        AttenuationSettings
    );

    return true;
}

void UISMFeedbackHandler_Audio::PreloadAssets_Implementation()
{
    if (!Sound.IsNull())
    {
        Sound.LoadSynchronous();
    }
}

// ===== Leaf Handler: Niagara =====

bool UISMFeedbackHandler_Niagara::Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext)
{
    // Load system if needed
    UNiagaraSystem* LoadedSystem = System.LoadSynchronous();
    if (!LoadedSystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("Niagara Handler: System not set or failed to load"));
        return false;
    }

    // Get world
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return false;
    }

    // Calculate rotation
    FRotator SpawnRotation = Context.Rotation;
    if (bOrientToNormal)
    {
        SpawnRotation = Context.Normal.Rotation();
    }

    // Calculate scale
    FVector SpawnScale = FVector(Context.Scale * ScaleMultiplier);

    // Spawn system
    UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        World,
        LoadedSystem,
        Context.Location,
        SpawnRotation,
        SpawnScale,
        bAutoDestroy
    );

    if (!NiagaraComp)
    {
        return false;
    }

    // Pass context as parameters if enabled
    if (bPassContextAsParameters)
    {
        NiagaraComp->SetFloatParameter(TEXT("User.Intensity"), Context.Intensity);
        NiagaraComp->SetFloatParameter(TEXT("User.Scale"), Context.Scale * ScaleMultiplier);
        NiagaraComp->SetFloatParameter(TEXT("User.Velocity"), Context.Velocity.Size());
        NiagaraComp->SetVectorParameter(TEXT("User.VelocityVector"), Context.Velocity);
        NiagaraComp->SetVectorParameter(TEXT("User.Normal"), Context.Normal);
    }

    return true;
}

void UISMFeedbackHandler_Niagara::PreloadAssets_Implementation()
{
    if (!System.IsNull())
    {
        System.LoadSynchronous();
    }
}

// ===== Leaf Handler: Decal =====

bool UISMFeedbackHandler_Decal::Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext)
{
    if (!DecalMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("Decal Handler: DecalMaterial not set"));
        return false;
    }

    // Get world
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return false;
    }

    // Calculate decal size
    FVector DecalSize = FVector(50.0f, 50.0f, 50.0f); // Base size
    if (bScaleByIntensity)
    {
        DecalSize *= Context.Intensity;
    }
    DecalSize *= Context.Scale;

    // Spawn decal
    UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(
        World,
        DecalMaterial,
        DecalSize,
        Context.Location,
        Context.Normal.Rotation(),
        Duration
    );

    return Decal != nullptr;
}

// ===== Leaf Handler: Debug =====

bool UISMFeedbackHandler_Debug::Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext)
{
    // Get world
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return false;
    }

    // Log to console
    if (bLogToConsole)
    {
        UE_LOG(LogTemp, Display,
            TEXT("Debug Handler: Tag='%s' Location=%s Intensity=%.2f Scale=%.2f"),
            *Context.FeedbackTag.ToString(),
            *Context.Location.ToString(),
            Context.Intensity,
            Context.Scale);
    }

    // Draw debug sphere
    if (bDrawDebugSphere)
    {
        DrawDebugSphere(
            World,
            Context.Location,
            50.0f * Context.Scale,
            12,
            DebugColor,
            false,
            DrawDuration,
            0,
            2.0f
        );
    }

    // Draw debug arrow (normal)
    if (bDrawDebugArrow)
    {
        DrawDebugDirectionalArrow(
            World,
            Context.Location,
            Context.Location + Context.Normal * 100.0f,
            50.0f,
            FColor::Yellow,
            false,
            DrawDuration,
            0,
            2.0f
        );
    }

    return true;
}


// ===== Composite Handler: Matcher Base =====

bool UISMFeedbackMatcherHandler::Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext)
{
    // 1. Derive tag from context (implemented by subclasses)
    FGameplayTag DerivedTag = DeriveTag(Context);

    // 2. Find handler for derived tag
    UISMFeedbackHandler* Handler = FindHandlerForTag(DerivedTag);

    // 3. If not found, use default handler
    if (!Handler)
    {
        Handler = DefaultHandler;
    }

    // 4. Execute handler if found
    if (Handler)
    {
        return Handler->Execute(Context, WorldContext);
    }

    // No handler found
    UE_LOG(LogTemp, Verbose,
        TEXT("MatcherHandler: No handler found for derived tag '%s' (original tag: '%s')"),
        *DerivedTag.ToString(),
        *Context.FeedbackTag.ToString());

    return false;
}

void UISMFeedbackMatcherHandler::PreloadAssets_Implementation()
{
    // Preload all child handlers
    for (const FTagHandlerMatchEntry& Entry : HandlerDB)
    {
        if (Entry.Handler)
        {
            Entry.Handler->PreloadAssets();
        }
    }

    // Preload default handler
    if (DefaultHandler)
    {
        DefaultHandler->PreloadAssets();
    }
}

UISMFeedbackHandler* UISMFeedbackMatcherHandler::FindHandlerForTag(FGameplayTag DerivedTag) const
{
    if (!DerivedTag.IsValid())
    {
        return nullptr;
    }

    // Search HandlerDB for matching tag
    for (const FTagHandlerMatchEntry& Entry : HandlerDB)
    {
        if (Entry.Tag == DerivedTag)
        {
            return Entry.Handler;
        }
    }

    return nullptr;
}

// ===== Concrete Matcher: Surface =====

FGameplayTag UISMFeedbackSurfaceMatcherHandler::DeriveTag_Implementation(const FISMFeedbackContext& Context) const
{
    // Get physical material from context
    UPhysicalMaterial* PhysMat = Context.PhysicalMaterial.Get();

    // Try subject participant if context material not available
    if (!PhysMat)
    {
        PhysMat = Context.Subject.ParticipantPhysicalMaterial.Get();
    }

    if (!PhysMat)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("SurfaceMatcher: No physical material in context"));
        return FGameplayTag();
    }

    // Get surface type
    EPhysicalSurface SurfaceType = PhysMat->SurfaceType;

    // Lookup in surface to tag map
    const FGameplayTag* FoundTag = SurfaceToTagMap.Find(SurfaceType);
    if (FoundTag && FoundTag->IsValid())
    {
        return *FoundTag;
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("SurfaceMatcher: No mapping found for SurfaceType %d"),
        (int32)SurfaceType);

    return FGameplayTag();
}

// ===== Concrete Matcher: Intensity =====

FGameplayTag UISMFeedbackIntensityMatcherHandler::DeriveTag_Implementation(const FISMFeedbackContext& Context) const
{
    float Intensity = Context.Intensity;

    // Find highest threshold that intensity meets
    FGameplayTag BestTag;
    float BestThreshold = -1.0f;

    for (const TPair<float, FGameplayTag>& Pair : IntensityThresholds)
    {
        float Threshold = Pair.Key;
        const FGameplayTag& Tag = Pair.Value;

        // Check if intensity meets this threshold and it's better than current best
        if (Intensity >= Threshold && Threshold > BestThreshold)
        {
            BestThreshold = Threshold;
            BestTag = Tag;
        }
    }

    if (BestTag.IsValid())
    {
        return BestTag;
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("IntensityMatcher: No threshold met for intensity %.2f"),
        Intensity);

    return FGameplayTag();
}