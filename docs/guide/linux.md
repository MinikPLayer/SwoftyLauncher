---
title: Linux and Steam Deck
description: Set up Swofty Launcher for F1 25 on Linux and the Steam Deck with the Steam launch option, and launch your first modded session.
---

# Linux and Steam Deck

You run F1 25 through Steam and Proton. With mods, you'll be playing single-player and offline.

## 1. Install the launcher

Follow [Installation](/guide/installation) to install the launcher with the installer or by hand.

If you use **Flatpak Steam**, the install folder must be inside `~/.var/app/com.valvesoftware.Steam/`.

## 2. Set the Steam launch option

1. Start Steam. You need F1 25 installed and a Proton version selected for it.
2. Open F1 25 in Steam → the gear icon → **Properties** → **General**.
3. In **Launch options**, enter this line. Replace `<install folder>` with the full path of the install folder, such as `/home/deck/SwoftyLauncher`:

   ```text
   <install folder>/bin/swofty run -- %command%
   ```

You can now start the modded game with either Steam's **Play** button or the launcher's **Play** button. To launch vanilla through Steam again, clear the launch option.

## 3. Play

1. Open **Swofty Launcher for F1 25** from the app menu, or `bin/swofty-launcher` in the install folder. The status line next to **Play** reads **Ready** when the launcher has found the game.
2. [Install your addons](/using/addons) and [mods](/using/mods), then pick a [profile](/using/profiles).
3. Press **Play**.
4. On your first Play, the launcher opens a one-time setup. Say whether your game folder holds mods Swofty didn't add, then select **I understand**. If you said no on a Steam copy, the launcher checks the game files against Steam's own list at launch and goes straight on. Otherwise select **Verify game files in Steam**, wait for Steam to finish, then select **I've verified, continue**. On the EA app copy, repair the game in the EA app instead.
5. On your first launch, you'll see the **Modded play runs offline** dialog. Select **Accept and launch**. For details, see [Offline play](/reference/safety).
6. The first launch after the setup can take 5 to 8 minutes. Don't press Play in Steam or the EA app while it runs.

If Steam is closed, the launcher starts it and waits for it to sign in first; see [Steam](/reference/launch#steam).

To see live logs as you play, select the arrow next to **Play** and choose **Play with console**, or run `swofty run` from a terminal.

If you see **F1 25 not found** next to the cover art, select **Locate game**, choose the folder with `F1_25.exe`, usually `.../steamapps/common/F1 25`, and select **Add and play this copy**.

To play the EA app copy, add its folder with **Add another folder…** in the game switcher and set `ea_launch_command`; see [The EA app copy on Linux](/using/game-copies#the-ea-app-copy-on-linux).

## Optional: run the game from an OverlayFS mount

By default, each session copies the profile's files into the game folder and puts them back when the game exits. On Linux you can skip the copying: add `--use-overlayfs` to the launch option, after `run`:

```text
<install folder>/bin/swofty run --use-overlayfs -- %command%
```

The game then runs from a mount that lays Swofty's files over the game folder, so nothing is copied into it, backed up or restored. Your game folder is never touched, and a crash or `kill -9` leaves nothing to clean up. The first launch builds the layers, which takes about as long as a normal install; later launches start in under a second and exit without a rollback.

The mount is made without root, in a private mount namespace that only the game's processes see, so it needs:

- **Unprivileged user namespaces and OverlayFS** (Linux 5.11 or newer). `swofty doctor` shows whether a test mount works on your system.
- **Steam from your distribution or Valve's own client**, not Flatpak Steam: the Flatpak sandbox blocks user namespaces.
- **A Steam copy of the game.** The EA app copy isn't supported.
- **No `swofty apply` install in the game folder.** Run `swofty restore` first.

The files the game writes while it runs (its logs, the Career Editor's data and caches) go to `$DATA/profiles/<profile>/writes/` instead of the game folder. Swofty copies the logs to `$DATA/logs/` and the session's `game-logs/` folder when the game exits.

### If OverlayFS can't be used

When a launch can't use OverlayFS, Swofty asks before it does anything. A dialog explains why and offers two choices:

- **Continue** launches this session the usual way, by copying files into the game folder.
- **Exit** cancels the launch. Nothing is changed, and the launcher says why the game didn't start.

Tick **Don't show again** and select **Continue** to skip the dialog next time and go straight to copying. **Exit** never saves the box. The two kinds of dialog have their own box: one for a mount that fails (no user namespaces, Flatpak Steam, a kernel without OverlayFS), the other for a launch OverlayFS doesn't support (an EA app copy, an installed `swofty apply` profile). The launcher also shows a note whenever the last launch fell back.

A game controller that relies on membership of the `input` group may not be seen inside the mount, because the mount's namespace maps only your own user and group. Controllers that work through the system's device ACLs, which covers most wheels and pads, work as usual. If one doesn't, remove `--use-overlayfs`.

## Steam Deck

Install the launcher and set the launch option in Desktop Mode. Once that's set, you can use **Play** in the Deck's library to start the modded game in Game Mode too.

## If your game files aren't the ones Steam installed

Before a modded session builds anything, the launcher checks the game against Steam's record of the files it installed. If a file was changed outside the launcher, for example by another mod tool or a manual install, F1 25 is not started and a **Your game files have been edited** modal says how many files changed, names the first six, and lists the fix.

To fix it:

1. Select **Verify game files in Steam** in the modal. Steam checks F1 25 and puts its own copies back. In Steam, it's under right-click F1 25 → **Properties** → **Installed Files** → **Verify integrity of game files**.
2. When Steam has finished, press **Play** again.

The next launch checks the game again and continues once it matches Steam's copy.

## If the game closes as it starts

F1 25 runs one copy at a time. A copy started while another runs closes itself before its window opens.

- If F1 25 is already running when you press **Play**, the modded copy is not started and an **F1 25 is already running** dialog appears.
- If the modded copy closes itself as it starts, an **F1 25 was opened twice** dialog appears when another copy was running, and **F1 25 closed as it started** when none was.

To fix it, close F1 25, check that no `F1_25.exe` process is left (`pgrep -l F1_25`), then press **Play** once.

## If the game crashes

The session puts the game folder back to vanilla when the game exits, crash or not. If the launcher was closed first, it puts the folder back the next time it starts. See [Restoring the game](/using/profiles-and-restoring).

To read a summary of your last session, run `swofty logs`.

## Advanced launch settings

If your setup needs CPU affinity or environment variables, set them in `$DATA/config.json`. To stop the launcher from starting Steam or the EA app, add `"start_store": false` under `"launch"`.

```json
{
  "launch": {
    "cpu_affinity": "0-11,14-31",
    "env": { "VKD3D_SHADER_CACHE_PATH": "0" }
  }
}
```

You can use NixOS too. To run the latest code instead of a release, [build from source](/reference/development).

## Next

- [Using the launcher](/using/): addons, mods, profiles and in-game settings.
- [Launching the game](/reference/launch): the Steam wrapper, logs and what a session does.
