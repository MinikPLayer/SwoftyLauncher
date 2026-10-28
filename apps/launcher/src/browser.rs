//! The Addon Browser: the addons published to the Swofty registry, to search,
//! sort, filter, star, read about, and install or update in one click.

mod authors;

pub(crate) use authors::View;

use std::collections::{BTreeMap, BTreeSet, HashMap, VecDeque};
use std::path::Path;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::input::{Input, InputEvent, InputState};
use gpui_kit::component::menu::{DropdownMenu, PopupMenuItem};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::scroll::ScrollableElement;
use gpui_kit::component::spinner::Spinner;
use gpui_kit::component::tooltip::Tooltip;
use gpui_kit::component::{ActiveTheme, Disableable, Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::catalog::publishers::{self, Creator, Profile};
use swofty_core::catalog::requires::{self, Event, Have, Plan};
use swofty_core::catalog::search::Query;
use swofty_core::catalog::stars::{self, Stars};
use swofty_core::catalog::{self, Addon, Stage, Trouble, Update};
use swofty_core::manifest::Category;
use swofty_core::update::current_version;

use crate::addons::{FILTERS_W, Type, ago, facet_row};
use crate::library::{Source, plural};
use crate::news::format_date;
use crate::pages::{GUTTER, TRACK, badge, label, stat, tag, tile};
use crate::play_bar::link;
use crate::shell::{Page, PlayState, Shell};
use crate::theme::{Palette, SCROLLBAR_GAP, ScrollArea};

const STALE: Duration = Duration::from_secs(30 * 60);
const TICK: Duration = Duration::from_secs(5 * 60);
const STAR_DELAY: Duration = Duration::from_secs(1);
const ACTIVITY_KEPT: usize = 50;
const ICONS_DIR: &str = "icons";
const AVATARS_DIR: &str = "avatars";
const CARD_W: f32 = 300.;
const GAP: f32 = 14.;
const SIDE_W: f32 = 340.;
const AUTHORS_SHOWN: usize = 8;
const TILE_MIN: f32 = 220.;
const TILE_ART: f32 = 96.;
const TILE_FACES: usize = 4;
const FACE: f32 = 40.;
const HELMET: &[u8] = include_bytes!("../assets/helmet.svg");
const NATIVE: &str = "Runs code in the game";
const VERIFYING: u8 = 1;
const INSTALLING: u8 = 2;

fn day(iso: &str) -> String {
    format_date(iso.get(..10).unwrap_or(iso))
}

fn since(iso: &str) -> Option<String> {
    catalog::epoch(iso).map(|s| ago(s as f64))
}

fn since_time(t: SystemTime) -> String {
    ago(t.duration_since(UNIX_EPOCH).map_or(0., |d| d.as_secs_f64()))
}

fn host(url: &str) -> &str {
    url.split_once("://").map_or(url, |(_, rest)| rest)
}

fn web_url(url: &str) -> bool {
    url.starts_with("https://") || url.starts_with("http://")
}

fn size_text(bytes: u64) -> String {
    const KB: f64 = 1024.;
    let b = bytes as f64;
    if b < KB {
        format!("{bytes} B")
    } else if b < KB * KB {
        format!("{:.0} KB", b / KB)
    } else if b < KB * KB * KB {
        format!("{:.1} MB", b / KB / KB)
    } else {
        format!("{:.2} GB", b / KB / KB / KB)
    }
}

fn compact(n: u64) -> String {
    match n {
        0..1000 => n.to_string(),
        1000..10_000 => format!("{:.1}k", n as f64 / 1000.),
        10_000..1_000_000 => format!("{}k", n / 1000),
        _ => format!("{:.1}M", n as f64 / 1e6),
    }
}

fn took_text(d: Duration) -> String {
    match d.as_millis() {
        ms @ 0..1000 => format!("{ms} ms"),
        ms => format!("{:.1} s", ms as f64 / 1000.),
    }
}

fn native_badge(id: String, p: Palette) -> Stateful<Div> {
    h_flex()
        .id(SharedString::from(id))
        .h(px(20.))
        .px(px(6.))
        .gap(px(4.))
        .flex_shrink_0()
        .rounded(px(4.))
        .bg(p.warning.opacity(0.14))
        .text_color(p.warning)
        .text_size(px(11.))
        .font_weight(FontWeight::SEMIBOLD)
        .whitespace_nowrap()
        .child(Icon::new(IconName::Cpu).size(px(11.)))
        .child(NATIVE)
        .tooltip(|window, cx| {
            Tooltip::new("It ships native code that runs inside F1 25 while it's on.").build(window, cx)
        })
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum Progress {
    Downloading { done: u64, size: u64, rate: f64 },
    Verifying,
    Installing,
}

impl Progress {
    fn fraction(self) -> f32 {
        match self {
            Self::Downloading { done, size, .. } if size > 0 => (done as f32 / size as f32).min(1.),
            Self::Downloading { .. } => 0.,
            Self::Verifying | Self::Installing => 1.,
        }
    }

    fn short(self) -> String {
        match self {
            Self::Downloading { size: 0, .. } => "Downloading…".into(),
            Self::Downloading { .. } => format!("Downloading {:.0}%", self.fraction() * 100.),
            Self::Verifying => "Verifying…".into(),
            Self::Installing => "Installing…".into(),
        }
    }

    /// What it's doing, naming `item` when that's a requirement, not the addon itself.
    fn long(self, item: Option<&str>) -> String {
        match self {
            Self::Downloading { done, size, rate } => {
                let mut text = match item {
                    Some(item) => format!("Downloading {item} · "),
                    None => "Downloading ".to_owned(),
                };
                text.push_str(&format!(
                    "{} of {} · {:.0}%",
                    size_text(done),
                    size_text(size),
                    self.fraction() * 100.
                ));
                if rate > 0. {
                    text.push_str(&format!(" · {}/s", size_text(rate as u64)));
                }
                text
            }
            Self::Verifying => match item {
                Some(item) => format!("Verifying {item}'s size and SHA-256…"),
                None => "Verifying the download's size and SHA-256…".into(),
            },
            Self::Installing => match item {
                Some(item) => format!("Installing {item}…"),
                None => "Installing…".into(),
            },
        }
    }
}

/// An install's downloads so far: every addon it fetches, requirements included.
#[derive(Default)]
struct Tally {
    finished: u64,
    current: u64,
    size: u64,
    stage: u8,
    item: Option<String>,
    also: Vec<String>,
}

#[derive(Clone)]
struct Job {
    tally: Arc<Mutex<Tally>>,
    started: Instant,
}

impl Job {
    fn new(size: u64) -> Self {
        Self {
            tally: Arc::new(Mutex::new(Tally {
                size,
                ..Tally::default()
            })),
            started: Instant::now(),
        }
    }

    fn tally(&self) -> std::sync::MutexGuard<'_, Tally> {
        self.tally.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn progress(&self) -> Progress {
        let t = self.tally();
        match t.stage {
            VERIFYING => Progress::Verifying,
            INSTALLING => Progress::Installing,
            _ => {
                let done = t.finished + t.current;
                let secs = self.started.elapsed().as_secs_f64();
                Progress::Downloading {
                    done,
                    size: t.size.max(done),
                    rate: if secs > 0.3 { done as f64 / secs } else { 0. },
                }
            }
        }
    }

    /// The requirement it's on, when it isn't on the addon itself.
    fn item(&self) -> Option<String> {
        self.tally().item.clone()
    }

    /// The other addons it installs, once they're known.
    fn also(&self) -> Vec<String> {
        self.tally().also.clone()
    }

    fn track(&self, root: &str, e: Event) {
        let mut t = self.tally();
        match e {
            Event::Planned(plan) => {
                t.size = plan.size();
                t.also = plan.also().iter().map(|s| s.label()).collect();
            }
            Event::Addon(a, _) => {
                t.finished += t.current;
                t.current = 0;
                t.stage = 0;
                t.item = (a.id != root).then(|| a.name.clone());
            }
            Event::Stage(Stage::Downloading(n)) => t.current = n,
            Event::Stage(Stage::Verifying) => t.stage = VERIFYING,
            Event::Stage(Stage::Installing) => t.stage = INSTALLING,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
enum Offer {
    Install,
    Installed(semver::Version),
    Update(semver::Version, semver::Version),
    TooOld,
    Working(Progress),
    Failed,
    /// Not installed, and its requirements can't all be met.
    Blocked,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Sort {
    Match,
    Stars,
    Downloads,
    Trending,
    Updated,
    Newest,
    Name,
}

impl Sort {
    const ALL: [Sort; 7] = [
        Self::Match,
        Self::Stars,
        Self::Downloads,
        Self::Trending,
        Self::Updated,
        Self::Newest,
        Self::Name,
    ];

    fn label(self) -> &'static str {
        match self {
            Self::Match => "Best match",
            Self::Stars => "Most stars",
            Self::Downloads => "Most downloads",
            Self::Trending => "Trending",
            Self::Updated => "Recently updated",
            Self::Newest => "Newest",
            Self::Name => "Name A–Z",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
enum Want {
    Installed,
    NotInstalled,
    Update,
    Starred,
    Works,
}

impl Want {
    const ALL: [Want; 5] = [
        Self::Installed,
        Self::NotInstalled,
        Self::Update,
        Self::Starred,
        Self::Works,
    ];

    fn label(self) -> &'static str {
        match self {
            Self::Installed => "Installed",
            Self::NotInstalled => "Not installed",
            Self::Update => "Update available",
            Self::Starred => "Starred by you",
            Self::Works => "Works with this launcher",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Group {
    Category,
    Type,
    State,
    Author,
    Code,
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
enum Facet {
    Type(Type),
    State(Want),
    Author { id: String, name: String },
}

impl Facet {
    fn group(&self) -> Group {
        match self {
            Self::Type(_) => Group::Type,
            Self::State(_) => Group::State,
            Self::Author { .. } => Group::Author,
        }
    }

    fn label(&self) -> String {
        match self {
            Self::Type(t) => t.label().to_owned(),
            Self::State(w) => w.label().to_owned(),
            Self::Author { name, .. } => name.clone(),
        }
    }
}

/// A category bar tab: all addons, one category, or none of them.
type Tab = Option<Option<Category>>;

fn tab_label(tab: Tab) -> &'static str {
    match tab {
        None => "All",
        Some(Some(c)) => c.label(),
        Some(None) => "Other",
    }
}

/// The categories the landing shows a tile for, Other last.
fn shelves() -> impl Iterator<Item = Tab> {
    Category::ALL
        .into_iter()
        .map(|c| Some(Some(c)))
        .chain([Some(None)])
}

fn shelf_tint(tab: Tab, p: Palette) -> Hsla {
    let hue = match tab {
        None => return p.accent,
        Some(None) => return p.text_muted,
        Some(Some(Category::Seasons)) => 38.,
        Some(Some(Category::Tracks)) => 145.,
        Some(Some(Category::Cars)) => 2.,
        Some(Some(Category::Helmets)) => 205.,
        Some(Some(Category::Shaders)) => 320.,
        Some(Some(Category::Career)) => 280.,
        Some(Some(Category::Ai)) => 175.,
        Some(Some(Category::Performance)) => 85.,
        Some(Some(Category::Utility)) => 245.,
    };
    Hsla {
        h: hue / 360.,
        ..p.accent
    }
}

fn shelf_glyph(tab: Tab, size: f32, color: Hsla) -> AnyElement {
    let icon = match tab {
        Some(Some(Category::Tracks)) => {
            return svg()
                .data(TRACK)
                .size(px(size))
                .text_color(color)
                .into_any_element();
        }
        Some(Some(Category::Helmets)) => {
            return svg()
                .data(HELMET)
                .size(px(size))
                .text_color(color)
                .into_any_element();
        }
        None => IconName::LayoutGrid,
        Some(None) => IconName::Shapes,
        Some(Some(Category::Seasons)) => IconName::CalendarRange,
        Some(Some(Category::Cars)) => IconName::Car,
        Some(Some(Category::Shaders)) => IconName::Palette,
        Some(Some(Category::Career)) => IconName::Briefcase,
        Some(Some(Category::Ai)) => IconName::Bot,
        Some(Some(Category::Performance)) => IconName::Gauge,
        Some(Some(Category::Utility)) => IconName::Wrench,
    };
    Icon::new(icon)
        .size(px(size))
        .text_color(color)
        .into_any_element()
}

/// How many tiles a landing row of `width` holds: 5, 2 or 1, so the rows come out even.
fn shelf_columns(width: f32) -> usize {
    [5, 2]
        .into_iter()
        .find(|&n| n as f32 * TILE_MIN + (n - 1) as f32 * GAP <= width)
        .unwrap_or(1)
}

/// How many icons fit on a tile `width` wide, beside its glyph and a count of the rest.
fn tile_faces(width: f32) -> usize {
    let room = width - 28. - 64. - 40.;
    (1 + ((room - FACE) / (FACE - 8.)).floor().max(0.) as usize).min(TILE_FACES)
}

/// The addons in `tab`, most starred first.
fn shelved(addons: &[Addon], tab: Tab, stars: impl Fn(&Addon) -> u64) -> Vec<&Addon> {
    let mut found: Vec<&Addon> = addons
        .iter()
        .filter(|a| tab.is_none_or(|t| a.category() == t))
        .collect();
    found.sort_by(|a, b| {
        stars(b)
            .cmp(&stars(a))
            .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
    });
    found
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Op {
    Catalog,
    Stars,
    Install,
    Details,
    Profile,
}

impl Op {
    fn icon(self) -> IconName {
        match self {
            Self::Catalog => IconName::RefreshCw,
            Self::Stars => IconName::Star,
            Self::Install => IconName::Download,
            Self::Details => IconName::Info,
            Self::Profile => IconName::UserRound,
        }
    }
}

struct Activity {
    at: SystemTime,
    op: Op,
    ok: bool,
    title: String,
    detail: String,
    took: Duration,
}

#[derive(Default)]
struct Batch {
    left: BTreeSet<String>,
    updated: Vec<String>,
    failed: Vec<(String, String)>,
}

impl Batch {
    fn total(&self) -> usize {
        self.left.len() + self.updated.len() + self.failed.len()
    }

    fn summary(&self) -> String {
        let mut lines = Vec::new();
        if !self.updated.is_empty() {
            lines.push(format!("Updated {}", self.updated.join(", ")));
        }
        lines.extend(
            self.failed
                .iter()
                .map(|(name, e)| format!("Couldn't update {name}: {e}")),
        );
        lines.join("\n")
    }
}

struct Fetched {
    addons: Option<Vec<Addon>>,
    icons: Vec<(String, Arc<Image>)>,
    error: Option<Trouble>,
    creators: std::result::Result<Vec<Creator>, Trouble>,
    avatars: Vec<(String, Arc<Image>)>,
}

fn images<'a>(
    dir: &Path,
    urls: impl IntoIterator<Item = &'a String>,
    known: &BTreeSet<String>,
) -> Vec<(String, Arc<Image>)> {
    let mut wanted: Vec<&String> = urls
        .into_iter()
        .filter(|url| web_url(url) && !known.contains(*url))
        .collect();
    wanted.sort();
    wanted.dedup();
    wanted
        .into_iter()
        .filter_map(|url| crate::news::image(dir, url).map(|i| (url.clone(), i)))
        .collect()
}

fn avatar_dir(root: &Path) -> std::path::PathBuf {
    root.join(catalog::CACHE_DIR).join(AVATARS_DIR)
}

fn fetch(root: &Path, shown: Vec<Addon>, known: BTreeSet<String>) -> Fetched {
    let (addons, error) = match catalog::fetch(root) {
        Ok(addons) => (Some(addons), None),
        Err(e) => (None, Some(e)),
    };
    let creators = publishers::fetch(root);
    let listed = addons.as_ref().unwrap_or(&shown);
    let icons = images(
        &root.join(catalog::CACHE_DIR).join(ICONS_DIR),
        listed.iter().filter_map(|a| a.icon.as_ref()),
        &known,
    );
    let faces = listed
        .iter()
        .flat_map(|a| a.owner().into_iter().chain(&a.publishers))
        .filter_map(|p| p.avatar.as_ref())
        .chain(creators.iter().flatten().filter_map(|c| c.avatar.as_ref()));
    let avatars = images(&avatar_dir(root), faces, &known);
    Fetched {
        addons,
        icons,
        error,
        creators,
        avatars,
    }
}

pub(crate) struct Browser {
    pub(crate) addons: Vec<Addon>,
    pub(crate) updates: Vec<Update>,
    pub(crate) toasts: Vec<Notification>,
    icons: HashMap<String, Arc<Image>>,
    checked: Option<Instant>,
    fetched_at: Option<SystemTime>,
    live: bool,
    saved: bool,
    fetching: bool,
    trouble: Option<Trouble>,
    search: Entity<InputState>,
    window: AnyWindowHandle,
    view: View,
    trail: Vec<(View, Point<Pixels>)>,
    scroll: ScrollHandle,
    pub(crate) smooth: crate::scroll::SmoothScroll,
    details: HashMap<String, Addon>,
    loading: BTreeSet<String>,
    working: HashMap<String, Job>,
    failed: HashMap<String, String>,
    refused: BTreeSet<String>,
    batch: Option<Batch>,
    held: BTreeMap<String, (semver::Version, String)>,
    ticking: bool,
    stars: Stars,
    syncing: bool,
    sync_again: bool,
    star_gen: u64,
    sort: Sort,
    search_sort: Option<Sort>,
    landing: bool,
    tab: Tab,
    facets: BTreeSet<Facet>,
    hide_code: bool,
    activity: VecDeque<Activity>,
    activity_open: bool,
    creators: Vec<Creator>,
    profiles: HashMap<String, Profile>,
    profile_loading: BTreeSet<String>,
    profile_failed: HashMap<String, String>,
    author_sort: Sort,
    avatars: HashMap<String, Arc<Image>>,
    _search: Subscription,
}

impl Browser {
    pub(crate) fn new(root: &Path, window: &mut Window, cx: &mut Context<Shell>) -> Self {
        let search = cx
            .new(|cx| InputState::new(window, cx).placeholder("Search names, authors, author: or category:"));
        let changed = cx.subscribe(&search, |_, _, e: &InputEvent, cx| {
            if matches!(e, InputEvent::Change) {
                cx.notify();
            }
        });
        let cached = catalog::cached(root);
        Self {
            saved: cached.is_some(),
            fetched_at: cached.as_ref().and_then(|_| catalog::cached_at(root)),
            addons: cached.unwrap_or_default(),
            updates: Vec::new(),
            toasts: Vec::new(),
            icons: HashMap::new(),
            checked: None,
            live: false,
            fetching: false,
            trouble: None,
            search,
            window: window.window_handle(),
            view: View::Grid,
            trail: Vec::new(),
            scroll: ScrollHandle::new(),
            smooth: Default::default(),
            details: HashMap::new(),
            loading: BTreeSet::new(),
            working: HashMap::new(),
            failed: HashMap::new(),
            refused: BTreeSet::new(),
            batch: None,
            held: BTreeMap::new(),
            ticking: false,
            stars: Stars::load(root),
            syncing: false,
            sync_again: false,
            star_gen: 0,
            sort: Sort::Stars,
            search_sort: None,
            landing: true,
            tab: None,
            facets: BTreeSet::new(),
            hide_code: false,
            activity: VecDeque::new(),
            activity_open: false,
            creators: publishers::cached(root).unwrap_or_default(),
            profiles: HashMap::new(),
            profile_loading: BTreeSet::new(),
            profile_failed: HashMap::new(),
            author_sort: Sort::Stars,
            avatars: HashMap::new(),
            _search: changed,
        }
    }

    fn stale(&self) -> bool {
        !self.fetching && self.checked.is_none_or(|t| t.elapsed() >= STALE)
    }

    fn subtitle(&self) -> String {
        let base = catalog::base_url();
        let n = plural(self.addons.len(), "addon", "addons");
        let age = self.fetched_at.map(since_time);
        if self.fetching && self.fetched_at.is_none() {
            return format!("Checking {}…", host(&base));
        }
        let mut text = match (&self.trouble, self.saved) {
            (None, _) => format!("{n} from {}", host(&base)),
            (Some(_), true) if !self.live => format!("Offline · showing the {n} saved last time"),
            (Some(_), true) => format!("Offline · showing {n} from {}", host(&base)),
            (Some(_), false) => "Offline · no addons saved yet".to_owned(),
        };
        if let Some(age) = age.filter(|_| self.saved) {
            text.push_str(&format!(" · updated {age}"));
        }
        if self.fetching {
            text.push_str(" · checking…");
        }
        text
    }

    fn find(&self, id: &str) -> Option<&Addon> {
        self.addons.iter().find(|a| a.id == id)
    }

    fn stars_of(&self, a: &Addon) -> u64 {
        self.stars.count(&a.id, a.stars)
    }

    fn clear_search(&self, cx: &mut App) {
        let (input, window) = (self.search.clone(), self.window);
        cx.defer(move |cx| {
            window
                .update(cx, |_, window, cx| {
                    input.update(cx, |s, cx| s.set_value("", window, cx))
                })
                .ok();
        });
    }

    /// Whether the category tiles show: nothing chosen, searched or filtered.
    fn on_landing(&self, searching: bool) -> bool {
        self.landing && self.tab.is_none() && !searching && self.facets.is_empty() && !self.hide_code
    }
}

impl Shell {
    pub(crate) fn watch_catalog(cx: &mut Context<Self>) -> Task<()> {
        cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(TICK).await;
                let ticked = this.update(cx, |s, cx| {
                    if s.browser.stale() {
                        s.fetch_catalog(cx);
                    } else {
                        s.sync_stars(cx);
                    }
                    cx.notify();
                });
                if ticked.is_err() {
                    break;
                }
            }
        })
    }

    fn record(&mut self, op: Op, ok: bool, title: String, detail: String, took: Duration) {
        let mut line = format!(
            "{} {title} ({})",
            if ok { "ok" } else { "failed" },
            took_text(took)
        );
        if !detail.is_empty() {
            line.push_str(&format!(": {detail}"));
        }
        catalog::log(&self.store.root, &line);
        let b = &mut self.browser;
        b.activity.push_front(Activity {
            at: SystemTime::now(),
            op,
            ok,
            title,
            detail,
            took,
        });
        b.activity.truncate(ACTIVITY_KEPT);
    }

    pub(crate) fn fetch_catalog(&mut self, cx: &mut Context<Self>) {
        if self.browser.fetching {
            return;
        }
        self.browser.fetching = true;
        let root = self.store.root.clone();
        let shown = self.browser.addons.clone();
        let known = self
            .browser
            .icons
            .keys()
            .chain(self.browser.avatars.keys())
            .cloned()
            .collect();
        let started = Instant::now();
        cx.spawn(async move |this, cx| {
            let fetched = cx
                .background_executor()
                .spawn(async move { fetch(&root, shown, known) })
                .await;
            this.update(cx, |s, cx| {
                let took = started.elapsed();
                let from = host(&catalog::base_url()).to_owned();
                match (&fetched.error, &fetched.addons) {
                    (Some(e), _) => s.record(
                        Op::Catalog,
                        false,
                        format!("Couldn't fetch the catalog from {from}"),
                        e.to_string(),
                        took,
                    ),
                    (None, Some(addons)) => s.record(
                        Op::Catalog,
                        true,
                        "Fetched the catalog".to_owned(),
                        format!("{} from {from}", plural(addons.len(), "addon", "addons")),
                        took,
                    ),
                    (None, None) => {}
                }
                match &fetched.creators {
                    Ok(creators) => catalog::log(
                        &s.store.root,
                        &format!(
                            "ok fetched {} from {from}",
                            plural(creators.len(), "publisher", "publishers")
                        ),
                    ),
                    Err(e) => catalog::log(&s.store.root, &format!("failed to fetch the publishers: {e}")),
                }
                let b = &mut s.browser;
                if let Ok(creators) = fetched.creators {
                    b.creators = creators;
                }
                b.avatars.extend(fetched.avatars);
                b.fetching = false;
                b.checked = Some(Instant::now());
                b.trouble = fetched.error;
                if let Some(addons) = fetched.addons {
                    b.addons = addons;
                    b.live = true;
                    b.saved = true;
                    b.fetched_at = Some(SystemTime::now());
                }
                b.icons.extend(fetched.icons);
                s.sync_catalog_updates();
                s.sync_stars(cx);
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    pub(crate) fn sync_stars(&mut self, cx: &mut Context<Self>) {
        let b = &mut self.browser;
        if b.syncing {
            b.sync_again = true;
            return;
        }
        b.syncing = true;
        let sent = b.stars.outgoing();
        let client = b.stars.client.clone();
        let started = Instant::now();
        cx.spawn(async move |this, cx| {
            let job = sent.clone();
            let result = cx
                .background_executor()
                .spawn(async move { stars::sync(&client, &job) })
                .await;
            this.update(cx, |s, cx| {
                let took = started.elapsed();
                s.browser.syncing = false;
                match result {
                    Ok(reply) => {
                        let applied = reply.applied;
                        let b = &mut s.browser;
                        b.stars.synced(&sent, reply, catalog::iso_now());
                        let detail = format!(
                            "{} sent, {applied} applied · {} starred · {}",
                            plural(sent.len(), "change", "changes"),
                            b.stars.starred.len(),
                            b.stars.user.as_deref().unwrap_or("no user yet")
                        );
                        s.record(Op::Stars, true, "Synced stars".to_owned(), detail, took);
                    }
                    Err(e) => {
                        s.browser.stars.failed(e.to_string());
                        let waiting = plural(s.browser.stars.pending.len(), "change", "changes");
                        s.record(
                            Op::Stars,
                            false,
                            "Couldn't sync stars".to_owned(),
                            format!("{e} · {waiting} waiting"),
                            took,
                        );
                    }
                }
                if let Err(e) = s.browser.stars.save(&s.store.root) {
                    eprintln!("swofty-launcher: could not save the stars: {e}");
                }
                if std::mem::take(&mut s.browser.sync_again) {
                    s.sync_stars(cx);
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    fn toggle_star(&mut self, id: &str, cx: &mut Context<Self>) {
        let listed = self.browser.find(id).map_or(0, |a| a.stars);
        let on = self.browser.stars.toggle(id, listed, catalog::iso_now());
        if let Err(e) = self.browser.stars.save(&self.store.root) {
            eprintln!("swofty-launcher: could not save the stars: {e}");
        }
        catalog::log(
            &self.store.root,
            &format!(
                "{} {id}, {} waiting",
                if on { "starred" } else { "unstarred" },
                plural(self.browser.stars.pending.len(), "change", "changes")
            ),
        );
        self.browser.star_gen += 1;
        let ticket = self.browser.star_gen;
        cx.spawn(async move |this, cx| {
            cx.background_executor().timer(STAR_DELAY).await;
            this.update(cx, |s, cx| {
                if s.browser.star_gen == ticket {
                    s.sync_stars(cx);
                }
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    pub(crate) fn sync_catalog_updates(&mut self) {
        let installed = self
            .library
            .addons
            .iter()
            .filter(|a| a.source != Source::Builtin)
            .map(|a| &a.manifest);
        self.browser.updates = catalog::updates(installed, &self.browser.addons, &current_version());
    }

    pub(crate) fn catalog_update(&self, id: &str) -> Option<&Update> {
        self.browser.updates.iter().find(|u| u.id == id)
    }

    fn installed_version(&self, id: &str) -> Option<semver::Version> {
        self.library
            .addons
            .iter()
            .find(|m| m.manifest.id == id)
            .map(|m| m.manifest.version.clone())
    }

    fn offer(&self, a: &Addon) -> Offer {
        if let Some(job) = self.browser.working.get(&a.id) {
            return Offer::Working(job.progress());
        }
        if self.browser.failed.contains_key(&a.id) {
            return Offer::Failed;
        }
        match (a.offered(&current_version()), self.installed_version(&a.id)) {
            (Some(v), Some(m)) if v.version > m => Offer::Update(m, v.version.clone()),
            (_, Some(m)) => Offer::Installed(m),
            (Some(_), None) if matches!(self.requirements(a), Some(Err(_))) => Offer::Blocked,
            (Some(_), None) => Offer::Install,
            (None, None) => Offer::TooOld,
        }
    }

    /// What installing the version of `a` this launcher would get also
    /// installs, from the requirements the registry lists; none when it doesn't
    /// list them all.
    fn requirements(&self, a: &Addon) -> Option<swofty_core::Result<Plan>> {
        let launcher = current_version();
        let v = a.offered(&launcher)?;
        let builtin: BTreeSet<&str> = self
            .library
            .addons
            .iter()
            .filter(|x| x.source == Source::Builtin)
            .map(|x| x.manifest.id.as_str())
            .collect();
        let have = Have::new(self.library.addons.iter().map(|x| &x.manifest), |id| {
            builtin.contains(id)
        });
        requires::preview(a, v, &self.browser.addons, &have, &launcher)
    }

    /// The other addons installing `a` brings, as `Name 1.2.0`, and why it
    /// can't be installed when the registry's requirements already say.
    fn needs_of(&self, a: &Addon) -> (Vec<String>, Option<String>) {
        if let Some(job) = self.browser.working.get(&a.id) {
            return (job.also(), None);
        }
        if self.installed_version(&a.id).is_some() {
            return (Vec::new(), None);
        }
        match self.requirements(a) {
            Some(Ok(plan)) => (plan.also().iter().map(|s| s.label()).collect(), None),
            Some(Err(e)) => (Vec::new(), Some(e.to_string())),
            None => (Vec::new(), None),
        }
    }

    /// A version's requirements as `Requires Name >=1.0.0, other.id`.
    fn requires_text(&self, reqs: &[swofty_core::manifest::Requirement]) -> String {
        let named: Vec<String> = reqs
            .iter()
            .map(|r| {
                let name = self
                    .browser
                    .find(&r.id)
                    .map_or(r.id.as_str(), |a| a.name.as_str());
                match r.version == semver::VersionReq::STAR {
                    true => name.to_owned(),
                    false => format!("{name} {}", r.version),
                }
            })
            .collect();
        format!("Requires {}", named.join(", "))
    }

    pub(crate) fn install_from_catalog(&mut self, id: &str, cx: &mut Context<Self>) {
        if self.browser.working.contains_key(id) {
            return;
        }
        let Some(addon) = self.browser.find(id).cloned() else {
            return;
        };
        let Some(version) = addon.offered(&current_version()).cloned() else {
            return;
        };
        if self.build.as_ref().is_some_and(|b| b.holds(id)) {
            self.browser
                .toasts
                .push(Notification::warning(crate::update::BUILDING));
            return;
        }
        let updating = self.installed_version(id).is_some();
        self.browser.failed.remove(id);
        self.browser.refused.remove(id);
        let job = Job::new(version.size);
        let preview: Vec<String> = match self.requirements(&addon) {
            Some(Ok(plan)) => plan.also().iter().map(|s| s.addon.id.clone()).collect(),
            _ => Vec::new(),
        };
        let riders: Vec<String> = preview
            .into_iter()
            .filter(|d| {
                !self.browser.working.contains_key(d)
                    && !self.browser.batch.as_ref().is_some_and(|b| b.left.contains(d))
            })
            .collect();
        for d in &riders {
            self.browser.working.insert(d.clone(), job.clone());
        }
        self.browser.working.insert(addon.id.clone(), job.clone());
        catalog::log(
            &self.store.root,
            &format!(
                "{} {} {} ({}) from {}",
                if updating { "updating" } else { "installing" },
                addon.id,
                version.version,
                size_text(version.size),
                version.url
            ),
        );
        let store = self.store.clone();
        let catalog_now = self.browser.addons.clone();
        let started = Instant::now();
        cx.spawn(async move |this, cx| {
            let (target, v) = (addon.clone(), version.clone());
            let result = cx
                .background_executor()
                .spawn(async move {
                    catalog::requires::install(&store, &catalog_now, &target, &v, &|e| {
                        job.track(&target.id, e)
                    })
                })
                .await;
            this.update(cx, |s, cx| {
                s.browser.working.remove(&addon.id);
                for d in &riders {
                    s.browser.working.remove(d);
                }
                let took = started.elapsed();
                let in_batch = s.browser.batch.as_mut().is_some_and(|b| b.left.remove(&addon.id));
                let verb = if updating { "update" } else { "install" };
                match result {
                    Ok(done) => {
                        let (m, also) = done.split_last().expect("an install ends with its addon");
                        let title = format!(
                            "{} {} {}",
                            if updating { "Updated" } else { "Installed" },
                            m.name,
                            m.version
                        );
                        let also: Vec<String> =
                            also.iter().map(|d| format!("{} {}", d.name, d.version)).collect();
                        let mut detail = format!("{} · {}", size_text(version.size), addon.id);
                        if !also.is_empty() {
                            detail.push_str(&format!(" · also installed {}", also.join(", ")));
                        }
                        s.record(Op::Install, true, title.clone(), detail, took);
                        s.reload(cx);
                        if in_batch {
                            if let Some(b) = s.browser.batch.as_mut() {
                                b.updated.push(format!("{} {}", m.name, m.version));
                            }
                        } else {
                            if s.page == Page::Browser {
                                s.show_installed(&m.id, cx);
                            }
                            if !also.is_empty() {
                                s.browser.toasts.push(
                                    Notification::success(format!("Also installed {}", also.join(", ")))
                                        .title(title),
                                );
                            } else if s.page != Page::Browser {
                                s.browser.toasts.push(Notification::success(title));
                            }
                        }
                    }
                    Err(e) => {
                        let refused = matches!(e, swofty_core::Error::Requires(_));
                        let error = e.to_string();
                        crate::app_log::action(verb).addon(&addon.id).failed(&error);
                        s.record(
                            Op::Install,
                            false,
                            format!("Couldn't {verb} {} {}", addon.name, version.version),
                            error.clone(),
                            took,
                        );
                        s.browser.failed.insert(addon.id.clone(), error.clone());
                        if refused {
                            s.browser.refused.insert(addon.id.clone());
                        }
                        if in_batch {
                            if let Some(b) = s.browser.batch.as_mut() {
                                b.failed.push((addon.name.clone(), error));
                            }
                        } else {
                            s.browser
                                .toasts
                                .push(Notification::error(error).title(if refused {
                                    format!("Can't {verb} {}: missing requirements", addon.name)
                                } else {
                                    format!("Could not {verb} {}", addon.name)
                                }));
                        }
                        s.reload(cx);
                    }
                }
                if in_batch && let Some(to) = s.catalog_update(&addon.id).map(|u| u.to.version.clone()) {
                    let error = s.browser.failed.get(&addon.id).cloned().unwrap_or_default();
                    s.browser.held.insert(addon.id.clone(), (to, error));
                }
                if in_batch && s.browser.batch.as_ref().is_some_and(|b| b.left.is_empty()) {
                    s.finish_addon_updates();
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        self.tick_browser(cx);
        cx.notify();
    }

    /// Opens where `id` now lists, highlighting it: Addons, or Tracks for a track addon.
    fn show_installed(&mut self, id: &str, cx: &mut Context<Self>) {
        let track = self.library.addons.iter().any(|a| a.manifest.id == id && a.track);
        match self.library.tracks_page.filter(|_| track) {
            Some(i) => self.open_page(Page::Section(i), cx),
            None => self.show_addon(id, cx),
        }
    }

    /// The addon updates the play bar offers: every outdated registry addon
    /// except the ones that failed at this version.
    fn addon_updates(&self) -> Vec<&Update> {
        self.browser
            .updates
            .iter()
            .filter(|u| {
                self.browser
                    .held
                    .get(&u.id)
                    .is_none_or(|(v, _)| *v != u.to.version)
            })
            .collect()
    }

    fn updating_addons(&self) -> bool {
        self.browser.batch.as_ref().is_some_and(|b| !b.left.is_empty())
    }

    pub(crate) fn addon_update_takes_play(&self, state: &PlayState) -> bool {
        self.updating_addons()
            || (matches!(state, PlayState::Ready | PlayState::Crashed(_)) && !self.addon_updates().is_empty())
    }

    fn update_addons(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if matches!(
            self.play_state(),
            PlayState::Running | PlayState::Elsewhere(_) | PlayState::Launching
        ) {
            window.push_notification(Notification::error("Close F1 25 before updating addons."), cx);
            return;
        }
        let building = self.build.clone();
        let (held, ids): (Vec<String>, Vec<String>) = self
            .addon_updates()
            .into_iter()
            .map(|u| u.id.clone())
            .filter(|id| !self.browser.working.contains_key(id))
            .partition(|id| building.as_ref().is_some_and(|b| b.holds(id)));
        if !held.is_empty() {
            window.push_notification(Notification::warning(crate::update::BUILDING), cx);
        }
        if ids.is_empty() || self.updating_addons() {
            return;
        }
        catalog::log(&self.store.root, &format!("updating addons: {}", ids.join(", ")));
        self.browser.batch = Some(Batch {
            left: ids.iter().cloned().collect(),
            ..Batch::default()
        });
        for id in ids {
            self.install_from_catalog(&id, cx);
        }
    }

    fn retry_addon_updates(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.browser.held.clear();
        self.update_addons(window, cx);
    }

    fn finish_addon_updates(&mut self) {
        let Some(b) = self.browser.batch.take() else {
            return;
        };
        let summary = b.summary();
        catalog::log(&self.store.root, &summary.replace('\n', "; "));
        let title = format!(
            "Updated {} of {}",
            b.updated.len(),
            plural(b.total(), "addon", "addons")
        );
        self.browser.toasts.push(if b.failed.is_empty() {
            Notification::success(summary).title(title)
        } else {
            Notification::warning(summary).title(title)
        });
    }

    fn batch_progress(&self) -> Option<(f32, String)> {
        let b = self.browser.batch.as_ref()?;
        let total = b.total().max(1) as f32;
        let mut done = (b.updated.len() + b.failed.len()) as f32;
        let mut detail = None;
        for id in &b.left {
            if let Some(job) = self.browser.working.get(id) {
                let pr = job.progress();
                done += pr.fraction();
                if detail.is_none() {
                    let name = self.browser.find(id).map_or(id.as_str(), |a| a.name.as_str());
                    detail = Some(format!("{name} · {}", pr.short()));
                }
            }
        }
        Some((done / total, detail.unwrap_or_else(|| "Starting…".into())))
    }

    pub(crate) fn addon_update_button(&self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let running = self.updating_addons();
        let due = self.addon_updates();
        let (label, sub, progress) = match self.batch_progress().filter(|_| running) {
            Some((done, detail)) => (format!("Updating {:.0}%", done * 100.), detail, Some(done)),
            None => (
                format!("Update addons ({})", due.len()),
                match due.as_slice() {
                    [one] => format!("{} {} → {}", one.name, one.installed, one.to.version),
                    many => plural(many.len(), "addon update", "addon updates"),
                },
                None,
            ),
        };
        div()
            .id("play")
            .relative()
            .overflow_hidden()
            .h(px(52.))
            .w(px(crate::update::BUTTON_WIDTH))
            .flex_shrink_0()
            .rounded(px(9.))
            .bg(if running { p.elevated } else { p.accent })
            .text_color(if running { p.text } else { p.on_accent })
            .flex()
            .items_center()
            .gap(px(10.))
            .px(px(14.))
            .when(running, |d| d.border_1().border_color(p.border))
            .when(!running, |d| {
                d.cursor_pointer()
                    .hover(move |s| s.bg(p.accent_hover))
                    .on_click(cx.listener(|this, _, window, cx| this.update_addons(window, cx)))
            })
            .map(|d| {
                if running {
                    d.child(Spinner::new().icon(IconName::LoaderCircle).with_size(px(18.)))
                } else {
                    d.child(Icon::new(IconName::CircleArrowUp).size(px(18.)))
                }
            })
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .child(
                        div()
                            .text_size(px(15.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child(label),
                    )
                    .child(
                        div()
                            .text_size(px(11.))
                            .truncate()
                            .text_color(if running { p.text_muted } else { p.on_accent })
                            .when(!running, |d| d.opacity(0.85))
                            .child(sub),
                    ),
            )
            .when_some(progress, |d, done| {
                d.child(
                    div()
                        .absolute()
                        .left_0()
                        .bottom_0()
                        .h(px(4.))
                        .w(relative(done.clamp(0., 1.)))
                        .bg(p.accent),
                )
            })
            .into_any_element()
    }

    pub(crate) fn addon_update_status(
        &self,
        state: &PlayState,
        row: Div,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> Option<Div> {
        let dot = |c: Hsla| div().size(px(7.)).rounded_full().bg(c).flex_shrink_0();
        let text = |t: String| div().min_w_0().truncate().child(t);
        if let Some(b) = self.browser.batch.as_ref().filter(|_| self.updating_addons()) {
            let names: Vec<String> = b
                .left
                .iter()
                .map(|id| {
                    self.browser
                        .find(id)
                        .map_or_else(|| id.clone(), |a| a.name.clone())
                })
                .collect();
            return Some(
                row.child(dot(p.warning))
                    .child(text(format!("Updating {}.", names.join(", ")))),
            );
        }
        if !matches!(state, PlayState::Ready | PlayState::Crashed(_)) {
            return None;
        }
        let due = self.addon_updates();
        if !due.is_empty() {
            let changes: Vec<String> = due
                .iter()
                .map(|u| format!("{} {} → {}", u.name, u.installed, u.to.version))
                .collect();
            return Some(
                row.child(dot(p.accent))
                    .child(text(format!("Addon updates: {}.", changes.join(", ")))),
            );
        }
        let failed: Vec<String> = self
            .browser
            .held
            .iter()
            .filter(|(id, (v, _))| self.catalog_update(id).is_some_and(|u| u.to.version == *v))
            .map(|(id, (_, e))| {
                let name = self.browser.find(id).map_or(id.as_str(), |a| a.name.as_str());
                format!("{name}: {e}")
            })
            .collect();
        if failed.is_empty() {
            return None;
        }
        let tip = SharedString::from(failed.join("\n"));
        Some(
            row.text_color(p.error)
                .child(Icon::new(IconName::CircleAlert).size(px(16.)))
                .child(
                    div()
                        .id("addon-updates-failed")
                        .min_w_0()
                        .truncate()
                        .child(format!("Couldn't update {}", failed.join("; ")))
                        .tooltip(move |window, cx| Tooltip::new(tip.clone()).build(window, cx)),
                )
                .child(
                    link("addon-updates-again", "Try again", p)
                        .flex_shrink_0()
                        .on_click(cx.listener(|this, _, window, cx| this.retry_addon_updates(window, cx))),
                ),
        )
    }

    fn tick_browser(&mut self, cx: &mut Context<Self>) {
        if self.browser.ticking {
            return;
        }
        self.browser.ticking = true;
        cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(Duration::from_millis(150)).await;
                let done = this
                    .update(cx, |s, cx| {
                        let done = s.browser.working.is_empty();
                        if done {
                            s.browser.ticking = false;
                        }
                        cx.notify();
                        done
                    })
                    .unwrap_or(true);
                if done {
                    break;
                }
            }
        })
        .detach();
    }

    fn open_details(&mut self, id: &str, cx: &mut Context<Self>) {
        self.browse(View::Addon(id.to_owned()), cx);
    }

    fn fetch_details(&mut self, id: &str, cx: &mut Context<Self>) {
        if !self.browser.loading.insert(id.to_owned()) {
            return;
        }
        let id = id.to_owned();
        let started = Instant::now();
        cx.spawn(async move |this, cx| {
            let job = id.clone();
            let result = cx
                .background_executor()
                .spawn(async move { catalog::fetch_one(&job) })
                .await;
            this.update(cx, |s, cx| {
                s.browser.loading.remove(&id);
                let took = started.elapsed();
                match result {
                    Ok(a) => {
                        let detail = format!(
                            "{} · {}",
                            plural(a.versions.len(), "version", "versions"),
                            if a.trend.is_some() {
                                "30-day trend"
                            } else {
                                "no trend"
                            }
                        );
                        s.record(Op::Details, true, format!("Fetched {}", a.name), detail, took);
                        s.browser.details.insert(id, a);
                    }
                    Err(e) => s.record(
                        Op::Details,
                        false,
                        format!("Couldn't fetch {id}"),
                        e.to_string(),
                        took,
                    ),
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    pub(crate) fn offer_button(&self, a: &Addon, id: String, cx: &mut Context<Self>) -> Button {
        let target = a.id.clone();
        let button = Button::new(SharedString::from(id)).small();
        let button = match self.offer(a) {
            Offer::Install => button.primary().icon(IconName::Download).label("Install"),
            Offer::Update(from, to) => {
                return button
                    .outline()
                    .icon(IconName::CircleArrowUp)
                    .label(format!("Installed v{from} · v{to} in the play bar"))
                    .disabled(true);
            }
            Offer::Failed => button.danger().icon(IconName::RotateCw).label("Failed – Retry"),
            Offer::Installed(v) => {
                return button
                    .outline()
                    .icon(IconName::Check)
                    .label(format!("Installed v{v}"))
                    .disabled(true);
            }
            Offer::TooOld => {
                return button.outline().label("Needs a newer launcher").disabled(true);
            }
            Offer::Blocked => {
                return button
                    .outline()
                    .icon(IconName::TriangleAlert)
                    .label("Missing requirements")
                    .disabled(true);
            }
            Offer::Working(progress) => {
                return button
                    .primary()
                    .loading(true)
                    .label(progress.short())
                    .disabled(true);
            }
        };
        button.on_click(cx.listener(move |this, _, _, cx| {
            cx.stop_propagation();
            this.install_from_catalog(&target, cx);
        }))
    }

    fn star_button(
        &self,
        a: &Addon,
        id: String,
        big: bool,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> Stateful<Div> {
        let on = self.browser.stars.has(&a.id);
        let n = self.browser.stars_of(a);
        let target = a.id.clone();
        let color = if on { p.warning } else { p.text_muted };
        h_flex()
            .id(SharedString::from(id))
            .h(px(if big { 28. } else { 24. }))
            .px(px(if big { 10. } else { 7. }))
            .gap(px(5.))
            .flex_shrink_0()
            .rounded(px(5.))
            .border_1()
            .border_color(if on { p.warning.opacity(0.5) } else { p.border })
            .when(on, |d| d.bg(p.warning.opacity(0.1)))
            .cursor_pointer()
            .hover(move |s| s.bg(p.hover).border_color(p.warning.opacity(0.7)))
            .text_size(px(12.))
            .font_weight(FontWeight::MEDIUM)
            .text_color(color)
            .child(Icon::new(IconName::Star).size(px(13.)).text_color(color))
            .when(big, |d| d.child(if on { "Starred" } else { "Star" }))
            .child(
                div()
                    .text_color(if on { p.text } else { p.text_muted })
                    .child(compact(n)),
            )
            .tooltip(move |window, cx| {
                Tooltip::new(if on {
                    "You starred this. Click to unstar it."
                } else {
                    "Star it. Stars sync to every launcher you use."
                })
                .build(window, cx)
            })
            .on_click(cx.listener(move |this, _, _, cx| {
                cx.stop_propagation();
                this.toggle_star(&target, cx);
            }))
    }

    fn catalog_icon(&self, a: &Addon, size: f32, p: Palette) -> Div {
        match a.icon.as_ref().and_then(|u| self.browser.icons.get(u)) {
            Some(image) => div()
                .size(px(size))
                .flex_shrink_0()
                .rounded(px(size * 0.22))
                .overflow_hidden()
                .child(img(image.clone()).size_full()),
            None => badge(IconName::Package, size, p),
        }
    }

    fn catalog_facet(&self, a: &Addon, f: &Facet) -> bool {
        match f {
            Facet::Type(t) => a.kind_labels().contains(&t.label()),
            Facet::State(Want::Installed) => self.installed_version(&a.id).is_some(),
            Facet::State(Want::NotInstalled) => self.installed_version(&a.id).is_none(),
            Facet::State(Want::Update) => self.catalog_update(&a.id).is_some(),
            Facet::State(Want::Starred) => self.browser.stars.has(&a.id),
            Facet::State(Want::Works) => a.offered(&current_version()).is_some(),
            Facet::Author { id, .. } => a.owner().is_some_and(|o| o.id == *id),
        }
    }

    fn catalog_passes(&self, a: &Addon, matched: bool, except: Option<Group>) -> bool {
        let b = &self.browser;
        matched
            && (except == Some(Group::Category) || b.tab.is_none_or(|t| a.category() == t))
            && (except == Some(Group::Code) || !(b.hide_code && a.native(&current_version())))
            && [Group::Type, Group::State, Group::Author]
                .into_iter()
                .filter(|g| Some(*g) != except)
                .all(|g| {
                    let mut chosen = b.facets.iter().filter(|f| f.group() == g).peekable();
                    chosen.peek().is_none() || chosen.any(|f| self.catalog_facet(a, f))
                })
    }

    fn sort_now(&self, query: &Query) -> Sort {
        if query.is_empty() {
            self.browser.sort
        } else {
            self.browser.search_sort.unwrap_or(Sort::Match)
        }
    }

    fn set_sort(&mut self, sort: Sort, cx: &mut Context<Self>) {
        let searching = !self.browser.search.read(cx).value().trim().is_empty();
        if searching {
            self.browser.search_sort = Some(sort);
        } else {
            self.browser.sort = sort;
        }
        cx.notify();
    }

    fn clear_filters(&mut self, cx: &mut Context<Self>) {
        let b = &mut self.browser;
        b.facets.clear();
        b.hide_code = false;
        b.search_sort = None;
        b.clear_search(cx);
        cx.notify();
    }

    fn shelve(&mut self, tab: Tab, cx: &mut Context<Self>) {
        let b = &mut self.browser;
        if b.landing || b.tab != tab {
            b.smooth.cancel();
            b.scroll.set_offset(Point::default());
        }
        b.landing = false;
        b.tab = tab;
        cx.notify();
    }

    fn show_categories(&mut self, cx: &mut Context<Self>) {
        let b = &mut self.browser;
        b.landing = true;
        b.tab = None;
        b.smooth.cancel();
        b.scroll.set_offset(Point::default());
        self.clear_filters(cx);
    }

    pub(crate) fn browser_page(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        if self.browser.stale() {
            self.fetch_catalog(cx);
        }
        let actions = h_flex()
            .gap(px(8.))
            .child(self.stars_status(p, cx))
            .child(self.creators_button(cx))
            .child(
                Button::new("browser-activity")
                    .map(|b| {
                        if self.browser.activity_open {
                            b.outline()
                        } else {
                            b.ghost()
                        }
                    })
                    .small()
                    .icon(IconName::Activity)
                    .label("Activity")
                    .on_click(cx.listener(|this, _, _, cx| {
                        this.browser.activity_open = !this.browser.activity_open;
                        cx.notify();
                    })),
            )
            .child(
                Button::new("browser-refresh")
                    .ghost()
                    .small()
                    .icon(IconName::RefreshCw)
                    .label("Refresh")
                    .loading(self.browser.fetching)
                    .disabled(self.browser.fetching)
                    .on_click(cx.listener(|this, _, _, cx| this.fetch_catalog(cx))),
            );
        let content = match self.browser.view.clone() {
            View::Addon(id) if self.browser.find(&id).is_some() => self.browser_detail(&id, p, cx),
            View::Author(id) => self.author_page(&id, p, cx),
            View::Creators => self.creators_page(p, cx),
            _ => self.browser_grid(p, cx),
        };
        v_flex()
            .size_full()
            .relative()
            .child(self.header(Page::Browser.label(), self.browser.subtitle(), actions, p))
            .children(self.trouble_banner(p, cx))
            .child(content)
            .when(self.browser.activity_open, |d| {
                d.child(self.activity_panel(p, cx))
            })
            .into_any_element()
    }

    fn stars_status(&self, p: Palette, cx: &mut Context<Self>) -> Stateful<Div> {
        let s = &self.browser.stars;
        let waiting = s.pending.len();
        let (icon, color, text) = if self.browser.syncing {
            (IconName::RefreshCw, p.text_muted, "Syncing stars…".to_owned())
        } else if s.error.is_some() {
            (
                IconName::CircleAlert,
                p.warning,
                match waiting {
                    0 => "Stars not synced".to_owned(),
                    n => format!("Stars not synced · {n} waiting"),
                },
            )
        } else if waiting > 0 {
            (
                IconName::Clock,
                p.text_muted,
                format!("{} waiting", plural(waiting, "star change", "star changes")),
            )
        } else if let Some(at) = s.synced_at.as_deref().and_then(since) {
            (IconName::Star, p.text_muted, format!("Stars synced {at}"))
        } else {
            (IconName::Star, p.text_muted, "Stars not synced yet".to_owned())
        };
        let tip = match (&s.error, &s.synced_at) {
            (Some(e), last) => format!(
                "The last sync failed: {e}\nLast synced: {}\nClick to try again.",
                last.as_deref().and_then(since).unwrap_or_else(|| "never".into())
            ),
            (None, _) => format!(
                "{} starred by you · client {}…\nClick to sync now.",
                s.starred.len(),
                &s.client[..8.min(s.client.len())]
            ),
        };
        h_flex()
            .id("browser-stars-status")
            .h(px(28.))
            .px(px(8.))
            .gap(px(6.))
            .rounded(px(5.))
            .cursor_pointer()
            .hover(move |s| s.bg(p.hover))
            .text_size(px(12.))
            .text_color(color)
            .child(Icon::new(icon).size(px(13.)))
            .child(text)
            .tooltip(move |window, cx| Tooltip::new(tip.clone()).build(window, cx))
            .on_click(cx.listener(|this, _, _, cx| this.sync_stars(cx)))
    }

    fn trouble_banner(&self, p: Palette, cx: &mut Context<Self>) -> Option<Div> {
        let trouble = self.browser.trouble.as_ref()?;
        let what = match trouble {
            Trouble::Offline(_) => "Couldn't reach the registry",
            Trouble::Failed(_) => "The registry answered with an error",
        };
        Some(
            h_flex()
                .mx(px(GUTTER))
                .mb(px(12.))
                .px(px(12.))
                .py(px(8.))
                .gap(px(10.))
                .flex_shrink_0()
                .items_center()
                .rounded(px(8.))
                .bg(p.warning.opacity(0.08))
                .border_1()
                .border_color(p.warning.opacity(0.4))
                .child(Icon::new(IconName::CloudOff).size(px(15.)).text_color(p.warning))
                .child(
                    v_flex()
                        .flex_1()
                        .min_w_0()
                        .child(
                            div()
                                .text_size(px(13.))
                                .font_weight(FontWeight::SEMIBOLD)
                                .child(format!(
                                    "{what} · {}",
                                    if self.browser.saved {
                                        "showing the saved catalog"
                                    } else {
                                        "nothing saved to show"
                                    }
                                )),
                        )
                        .child(
                            div()
                                .text_size(px(12.))
                                .text_color(p.text_muted)
                                .truncate()
                                .child(trouble.to_string()),
                        ),
                )
                .child(
                    Button::new("browser-retry")
                        .outline()
                        .small()
                        .icon(IconName::RefreshCw)
                        .label("Retry")
                        .loading(self.browser.fetching)
                        .disabled(self.browser.fetching)
                        .on_click(cx.listener(|this, _, _, cx| this.fetch_catalog(cx))),
                ),
        )
    }

    fn activity_panel(&self, p: Palette, cx: &mut Context<Self>) -> Stateful<Div> {
        let rows: Vec<AnyElement> = self
            .browser
            .activity
            .iter()
            .enumerate()
            .map(|(i, e)| {
                let color = if e.ok { p.text_muted } else { p.error };
                h_flex()
                    .px(px(14.))
                    .py(px(9.))
                    .gap(px(10.))
                    .items_start()
                    .when(i > 0, |d| d.border_t_1().border_color(p.border_variant))
                    .child(
                        div().pt(px(2.)).child(
                            Icon::new(if e.ok { e.op.icon() } else { IconName::CircleAlert })
                                .size(px(14.))
                                .text_color(color),
                        ),
                    )
                    .child(
                        v_flex()
                            .flex_1()
                            .min_w_0()
                            .gap(px(2.))
                            .child(
                                h_flex()
                                    .gap(px(8.))
                                    .child(
                                        div()
                                            .flex_1()
                                            .min_w_0()
                                            .truncate()
                                            .text_size(px(13.))
                                            .font_weight(FontWeight::MEDIUM)
                                            .text_color(if e.ok { p.text } else { p.error })
                                            .child(e.title.clone()),
                                    )
                                    .child(
                                        div()
                                            .flex_shrink_0()
                                            .text_size(px(11.))
                                            .text_color(p.text_placeholder)
                                            .child(format!("{} · {}", since_time(e.at), took_text(e.took))),
                                    ),
                            )
                            .when(!e.detail.is_empty(), |d| {
                                d.child(
                                    div()
                                        .text_size(px(12.))
                                        .line_height(px(17.))
                                        .text_color(p.text_muted)
                                        .child(e.detail.clone()),
                                )
                            }),
                    )
                    .into_any_element()
            })
            .collect();
        let empty = rows.is_empty().then(|| {
            div()
                .p(px(16.))
                .text_size(px(13.))
                .text_color(p.text_placeholder)
                .child("Nothing yet. Catalog fetches, star syncs and installs show here.")
        });
        v_flex()
            .id("browser-activity-panel")
            .absolute()
            .top(px(64.))
            .right(px(GUTTER))
            .w(px(460.))
            .rounded(px(10.))
            .border_1()
            .border_color(p.border)
            .bg(p.elevated)
            .shadow_lg()
            .occlude()
            .overflow_hidden()
            .child(
                h_flex()
                    .px(px(14.))
                    .h(px(44.))
                    .flex_shrink_0()
                    .gap(px(8.))
                    .items_center()
                    .border_b_1()
                    .border_color(p.border)
                    .child(
                        Icon::new(IconName::Activity)
                            .size(px(14.))
                            .text_color(p.text_muted),
                    )
                    .child(
                        div()
                            .flex_1()
                            .text_size(px(14.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child("Activity"),
                    )
                    .child(
                        div()
                            .text_size(px(12.))
                            .text_color(p.text_placeholder)
                            .child(format!(
                                "last {} · {}",
                                self.browser.activity.len(),
                                catalog::LOG_FILE
                            )),
                    )
                    .child(
                        Button::new("browser-activity-close")
                            .ghost()
                            .xsmall()
                            .icon(IconName::X)
                            .on_click(cx.listener(|this, _, _, cx| {
                                this.browser.activity_open = false;
                                cx.notify();
                            })),
                    ),
            )
            .child(
                v_flex()
                    .id("browser-activity-rows")
                    .max_h(px(470.))
                    .overflow_y_scrollbar()
                    .children(rows)
                    .children(empty),
            )
    }

    fn filter_column(&self, matched: &[(&Addon, Option<u32>)], p: Palette, cx: &mut Context<Self>) -> Div {
        let b = &self.browser;
        let query = b.search.read(cx).value().trim().to_owned();
        let active = !b.facets.is_empty() || b.hide_code || !query.is_empty();
        let count = |f: &Facet| {
            matched
                .iter()
                .filter(|(a, s)| {
                    self.catalog_passes(a, s.is_some(), Some(f.group())) && self.catalog_facet(a, f)
                })
                .count()
        };
        let row = |f: Facet, cx: &mut Context<Self>| {
            let n = count(&f);
            let on = b.facets.contains(&f);
            let key = f.clone();
            facet_row(format!("browser-filter-{f:?}"), on, f.label(), n, p)
                .on_click(cx.listener(move |this, _, _, cx| {
                    if !this.browser.facets.remove(&key) {
                        this.browser.facets.insert(key.clone());
                    }
                    cx.notify();
                }))
                .into_any_element()
        };
        let group = |title: &'static str, rows: Vec<AnyElement>| {
            v_flex()
                .gap(px(1.))
                .child(div().pb(px(4.)).child(label(title, p)))
                .children(rows)
        };
        let types: Vec<AnyElement> = Type::ALL
            .into_iter()
            .map(Facet::Type)
            .filter(|f| b.facets.contains(f) || b.addons.iter().any(|a| self.catalog_facet(a, f)))
            .map(|f| row(f, cx))
            .collect();
        let states: Vec<AnyElement> = Want::ALL.into_iter().map(|w| row(Facet::State(w), cx)).collect();
        let authors: Vec<AnyElement> = publishers::by_publisher(&b.addons)
            .into_iter()
            .map(|(pb, _)| Facet::Author {
                id: pb.id,
                name: pb.name,
            })
            .enumerate()
            .filter(|(i, f)| *i < AUTHORS_SHOWN || b.facets.contains(f))
            .map(|(_, f)| row(f, cx))
            .collect();
        let natives = matched
            .iter()
            .filter(|(a, s)| {
                a.native(&current_version()) && self.catalog_passes(a, s.is_some(), Some(Group::Code))
            })
            .count();
        let code = facet_row(
            "browser-filter-code",
            b.hide_code,
            "Hide addons that run code",
            natives,
            p,
        )
        .on_click(cx.listener(|this, _, _, cx| {
            this.browser.hide_code = !this.browser.hide_code;
            cx.notify();
        }))
        .into_any_element();
        let clear = active.then(|| {
            div()
                .id("browser-filters-clear")
                .text_size(px(12.))
                .text_color(p.accent)
                .cursor_pointer()
                .hover(move |s| s.text_color(p.accent_hover))
                .child("Clear filters")
                .on_click(cx.listener(|this, _, _, cx| this.clear_filters(cx)))
        });
        v_flex()
            .w(px(FILTERS_W))
            .flex_shrink_0()
            .h_full()
            .gap(px(14.))
            .child(
                Input::new(&b.search).cleanable(true).prefix(
                    Icon::new(IconName::Search)
                        .size(px(14.))
                        .text_color(p.text_placeholder),
                ),
            )
            .child(
                h_flex()
                    .pb(px(8.))
                    .mb(px(-6.))
                    .border_b_1()
                    .border_color(p.border_variant)
                    .justify_between()
                    .child(
                        h_flex()
                            .gap(px(6.))
                            .text_color(p.text_muted)
                            .child(Icon::new(IconName::Funnel).size(px(13.)))
                            .child(
                                div()
                                    .text_size(px(13.))
                                    .font_weight(FontWeight::MEDIUM)
                                    .child("Filters"),
                            ),
                    )
                    .children(clear),
            )
            .child(
                v_flex()
                    .flex_1()
                    .min_h_0()
                    .scroll_y("browser-filters")
                    .gap(px(16.))
                    .pr(px(SCROLLBAR_GAP))
                    .pb(px(8.))
                    .child(group("TYPE", types))
                    .child(group("STATE", states))
                    .child(group("RUNS CODE IN THE GAME", vec![code]))
                    .when(!authors.is_empty(), |d| d.child(group("AUTHOR", authors))),
            )
    }

    fn category_bar(&self, matched: &[(&Addon, Option<u32>)], p: Palette, cx: &mut Context<Self>) -> Div {
        let tabs: Vec<Tab> = [None].into_iter().chain(shelves()).collect();
        let pills: Vec<AnyElement> = tabs
            .into_iter()
            .map(|tab| {
                let n = matched
                    .iter()
                    .filter(|(a, s)| {
                        self.catalog_passes(a, s.is_some(), Some(Group::Category))
                            && tab.is_none_or(|t| a.category() == t)
                    })
                    .count();
                let on = self.browser.tab == tab;
                h_flex()
                    .id(SharedString::from(format!("browser-tab-{tab:?}")))
                    .h(px(28.))
                    .px(px(11.))
                    .gap(px(6.))
                    .flex_shrink_0()
                    .rounded(px(14.))
                    .border_1()
                    .cursor_pointer()
                    .text_size(px(13.))
                    .map(|d| {
                        if on {
                            d.bg(p.accent.opacity(0.14))
                                .border_color(p.accent.opacity(0.6))
                                .text_color(p.text)
                                .font_weight(FontWeight::MEDIUM)
                        } else {
                            d.border_color(p.border)
                                .text_color(p.text_muted)
                                .hover(move |s| s.bg(p.hover).text_color(p.text))
                        }
                    })
                    .child(tab_label(tab))
                    .child(
                        div()
                            .text_size(px(11.))
                            .text_color(if on { p.text_muted } else { p.text_placeholder })
                            .child(n.to_string()),
                    )
                    .on_click(cx.listener(move |this, _, _, cx| this.shelve(tab, cx)))
                    .into_any_element()
            })
            .collect();
        let back = h_flex()
            .id("browser-categories")
            .h(px(28.))
            .px(px(8.))
            .ml(px(-8.))
            .gap(px(6.))
            .flex_shrink_0()
            .rounded(px(5.))
            .cursor_pointer()
            .text_size(px(13.))
            .text_color(p.text_muted)
            .hover(move |s| s.bg(p.hover).text_color(p.text))
            .child(Icon::new(IconName::ArrowLeft).size(px(14.)))
            .child("Categories")
            .tooltip(|window, cx| Tooltip::new("Back to the category tiles").build(window, cx))
            .on_click(cx.listener(|this, _, _, cx| this.show_categories(cx)));
        h_flex()
            .gap(px(6.))
            .flex_wrap()
            .child(back)
            .child(
                div()
                    .flex_shrink_0()
                    .w(px(1.))
                    .h(px(14.))
                    .mx(px(4.))
                    .my(px(7.))
                    .bg(p.border),
            )
            .children(pills)
    }

    fn sort_button(&self, sort: Sort, searching: bool, cx: &mut Context<Self>) -> impl IntoElement + use<> {
        let shell = cx.entity().downgrade();
        Button::new("browser-sort")
            .outline()
            .small()
            .icon(IconName::ArrowUpDown)
            .label(sort.label())
            .dropdown_caret(true)
            .dropdown_menu_with_anchor(Anchor::TopRight, move |mut menu, _, _| {
                for s in Sort::ALL.into_iter().filter(|s| searching || *s != Sort::Match) {
                    let shell = shell.clone();
                    menu = menu.item(PopupMenuItem::new(s.label()).checked(s == sort).on_click(
                        move |_, _, cx| {
                            shell.update(cx, |this, cx| this.set_sort(s, cx)).ok();
                        },
                    ));
                }
                menu
            })
    }

    fn browser_grid(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let text = self.browser.search.read(cx).value().trim().to_owned();
        if text.is_empty() {
            self.browser.search_sort = None;
        }
        let query = Query::parse(&text);
        let matched: Vec<(&Addon, Option<u32>)> =
            self.browser.addons.iter().map(|a| (a, query.score(a))).collect();
        let mut shown: Vec<(&Addon, u32)> = matched
            .iter()
            .filter(|(a, s)| self.catalog_passes(a, s.is_some(), None))
            .map(|(a, s)| (*a, s.unwrap_or(0)))
            .collect();
        let sort = self.sort_now(&query);
        shown.sort_by(|(a, sa), (b, sb)| {
            let first = match sort {
                Sort::Match => sb
                    .cmp(sa)
                    .then_with(|| self.browser.stars_of(b).cmp(&self.browser.stars_of(a))),
                Sort::Stars => self.browser.stars_of(b).cmp(&self.browser.stars_of(a)),
                Sort::Downloads => b.downloads.cmp(&a.downloads),
                Sort::Trending => b.downloads_30d.cmp(&a.downloads_30d),
                Sort::Updated => b.updated_at.cmp(&a.updated_at),
                Sort::Newest => b.created_at.cmp(&a.created_at),
                Sort::Name => std::cmp::Ordering::Equal,
            };
            first.then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
        });
        let filtering = !query.is_empty()
            || !self.browser.facets.is_empty()
            || self.browser.tab.is_some()
            || self.browser.hide_code;
        let count = if filtering {
            plural(shown.len(), "result", "results")
        } else {
            plural(shown.len(), "addon", "addons")
        };
        let landing = !self.browser.addons.is_empty() && self.browser.on_landing(!query.is_empty());
        let width = self.content_width - FILTERS_W - 20.;
        if landing {
            let filters = self.filter_column(&matched, p, cx);
            let addons: Vec<&Addon> = shown.iter().map(|(a, _)| *a).collect();
            let rows = self.card_rows(&addons, &query, width, p, cx);
            let page = self
                .landing(width, p, cx)
                .child(self.all_heading(count, sort, width, p, cx))
                .children(rows);
            return self.browser_columns(filters, div(), self.scroll_area(page, cx));
        }
        let bar = h_flex()
            .pb(px(14.))
            .gap(px(12.))
            .flex_shrink_0()
            .items_start()
            .child(div().flex_1().min_w_0().child(self.category_bar(&matched, p, cx)))
            .child(
                h_flex()
                    .gap(px(10.))
                    .flex_shrink_0()
                    .items_center()
                    .h(px(28.))
                    .child(
                        div()
                            .text_size(px(12.))
                            .text_color(p.text_placeholder)
                            .child(count),
                    )
                    .child(self.sort_button(sort, !query.is_empty(), cx)),
            );
        let filters = self.filter_column(&matched, p, cx);
        let found = self.creators_found(&query, p, cx);
        let bare = query.is_empty() && self.browser.facets.is_empty() && !self.browser.hide_code;
        let body = if shown.is_empty() && found.is_some() {
            self.scroll_area(
                v_flex()
                    .id("browser-cards")
                    .pb(px(24.))
                    .gap(px(GAP))
                    .children(found)
                    .child(
                        div()
                            .text_size(px(13.))
                            .text_color(p.text_placeholder)
                            .child("No addons match this search and these filters."),
                    ),
                cx,
            )
            .into_any_element()
        } else if shown.is_empty() && bare && !self.browser.addons.is_empty() && self.browser.tab.is_some() {
            self.empty_shelf(self.browser.tab, p, cx)
        } else if shown.is_empty() {
            let (title, body, action) = if self.browser.addons.is_empty() {
                (
                    "No addons to show yet",
                    if self.browser.fetching {
                        "Checking the registry for addons…"
                    } else {
                        "The registry couldn't be reached. Retry to check again."
                    },
                    Button::new("browser-empty-retry")
                        .outline()
                        .small()
                        .icon(IconName::RefreshCw)
                        .label("Retry")
                        .disabled(self.browser.fetching)
                        .on_click(cx.listener(|this, _, _, cx| this.fetch_catalog(cx))),
                )
            } else {
                (
                    "No addons match",
                    "Nothing matches this search and these filters. Check the spelling, or try author: or category:.",
                    Button::new("browser-empty-clear")
                        .outline()
                        .small()
                        .icon(IconName::X)
                        .label("Clear search and filters")
                        .on_click(cx.listener(|this, _, _, cx| this.clear_filters(cx))),
                )
            };
            self.empty(IconName::PackageSearch, title, body, action, p)
        } else {
            let addons: Vec<&Addon> = shown.iter().map(|(a, _)| *a).collect();
            let rows = self.card_rows(&addons, &query, width, p, cx);
            self.scroll_area(
                v_flex()
                    .id("browser-cards")
                    .pb(px(24.))
                    .gap(px(GAP))
                    .children(found)
                    .children(rows),
                cx,
            )
            .into_any_element()
        };
        self.browser_columns(filters, bar, body)
    }

    fn browser_columns(&self, filters: Div, bar: Div, body: impl IntoElement) -> AnyElement {
        h_flex()
            .flex_1()
            .min_h_0()
            .items_start()
            .pl(px(GUTTER))
            .pb(px(20.))
            .gap(px(20.))
            .child(filters)
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .h_full()
                    .child(bar.pr(px(GUTTER)))
                    .child(body),
            )
            .into_any_element()
    }

    fn landing_bar(&self) -> Div {
        h_flex()
            .h(px(28.))
            .flex_shrink_0()
            .items_center()
            .text_size(px(15.))
            .font_weight(FontWeight::SEMIBOLD)
            .child("Browse by category")
    }

    fn all_heading(&self, count: String, sort: Sort, width: f32, p: Palette, cx: &mut Context<Self>) -> Div {
        h_flex()
            .w(px(width - 1.))
            .pt(px(10.))
            .gap(px(12.))
            .flex_shrink_0()
            .items_center()
            .child(
                div()
                    .flex_1()
                    .min_w_0()
                    .text_size(px(15.))
                    .font_weight(FontWeight::SEMIBOLD)
                    .child("All addons"),
            )
            .child(
                div()
                    .text_size(px(12.))
                    .text_color(p.text_placeholder)
                    .child(count),
            )
            .child(self.sort_button(sort, false, cx))
    }

    fn landing(&self, width: f32, p: Palette, cx: &mut Context<Self>) -> Stateful<Div> {
        let tabs: Vec<Tab> = shelves().collect();
        let columns = shelf_columns(width);
        let tile_w = ((width - GAP * (columns - 1) as f32) / columns as f32).floor() - 1.;
        let rows: Vec<AnyElement> = tabs
            .chunks(columns)
            .map(|chunk| {
                h_flex()
                    .flex_shrink_0()
                    .gap(px(GAP))
                    .children(chunk.iter().map(|t| self.shelf_tile(*t, tile_w, p, cx)))
                    .into_any_element()
            })
            .collect();
        v_flex()
            .id("browser-landing")
            .pb(px(24.))
            .gap(px(GAP))
            .child(self.landing_bar())
            .children(rows)
    }

    fn has_icon(&self, a: &Addon) -> bool {
        a.icon
            .as_ref()
            .is_some_and(|u| self.browser.icons.contains_key(u))
    }

    /// Up to `n` of `addons`' icons, overlapping, ringed in `ring`; the ones with
    /// an icon of their own first, then a count of the rest.
    fn faces(&self, addons: &[&Addon], n: usize, ring: Hsla, p: Palette) -> Div {
        let mut picked: Vec<&Addon> = addons.to_vec();
        picked.sort_by_key(|a| !self.has_icon(a));
        let rest = addons.len().saturating_sub(n);
        h_flex()
            .flex_shrink_0()
            .items_center()
            .children(picked.into_iter().take(n).enumerate().map(|(i, a)| {
                div()
                    .p(px(2.))
                    .rounded(px(FACE * 0.22 + 2.))
                    .bg(ring)
                    .when(i > 0, |d| d.ml(px(-12.)))
                    .child(self.catalog_icon(a, FACE, p))
            }))
            .when(rest > 0, |d| {
                d.child(
                    div()
                        .ml(px(6.))
                        .h(px(24.))
                        .px(px(8.))
                        .flex()
                        .items_center()
                        .rounded(px(12.))
                        .bg(ring.blend(p.text.opacity(0.07)))
                        .text_size(px(12.))
                        .font_weight(FontWeight::MEDIUM)
                        .text_color(p.text_muted)
                        .child(format!("+{rest}")),
                )
            })
    }

    fn shelf_tile(&self, tab: Tab, width: f32, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let addons = shelved(&self.browser.addons, tab, |a| self.browser.stars_of(a));
        let n = addons.len();
        let empty = n == 0;
        let tint = shelf_tint(tab, p);
        let band = if empty {
            p.surface
        } else {
            p.surface.blend(tint.opacity(0.13))
        };
        let slot = div()
            .size(px(FACE + 4.))
            .rounded(px(10.))
            .border_1()
            .border_dashed()
            .border_color(p.border)
            .flex()
            .items_center()
            .justify_center()
            .child(shelf_glyph(tab, 20., p.text_placeholder));
        let art = h_flex()
            .relative()
            .h(px(TILE_ART))
            .w_full()
            .px(px(14.))
            .flex_shrink_0()
            .items_center()
            .overflow_hidden()
            .rounded_t(px(11.))
            .bg(band)
            .border_b_1()
            .border_color(if empty {
                p.border_variant
            } else {
                tint.opacity(0.18)
            })
            .child(
                div()
                    .absolute()
                    .right(px(-10.))
                    .bottom(px(-18.))
                    .child(shelf_glyph(
                        tab,
                        TILE_ART + 8.,
                        if empty {
                            p.text_placeholder.opacity(0.1)
                        } else {
                            tint.opacity(0.22)
                        },
                    )),
            )
            .map(|d| {
                if empty {
                    d.child(slot)
                } else {
                    d.child(self.faces(&addons, tile_faces(width), band, p))
                }
            });
        v_flex()
            .id(SharedString::from(format!("browser-shelf-{}", tab_label(tab))))
            .w(px(width))
            .flex_none()
            .rounded(px(12.))
            .border_1()
            .border_color(if empty { p.border_variant } else { p.border })
            .bg(p.surface)
            .cursor_pointer()
            .hover(move |s| {
                if empty {
                    s.bg(p.hover)
                } else {
                    s.bg(p.hover).border_color(tint.opacity(0.75))
                }
            })
            .child(art)
            .child(
                h_flex()
                    .px(px(14.))
                    .py(px(12.))
                    .gap(px(10.))
                    .items_center()
                    .child(
                        v_flex()
                            .flex_1()
                            .min_w_0()
                            .gap(px(2.))
                            .child(
                                div()
                                    .text_size(px(15.))
                                    .font_weight(FontWeight::SEMIBOLD)
                                    .text_color(if empty { p.text_muted } else { p.text })
                                    .truncate()
                                    .child(tab_label(tab)),
                            )
                            .child(
                                div()
                                    .text_size(px(12.))
                                    .text_color(p.text_placeholder)
                                    .truncate()
                                    .child(if empty {
                                        "No addons yet".to_owned()
                                    } else {
                                        plural(n, "addon", "addons")
                                    }),
                            ),
                    )
                    .child(
                        Icon::new(IconName::ChevronRight)
                            .size(px(16.))
                            .text_color(p.text_placeholder),
                    ),
            )
            .on_click(cx.listener(move |this, _, _, cx| this.shelve(tab, cx)))
            .into_any_element()
    }

    fn empty_shelf(&self, tab: Tab, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        v_flex()
            .flex_1()
            .items_center()
            .justify_center()
            .gap(px(8.))
            .pb(px(48.))
            .child(tile(44., p).child(shelf_glyph(tab, 22., p.text_muted)))
            .child(
                div()
                    .pt(px(8.))
                    .text_size(px(15.))
                    .font_weight(FontWeight::SEMIBOLD)
                    .child(format!("Nothing in {} yet", tab_label(tab))),
            )
            .child(
                div()
                    .max_w(px(400.))
                    .text_center()
                    .text_size(px(13.))
                    .line_height(px(19.))
                    .text_color(p.text_muted)
                    .child("Addons published in this category show here."),
            )
            .child(
                h_flex()
                    .pt(px(8.))
                    .gap(px(8.))
                    .child(
                        Button::new("browser-shelf-empty-all")
                            .outline()
                            .small()
                            .icon(IconName::LayoutGrid)
                            .label("All addons")
                            .on_click(cx.listener(|this, _, _, cx| this.shelve(None, cx))),
                    )
                    .child(
                        Button::new("browser-shelf-empty-back")
                            .ghost()
                            .small()
                            .icon(IconName::ArrowLeft)
                            .label("Categories")
                            .on_click(cx.listener(|this, _, _, cx| this.show_categories(cx))),
                    ),
            )
            .into_any_element()
    }

    fn card_rows(
        &self,
        addons: &[&Addon],
        query: &Query,
        width: f32,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> Vec<AnyElement> {
        let columns = ((width + GAP) / (CARD_W + GAP)).floor().max(1.) as usize;
        let card_w = ((width - GAP * (columns - 1) as f32) / columns as f32).floor() - 1.;
        addons
            .chunks(columns)
            .map(|chunk| {
                h_flex()
                    .flex_shrink_0()
                    .items_stretch()
                    .gap(px(GAP))
                    .children(chunk.iter().map(|a| self.browser_card(a, query, card_w, p, cx)))
                    .into_any_element()
            })
            .collect()
    }

    fn scroll_area(&self, body: Stateful<Div>, cx: &Context<Self>) -> Div {
        div()
            .relative()
            .flex_1()
            .min_h_0()
            .w_full()
            .child(
                div()
                    .id("browser-scroll")
                    .size_full()
                    .flex()
                    .flex_col()
                    .overflow_y_scroll()
                    .track_scroll(&self.browser.scroll)
                    .child(body.flex_none()),
            )
            .child(self.browser.smooth.overlay(&self.browser.scroll, cx))
    }

    fn tags(&self, a: &Addon, id: String, p: Palette) -> Option<Div> {
        let category = a.category_label();
        let kinds = a.kind_labels();
        let native = a.native(&current_version());
        (category.is_some() || !kinds.is_empty() || native).then(|| {
            h_flex()
                .flex_wrap()
                .gap(px(6.))
                .when_some(category, |d, c| {
                    d.child(
                        tag(c, p)
                            .text_color(p.text)
                            .bg(p.accent.opacity(0.1))
                            .border_color(p.accent.opacity(0.35)),
                    )
                })
                .children(kinds.into_iter().map(|k| tag(k, p)))
                .when(native, |d| d.child(native_badge(id, p)))
        })
    }

    fn browser_card(
        &self,
        a: &Addon,
        query: &Query,
        width: f32,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let launcher = current_version();
        let offered = a.offered(&launcher).or_else(|| a.newest());
        let updating = self.catalog_update(&a.id).is_some();
        let failed = self.browser.failed.get(&a.id).cloned();
        let (also, blocked) = self.needs_of(a);
        let blocked = blocked.filter(|_| failed.is_none());
        let progress = self.browser.working.get(&a.id).map(Job::progress);
        let id = a.id.clone();
        let marks: Vec<(std::ops::Range<usize>, HighlightStyle)> = query
            .marks(&a.name)
            .into_iter()
            .map(|r| {
                (
                    r,
                    HighlightStyle {
                        color: Some(p.accent),
                        background_color: Some(p.accent.opacity(0.12)),
                        ..HighlightStyle::default()
                    },
                )
            })
            .collect();
        let name = StyledText::new(a.name.clone()).with_highlights(marks);
        let mut stats = vec![
            h_flex()
                .gap(px(4.))
                .child(Icon::new(IconName::Download).size(px(12.)))
                .child(compact(a.downloads))
                .into_any_element(),
        ];
        if a.downloads_30d > 0 {
            stats.push(
                div()
                    .child(format!("{} this month", compact(a.downloads_30d)))
                    .into_any_element(),
            );
        }
        if let Some(when) = since(&a.updated_at) {
            stats.push(div().child(format!("Updated {when}")).into_any_element());
        }
        let author = self.byline(a, format!("browser-card-by-{}", a.id), 12., p, cx);
        v_flex()
            .id(SharedString::from(format!("browser-card-{}", a.id)))
            .w(px(width))
            .flex_none()
            .p(px(16.))
            .gap(px(10.))
            .rounded(px(10.))
            .border_1()
            .border_color(if failed.is_some() {
                p.error.opacity(0.6)
            } else if updating {
                p.accent.opacity(0.6)
            } else {
                p.border
            })
            .bg(p.surface)
            .cursor_pointer()
            .hover(move |s| s.border_color(p.accent).bg(p.hover))
            .on_click(cx.listener(move |this, _, _, cx| this.open_details(&id, cx)))
            .child(
                h_flex()
                    .gap(px(12.))
                    .items_start()
                    .child(self.catalog_icon(a, 44., p))
                    .child(
                        v_flex()
                            .flex_1()
                            .min_w_0()
                            .gap(px(2.))
                            .child(
                                div()
                                    .text_size(px(15.))
                                    .font_weight(FontWeight::SEMIBOLD)
                                    .truncate()
                                    .child(name),
                            )
                            .child(author),
                    )
                    .child(self.star_button(a, format!("browser-star-{}", a.id), false, p, cx)),
            )
            .child(
                div()
                    .h(px(38.))
                    .text_size(px(13.))
                    .line_height(px(19.))
                    .text_color(p.text_muted)
                    .line_clamp(2)
                    .text_ellipsis()
                    .child(a.summary.clone()),
            )
            .children(self.tags(a, format!("browser-native-{}", a.id), p))
            .child(div().flex_1())
            .child(
                h_flex()
                    .gap(px(10.))
                    .text_size(px(12.))
                    .text_color(p.text_placeholder)
                    .overflow_hidden()
                    .whitespace_nowrap()
                    .children(stats),
            )
            .child(
                h_flex()
                    .gap(px(10.))
                    .items_center()
                    .child({
                        let version = offered.map(|v| format!("v{}", v.version)).unwrap_or_default();
                        let line = div()
                            .id(SharedString::from(format!("browser-card-also-{}", a.id)))
                            .flex_1()
                            .min_w_0()
                            .truncate()
                            .text_size(px(12.))
                            .text_color(p.text_placeholder);
                        if also.is_empty() {
                            line.child(version)
                        } else {
                            let tip = format!("Also installs: {}", also.join(", "));
                            line.child(format!("{version} · Also installs: {}", also.join(", ")))
                                .tooltip(move |window, cx| Tooltip::new(tip.clone()).build(window, cx))
                        }
                    })
                    .child(self.offer_button(a, format!("browser-offer-{}", a.id), cx)),
            )
            .when_some(progress, |d, pr| {
                d.child(
                    div()
                        .h(px(3.))
                        .w_full()
                        .rounded(px(2.))
                        .bg(p.border_variant)
                        .child(
                            div()
                                .h_full()
                                .rounded(px(2.))
                                .bg(p.accent)
                                .w(relative(pr.fraction())),
                        ),
                )
            })
            .when_some(failed, |d, e| {
                let tip = e.clone();
                d.child(
                    h_flex()
                        .id(SharedString::from(format!("browser-failed-{}", a.id)))
                        .gap(px(6.))
                        .text_size(px(12.))
                        .text_color(p.error)
                        .child(Icon::new(IconName::CircleAlert).size(px(12.)))
                        .child(div().flex_1().min_w_0().truncate().child(e))
                        .tooltip(move |window, cx| Tooltip::new(tip.clone()).build(window, cx)),
                )
            })
            .when_some(blocked, |d, e| {
                let tip = e.clone();
                d.child(
                    h_flex()
                        .id(SharedString::from(format!("browser-blocked-{}", a.id)))
                        .gap(px(6.))
                        .text_size(px(12.))
                        .text_color(p.warning)
                        .child(Icon::new(IconName::TriangleAlert).size(px(12.)))
                        .child(div().flex_1().min_w_0().truncate().child(e))
                        .tooltip(move |window, cx| Tooltip::new(tip.clone()).build(window, cx)),
                )
            })
            .into_any_element()
    }

    fn trend_chart(&self, a: &Addon, p: Palette) -> Option<Div> {
        let trend = a.trend.as_ref().filter(|t| !t.downloads.is_empty())?;
        let max = trend.downloads.iter().copied().max().unwrap_or(0).max(1);
        let total: u64 = trend.downloads.iter().sum();
        let bars: Vec<AnyElement> = trend
            .downloads
            .iter()
            .enumerate()
            .map(|(i, n)| {
                let when = trend.days.get(i).map(|d| day(d)).unwrap_or_default();
                let tip = format!("{when}: {}", plural(*n as usize, "download", "downloads"));
                div()
                    .id(SharedString::from(format!("browser-trend-{i}")))
                    .flex_1()
                    .h_full()
                    .flex()
                    .flex_col()
                    .justify_end()
                    .hover(move |s| s.bg(p.hover))
                    .child(
                        div()
                            .w_full()
                            .h(px((54. * *n as f32 / max as f32).max(2.)))
                            .rounded_t(px(2.))
                            .bg(if *n == 0 { p.border } else { p.accent.opacity(0.75) }),
                    )
                    .tooltip(move |window, cx| Tooltip::new(tip.clone()).build(window, cx))
                    .into_any_element()
            })
            .collect();
        Some(
            v_flex()
                .p(px(16.))
                .gap(px(10.))
                .rounded(px(10.))
                .border_1()
                .border_color(p.border)
                .bg(p.surface)
                .child(
                    h_flex()
                        .child(
                            div()
                                .flex_1()
                                .text_size(px(14.))
                                .font_weight(FontWeight::SEMIBOLD)
                                .child("Downloads, last 30 days"),
                        )
                        .child(
                            div()
                                .text_size(px(12.))
                                .text_color(p.text_placeholder)
                                .child(plural(total as usize, "download", "downloads")),
                        ),
                )
                .child(h_flex().h(px(56.)).gap(px(2.)).items_end().children(bars))
                .child(
                    h_flex()
                        .justify_between()
                        .text_size(px(11.))
                        .text_color(p.text_placeholder)
                        .child(trend.days.first().map(|d| day(d)).unwrap_or_default())
                        .child(trend.days.last().map(|d| day(d)).unwrap_or_default()),
                ),
        )
    }

    fn browser_detail(&self, id: &str, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let Some(listed) = self.browser.find(id) else {
            return div().into_any_element();
        };
        let full = self.browser.details.get(id);
        let a = full.unwrap_or(listed);
        let launcher = current_version();
        let mono = cx.theme().mono_font_family.clone();
        let installed = self.installed_version(&a.id);
        let offered = a.offered(&launcher).or_else(|| a.newest());
        let back = self.back_bar(p, cx);
        let homepage = a
            .homepage
            .clone()
            .filter(|u| u.starts_with("https://"))
            .map(|url| {
                Button::new("browser-homepage")
                    .ghost()
                    .small()
                    .icon(IconName::ExternalLink)
                    .label("Homepage")
                    .on_click(move |_, _, cx| cx.open_url(&url))
            });
        let hero = h_flex()
            .gap(px(16.))
            .items_start()
            .flex_shrink_0()
            .child(self.catalog_icon(a, 64., p))
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .gap(px(6.))
                    .child(
                        div()
                            .text_size(px(22.))
                            .line_height(px(28.))
                            .font_weight(FontWeight::BOLD)
                            .child(a.name.clone()),
                    )
                    .child(
                        h_flex()
                            .gap(px(4.))
                            .min_w_0()
                            .child(self.byline(a, "browser-detail-by".into(), 13., p, cx))
                            .child(
                                div()
                                    .flex_shrink_0()
                                    .text_size(px(13.))
                                    .text_color(p.text_placeholder)
                                    .child(format!("· {}", a.id)),
                            ),
                    )
                    .children(self.tags(a, "browser-detail-native".into(), p)),
            )
            .child(
                h_flex()
                    .gap(px(8.))
                    .flex_shrink_0()
                    .child(self.star_button(listed, "browser-detail-star".into(), true, p, cx))
                    .children(homepage)
                    .child(self.offer_button(listed, "browser-detail-offer".into(), cx)),
            );
        let working = self.browser.working.get(&a.id).map(|job| {
            let pr = job.progress();
            let item = job.item();
            let also = job.also();
            v_flex()
                .gap(px(6.))
                .p(px(12.))
                .rounded(px(8.))
                .border_1()
                .border_color(p.accent.opacity(0.4))
                .bg(p.accent.opacity(0.06))
                .child(div().text_size(px(13.)).child(pr.long(item.as_deref())))
                .when(!also.is_empty(), |d| {
                    d.child(
                        div()
                            .text_size(px(12.))
                            .text_color(p.text_muted)
                            .child(format!("Also installing: {}", also.join(", "))),
                    )
                })
                .child(
                    div()
                        .h(px(4.))
                        .w_full()
                        .rounded(px(2.))
                        .bg(p.border_variant)
                        .child(
                            div()
                                .h_full()
                                .rounded(px(2.))
                                .bg(p.accent)
                                .w(relative(pr.fraction())),
                        ),
                )
        });
        let failure = self.browser.failed.get(&a.id).map(|e| {
            let target = a.id.clone();
            h_flex()
                .gap(px(10.))
                .items_start()
                .p(px(12.))
                .rounded(px(8.))
                .bg(p.error.opacity(0.08))
                .border_1()
                .border_color(p.error.opacity(0.4))
                .child(Icon::new(IconName::CircleAlert).size(px(15.)).text_color(p.error))
                .child(
                    v_flex()
                        .flex_1()
                        .min_w_0()
                        .gap(px(2.))
                        .child(div().text_size(px(13.)).font_weight(FontWeight::SEMIBOLD).child(
                            if self.browser.refused.contains(&a.id) {
                                "Missing requirements"
                            } else if installed.is_some() {
                                "The update failed"
                            } else {
                                "The install failed"
                            },
                        ))
                        .child(div().text_size(px(13.)).text_color(p.text_muted).child(e.clone())),
                )
                .child(
                    Button::new("browser-detail-retry")
                        .outline()
                        .small()
                        .icon(IconName::RotateCw)
                        .label("Retry")
                        .on_click(cx.listener(move |this, _, _, cx| this.install_from_catalog(&target, cx))),
                )
        });
        let needs = (!self.browser.working.contains_key(&a.id)
            && !self.browser.failed.contains_key(&a.id)
            && installed.is_none())
        .then(|| self.requirements(listed))
        .flatten()
        .and_then(|plan| match plan {
            Ok(plan) if plan.also().is_empty() => None,
            Ok(plan) => Some((
                false,
                "Also installs".to_owned(),
                plan.also()
                    .iter()
                    .map(|s| match &s.replaces {
                        Some(old) => format!("{} (replaces {old})", s.label()),
                        None => s.label(),
                    })
                    .collect::<Vec<_>>()
                    .join(", "),
            )),
            Err(e) => Some((true, "Missing requirements".to_owned(), e.to_string())),
        })
        .map(|(bad, title, text)| {
            let tone = if bad { p.warning } else { p.accent };
            h_flex()
                .gap(px(10.))
                .items_start()
                .p(px(12.))
                .rounded(px(8.))
                .bg(tone.opacity(0.06))
                .border_1()
                .border_color(tone.opacity(0.4))
                .child(
                    Icon::new(if bad {
                        IconName::TriangleAlert
                    } else {
                        IconName::Package
                    })
                    .size(px(15.))
                    .text_color(tone),
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
                                .child(title),
                        )
                        .child(div().text_size(px(13.)).text_color(p.text_muted).child(text)),
                )
        });
        let requirement = offered
            .and_then(|v| v.launcher.as_ref())
            .map_or_else(|| "Any launcher".to_owned(), |r| r.to_string());
        let fits = offered.is_some_and(|v| v.accepts(&launcher));
        let sha = offered
            .map(|v| v.sha256.chars().take(12).collect::<String>())
            .unwrap_or_default();
        let facts = h_flex()
            .flex_wrap()
            .gap_x(px(28.))
            .gap_y(px(14.))
            .p(px(16.))
            .rounded(px(10.))
            .border_1()
            .border_color(p.border)
            .bg(p.surface)
            .child(stat("STARS", compact(self.browser.stars_of(listed)), p.text, p))
            .child(stat(
                "DOWNLOADS",
                format!(
                    "{} · {} in 30 days",
                    compact(a.downloads),
                    compact(a.downloads_30d)
                ),
                p.text,
                p,
            ))
            .child(stat(
                "SIZE",
                offered.map(|v| size_text(v.size)).unwrap_or_default(),
                p.text,
                p,
            ))
            .child(stat(
                "LAST UPDATED",
                since(&a.updated_at).unwrap_or_else(|| "–".into()),
                p.text,
                p,
            ))
            .child(stat("FIRST PUBLISHED", day(&a.created_at), p.text, p))
            .child(stat(
                "LAUNCHER",
                requirement,
                if fits { p.text } else { p.warning },
                p,
            ))
            .child(
                v_flex().gap(px(3.)).child(label("SHA-256", p)).child(
                    div()
                        .text_size(px(13.))
                        .font_family(mono.clone())
                        .child(format!("{sha}…")),
                ),
            )
            .when_some(installed.clone(), |d, v| {
                d.child(stat("INSTALLED", format!("v{v}"), p.success, p))
            });
        let description = if a.description.trim().is_empty() {
            vec![
                div()
                    .text_color(p.text.opacity(0.84))
                    .child(if a.summary.is_empty() {
                        "No description yet.".to_owned()
                    } else {
                        a.summary.clone()
                    })
                    .into_any_element(),
            ]
        } else {
            crate::news::render_markdown(&format!("browser-{}", a.id), &a.description, p, &mono)
        };
        let about = v_flex()
            .p(px(24.))
            .rounded(px(10.))
            .border_1()
            .border_color(p.border)
            .bg(p.surface)
            .text_size(px(14.))
            .line_height(px(22.))
            .child(div().pb(px(12.)).child(label("ABOUT", p)))
            .children(description);
        let people = self.published_by(listed, p, cx);
        let more = self.more_from(listed, p, cx);
        let listed_versions = a.listed();
        let count = listed_versions.len();
        let versions: Vec<AnyElement> = listed_versions
            .into_iter()
            .enumerate()
            .map(|(i, v)| {
                let fits = v.accepts(&launcher);
                let current = installed.as_ref() == Some(&v.version);
                v_flex()
                    .px(px(16.))
                    .py(px(12.))
                    .gap(px(6.))
                    .when(i > 0, |d| d.border_t_1().border_color(p.border_variant))
                    .child(
                        h_flex()
                            .gap(px(8.))
                            .items_center()
                            .child(
                                div()
                                    .text_size(px(14.))
                                    .font_weight(FontWeight::SEMIBOLD)
                                    .child(format!("v{}", v.version)),
                            )
                            .when(current, |d| d.child(tag("Installed", p).text_color(p.success)))
                            .child(div().flex_1())
                            .child(
                                div()
                                    .text_size(px(12.))
                                    .text_color(p.text_placeholder)
                                    .child(format!("{} · {}", day(&v.published_at), size_text(v.size))),
                            ),
                    )
                    .when(!fits, |d| {
                        d.child(div().text_size(px(12.)).text_color(p.warning).child(format!(
                            "Needs launcher {}",
                            v.launcher.as_ref().map_or_else(String::new, |r| r.to_string())
                        )))
                    })
                    .when_some(
                        v.requires
                            .as_ref()
                            .filter(|r| !r.is_empty())
                            .map(|r| self.requires_text(r)),
                        |d, text| d.child(div().text_size(px(12.)).text_color(p.text_muted).child(text)),
                    )
                    .child(
                        v_flex()
                            .text_size(px(13.))
                            .line_height(px(19.))
                            .when(v.changelog.trim().is_empty(), |d| {
                                d.child(div().text_color(p.text_placeholder).child("No changelog"))
                            })
                            .when(!v.changelog.trim().is_empty(), |d| {
                                d.children(crate::news::render_markdown(
                                    &format!("browser-{}-{}", a.id, v.version),
                                    &v.changelog,
                                    p,
                                    &mono,
                                ))
                            }),
                    )
                    .into_any_element()
            })
            .collect();
        let changelog = v_flex()
            .rounded(px(10.))
            .border_1()
            .border_color(p.border)
            .bg(p.surface)
            .overflow_hidden()
            .child(
                h_flex()
                    .px(px(16.))
                    .h(px(46.))
                    .items_center()
                    .border_b_1()
                    .border_color(p.border)
                    .child(
                        div()
                            .text_size(px(14.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child("Versions"),
                    )
                    .child(div().flex_1())
                    .child(div().text_size(px(12.)).text_color(p.text_placeholder).child(
                        if full.is_none() && self.browser.loading.contains(id) {
                            "Loading…".to_owned()
                        } else {
                            plural(count, "version", "versions")
                        },
                    )),
            )
            .children(versions);
        let body = v_flex()
            .id("browser-detail")
            .px(px(GUTTER))
            .pb(px(24.))
            .gap(px(18.))
            .child(back)
            .child(hero)
            .children(working)
            .children(failure)
            .children(needs)
            .child(facts)
            .child(
                h_flex()
                    .items_start()
                    .gap(px(16.))
                    .child(
                        v_flex()
                            .flex_1()
                            .min_w_0()
                            .gap(px(16.))
                            .children(self.trend_chart(a, p))
                            .child(about),
                    )
                    .child(
                        v_flex()
                            .w(px(SIDE_W))
                            .flex_shrink_0()
                            .gap(px(16.))
                            .children(people)
                            .children(more)
                            .child(changelog),
                    ),
            );
        self.scroll_area(body, cx).into_any_element()
    }
}

#[cfg(test)]
mod tests {
    use super::{
        Event, Job, Progress, Stage, compact, host, shelf_columns, shelved, shelves, size_text, tile_faces,
    };
    use swofty_core::catalog::requires::{Plan, Step};
    use swofty_core::catalog::{Addon, Version};
    use swofty_core::manifest::Category;

    fn step(id: &str, size: u64) -> Step {
        let addon: Addon =
            serde_json::from_value(serde_json::json!({ "id": id, "name": format!("{id} name") })).unwrap();
        let version: Version = serde_json::from_value(serde_json::json!({
            "version": "1.0.0", "size": size, "sha256": "", "url": ""
        }))
        .unwrap();
        Step {
            addon,
            version,
            replaces: None,
        }
    }

    #[test]
    fn tallies_every_download_of_an_install() {
        let (dep, root) = (step("dep", 300), step("root", 100));
        let job = Job::new(100);
        job.track("root", Event::Addon(&root.addon, &root.version));
        job.track("root", Event::Stage(Stage::Downloading(100)));
        let plan = Plan {
            steps: vec![dep.clone(), root.clone()],
        };
        job.track("root", Event::Planned(&plan));
        assert_eq!(job.also(), ["dep name 1.0.0"]);
        job.track("root", Event::Addon(&dep.addon, &dep.version));
        job.track("root", Event::Stage(Stage::Downloading(200)));
        assert_eq!(job.item().as_deref(), Some("dep name"));
        let Progress::Downloading { done, size, .. } = job.progress() else {
            panic!("still downloading");
        };
        assert_eq!((done, size), (300, 400));
        assert!(
            job.progress()
                .long(job.item().as_deref())
                .starts_with("Downloading dep name · ")
        );
        job.track("root", Event::Addon(&root.addon, &root.version));
        job.track("root", Event::Stage(Stage::Installing));
        assert_eq!(job.item(), None);
        assert_eq!(job.progress().long(None), "Installing…");
    }

    #[test]
    fn shelves_addons_by_category_most_starred_first() {
        let addon = |id: &str, category: Option<&str>, stars: u64| -> Addon {
            serde_json::from_value(serde_json::json!({
                "id": id, "name": id, "category": category, "stars": stars
            }))
            .unwrap()
        };
        let addons = [
            addon("a", Some("seasons"), 1),
            addon("b", Some("seasons"), 9),
            addon("c", None, 3),
            addon("d", Some("gameplay"), 5),
            addon("e", Some("liveries"), 0),
        ];
        let ids = |tab| -> Vec<String> {
            shelved(&addons, tab, |a| a.stars)
                .into_iter()
                .map(|a| a.id.clone())
                .collect()
        };
        assert_eq!(ids(Some(Some(Category::Seasons))), ["b", "a"]);
        assert_eq!(ids(Some(Some(Category::Cars))), ["e"]);
        assert_eq!(ids(Some(None)), ["d", "c"]);
        assert!(ids(Some(Some(Category::Tracks))).is_empty());
        assert_eq!(ids(None), ["b", "d", "c", "a", "e"]);
    }

    #[test]
    fn lays_out_even_rows_of_tiles() {
        assert_eq!(shelf_columns(2000.), 5);
        assert_eq!(shelf_columns(1156.), 5);
        assert_eq!(shelf_columns(1155.), 2);
        assert_eq!(shelf_columns(454.), 2);
        assert_eq!(shelf_columns(453.), 1);
        let tiles = shelves().count();
        assert_eq!(tiles, 10);
        for columns in [5, 2, 1] {
            assert_eq!(tiles % columns, 0, "{columns}");
        }
        assert_eq!(tile_faces(329.), 4);
        assert_eq!(tile_faces(299.), 4);
        assert_eq!(tile_faces(247.), 3);
        assert_eq!(tile_faces(120.), 1);
    }

    #[test]
    fn names_sizes_counts_and_hosts() {
        assert_eq!(host("https://api.f1.swofty.net"), "api.f1.swofty.net");
        assert_eq!(host("http://localhost:8471"), "localhost:8471");
        assert_eq!(size_text(512), "512 B");
        assert_eq!(size_text(1_892_759), "1.8 MB");
        assert_eq!(compact(999), "999");
        assert_eq!(compact(3412), "3.4k");
        assert_eq!(compact(41_000), "41k");
    }
}
