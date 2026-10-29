---
title: Send your logs to the developers
description: Send the launcher and game logs to the Swofty Launcher developers in one click, with your Discord username and what went wrong.
---

# Send your logs to the developers

When something goes wrong, you can send your logs to the developers straight from the launcher.

1. Select **Send Logs to Developers** at the bottom of the sidebar, above **ACTIVE PROFILE**.
2. Enter your Discord username. The developers use it to reply to you.
3. Under **What went wrong?**, describe what happened and what you were doing when it went wrong, in 10 to 1000 characters.
4. Select **Send**. Until both fields are filled in, **Send** is greyed out and a hint under each field says what's missing.

When your data folder has no launch log and no game session, the dialog warns: **We don't see a game launch in your logs yet. Press Play and reproduce the problem first, then send logs.** You can still send, and `summary.txt` then says that no game session was found.

The dialog shows the progress, then **Sent — thanks!**. If the logs could not be sent, it shows the reason, such as no connection to Discord or the HTTP status Discord returned, and **Try again**. The launcher remembers your username for next time.

## When F1 25 crashes

When a game session ends in a crash, the launcher opens a full-window **F1 25 crashed** screen. It reads what the game left behind: the session's `loader.log`, the crash report and the files in the game folder.

- When it recognises the crash, the screen names the cause and the steps to fix it, with **Send Logs to Developers** and **Got it**.
- When it doesn't, the screen says **We couldn't tell why it crashed**, with **Send Logs to Developers**, **Join the support Discord** and **Close**.

| Cause | What the launcher looks for |
|---|---|
| F1 25 needs more free space | F1 25's own **Insufficient Storage** dialog before it closed. The screen names the drive, the space F1 25 needs there and how much to free |
| Windows blocked Swofty's files from loading | Exit code `0xc0e90002`, from Smart App Control or an app control policy |
| F1 25's game memory filled up | `Ne heap error 0xc` in the `GAME_DEFAULT` heap. The screen names the imported tracks on in the profile, Sepang first, and the number of Classic Seasons years. The step to turn a track off says that a career whose next round is at that track needs the round changed with the Career Mode Editor first |
| Windows ran out of memory | `Ne heap error 0x58` or `0x5`, or an `allocateAligned` / `useAddressSpace` failure: RAM and the page file are used up |
| F1 25 froze and closed itself | F1 25's own 30-second hang watchdog closed the game. The screen names an addon that kept the game busy while it froze, a graphics driver error just before it, and graphics mods in the game folder |
| Manual Pit Lane crashed F1 25 in the pit lane | A crash in F1 25's code just as Manual Pit Lane (`kizi.pitlane`) kept control of the car or read a garbage control mode for it, as happens on Return to Garage or a flashback in the pit lane |
| Your career uses a track or season that is off | A crash in F1 25's code whose nearest named function on the stack is `Sess_Loader_SetupTrackAssets`, at most 10 seconds after `career.loaded` with no `career.unloaded` between, while a track or season addon is installed but off in the profile. The screen names the addon when one is off, and lists them all when several are |
| Your graphics driver stopped responding | `DXGI_ERROR_DEVICE_REMOVED` (`0x887a0005`) in the crash. When Swofty's in-game overlay drew just before, the screen names the addons it drew for. It also names full car packs on at once that replace each other's cars, and frame generation when it is on |
| F1 25 ran out of graphics memory | `E_OUTOFMEMORY` (`0x8007000e`) in the crash |
| A program that hooks into games | The crash in RivaTuner Statistics Server, Windhawk, Medal, Discord's overlay, OBS Studio's game capture, the NVIDIA app's overlay, the Steam overlay, Special K, or Windows' force feedback driver for a wheel (`pid.dll`). With no unhandled exception and no crash report, the last exception the mod loader saw counts when the game's exit code matches it |
| Graphics mods in the game folder | ReShade, OptiScaler, Special K or an ASI loader in the game folder, and a crash in graphics code or before the game's window opened |
| A crash in the graphics driver | The crash in an NVIDIA, AMD or Intel driver DLL |
| A crash in frame generation | The crash in DLSS Frame Generation or XeSS frame generation |

Under Wine or Proton, the graphics driver, graphics memory and Windows memory checks are skipped.

A session counts as a crash when it ended with a crash exit code, an exception the mod loader saw or F1 25's **Insufficient Storage** dialog, including a game the EA app started. A game that closes with another code, such as -1, shows as closed, not crashed. The screen opens by itself for a crash from the last 15 minutes. The play bar keeps **Crashed** with the cause, and **What happened** opens the screen again. `state/wrapper-run.log` gets a `[crash] known problem <id>: <title>` line, or `[crash] no known problem matches this crash`.

## Send the zip yourself

The launcher keeps a copy of the zip in `$DATA/logs/`, the newest 10. To send it to someone else, such as the author of a track or a mod:

1. Send your logs as above. The zip is kept whether or not they were sent.
2. Under the result, select **Click here to open them in File Explorer** (on Linux, **in your file manager**). The folder opens with the zip selected.
3. Drag the zip into Discord or a message.

On Linux, the launcher asks your file manager to select the zip over D-Bus (`org.freedesktop.FileManager1`). Without one that answers, it opens the `logs` folder.

## What gets sent

The logs go to the developers' Discord as one zip file, `swofty-logs-<username>-<time>.zip`. A large zip goes in several parts.

| In the zip | Contents |
|---|---|
| `summary.txt` | Your username and message, the launcher version, your operating system, CPU and memory (on Windows also the page file and the commit limit), the game's version and folder, a line when no launch log or game session was found, the addons on in the profile that draw over the game and, for each session, whether the overlay drew and which addons drew, a summary of the latest crash report, the game files Windows keeps for you in `%LOCALAPPDATA%\VirtualStore` instead of the game folder, the summary of the latest performance report, and the active profile's addons by name: those on in load order, then the rest, each with its version, id, where it came from (built in, DB Editor, imported from which mod file, or installed) and the settings changed from their defaults |
| `crash/crash-<time>.txt`, `crash/crash-<time>.dmp` | The newest crash report the game wrote and its minidump, when there is one. The minidump is left out, noted in the summary, when it is too big for the zip |
| `game/loader.log`, `game/perf.log` | The loader log and the performance report of the game that's running |
| `state/*.log` | The launcher's own logs, such as `launcher-run.log` and `wrapper-run.log`, `launcher-app.log` with each start of the launcher, whether it ran as administrator, what **Restart as administrator** did, each action in the launcher window that failed with its addon, files and error, and any panic, and `loader-fallback.log`, the loader's log when the game couldn't write in its folder |
| `sessions/<time>/` | The logs and small files of your three latest game sessions, such as `loader.log`, `perf.log`, `env.txt`, `exit_code` and `end.json` |
| `addons/<id>/helper.log` | Each addon helper's log |
| `addons.json` | The id, name, version, source, category, types, origin and experimental features of every installed addon, and each profile's addons with their names and settings |
| `conflicts.json` | From the active profile's last build: each pair of addons that change the same thing, which one wins, and the records, resources, files and text they both change |
| `config.json` | The launcher's settings |
| `game/career-editor/snapshot.json` | The Career Mode Editor's snapshot, when the game folder has one |

The logs can include folder paths with your computer's user name, your hardware, and your installed mods and profiles. Passwords, tokens and keys in `config.json`, in addon settings and in each session's `env.txt` are replaced with `[redacted]`. Each file is cut to its last 5 MB, and the whole zip holds at most 40 MB before compression.

A bot in the developers' Discord reads each report with Anthropic's Claude API to suggest a cause, so the zip's contents are also sent to Anthropic.

## From the command line

```sh
swofty logs send --name <your Discord username> --message "What happened and what you were doing"
```

`--message` takes 10 to 1000 characters, and the command stops without it unless you add `--dry-run`. When your data folder has no launch log and no game session, the command warns first. The command keeps the zip in `$DATA/logs/` too and prints its path. To write the zip and list what it holds without sending it, add `--dry-run`. See [`swofty logs send`](/reference/cli#swofty-logs-send).

Builds from source don't include the developers' Discord address. There, the button reads **Save Logs**: it saves the zip to `$DATA/logs/` without sending it, then offers the same link to the zip. `swofty logs send` only works with `--dry-run`.
