// Copyright (c) 2026. All rights reserved.
//
// Console commands for testing the plugin without any Blueprint or UI.
// Compiled out of Shipping builds. Type MS.Help in the console for the list.
//
// Output goes to the console that ran the command (so it is visible in a packaged
// game) and, when that is not the log itself, to the UE log / terminal as well.
// Results of async operations (find, join, ...) cannot appear in the console, so they
// are shown on screen and in the log instead; MS.Log prints the recent history.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Core/MSLog.h"
#include "Core/MSTypes.h"
#include "Game/MSGameMode.h"
#include "Game/MSGameState.h"
#include "Game/MSPlayerController.h"
#include "Game/MSPlayerState.h"
#include "Session/MSSessionSubsystem.h"
#include "MultiplayerSubsystem.h"
#include "Voice/MSVoiceSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

namespace MSDebug
{
	void Out(FOutputDevice& Ar, ELogVerbosity::Type Verbosity, const FString& Message)
	{
		Ar.Log(Verbosity, *Message);

		// When the command came from the in-game console (not the log), mirror to the terminal.
		if (&Ar != static_cast<FOutputDevice*>(GLog))
		{
			switch (Verbosity)
			{
			case ELogVerbosity::Error:
				UE_LOG(LogMultiplayerSubsystem, Error, TEXT("%s"), *Message);
				break;
			case ELogVerbosity::Warning:
				UE_LOG(LogMultiplayerSubsystem, Warning, TEXT("%s"), *Message);
				break;
			default:
				UE_LOG(LogMultiplayerSubsystem, Display, TEXT("%s"), *Message);
				break;
			}
		}
	}

	UMSSessionSubsystem* GetSession(UWorld* World)
	{
		return World && World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UMSSessionSubsystem>()
			: nullptr;
	}

	UMSVoiceSubsystem* GetVoice(UWorld* World)
	{
		return World && World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UMSVoiceSubsystem>()
			: nullptr;
	}

	APlayerController* GetLocalController(UWorld* World)
	{
		return World && World->GetGameInstance()
			? World->GetGameInstance()->GetFirstLocalPlayerController()
			: nullptr;
	}

	bool ParseBool(const FString& Value)
	{
		return Value.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("on"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
	}

	/** True when a bare word such as "lan" is present (case-insensitive). */
	bool HasFlag(const TArray<FString>& Args, const TCHAR* Flag)
	{
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(Flag, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	/** Reads "key=value" (case-insensitive key). */
	bool GetOption(const TArray<FString>& Args, const TCHAR* Key, FString& OutValue)
	{
		const FString Prefix = FString(Key) + TEXT("=");
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				OutValue = Arg.Mid(Prefix.Len());
				return true;
			}
		}
		return false;
	}

	/** First argument that is not a flag word or key=value pair. */
	bool GetFirstPositional(const TArray<FString>& Args, FString& OutValue)
	{
		for (const FString& Arg : Args)
		{
			if (!Arg.Contains(TEXT("=")) && !Arg.Equals(TEXT("lan"), ESearchCase::IgnoreCase) && !Arg.Equals(TEXT("nolan"), ESearchCase::IgnoreCase))
			{
				OutValue = Arg;
				return true;
			}
		}
		return false;
	}

	bool ParseVisibility(const FString& Text, EMSLobbyVisibility& OutVisibility)
	{
		if (Text.Equals(TEXT("public"), ESearchCase::IgnoreCase))
		{
			OutVisibility = EMSLobbyVisibility::Public;
			return true;
		}
		if (Text.Equals(TEXT("friends"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("friendsonly"), ESearchCase::IgnoreCase))
		{
			OutVisibility = EMSLobbyVisibility::FriendsOnly;
			return true;
		}
		if (Text.Equals(TEXT("invite"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("inviteonly"), ESearchCase::IgnoreCase))
		{
			OutVisibility = EMSLobbyVisibility::InviteOnly;
			return true;
		}
		return false;
	}

	bool RequireSession(UMSSessionSubsystem* Session, const TCHAR* Command, FOutputDevice& Ar)
	{
		if (!Session)
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("%s: no session subsystem (is there a GameInstance?)."), Command));
			return false;
		}
		return true;
	}

	void PrintLines(FOutputDevice& Ar, const TArray<FString>& Lines)
	{
		for (const FString& Line : Lines)
		{
			Out(Ar, ELogVerbosity::Display, Line);
		}
	}

	// -----------------------------------------------------------------------
	// Help
	// -----------------------------------------------------------------------
	void HandleHelp(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		static const TCHAR* Lines[] =
		{
			TEXT("=== MultiplayerSubsystem console commands ==="),
			TEXT("Session"),
			TEXT("  MS.Host [lan] [name=<text>] [max=<n>] [vis=public|friends|invite]   host a lobby (use quotes around names with spaces)"),
			TEXT("  MS.Find [lan]            search for lobbies (async: watch the screen / MS.Results)"),
			TEXT("  MS.Cancel                cancel a running search"),
			TEXT("  MS.Results               list the lobbies from the last search"),
			TEXT("  MS.Join [index]          join result <index> (default 0) from the last search"),
			TEXT("  MS.FindJoin [lan] [i]    search, then join result <i> (default 0) as soon as the search completes"),
			TEXT("  MS.Connect <ip[:port]> [nolan]   skip discovery and connect straight to an address (LAN debugging)"),
			TEXT("  MS.Leave                 leave the lobby and return to the main menu"),
			TEXT("  MS.Menu                  travel to the main menu map (leaving first if needed)"),
			TEXT("  MS.Visibility <public|friends|invite>   host only"),
			TEXT("  MS.Joinable <0|1>        host only: open/close the lobby to new players"),
			TEXT("  MS.Kick <name|#index> [reason]          host only"),
			TEXT("  MS.Invite                Steam invite overlay"),
			TEXT("  MS.Presence              re-publish Steam presence"),
			TEXT("Game flow"),
			TEXT("  MS.Travel <MapPath> [force]   host only: seamless travel"),
			TEXT("  MS.ReturnToLobby         host only: seamless travel back to the lobby map"),
			TEXT("  MS.Ready [0|1]           set / toggle your ready flag"),
			TEXT("  MS.ReadyStatus           ready summary and every player's flag"),
			TEXT("  MS.ResetReady            host only: clear all ready flags"),
			TEXT("  MS.Players               players in the current world (ping, id, host, ready)"),
			TEXT("Diagnostics"),
			TEXT("  MS.Status                one-screen state summary"),
			TEXT("  MS.Diag                  deep dump: login, online session, connect string, net drivers, config warnings"),
			TEXT("  MS.CheckConfig           configuration problems (alias: MS.Config)"),
			TEXT("  MS.Backend [auto|steam|null]   show or override the backend (LAN always uses Null)"),
			TEXT("  MS.Log [n]               last n log lines (default 40), including async events"),
			TEXT("  MS.Log.Clear             clear that history"),
			TEXT("  MS.Log.OnScreen <0|1|2>  on-screen messages: off / important / everything"),
			TEXT("  MS.Log.ScreenTime <sec>  how long on-screen messages stay"),
			TEXT("  MS.Overlay <0|1>         live status panel on screen"),
			TEXT("Voice"),
			TEXT("  MS.Voice.Status | MS.Voice.Mode <open|ptt> | MS.Voice.Talk <0|1> | MS.Voice.Mute <name> <0|1>"),
			TEXT("  MS.Voice.Volume <0-4> | MS.Voice.Threshold <0-1>"),
		};

		for (const TCHAR* Line : Lines)
		{
			Out(Ar, ELogVerbosity::Display, Line);
		}
	}

	// -----------------------------------------------------------------------
	// Session flow
	// -----------------------------------------------------------------------
	void HandleHost(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Host"), Ar))
		{
			return;
		}

		FMSHostSettings Settings;
		Settings.bIsLANMatch = HasFlag(Args, TEXT("lan"));

		FString Value;
		if (GetOption(Args, TEXT("name"), Value))
		{
			Settings.LobbyName = Value;
		}
		if (GetOption(Args, TEXT("max"), Value))
		{
			Settings.MaxPlayers = FCString::Atoi(*Value);
		}
		if (GetOption(Args, TEXT("vis"), Value) && !ParseVisibility(Value, Settings.Visibility))
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.Host: unknown visibility '%s' (use public, friends or invite)."), *Value));
			return;
		}

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Host: hosting (LAN=%d). Progress appears on screen; MS.Log shows history."), Settings.bIsLANMatch ? 1 : 0));
		if (!Session->HostLobby(Settings))
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.Host: not started (%s)."), *Session->GetLastErrorText().ToString()));
		}
	}

	void HandleFind(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Find"), Ar))
		{
			return;
		}

		FMSSearchSettings Settings;
		Settings.bIsLANMatch = HasFlag(Args, TEXT("lan"));

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Find: searching (LAN=%d). Wait for \"Find complete\" on screen, then MS.Results / MS.Join <index>."), Settings.bIsLANMatch ? 1 : 0));
		if (!Session->FindLobbies(Settings))
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.Find: not started (%s)."), *Session->GetLastErrorText().ToString()));
		}
	}

	void HandleCancel(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UMSSessionSubsystem* Session = GetSession(World))
		{
			Out(Ar, ELogVerbosity::Display, Session->IsSearchInProgress() ? TEXT("MS.Cancel: cancelling the search.") : TEXT("MS.Cancel: no search is running."));
			Session->CancelFind();
		}
	}

	void HandleResults(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Results"), Ar))
		{
			return;
		}

		TArray<FMSLobbyInfo> Rows;
		Session->GetLastSearchResults(Rows);

		if (Session->IsSearchInProgress())
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.Results: a search is still running; these are the previous results."));
		}

		if (Rows.Num() == 0)
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.Results: no lobbies. Run MS.Find [lan] and wait for it to finish."));
			return;
		}

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Results: %d lobby(ies)"), Rows.Num()));
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			const FMSLobbyInfo& Row = Rows[Index];
			Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("  [%d] '%s' host='%s' %d/%d ping=%dms LAN=%d phase=%d  -> MS.Join %d"),
				Index, *Row.LobbyName, *Row.HostName, Row.CurrentPlayers, Row.MaxPlayers, Row.PingInMs,
				Row.bIsLANMatch ? 1 : 0, static_cast<int32>(Row.Phase), Index));
		}
	}

	void HandleJoin(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Join"), Ar))
		{
			return;
		}

		FString Positional;
		const int32 Index = GetFirstPositional(Args, Positional) ? FCString::Atoi(*Positional) : 0;

		if (Session->IsSearchInProgress())
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.Join: the search is still running. Wait for \"Find complete\" on screen, or use MS.FindJoin."));
			return;
		}

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Join: joining result %d (of %d)..."), Index, Session->GetCachedSearchResultCount()));
		if (!Session->JoinSearchResultByIndex(Index))
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.Join: not started (%s)."), *Session->GetLastErrorText().ToString()));
		}
	}

	void HandleFindJoin(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.FindJoin"), Ar))
		{
			return;
		}

		FMSSearchSettings Settings;
		Settings.bIsLANMatch = HasFlag(Args, TEXT("lan"));

		FString Positional;
		const int32 Index = GetFirstPositional(Args, Positional) ? FCString::Atoi(*Positional) : 0;

		TWeakObjectPtr<UMSSessionSubsystem> WeakSession(Session);
		const bool bStarted = Session->FindLobbiesThen(Settings,
			[WeakSession, Index](bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error)
			{
				UMSSessionSubsystem* StrongSession = WeakSession.Get();
				if (!StrongSession)
				{
					return;
				}

				if (!bSuccess)
				{
					MS_LOG(Warning, TEXT("MS.FindJoin: search failed, not joining."));
					return;
				}

				if (!Results.IsValidIndex(Index))
				{
					MS_LOG(Warning, TEXT("MS.FindJoin: search found %d lobby(ies), none at index %d. Not joining."), Results.Num(), Index);
					return;
				}

				MS_LOG(Display, TEXT("MS.FindJoin: joining result %d."), Index);
				StrongSession->JoinSearchResultByIndex(Index);
			});

		if (!bStarted)
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.FindJoin: not started (%s)."), *Session->GetLastErrorText().ToString()));
			return;
		}

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.FindJoin: searching (LAN=%d), will join result %d when done."), Settings.bIsLANMatch ? 1 : 0, Index));
	}

	void HandleConnect(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Connect"), Ar))
		{
			return;
		}

		FString Address;
		if (!GetFirstPositional(Args, Address))
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.Connect: usage MS.Connect <ip[:port]> [nolan]   e.g. MS.Connect 192.168.1.20:7777  or  MS.Connect 127.0.0.1"));
			return;
		}

		const bool bLAN = !HasFlag(Args, TEXT("nolan"));
		if (!Session->JoinByAddress(Address, bLAN))
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.Connect: not started (%s)."), *Session->GetLastErrorText().ToString()));
		}
	}

	void HandleLeave(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UMSSessionSubsystem* Session = GetSession(World))
		{
			Session->LeaveLobby(true);
		}
	}

	void HandleMenu(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UMSSessionSubsystem* Session = GetSession(World))
		{
			Session->TravelToMainMenu();
		}
	}

	void HandleVisibility(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Visibility"), Ar))
		{
			return;
		}

		EMSLobbyVisibility Visibility = EMSLobbyVisibility::Public;
		if (Args.Num() < 1 || !ParseVisibility(Args[0], Visibility))
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.Visibility: usage MS.Visibility <public|friends|invite>"));
			return;
		}

		const bool bOk = Session->SetLobbyVisibility(Visibility);
		Out(Ar, bOk ? ELogVerbosity::Display : ELogVerbosity::Warning,
			FString::Printf(TEXT("MS.Visibility: %s."), bOk ? TEXT("ok") : *Session->GetLastErrorText().ToString()));
	}

	void HandleJoinable(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UMSSessionSubsystem* Session = GetSession(World))
		{
			const bool bJoinable = Args.Num() > 0 ? ParseBool(Args[0]) : true;
			const bool bOk = Session->SetLobbyJoinable(bJoinable);
			Out(Ar, bOk ? ELogVerbosity::Display : ELogVerbosity::Warning,
				FString::Printf(TEXT("MS.Joinable %d: %s."), bJoinable ? 1 : 0, bOk ? TEXT("ok") : *Session->GetLastErrorText().ToString()));
		}
	}

	void HandleKick(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		AMSGameMode* GameMode = World ? World->GetAuthGameMode<AMSGameMode>() : nullptr;
		if (!GameMode)
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.Kick: only the host running an MS GameMode can kick."));
			return;
		}

		AGameStateBase* GameState = World->GetGameState();
		if (Args.Num() < 1 || !GameState)
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.Kick: usage MS.Kick <name|#index> [reason]  (see MS.Players)"));
			return;
		}

		APlayerState* Target = nullptr;
		if (Args[0].StartsWith(TEXT("#")))
		{
			const int32 Index = FCString::Atoi(*Args[0].Mid(1));
			if (GameState->PlayerArray.IsValidIndex(Index))
			{
				Target = GameState->PlayerArray[Index];
			}
		}
		else
		{
			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				if (PlayerState && PlayerState->GetPlayerName().Equals(Args[0], ESearchCase::IgnoreCase))
				{
					Target = PlayerState;
					break;
				}
			}
		}

		if (!Target || !Target->GetPlayerController())
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.Kick: no player matches '%s'."), *Args[0]));
			return;
		}

		TArray<FString> ReasonWords = Args;
		ReasonWords.RemoveAt(0);
		const bool bOk = GameMode->KickPlayer(Target->GetPlayerController(), FText::FromString(FString::Join(ReasonWords, TEXT(" "))));
		Out(Ar, bOk ? ELogVerbosity::Display : ELogVerbosity::Warning, FString::Printf(TEXT("MS.Kick: %s."), bOk ? TEXT("kicked") : TEXT("refused")));
	}

	void HandleInvite(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UMSSessionSubsystem* Session = GetSession(World))
		{
			const bool bShown = Session->ShowInviteOverlay();
			Out(Ar, bShown ? ELogVerbosity::Display : ELogVerbosity::Warning, bShown ? TEXT("MS.Invite: overlay opened.") : TEXT("MS.Invite: overlay not opened."));
		}
	}

	void HandlePresence(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UMSSessionSubsystem* Session = GetSession(World))
		{
			Session->RefreshPresence();
			Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Presence: '%s'."), *Session->GetPublishedPresenceText()));
		}
	}

	// -----------------------------------------------------------------------
	// Game flow
	// -----------------------------------------------------------------------
	void HandleTravel(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		AMSGameMode* GameMode = World ? World->GetAuthGameMode<AMSGameMode>() : nullptr;
		if (!GameMode)
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.Travel: no MS GameMode (host only)."));
			return;
		}

		if (Args.Num() < 1)
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.Travel: usage MS.Travel <MapPath> [force]"));
			return;
		}

		const bool bForce = HasFlag(Args, TEXT("force"));
		const bool bStarted = bForce
			? GameMode->ForceTravelToMapPath(Args[0], TArray<FString>())
			: GameMode->TravelToMapPath(Args[0], TArray<FString>());

		Out(Ar, bStarted ? ELogVerbosity::Display : ELogVerbosity::Warning, FString::Printf(TEXT("MS.Travel: %s."), bStarted ? TEXT("started") : TEXT("refused (see log)")));
	}

	void HandleReturnToLobby(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		AMSGameMode* GameMode = World ? World->GetAuthGameMode<AMSGameMode>() : nullptr;
		if (!GameMode)
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.ReturnToLobby: no MS GameMode (host only)."));
			return;
		}

		const bool bStarted = GameMode->ReturnToLobby();
		Out(Ar, bStarted ? ELogVerbosity::Display : ELogVerbosity::Warning, FString::Printf(TEXT("MS.ReturnToLobby: %s."), bStarted ? TEXT("started") : TEXT("refused (see log)")));
	}

	void HandleReady(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		AMSPlayerController* Controller = Cast<AMSPlayerController>(GetLocalController(World));
		if (!Controller)
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.Ready: no local MS PlayerController."));
			return;
		}

		if (Args.Num() > 0)
		{
			Controller->SetReady(ParseBool(Args[0]));
		}
		else
		{
			Controller->ToggleReady();
		}

		Out(Ar, ELogVerbosity::Display, TEXT("MS.Ready: requested (the server validates it; see MS.ReadyStatus)."));
	}

	void HandleReadyStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		AMSGameState* GameState = World ? World->GetGameState<AMSGameState>() : nullptr;
		if (!GameState)
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.ReadyStatus: no MS GameState."));
			return;
		}

		FMSReadySummary Summary;
		GameState->GetReadySummary(Summary);

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.ReadyStatus: %d/%d ready, all ready=%s"),
			Summary.ReadyCount, Summary.TotalCount, Summary.bAllReady ? TEXT("yes") : TEXT("no")));

		for (APlayerState* BasePlayerState : GameState->PlayerArray)
		{
			if (const AMSPlayerState* PlayerState = Cast<AMSPlayerState>(BasePlayerState))
			{
				Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("  %s host=%s ready=%s"),
					*PlayerState->GetPlayerName(), PlayerState->IsLobbyHost() ? TEXT("yes") : TEXT("no"), PlayerState->IsReady() ? TEXT("yes") : TEXT("no")));
			}
		}
	}

	void HandleResetReady(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		AMSGameMode* GameMode = World ? World->GetAuthGameMode<AMSGameMode>() : nullptr;
		if (!GameMode)
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.ResetReady: no MS GameMode (host only)."));
			return;
		}

		GameMode->ResetAllReady();
		Out(Ar, ELogVerbosity::Display, TEXT("MS.ResetReady: done."));
	}

	void HandlePlayers(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		if (!GameState)
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.Players: no GameState in this world."));
			return;
		}

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Players: %d player(s)"), GameState->PlayerArray.Num()));
		for (int32 Index = 0; Index < GameState->PlayerArray.Num(); ++Index)
		{
			const APlayerState* PlayerState = GameState->PlayerArray[Index];
			if (!PlayerState)
			{
				continue;
			}

			const AMSPlayerState* MSPlayerState = Cast<AMSPlayerState>(PlayerState);
			Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("  #%d '%s' ping=%.0fms id=%s%s%s"),
				Index, *PlayerState->GetPlayerName(), PlayerState->GetPingInMilliseconds(), *PlayerState->GetUniqueId().ToString(),
				(MSPlayerState && MSPlayerState->IsLobbyHost()) ? TEXT(" [host]") : TEXT(""),
				(MSPlayerState && MSPlayerState->IsReady()) ? TEXT(" [ready]") : TEXT("")));
		}
	}

	// -----------------------------------------------------------------------
	// Diagnostics
	// -----------------------------------------------------------------------
	void HandleStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Status"), Ar))
		{
			return;
		}

		TArray<FString> Lines;
		Session->GetStatusLines(Lines);

		Out(Ar, ELogVerbosity::Display, TEXT("=== MultiplayerSubsystem status ==="));
		PrintLines(Ar, Lines);
		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("Presence: '%s'   Config warnings: %d (MS.CheckConfig)   More: MS.Diag, MS.Log"),
			*Session->GetPublishedPresenceText(), Session->GetConfigWarnings().Num()));
	}

	void HandleDiag(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Diag"), Ar))
		{
			return;
		}

		TArray<FString> Lines;
		Session->GetDiagnosticLines(Lines);

		Out(Ar, ELogVerbosity::Display, TEXT("=== MultiplayerSubsystem diagnostics ==="));
		PrintLines(Ar, Lines);
	}

	void HandleCheckConfig(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.CheckConfig"), Ar))
		{
			return;
		}

		const TArray<FText> Warnings = Session->GetConfigWarnings();
		if (Warnings.Num() == 0)
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.CheckConfig: no problems found."));
			return;
		}

		Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.CheckConfig: %d problem(s):"), Warnings.Num()));
		for (const FText& Warning : Warnings)
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("  - %s"), *Warning.ToString()));
		}
	}

	void HandleBackend(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSSessionSubsystem* Session = GetSession(World);
		if (!RequireSession(Session, TEXT("MS.Backend"), Ar))
		{
			return;
		}

		if (Args.Num() == 0)
		{
			Out(Ar, ELogVerbosity::Display, FString::Printf(
				TEXT("MS.Backend: configured=%s active=%s override=%s OSS=%s."),
				MSText::GetBackendName(Session->GetConfiguredBackend()),
				MSText::GetBackendName(Session->GetActiveBackend()),
				MSText::GetBackendName(Session->GetBackendOverride()),
				*Session->GetActiveOnlineSubsystemName()));
			return;
		}

		const FString Requested = Args[0].ToLower();
		EMSSessionBackend NewBackend = EMSSessionBackend::Auto;
		if (Requested == TEXT("auto") || Requested == TEXT("reset"))
		{
			NewBackend = EMSSessionBackend::Auto;
		}
		else if (Requested == TEXT("steam"))
		{
			NewBackend = EMSSessionBackend::Steam;
		}
		else if (Requested == TEXT("null"))
		{
			NewBackend = EMSSessionBackend::Null;
		}
		else
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.Backend: expected auto, steam or null."));
			return;
		}

		Session->SetBackendOverride(NewBackend);
		Out(Ar, ELogVerbosity::Display, FString::Printf(
			TEXT("MS.Backend: override=%s (configured=%s, active=%s). Applies to the next host/find/join; LAN always uses Null."),
			MSText::GetBackendName(NewBackend), MSText::GetBackendName(Session->GetConfiguredBackend()),
			MSText::GetBackendName(Session->GetActiveBackend())));
	}

	void HandleLog(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		const int32 Count = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 40;

		TArray<FString> Lines;
		MSLog::GetRecent(Count, Lines);

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("=== last %d MultiplayerSubsystem log line(s) ==="), Lines.Num()));
		PrintLines(Ar, Lines);
	}

	void HandleLogClear(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		MSLog::ClearRecent();
		Out(Ar, ELogVerbosity::Display, TEXT("MS.Log.Clear: history cleared."));
	}

	// -----------------------------------------------------------------------
	// Voice
	// -----------------------------------------------------------------------
	void HandleVoiceStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSVoiceSubsystem* Voice = GetVoice(World);
		if (!Voice)
		{
			Out(Ar, ELogVerbosity::Warning, TEXT("MS.Voice.Status: no voice subsystem."));
			return;
		}

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("Voice: available=%s mode=%s talking=%s threshold=%.3f output=%.2f"),
			Voice->IsVoiceAvailable() ? TEXT("yes") : TEXT("no"),
			Voice->GetInputMode() == EMSVoiceInputMode::OpenMic ? TEXT("OpenMic") : TEXT("PushToTalk"),
			Voice->IsLocalTalking() ? TEXT("yes") : TEXT("no"),
			Voice->GetMicThreshold(), Voice->GetOutputVolume()));

		const FString Reason = Voice->GetVoiceUnavailableReason();
		if (!Reason.IsEmpty())
		{
			Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("Voice unavailable: %s"), *Reason));
		}
	}

	void HandleVoiceMode(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSVoiceSubsystem* Voice = GetVoice(World);
		if (!Voice)
		{
			return;
		}

		if (Args.Num() < 1)
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.Voice.Mode: usage MS.Voice.Mode <open|ptt>"));
			return;
		}

		const bool bOpen = Args[0].Equals(TEXT("open"), ESearchCase::IgnoreCase);
		const bool bOk = Voice->SetInputMode(bOpen ? EMSVoiceInputMode::OpenMic : EMSVoiceInputMode::PushToTalk);
		Out(Ar, bOk ? ELogVerbosity::Display : ELogVerbosity::Warning, FString::Printf(TEXT("MS.Voice.Mode: %s."), bOk ? TEXT("ok") : TEXT("refused (changing mode is disabled in settings)")));
	}

	void HandleVoiceTalk(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSVoiceSubsystem* Voice = GetVoice(World);
		if (!Voice)
		{
			return;
		}

		const bool bTalk = Args.Num() > 0 ? ParseBool(Args[0]) : true;
		if (bTalk)
		{
			Voice->StartTalking();
		}
		else
		{
			Voice->StopTalking();
		}

		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Voice.Talk: %s (push-to-talk mode only)."), bTalk ? TEXT("start") : TEXT("stop")));
	}

	void HandleVoiceMute(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSVoiceSubsystem* Voice = GetVoice(World);
		if (!Voice || !World)
		{
			return;
		}

		if (Args.Num() < 2)
		{
			Out(Ar, ELogVerbosity::Display, TEXT("MS.Voice.Mute: usage MS.Voice.Mute <playerName> <0|1>"));
			return;
		}

		const bool bMuted = ParseBool(Args.Last());
		const FString PlayerName = FString::Join(Args, TEXT(" ")).LeftChop(Args.Last().Len()).TrimEnd();

		AGameStateBase* GameState = World->GetGameState();
		if (!GameState)
		{
			return;
		}

		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (PlayerState && PlayerState->GetPlayerName().Equals(PlayerName, ESearchCase::IgnoreCase))
			{
				Voice->SetPlayerVoiceMuted(PlayerState, bMuted);
				Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Voice.Mute: '%s' %s."), *PlayerName, bMuted ? TEXT("muted") : TEXT("unmuted")));
				return;
			}
		}

		Out(Ar, ELogVerbosity::Warning, FString::Printf(TEXT("MS.Voice.Mute: player '%s' not found."), *PlayerName));
	}

	void HandleVoiceVolume(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSVoiceSubsystem* Voice = GetVoice(World);
		if (!Voice)
		{
			return;
		}

		if (Args.Num() < 1)
		{
			Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Voice.Volume: currently %.2f. Usage MS.Voice.Volume <0-4>"), Voice->GetOutputVolume()));
			return;
		}

		Voice->SetOutputVolume(FCString::Atof(*Args[0]));
		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Voice.Volume: %.2f."), Voice->GetOutputVolume()));
	}

	void HandleVoiceThreshold(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UMSVoiceSubsystem* Voice = GetVoice(World);
		if (!Voice)
		{
			return;
		}

		if (Args.Num() < 1)
		{
			Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Voice.Threshold: currently %.3f. Usage MS.Voice.Threshold <0-1>"), Voice->GetMicThreshold()));
			return;
		}

		Voice->SetMicThreshold(FCString::Atof(*Args[0]));
		Out(Ar, ELogVerbosity::Display, FString::Printf(TEXT("MS.Voice.Threshold: %.3f."), Voice->GetMicThreshold()));
	}
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

#define MS_REGISTER_COMMAND(Var, Name, Help, Handler) \
	static FAutoConsoleCommandWithWorldArgsAndOutputDevice Var( \
		Name, Help, \
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&MSDebug::Handler));

MS_REGISTER_COMMAND(GCommandMSHelp,        TEXT("MS.Help"),        TEXT("MS.Help - list every MultiplayerSubsystem console command."), HandleHelp)
MS_REGISTER_COMMAND(GCommandMSHost,        TEXT("MS.Host"),        TEXT("MS.Host [lan] [name=<text>] [max=<n>] [vis=public|friends|invite] - host a lobby and open LobbyMap."), HandleHost)
MS_REGISTER_COMMAND(GCommandMSFind,        TEXT("MS.Find"),        TEXT("MS.Find [lan] - search for lobbies. Progress is shown on screen."), HandleFind)
MS_REGISTER_COMMAND(GCommandMSCancel,      TEXT("MS.Cancel"),      TEXT("MS.Cancel - cancel a running search."), HandleCancel)
MS_REGISTER_COMMAND(GCommandMSResults,     TEXT("MS.Results"),     TEXT("MS.Results - list the lobbies found by the last search."), HandleResults)
MS_REGISTER_COMMAND(GCommandMSJoin,        TEXT("MS.Join"),        TEXT("MS.Join [index] - join result <index> (default 0) from the last search."), HandleJoin)
MS_REGISTER_COMMAND(GCommandMSFindJoin,    TEXT("MS.FindJoin"),    TEXT("MS.FindJoin [lan] [index] - search, then join result <index> when the search completes."), HandleFindJoin)
MS_REGISTER_COMMAND(GCommandMSConnect,     TEXT("MS.Connect"),     TEXT("MS.Connect <ip[:port]> [nolan] - connect directly to an address, skipping discovery."), HandleConnect)
MS_REGISTER_COMMAND(GCommandMSLeave,       TEXT("MS.Leave"),       TEXT("MS.Leave - leave the current lobby and return to MainMenuMap."), HandleLeave)
MS_REGISTER_COMMAND(GCommandMSMenu,        TEXT("MS.Menu"),        TEXT("MS.Menu - travel to MainMenuMap (leaving the lobby first if needed)."), HandleMenu)
MS_REGISTER_COMMAND(GCommandMSVisibility,  TEXT("MS.Visibility"),  TEXT("MS.Visibility <public|friends|invite> - host only."), HandleVisibility)
MS_REGISTER_COMMAND(GCommandMSJoinable,    TEXT("MS.Joinable"),    TEXT("MS.Joinable <0|1> - host only: open or close the lobby for new players."), HandleJoinable)
MS_REGISTER_COMMAND(GCommandMSKick,        TEXT("MS.Kick"),        TEXT("MS.Kick <name|#index> [reason] - host only."), HandleKick)
MS_REGISTER_COMMAND(GCommandMSInvite,      TEXT("MS.Invite"),      TEXT("MS.Invite - open the Steam invite overlay."), HandleInvite)
MS_REGISTER_COMMAND(GCommandMSPresence,    TEXT("MS.Presence"),    TEXT("MS.Presence - re-publish presence and print it."), HandlePresence)
MS_REGISTER_COMMAND(GCommandMSTravel,      TEXT("MS.Travel"),      TEXT("MS.Travel <MapPath> [force] - host only: seamless travel."), HandleTravel)
MS_REGISTER_COMMAND(GCommandMSReturnToLobby, TEXT("MS.ReturnToLobby"), TEXT("MS.ReturnToLobby - host only: seamless travel back to the lobby map."), HandleReturnToLobby)
MS_REGISTER_COMMAND(GCommandMSReady,       TEXT("MS.Ready"),       TEXT("MS.Ready [0|1] - set or toggle the local ready flag."), HandleReady)
MS_REGISTER_COMMAND(GCommandMSReadyStatus, TEXT("MS.ReadyStatus"), TEXT("MS.ReadyStatus - print the ready summary and every player's flag."), HandleReadyStatus)
MS_REGISTER_COMMAND(GCommandMSResetReady,  TEXT("MS.ResetReady"),  TEXT("MS.ResetReady - host only: clear every ready flag."), HandleResetReady)
MS_REGISTER_COMMAND(GCommandMSPlayers,     TEXT("MS.Players"),     TEXT("MS.Players - list players in this world."), HandlePlayers)
MS_REGISTER_COMMAND(GCommandMSStatus,      TEXT("MS.Status"),      TEXT("MS.Status - print session, lobby, search and net-driver state."), HandleStatus)
MS_REGISTER_COMMAND(GCommandMSDiag,        TEXT("MS.Diag"),        TEXT("MS.Diag - deep diagnostics: login, online session, connect string, net drivers, config."), HandleDiag)
MS_REGISTER_COMMAND(GCommandMSCheckConfig, TEXT("MS.CheckConfig"), TEXT("MS.CheckConfig - print configuration warnings."), HandleCheckConfig)
MS_REGISTER_COMMAND(GCommandMSConfig,      TEXT("MS.Config"),      TEXT("MS.Config - alias of MS.CheckConfig."), HandleCheckConfig)
MS_REGISTER_COMMAND(GCommandMSBackend,     TEXT("MS.Backend"),     TEXT("MS.Backend [auto|steam|null] - show or override the backend for the next host/find/join (LAN always uses Null)."), HandleBackend)
MS_REGISTER_COMMAND(GCommandMSLog,         TEXT("MS.Log"),         TEXT("MS.Log [n] - print the last n plugin log lines (default 40)."), HandleLog)
MS_REGISTER_COMMAND(GCommandMSLogClear,    TEXT("MS.Log.Clear"),   TEXT("MS.Log.Clear - clear the plugin log history."), HandleLogClear)
MS_REGISTER_COMMAND(GCommandMSVoiceStatus, TEXT("MS.Voice.Status"), TEXT("MS.Voice.Status - print voice state and why it is unavailable."), HandleVoiceStatus)
MS_REGISTER_COMMAND(GCommandMSVoiceMode,   TEXT("MS.Voice.Mode"),  TEXT("MS.Voice.Mode <open|ptt> - switch the local input mode."), HandleVoiceMode)
MS_REGISTER_COMMAND(GCommandMSVoiceTalk,   TEXT("MS.Voice.Talk"),  TEXT("MS.Voice.Talk <0|1> - push-to-talk press/release."), HandleVoiceTalk)
MS_REGISTER_COMMAND(GCommandMSVoiceMute,   TEXT("MS.Voice.Mute"),  TEXT("MS.Voice.Mute <playerName> <0|1> - local-only mute."), HandleVoiceMute)
MS_REGISTER_COMMAND(GCommandMSVoiceVolume, TEXT("MS.Voice.Volume"), TEXT("MS.Voice.Volume <0-4> - remote voice volume."), HandleVoiceVolume)
MS_REGISTER_COMMAND(GCommandMSVoiceThreshold, TEXT("MS.Voice.Threshold"), TEXT("MS.Voice.Threshold <0-1> - open-mic threshold."), HandleVoiceThreshold)

#undef MS_REGISTER_COMMAND

#endif // !UE_BUILD_SHIPPING
