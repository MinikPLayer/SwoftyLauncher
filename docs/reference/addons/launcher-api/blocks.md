---
title: Blocks
description: The blocks a launcher plugin builds a page from - stats, facts, text and buttons, and from 1.0.10 the dashboard blocks hero, pills, tiles, meters, card and grid, plus image, drop zone and tabs.
---

# Blocks

A block is one piece of a page; a `blocks` body, a row's `detail`, a `card` and a `grid` each hold a list of them.

## A dashboard in full

*Launcher 1.0.10.* A team page with a hero, tiles, and two cards side by side:

```json
{ "subtitle": "My Team", "body": { "blocks": [
  { "hero": {
      "title": "Scuderia Swofty",
      "subtitle": "My Team · 2026 season · Round 7 of 24",
      "art": { "monogram": "S" },
      "pills": [ { "text": "LIVE", "tone": "good", "dot": true }, { "text": "In Team HQ", "tone": "accent" } ],
      "facts": [ { "label": "Next race", "value": "Monaco" }, { "label": "Last save", "value": "2 min ago" } ],
      "buttons": [ { "id": "backup", "label": "Back up now", "icon": "save" } ] } },
  { "tiles": [
      { "label": "Cash", "value": "$12.4M", "delta": "+$2.0M", "hint": "since Bahrain" },
      { "label": "Resource points", "value": "340", "hint": "of 500", "progress": 0.68, "tone": "accent" },
      { "label": "Constructors", "value": "P4", "delta": "-1", "delta_tone": "bad", "icon": "trophy" } ] },
  { "grid": { "columns": [2, 1], "blocks": [
      { "card": { "title": "Facilities", "blocks": [
          { "meters": [ { "label": "Wind tunnel", "value": 3, "max": 4, "text": "Phase 3 of 4", "tone": "good" } ] } ] } },
      { "card": { "title": "Session", "pills": [ { "text": "Offline", "tone": "muted" } ], "blocks": [
          { "facts": [ { "label": "Mode", "value": "My Team" } ] } ] } } ] } }
] } }
```

Every field not marked required can be left out. The template's **Overview** page builds the same blocks from `dashboard.json`:

```rust [src/main.rs]
blocks.push(json!({ "tiles": [
    { "label": "Budget", "value": format!("${}k", d["budget"]), "delta": "+$12k", "hint": "since the last race", "icon": "wallet" },
    { "label": "Speed", "value": format!("{speed}"), "hint": "of 100", "progress": speed / 100.0, "tone": "accent", "icon": "gauge" },
    { "label": "Items on", "value": format!("{} / {}", on.len(), items.len()), "icon": "package" }
] }));
```

## Basic blocks

```json
{ "stats": [ { "label": "Size", "value": "41 kB" } ] }
{ "facts": [ { "label": "Car number", "value": "#7" } ] }
{ "label": "WHAT IT CHANGES" }
{ "text": { "text": "The game is running.", "tone": "warn" } }
{ "names": { "label": "Mods", "items": ["A", "B"] } }
{ "table": { "columns": ["File", "Op"], "rows": [["a.erp", "patch"]] } }
{ "buttons": [ { "id": "backup", "label": "Back up now" } ] }
```

- **`stats`**: large label/value pairs. **`facts`**: small label/value pairs.
- **`label`**: a section heading.
- **`text`**: a line in a [tone](./views#tones).
- **`names`**: a heading and a name list.
- **`table`**: a table; sorting, search and editable cells from 1.0.10 ([Tables](./tables)).
- **`buttons`**: a row of [buttons](./views#buttons).
- **`form`** *(1.0.10)*: input fields with an Apply button ([Forms](./forms)).

## Dashboard blocks {#dashboard-blocks}

*Launcher 1.0.10.* They go wherever a block goes: in a `blocks` body, a row's `detail`, a `card` or a `grid`.

### hero

A header for the page's subject, on its own surface.

```json
{ "hero": { "title": "Scuderia Swofty", "subtitle": "My Team · 2026 season", "art": { "image": "art/team.png" },
  "pills": [ … ], "facts": [ { "label": "Next race", "value": "Monaco" } ], "buttons": [ … ] } }
```

- `title` *(required)*: the large name. `subtitle`: a muted line under it.
- `art`: `"none"` (default), `{ "monogram": "S" }` or `{ "image": "art/team.png" }`, at the left.
- `pills`: [pills](#pills) beside the title.
- `facts`: `{ label, value }` pairs in a row along the bottom.
- `buttons`: [buttons](./views#buttons) at the right.

### pills {#pills}

A row of status badges.

```json
{ "pills": [ { "text": "LIVE", "tone": "good", "dot": true }, { "text": "Budget locked", "tone": "accent", "icon": "lock" } ] }
```

- `text` is required. `tone` colours the pill (default `neutral`).
- `dot: true` puts a status dot before the text; `icon` puts an [icon](./views#icons) there instead.

### tiles

Stat tiles in equal columns: up to 4 in one row, 5 or 6 in rows of 3, more in rows of 4. A narrow page uses fewer columns.

```json
{ "tiles": [ { "label": "Constructors", "value": "P4", "delta": "-1", "delta_tone": "bad", "hint": "after Imola", "icon": "trophy", "progress": 0.4, "tone": "accent" } ] }
```

- `label` and `value` *(required)*: a small heading and the large figure, as text.
- `tone`: the figure's colour (default `neutral`).
- `delta`: a change beside the figure, such as `+$2.0M`. `delta_tone`: its colour. Default: `good` when it starts with `+`, `bad` when it starts with `-` or `−`, else `muted`.
- `hint`: a muted line under the figure.
- `icon`: an [icon](./views#icons) in the corner.
- `progress`: `0.0` to `1.0`, a bar along the foot of the tile, in `tone` (`accent` when neutral).

### meters

Labelled progress bars, one under another.

```json
{ "meters": [ { "label": "Wind tunnel", "value": 3, "min": 0, "max": 4, "text": "Phase 3 of 4", "tone": "good" } ] }
```

- `value` is required. `min` defaults to `0` and `max` to `100`; the bar is clamped to the range.
- `text` is shown at the right (default `<value> / <max>`).
- `tone` colours the bar (default `accent`).

### card

A bordered surface that holds blocks under an optional header.

```json
{ "card": { "title": "Drivers", "subtitle": "Click a rating to edit it", "pills": [ … ], "buttons": [ … ], "blocks": [ … ], "dashed": false } }
```

- `blocks` defaults to empty. A table in a card loses its own border.
- `dashed` *(1.3.0)*: `true` draws a dashed outline, for a placeholder or a drop target.

### grid

Blocks side by side.

```json
{ "grid": { "columns": [3, 2], "blocks": [ { "card": { … } }, { "card": { … } } ] } }
```

- `columns` is a count from 1 to 4 for equal columns (default 2), or a list of relative widths such as `[2, 1]`.
- The blocks fill the columns left to right, then the next row. Blocks in a row share its height.
- On a page too narrow for about 280 px per column, the grid becomes one column.

## image {#image}

*Launcher 1.0.12.* A picture from your plugin's `cache_dir` or `data_dir`:

```json
{ "image": { "path": "/home/you/.local/share/swofty/cache/addons/example.launcher-addon/helmets/helmet_042-91c0.png", "width": 240, "height": 135, "caption": "Carbon stripe", "fit": "contain" } }
{ "image": { "path": "/home/you/.local/share/swofty/cache/addons/example.launcher-addon/logos/ferrari.png", "width": 96, "height": 96, "fit": "contain", "transparent": true } }
```

- `path` (absolute), `width` and `height` (in pixels) are required. `width` shrinks to the space the block is in.
- `fit` is `cover` (default), which fills the box and crops, or `contain`, which shows the whole picture inside the box.
- `caption` is a muted line under it.
- `"transparent": true` *(1.5.25)* draws the picture without the box and border behind it, so its transparent parts show the card or panel under it. An older launcher ignores `transparent` and draws the box.
- A path outside those two folders shows an empty box.
- An older launcher fails the view.

## drop zone {#drop-zone}

*Launcher 1.3.0.* A large drop target with one main call to action, as the Simplified Importer's **Import a mod**: an icon, a title, a line of text and a big button, centred in a tall area.

```json
{ "drop_zone": { "icon": "upload", "title": "Import a mod", "text": "Drop a mod on the window, or choose one.",
  "button": { "id": "pick", "label": "Choose a mod", "style": "primary", "size": "large",
    "kind": { "pick": { "title": "Choose a mod", "extensions": ["zip"], "folders": false, "multiple": false } } } } }
```

- `title` and `button` are required; `text` and `icon` are optional.
- Files dropped on the window still go to the addon that [claims](./importers) them.

## Tabs {#tabs}

*Launcher 1.0.10.* A `tabs` block draws a row of tabs, so one sidebar page can hold several views.

```json
{ "tabs": {
  "id": "page",
  "items": [
    { "id": "overview", "label": "Overview", "icon": "gauge" },
    { "id": "drivers", "label": "Drivers", "icon": "users" },
    { "id": "history", "label": "History", "icon": "history", "badge": "2" },
    { "id": "weather", "label": "Weather", "icon": "cloud-rain", "disabled": true }
  ],
  "active": "overview"
} }
```

- `id` *(required)* names the tab bar in its events.
- `items` *(required)* are the tabs, left to right: `{ id, label, icon, badge, disabled }`. `id` and `label` are required. `badge` is a short count or word shown after the label. A `disabled` tab is greyed out and can't be clicked.
- `active` is the highlighted tab's `id` (default: the first).

Clicking a tab that isn't active fires `{ "tab": { "id": "page", "tab": "drivers" } }`. The launcher doesn't switch anything itself: keep the active tab in your plugin, return `"reload"`, and build the next view with that tab's blocks under the bar.

```rust [src/main.rs]
fn overview_event(&mut self, event: &Value) -> Result<Value, String> {
    if let Some(tab) = event["tab"]["tab"].as_str() {
        self.tab = tab.to_owned();         // the next view draws this tab
        return Ok(json!("reload"));
    }
    // …
}
```

Forms and searches keep their state per form `id`: give forms on different tabs different ids.
