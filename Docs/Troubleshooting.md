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
5. `MS.Diag` prints the configured `NetDriverDefinition`s, the active driver and **"LAN transport override"**. LAN runs on the
   **Null** online subsystem (see `README.md` -> Backends), so it does not need Steam. The transport is separate from the OSS:
   for a LAN session the plugin swaps `GameNetDriver` to `/Script/MultiplayerSubsystem.MSLANNetDriver` on both host and client,
   which always uses the platform UDP socket subsystem. The configured `SteamSocketsNetDriver` is restored afterwards. If the
   host and client logs show different drivers (for example one says `SteamSocketsNetDriver`, the other `IpNetDriver` or
   `MSLANNetDriver`), they are speaking different protocols and you will see PacketHandler parsing errors or a timeout.
   If the log shows the connection being attempted over a `steam.` address, the `?bIsLanMatch=1` tag was lost.

6. `MS.Find` / `MS.Results` show one row per **session id and per connect address** (`addr=`). Duplicate beacon answers are collapsed
   (lowest ping wins) and your own lobby is never listed. Two remaining rows with different addresses are different hosts. A row with a
   high ping (hundreds of ms on a LAN) or `0/N` players is usually a stale beacon from an old or backgrounded host process: close stray
   game processes (Task Manager), or use `MS.Connect <hostIP>` to bypass discovery. `MS.Log 100` lists every raw result and why it was kept or dropped.

7. **Check the transport.** About two seconds after `MS.Host lan` / `MS.Join`, both logs must contain
   `LAN transport check OK: ... driver is MSLANNetDriver`. If one side prints `LAN transport check FAILED`, something replaced
   `GameNetDriver` after the plugin set it; if neither prints anything, **Use Plain Ip Driver For LAN** is off or the session is not LAN.
   `incompatible_unique_net_id` in a LAN join means the host is not running `AMSGameMode` (a custom GameMode must call the plugin's PreLogin
   or skip the engine's id-type check itself for LAN).
   On the host, `LAN host is listening on port X but advertising ...` means another process already owns the advertised port
   (start each host with a different `-port=`).

### Two instances on one PC
LAN uses the Null subsystem, so both instances work with the same (or no) Steam account and get distinct Null ids. Internet lobbies
still cannot be joined by the Steam account that owns them, so test those with two accounts/machines. If a LAN join connects and then
drops, check the host log for **"A client is connecting from ..."** and the client log for **"Network failure"**.

## Nothing works / `NoOnlineSubsystem`
* Internet play: Steam client not running, or a packaged build started outside Steam without an app id. `MS.CheckConfig` reports a
  missing `SteamDevAppId` / `steam_appid.txt`.
* `MS.Status` shows the active backend and `OSS=`. For internet lobbies it must be `Steam` / `STEAM`. For LAN it should be `Null` /
  `NULL`; LAN does not need Steam. Use `MS.Backend` to see configured vs active.

## Internet lobbies are not found
* Check the Backend setting / `MS.Backend`: internet play needs `Auto` (with Steam as the default online subsystem) or `Steam`.
  `Null` only works on the local machine/network.
* 1.0 never set the Steam "search lobbies" flag, so Steam searched the dedicated-server list and never saw listen-server lobbies. Fixed in 1.1.
* Same Steam region/download region, same GameId + GameVersion, lobby visibility Public.

## "Already in session" / "session already exists" on the second attempt
Fixed in 1.1: leftover online sessions after failed joins, dropped connections and travel failures are now destroyed automatically
(you will see "A leftover online session exists ... Destroying it first").
