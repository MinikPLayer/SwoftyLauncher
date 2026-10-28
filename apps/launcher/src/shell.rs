use std::collections::{BTreeSet, HashMap, HashSet};
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariant};
use gpui_kit::component::dialog::AlertDialog;
use gpui_kit::component::input::{Input, InputState};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::slider::SliderState;
use gpui_kit::component::{Icon, TitleBar, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::crash::StartFailure;
use swofty_core::install::Source;
use swofty_core::launch;
use swofty_core::process::{GameProcess, stop_game_in};
use swofty_core::quarantine;
use swofty_core::recovery::{self, Blocked, Notice};
use swofty_core::saves::{self, SavesWarning};
use swofty_core::session::{LaunchMode, SessionRecord};
use swofty_core::stock;
use swofty_core::store::Store;
use swofty_core::view::View;

use crate::game::{self, Install, Launch, Owner, Probe};
use crate::game_edited::Edited;
use crate::library::{AddonIcon, Library, SectionItem};
use crate::pages::{addon_icon, label};
use crate::theme::{Palette, SCROLLBAR_GAP, ScrollArea, palette};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Page {
    News,
    Addons,
    Browser,
    Importer,
    Tools,
    Profiles,
    Database,
    Section(usize),
}

impl Page {
    pub fn label(self) -> &'static str {
        match self {
            Self::News => "News",
            Self::Addons => "Addons",
            Self::Browser => "Addon Browser",
            Self::Importer => "Simplified Importer",
            Self::Tools => "Tools",
            Self::Profiles => "Profiles",
            Self::Database => "DB Editor",
            Self::Section(_) => "",
        }
    }

    /// The sidebar's name for the page: the importer is just Importer there.
    pub fn nav_label(self) -> &'static str {
        match self {
            Self::Importer => "Importer",
            _ => self.label(),
        }
    }

    pub fn icon(self) -> IconName {
        match self {
            Self::News => IconName::Newspaper,
            Self::Addons => IconName::Puzzle,
            Self::Browser => IconName::PackageSearch,
            Self::Importer => IconName::FolderInput,
            Self::Tools => IconName::Wrench,
            Self::Profiles => IconName::Layers,
            Self::Database => IconName::Database,
            Self::Section(_) => IconName::Package,
        }
    }
}

pub enum PlayState {
    Detecting,
    NotInstalled,
    Ready,
    Launching,
    Restoring,
    Running,
    Elsewhere(Owner),
    Checking,
    Crashed(SessionRecord),
}

pub struct Shell {
    pub(crate) store: Store,
    pub(crate) page: Page,
    pub(crate) library: Library,
    pub(crate) open_addon: Option<String>,
    pub(crate) install: Option<Install>,
    pub(crate) copies: Vec<Install>,
    pub(crate) missing_copies: Vec<PathBuf>,
    pub(crate) detected: bool,
    pub(crate) rediscover: bool,
    pub(crate) processes: Vec<GameProcess>,
    pub(crate) owners: Vec<Owner>,
    pub(crate) session: Option<SessionRecord>,
    pub(crate) crash: Option<(PathBuf, Result<String, StartFailure>)>,
    pub(crate) dismissed: Option<PathBuf>,
    pub(crate) launch: Option<Launch>,
    pub(crate) notice: Option<String>,
    pub(crate) blocked: Option<Blocked>,
    pub(crate) game_edited: Option<Edited>,
    pub(crate) recovered_seen: f64,
    pub(crate) mods_blocked_seen: f64,
    pub(crate) not_stock_seen: f64,
    pub(crate) blocked_seen: f64,
    pub(crate) launch_failed_seen: f64,
    overlay_fallback_seen: f64,
    pub(crate) restoring: bool,
    pub(crate) sliders: HashMap<String, SliderSlot>,
    pub(crate) requests: Option<SystemTime>,
    pub(crate) console: Option<AnyWindowHandle>,
    pub(crate) views: HashMap<String, Result<Arc<View>, String>>,
    pub(crate) views_profile: String,
    pub(crate) view_gen: u64,
    pub(crate) view_gens: HashMap<String, u64>,
    pub(crate) busy: HashSet<String>,
    pub(crate) open_row: Option<String>,
    pub(crate) progress: Option<(f32, String)>,
    pub(crate) tasks: Option<crate::tasks::TaskDialog>,
    pub(crate) dropping: bool,
    pub(crate) vr_profiles: BTreeSet<String>,
    pub(crate) start_store: bool,
    pub(crate) updates: crate::update::Updates,
    pub(crate) forms: HashMap<String, crate::blocks::FormSlot>,
    pub(crate) searches: HashMap<String, crate::blocks::SearchSlot>,
    pub(crate) sorts: HashMap<String, (String, bool)>,
    /// The row whose detail panel is open, by table.
    pub(crate) table_rows: HashMap<String, String>,
    /// The scroll position and wheel motion of each addon page of blocks, by addon/section.
    pub(crate) section_scrolls: HashMap<String, (ScrollHandle, crate::scroll::SmoothScroll)>,
    /// Where each table's rows start in its page's scrolled content, from the last frame, so
    /// a long table draws only the rows in view.
    pub(crate) table_tops: Rc<std::cell::RefCell<HashMap<String, f32>>>,
    pub(crate) cell: Option<crate::blocks::CellEdit>,
    pub(crate) pending_cells: HashMap<String, String>,
    pub(crate) content_width: f32,
    pub(crate) short: bool,
    pub(crate) ticking: bool,
    painted: Instant,
    pub(crate) live_n: u64,
    /// The addon the Addons page highlights, and since when.
    pub(crate) highlight: Option<(String, Instant)>,
    pub(crate) addons: crate::addons::AddonsPage,
    pub(crate) db: crate::db::Db,
    pub(crate) news: crate::news::News,
    pub(crate) news_scroll: crate::news::NewsScroll,
    pub(crate) browser: crate::browser::Browser,
    pub(crate) tools: crate::tools::Carousel,
    _console_closed: Subscription,
    _poll: Task<()>,
    _live: Task<()>,
    _updates: Task<()>,
    _catalog: Task<()>,
}

pub(crate) struct Prepared {
    store: Store,
    library: Library,
    news: crate::news::News,
    config: swofty_core::session::Config,
    recovered_seen: f64,
    mods_blocked_seen: f64,
    not_stock_seen: f64,
    blocked_seen: f64,
    launch_failed_seen: f64,
    overlay_fallback_seen: f64,
}

impl Prepared {
    pub(crate) fn load() -> Self {
        let store = Store::open_default().with_builtins(swofty_builtins::all());
        let migrated = store.migrate();
        swofty_core::usage::launcher_heartbeats(store.root.clone());
        let recovered_seen = Notice::read(&store.root).map_or(0.0, |n| n.at);
        let mods_blocked_seen = quarantine::Report::read(&store.root).map_or(0.0, |r| r.at);
        let not_stock_seen = stock::Report::read(&store.root).map_or(0.0, |r| r.at);
        let blocked_seen = game::blocked_at(&store.root).unwrap_or(0.0);
        let launch_failed_seen = launch::Failure::read(&store.root).map_or(0.0, |f| f.at);
        let overlay_fallback_seen = swofty_core::overlayfs::Fallback::read(&store.root).map_or(0.0, |f| f.at);
        let mut library = Library::load(&store);
        match migrated {
            Ok(notes) => notes.iter().for_each(|n| eprintln!("swofty-launcher: {n}")),
            Err(e) => library.error = Some(e.to_string()),
        }
        let config = store.config();
        let news = crate::news::News::load(&store.root);
        Self {
            store,
            library,
            news,
            config,
            recovered_seen,
            mods_blocked_seen,
            not_stock_seen,
            blocked_seen,
            launch_failed_seen,
            overlay_fallback_seen,
        }
    }
}

pub struct SliderSlot {
    pub state: Entity<SliderState>,
    pub synced: usize,
    pub _changes: Subscription,
}

pub type Action = Rc<dyn Fn(&Store, &str) -> Result<String, String>>;

pub struct Prompt {
    pub title: &'static str,
    pub description: String,
    pub initial: String,
    pub placeholder: &'static str,
    pub ok: &'static str,
    pub browse: Option<PathPromptOptions>,
    pub action: Action,
}

fn section_label(title: &'static str, p: Palette) -> impl IntoElement {
    div()
        .text_size(px(11.))
        .font_weight(FontWeight::SEMIBOLD)
        .text_color(p.text_placeholder)
        .child(title)
}

fn terms_box(p: Palette) -> impl IntoElement {
    v_flex()
        .gap(px(6.))
        .p(px(12.))
        .rounded(px(6.))
        .bg(p.elevated)
        .border_1()
        .border_color(p.border)
        .child(
            h_flex()
                .gap(px(8.))
                .items_center()
                .text_color(p.accent)
                .child(Icon::new(IconName::CircleAlert).size(px(15.)))
                .child(
                    div()
                        .text_size(px(13.))
                        .font_weight(FontWeight::SEMIBOLD)
                        .text_color(p.text)
                        .child("Outside EA's terms of service"),
                ),
        )
        .child(div().text_size(px(13.)).text_color(p.text).child(
            "EA's terms don't cover playing without EA AntiCheat, even offline. We know of \
                 no action taken against players for offline modding, but EA can still act on \
                 accounts under its terms.",
        ))
}

/// The loader files an antivirus took: before the launch they stopped the
/// game from starting, in game it runs without mods. The dialog says which
/// files and how to get them back.
fn show_mods_blocked(r: &quarantine::Report, window: &mut Window, cx: &mut App) {
    let (title, text) = (r.title(), r.text());
    window.open_alert_dialog(cx, move |alert, _, _| {
        alert
            .width(px(560.))
            .title(title)
            .description(text.clone())
            .ok_text("OK")
    });
}

/// A launch that failed without a crash: F1 25 was already running, or the
/// game closed itself as it started. The dialog says why and what to do.
fn show_launch_failed(f: &launch::Failure, window: &mut Window, cx: &mut App) {
    let (title, text) = (f.title(), f.text());
    window.open_alert_dialog(cx, move |alert, _, _| {
        alert
            .width(px(560.))
            .title(title)
            .description(text.clone())
            .ok_text("OK")
    });
}

fn offline_terms(p: Palette) -> impl IntoElement {
    v_flex()
        .gap(px(14.))
        .child(div().text_size(px(13.)).text_color(p.text_muted).child(
            "Modded sessions run F1 25 without EA AntiCheat so the loader \
                 and addons can work. Everything is undone when the game exits.",
        ))
        .child(terms_box(p))
        .child(
            v_flex()
                .gap(px(7.))
                .child(section_label("OFFLINE ONLY", p))
                .child(div().text_size(px(13.)).text_color(p.text).child(
                    "Multiplayer is unavailable and cannot be enabled. The component is \
                         downloaded automatically and verified against pinned SHA-256 digests.",
                )),
        )
        .child(
            div()
                .text_size(px(12.))
                .text_color(p.text_muted)
                .child("We may collect anonymised usage data from your use of the launcher."),
        )
}

// Times shown as "5 min ago" stay current without a repaint every probe.
const REPAINT: Duration = Duration::from_secs(30);

impl Shell {
    pub fn new(prepared: Prepared, window: &mut Window, cx: &mut Context<Self>) -> Self {
        let Prepared {
            store,
            library,
            news,
            config,
            recovered_seen,
            mods_blocked_seen,
            not_stock_seen,
            blocked_seen,
            launch_failed_seen,
            overlay_fallback_seen,
        } = prepared;
        let vr_profiles = config.vr_profiles;
        let start_store = config.launch.start_store;
        let stale = store.clone();
        cx.background_executor()
            .spawn(async move {
                match game::recover_stale(&stale) {
                    Ok(lines) => lines.iter().for_each(|l| eprintln!("swofty-launcher: {l}")),
                    Err(e) => eprintln!("swofty-launcher: could not check for an unfinished session: {e}"),
                }
            })
            .detach();
        let poll = cx.spawn_in(window, async move |this, cx| {
            loop {
                let Ok((store, rediscover, seen)) = this.update(cx, |s, _| {
                    let r = std::mem::take(&mut s.rediscover);
                    (s.store.clone(), r, s.requests)
                }) else {
                    break;
                };
                let probe = cx
                    .background_executor()
                    .spawn(async move { game::probe(&store, rediscover, seen) })
                    .await;
                if this
                    .update_in(cx, |s, window, cx| s.apply(probe, window, cx))
                    .is_err()
                {
                    break;
                }
                cx.background_executor().timer(Duration::from_millis(1000)).await;
            }
        });
        let rediscover = cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(Duration::from_secs(10)).await;
                if this.update(cx, |s, _| s.rediscover = true).is_err() {
                    break;
                }
            }
        });
        rediscover.detach();
        let live = cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(Duration::from_millis(250)).await;
                if this.update(cx, |s, cx| s.live_poll(cx)).is_err() {
                    break;
                }
            }
        });
        let updates = Self::watch_updates(cx);
        let shell = cx.weak_entity();
        let console_closed = cx.on_window_closed(move |cx, id| {
            shell.update(cx, |s, cx| s.console_closed(id, cx)).ok();
        });
        let browser = crate::browser::Browser::new(&store.root, window, cx);
        let catalog = Self::watch_catalog(cx);
        let mut shell = Self {
            library,
            store,
            page: Page::News,
            open_addon: None,
            install: None,
            copies: Vec::new(),
            missing_copies: Vec::new(),
            detected: false,
            rediscover: true,
            processes: Vec::new(),
            owners: Vec::new(),
            session: None,
            crash: None,
            dismissed: None,
            launch: None,
            notice: None,
            blocked: None,
            game_edited: None,
            recovered_seen,
            mods_blocked_seen,
            not_stock_seen,
            blocked_seen,
            launch_failed_seen,
            overlay_fallback_seen,
            restoring: false,
            sliders: HashMap::new(),
            requests: None,
            console: None,
            views: HashMap::new(),
            views_profile: String::new(),
            view_gen: 0,
            view_gens: HashMap::new(),
            busy: HashSet::new(),
            open_row: None,
            progress: None,
            tasks: None,
            dropping: false,
            vr_profiles,
            start_store,
            updates: crate::update::Updates::new(),
            forms: HashMap::new(),
            searches: HashMap::new(),
            sorts: HashMap::new(),
            table_rows: HashMap::new(),
            section_scrolls: HashMap::new(),
            table_tops: Rc::default(),
            cell: None,
            pending_cells: HashMap::new(),
            content_width: 900.,
            short: false,
            ticking: false,
            painted: Instant::now(),
            live_n: 0,
            highlight: None,
            addons: crate::addons::AddonsPage::new(window, cx),
            db: crate::db::Db::new(cx),
            news,
            news_scroll: Default::default(),
            browser,
            tools: Default::default(),
            _console_closed: console_closed,
            _poll: poll,
            _live: live,
            _updates: updates,
            _catalog: catalog,
        };
        shell.refresh_views(cx);
        shell.fetch_news(cx);
        shell.sync_catalog_updates();
        shell.fetch_catalog(cx);
        shell.db_profile_changed(cx);
        if cfg!(windows) {
            cx.spawn_in(window, async move |_, cx| {
                let elevated = cx
                    .background_executor()
                    .spawn(async { swofty_core::compat::elevated() })
                    .await;
                if elevated {
                    let note = Notification::warning(
                        "Windows blocks dragging files onto a program that runs as administrator. Choose files \
                         with the launcher's buttons instead, or open Swofty Launcher normally to drag and drop.",
                    )
                    .title("Running as administrator");
                    cx.update(|window, cx| window.push_notification(note, cx)).ok();
                }
            })
            .detach();
        }
        shell
    }

    pub(crate) fn restore_console(&mut self, cx: &mut Context<Self>) {
        if self.store.config().console {
            self.console = crate::console::open(self.store.clone(), cx.weak_entity(), cx);
            cx.notify();
        }
    }

    pub(crate) fn set_console(&mut self, on: bool, window: &mut Window, cx: &mut Context<Self>) {
        match self.console.take() {
            Some(handle) if !on => cx.defer(move |cx| {
                handle.update(cx, |_, window, _| window.remove_window()).ok();
            }),
            Some(handle) => self.console = Some(handle),
            None if on => {
                self.console = crate::console::open(self.store.clone(), cx.weak_entity(), cx);
            }
            None => {}
        }
        let mut config = self.store.config();
        config.console = self.console.is_some();
        if let Err(e) = self.store.save_config(&config) {
            window.push_notification(Notification::error(e.to_string()), cx);
        }
        cx.notify();
    }

    fn console_closed(&mut self, id: WindowId, cx: &mut Context<Self>) {
        if self.console.is_some_and(|h| h.window_id() == id) {
            self.console = None;
            let mut config = self.store.config();
            config.console = false;
            self.store.save_config(&config).ok();
            cx.notify();
        }
    }

    fn apply(&mut self, probe: Probe, window: &mut Window, cx: &mut Context<Self>) {
        let before = (self.requests, self.processes.clone(), self.session.clone());
        let was_live = self.launch.is_some() || !self.processes.is_empty();
        self.requests = probe.requests;
        if let Some(n) = probe.recovered.filter(|n| n.at > self.recovered_seen) {
            self.recovered_seen = n.at;
            let mut text: Vec<String> = n.lines.iter().take(4).cloned().collect();
            if n.lines.len() > text.len() {
                text.push(format!(
                    "and {} more; the launch log has them all",
                    n.lines.len() - text.len()
                ));
            }
            window.push_notification(Notification::info(text.join("\n")).title(recovery::HEADLINE), cx);
        }
        if let Some(f) = probe
            .overlay_fallback
            .as_ref()
            .filter(|f| f.at > self.overlay_fallback_seen)
        {
            self.overlay_fallback_seen = f.at;
            self.notice = Some(f.note());
            window.push_notification(Notification::warning(f.note()).title("OverlayFS wasn't used"), cx);
        }
        if let Some(r) = probe
            .mods_blocked
            .as_ref()
            .filter(|r| r.at > self.mods_blocked_seen)
        {
            self.mods_blocked_seen = r.at;
            show_mods_blocked(r, window, cx);
        }
        if let Some(r) = probe.not_stock.as_ref().filter(|r| r.at > self.not_stock_seen) {
            self.not_stock_seen = r.at;
            self.game_edited = Some(Edited::NotStock(r.clone()));
            self.show_game_edited(window, cx);
        }
        if let Some(b) = probe
            .blocked
            .as_ref()
            .filter(|_| probe.blocked_at.is_some_and(|at| at > self.blocked_seen))
        {
            self.blocked_seen = probe.blocked_at.expect("blocked file timestamp");
            self.blocked = Some(b.clone());
            self.game_edited = Some(Edited::Leftovers(b.clone()));
            self.show_game_edited(window, cx);
        }
        if let Some(f) = probe
            .launch_failed
            .as_ref()
            .filter(|f| f.at > self.launch_failed_seen)
        {
            self.launch_failed_seen = f.at;
            self.notice = Some(f.headline().to_owned());
            show_launch_failed(f, window, cx);
        }
        if let Some(found) = probe.install {
            let changed = !self.detected || self.install != found.active || self.copies != found.all;
            self.install = found.active;
            self.copies = found.all;
            self.missing_copies = found.missing;
            self.detected = true;
            if changed || probe.merged {
                self.reload(cx);
            }
        } else if probe.merged {
            self.reload(cx);
        }
        self.processes = probe.processes;
        self.attribute();
        if let Some(launch) = self.launch.as_mut() {
            launch.saw_game |= self.owners.iter().any(|o| !matches!(o, Owner::Other(_)));
            launch.stage = probe.launch_stage;
            if let Some(ok) = launch.finished() {
                if !ok && !launch.saw_game {
                    self.blocked = Blocked::read(&self.store.root);
                    let refused = probe
                        .mods_blocked
                        .as_ref()
                        .is_some_and(|r| r.stage == quarantine::Stage::BeforeLaunch && r.at >= launch.at);
                    let not_stock = probe.not_stock.as_ref().is_some_and(|r| r.at >= launch.at);
                    let failed = probe
                        .launch_failed
                        .as_ref()
                        .filter(|f| f.at >= launch.at)
                        .map(|f| f.headline().to_owned());
                    self.notice = Some(match &self.blocked {
                        _ if refused => "an antivirus removed mod files, so F1 25 was not started".to_owned(),
                        _ if not_stock => match self.install.as_ref().map(|i| i.source) {
                            Some(Source::EaApp) => {
                                "your game files aren't the ones the EA app installed; repair them in the EA app"
                                    .to_owned()
                            }
                            _ => "your game files aren't the ones Steam installed; verify them in Steam".to_owned(),
                        },
                        Some(b) if b.files.len() == 1 => {
                            "a file Swofty did not put in the game folder is in the way".to_owned()
                        }
                        Some(b) => format!(
                            "{} files Swofty did not put in the game folder are in the way",
                            b.files.len()
                        ),
                        None => failed.unwrap_or_else(|| launch.failure()),
                    });
                }
                self.launch = None;
            }
        }
        if let Some(s) = probe.session.as_ref().filter(|s| s.crashed())
            && self.crash.as_ref().is_none_or(|(d, _)| *d != s.dir)
        {
            let session = s.clone();
            let game_dir = self.install.as_ref().map(|i| i.dir.clone());
            self.crash = Some((s.dir.clone(), Ok(String::new())));
            cx.spawn(async move |this, cx| {
                let dir = session.dir.clone();
                let summary = cx
                    .background_executor()
                    .spawn(async move { game::crash_summary(&session, game_dir.as_deref()) })
                    .await;
                this.update(cx, |s, cx| {
                    s.crash = Some((dir, summary));
                    cx.notify();
                })
                .ok();
            })
            .detach();
        }
        self.session = probe.session;
        self.db_poll(cx);
        let live = was_live || self.launch.is_some() || !self.processes.is_empty();
        if live
            || before != (self.requests, self.processes.clone(), self.session.clone())
            || self.painted.elapsed() >= REPAINT
        {
            cx.notify();
        }
    }

    pub(crate) fn reload(&mut self, cx: &mut Context<Self>) {
        self.library = Library::load(&self.store);
        let config = self.store.config();
        self.vr_profiles = config.vr_profiles;
        self.start_store = config.launch.start_store;
        if matches!(self.page, Page::Section(i) if i >= self.library.sections.len()) {
            self.page = Page::Addons;
        }
        self.refresh_views(cx);
        self.db_profile_changed(cx);
        self.sync_catalog_updates();
        cx.notify();
    }

    /// The copy addon panels work with: the one a running game belongs to,
    /// else the one Play uses.
    pub(crate) fn panel_install(&self) -> Option<Install> {
        self.owners
            .iter()
            .find_map(|o| match o {
                Owner::Other(i) => Some(i.clone()),
                _ => None,
            })
            .or_else(|| self.install.clone())
    }

    fn attribute(&mut self) {
        self.owners = self
            .processes
            .iter()
            .map(|g| game::owner(g, self.install.as_ref(), &self.copies))
            .collect();
    }

    pub(crate) fn play_state(&self) -> PlayState {
        if self.owners.contains(&Owner::Active) {
            return PlayState::Running;
        }
        if let Some(launch) = &self.launch {
            return if launch.saw_game {
                PlayState::Restoring
            } else {
                PlayState::Launching
            };
        }
        if self.restoring {
            return PlayState::Restoring;
        }
        if !self.detected {
            return PlayState::Detecting;
        }
        if let Some(owner) = self.owners.first() {
            return PlayState::Elsewhere(owner.clone());
        }
        if self.install.is_none() {
            return PlayState::NotInstalled;
        }
        if self.addons.checking {
            return PlayState::Checking;
        }
        match &self.session {
            Some(s)
                if s.crashed()
                    && self.dismissed.as_ref() != Some(&s.dir)
                    && s.ended.is_some_and(|e| game::now_secs() - e < 1800.0) =>
            {
                PlayState::Crashed(s.clone())
            }
            _ => PlayState::Ready,
        }
    }

    pub(crate) fn play(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(install) = self.install.clone() else {
            return;
        };
        if install.source == Source::EaApp && self.ea_app_warn_pending() {
            self.ea_app_warn(window, cx);
            return;
        }
        if self.addons.checking {
            window.push_notification(Notification::info("Play when the conflict check finishes."), cx);
            return;
        }
        if swofty_core::compat::write_blocker(&install.dir).is_some() {
            self.show_admin_needed(&install, window, cx);
            return;
        }
        if !swofty_core::setup::done(&self.store.root) {
            self.open_setup(window, cx);
            return;
        }
        if !swofty_core::offline::is_accepted(&self.store.root) {
            self.confirm_offline_play(window, cx);
            return;
        }
        if swofty_core::setup::first_launch(&self.store.root) {
            self.first_launch_notice(window, cx);
            return;
        }
        self.notice = None;
        self.blocked = None;
        self.game_edited = None;
        Blocked::clear(&self.store.root).ok();
        self.blocked_seen = game::blocked_at(&self.store.root).unwrap_or(0.0);
        self.dismissed = self.session.as_ref().map(|s| s.dir.clone());
        match Launch::start(
            &self.store,
            &self.library.profile.name,
            &install,
            self.launch_mode(),
        ) {
            Ok(l) => self.launch = Some(l),
            Err(e) => {
                self.launch_failed_seen = game::now_secs();
                window.push_notification(
                    Notification::error(e)
                        .title("Could not launch F1 25")
                        .autohide(false),
                    cx,
                );
            }
        }
        cx.notify();
    }

    pub(crate) fn launch_mode(&self) -> LaunchMode {
        if self.vr_profiles.contains(&self.library.profile.name) {
            LaunchMode::Vr
        } else {
            LaunchMode::Normal
        }
    }

    pub(crate) fn set_start_store(&mut self, on: bool, window: &mut Window, cx: &mut Context<Self>) {
        let mut config = self.store.config();
        config.launch.start_store = on;
        if let Err(e) = self.store.save_config(&config) {
            window.push_notification(Notification::error(e.to_string()), cx);
        }
        self.start_store = config.launch.start_store;
        cx.notify();
    }

    /// The game switcher: play another copy of F1 25.
    pub(crate) fn use_copy(&mut self, dir: PathBuf, window: &mut Window, cx: &mut Context<Self>) {
        let mut config = self.store.config();
        swofty_core::copies::set_active(&mut config, &dir);
        if let Err(e) = self.store.save_config(&config) {
            window.push_notification(Notification::error(e.to_string()), cx);
            return;
        }
        if let Some(copy) = self.copies.iter().find(|c| c.dir == dir) {
            self.install = Some(copy.clone());
        }
        self.attribute();
        self.notice = None;
        self.rediscover = true;
        self.reload(cx);
    }

    pub(crate) fn set_launch_mode(&mut self, mode: LaunchMode, window: &mut Window, cx: &mut Context<Self>) {
        let mut config = self.store.config();
        config.set_launch_mode(&self.library.profile.name, mode);
        if let Err(e) = self.store.save_config(&config) {
            window.push_notification(Notification::error(e.to_string()), cx);
        }
        self.vr_profiles = config.vr_profiles;
        cx.notify();
    }

    /// First launch: the offline play terms, shown as a dialog on Play.
    /// Accepting records consent and launches; declining changes nothing.
    pub(crate) fn confirm_offline_play(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let shell = cx.entity().downgrade();
        let store = self.store.clone();
        let p = palette(cx);
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (shell, store) = (shell.clone(), store.clone());
            alert
                .width(px(560.))
                .title("Modded play runs offline")
                .child(offline_terms(p))
                .ok_text("Accept and launch")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    if let Err(e) = swofty_core::offline::record_accept(&store.root) {
                        window.push_notification(Notification::error(e.to_string()), cx);
                        return false;
                    }
                    shell.update(cx, |s, cx| s.play(window, cx)).ok();
                    true
                })
        });
    }

    pub(crate) fn restore_game_folder(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(install) = self.install.clone() else {
            return;
        };
        let store = self.store.clone();
        self.notice = None;
        self.blocked = None;
        self.game_edited = None;
        self.restoring = true;
        cx.spawn_in(window, async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move { game::restore(&store, install.dir) })
                .await;
            this.update_in(cx, |s, window, cx| {
                s.restoring = false;
                match result {
                    Ok(text) => window
                        .push_notification(Notification::success(text).title("Game folder restored"), cx),
                    Err(e) => window.push_notification(Notification::error(e).title("Restore failed"), cx),
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    pub(crate) fn confirm_stop(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(dir) = self.install.as_ref().map(|i| i.dir.clone()) else {
            return;
        };
        let data = self.store.root.clone();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (dir, data) = (dir.clone(), data.clone());
            alert
                .title("Stop F1 25?")
                .description("The game closes immediately. Unsaved progress is lost.")
                .ok_text("Stop game")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    swofty_core::session::request_stop(&data);
                    if let Err(e) = stop_game_in(&dir) {
                        window.push_notification(Notification::error(e.to_string()), cx);
                    }
                    true
                })
        });
    }

    pub(crate) fn set_addon_enabled(
        &mut self,
        id: &str,
        on: bool,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let registry = self.library.registry.clone();
        let warning = (!on)
            .then(|| registry.turn_off_warning(&self.library.profile, id))
            .flatten();
        let Some(warning) = warning else {
            self.update_profile(window, cx, |p| registry.set_enabled(p, id, on));
            return;
        };
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (shell, id) = (shell.clone(), warning.id.clone());
            saves_alert(alert, &warning, None, p)
                .ok_text("Turn off anyway")
                .on_ok(move |_, window, cx| {
                    shell
                        .update(cx, |s, cx| {
                            let registry = s.library.registry.clone();
                            s.update_profile(window, cx, |p| registry.set_enabled(p, &id, false));
                        })
                        .ok();
                    true
                })
        });
        cx.notify();
    }

    pub(crate) fn remove_addon(&mut self, id: &str, name: &str, window: &mut Window, cx: &mut Context<Self>) {
        const REMOVES: &str = "Its files are deleted and it is taken out of every profile. \
                               You can install it again later.";
        const REMOVES_DB: &str = "Your DB Editor changes are deleted and it is taken out of every profile. \
                                  To keep them, use Share addon (.zip) in the DB Editor's Export menu first.";
        let db_editor = self
            .library
            .addons
            .iter()
            .any(|a| a.manifest.id == id && a.source == crate::library::Source::DbEditor);
        let removes = if db_editor { REMOVES_DB } else { REMOVES };
        let shell = cx.entity().downgrade();
        let store = self.store.clone();
        let id = id.to_owned();
        let name = name.to_owned();
        let warning = self
            .library
            .addons
            .iter()
            .find(|a| a.manifest.id == id)
            .and_then(|a| SavesWarning::of(&a.manifest));
        let p = palette(cx);
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (shell, store, id, name) = (shell.clone(), store.clone(), id.clone(), name.clone());
            let alert = match &warning {
                Some(w) => saves_alert(alert, w, Some(removes), p).ok_text("Remove anyway"),
                None => alert
                    .title(format!("Remove {name}?"))
                    .description(removes)
                    .ok_text("Remove addon"),
            };
            alert
                .ok_variant(ButtonVariant::Danger)
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    match store.remove_addon(&id) {
                        Ok(()) => {
                            window.push_notification(Notification::success(format!("Removed {name}")), cx);
                            shell
                                .update(cx, |s, cx| {
                                    s.open_addon = None;
                                    s.reload(cx);
                                })
                                .ok();
                        }
                        Err(e) => {
                            window.push_notification(Notification::error(e.to_string()), cx);
                        }
                    }
                    true
                })
        });
    }

    pub(crate) fn update_profile(
        &mut self,
        window: &mut Window,
        cx: &mut Context<Self>,
        f: impl FnOnce(&mut swofty_core::profile::Profile) -> swofty_core::Result<()>,
    ) {
        if let Err(e) = self.store.update_profile(&self.library.profile.name, f) {
            window.push_notification(Notification::error(e.to_string()), cx);
        }
        self.reload(cx);
    }

    pub(crate) fn switch_profile(&mut self, name: &str, window: &mut Window, cx: &mut Context<Self>) {
        if let Err(e) = self.store.set_active_profile(name) {
            window.push_notification(Notification::error(e.to_string()), cx);
        }
        self.reload(cx);
    }

    pub(crate) fn locate_game(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let adding = !self.copies.is_empty();
        self.prompt(
            Prompt {
                title: if adding { "Add another folder" } else { "Locate F1 25" },
                description: format!(
                    "The folder that contains {}, from Steam, the EA app or a copy of your own. Play uses it once it's added.",
                    swofty_core::GAME_EXE
                ),
                initial: String::new(),
                placeholder: if cfg!(windows) {
                    r"C:\Program Files\EA Games\F1 25"
                } else {
                    "/path/to/F1 25"
                },
                ok: "Add and play this copy",
                browse: Some(PathPromptOptions {
                    files: false,
                    directories: true,
                    multiple: false,
                    prompt: Some("Use folder".into()),
                }),
                action: Rc::new(|store, value| {
                    let dir = PathBuf::from(value);
                    let mut config = store.config();
                    swofty_core::copies::add(&mut config, &dir)?;
                    store.save_config(&config).map_err(|e| e.to_string())?;
                    Ok(format!(
                        "Added the {} copy",
                        swofty_core::copies::store_name(Source::detect(&dir))
                    ))
                }),
            },
            window,
            cx,
        );
    }

    pub(crate) fn prompt(&mut self, prompt: Prompt, window: &mut Window, cx: &mut Context<Self>) {
        let input = cx.new(|cx| {
            InputState::new(window, cx)
                .placeholder(prompt.placeholder)
                .default_value(prompt.initial.clone())
        });
        let shell = cx.entity().downgrade();
        let store = self.store.clone();
        let focus = std::cell::Cell::new(true);
        window.open_alert_dialog(cx, move |alert, window, cx| {
            if focus.replace(false) {
                input.update(cx, |s, cx| s.focus(window, cx));
            }
            let field = input.clone();
            let browse = prompt.browse.clone().map(|options| {
                Button::new("browse")
                    .icon(IconName::FolderOpen)
                    .label("Browse…")
                    .on_click(move |_, window, cx| {
                        let rx = cx.prompt_for_paths(options.clone());
                        let field = field.clone();
                        window
                            .spawn(cx, async move |cx| {
                                if let Ok(Ok(Some(paths))) = rx.await
                                    && let Some(path) = paths.first()
                                {
                                    let path = path.display().to_string();
                                    cx.update(|window, cx| {
                                        field.update(cx, |s, cx| s.set_value(path, window, cx))
                                    })
                                    .ok();
                                }
                            })
                            .detach();
                    })
            });
            let (input, shell, store, action) =
                (input.clone(), shell.clone(), store.clone(), prompt.action.clone());
            alert
                .width(px(560.))
                .title(prompt.title)
                .description(prompt.description.clone())
                .child(
                    h_flex()
                        .w_full()
                        .gap_2()
                        .child(div().flex_1().child(Input::new(&input)))
                        .children(browse),
                )
                .ok_text(prompt.ok)
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    let value = input.read(cx).value().trim().to_owned();
                    match action(&store, &value) {
                        Ok(message) => {
                            shell
                                .update(cx, |s, cx| {
                                    s.rediscover = true;
                                    s.reload(cx)
                                })
                                .ok();
                            window.push_notification(Notification::success(message), cx);
                            true
                        }
                        Err(e) => {
                            window.push_notification(Notification::error(e), cx);
                            false
                        }
                    }
                })
        });
    }

    fn title_bar(&self, p: Palette) -> impl IntoElement {
        TitleBar::new().bg(p.chrome).child(
            h_flex().flex_1().justify_end().pr(px(8.)).child(
                div()
                    .text_size(px(12.))
                    .text_color(p.text_placeholder)
                    .child(concat!("v", env!("CARGO_PKG_VERSION"))),
            ),
        )
    }

    pub(crate) fn cancel_scroll(&self) {
        self.news_scroll.cancel();
        self.addons.cancel_scroll();
        self.browser.smooth.cancel();
        self.db.rows_motion.cancel();
        self.db.list_motion.cancel();
    }

    pub(crate) fn open_page(&mut self, page: Page, cx: &mut Context<Self>) {
        if self.page != page {
            self.cancel_scroll();
        }
        self.db_page_change(page, cx);
        self.page = page;
        if let Some(i) = self.open_section().filter(|i| !self.section_loaded(*i)) {
            self.fetch_views(Some(i), cx);
        }
        cx.notify();
    }

    fn section_loaded(&self, i: usize) -> bool {
        self.library
            .sections
            .get(i)
            .is_some_and(|s| self.views.contains_key(&format!("{}/{}", s.addon, s.spec.id)))
    }

    fn big_nav(&self, page: Page, count: Option<usize>, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let art = NavArt::Icon(page.icon());
        self.big_row(page, page.nav_label().into(), art, count, p, cx)
    }

    fn big_row(
        &self,
        page: Page,
        title: SharedString,
        art: NavArt,
        count: Option<usize>,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let active = self.page == page;
        h_flex()
            .id(SharedString::from(format!("nav-{title}")))
            .h(px(40.))
            .flex_shrink_0()
            .pl(px(6.))
            .pr(px(10.))
            .gap(px(10.))
            .rounded(px(8.))
            .border_1()
            .cursor_pointer()
            .text_size(px(14.))
            .font_weight(FontWeight::MEDIUM)
            .map(|d| {
                if active {
                    d.bg(p.selected).border_color(p.accent).text_color(p.text)
                } else {
                    d.border_color(transparent_black())
                        .text_color(p.text_muted)
                        .hover(move |s| s.bg(p.hover).text_color(p.text))
                }
            })
            .child(nav_art(art, 28., active, p))
            .child(div().flex_1().truncate().child(title))
            .children(count.map(|n| {
                div()
                    .text_size(px(12.))
                    .text_color(if active { p.text_muted } else { p.text_placeholder })
                    .child(n.to_string())
            }))
            .on_click(cx.listener(move |this, _, _, cx| this.open_page(page, cx)))
            .into_any_element()
    }

    /// A section's sidebar art: its `sections/<id>.svg`, else its addon's icon, else
    /// the icon its manifest names.
    fn section_art(&self, s: &SectionItem) -> NavArt {
        let addon = self.library.addons.iter().find(|a| a.manifest.id == s.addon);
        match s.icon.clone().or_else(|| addon.and_then(|a| a.icon.clone())) {
            Some(AddonIcon::Glyph(data)) => NavArt::Glyph(data),
            Some(AddonIcon::Image(image)) => NavArt::Image(image),
            None => NavArt::Icon(crate::sections::icon(s.spec.icon)),
        }
    }

    /// Simplified for Swofty's pages, in the order its manifest lists them: the
    /// importer, as Importer with the import icon, and every other page with its
    /// own title and art. Until the addon is on, just the importer, which offers
    /// to install or turn it on.
    fn simplified(&self, p: Palette, cx: &mut Context<Self>) -> Vec<AnyElement> {
        let mut rows = Vec::new();
        let mut importer = false;
        for (i, s) in self.library.sections.iter().enumerate() {
            if !s.is_simplified() || s.is_importer_tracks() {
                continue;
            }
            if s.is_importer() {
                importer = true;
                rows.push(self.big_nav(Page::Importer, None, p, cx));
                continue;
            }
            let art = self.section_art(s);
            let count = self.section_count(i);
            rows.push(self.big_row(Page::Section(i), s.spec.title.clone().into(), art, count, p, cx));
        }
        if !importer {
            rows.push(self.big_nav(Page::Importer, None, p, cx));
        }
        rows
    }

    fn panel_nav(
        &self,
        page: Page,
        title: SharedString,
        art: NavArt,
        count: Option<usize>,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let active = self.page == page;
        h_flex()
            .id(SharedString::from(format!("panel-{title}")))
            .h(px(32.))
            .flex_shrink_0()
            .px(px(8.))
            .gap(px(10.))
            .rounded(px(6.))
            .border_1()
            .cursor_pointer()
            .text_size(px(13.))
            .map(|d| {
                if active {
                    d.bg(p.selected).border_color(p.accent).text_color(p.text)
                } else {
                    d.border_color(transparent_black())
                        .text_color(p.text_muted)
                        .hover(move |s| s.bg(p.hover).text_color(p.text))
                }
            })
            .child(panel_art(art, active, p))
            .child(div().flex_1().truncate().child(title))
            .children(count.map(|n| {
                div()
                    .text_size(px(11.))
                    .text_color(p.text_placeholder)
                    .child(n.to_string())
            }))
            .on_click(cx.listener(move |this, _, _, cx| this.open_page(page, cx)))
            .into_any_element()
    }

    /// The addon panels the sidebar lists: the DB Editor, then every addon
    /// page except Simplified for Swofty's, which have their own heading.
    fn panels(&self, p: Palette, cx: &mut Context<Self>) -> Vec<AnyElement> {
        let db = Page::Database;
        let mut rows = vec![self.panel_nav(
            db,
            db.label().into(),
            NavArt::Icon(db.icon()),
            Some(self.db_count()).filter(|n| *n > 0),
            p,
            cx,
        )];
        for (i, s) in self.library.sections.iter().enumerate() {
            if s.is_simplified() {
                continue;
            }
            let art = self.section_art(s);
            rows.push(self.panel_nav(
                Page::Section(i),
                s.spec.title.clone().into(),
                art,
                self.section_count(i),
                p,
                cx,
            ));
        }
        rows
    }

    fn sidebar(&self, p: Palette, short: bool, cx: &mut Context<Self>) -> impl IntoElement {
        let big = [
            (Page::News, None),
            (Page::Addons, Some(self.library.listed().count())),
            (Page::Browser, None),
            (Page::Tools, None),
            (Page::Profiles, Some(self.library.profiles.len())),
        ];
        v_flex()
            .w(px(220.))
            .h_full()
            .flex_shrink_0()
            .px(px(10.))
            .pt(px(4.))
            .pb(px(10.))
            .gap(px(8.))
            .bg(p.chrome)
            .child(
                h_flex()
                    .flex_shrink_0()
                    .justify_center()
                    .pt(px(2.))
                    .pb(px(if short { 6. } else { 14. }))
                    .child(crate::pages::wordmark(if short { 112. } else { 148. })),
            )
            .child(
                v_flex()
                    .flex_shrink_0()
                    .gap(px(2.))
                    .children(big.map(|(page, count)| self.big_nav(page, count, p, cx))),
            )
            .child(
                v_flex()
                    .flex_shrink_0()
                    .pt(px(if short { 4. } else { 12. }))
                    .gap(px(6.))
                    .child(label("SIMPLIFIED FOR SWOFTY", p).px(px(8.)))
                    .children(self.simplified(p, cx)),
            )
            .child(
                v_flex()
                    .flex_1()
                    .min_h_0()
                    .mr(px(-SCROLLBAR_GAP))
                    .pt(px(if short { 4. } else { 12. }))
                    .gap(px(6.))
                    .child(label("ADDON PANELS", p).px(px(8.)))
                    .child(
                        v_flex()
                            .flex_1()
                            .min_h_0()
                            .scroll_y("panels")
                            .pr(px(SCROLLBAR_GAP))
                            .gap(px(1.))
                            .children(self.panels(p, cx)),
                    ),
            )
            .child(self.send_logs_button(p, cx))
            .child(
                v_flex()
                    .flex_shrink_0()
                    .gap(px(6.))
                    .pt(px(4.))
                    .pb(px(2.))
                    .child(label("ACTIVE PROFILE", p).px(px(4.)))
                    .child(self.profile_picker(p, cx)),
            )
    }
}

enum NavArt {
    Icon(IconName),
    Glyph(Arc<[u8]>),
    Image(Arc<Image>),
}

fn nav_art(art: NavArt, size: f32, active: bool, p: Palette) -> Div {
    let color = if active { p.on_accent } else { p.text_muted };
    let frame = || {
        div()
            .size(px(size))
            .flex_shrink_0()
            .flex()
            .items_center()
            .justify_center()
            .rounded(px(size * 0.25))
            .bg(if active { p.accent } else { p.surface })
    };
    match art {
        NavArt::Image(image) => addon_icon(&AddonIcon::Image(image), size, p),
        NavArt::Glyph(data) => frame().child(svg().data(&data).size(px(size * 0.55)).text_color(color)),
        NavArt::Icon(icon) => frame().child(Icon::new(icon).size(px(size * 0.55)).text_color(color)),
    }
}

fn panel_art(art: NavArt, active: bool, p: Palette) -> Div {
    let color = if active {
        p.accent_hover
    } else {
        p.text_placeholder
    };
    let frame = div()
        .size(px(18.))
        .flex_shrink_0()
        .flex()
        .items_center()
        .justify_center();
    match art {
        NavArt::Image(image) => frame.child(addon_icon(&AddonIcon::Image(image), 18., p)),
        NavArt::Glyph(data) => frame.child(svg().data(&data).size(px(16.)).text_color(color)),
        NavArt::Icon(icon) => frame.child(Icon::new(icon).size(px(16.)).text_color(color)),
    }
}

impl Render for Shell {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        self.painted = Instant::now();
        let p = palette(cx);
        let wide = window.viewport_size().width > px(1500.);
        let short = window.viewport_size().height < px(760.);
        self.short = short;
        self.content_width =
            (f32::from(window.viewport_size().width) - 220. - 2. * crate::pages::GUTTER).max(320.);
        self.sync_sliders(window, cx);
        self.sync_blocks(window, cx);
        self.db_pending_tab(window, cx);
        if self.page == Page::Database {
            self.sync_db(window, cx);
        }
        for note in std::mem::take(&mut self.db.toasts) {
            window.push_notification(note, cx);
        }
        for note in std::mem::take(&mut self.browser.toasts) {
            window.push_notification(note, cx);
        }
        let content = match self.page {
            Page::News => self.news_page(p, short, cx),
            Page::Importer => self.importer_page(p, cx),
            Page::Tools => self.tools_page(p, window, cx),
            Page::Addons => self.addons_page(p, cx),
            Page::Browser => self.browser_page(p, cx),
            Page::Database => self.db_page(p, cx),
            Page::Profiles => self.profiles_page(p, wide, cx),
            Page::Section(i) => self.section_page(i, p, cx),
        };
        if self.dropping && !cx.has_active_drag() {
            self.dropping = false;
        }
        v_flex()
            .id("shell")
            .relative()
            .size_full()
            .on_drag_move::<ExternalPaths>(cx.listener(|this, _, _, cx| {
                if !this.dropping {
                    this.dropping = true;
                    cx.notify();
                }
            }))
            .on_drop(cx.listener(|this, paths: &ExternalPaths, window, cx| {
                this.drop_paths(paths.paths().to_vec(), window, cx)
            }))
            .bg(p.background)
            .text_color(p.text)
            .text_size(px(14.))
            .child(self.title_bar(p))
            .child(
                h_flex()
                    .flex_1()
                    .min_h_0()
                    .items_start()
                    .bg(p.chrome)
                    .child(self.sidebar(p, short, cx))
                    .child(
                        v_flex()
                            .flex_1()
                            .h_full()
                            .min_w_0()
                            .overflow_hidden()
                            .rounded_tl(px(16.))
                            .bg(p.background)
                            .child(content),
                    ),
            )
            .children(self.update_bar(p))
            .child(self.play_bar(p, wide, cx))
            .children(self.progress_card(p))
            .children(self.task_dialog(p, window, cx))
            .when(self.dropping, |d| d.child(self.drop_overlay(p)))
    }
}

fn saves_alert(alert: AlertDialog, w: &SavesWarning, then: Option<&'static str>, p: Palette) -> AlertDialog {
    let note = w.message.clone().map(|m| {
        h_flex()
            .gap(px(10.))
            .items_start()
            .p(px(12.))
            .rounded(px(6.))
            .bg(p.elevated)
            .border_1()
            .border_color(p.border)
            .child(
                Icon::new(IconName::CircleAlert)
                    .size(px(15.))
                    .text_color(p.warning),
            )
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .gap(px(2.))
                    .child(
                        div()
                            .text_size(px(13.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .text_color(p.text)
                            .child(w.name.clone()),
                    )
                    .child(div().text_size(px(13.)).text_color(p.text_muted).child(m)),
            )
    });
    alert
        .width(px(480.))
        .title(saves::TITLE)
        .child(
            v_flex()
                .gap(px(12.))
                .child(div().text_size(px(13.)).text_color(p.text).child(saves::BODY))
                .children(note)
                .when_some(then, |d, t| {
                    d.child(div().text_size(px(13.)).text_color(p.text_muted).child(t))
                }),
        )
        .ok_variant(ButtonVariant::Danger)
        .show_cancel(true)
}
