---
title: Editing menus with an addon
description: Open a menu's scene in Swofty UI, edit it, export the checked .idf with its replace-resource operations, and ship it in an addon that the launcher builds into the UI archives.
---

# Editing menus with an addon

To move elements or add a slot on a menu screen, edit the screen's [scene](./idf) in [Swofty UI](./swofty-ui), export it, and ship the exported `.idf` in an addon with one `replace-resource` [ERP operation](/reference/ops/erp#scenes) per archive that holds the scene.

## The whole addon

```text
my-addon/
├── swofty.json
├── erp/
│   └── scenes.json          # rnd_overview.erp.json from the export
└── ui/
    └── rnd_overview.idf     # the exported scene
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "author.rnd-layout",
  "name": "R&D layout",
  "version": "1.0.0",
  "author": "Your Name",
  "compat": { "game": "f1_25" },
  "kind": ["erp"],
  "operations": { "erp": ["erp/scenes.json"] }
}
```

```json [erp/scenes.json]
{
  "schema_version": 1,
  "ops": [
    {
      "op": "replace-resource",
      "erp": "2025_asset_groups/ui_package/flow_career_hub.erp",
      "resource": "eaid://ui_package/art/screens/career/rnd_overview.idf",
      "source": "ui/rnd_overview.idf"
    },
    {
      "op": "replace-resource",
      "erp": "2025_asset_groups/ui_package/flow_team_hq.erp",
      "resource": "eaid://ui_package/art/screens/career/rnd_overview.idf",
      "source": "ui/rnd_overview.idf"
    }
  ]
}
```

```sh
swofty addons install ./my-addon
swofty addons enable author.rnd-layout
swofty build
```

`swofty.rnd-tyres-thermal` ships its fifth R&D tile with these same two operations (`addons/rnd-tyres-thermal/erp/rnd_fifth_pie.json`).

## 1. Open the scene

Start Swofty UI and press <kbd>Ctrl</kbd>+<kbd>K</kbd>. Search for the screen by file name or folder, such as `rnd_overview` or `career laptop`, and press <kbd>Enter</kbd>. The title bar shows the scene's path, and the archive picker beside it lists every archive that holds it.

```sh
bin/swofty-ui --open art/screens/career/rnd_overview.idf   # or open it straight away
```

Swofty UI reads the archives in the game folder as they are. After `swofty apply`, those include your own edits; run `swofty restore` first to start from stock.

## 2. Edit it

Select an element on the canvas or in the hierarchy, then:

- drag it, or nudge it with the arrow keys, to move it;
- type a new position, scale, rotation or param value in the inspector;
- press <kbd>Ctrl</kbd>+<kbd>D</kbd> to clone it as a new sibling, with a new name and the ids it needs;
- rename it in the inspector, or press <kbd>Del</kbd> to delete it;
- for an `eXRef`, type another scene's path in the inspector's **XRef** section and press **Retarget** to make it draw that scene.

Every edit goes in the **Edits** list, and <kbd>Ctrl</kbd>+<kbd>Z</kbd> undoes it. Before moving an element, look for an **Anims** section in the inspector: an animated element's position is replaced when its animation plays, so move its unanimated children instead.

The same edits as an [HTTP API](./swofty-ui#http-api) list:

```json
[
  { "op": "clone", "key": "0.0.2.5.0", "name": "tile_5", "position": [0, -2.52, 0] }
]
```

## 3. Export

Press <kbd>Ctrl</kbd>+<kbd>S</kbd>. Swofty UI writes the scene, [checks it](./swofty-ui#export-checks), and saves three files to its export folder:

```text
rnd_overview.idf          the edited scene
rnd_overview.erp.json     an ERP operations file: one replace-resource per archive that holds the scene
rnd_overview.edits.json   the edit list, the source scene and archive, and the SHA-1 of the source and the export
```

## 4. Add it to your addon

Put the `.idf` in your addon's `ui/` folder and the operations file in `erp/`, as in [the whole addon](#the-whole-addon), and list the operations file under `operations.erp` in the [manifest](/reference/addons/manifest).

Each `replace-resource` replaces the scene's single fragment in one archive. Keep every archive Swofty UI listed: the game reads a scene from whichever archive loads it first.

## 5. Build and play

```sh
swofty build
```

Then press **Play** in the launcher. The build:

1. Checks that each `source` is a scene saved from the same `.idf`, by its [componentId](./idf#identity), and fails otherwise:

   ```text
   the source is scene 0x…, this resource is scene 0xbe419b0b8324e33c; it was saved from another .idf
   ```

2. Writes the new fragment, recompresses it, and updates the archive's sizes, offsets and hashes. Every other resource is copied byte for byte.
3. Delivers the archive. The in-game loader's overlay only carries a replacement with the stock fragment size. An edited scene usually changes size, so the build writes those archives into the game files through the journal and prints:

   ```text
   erp: 2025_asset_groups/ui_package/flow_career_hub.erp built into the game files; 1 resource(s) change their stock layout, which the in-game loader cannot load, such as eaid://ui_package/art/screens/career/rnd_overview.idf: fragment 0 is 30108 bytes, the stock one 29596; stock archives that bind to the replacement read it with the stock layout
   ```

4. Warns when another stock archive in the same folder also holds the resized scene and your addon doesn't replace it there:

   ```text
   erp: eaid://ui_package/art/screens/career/rnd_overview.idf is resized, but <archive> also carries it and keeps the stock copy; replace it there too, or the game may show either one
   ```

The launcher restores the rewritten archives when the game exits. `swofty restore` puts them back by hand, such as after `swofty apply` or a crash. See [ERP operations](/reference/ops/erp#scenes) for the operation itself.

## Limits

- **Live data needs `.ui` wiring or native code.** A scene holds rectangles, not content. Text, numbers, lists and colours come from the `.ui` node attached to an element and the view model behind it. A new element stays empty until a node attaches to its name, either in the screen's `.ui` or built by an [engine plugin](/reference/addons/in-game-ui). Data the screen's view model doesn't publish needs native code.
- **Animations and stackers override stored positions.** An animated element takes its transform from its clip while the clip plays. An `eStacker` lays out its children at runtime, so a child's stored position is replaced; a child added to a stacker is placed with the rest.
- **New art needs new textures or meshes.** A scene can reuse art: a clone keeps its geoms, and an `eXRef` can point at another scene. A new image is a new texture resource ([Textures and models](/reference/data/textures)), and a new shape, such as a narrower pie wedge, is a new mesh.
- **A reference loads the scene its `xref` id names.** The game finds an `eXRef`'s scene by its `xref` id, the md5-64 of the scene's path, not by its `fileRef` path. A scene written by another tool with a new `fileRef` and the old `xref` still shows the old scene in game. Swofty UI draws the scene the id names, marks the mismatch and refuses to export it; see [References](./idf#references).
- **A scene replaces the whole stock scene.** When a game update changes a scene your addon ships, export it again from the new stock scene. The `.edits.json` holds your edits for [replaying them](./swofty-ui#http-api).

## Next

- [Techniques](./techniques): tracing what draws an element, counts fixed in native code and patching them safely.
- [Debugging live](./debugging): attaching gdb to the running game.
- [ERP operations](/reference/ops/erp#scenes): every field of `replace-resource`.
- [Packaging and publishing](/reference/addons/publishing): sharing the addon.
