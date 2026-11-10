// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Core/MSTypes.h"
#include "MSHostLobbyAction.generated.h"

class UMSSessionSubsystem;

/** Blueprint node: Host Lobby. Completes exactly one pin. */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API UMSHostLobbyAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMSHostLobbySuccessPin);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSHostLobbyFailurePin, EMSSessionError, Error, FText, ErrorMessage);

	UPROPERTY(BlueprintAssignable)
	FMSHostLobbySuccessPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FMSHostLobbyFailurePin OnFailure;

	/** Validates settings, creates the session, then opens LobbyMap as a listen server. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem",
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Host Lobby"))
	static UMSHostLobbyAction* HostLobby(UObject* WorldContextObject, FMSHostSettings Settings);

	virtual void Activate() override;

private:
	UFUNCTION()
	void HandleHostComplete(bool bSuccess, EMSSessionError Error);

	void Complete(bool bSuccess, EMSSessionError Error);

	UPROPERTY()
	TObjectPtr<UMSSessionSubsystem> SessionSubsystem;

	FMSHostSettings CachedSettings;
	bool bCompleted = false;
};
