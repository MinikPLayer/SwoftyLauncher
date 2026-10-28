pub mod car_category;
pub mod career_select;
pub mod catalog;
pub mod check;
pub mod era;
pub mod hub;
pub mod idf;
pub mod mfd;
pub mod node;
pub mod pages;
pub mod patch;

use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::path::{Path, PathBuf};

use swofty_formats::erp::{Entry, ErpFile, Resource};
use swofty_formats::pkg::Node;

use crate::builtin::CORE_ID;
use crate::manifest::CAREER_SETUP_BACK;
use crate::settings::{SessionScreen, SessionSettings, UI_DIR, screen_file};
use crate::{Error, Result, io_err};
use node::{boolv, err, s, uidb_var};
use pages::{ADJUST_TOOLTIP, HOME, Sources, TILES};
use patch::Popup;

pub const UI: &str = "2025_asset_groups/ui_package";
pub const FLOW: &str = "flow_options";
pub const ANCHOR: &str = "settings_container";
const OPTIONS_FLOW: &str = "options_flow";
const FE_HOME: &str = "eaid://ui_package/screens/fe_home.ui";
const FE_CAREER: &str = "eaid://ui_package/screens/fe_career.ui";
const CONTAINER: &str = "eaid://ui_package/screens/settings_container.ui";
const ROUTES: &str = "eaid://ui_package/routes/routes_frontend_general.uir";
const GAME_ROUTES: &str = "eaid://ui_package/routes/routes.uir";
const WIZARD_ROUTES: &str = "eaid://ui_package/routes/routes_career_wizard.uir";
const SETUP_FLOW_ERP: &str = "flow_career_season_setup";
const SETUP_BACK_ERP: &str = "flow_frontend";
const FLOWS: &str = "eaid://ui_package/flows.ui";
const REGIONS: &str = "eaid://ui_package/region_loading.uiconfig";
const TOOLTIPS: &str = "eaid://ui_package/databases/tool_tips.uidb";
const UI_DATA: &str = "eaid://ui_package/databases/ui_data.uidb";
const MESSAGES: &str = "eaid://ui_package/message_data/onlineservices_messages.uimsg";
const HUD_MESSAGES: &str = "eaid://ui_package/message_data/hud_messages.uimsg";
const PERSISTENT_UI: &str = "eaid://ui_package/screens/persistent.ui";

pub const ERPS: &[&str] = &[
    "persistent.erp",
    "databases.erp",
    "flow_persistent.erp",
    "flow_frontend.erp",
    era::ARCHIVES[0],
    era::ARCHIVES[1],
    car_category::ARCHIVES[1],
];
pub const READS: &[&str] = &["flow_options.erp"];

pub fn ui_file(rel: &str) -> String {
    format!("{UI}/{rel}")
}

#[derive(Debug, Clone, Default)]
pub struct MenuBuild {
    pub erps: BTreeMap<String, PathBuf>,
    pub screens: BTreeMap<String, PathBuf>,
    pub session: Vec<SessionScreen>,
    pub strings: Vec<(String, String)>,
    pub lines: Vec<String>,
    pub warnings: Vec<String>,
}

/// The addon whose copy of a resource (second) in a game archive (first)
/// the build reads; `None` for the game's own copy.
pub type Writer<'a> = dyn Fn(&str, &str) -> Option<String> + 'a;

type Patch<'a> = Box<dyn FnOnce(&[u8]) -> Result<Vec<u8>> + 'a>;

fn read_frag(path: &Path, name: &str, index: usize) -> Result<Vec<u8>> {
    let file = ErpFile::open(path)?;
    let res = file.load(file.erp.find(name)?)?;
    Ok(res
        .fragments
        .get(index)
        .ok_or_else(|| err(format!("{name} has no fragment {index}")))?
        .data()?)
}

fn write_erp(src: &Path, dst: &Path, patches: Vec<(&str, Patch)>) -> Result<()> {
    let patches = patches.into_iter().map(|(name, f)| (name, 0, f)).collect();
    write_archive(src, dst, patches, Vec::new())
}

type Added = (String, &'static str, Vec<Vec<u8>>);

fn write_archive(
    src: &Path,
    dst: &Path,
    patches: Vec<(&str, usize, Patch)>,
    added: Vec<Added>,
) -> Result<()> {
    write_archive_copying(src, dst, patches, added, Vec::new())
}

/// [`write_archive`], also appending `copied`, resources of another archive kept byte for byte, under names `src`
/// doesn't have.
fn write_archive_copying(
    src: &Path,
    dst: &Path,
    patches: Vec<(&str, usize, Patch)>,
    added: Vec<Added>,
    copied: Vec<Resource>,
) -> Result<()> {
    let file = ErpFile::open(src)?;
    if let Some(r) = copied
        .iter()
        .find(|r| file.erp.resources.iter().any(|s| s.name == r.name))
    {
        return Err(err(format!("{} has {} already", src.display(), r.name)));
    }
    let mut owned: HashMap<String, Resource> = HashMap::new();
    for (name, index, f) in patches {
        let mut res = match owned.remove(name) {
            Some(r) => r,
            None => file.load(file.erp.find(name)?)?,
        };
        let frag = res
            .fragments
            .get(index)
            .ok_or_else(|| err(format!("{name} has no fragment {index}")))?;
        let data = f(&frag.data()?)?;
        res.set_fragment(index, &data)?;
        res.repair_stats()?;
        owned.insert(name.to_owned(), res);
    }
    let mut extra = Vec::new();
    for (name, kind, data) in added {
        let want = swofty_formats::erp::kind(kind)?;
        let like = file
            .erp
            .resources
            .iter()
            .find(|r| r.kind == want && r.fragments.len() == data.len())
            .ok_or_else(|| {
                err(format!(
                    "{}: no {kind} resource to take {name}'s layout from",
                    src.display()
                ))
            })?;
        let res = Resource::like(&name, like, &data)?;
        match file.erp.resources.iter().any(|r| r.name == name) {
            true => {
                owned.insert(name, res);
            }
            false => extra.push(Entry::Owned(res)),
        }
    }
    let mut entries: Vec<Entry> = file
        .erp
        .resources
        .iter()
        .map(|r| match owned.remove(&r.name) {
            Some(new) => Entry::Owned(new),
            None => Entry::Copy(r),
        })
        .collect();
    entries.extend(extra);
    entries.extend(copied.into_iter().map(Entry::Owned));
    let dir = dst.parent().expect("has parent");
    std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    file.write(entries, dst)?;
    Ok(())
}

fn screen_flow(input: &dyn Fn(&str) -> PathBuf, flows_ui: &[u8], erp: &str, screen: &str) -> Result<String> {
    let path = input(&ui_file(&format!("{erp}.erp")));
    let file = ErpFile::open(&path)?;
    if file
        .erp
        .find(&format!("eaid://ui_package/screens/{screen}.ui"))
        .is_err()
    {
        return Err(err(format!("{erp}.erp has no screen {screen}")));
    }
    check::flow_loading(flows_ui, erp)?.ok_or_else(|| err(format!("no flow in flows.ui loads {erp}")))
}

fn panel_shown(settings: &SessionSettings) -> bool {
    settings
        .addons
        .iter()
        .filter(|m| m.id == CORE_ID)
        .flat_map(|m| &m.options)
        .find(|o| o.id == "show_launcher_panel")
        .and_then(|o| o.states.get(o.current))
        .is_none_or(|s| s.value != serde_json::Value::Bool(false))
}

fn core_version(settings: &SessionSettings) -> String {
    settings
        .addons
        .iter()
        .find(|m| m.id == CORE_ID)
        .map(|m| m.version.clone())
        .unwrap_or_else(|| crate::builtin::core_manifest().version.to_string())
}

pub struct MfdRow {
    pub addon: String,
    pub title: String,
    pub option: String,
    pub label: String,
    pub text: String,
}

pub fn mfd_rows(settings: &SessionSettings) -> Vec<MfdRow> {
    let mut out = Vec::new();
    for a in settings.addons.iter().filter(|a| a.enabled) {
        let Some(m) = &a.mfd else { continue };
        for id in &m.rows {
            if let Some(o) = a.options.iter().find(|o| &o.id == id) {
                out.push(MfdRow {
                    addon: a.id.clone(),
                    title: m.title.clone(),
                    option: o.id.clone(),
                    label: o.label.clone(),
                    text: o
                        .states
                        .get(o.current)
                        .map(|s| s.text.clone())
                        .unwrap_or_default(),
                });
            }
        }
    }
    out.truncate(swofty_addon::manifest::MFD_ROWS_MAX);
    out
}

pub fn build(
    settings: &SessionSettings,
    input: &dyn Fn(&str) -> PathBuf,
    writer: &Writer,
    out: &Path,
    lang: bool,
) -> Result<MenuBuild> {
    let flow_options = input(&ui_file("flow_options.erp"));
    let flow_frontend = input(&ui_file("flow_frontend.erp"));
    let src = Sources::parse(
        &read_frag(&flow_options, CONTAINER, 1)?,
        &read_frag(&flow_frontend, FE_CAREER, 0)?,
        &read_frag(&flow_frontend, FE_HOME, 0)?,
        &read_frag(&flow_options, CONTAINER, 0)?,
    )?;
    let flow = pages::entries(&settings.addons, &settings.maps, &settings.mods);
    let mut built = pages::build_all(&src, &TILES, &flow)?;
    // Addon screens with page: options are their addon's options page under the name
    // swofty_addon_<name>_<addon index>; the view model reads the index after the last '_'.
    let mut generated: Vec<(String, String, String)> = Vec::new();
    for s in settings.ui.iter().flat_map(|u| &u.screens) {
        if s.page.as_deref() != Some("options") {
            continue;
        }
        let e = flow
            .iter()
            .find(|e| e.kind == pages::Kind::Addon && Some(e.id) == s.addon.as_deref())
            .ok_or_else(|| err(format!("screen {}: its addon is not on", s.name)))?;
        let full = e.screen.trim_start_matches("swofty_addon_");
        let screen = format!("swofty_addon_{}_{full}", s.name);
        let anchor = s.anchor.clone().unwrap_or_else(|| ANCHOR.to_owned());
        built
            .order
            .push((screen.clone(), pages::build_options_page(&src, e, &screen)?));
        if !e.options.is_empty() {
            built.view_models.push(screen.clone());
        }
        generated.push((s.name.clone(), anchor, screen));
    }
    let flows_ui = match flow.iter().any(|e| e.setup.is_some()) {
        true => read_frag(&input(&ui_file("persistent.erp")), FLOWS, 0)?,
        false => Vec::new(),
    };
    let mut setups: Vec<patch::SetupRoutes> = Vec::new();
    let mut setup_lines = Vec::new();
    for e in flow.iter().filter(|e| e.kind == pages::Kind::Addon) {
        let Some(setup) = e.setup else { continue };
        let (home, home_ui) = pages::build_setup_home(&src, e, setup)?;
        let (rows, rows_ui) = pages::build_setup_page(&src, e, setup)?;
        built.order.push((home.clone(), home_ui));
        built.order.push((rows.clone(), rows_ui));
        built.view_models.push(rows.clone());
        let back = CAREER_SETUP_BACK.to_owned();
        setups.push(patch::SetupRoutes {
            route: setup.route.clone(),
            next_flow: screen_flow(input, &flows_ui, SETUP_FLOW_ERP, &setup.next)?,
            back_flow: screen_flow(input, &flows_ui, SETUP_BACK_ERP, &back)?,
            next: setup.next.clone(),
            back,
            back_route: setup.back_route(),
            home,
            rows,
        });
        setup_lines.push(format!(
            "ui: {} has a career setup page {}: the route {} opens it from the career content select, CONFIRM \
             opens {}, and {}'s route {} opens it again",
            e.id,
            setup.screen(),
            setup.route,
            setup.next,
            setup.next,
            setup.back_route()
        ));
    }
    let names: Vec<String> = built.order.iter().map(|(n, _)| n.clone()).collect();
    // Addon screens in the options flow get the flow entry, region and route group an ADDON
    // SETTINGS page gets, so routes and signals to them open them.
    let addon_routes: Vec<(String, String)> = settings
        .ui
        .iter()
        .flat_map(|u| &u.screens)
        .filter(|s| s.flow == FLOW && s.page.is_none() && !names.contains(&s.name))
        .map(|s| {
            (
                s.name.clone(),
                s.anchor.clone().unwrap_or_else(|| ANCHOR.to_owned()),
            )
        })
        .collect();
    let addon_screens: Vec<String> = addon_routes.iter().map(|(n, _)| n.clone()).collect();
    let mut opened: Vec<(String, String, String)> = addon_routes
        .iter()
        .map(|(n, a)| (n.clone(), a.clone(), n.clone()))
        .collect();
    opened.extend(generated.iter().cloned());
    let mut addon_groups = addon_routes.clone();
    addon_groups.extend(generated.iter().map(|(_, a, s)| (s.clone(), a.clone())));
    let setup_groups: Vec<Node> = setups.iter().flat_map(patch::setup_route_groups).collect();
    let listed: Vec<String> = names.iter().chain(&addon_screens).cloned().collect();
    let mut problems = check::screens(&built.order, &built.view_models)?;
    let no_scenes = catalog::Scenes::default();
    for (name, data) in &built.order {
        let input = check::UiInput {
            resource: name,
            frag: "temp",
            data,
            stock: None,
        };
        let report = check::ui_edit(&input, &no_scenes, catalog::stock(), catalog::engine())?;
        problems.extend(report.hard.iter().map(|h| h.to_string()));
    }

    let pages: Vec<(String, String)> = flow
        .iter()
        .map(|e| (format!("open_{}_{}", e.kind.key(), e.index), e.screen.clone()))
        .collect();
    let mut setup_ids: Vec<String> = Vec::new();
    for s in &setups {
        setup_ids.extend([
            s.home.clone(),
            s.rows.clone(),
            format!("{}.{}", s.back, s.route),
            format!("{}.{}", s.next, s.back_route),
        ]);
    }
    let mut groups: Vec<&str> = vec![HOME, "fe_home.mod_menu"];
    groups.extend(pages.iter().map(|(_, s)| s.as_str()));
    groups.extend(setup_ids.iter().map(String::as_str));
    let mut state = pages::state_entries(&flow)?;
    let mfd_rows = mfd_rows(settings);
    if !mfd_rows.is_empty() {
        state.push((
            "mfd_enabled".to_owned(),
            uidb_var("mfd_enabled", "bool", boolv(true)),
        ));
        for (k, r) in mfd_rows.iter().enumerate() {
            let name = format!("m{k}");
            state.push((name.clone(), uidb_var(&name, "uistring", s(&r.text))));
        }
    }
    let popup = Popup::for_version(&core_version(settings));
    let show_panel = panel_shown(settings);

    let erp = |rel: &str| (input(&ui_file(rel)), out.join(ui_file(rel)));
    let (src_p, dst_p) = erp("persistent.erp");
    let routes_out = std::cell::RefCell::new(Vec::new());
    let game_routes_out = std::cell::RefCell::new(Vec::new());
    let wizard_routes_out = std::cell::RefCell::new(Vec::new());
    let flows_out = std::cell::RefCell::new(Vec::new());
    let placed = std::cell::RefCell::new(patch::Placed::default());
    let mut persistent: Vec<(&str, Patch)> = vec![
        (
            ROUTES,
            Box::new(|d| {
                let mut groups = patch::route_groups(&pages);
                groups.extend(setup_groups);
                let b = patch::routes(d, groups, patch::screen_route_groups(&addon_groups))?;
                let b = patch::setup_routes(&b, &setups, &mut placed.borrow_mut())?;
                *routes_out.borrow_mut() = b.clone();
                Ok(b)
            }),
        ),
        (
            FLOWS,
            Box::new(|d| {
                let b = patch::flow_entries(d, OPTIONS_FLOW, &listed)?;
                *flows_out.borrow_mut() = b.clone();
                Ok(b)
            }),
        ),
        (REGIONS, Box::new(|d| patch::regions(d, &listed))),
        (
            GAME_ROUTES,
            Box::new(|d| {
                let b = patch::anchor_routes(d, &opened)?;
                let b = patch::setup_routes(&b, &setups, &mut placed.borrow_mut())?;
                *game_routes_out.borrow_mut() = b.clone();
                Ok(b)
            }),
        ),
    ];
    if !setups.is_empty() {
        persistent.push((
            WIZARD_ROUTES,
            Box::new(|d| {
                let b = patch::setup_routes(d, &setups, &mut placed.borrow_mut())?;
                *wizard_routes_out.borrow_mut() = b.clone();
                Ok(b)
            }),
        ));
    }
    if !mfd_rows.is_empty() {
        persistent.push((mfd::METADATA, Box::new(mfd::metadata)));
    }
    write_erp(&src_p, &dst_p, persistent)?;
    for s in &setups {
        for id in [
            format!("{}.{}", s.back, s.route),
            format!("{}.{}", s.next, s.back_route),
        ] {
            if !placed.borrow().routes.contains(&id) {
                return Err(Error::Other(format!(
                    "career setup {}: the game's routes have no route group {} for {id}",
                    s.route,
                    id.split('.').next().unwrap_or_default()
                )));
            }
        }
    }
    let registered: BTreeMap<String, String> = listed.iter().map(|n| (n.clone(), FLOW.to_owned())).collect();
    let stock_screens: std::collections::BTreeSet<String> = setups
        .iter()
        .flat_map(|s| [s.back.clone(), s.next.clone()])
        .chain(placed.borrow().screens.iter().cloned())
        .collect();
    let (r, g, w) = (
        routes_out.borrow(),
        game_routes_out.borrow(),
        wizard_routes_out.borrow(),
    );
    let mut route_files: Vec<&[u8]> = vec![&r, &g];
    if !w.is_empty() {
        route_files.push(&w);
    }
    problems.extend(check::route_files(
        &route_files,
        &flows_out.borrow(),
        &groups,
        &registered,
        &stock_screens,
    )?);
    if !problems.is_empty() {
        return Err(Error::Other(format!(
            "ADDON SETTINGS failed its checks:\n  {}",
            problems.join("\n  ")
        )));
    }

    let (src_d, dst_d) = erp("databases.erp");
    write_erp(
        &src_d,
        &dst_d,
        vec![
            (TOOLTIPS, Box::new(|d| patch::tooltips(d, ADJUST_TOOLTIP))),
            (UI_DATA, Box::new(move |d| patch::ui_data(d, state))),
        ],
    )?;
    let (src_f, dst_f) = erp("flow_persistent.erp");
    write_erp(
        &src_f,
        &dst_f,
        vec![
            (MESSAGES, Box::new(|d| patch::messages(d, lang))),
            (HUD_MESSAGES, Box::new(patch::hud_messages)),
            (
                PERSISTENT_UI,
                Box::new(|d| {
                    let out = patch::persistent_gate(d)?;
                    let changed = check::stock_kept(d, &out, "swofty_")?;
                    if !changed.is_empty() {
                        return Err(Error::Other(format!(
                            "the popup gate changes the game's own popups:\n  {}",
                            changed.join("\n  ")
                        )));
                    }
                    Ok(out)
                }),
            ),
        ],
    )?;
    let (src_h, dst_h) = erp("flow_frontend.erp");
    let (mut career_lines, mut career_warnings) = (Vec::new(), Vec::new());
    let career = career_row(&src_h, writer, &mut career_lines, &mut career_warnings)?;
    let mut frontend: Vec<(&str, usize, Patch)> = vec![(
        FE_HOME,
        0,
        Box::new(move |d| {
            let d = patch::offline_panel(&patch::fe_home(d)?)?;
            if show_panel { Ok(d) } else { patch::hide_panel(&d) }
        }),
    )];
    if let Some((ui, type_ui, scene, templates)) = career {
        frontend.push((career_select::UI, 0, Box::new(move |_| Ok(ui))));
        frontend.push((career_select::TYPE_UI, 0, Box::new(move |_| Ok(type_ui))));
        frontend.push((career_select::SCENE, 0, Box::new(move |_| Ok(scene))));
        frontend.push((
            career_select::TEMPLATES,
            career_select::TEMPLATES_FRAGMENT,
            Box::new(move |_| Ok(templates)),
        ));
    }
    write_archive(&src_h, &dst_h, frontend, Vec::new())?;

    let mut build = MenuBuild::default();
    build.lines.extend(setup_lines);
    build.lines.extend(career_lines);
    build.warnings.extend(career_warnings);
    if !mfd_rows.is_empty() {
        let mut rows: Vec<mfd::Row> = Vec::new();
        for (k, r) in mfd_rows.iter().enumerate() {
            if k == 0 || mfd_rows[k - 1].addon != r.addon {
                rows.push(mfd::Row {
                    label: r.title.clone(),
                    value: None,
                });
            }
            rows.push(mfd::Row {
                label: r.label.clone(),
                value: Some(mfd::value_path(k)),
            });
        }
        for rel in mfd::TEMPLATE_ERPS {
            let (src, dst) = erp(rel);
            write_erp(
                &src,
                &dst,
                vec![(mfd::TEMPLATES, Box::new(|d| mfd::templates(d, &rows)))],
            )?;
            build.erps.insert(ui_file(rel), dst);
        }
        build.lines.push(format!(
            "ADDONS MFD page: {} row(s) from {}",
            mfd_rows.len(),
            mfd_rows
                .iter()
                .map(|r| r.addon.as_str())
                .collect::<std::collections::BTreeSet<_>>()
                .into_iter()
                .collect::<Vec<_>>()
                .join(", ")
        ));
    }
    let mut eras = era_rows(input, writer, out, &mut build.lines, &mut build.warnings)?;
    for rel in car_category_boxes(input, writer, out, &eras, &mut build.lines, &mut build.warnings)? {
        if !eras.contains(&rel) {
            eras.push(rel);
        }
    }
    let hubs = hub_tabs(
        settings,
        input,
        writer,
        out,
        &mut build.lines,
        &mut build.warnings,
    )?;
    if lang {
        build.strings = popup.strings();
    }
    for rel in ERPS
        .iter()
        .filter(|r| !era::ARCHIVES.contains(r) && !car_category::ARCHIVES.contains(r) || eras.contains(r))
    {
        build.erps.insert(ui_file(rel), out.join(ui_file(rel)));
    }
    for rel in hubs {
        build.erps.insert(ui_file(rel), out.join(ui_file(rel)));
    }
    for (name, data) in &built.order {
        let rel = format!("{UI_DIR}/{name}.ui");
        let path = out.join(&rel);
        let dir = path.parent().expect("has parent");
        std::fs::create_dir_all(dir).map_err(io_err(dir))?;
        std::fs::write(&path, data).map_err(io_err(&path))?;
        build.screens.insert(rel, path);
        build.session.push(SessionScreen {
            name: name.clone(),
            page: None,
            addon: None,
            file: screen_file(name),
            flow: FLOW.into(),
            anchor: Some(ANCHOR.into()),
            view_model: built.view_models.contains(name),
        });
    }
    Ok(build)
}

type CareerRow = (Vec<u8>, Vec<u8>, Vec<u8>, Vec<u8>);

fn career_row(
    src: &Path,
    writer: &Writer,
    lines: &mut Vec<String>,
    warnings: &mut Vec<String>,
) -> Result<Option<CareerRow>> {
    let ui = read_frag(src, career_select::UI, 0)?;
    let type_ui = read_frag(src, career_select::TYPE_UI, 0)?;
    let scene = read_frag(src, career_select::SCENE, 0)?;
    let templates = read_frag(src, career_select::TEMPLATES, career_select::TEMPLATES_FRAGMENT)?;
    let edit = career_select::taken(&ui, &scene, &templates).and_then(|taken| match taken {
        true => Ok(None),
        false => Ok(Some((
            career_select::build_ui(&ui)?,
            career_select::build_type_ui(&type_ui)?,
            career_select::build_scene(&scene)?,
            career_select::build_templates(&templates)?,
        ))),
    });
    let edit = match edit {
        Ok(edit) => edit,
        Err(e) => {
            let rel = ui_file(career_select::ARCHIVE);
            let by = [
                career_select::UI,
                career_select::TYPE_UI,
                career_select::SCENE,
                career_select::TEMPLATES,
            ]
            .into_iter()
            .find_map(|res| writer(&rel, res));
            let Some(addon) = by else { return Err(e) };
            warnings.push(format!(
                "ui: {addon} has its own copy of the career content select, which Swofty can't edit; addon careers \
                 follow the stock ones there while it's on ({e})"
            ));
            return Ok(None);
        }
    };
    match edit {
        Some(edit) => {
            lines.push("ui: the career content select gets Built In and Addons rows".to_owned());
            Ok(Some(edit))
        }
        None => {
            lines.push(
                "ui: the career content select already has an Addons row from another addon; left as it is"
                    .to_owned(),
            );
            Ok(None)
        }
    }
}

fn hub_tabs(
    settings: &SessionSettings,
    input: &dyn Fn(&str) -> PathBuf,
    writer: &Writer,
    out: &Path,
    lines: &mut Vec<String>,
    warnings: &mut Vec<String>,
) -> Result<Vec<&'static str>> {
    let mut pages = Vec::new();
    for m in settings.addons.iter().filter(|m| m.enabled) {
        let Some(tab) = &m.hub_tab else { continue };
        let rows = tab
            .options
            .iter()
            .map(|id| {
                let o = m.options.iter().find(|o| &o.id == id).ok_or_else(|| {
                    err(format!(
                        "{}: hub tab {} names option {id}, which it doesn't have",
                        m.id, tab.name
                    ))
                })?;
                Ok(hub::Row {
                    id,
                    label: &o.label,
                    description: &o.description,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        pages.push(hub::Page {
            addon: &m.id,
            tab,
            rows,
        });
    }
    if pages.is_empty() {
        return Ok(Vec::new());
    }
    let mut written = Vec::new();
    let options = input(&ui_file(hub::OPTIONS));
    let options_file = ErpFile::open(&options)?;
    let settings = read_frag(&options, hub::SETTINGS_UI, hub::SETTINGS_FRAGMENT)?;
    let mut shared = BTreeSet::new();
    for rel in hub::SHARED {
        let f = ErpFile::open(&input(&ui_file(rel)))?;
        shared.extend(f.erp.resources.iter().map(|r| r.name.clone()));
    }
    for rel in hub::ARCHIVES {
        let (src, dst) = (input(&ui_file(rel)), out.join(ui_file(rel)));
        let ui = match read_frag(&src, hub::UI, hub::UI_FRAGMENT)
            .and_then(|d| hub::build_ui(&d, &settings, &pages))
        {
            Ok(ui) => ui,
            Err(e) => {
                let Some(addon) = writer(&ui_file(rel), hub::UI) else {
                    return Err(e);
                };
                warnings.push(format!(
                    "ui: {addon} has its own copy of the laptop's overview page in {rel}, which Swofty can't edit; hub \
                     tabs are off there while it's on ({e})"
                ));
                continue;
            }
        };
        let mut have = shared.clone();
        have.extend(ErpFile::open(&src)?.erp.resources.iter().map(|r| r.name.clone()));
        let plan = hub::copy_plan(&options_file, &have, &hub::SCENES)?;
        lines.push(format!(
            "ui: hub tabs copy {} settings screen resources from {} into {rel}",
            plan.add.len(),
            hub::OPTIONS
        ));
        write_archive_copying(
            &src,
            &dst,
            vec![(hub::UI, hub::UI_FRAGMENT, Box::new(move |_| Ok(ui)))],
            Vec::new(),
            plan.add,
        )?;
        written.push(rel);
    }
    let files: Vec<(String, PathBuf, PathBuf)> = written
        .iter()
        .map(|rel| (ui_file(rel), input(&ui_file(rel)), out.join(ui_file(rel))))
        .collect();
    let pairs: Vec<(&str, &Path, &Path)> = files
        .iter()
        .map(|(name, src, dst)| (name.as_str(), src.as_path(), dst.as_path()))
        .collect();
    let report = check::archive_files(&pairs)?;
    if !report.hard.is_empty() {
        let found: Vec<String> = report.hard.iter().map(ToString::to_string).collect();
        return Err(Error::Other(format!(
            "the hub tab pages break the game's UI loader rules: {}",
            found.join("; ")
        )));
    }
    for p in &pages {
        lines.push(format!(
            "ui: {} has a hub tab template {} for {} careers in {} archive(s); its rows read {}",
            p.addon,
            p.tab.template(),
            p.tab.data_group,
            written.len(),
            p.tab.view_model()
        ));
    }
    Ok(written)
}

fn era_rows(
    input: &dyn Fn(&str) -> PathBuf,
    writer: &Writer,
    out: &Path,
    lines: &mut Vec<String>,
    warnings: &mut Vec<String>,
) -> Result<Vec<&'static str>> {
    let world = input(&ui_file(era::ARCHIVES[0]));
    let title = read_frag(&world, era::TITLE_SCENE, 0)?;
    let mut added = Vec::new();
    if era::ROW_FADE {
        let image = format!("{}.image", era::FADE_SOURCE);
        let fade = era::fade(
            &read_frag(&world, era::FADE_SOURCE, 0)?,
            &read_frag(&world, &image, 0)?,
            era::FADE,
        )?;
        added.push((era::FADE.to_owned(), era::SRV_KIND, vec![fade[0].clone()]));
        added.push((
            format!("{}.image", era::FADE),
            era::SURFACE_KIND,
            fade[1..].to_vec(),
        ));
    }
    let mut written = Vec::new();
    for rel in era::ARCHIVES {
        let (src, dst) = (input(&ui_file(rel)), out.join(ui_file(rel)));
        let ui = read_frag(&src, era::UI, era::UI_FRAGMENT)?;
        let scene = read_frag(&src, era::SCENE, 0)?;
        let edit = era::taken(&ui, &scene).and_then(|taken| match taken {
            true => Ok(None),
            false => Ok(Some((era::build_ui(&ui)?, era::build_scene(&scene, &title)?))),
        });
        let edit = match edit {
            Ok(edit) => edit,
            Err(e) => {
                let by = [
                    (rel, era::UI),
                    (rel, era::SCENE),
                    (era::ARCHIVES[0], era::TITLE_SCENE),
                ]
                .into_iter()
                .find_map(|(erp, res)| writer(&ui_file(erp), res));
                let Some(addon) = by else { return Err(e) };
                warnings.push(format!(
                    "ui: {addon} has its own copy of the era picker in {rel}, which Swofty can't edit; the era picker boxes there are off while it's on ({e})"
                ));
                continue;
            }
        };
        let Some((ui, scene)) = edit else {
            lines.push(format!(
                "ui: the era picker in {rel} already has an Addons row from another addon; left as it is"
            ));
            continue;
        };
        write_archive(
            &src,
            &dst,
            vec![
                (era::UI, era::UI_FRAGMENT, Box::new(move |_| Ok(ui))),
                (era::SCENE, 0, Box::new(move |_| Ok(scene))),
            ],
            added.clone(),
        )?;
        written.push(rel);
    }
    if !written.is_empty() {
        let files: Vec<(String, PathBuf, PathBuf)> = written
            .iter()
            .map(|rel| (ui_file(rel), input(&ui_file(rel)), out.join(ui_file(rel))))
            .collect();
        let pairs: Vec<(&str, &Path, &Path)> = files
            .iter()
            .map(|(name, src, dst)| (name.as_str(), src.as_path(), dst.as_path()))
            .collect();
        let report = check::archive_files(&pairs)?;
        if !report.hard.is_empty() {
            let found: Vec<String> = report.hard.iter().map(ToString::to_string).collect();
            return Err(Error::Other(format!(
                "the era picker rows break the game's UI loader rules: {}",
                found.join("; ")
            )));
        }
        lines.push(format!(
            "ui: the Grand Prix and Time Trial era picker gets Built In and Addons rows in {} archive(s)",
            written.len()
        ));
    }
    Ok(written)
}

/// Gives the LAN Car Category screen a row of addon boxes. An archive the era picker rows already
/// rewrote is read from that output. A failure leaves the screen stock and is a warning: the boxes
/// are an extra on a screen most players never open.
fn car_category_boxes(
    input: &dyn Fn(&str) -> PathBuf,
    writer: &Writer,
    out: &Path,
    eras: &[&'static str],
    lines: &mut Vec<String>,
    warnings: &mut Vec<String>,
) -> Result<Vec<&'static str>> {
    let mut written = Vec::new();
    for rel in car_category::ARCHIVES {
        let dst = out.join(ui_file(rel));
        let src = if eras.contains(&rel) {
            dst.clone()
        } else {
            input(&ui_file(rel))
        };
        if let Some(addon) = [car_category::UI, car_category::SCENE]
            .into_iter()
            .find_map(|res| writer(&ui_file(rel), res))
        {
            lines.push(format!(
                "ui: {addon} has its own copy of the Car Category screen in {rel}; left as it is"
            ));
            continue;
        }
        let edit = (|| -> Result<Option<(Vec<u8>, Vec<u8>)>> {
            let ui = read_frag(&src, car_category::UI, car_category::UI_FRAGMENT)?;
            let scene = read_frag(&src, car_category::SCENE, 0)?;
            if car_category::taken(&ui, &scene)? {
                return Ok(None);
            }
            Ok(Some((
                car_category::build_ui(&ui)?,
                car_category::build_scene(&scene)?,
            )))
        })();
        let (ui, scene) = match edit {
            Ok(Some(edit)) => edit,
            Ok(None) => continue,
            Err(e) => {
                warnings.push(format!(
                    "ui: the Car Category screen in {rel} keeps its two boxes ({e})"
                ));
                continue;
            }
        };
        let tmp = dst.with_extension("erp.car-category");
        write_archive(
            &src,
            &tmp,
            vec![
                (
                    car_category::UI,
                    car_category::UI_FRAGMENT,
                    Box::new(move |_| Ok(ui)),
                ),
                (car_category::SCENE, 0, Box::new(move |_| Ok(scene))),
            ],
            Vec::new(),
        )?;
        std::fs::rename(&tmp, &dst).map_err(io_err(&dst))?;
        written.push(rel);
    }
    if written.is_empty() {
        return Ok(written);
    }
    let files: Vec<(String, PathBuf, PathBuf)> = written
        .iter()
        .map(|rel| (ui_file(rel), input(&ui_file(rel)), out.join(ui_file(rel))))
        .collect();
    let pairs: Vec<(&str, &Path, &Path)> = files
        .iter()
        .map(|(name, src, dst)| (name.as_str(), src.as_path(), dst.as_path()))
        .collect();
    let report = check::archive_files(&pairs)?;
    if !report.hard.is_empty() {
        let found: Vec<String> = report.hard.iter().map(ToString::to_string).collect();
        return Err(Error::Other(format!(
            "the Car Category addon boxes break the game's UI loader rules: {}",
            found.join("; ")
        )));
    }
    lines.push(format!(
        "ui: the Car Category screen gets {} addon box slot(s) in {} archive(s)",
        car_category::SLOTS,
        written.len()
    ));
    Ok(written)
}

#[cfg(test)]
mod tests {

    #[test]
    #[ignore = "needs SWOFTY_STOCK_GAME, a stock F1 25 folder"]
    fn the_car_category_screen_gets_its_addon_boxes_within_the_loader_rules() {
        let stock = std::path::PathBuf::from(std::env::var_os("SWOFTY_STOCK_GAME").expect("set"));
        let out = std::env::temp_dir().join(format!("swofty-car-category-{}", std::process::id()));
        std::fs::remove_dir_all(&out).ok();
        let input = |rel: &str| stock.join(rel);
        let (mut lines, mut warnings) = (Vec::new(), Vec::new());
        let written = car_category_boxes(&input, &|_, _| None, &out, &[], &mut lines, &mut warnings).unwrap();
        assert_eq!(written, car_category::ARCHIVES, "{warnings:?}");
        assert!(warnings.is_empty(), "{warnings:?}");
        for rel in car_category::ARCHIVES {
            let dst = out.join(ui_file(rel));
            let ui = read_frag(&dst, car_category::UI, car_category::UI_FRAGMENT).unwrap();
            let scene = read_frag(&dst, car_category::SCENE, 0).unwrap();
            assert!(car_category::taken(&ui, &scene).unwrap(), "{rel}");
            for k in 0..car_category::SLOTS {
                let text = String::from_utf8_lossy(&ui).into_owned();
                for want in [
                    car_category::slot(k),
                    car_category::entry(k),
                    car_category::command(k),
                ] {
                    assert!(text.contains(&want), "{rel}: {want}");
                }
            }
        }
        let again = car_category_boxes(
            &|rel: &str| out.join(rel),
            &|_, _| None,
            &out,
            &[],
            &mut Vec::new(),
            &mut Vec::new(),
        )
        .unwrap();
        assert!(again.is_empty(), "boxes are added once");
        std::fs::remove_dir_all(&out).ok();
    }

    #[test]
    #[ignore = "needs SWOFTY_STOCK_FLOW_FRONTEND, a stock flow_frontend.erp"]
    fn the_career_content_select_gets_its_rows_within_the_loader_rules() {
        let src = std::path::PathBuf::from(std::env::var_os("SWOFTY_STOCK_FLOW_FRONTEND").expect("set"));
        let mut lines = Vec::new();
        let mut warnings = Vec::new();
        let (ui, type_ui, scene, templates) = career_row(&src, &|_, _| None, &mut lines, &mut warnings)
            .unwrap()
            .expect("a stock archive takes the rows");
        let out = std::env::temp_dir().join(format!("swofty-career-row-{}", std::process::id()));
        let dst = out.join("flow_frontend.erp");
        write_archive(
            &src,
            &dst,
            vec![
                (career_select::UI, 0, Box::new(move |_| Ok(ui))),
                (career_select::TYPE_UI, 0, Box::new(move |_| Ok(type_ui))),
                (career_select::SCENE, 0, Box::new(move |_| Ok(scene))),
                (
                    career_select::TEMPLATES,
                    career_select::TEMPLATES_FRAGMENT,
                    Box::new(move |_| Ok(templates)),
                ),
            ],
            Vec::new(),
        )
        .unwrap();
        let name = ui_file(career_select::ARCHIVE);
        let report = check::archive_files(&[(name.as_str(), src.as_path(), dst.as_path())]).unwrap();
        let hard: Vec<String> = report.hard.iter().map(ToString::to_string).collect();
        assert!(hard.is_empty(), "{hard:?}");
        assert_eq!(
            lines,
            ["ui: the career content select gets Built In and Addons rows"]
        );
        std::fs::remove_dir_all(&out).ok();
    }

    const SETUP_ROUTE: &str = "career_wizard_driver_career_progressive";

    fn choice(id: &str, addon: &str, values: &[&str]) -> serde_json::Value {
        let states: Vec<serde_json::Value> = values
            .iter()
            .enumerate()
            .map(|(i, v)| serde_json::json!({ "value": v, "text": v, "command": format!("swofty_set:{addon}:{id}:{i}") }))
            .collect();
        serde_json::json!({ "id": id, "type": "choice", "binds": "native", "label": id.replace('_', " "),
                            "description": format!("About {id}."), "current": 0, "states": states })
    }

    fn setup_settings() -> SessionSettings {
        let addon = "t.progressive";
        let years: Vec<String> = (2016..=2026).map(|y| y.to_string()).collect();
        let years: Vec<&str> = years.iter().map(String::as_str).collect();
        let mut dev = choice("dev_probe", addon, &["off", "on"]);
        dev["hidden"] = true.into();
        let mut start = choice("start_season", addon, &years);
        start["hidden"] = true.into();
        serde_json::from_value(serde_json::json!({
            "schema": 1, "profile": "t", "restart_command": "swofty_restart",
            "addons": [{
                "id": addon, "name": "Progressive", "version": "0.1.0", "author": "t",
                "description": "Every season takes that year's grid.", "enabled": true, "locked": false,
                "enable_commands": [format!("swofty_enable:{addon}:0"), format!("swofty_enable:{addon}:1")],
                "options": [
                    choice("show_hud", addon, &["on", "off"]),
                    start,
                    choice("rnd_mode", addon, &["real_life", "base", "hybrid"]),
                    choice("team_moves", addon, &["follow_driver", "choose"]),
                    dev,
                ],
                "career_setup": { "route": SETUP_ROUTE, "title": "Progressive Career",
                                  "options": ["start_season", "rnd_mode", "team_moves"] }
            }]
        }))
        .unwrap()
    }

    fn stock_input() -> impl Fn(&str) -> PathBuf {
        let data = PathBuf::from(std::env::var_os("SWOFTY_STOCK_DATA").expect("set"));
        let baseline = std::fs::read_dir(data.join("baseline"))
            .unwrap()
            .flatten()
            .map(|e| e.path().join("baseline.json"))
            .find(|p| p.is_file())
            .unwrap();
        let b: crate::baseline::Baseline = serde_json::from_slice(&std::fs::read(baseline).unwrap()).unwrap();
        let objects = crate::baseline::Objects {
            root: data.join("baseline/objects"),
        };
        move |rel: &str| {
            objects.path(
                &b.files
                    .get(rel)
                    .unwrap_or_else(|| panic!("no stock {rel}"))
                    .sha256,
            )
        }
    }

    fn groupings(path: &Path, res: &str) -> BTreeMap<String, BTreeMap<String, String>> {
        let p = swofty_formats::pkg::Pkg::parse(&read_frag(path, res, 0).unwrap()).unwrap();
        let mut out: BTreeMap<String, BTreeMap<String, String>> = BTreeMap::new();
        for g in p.root.o().unwrap().at("RouteGroupings").unwrap().a().unwrap() {
            let go = g.o().unwrap();
            let gid = go.str_at("Id").unwrap().to_owned();
            for r in go.at("Routes").unwrap().a().unwrap() {
                let rid = r.o().unwrap().str_at("Id").unwrap().to_owned();
                out.entry(gid.clone())
                    .or_default()
                    .insert(rid, node::canonical(r));
            }
        }
        out
    }

    #[test]
    #[ignore = "needs SWOFTY_STOCK_DATA, the launcher data folder that holds the game's baseline"]
    fn a_career_setup_page_builds_within_the_loader_rules() {
        let input = stock_input();
        let settings = setup_settings();
        let out = std::env::temp_dir().join(format!("swofty-career-setup-{}", std::process::id()));
        std::fs::remove_dir_all(&out).ok();
        let built = build(&settings, &input, &|_, _| None, &out, false).unwrap();
        let home = format!("swofty_setup_{SETUP_ROUTE}");
        let rows = format!("swofty_addon_{SETUP_ROUTE}_0");
        assert!(
            built
                .lines
                .iter()
                .any(|l| l.contains(&format!("career setup page {home}"))),
            "{:?}",
            built.lines
        );
        let session: BTreeMap<&str, bool> = built
            .session
            .iter()
            .map(|s| (s.name.as_str(), s.view_model))
            .collect();
        assert_eq!(session.get(home.as_str()), Some(&false));
        assert_eq!(session.get(rows.as_str()), Some(&true));

        let options = input(&ui_file("flow_options.erp"));
        let stock = check::ArchiveUi::read("flow_options.erp", &options).unwrap();
        let mut edited = check::ArchiveUi::read("flow_options.erp", &options).unwrap();
        for name in [&home, &rows] {
            let data = std::fs::read(&built.screens[&format!("{UI_DIR}/{name}.ui")]).unwrap();
            edited.resources.insert(
                format!("eaid://ui_package/screens/{name}.ui"),
                ("UIDF".to_owned(), vec![data]),
            );
        }
        let report = check::archives(&[edited], &[stock], catalog::stock(), catalog::engine()).unwrap();
        let hard: Vec<String> = report.hard.iter().map(ToString::to_string).collect();
        assert!(hard.is_empty(), "{hard:?}");

        let rows_ui = std::fs::read(&built.screens[&format!("{UI_DIR}/{rows}.ui")]).unwrap();
        let rows_ui = String::from_utf8_lossy(&rows_ui);
        for id in ["start_season", "rnd_mode", "team_moves"] {
            assert!(
                rows_ui.contains(&format!("swofty_next:t.progressive:{id}")),
                "{id}"
            );
        }
        assert!(!rows_ui.contains("t.progressive:show_hud"));
        let page = std::fs::read(&built.screens[&format!("{UI_DIR}/swofty_addon_0.ui")]).unwrap();
        let page = String::from_utf8_lossy(&page);
        assert!(page.contains("swofty_next:t.progressive:team_moves"));
        for hidden in ["dev_probe", "start_season"] {
            assert!(
                !page.contains(&format!("t.progressive:{hidden}")),
                "a hidden option stays off ADDON SETTINGS: {hidden}"
            );
        }

        let (src_p, dst_p) = (
            input(&ui_file("persistent.erp")),
            out.join(ui_file("persistent.erp")),
        );
        for res in [ROUTES, GAME_ROUTES, WIZARD_ROUTES] {
            let (before, after) = (groupings(&src_p, res), groupings(&dst_p, res));
            for (gid, routes) in &before {
                for (rid, r) in routes {
                    assert_eq!(after[gid].get(rid), Some(r), "{res}: {gid}.{rid} changed");
                }
            }
        }
        let after = groupings(&dst_p, ROUTES);
        let entry = &after["fe_career_dlc_select"][SETUP_ROUTE];
        for want in [home.as_str(), "options_flow"] {
            assert!(entry.contains(want), "{entry}");
        }
        assert!(!entry.contains("career_wizard_season_length_select"), "{entry}");
        assert!(entry.contains("Customisation"), "{entry}");
        let ids: Vec<&str> = after[&home].keys().map(String::as_str).collect();
        assert_eq!(
            ids,
            [
                "back",
                "close",
                "confirm",
                "open",
                "open_no_input",
                "open_options"
            ]
        );
        assert!(after[&home]["back"].contains("frontend_flow"));
        assert!(after[&home]["confirm"].contains("career_season_setup_flow"));
        let wizard = groupings(&dst_p, WIZARD_ROUTES);
        let back = &wizard["career_wizard_season_length_select"][&format!("back_{SETUP_ROUTE}")];
        for want in [
            home.as_str(),
            "options_flow",
            "career_wizard_summary",
            "slice_gen_screen_title",
            "Customisation",
            "AutoFocus",
        ] {
            assert!(back.contains(want), "{want}: {back}");
        }
        assert!(after[&home]["back"].contains("AutoFocus"));

        let stock_built = build(&setup_settings_without(), &input, &|_, _| None, &out, false).unwrap();
        assert!(!stock_built.lines.iter().any(|l| l.contains("career setup")));
        assert_eq!(
            read_frag(&src_p, WIZARD_ROUTES, 0).unwrap(),
            read_frag(&out.join(ui_file("persistent.erp")), WIZARD_ROUTES, 0).unwrap()
        );
        std::fs::remove_dir_all(&out).ok();
    }

    #[test]
    #[ignore = "needs SWOFTY_STOCK_DATA, the launcher data folder that holds the game's baseline"]
    fn a_hub_tab_page_builds_within_the_loader_rules() {
        let input = stock_input();
        let mut settings = setup_settings();
        for o in settings.addons[0]
            .options
            .iter_mut()
            .filter(|o| o.id == "rnd_mode")
        {
            o.hidden = true;
        }
        settings.addons[0].hub_tab = Some(crate::manifest::HubTab {
            name: "progressive".into(),
            title: "Progressive Settings".into(),
            options: vec!["rnd_mode".into(), "team_moves".into()],
            data_group: "f1_career_progressive".into(),
        });
        let out = std::env::temp_dir().join(format!("swofty-hub-tab-{}", std::process::id()));
        std::fs::remove_dir_all(&out).ok();
        let (mut lines, mut warnings) = (Vec::new(), Vec::new());
        let written = hub_tabs(&settings, &input, &|_, _| None, &out, &mut lines, &mut warnings).unwrap();
        assert_eq!(written, hub::ARCHIVES);
        assert!(warnings.is_empty(), "{warnings:?}");
        assert_eq!(lines.len(), 3, "{lines:?}");
        assert!(lines[0].starts_with("ui: hub tabs copy "), "{lines:?}");
        assert_eq!(
            lines[2],
            "ui: t.progressive has a hub tab template swofty_hub_progressive for f1_career_progressive careers in 2 \
             archive(s); its rows read view_model_data.hud_swofty_hub_progressive"
        );
        let templates = |path: &Path| -> BTreeMap<String, String> {
            let p = swofty_formats::pkg::Pkg::parse(&read_frag(path, hub::UI, hub::UI_FRAGMENT).unwrap())
                .unwrap();
            p.root
                .o()
                .unwrap()
                .at("Templates")
                .unwrap()
                .a()
                .unwrap()
                .iter()
                .map(|t| (t.name().to_owned(), node::canonical(t)))
                .collect()
        };
        let options = input(&ui_file(hub::OPTIONS));
        let options_file = ErpFile::open(&options).unwrap();
        let names =
            |f: &ErpFile| -> BTreeSet<String> { f.erp.resources.iter().map(|r| r.name.clone()).collect() };
        let mut shared = BTreeSet::new();
        for rel in hub::SHARED {
            shared.extend(names(&ErpFile::open(&input(&ui_file(rel))).unwrap()));
        }
        for rel in hub::ARCHIVES {
            let (before, after) = (
                templates(&input(&ui_file(rel))),
                templates(&out.join(ui_file(rel))),
            );
            for (name, t) in &before {
                assert_eq!(after.get(name), Some(t), "{rel}: {name} changed");
            }
            assert_eq!(after.len(), before.len() + 1, "{rel}");
            let page = &after["swofty_hub_progressive"];
            for want in [
                "#view_model_data.hud_swofty_hub_progressive.title",
                "#view_model_data.hud_swofty_hub_progressive.o0.label",
                "#view_model_data.hud_swofty_hub_progressive.o0.value",
                "#view_model_data.hud_swofty_hub_progressive.o1.label",
                "#view_model_data.hud_swofty_hub_progressive.o1.value",
                "swofty_next:t.progressive:rnd_mode",
                "swofty_prev:t.progressive:rnd_mode",
                "swofty_next:t.progressive:team_moves",
                "About rnd_mode.",
                "swofty_adjust",
                "options/options_edit_screen",
            ] {
                assert!(page.contains(want), "{rel}: {want}");
            }
            for unwanted in [
                "o2.label",
                "dataContext",
                "tab_content_emails_panel",
                "messages_panel",
                "email_list",
            ] {
                assert!(!page.contains(unwanted), "{rel}: {unwanted}");
            }

            let (src, dst) = (
                ErpFile::open(&input(&ui_file(rel))).unwrap(),
                ErpFile::open(&out.join(ui_file(rel))).unwrap(),
            );
            let (stock, built) = (names(&src), names(&dst));
            assert!(stock.is_subset(&built), "{rel}");
            let added: BTreeSet<String> = built.difference(&stock).cloned().collect();
            assert!(!added.is_empty(), "{rel}");
            for r in &src.erp.resources {
                if r.name == hub::UI {
                    continue;
                }
                let got = dst.erp.resources.iter().find(|d| d.name == r.name).unwrap();
                assert_eq!(got.hash, r.hash, "{rel}: {} changed", r.name);
            }
            let mut available = shared.clone();
            available.extend(built.iter().cloned());
            for name in &added {
                let copy = dst.load(dst.erp.find(name).unwrap()).unwrap();
                let original = options_file.load(options_file.erp.find(name).unwrap()).unwrap();
                assert_eq!(copy.kind, original.kind, "{rel}: {name}");
                assert_eq!(copy.fragments.len(), original.fragments.len(), "{rel}: {name}");
                for (a, b) in copy.fragments.iter().zip(&original.fragments) {
                    let data = a.data().unwrap();
                    assert_eq!(data, b.data().unwrap(), "{rel}: {name}");
                    for r in hub::references(&data) {
                        if !r.starts_with("eaid://ui_package/") || hub::resolves(&available, &r) {
                            continue;
                        }
                        assert!(
                            !hub::resolves(&names(&options_file), &r),
                            "{rel}: {name} uses {r}, which the archive lacks"
                        );
                    }
                }
            }
            for scene in hub::SCENES {
                assert!(available.contains(scene), "{rel}: {scene}");
            }

            let built_ui =
                Pkg::parse(&read_frag(&out.join(ui_file(rel)), hub::UI, hub::UI_FRAGMENT).unwrap()).unwrap();
            let tab = built_ui
                .root
                .o()
                .unwrap()
                .at("Templates")
                .unwrap()
                .a()
                .unwrap()
                .iter()
                .find(|t| t.name() == "swofty_hub_progressive")
                .unwrap();
            let on = node::canonical(&node::prop(node::boolv(true), None));
            let shown = |n: &Node| {
                n.props()
                    .unwrap()
                    .get("visible")
                    .is_none_or(|v| node::canonical(v) == on)
            };
            let kids: Vec<&str> = tab
                .slot(node::CHILDREN)
                .unwrap()
                .iter()
                .filter(|c| c.is_node())
                .map(|c| c.name())
                .collect();
            assert_eq!(
                kids,
                [
                    "fade_on",
                    "tool_tip_interface",
                    "hide_title",
                    "hide_filter",
                    "hide_scroll_bar",
                    "about",
                    "description",
                    "row_0",
                    "row_1"
                ],
                "{rel}"
            );
            for r in ["row_0", "row_1"] {
                let toggle = tab.child(r).unwrap().child("toggle").unwrap();
                assert_eq!(toggle.type_code(), "UIGP", "{rel}");
                assert!(!shown(toggle.child("indicator").unwrap()), "{rel}");
            }
            let scene = read_frag(&out.join(ui_file(rel)), hub::PAGE_SCENE, 0).unwrap();
            let scene = Pkg::parse(&scene).unwrap();
            assert!(idf::xref_mismatches(&scene).unwrap().is_empty(), "{rel}");
            assert!(idf::id_problems(&scene).unwrap().is_empty(), "{rel}");
            let scene = idf::scenes(&scene).unwrap()[0].o().unwrap().at("scene").unwrap();
            let parts: Vec<(&str, bool, &Node)> = tab
                .slot(node::CHILDREN)
                .unwrap()
                .iter()
                .filter(|c| c.is_node())
                .filter_map(|c| {
                    let step = c.slot(node::LAYOUT).ok()?.first()?.a().ok()?.last()?.o().ok()?;
                    let at = idf::find(scene, step.str_at("debugName").ok()?)?;
                    Some((c.name(), shown(c), at))
                })
                .collect();
            assert!(parts.iter().any(|p| p.0 == "row_0" && p.1), "{rel}");
            assert!(parts.iter().any(|p| p.0 == "description" && p.1), "{rel}");
            for (shown, _, at) in parts.iter().filter(|p| p.1) {
                for (hidden, _, over) in parts.iter().filter(|p| !p.1) {
                    assert!(
                        idf::find(over, idf::elem_name(at)).is_none(),
                        "{rel}: hidden {hidden} covers {shown}"
                    );
                }
            }
        }
        settings.addons[0].hub_tab = None;
        let none = hub_tabs(
            &settings,
            &input,
            &|_, _| None,
            &out,
            &mut Vec::new(),
            &mut Vec::new(),
        )
        .unwrap();
        assert!(none.is_empty());
        std::fs::remove_dir_all(&out).ok();
    }

    fn setup_settings_without() -> SessionSettings {
        let mut s = setup_settings();
        s.addons[0].career_setup = None;
        s
    }
    use super::*;
    use node::{NodeExt, ObjExt, arr, node, obj, s};
    use swofty_formats::erp::{Erp, Fragment, HEADER_LEN, MAGIC, Resource, STORED, kind};
    use swofty_formats::pkg::{Node, Pkg};

    const MOD: &str = "F1 2012 Season";

    fn pkg(root: Node) -> Vec<u8> {
        Pkg {
            name: *b"TEST",
            root,
            vca: Vec::new(),
            layout: None,
        }
        .to_bytes()
        .unwrap()
    }

    fn ui() -> Vec<u8> {
        pkg(obj(vec![(
            "Templates",
            arr(vec![node("dynamic_era_select", "UIGR", vec![], vec![], &[], &[])]),
        )]))
    }

    fn scene() -> Vec<u8> {
        pkg(obj(vec![(
            "scenelist",
            arr(vec![obj(vec![(
                "scene",
                obj(vec![("name", s("root")), ("children", arr(vec![]))]),
            )])]),
        )]))
    }

    fn resource(name: &str, ty: &str, frags: &[&[u8]]) -> Resource {
        let mut res = Resource {
            name: name.into(),
            kind: kind(ty).unwrap(),
            unk: vec![0; 6],
            fragments: frags
                .iter()
                .map(|d| Fragment {
                    name: *b"temp",
                    offset: 0,
                    size: d.len() as u64,
                    flags: 0,
                    compression: STORED,
                    packed: d.len() as u64,
                    raw: d.to_vec(),
                })
                .collect(),
            hash: [0; 16],
        };
        res.hash = res.content_hash().unwrap();
        res
    }

    fn archive(root: &Path, rel: &str, ui: &[u8]) -> PathBuf {
        let resources = vec![
            resource(era::UI, "UIDF", &[b"head", ui]),
            resource(era::SCENE, "UILayout", &[&scene()]),
            resource(era::TITLE_SCENE, "UILayout", &[&scene()]),
        ];
        write(root, rel, resources)
    }

    fn write(root: &Path, rel: &str, resources: Vec<Resource>) -> PathBuf {
        let mut header = [0u8; HEADER_LEN];
        header[..4].copy_from_slice(MAGIC);
        header[4..8].copy_from_slice(&4u32.to_le_bytes());
        let mut erp = Erp {
            header,
            version: 4,
            resources,
        };
        let path = root.join(ui_file(rel));
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(&path, erp.to_bytes()).unwrap();
        path
    }

    #[test]
    fn an_addon_era_picker_that_cannot_be_read_is_left_as_it_is() {
        let root = std::env::temp_dir().join(format!("swofty-era-skip-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let full = ui();
        let odd = full[..full.len() / 2].to_vec();
        let why = Pkg::parse(&odd).unwrap_err().to_string();
        let game = root.join("game");
        for rel in era::ARCHIVES {
            archive(&game, rel, &odd);
        }
        let input = |rel: &str| game.join(rel);
        let out = root.join("out");
        let from_mod = |_: &str, res: &str| (res == era::UI).then(|| MOD.to_owned());
        let (mut lines, mut warnings) = (Vec::new(), Vec::new());
        let written = era_rows(&input, &from_mod, &out, &mut lines, &mut warnings).unwrap();
        assert!(written.is_empty());
        assert!(!out.exists(), "nothing is written");
        assert!(lines.is_empty(), "{lines:?}");
        assert_eq!(warnings.len(), 2, "{warnings:?}");
        for (w, rel) in warnings.iter().zip(era::ARCHIVES) {
            assert!(
                w.starts_with(&format!("ui: {MOD} has its own copy of the era picker in {rel},")),
                "{w}"
            );
            assert!(
                w.contains("the era picker boxes there are off while it's on"),
                "{w}"
            );
            assert!(w.contains(&why), "{w}");
        }

        let stock = |_: &str, _: &str| None;
        let e = era_rows(&input, &stock, &out, &mut Vec::new(), &mut Vec::new()).unwrap_err();
        assert!(e.to_string().contains(&why), "{e}");

        let world_only = |erp: &str, res: &str| {
            (erp.ends_with(era::ARCHIVES[0]) && res == era::UI).then(|| MOD.to_owned())
        };
        let mut warnings = Vec::new();
        let e = era_rows(&input, &world_only, &out, &mut Vec::new(), &mut warnings).unwrap_err();
        assert!(e.to_string().contains(&why), "{e}");
        assert_eq!(warnings.len(), 1, "{warnings:?}");
        assert!(warnings[0].contains(era::ARCHIVES[0]), "{warnings:?}");
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn an_addon_era_picker_without_the_stock_templates_is_left_as_it_is() {
        let root = std::env::temp_dir().join(format!("swofty-era-templates-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let game = root.join("game");
        for rel in era::ARCHIVES {
            archive(&game, rel, &ui());
        }
        let input = |rel: &str| game.join(rel);
        let out = root.join("out");
        let from_mod = |_: &str, _: &str| Some(MOD.to_owned());
        let mut warnings = Vec::new();
        let written = era_rows(&input, &from_mod, &out, &mut Vec::new(), &mut warnings).unwrap();
        assert!(written.is_empty());
        assert_eq!(warnings.len(), 2, "{warnings:?}");
        assert!(
            warnings.iter().all(|w| w.contains("time_trial_select not found")),
            "{warnings:?}"
        );
        let stock = |_: &str, _: &str| None;
        let e = era_rows(&input, &stock, &out, &mut Vec::new(), &mut Vec::new()).unwrap_err();
        assert!(e.to_string().contains("time_trial_select not found"), "{e}");
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_missing_era_picker_resource_stays_an_error() {
        let root = std::env::temp_dir().join(format!("swofty-era-missing-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let game = root.join("game");
        let mut odd = ui();
        odd.extend_from_slice(&[0; 4]);
        archive(&game, era::ARCHIVES[0], &odd);
        write(
            &game,
            era::ARCHIVES[1],
            vec![resource(era::UI, "UIDF", &[b"head", &odd])],
        );
        let input = |rel: &str| game.join(rel);
        let from_mod = |_: &str, _: &str| Some(MOD.to_owned());
        let mut warnings = Vec::new();
        let e = era_rows(
            &input,
            &from_mod,
            &root.join("out"),
            &mut Vec::new(),
            &mut warnings,
        )
        .unwrap_err();
        assert!(e.to_string().contains(era::SCENE), "{e}");
        assert_eq!(warnings.len(), 1, "{warnings:?}");
        std::fs::remove_dir_all(&root).ok();
    }
}
