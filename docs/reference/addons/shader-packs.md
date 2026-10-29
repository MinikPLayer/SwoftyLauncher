---
title: Shader packs
description: Grade the race with your own colour look - a .cube LUT in an addon, with settings to pick the look and its strength.
---

# Shader packs

A shader pack is an addon that grades the game's 3D picture with a colour look, a `.cube` 3D LUT, which the in-game core applies after the game's tonemap while the HUD and menus draw on top in their own colours.

`.cube` is the format DaVinci Resolve, Photoshop and most grading tools export. Shader packs need launcher 1.4.0 or later; an earlier launcher rejects the manifest. Looks are SDR: with HDR on, the game keeps its own colours.

## A shader pack in full

This is [`templates/shader-pack`](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/templates/shader-pack): three looks and settings to pick one and its strength.

```json [swofty.json]
{
  "schema_version": 1,
  "id": "example.monochrome-looks",
  "name": "Monochrome Looks",
  "version": "0.1.0",
  "author": "Your Name",
  "description": "Grades the race in monochrome, overcast or noir.",
  "license": "MIT",
  "category": "shaders",
  "experimental_features": ["shaders"],
  "compat": { "game": "f1_25" },
  "options": [
    { "id": "look", "type": "choice", "label": "Look", "description": "The grade the picture goes through.",
      "default": "monochrome",
      "values": [
        { "value": "monochrome", "label": "Monochrome" },
        { "value": "overcast", "label": "Overcast" },
        { "value": "noir", "label": "Noir" }
      ],
      "binds": "db" },
    { "id": "strength", "type": "slider", "label": "Strength",
      "description": "How much of the look applies, from the game's own colours at 0 to the full look at 100.",
      "min": 0, "max": 100, "step": 10, "default": 100, "binds": "db" }
  ],
  "shader": { "lut": "looks/{{look}}.cube", "strength": "{{strength}}" }
}
```

```text [looks/noir.cube]
TITLE "Noir"
LUT_3D_SIZE 17
0.000000 0.000000 0.000000
0.000000 0.000000 0.000000
...
```

The template's `looks/make_looks.py` writes its three `.cube` files. The build prints the look:

```text
shaders: Monochrome Looks grades the picture with looks/noir.cube (17x17x17) at 100% strength
```

In game, `swofty/loader.log` shows:

```text
[shaders] grading the picture with Monochrome Looks's look (17x17x17) at 100%; HDR setting read from C:\Users\me\Documents\My Games\F1 25\hardwaresettings\hardware_settings_config.xml
[shaders] SDR: Monochrome Looks's look applies
[shaders] graded the game's tonemap LUT (brightest 0.981) with Monochrome Looks's look
[shaders] the picture now goes through Monochrome Looks's look
```

## Make a look

1. Turn every shader pack off and take a screenshot of the game in SDR.
2. Grade the screenshot in DaVinci Resolve, Photoshop, Affinity Photo or any tool that exports a 3D LUT.
3. Export the grade as a `.cube` file. 17, 33 and 65 points per side all work.
4. Put the file in your addon, such as `looks/my-look.cube`.

The core applies the look to the same sRGB colours a screenshot holds, so the race looks the way your graded screenshot does.

## The manifest

```jsonc
"category": "shaders",
"experimental_features": ["shaders"],   // the shaders feature is experimental, and on while an addon lists it here
"shader": {
  "lut": "looks/my-look.cube",          // the .cube file, a path inside the addon; {{id}} is replaced with a db option's value
  "strength": 100                       // 0 to 100, or "{{id}}" naming a db slider that stays within 0 to 100; default 100
}
```

Installing checks the block and refuses the manifest when `lut` is not a `.cube` file inside the addon, an option is unknown or binds `native`, or the strength is outside 0 to 100:

```text
invalid manifest: me.my-looks: shader: list "shaders" in experimental_features to use it
invalid manifest: me.my-looks: shader: lut looks/my-look.png must be a .cube file
invalid manifest: me.my-looks: shader: option look must bind db
invalid manifest: me.my-looks: shader: option strength must stay within 0 to 100
invalid manifest: me.my-looks: shader: strength 120 is not from 0 to 100
```

## Settings

To let players pick a look and its strength, declare `db` [options](./manifest#options) and name them in `shader`:

```json [swofty.json]
"options": [
  { "id": "look", "type": "choice", "label": "Look", "default": "monochrome",
    "values": [{ "value": "monochrome", "label": "Monochrome" }, { "value": "noir", "label": "Noir" }],
    "binds": "db" },
  { "id": "strength", "type": "slider", "label": "Strength", "min": 0, "max": 100, "step": 10,
    "default": 100, "binds": "db" }
],
"shader": { "lut": "looks/{{look}}.cube", "strength": "{{strength}}" }
```

- The settings appear on the launcher's **Addons** page and in the game's **ADDON SETTINGS**.
- The build reads them, so a change applies from the next launch.
- Strength 0 shows the game's own colours, 100 shows the look in full, and values between mix the two.

## The .cube file

```text
# a comment; other keyword lines are skipped
TITLE "My look"              optional
LUT_3D_SIZE 33               required: n from 2 to 129, before the first colour
DOMAIN_MIN 0 0 0             optional, default 0 0 0, before the first colour
DOMAIN_MAX 1 1 1             optional, default 1 1 1, before the first colour
LUT_3D_INPUT_RANGE 0 1       sets both DOMAIN_MIN and DOMAIN_MAX
0.000000 0.000000 0.000000   n³ colour lines: red changes fastest, then green, then blue
```

A 1D LUT (`LUT_1D_SIZE`) is refused. The build checks the file and stops at the first problem, with the line it is on:

```text
My Looks: looks/noir.cube: line 9: "0 0 x" is not 3 number(s)
My Looks: looks/noir.cube: line 1: a 1D LUT; shader packs take a 3D LUT (LUT_3D_SIZE)
My Looks: looks/noir.cube: line 2: LUT_3D_SIZE 1 is not a whole number from 2 to 129
My Looks: looks/noir.cube: 1 colour line(s), but LUT_3D_SIZE 2 needs 8
```

## What players see

- The **Addons** page lists the pack under **Shaders**, tags it **Uses experimental features** and shows its `lut` under **Look** in its panel.
- One look applies at a time. With two shader packs on, the one later in the load order applies, and the build warns about the other:

```text
My Looks: its look is not applied; one shader look applies at a time, and Other Looks loads after it
```

- With HDR on in the game's display settings, the picture keeps the game's own colours. The look applies again when HDR is off. The core reads the HDR switch, `<hdr mode="...">`, from the game's `hardware_settings_config.xml`; a missing file counts as HDR off:

```text
[shaders] HDR is on in the game's settings; My Looks's look waits for SDR
[shaders] SDR: My Looks's look applies
```

## How it works

The game maps the colour of the 3D picture to display colour through a 64×64×64 LUT, `eaid://ego_package/aces_rrt_odt_lut.image`, and rewrites it every frame.

- The build checks your `.cube` file and installs it as `swofty/shaders/look.lut`, and `swofty/assets.json` names it under `shader`.
- In game, the core reads the game's LUT back, runs each entry through your look and copies the result over the game's LUT every frame.
- Every two seconds it reads the game's LUT again, and grades it again when the game changed it, as after a brightness change:

```text
[shaders] regraded the game's tonemap LUT (brightest 0.974) with My Looks's look
```
