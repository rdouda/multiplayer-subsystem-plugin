// Copyright (c) 2026. All rights reserved.

#include "Core/MSLog.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"

namespace
{
	TAutoConsoleVariable<int32> CVarOnScreen(
		TEXT("MS.Log.OnScreen"),
		1,
		TEXT("MultiplayerSubsystem on-screen log.\n")
		TEXT("  0 = off\n")
		TEXT("  1 = important messages only (Display / Warning / Error)  [default]\n")
		TEXT("  2 = everything, including step-by-step detail"),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarScreenTime(
		TEXT("MS.Log.ScreenTime"),
		8.0f,
		TEXT("Seconds an on-screen MultiplayerSubsystem message stays visible."),
		ECVF_Default);

	constexpr int32 MaxRecentLines = 300;

	FCriticalSection RecentLock;
	TArray<FString> RecentLines;

	const TCHAR* VerbosityTag(ELogVerbosity::Type Verbosity)
	{
		switch (Verbosity)
		{
		case ELogVerbosity::Fatal:
		case ELogVerbosity::Error:   return TEXT("E");
		case ELogVerbosity::Warning: return TEXT("W");
		case ELogVerbosity::Display: return TEXT("I");
		default:                     return TEXT("D");
		}
	}
}

namespace MSLog
{
	void Emit(ELogVerbosity::Type Verbosity, const FString& Message)
	{
		{
			FScopeLock Lock(&RecentLock);
			RecentLines.Add(FString::Printf(TEXT("[%s] [%s] %s"),
				*FDateTime::Now().ToString(TEXT("%H:%M:%S")), VerbosityTag(Verbosity), *Message));

			if (RecentLines.Num() > MaxRecentLines)
			{
				RecentLines.RemoveAt(0, RecentLines.Num() - MaxRecentLines);
			}
		}

#if !UE_BUILD_SHIPPING
		const int32 Mode = CVarOnScreen.GetValueOnAnyThread();
		if (Mode <= 0 || !GEngine || !IsInGameThread())
		{
			return;
		}

		bool bShow = false;
		FColor Color = FColor::White;

		switch (Verbosity)
		{
		case ELogVerbosity::Fatal:
		case ELogVerbosity::Error:
			bShow = true;
			Color = FColor::Red;
			break;
		case ELogVerbosity::Warning:
			bShow = true;
			Color = FColor::Yellow;
			break;
		case ELogVerbosity::Display:
			bShow = true;
			Color = FColor::Cyan;
			break;
		default:
			bShow = Mode >= 2;
			Color = FColor(190, 190, 190);
			break;
		}

		if (bShow)
		{
			GEngine->AddOnScreenDebugMessage(-1, FMath::Max(1.0f, CVarScreenTime.GetValueOnAnyThread()), Color,
				FString::Printf(TEXT("[MS] %s"), *Message));
		}
#endif
	}

	void GetRecent(int32 MaxLines, TArray<FString>& OutLines)
	{
		FScopeLock Lock(&RecentLock);

		const int32 Count = FMath::Clamp(MaxLines, 0, RecentLines.Num());
		OutLines.Reset();
		for (int32 Index = RecentLines.Num() - Count; Index < RecentLines.Num(); ++Index)
		{
			OutLines.Add(RecentLines[Index]);
		}
	}

	void ClearRecent()
	{
		FScopeLock Lock(&RecentLock);
		RecentLines.Reset();
	}
}
