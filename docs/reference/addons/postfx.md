---
title: Post-process passes
description: Run your own HLSL shader on the race picture after the game's tonemap and before the HUD.
---

# Post-process passes

A post-process pass is an HLSL compute shader in an addon, which the launcher compiles to DXIL with dxc when it builds the profile and the in-game core runs on every frame of the 3D picture after the game's tonemap and colour grade, before the HUD draws.

Vignettes, saturation, sharpening, film grain and chromatic aberration are passes. Passes need launcher 1.5.4 or later; an earlier launcher rejects the manifest.

## A pass in full

This is [`templates/postfx-pass`](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/templates/postfx-pass): a vignette with saturation, and a setting for each.

```json [swofty.json]
{
  "schema_version": 1,
  "id": "example.vignette-pass",
  "name": "Vignette Pass",
  "version": "0.1.0",
  "author": "Your Name",
  "description": "Darkens the corners of the race and sets its saturation.",
  "license": "MIT",
  "category": "shaders",
  "experimental_features": ["postfx"],
  "compat": { "game": "f1_25" },
  "options": [
    { "id": "vignette", "type": "slider", "label": "Vignette", "min": 0, "max": 100, "step": 5, "default": 60,
      "description": "How much the corners darken, from none at 0 to black at 100.", "binds": "db" },
    { "id": "saturation", "type": "slider", "label": "Saturation", "min": 0, "max": 200, "step": 10, "default": 100,
      "description": "The picture's colour, from grey at 0 to the game's own at 100 and stronger above.", "binds": "db" }
  ],
  "postfx": [
    { "shader": "fx/vignette.hlsl", "name": "vignette", "options": ["{{vignette}}", "{{saturation}}"] }
  ]
}
```

```hlsl [fx/vignette.hlsl]
#include "swofty/postfx.hlsli"

float4 effect(float2 uv, uint2 pixel)
{
    float4 c = scene_at(int2(pixel));
    float3 rgb = to_linear(c.rgb);                         // work in linear light: the same in SDR and HDR
    float strength = option(0) / 100, saturation = option(1) / 100;
    rgb = lerp(luminance(rgb).xxx, rgb, saturation);
    float2 d = uv - 0.5;
    d.x *= float(swofty_size.x) / float(swofty_size.y);    // round, not stretched to the screen
    rgb *= 1 - strength * smoothstep(0.3, 0.9, length(d));
    return float4(from_linear(rgb), c.a);
}
```

The build prints one line per pass:

```text
postfx: Vignette Pass's pass vignette runs as pass 1 after the tonemap (5968 bytes of DXIL)
```

In game, `swofty/loader.log` shows:

```text
[postfx] 1 pass(es) wait for the game's tonemap
[postfx] Vignette Pass's pass vignette is ready (postfx/00.dxil, 5968 bytes of DXIL)
[postfx] passes run on the game's 1920x1080 picture (format 28)
[postfx] the picture now goes through 1 pass(es) before the HUD, on the game's queue 0000000066737380
[postfx] passes now run at Present on a snapshot of the picture before the HUD (queue 0000000066737380), so every frame gets them
```

The **Addons** page tags the addon **Changes the picture**.

## The manifest

`postfx` is a list of passes. It needs `"postfx"` in `experimental_features`; the `postfx` feature is experimental, and on while an addon lists it there.

```jsonc
"postfx": [
  {
    "shader": "fx/vignette.hlsl",   // the .hlsl file, a path inside the addon
    "name": "vignette",             // the pass's name in the build output and loader.log; defaults to the file name without .hlsl
    "options": ["{{vignette}}", 100], // up to 16 values, read with option(0) to option(15)
    "order": 0,                     // where it runs among every addon's passes: lower first, equal values in load order; default 0
    "budget_ms": 1.5                // GPU milliseconds a frame, above 0 and at most 16; default 1.5
  }
]
```

- An `options` value is a number, or <span v-pre>`"{{id}}"`</span> naming one of the addon's `db` options. A toggle reads as 1 or 0.
- Installing checks every pass and refuses the manifest when a `shader` is not a `.hlsl` file inside the addon, a pass has more than 16 options, an option is unknown or does not bind `db`, or a budget is outside its range:

```text
invalid manifest: example.vignette-pass: postfx[0]: list "postfx" in experimental_features to use it
invalid manifest: example.vignette-pass: postfx[0]: option vignette must bind db
invalid manifest: example.vignette-pass: postfx[0]: budget_ms 20 is not above 0 and at most 16
```

- At build, an <span v-pre>`{{id}}`</span> option whose value is not a number stops the build: <span v-pre>`Vignette Pass: postfx option {{vignette}} is "abc", not a number`</span>.
- Option values change from the next launch: the build reads them.

## The shader

A pass includes `swofty/postfx.hlsli` and defines `effect`, which returns the colour of one pixel. The header is in the SDK at `packages/sdk/include/swofty/postfx.hlsli`, and the build puts it on dxc's include path. Files next to the shader can be included with `#include "name.hlsli"`.

```hlsl
// the picture
float4 scene(float2 uv);           // at uv (0 to 1), filtered
float4 scene_at(int2 pixel);       // at a pixel, clamped to the edges
float  scene_depth(float2 uv);     // the game's depth buffer as stored (eaid://ego_package/gbuffer_depth_stencil.image):
                                   // 1 at the camera, falling towards 0 with distance; 0 everywhere when has_depth() is false
bool   has_depth();                // whether the depth buffer was readable this frame; false while passes run at Present

// values
float  option(uint i);             // the pass's options[i], 0 when not set
uint2  swofty_size;                // the picture's size in pixels
float2 swofty_texel;               // 1 over it
uint2  swofty_depth_size;          // the depth buffer's size: the render resolution, below the picture's with upscaling on
float  swofty_time;                // seconds since the passes started
uint   swofty_frame;               // frames since the game started
uint   swofty_encoding;            // SWOFTY_POSTFX_SDR, SWOFTY_POSTFX_HDR10 or SWOFTY_POSTFX_SCRGB
uint   swofty_pass;                // the pass's place in this frame's chain, from 0

// colour
float3 to_linear(float3 c);        // decode to linear light: sRGB in SDR, PQ in HDR10, unchanged in scRGB.
float3 from_linear(float3 c);      // encode back. In linear light 1 is SDR white, and 80 nits in HDR.
float  luminance(float3 linear_rgb); // Rec. 709 luminance of a linear colour
float3 srgb_to_linear(float3 c), linear_to_srgb(float3 c), pq_to_linear(float3 c), linear_to_pq(float3 c);
```

- The picture is the game's encoded picture: sRGB in SDR (R8G8B8A8), PQ in HDR (R10G10B10A2).
- A pass reads the result of the pass before it.
- The output is the picture's own format, so values outside 0 to 1 clip in SDR.
- The header runs `effect` from an 8×8 `main` compute shader, once per pixel of `swofty_size`.

## Build

Launcher releases include dxc in a `dxc/` folder beside `bin/`, built from the open-source DirectX Shader Compiler with its DXIL validator. The build finds dxc in this order:

```text
SWOFTY_DXC                 another dxc
<launcher>/dxc/bin/dxc     the release's own
dxc on PATH
```

A pass must compile to signed DXIL. dxc without its validator (`dxil.dll`, `libdxil.so`) beside it writes unsigned DXIL, which the build refuses.

To check a pass while you write it, compile the addon's passes the way the build does. The command prints dxc's errors and warnings in full and exits non-zero when a pass fails:

```sh
swofty postfx compile path/to/my-addon [--out dxil/]
```

```text
dxc: /home/me/SwoftyLauncher/dxc/bin/dxc
ok    vignette (fx/vignette.hlsl): 5984 bytes of signed DXIL
```

The build compiles each pass with `dxc -T cs_6_0 -E main -HV 2021 -O3` and keeps the DXIL in the addon's cache, so an unchanged pass is not compiled again. A pass that does not compile is left out, and the build warns with dxc's first error:

```text
Vignette Pass: pass vignette is skipped: dxc: fx/vignette.hlsl:6:12: error: use of undeclared identifier 'nope'
```

The compiled passes are installed as `swofty/postfx/00.dxil` onwards, and `swofty/assets.json` lists them under `postfx` in the order they run.

## In game

The passes run at Present. Every 60 s for the first 30 minutes, the log counts the frames presented and the snapshots taken. Frames without a snapshot show the game's own picture; loading screens are most of them.

```text
[postfx] last 60 s at Present: 3852 frame(s); snapshots before the HUD: 3171 in the game's lists, 182 between them; 0 frame(s) without one
```

With Frame Generation on, or unknown, in the game's settings, passes run on the split instead. Every 10 s for the first minute, the log then counts the frames the passes ran on and the frames they skipped. The game keeps a HUD-less copy of the picture for Frame Generation, `eaid://ego_package/hudless_backbuffer.image`; on the split, the passes' result goes into it too.

```text
[postfx] swapchain 0000000062296D50: passes stay before the HUD, as frame generation is on or unknown in the game's settings
[postfx] last 10 s: passes ran on 598 frame(s); 0 tonemap list(s) also drew UI and 0 left no back buffer to draw on, so passes skipped them
[postfx] the game's HUD-less copy of the picture gets the passes too, for Frame Generation
```

With HDR on, the back buffer is R10G10B10A2 and holds PQ, and `swofty_encoding` is `SWOFTY_POSTFX_HDR10`. A pass that works in `to_linear`/`from_linear` looks the same in SDR and HDR.

```text
[postfx] passes run on the game's 1908x1028 picture (format 24)
```

A pass is turned off when its pipeline cannot be made, or when its average GPU time over 120 frames stays above `budget_ms` three times in a row. The core measures each pass's GPU time.

```text
[postfx] Vignette Pass's pass vignette is off: its pipeline could not be made (0x80070057)
[postfx] Vignette Pass's pass vignette is off: it took 2.410 ms of GPU time a frame, over its 1.50 ms budget
```

Switches:

```sh
# in the game folder: every pass off within a second; remove the file to turn them on again
touch swofty/postfx.off

# in the game's environment: every pass off for the session
SWOFTY_POSTFX=0

# write the picture before and after the passes six times in the first minute,
# and once more each time a swofty/postfx.capture file appears in the game folder
SWOFTY_POSTFX_CAPTURE=C:\captures
```

At Present the captured pictures hold the HUD, and `postfx-NN-hudless.ppm` holds the snapshot before it; on the split they are without the HUD. `SWOFTY_FRAME_CAPTURE` writes the presented frame with the HUD ([logging switches](/reference/std/logging)).

```text
[postfx] SWOFTY_POSTFX=0; every pass is off
```

## How it works

The game tonemaps the 3D picture into `eaid://ego_package/tonemap_output.image`, grades and encodes it into the back buffer, then draws the HUD into the back buffer. On some frames, and on most in garages, the picture and the HUD are in one command list.

Every frame, the core copies the back buffer into a snapshot of its own between the picture and the HUD:

- Where the game moves `eaid://ego_package/hudless_backbuffer.image` out of UAV while the back buffer is a render target, the copy goes into the game's list just before that barrier.
- Where a list in which `tonemap_output.image` goes from written to read ends with the back buffer still a render target, the core submits a copy list right after it on the same queue.

Both add copies and barriers only, so the game's pipeline state stays as it was.

At Present, the core submits a list on the swapchain's queue: each pass runs on a copy of the snapshot, between two textures of its own, and a merge adds what the passes changed to the presented back buffer wherever it still matches the snapshot. The HUD keeps the game's colours, and so does the picture under translucent HUD panels.

With Frame Generation on, the core marks the list in which `tonemap_output.image` goes from written to read. When that list leaves the back buffer as a render target and draws no UI, the core submits a list of its own right after it on the same queue: it copies the back buffer, runs each pass, and copies the result back into the back buffer and the HUD-less copy. The HUD lists then run as the game submitted them. Frames where the game draws the picture and the HUD in one list keep the game's picture.
