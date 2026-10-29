---
title: Tables
description: The table block of a launcher plugin page - columns, sorting, editable cells, bars, pills, pictures, switch and colour cells, row details and search.
---

# Tables

A `table` block draws columns and rows; from launcher 1.0.10 they can sort, search and edit.

## A table in full

The template's **Drivers** tab: a switch column, sortable columns, a bold surname, an editable rating with a bar, a pill, a panel per row, and a colour column on launcher 1.5.25.

```rust [src/main.rs]
let rows: Vec<Value> = drivers.iter().map(|r| {
    let rating = r["rating"].as_f64().unwrap_or(0.0);
    let name = r["name"].as_str().unwrap_or_default();
    let (first, last) = name.rsplit_once(' ').unwrap_or(("", name));
    let active = r["active"] != false;
    let mut row = json!({ "id": r["id"], "subtitle": r["team"], "cells": [
        { "text": if active { "On" } else { "Off" }, "edit": { "toggle": { "value": active } } },
        // the surname in bold; `text` stays the whole name for search and sort
        { "text": name, "spans": [ { "text": format!("{first} ") }, { "text": last, "bold": true } ] },
        { "text": r["team"], "tone": "muted" },
        { "text": format!("{rating}"), "bar": rating / 100.0,
          "edit": { "number": { "min": 0, "max": 100, "step": 1 } } },
        { "text": "Strong", "tone": r["form"], "pill": true }
      ],
      // clicking the row opens this beside the table
      "title": name,
      "detail": [
          { "stats": [ { "label": "Rating", "value": format!("{rating}") }, { "label": "Team", "value": r["team"] } ] },
          { "facts": [ { "label": "Driver id", "value": r["id"] } ] }
      ] });
    // a colour cell needs launcher 1.5.25: self.colours is set from hello's launcher version
    if self.colours {
        let colour = r["colour"].as_str().unwrap_or("#737988");
        row["cells"].as_array_mut().into_iter().for_each(|c| c.push(json!({ "text": colour, "edit": { "colour": {} } })));
    }
    row
}).collect();

let mut columns = vec![
    json!({ "key": "active", "title": "Active", "width": 72 }),
    json!({ "key": "name", "title": "Driver", "sort": true }),
    json!({ "key": "team", "title": "Team", "sort": true }),
    json!({ "key": "rating", "title": "Rating", "sort": true, "width": 150 }),
    json!({ "key": "form", "title": "Form", "width": 96 }),
];
if self.colours {
    columns.push(json!({ "key": "colour", "title": "Colour", "width": 110 }));
}

json!({ "table": {
    "id": "drivers",
    "columns": columns,
    "rows": rows,
    "sort": { "column": "rating", "desc": true },
    "search": { "placeholder": "Search drivers" },
    "compact": true
} })
```

An edited rating, a flipped switch and a picked colour:

```json
{ "submit": { "form": "drivers", "row": "ada", "values": { "rating": 92 }, "changed": ["rating"] } }
{ "submit": { "form": "drivers", "row": "ada", "values": { "active": false }, "changed": ["active"] } }
{ "submit": { "form": "drivers", "row": "ada", "values": { "colour": "#00A19C" }, "changed": ["colour"] } }
```

```rust [src/main.rs]
(Some("drivers"), Some(row)) => {
    // a rating cell sends a number, the Active switch true or false, a colour cell "#RRGGBB"
    let (key, value) = match (values["active"].as_bool(), values["colour"].as_str()) {
        (Some(on), _) => ("active", json!(on)),
        (None, Some(colour)) => ("colour", json!(colour)),
        (None, None) => ("rating", json!(values["rating"].as_u64().ok_or("the rating is not a whole number")?)),
    };
    // …store it on the row with that id
    return Ok(json!("reload"));
}
```

## The table

```json
{ "table": { "id": "drivers", "columns": [ … ], "rows": [ … ], "sort": { "column": "pace", "desc": true },
  "search": { "placeholder": "Search drivers" }, "compact": true, "empty": "No drivers match", "row_height": 72 } }
```

- `columns` and `rows` are required.
- The old shape, plain column names and rows of strings, still works and looks as before: `{ "columns": ["File", "Op"], "rows": [["a.erp", "patch"]] }`. *Launcher 1.0.10*: any key below turns on the new table, which fills the width it's given.
- `id` names the table in `submit` and `search` events (default `table`).
- `sort` is the starting sort, `{ column, desc }`, with `column` a column's key.
- `search` puts a search box above the table ([Search](#search)).
- `compact: true` gives shorter rows and smaller text.
- `empty` is the line shown when no row is left after a search (default `No matches`).
- `row_height` *(1.5.8)* makes rows taller, in pixels, such as `72` for 64-pixel pictures. Never shorter than the default.
- *Since 1.5.9*, a table with more than 30 rows on a page of blocks draws only the rows in and near view.

## Columns

```json
"columns": [ "Driver", { "key": "pace", "title": "Pace", "align": "right", "width": 96, "sort": true } ]
```

- A column is a name, or `{ key, title, align, width, sort }`.
- `key` names the column in events and in `sort` (default: the title).
- `align` is `left` (default), `right` or `center`.
- `width` fixes its width in pixels; other columns share the rest.
- `sort: true` makes the header clickable. A click sorts by that column; a second click reverses it.
- Sorting is numeric when both cells are numbers (commas allowed) and by text otherwise. Give cells a `sort` key for formatted values such as `$12.4M`.

## Rows

```json
"rows": [
  { "id": "ver", "tone": "accent", "cells": [ "Max Verstappen", { "text": "Red Bull", "tone": "muted" }, "95" ] },
  ["Lando Norris", "McLaren", "93"]
]
```

- A row is a list of cells, or `{ id, cells, tone, title, subtitle, detail }`.
- `id` names the row in events (default: its position in `rows`, from `"0"`).
- `tone` colours its text.
- *Launcher 1.5.8*: `detail`, `title` and `subtitle` open a panel beside the table ([Row details](#row-details)).

## Cells

```json
"Lando Norris"
{ "text": "$12.4M", "sort": 12400000 }
{ "text": "Strong", "tone": "good", "pill": true }
{ "text": "95", "bar": 0.95, "edit": { "number": { "min": 0, "max": 100 } } }
{ "text": "Swofty Racing", "edit": { "text": { "placeholder": "Team name", "max_len": 32 } } }
{ "text": "On", "edit": { "toggle": { "value": true } } }
{ "text": "#E8002D", "edit": { "colour": {} } }
{ "text": "Finnish", "image": { "path": "/home/you/.local/share/swofty/cache/addons/example.launcher-addon/flags/fi.png", "width": 20, "height": 14 } }
{ "text": "Ferrari", "image": { "path": "/home/you/.local/share/swofty/cache/addons/example.launcher-addon/logos/ferrari.png", "width": 28, "height": 28, "fit": "contain", "transparent": true } }
{ "text": "Kimi RÄIKKÖNEN", "spans": [ { "text": "Kimi " }, { "text": "RÄIKKÖNEN", "bold": true } ] }
```

- **`text`**: the text shown. **`tone`**: its colour.
- **`sort`**: a number or string to sort by instead of `text`.
- **`pill`**: `true` draws the text as a [pill](./blocks#pills) in `tone`.
- **`bar`**: `0.0` to `1.0`, a small bar before the text, in `tone` (`accent` when neutral).
- **`edit`**: makes the cell editable, `{ "number": { value, min, max, step, unit } }` or `{ "text": { value, placeholder, max_len } }`. `value` defaults to the cell's text.
- **`edit.toggle`** *(1.5.8)*: `{ "toggle": { value, disabled } }` draws a switch, with the cell's text after it when there is any.
- **`edit.colour`** *(1.5.25)*: `{ "colour": { value, hex, disabled } }` draws the colour's dot before the text, or the dot alone with `"hex": false`, and opens a [colour picker](./forms#colour-fields) when clicked ([Colour cells](#colour-cells)).
- **`image`** *(1.5.8)*: a picture before the text. The absolute path of a PNG or JPEG under your plugin's `cache_dir` or `data_dir`, drawn as a square as tall as the row allows, or `{ path, width, height }` for a box of that size, such as `20` by `14` for a flag. The picture fills the box and is cropped to it.
- **`image.fit`** and **`image.transparent`** *(1.5.25)*: the object also takes `fit`, `cover` (default) or `contain`, which shows the whole picture, and `"transparent": true`, which drops the box behind the picture so its transparent parts show the row.
- **`spans`** *(1.5.8)*: the text in pieces, `[{ text, bold, tone }]`, drawn in place of `text`. Keep `text` as the whole text: it is what search and sort use, and what an older launcher shows.

An older launcher ignores `row_height`, `image`, `spans` and `detail`, and draws a switch cell as its text. Set `"compat": { "launcher": ">=1.5.8" }` in `swofty.json`, or check `launcher` in `hello` and fall back.

A launcher older than 1.5.25 fails a view with a colour cell, and ignores a cell picture's `fit` and `transparent`. Set `"compat": { "launcher": ">=1.5.25" }`, or check `launcher` in `hello` and fall back ([Versions](../launcher-api#versions)).

## Editing cells

- Click an editable cell to edit it. Enter or leaving the cell commits it, and Escape cancels.
- A committed value that differs fires `submit` with the table's `id` as `form`, and the row's `id` as `row`.
- The cell shows busy until the effect comes back, as a form does.
- Flipping a switch cell fires the same event with `true` or `false`. The switch shows its new position until the effect comes back.

### Colour cells {#colour-cells}

*Launcher 1.5.25.* A colour cell opens the [colour picker](./forms#colour-fields) under it. A committed colour that differs fires the same event with `"#RRGGBB"`:

```json
{ "text": "#E8002D", "edit": { "colour": {} } }
```

```json
{ "submit": { "form": "teams", "row": "ferrari", "values": { "colour": "#00A19C" }, "changed": ["colour"] } }
```

## Row details {#row-details}

*Launcher 1.5.8.* A row with `detail` opens a panel beside the table when it is clicked, as a row of the DB Editor does.

```json
{ "id": "rai", "title": "Kimi RÄIKKÖNEN", "subtitle": "kimi_raikkonen",
  "cells": [ "On", "Kimi RÄIKKÖNEN", "Finnish" ],
  "detail": [
    { "image": { "path": "…/cache/kimi.png", "width": 182, "height": 182 } },
    { "form": { "id": "driver:rai", "live": true, "fields": [ { "toggle": { "id": "on", "label": "Driver model", "value": true } } ] } },
    { "facts": [ { "label": "Car number", "value": "#7" } ] } ] }
```

- The row is highlighted, and the panel shows the row's `title` (default: the text of its first cell that isn't a switch), its `subtitle` and its blocks, under a close button.
- Clicking the row again or the close button closes it. Clicking another row shows that row instead.
- The panel is 380 pixels wide. When the table beside it would leave a column without a fixed `width` less than 96 pixels, the panel opens under its row instead, across the table.
- The panel takes any [blocks](./blocks). Their events reach your plugin as they do anywhere else on the page.
- It stays open while the view refreshes, so a form in it keeps working. It is hidden while its row isn't shown, such as during a search.
- Clicking an editable cell, a switch or a colour in a row edits it and doesn't open the panel.
- *Since 1.5.9*, a form in a row's panel is set up when the panel opens and dropped when it closes. Values typed in it but not sent are lost on closing.

## Search {#search}

A `list` body or a `table` takes `search`, which puts a search box above its rows:

```json
{ "search": { "id": "drivers", "placeholder": "Search drivers", "event": false, "filter": true } }
```

- `id` names the box in its events. Default: the table's `id`, or `list`.
- `placeholder` is the hint in the empty box (default `Search`).
- `filter: true` (default): the launcher hides rows that don't match. A row matches when every word of the query appears in it, ignoring case: in a list row's title, subtitle and tags, or in a table row's cells.
- `event: true` sends `{ "search": { "id", "query" } }` once typing pauses for 300 ms, and when the box is cleared. Default `false`. Set `filter` to `false` when you filter the rows yourself.
- The query stays in the box when the view refreshes, until the page is left.

```json
{ "method": "event", "params": { "section": "overview", "event": { "search": { "id": "drivers", "query": "ver" } } } }
```
