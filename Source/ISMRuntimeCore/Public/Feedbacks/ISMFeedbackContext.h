// ISMFeedbackContext.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ISMFeedbackContext.generated.h"

// Forward declarations
class UISMRuntimeComponent;
class UActorComponent;
class UPhysicalMaterial;
class UStaticMesh;
class AActor;

/**
 * Message type for feedback lifecycle management.
 * Enables continuous feedback (rolling, grinding, harvesting) vs one-shot events.
 */
UENUM(BlueprintType)
enum class EISMFeedbackMessageType : uint8
{
    /** Default feedback type. Fire & forget, single event (impact, destruction, etc.) */
    ONE_SHOT = 0    ,//UMETA(DisplayName = "One-Shot Feedback", Tooltip = "Default feedback type. Indicates a basic fire & forget feedback situation"),

    /** Continuous feedback just started (begin looping sounds/particles) */
    STARTED = 1     ,//UMETA(DisplayName = "Started Continuous Feedback", Tooltip = "Indicates a continuous feedback which just started"),

    /** Continuous feedback finished successfully (stop sounds cleanly) */
    COMPLETED = 2   ,//UMETA(DisplayName = "Finished Continuous Feedback", Tooltip = "Indicates a continuous feedback finished successfully"),

    /** Continuous feedback state changed (update RTPC/parameters while continuing) */
    UPDATED = 3     ,//UMETA(DisplayName = "Changed Continuous Feedback", Tooltip = "Indicates a continuous feedback state has changed (context data is different)"),

    /** Continuous feedback cancelled/interrupted (stop sounds abruptly) */
    INTERRUPTED = 4 //UMETA(DisplayName = "Cancelled Continuous Feedback", Tooltip = "Indicates a continuous feedback state has finished with failure")
};

USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMFeedbackBatchedInstanceInfo
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Feedback|BatchedInfo")
    int32 InstanceIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Feedback|BatchedInfo")
    FTransform InstanceTransform;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Feedback|BatchedInfo")
    FGameplayTagContainer InstanceTags;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Feedback|BatchedInfo")
    TArray<float> PerInstanceCustomData;

    bool IsValid() const
    {
        return InstanceIndex != INDEX_NONE;
	}

    static FISMFeedbackBatchedInstanceInfo GetInstanceInfo(const UISMRuntimeComponent* ISMComp, int32 InstanceIndex, bool bWithTags, int PerInstanceDataCount);
};


/**
 * Represents a participant in a feedback event (instigator or subject).
 * Captures who/what was involved along with relevant context.
 *
 * Design:
 * - Snapshot pattern: Stores transform at feedback creation time to prevent stale data
 * - Dual reference: Actor + Component for precise attribution
 * - Tags + PhysMat: Enables material-specific feedback routing
 *
 * Examples:
 * - Player harvesting tree: Participant = Player + ToolComponent
 * - Projectile impact: Participant = Projectile + ProjectileMovementComponent
 * - ISM instance: Participant = ISMRuntimeActor + ISMRuntimeComponent (with instance index)
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMFeedbackParticipant
{
    GENERATED_BODY()

    /** Actor involved in this feedback event */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Participant")
    TWeakObjectPtr<AActor> Participant = nullptr;

    /** Specific component on the actor (for more precise attribution) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Participant")
    TWeakObjectPtr<UActorComponent> ParticipantComponent = nullptr;

    /** Transform snapshot at feedback creation time (prevents stale data) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Participant")
    FTransform ParticipantTransform;

    /** Gameplay tags associated with this participant (for filtering/routing) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Participant")
    FGameplayTagContainer ParticipantTags;

    /** Physical material of this participant (for surface-specific feedback) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Participant")
    TWeakObjectPtr<UPhysicalMaterial> ParticipantPhysicalMaterial = nullptr;

    /** Check if this participant has valid data */
    bool IsValid() const
    {
        return Participant.IsValid() || ParticipantComponent.IsValid();
    }

    /** Create participant from ISM component and optional instance index */
    static FISMFeedbackParticipant FromISMComponent(const UISMRuntimeComponent* ISMComp, int32 InstanceIndex = INDEX_NONE);

    /** Create participant from any actor component */
    static FISMFeedbackParticipant FromActorComponent(const UActorComponent* ActorComp);
};

/**
 * Context data for a feedback request.
 * Contains all information needed for audio/VFX systems to respond appropriately.
 *
 * Design Philosophy:
 * - Uses gameplay tags as the primary identification mechanism
 * - Provides common spatial/physics data (location, normal, velocity, force)
 * - Extensible via CustomParameters map for project-specific needs
 * - Participant tracking for gameplay attribution (Instigator + Subject pattern)
 * - Intensity value for scalable feedback (0-1 range, but can exceed for extreme cases)
 * - Supports both one-shot and continuous feedback lifecycles
 *
 * Participant Pattern:
 * - Instigator: Who/what caused the feedback (player, projectile, tool)
 * - Subject: Who/what received the feedback (tree, rock, wall, ground)
 * - Examples:
 *   * Player harvests tree → Instigator=Player+Tool, Subject=Tree+ISMComponent
 *   * Projectile hits wall → Instigator=Projectile, Subject=Wall
 *   * Tree falls to ground → Instigator=Tree, Subject=Ground
 *
 * Example Usage (One-Shot):
 *
 *   FISMFeedbackContext Context;
 *   Context.FeedbackTag = FGameplayTag::RequestGameplayTag("Feedback.Impact.Wood.Heavy");
 *   Context.Location = ImpactPoint;
 *   Context.Normal = ImpactNormal;
 *   Context.Intensity = FMath::Clamp(ImpactForce / MaxForce, 0.0f, 1.0f);
 *   Context.Instigator = FISMFeedbackParticipant::FromActorComponent(ProjectileComponent);
 *   Context.Subject = FISMFeedbackParticipant::FromISMComponent(TreeComponent, HitInstanceIndex);
 *
 *   if (UISMFeedbackSubsystem* FeedbackSystem = World->GetSubsystem<UISMFeedbackSubsystem>())
 *   {
 *       FeedbackSystem->RequestFeedback(Context);
 *   }
 *
 * Example Usage (Continuous):
 *
 *   // Start harvesting
 *   Context.FeedbackMessageType = EISMFeedbackMessageType::STARTED;
 *   FeedbackSystem->RequestFeedback(Context);
 *
 *   // Update if surface changes
 *   Context.FeedbackMessageType = EISMFeedbackMessageType::UPDATED;
 *   Context.Intensity = NewProgress;
 *   FeedbackSystem->RequestFeedback(Context);
 *
 *   // Finish harvesting
 *   Context.FeedbackMessageType = EISMFeedbackMessageType::COMPLETED;
 *   FeedbackSystem->RequestFeedback(Context);
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMFeedbackContext
{
    GENERATED_BODY()

    // ===== Primary Identification =====

    /**
     * Primary gameplay tag identifying the feedback type.
     *
     * Recommended hierarchy:
     * - Feedback.Impact.{Material}.{Intensity}     (e.g., Feedback.Impact.Wood.Heavy)
     * - Feedback.Destruction.{ObjectType}          (e.g., Feedback.Destruction.Tree)
     * - Feedback.Gather.{ResourceType}             (e.g., Feedback.Gather.Stone)
     * - Feedback.Ambient.{ObjectType}              (e.g., Feedback.Ambient.Foliage)
     * - Feedback.Continuous.{Action}.{Material}    (e.g., Feedback.Continuous.Harvest.Wood)
     *
     * Projects can define their own hierarchies to match their audio/VFX design.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback")
    FGameplayTag FeedbackTag;

    /**
     * Optional additional tags for context (e.g., weather conditions, biome, time of day).
     * Can be used by feedback providers to modify behavior.
     *
     * Example: { "Environment.Wet", "Biome.Forest", "TimeOfDay.Night" }
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback")
    FGameplayTagContainer ContextTags;

    /**
     * Feedback message type (one-shot vs continuous lifecycle).
     * Defaults to ONE_SHOT for simple events.
     * Set to STARTED/UPDATED/COMPLETED/INTERRUPTED for continuous feedback.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback")
    EISMFeedbackMessageType FeedbackMessageType = EISMFeedbackMessageType::ONE_SHOT;

    // ===== Spatial Data =====

    /**
     * World location where feedback should occur.
     * Required for positioned audio and particle effects.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Spatial")
    FVector Location = FVector::ZeroVector;

    /**
     * Surface normal at the feedback location.
     * Useful for oriented particle effects and surface-dependent audio.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Spatial")
    FVector Normal = FVector::UpVector;

    /**
     * Velocity of the object/effect causing feedback.
     * Can be used for doppler effects or impact intensity scaling.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Spatial")
    FVector Velocity = FVector::ZeroVector;

    /**
     * Rotation for directional feedback (e.g., destruction debris direction).
     * Optional - many feedback types don't need this.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Spatial")
    FRotator Rotation = FRotator::ZeroRotator;

    // ===== Intensity & Scale =====

    /**
     * Normalized intensity of the feedback event (0.0 - 1.0 typical range).
     * Can exceed 1.0 for extreme cases.
     *
     * Use cases:
     * - Scale audio volume
     * - Scale particle count/size
     * - Determine which audio/VFX variant to use (light/medium/heavy)
     * - For continuous feedback: Can represent progress (0.0 = start, 1.0 = complete)
     *
     * Example: Impact force normalized by max expected force
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Intensity")
    float Intensity = 1.0f;

    /**
     * Physical scale of the object/effect (1.0 = normal size).
     * Useful for scaling audio pitch or particle sizes.
     *
     * Example: Tree trunk diameter relative to average tree
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Intensity")
    float Scale = 1.0f;

    /**
     * Impact force magnitude (in Newtons or game-specific units).
     * More specific than Intensity for physics-based feedback.
     * Optional - not all feedback types involve force.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Intensity")
    float Force = 0.0f;

    // ===== Attribution (Participant Pattern) =====

    /**
     * Who/what caused this feedback event.
     *
     * Examples:
     * - Player harvesting: Instigator = Player + Tool component
     * - Projectile impact: Instigator = Projectile + ProjectileMovement component
     * - Physics object falling: Instigator = Falling object + PrimitiveComponent
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Attribution")
    FISMFeedbackParticipant Instigator;

    /**
     * Who/what received this feedback event.
     *
     * Examples:
     * - Tree being harvested: Subject = ISMRuntimeActor + ISMRuntimeComponent (with instance)
     * - Wall being hit: Subject = Wall actor + StaticMeshComponent
     * - Ground impact: Subject = Landscape actor + LandscapeComponent
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Attribution")
    FISMFeedbackParticipant Subject;

    // ===== Surface/Material Info (Legacy - prefer Participant.ParticipantPhysicalMaterial) =====

    /**
     * Physical material at the impact point.
     * Redundant with Subject.ParticipantPhysicalMaterial in most cases.
     * Kept for backward compatibility and simple use cases.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Surface")
    TWeakObjectPtr<UPhysicalMaterial> PhysicalMaterial = nullptr;

    /**
     * Static mesh being interacted with (if relevant).
     * Useful for mesh-specific audio/VFX (e.g., different tree species).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Surface")
    TWeakObjectPtr<UStaticMesh> StaticMesh = nullptr;


    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Feedback|ISMRuntime|BatchedOperations")
	TArray<int32> BatchedInstanceIndices;

    // ===== Custom Parameters =====

    /**
     * Custom float parameters for project-specific needs.
     *
     * Example use cases:
     * - "WetnessFactor" for rain-affected sounds
     * - "HealthPercentage" for material-based destruction stages
     * - "ResonanceFrequency" for material-specific audio properties
     * - "HarvestProgress" for continuous gathering feedback
     */
    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Custom")
    //TMap<FName, float> CustomFloatParameters;

    /**
     * Custom string parameters for project-specific needs.
     *
     * Example: "BiomeName", "WeatherCondition", "CustomAudioEvent"
     */
    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Custom")
    //TMap<FName, FString> CustomStringParameters;

    /**
     * Custom object references for advanced scenarios.
     * Use sparingly - prefer gameplay tags and parameters when possible.
     */
    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Custom")
    //TMap<FName, TWeakObjectPtr<UObject>> CustomObjectParameters;

    // ===== Helper Functions =====

    /** Check if this context has valid data (at minimum, a feedback tag) */
    bool IsValid() const
    {
        return FeedbackTag.IsValid();
    }

    /** Check if this context has a valid instigator */
    bool HasInstigator() const { return Instigator.IsValid(); }

    /** Check if this context has a valid subject */
    bool HasSubject() const { return Subject.IsValid(); }

    /** Check if this is a continuous feedback message (not ONE_SHOT) */
    bool IsContinuous() const { return FeedbackMessageType != EISMFeedbackMessageType::ONE_SHOT; }

	bool IsBatched() const { return BatchedInstanceIndices.Num() > 0; }



	TArray<FTransform> GetTransformsForBatchedInstances() const;
	TArray<FISMFeedbackBatchedInstanceInfo> GetBatchedInstanceInfo(bool bWithGameplayTags=true, int customDataIndexes = -1) const;

	class UISMRuntimeComponent* GetISMComponentFromSubject() const;

    /** Get a custom float parameter with a default fallback */
   // float GetFloatParameter(FName ParameterName, float DefaultValue = 0.0f) const
   // {
   //     const float* Value = CustomFloatParameters.Find(ParameterName);
   //     return Value ? *Value : DefaultValue;
   // }

    /** Set a custom float parameter */
    //void SetFloatParameter(FName ParameterName, float Value)
    //{
    //    CustomFloatParameters.Add(ParameterName, Value);
    //}

    /** Get a custom string parameter with a default fallback */
    //FString GetStringParameter(FName ParameterName, const FString& DefaultValue = "") const
    //{
    //    const FString* Value = CustomStringParameters.Find(ParameterName);
    //    return Value ? *Value : DefaultValue;
    //}

    /** Set a custom string parameter */
    //void SetStringParameter(FName ParameterName, const FString& Value)
    //{
    //    CustomStringParameters.Add(ParameterName, Value);
    //}

    /** Get a custom object parameter with type safety */
   // template<typename T>
   // T* GetObjectParameter(FName ParameterName) const
   // {
   //     if (const TWeakObjectPtr<UObject>* ObjPtr = CustomObjectParameters.Find(ParameterName))
   //     {
   //         return Cast<T>(ObjPtr->Get());
   //     }
   //     return nullptr;
   // }

    /** Set a custom object parameter */
    //void SetObjectParameter(FName ParameterName, UObject* Value)
    //{
    //    CustomObjectParameters.Add(ParameterName, Value);
    //}

    /** Add a context tag */
    void AddContextTag(FGameplayTag Tag)
    {
        ContextTags.AddTag(Tag);
    }

    /** Check if a context tag is present */
    bool HasContextTag(FGameplayTag Tag) const
    {
        return ContextTags.HasTag(Tag);
    }


	// ==== Fluent Interface for Context Modification =====

	FISMFeedbackContext WithInstigator(FISMFeedbackParticipant NewInstigator) const
    {
        FISMFeedbackContext NewContext = *this;
        NewContext.Instigator = NewInstigator;
        return NewContext;
    }

    FISMFeedbackContext WithBatchedIndexes(TArray<int32> indexes) const
    {
        FISMFeedbackContext NewContext = *this;
		NewContext.BatchedInstanceIndices = indexes;
        return NewContext;
    }

    // ===== Static Helper Constructors =====

    /**
     * Create feedback context from an ISM instance.
     * Automatically populates subject participant and spatial data.
     *
     * @param FeedbackTag - The feedback tag to use
     * @param ISMComp - ISM runtime component containing the instance
     * @param InstanceIndex - Index of the specific instance
     * @return Constructed feedback context
     */
    static FISMFeedbackContext CreateFromInstance(
        FGameplayTag FeedbackTag,
        const UISMRuntimeComponent* ISMComp,
        int32 InstanceIndex);

    static FISMFeedbackContext CreateFromInstanceBatched(
        FGameplayTag FeedbackTag,
        const UISMRuntimeComponent* ISMComp,
        TArray<int32> InstanceIndexes);

    /**
     * Create feedback context from a hit result.
     * Automatically populates spatial data and participants from the hit.
     *
     * @param FeedbackTag - The feedback tag to use
     * @param InstigatorComp - Component that caused the hit (optional)
     * @param HitResult - The hit result data
     * @return Constructed feedback context
     */
    static FISMFeedbackContext CreateFromHitResult(
        FGameplayTag FeedbackTag,
        const UActorComponent* InstigatorComp,
        const struct FHitResult& HitResult);

};