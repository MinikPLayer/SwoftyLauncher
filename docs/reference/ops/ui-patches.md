---
title: UI patches
description: The planned UI patch format for operations.ui — targets, node paths, screen edits, route and UI database edits. The build skips these files today.
---

# UI patches

A UI patch describes changes to menus, labels and routes, as a `*.pkgpatch.json` file listed in `operations.ui`.

::: warning Not built yet
The build skips `operations.ui` and prints:

```text
warning: yourname.home-text: 1 UI operation file(s) skipped; that stage is not built yet
```

This page is the format for the planned build step. To change a menu today, ship an edited scene with `replace-resource` ([Editing menus](/reference/ui/editing-menus)), or edit a `!pkg` resource with [`patch-pkg`](/reference/ops/erp#editing-pkg-resources).
:::

## A UI patch in full

```jsonc [ui/home.pkgpatch.json]
{
  "schema_version": 1,
  "targets": [                                       // the resources to change; the home screen has two copies
    { "erp": "2025_asset_groups/ui_package/flow_frontend.erp",
      "resource": "eaid://ui_package/screens/fe_home.ui", "fragment": 0 },   // fragment is optional, default 0
    { "erp": "2025_asset_groups/ui_package/screens/fe_home.erp",
      "resource": "eaid://ui_package/screens/fe_home.ui", "fragment": 0 }
  ],
  "root": "Screens[0]:fe_home",                      // optional: node paths are relative to it
  "edits": [                                         // run in order
    { "op": "set-text", "node": "contextual_panels.offline_contextual_panel.subtitle",
      "value": "Launched with Swofty Launcher, press", "localise": false }
  ]
}
```

```json [swofty.json]
"operations": {
  "ui": ["ui/home.pkgpatch.json"]
}
```

One kind of [operation](/reference/data/operations); see the [manifest](/reference/addons/manifest#operations).

## Screen edits

```jsonc
{ "op": "clone-node", "src": "panel.button_a", "as": "button_b",          // copy src to a sibling named as
  "set": { "visible": true },                                           // optional: change properties of the copy
  "replace_event": { "event": "on_click", "signals": [ { "to": "flow", "pin": "open" } ] } }  // optional
{ "op": "set-text", "node": "panel.title", "value": "Garage", "localise": false }  // a label's text and localise flag
{ "op": "set-props", "node": "panel.title", "set": { "visible": false } }          // change properties
{ "op": "add-signal", "node": "panel.button_a", "event": "on_click",
  "signal": { "to": "flow", "pin": "open", "from": "out", "attrs": { "screen": "garage" } } } // add a signal
{ "op": "remove-signal", "node": "panel.button_a", "event": "on_click", "to": "flow" }        // remove signals to a target
```

A signal names a target node (`to`) and a pin (`pin`). `from` and `attrs` are optional.

## Routes and UI databases

For `.uir` and `.uidb` files, use `clone-entry` or `set-value`, the same edits [`patch-pkg`](/reference/ops/erp#editing-pkg-resources) runs. Paths use dots for keys and `[field=value]` to select an array entry:

```jsonc
{ "op": "clone-entry", "at": "routes", "src": "[name=fe_home]", "as": "fe_home_copy",
  "set": { "screen": "fe_garage" } }
{ "op": "set-value", "path": "routes[name=fe_home].screen", "value": "fe_garage" }
```

## Text and prompts

Use `set-text` with `"localise": false` for literal text. To change a string in every menu that shows it, use `lang.set` ([In-game text](/reference/data/text)).

Keyboard and controller icons come from entries in `tool_tips.uidb`. See [`!pkg` resources](/reference/pkg) for the file format.
