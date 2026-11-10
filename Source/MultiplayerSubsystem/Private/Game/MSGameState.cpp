// Copyright (c) 2026. All rights reserved.

#include "Game/MSGameState.h"

#include "Game/MSPlayerState.h"
#include "Settings/MSReadyCheckSettings.h"

#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

AMSGameState::AMSGameState()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMSGameState, SessionPhase);
}

void AMSGameState::SetSessionPhaseAuthoritative(EMSSessionPhase NewPhase)
{
	if (!HasAuthority() || SessionPhase == NewPhase)
	{
		return;
	}

	SessionPhase = NewPhase;

	// On the server OnRep does not run automatically.
	OnRep_SessionPhase();
}

void AMSGameState::OnRep_SessionPhase()
{
	// Nothing to do yet: consumers read GetSessionPhase() directly. Kept as a
	// replication hook so UI can bind to a delegate later without an API change.
}

void AMSGameState::AddPlayerState(APlayerState* PlayerState)
{
	Super::AddPlayerState(PlayerState);
	RecomputeReadySummary();
}

void AMSGameState::RemovePlayerState(APlayerState* PlayerState)
{
	Super::RemovePlayerState(PlayerState);
	RecomputeReadySummary();
}

void AMSGameState::RecomputeReadySummary()
{
	const UMSReadyCheckSettings* Settings = GetDefault<UMSReadyCheckSettings>();

	FMSReadySummary NewSummary;
	for (APlayerState* BasePlayerState : PlayerArray)
	{
		const AMSPlayerState* PlayerState = Cast<AMSPlayerState>(BasePlayerState);
		if (!PlayerState)
		{
			continue;
		}

		// Bots, spectators and inactive players are not part of the ready check.
		if (PlayerState->IsABot() || PlayerState->IsOnlyASpectator() || PlayerState->IsInactive())
		{
			continue;
		}

		++NewSummary.TotalCount;

		const bool bHostCountsAsReady = Settings->bHostIsAlwaysReady && PlayerState->IsLobbyHost();
		if (PlayerState->IsReady() || bHostCountsAsReady)
		{
			++NewSummary.ReadyCount;
		}
	}

	NewSummary.bAllReady = NewSummary.TotalCount > 0 && NewSummary.ReadyCount >= NewSummary.TotalCount;

	if (NewSummary.ReadyCount != ReadySummary.ReadyCount ||
		NewSummary.TotalCount != ReadySummary.TotalCount ||
		NewSummary.bAllReady != ReadySummary.bAllReady)
	{
		ReadySummary = NewSummary;
		OnReadySummaryChanged.Broadcast(ReadySummary);
	}
}
