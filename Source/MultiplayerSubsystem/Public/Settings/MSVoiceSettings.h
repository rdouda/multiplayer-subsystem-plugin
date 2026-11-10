// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/Attenuation.h"
#include "Core/MSTypes.h"
#include "MSVoiceSettings.generated.h"

class USoundAttenuation;

/**
 * Proximity voice settings.
 * Shown under Project Settings -> Plugins -> Multiplayer Subsystem: Voice.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Multiplayer Subsystem: Voice"))
class MULTIPLAYERSUBSYSTEM_API UMSVoiceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMSVoiceSettings();

	/** Master switch. */
	UPROPERTY(EditAnywhere, Config, Category = "Voice")
	bool bEnableVoice = true;

	/** Input mode for players with no saved preference. */
	UPROPERTY(EditAnywhere, Config, Category = "Voice")
	EMSVoiceInputMode DefaultInputMode = EMSVoiceInputMode::OpenMic;

	/** Whether the project's settings UI may switch the mode at runtime. */
	UPROPERTY(EditAnywhere, Config, Category = "Voice")
	bool bAllowPlayerInputModeChange = true;

	/** Open-mic activation threshold (linear amplitude, 0..1). Engine default is 0.08. */
	UPROPERTY(EditAnywhere, Config, Category = "Voice", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultMicThreshold = 0.08f;

	/** Remote voices are spatialized by distance. Off = everyone hears everyone. */
	UPROPERTY(EditAnywhere, Config, Category = "Proximity")
	bool bProximityEnabled = true;

	/** Global voice in the Lobby phase, proximity only in the game phase. */
	UPROPERTY(EditAnywhere, Config, Category = "Proximity")
	bool bProximityOnlyInGame = true;

	/** A speaker with no pawn is heard globally instead of silently. */
	UPROPERTY(EditAnywhere, Config, Category = "Proximity")
	bool bFallbackTo2DWhenNoPawn = true;

	/** Full volume inside this distance (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "Proximity", meta = (ClampMin = "0.0"))
	float AttenuationInnerRadius = 300.0f;

	/** Volume falls to zero over this extra distance (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "Proximity", meta = (ClampMin = "0.0"))
	float AttenuationFalloffDistance = 2200.0f;

	/** Distance model used when no AttenuationOverride is set. */
	UPROPERTY(EditAnywhere, Config, Category = "Proximity")
	EAttenuationDistanceModel AttenuationFunction = EAttenuationDistanceModel::Logarithmic;

	/** 3D panning. Quality depends on the project's spatialization plugin. */
	UPROPERTY(EditAnywhere, Config, Category = "Proximity")
	bool bEnableSpatialization = true;

	/** Optional Sound Attenuation asset. When set it replaces the numeric values above. */
	UPROPERTY(EditAnywhere, Config, Category = "Proximity")
	TSoftObjectPtr<USoundAttenuation> AttenuationOverride;

	/** Global scale for remote voices. */
	UPROPERTY(EditAnywhere, Config, Category = "Output", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float OutputVolumeMultiplier = 1.0f;

	/** Optional bandwidth optimization: the host stops sending out-of-range voices. */
	UPROPERTY(EditAnywhere, Config, Category = "Range Gate")
	bool bServerSideRangeGate = false;

	/** Update rate of the range gate. */
	UPROPERTY(EditAnywhere, Config, Category = "Range Gate", meta = (ClampMin = "0.05"))
	float RangeGateIntervalSeconds = 0.25f;

	/** Extra distance before re-enabling a gated listener (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "Range Gate", meta = (ClampMin = "0.0"))
	float RangeGateHysteresis = 200.0f;

	virtual FName GetCategoryName() const override;
};
