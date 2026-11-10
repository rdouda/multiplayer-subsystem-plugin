// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MSSessionSettings.generated.h"

class UWorld;

/**
 * Project-wide session settings.
 * Shown under Project Settings -> Plugins -> Multiplayer Subsystem: Session.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Multiplayer Subsystem: Session"))
class MULTIPLAYERSUBSYSTEM_API UMSSessionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMSSessionSettings();

	/** Map opened after leaving a lobby or after a disconnect. Required for Leave Lobby(true). */
	UPROPERTY(EditAnywhere, Config, Category = "Maps", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> MainMenuMap;

	/** Map opened by a host. Required for Host Lobby. */
	UPROPERTY(EditAnywhere, Config, Category = "Maps", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> LobbyMap;

	/** Sessions are only shown or joinable when this matches. Empty resolves to the project name. */
	UPROPERTY(EditAnywhere, Config, Category = "Identity")
	FString GameId;

	/** Sessions are only shown or joinable when this matches. Bump it when network compatibility breaks. */
	UPROPERTY(EditAnywhere, Config, Category = "Identity")
	FString GameVersion = TEXT("0.1.0");

	/** Fallback when a host passes MaxPlayers <= 0. */
	UPROPERTY(EditAnywhere, Config, Category = "Players", meta = (ClampMin = "1"))
	int32 DefaultMaxPlayers = 4;

	/** A host's MaxPlayers is clamped to this. */
	UPROPERTY(EditAnywhere, Config, Category = "Players", meta = (ClampMin = "1"))
	int32 MaxPlayersCap = 16;

	/** Results requested per search. */
	UPROPERTY(EditAnywhere, Config, Category = "Search", meta = (ClampMin = "1"))
	int32 MaxSearchResults = 200;

	/** Hard cap for a search. On Find Complete always fires by then. */
	UPROPERTY(EditAnywhere, Config, Category = "Search", meta = (ClampMin = "1.0"))
	float FindTimeoutSeconds = 15.0f;

	/** Host-side listen/driver failures travel back to MainMenuMap. */
	UPROPERTY(EditAnywhere, Config, Category = "Failures")
	bool bAutoReturnToMenuOnNetworkError = true;

	/** Master switch for Steam friends-list "Join Game". */
	UPROPERTY(EditAnywhere, Config, Category = "Friends List")
	bool bEnableFriendsListJoin = true;

	/** Publish a status such as "In Lobby (2/4)" while in a lobby. */
	UPROPERTY(EditAnywhere, Config, Category = "Friends List")
	bool bPublishPresence = true;

	virtual FName GetCategoryName() const override;

	/** GameId, or the project name when GameId is empty. Resolved in exactly one place. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Settings")
	FString GetResolvedGameId() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Settings")
	FString GetMainMenuMapPath() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Settings")
	FString GetLobbyMapPath() const;

	/** Clamps a requested player count into [1, MaxPlayersCap]. */
	int32 ClampMaxPlayers(int32 RequestedPlayers) const;
};
