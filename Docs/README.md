# Multiplayer Subsystem (prefix `MS`)

Listen-server sessions for quick multiplayer prototypes. **Steam** is the internet backend and the engine's **Null** subsystem
is used for **LAN** (and can be forced for local testing). The module and class names are backend-neutral so more backends
can be added later (see `Plans/BackendAbstraction.md`).

| Area | Classes |
| --- | --- |
| Sessions | `UMSSessionSubsystem` (GameInstance subsystem), Blueprint nodes `Host Lobby`, `Find Lobbies`, `Join Lobby` |
| Game flow | `AMSGameMode`, `AMSGameState`, `AMSPlayerState`, `AMSPlayerController` |
| Voice | `UMSVoiceSubsystem`, `UMSVoiceTalkerComponent` |
| Settings | Project Settings -> Plugins -> *Multiplayer Subsystem: Session / Ready Check / Voice* |
| Debug | `MS.*` console commands, `MSLog` (terminal + screen), `MS.Overlay` |

Other docs: `Troubleshooting.md`, `Migration.md` (coming from `SteamMultiplayer` / `SM*`), `Changelog.md`, `Plans/`.

## Backends

The **Backend** setting in *Project Settings -> Plugins -> Multiplayer Subsystem: Session* controls which online subsystem
internet sessions use:

| Backend | Internet host/find/join | LAN host/find/join |
| --- | --- | --- |
| `Auto` (default) | Project's default online subsystem (normally Steam) | **Null** |
| `Steam` | Steam | **Null** |
| `Null` | Null (local/offline testing only) | **Null** |

**LAN always uses the Null subsystem**, regardless of the setting. That is deliberate: Steam cannot join its own lobby and
needs a separate Steam account per instance, while Null LAN works without Steam and lets two instances on one machine host
and join each other. Friends-list join, presence and the invite overlay need Steam, so they are unavailable in LAN/Null lobbies.

`MS.Backend [auto|steam|null]` overrides the configured backend for the next host/find/join without editing settings
(LAN still resolves to Null). `MS.Backend` with no argument prints configured / active / override / OSS.

## Required project config (Steam internet play)

LAN and `Backend=Null` do not need any of this. Internet play through Steam does.

`Config/DefaultEngine.ini` (the plugin checks all of this at startup, see `MS.CheckConfig`):

```ini
[OnlineSubsystem]
DefaultPlatformService=Steam
bHasVoiceEnabled=true

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480
SteamAppId=480

[Voice]
bEnabled=true

[/Script/Engine.GameEngine]
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")

[/Script/SteamSockets.SteamSocketsNetDriver]
NetConnectionClassName="/Script/SteamSockets.SteamSocketsNetConnection"
```

Then set **LobbyMap** and **MainMenuMap** in *Multiplayer Subsystem: Session* and use `AMSGameMode` (or a Blueprint child) in your lobby/game maps.

## Logging

Every plugin message goes to three places:

1. **Terminal / log file** under `LogMultiplayerSubsystem`. Launch a packaged game with `-log` to get the terminal window.
2. **Game screen** (non-Shipping). `MS.Log.OnScreen 0|1|2` = off / important (default) / everything. `MS.Log.ScreenTime <sec>`.
3. **History buffer**: `MS.Log [n]` prints the last lines in the in-game console. Console commands cannot show output of async events (find results, join progress), so use this.

`MS.Overlay 1` shows a live status panel (backend, lobby, busy flags, search, net driver).

Colours on screen: cyan = info, yellow = warning, red = error, grey = detail (level 2 only).

## Console commands

Open the console with `~` in a Development/Debug/Test build. Commands are compiled out of Shipping. `MS.Help` prints this list.

| Command | What it does |
| --- | --- |
| `MS.Host [lan] [name=<text>] [max=<n>] [vis=public\|friends\|invite]` | Host a lobby and open LobbyMap as listen server. Quote names with spaces: `"name=My Lobby"` |
| `MS.Find [lan]` | Search for lobbies (async, progress on screen) |
| `MS.Cancel` | Cancel a running search |
| `MS.Results` | List lobbies from the last search |
| `MS.Join [index]` | Join result *index* (default 0). Refuses while a search is still running and tells you so |
| `MS.FindJoin [lan] [index]` | Search, then join automatically when the search completes |
| `MS.Connect <ip[:port]> [nolan]` | Skip discovery, connect straight to an address. Separates "discovery is broken" from "the connection is broken" |
| `MS.Leave` / `MS.Menu` | Leave the lobby / go to the main menu map |
| `MS.Visibility <public\|friends\|invite>` | Host only |
| `MS.Joinable <0\|1>` | Host only: open/close the lobby |
| `MS.Kick <name\|#index> [reason]` | Host only |
| `MS.Players` | Players in this world with ping, id, host and ready flags |
| `MS.Ready [0\|1]`, `MS.ReadyStatus`, `MS.ResetReady` | Ready check |
| `MS.Travel <MapPath> [force]`, `MS.ReturnToLobby` | Host only: seamless travel |
| `MS.Invite`, `MS.Presence` | Steam invite overlay / presence |
| `MS.Status` | One-screen state summary |
| `MS.Diag` | Deep dump: login state, raw online session, resolved connect string, net driver definitions, config warnings |
| `MS.CheckConfig` (`MS.Config`) | Configuration problems |
| `MS.Backend [auto\|steam\|null]` | Show or override the backend for the next host/find/join (LAN always uses Null) |
| `MS.Log [n]`, `MS.Log.Clear`, `MS.Log.OnScreen`, `MS.Log.ScreenTime`, `MS.Overlay` | Logging controls |
| `MS.Voice.Status / Mode / Talk / Mute / Volume / Threshold` | Voice |

### Two-instance LAN test

```
Instance A:  MS.Host lan
Instance B:  MS.FindJoin lan          (or: MS.Find lan, wait for "Find complete", MS.Join 0)
Fallback:    MS.Connect <A's IP>      (127.0.0.1 on the same PC)
```
Start both with `-log` so you can read the terminal. LAN uses the Null backend, so Steam does not have to be running and
both instances can use the same Steam account (or none). See `Troubleshooting.md` if it does not connect.
