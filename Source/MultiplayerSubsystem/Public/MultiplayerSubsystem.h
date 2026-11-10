// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

MULTIPLAYERSUBSYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogMultiplayerSubsystem, Log, All);

/**
 * Runtime module for the MultiplayerSubsystem plugin.
 *
 * The module itself does no work: the session subsystem runs the startup configuration
 * check, because it needs an initialized GameInstance and online subsystem.
 */
class FMultiplayerSubsystemModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
