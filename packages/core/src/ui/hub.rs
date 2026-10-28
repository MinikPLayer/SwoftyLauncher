use std::collections::{BTreeSet, HashMap};

use swofty_formats::erp::{ErpFile, Resource};
use swofty_formats::pkg::{Node, Pkg, Val};

use super::node::*;
use crate::Result;
use crate::manifest::HubTab;

pub const ARCHIVES: [&str; 2] = ["flow_career_hub.erp", "flow_team_hq.erp"];
/// The laptop's OVERVIEW page file; the hub tab templates join its templates.
pub const UI: &str = "eaid://ui_package/screens/laptop_overview.ui";
pub const UI_FRAGMENT: usize = 0;
/// The stock laptop page whose root (fade-in and button prompts) each hub tab starts from. Its own parts are
/// dropped.
pub const BASE: &str = "tab_content_emails_panel";
/// The archive holding the settings screens the page is made from.
pub const OPTIONS: &str = "flow_options.erp";
/// The archives every flow loads, whose resources a copied scene may use without a copy.
pub const SHARED: [&str; 2] = ["persistent.erp", "flow_persistent.erp"];
/// The settings templates: rows, the description panel and its text set, and the category box.
pub const SETTINGS_UI: &str = "eaid://ui_package/screens/settings_container.ui";
pub const SETTINGS_FRAGMENT: usize = 1;
const ROW: &str = "settings_edit_toggle";
const ABOUT: &str = "settings_edit_description_category";
const PANEL: &str = "settings_edit_description_panel";
const SET: &str = "settings_edit_description_set";
/// The settings rows page's scene: the row list on the left (`list_spawner`) and the description panel on the
/// right (`info_spawner`).
pub const PAGE_SCENE: &str = "eaid://ui_package/art/screens/options/options_edit_screen.idf";
/// The scenes the page shows, whose resources [`copy_plan`] copies from [`OPTIONS`] into an archive that lacks
/// them.
pub const SCENES: [&str; 5] = [
    PAGE_SCENE,
    "eaid://ui_package/art/components/options/options_spawned/o_toggle.idf",
    "eaid://ui_package/art/components/options/options_spawned/o_description_panel.idf",
    "eaid://ui_package/art/components/options/options_spawned/o_description_set.idf",
    "eaid://ui_package/art/components/options/options_spawned/o_description_panel_category.idf",
];
/// Parts of [`PAGE_SCENE`] the page hides: the settings screen title, the category filter and the scroll bar.
pub const HIDDEN_PARTS: [(&str, &str); 3] = [
    ("hide_title", "screen_title"),
    ("hide_filter", "filter"),
    ("hide_scroll_bar", "scroll_bar"),
];
const KEEP: [&str; 2] = ["fade_on", "tool_tip_interface"];
const ENTRIES: usize = 6;
const TOOLTIPS: &str = "tool_tip_interface";
const TOOLTIP_SOURCE: &str = "exit_laptop";
const HIDDEN_TOOLTIPS: [&str; 2] = ["tutorial_tooltip", "extra2"];
const SCHEME: &[u8] = b"eaid://";
const PACKAGE: &str = "eaid://ui_package/";

pub struct Row<'a> {
    pub id: &'a str,
    pub label: &'a str,
    pub description: &'a str,
}

pub struct Page<'a> {
    pub addon: &'a str,
    pub tab: &'a HubTab,
    pub rows: Vec<Row<'a>>,
}

pub fn row(i: usize) -> String {
    format!("row_{i}")
}

fn templates(p: &mut Pkg) -> Result<&mut Vec<Node>> {
    root_obj(p)?.at_mut("Templates")?.a_mut()
}

fn bind_text(n: &mut Node, path: &str) -> Result<()> {
    let p = n.props_mut()?;
    p.set("text", obj(vec![("value", s("")), ("dbentry", s(path))]));
    p.set("localise", prop(boolv(false), None));
    Ok(())
}

fn drop_contexts(n: &mut Node) -> Result<()> {
    n.props_mut()?.drop_key("dataContext");
    for c in n.slot_mut(CHILDREN)? {
        if c.is_node() {
            drop_contexts(c)?;
        }
    }
    Ok(())
}

fn hide(n: &mut Node) -> Result<()> {
    set_bool(n, "visible", false)
}

fn adjust_tooltip(t: &mut Node, name: &str) -> Result<()> {
    let tips = t.child_mut(TOOLTIPS)?;
    for c in tips.slot_mut(CHILDREN)? {
        if c.is_node() && HIDDEN_TOOLTIPS.contains(&c.name()) {
            hide(c)?;
        }
    }
    let mut tip = tips.child(TOOLTIP_SOURCE)?.clone();
    rename_refs(
        &mut tip,
        &format!("{name}.{TOOLTIPS}.{TOOLTIP_SOURCE}"),
        &format!("{name}.{TOOLTIPS}.adjust"),
    );
    rename(&mut tip, "adjust")?;
    set_prop_value(&mut tip, "tooltipsName", Val::str(super::pages::ADJUST_TOOLTIP))?;
    set_bool(&mut tip, "visible", true)?;
    set_prop_value(tip.child_mut("text")?, "localise", Val::bool(false))?;
    tips.slot_mut(CHILDREN)?.push(tip);
    Ok(())
}

fn on_scene(t: &mut Node, scene: &str) -> Result<()> {
    let path = scene.strip_suffix(".idf").unwrap_or(scene);
    let comp = t
        .slot_mut(5)?
        .first_mut()
        .ok_or_else(|| err(format!("{BASE} has no scene")))?
        .o_mut()?;
    comp.set(
        "componentId",
        Node::Val(Val::raw(*b"ui64", component_id(path).to_le_bytes())),
    );
    comp.set("debugName", s(path));
    Ok(())
}

fn template<'a>(settings: &'a HashMap<String, Node>, name: &str) -> Result<&'a Node> {
    settings
        .get(name)
        .ok_or_else(|| err(format!("the settings templates have no {name}")))
}

fn description_events(name: &str, r: &Row) -> Node {
    event(
        "focused",
        vec![
            sig_with(
                &format!("{name}.description.set.title"),
                "setText",
                "focused",
                &[("text", r.label.to_uppercase().as_str().into())],
            ),
            sig_with(
                &format!("{name}.description.set.body"),
                "setText",
                "focused",
                &[("text", r.description.into())],
            ),
        ],
    )
}

fn page(base: &Node, settings: &HashMap<String, Node>, p: &Page) -> Result<Node> {
    let name = p.tab.template();
    let vm = format!("#{}", p.tab.view_model());
    if p.rows.is_empty() || p.rows.len() > ENTRIES {
        return Err(err(format!(
            "hub tab {}: {} rows, not 1 to {ENTRIES}",
            p.tab.name,
            p.rows.len()
        )));
    }
    let mut t = base.clone();
    rename_refs(&mut t, BASE, &name);
    rename(&mut t, &name)?;
    strip_data(&mut t)?;
    drop_contexts(&mut t)?;
    set_prop(&mut t, "allowVMBinding", prop(boolv(true), None))?;
    on_scene(&mut t, PAGE_SCENE)?;
    t.slot_mut(CHILDREN)?
        .retain(|c| c.is_node() && KEEP.contains(&c.name()));
    adjust_tooltip(&mut t, &name)?;

    let mut kids: Vec<Node> = HIDDEN_PARTS
        .iter()
        .map(|(widget, part)| hidden(widget, &[part]))
        .collect();
    let mut about = instance(template(settings, ABOUT)?, &name, "about", &[], true)?;
    bind_text(about.child_mut("title")?, &format!("{vm}.title"))?;
    for part in ["body", "icon"] {
        hide(about.child_mut(part)?)?;
    }
    kids.push(about);
    let first = &p.rows[0];
    let mut panel = instance(template(settings, PANEL)?, &name, "description", &[], true)?;
    let title = first.label.to_uppercase();
    let set = instance(
        template(settings, SET)?,
        &format!("{name}.description"),
        "set",
        &[("title", &title), ("body", first.description)],
        true,
    )?;
    panel.slot_mut(CHILDREN)?.push(set);
    kids.push(panel);

    let mut rows_at = Vec::new();
    for (i, r) in p.rows.iter().enumerate() {
        let mut row_node = instance(template(settings, ROW)?, &name, &row(i), &[], true)?;
        let tg = row_node.child_mut("toggle")?;
        super::pages::as_group(tg)?;
        hide(tg.child_mut("indicator")?)?;
        bind_text(tg.child_mut("label")?, &format!("{vm}.o{i}.label"))?;
        bind_text(tg.child_mut("value")?, &format!("{vm}.o{i}.value"))?;
        for a in ["left_arrow", "right_arrow"] {
            set_bool(tg.child_mut(a)?, "enabled", true)?;
        }
        let command = |verb: &str| format!("swofty_{verb}:{}:{}", p.addon, r.id);
        let (next, prev) = (command("next"), command("prev"));
        add_events(
            &mut row_node,
            vec![
                description_events(&name, r),
                event("navRight", vec![raise_cmd(&name, "navRight", &next, &next)]),
                event("navLeft", vec![raise_cmd(&name, "navLeft", &prev, &prev)]),
            ],
        )?;
        rows_at.push(kids.len());
        kids.push(row_node);
    }
    chain_focus(&name, &mut kids, &rows_at)?;
    t.slot_mut(CHILDREN)?.extend(kids);
    let first_row = format!("{name}.{}", row(0));
    add_events(
        &mut t,
        vec![
            event("shown", vec![sig(&first_row, "focus", "shown")]),
            event("enabled", vec![sig(&first_row, "focus", "enabled")]),
        ],
    )?;
    fix_ids(&mut t, None);
    Ok(t)
}

/// The templates of the settings page file (fragment [`SETTINGS_FRAGMENT`] of [`SETTINGS_UI`]).
pub fn settings_templates(settings: &[u8]) -> Result<HashMap<String, Node>> {
    let p = Pkg::parse(settings)?;
    Ok(p.root
        .o()?
        .at("Templates")?
        .a()?
        .iter()
        .filter(|t| t.is_node())
        .map(|t| (t.name().to_owned(), t.clone()))
        .collect())
}

/// Adds each page's template to the laptop page file `data`: on [`PAGE_SCENE`], a settings row per option (the
/// label, and the value between arrows), the category box with the page title, and the description panel with the
/// focused option's label and description, as on the game's settings pages. The root's fade-in and button prompts
/// come from [`BASE`].
pub fn build_ui(data: &[u8], settings: &[u8], pages: &[Page]) -> Result<Vec<u8>> {
    let settings = settings_templates(settings)?;
    edit(data, |pkg| {
        let list = templates(pkg)?;
        let base = list
            .iter()
            .find(|t| t.is_node() && t.name() == BASE)
            .cloned()
            .ok_or_else(|| err(format!("the laptop page file has no {BASE}")))?;
        for p in pages {
            let name = p.tab.template();
            if list.iter().any(|t| t.is_node() && t.name() == name) {
                return Err(err(format!("the laptop page file has {name} already")));
            }
            list.push(page(&base, &settings, p)?);
        }
        Ok(())
    })
}

/// Every `eaid://` reference in `data`.
pub fn references(data: &[u8]) -> BTreeSet<String> {
    let mut out = BTreeSet::new();
    let mut at = 0;
    while let Some(i) = data[at..].windows(SCHEME.len()).position(|w| w == SCHEME) {
        let start = at + i;
        let end = data[start..]
            .iter()
            .position(|b| !(0x21..0x7f).contains(b))
            .map_or(data.len(), |n| start + n);
        out.insert(String::from_utf8_lossy(&data[start..end]).into_owned());
        at = end;
    }
    out
}

fn family<'a>(names: &'a BTreeSet<String>, reference: &str) -> Vec<&'a String> {
    let emb = reference.strip_suffix(".idf").map(|base| format!("{base}.emb"));
    names
        .iter()
        .filter(|n| n.starts_with(reference) || emb.as_ref().is_some_and(|e| n.starts_with(e.as_str())))
        .collect()
}

/// Whether some name in `names` is `reference` or starts with it (a mesh reference names its parts' prefix).
pub fn resolves(names: &BTreeSet<String>, reference: &str) -> bool {
    names
        .range(reference.to_owned()..)
        .next()
        .is_some_and(|n| n.starts_with(reference))
}

/// The copies [`copy_plan`] makes.
pub struct Plan {
    /// The resources of [`OPTIONS`] to add, in the order found.
    pub add: Vec<Resource>,
    /// References that resolve nowhere: not in the archive and not in [`OPTIONS`] either, as in the stock settings
    /// screens.
    pub stock_unresolved: BTreeSet<String>,
}

/// The resources of `from` that the scenes `roots` need and an archive whose resources (with the shared archives')
/// are `have` lacks: each scene's own resources (layout, animations, meshes and material binding), then whatever
/// they reference, such as other scenes, materials and textures, followed through `from`. Fails when a copied
/// resource references something `from` has but the archive would still lack.
pub fn copy_plan(from: &ErpFile, have: &BTreeSet<String>, roots: &[&str]) -> Result<Plan> {
    let names: BTreeSet<String> = from.erp.resources.iter().map(|r| r.name.clone()).collect();
    let mut seen = BTreeSet::new();
    let mut add: Vec<Resource> = Vec::new();
    let mut added = BTreeSet::new();
    let mut refs: Vec<(String, String)> = Vec::new();
    let mut todo: Vec<String> = roots.iter().rev().map(|r| (*r).to_owned()).collect();
    while let Some(reference) = todo.pop() {
        if !reference.starts_with(PACKAGE) || !seen.insert(reference.clone()) {
            continue;
        }
        for n in family(&names, &reference) {
            if have.contains(n) || added.contains(n) {
                continue;
            }
            let res = from.load(from.erp.find(n)?)?;
            for f in &res.fragments {
                for r in references(&f.data()?) {
                    refs.push((n.clone(), r.clone()));
                    todo.push(r);
                }
            }
            added.insert(n.clone());
            add.push(res);
        }
    }
    let mut stock_unresolved = BTreeSet::new();
    let mut missing = Vec::new();
    for (user, r) in refs {
        if !r.starts_with(PACKAGE) || resolves(have, &r) || resolves(&added, &r) {
            continue;
        }
        if resolves(&names, &r) {
            missing.push(format!("{user} uses {r}"));
        } else {
            stock_unresolved.insert(r);
        }
    }
    if !missing.is_empty() {
        return Err(err(format!(
            "the copied settings scenes use resources the archive lacks: {}",
            missing.join("; ")
        )));
    }
    Ok(Plan {
        add,
        stock_unresolved,
    })
}

pub fn has_base(data: &[u8]) -> Result<bool> {
    let p = Pkg::parse(data)?;
    Ok(p.root
        .o()?
        .at("Templates")?
        .a()?
        .iter()
        .any(|t| t.is_node() && t.name() == BASE))
}
