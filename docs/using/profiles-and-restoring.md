---
title: Restoring the game
description: How the game folder rolls back after every session, and when to run swofty restore to put every game file back to stock.
---

# Restoring the game

Every session puts the game folder back to vanilla when the game exits, whether it quits, crashes or is stopped. You don't need to do anything after you play. Use restore for the cases below:

| When | What to do |
|---|---|
| A launch stops because a file is in the way | [Files in the way](#files-in-the-way) |
| You ran `swofty apply`, which leaves the profile installed | [Restore](#restore) |
| The game updated and its files need repair | [After a game update](#after-a-game-update) |
| You're about to uninstall | [Restore](#restore), then [Uninstalling](/guide/uninstall) |

Sessions started with `--use-overlayfs` on Linux never change the game folder, so there is nothing to put back after them. If OverlayFS can't be used, the launch asks whether to copy files instead, and a copied session follows the rules on this page. See [OverlayFS sessions](/reference/launch#overlayfs-sessions).

## Restore

1. Close F1 25.
2. Run:

   ```sh
   swofty restore
   ```

3. Read the report. If anything couldn't be restored, resolve those issues before you play vanilla.

Restore puts back the backed-up files and removes what the launcher added.

## After a crash

If the launcher itself was stopped before the session finished, the next launch, `swofty run`, `swofty apply` or launcher start puts the folder back first and says **Recovered the game folder from an unfinished session**. Use `swofty verify` to check for remaining changes, or `swofty doctor` if you want to check your whole setup.

## Files in the way

When a file the profile needs to place is already in the game folder and the launcher can't prove it put it there (for example, a mod you copied in by hand), it leaves the file alone and doesn't start the game. The launcher opens a **Your game files have been edited** modal; **What to do** on the play bar reopens it.

1. Read the list of files in the modal.
2. Select **Restore game folder**. From a terminal, run `swofty restore` instead.
3. Press **Play** again.

Either way, restore rolls back the launcher's changes and moves those files to `$DATA/set-aside/<time>/`, where you can get them back.

## After a game update

The launcher won't put files from an old game build back over a newer one. If your game files still need repair:

1. In Steam, open F1 25 → **Properties** → **Installed Files** → **Verify integrity of game files**.
2. Run `swofty restore` again to remove the launcher's additions (`version.dll` and `swofty/`). Steam repairs game files but leaves those additions in place.

## Backups

Your [data folder](/guide/installation#where-your-files-are-stored) holds the original files needed to undo changes, so keep it until restoration is complete.

## Restoring a profile save

When a session without an addon that affects saves shrinks the profile save, the Addons page offers **Restore**, and `swofty saves restore <backup>` does the same. Both refuse while F1 25 or Steam runs. Exit Steam completely (Steam menu > Exit) first, restore, then start Steam, which uploads the restored save. If Steam asks which copy to keep, keep the local files (this computer).

Each restore first backs up the save it replaces, as `before restoring <backup>` in `swofty saves list`.

### After a restore made the game report a corrupted save

If F1 25 showed the corrupted save prompt after a restore and you accepted recovery, the game reset the profile. To get the old profile back:

1. Close F1 25, then exit Steam completely (Steam menu > Exit).
2. Run `swofty saves list`. Pick the backup taken before the restore: the one named `before restoring <backup>`, or the session backup from before it.
3. Run `swofty saves restore <that backup>`.
4. Start Steam. If it asks which copy to keep, choose the local files (this computer).
5. Start F1 25.

## Next

- [Uninstalling](/guide/uninstall): remove the launcher.
- [Backups and recovery](/reference/restore): how restoration works, for contributors.
