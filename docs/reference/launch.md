---
title: Launching the game
description: How Swofty Launcher for F1 25 starts a modded session on Windows and Linux, where the logs go, and what happens when the game exits.
---

# Launching the game

Modded sessions run offline. To get set up, follow [Windows](/guide/windows) or [Linux and Steam Deck](/guide/linux).

## Steam

F1 25 from Steam needs the Steam client running and signed in. When you select **Play** or run `swofty run` while Steam is closed, the launcher starts Steam in the background, waits for it to sign in, then launches the game. The status line next to **Play** reads **Starting Steam…**, then **Waiting for Steam to sign in…**.

When Steam doesn't come up, the launch stops with one of these messages:

| Message | What to do |
|---|---|
| `Steam didn't start within 90 s; open Steam and press Play again` | Open Steam yourself, then select **Play** again |
| `Steam is open but not signed in; sign in to Steam and press Play again` | Sign in to Steam, then select **Play** again |
| `could not start Steam (...); open Steam and press Play again` | Steam wasn't found or exited with an error. Open Steam yourself, then select **Play** again |

On Linux, when Steam runs but never reports signing in, the launch goes ahead after 90 seconds.

To turn this off, select the arrow next to **Play** and clear **Start Steam automatically**, or set `"start_store": false` under `"launch"` in `$DATA/config.json` (`"start_steam"` still works). For a single launch, run `swofty run --no-start-store`. With it off, the launch goes ahead whether Steam runs or not. If Steam is open and the launch still waits for it, turn the setting off and tell the developers with [Send Logs to Developers](/using/send-logs).

The launcher doesn't start Steam for a game folder outside a Steam library, or in Steam Deck Game Mode.

| | Running and signed in | Started with |
|---|---|---|
| Windows | `HKCU\Software\Valve\Steam\ActiveProcess`: `pid` is a running `steam.exe` and `ActiveUser` isn't 0 | `SteamExe` or `SteamPath` from `HKCU\Software\Valve\Steam`, with `-silent`; otherwise `steam://open/main` |
| Linux | `~/.steam/steam.pid` or `registry.vdf` names a running `steam` process. It's signed in when `ActiveUser` in `registry.vdf` isn't 0, or when `config/loginusers.vdf` changed after Steam started | `steam -silent` |
| Flatpak Steam | The same files under `~/.var/app/com.valvesoftware.Steam/` | `flatpak run com.valvesoftware.Steam -silent` |

`swofty run` and the launcher's log (`$DATA/state/launcher-run.log`) show each step on `[steam]` lines:

```text
[steam] detected: not running
[steam] starting Steam: /usr/bin/steam -silent
[steam] waiting for sign-in (pid 4242, ActiveUser 0) at 3.5 s
[steam] ready after 9.0 s (pid 4242, ActiveUser 12345678)
```

`swofty steam` shows the same check on its own, and `swofty steam --start` runs the start and the wait without launching the game. `swofty doctor` shows it in its `steam client` row.

## EA app

From 1.3.1, **Play** on an [EA app copy](/using/game-copies) runs in this order:

1. The launcher starts the EA app when it's closed, waits up to 90 seconds for `EADesktop.exe`, then waits 10 seconds more. On Windows it runs `EALauncher.exe`. On Linux it runs `ea_launch_command` without the link. The status line reads **Starting the EA app…**.
2. It installs your profile and offline play into the game folder.
3. It hands the EA app the launch link `origin2://game/launch?offerIds=<ids>&autoDownload=1`, with `&cmdParams=-vr` for **Play in VR**. The IDs are the `<contentID>` values in the game folder's `__Installer/installerdata.xml`. The status line reads **Starting F1 25 through the EA app…**.
4. It waits for F1 25 to start and close, including a second copy the EA app starts, then rolls the game folder back.

When F1 25 hasn't started 3 minutes after the link, the launcher opens the EA app and the status line reads **Press Play on F1 25 in the EA app**. Pressing **Play** there starts the modded session. After 10 more minutes without the game, the launch stops and the game folder rolls back.

| | Running | Launch link handed to |
|---|---|---|
| Windows | An `EADesktop.exe` process | The `origin2` handler in `HKEY_CLASSES_ROOT\origin2\shell\open\command`; otherwise `EALauncher.exe` from `HKEY_LOCAL_MACHINE\SOFTWARE\Electronic Arts\EA Desktop` or `Program Files\Electronic Arts\EA Desktop\EA Desktop` |
| Linux | An `EADesktop.exe` process under Wine | The `ea_launch_command` setting; see [The EA app copy on Linux](/using/game-copies#the-ea-app-copy-on-linux) |

On Windows, `ea_launch_command` replaces the handler when it's set. The EA app registers the handler with an unquoted path, and the launcher runs everything up to `.exe` as the program.

When the EA app doesn't come up, the launch stops with one of these messages:

| Message | What to do |
|---|---|
| `the EA app didn't start within 90 s; open the EA app and press Play again` | Open the EA app yourself, then select **Play** again |
| `could not start the EA app (...); open the EA app and press Play again` | Open the EA app yourself, then select **Play** again |
| `the EA app is closed and starting it is turned off ...` | Open the EA app, or turn **Start the EA app automatically** back on |
| `the EA app was not found on this computer ...` | Install or repair the EA app |
| `... Swofty starts it through the EA app with the launch.ea_launch_command setting ...` | Set `ea_launch_command` on Linux |

`swofty run` and the launcher's log show each step on `[ea app]` lines:

```text
[ea app] detected: closed; starting the EA app
[ea app] running after 6.5 s
[ea app] asking the EA app to launch F1 25: origin2://game/launch?offerIds=16425845,16425845_oa&autoDownload=1
[ea app] F1 25 started (pid 5120)
```

`swofty doctor` shows whether the EA app runs and how the link reaches it, in its `ea app` and `ea app hand-off` rows. `swofty run --direct` starts an EA app copy through Proton instead, without the EA app. `swofty game launch` hands the EA app the launch link for the copy as it's installed, without your profile or offline play.

## Game files changed outside Swofty

Before a modded session builds anything, the launcher checks the Steam copy against Steam's list of the files it installed. When files were changed outside Swofty, usually by a mod copied into the game folder by hand, F1 25 is not started and nothing in the game folder is changed. [Checking against Steam](/reference/restore#checking-against-steam) lists what the check compares.

The launcher opens **Your game files have been edited**. It says how many files changed, names the first six, and shows the fix:

1. Select **Verify game files in Steam**. It opens `steam://validate/3059520`, Steam's **Verify integrity of game files** for F1 25, which puts Steam's copies back. In Steam, it's under right-click F1 25 > **Properties** > **Installed Files**.
2. When Steam has finished, select **Play** again.

**What to do** on the play bar reopens the modal. An EA app copy is not checked against a list of the EA app's files; see [Repair the game files](/using/game-copies#repair-the-game-files).

`swofty run` and `swofty apply` stop with the same fix and the same link:

```text
swofty: common.erp, ferrari.erp, force_india.erp and 49 more files are not the copy Steam installed, usually after a mod was copied into the game folder by hand; nothing in the game folder was changed; verify the game files in Steam to put them back (open steam://validate/3059520, or F1 25 > Properties > Installed Files > Verify integrity of game files), then try again
```

## Linux: the Steam wrapper

Set Steam's launch option to `swofty run -- %command%` so its Play button starts Swofty Launcher for F1 25. Add `--use-overlayfs` after `run` to [run the game from an OverlayFS mount](#overlayfs-sessions) instead of copying files. The launcher uses your configured Proton version and records session logs.

To go back to Steam's normal launch, clear the option.

Without the wrapper in the launch option, the launcher's **Play** and `swofty run --direct` start Proton themselves. Proton reads wheels and controllers through SDL2. When the system has no 64-bit SDL2, as under NixOS `steam-run`, the launcher links the copy from Steam's runtime into `$DATA/state/runtime-libs` and adds it to the game's library path. `swofty run` then prints `controllers: this system has no SDL2`.

## OverlayFS sessions

On Linux, `swofty run --use-overlayfs -- %command%` runs the game from an OverlayFS mount instead of copying the profile into the game folder. Everything else about a launch is the same: Steam's **Play**, the launcher's **Play**, `--vr`, **Apply & restart** and `swofty run` from a terminal all work.

The wrapper chooses OverlayFS when the launch asks for it, Proton is in the Steam command, the game is a Steam copy, the launch applies the profile, and the game folder holds no `swofty apply` install. Otherwise it [falls back](#falling-back-to-copying).

**Layers.** The mount stacks three layers, highest first:

| Layer | Folder | Contents |
|---|---|---|
| Common | `$DATA/overlay/common/` | The offline play stub (as `EAAntiCheat.GameServiceLauncher.exe`), its hook, `version.dll` and the bundled `swofty/` loader folder. Shared by every profile. A profile that plays without addons uses `$DATA/overlay/common-stub/` |
| Profile | `$DATA/profiles/<profile>/overlay/` | The profile's replaced and added game files, its addons' native DLLs, assets, UI and `swofty/settings.json`. |
| Game folder | the Steam folder | Read-only: the original files |

Each layer is a plain copy of the files from the profile build (the `cache/overlay/<profile>/<hash>` folder is built as before). A stamp beside it (`overlay.stamp`, `common.stamp`) records what it was built from, so a launch with nothing changed copies nothing. A changed build replaces the whole layer by renaming a new one into place, so a crash leaves at most a `.new` or `.old` folder that the next launch removes. A launch that doesn't have the room for a layer falls back.

**Mount.** `swofty run` starts `swofty overlay-exec`, which makes a user and mount namespace that maps your own user and group to themselves, mounts the layers over `$DATA/state/overlay/game/` (never over the real game folder) and runs Proton there. The upper dir is `$DATA/profiles/<profile>/writes/` and the work dir `$DATA/profiles/<profile>/workdir/`. Both stay between sessions and aren't cleaned. The game starts from `$DATA/state/game-link-overlay`, a space-free link to the mount. The mount exists only for the processes of that namespace, so it ends with them: nothing needs recovering, and the host never sees it.

A session skips the stock check, the baseline backups, the install, the offline staging and the rollback, because the real folder isn't changed.

**What the game writes.** Its writes land in the upper dir: `swofty/loader.log`, `swofty/perf.log`, crash reports, caches and each addon's storage. When the game exits, `swofty run` copies the logs to `$DATA/logs/` and the session's `game-logs/` folder. A file the game rewrites that Swofty placed is copied up into the upper dir and then shadows later builds of it; the wrapper warns when it finds one, and you can delete it.

**The launcher during a session.** `$DATA/state/overlay/session.json` records the session's game folder, profile, mount and the process that keeps the mount namespace alive. The launcher recognises a game that runs from the mount as the game of that folder. For an addon's folder, the `addon_game_dir` call goes through `/proc/<pid>/root` of that process, so the launcher reads and writes exactly what the game sees; see [the launcher API](/reference/addons/launcher-api).

### Falling back to copying

A launch that can't use OverlayFS records why in `$DATA/state/overlayfs-fallback.json`, which the launcher shows as a note, and asks before copying. The wrapper opens `swofty-launcher --overlayfs-fallback-dialog` and waits for it:

| Reason | Kind | `news_seen` key |
|---|---|---|
| Not Proton, not a Steam copy, the launch doesn't apply the profile, or a `swofty apply` install is in the folder | `unsupported` | `overlayfs-unsupported` |
| The layers can't be built (including too little space), the user namespace or the mount fails, or the mount doesn't report in 30 seconds | `mount-failed` | `overlayfs-fallback` |

**Continue** (exit code 0) copies the files as in a launch without the flag. If **Don't show again** is ticked, the key of that kind is saved in `news_seen` in `config.json`, and later launches of that kind skip the dialog. **Exit** (exit code 1), or closing the window, cancels: the wrapper exits with code 75 and records the cancelled launch for the launcher, and nothing is copied or changed. **Exit** never saves the box. While the dialog is open, `$DATA/state/overlayfs-dialog.pending` exists and the wrapper logs every 30 seconds, so neither `swofty run` nor the launcher reports a stuck wrapper. A launch with no display, or without the launcher program beside `swofty`, copies without asking.

To try the fallback, set `SWOFTY_OVERLAYFS_FAIL=1` in the launch environment: `overlay-exec` then fails its mount on purpose.

## Windows

Select **Play** in the launcher or run `swofty run`. If Steam is closed, it [starts first](#steam). To follow the logs in the launcher, select the arrow next to **Play** and choose **Play with console**.

Modded sessions don't start while `F1_25.exe` is set to **Run this program as an administrator** and the launcher runs normally: Windows won't start the game unelevated (error 740), and the launch stops with a message naming the setting. To fix it, right-click `F1_25.exe` in the game folder, open **Properties > Compatibility** and untick the box, or untick it under **Change settings for all users** when it's set for all users. You can also run the launcher as administrator. `swofty doctor` shows the setting in its `run as admin` row.

The launcher doesn't pass its `__COMPAT_LAYER`, the variable Windows uses to hand compatibility settings on to the programs a program starts, to F1 25, to the offline play session launcher, or to the EA app when it starts the EA app. The launch log says `environment: not passing the Windows compatibility layer __COMPAT_LAYER=<layers> to the game` when it drops one. A `__COMPAT_LAYER` under `"env"` in `"launch"` in `$DATA/config.json` is still passed. Each session's `env.txt` lists the environment the launcher starts the game with.

Modded sessions also don't start while the game folder is one this process can't create files in, such as the EA app's `C:\Program Files\EA Games\F1 25`, and the launcher runs normally. The check creates and deletes a file in the game folder and in `2025_asset_groups/ui_package`, the first folder an install writes in. A file that Windows puts in `%LOCALAPPDATA%\VirtualStore` instead counts as one it can't create. The launch stops before the build with a message naming the folder; the launcher offers **Restart as administrator**. See [A game folder only administrators can change](/using/game-copies#a-game-folder-only-administrators-can-change).

## VR

**Play in VR** and `swofty run --vr` pass `-vr` to the game, the argument of Steam's VR launch option for F1 25. On Linux, SteamVR starts first when it isn't running, and the Steam wrapper passes on the arguments Steam gives the game, so Steam's **Launch in SteamVR mode** works too. See [Playing in VR](/using/vr).

## Logs

You'll find session logs in `$DATA/sessions/<id>/`. Run `swofty logs` for the latest summary, or use `swofty run` to follow the logs as you play.

If Proton can't create the game process, you'll see `Proton could not start the game process (CreateProcess error N)` instead of an exit code in `swofty run`, `swofty logs` and the launcher.

Each session's `end.json` says how the game ended, with its exit code (`null` when the session couldn't read it) and the reason:

| `end` | When |
|---|---|
| `quit` | The game exited with code 0. |
| `crash` | The game exited with a crash code (`0xC0000000` and up, other than -1), or `loader.log` shows an unhandled exception before the exit. |
| `stopped` | **Stop** in the launcher, `swofty run --restart` or a stop signal to the wrapper closed the game. |
| `unknown` | Anything else, such as -1 with no unhandled exception, or an EA app launch with no exit code. F1 25's crash handler exits with -1, and the game also leaves -1 when it fails while closing. |

The launcher shows a crash for `crash`, and for `unknown` with a non-zero exit code. It doesn't show one after **Stop**.

When the game exits, however it exits, the session puts the game folder back to vanilla. To check, run `swofty verify`. See [Restoring the game](/using/profiles-and-restoring) for the cases that need `swofty restore`.

## Next

- [Offline play](/reference/safety): what a modded session turns off.
- [Backups and recovery](/reference/restore): how the session rolls the game folder back.
