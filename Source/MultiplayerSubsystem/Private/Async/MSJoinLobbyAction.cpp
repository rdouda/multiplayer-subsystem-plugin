// Copyright (c) 2026. All rights reserved.

#include "Async/MSJoinLobbyAction.h"

#include "Session/MSSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UMSJoinLobbyAction* UMSJoinLobbyAction::JoinLobby(UObject* WorldContextObject, FMSLobbyInfo LobbyInfo)
{
	UMSJoinLobbyAction* Action = NewObject<UMSJoinLobbyAction>();

	// Without this the action can be garbage collected before the async result arrives (e.g. across a map change).
	Action->RegisterWithGameInstance(WorldContextObject);
	Action->CachedLobbyInfo = LobbyInfo;

	if (UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			Action->SessionSubsystem = GameInstance->GetSubsystem<UMSSessionSubsystem>();
		}
	}

	return Action;
}

void UMSJoinLobbyAction::Activate()
{
	if (!SessionSubsystem)
	{
		Complete(false, EMSSessionError::NoOnlineSubsystem, CachedLobbyInfo);
		return;
	}

	SessionSubsystem->OnJoinComplete.AddDynamic(this, &UMSJoinLobbyAction::HandleJoinComplete);

	if (!SessionSubsystem->JoinLobby(CachedLobbyInfo))
	{
		if (!bCompleted)
		{
			Complete(false, SessionSubsystem->GetLastError(), CachedLobbyInfo);
		}
	}
}

void UMSJoinLobbyAction::HandleJoinComplete(FMSJoinResult Result)
{
	Complete(Result.bSuccess, Result.Error, Result.LobbyInfo);
}

void UMSJoinLobbyAction::Complete(bool bSuccess, EMSSessionError Error, const FMSLobbyInfo& LobbyInfo)
{
	if (bCompleted)
	{
		return;
	}
	bCompleted = true;

	if (SessionSubsystem)
	{
		SessionSubsystem->OnJoinComplete.RemoveDynamic(this, &UMSJoinLobbyAction::HandleJoinComplete);
	}

	if (bSuccess)
	{
		OnSuccess.Broadcast(LobbyInfo);
	}
	else
	{
		OnFailure.Broadcast(Error, MSText::GetErrorText(Error));
	}

	SetReadyToDestroy();
}
