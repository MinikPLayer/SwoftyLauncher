---
title: Menus and UI
description: Change F1 25's menus from an addon. Find a screen's scene in Swofty UI, edit and export it, ship it with ERP operations or as a new screen, check a UI edit against the engine's rules, then test and debug it in game.
---

# Menus and UI

Every menu screen in F1 25 is a `.ui` resource (the widgets) attached to a [scene](./idf) (an `.idf` layout) in the game's UI archives; an addon changes a menu by shipping edited copies that the launcher builds into the archives when it builds a profile, so the game's own files stay stock.

## A menu edit in full

`swofty.rnd-tyres-thermal` adds a fifth R&D department. Its R&D overview gets a fifth tile: `tile_5` was cloned from `tile_4` in [Swofty UI](./swofty-ui), exported, and shipped with one `replace-resource` per archive that holds the scene:

```text
swofty.rnd-tyres-thermal/
├── swofty.json
├── erp/
│   └── rnd_fifth_pie.json       # the operations Swofty UI exported (rnd_overview.erp.json)
└── ui/
    └── rnd_overview.idf         # the exported scene
```

```json [swofty.json]
// trimmed to the menu edit; the real manifest also lists database, assets and native operations
{
  "schema_version": 1,
  "id": "swofty.rnd-tyres-thermal",
  "name": "R&D: Tyres & Thermal Management",
  "version": "0.11.0",
  "kind": ["database", "erp", "native"],
  "compat": { "game": "f1_25", "game_builds": [">=25251167"], "launcher": ">=1.0.9" },
  "operations": {
    "erp": ["erp/rnd_fifth_pie.json"]
  }
}
```

```json [erp/rnd_fifth_pie.json]
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
    // … the edited laptop_r_and_d.ui and the fifth pie's new scenes, meshes and textures follow
  ]
}
```

The exported scene carries the new element with its ids already correct:

```sh
python3 tools/idf.py tree ui/rnd_overview.idf | grep tile_
```

```text
          'tile_4' [eXRef]  pos=(-4.34812, 0.813113, 0)  scale=(0.999096, 0.999096, 1)  id=2861292184
          'tile_5' [eXRef]  pos=(-0.001875, 0.813113, 0)  scale=(0.999096, 0.999096, 1)  id=1536372253
```

`swofty build` checks that the scene was saved from the same `.idf`, writes it into both archives, and says how it delivers a scene whose size changed:

```text
erp: 2025_asset_groups/ui_package/flow_career_hub.erp built into the game files; 1 resource(s) change their stock layout, which the in-game loader cannot load, such as eaid://ui_package/art/screens/career/rnd_overview.idf: fragment 0 is 30108 bytes, the stock one 29596; stock archives that bind to the replacement read it with the stock layout
```

`tile_5` stays empty in game until a `.ui` node attaches to its name. The addon also ships an edited `laptop_r_and_d.ui` and a native plugin that publishes the fifth department's data.

## Edit a menu

1. **Find the scene.** Open Swofty UI, press <kbd>Ctrl</kbd>+<kbd>K</kbd> and search for the screen ([Open the scene](./editing-menus#_1-open-the-scene)).
2. **Edit and preview it.** Move, resize, clone, rename or delete elements; Swofty UI draws the scene with the game's textures as you go ([Edit it](./editing-menus#_2-edit-it)).
3. **Export it.** Export writes the checked `.idf` and the `replace-resource` operations for every archive that holds it ([Export](./editing-menus#_3-export)).
4. **Ship it in an addon.** List the operations file under `operations.erp` in `swofty.json` ([Add it to your addon](./editing-menus#_4-add-it-to-your-addon)). Pick the route from [Ways to ship a menu change](#ways-to-ship-a-menu-change).
5. **Test it and debug it live.** Build, play, and read `swofty/loader.log`; when a count or position is fixed in native code, trace it with gdb ([Build and play](./editing-menus#_5-build-and-play), [Techniques](./techniques), [Debugging live](./debugging)). For a `.ui` edit, follow [Make a safe UI edit](#make-a-safe-ui-edit).

## Ways to ship a menu change

- **A moved, resized or added element:** `replace-resource` in an `erp` operation file, with the exported `.idf` ([Editing menus](./editing-menus), [ERP operations](/reference/ops/erp#scenes)).
- **A cloned tooltip, label or list entry in a `!pkg` resource:** `patch-pkg` in an `erp` operation file, with `clone-entry` and `set-value` ([ERP operations](/reference/ops/erp#editing-pkg-resources)).
- **A new screen:** `operations.screens`, with a `.ui` file ([In-game UI](/reference/addons/in-game-ui#add-a-screen)).
- **A new home tile:** a site mixin in an engine plugin ([In-game UI](/reference/addons/in-game-ui#add-a-home-tile)).
- **Settings for your addon:** `options`; ADDON SETTINGS builds the page ([Settings](/reference/addons/settings)).
- **Edits to screens, routes and UI databases by node:** `operations.ui` patches. The build skips them for now ([UI patches](/reference/ops/ui-patches)).

## Make a safe UI edit

The game rejects a `.ui` node it can't load without writing a log line. A rejected node takes down its whole screen and every later screen of the same `.ui`, or its whole template instance; a tab whose content template is rejected loses its content. [UI engine internals](./engine) lists every check and what each failure takes down.

1. **Start from the stock resource.** Edit a copy of the stock `.ui` or `.idf` and keep every node, property and behaviour you don't mean to change. Properties the engine reads stay required: a node that lists the `NBVN` behaviour needs its `visualNav` property, and a list control needs `data` bound to an array ([What rejects a node](./engine#what-rejects-a-node)).

   ```json
   // the stock dynamic_era_select root; drop this while slot 6 still lists NBVN and every instance fails
   "visualNav": { "xOffset": 0, "yOffset": 0 }
   ```

2. **Keep the ids and references in step.** Recompute node ids and signal hashes after renaming or moving a node; the engine reads only the hashes. Retarget an `eXRef` by setting `fileRef` and `xref` together ([References](./idf#references)).
3. **Check the build.** Run the checks on each stock archive and the archive your build writes in its place:

   ```sh
   cargo run --release -p swofty-core --example ui_check -- <stock.erp>=<built.erp>
   ```

   Hard findings cite the engine rule, its error code, what a failure takes down and the function address, and stop an addon build. Warnings list what no stock file does ([Checking UI edits](./patterns#checking-ui-edits)).
4. **Look up what stock does.** [UI node reference](./nodes) lists, per node type, the properties, commands, events, pins and layout bindings stock files use; [UI patterns](./patterns) shows how stock screens combine them.
5. **Trace a runtime failure.** A failure the checks pass, such as an ordering or binding problem, shows in the live trace: `re/tools/ui_trace/ui_template_trace.sh` attaches gdb to the running game and prints every rejected template node, failed instance and tab miss with its error code ([Live trace](./engine#live-trace), [Debugging live](./debugging)).

## The map

- **[UI scenes (.idf)](./idf)**: the scene format.
  - **[Where scenes live](./idf#where-scenes-live)**: `UILayout` resources in `2025_asset_groups/ui_package/`, often in several archives.
  - **[The container](./idf#the-container)** and **[elements](./idf#elements)**: `scenelist`, element keys and classes.
  - **[Identity](./idf#identity)**: componentId, elementId, `nodeidlookup` and `xref`.
  - **[References](./idf#references)**: an `eXRef` loads by `xref`, not by `fileRef`.
  - **[How a screen attaches](./idf#how-a-screen-attaches)**: by componentId and by leaf name.
  - **[What a scene stores](./idf#what-a-scene-stores)**, and what animations and stackers replace.
  - **[Tools](./idf#tools)**: `tools/idf.py`.
- **[Swofty UI](./swofty-ui)**: the scene viewer and editor.
  - **[Get it](./swofty-ui#get-it)** and **[start it](./swofty-ui#start-it)**: release binary or `tools/swofty-ui/serve.py`, the game folder, options.
  - **[The window](./swofty-ui#the-window)**, **[references](./swofty-ui#references)** and **[edits](./swofty-ui#edit)**.
  - **[Export](./swofty-ui#export)**, its **[checks](./swofty-ui#export-checks)**, and **[`--check-refs`](./swofty-ui#check-every-reference)**.
  - **[Keyboard and mouse](./swofty-ui#keyboard-and-mouse)**.
  - **[HTTP API](./swofty-ui#http-api)**: open, apply, export and replay edits from scripts.
- **[Editing menus with an addon](./editing-menus)**: one scene from Swofty UI to the game, step by step, and its **[limits](./editing-menus#limits)**.
- **[Techniques](./techniques)**: tracing what draws an element, data limits against screen limits, counts fixed in native code, patching native builders safely, the **[test loop](./techniques#test-loop)**, and a checklist for AI agents.
- **[Debugging live](./debugging)**: gdb on the running game under Proton or Wine: tracepoints, watchpoints, reading memory.
- **[UI node reference](./nodes)**: every stock node type and scene element type, with properties, commands, events, pins, wiring and layout bindings.
- **[UI patterns](./patterns)**: how stock screens combine nodes.
  - **[Tab containers](./patterns#tab-containers)**, **[multi-row lists](./patterns#multi-row-lists)**, **[navigation between rows](./patterns#navigation-between-rows)**, **[focus and input blocking](./patterns#focus-and-input-blocking)**, **[masks](./patterns#masks-and-clipping)**, **[runtime bindings](./patterns#runtime-bindings)**.
  - **[Checking UI edits](./patterns#checking-ui-edits)**: engine rules, format checks, warnings, where the checks run.
- **[UI engine internals](./engine)**: what the game does with a `.ui`.
  - **[Load](./engine#load-one-ui-resource)** and **[node records](./engine#node-records-and-classes)**.
  - **[What rejects a node](./engine#what-rejects-a-node)** and **[what a failure takes down](./engine#what-a-failure-takes-down)**.
  - **[Template instantiation](./engine#template-instantiation)** and **[tabs](./engine#tabs-uitc)**.
  - **[Signals](./engine#signals-commands-and-events-at-runtime)**, **[layout binding](./engine#layout-binding)**, **[logs and debug switches](./engine#logs-and-debug-switches)**.
  - **[The era picker container failure](./engine#the-era-picker-container-failure)** and the **[live trace](./engine#live-trace)**.
