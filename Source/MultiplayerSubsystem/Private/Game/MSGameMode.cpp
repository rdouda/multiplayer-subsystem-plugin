// Copyright (c) 2026. All rights reserved.

#include "Game/MSGameMode.h"

#include "Core/MSLog.h"
#include "Game/MSGameState.h"
#include "Game/MSPlayerController.h"
#include "Game/MSPlayerState.h"
#include "Session/MSSessionSubsystem.h"
#include "Settings/MSReadyCheckSettings.h"
#include "Settings/MSSessionSettings.h"
#include "MultiplayerSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerState.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "MultiplayerSubsystem"

AMSGameMode::AMSGameMode()
{
	bUseSeamlessTravel = true;

	GameStateClass = AMSGameState::StaticClass();
	PlayerStateClass = AMSPlayerState::StaticClass();
	PlayerControllerClass = AMSPlayerController::StaticClass();
}

UMSSessionSubsystem* AMSGameMode::GetSessionSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UMSSessionSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// Engine overrides
// ---------------------------------------------------------------------------

void AMSGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	const UMSSessionSettings* Settings = GetDefault<UMSSessionSettings>();

	int32 DesiredMaxPlayers = Settings->DefaultMaxPlayers;
	if (const UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		const int32 SessionMaxPlayers = Subsystem->GetCurrentMaxPlayers();
		if (SessionMaxPlayers > 0)
		{
			DesiredMaxPlayers = SessionMaxPlayers;
		}
	}

	const int32 ClampedMaxPlayers = Settings->ClampMaxPlayers(DesiredMaxPlayers);
	if (GameSession)
	{
		// ApproveLogin reads this. URL options are lost on travel, so this is the source of truth.
		GameSession->MaxPlayers = ClampedMaxPlayers;
	}

	MS_LOG(Display, TEXT("GameMode started: map=%s phase=%d allowNewPlayers=%d maxPlayers=%d netMode=%d"),
		*MapName, static_cast<int32>(SessionPhase), bAllowNewPlayers ? 1 : 0, ClampedMaxPlayers, static_cast<int32>(GetNetMode()));

	if (UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->NotifyMapStarted(SessionPhase, bAllowNewPlayers);
	}
}

void AMSGameMode::StartPlay()
{
	Super::StartPlay();

	if (AMSGameState* MSGameState = GetGameState<AMSGameState>())
	{
		MSGameState->SetSessionPhaseAuthoritative(SessionPhase);
	}
}

void AMSGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	MS_LOG(Display, TEXT("A client is connecting from %s (unique id: %s)..."), *Address, *UniqueId.ToString());

	// AGameModeBase::PreLogin rejects a player whose unique id type differs from the DEFAULT online subsystem
	// ("incompatible_unique_net_id"). LAN lobbies run on Null, but a Steam project's default subsystem is Steam,
	// so every LAN joiner (id type NULL) would be refused. PreLogin only uses the id for that check; the real id
	// still reaches Login from the connection. So for LAN lobbies, validate with an empty id.
	FUniqueNetIdRepl IdForEngineCheck = UniqueId;
	if (const UMSSessionSubsystem* LanSubsystem = GetSessionSubsystem())
	{
		if (LanSubsystem->IsInLobby() && LanSubsystem->IsLANLobby() && UniqueId.IsValid())
		{
			MS_LOG(Log, TEXT("LAN lobby: skipping the engine's default-online-subsystem id-type check for %s."), *UniqueId.ToString());
			IdForEngineCheck = FUniqueNetIdRepl();
		}
	}

	FString SuperError;
	Super::PreLogin(Options, Address, IdForEngineCheck, SuperError);
	if (!SuperError.IsEmpty())
	{
		ErrorMessage = SuperError;
		MS_LOG(Warning, TEXT("Connection from %s rejected by the engine: %s"), *Address, *SuperError);
		return;
	}

	// Only enforce the plugin's lobby policy when a lobby is actually running. This keeps
	// PIE and non-lobby listen servers working normally.
	if (const UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		if (Subsystem->IsInLobby())
		{
			FText Reason;
			if (!Subsystem->IsJoinAllowedNow(Reason))
			{
				ErrorMessage = Reason.ToString();
				MS_LOG(Warning, TEXT("Connection from %s rejected by the lobby policy: %s"), *Address, *ErrorMessage);
			}
		}
	}
}

void AMSGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	MS_LOG(Display, TEXT("Player joined: '%s' (%d player(s) now)."),
		NewPlayer && NewPlayer->PlayerState ? *NewPlayer->PlayerState->GetPlayerName() : TEXT("?"), GetNumPlayers());

	ApplyHostState(NewPlayer);

	if (UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->NotifyPlayerCountChanged();
	}
}

void AMSGameMode::Logout(AController* Exiting)
{
	const FString LeavingName = (Exiting && Exiting->PlayerState) ? Exiting->PlayerState->GetPlayerName() : FString(TEXT("?"));

	Super::Logout(Exiting);

	MS_LOG(Display, TEXT("Player left: '%s' (%d player(s) now)."), *LeavingName, GetNumPlayers());

	if (UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->NotifyPlayerCountChanged(GetNumPlayers());
	}
}

void AMSGameMode::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();

	if (AMSGameState* MSGameState = GetGameState<AMSGameState>())
	{
		MSGameState->SetSessionPhaseAuthoritative(SessionPhase);
	}

	if (GetDefault<UMSReadyCheckSettings>()->bResetReadyAfterTravel)
	{
		ResetAllReady();
	}

	if (UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->NotifyTravelFinished(true);
	}

	OnPostSeamlessTravel();
}

void AMSGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	Super::HandleSeamlessTravelPlayer(C);

	ApplyHostState(C);

	if (APlayerController* Controller = Cast<APlayerController>(C))
	{
		OnPlayerSeamlessTravelled(Controller);
	}
}

// ---------------------------------------------------------------------------
// Join policy
// ---------------------------------------------------------------------------

void AMSGameMode::SetNewPlayersAllowed(bool bAllowed)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bAllowNewPlayers == bAllowed)
	{
		return;
	}

	bAllowNewPlayers = bAllowed;

	MS_LOG(Display, TEXT("New players %s."), bAllowed ? TEXT("allowed") : TEXT("disallowed"));

	if (UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->NotifyNewPlayersAllowed(bAllowed);
	}
}

// ---------------------------------------------------------------------------
// Travel
// ---------------------------------------------------------------------------

bool AMSGameMode::CanTravelNow(EMSTravelBlockReason& OutReason) const
{
	OutReason = EMSTravelBlockReason::None;

	if (!HasAuthority())
	{
		OutReason = EMSTravelBlockReason::NotAuthority;
		return false;
	}

	if (const UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		if (Subsystem->IsTravelInProgress())
		{
			OutReason = EMSTravelBlockReason::TravelInProgress;
			return false;
		}
	}

	const UMSReadyCheckSettings* Settings = GetDefault<UMSReadyCheckSettings>();
	if (Settings->bEnableReadyCheck && Settings->bRequireAllReadyToTravel && SessionPhase == EMSSessionPhase::Lobby)
	{
		if (!AreAllPlayersReady())
		{
			OutReason = EMSTravelBlockReason::NotAllReady;
			return false;
		}
	}

	return true;
}

bool AMSGameMode::TravelToMap(TSoftObjectPtr<UWorld> Map, const TArray<FString>& Options)
{
	const FString MapPath = Map.IsNull() ? FString() : MSUtil::GetPackagePath(Map.ToString());
	return TravelToMapInternal(MapPath, Options, false);
}

bool AMSGameMode::TravelToMapPath(const FString& MapPath, const TArray<FString>& Options)
{
	return TravelToMapInternal(MSUtil::GetPackagePath(MapPath), Options, false);
}

bool AMSGameMode::ForceTravelToMap(TSoftObjectPtr<UWorld> Map, const TArray<FString>& Options)
{
	const FString MapPath = Map.IsNull() ? FString() : MSUtil::GetPackagePath(Map.ToString());
	return TravelToMapInternal(MapPath, Options, true);
}

bool AMSGameMode::ForceTravelToMapPath(const FString& MapPath, const TArray<FString>& Options)
{
	return TravelToMapInternal(MSUtil::GetPackagePath(MapPath), Options, true);
}

bool AMSGameMode::ReturnToLobby()
{
	const FString LobbyMapPath = GetDefault<UMSSessionSettings>()->GetLobbyMapPath();
	return TravelToMapInternal(LobbyMapPath, TArray<FString>(), true);
}

bool AMSGameMode::TravelToMapInternal(const FString& MapPath, const TArray<FString>& Options, bool bForce)
{
	if (!HasAuthority())
	{
		BroadcastTravelBlocked(EMSTravelBlockReason::NotAuthority);
		return false;
	}

	if (MapPath.IsEmpty() || !FPackageName::DoesPackageExist(MapPath))
	{
		BroadcastTravelBlocked(EMSTravelBlockReason::InvalidMap);
		return false;
	}

	UMSSessionSubsystem* Subsystem = GetSessionSubsystem();
	if (Subsystem && Subsystem->IsTravelInProgress())
	{
		BroadcastTravelBlocked(EMSTravelBlockReason::TravelInProgress);
		return false;
	}

	if (!bForce)
	{
		EMSTravelBlockReason Reason = EMSTravelBlockReason::None;
		if (!CanTravelNow(Reason))
		{
			BroadcastTravelBlocked(Reason);
			return false;
		}
	}

	TArray<FString> FinalOptions = Options;

	// A listen server must stay a listen server after the travel. BuildTravelURL dedups.
	if (GetNetMode() == NM_ListenServer)
	{
		FinalOptions.Add(TEXT("listen"));
	}

	// SteamSockets reads this URL option to use its LAN path.
	if (Subsystem && Subsystem->IsLANLobby())
	{
		FinalOptions.Add(TEXT("bIsLanMatch=1"));
	}

	const FString URL = MSUtil::BuildTravelURL(MapPath, FinalOptions);

	OnBeforeTravel(MapPath);

	if (Subsystem)
	{
		Subsystem->NotifyTravelStarted();
	}

	MS_LOG(Display, TEXT("Seamless travel to %s"), *URL);
	GetWorld()->ServerTravel(URL, true);
	return true;
}

void AMSGameMode::BroadcastTravelBlocked(EMSTravelBlockReason Reason)
{
	MS_LOG(Warning, TEXT("Travel blocked: %s"), *MSText::GetTravelBlockText(Reason).ToString());
	OnTravelBlocked(Reason);
}

// ---------------------------------------------------------------------------
// Ready check
// ---------------------------------------------------------------------------

void AMSGameMode::ResetAllReady()
{
	if (!HasAuthority())
	{
		return;
	}

	if (AMSGameState* MSGameState = GetGameState<AMSGameState>())
	{
		for (APlayerState* BasePlayerState : MSGameState->PlayerArray)
		{
			if (AMSPlayerState* PlayerState = Cast<AMSPlayerState>(BasePlayerState))
			{
				// The host is "always ready" by setting, so keep its flag consistent with the summary.
				const bool bKeepReady = GetDefault<UMSReadyCheckSettings>()->bHostIsAlwaysReady && PlayerState->IsLobbyHost();
				PlayerState->SetReadyAuthoritative(bKeepReady);
			}
		}

		MSGameState->RecomputeReadySummary();
	}
}

bool AMSGameMode::AreAllPlayersReady() const
{
	if (const AMSGameState* MSGameState = GetGameState<AMSGameState>())
	{
		return MSGameState->AreAllPlayersReady();
	}

	return false;
}

bool AMSGameMode::TrySetPlayerReady(APlayerController* Controller, bool bReady)
{
	if (!HasAuthority() || !Controller)
	{
		return false;
	}

	const UMSReadyCheckSettings* Settings = GetDefault<UMSReadyCheckSettings>();
	if (!Settings->bEnableReadyCheck)
	{
		return false;
	}

	if (SessionPhase != EMSSessionPhase::Lobby)
	{
		return false;
	}

	if (const UMSSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		if (Subsystem->IsTravelInProgress())
		{
			return false;
		}
	}

	AMSPlayerState* PlayerState = Controller->GetPlayerState<AMSPlayerState>();
	if (!PlayerState)
	{
		return false;
	}

	if (Settings->bHostIsAlwaysReady && PlayerState->IsLobbyHost())
	{
		return false;
	}

	PlayerState->SetReadyAuthoritative(bReady);
	return true;
}

// ---------------------------------------------------------------------------
// Kick
// ---------------------------------------------------------------------------

bool AMSGameMode::KickPlayer(APlayerController* TargetPlayer, FText Reason)
{
	if (!HasAuthority() || !TargetPlayer)
	{
		return false;
	}

	// Never kick the listen-server owner.
	if (TargetPlayer->IsLocalController())
	{
		MS_LOG(Warning, TEXT("Kick refused: the target is the host."));
		return false;
	}

	if (const AMSPlayerState* PlayerState = TargetPlayer->GetPlayerState<AMSPlayerState>())
	{
		if (PlayerState->IsABot())
		{
			MS_LOG(Warning, TEXT("Kick refused: the target is a bot."));
			return false;
		}
	}

	if (!GameSession)
	{
		return false;
	}

	if (Reason.IsEmpty())
	{
		Reason = LOCTEXT("KickDefaultReason", "You were removed from the lobby by the host.");
	}

	const FString PlayerName = TargetPlayer->PlayerState ? TargetPlayer->PlayerState->GetPlayerName() : TEXT("Unknown");
	MS_LOG(Display, TEXT("Kicking %s. Reason: %s"), *PlayerName, *Reason.ToString());

	GameSession->KickPlayer(TargetPlayer, Reason);
	return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void AMSGameMode::ApplyHostState(AController* Controller)
{
	if (!Controller || GetNetMode() == NM_DedicatedServer || !Controller->IsLocalController())
	{
		return;
	}

	if (AMSPlayerState* PlayerState = Controller->GetPlayerState<AMSPlayerState>())
	{
		PlayerState->SetLobbyHostAuthoritative(true);

		if (GetDefault<UMSReadyCheckSettings>()->bHostIsAlwaysReady)
		{
			PlayerState->SetReadyAuthoritative(true);
		}
	}
}

#undef LOCTEXT_NAMESPACE
