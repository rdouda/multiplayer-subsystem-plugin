// Copyright (c) 2026. All rights reserved.

#include "Settings/MSVoiceSettings.h"

UMSVoiceSettings::UMSVoiceSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Multiplayer Subsystem Voice");
}

FName UMSVoiceSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
