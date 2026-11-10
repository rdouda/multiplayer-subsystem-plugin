// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "MultiplayerSubsystem.h"

/**
 * Plugin logging. Every MS_LOG line goes to three places:
 *   1. The normal UE log (terminal when launched with -log, Saved/Logs, editor Output Log).
 *   2. A ring buffer of recent lines (dump it any time with the MS.Log console command,
 *      handy because console commands cannot show output from async events).
 *   3. The game screen (non-Shipping builds), controlled by the MS.Log.OnScreen CVar.
 *
 * Verbosity guide:
 *   Display / Warning / Error  important, shown on screen with MS.Log.OnScreen 1 (default)
 *   Log                        step-by-step detail, on screen only with MS.Log.OnScreen 2
 */
namespace MSLog
{
	/** Records the line and shows it on screen according to MS.Log.OnScreen. Game thread only for the screen part. */
	MULTIPLAYERSUBSYSTEM_API void Emit(ELogVerbosity::Type Verbosity, const FString& Message);

	/** Copies up to MaxLines of the most recent lines (oldest first). */
	MULTIPLAYERSUBSYSTEM_API void GetRecent(int32 MaxLines, TArray<FString>& OutLines);

	MULTIPLAYERSUBSYSTEM_API void ClearRecent();
}

#define MS_LOG(Verbosity, Format, ...) \
	do \
	{ \
		const FString MSLogMessage__ = FString::Printf(Format, ##__VA_ARGS__); \
		UE_LOG(LogMultiplayerSubsystem, Verbosity, TEXT("%s"), *MSLogMessage__); \
		MSLog::Emit(ELogVerbosity::Verbosity, MSLogMessage__); \
	} while (0)
