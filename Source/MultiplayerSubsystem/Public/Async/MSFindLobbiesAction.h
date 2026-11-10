// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Core/MSTypes.h"
#include "MSFindLobbiesAction.generated.h"

class UMSSessionSubsystem;

/** Blueprint node: Find Lobbies. Completes exactly one pin, including on timeout. */
UCLASS()
class MULTIPLAYERSUBSYSTEM_API UMSFindLobbiesAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMSFindLobbiesSuccessPin, const TArray<FMSLobbyInfo>&, Results);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMSFindLobbiesFailurePin, EMSSessionError, Error, FText, ErrorMessage);

	UPROPERTY(BlueprintAssignable)
	FMSFindLobbiesSuccessPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FMSFindLobbiesFailurePin OnFailure;

	/** Searches Steam (or LAN). An empty Results array on success is not an error. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer Subsystem",
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Find Lobbies"))
	static UMSFindLobbiesAction* FindLobbies(UObject* WorldContextObject, FMSSearchSettings Settings);

	virtual void Activate() override;

private:
	UFUNCTION()
	void HandleFindComplete(bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error);

	void Complete(bool bSuccess, const TArray<FMSLobbyInfo>& Results, EMSSessionError Error);

	UPROPERTY()
	TObjectPtr<UMSSessionSubsystem> SessionSubsystem;

	FMSSearchSettings CachedSettings;
	bool bCompleted = false;
};
