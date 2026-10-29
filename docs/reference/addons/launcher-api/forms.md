---
title: Forms
description: The form block of a launcher plugin page - number, slider, text, select, toggle and colour fields, the Apply button, live forms and the submit event.
---

# Forms

*Launcher 1.0.10.* A `form` block holds input fields and an **Apply** button that sends every field in one `submit` event.

## A form in full

The template's **Settings** card, and the handler that saves it:

```rust [src/main.rs]
let mut fields = vec![
    json!({ "number": { "id": "budget", "label": "Budget", "value": d["budget"], "min": 0, "max": 1000, "step": 10, "unit": "k" } }),
    json!({ "text": { "id": "name", "label": "Team name", "value": d["name"], "max_len": 32 } }),
    json!({ "slider": { "id": "speed", "label": "Speed", "value": d["speed"], "min": 0, "max": 100, "step": 5 } }),
    json!({ "select": { "id": "mode", "label": "Weather", "value": d["mode"], "options": [
        { "value": "dry", "label": "Dry" }, { "value": "light", "label": "Light rain" }, { "value": "heavy", "label": "Heavy rain" } ] } }),
    json!({ "toggle": { "id": "lock", "label": "Lock budget", "value": d["lock"], "help": "Stops the budget from changing." } }),
];
// a colour field needs launcher 1.5.25: self.colours is set from hello's launcher version
if self.colours {
    fields.push(json!({ "colour": { "id": "colour", "label": "Team colour", "value": d["colour"] } }));
}
json!({ "form": { "id": "settings", "columns": 2, "fields": fields } })
```

```rust [src/main.rs]
if let Some(submit) = event.get("submit") {
    let values = &submit["values"];
    if submit["form"] == "settings" {
        for key in ["budget", "speed", "name", "mode", "lock", "colour"] {
            if let Some(v) = values.get(key) {
                d[key] = v.clone();
            }
        }
        self.save_dashboard(&d)?;
        // the form drops its edits and shows the next view's values
        return Ok(json!({ "notice": format!("Saved {}", submit["changed"]) }));
    }
}
```

**Apply** with only the budget changed:

```json
{ "submit": { "form": "settings", "values": { "budget": 150, "name": "Example Racing", "speed": 40, "mode": "dry", "lock": false, "colour": "#8B7FD6" }, "changed": ["budget"] } }
```

## The form

```json
{ "form": { "id": "finance", "fields": [ … ], "columns": 2, "apply": "Apply", "live": false, "disabled": false } }
```

- `id` *(required)* names the form in its `submit` events.
- `fields` *(required)* are the fields, in order.
- `columns` lays the fields out in 1 to 3 columns (default 1).
- `apply` is the Apply button's label (default `Apply`).
- `live: true` drops the Apply button: each field submits on its own as soon as it's committed. Default `false`.
- `disabled: true` greys out every field and the button, such as while the game can't take edits. Default `false`.

## Fields

Every field takes `id` *(required: its key in `values`)*, `label`, `help` (a muted line under it) and `disabled`, plus its own keys:

```json
{ "number": { "id": "cash", "label": "Cash", "value": 12400000, "min": 0, "max": 2147483647, "step": 100000, "unit": "$", "help": "Posted as a bank transaction." } }
{ "slider": { "id": "rp", "label": "Resource points", "value": 340, "min": 0, "max": 500, "step": 10, "unit": "RP" } }
{ "text": { "id": "name", "label": "Team name", "value": "Swofty Racing", "placeholder": "Team name", "max_len": 32 } }
{ "select": { "id": "weather", "label": "Weather", "value": "dry", "options": [ { "value": "dry", "label": "Dry" }, { "value": "wet", "label": "Heavy rain" } ] } }
{ "toggle": { "id": "lock", "label": "Lock budget", "value": false } }
{ "colour": { "id": "livery", "label": "Livery colour", "value": "#E10600" } }
```

```text
number   value, min, max, step (default 1), unit (shown after the number)    sends a number
slider   value, min (default 0), max (default 100), step (default 1), unit   sends a number
text     value, placeholder, max_len                                         sends a string
select   value, options, search (1.0.12)                                     sends the chosen option's value
toggle   value                                                               sends true or false
colour   1.5.25: value, hex (default true)                                   sends "#RRGGBB", or null without a value
```

### number and slider

- A number is clamped to `min`–`max` and rounded to `step` when it's committed.
- It is sent as an integer when the step and the value are whole.
- Up and Down step a focused number field.

### select

`options` are strings, `{ value, label }` objects, or `{ heading }` group titles *(1.0.12)*. A string option is its own value.

```json
{ "select": { "id": "helmet", "label": "Helmet", "value": "helmet_042", "search": true,
  "options": [
    { "heading": "Team" },
    { "value": "helmet_001", "label": "Classic red", "image": "/home/you/.local/share/swofty/cache/addons/example.launcher-addon/helmets/helmet_001-3fa2.png" },
    { "heading": "Custom" },
    { "value": "helmet_042", "label": "Carbon stripe", "image": "/home/you/.local/share/swofty/cache/addons/example.launcher-addon/helmets/helmet_042-91c0.png" } ] } }
```

- The list opens below the field, at most 320 pixels tall, and scrolls when the options don't fit. Up and Down move through the options, Enter picks one and Escape closes the list.
- **Headings** *(1.0.12)*: a `{ "heading": "Modded tracks" }` option starts a group. Its title shows above the options that follow it, with a line above it unless it's the first group, and it can't be picked. A search that matches a group's title keeps the whole group. An older launcher fails the view, so send headings only to 1.0.12 or later.
- **Images** *(1.0.12)*: a `{ value, label }` option can carry `"image"`, the absolute path of a PNG or JPEG under your plugin's `cache_dir` or `data_dir`. The menu shows it as a thumbnail before the label, and the closed field shows the picked option's thumbnail. When some options have an image, the others get an empty square. A path outside those two folders shows no image. The launcher caches an image by its path, so write changed content under a new file name. An older launcher ignores `image`.
- **Search** *(1.0.12)*: `"search": true` puts a search box at the top of the list. Typing filters the options to those whose label contains the text, ignoring case. The field still shows the current value while you search. An older launcher ignores `search` and shows the full list.

### colour {#colour-fields}

*Launcher 1.5.25.* A `colour` field shows a round dot of the colour, followed by its hex unless `"hex": false`. A field without a value shows an empty dot and `None`.

```json
{ "colour": { "id": "livery", "label": "Livery colour", "value": "#E10600" } }
{ "colour": { "id": "accent", "label": "Accent", "value": "#00A19C", "hex": false } }
```

```json
{ "submit": { "form": "team", "values": { "livery": "#E10600", "accent": "#00A19C" }, "changed": ["livery"] } }
```

- `value` takes `#RRGGBB` or `#RGB`, with or without `#`, in upper or lower case.
- The field sends the colour as `#RRGGBB` in upper case, and `null` while it has no value.
- Clicking the field opens a picker under it: a square for saturation and brightness with a handle to drag, a hue strip, a hex box and R, G and B boxes. They stay in sync, and the field shows the colour as it changes.
- Enter, a click outside the picker or a second click on the field commits the colour. Escape puts the previous colour back.
- One picker is open at a time, and it stays inside the window.
- A committed colour waits for **Apply** like any other field, and a `live` form sends it at once.
- A table cell takes a colour too ([Colour cells](./tables#colour-cells)).
- An older launcher fails a view that holds a `colour` field or cell. Set `"compat": { "launcher": ">=1.5.25" }` in `swofty.json`, or check `launcher` in `hello` and fall back ([Versions](../launcher-api#versions)).

## How a form behaves

- Edits stay in the form until you press **Apply**. Apply turns on once a field differs from the view; **Reset** beside it puts the view's values back.
- Apply fires one `submit` event. `values` holds every field; `changed` lists the fields that differ from the view, in field order.
- In a `live` form, a field is committed by Enter or leaving it (number, text), releasing it (slider), picking an option (select), flipping it (toggle) or committing the picker (colour). Its event holds only that field, in both `values` and `changed`:

```json
{ "submit": { "form": "driver:rai", "values": { "on": false }, "changed": ["on"] } }
```

- While the event runs, the form shows busy.
- When the effect comes back, the form drops its edits and shows the values of the next view: return `"reload"` or a notice to show what the edit did.
- If the event fails, the edits stay and the error shows as a notification.
- A new view, such as one after an `invalidate`, updates every field you haven't edited and leaves edited fields alone.
- Forms keep their state per form `id`: give forms on different [tabs](./blocks#tabs) different ids.
