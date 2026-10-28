use swofty_formats::pkg::{Node, Pkg, Val};

use super::check;
use super::era::{event_mut, first_root, place, set_dbentry, set_layout, template_mut};
use super::idf::{self, elem_name};
use super::node::*;
use crate::Result;

pub const ARCHIVES: [&str; 2] = ["flow_f1_world.erp", "screens/f1_world_container.erp"];
pub const UI: &str = "eaid://ui_package/screens/f1_world_container.ui";
pub const UI_FRAGMENT: usize = 1;
pub const SCENE: &str = "eaid://ui_package/art/screens/modular_menu/mm_mp_gp_create.idf";
/// Addon boxes the Car Category screen has room for; core fills at most this many
/// (`MAX_ADDED` in native/src/core/car_category_logic.hpp).
pub const SLOTS: usize = 4;

const TEMPLATE: &str = "session_create_dlc_select_tab";
const STOCK_BUTTON: &str = "base_game_select";
const STOCK_ELEMENTS: [&str; 2] = ["content_dlc", "content"];
const SCALE: f64 = 0.5;
const ROW_Y: [f64; 2] = [1.6, -2.3];
const COLUMN_X: [f64; SLOTS] = [-6.525, -2.175, 2.175, 6.525];
const GUIDS: [&str; SLOTS] = [
    "6a1f0c3e-58d2-4b7e-9c41-2e8d7b3a0f61",
    "b4e2d9a7-1c5f-4a38-8e06-7d3f2a9c1b72",
    "0d7c3b5e-9a24-4f61-b8d3-5c1e7a2f9083",
    "e95a2f1c-7b3d-4c86-a1f4-3b6d8e0c2a94",
];

/// The template node and scene element of addon box `k`.
pub fn slot(k: usize) -> String {
    format!("addon_select_{k}")
}

/// The VM child an addon box shows: core appends addon boxes to `buttons` after the two stock ones.
pub fn entry(k: usize) -> String {
    format!("buttons.entry_{}", k + 2)
}

/// The command a press raises; the stock boxes raise `button_1` and `button_2`.
pub fn command(k: usize) -> String {
    format!("button_{}", k + 3)
}

pub fn taken(ui: &[u8], scene: &[u8]) -> Result<bool> {
    let mut doc = Pkg::parse(ui)?;
    let tpl = template_mut(doc.root.o_mut()?, TEMPLATE)?;
    let in_ui = tpl
        .slot(CHILDREN)?
        .iter()
        .any(|k| k.is_node() && k.name().starts_with("addon_select_"));
    Ok(in_ui || idf::find(first_root(&Pkg::parse(scene)?)?, &slot(0)).is_some())
}

fn press_command(button: &mut Node) -> Result<&mut swofty_formats::pkg::Obj> {
    let first = event_mut(button, "pressed")?
        .at_mut("signals")?
        .a_mut()?
        .first_mut()
        .ok_or_else(|| err(format!("{STOCK_BUTTON} pressed sends nothing")))?;
    first
        .o_mut()?
        .at_mut("attributes")?
        .o_mut()?
        .at_mut("command")?
        .o_mut()?
        .at_mut("name")?
        .o_mut()
}

pub fn build_ui(data: &[u8]) -> Result<Vec<u8>> {
    let mut doc = Pkg::parse(data)?;
    if doc.to_bytes()? != data {
        return Err(err(format!("{UI} fragment {UI_FRAGMENT} does not round-trip")));
    }
    let tpl = template_mut(doc.root.o_mut()?, TEMPLATE)?;
    let kids = tpl.slot(CHILDREN)?;
    if kids
        .iter()
        .any(|k| k.is_node() && k.name().starts_with("addon_select_"))
    {
        return Err(err(format!("{TEMPLATE} already has addon boxes")));
    }
    let at = kids
        .iter()
        .position(|k| k.is_node() && k.name() == STOCK_BUTTON)
        .ok_or_else(|| err(format!("{TEMPLATE} has no {STOCK_BUTTON}")))?;
    let stock = kids[at].clone();
    let stock_command = press_command(&mut stock.clone())?
        .get("value")
        .and_then(Node::as_val)
        .and_then(Val::as_str)
        .map(str::to_owned);
    if stock_command.as_deref() != Some("button_1") {
        return Err(err(format!(
            "{STOCK_BUTTON} raises {stock_command:?}, not button_1"
        )));
    }
    let mut added = Vec::new();
    for k in 0..SLOTS {
        let name = slot(k);
        let mut button = clone_node(&stock, TEMPLATE, &name)?;
        set_layout(&mut button, &[&name])?;
        set_dbentry(&mut button, "dataContext", &entry(k))?;
        set_prop(
            &mut button,
            "visible",
            prop(boolv(false), Some(&format!("{}.enabled", entry(k)))),
        )?;
        set_str(press_command(&mut button)?, "value", &command(k))?;
        added.push(button);
    }
    tpl.slot_mut(CHILDREN)?.splice(at + 1..at + 1, added);
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

pub fn build_scene(data: &[u8]) -> Result<Vec<u8>> {
    let mut doc = Pkg::parse(data)?;
    let entry = idf::scenes_mut(&mut doc)?
        .first_mut()
        .ok_or_else(|| err(format!("{SCENE} has no scenes")))?;
    if idf::scene_name(entry) != Some(idf::scene_id(SCENE)) {
        return Err(err(format!("not {SCENE}")));
    }
    let root = entry.o_mut()?.at_mut("scene")?;
    if idf::find(root, &slot(0)).is_some() {
        return Err(err(format!("{SCENE} already has {}", slot(0))));
    }
    let kids = idf::children_mut(root)?;
    let mut base = None;
    for (column, name) in STOCK_ELEMENTS.iter().enumerate() {
        let at = kids
            .iter()
            .position(|k| elem_name(k) == *name)
            .ok_or_else(|| err(format!("{SCENE} has no {name}")))?;
        place(&mut kids[at], COLUMN_X[column], ROW_Y[0], Some(SCALE), None)?;
        if *name == "content" {
            base = Some(idf::detached(&kids[at]));
        }
    }
    let base = base.ok_or_else(|| err(format!("{SCENE} has no content")))?;
    for (k, guid) in GUIDS.iter().enumerate() {
        let name = slot(k);
        let mut tile = base.clone();
        tile.o_mut()?.set("name", s(&name));
        idf::set_param(&mut tile, "guid", Val::str(*guid))?;
        idf::recompute_ids(&mut tile, &name)?;
        place(&mut tile, COLUMN_X[k], ROW_Y[1], Some(SCALE), None)?;
        kids.push(tile);
    }
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
