use swofty_formats::hash::djb2;
use swofty_formats::pkg::{Node, Obj, Pkg, Val};

use super::check;
use super::idf::{self, elem_name};
use super::node::*;
use crate::Result;

pub const ARCHIVE: &str = "flow_frontend.erp";
pub const UI: &str = "eaid://ui_package/screens/fe_career_dlc_select.ui";
pub const TYPE_UI: &str = "eaid://ui_package/screens/fe_career.ui";
pub const SCENE: &str = "eaid://ui_package/art/screens/frontend/fe_career.idf";
pub const TEMPLATES: &str = "eaid://ui_package/screens/fe_home.ui";
pub const TEMPLATES_FRAGMENT: usize = 1;
pub const ADDON_TEMPLATE: &str = "addon_tile_button_default";
pub const ADDON_BUTTONS: &str = "addon_buttons";
pub const ADDON_VISIBLE: &str = "addon_buttons_visible";
pub const ADDON_PRESSED: &str = "addon_select_pressed";
pub const ADDON_FOCUSED: &str = "addon_focus_changed";

const SCREEN: &str = "fe_career_dlc_select";
const TYPE_SCREEN: &str = "fe_career";
const STOCK: &str = "spawnable_buttons";
const TITLE: &str = "screen_title";
const ACTION: &str = "Action";
const GATE: &str = "addon_buttons_gate";
const NAV_SOUND: &str = "trigger_nav_on_up_down";
const STACKER: &str = "stacker_items_contextual";
const STACKER_PATH: &str = "transition_anim_items.transition_fade_items.stacker_items_contextual";
const STOCK_SLOT: &str = "spawner_stacker_items";
const ADDON_SLOT: &str = "addon_stacker_items";
const STOCK_TEMPLATE: &str = "fe_tile_button_default";
const LABELS: [(&str, &str); 2] = [("label_builtin", "Built In"), ("label_addons", "Addons")];
const LABEL_SCALE: f64 = 0.8;
const GUIDS: [(&str, &str); 3] = [
    (ADDON_SLOT, "6d1e3b2a-4c5f-4e7a-9b80-1c2d3e4f5a61"),
    ("label_builtin", "9a2b4c6d-8e0f-4a1b-b3c5-d7e9f1a3b562"),
    ("label_addons", "2f4a6c8e-0b1d-4f3e-a5c7-e9b1d3f5a763"),
];

fn guid(name: &str) -> &'static str {
    GUIDS
        .iter()
        .find(|(n, _)| *n == name)
        .map(|(_, g)| *g)
        .expect("known guid")
}

fn path(name: &str) -> String {
    format!("{SCREEN}.{name}")
}

fn kid_mut<'a>(screen: &'a mut Node, name: &str) -> Result<&'a mut Node> {
    let owner = screen.name().to_owned();
    screen
        .slot_mut(CHILDREN)?
        .iter_mut()
        .find(|k| k.is_node() && k.name() == name)
        .ok_or_else(|| err(format!("{owner} has no {name}")))
}

fn event_mut<'a>(n: &'a mut Node, name: &str) -> Result<&'a mut Obj> {
    let owner = n.name().to_owned();
    n.slot_mut(EVENTS)?
        .iter_mut()
        .filter_map(Node::as_obj_mut)
        .find(|e| e.get("eventName").and_then(Node::as_val).and_then(Val::as_str) == Some(name))
        .ok_or_else(|| err(format!("{owner}: no {name} event")))
}

fn set_layout(n: &mut Node, slot: &str) -> Result<()> {
    let group = n
        .slot_mut(LAYOUT)?
        .first_mut()
        .ok_or_else(|| err("no layout group"))?
        .a_mut()?;
    let last = group
        .last_mut()
        .ok_or_else(|| err("empty layout group"))?
        .o_mut()?;
    set_str(last, "debugName", slot)?;
    last.at_mut("nodeId")?.v_mut()?.set_u32(djb2(slot));
    Ok(())
}

fn set_dbentry(n: &mut Node, key: &str, entry: &str) -> Result<()> {
    set_str(n.props_mut()?.at_mut(key)?.o_mut()?, "dbentry", entry)
}

fn command_name(n: &mut Node, event: &str, name: &str) -> Result<()> {
    let signal = event_mut(n, event)?
        .at_mut("signals")?
        .a_mut()?
        .first_mut()
        .ok_or_else(|| err(format!("{event} sends nothing")))?;
    let o = signal
        .o_mut()?
        .at_mut("attributes")?
        .o_mut()?
        .at_mut("command")?
        .o_mut()?
        .at_mut("name")?
        .o_mut()?;
    set_str(o, "value", name)
}

pub fn has_row(data: &[u8]) -> Result<bool> {
    let mut doc = Pkg::parse(data)?;
    let screen = first_screen(&mut doc)?;
    Ok(screen
        .slot(CHILDREN)?
        .iter()
        .any(|k| k.is_node() && (k.name() == ADDON_BUTTONS || LABELS.iter().any(|(n, _)| *n == k.name()))))
}

fn templates(doc: &mut Pkg) -> Result<&mut Vec<Node>> {
    doc.root.o_mut()?.at_mut("Templates")?.a_mut()
}

pub fn has_template(data: &[u8]) -> Result<bool> {
    let mut doc = Pkg::parse(data)?;
    Ok(templates(&mut doc)?
        .iter()
        .any(|t| t.is_node() && t.name() == ADDON_TEMPLATE))
}

pub fn taken(ui: &[u8], scene: &[u8], templates: &[u8]) -> Result<bool> {
    Ok(has_row(ui)?
        || idf::find(first_root(&Pkg::parse(scene)?)?, ADDON_SLOT).is_some()
        || has_template(templates)?)
}

/// fe_home.ui's template list with addon_tile_button_default, the career tile button laid out in the
/// Addons row: a spawned tile goes to the element its template's layout names, so the core types the
/// Addons row's tiles with this template instead of fe_tile_button_default (spawner_stacker_items).
pub fn build_templates(data: &[u8]) -> Result<Vec<u8>> {
    let mut doc = Pkg::parse(data)?;
    if doc.to_bytes()? != data {
        return Err(err(format!(
            "{TEMPLATES} fragment {TEMPLATES_FRAGMENT} does not round-trip"
        )));
    }
    let list = templates(&mut doc)?;
    if list.iter().any(|t| t.is_node() && t.name() == ADDON_TEMPLATE) {
        return Err(err(format!("{TEMPLATES} already has {ADDON_TEMPLATE}")));
    }
    let mut tpl = list
        .iter()
        .find(|t| t.is_node() && t.name() == STOCK_TEMPLATE)
        .ok_or_else(|| err(format!("{TEMPLATES} has no {STOCK_TEMPLATE}")))?
        .clone();
    rename_refs(&mut tpl, STOCK_TEMPLATE, ADDON_TEMPLATE);
    rename(&mut tpl, ADDON_TEMPLATE)?;
    set_layout(&mut tpl, ADDON_SLOT)?;
    fix_ids(&mut tpl, None);
    list.push(tpl);
    let out = doc.to_bytes()?;
    if Pkg::parse(&out)?.to_bytes()? != out {
        return Err(err(format!("the patched {TEMPLATES} does not round-trip")));
    }
    Ok(out)
}

/// The career content select with its stock tiles under a Built In label and an Addons row under them,
/// both labels shown only while the Addons row has a tile. The row shares the stock selection, so the
/// description below follows it, and raises its own press and focus commands for the core.
pub fn build_ui(data: &[u8]) -> Result<Vec<u8>> {
    let mut doc = Pkg::parse(data)?;
    if doc.to_bytes()? != data {
        return Err(err(format!("{UI} does not round-trip")));
    }
    if has_row(data)? {
        return Err(err(format!("{SCREEN} already has the Addons row")));
    }
    let screen = first_screen(&mut doc)?;
    if screen.name() != SCREEN {
        return Err(err(format!(
            "{UI}: the first screen is {}, not {SCREEN}",
            screen.name()
        )));
    }
    let title = kid_mut(screen, TITLE)?.clone();
    let mut addon = clone_node(kid_mut(screen, STOCK)?, SCREEN, ADDON_BUTTONS)?;
    set_layout(&mut addon, ADDON_SLOT)?;
    set_dbentry(&mut addon, "dataContext", ADDON_BUTTONS)?;
    event_mut(&mut addon, "spawned")?
        .at_mut("signals")?
        .a_mut()?
        .retain(|s| {
            s.as_obj()
                .and_then(|o| o.get("to_pin_name"))
                .and_then(Node::as_val)
                .and_then(Val::as_str)
                != Some("focus_and_reset")
        });
    command_name(&mut addon, "select_pressed", ADDON_PRESSED)?;
    command_name(&mut addon, "child_focus_changed", ADDON_FOCUSED)?;
    let sound = |from: &str| sig(&path(NAV_SOUND), "navigate", from);
    addon.slot_mut(EVENTS)?.push(event(
        "navigate_up_failed",
        vec![
            sig(&path(STOCK), "focus", "navigate_up_failed"),
            sig(&path(ADDON_BUTTONS), "unfocus", "navigate_up_failed"),
            sound("navigate_up_failed"),
        ],
    ));
    kid_mut(screen, STOCK)?.slot_mut(EVENTS)?.push(event(
        "navigate_down_failed",
        vec![sig(&path(GATE), "query", "navigate_down_failed")],
    ));
    let action = kid_mut(screen, ACTION)?;
    event_mut(action, "opened_screen")?
        .at_mut("signals")?
        .a_mut()?
        .push(sig(&path(ADDON_BUTTONS), "spawn", "opened_screen"));
    let kids = screen.slot_mut(CHILDREN)?;
    let at = kids
        .iter()
        .position(|k| k.is_node() && k.name() == STOCK)
        .expect("found above");
    kids.insert(at + 1, addon);
    for (name, text) in LABELS {
        let mut label = title.clone();
        rename_refs(&mut label, &path(TITLE), &path(name));
        rename(&mut label, name)?;
        set_layout(&mut label, name)?;
        literal_text(label.child_mut("label_title")?, text)?;
        set_prop(&mut label, "visible", prop(boolv(false), Some(ADDON_VISIBLE)))?;
        fix_ids(&mut label, Some(SCREEN));
        kids.push(label);
    }
    let mut gate = node(
        GATE,
        "SVCD",
        vec![
            ("value", prop(boolv(true), None)),
            ("dbEntry", prop(s(ADDON_VISIBLE), None)),
        ],
        vec![event(
            "yes",
            vec![
                sig(&path(ADDON_BUTTONS), "focus", "yes"),
                sig(&path(STOCK), "unfocus", "yes"),
                sound("yes"),
            ],
        )],
        &[],
        &[],
    );
    let mut nav = node(NAV_SOUND, "UIAM", vec![], vec![], &[], &[]);
    for n in [&mut gate, &mut nav] {
        fix_ids(n, Some(SCREEN));
    }
    kids.extend([gate, nav]);
    let out = doc.to_bytes()?;
    if Pkg::parse(&out)?.to_bytes()? != out {
        return Err(err(format!("the patched {UI} does not round-trip")));
    }
    let (_, _, bad) = check::integrity(&out)?;
    if !bad.is_empty() {
        return Err(err(format!("{UI}: {}", bad.join("; "))));
    }
    Ok(out)
}

/// The career type select, which shares the scene, with the Addons row's slots hidden.
pub fn build_type_ui(data: &[u8]) -> Result<Vec<u8>> {
    edit(data, |doc| {
        let screen = first_screen(doc)?;
        if screen.name() != TYPE_SCREEN {
            return Err(err(format!(
                "{TYPE_UI}: the first screen is {}, not {TYPE_SCREEN}",
                screen.name()
            )));
        }
        let kids = screen.slot_mut(CHILDREN)?;
        for (name, _) in LABELS {
            let mut n = hidden(name, &[name]);
            fix_ids(&mut n, Some(TYPE_SCREEN));
            kids.push(n);
        }
        Ok(())
    })
}

fn first_root(p: &Pkg) -> Result<&Node> {
    idf::scenes(p)?
        .first()
        .ok_or_else(|| err("a scene file without scenes"))?
        .o()?
        .at("scene")
}

fn find_mut<'a>(e: &'a mut Node, name: &str) -> Option<&'a mut Node> {
    if elem_name(e) == name {
        return Some(e);
    }
    let kids = idf::children_mut(e).ok()?;
    kids.iter_mut().find_map(|k| find_mut(k, name))
}

fn set_scale(e: &mut Node, scale: f64) -> Result<()> {
    e.o_mut()?
        .set("scale", Node::Val(idf::vec3([scale, scale, 1.0, 0.0])));
    Ok(())
}

/// fe_career.idf with a label slot over the stock list, and a label slot and an Addons stacker under it,
/// in the stacker that also holds the description, so the description moves down under the Addons row.
pub fn build_scene(data: &[u8]) -> Result<Vec<u8>> {
    let mut doc = Pkg::parse(data)?;
    let entry = idf::scenes_mut(&mut doc)?
        .first_mut()
        .ok_or_else(|| err(format!("{SCENE} has no scenes")))?;
    if idf::scene_name(entry) != Some(idf::scene_id(SCENE)) {
        return Err(err(format!("not {SCENE}")));
    }
    let root = entry.o_mut()?.at_mut("scene")?;
    if idf::find(root, ADDON_SLOT).is_some() {
        return Err(err(format!("{SCENE} already has {ADDON_SLOT}")));
    }
    let title = idf::find(root, TITLE).ok_or_else(|| err(format!("{SCENE} has no {TITLE}")))?;
    let mut labels = Vec::new();
    for (name, _) in LABELS {
        let mut label = idf::detached(title);
        label.o_mut()?.set("name", s(name));
        idf::set_param(&mut label, "guid", Val::str(guid(name)))?;
        idf::recompute_ids(&mut label, &format!("{STACKER_PATH}.{name}"))?;
        set_scale(&mut label, LABEL_SCALE)?;
        labels.push(label);
    }
    let stacker = find_mut(root, STACKER).ok_or_else(|| err(format!("{SCENE} has no {STACKER}")))?;
    let kids = idf::children_mut(stacker)?;
    let at = kids
        .iter()
        .position(|k| elem_name(k) == STOCK_SLOT)
        .ok_or_else(|| err(format!("{STACKER} has no {STOCK_SLOT}")))?;
    let mut addon = idf::detached(&kids[at]);
    addon.o_mut()?.set("name", s(ADDON_SLOT));
    idf::set_param(&mut addon, "guid", Val::str(guid(ADDON_SLOT)))?;
    idf::recompute_ids(&mut addon, &format!("{STACKER_PATH}.{ADDON_SLOT}"))?;
    let mut labels = labels.into_iter();
    let builtin = labels.next().expect("two labels");
    let addons = labels.next().expect("two labels");
    kids.insert(at + 1, addons);
    kids.insert(at + 2, addon);
    kids.insert(at, builtin);
    idf::relookup(entry)?;
    let out = doc.to_bytes()?;
    let back = Pkg::parse(&out)?;
    if back.to_bytes()? != out {
        return Err(err(format!("the patched {SCENE} does not round-trip")));
    }
    let mut bad = idf::xref_mismatches(&back)?;
    bad.extend(idf::id_problems(&back)?);
    if !bad.is_empty() {
        return Err(err(format!("{SCENE}: {}", bad.join("; "))));
    }
    Ok(out)
}
