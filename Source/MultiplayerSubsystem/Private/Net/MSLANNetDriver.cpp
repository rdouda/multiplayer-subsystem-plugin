// Copyright (c) 2026. All rights reserved.

#include "Net/MSLANNetDriver.h"

#include "SocketSubsystem.h"

ISocketSubsystem* UMSLANNetDriver::GetSocketSubsystem()
{
	// Deliberately bypass ISocketSubsystem::Get(): SteamSockets makes itself the default
	// socket subsystem, which is not compatible with a host that has no Steam OSS.
	return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
}
