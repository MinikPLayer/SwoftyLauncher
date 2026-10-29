# Command line

From your terminal, `swofty` lets you manage addons and mods, build and run profiles, check the game files and read logs.

## `swofty run`

```sh
swofty run
```

Run this to launch F1 25 and follow its logs. Modded play is [offline](/reference/safety) only. When Steam is closed, `swofty run` starts it and waits up to 90 seconds for it to sign in; see [Steam](/reference/launch#steam).

When you leave the game, you'll get a summary with the exit code, how the game ended and any crash details. `swofty run` exits once the session's game files are rolled back.

To stop following the logs while keeping the game running, press **Ctrl-C**. When Steam launched the game, `swofty run` exits right away. For a direct launch, it keeps waiting to roll the game files back when the game exits; press **Ctrl-C** again to leave now, and the next start rolls them back.

| Flag | Purpose |
|---|---|
| `--profile <name>` | Choose a profile; otherwise, use the active profile |
| `--game <dir>` | Play this game folder instead of the [active copy](/using/game-copies) |
| `--restart` | Close the copy's running F1 25 first. Another copy's game, or one the launcher can't match to a copy, is left running |
| `--direct` | Launch directly instead of asking Steam |
| `--no-start-store` | Don't start Steam or the EA app when it's closed. `--no-start-steam` still works. The launcher's **Start Steam automatically** setting does the same for every launch |
| `--vr` | Start the game in VR, like Steam's **Launch in SteamVR mode**: passes `-vr` to the game and, on Linux, starts SteamVR first. See [Playing in VR](/using/vr) |
| `--winedebug <channels>` | Choose Wine log channels |
| `--exe <file>` | Use a copy of `F1_25.exe` to identify crash locations |
| `--apply` | Install the profile (database, tracks, in-game loader) before the game starts and roll it back when it exits. The launcher always passes this flag |
| `--use-overlayfs` | Linux only. Run the game from an OverlayFS mount instead of copying files into the game folder. Falls back to copying when the mount isn't possible; see [OverlayFS sessions](/reference/launch#overlayfs-sessions) |

On Linux, the [Steam launch option](/guide/linux) uses `swofty run -- %command%`, or `swofty run --use-overlayfs -- %command%` for [OverlayFS sessions](/reference/launch#overlayfs-sessions).

`swofty overlay-exec` is internal: `swofty run` starts it to make the mount and run Proton inside it. It doesn't appear in `swofty --help`, and its arguments aren't stable.

For a direct launch, you'll find the wrapper's output in `$DATA/state/wrapper-run.log`, and `swofty run` prints what the wrapper reports after the game exits. If the wrapper exits before the game starts or while it runs, `swofty run` prints the wrapper's last lines and its error, rolls back anything the wrapper left, then exits with an error itself. It stops waiting once the wrapper has written nothing for 10 minutes and has no build running, or after 2 minutes when Steam launches the game. A build writes `[info] build: still working after <seconds> s` at least once a minute, and keeps `state/build.json` current every 2 seconds. An install that takes longer than 10 seconds writes `[info] installing: <copied> of <total> file(s) copied` every 10 seconds.

Before anything else, `swofty run` and `swofty apply` roll back what an unfinished session left in the game folder and print `Recovered the game folder from an unfinished session` with what they put back and the `swofty doctor` report. If a session is still rolling back, they wait up to 2 minutes for it, and then say to close F1 25 only when the game is running. If a session is still building or copying the mods, they stop at once with its step and progress, for example `Swofty is still copying the mods into the game folder (443 of 2,544 files) for profile default, started 2 h 36 min ago`. The wrapper's `build: still working after` lines name the step the build is on.

If the wrapper receives a stop signal (Steam's **Stop**, `kill`, a closed terminal), it closes the game it started and rolls the session back before exiting.

## `swofty logs`

To read a finished session's summary, use `swofty logs [<session dir>]`. Leave out the folder to see the latest session.

### `swofty logs send`

```sh
swofty logs send --name <username> --message <text> [--dry-run [<zip>]]
```

Sends your logs to the developers on Discord, like **Send Logs to Developers** in the launcher. [Send your logs to the developers](/using/send-logs) lists what the zip holds.

| Flag | Purpose |
|---|---|
| `--name <username>` | Your Discord username, 2 to 32 characters. Without it, the command uses the last username sent, from `config.json` `"discord_username"` |
| `--message <text>` | What happened and what you were doing, 10 to 1000 characters. Needed unless `--dry-run` is given |
| `--dry-run [<zip>]` | Write the zip to `<zip>` (a file or a folder; the current folder when left out), list its files, and send nothing |

When the data folder has no launch log (`state/launcher-run.log`, `state/wrapper-run.log`) and no game session, the command warns that the logs show no game launch yet. The command prints its progress and exits with an error when Discord can't be reached or refuses the upload, naming the HTTP status. A zip over 9 MB goes in several messages, labelled `part 1/3` and so on. Builds from source can't send; there, only `--dry-run` works.

Without `--dry-run`, the command keeps the zip in `$DATA/logs/swofty-logs-<username>-<time>.zip`, beside the launcher's, and removes all but the newest 10. After sending, it prints the zip's path and size. When sending fails, it prints the path before the error.

## `swofty build`

Use `swofty build [--profile <name>]` when you want to prepare your profile's database and track files without starting the game. You'll find the output in `$DATA/cache/overlay/<profile>/<buildhash>/`: `swofty/settings.json` holds the session settings, and `plan.json` lists the game files to replace and add, along with their sources. Track archives, menu archives and car handling files are built in `$DATA/cache/maps`, `$DATA/cache/menus` and `$DATA/cache/ers`. Once the build succeeds, it removes older builds from those folders and your profile's overlay folder, keeping any that another profile's latest build uses. Rebuilt archives are also kept in `$DATA/cache/archives`. A later build that needs the same archive links it from there instead of writing it again. The folder keeps archives used in the last 3 days, up to 10 GB.

After the database operations, the build checks each `add-row` copy for the label-keyed side data it lacks and warns for the known lookups it misses, with one line per addon counting the matches to review. `--side-data` lists every finding; see [The side-data check](/reference/ops/database#the-side-data-check).

The build also prepares addon ERP files and prints their warnings. Files with a `patch-pkg` edit other than `clone-entry` and `set-value` are skipped. Addon ERP operations work on the archives the track stages have already built; `add-mipmaps` files replace or add loose game files. If your build includes a `remove-resource`, it rewrites the archives even when `swofty_core` lists `erp` as `stable`. `file.put` operations and `operations.files` entries also replace or add loose game files. Other addon ERP and UI operations are currently skipped.

Some assets go into `swofty/assets.json` instead. This applies to each asset kind that `swofty_core` lists as `stable` in `swofty/addons/core/features.json`, or as `experimental` and named in `config.json` `"runtime_assets"`. These assets are asset groups, changed ERP resources, database records that differ from the game file, one language table per language and overlay files. For `group` and `lang` operations, this is the only delivery route: without the feature, they wait, and the build lists them. To keep `erp`, `db` or `file` kinds on the build route, list them in `config.json` `"build_assets"`. When the addons add more database records than the game's database takes once it has loaded (its map grows at 80% of its slots, 838,860 records for the stock game), the build puts the whole database into the game file instead, and the build log has a `database: built into the game file` line with both counts.

The output has a line per addon with what it writes, such as `Example Livery: 412 database values, 3 archive resources, 2 files`. Each pair of addons in conflict, where a later addon replaced what an earlier one wrote, prints a warning such as `Example Livery overrides Grid 24 on 12 database values and 1 file`.

When the game exits, `swofty run` merges any settings you changed in game into your profile. Choose **APPLY & RESTART** and it rebuilds the profile, then starts the game again.

## `swofty conflicts`

```sh
swofty conflicts [--profile <name>] [--check] [--game <dir>]
```

Prints the conflicts of the profile's last build, from `$DATA/cache/overlay/<profile>/conflicts.json`: a line per pair of addons, the winner first, then the overridden items. With `--check`, it builds the profile without installing it and prints that build's conflicts.

```text
Example Livery overrides Grid 24 on 1 database value and 1 file
  entrants.teams.team[_metadata_=mclaren_26].stat_id
  livery_package_shared/sponsor.dds
```

| Field in `conflicts.json` | Value |
|---|---|
| `profile` | The profile. |
| `built_at` | When the build ran, in Unix seconds. |
| `pairs` | `{ winner, loser, database, resources, files, strings, items }` per pair: the addon ids, how many database values, archive resources, game files and text strings the winner overrides, and up to 1000 item names. |
| `order` | The profile's turned-on addon ids in load order at the build. |

A database value shows as its path in the DB Editor's catalogue, or as a `0x…` key when the catalogue doesn't name it. An archive resource shows as `<archive>:<resource>`, a file as its game path, a text string as `text <key>`.

## `swofty apply`

Close the game, then use `swofty apply [--profile <name>] [--game <dir>]` to build your profile and install it into the game folder. This includes the database, the archives addons patch, the enabled tracks, their menu entries, the cars' ERS settings, and the [in-game loader](/reference/addons/#what-the-launcher-does-with-an-addon) with the session's `swofty/settings.json`. Every change goes through the journal. To remove the installation, run `swofty restore` or `swofty --profile vanilla apply`.

## `swofty verify`

Use `swofty verify [--game <dir>]` to check whether your game files differ from the backed-up originals and whether they can be read and written correctly.

## `swofty restore`

Close the game, then run `swofty restore` to put back the backed-up files and remove launcher additions. Files of unknown origin that stopped the last launch are moved to `$DATA/set-aside/<time>/` rather than deleted. If anything can't be restored, the command tells you.

See [Restoring the game](/using/profiles-and-restoring).

## `swofty addons`

| Command | Purpose |
|---|---|
| `swofty addons list` | List installed addons and whether they are enabled; an addon this launcher can't read is `unusable`, with the reason |
| `swofty addons install <path>` | Install an addon folder, a `.swoftyaddon` file or a zip that holds an addon |
| `swofty addons enable <id>` | Enable an addon |
| `swofty addons disable <id> [--yes]` | Disable an addon; SwoftyMod cannot be disabled |
| `swofty addons remove <id> [--yes]` | Delete an installed addon and take it out of every profile; built-in addons cannot be removed |

For an addon whose manifest sets [`saves.affects`](/reference/addons/manifest#saves), `disable` (when the addon is on) and `remove` print `Warning: <name> (<id>) has affected your saves. Careers you played with it may not load without it.`, then the addon's message, and ask `[y/N]`. `--yes` (`-y`) skips the question. When stdin is not a terminal, the command needs `--yes`; without it, nothing changes and the command exits with status 1.

If you use `swofty mods install <addon folder>` or `swofty mods enable|disable <addon id>`, they still run the `swofty addons` command and print a note.

Built-in addons add their own top-level commands; run `swofty --help` to see them.

If you've used an earlier version, each command first moves its stored data: addons from `$DATA/mods/` to `$DATA/addons/`, and tracks from `$DATA/maps/` into the Team Simplified library. Those tracks stay turned on in the same profiles. `swofty --help`, `swofty --version` and mistyped commands leave the data folder alone.

## `swofty mods`

These commands come from the built-in Team Simplified addon. Use them to manage its mod library and your profile's load order.

| Command | Purpose |
|---|---|
| `swofty mods list` | List the library in load order, with each mod's id and whether it is on |
| `swofty mods import <path>…` | Import `.serps` packages, zips or folders that hold an `F1 25` folder; new mods are turned on |
| `swofty mods remove <id>` | Delete a mod from the library and from every profile |
| `swofty mods enable <id>` | Turn a mod on in the profile |
| `swofty mods disable <id>` | Turn a mod off in the profile |
| `swofty mods move <id> <position>` | Move a mod in the load order; position 1 loads first, and a later mod wins |

## `swofty settings`

| Command | Purpose |
|---|---|
| `swofty settings list [<id>]` | List addon settings with the profile's values and any pending rebuild |
| `swofty settings set <id> <option> <value>` | Set a value: `true`, `5`, `replica` |
| `swofty settings reset <id>` | Put an addon's settings back to their defaults |
| `swofty settings export [--out <file>]` | Write the session settings file that the game reads |
| `swofty settings merge` | Merge the changes made in game into the profile |

## `swofty profile`

| Command | Purpose |
|---|---|
| `swofty profile list` | List the profiles and mark the active one |
| `swofty profile delete <name> [--yes]` | Delete a profile with its settings, build cache (`$DATA/cache/overlay/<name>/`), in-game changes and saves. It asks first; `--yes` skips the question |

`swofty profile delete` follows the same rules as the launcher's [Delete a profile](/using/profiles#delete-a-profile). It refuses `vanilla`, your only profile, any profile while F1 25 runs, and a profile with an open journal in the game folder; run `swofty restore` to close the journal. When you delete the active profile, `default` (or the first remaining profile) becomes active first.

## `swofty saves`

| Command | Purpose |
|---|---|
| `swofty saves list` | List the profile save backups in `$DATA/save-backups/`, newest first, with the profile, why each was taken, the addons the session left out, and the file count and size |
| `swofty saves restore <backup>` | Copy a backup back over the profile save. It refuses while F1 25 or Steam runs and backs up the current save first |

Before a session whose profile leaves out an installed addon with [`saves.affects`](/reference/addons/manifest#saves), `swofty run` copies the Steam profile save (`savegame@profile#*` in `userdata/<id>/3059520/remote`) into `$DATA/save-backups/<time>-<profile>/` with a `manifest.json` of the files' sizes and SHA-256 hashes, and prints `saves: backed up the profile save before a session without <addon ids> (their careers are removed if the game saves): <folder>`. It keeps the newest 10 backups. The EA app copy's saves are not backed up.

When the session leaves the profile save changed and at least 24 KB smaller than its backup, `swofty run` prints `saves: the profile save shrank from <before> to <after> bytes in a session without <addon ids>; ...` with the backup's name and the restore command, and the Addons page shows the same notice with **Restore** and **Dismiss**.

After a restore, Steam may ask which copy to keep the next time F1 25 starts; keep the local files.

## `swofty postfx`

```sh
swofty postfx compile <addon dir> [--out <dir>]
```

Compiles the addon's [post-process passes](./addons/postfx) with dxc the way the build does, without installing the addon. Each pass prints `ok` with its DXIL size, or `FAIL` with dxc's whole output. `--out` also writes each pass's DXIL as `<name>.dxil`. The exit code is 1 when a pass fails or dxc is missing.

## `swofty db`

These commands work on DB Editor addons. A DB Editor addon is a data addon of game database changes in `$DATA/addons/<id>/`, with its changes in `dbedit.json`. Its id starts with `dbedit.`.

| Command | Purpose |
|---|---|
| `swofty db addons` | List the DB Editor addons and whether each is on in the profile |
| `swofty db new <name>` | Create an empty DB Editor addon |
| `swofty db delete <id>` | Delete a DB Editor addon and take it out of every profile |
| `swofty db export <id> <file.zip>` | Write a DB Editor addon to a zip |
| `swofty db import <file>` | Add a DB Editor addon from a DB Editor `.zip`, a DB Editor `.json` export, an EGO GameDatabases Editor export, a modded `gamedatabases.2025.bin` or a mod `.zip` that holds one |
| `swofty db export-game <file.zip> [--addon <id>]…` | Write the game database with DB Editor addons applied to a zip that players without the launcher extract into the game folder |
| `swofty db off-all` | Turn off every DB Editor addon that is on in the profile |
| `swofty db details <id> [--name <name>] [--version <version>] [--author <author>] [--description <text>] [--category <category>]` | Show a DB Editor addon's details, or change the ones you pass |
| `swofty db search <query> [--limit <n>]` | Search names, tables, keys, values and English text |
| `swofty db get <path> [--addon <id>]` | Show a key's value, or what a table, row or container holds |
| `swofty db set <path> <value> --addon <id>` | Change a key's value in the addon |
| `swofty db add <path> <type> <value> --addon <id>` | Add a key the game database lacks; `<type>` is the record type number |
| `swofty db copy-row <table> <from> <to> --addon <id> [--field <field>]` | Add a row to `<table>` for `<to>`, a copy of `<from>`'s row, or of the game default row when `<from>` is `default` |
| `swofty db remove-row <table> <to> --addon <id> [--field <field>]` | Remove a row `copy-row` added, with its changes |
| `swofty db revert <path> --addon <id>` | Put a key back to its stock value in the addon |
| `swofty db revert --all --addon <id>` | Remove every change from the addon |
| `swofty db list --addon <id>` | List the addon's changes |

A `<path>` is a dotted path, a selector path such as `entrants.drivers.driver[_metadata_=max_verstappen].car_number`, or `0x` and 16 hex digits.
`swofty db get` without `--addon` shows stock values.
`<table>` names the rows without a row number, such as `driver_statistics.driver_statistics.DriverStatisticsHolder`.
`--field` is the field that names a row's entity, such as `driver`. Without it, `copy-row` and `remove-row` use the field the DB Editor pages pick the table's rows by, else `_metadata_`.
`copy-row` refuses an entity that already has a row in the table.
The new row has every key of the source row, nested tables included, with `--field` set to `<to>`, its own `_metadata_` label, and its own `stat_id` in a table of stat ids.
The build adds it after the table's last row, after the addons before this one.
`swofty db set`, `revert` and `get --addon` take selector paths into a copied row, such as `driver_statistics.driver_statistics.DriverStatisticsHolder[driver=romain_grosjean].StatisticsDataSet[_metadata_=default].Statistic[_metadata_=pace].evolution`.
A `<category>` is one of `seasons`, `tracks`, `cars`, `helmets`, `career`, `ai`, `performance`, `utility` and `uncategorised`.
A new DB Editor addon has no category and the origin tool `db-editor` in its `swofty.json`.
`swofty db import` of a DB Editor addon zip creates a new addon with the zip's name, version, author, description, category and changes, and copies no other file.
The new addon's id comes from its name.
When that id is taken, the new addon gets a new id and a number after its name.
`swofty addons install` of a DB Editor addon folder or zip does the same, and never replaces an installed addon.
`swofty db import` of a `.json` file creates a new addon named after the file.
`swofty db import` of a `.bin`, or of a zip that holds a `gamedatabases.2025.bin` and no `swofty.json`, creates an addon of its differences from the stock database, named after the file.
It refuses a file that is the same as the stock database or is not a game database.
It prints how many keys the file changes, adds and removes, and the tables it adds rows to.
A zip with `swofty.json` and without `dbedit.json` is not a DB Editor addon; install it with `swofty addons install`.
`swofty db export-game` applies the addons you name with `--addon`, in that order, or else the DB Editor addons that are on in the profile, in load order.
It starts from the game's stock database and writes `game_package/gamedatabases/gamedatabases.2025.bin` and a `README.txt` to the zip.
It prints each change that no longer matches the game database, and leaves it out.
`swofty db off-all` refuses `vanilla`.
Turn a DB Editor addon on or off with `swofty addons enable <id>` and `swofty addons disable <id>`.
DB Editor addons load in the profile's addon order, and a later addon wins.
The build skips a DB Editor addon's change that no longer matches the game database and prints a warning naming the addon.
Commands that read the game database take `--game <dir>` after `db`.
`--package <id>` and `swofty db packages` still work, as older names of `--addon <id>` and `swofty db addons`.

## `swofty setup`

| Command | Purpose |
|---|---|
| `swofty setup status` | Show whether the one-time setup before modded play is done |
| `swofty setup reset [--other-mods]` | Restore the game folder and start the record of original game files over |
| `swofty setup done` | Finish the setup after verifying the game files in Steam or repairing them in the EA app |

`swofty run --apply` and `swofty apply` refuse to start until the setup is done. See [The one-time setup](/reference/restore#the-one-time-setup).

## `swofty offline`

| Command | Purpose |
|---|---|
| `swofty offline status` | Show whether you've accepted the offline play terms, and the launch extension's digests and where it came from |
| `swofty offline accept` | Read the offline play terms and accept them |
| `swofty offline withdraw` | Withdraw consent; modded launches are refused |
| `swofty offline fetch` | Download the pinned release of the launch extension and verify its digests |
| `swofty offline install <dir>` | Verify and install the launch extension from a downloaded release folder |

See [Offline play](/reference/safety).

## Tracks

Tracks are mods: import a community track package with `swofty mods import <zip>`. Its mod id is the track code, or `<code>-track` if a mod in your library already has that id.

## `swofty update`

Close F1 25 before running `swofty update`; it refuses to update while the game runs. The command checks GitHub for the latest release and, if it's newer, installs it into the folder you're running the command from:

1. It downloads the archive for your platform and verifies it against the release's `SHA256SUMS`.
2. It extracts the archive to `.update/` in that folder.
3. It replaces the files in `bin/`, `loader/`, `addons/` and, on Linux, `share/`, and removes files from `loader/` and `addons/` that the new release no longer has.

If a step fails, it puts back the files replaced so far and removes `.update/`. You need to run the command from an extracted release it can write to; otherwise, it prints the release page instead.

To check for a newer release without installing it, use `swofty update --check`.

On Linux, each new file is renamed over the old one. On Windows, the old file moves into `.update/` first; the launcher removes `.update/` when you next start it. If a release has no `SHA256SUMS`, the command checks the download against the size and digest GitHub lists for the asset.

The launcher's update check reads `"updates"` in `$DATA/config.json`:

| Key | Purpose |
|---|---|
| `check` | `false` turns off the launcher's check for new launcher releases; `swofty update` still works and Simplified for Swofty is still kept up to date. The default is `true` |

Older configs may still hold `skip`, `checked_at` and `latest` under `"updates"`, and `checked_at` under `"managed"`. The launcher ignores them and drops them the next time it saves the config.

The launcher reads the latest release tag from the `releases/latest` redirect on github.com, which doesn't count against GitHub's API rate limit. It calls the GitHub API once for each new launcher release, to get its downloads, and waits for `Retry-After` or the rate limit reset when GitHub refuses a request. For Simplified for Swofty, it lists the 20 newest releases through the GitHub API when it opens, when the latest tag changes and every 15 minutes. It reads each new release's launcher requirement once, from a `swofty.json` release asset when there is one and from the package otherwise.

To test an update, set `SWOFTY_UPDATE_PRETEND_VERSION=0.1.0`. The launcher and `swofty update` will treat the running version as `0.1.0`.

## `swofty steam`

```sh
swofty steam [--start]
```

Shows whether Steam is running and signed in, with what the launcher read it from, and whether Steam is verifying or updating F1 25's files. It exits with an error when Steam isn't running or signed in. With `--start`, it starts Steam when it's closed and waits up to 90 seconds for it to sign in, as **Play** does, without launching the game. See [Steam](/reference/launch#steam).

## `swofty game`

```sh
swofty game list
swofty game use <steam|ea|folder>
swofty game add <folder>
swofty game launch [--vr] [--no-start-store] [--game <dir>]
```

From 1.3.1. `list` shows every copy of F1 25 the launcher found, with its store, version and folder, and `*` next to the active one. `use` picks the copy that `run`, `apply`, `build` and the launcher play. `add` adds a folder with `F1_25.exe` and makes it the active copy. See [Steam and EA app copies](/using/game-copies).

`launch` asks the EA app to start the EA app copy as it's installed, without your profile or offline play, so EA AntiCheat runs. It starts the EA app first when it's closed, prints the game's process ID once F1 25 starts, and exits. `--vr` passes `-vr`. It works only on an EA app copy.

## `swofty doctor`

Use `swofty doctor` to check Steam, your game copies (`copy` rows, the active one marked), your game install, Proton, launch options and the data folder. You'll also see any changes from the backed-up originals. The `steam client` row shows whether Steam is running and signed in. On Windows, the `run as admin` row shows whether `F1_25.exe` is set to run as administrator, which stops modded sessions from starting (see [Launching the game](/reference/launch#windows)).
