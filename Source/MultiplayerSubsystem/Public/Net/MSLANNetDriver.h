// Copyright (c) 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IpNetDriver.h"
#include "MSLANNetDriver.generated.h"

class ISocketSubsystem;

/**
 * Plain-IP net driver used for LAN sessions.
 *
 * UIpNetDriver uses ISocketSubsystem::Get(), which is the SteamSockets subsystem whenever
 * the Steam OSS is running and bUseSteamNetworking is enabled. A host without Steam listens
 * on a plain UDP socket, so a Steam-enabled client would send SteamSockets packets at it and
 * the connection would fail with PacketHandler errors / a timeout.
 *
 * Always using the platform socket subsystem keeps both sides on the same plain IP transport,
 * with or without Steam. Internet sessions keep the configured GameNetDriver (normally
 * SteamSocketsNetDriver); the session subsystem only installs this driver for LAN (and can be told
 * not to via UMSSessionSettings::bUsePlainIpDriverForLAN).
 *
 * Deliberately overrides nothing but GetSocketSubsystem(): the Init/Tick signatures change between
 * engine versions, and this one function is all that differs from UIpNetDriver.
 */
UCLASS(Transient, config = Engine)
class MULTIPLAYERSUBSYSTEM_API UMSLANNetDriver : public UIpNetDriver
{
	GENERATED_BODY()

public:
	virtual ISocketSubsystem* GetSocketSubsystem() override;
};
