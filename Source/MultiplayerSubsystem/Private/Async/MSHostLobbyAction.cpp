// Copyright (c) 2026. All rights reserved.

#include "Async/MSHostLobbyAction.h"

#include "Session/MSSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UMSHostLobbyAction* UMSHostLobbyAction::HostLobby(UObject* WorldContextObject, FMSHostSettings Settings)
{
	UMSHostLobbyAction* Action = NewObject<UMSHostLobbyAction>();

	// Without this the action can be garbage collected before the async result arrives (e.g. across a map change).
	Action->RegisterWithGameInstance(WorldContextObject);
	Action->CachedSettings = Settings;

	if (UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			Action->SessionSubsystem = GameInstance->GetSubsystem<UMSSessionSubsystem>();
		}
	}

	return Action;
}

void UMSHostLobbyAction::Activate()
{
	if (!SessionSubsystem)
	{
		Complete(false, EMSSessionError::NoOnlineSubsystem);
		return;
	}

	SessionSubsystem->OnHostComplete.AddDynamic(this, &UMSHostLobbyAction::HandleHostComplete);

	// Synchronous validation failures broadcast OnHostComplete before HostLobby returns.
	if (!SessionSubsystem->HostLobby(CachedSettings))
	{
		if (!bCompleted)
		{
			Complete(false, SessionSubsystem->GetLastError());
		}
	}
}

void UMSHostLobbyAction::HandleHostComplete(bool bSuccess, EMSSessionError Error)
{
	Complete(bSuccess, Error);
}

void UMSHostLobbyAction::Complete(bool bSuccess, EMSSessionError Error)
{
	if (bCompleted)
	{
		return;
	}
	bCompleted = true;

	if (SessionSubsystem)
	{
		SessionSubsystem->OnHostComplete.RemoveDynamic(this, &UMSHostLobbyAction::HandleHostComplete);
	}

	if (bSuccess)
	{
		OnSuccess.Broadcast();
	}
	else
	{
		OnFailure.Broadcast(Error, MSText::GetErrorText(Error));
	}

	SetReadyToDestroy();
}
