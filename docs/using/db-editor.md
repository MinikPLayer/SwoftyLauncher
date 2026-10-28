---
title: DB Editor
description: Change F1 25's game database in the launcher and save your changes as addons to turn on per profile, stack and share.
---

# DB Editor

The **DB Editor** page edits F1 25's game database, `gamedatabases.2025.bin`: drivers, teams, cars, line-ups, tracks, the calendar, weather, points, game modes and every other value the game reads from it. It's built into the launcher.

## Save your changes as an addon

The changes you make are a draft until you save them. The launcher saves the draft after every change, and it's still there when the launcher opens again. The top of the page names what you're editing, **New addon** or the name of an addon, and says how many changes are not saved to the addon yet, or **Saved**. On a narrow window, the buttons move above the name.

- **Save as Addon** turns a new draft into an addon. Give it a name, an author, a category and a description. The new addon is turned on in the active profile, and the launcher switches to **Addons** with the addon highlighted. **Vanilla** stays stock, so an addon saved there stays off.
- **Save** writes your changes into the addon you're editing and switches to **Addons**.
- **Discard changes** puts the addon back to how it was last saved, or a new draft back to no changes.
- **Edit in DB Editor** on an addon's row on **Addons** opens it here. With unsaved changes to another addon, the DB Editor asks whether to save them, discard them or stay.
- **New addon** starts a new draft from the game's stock values.
- **Details…** changes the name, version, author, category and description of the addon you're editing.

DB Editor addons are ordinary addons. **Addons** lists them with the tag **DB Editor**, turns them on and off, sets their order and removes them. When two addons change the same value, the one lower in the list wins. The launcher writes the changes into the game folder when the game starts and rolls the folder back when it exits.

A value another DB Editor addon that's on also changes says so, and says which change wins.

If an addon's files change on disk, for example from `swofty db`, the DB Editor reloads it and keeps your unsaved changes on top. If an addon's `dbedit.json` can't be read, the DB Editor says why and doesn't edit the addon until **Try again** reads it.

## What changes apply to

Most of the database is read when a career, Grand Prix or other mode starts. A career copies its drivers, line-ups, ratings and calendar when you create it, so changes to those apply to careers you start afterwards. To change a career in progress, use the [Career Mode Editor](/using/career-editor). Pages with such a counterpart link to it.

## Find something

Type in the search box at the top of the page, or press `Ctrl+F` or `/`. It searches driver, team and track names, table names, key paths, values and the game's English text. Pick a result to open it.

With Driver Manager installed, search also finds its drivers by name, code and label, such as `dm_208`, under **Driver Manager drivers**. Picking one opens the **Driver Manager** page, or the **Drivers** page when Driver Manager is off.

## Pages

| Group | Pages |
|---|---|
| Drivers | Drivers, Driver looks, Entrants, Characters, Helmets, Suits, Gloves, Poses, Podium emotes, Driver heads, Retirements |
| Teams & Cars | Teams, Line-ups, Cars, Engines, Tyre compounds, Tyre allocations, Mandatory tyres, Garage order, Liveries |
| Tracks & Calendar | Tracks, Track settings, Calendar, Track lists, Season lengths, Weather |
| Rules & Modes | Points systems, Game modes, Mode settings, Features, Crowd |
| Advanced | All tables, Unnamed keys, Changes |

A page shows its rows in a table. Pages with a choice above the table, such as the season on **Track settings** or the grid on **Line-ups**, show the rows of that choice. Click a cell to change it, or a row to see every field in the panel beside the table. Changed values are highlighted, and the arrow next to one puts the game's value back.

**Calendar**, **Garage order** and **Line-ups** have **Move up** and **Move down** on each row.

**Driver Manager** creates new drivers. The **Drivers**, **Entrants** and **Line-ups** pages link to it: the link opens its page when it's installed, offers to turn it on when it's off, and opens its download page when it isn't installed.

## Heads from addons

The **Head model** choices on **Drivers** and **Characters** also list the heads installed addons add to the game, such as an imported [face mod](/using/mods#face-mods), by the head's name and the addon's name. A head needs its addon on in the profile. When it's off, the driver or character keeps the game's head and the launch log names the addon.

## Driver Manager drivers

Driver Manager adds its drivers when the profile builds. The DB Editor's pages don't list them as rows, and they can't be edited here: edit them on the **Driver Manager** page.

With Driver Manager installed, the choices of a field that picks a driver or an entrant also list its drivers, after the game's, such as **Jacob Swofty (Driver Manager)**. These fields include a seat's **Entrant** and **Driver override** on **Line-ups**, an entrant's **Driver** on **Entrants**, **Driver** on **Driver looks** and on **Retirements**. A retired driver shows as **(Driver Manager, retired)**. A driver Driver Manager leaves out isn't listed, apart from one left out for a head or kit that isn't installed. Each driver's value is its label, `dm_` and its driver id, such as `dm_208`.

A value that picks a Driver Manager driver needs Driver Manager on in the profile. When Driver Manager is off, or doesn't add the driver, the launch log names the addon and the driver. **Export game files** names them too: the game files have no Driver Manager drivers.

## Copy a row from someone else

Some fields live in a row of another table, such as a driver's ratings on **Drivers**. A driver without such a row gets the game's default row, and those fields show **Inherited**.

**Copy from…** above the fields gives the driver their own row. It lists every driver who has one, and **Game default**. Search by name and pick one. The fields then hold that driver's values and change like any other value. A copy takes your unsaved changes to the source row.

A copied row says **Copied from** and the source's name, with **Remove own row**, which takes the row and its changes out of the draft. **Discard changes** and **Revert all changes** remove copied rows too. A field the source row doesn't have says **Not in the copied row**: the game default's ratings have no 2026 season, for example.

The build adds the copied row after the table's last row, and it stacks with other addons. It has every key of the source row and its own label. **Changes** lists copied rows under **Copied rows**.

An addon with copied rows needs Swofty Launcher 1.3.4 or later.

## All tables

**All tables** browses the whole database the way the game stores it: containers, tables and their rows, and keys with their type, meaning, unit and range. A table opens as a grid of rows and fields. **Add key** adds a key the game's database doesn't have. The game reads only the keys it looks for, and a missing key it looks for crashes it.

**Unnamed keys** lists the values whose name isn't known yet, by key hash.

## Changes

**All changes** at the top of the page lists every change in the draft, with the game's value and yours. Each change is named by its page's row and column, such as **Default Driver › Race number**; a change no page shows is named by its key path. Revert one change there, or every change with **Revert all changes**. Reverted changes are unsaved until you save.

## Import

**Import…** takes a DB Editor addon `.zip`, a DB Editor `.json` export, an EGO GameDatabases Editor export or a modded game database.

- A DB Editor addon `.zip` installs as a new addon, turned on in the active profile, and the launcher switches to **Addons** with it highlighted. It never replaces an installed addon. When its id is taken, the new addon gets a number after its name. Such a `.zip` also installs from **Addons**.
- Any other file opens as a new draft named after the file, with its changes unsaved. Check them on **Changes**, then **Save as Addon**. With unsaved changes in the current draft, the DB Editor first asks whether to save them, discard them or stay.

## Import from the EGO GameDatabases Editor

**Import…** turns an EGO GameDatabases Editor export of **Modified Records** into a new draft. An EGO export that also adds records isn't imported: only its value changes could be, and those alone can crash the game.

## Import a modded game database

**Import…** also takes a modded `gamedatabases.2025.bin`, or a mod `.zip` that holds one and has no `swofty.json`. It compares the file with the game's stock database and opens the differences as a new draft, named after the file. Changed values, added keys and removed keys all become changes. A file that's the same as the stock database, or isn't a game database, isn't imported.

The import names the tables the file adds rows to. An addon made this way that adds rows to a table doesn't stack with another addon that adds rows to the same table. Rows copied with **Copy from…** stack.

## Share without the launcher

**Export ▸ Share addon (.zip)** saves the addon you're editing as a `.zip` to share. The zip holds the saved addon, without unsaved changes.

**Export ▸ Export game files (.zip)** saves a `.zip` for players who don't use the launcher. It applies the changes in the DB Editor, saved or not, or every DB Editor addon on in the active profile in their order, to the game's stock database. The zip holds the database at its place in the game folder, `game_package/gamedatabases/gamedatabases.2025.bin`, and a `README.txt`.

The README names the addons and their versions, and the game build and database the zip was made for. It tells players to install the zip by extracting it into the F1 25 folder and replacing the file, and to undo it with **Verify integrity of game files** in Steam.

The launcher refuses modded launches while a replaced database file is in the game folder. Players who use the launcher install the addon `.zip` instead.

## When a change no longer applies

A change to a row that's gone, such as a row a game update removed, is skipped when the profile builds. The launch log names the addon and the change.

A row count above the rows its table has, such as **Events** on **Track lists** raised without new events, stays at the rows the table has when the profile builds, and in **Export game files**. The launch log names the addon and the table. The game reads every row up to the count, and a missing row crashes it at launch.
