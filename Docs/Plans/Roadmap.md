# Roadmap: missing or helpful features

Ordered roughly by how soon a real project will miss them. Items marked **(verify)** rest on engine behaviour that could not be tested while writing 1.1.

## P0: needed to trust the plugin
1. **LAN transport (verify).** LAN currently relies on the Steam net driver switching to plain IP when the URL has `?bIsLanMatch=1`.
   Needs a real two-machine test and a decision: either keep that, or add a dedicated `LANNetDriver` definition (IpNetDriver) plus a
   way to select it per connection, or run LAN sessions through `OnlineSubsystemNull` explicitly instead of the default (Steam) OSS.
   Also: the LAN address comes from the OSS's "local host address", which can pick a virtual adapter (VPN / Hyper-V / WSL). Add a setting to pin the adapter / IP.
2. **Automated tests.** UE Automation / Functional tests for the state machine (host, find, join, leave, cancel, stale cleanup) against a fake session interface. The 1.1 fixes are all state-machine bugs that a test would have caught.
3. **Packaged-build checklist.** Editor-time validation (a small Editor module) for: maps cooked, Steam ini keys, net driver definitions, `steam_appid.txt`, voice flags. Today it is a runtime warning only.
4. **Reconnect.** Re-joining the same lobby after a dropped connection (remember the last lobby, offer `Rejoin Last Lobby`).

## P1: gameplay flow
5. **Lobby metadata.** Arbitrary key/value pairs on the lobby (map, game mode, difficulty) written at host time, changeable by the host, filterable in search. `FMSLobbyInfo` only has name / counts today.
6. **Search filters, sorting and paging** (`FMSSearchSettings`): by name, free slots, max ping, phase; stable sort; "refresh" that keeps the selection.
7. **Host settings changeable at runtime**: lobby name, max players, password.
8. **Password-protected lobbies** (hashed on the server, checked in `PreLogin`).
9. **Ban list** (a kick is not a ban today), per-session and optionally persisted by unique id.
10. **Host migration or graceful end**: when the host leaves, clients get `HostClosed`; optionally promote another player.
11. **Team / slot assignment** in the lobby (teams, spectators, colour) with host-controlled changes.
12. **Lobby chat** (text), with the same mute list as voice.
13. **Start countdown** driven by the ready check (cancel if someone un-readies).

## P2: polish
14. **Invite flow**: invite specific friends from code, "friends playing this game" list, accept/decline UI events; join-in-progress policy (late join into `InGame`).
15. **Ping and region**: measure ping to the host after join, show region in the browser.
16. **Voice**: per-player volume sliders, push-to-talk keybinding helper, team-only channels, speaking indicator events for UI, device selection.
17. **Rich presence text** localisation and a customisable format (`In Lobby (2/4)` is hard-coded).
18. **Blueprint-friendly status events**: one `OnSessionStateChanged(State)` instead of many flags.
19. **Log sinks**: optional file/telemetry sink for `MSLog`; per-category verbosity switches.
20. **Dedicated server support** (`bIsDedicated`, no local player, server browser rows without a host player).
21. **Steam Datagram Relay / P2P options** exposed as settings (relay vs direct, `bAllowP2PPacketRelay`).

## Known limitations today
* Only one session at a time (`GameSession`); no party session alongside the game session.
* A joiner's lobby info is a snapshot from the search; it does not refresh player counts until the client has connected.
* `ESMFriendJoinState::NotSteam` (now `EMSFriendJoinState::NotSteam`) is Steam-specific wording; rename when a second backend lands.
* The debug console commands are compiled out of Shipping builds by design.
