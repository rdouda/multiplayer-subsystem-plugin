// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/MSTypes.h"
#include "MSPlayerState.generated.h"

class UMSVoiceTalkerComponent;

/**
 * Lobby player state.
 *
 * Replicates the host marker and the ready flag, keeps a local-only speaking
 * flag for the voice UI, and owns the voice talker component that registers a
 * UVOIPTalker for this player on every machine.
 */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API AMSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AMSPlayerState();

	virtual void CopyProperties(APlayerState* PlayerState) override;

	/** True only on the listen-server host's row. Replicated, so clients see it. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Lobby")
	bool IsLobbyHost() const { return bIsLobbyHost; }

	/** Ready checkmark. Replicated. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Ready")
	bool IsReady() const { return bIsReady; }

	/** Local-only speaking state for the speaking icon. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Voice")
	bool IsSpeaking() const { return bIsSpeaking; }

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Ready")
	FMSOnReadyChanged OnReadyChanged;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Voice")
	FMSOnSpeakingChanged OnSpeakingChanged;

	/** Server only. Marks or clears the lobby-host row. */
	void SetLobbyHostAuthoritative(bool bNewHost);

	/** Server only. Authoritative ready setter; the controller never writes the flag directly. */
	void SetReadyAuthoritative(bool bNewReady);

	/** Local-only. Called by the voice talker component when this player starts/stops talking. */
	void SetSpeakingLocal(bool bNewSpeaking);

	UMSVoiceTalkerComponent* GetVoiceTalkerComponent() const { return VoiceTalker; }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnRep_UniqueId() override;

	UFUNCTION()
	void OnRep_IsLobbyHost();

	UFUNCTION()
	void OnRep_IsReady();

	UPROPERTY(ReplicatedUsing = OnRep_IsLobbyHost, BlueprintReadOnly, Category = "Multiplayer Subsystem|Lobby")
	bool bIsLobbyHost = false;

	UPROPERTY(ReplicatedUsing = OnRep_IsReady, BlueprintReadOnly, Category = "Multiplayer Subsystem|Ready")
	bool bIsReady = false;

	/** Registers the UVOIPTalker for this player on every machine. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Multiplayer Subsystem|Voice")
	TObjectPtr<UMSVoiceTalkerComponent> VoiceTalker;

private:
	void NotifyReadyChanged();

	bool bIsSpeaking = false;
};
