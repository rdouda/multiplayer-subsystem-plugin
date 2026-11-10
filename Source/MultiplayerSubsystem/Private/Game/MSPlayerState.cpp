// Copyright (c) 2026. All rights reserved.

#include "Game/MSPlayerState.h"

#include "Game/MSGameState.h"
#include "Voice/MSVoiceTalkerComponent.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

AMSPlayerState::AMSPlayerState()
{
	PrimaryActorTick.bCanEverTick = false;

	VoiceTalker = CreateDefaultSubobject<UMSVoiceTalkerComponent>(TEXT("VoiceTalker"));
}

void AMSPlayerState::BeginPlay()
{
	Super::BeginPlay();
}

void AMSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMSPlayerState, bIsLobbyHost);
	DOREPLIFETIME(AMSPlayerState, bIsReady);
}

void AMSPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (const AMSPlayerState* OldState = Cast<AMSPlayerState>(PlayerState))
	{
		// The host marker must survive seamless travel.
		bIsLobbyHost = OldState->bIsLobbyHost;

		// bIsReady is intentionally NOT copied: ready flags reset after travel.
	}
}

void AMSPlayerState::SetLobbyHostAuthoritative(bool bNewHost)
{
	if (!HasAuthority() || bIsLobbyHost == bNewHost)
	{
		return;
	}

	bIsLobbyHost = bNewHost;
	NotifyReadyChanged();
}

void AMSPlayerState::SetReadyAuthoritative(bool bNewReady)
{
	if (!HasAuthority() || bIsReady == bNewReady)
	{
		return;
	}

	bIsReady = bNewReady;

	// On the server OnRep does not run automatically.
	OnReadyChanged.Broadcast(bIsReady);
	NotifyReadyChanged();
}

void AMSPlayerState::SetSpeakingLocal(bool bNewSpeaking)
{
	if (bIsSpeaking == bNewSpeaking)
	{
		return;
	}

	bIsSpeaking = bNewSpeaking;
	OnSpeakingChanged.Broadcast(bIsSpeaking);
}

void AMSPlayerState::OnRep_IsLobbyHost()
{
	NotifyReadyChanged();
}

void AMSPlayerState::OnRep_UniqueId()
{
	Super::OnRep_UniqueId();

	if (VoiceTalker)
	{
		VoiceTalker->RefreshVoiceSetup();
	}
}

void AMSPlayerState::OnRep_IsReady()
{
	OnReadyChanged.Broadcast(bIsReady);
	NotifyReadyChanged();
}

void AMSPlayerState::NotifyReadyChanged()
{
	if (UWorld* World = GetWorld())
	{
		if (AMSGameState* GameState = World->GetGameState<AMSGameState>())
		{
			GameState->RecomputeReadySummary();
		}
	}
}
