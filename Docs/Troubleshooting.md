# Troubleshooting

Always start both instances with `-log`, then run `MS.CheckConfig` and `MS.Diag`.
Everything the plugin does is logged; `MS.Log 100` prints the history in the in-game console.

## LAN: `MS.Host lan` / `MS.Find lan` / `MS.Join 0` does nothing

Before 1.1 this failed **silently**: results and errors only went to the log file, `MS.Join` printed nothing when it had no
results, and a failed attempt left a leftover online session that made every retry fail. Work through it with the new tooling:

1. `MS.Find lan` takes about 3 s (the engine's LAN query timeout). Wait for **"Find complete: N lobby(ies)"** on screen.
   `MS.Join` now refuses while the search runs; `MS.FindJoin lan` waits for you.
2. Read the Find output:
   * `nobody answered`: the host's LAN beacon did not reach you. Check the host really logged "LAN lobby is being advertised",
     both machines are on the same subnet, and the firewall allows UDP broadcast for the game.
   * `result #N REJECTED - different game or version`: GameId / GameVersion differ (Project Settings -> Session).
   * `REJECTED - lobby is full / not advertised`: self-explanatory.
3. If discovery works but joining fails, check the log lines **"Join: JoinSession callback"**, **"Join OK. Connecting to ... ClientTravel ..."**
   and then **"Network failure: ..."** / **"Travel failure: ..."**. The host logs **"A client is connecting from ..."** (or the reason for a rejection).
4. `MS.Connect <hostIP>` bypasses discovery entirely. If that connects but `MS.Join` does not, discovery or the advertised address is the problem
   (compare with `MS.Diag` on the host: "resolved connect string"). If neither connects, the issue is the net driver / firewall / port.
5. `MS.Diag` prints the configured `NetDriverDefinition`s and the active driver. The project is expected to use `SteamSocketsNetDriver`
   with an `IpNetDriver` fallback. The plugin tags LAN travel with `?bIsLanMatch=1` on both host and client, which is what the Steam net
   drivers key off to use plain IP. **This part is unverified here** (see `Plans/Roadmap.md`, "LAN transport"): if the log shows the
   connection being attempted over a `steam.` address or the driver failing to listen, that is the cause.

### Two instances on one PC
Both instances run as the same Steam user. Steam lobbies cannot be joined by the account that owns them, and invites to yourself do nothing, so
test internet lobbies with two accounts/machines. LAN should work on one PC but both processes then share one Steam id, which can confuse
anything that keys on it (voice, mute list, player state ids). If a LAN join connects and then drops, suspect this first.

## Nothing works / `NoOnlineSubsystem`
* Steam client not running, or a packaged build started outside Steam without an app id. `MS.CheckConfig` reports a missing
  `SteamDevAppId` / `steam_appid.txt`.
* `MS.Status` shows `OSS=` the active online subsystem. If it is not `STEAM`, internet lobbies cannot work.

## Internet lobbies are not found
* 1.0 never set the Steam "search lobbies" flag, so Steam searched the dedicated-server list and never saw listen-server lobbies. Fixed in 1.1.
* Same Steam region/download region, same GameId + GameVersion, lobby visibility Public.

## "Already in session" / "session already exists" on the second attempt
Fixed in 1.1: leftover online sessions after failed joins, dropped connections and travel failures are now destroyed automatically
(you will see "A leftover online session exists ... Destroying it first").
