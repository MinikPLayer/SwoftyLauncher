---
title: Swofty UI
description: A viewer and editor for F1 25's menu scenes. Browse every .idf in the game, see it with the game's textures, move, clone, rename and delete elements with undo, and export checked files for an addon.
---

![](/swofty-ui/logo.png){.docs-logo width=80 height=80}

# Swofty UI

Swofty UI is a viewer and editor for F1 25's menu [scenes](./idf) that runs on your computer, opens in your browser, reads the game's UI archives without changing them, draws each scene with the game's own textures and colours, and exports edited scenes ready to ship in an [addon](./editing-menus).

![Swofty UI with the R&D overview scene open: the hierarchy on the left, the scene drawn with its textures in the middle, and tile_4 selected in the inspector on the right](/swofty-ui/overview.png)

## A session in full

Start it from the extracted launcher folder:

```sh
bin/swofty-ui
```

```text
Swofty UI
Reading the UI archives in C:\Program Files (x86)\Steam\steamapps\common\F1 25\2025_asset_groups\ui_package (found via Steam)
Indexed 5208 scenes in 435 archives (0.8s)
Exports go to C:\Users\you\AppData\Local\Swofty\swofty-ui\exports

Swofty UI is running at http://127.0.0.1:7341/ — close this window or press Ctrl+C to stop
```

Your browser opens the editor. Everything the editor does also works over HTTP; this clones `tile_4` into `tile_5` one row lower and exports it:

```sh
base=http://127.0.0.1:7341
doc=$(curl -s "$base/api/open?eaid=art/screens/career/rnd_overview.idf" | jq -r .doc)
curl -s "$base/api/export" -H 'Content-Type: application/json' -d '{
  "doc": "'"$doc"'",
  "ops": [
    { "op": "clone", "key": "0.0.2.5.0", "name": "tile_5", "position": [0, -2.52, 0] }
  ]
}' | jq '{file, operationsFile, ok: .checks.ok}'
```

```json
{
  "file": "C:\\Users\\you\\AppData\\Local\\Swofty\\swofty-ui\\exports\\rnd_overview.idf",
  "operationsFile": "C:\\Users\\you\\AppData\\Local\\Swofty\\swofty-ui\\exports\\rnd_overview.erp.json",
  "ok": true
}
```

[Editing menus with an addon](./editing-menus) takes the export into the game.

## Get it

### From a release

Launcher releases after 1.0.6 include Swofty UI. Install the launcher as in [Installation](/guide/installation), then:

- **Windows:** double-click `bin\swofty-ui.exe` in the install folder.
- **Linux and Steam Deck:** run `bin/swofty-ui` from a terminal. The install folder also has a desktop entry and icons under `share/`.

It needs no Python.

### From source

Swofty UI is in the launcher's repository, in `tools/swofty-ui`. It needs Python 3.14 or newer. Python 3.10 to 3.13 also work with the `zstandard` module (`pip install zstandard`) or the `zstd` command installed.

```sh
git clone https://github.com/Swofty-Developments/SwoftyLauncher
cd SwoftyLauncher
python3 tools/swofty-ui/serve.py
```

Without either, it stops with:

```text
Reading the game archives needs zstd: run Python 3.14 or newer, or install the zstandard module or the zstd command.
```

## Start it

Swofty UI finds the game, reads the list of scenes, and opens the editor in your default browser.

- It listens on port 7341. When another program uses that port, it takes the next free one and opens that address instead.
- Close the window, or press <kbd>Ctrl</kbd>+<kbd>C</kbd>, to stop it.
- If the browser doesn't open, open the address it printed.

### The game folder

Swofty UI looks for F1 25 in this order:

1. The folder given with `--game`.
2. The `F1_25_DIR` environment variable.
3. The folder you picked with **Locate game** in the launcher.
4. Your Steam libraries, including extra library folders and Flatpak Steam.

`--game` takes the F1 25 folder, a copy of it, or its `2025_asset_groups/ui_package` folder. If Swofty UI can't find the game, it says where it looked and how to pass the folder, and waits for <kbd>Enter</kbd> before closing. Run it from a terminal in the extracted launcher folder:

```text [Windows]
bin\swofty-ui.exe --game "D:\SteamLibrary\steamapps\common\F1 25"
```

```sh [Linux]
bin/swofty-ui --game "$HOME/.local/share/Steam/steamapps/common/F1 25"
```

Swofty UI never writes to the game folder. It only reads the archives, writes exports to its export folder, and refuses an export folder inside the game folder:

```text
The export folder … is inside the game folder. Swofty UI never writes to the game folder; pick another --export-dir.
```

### Options

```text
--game <folder>         the game folder to read
--no-game               start without the game, to open .idf files from disk
--open <scene>          open a scene first: a path such as art/screens/career/rnd_overview.idf, or an .idf file
--export-dir <folder>   where exports go; default swofty-ui/exports in the launcher's data folder:
                        %LOCALAPPDATA%\Swofty on Windows, ~/.local/share/swofty on Linux
--port <n>              the first port to try, 7341 by default
--host <address>        the address to listen on, 127.0.0.1 by default
--no-browser            don't open the browser
--check-refs            check every scene reference in the game's UI archives, print the result and exit
                        (see Check every reference)
```

## Open a scene

Press <kbd>Ctrl</kbd>+<kbd>K</kbd> or **Open scene** and type part of a scene's path. Words match anywhere in it, so `career rnd` finds `art/screens/career/rnd_overview.idf`. Each result shows how many archives hold the scene. To open a file instead, paste its path, or choose **Open .idf from disk**.

![The Open scene search with "career rnd" typed, listing rnd_dept.idf, rnd_perks.idf, rnd_overview.idf and more, each with its folder and archive count](/swofty-ui/open.png)

When several archives hold the scene, the picker next to its path in the title bar switches between their copies.

## The window

- **Hierarchy**, on the left, is the element tree. Each row shows the element's class, `xref` marks a reference to another scene, and `id≠path` marks a [reference](#references) whose `xref` id and `fileRef` name different scenes. Filter it by name, click a row to select it, and double-click a row to zoom to it. Under it, **Edits** lists every edit in order.
- **The canvas**, in the middle, draws the scene. Click an element to select it. The chips at the top left count the elements of each class; click one to hide that class.
- **Inspector**, on the right, shows the selected element: its path and `elementId` with a check against its path, its local transform with its world position and size, its box, the scene it references, its params and animations. With nothing selected, it shows the scene's `componentId`, `nodeidlookup` and reference checks.
- **The status bar** shows the element count, the id checks, the number of edits and the last export.

### Display

- **Textures + colours** (<kbd>T</kbd>): draws each element's mesh with its BC7 texture and material colour, decoded from the archives.
- **Outlines** (<kbd>O</kbd>): draws each element's box, coloured by class.
- **Text placeholders**: writes each text element's name in its box.
- **Reveal anim-collapsed**: shows elements the game keeps at a scale near 0 until an animation or live data grows them.
- **XRef contents** (<kbd>X</kbd>): draws the scene each `eXRef` loads in game inside it, clipped by `eWindow` elements.
- **1920x1080 frame**: draws the screen as 19.2 × 10.8 units, centred on the origin.
- **Grid, Pivots, Labels**: draw the grid, each element's origin and element names.

![The R&D development-potential pie: on the left its four wedges are collapsed, on the right Reveal anim-collapsed shows them at full size](/swofty-ui/reveal.png)

The canvas is a preview. Gradients, rounded corners and circular fills are approximated, glass blur is a dark panel, and text shows element names instead of the game's strings. Animations and live data are not drawn.

### References

An `eXRef` stores the scene it references twice: its `fileRef` param holds the scene's path, and its `xref` holds the scene's componentId. The game loads the scene whose componentId is `xref` and ignores `fileRef` when it looks the scene up. Swofty UI does the same: it draws, outlines and opens the scene the `xref` id names. See [References](./idf#references).

The inspector's **XRef** section shows `fileRef`, the `xref` id with a check against md5-64 of `fileRef`, and the scene the game loads. When the id and the path name different scenes:

- the hierarchy row shows `id≠path`, and the canvas outlines the element in red with an `xref id ≠ fileRef` tag;
- the inspector says which scene the game loads, and offers **Load `<fileRef scene>` (set the id)** and **Keep `<id scene>` (set fileRef)**, which retarget the reference to one of the two;
- the status bar counts the mismatched references;
- [export](#export-checks) is refused until every reference agrees.

A referenced scene that itself holds such a reference gets a yellow note in the inspector, listing the references inside it and the scenes they load.

## Edit

- **Move:** drag the element, or press the arrow keys: 0.01 units a press, 0.1 with <kbd>Shift</kbd>. Hold <kbd>Shift</kbd> while dragging to keep to one axis. A move changes the element's local `position`.
- **Transform:** type a position, scale or rotation in the inspector. **Z angle** rotates the element in the screen plane.
- **Params:** edit numbers, vectors and toggles in the inspector's params. Text params are read-only.
- **Clone:** <kbd>Ctrl</kbd>+<kbd>D</kbd> or **Clone**. The copy is a new sibling with the next free name, such as `tile_5` for `tile_4`. When the element has a numbered sibling before it, the copy continues the spacing between them.
- **Rename:** type a new name in the inspector. Names must be unique in the scene and can't contain `.`.
- **Retarget:** type a scene path in the inspector's **XRef** section and press **Retarget**. It sets `fileRef`, the `xref` id the game loads by, and `XrefMaterialBinding` together. Editing `fileRef` alone isn't possible.
- **Delete:** <kbd>Del</kbd> or **Delete**.
- **Undo, redo:** <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>.

Clone, rename and delete keep the scene's ids valid: they set each changed element's `elementId` to the djb2 of its new path, give clones new guids, and rebuild `nodeidlookup`. See [Identity](./idf#identity).

Your browser keeps unsaved edits for each scene. When you open a scene you edited before, Swofty UI offers to restore them.

## Export

Press <kbd>Ctrl</kbd>+<kbd>S</kbd> or **Export .idf**. Swofty UI replays your edits on the original bytes, checks the result, and writes three files to its export folder:

```text
<scene>.idf          the edited scene
<scene>.erp.json     the ERP operations that ship it: one replace-resource per archive that holds the scene,
                     with source set to ui/<scene>.idf
<scene>.edits.json   the source scene and archive, the SHA-1 of the source and the export, and the edit list
```

```json [rnd_overview.edits.json]
{
 "source": { "kind": "erp", "eaid": "eaid://ui_package/art/screens/career/rnd_overview.idf", "erp": "flow_career_hub.erp", "label": "art/screens/career/rnd_overview.idf" },
 "sourceErps": ["flow_career_hub.erp", "flow_team_hq.erp"],
 "sourceSha1": "…",
 "exportSha1": "…",
 "exportedAt": "2026-10-27T10:00:00+1100",
 "ops": [ { "op": "clone", "key": "0.0.2.5.0", "name": "tile_5", "position": [0, -2.52, 0] } ]
}
```

An export replaces an earlier export of the same scene. The [ERP operations](/reference/ops/erp#scenes) file holds one stanza per archive. The dialog also has **Download .idf** and **Open export folder**.

### Export checks

The export's `checks` object, as `/api/export` returns it:

```text
reparse              the file reads back as a scene
roundtrip            reading and writing it again gives the same bytes
elementIdsOk         every elementId is the djb2 of its element's dotted path (elementIdsBad lists up to 10 paths)
nodeidlookupOk       nodeidlookup is the sorted, de-duplicated djb2 of every element name
componentIdsKept     the scene's componentId is unchanged, so the build accepts it for the same .idf
xrefIdsOk            every eXRef's xref id is md5-64 of its fileRef: the game loads the scene fileRef names
                     (xrefMismatches lists the ones that aren't)
identicalToSource    the file is byte-identical to the source
ok                   roundtrip, elementIdsOk, nodeidlookupOk, componentIdsKept and xrefIdsOk all hold
```

When the references check fails, Swofty UI writes nothing. The dialog lists each reference, its `fileRef` and the scene the game loads instead. Retarget each one, then export again. Swofty UI's own edits keep references consistent; a mismatch comes from a scene another tool wrote.

The dialog also says whether the file differs from the source. An export with no edits is identical to the source, and the same edits always give the same bytes.

### Check every reference

`--check-refs` reads every scene in the game's UI archives, checks each reference, and exits with status 1 if any `xref` id and `fileRef` disagree. Run it on a built game to check the scenes your addons ship:

```sh
bin/swofty-ui --check-refs --game "$HOME/.local/share/Steam/steamapps/common/F1 25"
```

```text
Checked 58925 references in 26913 scene copies (5208 distinct, 5208 scenes) in 5.5s
33 consistent references name a scene no UI archive holds
0 reference(s) whose xref id is not md5-64 of fileRef
```

On stock archives it finds no mismatch. Each mismatch prints its scene, element, archives and the scene the game loads.

## Keyboard and mouse

| Key | Action |
|---|---|
| <kbd>Ctrl</kbd>+<kbd>K</kbd>, <kbd>/</kbd> | Open a scene |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> | Export |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Undo |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Y</kbd> | Redo |
| <kbd>Ctrl</kbd>+<kbd>D</kbd> | Clone the selected element |
| <kbd>Del</kbd>, <kbd>Backspace</kbd> | Delete the selected element |
| Arrow keys | Move the selected element by 0.01 units; with <kbd>Shift</kbd>, by 0.1 |
| <kbd>P</kbd> | Select the parent |
| <kbd>F</kbd>, <kbd>Shift</kbd>+<kbd>F</kbd> | Fit the scene, fit the selection |
| <kbd>T</kbd>, <kbd>O</kbd>, <kbd>X</kbd> | Textures, outlines, referenced scenes |
| <kbd>Esc</kbd> | Close a dialog, or clear the selection |
| Wheel | Zoom |
| Middle or right drag, <kbd>Space</kbd>+drag | Pan |
| <kbd>Alt</kbd>+click | Select the next of several overlapping elements |

In an inspector field, <kbd>Enter</kbd> applies the value, <kbd>Esc</kbd> reverts it, and <kbd>↑</kbd>/<kbd>↓</kbd> step it by 0.01, or 0.1 with <kbd>Shift</kbd>. **Z angle** steps by 1°, or 10° with <kbd>Shift</kbd>.

## Example: a fifth R&D tile

The R&D overview screen lays out four department tiles in a fixed 2 × 2 grid. Each tile is an `eXRef`, `tile_1` to `tile_4`, under an animated parent, `transition_null_1` to `transition_null_4`. This adds `tile_5` one row below `tile_4`.

1. Press <kbd>Ctrl</kbd>+<kbd>K</kbd>, type `rnd_overview`, and open `art/screens/career/rnd_overview.idf`.
2. In the hierarchy, select `tile_4`, under `transition_null_4`. Leave `transition_null_4` where it is: its animation sets its position.
3. Press <kbd>Ctrl</kbd>+<kbd>D</kbd>. Swofty UI adds `tile_5` next to `tile_4`, with `elementId` `djb2("screen_null.tut_scaler.transition_null_4.tile_5")` and `djb2("tile_5")` added to `nodeidlookup`.
4. In the inspector, set **Position** to X `0`, Y `-2.52`. The grid's rows are 2.52 units apart, so `tile_5` now sits one row below `tile_4`, over the lower info panel. A finished layout moves the elements around it too.

   ![tile_5 selected one row below tile_4, marked new in the hierarchy, with three edits in the Edits list](/swofty-ui/clone.png)

5. Press <kbd>Ctrl</kbd>+<kbd>S</kbd>. All checks pass, and the export folder gets `rnd_overview.idf`, `rnd_overview.edits.json` and `rnd_overview.erp.json`, with stanzas for `flow_career_hub.erp` and `flow_team_hq.erp`.

   ![The export dialog: every check passed, the three file paths, and the ERP operations for flow_career_hub.erp and flow_team_hq.erp](/swofty-ui/export.png)

6. Ship it as in [Editing menus with an addon](./editing-menus#_4-add-it-to-your-addon).

`tile_5` draws the referenced tile's art in Swofty UI. In game, a tile's title and values come from the `.ui` node attached to it, so `tile_5` stays empty until a node attaches to its name. See [Limits](./editing-menus#limits).

## HTTP API

Swofty UI's editor talks to its server over HTTP on the same address, and scripts can too. Open a scene to get a `doc` id, then send edits to it, as in [a session in full](#a-session-in-full). To replay an export's edits on a fresh copy of the scene, send its `ops`:

```sh
curl -s "$base/api/export" -H 'Content-Type: application/json' \
  -d "$(jq -c --arg doc "$doc" '{doc: $doc, ops: .ops}' rnd_overview.edits.json)" | jq .checks.ok
```

```text
GET  /api/health                                  { ok: true, app: "swofty-ui" }
GET  /api/catalog                                 every scene with the indexes of the archives that hold it, and the export folder
GET  /api/open?eaid=<path>[&erp=<archive>]        open a scene from the archives; returns doc and the scene model
GET  /api/open-file?path=<file>                   open an .idf file
POST /api/upload?name=<name>                      open the .idf in the request body
POST /api/apply      { doc, ops }                 replay the edits and return the model, with each edit's result
POST /api/export     { doc, ops, name? }          write and check an export; returns the file paths, operations, size and checks
GET  /api/download?f=<file>                       the exported .idf
POST /api/reveal                                  open the export folder in the file manager
GET  /api/xref?eaid=<path>[&reveal=1]             the flattened boxes and geoms of a referenced scene, nested references resolved by
                                                  xref id, and mismatches for references inside it whose xref and fileRef disagree
POST /api/visuals    { items: [{ res, mat, erp }] }   the mesh, material params and texture names of each geom
GET  /api/texture?name=<.tif.image>               the top mip's BC7 bytes, size in X-Tex-Width and X-Tex-Height
GET  /api/resources?erp=&prefix=                  list an archive's resources
GET  /api/resource?erp=&name=&frag=               read one fragment
```

### Edits

`ops` is a list of edits, applied in order. `key` addresses an element by child indexes: `0` is the root of the first scene, `0.0` its first child, and `0.0.2.5.0` is `tile_4` in `rnd_overview.idf`. Keys refer to the tree as it is when that edit runs, so an edit after a clone or delete can address the new tree. The inspector shows each element's key.

```json
[
  { "op": "set",      "key": "0.0.2.5.0", "field": "position", "value": [0, -2.52, 0] },   // position or scale [x, y, z], rotation [x, y, z, w]
  { "op": "param",    "key": "0.0.2.5.0", "name": "pivotTrans", "value": [0, 0, 0, 0] },    // a number, vector or toggle param
  { "op": "clone",    "key": "0.0.2.5.0", "name": "tile_5", "position": [0, -2.52, 0] },    // a new sibling named name; position is optional
  { "op": "rename",   "key": "0.0.2.5.0", "name": "tile_four" },
  { "op": "delete",   "key": "0.0.2.5.0" },                                                  // the element and its children
  { "op": "retarget", "key": "0.0.2.5.0", "fileRef": "art/components/career/rnd_tree/rnd_dev_potential.idf" }
                                                                                             // sets fileRef, xref and XrefMaterialBinding
]
```

Each element in the model has `fileRef`, `xref` and, for an `eXRef`, `ref`: `{ path, id, pathId, ok, scene }`, where `scene` is the scene the game loads by `id` and `ok` is `id == pathId`. A `param` edit can't change `fileRef`; use `retarget`.

### Errors

```text
400   an edit failed: { error: "edit #N failed: …", opIndex: N-1 }
410   the doc id expired: "document expired; reopen it"
422   /api/export only: a reference's xref and fileRef disagree; { error, checks, exported: false }, nothing written
403   a request with an unexpected Host header, or a cross-origin POST
```

On a loopback address, the server only answers requests addressed to `127.0.0.1`, `localhost` or `[::1]` on its port, and refuses requests that other sites' pages send.
