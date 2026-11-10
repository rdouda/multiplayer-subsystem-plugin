// Copyright (c) 2026. All rights reserved.

#include "Game/MSPlayerController.h"

#include "Core/MSLog.h"
#include "Game/MSGameMode.h"
#include "Game/MSPlayerState.h"
#include "Session/MSSessionSubsystem.h"
#include "MultiplayerSubsystem.h"
#include "Voice/MSVoiceSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

AMSPlayerController::AMSPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AMSPlayerController::SetReady(bool bReady)
{
	if (!IsLocalController())
	{
		return;
	}

	ServerSetReady(bReady);
}

void AMSPlayerController::ToggleReady()
{
	const AMSPlayerState* MSPlayerState = GetPlayerState<AMSPlayerState>();
	if (!MSPlayerState)
	{
		return;
	}

	SetReady(!MSPlayerState->IsReady());
}

bool AMSPlayerController::ServerSetReady_Validate(bool bReady)
{
	return true;
}

void AMSPlayerController::ServerSetReady_Implementation(bool bReady)
{
	if (AMSGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMSGameMode>() : nullptr)
	{
		GameMode->TrySetPlayerReady(this, bReady);
	}
}

void AMSPlayerController::ClientWasKicked_Implementation(const FText& KickReason)
{
	LastKickReason = KickReason;

	MS_LOG(Warning, TEXT("You were kicked: %s"), *KickReason.ToString());

	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMSSessionSubsystem* Subsystem = GameInstance->GetSubsystem<UMSSessionSubsystem>())
		{
			Subsystem->NotifyKicked(KickReason);
		}
	}

	Super::ClientWasKicked_Implementation(KickReason);
}

void AMSPlayerController::ClientVoiceHandshakeComplete_Implementation()
{
	Super::ClientVoiceHandshakeComplete_Implementation();

	RefreshVoice();
}

void AMSPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	RefreshVoice();
}

void AMSPlayerController::RefreshVoice()
{
	if (!IsLocalController())
	{
		return;
	}

	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMSVoiceSubsystem* VoiceSubsystem = GameInstance->GetSubsystem<UMSVoiceSubsystem>())
		{
			VoiceSubsystem->RefreshVoiceState();
		}
	}
}
