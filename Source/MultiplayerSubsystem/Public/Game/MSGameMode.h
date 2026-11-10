// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "Core/MSTypes.h"
#include "MSGameMode.generated.h"

class APlayerController;
class UMSSessionSubsystem;
class UWorld;

/**
 * Lobby/Game game mode.
 *
 * Owns join admission, MaxPlayers, kick, seamless travel helpers and the ready
 * gate. Subclass it in Blueprint per map type and set SessionPhase /
 * bAllowNewPlayers in the class defaults.
 */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API AMSGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AMSGameMode();

	// -----------------------------------------------------------------------
	// Configuration
	// -----------------------------------------------------------------------

	/** Reported to the session subsystem when the map starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Multiplayer Subsystem|Session")
	EMSSessionPhase SessionPhase = EMSSessionPhase::Lobby;

	/** Gates PreLogin and the advertised joinability. Changed at runtime by Set New Players Allowed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Multiplayer Subsystem|Session")
	bool bAllowNewPlayers = true;

	// -----------------------------------------------------------------------
	// Engine overrides
	// -----------------------------------------------------------------------

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void PostSeamlessTravel() override;
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	// -----------------------------------------------------------------------
	// Join policy
	// -----------------------------------------------------------------------

	/** Host only. Gates server admission and updates the advertised session. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	void SetNewPlayersAllowed(bool bAllowed);

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	bool AreNewPlayersAllowed() const { return bAllowNewPlayers; }

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	EMSSessionPhase GetSessionPhase() const { return SessionPhase; }

	// -----------------------------------------------------------------------
	// Travel
	// -----------------------------------------------------------------------

	/** Pure. Returns false and fills OutReason when a travel would be refused. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Travel")
	bool CanTravelNow(EMSTravelBlockReason& OutReason) const;

	/** Authority only, seamless, respects the ready requirement. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Travel")
	bool TravelToMap(TSoftObjectPtr<UWorld> Map, const TArray<FString>& Options);

	/** Same as TravelToMap, by package path. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Travel")
	bool TravelToMapPath(const FString& MapPath, const TArray<FString>& Options);

	/** Authority only, seamless, ignores the ready requirement. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Travel")
	bool ForceTravelToMap(TSoftObjectPtr<UWorld> Map, const TArray<FString>& Options);

	/** Same as ForceTravelToMap, by package path. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Travel")
	bool ForceTravelToMapPath(const FString& MapPath, const TArray<FString>& Options);

	/** Seamless travel back to LobbyMap. Never blocked by readiness. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Travel")
	bool ReturnToLobby();

	// -----------------------------------------------------------------------
	// Ready check
	// -----------------------------------------------------------------------

	/** Host only. Clears every ready flag. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Ready")
	void ResetAllReady();

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Ready")
	bool AreAllPlayersReady() const;

	/** Server-side ready validation, called by AMSPlayerController's Server RPC. */
	bool TrySetPlayerReady(APlayerController* Controller, bool bReady);

	// -----------------------------------------------------------------------
	// Kick
	// -----------------------------------------------------------------------

	/** Host only. Refuses the host, the listen-server owner and bots. A kick is not a ban. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	bool KickPlayer(APlayerController* TargetPlayer, FText Reason);

	// -----------------------------------------------------------------------
	// Blueprint events
	// -----------------------------------------------------------------------

	/** Server, just before a seamless travel starts. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Multiplayer Subsystem|Travel")
	void OnBeforeTravel(const FString& DestinationMapPath);

	/** TravelToMap was refused to start. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Multiplayer Subsystem|Travel")
	void OnTravelBlocked(EMSTravelBlockReason Reason);

	/** Per player, after the travel re-initialised them. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Multiplayer Subsystem|Travel")
	void OnPlayerSeamlessTravelled(APlayerController* Controller);

	/** When the seamless travel to the new map finished. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Multiplayer Subsystem|Travel")
	void OnPostSeamlessTravel();

private:
	UMSSessionSubsystem* GetSessionSubsystem() const;

	bool TravelToMapInternal(const FString& MapPath, const TArray<FString>& Options, bool bForce);
	void BroadcastTravelBlocked(EMSTravelBlockReason Reason);
	void ApplyHostState(AController* Controller);
};
