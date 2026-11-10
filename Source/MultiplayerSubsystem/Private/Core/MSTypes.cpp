// Copyright (c) 2026. All rights reserved.

#include "Core/MSTypes.h"

#define LOCTEXT_NAMESPACE "MultiplayerSubsystem"

namespace MSSessionKeys
{
	const FName LobbyName(TEXT("LOBBYNAME"));
	const FName GameId(TEXT("GAMEID"));
	const FName GameVersion(TEXT("GAMEVER"));
	const FName Phase(TEXT("PHASE"));
}

namespace MSText
{
	const TCHAR* GetErrorName(EMSSessionError Error)
	{
		switch (Error)
		{
		case EMSSessionError::None:             return TEXT("None");
		case EMSSessionError::NoOnlineSubsystem: return TEXT("NoOnlineSubsystem");
		case EMSSessionError::MapNotConfigured:  return TEXT("MapNotConfigured");
		case EMSSessionError::AlreadyInSession:  return TEXT("AlreadyInSession");
		case EMSSessionError::NotInSession:      return TEXT("NotInSession");
		case EMSSessionError::NotHost:           return TEXT("NotHost");
		case EMSSessionError::SearchFailed:      return TEXT("SearchFailed");
		case EMSSessionError::TimedOut:          return TEXT("TimedOut");
		case EMSSessionError::SessionNotFound:   return TEXT("SessionNotFound");
		case EMSSessionError::SessionFull:       return TEXT("SessionFull");
		case EMSSessionError::SessionClosed:     return TEXT("SessionClosed");
		case EMSSessionError::GameMismatch:      return TEXT("GameMismatch");
		case EMSSessionError::JoinFailed:        return TEXT("JoinFailed");
		case EMSSessionError::NoConnectAddress:  return TEXT("NoConnectAddress");
		case EMSSessionError::AlreadySearching:  return TEXT("AlreadySearching");
		case EMSSessionError::Cancelled:         return TEXT("Cancelled");
		case EMSSessionError::HostClosed:        return TEXT("HostClosed");
		case EMSSessionError::TravelFailed:      return TEXT("TravelFailed");
		case EMSSessionError::NetworkError:      return TEXT("NetworkError");
		case EMSSessionError::Kicked:            return TEXT("Kicked");
		case EMSSessionError::FriendJoinDisabled: return TEXT("FriendJoinDisabled");
		case EMSSessionError::Busy:              return TEXT("Busy");
		default:                                 return TEXT("Unknown");
		}
	}

	const TCHAR* GetFriendJoinStateName(EMSFriendJoinState State)
	{
		switch (State)
		{
		case EMSFriendJoinState::NotInLobby:         return TEXT("NotInLobby");
		case EMSFriendJoinState::Open:               return TEXT("Open");
		case EMSFriendJoinState::ClosedByHost:       return TEXT("ClosedByHost");
		case EMSFriendJoinState::ChangingMap:        return TEXT("ChangingMap");
		case EMSFriendJoinState::LobbyFull:          return TEXT("LobbyFull");
		case EMSFriendJoinState::InviteOnly:         return TEXT("InviteOnly");
		case EMSFriendJoinState::DisabledBySettings: return TEXT("DisabledBySettings");
		case EMSFriendJoinState::NotSteam:           return TEXT("NotSteam");
		default:                                     return TEXT("Unknown");
		}
	}

	FText GetErrorText(EMSSessionError Error)
	{
		switch (Error)
		{
		case EMSSessionError::None:              return FText::GetEmpty();
		case EMSSessionError::NoOnlineSubsystem: return LOCTEXT("ErrNoOSS", "Steam is not available. Start Steam and relaunch the game.");
		case EMSSessionError::MapNotConfigured:  return LOCTEXT("ErrMapNotConfigured", "A required map is not configured in Project Settings -> Plugins -> Multiplayer Subsystem: Session.");
		case EMSSessionError::AlreadyInSession:  return LOCTEXT("ErrAlreadyInSession", "You are already in a lobby. Leave it first.");
		case EMSSessionError::NotInSession:      return LOCTEXT("ErrNotInSession", "You are not in a lobby.");
		case EMSSessionError::NotHost:           return LOCTEXT("ErrNotHost", "Only the lobby host can do that.");
		case EMSSessionError::SearchFailed:      return LOCTEXT("ErrSearchFailed", "Could not search for lobbies.");
		case EMSSessionError::TimedOut:          return LOCTEXT("ErrTimedOut", "The search timed out.");
		case EMSSessionError::SessionNotFound:   return LOCTEXT("ErrSessionNotFound", "That lobby no longer exists.");
		case EMSSessionError::SessionFull:       return LOCTEXT("ErrSessionFull", "That lobby is full.");
		case EMSSessionError::SessionClosed:     return LOCTEXT("ErrSessionClosed", "That lobby is not accepting new players.");
		case EMSSessionError::GameMismatch:      return LOCTEXT("ErrGameMismatch", "That lobby is running a different game or version.");
		case EMSSessionError::JoinFailed:        return LOCTEXT("ErrJoinFailed", "Could not join the lobby.");
		case EMSSessionError::NoConnectAddress:  return LOCTEXT("ErrNoConnectAddress", "The lobby did not provide a connect address.");
		case EMSSessionError::AlreadySearching:  return LOCTEXT("ErrAlreadySearching", "A search is already running.");
		case EMSSessionError::Cancelled:         return LOCTEXT("ErrCancelled", "The search was cancelled.");
		case EMSSessionError::HostClosed:        return LOCTEXT("ErrHostClosed", "The host closed the lobby.");
		case EMSSessionError::TravelFailed:      return LOCTEXT("ErrTravelFailed", "Could not open the map.");
		case EMSSessionError::NetworkError:      return LOCTEXT("ErrNetworkError", "Connection lost.");
		case EMSSessionError::Kicked:            return LOCTEXT("ErrKicked", "You were removed from the lobby by the host.");
		case EMSSessionError::FriendJoinDisabled: return LOCTEXT("ErrFriendJoinDisabled", "Joining from the friends list is disabled for this lobby.");
		case EMSSessionError::Busy:              return LOCTEXT("ErrBusy", "Another lobby operation is still in progress. Try again in a moment.");
		default:                                 return LOCTEXT("ErrUnknown", "Something went wrong.");
		}
	}

	FText GetTravelBlockText(EMSTravelBlockReason Reason)
	{
		switch (Reason)
		{
		case EMSTravelBlockReason::None:            return FText::GetEmpty();
		case EMSTravelBlockReason::NotAuthority:    return LOCTEXT("BlockNotAuthority", "Only the host can start the game.");
		case EMSTravelBlockReason::InvalidMap:      return LOCTEXT("BlockInvalidMap", "The destination map is not set.");
		case EMSTravelBlockReason::TravelInProgress: return LOCTEXT("BlockTravelInProgress", "A map change is already in progress.");
		case EMSTravelBlockReason::NotAllReady:     return LOCTEXT("BlockNotAllReady", "Not everyone is ready.");
		default:                                    return LOCTEXT("BlockUnknown", "Travel is not possible right now.");
		}
	}

	FText GetFriendJoinStateText(EMSFriendJoinState State)
	{
		switch (State)
		{
		case EMSFriendJoinState::NotInLobby:        return LOCTEXT("FriendNotInLobby", "Not in a lobby.");
		case EMSFriendJoinState::Open:              return LOCTEXT("FriendOpen", "Friends can join.");
		case EMSFriendJoinState::ClosedByHost:      return LOCTEXT("FriendClosed", "Joining is closed.");
		case EMSFriendJoinState::ChangingMap:       return LOCTEXT("FriendChangingMap", "Changing map...");
		case EMSFriendJoinState::LobbyFull:         return LOCTEXT("FriendFull", "Lobby is full.");
		case EMSFriendJoinState::InviteOnly:        return LOCTEXT("FriendInviteOnly", "Invite only: friends must be invited.");
		case EMSFriendJoinState::DisabledBySettings: return LOCTEXT("FriendDisabled", "Friends-list joining is disabled.");
		case EMSFriendJoinState::NotSteam:          return LOCTEXT("FriendNotSteam", "Not a Steam lobby.");
		default:                                    return FText::GetEmpty();
		}
	}
}

namespace MSUtil
{
	FString GetPackagePath(const FString& ObjectPath)
	{
		FString Path = ObjectPath;
		int32 DotIndex = INDEX_NONE;
		if (Path.FindChar(TEXT('.'), DotIndex))
		{
			Path.LeftInline(DotIndex, EAllowShrinking::No);
		}
		return Path;
	}

	FString BuildTravelURL(const FString& MapPath, const TArray<FString>& Options)
	{
		TArray<FString> FinalOptions;
		for (const FString& RawOption : Options)
		{
			FString Option = RawOption;
			Option.RemoveFromStart(TEXT("?"));
			Option.RemoveFromStart(TEXT("&"));

			// Accept "?A=1?B=2" and "?A=1&B=2" (and mixed) as separate options.
			FString Normalized = Option.Replace(TEXT("&"), TEXT("?"));
			TArray<FString> Split;
			Normalized.ParseIntoArray(Split, TEXT("?"), true);
			for (FString& Piece : Split)
			{
				Piece.TrimStartAndEndInline();
				if (!Piece.IsEmpty() && !FinalOptions.Contains(Piece))
				{
					FinalOptions.Add(Piece);
				}
			}
		}

		if (FinalOptions.Num() == 0)
		{
			return MapPath;
		}

		return FString::Printf(TEXT("%s?%s"), *MapPath, *FString::Join(FinalOptions, TEXT("?")));
	}
}

#undef LOCTEXT_NAMESPACE
