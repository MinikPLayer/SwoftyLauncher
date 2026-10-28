use swofty_formats::pkg::{Node, Obj, Val};

use super::node::*;
use super::pages::{HOME, STATE};
use crate::Result;

pub const ACTION: &str = "Menu Right Stick Click";
const ACTION_PREFIX: &str = "#input_data.action_sets.menu_actions.actions.";
pub const MOD_MENU: &str = "mod_menu";
pub const MOD_MENU_OFFLINE: &str = "mod_menu_offline";
pub const OFFLINE_TEXT: &str = "Launched with Swofty Launcher";
pub const OFFLINE_PROMPT: &str = "to open settings";
pub const PROMPT_LABEL: &str = "Addon Settings";
const PANEL: &str = "fe_home.contextual_panels.offline_contextual_panel";
const UIDM: &str = "persistent.consume_input_wrapper.persistent_multichoice_message";
const ONCE_ATTR: &str = "swofty_once";
const BUTTON: &str = "lng_continue";
pub const POPUP_TITLE: &str = "lng_swofty_popup_title";
pub const POPUP_BODY: &str = "lng_swofty_popup_body";
const ONLINE_MESSAGES: &[&str] = &[
    "online_services_generic_exception_online",
    "online_services_generic_exception_online_with_code",
    "online_services_generic_exception_offline",
    "online_services_generic_exception_offline_with_code",
];

pub fn popup_seen() -> String {
    format!("{STATE}.online_popup_seen")
}

pub struct Popup {
    pub title: String,
    pub body: String,
}

impl Popup {
    pub fn for_version(version: &str) -> Self {
        Self {
            title: format!("Swofty Mod v{version}"),
            body: format!(
                "Online services are off while mods are loaded, so multiplayer is unavailable.\n\n\
                 New in v{version}:\n\
                 - ADDON SETTINGS on the home screen (F9 / R3)\n\
                 - Addon settings page listing installed addons\n\
                 - Launcher panel on the home screen"
            ),
        }
    }

    pub fn strings(&self) -> Vec<(String, String)> {
        vec![
            (POPUP_TITLE.into(), self.title.clone()),
            (POPUP_BODY.into(), self.body.clone()),
        ]
    }
}

fn signal(to: &str, pin: &str, from: &str, attrs: &[(&str, &str)]) -> Node {
    let attrs: Vec<(&str, Attr)> = attrs.iter().map(|(k, v)| (*k, Attr::from(*v))).collect();
    sig_with(to, pin, from, &attrs)
}

fn sig_to(s: &Node) -> (&str, &str) {
    let o = s.as_obj();
    let get = |k| {
        o.and_then(|o| o.get(k))
            .and_then(Node::as_val)
            .and_then(Val::as_str)
            .unwrap_or_default()
    };
    (get("to_object_name"), get("to_pin_name"))
}

fn event_mut<'n>(n: &'n mut Node, name: &str) -> Result<&'n mut Vec<Node>> {
    let what = n.name().to_owned();
    n.slot_mut(EVENTS)?
        .iter_mut()
        .find(|e| e.o().ok().and_then(|o| o.str_at("eventName").ok()) == Some(name))
        .ok_or_else(|| err(format!("{what} has no {name} event")))?
        .o_mut()?
        .at_mut("signals")?
        .a_mut()
}

pub fn fe_home(data: &[u8]) -> Result<Vec<u8>> {
    edit(data, |p| {
        let screen = first_screen(p)?;
        if screen.name() != "fe_home" {
            return Err(err("the home screen file does not hold fe_home"));
        }
        let tooltips = screen.child_mut("tooltips")?;
        let mut tip = clone_node(tooltips.child("settings")?, "fe_home.tooltips", MOD_MENU)?;
        set_prop_value(&mut tip, "tooltipsName", Val::str(MOD_MENU))?;
        set_prop_value(tip.child_mut("text")?, "localise", Val::bool(false))?;
        tooltips.slot_mut(CHILDREN)?.push(tip);
        let mut listener = clone_node(screen.child("settings")?, "fe_home", "mod_menu_listener")?;
        set_prop_value(&mut listener, "action_name", Val::str(ACTION))?;
        let ev = listener
            .slot_mut(EVENTS)?
            .first_mut()
            .ok_or_else(|| err("the settings listener has no events"))?
            .o_mut()?;
        if ev.str_at("eventName")? != "pressed" {
            return Err(err("the settings listener does not start with pressed"));
        }
        *ev.at_mut("signals")?.a_mut()? = vec![
            signal("fe_home.Action", "route", "pressed", &[("key", MOD_MENU)]),
            signal("fe_home.audio_theme_trigger", "confirm", "pressed", &[]),
        ];
        fix_ids(&mut listener, Some("fe_home"));
        screen.slot_mut(CHILDREN)?.push(listener);
        Ok(())
    })
}

pub fn offline_panel(data: &[u8]) -> Result<Vec<u8>> {
    edit(data, |p| {
        let screen = first_screen(p)?;
        let cp = screen.child_mut("contextual_panels")?;
        let panel = cp.child_mut("offline_contextual_panel")?;
        literal_text(panel.child_mut("subtitle")?, OFFLINE_TEXT)?;
        literal_text(panel.child_mut("tooltip_label")?, OFFLINE_PROMPT)?;
        set_prop_value(
            panel.child_mut("tooltip_extra_2")?,
            "tooltipsName",
            Val::str(MOD_MENU_OFFLINE),
        )?;
        let decision = cp.child_mut("contextual_decision")?;
        event_mut(decision, "offline")?
            .retain(|s| sig_to(s).0 != "fe_home.contextual_panels.extra_2_listener");
        event_mut(decision, "default")?.push(signal(PANEL, "show", "default", &[]));
        fix_ids(screen, None);
        Ok(())
    })
}

pub fn hide_panel(data: &[u8]) -> Result<Vec<u8>> {
    edit(data, |p| {
        let screen = first_screen(p)?;
        let dec = screen
            .child_mut("contextual_panels")?
            .child_mut("contextual_decision")?;
        for e in dec.slot_mut(EVENTS)? {
            e.o_mut()?.at_mut("signals")?.a_mut()?.retain(|s| {
                let (to, pin) = sig_to(s);
                !(to.ends_with("offline_contextual_panel") && pin == "show")
            });
        }
        Ok(())
    })
}

fn tooltip_dict(p: &mut swofty_formats::pkg::Pkg) -> Result<&mut Obj> {
    root_obj(p)?
        .at_mut("tool_tips")?
        .o_mut()?
        .at_mut("Contents")?
        .o_mut()?
        .at_mut("tooltips_dictionary")?
        .o_mut()?
        .at_mut("Contents")?
        .o_mut()
}

fn add_tooltip(
    data: &[u8],
    name: &str,
    src: &str,
    label: &str,
    action: Option<&str>,
    priority: Option<&str>,
) -> Result<Vec<u8>> {
    edit(data, |p| {
        let d = tooltip_dict(p)?;
        if d.get(name).is_some() {
            return Err(err(format!("the tooltips already have {name}")));
        }
        let mut e = d.at(src)?.clone();
        let eo = e.o_mut()?;
        set_str(eo, "Name", name)?;
        let c = eo.at_mut("Contents")?.o_mut()?;
        let mut set = |key: &str, v: &str| set_str(c.at_mut(key)?.o_mut()?, "Value", v);
        if let Some(action) = action {
            set("action_path", &format!("{ACTION_PREFIX}{action}"))?;
        }
        set("text", label)?;
        if let Some(priority) = priority {
            set("priority", priority)?;
        }
        d.set(name, e);
        Ok(())
    })
}

pub fn tooltips(data: &[u8], adjust: &str) -> Result<Vec<u8>> {
    let d = add_tooltip(
        data,
        MOD_MENU,
        "settings_fe_home",
        PROMPT_LABEL,
        Some(ACTION),
        Some("5"),
    )?;
    let d = add_tooltip(&d, MOD_MENU_OFFLINE, "delete", OFFLINE_PROMPT, Some(ACTION), None)?;
    add_tooltip(&d, adjust, "change_driver", "Adjust", None, None)
}

fn route_cmd(kind: &str, id: u32, attrs: &[(&str, &str)], deps: Option<&str>) -> Node {
    let mut a = vec![obj(vec![("Id", s("Id")), ("Value", s(&id.to_string()))])];
    a.extend(
        attrs
            .iter()
            .map(|(k, v)| obj(vec![("Id", s(k)), ("Value", s(v))])),
    );
    let mut items = vec![("Attributes", arr(a)), ("Type", s(kind))];
    if let Some(d) = deps {
        items.push(("Dependencies", s(d)));
    }
    obj(items)
}

fn route(id: &str, cmds: Vec<Node>, deps: &str, kind: i32, destination: Option<&str>) -> Node {
    let mut items = vec![("Id", s(id)), ("Commands", arr(cmds))];
    if let Some(dst) = destination {
        items.push((
            "Attributes",
            arr(vec![obj(vec![("Id", s("Destination")), ("Value", s(dst))])]),
        ));
    }
    items.push(("Type", i32v(kind)));
    items.push(("Dependencies", s(deps)));
    obj(items)
}

fn screen_routes(name: &str) -> Vec<Node> {
    let open = |n| route_cmd("OpenScreen", n, &[("Target", name)], None);
    vec![
        route(
            "open",
            vec![
                open(1),
                route_cmd("InputState", 2, &[], Some("1")),
                route_cmd(
                    "TransitionScreen",
                    3,
                    &[("Direction", "in"), ("Target", name)],
                    Some("1"),
                ),
            ],
            "2,3",
            0,
            None,
        ),
        route(
            "open_no_input",
            vec![
                open(1),
                route_cmd(
                    "TransitionScreen",
                    2,
                    &[("Direction", "in"), ("Target", name)],
                    Some("1"),
                ),
            ],
            "2",
            0,
            None,
        ),
        route(
            "close",
            vec![
                route_cmd(
                    "TransitionScreen",
                    1,
                    &[("Direction", "out"), ("Target", name)],
                    None,
                ),
                route_cmd("CloseScreen", 2, &[("Target", name)], Some("1")),
            ],
            "2",
            0,
            None,
        ),
    ]
}

fn subpage_route(id: &str, src: &str, dst: &str, kind: i32) -> Node {
    route(
        id,
        vec![
            route_cmd(
                "TriggerSubRoute",
                1,
                &[("SubRoute", "close"), ("Target", src), ("To", dst)],
                None,
            ),
            route_cmd(
                "TriggerSubRoute",
                2,
                &[
                    ("SubRoute", "open"),
                    ("Target", dst),
                    ("From", src),
                    ("Message", ""),
                ],
                Some("1"),
            ),
        ],
        "2",
        kind,
        Some(dst),
    )
}

fn migrate_route(id: &str, src: &str, flow: &str, dst: &str) -> Node {
    migrate_route_with(id, src, flow, dst, ("open_no_input", ""), 2)
}

fn migrate_route_with(id: &str, src: &str, flow: &str, dst: &str, open: (&str, &str), kind: i32) -> Node {
    let (sub, message) = open;
    route(
        id,
        vec![
            route_cmd(
                "TriggerSubRoute",
                1,
                &[("SubRoute", "close"), ("Target", src), ("To", dst)],
                None,
            ),
            route_cmd(
                "Migrate",
                2,
                &[("Flow", flow), ("SkipActivator", "true")],
                Some("1"),
            ),
            route_cmd(
                "TriggerSubRoute",
                3,
                &[
                    ("SubRoute", sub),
                    ("Target", dst),
                    ("From", src),
                    ("Message", message),
                ],
                Some("2"),
            ),
        ],
        "3",
        kind,
        Some(dst),
    )
}

fn route_group(name: &str, routes: Vec<Node>) -> Node {
    obj(vec![
        ("Id", s(name)),
        ("Routes", arr(routes)),
        (
            "Attributes",
            arr(vec![obj(vec![("Id", s("Source")), ("Value", s(name))])]),
        ),
    ])
}

pub fn route_groups(pages: &[(String, String)]) -> Vec<Node> {
    let mut home = screen_routes(HOME);
    home.push(migrate_route("back", HOME, "frontend_flow", "fe_home"));
    home.extend(pages.iter().map(|(rid, s)| subpage_route(rid, HOME, s, 1)));
    let mut groups = vec![route_group(HOME, home)];
    for (_, s) in pages {
        let mut r = screen_routes(s);
        r.push(subpage_route("back", s, HOME, 2));
        groups.push(route_group(s, r));
    }
    groups
}

/// Route groups for addon screens, as ADDON SETTINGS pages have: open, open_no_input and close,
/// and back, which closes the screen and opens its anchor again.
pub fn screen_route_groups(screens: &[(String, String)]) -> Vec<Node> {
    screens
        .iter()
        .map(|(s, anchor)| {
            let mut r = screen_routes(s);
            r.push(subpage_route("back", s, anchor, 2));
            route_group(s, r)
        })
        .collect()
}

/// Adds open_<name> to each anchor's route group in these routes, when they hold it: the route
/// closes the anchor and opens the addon screen, as ADDON SETTINGS opens its pages. Each entry is
/// (name, anchor, screen).
pub fn anchor_routes(data: &[u8], screens: &[(String, String, String)]) -> Result<Vec<u8>> {
    edit(data, |p| {
        let gs = root_obj(p)?.at_mut("RouteGroupings")?.a_mut()?;
        for (name, anchor, s) in screens {
            let Some(g) = gs.iter_mut().find(|g| id_of(g) == Some(anchor)) else {
                continue;
            };
            let id = format!("open_{name}");
            let routes = g.o_mut()?.at_mut("Routes")?.a_mut()?;
            if !routes.iter().any(|r| id_of(r) == Some(&id)) {
                routes.push(subpage_route(&id, anchor, s, 1));
            }
        }
        Ok(())
    })
}

/// The Driver Career tile's route to the season length step, which a career setup's entry route copies.
pub const SETUP_ENTRY_SOURCE: &str = "career_wizard_driver_career_to_f1";
/// The message the career menu opens the career content select with for Driver Career.
pub const DRIVER_CAREER: &str = "driver_career";

pub struct SetupRoutes {
    pub route: String,
    pub home: String,
    pub rows: String,
    pub next: String,
    pub next_flow: String,
    pub back: String,
    pub back_flow: String,
    pub back_route: String,
}

/// The route groups of a career setup's home and rows page. The home's `confirm` closes it, moves to the
/// wizard's flow and opens `next`; its `back` moves to the content select's flow and opens it for Driver Career.
pub fn setup_route_groups(s: &SetupRoutes) -> Vec<Node> {
    let mut home = screen_routes(&s.home);
    let mut back = migrate_route_with("back", &s.home, &s.back_flow, &s.back, ("open", DRIVER_CAREER), 2);
    no_auto_focus(&mut back);
    home.push(back);
    home.push(migrate_route_with(
        super::pages::SETUP_CONFIRM,
        &s.home,
        &s.next_flow,
        &s.next,
        ("open", ""),
        1,
    ));
    home.push(subpage_route(super::pages::SETUP_OPTIONS, &s.home, &s.rows, 1));
    let mut rows = screen_routes(&s.rows);
    rows.push(subpage_route("back", &s.rows, &s.home, 2));
    vec![route_group(&s.home, home), route_group(&s.rows, rows)]
}

fn entry_route(group: &Node, s: &SetupRoutes) -> Result<Node> {
    let mut r = group
        .o()?
        .at("Routes")?
        .a()?
        .iter()
        .find(|r| id_of(r) == Some(SETUP_ENTRY_SOURCE))
        .cloned()
        .ok_or_else(|| err(format!("{} has no {SETUP_ENTRY_SOURCE} route", s.back)))?;
    let from = r
        .o()?
        .at("Attributes")?
        .a()?
        .iter()
        .filter_map(Node::as_obj)
        .find(|a| a.str_at("Id").ok() == Some("Destination"))
        .and_then(|a| a.str_at("Value").ok())
        .map(str::to_owned)
        .ok_or_else(|| err(format!("{SETUP_ENTRY_SOURCE} has no destination")))?;
    replace_strings(&mut r, &|v| match v {
        v if v == from => Some(s.home.clone()),
        "career_season_setup_flow" => Some("options_flow".into()),
        "open_no_input" => Some("open".into()),
        _ => None,
    });
    set_str(r.o_mut()?, "Id", &s.route)?;
    let cmds = r.o_mut()?.at_mut("Commands")?.a_mut()?;
    let id = cmds
        .iter()
        .filter_map(|c| cmd_attr(c, "Id")?.parse::<u32>().ok())
        .max()
        .unwrap_or(0)
        + 1;
    let wiped = cmds
        .iter()
        .find(|c| cmd_type(c) == Some("TriggerSubRoute") && cmd_attr(c, "SubRoute") == Some("close"))
        .and_then(|c| c.as_obj()?.get("Dependencies")?.as_val()?.as_str())
        .map(str::to_owned);
    cmds.push(route_cmd(
        "FrontendSceneSwitch",
        id,
        &SETUP_SCENE,
        wiped.as_deref(),
    ));
    Ok(r)
}

/// The stock CUSTOM settings page's backdrop, which every route into a career setup page switches to.
const SETUP_SCENE: [(&str, &str); 4] = [
    ("Target", ""),
    ("SceneOverride", "Customisation"),
    ("SubSceneOverride", "Generic"),
    ("CameraOverride", "Main"),
];

fn cmd_type(c: &Node) -> Option<&str> {
    c.as_obj()?.get("Type")?.as_val()?.as_str()
}

fn cmd_attr<'a>(c: &'a Node, key: &str) -> Option<&'a str> {
    c.as_obj()?
        .get("Attributes")?
        .as_arr()?
        .items
        .iter()
        .filter_map(Node::as_obj)
        .find(|a| a.str_at("Id").ok() == Some(key))?
        .str_at("Value")
        .ok()
}

fn no_auto_focus(r: &mut Node) {
    if let Some(a) = r
        .as_obj_mut()
        .and_then(|o| o.get_mut("Attributes"))
        .and_then(Node::as_arr_mut)
    {
        a.items
            .push(obj(vec![("Id", s("AutoFocus")), ("Value", s("false"))]));
    }
}

/// The way back from the wizard step `next` to the home, as the wizard's own routes leave its flow: `next` and the
/// slices its group opens transition out, the slices close, and the flow moves to the options flow.
fn wizard_exit_route(s: &SetupRoutes, slices: &[&str]) -> Node {
    let out = |id: u32, target: &str| {
        route_cmd(
            "TransitionScreen",
            id,
            &[("Direction", "out"), ("Target", target)],
            None,
        )
    };
    let mut cmds = vec![out(1, &s.next)];
    let mut done = vec!["1".to_owned()];
    let mut id = 1;
    for slice in slices {
        cmds.push(out(id + 1, slice));
        cmds.push(route_cmd(
            "CloseScreen",
            id + 2,
            &[("Target", slice)],
            Some(&(id + 1).to_string()),
        ));
        done.push((id + 2).to_string());
        id += 2;
    }
    let migrate = (id + 1).to_string();
    cmds.push(route_cmd(
        "Migrate",
        id + 1,
        &[("Flow", "options_flow"), ("SkipActivator", "true")],
        Some(&done.join(",")),
    ));
    let open = (id + 2).to_string();
    cmds.push(route_cmd(
        "TriggerSubRoute",
        id + 2,
        &[
            ("SubRoute", "open"),
            ("Target", &s.home),
            ("From", &s.next),
            ("Message", ""),
        ],
        Some(&migrate),
    ));
    cmds.push(route_cmd(
        "FrontendSceneSwitch",
        id + 3,
        &SETUP_SCENE,
        Some(&migrate),
    ));
    let mut r = route(&s.back_route, cmds, &open, 2, Some(&s.home));
    no_auto_focus(&mut r);
    r
}

fn slices_of(g: &Node) -> Vec<String> {
    g.as_obj()
        .and_then(|o| o.get("Attributes"))
        .and_then(Node::as_arr)
        .into_iter()
        .flat_map(|a| &a.items)
        .filter_map(Node::as_obj)
        .filter(|a| a.str_at("Id").ok() == Some("Slices"))
        .filter_map(|a| a.str_at("Value").ok())
        .flat_map(|v| v.split(','))
        .map(str::trim)
        .filter(|v| !v.is_empty())
        .map(str::to_owned)
        .collect()
}

fn add_route(g: &mut Node, r: Node) -> Result<()> {
    let gid = id_of(g).unwrap_or_default().to_owned();
    let rid = id_of(&r).unwrap_or_default().to_owned();
    let routes = g.o_mut()?.at_mut("Routes")?.a_mut()?;
    if routes.iter().any(|x| id_of(x) == Some(&rid)) {
        return Err(err(format!("{gid} has a route {rid} already")));
    }
    routes.push(r);
    Ok(())
}

#[derive(Debug, Default)]
pub struct Placed {
    /// Each route added, as `<group>.<route>`.
    pub routes: Vec<String>,
    /// The game screens the added routes name: the slices of each `next` screen's group.
    pub screens: Vec<String>,
}

/// Adds each career setup's entry route to the content select's route group and its back route to the group of
/// its `next` screen, for the groups these routes hold.
pub fn setup_routes(data: &[u8], setups: &[SetupRoutes], placed: &mut Placed) -> Result<Vec<u8>> {
    edit(data, |p| {
        let gs = root_obj(p)?.at_mut("RouteGroupings")?.a_mut()?;
        for s in setups {
            if let Some(g) = gs.iter_mut().find(|g| id_of(g) == Some(&s.back)) {
                let r = entry_route(g, s)?;
                add_route(g, r)?;
                placed.routes.push(format!("{}.{}", s.back, s.route));
            }
            if let Some(g) = gs.iter_mut().find(|g| id_of(g) == Some(&s.next)) {
                let slices = slices_of(g);
                let names: Vec<&str> = slices.iter().map(String::as_str).collect();
                add_route(g, wizard_exit_route(s, &names))?;
                placed.routes.push(format!("{}.{}", s.next, s.back_route));
                placed.screens.extend(slices);
            }
        }
        Ok(())
    })
}

fn id_of(n: &Node) -> Option<&str> {
    n.as_obj()?.get("Id")?.as_val()?.as_str()
}

fn mod_menu_route(groupings: &[Node]) -> Result<Node> {
    let home = groupings
        .iter()
        .find(|g| id_of(g) == Some("fe_home"))
        .ok_or_else(|| err("the routes have no fe_home grouping"))?;
    let mut r = home
        .o()?
        .at("Routes")?
        .a()?
        .iter()
        .find(|r| id_of(r) == Some("options"))
        .cloned()
        .ok_or_else(|| err("fe_home has no options route"))?;
    set_str(r.o_mut()?, "Id", MOD_MENU)?;
    replace_strings(&mut r, &|v| match v {
        "settings_container" => Some(HOME.into()),
        "open_no_input" => Some("open".into()),
        _ => None,
    });
    Ok(r)
}

/// Adds ADDON SETTINGS' route groups, which must be new, and each of `optional` whose group the
/// routes don't have yet.
pub fn routes(data: &[u8], groups: Vec<Node>, optional: Vec<Node>) -> Result<Vec<u8>> {
    edit(data, |p| {
        let gs = root_obj(p)?.at_mut("RouteGroupings")?.a_mut()?;
        let replace = mod_menu_route(gs)?;
        for g in optional {
            if !gs.iter().any(|x| id_of(x) == id_of(&g)) {
                gs.push(g);
            }
        }
        for g in groups {
            if gs.iter().any(|x| id_of(x) == id_of(&g)) {
                return Err(err(format!(
                    "route group {} exists already",
                    id_of(&g).unwrap_or_default()
                )));
            }
            gs.push(g);
        }
        let rid = id_of(&replace).unwrap_or_default().to_owned();
        let grp = gs
            .iter_mut()
            .find(|x| id_of(x) == Some("fe_home"))
            .ok_or_else(|| err("the routes have no fe_home grouping"))?;
        let routes = grp.o_mut()?.at_mut("Routes")?.a_mut()?;
        routes.retain(|r| id_of(r) != Some(&rid));
        routes.push(replace);
        Ok(())
    })
}

pub fn flow_entries(data: &[u8], flow: &str, screens: &[String]) -> Result<Vec<u8>> {
    edit(data, |p| {
        let flows = root_obj(p)?.at_mut("Flows")?.a_mut()?;
        let f = flows
            .iter_mut()
            .find(|f| f.name() == flow)
            .ok_or_else(|| err(format!("flows.ui has no {flow}")))?;
        let ents = f
            .slot_mut(CHILDREN)?
            .first_mut()
            .ok_or_else(|| err(format!("{flow} has no scenes")))?
            .slot_mut(CHILDREN)?;
        let tmpl = ents
            .first()
            .cloned()
            .ok_or_else(|| err(format!("{flow} has no entries")))?;
        for screen in screens {
            let entry = format!("enter_{screen}");
            if ents.iter().any(|e| e.name() == entry) {
                return Err(err(format!("{flow} has {entry} already")));
            }
            let mut e = tmpl.clone();
            let p0 = e.props_mut()?;
            set_str(p0, "name", &entry)?;
            set_str(p0.at_mut("screen")?.o_mut()?, "value", screen)?;
            p0.drop_key("command");
            ents.push(e);
        }
        fix_ids(f, None);
        Ok(())
    })
}

pub fn regions(data: &[u8], screens: &[String]) -> Result<Vec<u8>> {
    edit(data, |p| {
        let regs = root_obj(p)?
            .at_mut("regions")?
            .o_mut()?
            .at_mut("Contents")?
            .a_mut()?;
        let reg = regs
            .iter_mut()
            .find(|r| r.o().ok().and_then(|o| o.str_at("Name").ok()) == Some("region_undefined"))
            .ok_or_else(|| err("region_loading has no region_undefined"))?;
        let c = reg.o_mut()?.at_mut("Contents")?.o_mut()?;
        for screen in screens {
            if c.get(screen).is_some() {
                return Err(err(format!("region_undefined has {screen} already")));
            }
            let mut e = c.at("fe_home")?.clone();
            let eo = e.o_mut()?;
            set_str(eo, "Name", screen)?;
            set_str(
                eo,
                "Value",
                &format!("asset_groups/ui_package/screens/{screen}.assetgroup"),
            )?;
            c.set(screen, e);
        }
        Ok(())
    })
}

pub fn ui_data(data: &[u8], entries: Vec<(String, Node)>) -> Result<Vec<u8>> {
    edit(data, |p| {
        let c = root_obj(p)?
            .at_mut("ui_menu_data")?
            .o_mut()?
            .at_mut("Contents")?
            .o_mut()?;
        if c.get("swofty").is_some() {
            return Err(err("ui_menu_data has swofty already"));
        }
        c.set("swofty", uidb_dict("swofty", entries));
        Ok(())
    })
}

pub const MESSAGE_ID: &str = "swofty_message";
const MESSAGE_TEMPLATE: &str = "online_services_EA_not_logged_in";
const ATTRIBUTE_ROW: &str = "#ui_messages.default_queue.multichoice_message.user_attributes";

fn attribute_token(attribute: &str) -> Node {
    obj(vec![(
        "VALUE",
        obj(vec![
            ("localise", boolv(false)),
            ("value", s("")),
            ("name", s("VALUE")),
            ("dbEntry", s(&format!("{ATTRIBUTE_ROW}[{attribute}].Value"))),
        ]),
    )])
}

fn attribute(name: &str) -> Node {
    obj(vec![
        ("localise", boolv(false)),
        ("value", s("")),
        ("name", s(name)),
        ("dbEntry", s("")),
    ])
}

fn by_message_id<'n>(list: &'n [Node], id: &str) -> Option<&'n Node> {
    list.iter()
        .find(|m| m.as_obj().and_then(|o| o.str_at("id").ok()) == Some(id))
}

fn runtime_message(list: &[Node]) -> Result<Option<Node>> {
    if by_message_id(list, MESSAGE_ID).is_some() {
        return Ok(None);
    }
    let Some(template) = by_message_id(list, MESSAGE_TEMPLATE) else {
        return Ok(None);
    };
    let mut m = template.clone();
    let o = m.o_mut()?;
    set_str(o, "id", MESSAGE_ID)?;
    set_str(o, "title", "lng_value_tag")?;
    set_str(o, "messageBody", "lng_value_tag")?;
    o.set("titleTokens", attribute_token("title"));
    o.set("bodyTokens", attribute_token("message"));
    *o.at_mut("attributes")?.a_mut()? = vec![attribute("title"), attribute("message")];
    *o.at_mut("options")?.a_mut()? = vec![obj(vec![
        ("value", s("lng_msg_ok")),
        ("response", s("ok")),
        ("defaultSelection", boolv(true)),
    ])];
    Ok(Some(m))
}

pub fn messages(data: &[u8], text: bool) -> Result<Vec<u8>> {
    edit(data, |p| {
        let list = root_obj(p)?.at_mut("messages")?.a_mut()?;
        if let Some(added) = runtime_message(list)? {
            list.push(added);
        }
        let mut done = Vec::new();
        for m in root_obj(p)?.at_mut("messages")?.a_mut()? {
            let m = m.o_mut()?;
            let id = m.str_at("id")?.to_owned();
            if !ONLINE_MESSAGES.contains(&id.as_str()) {
                continue;
            }
            if text {
                set_str(m, "title", POPUP_TITLE)?;
                set_str(m, "messageBody", POPUP_BODY)?;
                m.drop_key("titleTokens");
                m.drop_key("bodyTokens");
            }
            *m.at_mut("options")?.a_mut()? = vec![obj(vec![
                ("value", s(BUTTON)),
                ("response", s("cancel")),
                ("defaultSelection", boolv(true)),
            ])];
            m.at_mut("attributes")?.a_mut()?.push(obj(vec![
                ("localise", boolv(false)),
                ("value", s("true")),
                ("name", s(ONCE_ATTR)),
                ("dbEntry", s("")),
            ]));
            done.push(id);
        }
        done.sort();
        let mut want: Vec<&str> = ONLINE_MESSAGES.to_vec();
        want.sort();
        if done != want {
            return Err(err(format!(
                "the online services messages changed: found {done:?}"
            )));
        }
        Ok(())
    })
}

pub const TOAST_PREFIX: &str = "swofty_toast_";
pub const TOAST_SECONDS: &[u32] = &[2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 30];
const TOAST_TEMPLATE: &str = "teammate_entering_pit";
const TOAST_FLAG: &str = "#overrides.osd.race_flags.generic";

fn toast_attribute(name: &str, value: Node) -> Node {
    obj(vec![
        ("localise", boolv(false)),
        ("value", value),
        ("name", s(name)),
        ("dbEntry", s("")),
    ])
}

pub fn hud_messages(data: &[u8]) -> Result<Vec<u8>> {
    edit(data, |p| {
        let list = root_obj(p)?.at_mut("messages")?.a_mut()?;
        let Some(template) = by_message_id(list, TOAST_TEMPLATE).cloned() else {
            return Err(err(format!("the HUD messages have no {TOAST_TEMPLATE}")));
        };
        for &n in TOAST_SECONDS {
            let id = format!("{TOAST_PREFIX}{n}");
            if by_message_id(list, &id).is_some() {
                continue;
            }
            let mut m = template.clone();
            let o = m.o_mut()?;
            set_str(o, "id", &id)?;
            *o.at_mut("attributes")?.a_mut()? = vec![
                toast_attribute("message", s("")),
                toast_attribute("sub_message", s("")),
                toast_attribute("msg_duration", f32v(n as f32)),
                toast_attribute("flag", s(TOAST_FLAG)),
                toast_attribute("enabled_in_spectator", boolv(true)),
            ];
            list.push(m);
        }
        Ok(())
    })
}

fn multichoice(p: &mut swofty_formats::pkg::Pkg) -> Result<&mut Node> {
    let scr = first_screen(p)?;
    if scr.name() != "persistent" {
        return Err(err("the persistent screen file does not hold persistent"));
    }
    scr.child_mut("consume_input_wrapper")?
        .child_mut("persistent_multichoice_message")
}

fn relabel(sigs: &[Node], from: &str) -> Result<Vec<Node>> {
    let mut out = sigs.to_vec();
    for s in &mut out {
        set_str(s.o_mut()?, "from_pin_name", from)?;
    }
    Ok(out)
}

pub fn persistent_gate(data: &[u8]) -> Result<Vec<u8>> {
    let seen = popup_seen();
    edit(data, |p| {
        let m = multichoice(p)?;
        let act = event_mut(m, "activate_message")?;
        let names: Vec<(String, String)> = act
            .iter()
            .map(|s| {
                let (a, b) = sig_to(s);
                (a.to_owned(), b.to_owned())
            })
            .collect();
        let want = [
            (
                "persistent.consume_input_wrapper".to_owned(),
                "consumeStart".to_owned(),
            ),
            (UIDM.to_owned(), "show".to_owned()),
        ];
        if names != want {
            return Err(err(format!("activate_message changed: {names:?}")));
        }
        let show = act.clone();
        *act = vec![sig(
            &format!("{UIDM}.swofty_once_check"),
            "is_valid",
            "activate_message",
        )];
        let hidden_props = || {
            vec![
                ("enabled", prop(boolv(false), None)),
                ("visible", prop(boolv(false), None)),
            ]
        };
        let mut once = hidden_props();
        once.push((
            "dbEntry",
            prop(s(""), Some(&format!("user_attributes.{ONCE_ATTR}"))),
        ));
        let mut check = hidden_props();
        check.push(("value", prop(boolv(true), None)));
        check.push(("dbEntry", prop(s(&seen), None)));
        let mut mark = hidden_props();
        mark.push(("source", prop(boolv(true), None)));
        mark.push(("target", prop(boolv(false), Some(&seen))));
        let mut no_seen = vec![sig(&format!("{UIDM}.swofty_mark_seen"), "trigger", "no")];
        no_seen.extend(relabel(&show, "no")?);
        let kids = vec![
            node(
                "swofty_once_check",
                "DBVV",
                once,
                vec![
                    event(
                        "yes",
                        vec![sig(&format!("{UIDM}.swofty_seen_check"), "query", "yes")],
                    ),
                    event("no", relabel(&show, "no")?),
                ],
                &[],
                &[],
            ),
            node(
                "swofty_seen_check",
                "SVCD",
                check,
                vec![
                    event("no", no_seen),
                    event(
                        "yes",
                        vec![
                            sig(UIDM, "deactivate_message", "yes"),
                            sig(&format!("{UIDM}.disable_input"), "trigger", "yes"),
                            signal(
                                &format!("{UIDM}.user_response_setter"),
                                "setValue",
                                "yes",
                                &[("value", "cancel")],
                            ),
                        ],
                    ),
                ],
                &[],
                &[],
            ),
            node("swofty_mark_seen", "UIDS", mark, vec![], &[], &[]),
        ];
        m.slot_mut(CHILDREN)?.extend(kids);
        fix_ids(first_screen(p)?, None);
        Ok(())
    })
}

#[cfg(test)]
mod tests {
    use swofty_formats::pkg::Pkg;

    use super::*;
    use crate::ui::check::stock_kept;

    fn label(name: &str) -> Node {
        node(
            name,
            "UILB",
            vec![(
                "text",
                obj(vec![
                    ("value", s("")),
                    ("dbentry", s(name)),
                    ("isloctag", boolv(true)),
                ]),
            )],
            vec![],
            &[],
            &[],
        )
    }

    fn with_kids(mut n: Node, kids: Vec<Node>) -> Node {
        n.slot_mut(CHILDREN).unwrap().extend(kids);
        n
    }

    fn stock_popup() -> Vec<u8> {
        let activate = vec![
            sig(
                "persistent.consume_input_wrapper",
                "consumeStart",
                "activate_message",
            ),
            sig(UIDM, "show", "activate_message"),
        ];
        let pressed = vec![
            sig(UIDM, "deactivate_message", "pressed"),
            sig(&format!("{UIDM}.disable_input"), "trigger", "pressed"),
        ];
        let button = with_kids(
            node(
                "button1",
                "UIBT",
                vec![],
                vec![event("pressed", pressed)],
                &[],
                &[],
            ),
            vec![label("title")],
        );
        let back = node(
            "back_listener",
            "UIBT",
            vec![],
            vec![event(
                "pressed",
                vec![
                    sig(UIDM, "deactivate_message", "pressed"),
                    sig(&format!("{UIDM}.disable_input"), "trigger", "pressed"),
                    signal(
                        &format!("{UIDM}.user_response_setter"),
                        "setValue",
                        "pressed",
                        &[("value", "cancel")],
                    ),
                ],
            )],
            &[],
            &[],
        );
        let message = with_kids(
            node(
                "persistent_multichoice_message",
                "UIDM",
                vec![],
                vec![event("activate_message", activate)],
                &[],
                &[],
            ),
            vec![
                label("title"),
                label("body"),
                button,
                back,
                node("disable_input", "UIGP", vec![], vec![], &[], &[]),
                node("user_response_setter", "UIDS", vec![], vec![], &[], &[]),
            ],
        );
        let wrapper = with_kids(
            node("consume_input_wrapper", "UIGP", vec![], vec![], &[], &[]),
            vec![message],
        );
        let mut screen = with_kids(
            node("persistent", "UISC", vec![], vec![], &[], &[]),
            vec![wrapper],
        );
        fix_ids(&mut screen, None);
        ui_bytes(vec![screen]).unwrap()
    }

    fn popup_node(data: &[u8]) -> Node {
        first_screen_of(data)
            .unwrap()
            .find_mut("consume_input_wrapper.persistent_multichoice_message")
            .unwrap()
            .clone()
    }

    fn event_signals(n: &Node, name: &str) -> Vec<Node> {
        n.slot(EVENTS)
            .unwrap()
            .iter()
            .find(|e| e.o().unwrap().str_at("eventName").unwrap() == name)
            .unwrap()
            .o()
            .unwrap()
            .at("signals")
            .unwrap()
            .a()
            .unwrap()
            .clone()
    }

    #[test]
    fn the_popup_gate_leaves_stock_popups_alone() {
        let stock = stock_popup();
        let gated = persistent_gate(&stock).unwrap();
        let changed = stock_kept(&stock, &gated, "swofty_").unwrap();
        assert!(changed.is_empty(), "{changed:?}");

        let popup = popup_node(&gated);
        let shown = relabel(&event_signals(&popup_node(&stock), "activate_message"), "no").unwrap();
        assert_eq!(
            canonical(&arr(event_signals(
                popup.child("swofty_once_check").unwrap(),
                "no"
            ))),
            canonical(&arr(shown))
        );
    }

    #[test]
    fn a_signal_to_the_popup_labels_fails_the_check() {
        let stock = stock_popup();
        let gated = edit(&persistent_gate(&stock).unwrap(), |p| {
            let m = multichoice(p)?;
            event_mut(m.child_mut("swofty_once_check")?, "no")?.push(sig_with(
                &format!("{UIDM}.title"),
                "setLocalise",
                "no",
                &[("localise", Attr::Bool(true))],
            ));
            Ok(())
        })
        .unwrap();
        let changed = stock_kept(&stock, &gated, "swofty_").unwrap();
        assert_eq!(changed.len(), 1, "{changed:?}");
        assert!(changed[0].contains(".title.setLocalise"), "{changed:?}");
    }

    fn message(id: &str, tokens: bool) -> Node {
        let mut items = vec![
            ("id", s(id)),
            ("title", s(&format!("lng_{id}_title"))),
            ("messageBody", s(&format!("lng_{id}_body"))),
            ("attributes", arr(vec![])),
            (
                "options",
                arr(vec![obj(vec![
                    ("value", s("lng_msg_cancel")),
                    ("response", s("cancel")),
                    ("defaultSelection", boolv(false)),
                ])]),
            ),
        ];
        if tokens {
            items.push((
                "bodyTokens",
                obj(vec![(
                    "ERRORCODE",
                    obj(vec![("localise", boolv(false)), ("value", s(""))]),
                )]),
            ));
        }
        obj(items)
    }

    fn messages_file() -> Vec<u8> {
        let mut all: Vec<Node> = ONLINE_MESSAGES
            .iter()
            .map(|id| message(id, id.ends_with("_with_code")))
            .collect();
        all.push(message("quit_session", false));
        all.push(message(MESSAGE_TEMPLATE, true));
        Pkg {
            name: [0; 4],
            root: obj(vec![("messages", arr(all))]),
            vca: Vec::new(),
            layout: None,
        }
        .to_bytes()
        .unwrap()
    }

    fn by_id(data: &[u8]) -> Vec<(String, Node)> {
        Pkg::parse(data)
            .unwrap()
            .root
            .o()
            .unwrap()
            .at("messages")
            .unwrap()
            .a()
            .unwrap()
            .iter()
            .map(|m| (m.o().unwrap().str_at("id").unwrap().to_owned(), m.clone()))
            .collect()
    }

    #[test]
    fn online_messages_show_language_strings_and_others_stay_stock() {
        let stock = by_id(&messages_file());
        let ours = by_id(&messages(&messages_file(), true).unwrap());
        assert_eq!(ours.len(), stock.len() + 1);
        assert_eq!(ours.last().unwrap().0, MESSAGE_ID);
        for ((id, m), (_, before)) in ours.iter().zip(&stock) {
            let o = m.o().unwrap();
            if !ONLINE_MESSAGES.contains(&id.as_str()) {
                assert_eq!(canonical(m), canonical(before), "{id} changed");
                continue;
            }
            assert_eq!(o.str_at("title").unwrap(), POPUP_TITLE);
            assert_eq!(o.str_at("messageBody").unwrap(), POPUP_BODY);
            assert!(o.get("titleTokens").is_none() && o.get("bodyTokens").is_none());
            assert_eq!(o.at("options").unwrap().a().unwrap().len(), 1);
        }
        let plain = by_id(&messages(&messages_file(), false).unwrap());
        assert_eq!(plain.len(), stock.len() + 1);
        for ((id, m), (_, before)) in plain.iter().zip(&stock) {
            let (o, b) = (m.o().unwrap(), before.o().unwrap());
            assert_eq!(o.str_at("title").unwrap(), b.str_at("title").unwrap(), "{id}");
            assert_eq!(
                o.str_at("messageBody").unwrap(),
                b.str_at("messageBody").unwrap(),
                "{id}"
            );
        }
        let strings = Popup::for_version("1.2.3").strings();
        assert_eq!(
            strings[0],
            (POPUP_TITLE.to_owned(), "Swofty Mod v1.2.3".to_owned())
        );
        assert_eq!(strings[1].0, POPUP_BODY);
        assert!(strings[1].1.contains("New in v1.2.3"));
    }

    #[test]
    fn the_runtime_message_reads_its_text_from_two_attributes() {
        let ours = by_id(&messages(&messages_file(), false).unwrap());
        let (_, m) = ours.iter().find(|(id, _)| id == MESSAGE_ID).unwrap();
        let o = m.o().unwrap();
        assert_eq!(o.str_at("title").unwrap(), "lng_value_tag");
        assert_eq!(o.str_at("messageBody").unwrap(), "lng_value_tag");
        for (tokens, attr) in [("titleTokens", "title"), ("bodyTokens", "message")] {
            let t = o
                .at(tokens)
                .unwrap()
                .o()
                .unwrap()
                .at("VALUE")
                .unwrap()
                .o()
                .unwrap();
            assert_eq!(t.str_at("name").unwrap(), "VALUE");
            assert_eq!(
                t.str_at("dbEntry").unwrap(),
                format!("{ATTRIBUTE_ROW}[{attr}].Value")
            );
            assert_eq!(canonical(t.get("localise").unwrap()), canonical(&boolv(false)));
        }
        let names: Vec<&str> = o
            .at("attributes")
            .unwrap()
            .a()
            .unwrap()
            .iter()
            .map(|a| a.o().unwrap().str_at("name").unwrap())
            .collect();
        assert_eq!(names, ["title", "message"]);
        let options = o.at("options").unwrap().a().unwrap();
        assert_eq!(options.len(), 1);
        assert_eq!(options[0].o().unwrap().str_at("response").unwrap(), "ok");
        let twice = by_id(&messages(&messages(&messages_file(), false).unwrap(), false).unwrap());
        assert_eq!(twice.iter().filter(|(id, _)| id == MESSAGE_ID).count(), 1);
    }

    fn hud_message(id: &str) -> Node {
        obj(vec![
            ("id", s(id)),
            ("type", s("hud_main")),
            ("playerFilter", s("exclusive")),
            ("priority", u32v(2)),
            (
                "attributes",
                arr(vec![toast_attribute("message", s(&format!("lng_{id}")))]),
            ),
            ("options", arr(vec![])),
            ("queue", s("hud_main_queue")),
        ])
    }

    #[test]
    fn toasts_are_hud_main_messages_with_literal_text_and_a_fixed_duration() {
        let stock = Pkg {
            name: [0; 4],
            root: obj(vec![(
                "messages",
                arr(vec![hud_message("drs_enabled"), hud_message(TOAST_TEMPLATE)]),
            )]),
            vca: Vec::new(),
            layout: None,
        }
        .to_bytes()
        .unwrap();
        let ours = by_id(&hud_messages(&stock).unwrap());
        assert_eq!(ours.len(), 2 + TOAST_SECONDS.len());
        for (i, &n) in TOAST_SECONDS.iter().enumerate() {
            let (id, m) = &ours[2 + i];
            assert_eq!(id, &format!("{TOAST_PREFIX}{n}"));
            let o = m.o().unwrap();
            assert_eq!(o.str_at("type").unwrap(), "hud_main");
            assert_eq!(o.str_at("queue").unwrap(), "hud_main_queue");
            let attrs = o.at("attributes").unwrap().a().unwrap();
            let names: Vec<&str> = attrs
                .iter()
                .map(|a| a.o().unwrap().str_at("name").unwrap())
                .collect();
            assert_eq!(
                names,
                [
                    "message",
                    "sub_message",
                    "msg_duration",
                    "flag",
                    "enabled_in_spectator"
                ]
            );
            for a in attrs {
                let a = a.o().unwrap();
                assert_eq!(canonical(a.get("localise").unwrap()), canonical(&boolv(false)));
            }
            let duration = attrs[2].o().unwrap().get("value").unwrap();
            assert_eq!(canonical(duration), canonical(&f32v(n as f32)));
        }
        assert_eq!(canonical(&ours[0].1), canonical(&hud_message("drs_enabled")));
        let twice = by_id(&hud_messages(&hud_messages(&stock).unwrap()).unwrap());
        assert_eq!(twice.len(), ours.len());
    }

    fn routes_file() -> Vec<u8> {
        let options = route("options", vec![], "", 0, None);
        Pkg {
            name: [0; 4],
            root: obj(vec![(
                "RouteGroupings",
                arr(vec![
                    route_group("fe_home", vec![options]),
                    route_group("settings_container", vec![]),
                ]),
            )]),
            vca: Vec::new(),
            layout: None,
        }
        .to_bytes()
        .unwrap()
    }

    fn group<'a>(p: &'a Pkg, id: &str) -> Option<&'a Node> {
        p.root
            .o()
            .unwrap()
            .at("RouteGroupings")
            .unwrap()
            .a()
            .unwrap()
            .iter()
            .find(|g| id_of(g) == Some(id))
    }

    fn route_ids(g: &Node) -> Vec<String> {
        g.o()
            .unwrap()
            .at("Routes")
            .unwrap()
            .a()
            .unwrap()
            .iter()
            .map(|r| id_of(r).unwrap().to_owned())
            .collect()
    }

    #[test]
    fn addon_screens_get_route_groups_and_an_open_route_on_their_anchor() {
        let screens = [("ai".to_owned(), "settings_container".to_owned())];
        let d = routes(&routes_file(), route_groups(&[]), screen_route_groups(&screens)).unwrap();
        // a group already there is kept, not added twice
        let d = routes(&d, Vec::new(), screen_route_groups(&screens)).unwrap();
        let opened = [
            (
                "ai".to_owned(),
                "settings_container".to_owned(),
                "swofty_addon_ai_3".to_owned(),
            ),
            ("x".to_owned(), "no_such_anchor".to_owned(), "x".to_owned()),
        ];
        let d = anchor_routes(&anchor_routes(&d, &opened).unwrap(), &opened).unwrap();
        let p = Pkg::parse(&d).unwrap();
        let ai: Vec<&Node> = p
            .root
            .o()
            .unwrap()
            .at("RouteGroupings")
            .unwrap()
            .a()
            .unwrap()
            .iter()
            .filter(|g| id_of(g) == Some("ai"))
            .collect();
        assert_eq!(ai.len(), 1);
        assert_eq!(route_ids(ai[0]), ["open", "open_no_input", "close", "back"]);
        let back = canonical(&ai[0].o().unwrap().at("Routes").unwrap().a().unwrap()[3]);
        assert_eq!(
            back,
            canonical(&subpage_route("back", "ai", "settings_container", 2))
        );
        assert_eq!(route_ids(group(&p, "settings_container").unwrap()), ["open_ai"]);
        assert!(group(&p, "no_such_anchor").is_none());
    }

    fn setup() -> SetupRoutes {
        SetupRoutes {
            route: "career_wizard_driver_career_pg".into(),
            home: "swofty_setup_career_wizard_driver_career_pg".into(),
            rows: "swofty_addon_career_wizard_driver_career_pg_2".into(),
            next: "career_wizard_season_length_select".into(),
            next_flow: "career_season_setup_flow".into(),
            back: "fe_career_dlc_select".into(),
            back_flow: "frontend_flow".into(),
            back_route: "back_career_wizard_driver_career_pg".into(),
        }
    }

    fn attr_pairs(list: &Node) -> Vec<(String, String)> {
        list.a()
            .unwrap()
            .iter()
            .map(|a| {
                let a = a.o().unwrap();
                (
                    a.str_at("Id").unwrap().to_owned(),
                    a.str_at("Value").unwrap().to_owned(),
                )
            })
            .filter(|(k, _)| k != "Id")
            .collect()
    }

    fn command_attrs(r: &Node) -> Vec<(String, Vec<(String, String)>)> {
        r.o()
            .unwrap()
            .at("Commands")
            .unwrap()
            .a()
            .unwrap()
            .iter()
            .map(|c| {
                let o = c.o().unwrap();
                (
                    o.str_at("Type").unwrap().to_owned(),
                    attr_pairs(o.at("Attributes").unwrap()),
                )
            })
            .collect()
    }

    fn route_by_id<'a>(g: &'a Node, id: &str) -> &'a Node {
        g.o()
            .unwrap()
            .at("Routes")
            .unwrap()
            .a()
            .unwrap()
            .iter()
            .find(|r| id_of(r) == Some(id))
            .unwrap()
    }

    fn pairs(v: &[(&str, &str)]) -> Vec<(String, String)> {
        v.iter()
            .map(|(a, b)| ((*a).to_owned(), (*b).to_owned()))
            .collect()
    }

    fn file(groups: Vec<Node>) -> Vec<u8> {
        Pkg {
            name: [0; 4],
            root: obj(vec![("RouteGroupings", arr(groups))]),
            vca: Vec::new(),
            layout: None,
        }
        .to_bytes()
        .unwrap()
    }

    #[test]
    fn a_career_setup_gets_an_entry_route_a_page_pair_and_a_way_back_from_the_wizard() {
        let s = setup();
        let to_f1 = route(
            SETUP_ENTRY_SOURCE,
            vec![
                route_cmd("InputState", 1, &[], None),
                route_cmd(
                    "TriggerSubRoute",
                    2,
                    &[("SubRoute", "close"), ("Target", &s.back), ("To", &s.next)],
                    Some("1"),
                ),
                route_cmd(
                    "Migrate",
                    3,
                    &[("Flow", "career_season_setup_flow"), ("SkipActivator", "true")],
                    Some("2"),
                ),
                route_cmd(
                    "TriggerSubRoute",
                    4,
                    &[
                        ("SubRoute", "open_no_input"),
                        ("Target", &s.next),
                        ("From", &s.back),
                    ],
                    Some("3"),
                ),
            ],
            "1,4",
            1,
            Some(&s.next),
        );
        let frontend = file(vec![route_group(&s.back, vec![to_f1.clone()])]);
        let mut next_group = route_group(&s.next, screen_routes(&s.next));
        next_group
            .o_mut()
            .unwrap()
            .at_mut("Attributes")
            .unwrap()
            .a_mut()
            .unwrap()
            .push(obj(vec![
                ("Id", super::s("Slices")),
                ("Value", super::s("career_wizard_summary,slice_gen_screen_title")),
            ]));
        let wizard = file(vec![next_group]);
        let mut placed = Placed::default();
        let frontend = setup_routes(&frontend, &[setup()], &mut placed).unwrap();
        let wizard = setup_routes(&wizard, &[setup()], &mut placed).unwrap();
        assert_eq!(
            placed.routes,
            [
                format!("{}.{}", s.back, s.route),
                format!("{}.{}", s.next, s.back_route)
            ]
        );
        assert_eq!(
            placed.screens,
            ["career_wizard_summary", "slice_gen_screen_title"]
        );
        let scene = pairs(&SETUP_SCENE);

        let p = Pkg::parse(&frontend).unwrap();
        let dlc = group(&p, &s.back).unwrap();
        assert_eq!(route_ids(dlc), [SETUP_ENTRY_SOURCE, s.route.as_str()]);
        assert_eq!(canonical(route_by_id(dlc, SETUP_ENTRY_SOURCE)), canonical(&to_f1));
        let entry = route_by_id(dlc, &s.route);
        assert_eq!(
            command_attrs(entry),
            [
                ("InputState".to_owned(), vec![]),
                (
                    "TriggerSubRoute".to_owned(),
                    pairs(&[("SubRoute", "close"), ("Target", &s.back), ("To", &s.home)])
                ),
                (
                    "Migrate".to_owned(),
                    pairs(&[("Flow", "options_flow"), ("SkipActivator", "true")])
                ),
                (
                    "TriggerSubRoute".to_owned(),
                    pairs(&[("SubRoute", "open"), ("Target", &s.home), ("From", &s.back)])
                ),
                ("FrontendSceneSwitch".to_owned(), scene.clone()),
            ]
        );
        let switch = &entry.o().unwrap().at("Commands").unwrap().a().unwrap()[4];
        assert_eq!(switch.o().unwrap().str_at("Dependencies").unwrap(), "1");
        assert_eq!(
            attr_pairs(entry.o().unwrap().at("Attributes").unwrap()),
            pairs(&[("Destination", &s.home)])
        );

        let p = Pkg::parse(&wizard).unwrap();
        let back = route_by_id(group(&p, &s.next).unwrap(), &s.back_route);
        let out = |t: &str| {
            (
                "TransitionScreen".to_owned(),
                pairs(&[("Direction", "out"), ("Target", t)]),
            )
        };
        let close = |t: &str| ("CloseScreen".to_owned(), pairs(&[("Target", t)]));
        assert_eq!(
            command_attrs(back),
            [
                out(&s.next),
                out("career_wizard_summary"),
                close("career_wizard_summary"),
                out("slice_gen_screen_title"),
                close("slice_gen_screen_title"),
                (
                    "Migrate".to_owned(),
                    pairs(&[("Flow", "options_flow"), ("SkipActivator", "true")])
                ),
                (
                    "TriggerSubRoute".to_owned(),
                    pairs(&[
                        ("SubRoute", "open"),
                        ("Target", &s.home),
                        ("From", &s.next),
                        ("Message", "")
                    ])
                ),
                ("FrontendSceneSwitch".to_owned(), scene.clone()),
            ]
        );
        let deps: Vec<&str> = back
            .o()
            .unwrap()
            .at("Commands")
            .unwrap()
            .a()
            .unwrap()
            .iter()
            .map(|c| c.o().unwrap().str_at("Dependencies").unwrap_or(""))
            .collect();
        assert_eq!(deps, ["", "", "2", "", "4", "1,3,5", "6", "6"]);
        let back = back.o().unwrap();
        assert_eq!(back.str_at("Dependencies").unwrap(), "7");
        assert_eq!(
            attr_pairs(back.at("Attributes").unwrap()),
            pairs(&[("Destination", &s.home), ("AutoFocus", "false")])
        );

        let groups = setup_route_groups(&s);
        assert_eq!(
            groups.iter().map(|g| id_of(g).unwrap()).collect::<Vec<_>>(),
            [s.home.as_str(), s.rows.as_str()]
        );
        assert_eq!(
            route_ids(&groups[0]),
            [
                "open",
                "open_no_input",
                "close",
                "back",
                "confirm",
                "open_options"
            ]
        );
        assert_eq!(route_ids(&groups[1]), ["open", "open_no_input", "close", "back"]);
        assert_eq!(
            attr_pairs(
                route_by_id(&groups[0], "back")
                    .o()
                    .unwrap()
                    .at("Attributes")
                    .unwrap()
            ),
            pairs(&[("Destination", &s.back), ("AutoFocus", "false")])
        );
        let back = command_attrs(route_by_id(&groups[0], "back"));
        assert_eq!(
            back[1],
            (
                "Migrate".to_owned(),
                pairs(&[("Flow", "frontend_flow"), ("SkipActivator", "true")])
            )
        );
        assert_eq!(
            back[2].1,
            pairs(&[
                ("SubRoute", "open"),
                ("Target", &s.back),
                ("From", &s.home),
                ("Message", DRIVER_CAREER)
            ])
        );
        let confirm = command_attrs(route_by_id(&groups[0], "confirm"));
        assert_eq!(
            confirm[1].1,
            pairs(&[("Flow", "career_season_setup_flow"), ("SkipActivator", "true")])
        );
        assert_eq!(confirm[2].1[1], ("Target".to_owned(), s.next.clone()));
        assert_eq!(
            canonical(route_by_id(&groups[1], "back")),
            canonical(&subpage_route("back", &s.rows, &s.home, 2))
        );

        let e = setup_routes(&frontend, &[setup()], &mut Placed::default()).unwrap_err();
        assert!(
            e.to_string()
                .contains(&format!("{} has a route {} already", s.back, s.route)),
            "{e}"
        );
        let bare = file(vec![route_group(&s.back, vec![])]);
        let e = setup_routes(&bare, &[setup()], &mut Placed::default()).unwrap_err();
        assert!(e.to_string().contains(SETUP_ENTRY_SOURCE), "{e}");
    }
}
