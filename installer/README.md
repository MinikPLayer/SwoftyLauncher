# Swofty Launcher Installer

A small window that installs the latest Swofty Launcher release and opens it.

| Platform | Download |
|---|---|
| Windows | [SwoftyLauncherInstaller.exe](https://github.com/Swofty-Developments/SwoftyLauncher/raw/main/installer/SwoftyLauncherInstaller.exe) |
| Linux and Steam Deck | [SwoftyLauncherInstaller-linux.zip](https://github.com/Swofty-Developments/SwoftyLauncher/raw/main/installer/SwoftyLauncherInstaller-linux.zip), then extract it |
| Both | [SwoftyLauncherInstaller.zip](https://github.com/Swofty-Developments/SwoftyLauncher/raw/main/installer/SwoftyLauncherInstaller.zip) |

Run `SwoftyLauncherInstaller.exe` on Windows, or `SwoftyLauncherInstaller` on Linux and Steam Deck, choose a folder and select **Install**.

The installer downloads the latest release from GitHub, checks it against the release's `SHA256SUMS`, extracts it into the folder, adds a Start menu or app menu entry and, if ticked, a desktop shortcut, then opens the launcher. On Windows it runs without administrator rights, and an installer run as administrator opens the launcher through the desktop's shell, as the signed-in user without administrator rights. Running it again finds the existing install, in the default folder, behind the Start menu, app menu or desktop shortcut, or from a running launcher, and updates it in place. Choosing another folder offers to move that install there: the installer moves the release files, checks them, deletes the old folder once it's empty and points the shortcuts at the new folder. If the launcher is open from the folder it installs to, the installer offers to close it first, and while F1 25 is running it doesn't install. After the first install the launcher updates itself.

Default folders:

| Platform | Folder |
|---|---|
| Windows | `%LOCALAPPDATA%\Programs\SwoftyLauncher` |
| Linux | `~/SwoftyLauncher` |

## Data folder

Under **Keep addons, profiles and backups in**, the installer shows the launcher's data folder: addons, profiles, backups, caches, downloads and state. Choosing an empty or new folder there moves the data folder before the install:

| Platform | Default data folder | After a move |
|---|---|---|
| Windows | `%LOCALAPPDATA%\Swofty` | A junction at `%LOCALAPPDATA%\Swofty` points to the new folder |
| Linux | `~/.local/share/swofty`, or `~/.var/app/com.valvesoftware.Steam/data/swofty` with Flatpak Steam | A symbolic link at the default folder points to the new folder |

The launcher and the `swofty` command read the default folder and follow the junction or link, and on Windows the in-game code does the same; the release in use needs no update. On the same drive the installer renames the folder; on another drive it checks the free space, copies the folder, compares the copy with the original and deletes the original. Choosing another folder later re-points the junction or link, and choosing the default folder moves the data back and removes it. While Swofty Launcher, Swofty UI, the `swofty` command line or F1 25 runs, the installer moves nothing and names what to close. When a step fails, the error names the step and the data stays in the old folder, or in the new one when only deleting the old copy failed.

When `SWOFTY_DATA_DIR` is set, the installer shows that folder. On Windows a move sets the user variable `SWOFTY_DATA_DIR` to the new folder; on Linux the installer doesn't move it.

## Building

```bash
cargo build --release -p swofty-launcher-installer
```

CI builds both executables on every push (`Installer` job, artifacts `installer-Linux` and `installer-Windows`). The files in this folder are those artifacts, renamed: `SwoftyLauncherInstaller.exe` is the Windows build, `SwoftyLauncherInstaller-linux.zip` holds the Linux build as `SwoftyLauncherInstaller`, and `SwoftyLauncherInstaller.zip` holds both.
