// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MSPlayerController.generated.h"

/**
 * Lobby player controller.
 *
 * Delivers the kick reason (the engine base implementation is empty), routes the
 * ready flag through a validated Server RPC, and re-applies the local voice mode
 * after the voice handshake and on every new map.
 */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API AMSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMSPlayerController();

	/** Client callable. Sends the request to the server, which validates it. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Ready")
	void SetReady(bool bReady);

	/** Client callable. Flips the local player's ready flag. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Ready")
	void ToggleReady();

	/** Last kick reason delivered to this client. Empty when never kicked. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	FText GetLastKickReason() const { return LastKickReason; }

	virtual void ClientWasKicked_Implementation(const FText& KickReason) override;
	virtual void ClientVoiceHandshakeComplete_Implementation() override;
	virtual void ReceivedPlayer() override;

protected:
	/** Reliable Server RPC. The server re-validates everything in AMSGameMode. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetReady(bool bReady);

private:
	void RefreshVoice();

	FText LastKickReason;
};
