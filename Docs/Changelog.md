# Changelog

## 1.2
**Auto backend selection.** New `EMSSessionBackend` (`Auto` / `Steam` / `Null`) and a **Backend** setting in
*Multiplayer Subsystem: Session*. LAN sessions always use the engine's **Null** online subsystem, so LAN works without Steam and
two instances on one machine can host/join each other. Internet sessions use the configured backend or, for `Auto`, the project's
default online subsystem (normally Steam).

* New Blueprint API: `Get Configured Backend`, `Get Active Backend`, `Get Active Backend Name`. `FMSLobbyInfo` gained a `Backend` field.
* New `MS.Backend [auto|steam|null]` console command (shows configured/active/override or sets a runtime override).
* Session delegates are now bound per online subsystem, because LAN (Null) and internet (Steam) can both be used in one process.
* `MS.CheckConfig` is backend-aware: Steam checks only apply when internet play uses Steam; the Null subsystem is verified for LAN;
  `SteamSockets` or `IpNetDriver` are accepted for LAN/Null.
* Friends-list join, presence, visibility and the invite overlay report `NotSteam` / fail cleanly on LAN/Null lobbies.

## 1.1
**Rename:** `SteamMultiplayer`/`SM` -> `MultiplayerSubsystem`/`MS` (see `Migration.md`).

**Bug fixes**
* Internet search never set `SEARCH_LOBBIES`, so Steam searched the dedicated-server list instead of lobbies.
* A failed connect, network failure or travel failure on a client left the online `GameSession` alive, so every following Join/Host failed with *AlreadyInSession*. Stale sessions are now destroyed (also before Host and Join).
* `HandleJoinSessionComplete` marked the player "in session" before checking for a connect address / PlayerController, leaving a stuck state on failure, and leaked the online session. Both fixed.
* `Find Lobbies` called while a search was running broadcast a failure that completed the *first* caller's node. It now fails only the second caller.
* Blueprint async nodes (Host / Find / Join) were not registered with the GameInstance and could be garbage collected before their result arrived (for example when the map changed). They are now.
* Session callbacks reacted to any named session, not just `GameSession`.
* `Leave Lobby` could run its completion twice when the backend fired the destroy callback synchronously.
* The host's own *ready* flag was cleared by "reset ready after travel" although the host counts as always ready.
* Join returned `false` with no error set when another join was running; there is now `EMSSessionError::Busy`.
* `MS.Join` failed silently when the search had not finished (LAN search takes ~3 s) or had no results.

**Logging** `MS_LOG` writes to the log/terminal, a history buffer and the game screen; every host / find / join / leave step, every rejected search result (with the reason), net and travel failures, joins/leaves/rejections on the host are logged. `MS.Log`, `MS.Overlay`, `MS.Log.OnScreen`.

**Console** New: `MS.Help`, `MS.Cancel`, `MS.Results`, `MS.FindJoin`, `MS.Connect`, `MS.Menu`, `MS.Visibility`, `MS.Kick`, `MS.Players`, `MS.ReturnToLobby`, `MS.ResetReady`, `MS.Diag`, `MS.Log`, `MS.Log.Clear`, `MS.Overlay`, `MS.Voice.Volume`, `MS.Voice.Threshold`. `MS.Host` takes `name=`, `max=`, `vis=`. Output now appears in the in-game console (before it only reached the log).

**Config check** now also verifies the Steam app id and `[OnlineSubsystemSteam] bEnabled`.
