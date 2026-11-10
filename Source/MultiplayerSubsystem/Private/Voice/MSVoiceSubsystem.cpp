// Copyright (c) 2026. All rights reserved.

#include "Voice/MSVoiceSubsystem.h"

#include "Core/MSLog.h"
#include "Game/MSPlayerState.h"
#include "Session/MSSessionSubsystem.h"
#include "Settings/MSVoiceSettings.h"
#include "MultiplayerSubsystem.h"
#include "Voice/MSVoiceTalkerComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ConfigCacheIni.h"
#include "Net/VoiceConfig.h"
#include "OnlineSubsystem.h"
#include "Interfaces/VoiceInterface.h"

namespace
{
	const TCHAR* VoicePrefsSection = TEXT("MultiplayerSubsystemVoice");
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UMSVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadPreferences();

	bInitialized = true;

	UE_LOG(LogMultiplayerSubsystem, Log, TEXT("Voice subsystem initialized. Mode=%d Threshold=%.3f Output=%.2f"),
		static_cast<int32>(CurrentInputMode), MicThreshold, OutputVolume);
}

void UMSVoiceSubsystem::Deinitialize()
{
	StopVoiceInternal();
	bInitialized = false;

	Super::Deinitialize();
}

bool UMSVoiceSubsystem::IsTickable() const
{
	return bInitialized && !IsTemplate() && GetGameInstance() != nullptr;
}

void UMSVoiceSubsystem::Tick(float DeltaTime)
{
	UpdateLocalTalking();
}

TStatId UMSVoiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMSVoiceSubsystem, STATGROUP_Tickables);
}

UWorld* UMSVoiceSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

// ---------------------------------------------------------------------------
// Online access
// ---------------------------------------------------------------------------

TSharedPtr<IOnlineVoice> UMSVoiceSubsystem::GetVoiceInterface() const
{
	if (IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get())
	{
		return OnlineSubsystem->GetVoiceInterface();
	}

	return nullptr;
}

UMSSessionSubsystem* UMSVoiceSubsystem::GetSessionSubsystem() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMSSessionSubsystem>() : nullptr;
}

bool UMSVoiceSubsystem::IsVoiceAvailable() const
{
	const UMSVoiceSettings* Settings = GetDefault<UMSVoiceSettings>();
	if (!Settings->bEnableVoice)
	{
		return false;
	}

	if (!GetVoiceInterface().IsValid())
	{
		return false;
	}

	const UMSSessionSubsystem* SessionSubsystem = GetSessionSubsystem();
	return SessionSubsystem && SessionSubsystem->IsInLobby();
}

FString UMSVoiceSubsystem::GetVoiceUnavailableReason() const
{
	if (!GetDefault<UMSVoiceSettings>()->bEnableVoice)
	{
		return TEXT("voice is disabled in Project Settings -> Multiplayer Subsystem: Voice");
	}

	if (!GetVoiceInterface().IsValid())
	{
		return TEXT("the online subsystem has no voice interface (add [OnlineSubsystem] bHasVoiceEnabled=true and [Voice] bEnabled=true to DefaultEngine.ini)");
	}

	const UMSSessionSubsystem* SessionSubsystem = GetSessionSubsystem();
	if (!SessionSubsystem || !SessionSubsystem->IsInLobby())
	{
		return TEXT("not in a lobby");
	}

	return FString();
}

// ---------------------------------------------------------------------------
// Input mode and local talker
// ---------------------------------------------------------------------------

bool UMSVoiceSubsystem::SetInputMode(EMSVoiceInputMode NewMode)
{
	if (!GetDefault<UMSVoiceSettings>()->bAllowPlayerInputModeChange)
	{
		return false;
	}

	if (CurrentInputMode == NewMode)
	{
		return true;
	}

	CurrentInputMode = NewMode;
	bPushToTalkHeld = false;

	SavePreferences();
	ApplyInputMode();
	OnInputModeChanged.Broadcast(CurrentInputMode);

	MS_LOG(Display, TEXT("Voice input mode set to %s."),
		CurrentInputMode == EMSVoiceInputMode::OpenMic ? TEXT("OpenMic") : TEXT("PushToTalk"));

	return true;
}

void UMSVoiceSubsystem::StartTalking()
{
	if (CurrentInputMode != EMSVoiceInputMode::PushToTalk)
	{
		return;
	}

	if (bPushToTalkHeld)
	{
		return;
	}

	bPushToTalkHeld = true;
	ApplyInputMode();
}

void UMSVoiceSubsystem::StopTalking()
{
	if (CurrentInputMode != EMSVoiceInputMode::PushToTalk)
	{
		return;
	}

	if (!bPushToTalkHeld)
	{
		return;
	}

	bPushToTalkHeld = false;
	ApplyInputMode();
}

void UMSVoiceSubsystem::RefreshVoiceState()
{
	if (!IsVoiceAvailable())
	{
		StopVoiceInternal();
		return;
	}

	// If everything is already in the desired state, do nothing.
	const bool bShouldTransmit =
		CurrentInputMode == EMSVoiceInputMode::OpenMic || bPushToTalkHeld;
	if (bLocalTalkerRegistered && bNetworkedVoiceActive == bShouldTransmit)
	{
		return;
	}

	UVOIPStatics::SetMicThreshold(MicThreshold);
	ApplyInputMode();
	UpdateLocalTalking();
}

void UMSVoiceSubsystem::ApplyInputMode()
{
	if (!IsVoiceAvailable())
	{
		StopVoiceInternal();
		return;
	}

	TSharedPtr<IOnlineVoice> VoiceInterface = GetVoiceInterface();
	if (!VoiceInterface.IsValid())
	{
		return;
	}

	if (!bLocalTalkerRegistered)
	{
		bLocalTalkerRegistered = VoiceInterface->RegisterLocalTalker(0);
	}

	const bool bShouldTransmit =
		CurrentInputMode == EMSVoiceInputMode::OpenMic ||
		(CurrentInputMode == EMSVoiceInputMode::PushToTalk && bPushToTalkHeld);

	if (bShouldTransmit && !bNetworkedVoiceActive)
	{
		VoiceInterface->StartNetworkedVoice(0);
		bNetworkedVoiceActive = true;
	}
	else if (!bShouldTransmit && bNetworkedVoiceActive)
	{
		VoiceInterface->StopNetworkedVoice(0);
		bNetworkedVoiceActive = false;
	}
}

void UMSVoiceSubsystem::StopVoiceInternal()
{
	if (TSharedPtr<IOnlineVoice> VoiceInterface = GetVoiceInterface())
	{
		if (bNetworkedVoiceActive)
		{
			VoiceInterface->StopNetworkedVoice(0);
			bNetworkedVoiceActive = false;
		}

		if (bLocalTalkerRegistered)
		{
			VoiceInterface->UnregisterLocalTalker(0);
			bLocalTalkerRegistered = false;
		}
	}

	bPushToTalkHeld = false;

	if (bLocalTalking)
	{
		bLocalTalking = false;
		OnLocalTalkingChanged.Broadcast(false);
	}
}

void UMSVoiceSubsystem::UpdateLocalTalking()
{
	bool bNewTalking = false;

	if (bInitialized && IsVoiceAvailable())
	{
		if (TSharedPtr<IOnlineVoice> VoiceInterface = GetVoiceInterface())
		{
			bNewTalking = VoiceInterface->IsLocalPlayerTalking(0);
		}
	}

	if (bNewTalking != bLocalTalking)
	{
		bLocalTalking = bNewTalking;
		OnLocalTalkingChanged.Broadcast(bLocalTalking);
	}
}

// ---------------------------------------------------------------------------
// Threshold and output volume
// ---------------------------------------------------------------------------

void UMSVoiceSubsystem::SetMicThreshold(float NewThreshold)
{
	const float Clamped = FMath::Clamp(NewThreshold, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(Clamped, MicThreshold))
	{
		return;
	}

	MicThreshold = Clamped;
	UVOIPStatics::SetMicThreshold(MicThreshold);
	SavePreferences();
}

void UMSVoiceSubsystem::SetOutputVolume(float NewVolume)
{
	const float Clamped = FMath::Clamp(NewVolume, 0.0f, 4.0f);
	if (FMath::IsNearlyEqual(Clamped, OutputVolume))
	{
		return;
	}

	OutputVolume = Clamped;
	SavePreferences();
	RefreshAllTalkerVolumes();
}

// ---------------------------------------------------------------------------
// Local mute and audibility
// ---------------------------------------------------------------------------

FString UMSVoiceSubsystem::GetPlayerKey(const APlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return FString();
	}

	const FUniqueNetIdRepl& UniqueId = PlayerState->GetUniqueId();
	if (UniqueId.IsValid())
	{
		return UniqueId.ToString();
	}

	return PlayerState->GetPlayerName();
}

void UMSVoiceSubsystem::SetPlayerVoiceMuted(APlayerState* PlayerState, bool bMuted)
{
	const FString Key = GetPlayerKey(PlayerState);
	if (Key.IsEmpty())
	{
		return;
	}

	if (bMuted)
	{
		MutedPlayerKeys.Add(Key);
	}
	else
	{
		MutedPlayerKeys.Remove(Key);
	}

	if (TSharedPtr<IOnlineVoice> VoiceInterface = GetVoiceInterface())
	{
		const FUniqueNetIdRepl& UniqueId = PlayerState->GetUniqueId();
		if (UniqueId.IsValid())
		{
			if (bMuted)
			{
				VoiceInterface->MuteRemoteTalker(0, *UniqueId, false);
			}
			else
			{
				VoiceInterface->UnmuteRemoteTalker(0, *UniqueId, false);
			}
		}
	}

	RefreshAllTalkerVolumes();
}

bool UMSVoiceSubsystem::IsPlayerVoiceMuted(APlayerState* PlayerState) const
{
	const FString Key = GetPlayerKey(PlayerState);
	return !Key.IsEmpty() && MutedPlayerKeys.Contains(Key);
}

bool UMSVoiceSubsystem::IsPlayerAudible(const APlayerState* PlayerState) const
{
	const FString Key = GetPlayerKey(PlayerState);
	if (Key.IsEmpty())
	{
		return true;
	}

	const bool* Override = PlayerAudibleOverrides.Find(Key);
	return !Override || *Override;
}

void UMSVoiceSubsystem::SetPlayerVoiceAudible(APlayerState* PlayerState, bool bAudible)
{
	const FString Key = GetPlayerKey(PlayerState);
	if (Key.IsEmpty())
	{
		return;
	}

	if (bAudible)
	{
		PlayerAudibleOverrides.Remove(Key);
	}
	else
	{
		PlayerAudibleOverrides.Add(Key, false);
	}

	RefreshAllTalkerVolumes();
}

void UMSVoiceSubsystem::RefreshAllTalkerVolumes()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	AGameStateBase* GameState = World->GetGameState();
	if (!GameState)
	{
		return;
	}

	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (AMSPlayerState* MSPlayerState = Cast<AMSPlayerState>(PlayerState))
		{
			if (UMSVoiceTalkerComponent* TalkerComponent = MSPlayerState->GetVoiceTalkerComponent())
			{
				TalkerComponent->ApplyVolumeToActiveTalker();
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

void UMSVoiceSubsystem::LoadPreferences()
{
	const UMSVoiceSettings* Settings = GetDefault<UMSVoiceSettings>();

	CurrentInputMode = Settings->DefaultInputMode;
	MicThreshold = FMath::Clamp(Settings->DefaultMicThreshold, 0.0f, 1.0f);
	OutputVolume = FMath::Clamp(Settings->OutputVolumeMultiplier, 0.0f, 4.0f);

	int32 InputModeInt = static_cast<int32>(CurrentInputMode);
	if (GConfig->GetInt(VoicePrefsSection, TEXT("InputMode"), InputModeInt, GGameUserSettingsIni))
	{
		CurrentInputMode = static_cast<EMSVoiceInputMode>(FMath::Clamp(InputModeInt, 0, 1));
	}

	GConfig->GetFloat(VoicePrefsSection, TEXT("MicThreshold"), MicThreshold, GGameUserSettingsIni);
	MicThreshold = FMath::Clamp(MicThreshold, 0.0f, 1.0f);

	GConfig->GetFloat(VoicePrefsSection, TEXT("OutputVolume"), OutputVolume, GGameUserSettingsIni);
	OutputVolume = FMath::Clamp(OutputVolume, 0.0f, 4.0f);

	UVOIPStatics::SetMicThreshold(MicThreshold);
}

void UMSVoiceSubsystem::SavePreferences()
{
	GConfig->SetInt(VoicePrefsSection, TEXT("InputMode"), static_cast<int32>(CurrentInputMode), GGameUserSettingsIni);
	GConfig->SetFloat(VoicePrefsSection, TEXT("MicThreshold"), MicThreshold, GGameUserSettingsIni);
	GConfig->SetFloat(VoicePrefsSection, TEXT("OutputVolume"), OutputVolume, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}
