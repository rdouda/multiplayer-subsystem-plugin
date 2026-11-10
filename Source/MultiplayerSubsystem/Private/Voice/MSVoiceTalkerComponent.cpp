// Copyright (c) 2026. All rights reserved.

#include "Voice/MSVoiceTalkerComponent.h"

#include "Core/MSLog.h"
#include "Game/MSPlayerState.h"
#include "Session/MSSessionSubsystem.h"
#include "Settings/MSVoiceSettings.h"
#include "MultiplayerSubsystem.h"
#include "Voice/MSVoiceSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/Attenuation.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Sound/SoundAttenuation.h"
#include "TimerManager.h"

// ---------------------------------------------------------------------------
// UMSVoiceTalker
// ---------------------------------------------------------------------------

void UMSVoiceTalker::OnTalkingBegin(UAudioComponent* AudioComponent)
{
	if (UMSVoiceTalkerComponent* Component = OwningComponent.Get())
	{
		Component->HandleTalkingBegin(AudioComponent);
	}

	Super::OnTalkingBegin(AudioComponent);
}

void UMSVoiceTalker::OnTalkingEnd()
{
	if (UMSVoiceTalkerComponent* Component = OwningComponent.Get())
	{
		Component->HandleTalkingEnd();
	}

	Super::OnTalkingEnd();
}

// ---------------------------------------------------------------------------
// UMSVoiceTalkerComponent
// ---------------------------------------------------------------------------

UMSVoiceTalkerComponent::UMSVoiceTalkerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMSVoiceTalkerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerState* PlayerState = GetOwningPlayerState())
	{
		PlayerState->OnPawnSet.AddDynamic(this, &UMSVoiceTalkerComponent::HandlePawnSet);
		bPawnBindingDone = true;
	}

	RefreshVoiceSetup();
}

void UMSVoiceTalkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryTimer);
	}

	if (APlayerState* PlayerState = GetOwningPlayerState())
	{
		if (bPawnBindingDone)
		{
			PlayerState->OnPawnSet.RemoveDynamic(this, &UMSVoiceTalkerComponent::HandlePawnSet);
			bPawnBindingDone = false;
		}
	}

	if (Talker)
	{
		// OnComponentDestroyed unregisters the talker from the engine map.
		Talker->DestroyComponent();
		Talker = nullptr;
	}

	CurrentAudioComponent.Reset();

	Super::EndPlay(EndPlayReason);
}

void UMSVoiceTalkerComponent::RefreshVoiceSetup()
{
	if (!Talker)
	{
		TryRegisterTalker();
	}
	else
	{
		RefreshProximity();
	}
}

void UMSVoiceTalkerComponent::TryRegisterTalker()
{
	if (Talker)
	{
		return;
	}

	APlayerState* PlayerState = GetOwningPlayerState();
	if (!PlayerState || !PlayerState->GetUniqueId().IsValid())
	{
		ScheduleRetry();
		return;
	}

	// The local player does not need a remote talker.
	if (APlayerController* PlayerController = PlayerState->GetPlayerController())
	{
		if (PlayerController->IsLocalController())
		{
			return;
		}
	}

	Talker = NewObject<UMSVoiceTalker>(PlayerState, NAME_None, RF_Transient);
	if (!Talker)
	{
		return;
	}

	Talker->SetOwningComponent(this);
	Talker->RegisterComponent();
	Talker->RegisterWithPlayerState(PlayerState);

	RefreshProximity();

	MS_LOG(Log, TEXT("Voice talker registered for %s."), *PlayerState->GetPlayerName());
}

void UMSVoiceTalkerComponent::ScheduleRetry()
{
	if (RetryCount >= 20)
	{
		return;
	}

	++RetryCount;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RetryTimer, this, &UMSVoiceTalkerComponent::TryRegisterTalker, 0.5f, false);
	}
}

void UMSVoiceTalkerComponent::RefreshProximity()
{
	if (!Talker)
	{
		return;
	}

	FVoiceSettings& Settings = Talker->Settings;
	USceneComponent* SpeakerComponent = nullptr;

	if (ShouldUseProximity())
	{
		if (const AMSPlayerState* PlayerState = GetOwningMSPlayerState())
		{
			if (APawn* Pawn = PlayerState->GetPawn())
			{
				SpeakerComponent = Pawn->GetRootComponent();
			}
		}
	}

	// No pawn (or proximity off) means the voice plays as global 2D audio.
	Settings.ComponentToAttachTo = SpeakerComponent;
	Settings.AttenuationSettings = SpeakerComponent ? GetAttenuationAsset() : nullptr;

	// A pawn may have appeared or disappeared while the player is already talking.
	ApplyVolume();
}

void UMSVoiceTalkerComponent::HandlePawnSet(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn)
{
	RefreshProximity();
}

void UMSVoiceTalkerComponent::HandleTalkingBegin(UAudioComponent* AudioComponent)
{
	CurrentAudioComponent = AudioComponent;

	if (AMSPlayerState* PlayerState = GetOwningMSPlayerState())
	{
		PlayerState->SetSpeakingLocal(true);
	}

	ApplyVolume();
}

void UMSVoiceTalkerComponent::HandleTalkingEnd()
{
	CurrentAudioComponent.Reset();

	if (AMSPlayerState* PlayerState = GetOwningMSPlayerState())
	{
		PlayerState->SetSpeakingLocal(false);
	}
}

void UMSVoiceTalkerComponent::ApplyVolumeToActiveTalker()
{
	ApplyVolume();
}

void UMSVoiceTalkerComponent::ApplyVolume()
{
	UAudioComponent* AudioComponent = CurrentAudioComponent.Get();
	if (!AudioComponent)
	{
		return;
	}

	float Volume = 0.0f;

	if (UMSVoiceSubsystem* VoiceSubsystem = GetVoiceSubsystem())
	{
		APlayerState* PlayerState = GetOwningPlayerState();
		if (VoiceSubsystem->IsVoiceAvailable() &&
			!VoiceSubsystem->IsPlayerVoiceMuted(PlayerState) &&
			VoiceSubsystem->IsPlayerAudible(PlayerState))
		{
			Volume = VoiceSubsystem->GetEffectiveOutputVolume();
		}
	}

	// Proximity is on, the speaker has no pawn and the project opted out of the
	// 2D fallback: stay silent until a pawn exists.
	if (Volume > 0.0f && ShouldUseProximity() && !GetDefault<UMSVoiceSettings>()->bFallbackTo2DWhenNoPawn)
	{
		const AMSPlayerState* PlayerState = GetOwningMSPlayerState();
		if (PlayerState && !PlayerState->GetPawn())
		{
			Volume = 0.0f;
		}
	}

	AudioComponent->SetVolumeMultiplier(Volume);
}

bool UMSVoiceTalkerComponent::ShouldUseProximity() const
{
	const UMSVoiceSettings* Settings = GetDefault<UMSVoiceSettings>();
	if (!Settings->bProximityEnabled)
	{
		return false;
	}

	if (Settings->bProximityOnlyInGame)
	{
		const UWorld* World = GetWorld();
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UMSSessionSubsystem* SessionSubsystem = GameInstance ? GameInstance->GetSubsystem<UMSSessionSubsystem>() : nullptr;
		if (SessionSubsystem && SessionSubsystem->GetSessionPhase() != EMSSessionPhase::InGame)
		{
			return false;
		}
	}

	return true;
}

USoundAttenuation* UMSVoiceTalkerComponent::GetAttenuationAsset()
{
	const UMSVoiceSettings* Settings = GetDefault<UMSVoiceSettings>();

	if (!Settings->AttenuationOverride.IsNull())
	{
		return Settings->AttenuationOverride.LoadSynchronous();
	}

	if (!RuntimeAttenuation)
	{
		RuntimeAttenuation = NewObject<USoundAttenuation>(this);

		FSoundAttenuationSettings& Attenuation = RuntimeAttenuation->Attenuation;
		Attenuation.DistanceAlgorithm = Settings->AttenuationFunction;
		Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation.AttenuationShapeExtents = FVector(FMath::Max(0.0f, Settings->AttenuationInnerRadius), 0.0f, 0.0f);
		Attenuation.FalloffDistance = FMath::Max(0.0f, Settings->AttenuationFalloffDistance);
		Attenuation.bAttenuate = true;
		Attenuation.bSpatialize = Settings->bEnableSpatialization;
	}

	return RuntimeAttenuation;
}

APlayerState* UMSVoiceTalkerComponent::GetOwningPlayerState() const
{
	return Cast<APlayerState>(GetOwner());
}

AMSPlayerState* UMSVoiceTalkerComponent::GetOwningMSPlayerState() const
{
	return Cast<AMSPlayerState>(GetOwningPlayerState());
}

UMSVoiceSubsystem* UMSVoiceTalkerComponent::GetVoiceSubsystem() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UMSVoiceSubsystem>() : nullptr;
}
