// Copyright (c) 2026. All rights reserved.

#include "MultiplayerSubsystem.h"

DEFINE_LOG_CATEGORY(LogMultiplayerSubsystem);

void FMultiplayerSubsystemModule::StartupModule()
{
	UE_LOG(LogMultiplayerSubsystem, Log, TEXT("MultiplayerSubsystem module loaded."));
}

void FMultiplayerSubsystemModule::ShutdownModule()
{
	UE_LOG(LogMultiplayerSubsystem, Log, TEXT("MultiplayerSubsystem module unloaded."));
}

IMPLEMENT_MODULE(FMultiplayerSubsystemModule, MultiplayerSubsystem);
