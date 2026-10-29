---
title: Techniques for menu edits
description: General methods for changing F1 25 menus — trace what draws an element, tell a data limit from a screen limit, spot counts fixed in native code, patch native builders safely, edit scenes without breaking references, test, and a checklist for AI agents.
---

# Techniques for menu edits

These methods apply to any menu change: trace an element through its three layers before changing any of them, prove which layer limits a change, and patch native code only behind exact checks.

## A trace in full

A menu has three layers:

```text
.ui screen (UIDF)   widgets, their data bindings and events, and where each attaches    tools/uitree.py, tools/pkg.py
scene (.idf)        the rectangles widgets attach to, and the art they draw              Swofty UI, tools/idf.py
view model          the data the bindings read, published by native code                the game's log, the Game API
```

From an open menu to the data behind one element:

```sh
# 1. which screen opened: the game's UI and FLOW channels at info level
#    (swofty/session.json: "game_log": { "channels": "default,verbose" })
grep -E "OPEN SCREEN" swofty/loader.log

# 2. the screens an archive holds (type UIDF), and one screen's node tree from Swofty UI
python3 tools/erp.py 2025_asset_groups/ui_package/flow_frontend.erp '*' | grep UIDF
base=http://127.0.0.1:7341
curl -s "$base/api/resource?erp=flow_frontend.erp&name=eaid://ui_package/screens/fe_home.ui&frag=0" -o fe_home.ui
python3 tools/uitree.py -f fe_home.ui     # node tree: class, properties, bindings, events
python3 tools/pkg.py json fe_home.ui      # everything, including each node's layout attach

# 3. the element a node attaches to, in the scene the root attaches to
python3 tools/idf.py find rnd_overview.idf tile_4   # one element: transform, size, params, xref, geoms
python3 tools/idf.py refs rnd_overview.idf          # each reference: fileRef, xref id, OK or MISMATCH

# 4. the data: the binding's view-model path, published by native code (the Game API, a read-only hook)
```

The formats are on [UI scenes (.idf)](./idf), the addon workflow on [Editing menus with an addon](./editing-menus), and the editor on [Swofty UI](./swofty-ui). Native changes go through the [mixin system](/reference/addons/hooking). To trace native code while the game runs, see [Debugging live](./debugging).

## Find what draws an element

Trace an element through all three layers before changing any of them.

### 1. Find the screen

The game's log names the screens and flows that open. `swofty/loader.log` carries the game's `UI`, `VM` and `FLOW` messages at warn level and above by default; add `verbose` to the channels in `swofty/session.json` to copy their info messages too ([The game's own log](/reference/std/logging#the-game-s-own-log)):

```json [swofty/session.json]
{ "game_log": { "level": "info", "channels": "default,verbose" } }
```

Open the menu, then read which screen opened. List the screens an archive holds; their type is `UIDF`:

```sh
python3 tools/erp.py 2025_asset_groups/ui_package/flow_frontend.erp '*' | grep UIDF
```

With [Swofty UI](./swofty-ui) running, read a screen's node tree, fragment 0, from the archive. `erp` is the archive's path inside `ui_package`:

```sh
base=http://127.0.0.1:7341
curl -s "$base/api/resource?erp=flow_frontend.erp&name=eaid://ui_package/screens/fe_home.ui&frag=0" -o fe_home.ui
```

### 2. Read the widget and its attach

```sh
python3 tools/uitree.py -f fe_home.ui     # node tree: class, properties, bindings, events
python3 tools/pkg.py json fe_home.ui      # everything, including each node's layout attach
```

In the tree, `name [CLASS]` is a node and its FourCC class. A property shown as `value@path` is bound to view-model data, and `~ on <event> -> <node>.<pin>` is an event. In the JSON, a node's layout attach is one of:

```text
{ componentId, isComponentId: true, debugName: "<path>.idf" }   on the screen's root: the scene the screen draws in
{ nodeId, debugName: "<leaf>" }                                  the scene element the node attaches to, by its leaf name;
                                                                 a list of several walks through references, outermost first
```

See [How a screen attaches](./idf#how-a-screen-attaches).

### 3. Follow it into the scene

Open the root attach's scene in Swofty UI (<kbd>Ctrl</kbd>+<kbd>K</kbd> and its path) and filter the hierarchy by the leaf name. When the element is an `eXRef`, its art is in another scene: the scene its **`xref` id** names, which the inspector shows as the scene the game loads. `fileRef` does not decide it ([References](./idf#references)). Repeat until you reach the element with geoms.

```sh
python3 tools/idf.py find rnd_overview.idf tile_4   # one element: transform, size, params, xref, geoms
python3 tools/idf.py refs rnd_overview.idf          # each reference: fileRef, xref id, OK or MISMATCH
```

### 4. Find the data

A binding names a view-model path, such as `@tiles` or `@#frontend_flow.fe_home.tutorials_enabled`, and native code publishes it. How the screen gets its count tells you where to change it:

- A spawner node (class `F1TS`) with `dataContext=@rows` builds one child per entry of the bound list. More entries show more children, if the scene has room for them.
- A fixed set of named nodes, such as `tile_1` to `tile_4`, each bound to its own path, is a fixed count in the `.ui`. A fifth needs a fifth node, a fifth element for it to attach to, and data at a fifth path.

## Data layer and UI layer

Loading is usually data-driven. The game reads [database](/reference/gamedb) rows by key and loops on `_instancecount_` keys, so a new row loads with no code. Screens often aren't: a native builder can create a fixed number of widgets from constants, and a scene authors a fixed number of slots. A new row can therefore be loaded, real in the game's model, and never shown.

Prove which layer stops a change before editing either:

1. Log what the loader reads: a read-only hook after the table cursor opens, logging the row count for each category.
2. Log what the screen's builder receives: the results it keeps for each category.
3. Compare. If the loader reads the new row and the builder never sees it, the limit is between them, in native code. If the builder sees it and nothing draws, the limit is in the `.ui` or the scene.

A category, such as a team, a department or a mode, usually reaches the screen through several paths: its name, icon, colour, counts, selection and Back navigation each read the category index through their own table or guard. Find every reader of the field before calling a change done. Search the disassembly for every instruction that reads the field's offset, such as every `byte [reg+<field>]`, and record what each does with a new value.

### Counts fixed in native code

These shapes mean a count is fixed in code. The values are examples.

```text
unrolled construction   the same constructor called N times, each with literal          N widgets; there is no loop to extend
                        arguments such as a name string and an enum
exact-size buffer       mov edx, 0x4a0   where 0x4a0 = 0x20 + 4 × 0x120                 room for 4 slots; growing the allocation constructs nothing
inlined loop            add rax, 0x120 ; cmp rax, 0x4a0 ; jne                         exactly 4 slots are constructed
vector sized exactly    end - begin compared with or set to N × 8                       N pointers, then truncated
range guard             cmp r14d, 3 ; ja <skip>                                         values above 3 are skipped
clamp                   xor ecx, ecx ; cmp al, 4 ; cmovae eax, ecx                      an index of 4 or more becomes 0
early return            cmp byte [rcx+<field>], 3 ; ja <ret>   at a function's start    the function does nothing for values above 3
sentinel branch         cmp byte [rcx+<field>], 4 ; jne <normal>                        value 4 gets special handling, such as a disabled placeholder
jump table              cmp r14d, 3 ; ja <default>   before an indirect jmp             4 cases; the default often produces an error_* name and white
lookup table            lea rcx, [table] ; mov rdx, [rcx+rax*8]                         index N reads whatever follows the table
```

For a lookup table, check what follows it in the executable. When another table follows, index N reads that table's first entry: a colour read as a `char *` crashes on first use. When a string follows, index N reads that string: no crash, wrong text. A table needs a real entry N before any guard in front of it is relaxed.

On screen, an `error_*` label or a missing icon frame, such as a lang key `lng_…_error_…` or an atlas frame `error_…`, shows that an index went past a bounded lookup.

## Patch native builders safely

These rules come on top of the [engine plugin rules](/reference/addons/native-plugins#rules).

- **Gate on exactly one match.** Find each site by signature in `.xtls`; `scan` sets the address only for exactly one hit. Then verify the bytes of every other site you touch at its offset from that gate. On any mismatch, install nothing and log which check failed.
- **All or nothing, guard last.** Create every hook first, then grow buffers, then relax guards and clamps as the last step. When one step fails, undo the earlier ones. A relaxed guard is what sends new values into old code.
- **Never relax a bound before the data behind it exists.** Before `cmp …, 3` becomes `cmp …, 4`, entry 4 must exist in every table the guarded code indexes, every buffer must have room for it, and every object it touches must be constructed.
- **Check branch targets.** No branch in the function may land inside the bytes a hook overwrites. Check the containing function's code, and search `.xtls` for relative branches into the window.
- **Construct extra slots where the real constructor runs.** Grow the allocation, then build the extra slot right after the real constructor returns, the same way it builds the others: the same vtables, empty links. Read the code around that point:
  - A block after a move-assign that destroys the moved-from temporary can be dead. The move clears the source, so the block's null check always skips it, and code placed there never runs.
  - A function called next to construction can be the destructor. Read its callers: when every caller is a teardown path, it destroys.
  - Memory nobody constructed holds the allocator's leftovers. The first move-assign into it reads a garbage pointer and crashes while unlinking it.
- **Destroy extra slots on every teardown path.** A rebuild (move-assign), an explicit teardown and the destructor each destroy only the stock slots. Hook a point every path passes, such as the slot destructor's entry, and destroy your slot there before the stock ones. Otherwise its handles stay linked into live lists from freed memory.
- **Gate use on a marker.** Write a footer into the grown allocation, such as a magic value, the object's address xor a key, the capacity and a generation, and keep a registry of the objects you extended. Before code uses an extra slot, check both. On a miss, skip the slot and let the stock path run.
- **Identify your objects by content.** Games often build an object on the stack and then move it. Match your rows by name or hash, never by address.
- **Log before you change behaviour.** Ship read-only hooks first, logging counts and state: rows read per category, results kept per category, the size of a slot vector. Log once per build or rate-limit, prefix lines with your addon id, and log each refusal with its reason. Keep the diagnostics alongside the behaviour change for its first runs.
- **Expect saved state.** The game saves some runtime state and restores it as it was. A state an earlier build of your addon wrote comes back on load, so repair it after load, not only at creation.

## Edit scenes

- **Set `xref` with `fileRef`.** The game loads a reference by its `xref` id. Use **Retarget** in Swofty UI or `tools/idf.py retarget`, and check with `tools/idf.py refs` or `swofty-ui --check-refs` ([References](./idf#references)).
- **Keep ids consistent.** Each `elementId` is the djb2 of its dotted path, `nodeidlookup` holds every leaf name's djb2, and every leaf name is unique. Clone, rename and delete with Swofty UI or `tools/idf.py clone-slot` ([Identity](./idf#identity)).
- **Animations and stackers replace stored positions.** Move an animated element's unanimated children instead. A stacker lays its children out at runtime. Some elements are stored collapsed and grown by an animation or live data ([What a scene stores](./idf#what-a-scene-stores)).
- **New shapes need new meshes.** A shape, such as a wedge's angle, is baked into its mesh, and rotating copies of a mesh made for four parts to make five overlaps them. A new image is a new texture ([Textures and models](/reference/data/textures)).
- **Replace the scene everywhere.** Ship it to every archive that holds it; the game reads the copy in whichever archive loads first.
- **Change the chain the screen uses.** Different screens can draw the same component through different scenes. Follow the chain from the root attach of the screen you're changing.
- **Wire the new element.** A new element stays empty until a `.ui` node attaches to its leaf name. When the screen refreshes its widgets through a list of named targets, such as an event sent to each tile, add the new node to that list.

```sh
python3 tools/idf.py clone-slot rnd_overview.idf tile_4 tile_5 0 -2.52 0 out.idf
python3 tools/idf.py refs out.idf && python3 tools/idf.py roundtrip out.idf
```

## Test loop

```sh
swofty build                 # 2. read its warnings
swofty run --apply           # 3. installs the profile, follows the logs, rolls the game files back when the game exits
swofty logs                  # 4. afterwards: each hook installed, each refusal, your diagnostics
swofty verify                # 5. the game files match the backed-up originals?
swofty restore               #    when they don't
```

1. Test in a duplicate of your profile; each profile keeps its own saves. Copy its saves before a run that changes native state.
2. `swofty build`, and read its warnings.
3. `swofty run --apply`. It installs the profile, follows the logs, and rolls the game files back when the game exits.
4. Read `swofty/loader.log`, or `swofty logs` afterwards: each hook installed, each refusal, and your diagnostics.
5. After the game exits, run `swofty verify`. It checks that the game files match the backed-up originals. When they don't, run `swofty restore`.

When chasing a crash, change one thing per run. See the [command line](/reference/cli) for each command.

## For AI agents

Work through files and HTTP, not the browser:

- **Swofty UI's HTTP API.** Start it with `--no-browser` (`bin/swofty-ui --no-browser`, or `python3 tools/swofty-ui/serve.py --no-browser`). `GET /api/open` returns a `doc` id. `POST /api/apply` with `{ doc, ops }` replays edits and returns the scene model with a `results` entry per edit, or HTTP 400 with the failing edit's `opIndex`. `POST /api/export` writes and checks the scene: read `checks.ok`, `checks.elementIdsBad` and `checks.xrefMismatches`; HTTP 422 means a reference mismatch, and nothing was written. Keys address elements by child index in the tree as it is when each edit runs ([HTTP API](./swofty-ui#http-api)).
- **Replays.** An export's `.edits.json` holds its `ops`. After a game update, send them to `/api/export` on the new stock scene instead of editing the old file.
- **Validators.** Check every scene you write with `python3 tools/idf.py roundtrip <file>`, `python3 tools/idf.py refs <file>` (exit status 1 on a mismatch) and the export checks, and run `swofty-ui --check-refs` on a built game.
- **Screens.** Read `.ui` screens with `GET /api/resource` and `tools/uitree.py -f` or `tools/pkg.py json`, as in [Find what draws an element](#find-what-draws-an-element).

```sh
python3 tools/swofty-ui/serve.py --no-browser &
doc=$(curl -s "$base/api/open?eaid=art/screens/career/rnd_overview.idf" | jq -r .doc)
curl -s "$base/api/apply" -H 'Content-Type: application/json' \
  -d '{"doc":"'"$doc"'","ops":[{"op":"clone","key":"0.0.2.5.0","name":"tile_5"}]}' | jq '.results'
```

Guardrails:

- Never write into the game folder. Ship changes as an addon and let the build install them through the journal. Never commit game files; the tools read them from `work/`, which git ignores, or `F1_25_DIR`.
- Ask before starting the game or touching saves.
- Claim only what you have evidence for: an address from disassembly, a count from a log line, a layout from a rendered scene. Mark the rest as unverified. Record engine findings in the [RE knowledge base](https://github.com/Swofty-Developments/SwoftyLauncher/tree/main/re/db) through `re/tools/kb.py`, with the evidence levels in [`re/PROTOCOL.md`](https://github.com/Swofty-Developments/SwoftyLauncher/blob/main/re/PROTOCOL.md).
- Follow the patching rules above: exactly-one gates, byte checks, guards relaxed last, diagnostics before behaviour.
- Change one thing per test run, and read the log before the next.
