// Copyright (c) 2026. All rights reserved.

#include "Settings/MSSessionSettings.h"
#include "Core/MSTypes.h"
#include "Misc/App.h"

UMSSessionSettings::UMSSessionSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Multiplayer Subsystem Session");
}

FName UMSSessionSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FString UMSSessionSettings::GetResolvedGameId() const
{
	if (!GameId.IsEmpty())
	{
		return GameId;
	}

	return FApp::GetProjectName();
}

FString UMSSessionSettings::GetMainMenuMapPath() const
{
	return MainMenuMap.IsNull() ? FString() : MSUtil::GetPackagePath(MainMenuMap.ToString());
}

FString UMSSessionSettings::GetLobbyMapPath() const
{
	return LobbyMap.IsNull() ? FString() : MSUtil::GetPackagePath(LobbyMap.ToString());
}

int32 UMSSessionSettings::ClampMaxPlayers(int32 RequestedPlayers) const
{
	const int32 Cap = FMath::Max(1, MaxPlayersCap);
	const int32 Fallback = FMath::Clamp(DefaultMaxPlayers, 1, Cap);
	const int32 Wanted = RequestedPlayers > 0 ? RequestedPlayers : Fallback;
	return FMath::Clamp(Wanted, 1, Cap);
}
