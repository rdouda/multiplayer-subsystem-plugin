# Migrating from SteamMultiplayer (`SM*`) to MultiplayerSubsystem (`MS*`)

Everything was renamed: module `SteamMultiplayer` -> `MultiplayerSubsystem`, prefix `SM` -> `MS`
(`USMSessionSubsystem` -> `UMSSessionSubsystem`, `ESMSessionError` -> `EMSSessionError`, ...), log category
`LogMultiplayerSubsystem`, API macro `MULTIPLAYERSUBSYSTEM_API`, console commands `SM.*` -> `MS.*`,
Blueprint category "Steam Multiplayer" -> "Multiplayer Subsystem".

## 1. Rebuild
Delete the plugin's old folder (and its `Binaries/` + `Intermediate/`), drop in `MultiplayerSubsystem/`, delete your project's
`Intermediate/` and `Binaries/`, regenerate project files, build. Replace `"SteamMultiplayer"` with `"MultiplayerSubsystem"` in any
`*.Build.cs` and C++ includes (`SteamMultiplayer.h` -> `MultiplayerSubsystem.h`, `Session/SMSessionSubsystem.h` -> `Session/MSSessionSubsystem.h`, ...).

## 2. Keep your Blueprints working: core redirects
Add to `Config/DefaultEngine.ini`, open the project, then re-save the affected assets and remove the redirects once everything is saved:

```ini
[CoreRedirects]
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMGameMode",NewName="/Script/MultiplayerSubsystem.MSGameMode")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMGameState",NewName="/Script/MultiplayerSubsystem.MSGameState")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMPlayerController",NewName="/Script/MultiplayerSubsystem.MSPlayerController")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMPlayerState",NewName="/Script/MultiplayerSubsystem.MSPlayerState")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMSessionSubsystem",NewName="/Script/MultiplayerSubsystem.MSSessionSubsystem")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMVoiceSubsystem",NewName="/Script/MultiplayerSubsystem.MSVoiceSubsystem")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMVoiceTalkerComponent",NewName="/Script/MultiplayerSubsystem.MSVoiceTalkerComponent")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMVoiceTalker",NewName="/Script/MultiplayerSubsystem.MSVoiceTalker")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMSessionSettings",NewName="/Script/MultiplayerSubsystem.MSSessionSettings")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMReadyCheckSettings",NewName="/Script/MultiplayerSubsystem.MSReadyCheckSettings")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMVoiceSettings",NewName="/Script/MultiplayerSubsystem.MSVoiceSettings")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMHostLobbyAction",NewName="/Script/MultiplayerSubsystem.MSHostLobbyAction")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMFindLobbiesAction",NewName="/Script/MultiplayerSubsystem.MSFindLobbiesAction")
+ClassRedirects=(OldName="/Script/SteamMultiplayer.SMJoinLobbyAction",NewName="/Script/MultiplayerSubsystem.MSJoinLobbyAction")
+StructRedirects=(OldName="/Script/SteamMultiplayer.SMHostSettings",NewName="/Script/MultiplayerSubsystem.MSHostSettings")
+StructRedirects=(OldName="/Script/SteamMultiplayer.SMSearchSettings",NewName="/Script/MultiplayerSubsystem.MSSearchSettings")
+StructRedirects=(OldName="/Script/SteamMultiplayer.SMLobbyInfo",NewName="/Script/MultiplayerSubsystem.MSLobbyInfo")
+StructRedirects=(OldName="/Script/SteamMultiplayer.SMJoinResult",NewName="/Script/MultiplayerSubsystem.MSJoinResult")
+StructRedirects=(OldName="/Script/SteamMultiplayer.SMReadySummary",NewName="/Script/MultiplayerSubsystem.MSReadySummary")
+EnumRedirects=(OldName="/Script/SteamMultiplayer.SMSessionError",NewName="/Script/MultiplayerSubsystem.MSSessionError")
+EnumRedirects=(OldName="/Script/SteamMultiplayer.SMSessionPhase",NewName="/Script/MultiplayerSubsystem.MSSessionPhase")
+EnumRedirects=(OldName="/Script/SteamMultiplayer.SMLobbyVisibility",NewName="/Script/MultiplayerSubsystem.MSLobbyVisibility")
+EnumRedirects=(OldName="/Script/SteamMultiplayer.SMJoinSource",NewName="/Script/MultiplayerSubsystem.MSJoinSource")
+EnumRedirects=(OldName="/Script/SteamMultiplayer.SMFriendJoinState",NewName="/Script/MultiplayerSubsystem.MSFriendJoinState")
+EnumRedirects=(OldName="/Script/SteamMultiplayer.SMTravelBlockReason",NewName="/Script/MultiplayerSubsystem.MSTravelBlockReason")
+EnumRedirects=(OldName="/Script/SteamMultiplayer.SMVoiceInputMode",NewName="/Script/MultiplayerSubsystem.MSVoiceInputMode")```

## 3. Move your settings
Section names in `Config/DefaultGame.ini` change with the class names:

| Old | New |
| --- | --- |
| `[/Script/SteamMultiplayer.SMSessionSettings]` | `[/Script/MultiplayerSubsystem.MSSessionSettings]` |
| `[/Script/SteamMultiplayer.SMReadyCheckSettings]` | `[/Script/MultiplayerSubsystem.MSReadyCheckSettings]` |
| `[/Script/SteamMultiplayer.SMVoiceSettings]` | `[/Script/MultiplayerSubsystem.MSVoiceSettings]` |

Otherwise LobbyMap / MainMenuMap are empty after the rename. Per-player voice preferences moved from `[SteamMultiplayerVoice]`
to `[MultiplayerSubsystemVoice]` in `GameUserSettings.ini` (they reset once).

## 4. Enum change
`EMSSessionError` gained `Busy` (before `Unknown`). Blueprint switches that use the *Unknown* pin keep working (enums are stored by name).
