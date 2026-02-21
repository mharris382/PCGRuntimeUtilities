#include "ISMAnimationComponent.h"
#include "ISMAnimationDataAsset.h"
#include "ISMRuntimeComponent.h"
#include "ISMAnimationTransformer.h"
#include "Logging/LogMacros.h"
#include "ISMRuntimeSubsystem.h"
#include "GameFramework/Actor.h"
#include "Batching/ISMBatchScheduler.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"

UISMAnimationComponent::UISMAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UISMAnimationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!TargetISM)
	{
	  TargetISM = Cast<UInstancedStaticMeshComponent>(GetOwner()->GetComponentByClass(UInstancedStaticMeshComponent::StaticClass()));
	  if (!TargetISM)
	  {
		  UE_LOG(LogISMRuntimeAnimation, Warning, TEXT("UISMAnimationComponent on actor %s has no TargetISM set and couldn't find one on the same actor. Animation will not run."), *GetOwner()->GetName());
		  return;
	  }
	}

	if (AnimationName == NAME_None)
	{
		UE_LOG(LogISMRuntimeAnimation, Warning, TEXT("UISMAnimationComponent on '%s': AnimationName is None. This is not an error but may make debugging more difficult."), *GetOwner()->GetName());
		AnimationName = FName(*FString::Printf(TEXT("ISMAnimation.%s.%s"), *GetOwner()->GetName(), *TargetISM->GetName()));
	}

	if (!AnimationData)
	{
		UE_LOG(LogISMRuntimeAnimation, Warning,TEXT("UISMAnimationComponent on '%s': no AnimationData assigned. Animation will not run."), *GetOwner()->GetName());
		return;
	}

	UISMRuntimeSubsystem* RuntimeSubsystem = GetWorld()->GetSubsystem<UISMRuntimeSubsystem>();
	if(!RuntimeSubsystem)
	{
		UE_LOG(LogISMRuntimeAnimation, Warning, TEXT("UISMAnimationComponent on actor %s couldn't find UISMRuntimeSubsystem. Animation will not run."), *GetOwner()->GetName());
		return;
	}


	CachedScheduler = RuntimeSubsystem->GetOrCreateBatchSchduler();
	bWaitingForRuntimeComponent = true;

	RuntimeSubsystem->RequestRuntimeComponent(TargetISM, [this](UISMRuntimeComponent* Comp) {
		OnRuntimeComponentReady(Comp);
		UE_LOG(LogISMRuntimeAnimation, Log, TEXT("UISMAnimationComponent on actor %s received runtime component for ISM %s."), *GetOwner()->GetName(), *TargetISM->GetName());
		});
}

void UISMAnimationComponent::EndPlay(const EEndPlayReason::Type EndReason)
{
	// Unregister the transformer before releasing it so the scheduler doesn't
   // tick a dangling pointer.
	if (Transformer.IsValid())
	{
		if (UISMBatchScheduler* Scheduler = CachedScheduler.Get())
		{
			Scheduler->UnregisterTransformer(Transformer->GetTransformerName());
		}
		Transformer.Reset();
	}

	bWaitingForRuntimeComponent = false;
	Super::EndPlay(EndReason);
}

void UISMAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (bWaitingForRuntimeComponent || !Transformer.IsValid() || bAnimationPaused) 
		return;

	Transformer->UpdateFrameParams(BuildFrameParams(DeltaTime));
}


void UISMAnimationComponent::SetWindParams(FVector InWindDirection, float InWindStrength)
{
	if (WindDirection == InWindDirection || FMath::IsNearlyEqual(WindStrength, InWindStrength))
	{
		return; // No change, skip update
	}
	WindDirection = InWindDirection.GetSafeNormal();
	WindStrength = FMath::Max(0.0f, InWindStrength);
	
}

void UISMAnimationComponent::SetAnimationPaused(bool bPaused)
{
	bAnimationPaused = bPaused;
	// When un-pausing, mark the transformer dirty immediately so it picks up
	// on the very next scheduler tick rather than waiting for the next natural dirty cycle.
	if (!bAnimationPaused && Transformer.IsValid())
	{
		Transformer->SetDirty();
	}
}

int32 UISMAnimationComponent::GetLastAnimatedInstanceCount() const
{
	if (Transformer.IsValid())
	{
		return Transformer->GetLastAnimatedInstanceCount();
	}
	return 0;
}

int32 UISMAnimationComponent::GetLastSkippedInstanceCount() const
{
	if (Transformer.IsValid())
	{
		return Transformer->GetLastSkippedInstanceCount();
	}
	return 0;
}

void UISMAnimationComponent::OnRuntimeComponentReady(UISMRuntimeComponent* RuntimeComponent)
{
	bWaitingForRuntimeComponent = false;
	if (!RuntimeComponent)
	{
		UE_LOG(LogISMRuntimeAnimation, Warning,TEXT("UISMAnimationComponent on '%s': received null runtime component. Animation will not run."),*GetOwner()->GetName());
		return;
	}
	UE_LOG(LogISMRuntimeAnimation, Log, TEXT("UISMAnimationComponent on '%s': runtime component ready, creating transformer."), *GetOwner()->GetName());
	UISMBatchScheduler* Scheduler = CachedScheduler.Get();
	if (!Scheduler)
	{
		UE_LOG(LogISMRuntimeAnimation, Warning, TEXT("UISMAnimationComponent on '%s': scheduler no longer valid. Animation will not run."), *GetOwner()->GetName());
		return;
	}
	
	Transformer = MakeShared<FISMAnimationTransformer>(RuntimeComponent, AnimationData, AnimationName);
	if (!Scheduler->RegisterTransformer(Transformer.Get()))
	{
		UE_LOG(LogISMRuntimeAnimation, Warning,TEXT("UISMAnimationComponent on '%s': failed to register transformer (name collision?)."), *GetOwner()->GetName());
		Transformer.Reset();
	}
}

FISMAnimationFrameParams UISMAnimationComponent::BuildFrameParams(float DeltaTime) const
{
	FISMAnimationFrameParams Params;
	Params.DeltaTime = DeltaTime;
	Params.WorldTime = GetWorld()->GetTimeSeconds();
	Params.WindDirection = WindDirection;
	Params.WindStrength = WindStrength;
	Params.ReferenceLocation = GetReferenceLocation();
	return Params;
}

FVector UISMAnimationComponent::GetReferenceLocation() const
{
	if (!bUseCameraAsReferenceLocation)
	{
		return ReferenceLocationActor ? ReferenceLocationActor->GetActorLocation() : GetOwner()->GetActorLocation();
	}
	// Try to get the player camera manager - most reliable source of camera position
	if (APlayerCameraManager* CameraManager =UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0))
	{
		return CameraManager->GetCameraLocation();
	}

	// Fallback: player pawn location if no camera manager yet (early BeginPlay edge case)
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			return Pawn->GetActorLocation();
		}
	}

	// Last resort: world origin (will disable distance culling effectively)
	return FVector::ZeroVector;
}
