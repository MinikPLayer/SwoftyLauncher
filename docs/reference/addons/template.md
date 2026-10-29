---
title: Templates
description: Four starting points in the repository. templates/addon for data and an engine plugin, templates/launcher-addon for a launcher plugin, templates/postfx-pass for a post-process pass and templates/shader-pack for colour looks. What each file is and how to build it.
---

# Templates

A template is a working addon in the repository's `templates/` folder. Copy it, change `id` in `swofty.json`, then build and install it:

```sh
cp -r templates/addon my-addon && cd my-addon
# set "id": "yourname.my-addon" in swofty.json
native/build.sh                           # builds native/sample_plugin.dll
swofty addons install .
swofty addons enable yourname.my-addon
swofty run --apply
```

All four templates are MIT-licensed; your addon can use any licence.

- [`templates/addon`](#templates-addon): [data](/reference/data/) and an [engine plugin](./native-plugins). Database and UI edits driven by settings, plus a C++ plugin.
- [`templates/launcher-addon`](#templates-launcher-addon): a [launcher plugin](./launcher-api). A dashboard page, a page with a switch per item, an importer, a build hook and a command.
- [`templates/postfx-pass`](#templates-postfx-pass): a [post-process pass](./postfx). A vignette with saturation, with settings for both.
- [`templates/shader-pack`](#templates-shader-pack): a [shader pack](./shader-packs). Three colour looks, with settings to pick one and its strength.

## templates/addon

[`templates/addon`](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/templates/addon) connects settings to data edits and an engine plugin. The settings drive `gamedatabases` and UI edits, and the C++ plugin reads them too.

```text
templates/addon/
├── swofty.json                  a toggle, a choice and two sliders (one db, the rest native); database, ui and native operations
├── db/example.json              a set whose value comes from the penalty_seconds setting, and a set through a row selector
├── ui/example.pkgpatch.json     a !pkg patch that clones a tooltip entry in tool_tips.uidb
├── native/
│   ├── src/sample_plugin.cpp    reads its settings, reacts to in-game changes, logs career loads, runs one signature scan
│   ├── include/                 the plugin C ABI (swofty_api.h) and the C++ layer (swofty/*.hpp)
│   └── build.sh                 builds native/sample_plugin.dll with zig c++ for x86_64-windows-gnu
└── README.md                    build and install steps
```

```json [db/example.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "{{penalty_seconds}}" },
    {
      "op": "set",
      "select": "entrants.entrant_setup_modes.entrant_setup_mode[_metadata_=f1_26]",
      "field": "max_entrants",
      "value": "22"
    }
  ]
}
```

```json [ui/example.pkgpatch.json]
{
  "schema_version": 1,
  "targets": [
    { "erp": "2025_asset_groups/ui_package/databases.erp", "resource": "eaid://ui_package/databases/tool_tips.uidb" }
  ],
  "edits": [
    {
      "op": "clone-entry",
      "at": "tool_tips.Contents.tooltips_dictionary.Contents",
      "src": "settings_fe_home",
      "as": "sample_mod_hint",
      "set": { "Name": "sample_mod_hint", "Contents.text.Value": "Sample Mod" }
    }
  ]
}
```

Build the plugin, then install and enable the addon:

```sh
native/build.sh
swofty addons install .
swofty addons enable example.sample-mod
```

```text
built /path/to/my-addon/native/sample_plugin.dll
```

For data edits only, remove the `native` entry from `swofty.json` and the `native/` directory. The folder then installs as it is, without a compiler.

[Settings](./settings) explains the template's four settings and the plugin code that reads them.

## templates/launcher-addon

[`templates/launcher-addon`](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/templates/launcher-addon) is a Rust helper program with one dependency, `serde_json`. It talks to the launcher over stdin and stdout, and answers `hello`, `view`, `event`, `tick`, `import`, `build`, `command` and `summary`.

```text
templates/launcher-addon/
├── swofty.json      launcher names the helper for each OS, the Overview and Example sections, the exmod import extension, the build hook and the example command
├── Cargo.toml       one binary, example-helper, depending on serde_json
├── src/main.rs      the helper: a dashboard page, a page of imported .exmod files with a switch each, an importer, a build hook and the example command
└── README.md        build and install steps
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "example.launcher-addon",
  "name": "Example Launcher Addon",
  "version": "0.1.0",
  "author": "Your Name",
  "description": "A dashboard page with every launcher block, a page with a switch per item, an importer for .exmod files and a build hook.",
  "license": "MIT",
  "compat": { "game": "f1_25" },
  "launcher": {
    "program": { "windows": "bin/example-helper.exe", "linux": "bin/example-helper" },
    "sections": [
      { "id": "overview", "title": "Overview", "icon": "activity" },
      { "id": "items", "title": "Example", "icon": "package" }
    ],
    "imports": ["exmod"],
    "build": true,
    "commands": [{ "name": "example", "about": "List the example items" }]
  }
}
```

- **Overview** is a dashboard built from `dashboard.json` in the addon's data folder, with every launcher block. It ticks while open.
- **Example** lists the imported `.exmod` files with a per-profile switch for each, so you choose which items are on.
- **The importer** copies dropped `.exmod` files into the addon's data folder, `$DATA/addon-data/example.launcher-addon/items/`.
- **The build hook** writes the names of the items that are on to `enabled.txt` and installs it with `file.put`:

```rust [src/main.rs]
Ok(json!({
    "ops": [{
        "op": "file.put",
        "source": { "root": "cache", "path": "enabled.txt" },
        "target": "example-launcher-addon/enabled.txt"
    }],
    "lines": [format!("{} items on", on.len())]
}))
```

- **`swofty example`** prints each item and whether it's on.

Build and install the helper:

```sh
cargo build --release
mkdir -p bin
cp target/release/example-helper bin/
swofty addons install .
swofty addons enable example.launcher-addon
```

On Windows, build `example-helper.exe` and copy it to `bin/`. Ship both binaries in `bin/`.

## templates/postfx-pass

[`templates/postfx-pass`](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/templates/postfx-pass) is a [post-process pass](./postfx): a vignette with saturation, and a setting for each.

```text
templates/postfx-pass/
├── swofty.json        the postfx experimental feature, vignette and saturation sliders (both db), and the postfx block that names the shader and passes the sliders to it
├── fx/vignette.hlsl   the pass
└── README.md          how to change the pass, and install steps
```

```json [swofty.json]
"category": "shaders",
"experimental_features": ["postfx"],
"postfx": [
  { "shader": "fx/vignette.hlsl", "name": "vignette", "options": ["{{vignette}}", "{{saturation}}"] }
]
```

The build compiles the pass with the launcher's dxc; see [Build](./postfx#build).

```sh
swofty addons install .
swofty addons enable example.vignette-pass
```

## templates/shader-pack

[`templates/shader-pack`](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/templates/shader-pack) is a [shader pack](./shader-packs): colour looks for the race, picked with a setting.

```text
templates/shader-pack/
├── swofty.json             the shaders category and experimental feature, a look choice and a strength slider (both db), and the shader block that names the look's .cube file
├── looks/monochrome.cube   the three looks, 17×17×17 each
├── looks/overcast.cube
├── looks/noir.cube
├── looks/make_looks.py     the script that wrote the three .cube files
└── README.md               how to make your own look, and install steps
```

```json [swofty.json]
"category": "shaders",
"experimental_features": ["shaders"],
"shader": { "lut": "looks/{{look}}.cube", "strength": "{{strength}}" }
```

It needs no compiler. Install and enable it as it is:

```sh
swofty addons install .
swofty addons enable example.monochrome-looks
```

## Share

Share the folder as it is, or zip its contents with `swofty.json` at the root and give the archive the `.swoftyaddon` extension. Include your changes and compiled binaries, never copies of the game's archives, database or executable. See [Packaging and publishing](./publishing).

## Next

- [Your first addon](./first-addon): a launcher plugin written from scratch, step by step.
- [Data addons](/reference/data/), [Engine plugins](./native-plugins) and [Launcher plugins](./launcher-api): each path in full.
- [Settings](./settings): the template's four settings, explained.
