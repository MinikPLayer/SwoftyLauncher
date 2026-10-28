# fe_tiles: generic tile-menu builder and item classes

Owner: see `subsystems.tsv`. Write-up: research/exe.md §3.1–3.2.

## Map
- `FeTile_Build_List(TileList*, ctx, a3, a4, menu_kind, sub_kind, a7)` (0x14173df80) serves home, multiplayer, F1 World, showroom, customisation and career content. Callers 0x141bb2125 (FEHomeViewModel) and 0x141a48cc5.
  - clears `TileList.items` (0x10-byte `TileEntry {node, tile}`), binds VM `buttons` (+0x38) and `selected_button` (+0x58), switches on (menu_kind, sub_kind);
  - after the switch (`site_FeTile_PopulateLoop`, 0x141745016) it calls `item->vt.init` then `item->vt.populate` for every item in vector order, so an item can be re-typed between its adder and its populate.
- Tile = 0xd8 bytes (`structs/Tile.h`), HomeItem = 0x148 bytes (`structs/HomeItem.h`), vtable layout `structs/ItemVtbl.h` (19 slots).
- Base implementations: `FeTile_ItemBase_*` (slot names in functions.tsv). Generic getAction `FeTile_ItemGeneric_GetAction` returns `{1, item->id}` and has 21 byte-identical clones, so reach it through a vtable, never a signature.
- `.data1` bodies (black boxes, call only): `FeTile_ItemVec_PushBack_Body`, `FeTile_List_GetSelectedItem_Body`, `FeTile_ItemBase_IsSelectable_Body`.

### Era select (Grand Prix, Time Trial and split-screen category boxes)
- `DynamicEraSelectTabViewModel` (VM `dynamic_era_select`) is not a `FeTile_Build_List` menu. `FeTile_Build_EraTiles` (0x141a6e980, vtable 0x14572a4a0 slot 19 at 0x14572a538) builds it on tab activation:
  - resets VM `tiles` and `selected_tile`;
  - adds a fixed list of tiles with `FeTile_Add_EraTile` (0x141a6d0e0), each behind `FeTile_Is_EraScenarioAvailable_Thunk(*(vm+0x670), setup mode, entrant list)`;
  - stops at `site_FeTile_EraTilesBuilt` (0x141a6f728, rsi = vm).
- The tile list is hardcoded:

  | name | setup mode / entrant list | title | image (`tiles_solo_6x6[...]`) |
  |---|---|---|---|
  | `f1_26` | `f1_26` / `f1_2026` | `lng_showroom_f1_2026` (is_dlc) | `f1_26` |
  | `f1` | `f1` / `f1_2025` | `lng_showroom_f1` | `f1` |
  | `f1` | `f1` / `f1_2024` | `lng_showroom_f1` | `f1` |
  | `f1_world` | `f1_world` | `lng_gp_f1_world` (disabled, `lng_not_available_in_preview`, while `*(*0x1460fddb8+0x18)` is set) | `player_car` |
  | `f2_26`, `f2_25`, `f2_24`, `f2_23` | `f2` / `f2_2026` ... `f2_2023` | `lng_showroom_f2_*` (`f2_26` is_dlc) | `f2_*` |
  | `f1_preview` | previews, also gated by 0x14078dbe0(*(vm+0x678), *(vm+0x680), entrant list) | | |
  | `0000` | test | | |

  A `game_setup.scenarios` row with another entrant list never gets a tile, whatever the database says.
- Tile objects are 0x1a8 bytes (template `era_select_tile`): name holder, title at Tile+0x110, subtitle +0x130, command (entrant list) +0x150. The vector at vm+0x6d8 holds 0x10-byte `{allocator, Tile*}` entries; the allocator is `*(vm+0x270)`.
- `FeTile_Press_EraTile` (0x141a6e420, slot 10 at 0x14572a4f0, also via the adjustor thunk 0x141a6e970) takes the pressed Tile from that vector and raises `entrant_list_selected` with its command. A tile appended at `site_FeTile_EraTilesBuilt` with `FeTile_Add_EraTile(vm+0x690, *(vm+0x270), count, "select_tile_<count>", ...)` is therefore pressed like a stock tile.
- The tile set at vm+0x690 is `{vtable, tiles handle +0x8, vtable +0x20, selected_tile handle +0x28, vector allocator +0x40, vector +0x48..+0x58}`; each handle is `{node, next holder, previous holder}` linked into the node's holder list at node+0x28, with the node's reference count at node+0x24. `FeTile_Build_EraTiles` creates `tiles` with `Vm_GetOrCreate_ListChild` (call 0x141a6edab) and `selected_tile` with `Vm_Set_NodeRef` (call 0x141a6eeb9) under the VM root node `*(vm+0x10)+8`, and clears the old set first with `FeTile_Clear_EraTileSet_Thunk` (call 0x141a6eb62, body 0x14bd31910 in `.data1`), which removes the list node's children, frees the vector's tiles and releases both handles. A second set with the same layout and an empty vector can hold another list and reference (the core's addon row does this); the clear then only empties its list and releases its two handles.

## Open
- `FeTile_Unk_14174c710`, `FeTile_Unk_14174cb20`, `FeTile_Unk_141745280`: decompiled next to the builder, roles unknown.
- Which other menus (menu_kind values) map to which cases.

## Inbox
- 2026-10-19 14:03 ui-re: generic container tab builders (SessionDirector/Results addTab*Screen) are mapped in fe_flow, not as FeTile_Build_List menus: they push per-tab VMs into a container vector at +0x288, distinct from the TileList item vector. The vm lane now has the full ~97-class node-class (widget) FourCC registry (Vm_Register_NodeClasses 0x14419b1d0).
- 2026-10-22 22:15 verify-seasons: verify-seasons (V1002-V1006, all agree), corrections for the owner. (1) The vtable slots are wrong. FeTile_Build_EraTiles is at 0x14572a538, not 0x145729638 (that address holds string bytes). FeTile_Press_EraTile is at 0x14572a4f0, not 0x1457295f0 (that holds 0x141b40b80). Both are in the vtable at 0x14572a4a0, slots 19 and 10. The press handler is also reached through the adjustor thunk 0x141a6e970 (jmp at 0x141a6e977, not 0x141a6e978), which sits in the slot at 0x14572a5e0. Fix the functions.tsv evidence and notes, the site_FeTile_EraTilesBuilt notes and fe_tiles.md lines 14 and 32. (2) The site_FeTile_EraTilesBuilt xref 0x141a6f6c6 is mid-instruction. The skip branch is je 0x141a6f728 at 0x141a6f6b5. (3) The table leaves out three things. The preview tiles also need 0x14078dbe0(*(vm+0x678), *(vm+0x680), entrant list). f2_26/f2_2026 is is_dlc 1. f1_world is disabled and titled lng_not_available_in_preview when byte *(*0x1460fddb8+0x18) is set.
- 2026-10-22 22:20 classic-seasons: applied the V1002-V1006 corrections (slots 0x14572a538/0x14572a4f0, thunk 0x141a6e970/0x141a6e977, skip branch 0x141a6f6b5, preview gate, f2_26 is_dlc, f1_world preview state) to the rows and the map above.
- 2026-10-28 12:19 career-scope: site_FeTile_CareerContentEnd (0x141741831) is a shared tail: outer case 4 (showroom, menu kind 4) ends its last tile (showroom_player_cars, lea 0x141742e4d, push back 0x141742eba) in the holder release block 0x141742efb-0x141742f3d, whose jne 0x141741820 at 0x141742f37 and jmp 0x14174181c at 0x141742f3d enter case 6's release (call 0x1442b3d70 at 0x14174182c), so the site runs for both menus; no other branch in FeTile_Build_List lands in 0x141741600-0x141741836 besides case 6's own. The showroom VM comes from the screen factory 0x141649f80: 0x3c8 bytes, base ctor 0x141a482f0 with 'fe_showroom' 0x145739089, vtable 0x1457390a0 (lea 0x14164beba, store 0x14164bec1), word 0x0400 at +0x3a8 (0x14164beee), so its TileList is also at +0x2c8. The factory's other menu kinds: 0x0100 (0x14164bb9a), 0x0600 career content (0x14164bcb6), 0x0200 (0x14164bdd2), byte +0x3a9 = 5 (0x14164c4d3). Seen at runtime (two Send Logs reports, 1.5.21 and 1.5.23): the core's site logged a 7-tile list during fe_showroom.
