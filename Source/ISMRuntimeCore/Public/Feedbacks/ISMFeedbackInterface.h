// ISMFeedbackInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISMFeedbackContext.h"
#include "ISMFeedbackInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UISMFeedbackInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for objects that can handle ISM feedback requests.
 * 
 * Implement this on:
 * - Audio manager components (Wwise, FMOD, or custom)
 * - VFX manager components (Niagara systems)
 * - GAS components (for gameplay cues)
 * - Custom feedback handlers
 * 
 * Design Philosophy:
 * - Interface-based design allows projects to implement feedback however they want
 * - No dependencies on specific third-party systems
 * - Can have multiple providers active simultaneously (audio + VFX + haptics, etc.)
 * - Providers can selectively handle feedback based on tags or context
 * - Supports both one-shot and continuous feedback lifecycles
 * 
 * Example Implementation (Niagara VFX):
 * 
 *   bool UMyVFXManager::HandleFeedback_Implementation(const FISMFeedbackContext& Context)
 *   {
 *       // Only handle impact effects (one-shot only)
 *       if (Context.IsContinuous() || 
 *           !Context.FeedbackTag.MatchesTag(FGameplayTag::RequestGameplayTag("Feedback.Impact")))
 *       {
 *           return false;
 *       }
 *       
 *       // Look up Niagara system based on tag
 *       if (UNiagaraSystem* System = GetSystemForTag(Context.FeedbackTag))
 *       {
 *           UNiagaraFunctionLibrary::SpawnSystemAtLocation(
 *               this, System, Context.Location, Context.Normal.Rotation());
 *           return true;
 *       }
 *       
 *       return false;
 *   }
 * 
 * Example Implementation (Wwise Audio with Continuous Support):
 * 
 *   bool UMyWwiseManager::HandleFeedback_Implementation(const FISMFeedbackContext& Context)
 *   {
 *       FString EventName = ConvertTagToWwiseEvent(Context.FeedbackTag);
 *       
 *       switch (Context.FeedbackMessageType)
 *       {
 *           case EISMFeedbackMessageType::ONE_SHOT:
 *               // Simple fire & forget
 *               PostEventAtLocation(EventName, Context.Location);
 *               break;
 *               
 *           case EISMFeedbackMessageType::STARTED:
 *               // Start looping sound, store playing ID
 *               PlayingID = PostEventAtLocation(EventName + "_Loop", Context.Location);
 *               break;
 *               
 *           case EISMFeedbackMessageType::UPDATED:
 *               // Update RTPC parameters
 *               SetRTPCValue("Intensity", Context.Intensity * 100.0f, PlayingID);
 *               break;
 *               
 *           case EISMFeedbackMessageType::COMPLETED:
 *               // Stop sound cleanly
 *               StopEvent(PlayingID, FadeOutMs);
 *               break;
 *               
 *           case EISMFeedbackMessageType::INTERRUPTED:
 *               // Stop sound abruptly
 *               StopEvent(PlayingID, 0);
 *               break;
 *       }
 *       
 *       return true;
 *   }
 */
class ISMRUNTIMECORE_API IISMFeedbackInterface
{
    GENERATED_BODY()

public:
    /**
     * Handle a feedback request.
     * 
     * @param Context - Complete context data for the feedback event
     * @return True if this provider handled the feedback, false if it should be passed to other providers
     * 
     * Guidelines:
     * - Return true if you handled the feedback (blocks other providers from receiving it)
     * - Return false if you didn't handle it (allows other providers to try)
     * - Check Context.FeedbackTag to determine if you should handle this feedback
     * - Check Context.FeedbackMessageType for continuous feedback lifecycle
     * - Use Context.ContextTags for environmental modulation (wetness, biome, etc.)
     * - Implementations should be fast - avoid expensive operations on the game thread
     * 
     * Continuous Feedback Pattern:
     * - STARTED: Begin looping sounds/particles, store handles
     * - UPDATED: Update parameters (RTPC, intensity, etc.)
     * - COMPLETED: Stop cleanly with fade-out
     * - INTERRUPTED: Stop abruptly
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Feedback")
    bool HandleFeedback(const FISMFeedbackContext& Context);
    virtual bool HandleFeedback_Implementation(const FISMFeedbackContext& Context) { return false; }
    
    /**
     * Check if this provider can handle a specific feedback tag.
     * Used for early filtering before calling HandleFeedback.
     * 
     * @param FeedbackTag - The tag to check
     * @return True if this provider might handle feedback with this tag
     * 
     * Optional optimization:
     * - Return true to indicate you might handle this tag (HandleFeedback will be called)
     * - Return false to skip this provider entirely for this tag
     * - Default implementation returns true (always try to handle)
     * 
     * Example:
     *   bool UMyAudioManager::CanHandleFeedbackTag_Implementation(FGameplayTag Tag) const
     *   {
     *       // Only handle audio tags
     *       return Tag.MatchesTag(FGameplayTag::RequestGameplayTag("Feedback.Audio"));
     *   }
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Feedback")
    bool CanHandleFeedbackTag(FGameplayTag FeedbackTag) const;
    virtual bool CanHandleFeedbackTag_Implementation(FGameplayTag FeedbackTag) const { return true; }
    
    /**
     * Get the priority of this feedback provider.
     * Higher priority providers are called first.
     * 
     * @return Priority value (0 = default, higher = called first)
     * 
     * Use cases:
     * - VFX providers might have high priority to spawn effects immediately (100)
     * - Audio providers might have medium priority (50)
     * - Analytics/logging providers might have low priority (10)
     * 
     * Default implementation returns 0 (no specific priority).
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Feedback")
    int32 GetFeedbackPriority() const;
    virtual int32 GetFeedbackPriority_Implementation() const { return 0; }
    
    /**
     * Optional: Called when this provider is registered with the feedback subsystem.
     * Use for initialization, cache warmup, or diagnostics.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Feedback")
    void OnFeedbackProviderRegistered();
    virtual void OnFeedbackProviderRegistered_Implementation() {}
    
    /**
     * Optional: Called when this provider is unregistered from the feedback subsystem.
     * Use for cleanup of continuous feedback handles.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Feedback")
    void OnFeedbackProviderUnregistered();
    virtual void OnFeedbackProviderUnregistered_Implementation() {}
};