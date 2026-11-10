// Copyright (c) 2026. All rights reserved.

#include "Session/MSSessionSubsystem.h"

#include "Core/MSLog.h"
#include "Core/MSTypes.h"
#include "Settings/MSSessionSettings.h"
#include "Game/MSGameMode.h"
#include "Game/MSGameState.h"
#include "Net/MSLANNetDriver.h"
#include "MultiplayerSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
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

	/** "192.168.1.20:7777" -> 7777, or INDEX_NONE when there is no port. */
	int32 PortOfAddress(const FString& Address)
	{
		int32 ColonIndex = INDEX_NONE;
		if (!Address.FindLastChar(TEXT(':'), ColonIndex) || ColonIndex < 0)
		{
			return INDEX_NONE;
		}

		const FString PortText = Address.Mid(ColonIndex + 1);
		return PortText.IsNumeric() ? FCString::Atoi(*PortText) : INDEX_NONE;
	}

	/** Lower-cased ip:port with the default game port made explicit, so "10.0.0.5" and "10.0.0.5:7777" compare equal. */
	FString NormalizeConnectAddress(const FString& Address)
	{
		FString Normalized = Address.TrimStartAndEnd().ToLower();
		int32 OptionIndex = INDEX_NONE;
		if (Normalized.FindChar(TEXT('?'), OptionIndex))
		{
			Normalized.LeftInline(OptionIndex);
		}

		if (!Normalized.IsEmpty() && PortOfAddress(Normalized) == INDEX_NONE)
		{
			Normalized += TEXT(":7777");
		}
		return Normalized;
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

	MS_LOG(Display, TEXT("MultiplayerSubsystem ready. Backend=%s (configured=%s, OSS=%s, Steam=%s, %s) GameId=%s Version=%s. Type MS.Help in the console."),
		MSText::GetBackendName(GetActiveBackend()), MSText::GetBackendName(GetConfiguredBackend()),
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

	FTSTicker::GetCoreTicker().RemoveTicker(TransportCheckHandle);
	TransportCheckHandle.Reset();

	SetLANNetDriverOverride(false);
	UnbindDelegates();

	Super::Deinitialize();
}

void UMSSessionSubsystem::BindDelegates()
{
	if (IOnlineSubsystem* DefaultOSS = IOnlineSubsystem::Get())
	{
		EnsureSessionDelegatesBound(DefaultOSS->GetSessionInterface().Get());
	}

	// Bind the explicitly configured backend too, so callbacks arrive even before the
	// first host/find/join (for example a friends-list invite on Steam).
	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	if (Settings->Backend != EMSSessionBackend::Auto)
	{
		if (IOnlineSubsystem* ConfiguredOSS = GetOSSForBackend(Settings->Backend))
		{
			EnsureSessionDelegatesBound(ConfiguredOSS->GetSessionInterface().Get());
		}
	}

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UMSSessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UMSSessionSubsystem::HandleTravelFailure);
	}
}

void UMSSessionSubsystem::EnsureSessionDelegatesBound(IOnlineSession* SessionInt)
{
	if (!SessionInt)
	{
		return;
	}

	for (const FMSBoundSessionDelegates& Bound : BoundSessionDelegates)
	{
		if (Bound.SessionInterface == SessionInt)
		{
			return;
		}
	}

	FMSBoundSessionDelegates Bound;
	Bound.SessionInterface = SessionInt;
	Bound.CreateSessionCompleteHandle = SessionInt->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleCreateSessionComplete));
	Bound.DestroySessionCompleteHandle = SessionInt->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleDestroySessionComplete));
	Bound.FindSessionsCompleteHandle = SessionInt->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleFindSessionsComplete));
	Bound.JoinSessionCompleteHandle = SessionInt->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleJoinSessionComplete));
	Bound.UpdateSessionCompleteHandle = SessionInt->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleUpdateSessionComplete));
	Bound.InviteAcceptedHandle = SessionInt->AddOnSessionUserInviteAcceptedDelegate_Handle(
		FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UMSSessionSubsystem::HandleSessionUserInviteAccepted));

	BoundSessionDelegates.Add(Bound);
}

void UMSSessionSubsystem::UnbindDelegates()
{
	for (FMSBoundSessionDelegates& Bound : BoundSessionDelegates)
	{
		if (!Bound.SessionInterface)
		{
			continue;
		}

		Bound.SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(Bound.CreateSessionCompleteHandle);
		Bound.SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(Bound.DestroySessionCompleteHandle);
		Bound.SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(Bound.FindSessionsCompleteHandle);
		Bound.SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(Bound.JoinSessionCompleteHandle);
		Bound.SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(Bound.UpdateSessionCompleteHandle);
		Bound.SessionInterface->ClearOnSessionUserInviteAcceptedDelegate_Handle(Bound.InviteAcceptedHandle);
	}

	BoundSessionDelegates.Reset();

	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();
}

// ---------------------------------------------------------------------------
// Backend selection
// ---------------------------------------------------------------------------

IOnlineSubsystem* UMSSessionSubsystem::GetOSSForBackend(EMSSessionBackend Backend) const
{
	switch (Backend)
	{
	case EMSSessionBackend::Steam:
		return IOnlineSubsystem::Get(TEXT("STEAM"));
	case EMSSessionBackend::Null:
		return IOnlineSubsystem::Get(TEXT("NULL"));
	case EMSSessionBackend::Auto:
	default:
		return IOnlineSubsystem::Get();
	}
}

IOnlineSubsystem* UMSSessionSubsystem::GetActiveOSS() const
{
	if (bHasActiveBackend && ActiveOSS)
	{
		return ActiveOSS;
	}

	// No session yet: report the subsystem for the configured backend so status and
	// the startup config check describe what will actually be used (Auto -> default).
	const EMSSessionBackend Configured = GetConfiguredBackend();
	if (Configured != EMSSessionBackend::Auto)
	{
		if (IOnlineSubsystem* ConfiguredOSS = GetOSSForBackend(Configured))
		{
			return ConfiguredOSS;
		}
	}

	return IOnlineSubsystem::Get();
}

EMSSessionBackend UMSSessionSubsystem::ResolveBackend(bool bIsLANMatch) const
{
	if (bIsLANMatch)
	{
		// LAN always runs on the Null subsystem. It works without Steam and lets two
		// instances on one machine join; Steam cannot join its own lobby.
		return EMSSessionBackend::Null;
	}

	if (BackendOverride != EMSSessionBackend::Auto)
	{
		return BackendOverride;
	}

	return GetDefault<UMSSessionSettings>()->Backend;
}

bool UMSSessionSubsystem::SelectBackend(EMSSessionBackend Backend)
{
	IOnlineSubsystem* OSS = GetOSSForBackend(Backend);
	if (!OSS)
	{
		MS_LOG(Error, TEXT("Backend %s is not available (is the matching OnlineSubsystem plugin enabled?)."),
			MSText::GetBackendName(Backend));
		return false;
	}

	ActiveOSS = OSS;
	bHasActiveBackend = true;

	// Auto resolves to whatever the project's default online subsystem is. Keep the
	// resolved name so status output and UI can show Steam / Null instead of Auto.
	const FString OSSName = OSS->GetSubsystemName().ToString();
	if (OSSName.Equals(TEXT("STEAM"), ESearchCase::IgnoreCase))
	{
		ActiveBackend = EMSSessionBackend::Steam;
	}
	else if (OSSName.Equals(TEXT("NULL"), ESearchCase::IgnoreCase))
	{
		ActiveBackend = EMSSessionBackend::Null;
	}
	else
	{
		ActiveBackend = Backend;
	}

	EnsureSessionDelegatesBound(OSS->GetSessionInterface().Get());

	MS_LOG(Log, TEXT("Backend selected: %s (online subsystem %s)."),
		MSText::GetBackendName(ActiveBackend), *OSSName);

	return true;
}

void UMSSessionSubsystem::SetBackendOverride(EMSSessionBackend Backend)
{
	BackendOverride = Backend;

	MS_LOG(Display, TEXT("Backend override: %s. It applies to the next host/find/join; LAN still always uses Null."),
		MSText::GetBackendName(Backend));
}

EMSSessionBackend UMSSessionSubsystem::GetConfiguredBackend() const
{
	return BackendOverride != EMSSessionBackend::Auto
		? BackendOverride
		: GetDefault<UMSSessionSettings>()->Backend;
}

EMSSessionBackend UMSSessionSubsystem::GetActiveBackend() const
{
	return bHasActiveBackend ? ActiveBackend : GetConfiguredBackend();
}

FString UMSSessionSubsystem::GetActiveBackendName() const
{
	return FString(MSText::GetBackendName(GetActiveBackend()));
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
// LAN transport
// ---------------------------------------------------------------------------

void UMSSessionSubsystem::SetLANNetDriverOverride(bool bEnable)
{
	if (!GEngine)
	{
		return;
	}

	FNetDriverDefinition* GameDefinition = GEngine->NetDriverDefinitions.FindByPredicate(
		[](const FNetDriverDefinition& Definition) { return Definition.DefName == NAME_GameNetDriver; });

	if (!bEnable)
	{
		if (!bLANNetDriverOverrideActive)
		{
			return;
		}

		// Restore in place: FNamedNetDriver keeps a pointer to this entry, so the array
		// must not be resized while a driver is alive.
		if (GameDefinition)
		{
			GameDefinition->DriverClassName = FName(*SavedGameNetDriverClassName);
			GameDefinition->DriverClassNameFallback = FName(*SavedGameNetDriverFallbackClassName);
		}

		FTSTicker::GetCoreTicker().RemoveTicker(TransportCheckHandle);
		TransportCheckHandle.Reset();

		bLANNetDriverOverrideActive = false;
		MS_LOG(Log, TEXT("LAN transport override cleared. GameNetDriver restored to %s (fallback %s)."),
			*SavedGameNetDriverClassName, *SavedGameNetDriverFallbackClassName);
		SavedGameNetDriverClassName.Empty();
		SavedGameNetDriverFallbackClassName.Empty();
		return;
	}

	if (bLANNetDriverOverrideActive)
	{
		return;
	}

	if (!GetDefault<UMSSessionSettings>()->bUsePlainIpDriverForLAN)
	{
		MS_LOG(Warning, TEXT("LAN: 'Use Plain Ip Driver For LAN' is off. The LAN lobby will use the configured GameNetDriver (%s). ")
			TEXT("If that is SteamSocketsNetDriver, host and client can disagree on the protocol and the join will fail."),
			GameDefinition ? *GameDefinition->DriverClassName.ToString() : TEXT("unknown"));
		return;
	}

	if (!GameDefinition)
	{
		MS_LOG(Warning, TEXT("LAN: GameNetDriver is missing from NetDriverDefinitions; the session will use the configured driver."));
		return;
	}

	SavedGameNetDriverClassName = GameDefinition->DriverClassName.ToString();
	SavedGameNetDriverFallbackClassName = GameDefinition->DriverClassNameFallback.ToString();

	const FName LANDriverClass(*UMSLANNetDriver::StaticClass()->GetPathName());
	GameDefinition->DriverClassName = LANDriverClass;
	GameDefinition->DriverClassNameFallback = LANDriverClass;
	bLANNetDriverOverrideActive = true;

	MS_LOG(Display, TEXT("LAN transport override: GameNetDriver %s -> %s (fallback %s -> %s)."),
		*SavedGameNetDriverClassName, *LANDriverClass.ToString(),
		*SavedGameNetDriverFallbackClassName, *LANDriverClass.ToString());

	ScheduleTransportCheck();
}

void UMSSessionSubsystem::ScheduleTransportCheck()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TransportCheckHandle);
	TransportCheckAttempts = 0;
	TransportCheckHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMSSessionSubsystem::RunTransportCheck), 2.0f);
}

bool UMSSessionSubsystem::RunTransportCheck(float /*DeltaTime*/)
{
	if (!bLANNetDriverOverrideActive || !GEngine)
	{
		TransportCheckHandle.Reset();
		return false;
	}

	// The host's driver appears when the lobby map finishes loading; a joining client's driver is
	// the pending one until the handshake completes.
	UNetDriver* Driver = nullptr;
	const TCHAR* Where = TEXT("world");
	if (const UWorld* World = GetGameWorld())
	{
		Driver = World->GetNetDriver();
	}
	if (!Driver)
	{
		const UGameInstance* GameInstance = GetGameInstance();
		if (const FWorldContext* Context = GameInstance ? GameInstance->GetWorldContext() : nullptr)
		{
			if (Context->PendingNetGame)
			{
				Driver = Context->PendingNetGame->GetNetDriver();
				Where = TEXT("pending connection");
			}
		}
	}

	if (!Driver)
	{
		// Map still loading (host) or the attempt already ended (client). Try a few more times.
		if (++TransportCheckAttempts < 8)
		{
			return true;
		}
		MS_LOG(Log, TEXT("LAN transport check: no net driver appeared within the check window (connection already ended?)."));
		TransportCheckHandle.Reset();
		return false;
	}

	TransportCheckHandle.Reset();

	const FString LocalAddress = Driver->LowLevelGetNetworkNumber();
	if (Driver->GetClass() != UMSLANNetDriver::StaticClass())
	{
		MS_LOG(Error, TEXT("LAN transport check FAILED: this is a LAN lobby but the %s driver is %s, not MSLANNetDriver. ")
			TEXT("Host and client will probably speak different protocols (PacketHandler 'zero's in last byte' / timeout). ")
			TEXT("Check MS.Diag -> NetDriverDefinition GameNetDriver, and that no other code replaces GameNetDriver."),
			Where, *Driver->GetClass()->GetName());
		return false;
	}

	MS_LOG(Display, TEXT("LAN transport check OK: %s driver is MSLANNetDriver (plain UDP), local address %s."),
		Where, LocalAddress.IsEmpty() ? TEXT("<not bound yet>") : *LocalAddress);

	// Null advertises the port it read when the session was created, before the listen socket existed.
	// If the port was taken (two hosts on one PC), UE listens on the next free one and joiners hit the wrong port.
	if (bLocalHost)
	{
		FString Advertised;
		IOnlineSession* SessionInt = GetSessionInterface();
		if (SessionInt && SessionInt->GetResolvedConnectString(NAME_GameSession, Advertised))
		{
			const int32 AdvertisedPort = PortOfAddress(Advertised);
			const int32 ListenPort = PortOfAddress(LocalAddress);
			// Port 0 means "not known when the session was created". The beacon fills in the real port when it
			// answers a query, so that is not a mismatch (joiners see ip:7777 in their results).
			if (AdvertisedPort > 0 && ListenPort != INDEX_NONE && AdvertisedPort != ListenPort)
			{
				MS_LOG(Error, TEXT("LAN host is listening on port %d but advertising %s. Joiners will connect to the wrong port ")
					TEXT("(usually another instance on this PC already owns the advertised port). Give each host its own port with -port=<n>, ")
					TEXT("or close the other process."), ListenPort, *Advertised);
			}
		}
	}

	return false;
}

// ---------------------------------------------------------------------------
// Host
// ---------------------------------------------------------------------------

bool UMSSessionSubsystem::HostLobby(const FMSHostSettings& InSettings)
{
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

	// Internet sessions always use the project's configured GameNetDriver. Clear a stale
	// LAN override left behind by an earlier failed attempt.
	if (!InSettings.bIsLANMatch)
	{
		SetLANNetDriverOverride(false);
	}

	const EMSSessionBackend Backend = ResolveBackend(InSettings.bIsLANMatch);
	if (!SelectBackend(Backend))
	{
		MS_LOG(Error, TEXT("Host: backend %s is not available."), MSText::GetBackendName(Backend));
		FailHost(EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	IOnlineSession* SessionInt = GetSessionInterface();
	if (!SessionInt || !GetIdentityInterface())
	{
		MS_LOG(Error, TEXT("Host: no usable online subsystem (backend %s, active OSS: %s). Run MS.CheckConfig."),
			MSText::GetBackendName(ActiveBackend), *GetActiveOnlineSubsystemName());
		FailHost(EMSSessionError::NoOnlineSubsystem);
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
		MS_LOG(Warning, TEXT("Host: this is an internet (non-LAN) lobby but the active backend is '%s' (%s), not Steam. Nobody outside this LAN will find it. Set Backend to Steam or Auto in Project Settings."),
			MSText::GetBackendName(ActiveBackend), *GetActiveOnlineSubsystemName());
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
	CurrentLobbyInfo.Backend = ActiveBackend;

	bHostInProgress = true;
	bHostCompletionFired = false;
	SetLastError(EMSSessionError::None);

	MS_LOG(Display, TEXT("Hosting '%s': %d players max, %s, visibility %s, backend %s (OSS: %s)"),
		*CurrentLobbyName, CurrentMaxPlayers, InSettings.bIsLANMatch ? TEXT("LAN") : TEXT("internet"),
		VisibilityName(CurrentVisibility), MSText::GetBackendName(ActiveBackend), *GetActiveOnlineSubsystemName());

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
	CurrentLobbyInfo.Backend = ActiveBackend;

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
			MS_LOG(Display, TEXT("LAN lobby is being advertised (host IP %s; the port is filled in when the listen socket is up)."), *AdvertisedAddress);
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

	// UWorld::Listen creates the net driver from GameNetDriver while the map loads.
	if (CurrentLobbyInfo.bIsLANMatch)
	{
		SetLANNetDriverOverride(true);
	}

	UGameplayStatics::OpenLevel(GetGameInstance(), FName(*LobbyMapPath), true, OptionString);

	OnHostComplete.Broadcast(true, EMSSessionError::None);
}

void UMSSessionSubsystem::FailHost(EMSSessionError Error)
{
	bHostInProgress = false;
	SetLANNetDriverOverride(false);
	SetLastError(Error);
	MS_LOG(Warning, TEXT("Host Lobby failed: %s - %s"), MSText::GetErrorName(Error), *MSText::GetErrorText(Error).ToString());
	OnHostComplete.Broadcast(false, Error);
}

// ---------------------------------------------------------------------------
// Find
// ---------------------------------------------------------------------------

bool UMSSessionSubsystem::FindLobbies(const FMSSearchSettings& InSettings)
{
	if (bSearchInProgress)
	{
		// Deliberately not broadcast: that would wrongly finish the search that is already running.
		MS_LOG(Warning, TEXT("Find: a search is already running."));
		SetLastError(EMSSessionError::AlreadySearching);
		return false;
	}

	const EMSSessionBackend Backend = ResolveBackend(InSettings.bIsLANMatch);
	if (!SelectBackend(Backend))
	{
		MS_LOG(Error, TEXT("Find: backend %s is not available."), MSText::GetBackendName(Backend));
		SetLastError(EMSSessionError::NoOnlineSubsystem);
		OnFindComplete.Broadcast(false, TArray<FMSLobbyInfo>(), EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	IOnlineSession* SessionInt = GetSessionInterface();
	if (!SessionInt)
	{
		MS_LOG(Error, TEXT("Find: no online subsystem (backend %s, active OSS: %s). Run MS.CheckConfig."),
			MSText::GetBackendName(ActiveBackend), *GetActiveOnlineSubsystemName());
		SetLastError(EMSSessionError::NoOnlineSubsystem);
		OnFindComplete.Broadcast(false, TArray<FMSLobbyInfo>(), EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	LastSearchBackend = ActiveBackend;

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

	MS_LOG(Display, TEXT("Searching for %s lobbies (backend %s, OSS %s, GameId=%s Version=%s, timeout %.0fs)..."),
		InSettings.bIsLANMatch ? TEXT("LAN") : TEXT("internet"), MSText::GetBackendName(ActiveBackend),
		*GetActiveOnlineSubsystemName(), *ResolvedGameId, *Settings->GameVersion, Settings->FindTimeoutSeconds);

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

	// If this process is hosting, its own beacon answers its own query. Never offer a lobby to itself.
	IOnlineSession* SearchSessionInt = GetSessionInterface();
	FString OwnSessionId;
	if (bInSession && bLocalHost && SearchSessionInt)
	{
		if (const FNamedOnlineSession* OwnSession = SearchSessionInt->GetNamedSession(NAME_GameSession))
		{
			if (OwnSession->SessionInfo.IsValid())
			{
				OwnSessionId = OwnSession->SessionInfo->GetSessionId().ToString();
			}
		}
	}

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

			if (!OwnSessionId.IsEmpty() && Result.GetSessionIdStr() == OwnSessionId)
			{
				RejectReason = TEXT("this is your own lobby");
			}
			else if (!SearchSettings.bShouldAdvertise)
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

		FMSLobbyInfo Info = MakeLobbyInfo(Result, LastSearchBackend);
		if (Info.LobbyName.IsEmpty())
		{
			Info.LobbyName = Info.HostName;
		}

		// The address a joiner would connect to. Shown in the results and used to spot phantom rows.
		FString ResolvedAddress;
		if (SearchSessionInt)
		{
			SearchSessionInt->GetResolvedConnectString(Result, NAME_GamePort, ResolvedAddress);
		}
		Info.ConnectAddress = ResolvedAddress;
		const FString AddressKey = Info.bIsLANMatch ? NormalizeConnectAddress(ResolvedAddress) : FString();

		// A LAN beacon can answer more than once (several interfaces, retries) and old host processes can keep
		// answering. Keep a single row per session id AND per connect address: one address can only be one
		// listening host, so two rows pointing at it are the same lobby (or a stale beacon of a dead one).
		// The lowest-ping copy wins. MS.Results / MS.Join <index> stay unambiguous.
		int32 DuplicateIndex = INDEX_NONE;
		const TCHAR* DuplicateReason = TEXT("");
		for (int32 ExistingIndex = 0; ExistingIndex < Results.Num(); ++ExistingIndex)
		{
			const FMSLobbyInfo& Other = Results[ExistingIndex];
			if (Other.SessionId == Info.SessionId)
			{
				DuplicateIndex = ExistingIndex;
				DuplicateReason = TEXT("same session id");
				break;
			}

			if (!AddressKey.IsEmpty() && Other.bIsLANMatch && NormalizeConnectAddress(Other.ConnectAddress) == AddressKey)
			{
				DuplicateIndex = ExistingIndex;
				DuplicateReason = TEXT("same connect address");
				break;
			}
		}

		if (DuplicateIndex != INDEX_NONE)
		{
			FMSLobbyInfo& Existing = Results[DuplicateIndex];
			const bool bBetterPing = Info.PingInMs >= 0 && (Existing.PingInMs < 0 || Info.PingInMs < Existing.PingInMs);
			if (bBetterPing)
			{
				MS_LOG(Log, TEXT("  result #%d duplicate (%s, id=%s addr=%s): keeping this copy (ping %dms) over row %d (id=%s, ping %dms)."),
					Index, DuplicateReason, *Info.SessionId, *ResolvedAddress, Info.PingInMs, DuplicateIndex, *Existing.SessionId, Existing.PingInMs);

				CachedSearchResults.Remove(Existing.SessionId);
				Existing = Info;
				CachedSearchResults.Add(Info.SessionId, Result);
			}
			else
			{
				MS_LOG(Log, TEXT("  result #%d duplicate (%s, id=%s addr=%s): skipped (ping %dms; row %d has %dms)."),
					Index, DuplicateReason, *Info.SessionId, *ResolvedAddress, Info.PingInMs, DuplicateIndex, Existing.PingInMs);
			}
			continue;
		}

		MS_LOG(Log, TEXT("  result #%d accepted: addr=%s %s"), Index, *ResolvedAddress, *Description);

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
			MS_LOG(Display, TEXT("  [%d] '%s' host='%s' %d/%d players, ping %dms%s%s%s  (MS.Join %d)"),
				Index, *Row.LobbyName, *Row.HostName, Row.CurrentPlayers, Row.MaxPlayers, Row.PingInMs,
				Row.bIsLANMatch ? TEXT(", LAN") : TEXT(""),
				Row.ConnectAddress.IsEmpty() ? TEXT("") : TEXT(", at "), *Row.ConnectAddress, Index);
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
	if (bJoinInProgress || bDestroyingStale)
	{
		MS_LOG(Warning, TEXT("Join: another join or cleanup is still in progress."));
		SetLastError(EMSSessionError::Busy);
		return false;
	}

	const EMSSessionBackend Backend = LastSearchBackend != EMSSessionBackend::Auto
		? LastSearchBackend
		: ResolveBackend(LobbyInfo.bIsLANMatch);
	if (!SelectBackend(Backend))
	{
		MS_LOG(Error, TEXT("Join: backend %s is not available."), MSText::GetBackendName(Backend));
		CompleteJoin(false, EMSSessionError::NoOnlineSubsystem);
		return false;
	}

	if (!IsOnlineAvailable())
	{
		MS_LOG(Error, TEXT("Join: no online subsystem (backend %s, active OSS: %s)."), MSText::GetBackendName(ActiveBackend), *GetActiveOnlineSubsystemName());
		CompleteJoin(false, EMSSessionError::NoOnlineSubsystem);
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
	BeginJoin(Result, EMSJoinSource::Browser, Backend);
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

	BeginJoin(Result, EMSJoinSource::Browser, LastSearchBackend);
	return true;
}

void UMSSessionSubsystem::BeginJoin(const FOnlineSessionSearchResult& Result, EMSJoinSource Source, EMSSessionBackend Backend)
{
	if (bJoinInProgress)
	{
		return;
	}

	if (!SelectBackend(Backend))
	{
		CompleteJoin(false, EMSSessionError::NoOnlineSubsystem);
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

	// Internet sessions use the configured GameNetDriver. LAN applies its override right
	// before ClientTravel, so the active driver is not changed while we leave a session.
	if (!Result.Session.SessionSettings.bIsLANMatch)
	{
		SetLANNetDriverOverride(false);
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
		JoinedInfo = MakeLobbyInfo(*PendingJoinTarget, ActiveBackend);
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

	// UPendingNetGame creates the driver from GameNetDriver shortly after ClientTravel.
	SetLANNetDriverOverride(JoinedInfo.bIsLANMatch);

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
		JoinResult.LobbyInfo = MakeLobbyInfo(*PendingJoinTarget, ActiveBackend);
	}

	SetLastError(Error);
	if (!bSuccess)
	{
		// The LAN driver must not leak into a later internet join.
		SetLANNetDriverOverride(false);
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
	SetLANNetDriverOverride(false);

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
	SetLANNetDriverOverride(false);

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

	// Friends-list join needs Steam presence. LAN and the Null backend have neither.
	if (!IsSteamActive())
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

	// LAN sessions have no lobby to update, but the beacon answers every query from the live session, so its
	// open-slot count must be kept current. The engine registers joining players with the project's DEFAULT online
	// subsystem (Steam), never with the Null session, so the count would stay at "0/N" forever.
	if (NamedSession->SessionSettings.bIsLANMatch)
	{
		NamedSession->NumOpenPublicConnections = FMath::Clamp(CurrentMaxPlayers - CurrentPlayerCount, 0, CurrentMaxPlayers);
		return true;
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

	// Visibility is a Steam lobby concept. LAN and Null lobbies have no lobby to re-advertise.
	if (!IsSteamActive())
	{
		MS_LOG(Warning, TEXT("Set visibility: only available on the Steam backend (active backend is %s)."),
			MSText::GetBackendName(ActiveBackend));
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
		// The LAN session could not start; do not leave the override behind.
		SetLANNetDriverOverride(false);
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

	const EMSSessionBackend Backend = ResolveBackend(InviteResult.Session.SessionSettings.bIsLANMatch);

	MS_LOG(Display, TEXT("Invite / friends-list join accepted: %s (source=%d, backend=%s)"),
		*DescribeSearchResult(InviteResult), static_cast<int32>(Source), MSText::GetBackendName(Backend));

	const FMSLobbyInfo Info = MakeLobbyInfo(InviteResult, Backend);
	OnInviteAccepted.Broadcast(Info, Source);

	BeginJoin(InviteResult, Source, Backend);
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

void UMSSessionSubsystem::NotifyPlayerCountChanged(int32 OverrideCount)
{
	if (OverrideCount != INDEX_NONE)
	{
		// Logout runs before the leaving player's PlayerState leaves PlayerArray, so the caller passes the real count.
		CurrentPlayerCount = FMath::Max(0, OverrideCount);
	}
	else if (UWorld* World = GetGameWorld())
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

FMSLobbyInfo UMSSessionSubsystem::MakeLobbyInfo(const FOnlineSessionSearchResult& Result, EMSSessionBackend Backend) const
{
	FMSLobbyInfo Info;
	Info.SessionId = Result.GetSessionIdStr();
	Info.HostName = Result.Session.OwningUserName;
	Info.PingInMs = Result.PingInMs;
	Info.Backend = Backend;

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

	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();
	const EMSSessionBackend Configured = GetConfiguredBackend();
	// Auto and Steam both expect Steam for internet play. Null does not, and LAN always
	// uses Null, so the Steam-specific checks only matter for those two settings.
	const bool bUsesSteamForInternet = Configured != EMSSessionBackend::Null;

	// 1. Default online subsystem. Auto resolves to it, so it should be Steam.
	FString DefaultPlatformService;
	GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), DefaultPlatformService, GEngineIni);
	const bool bDefaultIsSteam = DefaultPlatformService.Equals(TEXT("Steam"), ESearchCase::IgnoreCase);

	if (bUsesSteamForInternet && !bDefaultIsSteam)
	{
		ConfigWarnings.Add(FText::FromString(FString::Printf(
			TEXT("Backend is %s but OnlineSubsystem.DefaultPlatformService is '%s', not Steam. Internet play needs Steam: add [OnlineSubsystem] DefaultPlatformService=Steam to Config/DefaultEngine.ini, or set Backend to Null for LAN-only / offline testing."),
			MSText::GetBackendName(Configured),
			DefaultPlatformService.IsEmpty() ? TEXT("<empty>") : *DefaultPlatformService)));
	}

	if (bUsesSteamForInternet && bDefaultIsSteam)
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

	// The Null subsystem is required for LAN (and for Backend=Null). Loading it here also
	// makes sure the module is present before the first LAN host/find.
	if (!IOnlineSubsystem::Get(TEXT("NULL")))
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("The Null online subsystem is not available. Enable the OnlineSubsystemNull plugin in the .uproject; LAN and Backend=Null need it.")));
	}

	if (!IsOnlineAvailable())
	{
		ConfigWarnings.Add(FText::FromString(FString::Printf(
			TEXT("The active online subsystem (%s) has no session or identity interface. For internet play, start Steam and enable the Steam online subsystem."),
			*GetActiveOnlineSubsystemName())));
	}

	// 2. GameNetDriver. Internet (Steam) play needs SteamSockets. LAN installs its own
	//    plain-IP driver for the session, so the configured driver only matters for internet.
	bool bGameNetDriverFound = false;
	bool bGameNetDriverIsSteamSockets = false;
	FString GameNetDriverClass;
	if (GEngine)
	{
		for (const FNetDriverDefinition& Definition : GEngine->NetDriverDefinitions)
		{
			if (Definition.DefName == TEXT("GameNetDriver"))
			{
				bGameNetDriverFound = true;
				GameNetDriverClass = Definition.DriverClassName.ToString();
				bGameNetDriverIsSteamSockets = GameNetDriverClass.Contains(TEXT("SteamSockets"));
				break;
			}
		}
	}

	if (!bGameNetDriverFound)
	{
		ConfigWarnings.Add(FText::FromString(
			TEXT("There is no GameNetDriver in NetDriverDefinitions. Add a GameNetDriver block to Config/DefaultEngine.ini (see the plugin documentation).")));
	}
	else if (bUsesSteamForInternet && !bGameNetDriverIsSteamSockets)
	{
		ConfigWarnings.Add(FText::FromString(FString::Printf(
			TEXT("GameNetDriver is '%s', not SteamSockets. Internet play needs /Script/SteamSockets.SteamSocketsNetDriver (see the plugin documentation)."),
			*GameNetDriverClass)));
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
		MS_LOG(Log, TEXT("Configuration check: Configuration OK (backend %s)."), MSText::GetBackendName(Configured));
	}
	else
	{
		MS_LOG(Warning, TEXT("Configuration check: %d problem(s) found (backend %s). Run MS.CheckConfig for the list."),
			ConfigWarnings.Num(), MSText::GetBackendName(Configured));
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

	SetLANNetDriverOverride(bLAN);

	bInSession = true;
	bLocalHost = false;
	bAllowNewPlayers = true;
	bTravelInProgress = false;
	CurrentLobbyInfo = FMSLobbyInfo();
	CurrentLobbyInfo.LobbyName = Address;
	CurrentLobbyInfo.bIsLANMatch = bLAN;
	CurrentLobbyInfo.Backend = bLAN ? EMSSessionBackend::Null : GetActiveBackend();
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

	OutLines.Add(FString::Printf(TEXT("Backend: active=%s configured=%s OSS=%s Steam=%s GameId=%s Version=%s"),
		MSText::GetBackendName(GetActiveBackend()), MSText::GetBackendName(GetConfiguredBackend()),
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

	OutLines.Add(FString::Printf(TEXT("Transport: LAN override=%s"),
		bLANNetDriverOverrideActive ? TEXT("on (MSLANNetDriver)") : TEXT("off")));
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

	OutLines.Add(FString::Printf(TEXT("LAN transport override: %s"),
		bLANNetDriverOverrideActive ? TEXT("active (GameNetDriver -> MSLANNetDriver)") : TEXT("inactive")));

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
