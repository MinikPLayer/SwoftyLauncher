---
title: Installation
description: Install Swofty Launcher for F1 25 with the installer or by hand, and find the install folder and the data folder.
---

# Installation

Install the launcher with the installer or by hand, then follow the setup steps for [Windows](/guide/windows) or [Linux and Steam Deck](/guide/linux). You'll play modded games in single-player, offline.

## Requirements

- F1 25 on Steam, with the F1 2026 season content installed.
- The EA app version of F1 25 has experimental support; **Play** on it shows a one-time note before starting the game, and `swofty run` and `swofty apply` print the same note and continue. From 1.3.1, the launcher finds the EA app copy on Windows, starts it through the EA app, and switches between it and the Steam copy; see [Steam and EA app copies](/using/game-copies).
- Windows 10 or 11 (x86-64), or Linux with Steam and Proton. Steam Deck, NixOS and Flatpak Steam work. macOS does not.
- On Windows 11, Smart App Control off. It blocks the launcher's programs, which aren't code-signed; see [If Windows blocks the launcher](/guide/windows#if-windows-blocks-the-launcher).
- Free disk space: your content changes copies of the game files, so you'll need room for those copies.

## Install with the installer

1. Download the installer for your platform:

   | Platform | Download |
   |---|---|
   | Windows | [SwoftyLauncherInstaller.exe](https://github.com/Swofty-Developments/SwoftyLauncher/raw/main/installer/SwoftyLauncherInstaller.exe) |
   | Linux and Steam Deck | [SwoftyLauncherInstaller-linux.zip](https://github.com/Swofty-Developments/SwoftyLauncher/raw/main/installer/SwoftyLauncherInstaller-linux.zip), then extract it |

2. Run `SwoftyLauncherInstaller.exe` on Windows, or `SwoftyLauncherInstaller` on Linux and Steam Deck. Windows may show **Windows protected your PC**; select **More info**, then **Run anyway**.
3. Under **Install to**, keep the default folder, or select **Browse…** to choose another.
4. Leave **Add a desktop shortcut** ticked if you want one.
5. Under **Keep addons, profiles and backups in**, keep the folder shown, or choose another; see [Moving the data folder](#moving-the-data-folder).
6. Select **Install**.

The installer downloads the latest release, checks it against the release's `SHA256SUMS` and extracts it into that folder. It adds **Swofty Launcher** to the Start menu on Windows, or **Swofty Launcher for F1 25** to the app menu on Linux, adds the desktop shortcut if you ticked it, then opens the launcher.

On Windows the installer runs without administrator rights. When you run it as administrator anyway, it still opens the launcher as you, without administrator rights.

| Platform | Default folder |
|---|---|
| Windows | `%LOCALAPPDATA%\Programs\SwoftyLauncher` |
| Linux and Steam Deck | `~/SwoftyLauncher` |

When you browse to a folder that isn't empty, isn't named `SwoftyLauncher` and holds no install, the installer uses a `SwoftyLauncher` folder inside it. With **Flatpak Steam**, choose a folder inside `~/.var/app/com.valvesoftware.Steam/`, such as `~/.var/app/com.valvesoftware.Steam/SwoftyLauncher`.

Under **Install to**, the installer shows the full path it installs to. When that folder already holds the launcher, **Install** updates it in place, and your mods and settings stay.

When Swofty Launcher, Swofty UI or the `swofty` command line is open from that folder, the installer asks to close it first. Select **Close Swofty Launcher and continue**: the installer asks it to close, ends it if it's still open after 10 seconds, then installs. While F1 25 is running, the installer doesn't install; close the game, then select **Try again**.

You only need the installer once. After that, open the launcher from the Start menu or app menu, and it [updates itself](/using/updating).

### Update or move an existing install

The installer finds the launcher you already have: in the default folder, in the folder the Start menu, app menu or desktop shortcut opens, and in the folder of a running launcher. **Install to** shows that folder, and **Install** updates it in place. With more than one install, choose one under **Installed in**.

Choose another folder to move the install there. **Move your existing install from … to …** is ticked, and **Install** then:

1. Moves `bin/`, `loader/`, `share/` and the other release files to the new folder, and checks them there.
2. Deletes the old folder once it's empty. Files of your own in it stay.
3. Points the Start menu or app menu entry at the new folder, along with the desktop shortcut if **Add a desktop shortcut** is ticked, and deletes shortcuts to the old folder.
4. Installs the latest release in the new folder.

Close the launcher and F1 25 before moving it. Untick **Move your existing install** to install a second copy and keep the first. Moving the install leaves your [data folder](#where-your-files-are-stored) where it is; to move the data folder, see [Moving the data folder](#moving-the-data-folder).

## Install by hand

1. Open [Releases](https://github.com/Swofty-Developments/SwoftyLauncher/releases).
2. Download the archive for your platform:

   | Platform | Archive |
   |---|---|
   | Windows | `swofty-launcher-<version>-windows-x86_64.zip` |
   | Linux and Steam Deck | `swofty-launcher-<version>-linux-x86_64.tar.gz` |

3. Extract it where you want to keep the launcher, such as `Documents\SwoftyLauncher` or `~/SwoftyLauncher`. With **Flatpak Steam**, extract it inside `~/.var/app/com.valvesoftware.Steam/`.

A hand install adds no menu entries or shortcuts; open the launcher from `bin/`.

## The install folder

Either way, the install folder holds:

| Path | Contents |
|---|---|
| `bin/swofty-launcher` | The launcher |
| `bin/swofty` | The command line |
| `bin/swofty-ui` | [Swofty UI](/reference/ui/swofty-ui) |
| `loader/` | The in-game loader; keep it next to `bin/` |

On Windows, the files in `bin/` end in `.exe`. The setup guides call this folder the install folder.

## Where your files are stored

When these docs refer to `$DATA`, they mean the folder below for your platform:

| Platform | Folder |
|---|---|
| Linux | `$XDG_DATA_HOME/swofty`, usually `~/.local/share/swofty` |
| Flatpak Steam | `~/.var/app/com.valvesoftware.Steam/data/swofty` |
| Windows | `%LOCALAPPDATA%\Swofty` |

Your addons, mods, profiles, backups and session logs live here, apart from the install folder. It holds your backups, so keep this folder until you've [restored the game to stock](/using/profiles-and-restoring).

### Moving the data folder

Run the [installer](#install-with-the-installer), choose an empty or new folder under **Keep addons, profiles and backups in**, or select **Browse…** next to it, then select **Install**. Under the folder, the installer shows what **Install** does with your data. **Install** moves the data folder there, then updates the launcher.

- On Windows, the installer moves `%LOCALAPPDATA%\Swofty` to the new folder and leaves a junction at `%LOCALAPPDATA%\Swofty` that points to it. The launcher, the `swofty` command and the in-game code keep using `%LOCALAPPDATA%\Swofty`.
- On Linux and Steam Deck, the installer leaves a symbolic link at the folder in the table above.
- On the same drive, the installer renames the folder. On another drive, it copies the folder, checks the copy against the original, then deletes the original. When the other drive has too little free space, it moves nothing.
- Choosing another folder later moves the data again and points the junction or link there. Choosing the folder in the table above moves the data back and removes the junction or link.
- While Swofty Launcher, Swofty UI, the `swofty` command line or F1 25 is open, the installer doesn't move the data and names what to close; close it, then select **Try again**.
- When a step fails, the installer names the step and doesn't update the launcher. Your data stays in the old folder, or in the new one when only deleting the old copy failed.

To move it by hand on Windows, close the launcher and F1 25, move `%LOCALAPPDATA%\Swofty` to the new folder, then run this in Command Prompt:

```bat
mklink /J "%LOCALAPPDATA%\Swofty" "D:\SwoftyData"
```

#### With `SWOFTY_DATA_DIR`

Setting the `SWOFTY_DATA_DIR` environment variable to a folder makes the launcher and the `swofty` command use that folder as `$DATA`. The `--data-dir` option of `swofty` does the same for one command. The in-game code doesn't read `SWOFTY_DATA_DIR`.

```powershell
setx SWOFTY_DATA_DIR "D:\SwoftyData"
```

```sh
export SWOFTY_DATA_DIR=/mnt/games/swofty
```

Close the launcher and F1 25, copy the contents of the old data folder into the new one, then start the launcher again.

When `SWOFTY_DATA_DIR` is set, the installer shows its folder under **Keep addons, profiles and backups in**. On Windows, choosing another folder moves the data there and sets `SWOFTY_DATA_DIR` to it. On Linux, the installer doesn't move it; change the variable instead.

## Updating

The launcher updates itself. See [Updating](/using/updating).

## Next

- Set up on [Windows](/guide/windows) or [Linux and Steam Deck](/guide/linux).
