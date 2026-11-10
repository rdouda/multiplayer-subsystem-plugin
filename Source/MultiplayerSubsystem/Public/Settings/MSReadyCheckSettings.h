// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MSReadyCheckSettings.generated.h"

/**
 * Passive ready-check settings.
 * Shown under Project Settings -> Plugins -> Multiplayer Subsystem: Ready Check.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Multiplayer Subsystem: Ready Check"))
class MULTIPLAYERSUBSYSTEM_API UMSReadyCheckSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMSReadyCheckSettings();

	/** Master switch. When off, ready flags are ignored and nothing is gated. */
	UPROPERTY(EditAnywhere, Config, Category = "Ready Check")
	bool bEnableReadyCheck = true;

	/** When on, Travel To Map is blocked until every eligible player is ready. */
	UPROPERTY(EditAnywhere, Config, Category = "Ready Check")
	bool bRequireAllReadyToTravel = false;

	/** The host counts as ready and cannot toggle. */
	UPROPERTY(EditAnywhere, Config, Category = "Ready Check")
	bool bHostIsAlwaysReady = true;

	/** Clears every ready flag when a seamless travel completes. */
	UPROPERTY(EditAnywhere, Config, Category = "Ready Check")
	bool bResetReadyAfterTravel = true;

	virtual FName GetCategoryName() const override;
};
