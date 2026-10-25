---
title: Backups and recovery
description: For contributors — how the baseline, the journal and session locks let the launcher undo every change to the game folder.
---

# Backups and recovery

This page is for contributors. Players should use [Restoring the game](/using/profiles-and-restoring).

The **baseline** records original game files. The **journal** records changes so they can be undone after a crash.

## Stored files

| Path | Contents |
|---|---|
| `$DATA/baseline/objects/<sha256>` | Original file contents |
| `$DATA/baseline/<build_id>/baseline.json` | File sizes and hashes for a game build |
| `$DATA/state/journal/<id>.json` | Changes to undo; `"session": true` for a `swofty run` session |
| `$DATA/state/placed.json` | Every file the launcher wrote into the game folder, with its SHA-256 |
| `$DATA/state/session.lock` | Held by a running session until its game files are rolled back |
| `$DATA/state/blocked.json` | Files of unknown origin that stopped the last launch |
| `$DATA/state/not-stock.json` | Game files that don't match Steam's copy, which stopped the last launch, with each file's measured path, found size, Steam's size and modified time |
| `$DATA/state/recovered.json` | The last recovery, for the launcher to announce |
| `$DATA/state/setup.json` | The one-time setup: its version, the player's answer about other mods, when the game folder was put back, when the setup finished, and whether the first-launch notice was shown |
| `$DATA/baseline-retired/` | The baselines the setup moved aside, until the setup finishes |

## Checking against Steam

Steam keeps a manifest for each installed depot in `<steam>/depotcache/<depot>_<manifest>.manifest`, listing every file's size and SHA-1. The installed depots and their manifest ids are in the library's `appmanifest_3059520.acf`. Before a session builds anything, `swofty run` and `swofty apply` check the game against them:

- Each original the baseline recorded at first sight (`"source": "first-sight"`) is compared with Steam's copy. A match becomes `"source": "depot"` and isn't compared again. A mismatch whose game folder file now matches Steam's, as after a Verify in Steam, is backed up again from the game folder.
- Until a check passes for a game build, every file Steam installed is checked by size (`"stock_checked": true` in `baseline.json` once it has). Files `placed.json` records as written by the launcher are left to the install.
- EA AntiCheat's launcher files (`EAAntiCheat.*` in the game root) are logged when they differ from Steam's and never stop a launch. EA rewrites them on a normal start. Modded sessions do not use them. Offline play parks and restores `EAAntiCheat.GameServiceLauncher.exe` through its own journal.
- When a game repair or update put `EAAntiCheat.GameServiceLauncher.exe` back while offline play was staged, rolling back offline play counts it as restored: with no `.bak` left (an EA app repair), or with a `.bak` identical to the repaired exe, which is removed (a Steam verify). A `.bak` that differs from the repaired exe still stops the roll-back.
- Before the check, a stale `EAAntiCheat.GameServiceLauncher.exe.bak` an interrupted session left is cleared: a backup that is Steam's copy is put back over the live exe, and a backup that is not is set aside under `swofty/offline-recovery/` while a Steam-verified live exe is kept. This step is skipped while a session is pending.
- Each original the install backs up for the first time is compared with Steam's copy before it is copied.

A file that doesn't match, other than an EA AntiCheat launcher file, stops the session before anything is built or changed and writes `state/not-stock.json`. The launcher opens a **Your game files have been edited** modal that says how many files changed, names the first six, and lists the fix: **Verify game files in Steam** (`steam://validate/3059520`), or add mods through the Simplified Importer. `swofty run` and `swofty apply` print the same link. Files Steam doesn't list are not checked. A game folder outside Steam is not checked. Without a readable manifest for every installed depot, the check is skipped and the launch log says why.

## The one-time setup

Modded play waits for the setup in `swofty_core::setup`. The launcher opens it on Play, and `swofty setup` runs it from the command line.

1. The player says whether the game folder holds mods Swofty didn't add.
2. `setup::reset` takes the session lock and restores the game folder as `swofty restore` does. It then moves `$DATA/baseline` to `$DATA/baseline-retired` and deletes `$DATA/cache/overlay`.
   - When the restore fails on a Steam or EA app copy, the reset continues: `$DATA/state/journal` moves to `$DATA/baseline-retired/journal`, and a second restore against the empty record removes Swofty's own files. The wizard names the error and says verifying puts the rest back.
   - When the restore fails on a copy from neither store, the reset stops before anything moves.
3. The player verifies the game files in Steam or repairs them in the EA app. A player who says no other mods are in the folder skips this on a Steam copy whose depot manifests `setup::store_checks` can read, when the restore put every file back: the install checks each original against those manifests. On a Steam copy, **I've verified, continue** stays off until the player opens the verify, and while Steam is verifying or updating the game. A running Steam's state comes from its `logs/content_log.txt`, since Steam doesn't update the `StateFlags` of the app manifest during a verify; the manifest covers the rest.
4. `setup::finish` records the setup as done and deletes `$DATA/baseline-retired`.

The next install records each original again as it first sees it, checked against Steam's manifests when they can be read. Raising `setup::VERSION` sends every player through the setup again. `swofty run --apply` and every install refuse to start until the setup is done. `swofty run` also refuses to start while Steam is verifying or updating the game.

## Recovery rules

Record every change of an install in the journal, in one write, before applying the first. Undo changes in reverse order, including steps that may have been interrupted or never started.

Restore replaced files from backups. Remove files, links and empty folders created by the launcher. Remove the Wine override added for the session.

If a file has unexpected contents, leave it alone and report it. Never restore files from an old game build over a newer one.

## Links on Linux

On Linux the install links files into the game folder instead of copying them, and moves stock files to be replaced aside, instead of restoring them by copy. Windows still copies files in and restores originals from `baseline/objects`.

- `place` makes a symlink to the built file in the data folder. It creates the link under a temporary name and renames it into place.
- The install creates as few links as possible. A file is linked on its own. A folder the game folder lacks is linked as a whole (journal op `LinkDir`), but only when all its files come from one folder in the data folder with the same relative paths, that folder holds exactly those files, and nothing else in the plan (`data` or loader files) targets it. A folder the game folder already has is entered, and the same rules apply to what is inside it.
- `version.dll` and the `swofty/` loader files stay real copies.
- A linked source is made read-only, so a write through the link can't change the data folder. The write bit is given back before the link is removed. `Hashes::restamp` keeps the hash cache valid across the chmod, which changes ctime.
- A stock file the link replaces is renamed to the hidden sibling `.<name>.swofty-orig`. Rolling back gives the link's source its write bit back and renames the hidden file over the link, which writes no data.
- `baseline/objects` stays the source of truth. The hidden file is trusted only when it is a regular file, its size equals the backed-up copy, its metadata is unchanged since it was hashed (`Hashes::unchanged`), and the hash cache still has an entry for it (`Hashes::rename` moves the entry with the file). The hash is not taken again, since reading it would cost what the move saves. Otherwise the original is restored from `baseline/objects`, which checks the hash.
- Removing a file inside a linked folder removes the link, never the target. This covers rollback, sweep, `clear` and `remove_ours`.

If the process dies between the two renames, the entry is still pending, the original is at the hidden name and nothing is at the original path. Rollback renames it back, or copies it from `baseline/objects` when no hash record was saved. With the link in place, rollback renames the original over the link, which replaces it.

`settle_asides` handles hidden files left with no journal: it renames them back where a symlink or nothing stands, and deletes them where the game folder has a real file again. At the start of an install it leaves the places where a symlink stands alone. It does nothing while a journal is open. It runs from sweep, `tidy`, `restore` and at the start of an install. `swofty verify` reports a hidden original left with no running session as an addition.

## Free space

Before an install backs up or copies anything, it adds up what it writes on each volume: on the game folder's, the files it copies less the files they replace (linked files are not counted); on the data folder's, the originals it backs up for the first time and 2 KB per file for the journal and the other state files. Both folders on one volume count together. When a volume has less free space than that, the install stops with `not enough free space on <drive>: this profile needs about <X> GB, <Y> GB free`, in GB of 1024³ bytes. A write that fails on a full volume during the install stops it with the same message, and the session rolls back.

## Sessions

The `swofty run` wrapper holds `session.lock` from before it installs the profile until the offline play staging and the profile are rolled back. It rolls back when the game exits for any reason, on an error, and on a stop signal. A direct `swofty run` waits for the wrapper to exit and rolls back anything the wrapper left.

A session journal still open while the lock is free belongs to a session that ended without rolling back. `swofty run`, `swofty apply` and the wrapper roll back every open journal before they start. The launcher rolls back session journals when it starts, and keeps a `swofty apply` install.

## Leftovers

A file in the way of an install counts as the launcher's own when `placed.json` records those exact bytes at that path, or when it is byte-identical to the file about to be placed and not a backed-up original. The install clears those files, logs the recovery and the `swofty doctor` report, and tries once more. Any other file stops the install untouched and is recorded in `blocked.json`; `swofty restore` moves it to `$DATA/set-aside/<time>/`. The launcher opens the **Your game files have been edited** modal, whose **Restore game folder** button runs the same restore.

At start, files that `placed.json` records and no open journal covers are cleared the same way.

## Damaged state files

State files are written to a temporary file, flushed to disk and renamed over the old one. Backups in `baseline/objects` and restored originals are flushed before they take their name.

A state file that is not JSON at all (empty, zero-filled or truncated) is moved to `<name>.corrupt-<unix time>` and never deleted. Valid JSON in a shape the launcher cannot read is left in place and reported as an error.

| File | When damaged |
|---|---|
| `baseline.json` | Started over in rebuild mode (`"rebuild"` in the file) |
| A journal | Skipped; `placed.json` and the baseline clear its leftovers |
| `placed.json` | Started over empty; leftovers are reported as files of unknown origin |
| A profile or `config.json` | Started over as if missing |

In rebuild mode a game file is recorded as an original only when its bytes are already a backed-up original in `baseline/objects`. Any other file stops the install untouched with "the launcher's record of your original game files is damaged; verify the game files in Steam, then press Play again". The next install records such files at first sight. A file that `placed.json` records as written by the launcher is never recorded as an original, and `swofty verify` reports it until the game files are verified.

`swofty doctor` lists the set-aside copies.

## Commands

- `swofty restore`: restore originals and remove launcher additions.
- `swofty verify`: report remaining changes.
- `swofty doctor`: report changes and check the rest of the setup.

## Tests

`packages/core/tests/restore.rs` covers profile changes, interrupted changes and game updates using a test folder. On Linux it also covers rollback by rename, a crash around the rename, a hidden original deleted or replaced by hand, repeated cycles and hidden originals left with no journal. `packages/core/tests/session_recovery.rs` covers crashed and killed sessions, leftovers of lost journals and files copied in by hand. `packages/core/tests/damaged_state.rs` covers damaged state files.

```sh
cargo test -p swofty-core --test restore --test session_recovery --test damaged_state
```
