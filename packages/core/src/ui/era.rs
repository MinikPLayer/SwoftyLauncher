use swofty_formats::hash::djb2;
use swofty_formats::pkg::{Node, Obj, Pkg, Val};

use super::check;
use super::idf::{self, elem_name};
use super::node::*;
use crate::Result;

pub const ARCHIVES: [&str; 2] = ["flow_f1_world.erp", "screens/f1_world_activity_container.erp"];
pub const UI: &str = "eaid://ui_package/screens/f1_world_activity_container.ui";
pub const UI_FRAGMENT: usize = 1;
pub const SCENE: &str = "eaid://ui_package/art/screens/vehicle_type/vehicle_type.idf";
pub const TITLE_SCENE: &str = "eaid://ui_package/art/screens/modular_menu/mm_tt_select.idf";
pub const FADE_SOURCE: &str =
    "eaid://ui_package/art/textures/key_textures/gradients/grad_mask_left_clamped.tif";
pub const FADE: &str = "eaid://ui_package/art/textures/swofty/era_rows/row_fade.tif";
pub const ROW_FADE: bool = false;
pub const SRV_KIND: &str = "GfxSRVResource";
pub const SURFACE_KIND: &str = "GfxSurfaceRes";
pub const ADDON_TILES: &str = "addon_tiles";
pub const ADDON_SELECTION: &str = "selected_addon_tile";
pub const ADDON_PRESSED: &str = "addon_select_pressed";
pub const ADDON_VISIBLE: &str = "addon_tiles_visible";
pub const PIXELS: &[u8] = include_bytes!("row_fade.bc7");

const TEMPLATE: &str = "dynamic_era_select";
const TITLE_TEMPLATE: &str = "time_trial_select";
const TOP: &str = "tiles";
const GATE: &str = "addon_row_has_tiles";
const NAV_SOUND: &str = "trigger_nav_on_up_down";
const STOCK_ROW: &str = "play_categories_row";
const ADDON_ROW: &str = "addon_categories_row";
const SCALE: f64 = 0.5;
const ROW_X: f64 = -8.0;
const ROW_Y: [f64; 2] = [1.6, -2.45];
const FADE_SPAN: (f64, f64) = (-8.6, 9.8);
pub const FADE_X: (f64, f64) = (-8.6, -8.0);
pub const FADE_W: usize = 512;
pub const FADE_H: usize = 40;
const LABELS: [(&str, &str, f64); 2] = [
    ("label_builtin", "Built In", ROW_Y[0]),
    ("label_addons", "Addons", ROW_Y[1]),
];
const LABEL_X: f64 = -8.6;
const LABEL_DY: f64 = -0.9;
const LABEL_ROTATION: [f64; 4] = [0.0, 0.0, -0.70710677, -0.70710677];
const GUIDS: [(&str, &str); 3] = [
    (ADDON_ROW, "5b0f6c1e-3d2a-4c8e-9a51-7e4b2c9d0a11"),
    ("label_builtin", "8c2d4e6f-1a3b-4c5d-8e7f-9a0b1c2d3e41"),
    ("label_addons", "3e5f7a9b-2c4d-4e6f-8a1b-3c5d7e9f0b52"),
];

fn guid(name: &str) -> &'static str {
    GUIDS
        .iter()
        .find(|(n, _)| *n == name)
        .map(|(_, g)| *g)
        .expect("known guid")
}

pub fn fade_alpha(px: usize) -> u8 {
    let (x0, x1) = FADE_SPAN;
    let (f0, f1) = FADE_X;
    let x = x0 + (px as f64 + 0.5) / FADE_W as f64 * (x1 - x0);
    let t = ((x - f0) / (f1 - f0)).clamp(0.0, 1.0);
    (255.0 * t * t * (3.0 - 2.0 * t)).round() as u8
}

pub(super) fn template_mut<'a>(root: &'a mut Obj, name: &str) -> Result<&'a mut Node> {
    root.at_mut("Templates")?
        .a_mut()?
        .iter_mut()
        .find(|t| t.is_node() && t.name() == name)
        .ok_or_else(|| err(format!("template {name} not found")))
}

pub(super) fn named<'a>(n: &'a mut Node, slot: usize, key: &str, name: &str) -> Result<&'a mut Obj> {
    let owner = n.name().to_owned();
    n.slot_mut(slot)?
        .iter_mut()
        .filter_map(Node::as_obj_mut)
        .find(|e| e.get(key).and_then(Node::as_val).and_then(Val::as_str) == Some(name))
        .ok_or_else(|| err(format!("{owner}: no {name} {key}")))
}

pub(super) fn event_mut<'a>(n: &'a mut Node, name: &str) -> Result<&'a mut Obj> {
    named(n, EVENTS, "eventName", name)
}

pub(super) fn set_layout(n: &mut Node, path: &[&str]) -> Result<()> {
    let group = n
        .slot_mut(LAYOUT)?
        .first_mut()
        .ok_or_else(|| err("no layout group"))?
        .a_mut()?;
    let like = group.first().ok_or_else(|| err("empty layout group"))?.clone();
    let mut out = Vec::new();
    for slot in path {
        let mut b = like.clone();
        let o = b.o_mut()?;
        set_str(o, "debugName", slot)?;
        o.at_mut("nodeId")?.v_mut()?.set_u32(djb2(slot));
        out.push(b);
    }
    *group = out;
    Ok(())
}

fn input_on_active_part(spawner: &mut Node) -> Result<()> {
    let props = spawner.props_mut()?;
    let at = props
        .items
        .iter()
        .position(|(k, _)| k.as_str() == Some("consumeStrategy"))
        .ok_or_else(|| err("tile_spawner has no consumeStrategy"))?;
    let (mut key, mut value) = props.items[at].clone();
    key.set_str("InputOnActPrt");
    for (_, v) in &mut value.o_mut()?.items {
        *v = boolv(true);
    }
    props.items[at] = (key, value);
    Ok(())
}

pub(super) fn set_dbentry(n: &mut Node, key: &str, entry: &str) -> Result<()> {
    set_str(n.props_mut()?.at_mut(key)?.o_mut()?, "dbentry", entry)
}

pub fn has_rows(data: &[u8]) -> Result<bool> {
    let mut doc = Pkg::parse(data)?;
    let tpl = template_mut(doc.root.o_mut()?, TEMPLATE)?;
    Ok(tpl
        .slot(CHILDREN)?
        .iter()
        .any(|k| k.is_node() && (k.name() == ADDON_TILES || LABELS.iter().any(|(n, _, _)| *n == k.name()))))
}

pub fn taken(ui: &[u8], scene: &[u8]) -> Result<bool> {
    Ok(has_rows(ui)? || idf::find(first_root(&Pkg::parse(scene)?)?, ADDON_ROW).is_some())
}

fn path(row: &str) -> String {
    format!("{TEMPLATE}.{row}")
}

fn row_chain(row: &mut Node, other: &str, nav: [Vec<Node>; 4]) -> Result<()> {
    let name = row.name().to_owned();
    let spawner = format!("{}.tile_spawner", path(&name));
    let pins = ["navUp", "navDown", "navLeft", "navRight"];
    let failed = [
        "navigate_up_failed",
        "navigate_down_failed",
        "navigate_left_failed",
        "navigate_right_failed",
    ];
    let events = row.child_mut("tile_spawner")?.slot_mut(EVENTS)?;
    for (from, pin) in failed.into_iter().zip(pins) {
        events.push(event(from, vec![sig(&path(&name), pin, from)]));
    }
    row.slot_mut(COMMANDS)?.extend(
        [
            ("focus", "focus"),
            ("block_input", "inputDisallow"),
            ("unblock_input", "inputAllow"),
        ]
        .map(|(cmd, pin)| command(cmd, vec![sig(&spawner, pin, cmd)])),
    );
    let events = row.slot_mut(EVENTS)?;
    events.push(event("focused", vec![sig(&path(other), "unfocus", "focused")]));
    for (pin, signals) in pins.into_iter().zip(nav) {
        events.push(event(pin, signals));
    }
    Ok(())
}

pub fn build_ui(data: &[u8]) -> Result<Vec<u8>> {
    let mut doc = Pkg::parse(data)?;
    if doc.to_bytes()? != data {
        return Err(err(format!("{UI} fragment {UI_FRAGMENT} does not round-trip")));
    }
    if has_rows(data)? {
        return Err(err(format!("{TEMPLATE} already has the addon row")));
    }
    let root = doc.root.o_mut()?;
    let title = template_mut(root, TITLE_TEMPLATE)?.child("screen_title")?.clone();
    let tpl = template_mut(root, TEMPLATE)?;
    let at = tpl
        .slot(CHILDREN)?
        .iter()
        .position(|k| k.is_node() && k.name() == TOP)
        .ok_or_else(|| err(format!("{TEMPLATE} has no {TOP}")))?;
    let mut tiles = tpl.slot(CHILDREN)?[at].clone();
    input_on_active_part(tiles.child_mut("tile_spawner")?)?;
    let mut addon = clone_node(&tiles, TEMPLATE, ADDON_TILES)?;
    set_layout(&mut addon, &[ADDON_ROW])?;
    let spawner = addon.child_mut("tile_spawner")?;
    set_dbentry(spawner, "dataContext", ADDON_TILES)?;
    set_dbentry(spawner, "selection", ADDON_SELECTION)?;
    event_mut(spawner, "spawned")?
        .at_mut("signals")?
        .a_mut()?
        .retain(|s| {
            s.as_obj()
                .and_then(|o| o.get("to_pin_name"))
                .and_then(Node::as_val)
                .and_then(Val::as_str)
                != Some("focus_and_reset")
        });
    let press = event_mut(spawner, "select_pressed")?
        .at_mut("signals")?
        .a_mut()?
        .first_mut()
        .ok_or_else(|| err("select_pressed sends nothing"))?;
    let name = press
        .o_mut()?
        .at_mut("attributes")?
        .o_mut()?
        .at_mut("command")?
        .o_mut()?
        .at_mut("name")?
        .o_mut()?;
    set_str(name, "value", ADDON_PRESSED)?;
    let sound = |from: &str| sig(&path(NAV_SOUND), "navigate", from);
    row_chain(
        &mut tiles,
        ADDON_TILES,
        [vec![], vec![sig(&path(GATE), "query", "navDown")], vec![], vec![]],
    )?;
    row_chain(
        &mut addon,
        TOP,
        [
            vec![sig(&path(TOP), "focus", "navUp"), sound("navUp")],
            vec![],
            vec![],
            vec![],
        ],
    )?;
    for (cmd, pins) in [
        ("onShowCompleted", &["trigger", "unblock_input"][..]),
        ("onTabDeselectBegin", &["detach", "block_input"]),
    ] {
        let signals = named(tpl, COMMANDS, "commandName", cmd)?
            .at_mut("signals")?
            .a_mut()?;
        signals.push(sig(&path(ADDON_TILES), pins[0], cmd));
        for pin in &pins[1..] {
            signals.extend([TOP, ADDON_TILES].map(|row| sig(&path(row), pin, cmd)));
        }
    }
    for cmd in ["onTabSelected", "onTabActivated"] {
        let signals = [TOP, ADDON_TILES].map(|row| sig(&path(row), "unblock_input", cmd));
        tpl.slot_mut(COMMANDS)?.push(command(cmd, signals.to_vec()));
    }
    let kids = tpl.slot_mut(CHILDREN)?;
    kids[at] = tiles;
    kids.insert(at + 1, addon);
    for (name, text, _) in LABELS {
        let mut label = title.clone();
        rename_refs(&mut label, &format!("{TITLE_TEMPLATE}.screen_title"), &path(name));
        rename(&mut label, name)?;
        set_layout(&mut label, &[name])?;
        literal_text(label.child_mut("label_title")?, text)?;
        fix_ids(&mut label, Some(TEMPLATE));
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
            vec![sig(&path(ADDON_TILES), "focus", "yes"), sound("yes")],
        )],
        &[],
        &[],
    );
    let mut nav = node(NAV_SOUND, "UIAM", vec![], vec![], &[], &[]);
    for n in [&mut gate, &mut nav] {
        fix_ids(n, Some(TEMPLATE));
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

pub(super) fn place(
    e: &mut Node,
    x: f64,
    y: f64,
    scale: Option<f64>,
    rotation: Option<[f64; 4]>,
) -> Result<()> {
    let o = e.o_mut()?;
    o.set("position", Node::Val(idf::vec3([x, y, 0.0, 0.0])));
    if let Some(s) = scale {
        o.set("scale", Node::Val(idf::vec3([s, s, 1.0, 0.0])));
    }
    if let Some(r) = rotation {
        o.set("rotation", Node::Val(idf::quat(r)));
    }
    Ok(())
}

pub(super) fn first_root(p: &Pkg) -> Result<&Node> {
    idf::scenes(p)?
        .first()
        .ok_or_else(|| err("a scene file without scenes"))?
        .o()?
        .at("scene")
}

pub fn build_scene(data: &[u8], title: &[u8]) -> Result<Vec<u8>> {
    let mut doc = Pkg::parse(data)?;
    let title_doc = Pkg::parse(title)?;
    let title = idf::find(first_root(&title_doc)?, "screen_title").ok_or_else(|| err("no screen_title"))?;
    let entry = idf::scenes_mut(&mut doc)?
        .first_mut()
        .ok_or_else(|| err(format!("{SCENE} has no scenes")))?;
    if idf::scene_name(entry) != Some(idf::scene_id(SCENE)) {
        return Err(err(format!("not {SCENE}")));
    }
    let root = entry.o_mut()?.at_mut("scene")?;
    if idf::find(root, ADDON_ROW).is_some() {
        return Err(err(format!("{SCENE} already has {ADDON_ROW}")));
    }
    let kids = idf::children_mut(root)?;
    let at = kids
        .iter()
        .position(|k| elem_name(k) == STOCK_ROW)
        .ok_or_else(|| err(format!("{SCENE} has no {STOCK_ROW}")))?;
    let mut addon = idf::detached(&kids[at]);
    addon.o_mut()?.set("name", s(ADDON_ROW));
    idf::set_param(&mut addon, "guid", Val::str(guid(ADDON_ROW)))?;
    idf::recompute_ids(&mut addon, ADDON_ROW)?;
    place(&mut kids[at], ROW_X, ROW_Y[0], Some(SCALE), None)?;
    place(&mut addon, ROW_X, ROW_Y[1], Some(SCALE), None)?;
    kids.push(addon);
    for (name, _, y) in LABELS {
        let mut label = idf::detached(title);
        label.o_mut()?.set("name", s(name));
        idf::set_param(&mut label, "guid", Val::str(guid(name)))?;
        idf::recompute_ids(&mut label, name)?;
        place(&mut label, LABEL_X, y + LABEL_DY, None, Some(LABEL_ROTATION))?;
        kids.push(label);
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

pub fn fade(srv: &[u8], header: &[u8], fade: &str) -> Result<Vec<Vec<u8>>> {
    let old = format!("{FADE_SOURCE}.image");
    let new = format!("{fade}.image");
    let at = srv
        .windows(old.len())
        .position(|w| w == old.as_bytes())
        .filter(|&i| i + old.len() + 1 == srv.len() && srv[srv.len() - 1] == 0)
        .ok_or_else(|| err(format!("{FADE_SOURCE}: unexpected shader resource view")))?;
    let mut out = srv[..at].to_vec();
    out.extend_from_slice(new.as_bytes());
    out.push(0);
    let dim = |o: usize| {
        header
            .get(o..o + 4)
            .map(|b| u32::from_le_bytes(b.try_into().expect("4 bytes")))
    };
    let (w, h) = (dim(12), dim(16));
    if (w, h) != (Some(FADE_W as u32), Some(FADE_H as u32)) || PIXELS.len() != FADE_W * FADE_H {
        return Err(err(format!(
            "{FADE_SOURCE}: {w:?}x{h:?}, the fade mask is {FADE_W}x{FADE_H}"
        )));
    }
    Ok(vec![out, header.to_vec(), PIXELS.to_vec()])
}

#[cfg(test)]
mod tests {
    use super::*;
    use swofty_formats::texture::{Codec, decode_pixels};

    #[test]
    fn the_fade_mask_is_white_and_fades_in_behind_the_labels() {
        let rgba = decode_pixels(Codec::Bc7, FADE_W as u32, FADE_H as u32, PIXELS).unwrap();
        assert_eq!(rgba.len(), FADE_W * FADE_H * 4);
        let mut worst = 0;
        for (i, px) in rgba.chunks(4).enumerate() {
            assert_eq!(&px[..3], [255, 255, 255], "pixel {i}");
            worst = worst.max(px[3].abs_diff(fade_alpha(i % FADE_W)));
        }
        assert!(worst <= 8, "{worst} levels off");
        assert_eq!((fade_alpha(0), fade_alpha(FADE_W / 2)), (1, 255));
    }

    #[test]
    fn the_fade_view_names_its_own_surface() {
        let mut header = vec![0; 40];
        header[12..16].copy_from_slice(&(FADE_W as u32).to_le_bytes());
        header[16..20].copy_from_slice(&(FADE_H as u32).to_le_bytes());
        let mut srv = b"head".to_vec();
        srv.extend_from_slice(format!("{FADE_SOURCE}.image\0").as_bytes());
        let out = fade(&srv, &header, FADE).unwrap();
        let mut want = b"head".to_vec();
        want.extend_from_slice(format!("{FADE}.image\0").as_bytes());
        assert_eq!(out, [want, header.clone(), PIXELS.to_vec()]);
        assert!(fade(b"head\0", &header, FADE).is_err());
        assert!(fade(&srv, &[0; 40], FADE).is_err());
    }
}
