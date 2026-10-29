---
title: UI patterns
description: How stock F1 25 screens build multi-row lists, move focus between rows, block input, clip and fade scrolling content and host tab templates, what is proven in game and what is inferred, and how the launcher checks UI edits before they ship.
---

# UI patterns

These are the ways stock F1 25 screens combine UI nodes, read from the stock `.ui` files and `.idf` scenes, each marked **verified in game** or **inferred** from stock files alone; the data behind every statement is in `tools/uicatalog/catalog.json` and on [UI node reference](./nodes).

## A pattern in full

The launcher's era picker adds an Addons row under the stock Built In row of `dynamic_era_select`, and wires the two rows together the way `promo_store` does. This is the wiring, from `packages/core/src/ui/era.rs`:

```rust
// one row: its spawner reports a failed move to the row, the row forwards focus and input to its spawner
fn row_chain(row: &mut Node, other: &str, nav: [Vec<Node>; 4]) -> Result<()> {
    let name = row.name().to_owned();
    let spawner = format!("{}.tile_spawner", path(&name));
    let pins = ["navUp", "navDown", "navLeft", "navRight"];
    let failed = ["navigate_up_failed", "navigate_down_failed", "navigate_left_failed", "navigate_right_failed"];
    let events = row.child_mut("tile_spawner")?.slot_mut(EVENTS)?;
    for (from, pin) in failed.into_iter().zip(pins) {
        events.push(event(from, vec![sig(&path(&name), pin, from)]));   // spawner navigate_*_failed -> row navUp…
    }
    row.slot_mut(COMMANDS)?.extend(
        [("focus", "focus"), ("block_input", "inputDisallow"), ("unblock_input", "inputAllow")]
            .map(|(cmd, pin)| command(cmd, vec![sig(&spawner, pin, cmd)])),
    );
    let events = row.slot_mut(EVENTS)?;
    events.push(event("focused", vec![sig(&path(other), "unfocus", "focused")]));   // focusing one row unfocuses the other
    for (pin, signals) in pins.into_iter().zip(nav) {
        events.push(event(pin, signals));                                // what each row event does
    }
    Ok(())
}

// Built In: down asks the gate; Addons: up focuses Built In and plays the navigate sound
row_chain(&mut tiles, ADDON_TILES, [vec![], vec![sig(&path(GATE), "query", "navDown")], vec![], vec![]])?;
row_chain(&mut addon, TOP, [vec![sig(&path(TOP), "focus", "navUp"), sound("navUp")], vec![], vec![], vec![]])?;

// the gate: an SVCD that reads the view-model bool addon_tiles_visible and on yes focuses the Addons row
let mut gate = node(GATE, "SVCD",
    vec![("value", prop(boolv(true), None)), ("dbEntry", prop(s(ADDON_VISIBLE), None))],
    vec![event("yes", vec![sig(&path(ADDON_TILES), "focus", "yes"), sound("yes")])], &[], &[]);
let mut nav = node(NAV_SOUND, "UIAM", vec![], vec![], &[], &[]);    // trigger_nav_on_up_down
```

The same builder sets `InputOnActPrt` on the stock spawner, binds the new spawner's `dataContext` and `selection` to `addon_tiles` and `selected_addon_tile`, and adds `trigger`/`detach` and `unblock_input`/`block_input` to the template's `onShowCompleted` and `onTabDeselectBegin`. Each pattern below explains one of these parts.

## Tab containers

A container screen (`UISC`) holds a tab controller (`UITC`) and hosts tab templates from fragment 1 of the same `.ui`.

```text
#f1_world_flow.f1_world_compendium_container.tab_controller.tab_1( f1_world_compendium_chapter_tab )
                                              └ controller     └ tab_<n>( <template> )
```

- The `UITC` binds the scene's content element with one layout entry, such as `screen_container`, an `eXRef`. `contentPoint` (`Scene Root.screen.screen_cc`) and `usedElement` (`screen_cc`) name where a tab template's scene is placed, and `tabContainer` names the stacker that holds the tab headers.
- The tab templates are `UIGP` roots in the `Templates` list, each with its own scene in the components slot. Which templates become tabs comes from the view model: stock bindings name them as `tab_controller.tab_<n>( <template> )`, as above.
- The controller raises lifecycle commands on a tab template root. Stock `UIGP` nodes declare `onTabSelected` (43), `onShowCompleted` (30), `onTabActivated` (30), `onTabDeselectBegin` (10), `onTabDeselected` and `onTabDeactivated`, and forward them to their children. Stock `dynamic_era_select` declares only `onShowCompleted` (→ `tiles.trigger`, which spawns the row) and `onTabDeselectBegin` (→ `tiles.detach`, which despawns it).
- Fragment 1 of `f1_world_activity_container.ui` holds the tab templates `f1_world_activity` and `f1_world_activity_offline` (the play screen), `dynamic_era_select` (the era picker), `public_gp_select` and `time_trial_select`, and the tile templates they spawn (`activity_*_tile`, `era_select_tile`). The container's view model creates seven tabs: the two activity tabs, `dynamic_era_select` three times (Grand Prix, Time Trial, split-screen), `public_gp_select` and `session_create_dlc_select_tab` from `f1_world_container.ui`. No tab uses `time_trial_select` ([engine](./engine#tabs-uitc)).

**Verified in game:** when the game rejects one edited tab template, the whole container fails without an error at warn level. The play screen's category boxes vanish even though every other template in the file is byte-identical to stock. With the era UI edit off, the play screen always shows. With the same edited build (two rows, earlier navigation, no masks), the play screen showed in one session and not in a later one.

**Traced in game:** in that build the `dynamic_era_select` root kept the `NBVN` behaviour without its `visualNav` property, so its init returned 3 in all three era tabs. The tab controller dropped those three content instances without a log and built all seven headers ([engine](./engine#the-era-picker-container-failure)).

**Inferred:** the controller then maps the seven tabs to the four content instances it has out of step, which empties the play screen depending on which tab is selected first.

## Multi-row lists

A row is a `UIGP` holding a tile spawner (`F1TS`), magnets (`UIGP` nodes bound to `magnet_full_left`, `magnet_full_centre`, `magnet_full_right`) and a scrollbar (`UISB`).

```text
row (UIGP)
├── tile_spawner (F1TS)   layout: item_spawner | spawner_list (eStacker)   dataContext=@<list>  selection=@<selected>
│     spawned -> self.trigger_children, self.focus_and_reset, scrollbar.attach
├── magnet_full_left / magnet_full_centre / magnet_full_right (UIGP)
└── scroll_bar (UISB)
```

- The `F1TS` binds the row's stacker, `item_spawner` or `spawner_list` (`eStacker`, 207 of 267 spawners; 19 more through an `eXRef`). `dataContext` binds the list the spawner builds from and `selection` the selected item.
- On `spawned` a spawner signals itself `trigger_children` and `focus_and_reset`, and the scrollbar `attach`.
- The row's scene is a component reached through an `eXRef` in the screen's scene. In `vehicle_type.idf` that is `play_categories_row`, which references `vehicle_type_spawner.idf` (`scroll_bar`, `item_spawner`, three magnets).
- `promo_store` in `unified_store.ui` has three spawned rows (`featured_item`, `row_1`, `row_2`). They spawn on the root commands `attach_spawner` and `detach_spawner`, which the store's native view model raises.

**Verified in game:** two half-scale rows at the top level of `vehicle_type.idf` (the stock `play_categories_row` and a clone), each `F1TS` bound to its own view-model list, draw and scroll.

## Navigation between rows

A spawner fires `navigate_up_failed`, `navigate_down_failed`, `navigate_left_failed` and `navigate_right_failed` when focus tries to leave it. Two stock ways route that to another row:

1. **Direct.** The failed event signals the other row's spawner `focus`. **Verified in game** in the era picker: `navigate_down_failed` on the Built In spawner focuses the Addons spawner and `navigate_up_failed` on Addons focuses Built In.

   ```text
   builtin.tile_spawner  navigate_down_failed -> addons.tile_spawner.focus
   addons.tile_spawner   navigate_up_failed   -> builtin.tile_spawner.focus
   ```

2. **Through the row (`promo_store`).** Each row `UIGP` declares the commands and events, and the spawner reports to its row:
   - spawner `navigate_*_failed` signals its row's `navUp`, `navDown`, `navLeft`, `navRight` pin; a signal to a `UIGP` pin named like an event raises that event;
   - the row declares `focus` → spawner `focus`, `block_input` → spawner `inputDisallow`, `unblock_input` → spawner `inputAllow`;
   - the row's `focused` event signals the other row's `unfocus`;
   - `row_1`'s `navDown` signals the `SVCD` `row1_items_visible` `query` and the `UIAM` `trigger_nav_on_up_down` `navigate`; that `SVCD` reads the view-model bool `row2_items_visible` and on `yes` raises the screen command `row_2_focused`, which native code handles;
   - `navLeft` and `navRight` raise screen commands (`row_navigate_left`, `row_navigate_right`);
   - the template root forwards `onTabDeselectBegin` to the rows' `block_input` and `onTabActivated` to their `unblock_input`.

   ```text
   row_1.tile_spawner  navigate_down_failed -> row_1.navDown
   row_1               navDown              -> row1_items_visible.query, trigger_nav_on_up_down.navigate
   row1_items_visible  yes                  -> screen command row_2_focused   (native)
   row_1               focused              -> row_2.unfocus
   ```

   `row_1` declares no `navUp` event. Every stock `UIGP` `navUp`, `navDown`, `navLeft` and `navRight` event has at least one signal (106, 109, 22 and 102 of them).

   The launcher's era picker uses this form, with its own `addon_row_has_tiles` gate, as in [a pattern in full](#a-pattern-in-full).

**Inferred:** the promo_store chain depends on native handlers: `row_2_focused` and the `attach_spawner` commands come from the store's view model.

## Focus and input blocking

```rust
// packages/core/src/ui/era.rs: the stock era spawner takes InputOnActPrt
key.set_str("InputOnActPrt");
```

- `InputOnActPrt` is a `bool` set on `F1TS` (37), `UITG` (32) and `SCRL` (4) only. **Verified in game:** setting it on the era `F1TS` in place of `consumeStrategy` works.
- `consumeStrategy` is a `ui32` on 9 node types, mostly `DDMC` message containers (26).
- `inputAllow` and `inputDisallow` are pins on `F1TS`, `UIGP`, `TILE`, `UITC`, `UIKL` and others. `inputAllowed` is the matching property (a literal or bound `bool`).
- `focus`, `unfocus`, `focus_and_reset` and `focusFirstAvailable` are the spawner focus pins. **Verified in game:** the magnet rename from `focused` to `unfocus` works.
- A template root that drops `visualNav` must also drop `NBVN` from its behaviours; with `NBVN` kept, the template fails to build ([`behaviour.nbvn_visualnav`](#engine-rules)).

## Masks and clipping

Three element types clip:

```text
eMaskTexture   899 stock elements   a texture alpha mask (eMaskTexture.maskTextureResource, eMaskTexture.maskHalfVec, width, height);
                                    its children are eNull (1,053), eXRef (81), eStacker (29) and eScroller (17)
eWindow        491                  a clipping rectangle with rounded corners (cornerCurves, cornerCurvesRadius, maskedWindow);
                                    it holds 1,217 eXRef children
eMask          3                    only in driver_of_the_day.idf and tut_overlay_mask_group.idf; its children are eNull
```

In stock the mask sits inside the list's own component scene, between the transition nulls and the stacker:

```text
play_button_list.idf
└── transition_panel_list
    └── mask_list (eMaskTexture, play_side_panel_button_mask.tif)
        └── transition_list
            └── spawner_list (eStacker)
```

- The screen scene places that component with one `eXRef`. Other examples: `con_info_button_list.idf` (`mask_list` > `transition_list` > `spawner_list`), the laptop message lists (`content_mask` > `scroller` > `item_stacker`) and the career rewards lists (`mask_list` > `scroller`).
- The UI side binds the stacker inside the masked component, through the `eXRef` when needed (`eXRef>eStacker`). Nodes bind an `eMaskTexture` itself only to animate or hide it: `UIGP` 40, `UIXA` 9, `UIL2` 2.
- Every resolved stock layout path with more than one entry passes only through `eXRef` elements before its last entry.

**Verified in game:** wrapping each row `eXRef` of `vehicle_type.idf` in an `eMaskTexture` cloned from `mask_list`, with layout paths `[row_mask_x, row]`, empties the play screen. That build also added a new fade texture resource for the masks. The launcher's era picker ships with its row fade off (`ROW_FADE: bool = false` in `era.rs`).

## Runtime bindings

A binding names a view-model key relative to the template's view model, such as `tiles`, or a global path starting with `#`. Native code publishes the keys. A key that native code adds after the template is created, such as the era picker's `addon_tiles`, `selected_addon_tile` and `addon_tiles_visible` (published at `site_FeTile_EraTilesBuilt`), exists at bind time only if the native step ran first. Stock `dynamic_era_select` binds only `tiles` and `selected_tile`.

```rust
// packages/core/src/ui/era.rs
pub const ADDON_TILES: &str = "addon_tiles";
pub const ADDON_SELECTION: &str = "selected_addon_tile";
pub const ADDON_PRESSED: &str = "addon_select_pressed";
pub const ADDON_VISIBLE: &str = "addon_tiles_visible";
```

The launcher flags every view-model key an edited template binds that no stock node of the same template binds (`vm-key-new`).

## Checking UI edits

`packages/core/src/ui/check.rs` checks edited `.ui` and `.idf` resources in three layers:

```text
hard, engine   re/db/ui/loader_rules.json: the checks the engine's UI loader applies, read from F1_25.exe and documented
               on UI engine internals. The build stops. The finding cites the rule id, its evidence level, the error code,
               what a failure takes down and the function address
hard, format   the hashes and ids the engine resolves by, and bindings the edit breaks. The build stops
warning        the stock catalogue, and a diff against the stock template. Listed in the build's warnings
```

Run by hand on the era picker build that dropped `visualNav`:

```sh
cargo run --release -p swofty-core --example ui_check -- \
  stock/screens/f1_world_activity_container.erp=built/screens/f1_world_activity_container.erp
```

```text
HARD f1_world_activity_container.erp: eaid://ui_package/screens/f1_world_activity_container.ui: ui#1: dynamic_era_select [UIGP]: UIGP with NBVN: visualNav is missing [behaviour.nbvn_visualnav L2: rc 3, takes down the node; 0x144289830 Vm_VisualNavBehaviour_Init; 0x14417f1b0 Vm_Read_PropObject_Thunk; 0x143927f90 Vm_Find_PkgObjectKey_Thunk]
1 hard, 0 warnings, 1 not in the --against build
```

In a profile build, each finding starts with the name of the addon that last wrote the resource, and hard findings stop the build:

```text
1 UI edit(s) break rules the game's UI loader enforces:
  <addon>: f1_world_activity_container.erp: eaid://ui_package/screens/f1_world_activity_container.ui: ui#1: dynamic_era_select [UIGP]: UIGP with NBVN: visualNav is missing [behaviour.nbvn_visualnav L2: …]
```

A `stale-hash` finding in an addon's resource is a warning there: the engine resolves the node, event or signal by its stored hash, so it keeps the identity it had before the rename. The build's warning ends `the name no longer matches its stored hash, so the game still finds it by the hash, as before the rename`. The launcher's own menus still stop on it.

### Engine rules

Each rule in `re/db/ui/loader_rules.json` that the checker implements runs when the file lists its id; its id, level, `fail.code`, `fail.scope` and `fn` go into every finding it raises.

```text
node.record                 every node is [props with name and type, children, commands, events, layout, components(, behaviours, slot7)]
node.class_registered       the type FourCC is one of the 136 registered node classes
layout.path_nonempty        the first layout path is not an empty list
layout.step_keys            each layout step has isComponentId with a u64 componentId, or a u32 nodeId
components.keys             the first component has name and a u64 componentId
behaviours.type             every behaviour has a type
commands.entry, events.entry  every command has command and signals, every event event and signals (an empty list is allowed)
signals.entry               every signal has to_pin and to_object, every mapping a value
listbase.data_binding       list classes bind data with a non-empty dbentry
svcd.value                  an SVCD sets value
f1ts.input_type             an F1TS inputType is normal or dpadOnly
behaviour.nbvn_visualnav    a node that lists the NBVN behaviour sets a visualNav object; without it NBVN init returns 3
                            and a template root takes the whole instance down
template.duplicate          no template name repeats in the file or matches a template another stock .ui registers
screen.duplicate            no screen name repeats in the file or matches a screen another stock .ui registers
```

**Verified in game** for `behaviour.nbvn_visualnav`: the era picker edit that dropped `visualNav` from the `dynamic_era_select` root emptied the F1 World play screen.

Rules with a property requirement carry it as data, and the checker applies every such rule the file lists:

```json
{
  "id": "behaviour.nbvn_visualnav",
  "level": "L2",
  "fail": { "code": 3, "scope": "node" },
  "fn": "0x144289830 Vm_VisualNavBehaviour_Init; 0x14417f1b0 Vm_Read_PropObject_Thunk; 0x143927f90 Vm_Find_PkgObjectKey_Thunk",
  "applies_to": { "behaviours": ["NBVN"] },
  "props": [{ "key": "visualNav", "required": true, "shape": "object" }]
}
```

`applies_to` names node classes (`classes`), behaviour types in slot 6 (`behaviours`), or both. Each entry in `props` has a `key`, `required`, a `shape` (`any`, `object`, `value` for `{ value }`, `binding` for `{ value, dbentry }` with a non-empty `dbentry`), and optionally `values` with `case_insensitive`.

The rules `node.fourcc_matches_class`, `template.child_abort`, `template.link` and `screen.child_abort` describe what a failure takes down; they have no check of their own. Findings for pins and empty signal lists quote the engine's silently ignored cases: a pin the target class does not handle, and an event with no signals, do nothing at run time.

### Format checks

```text
stale-hash            node id and nameId, layout nodeId, event and command hashes, and signal to_object and to_pin hashes
                      match the djb2 of their names
integrity             signals to nodes missing from their own root
xref-mismatch         an eXRef's xref is md5-64 of its fileRef
scene-id              a scene's name is the componentId of its resource path
nodeidlookup          a scene's nodeidlookup is the sorted djb2 of every element's leaf name
component-missing     a screen or template root's scene is in this archive or in a stock archive
layout-node-missing   a layout step whose nodeId was in the stock scenes of the archive is still in an edited scene's nodeidlookup
layout-unresolved     a layout path that resolved against the stock scenes still resolves against the edited ones
```

`check::archives` runs these for every archive that carries an edited resource, each with its own scenes, so a screen carried by both `flow_f1_world.erp` and `screens/f1_world_activity_container.erp` is checked in both.

### Warnings

```text
node-type, element-type                        a node or element type no stock file uses
root-type, parent-type, element-parent         a type as a screen or template root, or under a parent type, where stock never puts it
prop, prop-shape, param, param-type            a property or param stock never sets on that type, or a value shape stock never uses for it
command, event                                 a command or event name stock never declares on that type
event-no-signals, command-no-signals           an event or command with no signals where every stock one of that name on that type has signals
event-signals, command-signals                 signals on an event or command that never has any in stock
pin-in, pin-out, link, pin-attributes          a signal to a pin stock never signals on the target type, from a pin the source type never
                                               sends from, between types stock never links, or with attribute shapes stock never sends
layout-shape, layout-element, layout-through   a layout attach shape, a bound element-type chain, or a path through a non-eXRef element
                                               that stock never uses
layout-node-unknown                            a new layout step whose nodeId is in no scene of the archive
component-elsewhere                            a new root whose scene is in another archive
element-id                                     an elementId that is not the djb2 of the element's dotted path
copies-differ                                  two archives carry different copies of the same resource; the game uses the copy from the
                                               archive that loads first
vm-key-new                                     a view-model key that no stock node of the same template binds
prop-dropped                                   a property the stock node sets and the engine reads (a rule's props, the class init, or the
                                               element base init) that the edit removes
```

### Where it runs

- Every addon build: after an addon's ERP operations apply, every archive with a changed `.ui` or `.idf` goes through `check::archives` against its stock copy. Hard findings stop the build.
- Every addon screen in `operations.screens`: hard findings stop the build; warnings go in the build's warnings.
- The core menu build: the hard checks run on every ADDON SETTINGS screen it generates. Menu edits written with `write_erp` call `check::archive_files(&[(name, stock archive, built archive), …])` and stop on hard findings.
- By hand:

```sh
cargo run --release -p swofty-core --example ui_check -- <stock.erp>=<built.erp> [<stock.erp>=<built.erp> …] [--against <stock.erp>=<other.erp>]
```

Each pair is a stock archive and the built archive that replaces it. `--against` prints only findings the other build does not have. The exit status is 1 when there are hard findings.

### Rebuilding the catalogue

```sh
F1_25_DIR=<game folder> python3 tools/ui_catalog.py nodes --baseline <baseline.json> [--stock <pristine copy>]
python3 tools/ui_catalog.py nodes-doc
F1_25_DIR=<pristine game folder> cargo test --release -p swofty-core --test real_ui_catalog
```

The last command checks that every stock `.ui` and `.idf` passes with no findings.
