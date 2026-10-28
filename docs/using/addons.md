---
title: Addons
description: Install addons, find them with search and filters, turn them on per profile, set their load order, check their conflicts and remove them on the Addons page.
---

# Addons

Everything the launcher loads is an addon: a folder with a `swofty.json`. A data addon changes the game's database, text, archives and files; a code addon adds an engine plugin, a launcher page or both. Mods you download for the game folder become addons through the [Simplified Importer](/using/mods), and changes you make in the [DB Editor](/using/db-editor) save as addons.

## Install an addon

The two cards at the top of the **Addons** page are the ways in:

- **Installing a Swofty Addon?** Drop a `.swoftyaddon`, a zip of an addon or an addon folder on the card, or click it to choose a file. **Choose a folder…** picks an addon folder. You can also drop an addon anywhere on the launcher window.
- **Installing a mod?** **Go to Simplified Importer** opens the [Simplified Importer](/using/mods), which turns the mod into an addon.

A file that isn't an addon, and that no addon such as the Simplified Importer imports, opens a dialog that names it. The dialog lists up to five files. **Go to Simplified Importer** opens the [Simplified Importer](/using/mods), and **Read the addon docs** opens the [addon reference](/reference/addons/) in your browser.

The launcher copies the addon to `$DATA/addons/<id>/`, opens **Addons** and highlights it. Installing an addon with an id that's already installed replaces it. A new addon starts off in every profile.

## Find an addon

Type in the search box on the left to search names, ids, authors and descriptions. Under it, the filters narrow the list:

| Filter | Values |
|---|---|
| Category | What the addon is for: Seasons, Tracks, Cars, Helmets, Shaders, Career, AI, Performance, Utility, or Uncategorised. Built-in addons show no category. Shaders are [colour looks](/reference/addons/shader-packs) for the race. |
| Type | What it changes: Database, Archives, Text & UI, Simplified, Files, Code. Simplified is a mod merged into the game's shader packages, as the Simplified Suite loads `.serps` mods. |
| Source | Built in, DB Editor, Imported, Installed. Imported addons come from the [Simplified Importer](/using/mods). |
| State | On, Off, Has conflicts, Update available |

Under its name, each row shows the addon's category, its types and a tag with its source. An addon that turns on an experimental feature of the game's core, such as the [overlay](/reference/std/overlay) or a [shader pack](/reference/addons/shader-packs)'s look, also has a **Uses experimental features** tag, and its panel lists the features. Tags also say what the addon does in game: **Draws over the game** (the overlay) and **Changes the picture** (a shader pack's look). Its panel lists them under **In game**. Each value shows how many addons match. **Clear** removes every filter. An open panel closes when its addon no longer matches.

On a window less than 760 pixels tall, the install cards shrink to one row.

## Turn addons on or off

Use the switch on a row to turn an addon on or off in the active [profile](/using/profiles). An addon keeps its place in the list when it is off; its row is dimmed and has no load order number. A new addon starts off in every profile. The next time you press **Play**, the launcher builds the profile with the change.

Some addons, such as Driver Manager, change what your careers store. Turning one of those off asks first: careers you played with it may not load without it. Select **Turn off anyway** to go ahead, or **Cancel** to leave it on.

**Turn off all** turns off every addon except those that show **Always on** and those that would ask about your saves; the launcher names those. **Undo** on the message that follows turns the same addons back on in their old order.

In the `vanilla` profile the list is read-only. Switch to a modded profile to turn addons on.

## Folders

The list groups addons into folders:

- **Built In** holds SwoftyMod and Simplified for Swofty. It is always first and its addons can't be moved.
- Each category with an addon gets a folder of the same name, such as **Seasons** or **AI**. A new addon goes into its category's folder.
- Uncategorised addons sit outside the folders.
- **New folder**, above the list, makes a folder of your own. A new folder starts at the end of the list. The pencil on its row renames it, and the bin removes it; its addons go back to their category's folder or out of folders.

Drag an addon onto a folder to put it at the end of that folder, or between two of its rows to put it there. Drag it between two rows outside a folder, or onto **Drop here to move to the end**, to take it out. An addon you move into another folder stays there; one you take out stays out of folders.

Select a folder to close or open it. Folders stay closed between launches, and a search or filter opens them all.

Folders are shared by every profile. The launcher saves them in `$DATA/folders.json`.

## Load order

The list is the load order, top to bottom, and the number on a row is its place among the addons that are on. When two addons change the same thing, the one lower in the list wins. A folder's addons load together. Dragging a folder by its handle moves all of them and keeps their order. Drag a row, or use the arrows in its panel, to move an addon. The arrows move it within its folder. Rows that are always on have no drag handle. SwoftyMod and Simplified for Swofty always load first: nothing can be dragged above them.

When the launcher opens a profile whose addons aren't grouped by folder, it moves each folder's addons up to the first of them and keeps their order. That can change which addon wins between two in different folders.

After a conflict check, a row that is on shows **Loses to** with the number of addons lower in the list that replace some of its changes, or **Wins over** with the number it replaces. The badges go away when an addon is turned on, turned off or moved, until the next check.

An addon loads after every addon it requires, whatever their places in the list. A row that is on and listed above an addon it requires shows **Loads after** with that addon's name. A row that is on whose required addon isn't installed, is off, or has a version outside the range shows **Needs** with its name, and **Play** stops until that addon is on. The panel's **Requires** line says which.

A row whose addon asks to remove records the game's database ships with shows a badge such as **12 removals skipped**. The build leaves those records in place, and the panel's **Conflicts** section says so. See [Removing stock records](/reference/ops/database#removing-stock-records).

## Check an addon

Select a row to open its panel: description, author, version, source, whether it works with your game version, what it changes (database values, game assets, text, files, tracks), its settings and its conflicts. **Game version** says **Works with this game version**, or names the game build the addon was made for next to yours. **Advanced** shows the addon's id.

**Conflicts** lists the addons this one overrides and the ones that override it, with what they both change. The list comes from the profile's last build. **Check conflicts**, at the top of the page and in the panel, builds the profile again without starting the game and refreshes the list. The list says it is out of date after an addon is turned on, turned off or moved, or after the addon itself changes. Play waits while a check runs.

Select a conflict to see what it covers, grouped by archive, database table, text or folder. **Details** lists the items, ten at first; **Show all** lists the rest.

An addon from the DB Editor has **Edit in DB Editor**.

## Change its settings

Change a value under **Settings** in the addon's panel. **Reset to defaults** puts every setting back. Settings save to the active profile; you can change them in game too, see [In-game addon settings](/using/swofty-mod).

## Remove an addon

Select **Remove** in the addon's panel and confirm. Removing an addon deletes its files and takes it out of every profile. Built-in addons can't be removed.

Removing a DB Editor addon deletes its changes. To keep them, select **Export** › **Share addon (.zip)** in the [DB Editor](/using/db-editor) first.

## Addons that can't be used

An addon made for a newer Swofty Launcher, or one whose `swofty.json` is damaged, is listed under **Can't be used** with the reason, such as "It needs a newer Swofty Launcher (unknown field `hud`)". The launcher leaves it out of builds, and **Remove** deletes it. The rest of your addons work as before. Installing such an addon is refused with the same reason.

A profile saved by a newer launcher keeps the settings this launcher doesn't know, and saves them back unchanged.

## Built-in addons

| Addon | What it does |
|---|---|
| **SwoftyMod** | Loads addons in game and adds **ADDON SETTINGS**. It's always on in modded profiles |
| **Simplified for Swofty** | Team Simplified's addon: the [Simplified Importer](/using/mods). The launcher installs it, [keeps it up to date](/using/updating#simplified-for-swofty) and keeps it on |

Every other addon, including [Swofty's own](/using/swofty-addons), comes from the [Addon Browser](/using/addon-browser).

## From a terminal

| Command | Does |
|---|---|
| `swofty addons list` | List installed addons and whether each is on in the profile |
| `swofty addons install <path>` | Install a folder, `.swoftyaddon` or zip |
| `swofty addons enable <id>` | Turn an addon on |
| `swofty addons disable <id>` | Turn an addon off |
| `swofty addons remove <id>` | Remove an addon and take it out of every profile |
| `swofty conflicts [--check]` | Show the profile's conflicts from its last build, or build it again first |
| `swofty settings list [id]` | List addon settings with the profile's values |
| `swofty settings set <id> <setting> <value>` | Change a setting |
| `swofty settings reset <id>` | Put an addon's settings back to their defaults |

Add `--profile <name>` to work on a profile other than the active one. For an addon that has affected your saves, `disable` and `remove` print the warning and ask before going ahead; `--yes` skips the question and is required outside a terminal.

## Next

- [Simplified Importer](/using/mods): turn a mod into an addon.
- [DB Editor](/using/db-editor): change the game database and save it as an addon.
- [Profiles](/using/profiles): keep different sets of addons apart.
- To make an addon, see [Addons](/reference/addons/) in the developer docs.
