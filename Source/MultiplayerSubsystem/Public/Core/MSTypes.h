// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MSTypes.generated.h"

/** Errors reported by the session subsystem. */
UENUM(BlueprintType)
enum class EMSSessionError : uint8
{
	None,
	NoOnlineSubsystem,
	MapNotConfigured,
	AlreadyInSession,
	NotInSession,
	NotHost,
	SearchFailed,
	TimedOut,
	SessionNotFound,
	SessionFull,
	SessionClosed,
	GameMismatch,
	JoinFailed,
	NoConnectAddress,
	AlreadySearching,
	Cancelled,
	HostClosed,
	TravelFailed,
	NetworkError,
	Kicked,
	FriendJoinDisabled,
	/** Another host / join / cleanup is still in progress. Try again in a moment. */
	Busy,
	Unknown
};

/** Which part of the session flow the local lobby is currently in. */
UENUM(BlueprintType)
enum class EMSSessionPhase : uint8
{
	None,
	Lobby,
	InGame
};

/** Steam lobby visibility. */
UENUM(BlueprintType)
enum class EMSLobbyVisibility : uint8
{
	/** Searchable and joinable by anyone playing the same GameId / GameVersion. */
	Public,
	/** Not searchable; Steam friends can join from the friends list. */
	FriendsOnly,
	/** Not searchable and not joinable from the friends list; invites only. */
	InviteOnly
};

/** How a join request reached the subsystem. */
UENUM(BlueprintType)
enum class EMSJoinSource : uint8
{
	Browser,
	Invite,
	FriendsList,
	CommandLine
};

/** Whether friends can join the local lobby right now, and why not. */
UENUM(BlueprintType)
enum class EMSFriendJoinState : uint8
{
	NotInLobby,
	Open,
	ClosedByHost,
	ChangingMap,
	LobbyFull,
	InviteOnly,
	DisabledBySettings,
	NotSteam
};

/** Why a travel request was refused. */
UENUM(BlueprintType)
enum class EMSTravelBlockReason : uint8
{
	None,
	NotAuthority,
	InvalidMap,
	TravelInProgress,
	NotAllReady
};

/** Local player voice input mode. */
UENUM(BlueprintType)
enum class EMSVoiceInputMode : uint8
{
	OpenMic,
	PushToTalk
};

/** Settings passed to Host Lobby. */
USTRUCT(BlueprintType)
struct FMSHostSettings
{
	GENERATED_BODY()

	/** Displayed in the server browser and in presence. Empty resolves to "<SteamName>'s Lobby". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiplayer Subsystem")
	FString LobbyName;

	/** Requested player count, clamped to MaxPlayersCap. <= 0 uses DefaultMaxPlayers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiplayer Subsystem")
	int32 MaxPlayers = 0;

	/** Host over plain IP instead of Steam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiplayer Subsystem")
	bool bIsLANMatch = false;

	/** Steam lobby visibility. Ignored for LAN. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiplayer Subsystem")
	EMSLobbyVisibility Visibility = EMSLobbyVisibility::Public;
};

/** Settings passed to Find Lobbies. */
USTRUCT(BlueprintType)
struct FMSSearchSettings
{
	GENERATED_BODY()

	/** Search LAN sessions instead of Steam lobbies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiplayer Subsystem")
	bool bIsLANMatch = false;
};

/** One lobby as shown in a server browser. */
USTRUCT(BlueprintType)
struct FMSLobbyInfo
{
	GENERATED_BODY()

	/** Backend session id. Pass this whole struct back to Join Lobby. */
	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	FString SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	FString LobbyName;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	int32 PingInMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	bool bIsLANMatch = false;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	EMSSessionPhase Phase = EMSSessionPhase::None;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	EMSLobbyVisibility Visibility = EMSLobbyVisibility::Public;

	/** True when SessionId is set. Plain C++ helper; Blueprints should test SessionId directly. */
	bool IsValid() const { return !SessionId.IsEmpty(); }
};

/** Result of a join attempt. */
USTRUCT(BlueprintType)
struct FMSJoinResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	EMSSessionError Error = EMSSessionError::None;

	/** Safe to show in UI. */
	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	FText ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	FMSLobbyInfo LobbyInfo;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	EMSJoinSource Source = EMSJoinSource::Browser;
};

/** Aggregate ready state for the lobby. */
USTRUCT(BlueprintType)
struct FMSReadySummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	int32 ReadyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	int32 TotalCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer Subsystem")
	bool bAllReady = false;
};

// ---------------------------------------------------------------------------
// Delegates (all Blueprint assignable)
// ---------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSOnHostComplete, bool, bSuccess, EMSSessionError, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMSOnFindComplete, bool, bSuccess, const TArray<FMSLobbyInfo>&, Results, EMSSessionError, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnJoinComplete, FMSJoinResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnLobbyLeft, EMSSessionError, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSOnNetworkError, EMSSessionError, Type, FText, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSOnInviteAccepted, FMSLobbyInfo, LobbyInfo, EMSJoinSource, Source);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMSOnLobbyInfoChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnFriendJoinStateChanged, EMSFriendJoinState, NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnReadyChanged, bool, bIsReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnSpeakingChanged, bool, bSpeaking);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnReadySummaryChanged, FMSReadySummary, Summary);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnVoiceInputModeChanged, EMSVoiceInputMode, Mode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSOnLocalTalkingChanged, bool, bTalking);

/** Session setting keys written into every hosted session. */
namespace MSSessionKeys
{
	extern MULTIPLAYERSUBSYSTEM_API const FName LobbyName;
	extern MULTIPLAYERSUBSYSTEM_API const FName GameId;
	extern MULTIPLAYERSUBSYSTEM_API const FName GameVersion;
	extern MULTIPLAYERSUBSYSTEM_API const FName Phase;
}

/** Display helpers for the enums above. */
namespace MSText
{
	MULTIPLAYERSUBSYSTEM_API FText GetErrorText(EMSSessionError Error);
	MULTIPLAYERSUBSYSTEM_API FText GetTravelBlockText(EMSTravelBlockReason Reason);
	MULTIPLAYERSUBSYSTEM_API FText GetFriendJoinStateText(EMSFriendJoinState State);
	MULTIPLAYERSUBSYSTEM_API const TCHAR* GetErrorName(EMSSessionError Error);
	MULTIPLAYERSUBSYSTEM_API const TCHAR* GetFriendJoinStateName(EMSFriendJoinState State);
}

/** Small string helpers shared by the session code. */
namespace MSUtil
{
	/** Strips the object name from "/Game/Maps/Lobby.Lobby" -> "/Game/Maps/Lobby". */
	MULTIPLAYERSUBSYSTEM_API FString GetPackagePath(const FString& ObjectPath);

	/** Joins a map path with URL options using '?' and drops duplicates. Options may be passed with or without a leading '?'. */
	MULTIPLAYERSUBSYSTEM_API FString BuildTravelURL(const FString& MapPath, const TArray<FString>& Options);
}
