---
title: Windows
description: Set up Swofty Launcher for F1 25 on Windows, point it at the game and launch your first modded session.
---

# Windows

## 1. Install the launcher

Follow [Installation](/guide/installation) to install the launcher with the installer or by hand.

## 2. Open it and find the game

1. Make sure you have F1 25 installed with Steam.
2. Open **Swofty Launcher** from the Start menu, or `bin\swofty-launcher.exe` in the install folder.

Watch the status line next to **Play**: you'll see **Detecting F1 25…** while the launcher looks for the game, then **Ready** when it finds it.

### If the game is not found

If the launcher can't find the game, you'll see **F1 25 not found** next to the cover art and a **Locate game** button. To point it to your installation:

1. Select **Locate game**.
2. Type the F1 25 folder's path, or select **Browse…** and choose it. It's the folder with `F1_25.exe`, usually `...\steamapps\common\F1 25` or `C:\Program Files\EA Games\F1 25`.
3. Select **Add and play this copy**.

With F1 25 on both Steam and the EA app, select the game name next to the cover art to pick the copy **Play** uses. See [Steam and EA app copies](/using/game-copies).

## 3. Play

1. [Install your addons](/using/addons) and [mods](/using/mods), then pick a [profile](/using/profiles).
2. Press **Play**.
3. On your first Play, the launcher opens a one-time setup. Say whether your game folder holds mods Swofty didn't add, then select **I understand**. If you said no on a Steam copy, the launcher checks the game files against Steam's own list at launch and goes straight on. Otherwise select **Verify game files in Steam**, wait for Steam to finish, then select **I've verified, continue**. On the EA app copy, repair the game in the EA app instead.
4. On your first launch, select **Accept and launch** in the **Modded play runs offline** dialog. For why modded play runs offline, see [Offline play](/reference/safety).
5. The first launch after the setup can take 5 to 8 minutes. Don't press Play in Steam or the EA app while it runs.

You'll see **Launching…** on the button, then **Stop** while the game runs. While Swofty builds your mods or copies them into the game folder, the button shows **Building…** and the bar beside it shows how far it got. **Cancel** stops the build after the file it is on and puts the game files back; your profile and addons stay as they are, and the next Play builds again. Addons in a running build can't be updated or removed until it finishes. If Steam is closed, the launcher starts it and waits for it to sign in first; see [Steam](/reference/launch#steam). When you want to play vanilla, launch through Steam as usual.

To follow the logs as you play, select the arrow next to **Play** and choose **Play with console**, or run `swofty run` from a terminal.

## If the mods don't load

An antivirus such as Windows Defender can delete, quarantine or block the launcher's `version.dll` and its other DLLs, and F1 25 would then run without mods.

- Before the game starts, the launcher checks that the files it put in the game folder are still there. If one is missing or changed, F1 25 is not started: the game folder goes back to vanilla and a **Your antivirus removed mod files** dialog names the files.
- When `swofty.exe` is gone from the launcher's `bin` folder, **Play** shows **Could not launch F1 25** with how to restore it from quarantine, which folder to exclude, and to reinstall the launcher if it isn't there.
- When `bin\simplified-helper.exe` is gone from Simplified for Swofty's folder, **Play** downloads and installs Simplified for Swofty again. When another addon's helper program is gone, the error names it and says to allow the addon's folder in the antivirus, then reinstall the addon.
- When the launcher's own `loader` folder is gone from its install, F1 25 is not started and a **Swofty Launcher's loader folder is missing** dialog says to reinstall the launcher. Your addons and settings are kept.
- Once the game runs, the launcher checks that the mods loaded. If they did not, a **Mods did not load** dialog appears, and the game keeps running without mods.

To fix it:

1. Close F1 25 if it is running.
2. Open **Windows Security** > **Virus & threat protection** > **Protection history** and restore the files the dialog names.
3. Add exclusions for the game folder and the launcher folder (`%LOCALAPPDATA%\Programs\SwoftyLauncher`).
4. Press **Play** again.

### If version.dll stays in the game folder but doesn't load

When the mods did not load and `version.dll` is still in the game folder, the launcher checks what kept it out of the game:

- which `version.dll` the running game loaded, and from which folder
- the DLL load policies on `F1_25.exe`, such as Exploit protection's Code integrity guard
- Exploit protection's program settings for `F1_25.exe` (`MitigationOptions`, `MitigationAuditOptions` or `EAFModules` under its Image File Execution Options)
- the compatibility settings on `F1_25.exe` (its **Compatibility** tab, for you and for all users), and the compatibility shims Windows loaded into the game (`AcLayers.dll` and the like)
- the compatibility settings on the programs that start the game (`EADesktop.exe`, `EALauncher.exe`, `swofty-launcher.exe` and the offline play stub), and the compatibility shims in the running EA app. When the game has shims and `F1_25.exe` has no setting, the advice names the program they came from
- whether `version.dll` is listed under KnownDLLs, and whether Smart App Control is on
- Code Integrity and Microsoft Defender events since the game started
- the antivirus registered with Windows, and programs that hook into games, such as Medal or RivaTuner, among the game's modules

When the loader runs but the game can't create its log in the game folder, the log is in `state\loader-fallback.log` in the data folder instead. The session then counts the mods as loaded, and its `[warning]` line in `state\wrapper-run.log` says what to do.

The **Mods did not load** dialog then names what it found and its fix, for example turning off Smart App Control, removing the Exploit protection settings for `F1_25.exe`, unticking its **Compatibility** tab, or the DLL protection of an antivirus other than Microsoft Defender, by name. When nothing it found explains it, the dialog says so, names any program hooked into the game to close first, and offers **Send Logs to Developers** and **Join the support Discord**. When Windows doesn't let the launcher look inside the game and no antivirus other than Microsoft Defender is on, the dialog says that instead of naming an antivirus. The details are under `[loader]` in `state\wrapper-run.log`, which [Send Logs](/using/send-logs) includes.

## If Windows blocks the launcher

Smart App Control on Windows 11, and app control policies on work or school PCs, block programs that aren't code-signed. The launcher's programs aren't code-signed. When Windows blocks one as you press **Play**, a **Could not launch F1 25** notification names the program, such as `swofty.exe`, and the error code. When it blocks one later in the launch, a **Windows blocked a Swofty program** dialog does the same, and F1 25 is not started.

| Error | Blocked by |
|---|---|
| 4551, 577 | Smart App Control or an app control policy |
| 4556 to 4559, 4580 to 4582 | Smart App Control |
| 1260, 786 | A Windows policy set by whoever manages the PC |
| 225, 226 | An antivirus, see [If the mods don't load](#if-the-mods-don-t-load) |

Smart App Control has no exceptions for single apps. To use the launcher, turn it off:

1. Open **Windows Security** > **App & browser control** > **Smart App Control settings**.
2. Select **Off**.
3. Press **Play** again.

On some versions of Windows, Smart App Control can only be turned back on by resetting Windows. Microsoft Defender Antivirus keeps protecting the PC with it off. To keep Smart App Control on, play F1 25 from Steam, without mods.

If Smart App Control is already off, a policy set by whoever manages the PC blocked the program. They can allow the launcher folder and the game folder.

## If your game files aren't the ones Steam installed

Before a modded session builds anything, the launcher checks the game against Steam's record of the files it installed. If a file was changed outside the launcher, for example by another mod tool or a manual install, F1 25 is not started and a **Your game files have been edited** modal says how many files changed, names the first six, and lists the fix.

To fix it:

1. Select **Verify game files in Steam** in the modal. Steam checks F1 25 and puts its own copies back. In Steam, it's under right-click F1 25 → **Properties** → **Installed Files** → **Verify integrity of game files**.
2. When Steam has finished, press **Play** again.

The next launch checks the game again and continues once it matches Steam's copy.

## If an addon needs a newer F1 25

Addons are made for the latest F1 25 update. When an addon's changes don't apply to an older F1 25, F1 25 is not started and the play bar names the addon, your F1 25 version and the version it needs. Update F1 25 in Steam or the EA app, then press **Play** again. The original error is in the launch log, under **View log**.

## If the game closes as it starts

F1 25 runs one copy at a time. A copy started while another runs closes itself before its window opens.

- If F1 25 is already running when you press **Play**, or is started from Steam or a shortcut while the launcher gets ready, the modded copy is not started and an **F1 25 is already running** dialog appears.
- If the modded copy closes itself as it starts, an **F1 25 was opened twice** dialog appears when another copy was running, and **F1 25 closed as it started** when none was.

To fix it:

1. Close F1 25, and check in Task Manager that no `F1_25.exe` is left.
2. Press **Play** in the launcher, and don't start F1 25 from Steam until the session ends.

## If the game crashes

When a session ends in a crash, the launcher opens an **F1 25 crashed** screen with the cause and its fix when it recognises one, or **Send Logs to Developers** and the support Discord when it doesn't. See [When F1 25 crashes](/using/send-logs#when-f1-25-crashes).

The session puts the game folder back to vanilla when the game exits, crash or not. If the launcher was closed first, it puts the folder back the next time it starts. See [Restoring the game](/using/profiles-and-restoring).

To read a summary of your last session, run `swofty logs`. You'll find the logs in `$DATA\sessions\<id>`.

## Next

- [Using the launcher](/using/): addons, mods, profiles and in-game settings.
- [Launching the game](/reference/launch): logs and what a session does.
