// Copyright (c) 2026. All rights reserved.

#include "Async/MSFindLobbiesAction.h"

#include "Session/MSSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UMSFindLobbiesAction* UMSFindLobbiesAction::FindLobbies(UObject* WorldContextObject, FMSSearchSettings Settings)
{
	UMSFindLobbiesAction* Action = NewObject<UMSFindLobbiesAction>();

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

void UMSFindLobbiesAction::Activate()
{
	if (!SessionSubsystem)
	{
		Complete(false, TArray<FMSLobbyInfo>(), EMSSessionError::NoOnlineSubsystem);
		return;
	}

	SessionSubsystem->OnFindComplete.AddDynamic(this, &UMSFindLobbiesAction::HandleFindComplete);

	if (!SessionSubsystem->FindLobbies(CachedSettings))
	{
		if (!bCompleted)
		{
			Complete(false, TArray<FMSLobbyInfo>(), SessionSubsystem->GetLastError());
		}
	}
}

void UMSFindLobbiesAction::HandleFindComplete(bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error)
{
	Complete(bSuccess, Results, Error);
}

void UMSFindLobbiesAction::Complete(bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error)
{
	if (bCompleted)
	{
		return;
	}
	bCompleted = true;

	if (SessionSubsystem)
	{
		SessionSubsystem->OnFindComplete.RemoveDynamic(this, &UMSFindLobbiesAction::HandleFindComplete);
	}

	if (bSuccess)
	{
		OnSuccess.Broadcast(Results);
	}
	else
	{
		OnFailure.Broadcast(Error, MSText::GetErrorText(Error));
	}

	SetReadyToDestroy();
}
