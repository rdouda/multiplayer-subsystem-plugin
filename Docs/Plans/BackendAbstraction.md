# Plan: more than one multiplayer backend (auto selection + LAN Null done in 1.2; full interface not started)

Goal: keep the public API (`UMSSessionSubsystem`, the Blueprint nodes, `FMSLobbyInfo`, delegates) and make the backend swappable:
Steam (today), EOS, Null/LAN, later platform OSSes or a custom service.

**1.2 progress:** `EMSSessionBackend` (`Auto` / `Steam` / `Null`) and the **Backend** setting exist. `Auto`/`Steam` use the
default OSS / Steam for internet sessions, LAN always selects Null, and session delegates are bound per online subsystem. This is a
pragmatic first step; the `IMSBackend` interface and capabilities below are still open.

## What is already backend-neutral
Module/class names, `FMSHostSettings`, `FMSSearchSettings`, `FMSLobbyInfo`, `FMSJoinResult`, `EMSSessionError`, the game classes, voice and the ready check.
The subsystem talks to the engine only through `IOnlineSession`, `IOnlineIdentity`, `IOnlinePresence`, `IOnlineExternalUI` obtained from `IOnlineSubsystem::Get()` (see `GetActiveOSS()` / `GetSessionInterface()` in `MSSessionSubsystem.cpp`).

## What is Steam specific and needs isolating
| Where | Steam assumption |
| --- | --- |
| `BuildSessionSettings` | `bUseLobbiesIfAvailable`, `bUsesPresence`, presence join flags |
| `FindLobbies` | `SEARCH_LOBBIES` query flag, `ViaOnlineService` lobby keys |
| `HandleJoinSessionComplete` | `?bIsLanMatch=1` travel option for the Steam net drivers, `steam.` address check |
| `PublishPresence`, `ShowInviteOverlay`, friends-list join | Steam friends / overlay |
| `RunConfigCheck` | Steam ini keys, SteamSockets net driver |
| `EMSFriendJoinState::NotSteam`, `IsSteamActive()` | naming |

## Proposed shape
1. `IMSBackend` (plain C++ interface, one instance per subsystem): `CreateSession`, `FindSessions`, `JoinSession`, `DestroySession`, `UpdateSession`,
   `ResolveConnectString`, `PublishPresence`, `ShowInviteUI`, `GetConfigWarnings`, `GetCapabilities()`.
2. `FMSBackendCapabilities` (bSupportsFriendsList, bSupportsInvites, bSupportsPresence, bSupportsLAN, bNeedsLanTravelOption, ...) so the subsystem and UI can adapt instead of testing `IsSteamActive()`.
3. Implementations: `FMSOssBackend` (generic over any `IOnlineSubsystem`, covers Null and mostly EOS), `FMSSteamBackend` (derives from it, adds the Steam lobby flags, presence, overlay), later `FMSEosBackend`.
4. Selection: setting `Backend = Auto | Steam | EOS | Null` in *Multiplayer Subsystem: Session*; `Auto` picks the default OSS. LAN always uses the Null backend, so LAN no longer depends on Steam being up.
5. `UMSSessionSubsystem` keeps the state machine (host / find / join / leave, stale cleanup, `Busy` handling) and delegates the I/O to the backend. The Blueprint API does not change.
6. Per-backend config check; the check results grouped by backend in `MS.CheckConfig`.
7. Tests: a `FMSFakeBackend` to unit test the state machine without any online service.

## Order of work
1. Extract the interface and move the existing Steam code behind it unchanged (no behaviour change, tests prove it).
2. ~~Add the Null/LAN backend and route `bIsLANMatch` to it.~~ **Done in 1.2** (LAN resolves to Null; still goes through the shared `IOnlineSession` code rather than a dedicated interface).
3. Add capabilities and update the UI-facing queries (`GetFriendJoinState`, presence). *Partially done:* `GetFriendJoinState` now returns `NotSteam` for any non-Steam active backend.
4. Add the EOS backend.
