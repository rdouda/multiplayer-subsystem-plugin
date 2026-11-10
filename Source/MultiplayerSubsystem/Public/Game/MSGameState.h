// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "Core/MSTypes.h"
#include "MSGameState.generated.h"

class APlayerState;

/**
 * Lobby/Game game state.
 *
 * Owns the aggregate ready summary. It is recomputed on the server and on every
 * client whenever a PlayerState is added, removed or changes its ready flag.
 * The summary only broadcasts when the counts actually change.
 *
 * Derives from AGameState (not AGameStateBase) because AMSGameMode derives from
 * AGameMode: the engine requires the two bases to match, otherwise InitGameState
 * logs "Mixing AGameStateBase with AGameMode is not compatible".
 */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API AMSGameState : public AGameState
{
	GENERATED_BODY()

public:
	AMSGameState();

	/** Current aggregate ready state. Pure, works on host and clients. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Ready")
	void GetReadySummary(FMSReadySummary& OutSummary) const { OutSummary = ReadySummary; }

	/** True when there is at least one eligible player and all of them are ready. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Ready")
	bool AreAllPlayersReady() const { return ReadySummary.bAllReady; }

	/** Lobby or InGame. Replicated from the server so clients can drive phase-dependent UI and voice. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	EMSSessionPhase GetSessionPhase() const { return SessionPhase; }

	/** Server only. Called by AMSGameMode when the map starts. */
	void SetSessionPhaseAuthoritative(EMSSessionPhase NewPhase);

	/** Fires only when the summary counts actually change. */
	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Ready")
	FMSOnReadySummaryChanged OnReadySummaryChanged;

	/** Recomputes and broadcasts if the summary changed. Safe to call on any machine. */
	void RecomputeReadySummary();

	virtual void AddPlayerState(APlayerState* PlayerState) override;
	virtual void RemovePlayerState(APlayerState* PlayerState) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_SessionPhase();

	FMSReadySummary ReadySummary;

	UPROPERTY(ReplicatedUsing = OnRep_SessionPhase)
	EMSSessionPhase SessionPhase = EMSSessionPhase::None;
};
