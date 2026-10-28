use std::collections::HashMap;

use swofty_formats::pkg::{Node, Pkg, Val, Value};

use super::node::*;
use crate::Result;
use crate::manifest::{Binds, CareerSetup, OptionType};
use crate::settings::{SessionAddon, SessionMap, SessionMod, SessionOption};

pub const HOME: &str = "swofty_settings";
pub const STATE: &str = "#ui_menu_data.swofty";
pub const HOME_LAYOUT: &str = "eaid://ui_package/art/screens/options/options_screen";
pub const PAGE_LAYOUT: &str = "eaid://ui_package/art/screens/options/options_edit_screen";
pub const ADJUST_TOOLTIP: &str = "swofty_adjust";
const TAG_STYLE: &str = "18_regular_upper_part_neg";
const LIGHT_TAG: &str = "17_regular_uber";
const SETTINGS_ICON: &str = "ui_package/art/textures/options/o_category/";
/// Columns of tiles ADDON SETTINGS keeps on screen; past them the row
/// scrolls by hiding the column at the other edge, which the stacker closes up.
const VISIBLE_COLUMNS: usize = 3;

pub struct TileStyle {
    pub rows: usize,
    pub slot: fn(usize) -> &'static str,
    pub icon: fn(Kind) -> Icon,
    pub attach: &'static [&'static str],
    pub hide_divider: bool,
    pub by_gap: bool,
    pub wrap: Option<(usize, usize)>,
}

pub enum Icon {
    Texture(&'static str),
    Ref(&'static str),
    Path(&'static str),
}

pub const TILES: TileStyle = TileStyle {
    rows: 3,
    slot: |_| "1a",
    icon: |k| match k {
        Kind::Addon => Icon::Ref("#overrides.icons.session.settings"),
        Kind::Map | Kind::Mod => {
            Icon::Path("eaid://ui_package/art/textures/custom_track_select/race_completed_icon.tif")
        }
    },
    attach: &["title_1", "stacker_title"],
    hide_divider: true,
    by_gap: true,
    wrap: Some((24, 32)),
};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Kind {
    Addon,
    Map,
    Mod,
}

impl Kind {
    pub fn key(self) -> &'static str {
        match self {
            Kind::Addon => "addon",
            Kind::Map => "map",
            Kind::Mod => "mod",
        }
    }

    fn tag(self) -> &'static str {
        match self {
            Kind::Addon => "ADDON",
            Kind::Map | Kind::Mod => "MOD",
        }
    }
}

pub struct Entry<'a> {
    pub kind: Kind,
    pub index: usize,
    pub screen: String,
    pub name: &'a str,
    pub author: &'a str,
    pub version: &'a str,
    pub description: &'a str,
    pub id: &'a str,
    pub options: Vec<Opt<'a>>,
    /// Every option of the addon, hidden ones too, for its career setup page.
    pub all: Vec<Opt<'a>>,
    pub setup: Option<&'a CareerSetup>,
}

pub struct Opt<'a> {
    pub k: usize,
    pub local: usize,
    pub o: &'a SessionOption,
}

impl Entry<'_> {
    fn details(&self) -> String {
        let author = if self.author.is_empty() {
            "unknown"
        } else {
            self.author
        };
        let version = if self.version.is_empty() {
            "?"
        } else {
            self.version
        };
        format!("by {author}, v{version}")
    }

    fn upper(&self) -> String {
        self.name.to_uppercase()
    }
}

pub fn entries<'a>(
    addons: &'a [SessionAddon],
    maps: &'a [SessionMap],
    mods: &'a [SessionMod],
) -> Vec<Entry<'a>> {
    let mut k = 0;
    let mut out = Vec::new();
    for (i, (full, m)) in addons.iter().enumerate().filter(|(_, m)| m.enabled).enumerate() {
        let all: Vec<Opt> = m
            .options
            .iter()
            .enumerate()
            .map(|(local, o)| {
                k += 1;
                Opt { k: k - 1, local, o }
            })
            .collect();
        let options = all
            .iter()
            .filter(|o| !o.o.hidden)
            .map(|o| Opt {
                k: o.k,
                local: o.local,
                o: o.o,
            })
            .collect();
        out.push(Entry {
            kind: Kind::Addon,
            index: i,
            screen: format!("swofty_addon_{full}"),
            name: &m.name,
            author: &m.author,
            version: &m.version,
            description: &m.description,
            id: &m.id,
            options,
            all,
            setup: m.career_setup.as_ref(),
        });
    }
    for (j, mp) in maps.iter().enumerate() {
        out.push(Entry {
            kind: Kind::Map,
            index: j,
            screen: format!("swofty_map_{j}"),
            name: &mp.name,
            author: &mp.author,
            version: &mp.version,
            description: &mp.description,
            id: &mp.code,
            options: Vec::new(),
            all: Vec::new(),
            setup: None,
        });
    }
    for (j, md) in mods.iter().enumerate() {
        out.push(Entry {
            kind: Kind::Mod,
            index: j,
            screen: format!("swofty_mod_{j}"),
            name: &md.name,
            author: &md.author,
            version: &md.version,
            description: &md.description,
            id: &md.id,
            options: Vec::new(),
            all: Vec::new(),
            setup: None,
        });
    }
    out
}

fn tagged(text: &str, tag: &str, style: &str) -> String {
    format!("{text}  {{s:{style}}}{tag}{{/s}}")
}

fn wrap_text(text: &str, width: usize) -> String {
    let mut out = Vec::new();
    for para in text.split('\n') {
        let mut line = String::new();
        for word in para.split(' ') {
            if !line.is_empty() && line.chars().count() + 1 + word.chars().count() > width {
                out.push(std::mem::replace(&mut line, word.to_owned()));
            } else if line.is_empty() {
                line = word.to_owned();
            } else {
                line = format!("{line} {word}");
            }
        }
        out.push(line);
    }
    out.join("\n")
}

fn label_case(s: &str) -> String {
    s.split(' ')
        .map(|w| {
            let mut c = w.chars();
            match c.next() {
                Some(f) => f.to_uppercase().chain(c).collect(),
                None => String::new(),
            }
        })
        .collect::<Vec<_>>()
        .join(" ")
}

pub struct Sources {
    pub templates: HashMap<String, Node>,
    pub career: Node,
    pub home: Node,
    pub container: Node,
}

impl Sources {
    pub fn parse(syst: &[u8], career: &[u8], home: &[u8], container: &[u8]) -> Result<Self> {
        let mut templates = HashMap::new();
        let p = Pkg::parse(syst)?;
        for t in p.root.o()?.at("Templates")?.a()? {
            templates.insert(t.name().to_owned(), t.clone());
        }
        Ok(Self {
            templates,
            career: first_screen_of(career)?,
            home: first_screen_of(home)?,
            container: first_screen_of(container)?,
        })
    }

    fn t(&self, name: &str) -> Result<&Node> {
        self.templates
            .get(name)
            .ok_or_else(|| err(format!("the settings templates have no {name}")))
    }

    fn frame(&self, sname: &str) -> Result<Frame> {
        frame(
            sname,
            &FrameSources {
                action: self.career.child("Action")?,
                audio: self.home.child("audio_theme_trigger")?,
                back: self.home.child("back_listener")?,
                tooltips: self.career.child("tooltips")?,
            },
            &["select", "back"],
        )
    }

    fn screen(&self, name: &str, layout: &str) -> Result<Node> {
        screen(name, layout, self.career.props()?)
    }

    fn title(&self, sname: &str, panel: &str, text: &str) -> Result<Node> {
        let mut t = self.t(panel)?.child("screen_title")?.clone();
        rename_refs(
            &mut t,
            &format!("{panel}.screen_title"),
            &format!("{sname}.screen_title"),
        );
        strip_data(&mut t)?;
        literal_label(t.child_mut("title")?, text)?;
        literal_label(t.child_mut("hidden_settings_text")?, "")?;
        for c in t.slot_mut(CHILDREN)? {
            if matches!(c.name(), "hidden_settings_bg" | "hidden_settings_text") {
                set_bool(c, "visible", false)?;
            }
        }
        Ok(t)
    }
}

pub(super) fn as_group(n: &mut Node) -> Result<()> {
    let keep = [
        "id", "version", "type", "nameId", "name", "animData", "enabled", "visible",
    ];
    let p = n.props_mut()?;
    p.items
        .retain(|(k, _)| k.as_str().is_some_and(|k| keep.contains(&k)));
    p.at_mut("type")?.v_mut()?.set_u32(fourcc("UIGP"));
    n.a_mut()?[BEHAVIOURS] = behaviours(GROUP_BEHAVIOURS);
    Ok(())
}

fn adjust_tip(fr: &mut Frame) -> Result<()> {
    let old = fr.tooltips_old.clone();
    let tips = fr.tooltips.slot_mut(CHILDREN)?;
    let mut c = tips
        .iter()
        .find(|c| c.name() == "select")
        .cloned()
        .ok_or_else(|| err("the tooltips have no select"))?;
    rename_refs(&mut c, &format!("{old}.select"), &format!("{old}.adjust"));
    rename(&mut c, "adjust")?;
    set_prop_value(&mut c, "tooltipsName", Val::str(ADJUST_TOOLTIP))?;
    tips.retain(|x| x.name() != "select");
    tips.push(c);
    Ok(())
}

fn panel(src: &Sources, sname: &str, e: &Entry, first_title: &str, first_body: &str) -> Result<[Node; 2]> {
    let body = [e.details(), e.description.to_owned()]
        .into_iter()
        .filter(|x| !x.is_empty())
        .collect::<Vec<_>>()
        .join("\n");
    let upper = e.upper();
    let mut cat = instance(
        src.t("settings_edit_description_category")?,
        sname,
        "about",
        &[("title", &upper), ("body", &body)],
        true,
    )?;
    for c in cat.slot_mut(CHILDREN)? {
        if c.name() == "icon" {
            set_bool(c, "visible", false)?;
        }
    }
    let mut panel = instance(
        src.t("settings_edit_description_panel")?,
        sname,
        "description",
        &[],
        true,
    )?;
    let set = instance(
        src.t("settings_edit_description_set")?,
        &format!("{sname}.description"),
        "set",
        &[("title", first_title), ("body", first_body)],
        true,
    )?;
    panel.slot_mut(CHILDREN)?.push(set);
    Ok([cat, panel])
}

struct Row<'a> {
    key: String,
    kind: RowKind<'a>,
    label: String,
    title: String,
    body: String,
}

enum RowKind<'a> {
    Info(String),
    Slider(&'a Opt<'a>),
    Toggle(&'a Opt<'a>),
}

fn addon_rows<'a>(e: &'a Entry<'a>) -> Vec<Row<'a>> {
    if e.options.is_empty() {
        return vec![
            Row {
                key: "row_id".into(),
                kind: RowKind::Info(e.id.into()),
                label: "Addon ID".into(),
                title: "ADDON ID".into(),
                body: "This addon has no options.".into(),
            },
            Row {
                key: "row_version".into(),
                kind: RowKind::Info(e.version.into()),
                label: "Version".into(),
                title: "VERSION".into(),
                body: String::new(),
            },
        ];
    }
    e.options
        .iter()
        .map(|opt| {
            let o = opt.o;
            let restart = if o.binds == Binds::Db {
                " Applies after a restart."
            } else {
                ""
            };
            Row {
                key: format!("row_{}", opt.k),
                kind: if o.kind == OptionType::Slider {
                    RowKind::Slider(opt)
                } else {
                    RowKind::Toggle(opt)
                },
                label: label_case(&o.label),
                title: o.label.to_uppercase(),
                body: format!("{}{restart}", o.description).trim().to_owned(),
            }
        })
        .collect()
}

fn map_rows<'a>(e: &Entry) -> Vec<Row<'a>> {
    let info = |key: &str, label: &str, value: &str, title: &str, body: &str| Row {
        key: key.into(),
        kind: RowKind::Info(value.into()),
        label: label.into(),
        title: title.into(),
        body: body.into(),
    };
    vec![
        info(
            "row_code",
            "Track Code",
            e.id,
            "TRACK CODE",
            "Pick this track in any mode to race on it.",
        ),
        info("row_author", "Author", e.author, "AUTHOR", ""),
        info("row_version", "Version", e.version, "VERSION", ""),
    ]
}

fn mod_rows<'a>(e: &Entry) -> Vec<Row<'a>> {
    let info = |key: &str, label: &str, value: &str, title: &str, body: &str| Row {
        key: key.into(),
        kind: RowKind::Info(value.into()),
        label: label.into(),
        title: title.into(),
        body: body.into(),
    };
    vec![
        info(
            "row_id",
            "Mod ID",
            e.id,
            "MOD ID",
            "Enabled by its addon. Turn it on or off from the addon's page in the launcher.",
        ),
        info("row_author", "Author", e.author, "AUTHOR", ""),
        info("row_version", "Version", e.version, "VERSION", ""),
    ]
}

fn state_value(o: &SessionOption) -> Result<&crate::settings::SessionState> {
    o.states
        .get(o.current)
        .ok_or_else(|| err(format!("option {} has no state {}", o.id, o.current)))
}

fn number(v: &serde_json::Value) -> Result<f32> {
    v.as_f64()
        .map(|x| x as f32)
        .ok_or_else(|| err(format!("slider value {v} is not a number")))
}

fn build_page(src: &Sources, e: &Entry, rows: &[Row], adjustable: bool) -> Result<Node> {
    let sname = e.screen.as_str();
    let scr = src.screen(sname, PAGE_LAYOUT)?;
    let mut fr = src.frame(sname)?;
    if adjustable {
        adjust_tip(&mut fr)?;
    } else {
        fr.tooltips.slot_mut(CHILDREN)?.retain(|x| x.name() != "select");
    }
    let mut kids = vec![
        src.title(sname, "settings_edit_screen", &e.upper())?,
        hidden("hide_filter", &["filter"]),
        hidden("hide_scroll_bar", &["scroll_bar"]),
    ];
    let first = rows.first().ok_or_else(|| err(format!("{sname} has no rows")))?;
    kids.extend(panel(src, sname, e, &first.title, &first.body)?);
    let mut rows_at = Vec::new();
    for r in rows {
        let key = r.key.as_str();
        let mut t = match &r.kind {
            RowKind::Info(value) => {
                let mut t = instance(
                    src.t("settings_edit_toggle")?,
                    sname,
                    key,
                    &[("toggle.label", &r.label), ("toggle.value", value)],
                    true,
                )?;
                let tg = t.child_mut("toggle")?;
                as_group(tg)?;
                for c in tg.slot_mut(CHILDREN)? {
                    if matches!(c.name(), "indicator" | "left_arrow" | "right_arrow") {
                        set_bool(c, "visible", false)?;
                    }
                }
                t
            }
            RowKind::Slider(opt) => {
                let o = opt.o;
                let cur = state_value(o)?;
                let mut t = instance(
                    src.t("settings_edit_slider")?,
                    sname,
                    key,
                    &[("slider.label", &r.label), ("slider.value", &cur.text)],
                    true,
                )?;
                let vals = o
                    .states
                    .iter()
                    .map(|s| number(&s.value))
                    .collect::<Result<Vec<_>>>()?;
                let sl = t.child_mut("slider")?;
                let p = sl.props_mut()?;
                p.drop_key("dataContext");
                p.set(
                    "value",
                    prop(f32v(number(&cur.value)?), Some(&format!("{STATE}.s{}", opt.k))),
                );
                p.set("min", prop(f32v(vals[0]), None));
                p.set(
                    "max",
                    prop(
                        f32v(
                            *vals
                                .last()
                                .ok_or_else(|| err(format!("option {} has no states", o.id)))?,
                        ),
                        None,
                    ),
                );
                p.set("step", prop(f32v(1.0), None));
                let c = o.current;
                set_bool(sl.child_mut("left_toggle")?, "enabled", c > 0)?;
                set_bool(sl.child_mut("right_toggle")?, "enabled", c + 1 < vals.len())?;
                set_bool(sl.child_mut("indicator")?, "visible", false)?;
                t
            }
            RowKind::Toggle(opt) => {
                let o = opt.o;
                let c = o.current;
                let cur = state_value(o)?;
                let mut t = instance(
                    src.t("settings_edit_toggle")?,
                    sname,
                    key,
                    &[("toggle.label", &r.label), ("toggle.value", &cur.text)],
                    true,
                )?;
                let tg = t.child_mut("toggle")?;
                as_group(tg)?;
                set_bool(tg.child_mut("indicator")?, "visible", false)?;
                set_bool(tg.child_mut("left_arrow")?, "enabled", c > 0)?;
                set_bool(tg.child_mut("right_arrow")?, "enabled", c + 1 < o.states.len())?;
                t
            }
        };
        let mut evs = vec![event(
            "focused",
            vec![
                sig_with(
                    &format!("{sname}.description.set.title"),
                    "setText",
                    "focused",
                    &[("text", r.title.as_str().into())],
                ),
                sig_with(
                    &format!("{sname}.description.set.body"),
                    "setText",
                    "focused",
                    &[("text", r.body.as_str().into())],
                ),
            ],
        )];
        if let RowKind::Slider(opt) | RowKind::Toggle(opt) = &r.kind {
            let k = opt.k;
            evs.push(event(
                "navRight",
                vec![sig(&format!("{sname}.o{k}_next"), "query", "navRight")],
            ));
            evs.push(event(
                "navLeft",
                vec![sig(&format!("{sname}.o{k}_prev"), "query", "navLeft")],
            ));
        }
        add_events(&mut t, evs)?;
        rows_at.push(kids.len());
        kids.push(t);
    }
    chain_focus(sname, &mut kids, &rows_at)?;
    let first = kids[rows_at[0]].name().to_owned();
    finish(scr, fr, kids, &first)
}

fn retitle(scr: &mut Node, e: &Entry) -> Result<()> {
    let upper = e.upper();
    let tag = e.kind.tag();
    set_text_value(
        scr.child_mut("screen_title")?.child_mut("title")?,
        &tagged(&upper, tag, LIGHT_TAG),
    )?;
    set_text_value(
        scr.child_mut("about")?.child_mut("title")?,
        &tagged(&upper, tag, TAG_STYLE),
    )
}

fn bind_label(n: &mut Node, path: &str) -> Result<()> {
    let p = n.props_mut()?;
    p.set("text", obj(vec![("value", s("")), ("dbentry", s(path))]));
    p.set("localise", prop(boolv(false), None));
    Ok(())
}

fn vm_page(scr: &mut Node, e: &Entry) -> Result<()> {
    let sname = scr.name().to_owned();
    set_prop(scr, "allowVMBinding", prop(boolv(true), None))?;
    for opt in &e.options {
        let o = opt.o;
        let row = scr.child_mut(&format!("row_{}", opt.k))?;
        let loc = format!("o{}", opt.local);
        let cmd = format!("{}:{}", e.id, o.id);
        let slider = o.kind == OptionType::Slider;
        let sound = if slider { "slider" } else { "toggle" };
        let (ctl, arrows) = if slider {
            let ctl = row.child_mut("slider")?;
            let p = ctl.props_mut()?;
            p.set("dataContext", obj(vec![("value", s("")), ("dbentry", s(&loc))]));
            p.set("value", prop(i32v(0), Some(&format!("{loc}.value"))));
            p.set("min", prop(i32v(0), Some(&format!("{loc}.min"))));
            p.set("max", prop(i32v(1), Some(&format!("{loc}.max"))));
            p.set("step", prop(i32v(1), None));
            (ctl, ["left_toggle", "right_toggle"])
        } else {
            (row.child_mut("toggle")?, ["left_arrow", "right_arrow"])
        };
        let (label, text) = if slider {
            ("label".to_owned(), "text".to_owned())
        } else {
            (format!("{loc}.label"), format!("{loc}.text"))
        };
        bind_label(ctl.child_mut("label")?, &label)?;
        bind_label(ctl.child_mut("value")?, &text)?;
        for a in arrows {
            set_bool(ctl.child_mut(a)?, "enabled", true)?;
        }
        for ev in scr.child_mut(&format!("row_{}", opt.k))?.slot_mut(EVENTS)? {
            let o = ev.o_mut()?;
            let nm = o.str_at("eventName")?.to_owned();
            let verb = match nm.as_str() {
                "navRight" => "next",
                "navLeft" => "prev",
                _ => continue,
            };
            let c = format!("swofty_{verb}:{cmd}");
            *o.at_mut("signals")?.a_mut()? = vec![
                sig(&format!("{sname}.audio_theme_trigger"), sound, &nm),
                raise_cmd(&sname, &nm, &c, &c),
            ];
        }
    }
    fix_ids(scr, None);
    Ok(())
}

fn tile(src: &Sources, style: &TileStyle, parent: &str, name: &str, row: usize, e: &Entry) -> Result<Node> {
    let title = tagged(&e.upper(), e.kind.tag(), TAG_STYLE);
    let text = TileText {
        title: &title,
        by: &e.details(),
        description: e.description,
    };
    tile_with(src, style, parent, name, row, (style.icon)(e.kind), &text)
}

struct TileText<'a> {
    title: &'a str,
    by: &'a str,
    description: &'a str,
}

fn tile_with(
    src: &Sources,
    style: &TileStyle,
    parent: &str,
    name: &str,
    row: usize,
    icon: Icon,
    text: &TileText,
) -> Result<Node> {
    let mut t = instance(
        src.t("settings_home_category")?,
        parent,
        name,
        &[("title", text.title), ("subtitle", "")],
        true,
    )?;
    t.a_mut()?[LAYOUT] = layout_path(&[(style.slot)(row)]);
    for c in t.slot_mut(CHILDREN)? {
        if c.name() == "icon" {
            let p = c.props_mut()?;
            match icon {
                Icon::Ref(r) => p.set("textureOverride", prop(s(""), Some(r))),
                Icon::Path(path) => p.set("textureOverride", prop(s(path), None)),
                Icon::Texture(name) => p.set(
                    "textureOverride",
                    prop(s(&format!("{SETTINGS_ICON}{name}.tif")), None),
                ),
            }
            p.set("visible", prop(boolv(true), None));
        }
    }
    let (by, description) = match style.wrap {
        Some((by_width, desc_width)) => (
            wrap_text(text.by, by_width),
            wrap_text(text.description, desc_width),
        ),
        None => (text.by.to_owned(), text.description.to_owned()),
    };
    let gap = if style.by_gap && !by.is_empty() && !description.is_empty() {
        "\n"
    } else {
        ""
    };
    let mut info = instance(
        src.t("settings_edit_description_set")?,
        &format!("{parent}.{name}"),
        "info",
        &[("title", &format!("{by}{gap}")), ("body", &description)],
        true,
    )?;
    info.a_mut()?[LAYOUT] = layout_path(style.attach);
    // Like the stock SETTINGS tiles, the details show only on the focused tile.
    set_bool(&mut info, "visible", false)?;
    let info_path = format!("{parent}.{name}.info");
    add_events(
        &mut t,
        vec![
            event("focused", vec![sig(&info_path, "show", "focused")]),
            event("unfocused", vec![sig(&info_path, "hide", "unfocused")]),
        ],
    )?;
    for c in info.slot_mut(CHILDREN)? {
        match c.name() {
            "title" => rename(c, "by")?,
            "body" => rename(c, "desc")?,
            _ => {}
        }
        set_bool(c, "visible", true)?;
    }
    if style.hide_divider {
        info.slot_mut(CHILDREN)?
            .push(hidden("hide_divider", &["divider"]));
    }
    t.slot_mut(CHILDREN)?.push(info);
    Ok(t)
}

fn build_home(src: &Sources, style: &TileStyle, flow: &[Entry]) -> Result<Node> {
    let scr = src.screen(HOME, HOME_LAYOUT)?;
    let fr = src.frame(HOME)?;
    let mut kids = vec![src.title(HOME, "settings_home_screen", "ADDON SETTINGS")?];
    for n in ["filter", "side_panel", "scroll_bar", "scroll_bar_panel"] {
        kids.push(hidden(&format!("hide_{n}"), &[n]));
    }
    let mut cols: Vec<Vec<String>> = Vec::new();
    let mut keyed = Vec::new();
    for (c, chunk) in flow.chunks(style.rows).enumerate() {
        let bname = format!("col_{c}");
        let mut block = instance(src.t("settings_home_block")?, HOME, &bname, &[], true)?;
        let parent = format!("{HOME}.{bname}");
        let mut col = Vec::new();
        for (r, e) in chunk.iter().enumerate() {
            let n = c * style.rows + r;
            let tname = format!("t{n}");
            let mut t = tile(src, style, &parent, &tname, r, e)?;
            let path = format!("{parent}.{tname}");
            add_events(
                &mut t,
                vec![event(
                    "pressed",
                    vec![
                        sig(&format!("{HOME}.audio_theme_trigger"), "confirm", "pressed"),
                        sig(&format!("{HOME}.remember_{tname}"), "trigger", "pressed"),
                        sig_with(
                            &format!("{HOME}.Action"),
                            "route",
                            "pressed",
                            &[("key", format!("open_{}_{}", e.kind.key(), e.index).into())],
                        ),
                    ],
                )],
            )?;
            block.slot_mut(CHILDREN)?.push(t);
            col.push(path.clone());
            keyed.push((path, tname, c));
        }
        cols.push(col);
        kids.push(block);
    }
    let first_block = kids.len() - cols.len();
    let scrolls = cols.len() > VISIBLE_COLUMNS;
    let column = |c: usize| format!("{HOME}.col_{c}");
    for (ci, col) in cols.iter().enumerate() {
        for (ri, _) in col.iter().enumerate() {
            let mut evs = Vec::new();
            if ri > 0 {
                evs.push(event("navUp", vec![sig(&col[ri - 1], "focus", "navUp")]));
            }
            if ri + 1 < col.len() {
                evs.push(event("navDown", vec![sig(&col[ri + 1], "focus", "navDown")]));
            }
            if ci > 0 {
                let left = &cols[ci - 1];
                let mut sigs = vec![sig(&left[ri.min(left.len() - 1)], "focus", "navLeft")];
                if scrolls {
                    sigs.push(sig(&column(ci - 1), "show", "navLeft"));
                }
                evs.push(event("navLeft", sigs));
            }
            if ci + 1 < cols.len() {
                let right = &cols[ci + 1];
                let mut sigs = vec![sig(&right[ri.min(right.len() - 1)], "focus", "navRight")];
                if scrolls && ci + 1 >= VISIBLE_COLUMNS {
                    sigs.push(sig(&column(ci + 1 - VISIBLE_COLUMNS), "hide", "navRight"));
                }
                evs.push(event("navRight", sigs));
            }
            let t = &mut kids[first_block + ci].slot_mut(CHILDREN)?;
            let tname = format!("t{}", ci * style.rows + ri);
            let tile = t
                .iter_mut()
                .find(|x| x.is_node() && x.name() == tname)
                .ok_or_else(|| err(format!("tile {tname} went missing")))?;
            add_events(tile, evs)?;
        }
    }
    let first = keyed
        .first()
        .map(|(p, _, _)| p[HOME.len() + 1..].to_owned())
        .ok_or_else(|| err("ADDON SETTINGS has nothing to list"))?;
    finish_home(scr, fr, kids, &keyed, cols.len(), &first)
}

/// The home screen's tiles are `keyed` as (path, name, column) in `columns`
/// columns. Coming back to a tile scrolls the row so the tile is on screen.
fn finish_home(
    scr: Node,
    fr: Frame,
    mut kids: Vec<Node>,
    keyed: &[(String, String, usize)],
    columns: usize,
    first: &str,
) -> Result<Node> {
    let last = format!("{STATE}.last");
    for (_, key, _) in keyed {
        kids.push(node(
            &format!("remember_{key}"),
            "UIDS",
            vec![
                ("source", prop(s(key), None)),
                ("target", prop(s(""), Some(&last))),
            ],
            vec![],
            &[],
            &[],
        ));
    }
    kids.push(node(
        "restore_focus",
        "MVCD",
        vec![("dbEntry", prop(s(&last), None))],
        keyed
            .iter()
            .map(|(path, key, column)| {
                let mut sigs = Vec::new();
                if columns > VISIBLE_COLUMNS {
                    let shown = column.saturating_sub(VISIBLE_COLUMNS - 1);
                    sigs.extend((0..columns).map(|c| {
                        let pin = if c < shown { "hide" } else { "show" };
                        sig(&format!("{HOME}.col_{c}"), pin, key)
                    }));
                }
                sigs.push(sig(path, "focus", key));
                event(key, sigs)
            })
            .collect(),
        &[],
        &[],
    ));
    let mut scr = finish(scr, fr, kids, first)?;
    for e in scr.child_mut("Action")?.slot_mut(EVENTS)? {
        let o = e.o_mut()?;
        let nm = o.str_at("eventName")?.to_owned();
        if matches!(nm.as_str(), "opened_screen" | "finishedTranIn") {
            o.at_mut("signals")?
                .a_mut()?
                .push(sig(&format!("{HOME}.restore_focus"), "query", &nm));
        }
    }
    fix_ids(&mut scr, None);
    Ok(scr)
}

fn settings_state_matrix(src: &Sources, scr: &mut Node) -> Result<()> {
    let want: Value = src
        .container
        .props()?
        .at("animData")?
        .o()?
        .at("stateMatrix")?
        .o()?
        .at("value")?
        .v()?
        .value
        .clone();
    scr.props_mut()?
        .at_mut("animData")?
        .o_mut()?
        .at_mut("stateMatrix")?
        .o_mut()?
        .at_mut("value")?
        .v_mut()?
        .value = want;
    Ok(())
}

fn literal_tooltips(scr: &mut Node) -> Result<()> {
    for t in scr.child_mut("tooltips")?.slot_mut(CHILDREN)? {
        if t.name() == "adjust" {
            set_prop_value(t.child_mut("text")?, "localise", Val::bool(false))?;
        }
    }
    Ok(())
}

pub struct Screens {
    pub order: Vec<(String, Vec<u8>)>,
    pub view_models: Vec<String>,
}

/// An addon's options page under another screen name, for an addon screen with `page: options`.
pub fn build_options_page(src: &Sources, e: &Entry, screen: &str) -> Result<Vec<u8>> {
    let e = Entry {
        kind: e.kind,
        index: e.index,
        screen: screen.to_owned(),
        name: e.name,
        author: e.author,
        version: e.version,
        description: e.description,
        id: e.id,
        options: e
            .options
            .iter()
            .map(|o| Opt {
                k: o.k,
                local: o.local,
                o: o.o,
            })
            .collect(),
        all: Vec::new(),
        setup: e.setup,
    };
    let rows = addon_rows(&e);
    let mut scr = build_page(src, &e, &rows, !e.options.is_empty())?;
    retitle(&mut scr, &e)?;
    if !e.options.is_empty() {
        vm_page(&mut scr, &e)?;
    }
    settings_state_matrix(src, &mut scr)?;
    literal_tooltips(&mut scr)?;
    ui_bytes(vec![scr])
}

pub const SETUP_CONFIRM: &str = "confirm";
pub const SETUP_OPTIONS: &str = "open_options";

pub fn setup_title<'a>(e: &Entry<'a>, setup: &'a CareerSetup) -> &'a str {
    setup.title.as_deref().map(str::trim).unwrap_or(e.name)
}

pub fn setup_rows_screen(e: &Entry, setup: &CareerSetup) -> String {
    let full = e.screen.trim_start_matches("swofty_addon_");
    format!("swofty_addon_{}_{full}", setup.route)
}

fn setup_entry<'a>(e: &Entry<'a>, setup: &'a CareerSetup) -> Entry<'a> {
    Entry {
        kind: e.kind,
        index: e.index,
        screen: setup_rows_screen(e, setup),
        name: setup_title(e, setup),
        author: e.author,
        version: e.version,
        description: e.description,
        id: e.id,
        options: setup
            .options
            .iter()
            .filter_map(|id| e.all.iter().find(|o| &o.o.id == id))
            .map(|o| Opt {
                k: o.k,
                local: o.local,
                o: o.o,
            })
            .collect(),
        all: Vec::new(),
        setup: Some(setup),
    }
}

/// A career setup's rows page: the setup's options, as ADDON SETTINGS shows them, under the setup's title.
pub fn build_setup_page(src: &Sources, e: &Entry, setup: &CareerSetup) -> Result<(String, Vec<u8>)> {
    let e = setup_entry(e, setup);
    if e.options.len() != setup.options.len() {
        return Err(err(format!(
            "career setup {}: {} names options its addon does not have",
            setup.route, e.id
        )));
    }
    let rows = addon_rows(&e);
    let mut scr = build_page(src, &e, &rows, true)?;
    vm_page(&mut scr, &e)?;
    settings_state_matrix(src, &mut scr)?;
    literal_tooltips(&mut scr)?;
    Ok((e.screen.clone(), ui_bytes(vec![scr])?))
}

/// A career setup's home: a CONFIRM tile in the first column and one category tile, which opens the rows page.
pub fn build_setup_home(src: &Sources, e: &Entry, setup: &CareerSetup) -> Result<(String, Vec<u8>)> {
    let home = setup.screen();
    let title = setup_title(e, setup).to_uppercase();
    let scr = src.screen(&home, HOME_LAYOUT)?;
    let fr = src.frame(&home)?;
    let mut kids = vec![src.title(&home, "settings_home_screen", &title)?];
    for n in ["filter", "side_panel", "scroll_bar", "scroll_bar_panel"] {
        kids.push(hidden(&format!("hide_{n}"), &[n]));
    }
    let left = format!("{home}.col_0");
    let right = format!("{home}.col_1");
    let confirm_at = format!("{left}.{SETUP_CONFIRM}");
    let options_at = format!("{right}.options");
    let press = |key: &str| {
        event(
            "pressed",
            vec![
                sig(&format!("{home}.audio_theme_trigger"), "confirm", "pressed"),
                sig_with(
                    &format!("{home}.Action"),
                    "route",
                    "pressed",
                    &[("key", key.into())],
                ),
            ],
        )
    };
    let mut confirm = instance(
        src.t("settings_home_confirm")?,
        &left,
        SETUP_CONFIRM,
        &[
            ("title", "CONFIRM"),
            ("subtitle", ""),
            ("info_title", ""),
            ("info_value", ""),
            ("info_body", ""),
        ],
        true,
    )?;
    confirm.a_mut()?[LAYOUT] = layout_path(&[(TILES.slot)(0)]);
    add_events(
        &mut confirm,
        vec![
            press(SETUP_CONFIRM),
            event("navRight", vec![sig(&options_at, "focus", "navRight")]),
        ],
    )?;
    let labels: Vec<String> = setup_entry(e, setup)
        .options
        .iter()
        .map(|o| label_case(&o.o.label))
        .collect();
    let text = TileText {
        title: &title,
        by: "",
        description: &labels.join(", "),
    };
    let mut category = tile_with(
        src,
        &TILES,
        &right,
        "options",
        0,
        (TILES.icon)(Kind::Addon),
        &text,
    )?;
    add_events(
        &mut category,
        vec![
            press(SETUP_OPTIONS),
            event("navLeft", vec![sig(&confirm_at, "focus", "navLeft")]),
        ],
    )?;
    for (name, tile) in [("col_0", confirm), ("col_1", category)] {
        let mut block = instance(src.t("settings_home_block")?, &home, name, &[], true)?;
        block.slot_mut(CHILDREN)?.push(tile);
        kids.push(block);
    }
    let mut scr = finish(scr, fr, kids, "col_1.options")?;
    settings_state_matrix(src, &mut scr)?;
    literal_tooltips(&mut scr)?;
    Ok((home, ui_bytes(vec![scr])?))
}

pub fn build_all(src: &Sources, style: &TileStyle, flow: &[Entry]) -> Result<Screens> {
    let mut nodes = vec![(HOME.to_owned(), build_home(src, style, flow)?)];
    let mut view_models = Vec::new();
    for e in flow.iter().filter(|e| e.kind == Kind::Addon) {
        let rows = addon_rows(e);
        let mut scr = build_page(src, e, &rows, !e.options.is_empty())?;
        retitle(&mut scr, e)?;
        if !e.options.is_empty() {
            vm_page(&mut scr, e)?;
            view_models.push(e.screen.clone());
        }
        nodes.push((e.screen.clone(), scr));
    }
    for e in flow.iter().filter(|e| e.kind == Kind::Map) {
        let mut scr = build_page(src, e, &map_rows(e), false)?;
        retitle(&mut scr, e)?;
        nodes.push((e.screen.clone(), scr));
    }
    for e in flow.iter().filter(|e| e.kind == Kind::Mod) {
        let mut scr = build_page(src, e, &mod_rows(e), false)?;
        retitle(&mut scr, e)?;
        nodes.push((e.screen.clone(), scr));
    }
    let mut order = Vec::new();
    for (name, mut scr) in nodes {
        settings_state_matrix(src, &mut scr)?;
        literal_tooltips(&mut scr)?;
        order.push((name, ui_bytes(vec![scr])?));
    }
    Ok(Screens { order, view_models })
}

pub fn state_entries(flow: &[Entry]) -> Result<Vec<(String, Node)>> {
    let mut out = vec![
        ("last".to_owned(), uidb_var("last", "uistring", s(""))),
        (
            "online_popup_seen".to_owned(),
            uidb_var("online_popup_seen", "bool", boolv(false)),
        ),
    ];
    for e in flow.iter().filter(|e| e.kind == Kind::Addon) {
        for opt in &e.options {
            let k = opt.k;
            let o = opt.o;
            let name = format!("o{k}");
            out.push((
                name.clone(),
                uidb_var(&name, "uistring", s(&o.current.to_string())),
            ));
            if o.kind == OptionType::Slider {
                let name = format!("s{k}");
                let v = number(&state_value(o)?.value)?;
                out.push((name.clone(), uidb_var(&name, "float", f32v(v))));
            }
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn map(code: &str, name: &str) -> SessionMap {
        SessionMap {
            code: code.into(),
            name: name.into(),
            version: "1.0".into(),
            author: "a".into(),
            description: String::new(),
        }
    }

    fn a_mod(id: &str, name: &str) -> SessionMod {
        SessionMod {
            id: id.into(),
            name: name.into(),
            version: "2.0".into(),
            author: "b".into(),
            description: String::new(),
        }
    }

    #[test]
    fn entries_list_maps_then_mods_with_their_own_screens() {
        let maps = [map("imola", "Imola")];
        let mods = [a_mod("ts.sauber", "Sauber"), a_mod("ts.rb", "Red Bull")];
        let flow = entries(&[], &maps, &mods);
        let shape: Vec<(Kind, &str, usize)> = flow
            .iter()
            .map(|e| (e.kind, e.screen.as_str(), e.index))
            .collect();
        assert_eq!(
            shape,
            [
                (Kind::Map, "swofty_map_0", 0),
                (Kind::Mod, "swofty_mod_0", 0),
                (Kind::Mod, "swofty_mod_1", 1),
            ]
        );
        assert_eq!(Kind::Mod.key(), "mod");
        assert_eq!(Kind::Mod.tag(), "MOD");
        assert_eq!((flow[1].name, flow[1].id), ("Sauber", "ts.sauber"));
        assert!(flow[1].options.is_empty());
    }
}
