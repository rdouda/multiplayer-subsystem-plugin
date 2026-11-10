// Copyright (c) 2026. All rights reserved.

#include "Settings/MSReadyCheckSettings.h"

UMSReadyCheckSettings::UMSReadyCheckSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Multiplayer Subsystem Ready Check");
}

FName UMSReadyCheckSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
