// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Net/VoiceConfig.h"
#include "MSVoiceTalkerComponent.generated.h"

class APlayerState;
class APawn;
class AMSPlayerState;
class UAudioComponent;
class UMSVoiceSubsystem;
class USoundAttenuation;

/**
 * UVOIPTalker subclass that forwards talking events to the owning component so
 * proximity settings and the speaking flag can be applied.
 */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API UMSVoiceTalker : public UVOIPTalker
{
	GENERATED_BODY()

public:
	virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
	virtual void OnTalkingEnd() override;

	void SetOwningComponent(UMSVoiceTalkerComponent* InComponent) { OwningComponent = InComponent; }

private:
	TWeakObjectPtr<UMSVoiceTalkerComponent> OwningComponent;
};

/**
 * Registers a UVOIPTalker for the owning PlayerState on every machine.
 *
 * The talker is created once the PlayerState has a valid unique net ID (never
 * assumed in BeginPlay). The component keeps FVoiceSettings up to date so the
 * engine can attach the voice audio component to the speaker's pawn and apply
 * distance attenuation before playback starts.
 */
UCLASS(ClassGroup = (MultiplayerSubsystem), meta = (BlueprintSpawnableComponent))
class MULTIPLAYERSUBSYSTEM_API UMSVoiceTalkerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMSVoiceTalkerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Re-evaluates registration and proximity settings. Safe to call at any time. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem|Voice")
	void RefreshVoiceSetup();

	UFUNCTION(BlueprintPure, Category = "Multiplayer Subsystem|Voice")
	bool IsTalkerRegistered() const { return Talker != nullptr; }

	/** Called by UMSVoiceTalker. */
	void HandleTalkingBegin(UAudioComponent* AudioComponent);
	void HandleTalkingEnd();

	/** Re-applies volume/mute to the currently playing voice, if any. */
	void ApplyVolumeToActiveTalker();

private:
	void TryRegisterTalker();
	void ScheduleRetry();
	void RefreshProximity();
	void ApplyVolume();

	UFUNCTION()
	void HandlePawnSet(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn);

	APlayerState* GetOwningPlayerState() const;
	AMSPlayerState* GetOwningMSPlayerState() const;
	UMSVoiceSubsystem* GetVoiceSubsystem() const;
	bool ShouldUseProximity() const;
	USoundAttenuation* GetAttenuationAsset();

	UPROPERTY(Transient)
	TObjectPtr<UMSVoiceTalker> Talker;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeAttenuation;

	TWeakObjectPtr<UAudioComponent> CurrentAudioComponent;
	FTimerHandle RetryTimer;
	int32 RetryCount = 0;
	bool bPawnBindingDone = false;
};
