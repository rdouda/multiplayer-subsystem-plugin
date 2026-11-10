// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/TimerHandle.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Core/MSTypes.h"
#include "MSSessionSubsystem.generated.h"

class IOnlineSession;
class IOnlineIdentity;
class IOnlinePresence;
class IOnlineExternalUI;
class UWorld;
class UNetDriver;

/**
 * Host / find / join / leave, presence, friends-list joinability and the startup
 * configuration check. Lives on the GameInstance, so it survives map travel.
 *
 * All Blueprint functions are under the category "Multiplayer Subsystem".
 */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API UMSSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// -----------------------------------------------------------------------
	// Session flow (Blueprint)
	// -----------------------------------------------------------------------

	/** Validates settings, creates the session, publishes presence, then opens LobbyMap with ?listen. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	bool HostLobby(const FMSHostSettings& Settings);

	/** Searches Steam (or LAN). On Find Complete fires exactly once, including on timeout. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	bool FindLobbies(const FMSSearchSettings& Settings);

	/** Ends a running search and fires On Find Complete(Cancelled). */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	void CancelFind();

	/** Validated join. Leaves a current session first, resolves the connect string, then ClientTravel. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	bool JoinLobby(const FMSLobbyInfo& LobbyInfo);

	/** Destroys the session, clears presence, optionally travels to MainMenuMap. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	void LeaveLobby(bool bReturnToMainMenu = true);

	/** Host only. Opens or closes the lobby for new players (server admission and advertised state). */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	bool SetLobbyJoinable(bool bJoinable);

	/** Host only. Best-effort runtime visibility change. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	bool SetLobbyVisibility(EMSLobbyVisibility NewVisibility);

	/** Opens the Steam invite overlay. No-op in PIE or when not in a lobby. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	bool ShowInviteOverlay();

	/** Re-publishes presence. Normally automatic. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	void RefreshPresence();

	/** Leaves first when in a lobby, then opens MainMenuMap. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	void TravelToMainMenu();

	/** Returns true once if a network error is stored, and clears it. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	bool ConsumeLastNetworkError(FText& OutMessage);

	// -----------------------------------------------------------------------
	// Queries (Blueprint)
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	EMSFriendJoinState GetFriendJoinState() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	bool CanFriendsJoinNow() const;

	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Session")
	void GetLastSearchResults(TArray<FMSLobbyInfo>& OutResults) const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	TArray<FText> GetConfigWarnings() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	bool IsOnlineAvailable() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	bool IsSteamActive() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	FString GetActiveOnlineSubsystemName() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	bool IsInLobby() const { return bInSession; }

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	bool IsLobbyHost() const { return bInSession && bLocalHost; }

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	bool GetCurrentLobbyInfo(FMSLobbyInfo& OutInfo) const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	FString GetLocalDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	EMSSessionPhase GetSessionPhase() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	EMSSessionError GetLastError() const { return LastError; }

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	FText GetLastErrorText() const { return LastErrorText; }

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Session")
	bool IsSearchInProgress() const { return bSearchInProgress; }

	// -----------------------------------------------------------------------
	// Events (Blueprint assignable)
	// -----------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Session")
	FMSOnHostComplete OnHostComplete;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Session")
	FMSOnFindComplete OnFindComplete;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Session")
	FMSOnJoinComplete OnJoinComplete;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Session")
	FMSOnLobbyLeft OnLobbyLeft;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Session")
	FMSOnNetworkError OnNetworkError;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Session")
	FMSOnInviteAccepted OnInviteAccepted;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Session")
	FMSOnLobbyInfoChanged OnLobbyInfoChanged;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Session")
	FMSOnFriendJoinStateChanged OnFriendJoinStateChanged;

	// -----------------------------------------------------------------------
	// Internal hooks used by the plugin's game classes and debug commands
	// (not Blueprint facing)
	// -----------------------------------------------------------------------

	/** Called by AMSGameMode::InitGame. Applies the phase and join policy and re-advertises the session. */
	void NotifyMapStarted(EMSSessionPhase NewPhase, bool bAllowNewPlayers);

	/** Called by AMSGameMode when a seamless travel starts. */
	void NotifyTravelStarted();

	/** Called by AMSGameMode when a seamless travel failed or completed. */
	void NotifyTravelFinished(bool bSucceeded);

	/** Called by AMSGameMode::PostLogin / Logout. */
	void NotifyPlayerCountChanged();

	/** Called by AMSGameMode::SetNewPlayersAllowed. */
	void NotifyNewPlayersAllowed(bool bAllowed);

	/** Called by AMSPlayerController::ClientWasKicked_Implementation. */
	void NotifyKicked(const FText& Reason);

	/** Server-side admission rule. Mirrors the advertised joinability. */
	bool IsJoinAllowedNow(FText& OutReason) const;

	bool IsTravelInProgress() const { return bTravelInProgress; }
	bool IsLANLobby() const { return CurrentLobbyInfo.bIsLANMatch; }
	int32 GetCurrentPlayerCount() const { return CurrentPlayerCount; }
	int32 GetCurrentMaxPlayers() const { return CurrentMaxPlayers; }
	FString GetResolvedGameId() const { return ResolvedGameId; }
	FString GetPublishedPresenceText() const { return LastPresenceText; }
	EMSFriendJoinState GetLastFriendJoinState() const { return LastFriendJoinState; }

	/** The single rule behind the advertised joinable flag, PreLogin and Get Friend Join State. */
	bool ComputeAdvertisedJoinable() const;

	/** Debug helpers: cached results by index, same order as On Find Complete. */
	int32 GetCachedSearchResultCount() const;
	bool GetCachedSearchResult(int32 Index, FOnlineSessionSearchResult& OutResult) const;
	bool JoinSearchResultByIndex(int32 Index);

	/** Native variant of FindLobbies for console commands / C++: OnDone runs once when the search completes. */
	bool FindLobbiesThen(const FMSSearchSettings& Settings,
		TFunction<void(bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error)> OnDone);

	/**
	 * Debug / fallback: skips session discovery and ClientTravels straight to an address ("192.168.1.20" or
	 * "192.168.1.20:7777"). Useful to tell "LAN discovery is broken" apart from "the connection itself is broken".
	 */
	bool JoinByAddress(const FString& Address, bool bLAN);

	/** One line per fact: backend, lobby, flags, search, net driver. Used by MS.Status and the on-screen overlay. */
	void GetStatusLines(TArray<FString>& OutLines) const;

	/** Everything in GetStatusLines plus login state, the raw online session, resolved connect string, net driver definitions. */
	void GetDiagnosticLines(TArray<FString>& OutLines) const;

private:
	// --- Online interface access ---
	IOnlineSession* GetSessionInterface() const;
	IOnlineIdentity* GetIdentityInterface() const;
	IOnlinePresence* GetPresenceInterface() const;
	IOnlineExternalUI* GetExternalUIInterface() const;
	UWorld* GetGameWorld() const;

	// --- Delegate binding ---
	void BindDelegates();
	void UnbindDelegates();

	// --- Session internals ---
	void BuildSessionSettings(const FMSHostSettings& InSettings, FOnlineSessionSettings& OutSettings) const;
	bool RefreshAdvertisedSession();
	void PublishPresence();
	void ClearPresence();
	void UpdateFriendJoinState();

	FMSLobbyInfo MakeLobbyInfo(const FOnlineSessionSearchResult& Result) const;
	bool ValidateResultGame(const FOnlineSessionSearchResult& Result, EMSSessionError& OutError) const;
	void CompleteFind(bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error);
	void CompleteJoin(bool bSuccess, EMSSessionError Error, const FString& CustomMessage = FString());
	void ClearLocalSessionState();
	void FailHost(EMSSessionError Error);
	void SetLastError(EMSSessionError Error);
	void RecordNetworkError(EMSSessionError Error, const FText& Message);

	void BeginJoin(const FOnlineSessionSearchResult& Result, EMSJoinSource Source);
	void ContinueJoinAfterLeave();
	void ReturnToMainMenuInternal();

	/**
	 * If a leftover online session named GameSession exists (failed connect, dropped connection, ...) destroys it
	 * and runs Continuation once the destroy completes. Returns false when there is nothing to clean up, in which
	 * case the caller just continues immediately. Without this, Host / Join fail with "already exists / already in session".
	 */
	bool BeginStaleSessionCleanup(TFunction<void()> Continuation);

	/** Fire-and-forget: destroys the online session after a client-side failure and resets local state. */
	void AbandonClientSession(const TCHAR* Reason);

	bool TickOverlay(float DeltaTime);

	// --- Online subsystem callbacks ---
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleUpdateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);
	void HandleFindTimeout();
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	// --- Configuration check ---
	void RunConfigCheck();

private:
	FString ResolvedGameId;

	// Session state
	bool bInSession = false;
	bool bLocalHost = false;
	bool bAllowNewPlayers = true;
	bool bTravelInProgress = false;
	bool bHostInProgress = false;
	bool bJoinInProgress = false;
	bool bLeaveInProgress = false;
	bool bLeaveToJoin = false;
	bool bHostCompletionFired = true;
	bool bJoinCompletionFired = true;
	bool bFindCompletionFired = true;
	bool bSearchInProgress = false;
	bool bSearchCancelled = false;
	bool bAutoTravelAfterLeave = false;
	bool bKickPending = false;
	bool bPendingCommandLineInvite = false;

	EMSSessionPhase CurrentPhase = EMSSessionPhase::None;
	EMSLobbyVisibility CurrentVisibility = EMSLobbyVisibility::Public;
	EMSFriendJoinState LastFriendJoinState = EMSFriendJoinState::NotInLobby;
	EMSSessionError LastError = EMSSessionError::None;
	FText LastErrorText;

	int32 CurrentPlayerCount = 0;
	int32 CurrentMaxPlayers = 0;
	FString CurrentLobbyName;
	FMSLobbyInfo CurrentLobbyInfo;
	FString LastPresenceText;

	FText LastNetworkError;
	bool bHasNetworkError = false;

	TArray<FText> ConfigWarnings;
	TArray<FMSLobbyInfo> LastSearchResults;
	TMap<FString, FOnlineSessionSearchResult> CachedSearchResults;
	TSharedPtr<FOnlineSessionSearchResult> PendingJoinTarget;
	EMSJoinSource PendingJoinSource = EMSJoinSource::Browser;

	TSharedPtr<FOnlineSessionSearch> ActiveSearch;
	FTimerHandle FindTimeoutTimer;

	// Stale-session cleanup (see BeginStaleSessionCleanup)
	bool bDestroyingStale = false;
	TFunction<void()> PostDestroyContinuation;

	// Native find callback (see FindLobbiesThen)
	TFunction<void(bool, const TArray<FMSLobbyInfo>&, EMSSessionError)> NativeFindCallback;

	// On-screen status overlay (MS.Overlay 1), non-Shipping only
	FTSTicker::FDelegateHandle OverlayTickHandle;

	// Delegate handles
	FDelegateHandle CreateSessionCompleteHandle;
	FDelegateHandle DestroySessionCompleteHandle;
	FDelegateHandle FindSessionsCompleteHandle;
	FDelegateHandle JoinSessionCompleteHandle;
	FDelegateHandle UpdateSessionCompleteHandle;
	FDelegateHandle InviteAcceptedHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
