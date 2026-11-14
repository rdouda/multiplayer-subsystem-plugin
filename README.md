# Multiplayer Subsystem

A drop-in Unreal Engine 5.8 plugin that gives any C++ or Blueprint project
**menu → lobby → game map(s) → lobby** multiplayer in minutes, with LAN and
Steam backends, a host-only START flow, an optional voice layer, and a
`MS.*` console toolbox for fast iteration.

The full specification lives in [Docs/DESIGN.md](Docs/DESIGN.md).

---

## Why

Multiplayer prototyping in Unreal usually means writing the same session,
lobby, ready-check and travel plumbing on every project. This plugin ships
that plumbing — server-authoritative, Blueprint-friendly, and driven entirely
by Project Settings — so you can go from an empty project to two players
standing in a lobby in under ten minutes.

## What you get

- **`UMultiplayerSubsystem`** (`UGameInstanceSubsystem`) with a full Blueprint
  API: host / find / join / leave / kick / visibility / travel.
- **Three connection modes** selectable per call or in config:
  - **LAN / Null** — no Steam needed (non-Shipping builds only)
  - **LAN / Steam** — Steam identity + plain-IP transport (works in Shipping)
  - **Internet / Steam** — Steam lobbies
- **One `AMSGameMode`** with a `MapRole` enum (`Menu`, `Lobby`, `Game`,
  `None`) plus thin `AMS*` base classes for GameState, PlayerState,
  PlayerController, Character, Spectator, GameSession and GameInstance.
- **Match config** — a map plus an opaque JSON string chosen by the host in
  the lobby, committed by the server, replicated to every client in the game
  map, and preserved across seamless travel.
- **Optional voice** (`bEnableVoice`) — proximity, global, team, and
  walkie-talkie radio; Native (engine VoIP) and Custom (Opus over RPC)
  backends; one config switch removes it from the runtime entirely.
- **Optional Steam social** (`bEnableSocial`) — own Steam name and avatar,
  friends list with presence, invite a friend, join a friend who is playing
  the game.
- **Dev toolbox** — `MS.*` console commands, one logging helper that drives
  screen + log + history, a live status overlay, `MS.CheckConfig`, and
  `-MSAuto*` command-line flags. All compiled out of Shipping.

## Connection modes

| Mode | OSS | Transport | Discovery | Steam? | Shipping? |
|---|---|---|---|---|---|
| LAN / Null | Null | Plain UDP/IP | LAN beacon | No | No (non-Shipping only) |
| LAN / Steam | Steam | Plain UDP/IP | LAN beacon | Yes | **Yes** |
| Internet / Steam | Steam | SteamSockets | Steam lobbies | Yes | Yes |

`LanBackend=Auto` (the default) resolves to **Null in non-Shipping builds**
— so two instances on one PC can host and join with no accounts — and to
**Steam in Shipping**, where the Null OSS is compiled out.

## The flow in one picture

```
Menu → (host or join) → Lobby (ready, pick map + JSON) → [host presses START] → Game → Lobby → …
```

- Only the **host** starts a match. No countdown, no auto-start.
- Late joiners during a match enter as **spectators** and become normal
  players at the next lobby.
- Lobby ↔ game can loop any number of times without leaking state or
  getting stuck "busy".

## Requirements

- **Unreal Engine 5.8.**
- A C++ project (the plugin is C++; Blueprint-only projects can consume it
  once it has been compiled).
- For Steam modes: the `OnlineSubsystemSteam` and `SteamSockets` plugins
  enabled, and Steam running. App ID 480 (Spacewar) is fine for testing.
- For LAN / Null: nothing extra — no Steam, no accounts, no packaging.

## Installation

```
YourProject/
└─ Plugins/
   └─ MultiplayerSubsystem/
      ├─ MultiplayerSubsystem.uplugin
      └─ Source/ …
```

1. Copy the `MultiplayerSubsystem` folder into `Plugins/`.
2. Enable the plugin in the editor and restart.
3. Open *Project Settings → Game → &lt;Project&gt; Network* and fill in the
   map paths. See [Docs/QUICKSTART.md](Docs/QUICKSTART.md) for a ten-minute
   walkthrough.

## Documentation

| Document | Contents |
|---|---|
| [Docs/DESIGN.md](Docs/DESIGN.md) | Full design specification: architecture, flows, settings, backend, voice, commands, tests, verification checklist. |
| [Docs/QUICKSTART.md](Docs/QUICKSTART.md) | Ten-minute setup for a new project. |
| [Docs/COMMANDS.md](Docs/COMMANDS.md) | Every `MS.*` console command (generated from the registry). |
| [Docs/VERIFICATION.md](Docs/VERIFICATION.md) | Engine API assumptions checked against 5.8. |

## Non-goals (v1)

Dedicated servers, host migration, matchmaking services, party systems,
anti-cheat, UMG content in the runtime module, and any Steam feature beyond
name / avatar / friends / invites / friend-join. See Appendix D of the
design doc for the full list and for future extensions.
