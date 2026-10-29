---
title: Views
description: The page a launcher plugin returns from view - its header, notices, body and footer, list rows, trailing controls, row menus, buttons, tones and icons.
---

# Views

A view is the page a plugin returns from `view`: a header, notices, one body and a footer.

## A view in full

The template's **Example** page: a header with an import button, a notice, a searchable list with a switch per row, and a footer.

```json
{
  "subtitle": "2 items, 1 on",
  "actions": [ { "id": "pick", "label": "Import…", "icon": "upload",
    "kind": { "pick": { "title": "Import example items", "extensions": ["exmod"], "folders": false, "multiple": true } } } ],
  "notices": [ { "text": "Team Simplified is off in this profile.", "tone": "warn",
    "action": { "id": "enable", "label": "Turn on" } } ],
  "body": { "list": {
    "rows": [
      { "id": "okayama.exmod", "title": "okayama.exmod", "subtitle": "41 bytes",
        "art": { "monogram": "O" },
        "tags": [ { "text": "Track", "tone": "muted" } ],
        "trailing": { "switch": { "id": "okayama.exmod", "on": true, "enabled": true } },
        "detail": [ { "stats": [ { "label": "Size", "value": "41 bytes" } ] } ] },
      { "id": "suzuka.exmod", "title": "suzuka.exmod", "subtitle": "38 bytes",
        "art": { "monogram": "S" },
        "trailing": { "switch": { "id": "suzuka.exmod", "on": false, "enabled": true } } }
    ],
    "reorder": false,
    "search": { "placeholder": "Search items" } } },
  "footer": "By Team Simplified."
}
```

```text
subtitle   required  the page header's line; a list body also shows its row count
actions              buttons in the header
notices              bars under the header: { text, tone, action }; tone colours the bar, action is an optional button
body       required  one of list, empty, blocks
footer               a muted line at the end of the page, after the body
tick       1.0.10    true asks for a tick about once a second while the page is open (Live pages); default false
```

## Bodies

```json
{ "list": { "rows": [ … ], "reorder": false, "search": { "placeholder": "Search mods" } } }
{ "empty": { "title": "No example items", "text": "Drop a .exmod file on the launcher, or import one.", "action": { "id": "pick", "label": "Import…" } } }
{ "blocks": [ … ] }
```

- **`list`**: rows with art, tags, a trailing control and an expandable `detail`. `rows` and `reorder` are required. A `reorder` list adds **Move up** and **Move down** to each expanded row, which fire a [`move` event](./events). An optional `search` *(1.0.10)* puts a search box above the rows ([Search](./tables#search)).
- **`empty`**: a title, a line and one button. `title` and `text` are required.
- **`blocks`**: the [blocks](./blocks), one after another.

The template returns `empty` until the first item is imported:

```rust [src/main.rs]
if items.is_empty() {
    return Ok(json!({
        "subtitle": "No items",
        "body": { "empty": {
            "title": "No example items",
            "text": format!("Drop a .{EXTENSION} file on the launcher, or import one."),
            "action": pick
        } }
    }));
}
```

## Rows {#rows}

```json
{ "id": "okayama.exmod", "title": "Okayama", "subtitle": "41 kB",
  "art": { "monogram": "O" },
  "tags": [ { "text": "Track", "tone": "muted" } ],
  "trailing": { "switch": { "id": "okayama.exmod", "on": true, "enabled": true } },
  "detail": [ { "stats": [ { "label": "Size", "value": "41 kB" } ] } ],
  "menu": [ { "id": "remove:okayama.exmod", "label": "Remove", "style": "danger",
    "kind": { "confirm": { "title": "Remove Okayama?", "text": "It is deleted." } } } ] }
```

- `id`, `title` and `subtitle` are required. `art`, `tags`, `trailing`, `detail` and `menu` default to empty.
- `art` is `"none"`, `{ "monogram": "O" }` or `{ "image": "art/okayama.png" }`.
- `tags` are `{ text, tone }` badges.
- `detail` holds [blocks](./blocks), shown when the row is expanded.
- `menu` *(1.0.0)* opens on right-click. Each [button](#buttons) is a menu item and acts as the button would: a `confirm` button asks in a dialog first.

### Trailing controls

```json
{ "switch": { "id": "okayama.exmod", "on": true, "enabled": true } }
{ "choice": { "id": "livery", "options": ["Title sponsor", "Tier 1", "Replica"], "current": 0 } }
{ "button": { "id": "remove", "label": "Remove", "style": "danger" } }
```

```text
switch   fires { "toggle": { "id", "on" } }
choice   fires { "choose": { "id", "index" } }
button   fires what the button's kind fires
```

## Buttons {#buttons}

```json
{ "id": "import", "label": "Import…", "icon": "upload", "style": "primary", "size": "large",
  "kind": { "pick": { "title": "Import items", "extensions": ["exmod"], "folders": false, "multiple": true } } }
```

- `id` and `label` are required; `icon` is optional.
- `style` is `primary`, `outline`, `ghost` or `danger` (default `outline`).
- `size` *(1.3.0)* is `normal` (default) or `large`, for a page's main call to action.
- `kind` says what a click does:

```text
kind omitted (default)                               fires { "click": { "id" } }
{ "confirm": { "title", "text" } }                   asks in a dialog, then fires { "click": { "id" } }
{ "pick": { "title", "extensions",                   opens the file picker, then fires { "picked": { "id", "paths" } }
            "folders", "multiple" } }
```

## Tones {#tones}

`tone` colours tags, notices, text, pills, tiles, meters and cells: `neutral` (default), `muted`, `accent`, `good`, `warn`, `bad`.

```json
{ "text": { "text": "The game is running: edits apply after it closes.", "tone": "warn" } }
```

## Icons {#icons}

`icon` names one of a fixed set. A view with a name outside it is a result of the wrong shape, and the launcher marks the plugin failed (`sent a bad result: unknown variant …`).

```text
activity  arrow-right-left  chevron-down  chevron-up  circle-alert  circle-check  copy  eraser
folder-open  gamepad  layers  lock  map  package  pencil  play  plus  puzzle  settings
shield-check  trash  upload
```

*Launcher 1.0.10* adds:

```text
calendar  car  clock  cloud-rain  flag  gauge  history  info  refresh  save  search  timer
trending-down  trending-up  triangle-alert  trophy  undo  user  users  wallet  wrench  zap
```
