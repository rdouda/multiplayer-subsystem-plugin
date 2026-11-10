// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Core/MSTypes.h"
#include "MSJoinLobbyAction.generated.h"

class UMSSessionSubsystem;

/** Blueprint node: Join Lobby. Completes exactly one pin. */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API UMSJoinLobbyAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSJoinLobbySuccessPin, FMSLobbyInfo, LobbyInfo);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSJoinLobbyFailurePin, EMSSessionError, Error, FText, ErrorMessage);

	UPROPERTY(BlueprintAssignable)
	FMSJoinLobbySuccessPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FMSJoinLobbyFailurePin OnFailure;

	/** Validated join. Leaves any current session first, then ClientTravels. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem",
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Join Lobby"))
	static UMSJoinLobbyAction* JoinLobby(UObject* WorldContextObject, FMSLobbyInfo LobbyInfo);

	virtual void Activate() override;

private:
	UFUNCTION()
	void HandleJoinComplete(FMSJoinResult Result);

	void Complete(bool bSuccess, EMSSessionError Error, const FMSLobbyInfo& LobbyInfo);

	UPROPERTY()
	TObjectPtr<UMSSessionSubsystem> SessionSubsystem;

	FMSLobbyInfo CachedLobbyInfo;
	bool bCompleted = false;
};
