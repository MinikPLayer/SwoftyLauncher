mod fields;
mod page;
mod raw;
mod save;

use std::collections::HashMap;
use std::panic::AssertUnwindSafe;
use std::path::{Path, PathBuf};
use std::sync::{Arc, LazyLock, Mutex};
use std::time::{Duration, SystemTime};

use gpui_kit::assets::IconName;
use gpui_kit::component::input::{InputEvent, InputState};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::{WindowExt, v_flex};
use gpui_kit::*;
use swofty_core::dbedit::draft::{DraftFile, Opened, Written};
use swofty_core::dbedit::package::{self, Incoming, PackageInfo};
use swofty_core::dbedit::{
    Catalog, Draft, Edit, Edits, Hit, ImportReport, Kind, LinkSpec, Module, ModuleView, NodeId, NodeKind,
    Target, driver_manager,
};
use swofty_core::store::Store;
use swofty_core::view::{Block, Body};

use crate::library::plural;
use crate::shell::{Page, Shell};

pub(crate) use fields::Slots;

const DEBOUNCE: Duration = Duration::from_millis(120);
const PERSIST_DELAY: Duration = Duration::from_millis(300);
pub(crate) const CAREER_EDITOR: &str = "swofty.career-editor";
const CAREER_EDITOR_URL: &str = "https://f1.swofty.net/using/career-editor";

pub(crate) enum Load {
    Idle,
    Loading,
    Ready(Arc<Catalog>),
    Failed(String),
}

#[derive(Clone, PartialEq, Eq)]
pub(crate) enum Nav {
    Module(String),
    Raw(NodeId),
    Unnamed,
    Changes,
}

pub(crate) struct Reveal {
    module: String,
    scope: Option<String>,
    label: String,
}

pub(crate) struct ViewSlot {
    pub module: String,
    pub scope: Option<String>,
    pub view: Arc<ModuleView>,
    pub choices: Arc<HashMap<usize, Vec<(String, String)>>>,
    pub shown: Vec<usize>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Which {
    Search,
    Filter,
    Unnamed,
    Raw,
}

pub(crate) struct Debounced {
    pub input: Entity<InputState>,
    pub query: String,
    generation: u64,
    _sub: Subscription,
}

type GameId = (PathBuf, Option<u64>, Option<String>);

fn game_id(install: &crate::game::Install) -> GameId {
    (install.dir.clone(), install.build_id, install.exe_version.clone())
}

pub(crate) struct Db {
    pub load: Load,
    pub game: Option<GameId>,
    pub modules: &'static [Module],
    /// The changes being edited; `draft.edits` is what every view shows.
    pub draft: Draft,
    draft_read: bool,
    file: Arc<Mutex<DraftFile>>,
    persist_gen: u64,
    /// Changes not yet written to the draft file.
    unwritten: bool,
    writing: bool,
    /// Another launcher window wrote a newer draft; nothing is written until the player picks one.
    pub newer: bool,
    others_gen: u64,
    _quit: Subscription,
    index: HashMap<u64, usize>,
    /// Why the draft's addon could not be read; editing stays off until it reads.
    pub addon_error: Option<String>,
    /// The DB Editor addons installed, from their manifests.
    pub addons: Vec<PackageInfo>,
    /// An addon to open once the player answers Save / Discard / Cancel.
    pending_open: Option<String>,
    /// The addon file time last looked at, so a damaged file is read once per change.
    seen: Option<Option<SystemTime>>,
    pub others: Arc<HashMap<u64, Vec<(String, String)>>>,
    pub toasts: Vec<Notification>,
    pub nav: Nav,
    pub scopes: HashMap<String, String>,
    pub view: Option<ViewSlot>,
    pub view_error: Option<String>,
    pub save_error: Option<String>,
    view_want: Option<(String, Option<String>, u64)>,
    stale_choices: bool,
    rev: u64,
    pub row: Option<String>,
    pub max_cols: usize,
    pub reveal: Option<Reveal>,
    pub key: Option<NodeId>,
    pub errors: HashMap<u64, String>,
    pub search: Option<Debounced>,
    pub hits: Vec<Hit>,
    pub hits_open: bool,
    pub hit: usize,
    pub filter: Option<Debounced>,
    pub unnamed: Option<Debounced>,
    pub raw_filter: Option<Debounced>,
    pub raw_rows: Option<(NodeId, String, Arc<Vec<NodeId>>)>,
    clear_raw: bool,
    pub unnamed_rows: Arc<Vec<NodeId>>,
    pub cell: Option<CellEdit>,
    pub slots: Slots,
    pub focus: Option<FocusHandle>,
    pub rows_scroll: UniformListScrollHandle,
    pub rows_motion: crate::scroll::SmoothScroll,
    pub list_scroll: UniformListScrollHandle,
    pub list_motion: crate::scroll::SmoothScroll,
    pub grid_scroll: ScrollHandle,
    pending_tab: Option<(String, String, String)>,
    pending_link: Option<LinkSpec>,
    pub importing: bool,
}

pub(crate) struct CellEdit {
    pub key: u64,
    pub input: Entity<InputState>,
    _sub: Subscription,
}

impl Db {
    pub(crate) fn new(cx: &mut Context<Shell>) -> Self {
        Self {
            load: Load::Idle,
            game: None,
            modules: &[],
            draft: Draft::default(),
            draft_read: false,
            file: Arc::default(),
            persist_gen: 0,
            unwritten: false,
            writing: false,
            newer: false,
            others_gen: 0,
            _quit: cx.on_app_quit(|s: &mut Shell, _| {
                s.db_flush_now();
                async {}
            }),
            index: HashMap::new(),
            addon_error: None,
            addons: Vec::new(),
            pending_open: None,
            seen: None,
            others: Arc::new(HashMap::new()),
            toasts: Vec::new(),
            nav: Nav::Raw(0),
            scopes: HashMap::new(),
            view: None,
            view_error: None,
            save_error: None,
            view_want: None,
            stale_choices: false,
            rev: 0,
            row: None,
            max_cols: 8,
            reveal: None,
            key: None,
            errors: HashMap::new(),
            search: None,
            hits: Vec::new(),
            hits_open: false,
            hit: 0,
            filter: None,
            unnamed: None,
            raw_filter: None,
            raw_rows: None,
            clear_raw: false,
            unnamed_rows: Arc::new(Vec::new()),
            cell: None,
            slots: Slots::default(),
            focus: None,
            rows_scroll: UniformListScrollHandle::new(),
            rows_motion: Default::default(),
            list_scroll: UniformListScrollHandle::new(),
            list_motion: Default::default(),
            grid_scroll: ScrollHandle::new(),
            pending_tab: None,
            pending_link: None,
            importing: false,
        }
    }

    pub(crate) fn catalog(&self) -> Option<&Arc<Catalog>> {
        match &self.load {
            Load::Ready(c) => Some(c),
            _ => None,
        }
    }

    pub(crate) fn edit(&self, key: u64) -> Option<&Edit> {
        self.index.get(&key).and_then(|&i| self.draft.edits.edits.get(i))
    }

    pub(crate) fn current(&self, key: u64) -> String {
        match self.edit(key) {
            Some(e) => e.value.clone().unwrap_or_default(),
            None => self
                .catalog()
                .and_then(|c| self.draft.edits.value(c, key))
                .map(str::to_owned)
                .unwrap_or_default(),
        }
    }

    fn reindex(&mut self) {
        self.index = self
            .draft
            .edits
            .edits
            .iter()
            .enumerate()
            .map(|(i, e)| (e.key, i))
            .collect();
    }

    pub(crate) fn module(&self, id: &str) -> Option<&'static Module> {
        self.modules.iter().find(|m| m.id == id)
    }
}

pub(crate) fn kind_of(spec: Option<Kind>, tag: u8) -> Kind {
    spec.unwrap_or(match tag {
        0 => Kind::Int,
        1 => Kind::Float,
        5 => Kind::Bool,
        6 => Kind::Time,
        9 => Kind::Curve,
        11 => Kind::Color,
        13 => Kind::List,
        14 => Kind::Vector,
        _ => Kind::Text,
    })
}

pub(crate) fn type_name(tag: u8) -> String {
    match tag {
        0 => "int".into(),
        1 => "float".into(),
        2 => "string".into(),
        3 => "enum".into(),
        4 => "id".into(),
        5 => "bool".into(),
        6 => "time".into(),
        9 => "curve".into(),
        11 => "colour".into(),
        13 => "list".into(),
        14 => "vector".into(),
        t => format!("t{t}"),
    }
}

pub(crate) const ADDABLE: [(u8, &str); 9] = [
    (0, "int"),
    (1, "float"),
    (2, "string"),
    (3, "enum"),
    (4, "id"),
    (5, "bool"),
    (6, "time"),
    (13, "list"),
    (14, "vector"),
];

pub(crate) fn icon_named(name: &str) -> IconName {
    static ICONS: LazyLock<HashMap<String, IconName>> = LazyLock::new(|| {
        IconName::ALL
            .iter()
            .map(|i| {
                let path = i.path().to_string();
                let stem = path
                    .trim_start_matches("icons/")
                    .trim_end_matches(".svg")
                    .to_owned();
                (stem, *i)
            })
            .collect()
    });
    ICONS.get(name).copied().unwrap_or(IconName::Table2)
}

/// Rows of these modules reorder by swapping one field with the neighbour row.
pub(crate) const SWAPS: [(&str, &str); 3] = [
    ("calendar", "event_name"),
    ("garage", "team"),
    ("lineups", "entrant_reference"),
];

pub(crate) fn swap_field(module: &Module) -> Option<usize> {
    let (_, field) = SWAPS.iter().find(|(m, _)| *m == module.id)?;
    module.fields.iter().position(|f| f.id == *field)
}

/// English text without the game's `{o:upper}…{/o}` markup.
pub(crate) fn clean(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut depth = 0;
    for c in text.chars() {
        match c {
            '{' => depth += 1,
            '}' if depth > 0 => depth -= 1,
            _ if depth == 0 => out.push(c),
            _ => {}
        }
    }
    out
}

pub(crate) fn bool_text(tag: u8, stock: &str, on: bool) -> String {
    let (yes, no) = match (tag, stock) {
        (0, _) | (_, "1" | "0") => ("1", "0"),
        (_, "true" | "false") => ("true", "false"),
        _ => ("True", "False"),
    };
    if on { yes } else { no }.into()
}

pub(crate) fn short(v: &str) -> String {
    match v.chars().count() {
        0 => "empty".into(),
        n if n > 18 => format!("{}…", v.chars().take(17).collect::<String>()),
        _ => v.to_owned(),
    }
}

pub(crate) fn hex(key: u64) -> String {
    format!("0x{key:016x}")
}

type Loaded = Result<(Arc<Catalog>, &'static [Module]), String>;

pub(crate) fn guarded<T>(f: impl FnOnce() -> Result<T, String>) -> Result<T, String> {
    std::panic::catch_unwind(AssertUnwindSafe(f))
        .unwrap_or_else(|_| Err("The database editor is not available in this build.".into()))
}

fn lock(file: &Mutex<DraftFile>) -> std::sync::MutexGuard<'_, DraftFile> {
    file.lock().unwrap_or_else(std::sync::PoisonError::into_inner)
}

pub(crate) fn text<E: std::fmt::Display>(e: E) -> String {
    e.to_string()
}

pub(crate) fn home(store: &Store) -> PathBuf {
    std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .map_or_else(|| store.root.clone(), PathBuf::from)
}

/// Debug builds only: a file (`SWOFTY_DEBUG_IMPORT`) or folder (`SWOFTY_DEBUG_EXPORT`)
/// used instead of the file dialogs, which need a desktop portal.
pub(crate) fn debug_path(var: &str) -> Option<PathBuf> {
    if cfg!(debug_assertions) {
        std::env::var_os(var).map(PathBuf::from)
    } else {
        None
    }
}

fn report_text(r: &ImportReport) -> String {
    let format = match r.format {
        "ego" => "EGO GameDatabases Editor export",
        "package" | "zip" => "DB Editor addon",
        "bin" => "Modded game database",
        _ => "DB Editor export",
    };
    if r.format == "bin" {
        let mut lines = vec![format.to_owned(), format!("{} imported", r.imported)];
        lines.extend(r.skipped.iter().cloned());
        return lines.join("\n");
    }
    let mut lines = vec![
        format.to_owned(),
        format!("{} imported · {} unchanged", r.imported, r.unchanged),
    ];
    if !r.skipped.is_empty() {
        lines.push(format!("{} skipped:", r.skipped.len()));
        lines.extend(r.skipped.iter().take(4).cloned());
        if r.skipped.len() > 4 {
            lines.push(format!("and {} more", r.skipped.len() - 4));
        }
    }
    lines.join("\n")
}

/// What runs once the player answers Save / Discard / Cancel about unsaved changes.
#[derive(Clone)]
pub(crate) enum Then {
    Open(String),
    New,
    Load {
        file: String,
        name: String,
        edits: Edits,
        report: ImportReport,
    },
}

impl Shell {
    pub(crate) fn db_count(&self) -> usize {
        self.db.draft.unsaved()
    }

    pub(crate) fn db_profile_changed(&mut self, cx: &mut Context<Self>) {
        self.db_refresh(cx);
    }

    pub(crate) fn db_addon(&self, id: &str) -> Option<&PackageInfo> {
        self.db.addons.iter().find(|p| p.id == id)
    }

    /// The installed addon the draft edits, if any.
    pub(crate) fn db_bound(&self) -> Option<&PackageInfo> {
        self.db.draft.addon.as_deref().and_then(|id| self.db_addon(id))
    }

    /// The draft's addon name, or "the new addon".
    pub(crate) fn db_draft_name(&self) -> String {
        match (&self.db.draft.addon, self.db_bound()) {
            (_, Some(info)) => info.name.clone(),
            (Some(id), None) => id.clone(),
            (None, None) => "the new addon".into(),
        }
    }

    pub(crate) fn db_locked(&self) -> bool {
        self.db.addon_error.is_some()
    }

    pub(crate) fn db_enabled(&self, id: &str) -> bool {
        self.library.profile.enabled().any(|e| e == id)
    }

    /// The DB Editor addons on in the active profile, in load order.
    pub(crate) fn db_stack(&self) -> Vec<String> {
        self.library
            .profile
            .enabled()
            .filter(|id| self.db_addon(id).is_some())
            .map(str::to_owned)
            .collect()
    }

    pub(crate) fn db_refresh(&mut self, cx: &mut Context<Self>) {
        let mut addons: Vec<PackageInfo> = self
            .library
            .addons
            .iter()
            .filter(|a| a.package)
            .map(|a| {
                let m = &a.manifest;
                PackageInfo {
                    id: m.id.clone(),
                    name: m.name.clone(),
                    version: m.version.to_string(),
                    author: m.author.clone(),
                    description: m.description.clone().unwrap_or_default(),
                    changes: 0,
                    game_build: String::new(),
                }
            })
            .collect();
        addons.sort_by_key(|p| p.name.to_lowercase());
        self.db.addons = addons;
        if !self.db.draft_read {
            self.db_read_draft();
        }
        self.db_sync_addon(false, cx);
        self.db_load_others(cx);
    }

    // Reads every enabled DB Editor addon off the UI thread for "Also changed by".
    fn db_load_others(&mut self, cx: &mut Context<Self>) {
        self.db.others_gen += 1;
        let at = self.db.others_gen;
        let (store, profile) = (self.store.clone(), self.library.profile.name.clone());
        cx.spawn(async move |this, cx| {
            let others = cx
                .background_executor()
                .spawn(async move {
                    guarded(|| package::enabled_changes(&store, &profile).map_err(text)).unwrap_or_default()
                })
                .await;
            this.update(cx, |s, cx| {
                if s.db.others_gen == at {
                    s.db.others = Arc::new(others);
                    cx.notify();
                }
            })
            .ok();
        })
        .detach();
    }

    // Reads the autosaved draft once; a file that does not read is set aside.
    fn db_read_draft(&mut self) {
        self.db.draft_read = true;
        let store = self.store.clone();
        let opened = guarded(|| Ok(lock(&self.db.file).open(&store)));
        let body = match opened {
            Ok(Opened::Read(d)) => {
                self.db.draft = d;
                None
            }
            Ok(Opened::SetAside { error, kept }) => {
                Some(format!("{error}\nIt was kept as {}", kept.display()))
            }
            Ok(Opened::Stuck { error }) => {
                self.db.save_error = Some(format!(
                    "Your changes are not saved to the draft until the damaged draft file is moved: {error}"
                ));
                Some(error)
            }
            Err(e) => Some(e),
        };
        if let Some(body) = body {
            self.db.toasts.push(
                Notification::error(body)
                    .title("The DB Editor draft could not be read")
                    .autohide(false),
            );
        }
        self.db.reindex();
    }

    /// Writes the draft to disk off the UI thread once changes stop for a moment.
    pub(crate) fn db_persist(&mut self, cx: &mut Context<Self>) {
        self.db.unwritten = true;
        self.db.persist_gen += 1;
        let at = self.db.persist_gen;
        cx.spawn(async move |this, cx| {
            cx.background_executor().timer(PERSIST_DELAY).await;
            this.update(cx, |s, cx| {
                if s.db.persist_gen == at {
                    s.db_flush(cx);
                }
            })
            .ok();
        })
        .detach();
    }

    /// Starts writing the draft's unwritten changes now.
    pub(crate) fn db_flush(&mut self, cx: &mut Context<Self>) {
        if !self.db.unwritten || self.db.writing || self.db.newer {
            return;
        }
        self.db.unwritten = false;
        self.db.writing = true;
        let (store, draft, file) = (self.store.clone(), self.db.draft.clone(), self.db.file.clone());
        cx.spawn(async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move { guarded(|| lock(&file).write(&store, &draft).map_err(text)) })
                .await;
            this.update(cx, |s, cx| {
                s.db.writing = false;
                s.db_written(result, cx);
            })
            .ok();
        })
        .detach();
    }

    fn db_written(&mut self, result: Result<Written, String>, cx: &mut Context<Self>) {
        match result {
            Ok(Written::Saved) => self.db.save_error = None,
            Ok(Written::Newer) => {
                self.db.unwritten = true;
                self.db.newer = true;
            }
            Err(e) => {
                self.db.unwritten = true;
                self.db.save_error = Some(format!("Your changes could not be saved to the draft: {e}"));
                cx.notify();
                return;
            }
        }
        if self.db.unwritten && !self.db.newer {
            self.db_persist(cx);
        }
        cx.notify();
    }

    /// Writes pending draft changes when the player leaves the DB Editor.
    pub(crate) fn db_page_change(&mut self, to: Page, cx: &mut Context<Self>) {
        if self.page == Page::Database && to != Page::Database {
            self.db_flush(cx);
        }
        // Other pages change what choices offer, such as Driver Manager's drivers.
        if self.page != Page::Database && to == Page::Database {
            self.db.stale_choices = true;
            self.db.view_want = None;
            self.db_refresh_view(cx);
        }
    }

    /// Writes the draft before the launcher quits.
    pub(crate) fn db_flush_now(&mut self) {
        if !self.db.unwritten || self.db.newer {
            return;
        }
        let (store, draft) = (self.store.clone(), &self.db.draft);
        if let Ok(Written::Saved) = guarded(|| lock(&self.db.file).write(&store, draft).map_err(text)) {
            self.db.unwritten = false;
        }
    }

    /// Replaces this window's draft with the newer one another window wrote.
    pub(crate) fn db_take_newer(&mut self, cx: &mut Context<Self>) {
        self.db.newer = false;
        self.db.unwritten = false;
        self.db.persist_gen += 1;
        self.db.draft_read = false;
        self.db_read_draft();
        self.db.seen = None;
        self.db.errors.clear();
        self.db.cell = None;
        self.db.slots = Slots::default();
        self.db_sync_addon(true, cx);
        self.db_changed(None, false, cx);
    }

    /// Keeps this window's draft, writing it over the one another window wrote.
    pub(crate) fn db_keep_mine(&mut self, cx: &mut Context<Self>) {
        let store = self.store.clone();
        guarded(|| {
            lock(&self.db.file).adopt(&store);
            Ok(())
        })
        .ok();
        self.db.newer = false;
        self.db_persist(cx);
        cx.notify();
    }

    // Another window wrote the draft while this one had nothing to write: take it.
    fn db_follow_draft(&mut self, cx: &mut Context<Self>) {
        if self.db.unwritten || self.db.writing || self.db.newer {
            return;
        }
        let store = self.store.clone();
        if guarded(|| Ok(lock(&self.db.file).is_newer(&store))).unwrap_or(false) {
            self.db_take_newer(cx);
            self.db.toasts.push(Notification::info(
                "Another launcher window changed the draft, so it was reloaded.",
            ));
        }
    }

    // Follows the draft's addon on disk: removed, damaged, or saved by another writer.
    fn db_sync_addon(&mut self, quiet: bool, cx: &mut Context<Self>) {
        let Some(id) = self.db.draft.addon.clone() else {
            if self.db.addon_error.take().is_some() {
                self.db_changed(None, false, cx);
            }
            return;
        };
        let store = self.store.clone();
        if !guarded(|| Ok(package::is_package(&store, &id))).unwrap_or(false) {
            let name = self.db_draft_name();
            if self.db.draft.is_dirty() {
                let edits = self.db.draft.edits.clone();
                self.db.draft.clear();
                self.db.draft.edits = edits;
                self.db.draft.name = Some(name.clone());
                self.db.toasts.push(Notification::warning(format!(
                    "{name} was removed. Your unsaved changes stay in the DB Editor as a new addon."
                )));
            } else {
                self.db.draft.clear();
            }
            self.db.addon_error = None;
            self.db.seen = None;
            self.db_persist(cx);
            self.db_changed(None, false, cx);
            return;
        }
        let stamp = guarded(|| Ok(package::modified(&store, &id))).ok().flatten();
        if self.db.seen == Some(stamp) {
            return;
        }
        self.db.seen = Some(stamp);
        if self.db.addon_error.is_none() && stamp == self.db.draft.stamp {
            return;
        }
        match guarded(|| package::Package::open(&store, &id).map_err(text)) {
            Ok(pkg) => {
                let reload = self.db.addon_error.is_none() && self.db.draft.stamp.is_some();
                let dirty = self.db.draft.is_dirty();
                self.db.draft.rebase(pkg.edits, stamp);
                self.db.addon_error = None;
                self.db_persist(cx);
                if reload && !quiet {
                    let name = self.db_draft_name();
                    self.db.toasts.push(Notification::info(if dirty {
                        format!(
                            "{name} changed on disk and was reloaded. Your unsaved changes are kept on top."
                        )
                    } else {
                        format!("{name} changed on disk and was reloaded.")
                    }));
                }
            }
            Err(e) => self.db.addon_error = Some(e),
        }
        self.db.errors.clear();
        self.db.cell = None;
        self.db.slots = Slots::default();
        self.db_changed(None, false, cx);
    }

    /// Re-reads the draft's addon after this window changed its files, without a notice.
    pub(crate) fn db_quiet_reload(&mut self, cx: &mut Context<Self>) {
        self.db.seen = None;
        self.db_sync_addon(true, cx);
    }

    /// Try again on a draft whose addon did not read.
    pub(crate) fn db_retry(&mut self, cx: &mut Context<Self>) {
        self.db.seen = None;
        self.db_sync_addon(false, cx);
        cx.notify();
    }

    /// The poll's check: another writer (the CLI, a second window) changed the draft or its addon.
    pub(crate) fn db_poll(&mut self, cx: &mut Context<Self>) {
        if self.page != crate::shell::Page::Database {
            return;
        }
        self.db_follow_draft(cx);
        if self.db.draft.addon.is_some() {
            self.db_sync_addon(false, cx);
        }
    }

    fn db_ready_to_edit(&mut self, cx: &mut Context<Self>) -> bool {
        self.db_sync_addon(false, cx);
        self.db.catalog().is_some() && self.db.addon_error.is_none()
    }

    pub(crate) fn db_turn_on(&mut self, id: &str) -> Result<(), String> {
        let registry = self.library.registry.clone();
        self.store
            .update_profile(&self.library.profile.name, |p| registry.set_enabled(p, id, true))
            .map_err(text)
    }

    /// Opens the DB Editor addon `id` as the draft. Unsaved changes in the
    /// draft ask Save / Discard / Cancel first.
    pub(crate) fn db_edit_addon(&mut self, id: &str, cx: &mut Context<Self>) {
        self.open_page(Page::Database, cx);
        if self.db.draft.addon.as_deref() == Some(id) {
            self.db_retry(cx);
        } else if self.db.draft.is_dirty() {
            self.db.pending_open = Some(id.to_owned());
        } else {
            self.db_then(Then::Open(id.to_owned()), cx);
        }
        cx.notify();
    }

    pub(crate) fn db_then(&mut self, then: Then, cx: &mut Context<Self>) {
        let store = self.store.clone();
        let mut toast = None;
        match then {
            Then::Open(id) => {
                let draft = &mut self.db.draft;
                self.db.addon_error = guarded(|| draft.open(&store, &id).map_err(text)).err();
                if self.db.addon_error.is_some() {
                    self.db.draft.clear();
                    self.db.draft.addon = Some(id);
                }
            }
            Then::New => self.db.draft.clear(),
            Then::Load {
                file,
                name,
                edits,
                report,
            } => {
                self.db.draft.clear();
                self.db.draft.edits = edits;
                self.db.draft.name = Some(name);
                self.db.addon_error = None;
                let note = if report.format == "bin" && report.skipped.len() > 1 {
                    Notification::warning(report_text(&report)).autohide(false)
                } else {
                    Notification::success(report_text(&report))
                };
                toast = Some(note.title(format!(
                    "Opened {file} as a new addon. Review it, then Save as Addon."
                )));
                self.db_go(Nav::Changes, cx);
            }
        }
        self.db.seen = Some(self.db.draft.stamp);
        self.db.errors.clear();
        self.db.cell = None;
        self.db.slots = Slots::default();
        self.db_persist(cx);
        self.db.toasts.extend(toast);
        self.db_load_others(cx);
        self.db_changed(None, false, cx);
    }

    /// "Also changed by …" for a key another enabled DB Editor addon changes.
    pub(crate) fn db_others(&self, key: u64) -> Option<String> {
        let editing = self.db.draft.addon.as_deref();
        let others: Vec<&(String, String)> = self
            .db
            .others
            .get(&key)?
            .iter()
            .filter(|(id, _)| Some(id.as_str()) != editing)
            .collect();
        let last = others.last()?;
        let name = |id: &str| {
            self.db_addon(id)
                .map_or_else(|| id.to_owned(), |p| p.name.clone())
        };
        let list: Vec<String> = others
            .iter()
            .map(|(id, v)| format!("{} ({})", name(id), short(v)))
            .collect();
        let order: Vec<&str> = self.library.profile.enabled().collect();
        let at = |id: &str| order.iter().position(|o| *o == id);
        let mine = self.db.edit(key).is_some();
        let later = match editing {
            Some(id) => at(id).is_some_and(|i| at(&last.0).is_none_or(|j| i > j)),
            None => true,
        };
        let wins = if mine && later {
            Some("your change wins".to_owned())
        } else if mine || others.len() > 1 {
            Some(format!("{} wins", name(&last.0)))
        } else {
            None
        };
        let mut text = format!("Also changed by {}", list.join(", "));
        if let Some(w) = wins {
            text.push_str(&format!(" · {w}"));
        }
        Some(text)
    }

    pub(crate) fn db_shared(&self, key: u64) -> bool {
        let editing = self.db.draft.addon.as_deref();
        self.db
            .others
            .get(&key)
            .is_some_and(|l| l.iter().any(|(id, _)| Some(id.as_str()) != editing))
    }

    // A new game folder or build: drop the catalog and everything that points into it.
    fn db_forget_game(&mut self) {
        let db = &mut self.db;
        db.load = Load::Idle;
        db.game = None;
        db.view = None;
        db.view_want = None;
        db.rev += 1;
        db.row = None;
        db.key = None;
        db.reveal = None;
        db.cell = None;
        db.hits.clear();
        db.raw_rows = None;
        db.unnamed_rows = Arc::new(Vec::new());
        db.scopes.clear();
        db.slots = Slots::default();
        if matches!(db.nav, Nav::Raw(_)) {
            db.nav = Nav::Raw(0);
        }
    }

    pub(crate) fn sync_db(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let id = self.install.as_ref().map(game_id);
        if self.db.game.is_some() && self.db.game != id {
            self.db_forget_game();
        }
        if matches!(self.db.load, Load::Idle) && id.is_some() {
            self.db_open(cx);
        }
        if self.db.focus.is_none() {
            let focus = cx.focus_handle();
            focus.focus(window, cx);
            self.db.focus = Some(focus);
        }
        if self.db.search.is_none() {
            let search = self.debounced(Which::Search, window, cx);
            self.db.search = Some(search);
        }
        if self.db.filter.is_none() {
            let filter = self.debounced(Which::Filter, window, cx);
            self.db.filter = Some(filter);
        }
        if self.db.unnamed.is_none() {
            let unnamed = self.debounced(Which::Unnamed, window, cx);
            self.db.unnamed = Some(unnamed);
        }
        if self.db.raw_filter.is_none() {
            let raw = self.debounced(Which::Raw, window, cx);
            self.db.raw_filter = Some(raw);
        }
        if std::mem::take(&mut self.db.clear_raw)
            && let Some(d) = self.db.raw_filter.as_mut()
        {
            d.query.clear();
            d.input.update(cx, |s, cx| s.set_value("", window, cx));
        }
        if let Some(id) = self.db.pending_open.take() {
            self.db_leave(Then::Open(id), window, cx);
        }
        self.db_apply_reveal(cx);
        self.sync_fields(window, cx);
    }

    // A search jump selects its row once the view shows the hit's module and scope.
    fn db_apply_reveal(&mut self, cx: &mut Context<Self>) {
        let (Some(r), Some(slot)) = (self.db.reveal.as_ref(), self.db.view.as_ref()) else {
            return;
        };
        let here = Nav::Module(slot.module.clone()) == self.db.nav
            && r.module == slot.module
            && r.scope.as_ref().is_none_or(|s| slot.scope.as_ref() == Some(s));
        if !here {
            return;
        }
        let Some(r) = self.db.reveal.take() else {
            return;
        };
        if let Some(i) = slot
            .shown
            .iter()
            .position(|&i| slot.view.rows[i].label == r.label)
        {
            self.db.rows_motion.cancel();
            self.db.rows_scroll.scroll_to_item(i, ScrollStrategy::Center);
        }
        if slot.view.rows.iter().any(|row| row.label == r.label) {
            self.db.row = Some(r.label);
        }
        cx.notify();
    }

    fn slot(&mut self, which: Which) -> Option<&mut Debounced> {
        match which {
            Which::Search => self.db.search.as_mut(),
            Which::Filter => self.db.filter.as_mut(),
            Which::Unnamed => self.db.unnamed.as_mut(),
            Which::Raw => self.db.raw_filter.as_mut(),
        }
    }

    fn debounced(&mut self, which: Which, window: &mut Window, cx: &mut Context<Self>) -> Debounced {
        let placeholder = match which {
            Which::Search => "Search drivers, teams, tracks, keys and values",
            Which::Filter => "Filter rows",
            Which::Unnamed => "Filter by key or value",
            Which::Raw => "Filter this level",
        };
        let input = cx.new(|cx| InputState::new(window, cx).placeholder(placeholder));
        let sub = cx.subscribe_in(&input, window, move |this, state, ev: &InputEvent, window, cx| {
            match ev {
                InputEvent::Change => {}
                InputEvent::PressEnter { .. } if which == Which::Search => {
                    this.db_open_hit(this.db.hit, window, cx);
                    return;
                }
                InputEvent::Focus if which == Which::Search => {
                    this.db.hits_open = true;
                    cx.notify();
                    return;
                }
                _ => return,
            }
            let query = state.read(cx).value().to_string();
            let Some(slot) = this.slot(which) else {
                return;
            };
            slot.generation += 1;
            let generation = slot.generation;
            cx.spawn(async move |this, cx| {
                cx.background_executor().timer(DEBOUNCE).await;
                this.update(cx, |s, cx| {
                    if let Some(slot) = s.slot(which).filter(|d| d.generation == generation) {
                        slot.query = query;
                        match which {
                            Which::Search => s.db_search(cx),
                            Which::Filter => s.db_filter(cx),
                            Which::Unnamed => s.db_unnamed(cx),
                            Which::Raw => cx.notify(),
                        }
                    }
                })
                .ok();
            })
            .detach();
        });
        Debounced {
            input,
            query: String::new(),
            generation: 0,
            _sub: sub,
        }
    }

    fn db_open(&mut self, cx: &mut Context<Self>) {
        let Some(install) = self.install.clone() else {
            return;
        };
        let id = game_id(&install);
        self.db.load = Load::Loading;
        self.db.game = Some(id.clone());
        let store = self.store.clone();
        cx.spawn(async move |this, cx| {
            let loaded: Loaded = cx
                .background_executor()
                .spawn(async move {
                    let catalog = guarded(|| {
                        let game = crate::sections::game(&store, Some(&install))
                            .ok_or("The game folder could not be read.")?;
                        Catalog::open(&store, &game)
                            .map_err(|e| format!("The game database could not be read: {e}"))
                    })?;
                    guarded(|| Ok(catalog.search("x", 1))).ok();
                    let modules = guarded(|| Ok(swofty_core::dbedit::modules())).unwrap_or(&[]);
                    Ok((catalog, modules))
                })
                .await;
            this.update(cx, |s, cx| {
                if s.db.game.as_ref() == Some(&id) {
                    s.db_loaded(loaded, cx);
                }
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    fn db_loaded(&mut self, loaded: Loaded, cx: &mut Context<Self>) {
        match loaded {
            Ok((catalog, modules)) => {
                self.db.modules = modules;
                if let Nav::Raw(0) = self.db.nav {
                    self.db.nav = modules
                        .first()
                        .map_or(Nav::Raw(catalog.root()), |m| Nav::Module(m.id.clone()));
                }
                self.db.load = Load::Ready(catalog);
                self.db_changed(None, false, cx);
                self.db_unnamed(cx);
            }
            Err(e) => self.db.load = Load::Failed(e),
        }
        cx.notify();
    }

    pub(crate) fn db_go(&mut self, nav: Nav, cx: &mut Context<Self>) {
        if self.db.nav != nav {
            self.db.rows_motion.cancel();
            self.db.list_motion.cancel();
            self.db.nav = nav;
            self.db.row = None;
            self.db.reveal = None;
            self.db.key = None;
            self.db.cell = None;
            self.db.rows_scroll = UniformListScrollHandle::new();
            self.db.list_scroll = UniformListScrollHandle::new();
            self.db.clear_raw = self.db.raw_filter.as_ref().is_some_and(|d| !d.query.is_empty());
        }
        self.db.hits_open = false;
        self.db_refresh_view(cx);
        cx.notify();
    }

    // After any edit: persist, patch the cached module view, then rebuild it off the UI thread.
    pub(crate) fn db_changed(&mut self, key: Option<u64>, save: bool, cx: &mut Context<Self>) {
        self.db.rev += 1;
        self.db.reindex();
        if save {
            self.db_persist(cx);
        }
        let edit = key.and_then(|k| self.db.edit(k).cloned());
        if let (Some(key), Some(catalog), Some(slot)) =
            (key, self.db.catalog().cloned(), self.db.view.as_mut())
        {
            let view = Arc::make_mut(&mut slot.view);
            for row in &mut view.rows {
                for cell in row.cells.iter_mut().filter(|c| c.key == Some(key)) {
                    cell.edited = edit.is_some();
                    cell.value = match &edit {
                        Some(e) => e.value.clone().unwrap_or_default(),
                        None => catalog
                            .record_by_key(key)
                            .map_or(cell.stock.clone(), |r| r.value.to_owned()),
                    };
                }
            }
        }
        self.db_refresh_view(cx);
        cx.notify();
    }

    pub(crate) fn db_refresh_view(&mut self, cx: &mut Context<Self>) {
        let Nav::Module(id) = self.db.nav.clone() else {
            return;
        };
        let (Some(catalog), Some(module)) = (self.db.catalog().cloned(), self.db.module(&id)) else {
            return;
        };
        let scope = self.db.scopes.get(&id).cloned();
        let want = (id.clone(), scope.clone(), self.db.rev);
        if self.db.view_want.as_ref() == Some(&want) {
            return;
        }
        self.db.view_want = Some(want.clone());
        let stale = std::mem::take(&mut self.db.stale_choices);
        let choices = self
            .db
            .view
            .as_ref()
            .filter(|v| !stale && v.module == id && v.scope == scope)
            .map(|v| v.choices.clone());
        let edits = self.db.draft.edits.clone();
        let store = self.store.clone();
        cx.spawn(async move |this, cx| {
            let (view, choices) = cx
                .background_executor()
                .spawn(async move {
                    let view = guarded(|| {
                        module
                            .view(&catalog, &edits, scope.as_deref())
                            .map_err(|e| e.to_string())
                    });
                    let choices = choices.unwrap_or_else(|| {
                        let tracks = swofty_core::tracks::installed(&store);
                        let drivers = driver_manager::drivers(&store, &catalog);
                        let heads = swofty_core::heads::installed(&store);
                        let refs = module
                            .fields
                            .iter()
                            .enumerate()
                            .filter(|(_, f)| matches!(f.kind, Some(Kind::Ref | Kind::RefList)))
                            .map(|(i, _)| {
                                let c = module.choices_with(
                                    &catalog,
                                    i,
                                    scope.as_deref(),
                                    &tracks,
                                    &drivers,
                                    &heads,
                                );
                                (i, c)
                            })
                            .collect();
                        Arc::new(refs)
                    });
                    (view, choices)
                })
                .await;
            this.update(cx, |s, cx| {
                if s.db.view_want.as_ref() != Some(&want) {
                    return;
                }
                match view {
                    Ok(view) => {
                        if let Some(scope) = view.scope.clone() {
                            s.db.scopes.entry(want.0.clone()).or_insert(scope);
                        }
                        if s.db
                            .view
                            .as_ref()
                            .is_some_and(|v| v.module == want.0 && v.scope != view.scope)
                        {
                            s.db.row = None;
                            s.db.cell = None;
                        }
                        s.db.view_error = None;
                        s.db.view = Some(ViewSlot {
                            module: want.0,
                            scope: view.scope.clone(),
                            view: Arc::new(view),
                            choices,
                            shown: Vec::new(),
                        });
                        s.db_filter(cx);
                    }
                    Err(e) => {
                        s.db.view = None;
                        s.db.view_error = Some(e);
                    }
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    pub(crate) fn db_set_scope(&mut self, scope: String, cx: &mut Context<Self>) {
        if let Nav::Module(id) = &self.db.nav {
            self.db.scopes.insert(id.clone(), scope);
            self.db.row = None;
            self.db.cell = None;
            self.db.reveal = None;
            self.db_refresh_view(cx);
        }
    }

    fn db_filter(&mut self, cx: &mut Context<Self>) {
        let query = self
            .db
            .filter
            .as_ref()
            .map(|f| f.query.trim().to_lowercase())
            .unwrap_or_default();
        if let Some(slot) = self.db.view.as_mut() {
            let view = &slot.view;
            let columns: Vec<usize> = view.columns.iter().map(|c| c.field).collect();
            slot.shown = (0..view.rows.len())
                .filter(|&i| {
                    let r = &view.rows[i];
                    query.is_empty()
                        || r.name.to_lowercase().contains(&query)
                        || r.label.to_lowercase().contains(&query)
                        || columns.iter().any(|&c| {
                            r.cells.get(c).is_some_and(|cell| {
                                cell.value.to_lowercase().contains(&query)
                                    || cell
                                        .text
                                        .as_ref()
                                        .is_some_and(|t| t.to_lowercase().contains(&query))
                            })
                        })
                })
                .collect();
        }
        cx.notify();
    }

    fn db_search(&mut self, cx: &mut Context<Self>) {
        let query = self
            .db
            .search
            .as_ref()
            .map(|s| s.query.trim().to_owned())
            .unwrap_or_default();
        let generation = self.db.search.as_ref().map_or(0, |s| s.generation);
        let Some(catalog) = self.db.catalog().cloned() else {
            return;
        };
        if query.is_empty() {
            self.db.hits.clear();
            cx.notify();
            return;
        }
        let from = catalog.clone();
        let store = self.store.clone();
        cx.spawn(async move |this, cx| {
            let hits = cx
                .background_executor()
                .spawn(async move {
                    guarded(|| {
                        let drivers = driver_manager::drivers(&store, &catalog);
                        Ok(catalog.search_with(&query, 60, &drivers))
                    })
                    .unwrap_or_default()
                })
                .await;
            this.update(cx, |s, cx| {
                if s.db.search.as_ref().is_some_and(|d| d.generation == generation)
                    && s.db.catalog().is_some_and(|c| Arc::ptr_eq(c, &from))
                {
                    let mut hits = hits;
                    hits.sort_by_key(|h| h.kind);
                    s.db.hits = hits;
                    s.db.hit = 0;
                    s.db.hits_open = true;
                    cx.notify();
                }
            })
            .ok();
        })
        .detach();
    }

    fn db_unnamed(&mut self, cx: &mut Context<Self>) {
        let Some(catalog) = self.db.catalog().cloned() else {
            return;
        };
        let query = self
            .db
            .unnamed
            .as_ref()
            .map(|d| d.query.trim().to_lowercase())
            .unwrap_or_default();
        let generation = self.db.unnamed.as_ref().map_or(0, |d| d.generation);
        let from = catalog.clone();
        cx.spawn(async move |this, cx| {
            let rows = cx
                .background_executor()
                .spawn(async move {
                    guarded(|| {
                        let Some(root) = catalog
                            .children(catalog.root())
                            .iter()
                            .copied()
                            .find(|&c| catalog.node(c).kind == NodeKind::Unnamed)
                        else {
                            return Ok(Vec::new());
                        };
                        Ok(catalog
                            .children(root)
                            .iter()
                            .copied()
                            .filter(|&n| {
                                if query.is_empty() {
                                    return true;
                                }
                                let node = catalog.node(n);
                                node.name.to_lowercase().contains(&query)
                                    || node.record.is_some_and(|r| {
                                        catalog.record(r).value.to_lowercase().contains(&query)
                                    })
                            })
                            .collect())
                    })
                    .unwrap_or_default()
                })
                .await;
            this.update(cx, |s, cx| {
                if s.db.unnamed.as_ref().is_none_or(|d| d.generation == generation)
                    && s.db.catalog().is_some_and(|c| Arc::ptr_eq(c, &from))
                {
                    s.db.unnamed_rows = Arc::new(rows);
                    cx.notify();
                }
            })
            .ok();
        })
        .detach();
    }

    pub(crate) fn db_focus_search(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if let Some(s) = &self.db.search {
            s.input.update(cx, |s, cx| {
                s.focus(window, cx);
                s.select_all(window, cx);
            });
            self.db.hits_open = true;
            cx.notify();
        }
    }

    pub(crate) fn db_open_hit(&mut self, index: usize, window: &mut Window, cx: &mut Context<Self>) {
        let Some(hit) = self.db.hits.get(index).cloned() else {
            return;
        };
        self.db.hits_open = false;
        if let Some(f) = &self.db.focus {
            f.focus(window, cx);
        }
        match hit.target {
            Target::ModuleRow { module, scope, label } => {
                if let Some(scope) = scope.clone() {
                    self.db.scopes.insert(module.clone(), scope);
                }
                self.db_go(Nav::Module(module.clone()), cx);
                self.db.row = None;
                self.db.cell = None;
                self.db.reveal = Some(Reveal { module, scope, label });
                self.db_refresh_view(cx);
            }
            Target::Node(node) => self.db_show_node(node, cx),
            Target::Link(link) => match self.db_link_state(&link) {
                LinkState::Open => self.db_open_section(&link, cx),
                _ => {
                    if let Some(m) = self.db.modules.iter().find(|m| m.links.contains(&link)) {
                        self.db_go(Nav::Module(m.id.clone()), cx);
                    }
                }
            },
        }
        cx.notify();
    }

    pub(crate) fn db_show_node(&mut self, node: NodeId, cx: &mut Context<Self>) {
        let Some(catalog) = self.db.catalog().cloned() else {
            return;
        };
        let n = catalog.node(node);
        let parent = n.parent.unwrap_or(catalog.root());
        match n.kind {
            NodeKind::Key if catalog.node(parent).kind == NodeKind::Unnamed => {
                self.db_go(Nav::Unnamed, cx);
                self.db.key = Some(node);
            }
            NodeKind::Key => {
                self.db_go(Nav::Raw(parent), cx);
                self.db.key = Some(node);
            }
            NodeKind::Unnamed => self.db_go(Nav::Unnamed, cx),
            _ => self.db_go(Nav::Raw(node), cx),
        }
    }

    pub(crate) fn db_set(&mut self, key: u64, value: &str, cx: &mut Context<Self>) -> bool {
        let Some(catalog) = self.db.catalog().cloned() else {
            return false;
        };
        if !self.db_ready_to_edit(cx) {
            return false;
        }
        let result = guarded(|| self.db.draft.edits.set(&catalog, key, value));
        match result {
            Ok(()) => {
                self.db.errors.remove(&key);
                self.db_changed(Some(key), true, cx);
                true
            }
            Err(e) => {
                self.db.errors.insert(key, e);
                cx.notify();
                false
            }
        }
    }

    /// The table and field of row spec `spec` of the open page, and the selected row's label.
    fn db_row_target(&self, spec: usize) -> Option<(String, String, String)> {
        let slot = self.db.view.as_ref()?;
        let module = self.db.module(&slot.module)?;
        let spec = module.row_specs().into_iter().nth(spec)?;
        let table = module.row_table(&spec, slot.scope.as_deref());
        Some((table, spec.field, self.db.row.clone()?))
    }

    /// Gives the selected row its own row of spec `spec`'s table: a copy of `from`'s, or of the game default.
    pub(crate) fn db_copy_row(&mut self, spec: usize, from: Option<String>, cx: &mut Context<Self>) {
        let (Some(catalog), Some((table, field, to))) =
            (self.db.catalog().cloned(), self.db_row_target(spec))
        else {
            return;
        };
        if !self.db_ready_to_edit(cx) {
            return;
        }
        let edits = &mut self.db.draft.edits;
        match guarded(|| edits.copy_row(&catalog, &table, &field, from.as_deref(), &to)) {
            Ok(()) => self.db_changed(None, true, cx),
            Err(e) => {
                crate::app_log::action("db editor: copy a row").failed(&e);
                self.db.toasts.push(Notification::error(e));
                cx.notify();
            }
        }
    }

    pub(crate) fn db_remove_row(&mut self, spec: usize, cx: &mut Context<Self>) {
        let Some((table, field, to)) = self.db_row_target(spec) else {
            return;
        };
        self.db_remove_copy(&table, &field, &to, cx);
    }

    pub(crate) fn db_remove_copy(&mut self, table: &str, field: &str, to: &str, cx: &mut Context<Self>) {
        if !self.db_ready_to_edit(cx) {
            return;
        }
        if guarded(|| Ok(self.db.draft.edits.remove_row(table, field, to))).unwrap_or(false) {
            self.db.errors.clear();
            self.db.slots = Slots::default();
            self.db_changed(None, true, cx);
        }
    }

    pub(crate) fn db_move(&mut self, label: &str, down: bool, cx: &mut Context<Self>) {
        let Some(slot) = self.db.view.as_ref() else {
            return;
        };
        let Some(field) = self.db.module(&slot.module).and_then(swap_field) else {
            return;
        };
        let rows = &slot.view.rows;
        let Some(i) = rows.iter().position(|r| r.label == label) else {
            return;
        };
        let Some(j) = (if down { i.checked_add(1) } else { i.checked_sub(1) }).filter(|j| *j < rows.len())
        else {
            return;
        };
        let (a, b) = (&rows[i].cells[field], &rows[j].cells[field]);
        let (Some(ka), Some(kb)) = (a.key, b.key) else {
            return;
        };
        let (va, vb) = (a.value.clone(), b.value.clone());
        let reveal = Reveal {
            module: slot.module.clone(),
            scope: slot.scope.clone(),
            label: rows[j].label.clone(),
        };
        if self.db_set(ka, &vb, cx) && self.db_set(kb, &va, cx) {
            self.db.row = Some(reveal.label.clone());
            self.db.reveal = Some(reveal);
        }
    }

    /// How many values `field` of the open page reads in the rows the page lists.
    pub(crate) fn db_listed(&self, field: usize) -> usize {
        let Some(slot) = self.db.view.as_ref() else {
            return 0;
        };
        self.db
            .module(&slot.module)
            .map_or(0, |m| m.keys_of(&slot.view, &slot.shown, field).len())
    }

    pub(crate) fn db_confirm_set_all(
        &mut self,
        field: usize,
        value: String,
        shown: String,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let Some(slot) = self.db.view.as_ref() else {
            return;
        };
        let Some(f) = self.db.module(&slot.module).and_then(|m| m.fields.get(field)) else {
            return;
        };
        let n = self.db_listed(field);
        let title = format!("Set {} to {shown} on {}?", f.label, plural(n, "row", "rows"));
        let rows = if slot.shown.len() < slot.view.rows.len() {
            "Every row the search lists"
        } else {
            "Every row of this page"
        };
        let description = format!(
            "{rows} gets this value, held inside the field's range. It saves to the addon like any other change."
        );
        let shell = cx.entity().downgrade();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (shell, value) = (shell.clone(), value.clone());
            alert
                .title(title.clone())
                .description(description.clone())
                .ok_text("Set all")
                .show_cancel(true)
                .on_ok(move |_, _, cx| {
                    shell.update(cx, |s, cx| s.db_set_all(field, &value, cx)).ok();
                    true
                })
        });
    }

    /// Sets `field` of the open page to `value` in every listed row, as one change.
    pub(crate) fn db_set_all(&mut self, field: usize, value: &str, cx: &mut Context<Self>) {
        let Some(catalog) = self.db.catalog().cloned() else {
            return;
        };
        if !self.db_ready_to_edit(cx) {
            return;
        }
        let Some(slot) = self.db.view.as_ref() else {
            return;
        };
        let Some(module) = self.db.module(&slot.module) else {
            return;
        };
        let (view, rows) = (slot.view.clone(), slot.shown.clone());
        let label = module
            .fields
            .get(field)
            .map(|f| f.label.clone())
            .unwrap_or_default();
        let edits = &mut self.db.draft.edits;
        match guarded(|| module.set_all(&catalog, edits, &view, &rows, field, value)) {
            Ok(0) => {
                self.db.toasts.push(Notification::info(format!(
                    "Every listed row already has that {label}"
                )));
                cx.notify();
            }
            Ok(n) => {
                self.db.slots = Slots::default();
                self.db.toasts.push(Notification::success(format!(
                    "{label}: changed {}",
                    plural(n, "row", "rows")
                )));
                self.db_changed(None, true, cx);
            }
            Err(e) => {
                crate::app_log::action("db editor: change rows").failed(&e);
                self.db.toasts.push(Notification::error(e));
                cx.notify();
            }
        }
    }

    pub(crate) fn db_revert(&mut self, key: u64, cx: &mut Context<Self>) {
        self.db.errors.remove(&key);
        self.db.slots.forget(key);
        if !self.db_ready_to_edit(cx) {
            return;
        }
        if guarded(|| Ok(self.db.draft.edits.revert(key))).unwrap_or(false) {
            self.db_changed(Some(key), true, cx);
        }
        cx.notify();
    }

    pub(crate) fn db_revert_all(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let n = self.db.draft.edits.len();
        if n == 0 {
            return;
        }
        let shell = cx.entity().downgrade();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let shell = shell.clone();
            alert
                .title(format!("Revert all {n} changes?"))
                .description(
                    "Every value goes back to the game's stock value. Save afterwards to keep that in the addon.",
                )
                .ok_text("Revert all")
                .ok_variant(gpui_kit::component::button::ButtonVariant::Danger)
                .show_cancel(true)
                .on_ok(move |_, _, cx| {
                    shell
                        .update(cx, |s, cx| {
                            if !s.db_ready_to_edit(cx) {
                                return;
                            }
                            s.db.draft.edits.clear();
                            s.db.errors.clear();
                            s.db.slots = Slots::default();
                            s.db_changed(None, true, cx);
                        })
                        .ok();
                    true
                })
        });
    }

    pub(crate) fn db_import(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if let Some(path) = debug_path("SWOFTY_DEBUG_IMPORT") {
            self.db_import_path(&path, window, cx);
            return;
        }
        let rx = cx.prompt_for_paths(PathPromptOptions {
            files: true,
            directories: false,
            multiple: false,
            prompt: Some("Import".into()),
        });
        cx.spawn_in(window, async move |this, cx| {
            let Ok(Ok(Some(paths))) = rx.await else {
                return;
            };
            let Some(path) = paths.into_iter().next() else {
                return;
            };
            this.update_in(cx, |s, window, cx| s.db_import_path(&path, window, cx))
                .ok();
        })
        .detach();
    }

    fn db_import_path(&mut self, path: &Path, window: &mut Window, cx: &mut Context<Self>) {
        let file = path.file_name().map_or_else(
            || path.display().to_string(),
            |n| n.to_string_lossy().into_owned(),
        );
        let Some(catalog) = self.db.catalog().cloned() else {
            return;
        };
        if self.db.importing {
            return;
        }
        self.db.importing = true;
        window.push_notification(Notification::info(format!("Importing {file}…")), cx);
        cx.notify();
        let (store, path) = (self.store.clone(), path.to_path_buf());
        cx.spawn_in(window, async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move { guarded(|| package::read(&store, &catalog, &path).map_err(text)) })
                .await;
            this.update_in(cx, |s, window, cx| {
                s.db.importing = false;
                s.db_imported(result, file, window, cx);
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    // An addon .zip installs as a new addon and shows on Addons; other files open as a new draft.
    fn db_imported(
        &mut self,
        result: Result<Incoming, String>,
        file: String,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        match result {
            Ok(Incoming::Addon(done)) => {
                let mut body = report_text(&done.report);
                if let Some(from) = &done.renamed_from {
                    body.push_str(&format!(
                        "\n{from} was already installed; this copy is {}",
                        done.package.id
                    ));
                }
                let id = done.package.id.clone();
                let profile = self.library.profile.name.clone();
                if !self.vanilla() && self.db_turn_on(&id).is_ok() {
                    body.push_str(&format!("\nTurned on in {profile}"));
                }
                let title = format!("Imported {}", done.package.name);
                window.push_notification(Notification::success(body).title(title), cx);
                self.reload(cx);
                self.show_addon(&id, cx);
            }
            Ok(Incoming::Changes { name, edits, report }) => {
                let then = Then::Load {
                    file,
                    name,
                    edits,
                    report,
                };
                self.db_leave(then, window, cx);
            }
            Err(e) => {
                crate::app_log::action("db editor: import")
                    .file(std::path::Path::new(&file))
                    .failed(&e);
                window.push_notification(
                    Notification::error(e).title(format!("Could not import {file}")),
                    cx,
                )
            }
        }
    }

    pub(crate) fn db_add_key(
        &mut self,
        path: &str,
        tag: u8,
        value: &str,
        cx: &mut Context<Self>,
    ) -> Result<(), String> {
        let catalog = self.db.catalog().cloned().ok_or("The database is not loaded")?;
        if !self.db_ready_to_edit(cx) {
            return Err(self
                .db
                .addon_error
                .clone()
                .unwrap_or_else(|| "The draft can't be edited now.".into()));
        }
        let key = guarded(|| self.db.draft.edits.add(&catalog, path, tag, value))?;
        self.db_changed(Some(key), true, cx);
        Ok(())
    }

    pub(crate) fn db_link_state(&self, link: &LinkSpec) -> LinkState {
        match link
            .addon
            .as_deref()
            .and_then(|id| self.library.addons.iter().find(|a| a.manifest.id == id))
        {
            Some(a) if a.enabled => LinkState::Open,
            Some(a) => LinkState::Off(a.manifest.name.clone()),
            None => LinkState::Missing,
        }
    }

    pub(crate) fn db_follow(&mut self, link: &LinkSpec, window: &mut Window, cx: &mut Context<Self>) {
        match self.db_link_state(link) {
            LinkState::Open => self.db_open_section(link, cx),
            LinkState::Off(_) => {
                if let Some(id) = link.addon.clone() {
                    self.db.pending_link = Some(link.clone());
                    self.set_addon_enabled(&id, true, window, cx);
                    if let Some(link) = self.db.pending_link.take()
                        && matches!(self.db_link_state(&link), LinkState::Open)
                    {
                        self.db_open_section(&link, cx);
                    }
                }
            }
            LinkState::Missing => {
                if let Some(url) = &link.url {
                    cx.open_url(url);
                }
            }
        }
    }

    fn db_open_section(&mut self, link: &LinkSpec, cx: &mut Context<Self>) {
        let Some(addon) = link.addon.clone() else {
            return;
        };
        let Some(i) = self
            .library
            .sections
            .iter()
            .position(|s| s.addon == addon && link.section.as_ref().is_none_or(|id| *id == s.spec.id))
        else {
            return;
        };
        let section = self.library.sections[i].spec.id.clone();
        self.db.pending_tab = link.tab.clone().map(|t| (addon, section, t));
        self.open_page(Page::Section(i), cx);
    }

    pub(crate) fn career_link() -> LinkSpec {
        LinkSpec {
            label: "Open Career Mode Editor".into(),
            text: String::new(),
            addon: Some(CAREER_EDITOR.into()),
            section: Some("editor".into()),
            tab: None,
            url: Some(CAREER_EDITOR_URL.into()),
        }
    }

    // A link that named a tab: once the section's view is in, switch it to that tab.
    pub(crate) fn db_pending_tab(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some((addon, section, tab)) = self.db.pending_tab.clone() else {
            return;
        };
        let Page::Section(i) = self.page else {
            self.db.pending_tab = None;
            return;
        };
        if self
            .library
            .sections
            .get(i)
            .is_none_or(|s| s.addon != addon || s.spec.id != section)
        {
            self.db.pending_tab = None;
            return;
        }
        let view = match self.views.get(&format!("{addon}/{section}")) {
            Some(Ok(v)) => v.clone(),
            Some(Err(_)) => {
                self.db.pending_tab = None;
                return;
            }
            None => return,
        };
        self.db.pending_tab = None;
        let mut blocks = Vec::new();
        if let Body::Blocks(b) = &view.body {
            crate::blocks::walk(b, &mut blocks);
        }
        let Some(tabs) = blocks.into_iter().find_map(|b| match b {
            Block::Tabs(t) if t.items.iter().any(|i| i.id == tab) => Some(t.clone()),
            _ => None,
        }) else {
            return;
        };
        if tabs.active.as_deref() == Some(tab.as_str()) {
            return;
        }
        let control = format!("{addon}/{section}/{}:{tab}", tabs.id);
        let event = swofty_core::view::Event::Tab {
            id: tabs.id.clone(),
            tab,
        };
        self.send(&addon, &section, control, event, window, cx);
    }

    pub(crate) fn db_start_cell(
        &mut self,
        key: u64,
        value: String,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        if self.db_locked() {
            return;
        }
        let input = cx.new(|cx| InputState::new(window, cx).default_value(value));
        input.update(cx, |s, cx| {
            s.focus(window, cx);
            s.select_all(window, cx);
        });
        let sub = cx.subscribe_in(&input, window, move |this, state, ev: &InputEvent, _, cx| {
            if !matches!(ev, InputEvent::PressEnter { .. } | InputEvent::Blur) {
                return;
            }
            if this.db.cell.as_ref().is_none_or(|c| c.key != key) {
                return;
            }
            let text = state.read(cx).value().trim().to_owned();
            this.db.cell = None;
            this.db_set(key, &text, cx);
            cx.notify();
        });
        self.db.cell = Some(CellEdit {
            key,
            input,
            _sub: sub,
        });
        cx.notify();
    }

    pub(crate) fn db_loading(&self, text: &str, p: crate::theme::Palette) -> AnyElement {
        v_flex()
            .flex_1()
            .items_center()
            .justify_center()
            .gap(px(10.))
            .pb(px(48.))
            .child(crate::pages::badge(IconName::Database, 44., p))
            .child(
                div()
                    .pt(px(8.))
                    .text_size(px(13.))
                    .text_color(p.text_muted)
                    .child(text.to_owned()),
            )
            .into_any_element()
    }
}

pub(crate) enum LinkState {
    Open,
    Off(String),
    Missing,
}
