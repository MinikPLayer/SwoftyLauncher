use swofty_formats::bxml::{Document, Element, Kind};
use swofty_formats::pkg::{Node, Val};

use super::node::*;
use crate::Result;

pub const PANEL: &str = "swofty_addons_panel";
pub const ENABLED: &str = "#ui_menu_data.swofty.mfd_enabled";
pub const TEMPLATES: &str = "eaid://ui_package/templates/hud_dynamic_templates.ui";
pub const METADATA: &str = "eaid://ui_package/mfd/mfd.ui_mfd_metadata";
pub const TEMPLATE_ERPS: &[&str] = &["flow_race.erp", "flow_options.erp"];
const TEMPLATE: &str = "hud_mfd_dynamic";
const ATTACHER: &str = "mfd.attacher";
const STOCK_PANEL: &str = "car_settings_panel";
const STOCK_ROW: &str = "settings_list.brake_bias";
const GROUP: &str = "osd_main";
const NEXT_TO: &str = "mods_panel";

pub struct Row {
    pub label: String,
    pub value: Option<String>,
}

pub fn value_path(k: usize) -> String {
    format!("#ui_menu_data.swofty.m{k}")
}

fn template_mut<'p>(list: &'p mut [Node], name: &str) -> Result<&'p mut Node> {
    list.iter_mut()
        .find(|t| t.is_node() && t.name() == name)
        .ok_or_else(|| err(format!("the HUD templates have no {name}")))
}

fn row(stock: &Node, parent: &str, k: usize, r: &Row) -> Result<Node> {
    let name = format!("r{k}");
    let mut n = stock.clone();
    let p = n.props_mut()?;
    for key in ["enabled", "dataContext", "interactWhenDis", "visible"] {
        p.drop_key(key);
    }
    p.set("type", u32v(fourcc("UIGP")));
    p.set("visible", prop(boolv(true), None));
    *n.slot_mut(BEHAVIOURS)? = behaviours(GROUP_BEHAVIOURS)
        .as_arr()
        .cloned()
        .map(|a| a.items)
        .unwrap_or_default();
    *n.slot_mut(COMMANDS)? = vec![];
    *n.slot_mut(EVENTS)? = vec![];
    for c in n.slot_mut(5)? {
        set_str(c.o_mut()?, "name", &name)?;
    }
    let kids: Vec<Node> = stock
        .slot(CHILDREN)?
        .iter()
        .filter(|c| c.is_node() && matches!(c.name(), "name" | "value"))
        .cloned()
        .collect();
    let mut out = Vec::new();
    for mut c in kids {
        if c.name() == "name" {
            literal_label(&mut c, &r.label)?;
        } else {
            c.props_mut()?.set("text", prop(s(""), r.value.as_deref()));
            set_prop_value(&mut c, "localise", Val::bool(false))?;
        }
        *c.slot_mut(EVENTS)? = vec![];
        out.push(c);
    }
    *n.slot_mut(CHILDREN)? = out;
    rename(&mut n, &name)?;
    fix_ids(&mut n, Some(parent));
    Ok(n)
}

pub fn templates(data: &[u8], rows: &[Row]) -> Result<Vec<u8>> {
    edit(data, |p| {
        let list = root_obj(p)?.at_mut("Templates")?.a_mut()?;
        let attacher = template_mut(list, TEMPLATE)?.find_mut(ATTACHER)?;
        if attacher.child(PANEL).is_ok() {
            return Ok(());
        }
        let parent = format!("{TEMPLATE}.{ATTACHER}");
        let stock = attacher.child(STOCK_PANEL)?.clone();
        let stock_row = stock.clone().find_mut(STOCK_ROW)?.clone();
        let mut panel = clone_node(&stock, &parent, PANEL)?;
        panel.props_mut()?.drop_key("allowVMBinding");
        let panel_path = format!("{parent}.{PANEL}");
        let mut kids: Vec<Node> = panel
            .slot(CHILDREN)?
            .iter()
            .filter(|c| c.is_node() && c.type_code() == "UIMO")
            .cloned()
            .collect();
        for (k, r) in rows.iter().enumerate() {
            kids.push(row(&stock_row, &panel_path, k, r)?);
        }
        *panel.slot_mut(CHILDREN)? = kids;
        for c in panel.slot_mut(5)? {
            set_str(c.o_mut()?, "name", PANEL)?;
        }
        fix_ids(&mut panel, Some(&parent));
        let mut paths = Vec::new();
        node_paths(&panel, &parent, &mut paths);
        drop_signals_to_missing(&mut panel, &format!("{panel_path}."), &paths)?;
        attacher.slot_mut(CHILDREN)?.push(panel);
        Ok(())
    })
}

fn node_paths(n: &Node, parent: &str, out: &mut Vec<String>) {
    let path = format!("{parent}.{}", n.name());
    if let Ok(kids) = n.slot(CHILDREN) {
        for c in kids.iter().filter(|c| c.is_node()) {
            node_paths(c, &path, out);
        }
    }
    out.push(path);
}

fn drop_signals_to_missing(n: &mut Node, inside: &str, paths: &[String]) -> Result<()> {
    for slot in [COMMANDS, EVENTS] {
        for entry in n.slot_mut(slot)? {
            let Some(sigs) = entry.as_obj_mut().and_then(|o| o.get_mut("signals")) else {
                continue;
            };
            sigs.a_mut()?.retain(|sig| {
                let to = sig
                    .as_obj()
                    .and_then(|o| o.get("to_object_name"))
                    .and_then(Node::as_val)
                    .and_then(Val::as_str)
                    .unwrap_or_default();
                !to.starts_with(inside) || paths.iter().any(|p| p == to)
            });
        }
    }
    for c in n.slot_mut(CHILDREN)?.iter_mut().filter(|c| c.is_node()) {
        drop_signals_to_missing(c, inside, paths)?;
    }
    Ok(())
}

fn attr(e: &Element, key: &str) -> Option<String> {
    e.attr(key.as_bytes())
        .map(|v| String::from_utf8_lossy(v).into_owned())
}

pub fn metadata(data: &[u8]) -> Result<Vec<u8>> {
    let mut doc = Document::parse(data)?;
    let group = doc
        .root
        .children
        .iter_mut()
        .find(|g| {
            g.kind == Kind::Element && g.name == b"PanelGroup" && attr(g, "Name").as_deref() == Some(GROUP)
        })
        .ok_or_else(|| err(format!("the MFD metadata has no {GROUP} panel group")))?;
    let panels = |g: &Element| {
        g.children
            .iter()
            .filter(|c| c.kind == Kind::Element && c.name == b"Panel")
            .filter_map(|c| attr(c, "Name"))
            .collect::<Vec<_>>()
    };
    if panels(group).iter().any(|n| n == PANEL) {
        return Ok(data.to_vec());
    }
    let at = group
        .children
        .iter()
        .position(|c| {
            c.kind == Kind::Element && c.name == b"Panel" && attr(c, "Name").as_deref() == Some(NEXT_TO)
        })
        .ok_or_else(|| err(format!("the {GROUP} panel group has no {NEXT_TO}")))?;
    let mut ours = group.children[at].clone();
    ours.set_attr(b"Name", PANEL.as_bytes());
    ours.set_attr(b"PanelType", PANEL.as_bytes());
    ours.set_attr(b"Enabled", ENABLED.as_bytes());
    group.children.insert(at + 1, ours);
    Ok(doc.to_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn doc() -> Vec<u8> {
        let el = |name: &str, attrs: &[(&str, &str)], children: Vec<Element>| Element {
            kind: Kind::Element,
            pad: 0,
            name: name.as_bytes().to_vec(),
            attrs: attrs
                .iter()
                .map(|(k, v)| (k.as_bytes().to_vec(), v.as_bytes().to_vec()))
                .collect(),
            children,
            end_pad: 0,
        };
        let action = || {
            el(
                "Action",
                &[("Name", "MFD Button"), ("Type", "OpenNextPanel")],
                vec![],
            )
        };
        let panel = |n: &str| {
            el(
                "Panel",
                &[
                    ("Name", n),
                    ("PanelType", n),
                    ("Enabled", &format!("#x.{n}_enabled")),
                ],
                vec![action()],
            )
        };
        Document {
            root: el(
                "Mfd",
                &[],
                vec![
                    el(
                        "PanelGroup",
                        &[("Name", GROUP)],
                        vec![panel("car_settings_panel"), panel(NEXT_TO)],
                    ),
                    el(
                        "PanelGroup",
                        &[("Name", "anti_stall")],
                        vec![panel("anti_stall_panel")],
                    ),
                ],
            ),
            tail: vec![],
        }
        .to_bytes()
    }

    #[test]
    fn the_addons_page_follows_the_mods_page_in_the_main_group() {
        let out = metadata(&doc()).unwrap();
        let d = Document::parse(&out).unwrap();
        let main = &d.root.children[0];
        let names: Vec<String> = main.children.iter().filter_map(|c| attr(c, "Name")).collect();
        assert_eq!(names, ["car_settings_panel", NEXT_TO, PANEL]);
        let ours = &main.children[2];
        assert_eq!(attr(ours, "PanelType").as_deref(), Some(PANEL));
        assert_eq!(attr(ours, "Enabled").as_deref(), Some(ENABLED));
        assert_eq!(ours.children.len(), 1);
        assert_eq!(metadata(&out).unwrap(), out);
        assert_eq!(d.root.children[1].children.len(), 1);
    }
}

#[cfg(test)]
mod real {
    use super::*;
    use crate::ui::{catalog, check};

    #[test]
    fn the_addons_panel_passes_the_ui_checks_on_the_real_templates() {
        let Ok(path) = std::env::var("SWOFTY_HUD_TEMPLATES") else {
            return;
        };
        let stock = std::fs::read(path).unwrap();
        let rows = [
            Row {
                label: "PIT LANE".into(),
                value: None,
            },
            Row {
                label: "Pit limiter".into(),
                value: Some(value_path(0)),
            },
            Row {
                label: "Mode".into(),
                value: Some(value_path(1)),
            },
        ];
        let out = templates(&stock, &rows).unwrap();
        assert_eq!(templates(&out, &rows).unwrap(), out);
        let input = check::UiInput {
            resource: TEMPLATES,
            frag: "syst",
            data: &out,
            stock: Some(&stock),
        };
        let report = check::ui_edit(
            &input,
            &catalog::Scenes::default(),
            catalog::stock(),
            catalog::engine(),
        )
        .unwrap();
        let hard: Vec<String> = report.hard.iter().map(|h| h.to_string()).collect();
        let warn: Vec<String> = report.warnings.iter().map(|h| h.to_string()).collect();
        eprintln!("hard: {hard:#?}\nwarnings: {warn:#?}");
        assert!(hard.is_empty(), "{hard:#?}");
    }

    #[test]
    fn the_real_metadata_gets_the_addons_page() {
        let Ok(path) = std::env::var("SWOFTY_MFD_METADATA") else {
            return;
        };
        let stock = std::fs::read(path).unwrap();
        assert_eq!(Document::parse(&stock).unwrap().to_bytes(), stock);
        let out = metadata(&stock).unwrap();
        let d = Document::parse(&out).unwrap();
        let main = d
            .root
            .children
            .iter()
            .find(|g| attr(g, "Name").as_deref() == Some(GROUP))
            .unwrap();
        let names: Vec<String> = main.children.iter().filter_map(|c| attr(c, "Name")).collect();
        eprintln!("{names:?}");
        assert_eq!(names.last().map(String::as_str), Some(PANEL));
    }
}
