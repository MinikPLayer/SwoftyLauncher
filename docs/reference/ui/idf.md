---
title: UI scenes (.idf)
description: The scene layout behind every F1 25 menu screen — a !pkg holding an eScene graph — how a .ui screen attaches to it, where scenes live in the UI archives, and what a scene does and doesn't store.
---

# UI scenes (`.idf`)

A UI scene is the layout of a menu screen, the named rectangles a screen fills with their positions, sizes and nesting, stored as an `.idf` resource that holds a [`!pkg`](/reference/pkg) container with an `eScene` graph of elements.

## A scene in full

The R&D overview, `eaid://ui_package/art/screens/career/rnd_overview.idf`, as `tools/idf.py` prints it (trimmed):

```sh
python3 tools/idf.py tree rnd_overview.idf
```

```text
scene[0] componentId=13709409214439678780 (0xbe419b0b8324e33c)  nodeidlookup=43
  'Scene Root' [eScene]  id=3368332727
    'screen_null' [eNull]  pos=(-8.6, 3.4, 0.050001)  id=3754916351
      'transition_null_7' [eNull]  pos=(17.3398, -0.050065, -5e-05)  anims=1  id=965168136
        'secondary_panel_overlay' [eNull]  pos=(-0.139805, 0.050065, 5.1e-05)  size(w,d,h)=(4, 7.36, 0)  geoms=1  id=2233497966
        'secondary_panel_2' [eScaler]  pos=(-8.3398, -0.248442, -0.049951)  id=175802430
          'bg_1' [eXRef]  pos=(3.95, -3.60195, -0)  scale=(1.66016, 1.52361, 1)  id=2020034021
          'info_stacker' [eStacker]  id=1380732324
            'perf_stack' [eXRef]  pos=(0, -0.001493, 0.05)  id=3509322036
            'dev_potential' [eXRef]  pos=(-1e-06, -4.4624, 0.05)  id=2825381984
      'tut_scaler' [eScaler]  pos=(0, 0, 1e-06)  id=2222365731
        'transition_null_4' [eNull]  pos=(6.42481, -3.68006, -5.1e-05)  anims=1  id=1916358441
          'tile_4' [eXRef]  pos=(-4.34812, 0.813113, 0)  scale=(0.999096, 0.999096, 1)  id=2861292184
```

One element in detail:

```sh
python3 tools/idf.py find rnd_overview.idf tile_4
```

```text
element 'tile_4' [eXRef] id=2861292184 (djb2 ok=True)
  position   = (-4.34812, 0.813113, 0)
  scale      = (0.999096, 0.999096, 1)
  rotation   = [-0.0, -0.0, 0.0, -1.0]
  children   = []
  param .XrefMaterialBinding = eaid://ui_package/art/components/tiles/tiles_rnd/3x4_rnd_overview.emb?context=default
  param .className = eXRef
  param .fileRef = eaid://ui_package/art/components/tiles/tiles_rnd/3x4_rnd_overview.idf
  param .guid = b67736f4-d8ed-457e-9c6b-bdda48ce29a1
  xref       = 12809085282548104185 (== md5-64(fileRef))
```

A `.ui` screen attaches its widgets to these elements by name. [Swofty UI](./swofty-ui) opens, renders and edits scenes. [Editing menus with an addon](./editing-menus) takes an edited scene into the game.

Cars and tracks also name their model resources `.idf`. This page covers the menu scenes in `ui_package`.

## Where scenes live

Scenes ship in the UI archives: the `.erp` files in `2025_asset_groups/ui_package/` and its subfolders, such as `screens/`. The scene itself is the `UILayout` resource named exactly `<path>.idf`, with no `?context=`. It has one fragment, the `!pkg`.

The archive also holds the resources the scene's elements point at:

```text
eaid://ui_package/art/screens/career/rnd_overview.idf                  UILayout       the scene
….idf/<element>?context=render&lod=0&mesh                               GfxMeshRes     an element's mesh: vertex and index buffer names, a material slot
….idf/<element>?context=render&lod=0&vb=0   (and &ib=0)                 GfxBufferRes   the mesh's vertex and index buffers
….idf/<element>?context=animation                                       AnimClip       an element's animation clip
….idf?context=animstringtable&file=0                                    ACStringTable  names the clips use
art/screens/career/rnd_overview.emb?context=default                     GfxMatBindRes  the scene's material bindings: mesh slot to .material
```

Textures (`.tif`, `GfxSurfaceRes`) and materials (`.material`, `GfxMaterialRes`) are separate resources that the materials name.

The same scene is often in several archives: 2,161 of the 5,208 scene names appear in exactly two, and shared components such as tiles appear in dozens. List the archives that hold a scene with [Swofty UI](./swofty-ui), or with:

```sh
python3 tools/erp.py <archive> 'rnd_overview.idf' | grep UILayout   # the second argument matches anywhere in the name; '*' lists all
```

## The container

The `!pkg` root holds a `scenelist`. Every stock scene has exactly one entry:

```text
{ scenelist: [ { scene: <element>, name: <ui64>, nodeidlookup: [<ui32>, …] } ] }

scene          element  the root element, named "Scene Root", of type eScene
name           ui64     the scene's componentId
nodeidlookup   ui32[]   the name index: the djb2 of every element's leaf name
```

## Elements

```text
name        stri                  the leaf name a widget attaches to, such as tile_4
elementId   ui32                  djb2 of the element's dotted path
typeName    shnm                  the element class, such as eNull or eXRef
position    vec3                  local translation, relative to the parent
scale       vec3                  local scale
rotation    quat                  local rotation, x y z w
params      [{ name, value }]     named parameters
geoms       [{ resourceid, materialid, extentsmin, extentsmax, asset }]
                                  the element's art: a mesh resource, its material bindings and its bounds
anims       [{ asset }]           the element's animation clips
children    elements              child elements
xref        ui64                  on an eXRef only: the componentId of the scene it references
```

An element's place on screen is its `position`, `rotation` and `scale` composed with those of every parent. Its size is the local box in its `minExtents` and `maxExtents` params, or the bounds of its geoms.

### Classes

```text
eScene                                      the scene root
eNull                                       a group or transform, the most common class
eXRef                                       a reference to another scene: the game loads the scene whose componentId is the
                                            element's xref; fileRef names that scene's .idf and XrefMaterialBinding its .emb
eText                                       a text box: style, boxWidth, halign, wrapping, textcase
eStacker                                    lays its children out in a line: stackerType, gap, useStaticGap
eCentre                                     centres its children
eScaler                                     a scaling container: scaleX, scaleY, scaleZ
eWindow                                     a clipping rectangle
ePoint, eAnchor                             anchor points
eMask, eMaskTexture                         masks
eScroller, eCanvas, eCamera, eBoundless     scroll regions, canvases, cameras and unclipped groups
```

See [References](#references) for `eXRef`. Common params are `className` (the class again), `guid` (a unique id per element), `minExtents` and `maxExtents`, `pivotRot`, `pivotTrans` and `scaleRot`, and `eLayer.layer` with `eLayer.layerIsOffset` for draw order.

## Identity

Three hashes tie a scene to the screens that use it, and a fourth ties a reference to the scene it loads. A tool that edits a scene keeps all four consistent.

```python
# tools/idf.py
def djb2(s):                          # over the name's UTF-8 bytes
    h = 5381
    for c in s.encode():
        h = ((h * 33) + c) & 0xFFFFFFFF
    return h

def component_id(path):               # md5-64: the first 8 bytes, big-endian, of the MD5 of the path without .idf
    if path.endswith('.idf'):
        path = path[:-4]
    ...
```

**componentId.** The scene's `name`:

```text
md5("eaid://ui_package/art/screens/career/rnd_overview") = be419b0b8324e33c…
componentId                                              = 0xbe419b0b8324e33c
```

```sh
python3 tools/idf.py scene-id rnd_overview.idf eaid://ui_package/art/screens/career/rnd_overview.idf
# componentId=13709409214439678780  0xbe419b0b8324e33c
#   md5-64(path)=13709409214439678780  match=True
```

**elementId.** Each element's `elementId` is the djb2 of its dotted path. The path starts at the top-level child's name; `Scene Root` is not part of it. `Scene Root`'s own id is `djb2("Scene Root")`.

```text
screen_null.tut_scaler.transition_null_4.tile_4  ->  elementId 2861292184
```

**nodeidlookup.** The scene's name index is the sorted, de-duplicated list of `djb2(leaf name)` for every element, `Scene Root` included. It holds leaf-name hashes, not elementIds. A new element adds the hash of its name in sorted position.

**xref.** An `eXRef`'s `xref` is the componentId of the scene it references: md5-64 of its `fileRef` path without `.idf`.

```text
fileRef  eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential.idf
xref     md5-64("eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential") = 0x91ea45fa2953cd98
```

## References

An `eXRef` element draws another scene inside this one. It stores the referenced scene twice:

```text
xref                         the referenced scene's componentId (ui64)
fileRef param                the referenced scene's eaid://….idf path
XrefMaterialBinding param    the referenced scene's .emb?context=default
```

The game finds the referenced scene by `xref` alone. It loads the scene whose componentId is `xref`, whatever `fileRef` says. Changing only `fileRef` changes nothing in game: the reference still loads the scene its `xref` names.

To point a reference at another scene, set `fileRef` and set `xref` to md5-64 of the new path:

```sh
python3 tools/idf.py retarget rnd_dev_potential_full.idf chart_1 \
  eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential5.idf out.idf
# chart_1 -> eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential5.idf  xref=6468277874628786377; wrote out.idf
```

In every stock scene the two agree: all 58,925 references in the 5,208 scenes' 26,913 copies in the UI archives have `xref == md5-64(fileRef)`, and every `XrefMaterialBinding` is the `fileRef` path with `.idf` replaced by `.emb?context=default`.

A reference whose `xref` and `fileRef` disagree shows the scene its `xref` names. For example, a `chart_1` whose `fileRef` was changed to a new `rnd_dev_potential5.idf` with its `xref` left at `0x91ea45fa2953cd98` draws the stock `rnd_dev_potential.idf` in game. `refs` marks it:

```text
MISMATCH chart_1  fileRef=eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential5.idf  xref=10514293220787342744  md5-64(fileRef)=6468277874628786377  (the game loads the scene whose componentId is 10514293220787342744)
mismatches 1
```

[Swofty UI](./swofty-ui#references) resolves references by `xref` as the game does, marks a reference whose `xref` and `fileRef` disagree, and refuses to export one. Its **Retarget** edit, and `retarget` and `set_param('fileRef', …)` in `tools/idf.py`, set `fileRef`, `xref` and `XrefMaterialBinding` together.

## How a screen attaches

A `.ui` screen (UIDF) places its nodes with layout attaches:

```text
// the screen's root: the whole scene, found by the scene's name (componentId)
{ componentId: 0xbe419b0b8324e33c, isComponentId: true, debugName: "art/screens/career/rnd_overview.idf" }

// a widget: one element, found by djb2(leaf name) through nodeidlookup
{ nodeId: djb2("tile_4"), debugName: "tile_4" }

// a list of entries walks through referenced scenes, outermost first
[ { nodeId: djb2("screen_container"), debugName: "screen_container" },   // the eXRef
  { nodeId: djb2("screen_cc"),        debugName: "screen_cc" } ]         // inside the scene it references
```

`debugName` is the element's leaf name, never a dotted path. A widget therefore binds to an element by name. Two elements with the same leaf name are ambiguous to an attach, so a new element needs a name no other element in the scene uses, such as `tile_5`.

## What a scene stores

A scene stores layout: the element tree, names and ids, transforms, boxes, params such as text style and stacker gaps, and references to art, animation clips and other scenes.

It does not store:

- **Pixels.** Textures are separate resources. A geom names a mesh and a material binding, and the material names its textures.
- **Meshes.** A geom points at `GfxMeshRes` and `GfxBufferRes` resources by name. A wedge's arc, for example, is baked into its mesh.
- **Text.** An `eText` element is a box with a style. The string comes from the `.ui` node attached to it, at runtime.
- **Live data.** Values, lists, colours and visibility come from the screen's `.ui` bindings and the game's view models.

What the game shows can also differ from the stored layout:

- **Animations.** An element with `anims` plays clips whose channels are element names and whose keys are absolute local transforms. When a clip plays, it sets the element's position, rotation and scale, which replaces a stored edit to that element. Move an unanimated child instead.
- **Stackers.** An `eStacker` lays out its children at runtime by `gap`, so a child's stored position is replaced. A stacker places any child added to it.
- **Collapsed elements.** Some elements, such as bars, pie wedges and highlights, are stored at a scale near 0, and an animation or live data grows them.

## Tools

[Swofty UI](./swofty-ui) browses, renders and edits scenes, and checks every export. `tools/idf.py` prints and edits scenes from the command line:

```sh
python3 tools/idf.py tree rnd_overview.idf          # tree with positions, sizes and ids
python3 tools/idf.py params rnd_overview.idf        # the same, with every param
python3 tools/idf.py geoms rnd_overview.idf         # the same, with every geom
python3 tools/idf.py slots rnd_overview.idf         # name, elementId, type, position, size as tab-separated rows
python3 tools/idf.py json rnd_overview.idf          # the whole container as JSON
python3 tools/idf.py find rnd_overview.idf tile_4   # one element in detail
python3 tools/idf.py scene-id rnd_overview.idf eaid://ui_package/art/screens/career/rnd_overview.idf
python3 tools/idf.py refs rnd_dev_potential_full.idf     # each reference, MISMATCH where xref != md5-64(fileRef); exit 1 on a mismatch
python3 tools/idf.py roundtrip *.idf                # parse and rewrite, compare bytes; exit 1 on a difference
python3 tools/idf.py clone-slot rnd_overview.idf tile_4 tile_5 0 -2.52 0 out.idf
python3 tools/idf.py set-pos rnd_overview.idf tile_4 0 -2.52 0 out.idf
python3 tools/idf.py retarget rnd_dev_potential_full.idf chart_1 eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential5.idf out.idf
```

```text
OK   ui/rnd_overview.idf 30732 bytes
bad 0
```

To check every scene in the game's UI archives, run `swofty-ui --check-refs` ([Swofty UI](./swofty-ui#check-every-reference)).

`tools/pkg.py` and `packages/formats/src/pkg.rs` read and write the container. All 5,208 scenes in the UI archives round-trip byte for byte through `tools/idf.py`, with every `elementId`, `nodeidlookup`, `componentId` and `xref` matching the rules above.
