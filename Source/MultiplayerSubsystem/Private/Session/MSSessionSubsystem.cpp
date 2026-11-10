// Copyright (c) 2026. All rights reserved.

#include "Session/MSSessionSubsystem.h"

#include "Core/MSLog.h"
#include "Core/MSTypes.h"
#include "Settings/MSSessionSettings.h"
#include "Game/MSGameMode.h"
#include "Game/MSGameState.h"
#include "MultiplayerSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/EngineBaseTypes.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

#include "OnlineSubsystem.h"
#include "OnlineSubsystemTypes.h"
#include "Online/OnlineSessionNames.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Interfaces/OnlineExternalUIInterface.h"

#define LOCTEXT_NAMESPACE "MultiplayerSubsystem"

namespace
{
	const TCHAR* RejectClosed = TEXT("The lobby is closed.");
	const TCHAR* RejectFull = TEXT("The lobby is full.");
	const TCHAR* RejectChangingMap = TEXT("The lobby is changing maps.");

	FString ResolveLobbyName(const FString& Requested, const FString& DisplayName)
	{
		if (!Requested.IsEmpty())
		{
			return Requested;
		}

		return FString::Printf(TEXT("%s's Lobby"), *DisplayName);
	}

	const TCHAR* VisibilityName(EMSLobbyVisibility Visibility)
	{
		switch (Visibility)
		{
		case EMSLobbyVisibility::Public:     return TEXT("Public");
		case EMSLobbyVisibility::FriendsOnly: return TEXT("FriendsOnly");
		case EMSLobbyVisibility::InviteOnly: return TEXT("InviteOnly");
		default:                             return TEXT("Unknown");
		}
	}

	const TCHAR* PhaseName(EMSSessionPhase Phase)
	{
		switch (Phase)
		{
		case EMSSessionPhase::None:   return TEXT("None");
		case EMSSessionPhase::Lobby:  return TEXT("Lobby");
		case EMSSessionPhase::InGame: return TEXT("InGame");
		default:                      return TEXT("Unknown");
		}
	}

	const TCHAR* NetModeName(ENetMode Mode)
	{
		switch (Mode)
		{
		case NM_Standalone:      return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer:    return TEXT("ListenServer");
		case NM_Client:          return TEXT("Client");
		default:                 return TEXT("Unknown");
		}
	}

	const TCHAR* NetworkFailureName(ENetworkFailure::Type Type)
	{
		switch (Type)
		{
		case ENetworkFailure::NetDriverListenFailure:   return TEXT("NetDriverListenFailure");
		case ENetworkFailure::NetDriverCreateFailure:   return TEXT("NetDriverCreateFailure");
		case ENetworkFailure::ConnectionLost:           return TEXT("ConnectionLost");
		case ENetworkFailure::ConnectionTimeout:        return TEXT("ConnectionTimeout");
		case ENetworkFailure::FailureReceived:          return TEXT("FailureReceived");
		case ENetworkFailure::PendingConnectionFailure: return TEXT("PendingConnectionFailure");
		default:                                        return TEXT("Other");
		}
	}

	const TCHAR* JoinResultName(EOnJoinSessionCompleteResult::Type Result)
	{
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::Success:                 return TEXT("Success");
		case EOnJoinSessionCompleteResult::SessionIsFull:           return TEXT("SessionIsFull");
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:     return TEXT("SessionDoesNotExist");
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress: return TEXT("CouldNotRetrieveAddress");
		case EOnJoinSessionCompleteResult::AlreadyInSession:        return TEXT("AlreadyInSession");
		default:                                                    return TEXT("UnknownError");
		}
	}

	const TCHAR* SessionStateName(EOnlineSessionState::Type State)
	{
		switch (State)
		{
		case EOnlineSessionState::NoSession:  return TEXT("NoSession");
		case EOnlineSessionState::Creating:   return TEXT("Creating");
		case EOnlineSessionState::Pending:    return TEXT("Pending");
		case EOnlineSessionState::Starting:   return TEXT("Starting");
		case EOnlineSessionState::InProgress: return TEXT("InProgress");
		case EOnlineSessionState::Ending:     return TEXT("Ending");
		case EOnlineSessionState::Ended:      return TEXT("Ended");
		case EOnlineSessionState::Destroying: return TEXT("Destroying");
		default:                              return TEXT("Unknown");
		}
	}

	const TCHAR* LoginStatusName(ELoginStatus::Type Status)
	{
		switch (Status)
		{
		case ELoginStatus::NotLoggedIn:       return TEXT("NotLoggedIn");
		case ELoginStatus::UsingLocalProfile: return TEXT("UsingLocalProfile");
		case ELoginStatus::LoggedIn:          return TEXT("LoggedIn");
		default:                              return TEXT("Unknown");
		}
	}

	/** One-line description of a raw search result, for the log. */
	FString DescribeSearchResult(const FOnlineSessionSearchResult& Result)
	{
		FString FoundGameId(TEXT("<none>"));
		FString FoundVersion(TEXT("<none>"));
		Result.Session.SessionSettings.Get(MSSessionKeys::GameId, FoundGameId);
		Result.Session.SessionSettings.Get(MSSessionKeys::GameVersion, FoundVersion);

		const FString Info = Result.Session.SessionInfo.IsValid() ? Result.Session.SessionInfo->ToString() : FString(TEXT("<null>"));

		return FString::Printf(TEXT("owner='%s' id=%s ping=%dms LAN=%d open=%d/%d gameId='%s' version='%s' info=[%s]"),
			*Result.Session.OwningUserName,
			*Result.GetSessionIdStr(),
			Result.PingInMs,
			Result.Session.SessionSettings.bIsLANMatch ? 1 : 0,
			Result.Session.NumOpenPublicConnections,
			Result.Session.SessionSettings.NumPublicConnections,
			*FoundGameId,
			*FoundVersion,
			*Info);
	}
}

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarMSOverlay(
	TEXT("MS.Overlay"),
	0,
	TEXT("1 = show a live MultiplayerSubsystem status panel on the game screen (session, lobby, search, net driver)."),
	ECVF_Default);
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UMSSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ResolvedGameId = GetDefault<UMSSessionSettings>()->GetResolvedGameId();

	const FString CommandLine = FCommandLine::Get();
	bPendingCommandLineInvite = CommandLine.Contains(TEXT("+connect_lobby"), ESearchCase::IgnoreCase);

	BindDelegates();
	RunConfigCheck();

#if !UE_BUILD_SHIPPING
	OverlayTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMSSessionSubsystem::TickOverlay), 0.25f);
#endif

	FString LoginText = TEXT("n/a");
	if (IOnlineIdentity* Identity = GetIdentityInterface())
	{
		LoginText = FString::Printf(TEXT("%s as '%s'"), LoginStatusName(Identity->GetLoginStatus(0)), *GetLocalDisplayName());
	}

	MS_LOG(Display, TEXT("MultiplayerSubsystem ready. OSS=%s (Steam=%s, %s) GameId=%s Version=%s. Type MS.Help in the console."),
		*GetActiveOnlineSubsystemName(), IsSteamActive() ? TEXT("yes") : TEXT("no"), *LoginText,
		*ResolvedGameId, *GetDefault<UMSSessionSettings>()->GameVersion);
}

void UMSSessionSubsystem::Deinitialize()
{
#if !UE_BUILD_SHIPPING
	FTSTicker::GetCoreTicker().RemoveTicker(OverlayTickHandle);
	OverlayTickHandle.Reset();
#endif

	if (UWorld* World = GetGameWorld())
	{
		World->GetTimerManager().ClearTimer(FindTimeoutTimer);
	}

	UnbindDelegates();

	Super::Deinitialize();
}

void UMSSessionSubsystem::BindDelegates()
{
	if (IOnlineSession* SessionInt = GetSessionInterface())
	{
		CreateSessionCompleteHandle = SessionInt->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleCreateSessionComplete));

		DestroySessionCompleteHandle = SessionInt->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleDestroySessionComplete));

		FindSessionsCompleteHandle = SessionInt->AddOnFindSessionsCompleteDelegate_Handle(
			FOnFindSessionsCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleFindSessionsComplete));

		JoinSessionCompleteHandle = SessionInt->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleJoinSessionComplete));

		UpdateSessionCompleteHandle = SessionInt->AddOnUpdateSessionCompleteDelegate_Handle(
			FOnUpdateSessionCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleUpdateSessionComplete));

		InviteAcceptedHandle = SessionInt->AddOnSessionUserInviteAcceptedDelegate_Handle(
			FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleSessionUserInviteAccepted));
	}

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UMSSessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UMSSessionSubsystem::HandleTravelFailure);
	}
}

void UMSSessionSubsystem::UnbindDelegates()
{
	if (IOnlineSession* SessionInt = GetSessionInterface())
	{
		SessionInt->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		SessionInt->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
		SessionInt->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		SessionInt->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
		SessionInt->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateSessionCompleteHandle);
		SessionInt->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedHandle);
	}

	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();
}

// ---------------------------------------------------------------------------
// Online interface access
// ---------------------------------------------------------------------------

static IOnlineSubsystem* GetActiveOSS()
{
	return IOnlineSubsystem::Get();
}

IOnlineSession* UMSSessionSubsystem::GetSessionInterface() const
{
	IOnlineSubsystem* OSS = GetActiveOSS();
	return OSS ? OSS->GetSessionInterface().Get() : nullptr;
}

IOnlineIdentity* UMSSessionSubsystem::GetIdentityInterface() const
{
	IOnlineSubsystem* OSS = GetActiveOSS();
	return OSS ? OSS->GetIdentityInterface().Get() : nullptr;
}

IOnlinePresence* UMSSessionSubsystem::GetPresenceInterface() const
{
	IOnlineSubsystem* OSS = GetActiveOSS();
	return OSS ? OSS->GetPresenceInterface().Get() : nullptr;
}

IOnlineExternalUI* UMSSessionSubsystem::GetExternalUIInterface() const
{
	IOnlineSubsystem* OSS = GetActiveOSS();
	return OSS ? OSS->GetExternalUIInterface().Get() : nullptr;
}

UWorld* UMSSessionSubsystem::GetGameWorld() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

// ---------------------------------------------------------------------------
// Host
// ---------------------------------------------------------------------------

bool UMSSessionSubsystem::HostLobby(const FMSHostSettings& InSettings)
{
	IOnlineSession* SessionInt = GetSessionInterface();
	if (!SessionInt || !GetIdentityInterface())
	{
		MS_LOG(Error, TEXT("Host: no usable online subsystem (active OSS: %s). Run MS.CheckConfig."), *GetActiveOnlineSubsystemName());
		FailHost(EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	if (bDestroyingStale)
	{
		MS_LOG(Warning, TEXT("Host: still cleaning up a previous session. Try again in a moment."));
		FailHost(EMSSessionError::Busy);
		return false;
	}

	if (bInSession || bHostInProgress)
	{
		MS_LOG(Warning, TEXT("Host: refused, %s."),
			bInSession ? TEXT("you are already in a lobby (MS.Leave first)") : TEXT("a host request is already running"));
		FailHost(EMSSessionError::AlreadyInSession);
		return false;
	}

	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	const FString LobbyMapPath = Settings->GetLobbyMapPath();
	if (LobbyMapPath.IsEmpty() || !FPackageName::DoesPackageExist(LobbyMapPath))
	{
		MS_LOG(Error, TEXT("Host: LobbyMap is %s (resolved path '%s'). Set it in Project Settings -> Plugins -> Multiplayer Subsystem: Session and make sure the map is packaged."),
			LobbyMapPath.IsEmpty() ? TEXT("not set") : TEXT("not found"), *LobbyMapPath);
		FailHost(EMSSessionError::MapNotConfigured);
		return false;
	}

	// A leftover online session (failed join, dropped connection) makes CreateSession fail. Clear it first.
	if (BeginStaleSessionCleanup([this, InSettings]() { HostLobby(InSettings); }))
	{
		return true;
	}

	if (!InSettings.bIsLANMatch && !IsSteamActive())
	{
		MS_LOG(Warning, TEXT("Host: this is an internet (non-LAN) lobby but the active online subsystem is '%s', not Steam. Nobody will find it. Is Steam running?"),
			*GetActiveOnlineSubsystemName());
	}

	FOnlineSessionSettings NewSettings;
	BuildSessionSettings(InSettings, NewSettings);

	CurrentVisibility = InSettings.bIsLANMatch ? EMSLobbyVisibility::Public : InSettings.Visibility;
	CurrentMaxPlayers = NewSettings.NumPublicConnections;
	CurrentPlayerCount = 1;
	CurrentLobbyName = ResolveLobbyName(InSettings.LobbyName, GetLocalDisplayName());

	CurrentLobbyInfo = FMSLobbyInfo();
	CurrentLobbyInfo.LobbyName = CurrentLobbyName;
	CurrentLobbyInfo.HostName = GetLocalDisplayName();
	CurrentLobbyInfo.CurrentPlayers = 1;
	CurrentLobbyInfo.MaxPlayers = CurrentMaxPlayers;
	CurrentLobbyInfo.bIsLANMatch = InSettings.bIsLANMatch;
	CurrentLobbyInfo.Phase = EMSSessionPhase::Lobby;
	CurrentLobbyInfo.Visibility = CurrentVisibility;

	bHostInProgress = true;
	bHostCompletionFired = false;
	SetLastError(EMSSessionError::None);

	MS_LOG(Display, TEXT("Hosting '%s': %d players max, %s, visibility %s (OSS: %s)"),
		*CurrentLobbyName, CurrentMaxPlayers, InSettings.bIsLANMatch ? TEXT("LAN") : TEXT("internet"),
		VisibilityName(CurrentVisibility), *GetActiveOnlineSubsystemName());

	if (!SessionInt->CreateSession(0, NAME_GameSession, NewSettings))
	{
		MS_LOG(Error, TEXT("Host: CreateSession returned false."));
		if (!bHostCompletionFired)
		{
			FailHost(EMSSessionError::Unknown);
		}
		return false;
	}

	return true;
}

void UMSSessionSubsystem::BuildSessionSettings(const FMSHostSettings& InSettings, FOnlineSessionSettings& OutSettings) const
{
	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	const int32 MaxPlayers = Settings->ClampMaxPlayers(InSettings.MaxPlayers);

	OutSettings = FOnlineSessionSettings();
	OutSettings.NumPublicConnections = MaxPlayers;
	OutSettings.NumPrivateConnections = 0;
	OutSettings.bIsLANMatch = InSettings.bIsLANMatch;
	OutSettings.bIsDedicated = false;
	OutSettings.bUsesStats = false;
	OutSettings.bAntiCheatProtected = false;
	OutSettings.bAllowInvites = true;
	OutSettings.bShouldAdvertise = true;
	OutSettings.bAllowJoinInProgress = true;

	if (InSettings.bIsLANMatch)
	{
		OutSettings.bUsesPresence = false;
		OutSettings.bUseLobbiesIfAvailable = false;
		OutSettings.bAllowJoinViaPresence = false;
		OutSettings.bAllowJoinViaPresenceFriendsOnly = false;
	}
	else
	{
		OutSettings.bUsesPresence = true;
		OutSettings.bUseLobbiesIfAvailable = true;
		OutSettings.bAllowJoinViaPresence = InSettings.Visibility != EMSLobbyVisibility::InviteOnly;
		OutSettings.bAllowJoinViaPresenceFriendsOnly = InSettings.Visibility == EMSLobbyVisibility::FriendsOnly;
	}

	OutSettings.Set(MSSessionKeys::LobbyName, ResolveLobbyName(InSettings.LobbyName, GetLocalDisplayName()),
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	OutSettings.Set(MSSessionKeys::GameId, ResolvedGameId, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	OutSettings.Set(MSSessionKeys::GameVersion, Settings->GameVersion, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	OutSettings.Set(MSSessionKeys::Phase, FString(TEXT("Lobby")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
}

void UMSSessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession)
	{
		return;
	}

	bHostCompletionFired = true;

	if (!bHostInProgress)
	{
		return;
	}
	bHostInProgress = false;

	if (!bWasSuccessful)
	{
		MS_LOG(Error, TEXT("Host: the online subsystem failed to create the session."));
		FailHost(EMSSessionError::Unknown);
		return;
	}

	bInSession = true;
	bLocalHost = true;
	bAllowNewPlayers = true;
	bTravelInProgress = false;
	CurrentPhase = EMSSessionPhase::Lobby;
	CurrentPlayerCount = 1;

	CurrentLobbyInfo.HostName = GetLocalDisplayName();
	CurrentLobbyInfo.CurrentPlayers = 1;
	CurrentLobbyInfo.MaxPlayers = CurrentMaxPlayers;
	CurrentLobbyInfo.Phase = CurrentPhase;
	CurrentLobbyInfo.Visibility = CurrentVisibility;
	CurrentLobbyInfo.LobbyName = CurrentLobbyName;

	RefreshAdvertisedSession();
	PublishPresence();
	UpdateFriendJoinState();
	OnLobbyInfoChanged.Broadcast();

	if (CurrentLobbyInfo.bIsLANMatch)
	{
		FString AdvertisedAddress;
		IOnlineSession* SessionInt = GetSessionInterface();
		if (SessionInt && SessionInt->GetResolvedConnectString(NAME_GameSession, AdvertisedAddress))
		{
			MS_LOG(Display, TEXT("LAN lobby is being advertised. Joiners will be told to connect to: %s"), *AdvertisedAddress);
		}
		else
		{
			MS_LOG(Warning, TEXT("LAN lobby created but the online subsystem has no connect address for it yet."));
		}
	}

	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	const FString LobbyMapPath = Settings->GetLobbyMapPath();
	if (LobbyMapPath.IsEmpty())
	{
		FailHost(EMSSessionError::MapNotConfigured);
		return;
	}

	TArray<FString> Options;
	Options.Add(TEXT("listen"));
	if (CurrentLobbyInfo.bIsLANMatch)
	{
		Options.Add(TEXT("bIsLanMatch=1"));
	}

	const FString OptionString = FString::Join(Options, TEXT("?"));
	MS_LOG(Display, TEXT("Session created. Opening %s?%s"), *LobbyMapPath, *OptionString);

	UGameplayStatics::OpenLevel(GetGameInstance(), FName(*LobbyMapPath), true, OptionString);

	OnHostComplete.Broadcast(true, EMSSessionError::None);
}

void UMSSessionSubsystem::FailHost(EMSSessionError Error)
{
	bHostInProgress = false;
	SetLastError(Error);
	MS_LOG(Warning, TEXT("Host Lobby failed: %s - %s"), MSText::GetErrorName(Error), *MSText::GetErrorText(Error).ToString());
	OnHostComplete.Broadcast(false, Error);
}

// ---------------------------------------------------------------------------
// Find
// ---------------------------------------------------------------------------

bool UMSSessionSubsystem::FindLobbies(const FMSSearchSettings& InSettings)
{
	IOnlineSession* SessionInt = GetSessionInterface();
	if (!SessionInt)
	{
		MS_LOG(Error, TEXT("Find: no online subsystem (active OSS: %s). Run MS.CheckConfig."), *GetActiveOnlineSubsystemName());
		SetLastError(EMSSessionError::NoOnlineSubsystem);
		OnFindComplete.Broadcast(false, TArray<FMSLobbyInfo>(), EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	if (bSearchInProgress)
	{
		// Deliberately not broadcast: that would wrongly finish the search that is already running.
		MS_LOG(Warning, TEXT("Find: a search is already running."));
		SetLastError(EMSSessionError::AlreadySearching);
		return false;
	}

	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();

	ActiveSearch = MakeShared<FOnlineSessionSearch>();
	ActiveSearch->MaxSearchResults = FMath::Max(1, Settings->MaxSearchResults);
	ActiveSearch->bIsLanQuery = InSettings.bIsLANMatch;

	if (!InSettings.bIsLANMatch)
	{
		// Without this the Steam OSS searches the dedicated-server list instead of lobbies and never finds a listen-server lobby.
		ActiveSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
		ActiveSearch->QuerySettings.Set(MSSessionKeys::GameId, ResolvedGameId, EOnlineComparisonOp::Equals);
		ActiveSearch->QuerySettings.Set(MSSessionKeys::GameVersion, Settings->GameVersion, EOnlineComparisonOp::Equals);
	}

	bSearchInProgress = true;
	bSearchCancelled = false;
	bFindCompletionFired = false;
	CachedSearchResults.Reset();
	SetLastError(EMSSessionError::None);

	MS_LOG(Display, TEXT("Searching for %s lobbies (GameId=%s Version=%s OSS=%s, timeout %.0fs)..."),
		InSettings.bIsLANMatch ? TEXT("LAN") : TEXT("internet"), *ResolvedGameId, *Settings->GameVersion,
		*GetActiveOnlineSubsystemName(), Settings->FindTimeoutSeconds);

	if (!SessionInt->FindSessions(0, ActiveSearch.ToSharedRef()))
	{
		MS_LOG(Error, TEXT("Find: FindSessions returned false."));
		if (!bFindCompletionFired)
		{
			CompleteFind(false, TArray<FMSLobbyInfo>(), EMSSessionError::SearchFailed);
		}
		return false;
	}

	if (UWorld* World = GetGameWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(FindTimeoutTimer);
		TimerManager.SetTimer(FindTimeoutTimer, this, &UMSSessionSubsystem::HandleFindTimeout,
			FMath::Max(1.0f, Settings->FindTimeoutSeconds), false);
	}

	return true;
}

bool UMSSessionSubsystem::FindLobbiesThen(const FMSSearchSettings& Settings,
	TFunction<void(bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error)> OnDone)
{
	if (bSearchInProgress)
	{
		MS_LOG(Warning, TEXT("Find: a search is already running."));
		SetLastError(EMSSessionError::AlreadySearching);
		return false;
	}

	NativeFindCallback = MoveTemp(OnDone);
	const bool bStarted = FindLobbies(Settings);
	if (!bStarted)
	{
		// If CompleteFind already ran it consumed the callback; otherwise drop it so it can't fire for a later search.
		NativeFindCallback = nullptr;
	}
	return bStarted;
}

void UMSSessionSubsystem::CancelFind()
{
	if (!bSearchInProgress)
	{
		return;
	}

	MS_LOG(Display, TEXT("Find: cancelled."));
	bSearchCancelled = true;

	if (IOnlineSession* SessionInt = GetSessionInterface())
	{
		SessionInt->CancelFindSessions();
	}

	CompleteFind(false, LastSearchResults, EMSSessionError::Cancelled);
}

void UMSSessionSubsystem::HandleFindTimeout()
{
	if (!bSearchInProgress)
	{
		return;
	}

	MS_LOG(Warning, TEXT("Find: timed out after %.0fs without the online subsystem reporting completion."),
		GetDefault<UMSSessionSettings>()->FindTimeoutSeconds);

	if (IOnlineSession* SessionInt = GetSessionInterface())
	{
		SessionInt->CancelFindSessions();
	}

	CompleteFind(false, TArray<FMSLobbyInfo>(), EMSSessionError::TimedOut);
}

void UMSSessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (!bSearchInProgress || bFindCompletionFired)
	{
		MS_LOG(Log, TEXT("Find: ignoring a FindSessions callback (no search of ours is running)."));
		return;
	}

	if (bSearchCancelled)
	{
		CompleteFind(false, LastSearchResults, EMSSessionError::Cancelled);
		return;
	}

	if (!bWasSuccessful || !ActiveSearch.IsValid())
	{
		MS_LOG(Error, TEXT("Find: the online subsystem reported the search as failed."));
		CompleteFind(false, TArray<FMSLobbyInfo>(), EMSSessionError::SearchFailed);
		return;
	}

	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	TArray<FMSLobbyInfo> Results;

	const int32 RawCount = ActiveSearch->SearchResults.Num();
	MS_LOG(Log, TEXT("Find: online subsystem returned %d raw result(s)."), RawCount);

	for (int32 Index = 0; Index < RawCount; ++Index)
	{
		const FOnlineSessionSearchResult& Result = ActiveSearch->SearchResults[Index];
		const FString Description = DescribeSearchResult(Result);
		FString RejectReason;

		// LAN results may have no OwningUserId, so validate the session info instead of IsValid().
		if (Result.GetSessionIdStr().IsEmpty() || !Result.IsSessionInfoValid())
		{
			RejectReason = TEXT("no valid session info");
		}
		else
		{
			const FOnlineSessionSettings& SearchSettings = Result.Session.SessionSettings;

			if (!SearchSettings.bShouldAdvertise)
			{
				RejectReason = TEXT("not advertised");
			}
			else if (SearchSettings.bAllowJoinViaPresenceFriendsOnly)
			{
				RejectReason = TEXT("friends-only lobby");
			}
			else if (!SearchSettings.bAllowJoinViaPresence && !SearchSettings.bIsLANMatch)
			{
				RejectReason = TEXT("invite-only lobby");
			}
			else if (Result.Session.NumOpenPublicConnections <= 0)
			{
				RejectReason = TEXT("lobby is full");
			}
			else
			{
				EMSSessionError ValidationError = EMSSessionError::None;
				if (!ValidateResultGame(Result, ValidationError))
				{
					RejectReason = FString::Printf(TEXT("different game or version (this build: gameId='%s' version='%s')"),
						*ResolvedGameId, *Settings->GameVersion);
				}
			}
		}

		if (!RejectReason.IsEmpty())
		{
			MS_LOG(Log, TEXT("  result #%d REJECTED - %s: %s"), Index, *RejectReason, *Description);
			continue;
		}

		FMSLobbyInfo Info = MakeLobbyInfo(Result);
		if (Info.LobbyName.IsEmpty())
		{
			Info.LobbyName = Info.HostName;
		}

		MS_LOG(Log, TEXT("  result #%d accepted: %s"), Index, *Description);

		Results.Add(Info);
		CachedSearchResults.Add(Info.SessionId, Result);
	}

	if (RawCount == 0)
	{
		MS_LOG(Warning, TEXT("Find: nobody answered. %s"),
			ActiveSearch->bIsLanQuery
				? TEXT("LAN: is the host running MS.Host lan? Same network, and does the firewall allow UDP broadcast? You can bypass discovery with MS.Connect <hostIP>.")
				: TEXT("Internet: is the lobby Public and on the same GameId/GameVersion? Is Steam running and online?"));
	}

	CompleteFind(true, Results, EMSSessionError::None);
}

void UMSSessionSubsystem::CompleteFind(bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error)
{
	if (bFindCompletionFired)
	{
		return;
	}
	bFindCompletionFired = true;
	bSearchInProgress = false;

	if (UWorld* World = GetGameWorld())
	{
		World->GetTimerManager().ClearTimer(FindTimeoutTimer);
	}

	LastSearchResults = Results;
	SetLastError(Error);

	if (bSuccess)
	{
		MS_LOG(Display, TEXT("Find complete: %d lobby(ies) available."), Results.Num());
		for (int32 Index = 0; Index < Results.Num(); ++Index)
		{
			const FMSLobbyInfo& Row = Results[Index];
			MS_LOG(Display, TEXT("  [%d] '%s' host='%s' %d/%d players, ping %dms%s  (MS.Join %d)"),
				Index, *Row.LobbyName, *Row.HostName, Row.CurrentPlayers, Row.MaxPlayers, Row.PingInMs,
				Row.bIsLANMatch ? TEXT(", LAN") : TEXT(""), Index);
		}
	}
	else
	{
		MS_LOG(Warning, TEXT("Find failed: %s - %s"), MSText::GetErrorName(Error), *MSText::GetErrorText(Error).ToString());
	}

	OnFindComplete.Broadcast(bSuccess, LastSearchResults, Error);

	if (NativeFindCallback)
	{
		TFunction<void(bool, const TArray<FMSLobbyInfo>&, EMSSessionError)> Callback = MoveTemp(NativeFindCallback);
		NativeFindCallback = nullptr;
		Callback(bSuccess, LastSearchResults, Error);
	}
}

void UMSSessionSubsystem::GetLastSearchResults(TArray<FMSLobbyInfo>& OutResults) const
{
	OutResults = LastSearchResults;
}

// ---------------------------------------------------------------------------
// Join
// ---------------------------------------------------------------------------

bool UMSSessionSubsystem::JoinLobby(const FMSLobbyInfo& LobbyInfo)
{
	if (!IsOnlineAvailable())
	{
		MS_LOG(Error, TEXT("Join: no online subsystem (active OSS: %s)."), *GetActiveOnlineSubsystemName());
		CompleteJoin(false, EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	if (bJoinInProgress || bDestroyingStale)
	{
		MS_LOG(Warning, TEXT("Join: another join or cleanup is still in progress."));
		SetLastError(EMSSessionError::Busy);
		return false;
	}

	if (LobbyInfo.SessionId.IsEmpty())
	{
		MS_LOG(Warning, TEXT("Join: the lobby has no session id."));
		CompleteJoin(false, EMSSessionError::SessionNotFound);
		return false;
	}

	const FOnlineSessionSearchResult* Found = CachedSearchResults.Find(LobbyInfo.SessionId);
	if (!Found)
	{
		MS_LOG(Warning, TEXT("Join: session %s is not in the last search results. Search again."), *LobbyInfo.SessionId);
		CompleteJoin(false, EMSSessionError::SessionNotFound);
		return false;
	}

	const FOnlineSessionSearchResult Result = *Found;
	BeginJoin(Result, EMSJoinSource::Browser);
	return true;
}

bool UMSSessionSubsystem::JoinSearchResultByIndex(int32 Index)
{
	if (bSearchInProgress)
	{
		MS_LOG(Warning, TEXT("Join: the search is still running. Wait for \"Find complete\" or use MS.FindJoin."));
		SetLastError(EMSSessionError::Busy);
		return false;
	}

	FOnlineSessionSearchResult Result;
	if (!GetCachedSearchResult(Index, Result))
	{
		MS_LOG(Warning, TEXT("Join: there is no search result at index %d (last search found %d). Run MS.Find first, then MS.Results."),
			Index, LastSearchResults.Num());
		SetLastError(EMSSessionError::SessionNotFound);
		return false;
	}

	if (bJoinInProgress || bDestroyingStale)
	{
		MS_LOG(Warning, TEXT("Join: another join or cleanup is still in progress."));
		SetLastError(EMSSessionError::Busy);
		return false;
	}

	BeginJoin(Result, EMSJoinSource::Browser);
	return true;
}

void UMSSessionSubsystem::BeginJoin(const FOnlineSessionSearchResult& Result, EMSJoinSource Source)
{
	if (bJoinInProgress)
	{
		return;
	}

	EMSSessionError ValidationError = EMSSessionError::None;
	if (!ValidateResultGame(Result, ValidationError))
	{
		MS_LOG(Warning, TEXT("Join: refused, the lobby belongs to a different game or version. %s"), *DescribeSearchResult(Result));
		CompleteJoin(false, ValidationError);
		return;
	}

	if (Source == EMSJoinSource::FriendsList || Source == EMSJoinSource::Invite)
	{
		if (!GetDefault<UMSSessionSettings>()->bEnableFriendsListJoin)
		{
			MS_LOG(Warning, TEXT("Join: friends-list joining is disabled in the plugin settings."));
			CompleteJoin(false, EMSSessionError::FriendJoinDisabled);
			return;
		}
	}

	PendingJoinTarget = MakeShared<FOnlineSessionSearchResult>(Result);
	PendingJoinSource = Source;
	bJoinInProgress = true;
	bJoinCompletionFired = false;
	SetLastError(EMSSessionError::None);

	MS_LOG(Display, TEXT("Joining lobby (source=%d): %s"), static_cast<int32>(Source), *DescribeSearchResult(Result));

	if (bInSession)
	{
		MS_LOG(Log, TEXT("Join: leaving the current lobby first."));
		IOnlineSession* SessionInt = GetSessionInterface();
		bLeaveToJoin = true;

		if (!SessionInt || !SessionInt->DestroySession(NAME_GameSession))
		{
			bLeaveToJoin = false;
			ClearLocalSessionState();
			ContinueJoinAfterLeave();
		}
		return;
	}

	// A leftover online session from an earlier failure would make JoinSession fail with AlreadyInSession.
	if (BeginStaleSessionCleanup([this]() { ContinueJoinAfterLeave(); }))
	{
		return;
	}

	ContinueJoinAfterLeave();
}

void UMSSessionSubsystem::ContinueJoinAfterLeave()
{
	if (!PendingJoinTarget.IsValid())
	{
		CompleteJoin(false, EMSSessionError::JoinFailed);
		return;
	}

	IOnlineSession* SessionInt = GetSessionInterface();
	if (!SessionInt)
	{
		CompleteJoin(false, EMSSessionError::NoOnlineSubsystem);
		return;
	}

	MS_LOG(Log, TEXT("Join: calling JoinSession on the online subsystem."));

	if (!SessionInt->JoinSession(0, NAME_GameSession, *PendingJoinTarget))
	{
		MS_LOG(Error, TEXT("Join: JoinSession returned false."));
		if (!bJoinCompletionFired)
		{
			CompleteJoin(false, EMSSessionError::JoinFailed);
		}
	}
}

void UMSSessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (SessionName != NAME_GameSession)
	{
		return;
	}

	if (!bJoinInProgress || bJoinCompletionFired)
	{
		MS_LOG(Log, TEXT("Join: ignoring an unexpected JoinSession callback (%s)."), JoinResultName(Result));
		return;
	}

	MS_LOG(Log, TEXT("Join: JoinSession callback: %s"), JoinResultName(Result));

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		EMSSessionError Error = EMSSessionError::JoinFailed;
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:
			Error = EMSSessionError::SessionFull;
			break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			Error = EMSSessionError::SessionNotFound;
			break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			Error = EMSSessionError::NoConnectAddress;
			break;
		case EOnJoinSessionCompleteResult::AlreadyInSession:
			Error = EMSSessionError::AlreadyInSession;
			break;
		default:
			Error = EMSSessionError::JoinFailed;
			break;
		}

		// Make sure no half-created online session survives to break the next attempt.
		AbandonClientSession(TEXT("JoinSession failed"));
		CompleteJoin(false, Error);
		return;
	}

	IOnlineSession* SessionInt = GetSessionInterface();
	FString Address;
	if (!SessionInt || !SessionInt->GetResolvedConnectString(NAME_GameSession, Address) || Address.IsEmpty())
	{
		MS_LOG(Error, TEXT("Join: the online subsystem could not resolve a connect address for this lobby."));
		AbandonClientSession(TEXT("no connect address"));
		CompleteJoin(false, EMSSessionError::NoConnectAddress);
		return;
	}

	FMSLobbyInfo JoinedInfo;
	if (PendingJoinTarget.IsValid())
	{
		JoinedInfo = MakeLobbyInfo(*PendingJoinTarget);
	}

	FString TravelAddress = Address;
	if (JoinedInfo.bIsLANMatch)
	{
		TravelAddress += TEXT("?bIsLanMatch=1");

		if (Address.StartsWith(TEXT("steam."), ESearchCase::IgnoreCase))
		{
			MS_LOG(Warning, TEXT("Join: this is a LAN lobby but the resolved address '%s' is a Steam P2P address. The connection will probably fail; try MS.Connect <hostIP>."), *Address);
		}
	}

	UGameInstance* GameInstance = GetGameInstance();
	APlayerController* PlayerController = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	if (!PlayerController)
	{
		MS_LOG(Error, TEXT("Join: there is no local PlayerController to travel with."));
		AbandonClientSession(TEXT("no player controller"));
		CompleteJoin(false, EMSSessionError::TravelFailed);
		return;
	}

	bInSession = true;
	bLocalHost = false;
	bAllowNewPlayers = true;
	bTravelInProgress = false;

	CurrentLobbyInfo = JoinedInfo;
	CurrentLobbyName = CurrentLobbyInfo.LobbyName;
	CurrentPlayerCount = CurrentLobbyInfo.CurrentPlayers;
	CurrentMaxPlayers = CurrentLobbyInfo.MaxPlayers;
	CurrentPhase = CurrentLobbyInfo.Phase;
	CurrentVisibility = CurrentLobbyInfo.Visibility;

	MS_LOG(Display, TEXT("Join OK. Connecting to '%s' via %s (ClientTravel %s)"),
		*CurrentLobbyName, JoinedInfo.bIsLANMatch ? TEXT("LAN") : TEXT("Steam"), *TravelAddress);

	PlayerController->ClientTravel(TravelAddress, TRAVEL_Absolute);

	UpdateFriendJoinState();
	OnLobbyInfoChanged.Broadcast();
	CompleteJoin(true, EMSSessionError::None);
}

void UMSSessionSubsystem::CompleteJoin(bool bSuccess, EMSSessionError Error, const FString& CustomMessage)
{
	if (bJoinCompletionFired)
	{
		return;
	}
	bJoinCompletionFired = true;
	bJoinInProgress = false;

	FMSJoinResult JoinResult;
	JoinResult.bSuccess = bSuccess;
	JoinResult.Error = Error;
	JoinResult.ErrorMessage = CustomMessage.IsEmpty() ? MSText::GetErrorText(Error) : FText::FromString(CustomMessage);
	JoinResult.Source = PendingJoinSource;
	if (PendingJoinTarget.IsValid())
	{
		JoinResult.LobbyInfo = MakeLobbyInfo(*PendingJoinTarget);
	}

	SetLastError(Error);
	if (!bSuccess)
	{
		MS_LOG(Warning, TEXT("Join failed: %s - %s"), MSText::GetErrorName(Error), *JoinResult.ErrorMessage.ToString());
	}

	PendingJoinTarget.Reset();
	OnJoinComplete.Broadcast(JoinResult);
}

// ---------------------------------------------------------------------------
// Leave
// ---------------------------------------------------------------------------

void UMSSessionSubsystem::LeaveLobby(bool bReturnToMainMenu)
{
	if (!bInSession)
	{
		ClearLocalSessionState();
		if (bReturnToMainMenu)
		{
			ReturnToMainMenuInternal();
		}
		return;
	}

	if (bLeaveInProgress)
	{
		return;
	}

	MS_LOG(Display, TEXT("Leaving lobby (return to main menu: %s)."), bReturnToMainMenu ? TEXT("yes") : TEXT("no"));

	bLeaveInProgress = true;
	bAutoTravelAfterLeave = bReturnToMainMenu;

	ClearPresence();

	IOnlineSession* SessionInt = GetSessionInterface();
	if (!SessionInt || !SessionInt->DestroySession(NAME_GameSession))
	{
		// Some backends fire the destroy callback synchronously before returning false. Don't handle the leave twice.
		if (bLeaveInProgress)
		{
			bLeaveInProgress = false;
			bAutoTravelAfterLeave = false;
			ClearLocalSessionState();
			OnLobbyLeft.Broadcast(EMSSessionError::None);
			if (bReturnToMainMenu)
			{
				ReturnToMainMenuInternal();
			}
		}
	}
}

void UMSSessionSubsystem::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession)
	{
		return;
	}

	MS_LOG(Log, TEXT("DestroySession complete (success=%d)."), bWasSuccessful ? 1 : 0);

	if (bDestroyingStale)
	{
		bDestroyingStale = false;
		TFunction<void()> Continuation = MoveTemp(PostDestroyContinuation);
		PostDestroyContinuation = nullptr;
		if (Continuation)
		{
			Continuation();
		}
		return;
	}

	if (bLeaveToJoin)
	{
		bLeaveToJoin = false;
		ClearLocalSessionState();
		ContinueJoinAfterLeave();
		return;
	}

	if (!bLeaveInProgress)
	{
		return;
	}

	bLeaveInProgress = false;
	const bool bShouldTravel = bAutoTravelAfterLeave;
	bAutoTravelAfterLeave = false;

	ClearLocalSessionState();
	OnLobbyLeft.Broadcast(EMSSessionError::None);

	if (bShouldTravel)
	{
		ReturnToMainMenuInternal();
	}
}

void UMSSessionSubsystem::TravelToMainMenu()
{
	if (bInSession)
	{
		LeaveLobby(true);
		return;
	}

	ReturnToMainMenuInternal();
}

void UMSSessionSubsystem::ReturnToMainMenuInternal()
{
	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	const FString MapPath = Settings->GetMainMenuMapPath();
	if (MapPath.IsEmpty() || !FPackageName::DoesPackageExist(MapPath))
	{
		SetLastError(EMSSessionError::MapNotConfigured);
		MS_LOG(Warning, TEXT("Travel To Main Menu failed: MainMenuMap is not configured."));
		return;
	}

	UGameplayStatics::OpenLevel(GetGameInstance(), FName(*MapPath), true);
}

void UMSSessionSubsystem::ClearLocalSessionState()
{
	bInSession = false;
	bLocalHost = false;
	bAllowNewPlayers = true;
	bTravelInProgress = false;
	bLeaveInProgress = false;
	bLeaveToJoin = false;
	bKickPending = false;

	CurrentPhase = EMSSessionPhase::None;
	CurrentVisibility = EMSLobbyVisibility::Public;
	CurrentPlayerCount = 0;
	CurrentMaxPlayers = 0;
	CurrentLobbyName.Empty();
	CurrentLobbyInfo = FMSLobbyInfo();
	LastPresenceText.Empty();
	LastFriendJoinState = EMSFriendJoinState::NotInLobby;
}

// ---------------------------------------------------------------------------
// Joinability, presence and updates
// ---------------------------------------------------------------------------

bool UMSSessionSubsystem::ComputeAdvertisedJoinable() const
{
	if (!bInSession || !bLocalHost)
	{
		return false;
	}
	if (!bAllowNewPlayers)
	{
		return false;
	}
	if (bTravelInProgress)
	{
		return false;
	}
	if (CurrentPlayerCount >= CurrentMaxPlayers)
	{
		return false;
	}
	return true;
}

EMSFriendJoinState UMSSessionSubsystem::GetFriendJoinState() const
{
	if (!bInSession)
	{
		return EMSFriendJoinState::NotInLobby;
	}

	if (CurrentLobbyInfo.bIsLANMatch)
	{
		return EMSFriendJoinState::NotSteam;
	}

	if (!GetDefault<UMSSessionSettings>()->bEnableFriendsListJoin)
	{
		return EMSFriendJoinState::DisabledBySettings;
	}

	if (CurrentVisibility == EMSLobbyVisibility::InviteOnly)
	{
		return EMSFriendJoinState::InviteOnly;
	}

	if (bTravelInProgress)
	{
		return EMSFriendJoinState::ChangingMap;
	}

	if (!bAllowNewPlayers)
	{
		return EMSFriendJoinState::ClosedByHost;
	}

	if (CurrentPlayerCount >= CurrentMaxPlayers)
	{
		return EMSFriendJoinState::LobbyFull;
	}

	return EMSFriendJoinState::Open;
}

bool UMSSessionSubsystem::CanFriendsJoinNow() const
{
	return GetFriendJoinState() == EMSFriendJoinState::Open;
}

void UMSSessionSubsystem::UpdateFriendJoinState()
{
	const EMSFriendJoinState NewState = GetFriendJoinState();
	if (NewState != LastFriendJoinState)
	{
		LastFriendJoinState = NewState;
		OnFriendJoinStateChanged.Broadcast(NewState);
	}
}

bool UMSSessionSubsystem::RefreshAdvertisedSession()
{
	if (!bInSession || !bLocalHost)
	{
		return false;
	}

	IOnlineSession* SessionInt = GetSessionInterface();
	if (!SessionInt)
	{
		return false;
	}

	FNamedOnlineSession* NamedSession = SessionInt->GetNamedSession(NAME_GameSession);
	if (!NamedSession)
	{
		return false;
	}

	// LAN sessions have no lobby to update.
	if (NamedSession->SessionSettings.bIsLANMatch)
	{
		return false;
	}

	FOnlineSessionSettings UpdatedSettings = NamedSession->SessionSettings;
	UpdatedSettings.NumPublicConnections = FMath::Max(1, CurrentMaxPlayers);
	UpdatedSettings.bAllowJoinInProgress = ComputeAdvertisedJoinable();
	UpdatedSettings.bShouldAdvertise = true;
	UpdatedSettings.bAllowInvites = true;
	UpdatedSettings.bAllowJoinViaPresence = CurrentVisibility != EMSLobbyVisibility::InviteOnly;
	UpdatedSettings.bAllowJoinViaPresenceFriendsOnly = CurrentVisibility == EMSLobbyVisibility::FriendsOnly;
	UpdatedSettings.Set(MSSessionKeys::Phase,
		CurrentPhase == EMSSessionPhase::InGame ? FString(TEXT("InGame")) : FString(TEXT("Lobby")),
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	UpdatedSettings.Set(MSSessionKeys::LobbyName, CurrentLobbyName,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	return SessionInt->UpdateSession(NAME_GameSession, UpdatedSettings, true);
}

void UMSSessionSubsystem::HandleUpdateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		MS_LOG(Warning, TEXT("Update Session failed (advertised lobby data may be stale)."));
	}
}

void UMSSessionSubsystem::PublishPresence()
{
	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	if (!Settings->bPublishPresence || !bInSession || CurrentLobbyInfo.bIsLANMatch)
	{
		return;
	}

	IOnlinePresence* Presence = GetPresenceInterface();
	IOnlineIdentity* Identity = GetIdentityInterface();
	if (!Presence || !Identity)
	{
		return;
	}

	FUniqueNetIdPtr UserId = Identity->GetUniquePlayerId(0);
	if (!UserId.IsValid())
	{
		return;
	}

	const TCHAR* PhaseText = CurrentPhase == EMSSessionPhase::InGame ? TEXT("In Game") : TEXT("In Lobby");

	FOnlineUserPresenceStatus Status;
	Status.State = EOnlinePresenceState::Online;
	Status.StatusStr = FString::Printf(TEXT("%s (%d/%d)"), PhaseText, CurrentPlayerCount, CurrentMaxPlayers);

	LastPresenceText = Status.StatusStr;
	Presence->SetPresence(*UserId, Status);
}

void UMSSessionSubsystem::ClearPresence()
{
	LastPresenceText.Empty();

	IOnlinePresence* Presence = GetPresenceInterface();
	IOnlineIdentity* Identity = GetIdentityInterface();
	if (!Presence || !Identity)
	{
		return;
	}

	FUniqueNetIdPtr UserId = Identity->GetUniquePlayerId(0);
	if (!UserId.IsValid())
	{
		return;
	}

	FOnlineUserPresenceStatus Status;
	Status.State = EOnlinePresenceState::Online;
	Status.StatusStr = FString();
	Presence->SetPresence(*UserId, Status);
}

void UMSSessionSubsystem::RefreshPresence()
{
	PublishPresence();
}

bool UMSSessionSubsystem::SetLobbyJoinable(bool bJoinable)
{
	if (!bInSession || !bLocalHost)
	{
		SetLastError(EMSSessionError::NotHost);
		return false;
	}

	if (AMSGameMode* GameMode = GetGameWorld() ? GetGameWorld()->GetAuthGameMode<AMSGameMode>() : nullptr)
	{
		GameMode->SetNewPlayersAllowed(bJoinable);
		return true;
	}

	NotifyNewPlayersAllowed(bJoinable);
	return true;
}

bool UMSSessionSubsystem::SetLobbyVisibility(EMSLobbyVisibility NewVisibility)
{
	if (!bInSession || !bLocalHost)
	{
		SetLastError(EMSSessionError::NotHost);
		return false;
	}

	// A LAN lobby has no Steam lobby to re-advertise.
	if (CurrentLobbyInfo.bIsLANMatch)
	{
		SetLastError(EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	CurrentVisibility = NewVisibility;
	CurrentLobbyInfo.Visibility = NewVisibility;

	const bool bUpdated = RefreshAdvertisedSession();
	PublishPresence();
	UpdateFriendJoinState();
	OnLobbyInfoChanged.Broadcast();
	return bUpdated;
}

bool UMSSessionSubsystem::ShowInviteOverlay()
{
	if (!bInSession)
	{
		SetLastError(EMSSessionError::NotInSession);
		return false;
	}

	IOnlineExternalUI* ExternalUI = GetExternalUIInterface();
	if (!ExternalUI || !IsSteamActive())
	{
		SetLastError(EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	const bool bShown = ExternalUI->ShowInviteUI(0, NAME_GameSession);
	if (!bShown)
	{
		MS_LOG(Warning, TEXT("Show Invite Overlay failed (PIE, no Steam overlay, or not launched through Steam)."));
	}
	return bShown;
}

// ---------------------------------------------------------------------------
// Network failures
// ---------------------------------------------------------------------------

void UMSSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	MS_LOG(Warning, TEXT("Network failure: %s (%d) '%s'  [host=%d inLobby=%d driver=%s]"),
		NetworkFailureName(FailureType), static_cast<int32>(FailureType), *ErrorString,
		bLocalHost ? 1 : 0, bInSession ? 1 : 0, Driver ? *Driver->GetClass()->GetName() : TEXT("none"));

	// A recorded kick must not be overwritten by the generic disconnect that follows.
	if (LastError == EMSSessionError::Kicked && bHasNetworkError)
	{
		return;
	}

	// Late admission rejection after a successful JoinSession.
	if (bInSession && !bLocalHost)
	{
		EMSSessionError LateError = EMSSessionError::None;
		if (ErrorString.Contains(TEXT("lobby is full"), ESearchCase::IgnoreCase))
		{
			LateError = EMSSessionError::SessionFull;
		}
		else if (ErrorString.Contains(TEXT("lobby is closed"), ESearchCase::IgnoreCase) ||
				 ErrorString.Contains(TEXT("changing maps"), ESearchCase::IgnoreCase))
		{
			LateError = EMSSessionError::SessionClosed;
		}

		if (LateError != EMSSessionError::None)
		{
			ClearPresence();
			if (IOnlineSession* SessionInt = GetSessionInterface())
			{
				SessionInt->DestroySession(NAME_GameSession);
			}
			ClearLocalSessionState();
			RecordNetworkError(LateError, MSText::GetErrorText(LateError));
			return;
		}
	}

	// Host-side listen/driver failures.
	if (bLocalHost &&
		(FailureType == ENetworkFailure::NetDriverListenFailure || FailureType == ENetworkFailure::NetDriverCreateFailure))
	{
		RecordNetworkError(EMSSessionError::TravelFailed, FText::FromString(ErrorString));
		if (GetDefault<UMSSessionSettings>()->bAutoReturnToMenuOnNetworkError)
		{
			LeaveLobby(true);
		}
		return;
	}

	EMSSessionError MappedError = EMSSessionError::NetworkError;
	if (ErrorString.Contains(TEXT("Host has left"), ESearchCase::IgnoreCase))
	{
		MappedError = EMSSessionError::HostClosed;
	}

	if (!bLocalHost)
	{
		// Destroy the online session too, otherwise the next Join fails with AlreadyInSession.
		AbandonClientSession(TEXT("network failure"));
	}

	RecordNetworkError(MappedError, FText::FromString(ErrorString));
}

void UMSSessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	MS_LOG(Warning, TEXT("Travel failure: type=%d '%s'  [host=%d inLobby=%d]"),
		static_cast<int32>(FailureType), *ErrorString, bLocalHost ? 1 : 0, bInSession ? 1 : 0);

	bTravelInProgress = false;

	if (bLocalHost)
	{
		RefreshAdvertisedSession();
		PublishPresence();
		UpdateFriendJoinState();
	}
	else if (bInSession)
	{
		AbandonClientSession(TEXT("travel failure"));
	}

	RecordNetworkError(EMSSessionError::TravelFailed, FText::FromString(ErrorString));
}

void UMSSessionSubsystem::RecordNetworkError(EMSSessionError Error, const FText& Message)
{
	LastError = Error;
	LastErrorText = Message;
	LastNetworkError = Message;
	bHasNetworkError = true;

	MS_LOG(Warning, TEXT("Network error (%s): %s"), MSText::GetErrorName(Error), *Message.ToString());
	OnNetworkError.Broadcast(Error, Message);
}

void UMSSessionSubsystem::NotifyKicked(const FText& Reason)
{
	const FString Message = Reason.IsEmpty()
		? MSText::GetErrorText(EMSSessionError::Kicked).ToString()
		: FString::Printf(TEXT("%s Reason: %s"), *MSText::GetErrorText(EMSSessionError::Kicked).ToString(), *Reason.ToString());

	bKickPending = true;
	RecordNetworkError(EMSSessionError::Kicked, FText::FromString(Message));
}

bool UMSSessionSubsystem::ConsumeLastNetworkError(FText& OutMessage)
{
	if (!bHasNetworkError)
	{
		OutMessage = FText::GetEmpty();
		return false;
	}

	OutMessage = LastNetworkError;
	LastNetworkError = FText::GetEmpty();
	bHasNetworkError = false;
	return true;
}

// ---------------------------------------------------------------------------
// Invites and friends-list joins
// ---------------------------------------------------------------------------

void UMSSessionSubsystem::HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful)
	{
		MS_LOG(Warning, TEXT("Invite / friends-list join failed before reaching the plugin."));
		SetLastError(EMSSessionError::JoinFailed);
		return;
	}

	if (InviteResult.GetSessionIdStr().IsEmpty())
	{
		MS_LOG(Warning, TEXT("Invite / friends-list join carried no session id."));
		SetLastError(EMSSessionError::SessionNotFound);
		return;
	}

	const EMSJoinSource Source = bPendingCommandLineInvite ? EMSJoinSource::CommandLine : EMSJoinSource::FriendsList;
	bPendingCommandLineInvite = false;

	MS_LOG(Display, TEXT("Invite / friends-list join accepted: %s (source=%d)"),
		*DescribeSearchResult(InviteResult), static_cast<int32>(Source));

	const FMSLobbyInfo Info = MakeLobbyInfo(InviteResult);
	OnInviteAccepted.Broadcast(Info, Source);

	BeginJoin(InviteResult, Source);
}

// ---------------------------------------------------------------------------
// GameMode hooks
// ---------------------------------------------------------------------------

void UMSSessionSubsystem::NotifyMapStarted(EMSSessionPhase NewPhase, bool bAllowNewPlayersIn)
{
	CurrentPhase = NewPhase;
	bAllowNewPlayers = bAllowNewPlayersIn;

	// bTravelInProgress is deliberately NOT cleared here. It is cleared by
	// NotifyTravelFinished (PostSeamlessTravel / failure) or by the initial
	// host/join completion, so the join window stays closed until the
	// destination map has fully settled.

	if (UWorld* World = GetGameWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			CurrentPlayerCount = GameState->PlayerArray.Num();
		}

		if (const AGameModeBase* GameMode = World->GetAuthGameMode())
		{
			if (GameMode->GameSession)
			{
				CurrentMaxPlayers = GameMode->GameSession->MaxPlayers;
			}
		}
	}

	CurrentLobbyInfo.Phase = CurrentPhase;
	CurrentLobbyInfo.CurrentPlayers = CurrentPlayerCount;
	CurrentLobbyInfo.MaxPlayers = CurrentMaxPlayers;

	if (bLocalHost)
	{
		RefreshAdvertisedSession();
		PublishPresence();
		UpdateFriendJoinState();
	}

	OnLobbyInfoChanged.Broadcast();
}

void UMSSessionSubsystem::NotifyTravelStarted()
{
	if (!bLocalHost)
	{
		return;
	}

	bTravelInProgress = true;
	RefreshAdvertisedSession();
	PublishPresence();
	UpdateFriendJoinState();
}

void UMSSessionSubsystem::NotifyTravelFinished(bool bSucceeded)
{
	if (!bLocalHost)
	{
		return;
	}

	bTravelInProgress = false;
	RefreshAdvertisedSession();
	PublishPresence();
	UpdateFriendJoinState();
}

void UMSSessionSubsystem::NotifyPlayerCountChanged()
{
	if (UWorld* World = GetGameWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			CurrentPlayerCount = GameState->PlayerArray.Num();
		}
	}

	CurrentLobbyInfo.CurrentPlayers = CurrentPlayerCount;

	if (bLocalHost)
	{
		RefreshAdvertisedSession();
		PublishPresence();
		UpdateFriendJoinState();
	}

	OnLobbyInfoChanged.Broadcast();
}

void UMSSessionSubsystem::NotifyNewPlayersAllowed(bool bAllowed)
{
	if (!bInSession || !bLocalHost)
	{
		return;
	}

	bAllowNewPlayers = bAllowed;
	RefreshAdvertisedSession();
	PublishPresence();
	UpdateFriendJoinState();
}

bool UMSSessionSubsystem::IsJoinAllowedNow(FText& OutReason) const
{
	if (!bInSession || !bLocalHost)
	{
		OutReason = FText::FromString(RejectClosed);
		return false;
	}

	if (bTravelInProgress)
	{
		OutReason = FText::FromString(RejectChangingMap);
		return false;
	}

	if (!bAllowNewPlayers)
	{
		OutReason = FText::FromString(RejectClosed);
		return false;
	}

	if (CurrentPlayerCount >= CurrentMaxPlayers)
	{
		OutReason = FText::FromString(RejectFull);
		return false;
	}

	OutReason = FText::GetEmpty();
	return true;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

FMSLobbyInfo UMSSessionSubsystem::MakeLobbyInfo(const FOnlineSessionSearchResult& Result) const
{
	FMSLobbyInfo Info;
	Info.SessionId = Result.GetSessionIdStr();
	Info.HostName = Result.Session.OwningUserName;
	Info.PingInMs = Result.PingInMs;

	const FOnlineSessionSettings& SessionSettings = Result.Session.SessionSettings;
	Info.MaxPlayers = SessionSettings.NumPublicConnections;
	Info.CurrentPlayers = FMath::Max(0, SessionSettings.NumPublicConnections - Result.Session.NumOpenPublicConnections);
	Info.bIsLANMatch = SessionSettings.bIsLANMatch;

	SessionSettings.Get(MSSessionKeys::LobbyName, Info.LobbyName);

	FString PhaseString;
	if (SessionSettings.Get(MSSessionKeys::Phase, PhaseString))
	{
		Info.Phase = PhaseString.Equals(TEXT("InGame"), ESearchCase::IgnoreCase) ? EMSSessionPhase::InGame : EMSSessionPhase::Lobby;
	}

	if (SessionSettings.bAllowJoinViaPresenceFriendsOnly)
	{
		Info.Visibility = EMSLobbyVisibility::FriendsOnly;
	}
	else if (!SessionSettings.bAllowJoinViaPresence)
	{
		Info.Visibility = EMSLobbyVisibility::InviteOnly;
	}
	else
	{
		Info.Visibility = EMSLobbyVisibility::Public;
	}

	return Info;
}

bool UMSSessionSubsystem::ValidateResultGame(const FOnlineSessionSearchResult& Result, EMSSessionError& OutError) const
{
	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();

	FString FoundGameId;
	if (!Result.Session.SessionSettings.Get(MSSessionKeys::GameId, FoundGameId) ||
		!FoundGameId.Equals(ResolvedGameId, ESearchCase::IgnoreCase))
	{
		OutError = EMSSessionError::GameMismatch;
		return false;
	}

	FString FoundVersion;
	if (!Result.Session.SessionSettings.Get(MSSessionKeys::GameVersion, FoundVersion) ||
		!FoundVersion.Equals(Settings->GameVersion, ESearchCase::CaseSensitive))
	{
		OutError = EMSSessionError::GameMismatch;
		return false;
	}

	return true;
}

void UMSSessionSubsystem::SetLastError(EMSSessionError Error)
{
	LastError = Error;
	LastErrorText = MSText::GetErrorText(Error);
}

bool UMSSessionSubsystem::GetCurrentLobbyInfo(FMSLobbyInfo& OutInfo) const
{
	if (!bInSession)
	{
		OutInfo = FMSLobbyInfo();
		return false;
	}

	OutInfo = CurrentLobbyInfo;
	OutInfo.CurrentPlayers = CurrentPlayerCount;
	OutInfo.MaxPlayers = CurrentMaxPlayers;
	OutInfo.Phase = CurrentPhase;
	OutInfo.Visibility = CurrentVisibility;
	OutInfo.LobbyName = CurrentLobbyName;
	return true;
}

EMSSessionPhase UMSSessionSubsystem::GetSessionPhase() const
{
	// The GameState replicates the phase to clients. Fall back to the last value
	// reported by the local GameMode before the GameState exists.
	if (const UWorld* World = GetGameWorld())
	{
		if (const AMSGameState* GameState = World->GetGameState<AMSGameState>())
		{
			const EMSSessionPhase ReplicatedPhase = GameState->GetSessionPhase();
			if (ReplicatedPhase != EMSSessionPhase::None)
			{
				return ReplicatedPhase;
			}
		}
	}

	return CurrentPhase;
}

FString UMSSessionSubsystem::GetLocalDisplayName() const
{
	if (IOnlineIdentity* Identity = GetIdentityInterface())
	{
		const FUniqueNetIdPtr UserId = Identity->GetUniquePlayerId(0);
		if (UserId.IsValid())
		{
			const FString Nickname = Identity->GetPlayerNickname(*UserId);
			if (!Nickname.IsEmpty())
			{
				return Nickname;
			}
		}
	}

	return TEXT("Player");
}

bool UMSSessionSubsystem::IsOnlineAvailable() const
{
	return GetSessionInterface() != nullptr && GetIdentityInterface() != nullptr;
}

bool UMSSessionSubsystem::IsSteamActive() const
{
	IOnlineSubsystem* OSS = GetActiveOSS();
	return OSS != nullptr && OSS->GetSubsystemName().ToString().Equals(TEXT("STEAM"), ESearchCase::IgnoreCase);
}

FString UMSSessionSubsystem::GetActiveOnlineSubsystemName() const
{
	IOnlineSubsystem* OSS = GetActiveOSS();
	return OSS ? OSS->GetSubsystemName().ToString() : FString(TEXT("NONE"));
}

TArray<FText> UMSSessionSubsystem::GetConfigWarnings() const
{
	return ConfigWarnings;
}

int32 UMSSessionSubsystem::GetCachedSearchResultCount() const
{
	return LastSearchResults.Num();
}

bool UMSSessionSubsystem::GetCachedSearchResult(int32 Index, FOnlineSessionSearchResult& OutResult) const
{
	if (!LastSearchResults.IsValidIndex(Index))
	{
		return false;
	}

	const FOnlineSessionSearchResult* Found = CachedSearchResults.Find(LastSearchResults[Index].SessionId);
	if (!Found)
	{
		return false;
	}

	OutResult = *Found;
	return true;
}

// ---------------------------------------------------------------------------
// Configuration check
// ---------------------------------------------------------------------------

void UMSSessionSubsystem::RunConfigCheck()
{
	ConfigWarnings.Reset();

	// 1. Default online subsystem.
	FString DefaultPlatformService;
	GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), DefaultPlatformService, GEngineIni);
	if (!DefaultPlatformService.Equals(TEXT("Steam"), ESearchCase::IgnoreCase))
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("OnlineSubsystem.DefaultPlatformService is not Steam. Add [OnlineSubsystem] DefaultPlatformService=Steam to Config/DefaultEngine.ini.")));
	}

	if (DefaultPlatformService.Equals(TEXT("Steam"), ESearchCase::IgnoreCase))
	{
		bool bSteamEnabled = true;
		if (GConfig->GetBool(TEXT("OnlineSubsystemSteam"), TEXT("bEnabled"), bSteamEnabled, GEngineIni) && !bSteamEnabled)
		{
			ConfigWarnings.Add(FText::FromString(
				TEXT("[OnlineSubsystemSteam] bEnabled is false in Config/DefaultEngine.ini. Steam will not initialise.")));
		}

		int32 SteamDevAppId = 0;
		GConfig->GetInt(TEXT("OnlineSubsystemSteam"), TEXT("SteamDevAppId"), SteamDevAppId, GEngineIni);
		const bool bHasAppIdFile = FPaths::FileExists(FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("steam_appid.txt")));
		if (SteamDevAppId <= 0 && !bHasAppIdFile)
		{
			ConfigWarnings.Add(FText::FromString(
				TEXT("No Steam app id: set [OnlineSubsystemSteam] SteamDevAppId=480 (test id) in Config/DefaultEngine.ini or put a steam_appid.txt next to the executable. A packaged build started outside Steam cannot initialise Steam without it.")));
		}
	}

	if (!IsOnlineAvailable())
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("The online session or identity interface is missing. Make sure Steam is running and the Steam online subsystem is enabled.")));
	}

	// 2. GameNetDriver.
	bool bGameNetDriverIsSteamSockets = false;
	if (GEngine)
	{
		for (const FNetDriverDefinition& Definition : GEngine->NetDriverDefinitions)
		{
			if (Definition.DefName == TEXT("GameNetDriver"))
			{
				bGameNetDriverIsSteamSockets = Definition.DriverClassName.ToString().Contains(TEXT("SteamSockets"));
				break;
			}
		}
	}

	if (!bGameNetDriverIsSteamSockets)
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("GameNetDriver is not SteamSockets. Add the NetDriverDefinitions block for /Script/SteamSockets.SteamSocketsNetDriver to Config/DefaultEngine.ini (see the plugin documentation).")));
	}

	// 3. Voice switches.
	bool bVoiceModuleEnabled = false;
	GConfig->GetBool(TEXT("Voice"), TEXT("bEnabled"), bVoiceModuleEnabled, GEngineIni);
	if (!bVoiceModuleEnabled)
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("Voice capture is disabled. Add [Voice] bEnabled=true to Config/DefaultEngine.ini.")));
	}

	bool bVoiceInterfaceEnabled = false;
	GConfig->GetBool(TEXT("OnlineSubsystem"), TEXT("bHasVoiceEnabled"), bVoiceInterfaceEnabled, GEngineIni);
	if (!bVoiceInterfaceEnabled)
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("The online voice interface is disabled. Add [OnlineSubsystem] bHasVoiceEnabled=true to Config/DefaultEngine.ini.")));
	}

	// 4. Maps.
	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	if (Settings->GetLobbyMapPath().IsEmpty())
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("LobbyMap is not set in Project Settings -> Plugins -> Multiplayer Subsystem: Session. Host Lobby will fail with MapNotConfigured.")));
	}
	if (Settings->GetMainMenuMapPath().IsEmpty())
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("MainMenuMap is not set in Project Settings -> Plugins -> Multiplayer Subsystem: Session. Leave Lobby(true) cannot travel back.")));
	}

	if (ConfigWarnings.Num() == 0)
	{
		MS_LOG(Log, TEXT("Configuration check: Configuration OK."));
	}
	else
	{
		MS_LOG(Warning, TEXT("Configuration check: %d problem(s) found. Run MS.CheckConfig for the list."), ConfigWarnings.Num());
		for (const FText& Warning : ConfigWarnings)
		{
			MS_LOG(Log, TEXT("  - %s"), *Warning.ToString());
		}
	}
}

// ---------------------------------------------------------------------------
// Stale session handling
// ---------------------------------------------------------------------------

bool UMSSessionSubsystem::BeginStaleSessionCleanup(TFunction<void()> Continuation)
{
	IOnlineSession* SessionInt = GetSessionInterface();
	if (!SessionInt || !SessionInt->GetNamedSession(NAME_GameSession))
	{
		return false;
	}

	MS_LOG(Warning, TEXT("A leftover online session exists (from a failed attempt or dropped connection). Destroying it first."));

	PostDestroyContinuation = MoveTemp(Continuation);
	bDestroyingStale = true;

	if (!SessionInt->DestroySession(NAME_GameSession))
	{
		MS_LOG(Error, TEXT("Could not destroy the leftover online session; continuing anyway."));
		bDestroyingStale = false;
		PostDestroyContinuation = nullptr;
		return false;
	}

	return true;
}

void UMSSessionSubsystem::AbandonClientSession(const TCHAR* Reason)
{
	MS_LOG(Log, TEXT("Abandoning the client-side session (%s)."), Reason);

	ClearPresence();

	if (IOnlineSession* SessionInt = GetSessionInterface())
	{
		if (SessionInt->GetNamedSession(NAME_GameSession))
		{
			// The completion callback is ignored on purpose (no leave / stale flag is set).
			SessionInt->DestroySession(NAME_GameSession);
		}
	}

	ClearLocalSessionState();
}

// ---------------------------------------------------------------------------
// Direct connect
// ---------------------------------------------------------------------------

bool UMSSessionSubsystem::JoinByAddress(const FString& InAddress, bool bLAN)
{
	if (bJoinInProgress || bDestroyingStale)
	{
		MS_LOG(Warning, TEXT("Connect: another join or cleanup is still in progress."));
		SetLastError(EMSSessionError::Busy);
		return false;
	}

	FString Address = InAddress.TrimStartAndEnd();
	if (Address.IsEmpty())
	{
		MS_LOG(Warning, TEXT("Connect: no address given."));
		SetLastError(EMSSessionError::NoConnectAddress);
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	APlayerController* PlayerController = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	if (!PlayerController)
	{
		MS_LOG(Error, TEXT("Connect: there is no local PlayerController to travel with."));
		SetLastError(EMSSessionError::TravelFailed);
		return false;
	}

	if (bLAN && !Address.Contains(TEXT("bIsLanMatch"), ESearchCase::IgnoreCase))
	{
		Address += TEXT("?bIsLanMatch=1");
	}

	if (bInSession)
	{
		MS_LOG(Warning, TEXT("Connect: you are in a lobby. Run MS.Leave first."));
		SetLastError(EMSSessionError::AlreadyInSession);
		return false;
	}

	bInSession = true;
	bLocalHost = false;
	bAllowNewPlayers = true;
	bTravelInProgress = false;
	CurrentLobbyInfo = FMSLobbyInfo();
	CurrentLobbyInfo.LobbyName = Address;
	CurrentLobbyInfo.bIsLANMatch = bLAN;
	CurrentLobbyName = Address;

	MS_LOG(Display, TEXT("Direct connect (no session discovery): ClientTravel %s"), *Address);
	PlayerController->ClientTravel(Address, TRAVEL_Absolute);

	UpdateFriendJoinState();
	OnLobbyInfoChanged.Broadcast();
	return true;
}

// ---------------------------------------------------------------------------
// Status and diagnostics
// ---------------------------------------------------------------------------

void UMSSessionSubsystem::GetStatusLines(TArray<FString>& OutLines) const
{
	OutLines.Reset();

	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();

	OutLines.Add(FString::Printf(TEXT("Backend: OSS=%s  Steam=%s  GameId=%s  Version=%s"),
		*GetActiveOnlineSubsystemName(), IsSteamActive() ? TEXT("yes") : TEXT("no"), *ResolvedGameId, *Settings->GameVersion));

	OutLines.Add(FString::Printf(TEXT("Lobby: in=%s host=%s LAN=%s phase=%s players=%d/%d visibility=%s name='%s'"),
		bInSession ? TEXT("yes") : TEXT("no"), bLocalHost ? TEXT("yes") : TEXT("no"),
		CurrentLobbyInfo.bIsLANMatch ? TEXT("yes") : TEXT("no"), PhaseName(GetSessionPhase()),
		CurrentPlayerCount, CurrentMaxPlayers, VisibilityName(CurrentVisibility), *CurrentLobbyName));

	OutLines.Add(FString::Printf(TEXT("Join policy: friends=%s allowNewPlayers=%s travelInProgress=%s"),
		MSText::GetFriendJoinStateName(GetFriendJoinState()),
		bAllowNewPlayers ? TEXT("yes") : TEXT("no"), bTravelInProgress ? TEXT("yes") : TEXT("no")));

	OutLines.Add(FString::Printf(TEXT("Busy: hosting=%s joining=%s searching=%s leaving=%s cleaningStale=%s"),
		bHostInProgress ? TEXT("yes") : TEXT("no"), bJoinInProgress ? TEXT("yes") : TEXT("no"),
		bSearchInProgress ? TEXT("yes") : TEXT("no"), bLeaveInProgress ? TEXT("yes") : TEXT("no"),
		bDestroyingStale ? TEXT("yes") : TEXT("no")));

	OutLines.Add(FString::Printf(TEXT("Search: %d result(s) cached   Last error: %s (%s)"),
		LastSearchResults.Num(), MSText::GetErrorName(LastError), *LastErrorText.ToString()));

	if (const UWorld* World = GetGameWorld())
	{
		const UNetDriver* Driver = World->GetNetDriver();
		FString Remote = TEXT("-");
		if (Driver && Driver->ServerConnection)
		{
			Remote = Driver->ServerConnection->LowLevelGetRemoteAddress(true);
		}

		OutLines.Add(FString::Printf(TEXT("World: map=%s netMode=%s driver=%s clients=%d server=%s"),
			*World->GetMapName(), NetModeName(World->GetNetMode()),
			Driver ? *Driver->GetClass()->GetName() : TEXT("none"),
			Driver ? Driver->ClientConnections.Num() : 0, *Remote));
	}
}

void UMSSessionSubsystem::GetDiagnosticLines(TArray<FString>& OutLines) const
{
	GetStatusLines(OutLines);

	if (IOnlineIdentity* Identity = GetIdentityInterface())
	{
		const FUniqueNetIdPtr UserId = Identity->GetUniquePlayerId(0);
		OutLines.Add(FString::Printf(TEXT("Identity: login=%s nickname='%s' id=%s"),
			LoginStatusName(Identity->GetLoginStatus(0)), *GetLocalDisplayName(),
			UserId.IsValid() ? *UserId->ToString() : TEXT("<none>")));
	}
	else
	{
		OutLines.Add(TEXT("Identity: no identity interface"));
	}

	if (IOnlineSession* SessionInt = GetSessionInterface())
	{
		if (const FNamedOnlineSession* Named = SessionInt->GetNamedSession(NAME_GameSession))
		{
			OutLines.Add(FString::Printf(TEXT("Online session 'GameSession': state=%s LAN=%s public=%d open=%d registered=%d owner='%s'"),
				SessionStateName(Named->SessionState), Named->SessionSettings.bIsLANMatch ? TEXT("yes") : TEXT("no"),
				Named->SessionSettings.NumPublicConnections, Named->NumOpenPublicConnections,
				Named->RegisteredPlayers.Num(), *Named->OwningUserName));

			OutLines.Add(FString::Printf(TEXT("  session info: %s"),
				Named->SessionInfo.IsValid() ? *Named->SessionInfo->ToString() : TEXT("<null>")));

			FString Connect;
			if (SessionInt->GetResolvedConnectString(NAME_GameSession, Connect))
			{
				OutLines.Add(FString::Printf(TEXT("  resolved connect string: %s"), *Connect));
			}
			else
			{
				OutLines.Add(TEXT("  resolved connect string: <none>"));
			}
		}
		else
		{
			OutLines.Add(TEXT("Online session 'GameSession': none"));
		}
	}
	else
	{
		OutLines.Add(TEXT("Session interface: missing"));
	}

	if (GEngine)
	{
		for (const FNetDriverDefinition& Definition : GEngine->NetDriverDefinitions)
		{
			OutLines.Add(FString::Printf(TEXT("NetDriverDefinition %s -> %s (fallback %s)"),
				*Definition.DefName.ToString(), *Definition.DriverClassName.ToString(), *Definition.DriverClassNameFallback.ToString()));
		}
	}

	OutLines.Add(FString::Printf(TEXT("Config warnings: %d"), ConfigWarnings.Num()));
	for (const FText& Warning : ConfigWarnings)
	{
		OutLines.Add(FString::Printf(TEXT("  - %s"), *Warning.ToString()));
	}
}

bool UMSSessionSubsystem::TickOverlay(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	if (CVarMSOverlay.GetValueOnGameThread() > 0 && GEngine)
	{
		TArray<FString> Lines;
		GetStatusLines(Lines);

		int32 Key = 0x4D530000;
		for (const FString& Line : Lines)
		{
			GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::Green, FString::Printf(TEXT("[MS] %s"), *Line));
		}
	}
#endif
	return true;
}

#undef LOCTEXT_NAMESPACE
