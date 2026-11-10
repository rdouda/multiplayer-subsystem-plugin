// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Core/MSTypes.h"
#include "MSVoiceSubsystem.generated.h"

class APlayerState;
class IOnlineVoice;
class UMSSessionSubsystem;
class UWorld;

/**
 * Local voice runtime for player 0.
 *
 * Applies the input mode (open mic / push to talk), the mic threshold and the
 * remote output volume, keeps the local talking state for the UI, and exposes
 * local-only per-player mute. Preferences are saved per machine in
 * GameUserSettings.ini, section [MultiplayerSubsystemVoice].
 */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API UMSVoiceSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	// -----------------------------------------------------------------------
	// Blueprint API
	// -----------------------------------------------------------------------

	/** Enabled in settings, voice interface present, and in an online lobby. */
	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Voice")
	bool IsVoiceAvailable() const;

	/** Human readable reason IsVoiceAvailable is false, or an empty string when voice is available. */
	FString GetVoiceUnavailableReason() const;

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Voice")
	EMSVoiceInputMode GetInputMode() const { return CurrentInputMode; }

	/** Refused when bAllowPlayerInputModeChange is off. Saved per machine. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Voice")
	bool SetInputMode(EMSVoiceInputMode NewMode);

	/** Push-to-talk press. No-op in open mic. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Voice")
	void StartTalking();

	/** Push-to-talk release. No-op in open mic. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Voice")
	void StopTalking();

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Voice")
	bool IsLocalTalking() const { return bLocalTalking; }

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Voice")
	float GetMicThreshold() const { return MicThreshold; }

	/** Open-mic sensitivity, linear amplitude 0..1. Saved per machine. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Voice")
	void SetMicThreshold(float NewThreshold);

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Voice")
	float GetOutputVolume() const { return OutputVolume; }

	/** Remote voice volume, 0..4. Saved per machine. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Voice")
	void SetOutputVolume(float NewVolume);

	/** Local-only mute. Does not affect other players. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Voice")
	void SetPlayerVoiceMuted(APlayerState* PlayerState, bool bMuted);

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Voice")
	bool IsPlayerVoiceMuted(APlayerState* PlayerState) const;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Voice")
	FMSOnVoiceInputModeChanged OnInputModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer Subsystem|Voice")
	FMSOnLocalTalkingChanged OnLocalTalkingChanged;

	// -----------------------------------------------------------------------
	// Internal hooks (voice talker component / range gate)
	// -----------------------------------------------------------------------

	/** Re-applies the local talker, input mode and threshold. Called on handshake and new maps. */
	void RefreshVoiceState();

	/** Global scale applied to every remote voice. */
	float GetEffectiveOutputVolume() const { return FMath::Clamp(OutputVolume, 0.0f, 4.0f); }

	/** True unless a local audibility override says otherwise. */
	bool IsPlayerAudible(const APlayerState* PlayerState) const;

	/** Local audibility override, used by the optional range gate. */
	void SetPlayerVoiceAudible(APlayerState* PlayerState, bool bAudible);

	/** Re-applies the current volume/mute to every active talker. */
	void RefreshAllTalkerVolumes();

private:
	TSharedPtr<IOnlineVoice> GetVoiceInterface() const;
	UMSSessionSubsystem* GetSessionSubsystem() const;

	void LoadPreferences();
	void SavePreferences();
	void ApplyInputMode();
	void StopVoiceInternal();
	void UpdateLocalTalking();
	FString GetPlayerKey(const APlayerState* PlayerState) const;

	EMSVoiceInputMode CurrentInputMode = EMSVoiceInputMode::OpenMic;
	float MicThreshold = 0.08f;
	float OutputVolume = 1.0f;

	bool bLocalTalking = false;
	bool bLocalTalkerRegistered = false;
	bool bNetworkedVoiceActive = false;
	bool bPushToTalkHeld = false;
	bool bInitialized = false;

	TSet<FString> MutedPlayerKeys;
	TMap<FString, bool> PlayerAudibleOverrides;
};
