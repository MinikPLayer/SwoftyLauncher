use std::collections::{HashMap, HashSet};
use std::path::Path;
use std::sync::Arc;
use std::time::{Duration, Instant};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::input::{Input, InputEvent, InputState};
use gpui_kit::component::menu::{DropdownMenu, PopupMenuItem};
use gpui_kit::component::switch::Switch;
use gpui_kit::component::{Disableable, Icon, Sizable, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::catalog::{self, Addon};
use swofty_core::install::Game;
use swofty_core::multiplayer::diag;
use swofty_core::multiplayer::fingerprint::{Mismatch, MismatchKind};
use swofty_core::multiplayer::game::{self as mp_game, Command, GameStatus, Screen};
use swofty_core::multiplayer::rooms::{Api, RoomSettings, RoomSummary};
use swofty_core::multiplayer::session::{
    FixStep, Local, MemberState, Phase, PrintMatch, RelayStatus, Session, SessionState, Setup,
};
use swofty_core::store::Store;

use crate::library::plural;
use crate::pages::{GUTTER, TRACK, badge, label, monogram, tag};
use crate::shell::{Page, Shell};
use crate::theme::{Palette, ScrollArea};

const TICK: Duration = Duration::from_millis(500);
const GAME_EVERY: Duration = Duration::from_secs(2);
const ROOMS_EVERY: Duration = Duration::from_secs(5);
const STATUS_TIMEOUT: Duration = Duration::from_secs(3);
const MIN_PLAYERS: u8 = 2;
const MAX_PLAYERS: u8 = 20;
const TRACKS_SHOWN: usize = 3;
const AVATARS_DIR: &str = "avatars";

/// Where the game is, in the page header.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum GameState {
    NotRunning,
    MainMenu,
    LanLobby,
    Busy,
}

impl GameState {
    pub(crate) fn of(running: bool, status: Option<&GameStatus>) -> Self {
        if !running {
            return Self::NotRunning;
        }
        match status.map(|s| s.screen) {
            Some(Screen::MainMenu) => Self::MainMenu,
            Some(Screen::LanLobby) => Self::LanLobby,
            Some(Screen::Other) | None => Self::Busy,
        }
    }

    pub(crate) fn text(self) -> &'static str {
        match self {
            Self::NotRunning => "F1 25 isn't running",
            Self::MainMenu => "F1 25 is on the main menu",
            Self::LanLobby => "F1 25 is in a LAN lobby",
            Self::Busy => "F1 25 is busy",
        }
    }

    /// Why Create and Join wait, or `None` on the main menu.
    pub(crate) fn not_ready(self) -> Option<&'static str> {
        match self {
            Self::MainMenu => None,
            Self::NotRunning => Some("Start F1 25 to create or join a room."),
            Self::LanLobby => Some("Leave F1 25's LAN lobby to create or join a room."),
            Self::Busy => Some("Go to F1 25's main menu to create or join a room."),
        }
    }
}

pub(crate) fn tracks_text(tracks: &[String]) -> Option<String> {
    if tracks.is_empty() {
        return None;
    }
    let mut text = tracks
        .iter()
        .take(TRACKS_SHOWN)
        .cloned()
        .collect::<Vec<_>>()
        .join(", ");
    if tracks.len() > TRACKS_SHOWN {
        text.push_str(&format!(" and {} more", tracks.len() - TRACKS_SHOWN));
    }
    Some(text)
}

/// The room row's second line: host and players.
pub(crate) fn room_meta(room: &RoomSummary) -> String {
    format!(
        "{} \u{00b7} {}/{} players",
        room.host.name, room.members, room.max_members
    )
}

/// What a room's one status line says, and whether it needs the player's attention.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum RoomStatus {
    Full,
    Differ,
    Match,
    Unknown,
}

impl RoomStatus {
    pub(crate) fn of(room: &RoomSummary, mine: Option<&str>) -> Self {
        if room.members >= room.max_members {
            return Self::Full;
        }
        match (room.digest.as_deref(), mine) {
            (Some(host), Some(mine)) if host.eq_ignore_ascii_case(mine) => Self::Match,
            (Some(_), Some(_)) => Self::Differ,
            _ => Self::Unknown,
        }
    }

    pub(crate) fn text(self) -> &'static str {
        match self {
            Self::Full => "Full",
            Self::Differ => "Your addons differ",
            Self::Match => "Your addons match",
            Self::Unknown => "",
        }
    }
}

fn capitalised(text: &str) -> String {
    let mut chars = text.chars();
    match chars.next() {
        Some(c) => c.to_uppercase().chain(chars).collect(),
        None => String::new(),
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Tone {
    Working,
    Blocked,
    Good,
    Over,
    Failed,
}

/// The room's state as the player reads it: a short line and what to do next. The session's
/// own wording goes to the multiplayer log.
pub(crate) fn status_text(state: &SessionState) -> (Tone, String, Option<&'static str>) {
    let host = state.host;
    match &state.phase {
        Phase::Connecting(_) if host => (Tone::Working, "Opening your room\u{2026}".into(), None),
        Phase::Connecting(_) => (Tone::Working, "Joining the room\u{2026}".into(), None),
        Phase::WaitingForGame(_) if !state.mismatches.is_empty() => {
            (Tone::Blocked, "Your addons don't match the host's".into(), None)
        }
        Phase::WaitingForGame(why) => waiting_text(why, host),
        Phase::InLobby if host => (
            Tone::Good,
            "Your room is open".into(),
            Some("Pick tracks and settings in F1 25's LAN lobby, then start the race there."),
        ),
        Phase::InLobby => (
            Tone::Good,
            "You're in the lobby".into(),
            Some("The host picks the tracks and starts the race."),
        ),
        Phase::Racing if host => (
            Tone::Good,
            "Racing".into(),
            Some("Mark the race over when everyone is back in the lobby."),
        ),
        Phase::Racing => (Tone::Good, "Racing".into(), None),
        Phase::Ended(why) => (Tone::Over, capitalised(why), None),
        Phase::Failed(_) => (
            Tone::Failed,
            "The room stopped unexpectedly".into(),
            Some("Leave it and try again. Send Logs has the details."),
        ),
    }
}

fn waiting_text(why: &str, host: bool) -> (Tone, String, Option<&'static str>) {
    let has = |s: &str| why.contains(s);
    let (tone, text, hint): (Tone, &str, Option<&'static str>) = if has("didn't host") {
        (
            Tone::Blocked,
            "Couldn't start hosting. Go back to F1 25's main menu and try again.",
            None,
        )
    } else if has("didn't join") {
        (
            Tone::Blocked,
            "Couldn't join the host's lobby. Go back to F1 25's main menu and try again.",
            None,
        )
    } else if has("left the LAN session") {
        (
            Tone::Blocked,
            "F1 25 left the lobby. Try again, or leave the room.",
            None,
        )
    } else if has("room settings") {
        (
            Tone::Blocked,
            "F1 25 didn't take the room. Restart it from the launcher and try again.",
            None,
        )
    } else if has("car category") {
        (
            Tone::Working,
            "Pick the car category in F1 25",
            Some("F1 25 opens its LAN lobby after that."),
        )
    } else if has("joining the host") {
        (Tone::Working, "Joining the host's lobby in F1 25\u{2026}", None)
    } else if has("host's addon list") {
        (Tone::Working, "Waiting for the host\u{2026}", None)
    } else if has("already in a LAN session") {
        (Tone::Blocked, "Leave the LAN lobby in F1 25 first.", None)
    } else if has("start F1 25") {
        (
            Tone::Blocked,
            "Start F1 25 from the launcher.",
            Some("Play starts it ready for multiplayer."),
        )
    } else if has("main menu") {
        (Tone::Blocked, "Go to F1 25's main menu.", None)
    } else if host {
        (Tone::Blocked, "Waiting for F1 25 to host.", None)
    } else {
        (Tone::Blocked, "Waiting for F1 25 to join.", None)
    };
    (tone, text.to_owned(), hint)
}

/// Retry drives the game again: offered while the session waits on the game for anything but
/// the addons, or after an error.
pub(crate) fn retry_offered(state: &SessionState) -> bool {
    match &state.phase {
        Phase::WaitingForGame(_) => state.mismatches.is_empty(),
        Phase::Connecting(_) | Phase::InLobby | Phase::Racing => state.last_error.is_some(),
        Phase::Ended(_) | Phase::Failed(_) => false,
    }
}

/// One word for a member in the player list.
pub(crate) fn member_status(m: &MemberState, phase: &Phase) -> &'static str {
    if !m.connected {
        "Disconnected"
    } else if m.fingerprint == PrintMatch::Differs {
        "Addons differ"
    } else if m.me {
        match phase {
            Phase::InLobby | Phase::Racing => "Ready",
            _ => "Joining\u{2026}",
        }
    } else if m.relay == RelayStatus::Ready && (m.host || m.fingerprint == PrintMatch::Matches) {
        "Ready"
    } else {
        "Joining\u{2026}"
    }
}

/// A difference from the host in plain words.
pub(crate) fn difference_text(m: &Mismatch) -> String {
    match &m.kind {
        MismatchKind::GameBuild { .. } => "Your F1 25 version is not the host's".into(),
        MismatchKind::Dlc { .. } => "Your DLC is not the host's".into(),
        MismatchKind::Launcher { .. } => "Your Swofty Launcher version is not the host's".into(),
        MismatchKind::MissingAddon { name, version, .. } => format!("{name} {version} is missing"),
        MismatchKind::ExtraAddon { name, .. } => format!("{name} is on; the host doesn't use it"),
        MismatchKind::VersionDiffers {
            name, local, host, ..
        } => format!("{name}: you have {local}, the host has {host}"),
        MismatchKind::SourceDiffers { name, .. } => format!("{name} is a different copy from the host's"),
        MismatchKind::InputsDiffer { name, .. } => format!("{name} has other data than the host's"),
        MismatchKind::LoadOrder { .. } => "Your addons load in a different order".into(),
        MismatchKind::OptionDiffers { addon, option, .. } => {
            format!("A setting differs: {addon} {option}")
        }
    }
}

/// The fix steps the launcher can take on its own, and the lines left to the player.
pub(crate) fn fixable(steps: &[FixStep]) -> (usize, Vec<String>) {
    let manual = steps
        .iter()
        .filter_map(|s| match s {
            FixStep::Manual { line } => Some(capitalised(line)),
            _ => None,
        })
        .collect::<Vec<_>>();
    (steps.len() - manual.len(), manual)
}

#[derive(Debug, Default, Clone, PartialEq)]
pub(crate) struct Fixed {
    pub(crate) done: Vec<String>,
    pub(crate) failed: Vec<String>,
}

fn install_exact(
    store: &Store,
    profile: &str,
    catalog: &[Addon],
    id: &str,
    version: &semver::Version,
    sha256: Option<&str>,
) -> Result<(), String> {
    let addon = catalog
        .iter()
        .find(|a| a.id == id)
        .ok_or_else(|| format!("{id} is not in the Addon Browser"))?;
    let v = addon
        .versions
        .iter()
        .chain(addon.latest.iter())
        .find(|v| v.version == *version && sha256.is_none_or(|s| v.sha256.trim().eq_ignore_ascii_case(s)))
        .ok_or_else(|| format!("{} {version} is not in the Addon Browser", addon.name))?;
    catalog::requires::install(store, catalog, addon, v, &|_| {}).map_err(|e| e.to_string())?;
    let registry = store.registry().map_err(|e| e.to_string())?;
    store
        .update_profile(profile, |p| registry.set_enabled(p, id, true))
        .map_err(|e| e.to_string())
}

fn reorder(order: &[String], addons: &mut [swofty_core::profile::ProfileAddon]) {
    let slots: Vec<usize> = (0..addons.len())
        .filter(|&i| order.contains(&addons[i].id))
        .collect();
    let mut placed: Vec<_> = slots.iter().map(|&i| addons[i].clone()).collect();
    placed.sort_by_key(|a| order.iter().position(|id| *id == a.id));
    for (slot, addon) in slots.into_iter().zip(placed) {
        addons[slot] = addon;
    }
}

/// Makes `profile` match the host as far as the launcher can: exact registry versions, addons
/// turned off, options and load order. The game picks the changes up when it starts again.
fn apply_fix(
    store: &Store,
    profile: &str,
    catalog: &[Addon],
    steps: &[FixStep],
) -> (Vec<String>, Vec<String>) {
    let (mut done, mut failed) = (Vec::new(), Vec::new());
    for step in steps {
        let result = match step {
            FixStep::Install {
                id,
                name,
                version,
                sha256,
            } => install_exact(store, profile, catalog, id, version, sha256.as_deref())
                .map(|_| format!("Installed {name} {version}")),
            FixStep::Disable { id, name } => store
                .registry()
                .and_then(|r| store.update_profile(profile, |p| r.set_enabled(p, id, false)))
                .map(|_| format!("Turned off {name}"))
                .map_err(|e| e.to_string()),
            FixStep::SetOption { addon, option, value } => store
                .addons()
                .and_then(|all| {
                    let m = all
                        .get(addon)
                        .ok_or_else(|| swofty_core::Error::Other(format!("{addon} is not installed")))?;
                    let value = match value {
                        Some(v) => v.clone(),
                        None => m
                            .options
                            .iter()
                            .find(|o| o.id == *option)
                            .map(|o| o.default_value())
                            .unwrap_or_default(),
                    };
                    store.update_profile(profile, |p| p.set_option(m, option, value).map(drop))
                })
                .map(|_| format!("Set {addon}.{option} to the host's value"))
                .map_err(|e| e.to_string()),
            FixStep::Reorder { order } => store
                .update_profile(profile, |p| {
                    reorder(order, &mut p.addons);
                    Ok(())
                })
                .map(|_| "Put the addons in the host's load order".to_owned())
                .map_err(|e| e.to_string()),
            FixStep::Manual { .. } => continue,
        };
        match result {
            Ok(line) => done.push(line),
            Err(e) => failed.push(e),
        }
    }
    (done, failed)
}

fn read_local(store: &Store, profile: &str, dir: &Path) -> Result<Local, String> {
    let game = Game::locate(&store.root, dir.to_path_buf()).map_err(|e| e.to_string())?;
    let profile = store.profile(profile).map_err(|e| e.to_string())?;
    Local::read(store, &profile, &game.identity).map_err(|e| e.to_string())
}

fn read_game(data: &Path, dir: &Path, running: bool) -> Option<GameStatus> {
    let dir = &mp_game::view(data, dir).ok()?;
    mp_game::last_status(dir).or_else(|| {
        running
            .then(|| mp_game::send(dir, &Command::Status, STATUS_TIMEOUT).ok())
            .flatten()
            .map(|r| r.status)
    })
}

enum Start {
    Create(RoomSettings),
    Join(String),
}

pub(crate) struct Multiplayer {
    visible: bool,
    game: Option<GameStatus>,
    game_at: Option<Instant>,
    game_polling: bool,
    rooms: Vec<RoomSummary>,
    rooms_loaded: bool,
    rooms_error: Option<String>,
    rooms_at: Option<Instant>,
    rooms_polling: bool,
    digest: Option<String>,
    digest_busy: bool,
    form: bool,
    name: Entity<InputState>,
    code: Entity<InputState>,
    public: bool,
    max: u8,
    starting: Option<String>,
    session: Option<Session>,
    state: Option<SessionState>,
    leaving: bool,
    fixing: bool,
    fixed: Option<Fixed>,
    refreshing: bool,
    refreshed: bool,
    error: Option<String>,
    copied: Option<Instant>,
    faces: HashMap<String, Arc<Image>>,
    faces_asked: HashSet<String>,
    _inputs: [Subscription; 2],
}

impl Multiplayer {
    pub(crate) fn new(window: &mut Window, cx: &mut Context<Shell>) -> Self {
        let name = cx.new(|cx| InputState::new(window, cx).placeholder("Room name"));
        let code = cx.new(|cx| InputState::new(window, cx).placeholder("Invite code"));
        let changed = |cx: &mut Context<Shell>, input: &Entity<InputState>| {
            cx.subscribe(input, |_, _, e: &InputEvent, cx| {
                if matches!(e, InputEvent::Change) {
                    cx.notify();
                }
            })
        };
        let inputs = [changed(cx, &name), changed(cx, &code)];
        Self {
            visible: false,
            game: None,
            game_at: None,
            game_polling: false,
            rooms: Vec::new(),
            rooms_loaded: false,
            rooms_error: None,
            rooms_at: None,
            rooms_polling: false,
            digest: None,
            digest_busy: false,
            form: false,
            name,
            code,
            public: true,
            max: 8,
            starting: None,
            session: None,
            state: None,
            leaving: false,
            fixing: false,
            fixed: None,
            refreshing: false,
            refreshed: false,
            error: None,
            copied: None,
            faces: HashMap::new(),
            faces_asked: HashSet::new(),
            _inputs: inputs,
        }
    }

    fn busy(&self) -> bool {
        self.starting.is_some() || self.session.is_some()
    }

    fn finish_leaving(&mut self) {
        self.session = None;
        self.state = None;
        self.leaving = false;
        self.fixing = false;
        self.fixed = None;
        self.refreshing = false;
        self.refreshed = false;
        self.rooms_at = None;
        self.game_at = None;
        self.digest = None;
    }
}

fn due(at: Option<Instant>, every: Duration, now: Instant) -> bool {
    at.is_none_or(|at| now.duration_since(at) >= every)
}

impl Shell {
    pub(crate) fn mp_allowed(&self) -> bool {
        self.mp.as_ref().is_some_and(|m| m.allowed)
    }

    pub(crate) fn watch_multiplayer(cx: &mut Context<Self>) {
        cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(TICK).await;
                if this.update(cx, |s, cx| s.mp_tick(cx)).is_err() {
                    break;
                }
            }
        })
        .detach();
    }

    fn mp_tick(&mut self, cx: &mut Context<Self>) {
        let m = &mut self.mp_page;
        if m.leaving && m.session.as_ref().is_none_or(Session::is_finished) {
            m.finish_leaving();
            cx.notify();
        }
        if self.page != Page::Multiplayer || !self.mp_allowed() {
            self.mp_page.visible = false;
            return;
        }
        let now = Instant::now();
        let m = &mut self.mp_page;
        if !m.visible {
            m.visible = true;
            m.rooms_at = None;
            m.game_at = None;
            m.digest = None;
        }
        if let Some(session) = &m.session {
            let state = session.state();
            if m.refreshed && state.mismatches.is_empty() {
                m.fixed = None;
                m.refreshed = false;
            }
            if m.state.as_ref() != Some(&state) {
                m.state = Some(state);
                cx.notify();
            }
        }
        if m.copied
            .is_some_and(|at| now.duration_since(at) > Duration::from_secs(2))
        {
            m.copied = None;
            cx.notify();
        }
        if m.session.is_none() && !m.game_polling && due(m.game_at, GAME_EVERY, now) {
            self.mp_poll_game(cx);
        }
        let m = &self.mp_page;
        if m.session.is_none() && !m.rooms_polling && due(m.rooms_at, ROOMS_EVERY, now) {
            self.mp_poll_rooms(cx);
        }
        let m = &self.mp_page;
        if m.digest.is_none() && !m.digest_busy && m.session.is_none() {
            self.mp_read_digest(cx);
        }
        self.mp_load_faces(cx);
    }

    fn mp_load_faces(&mut self, cx: &mut Context<Self>) {
        let m = &mut self.mp_page;
        let members = m
            .state
            .iter()
            .flat_map(|s| &s.members)
            .filter_map(|x| x.avatar.as_ref());
        let hosts = m.rooms.iter().filter_map(|r| r.host.avatar.as_ref());
        let wanted: Vec<String> = members
            .chain(hosts)
            .filter(|u| u.starts_with("http") && !m.faces_asked.contains(*u))
            .cloned()
            .collect::<HashSet<_>>()
            .into_iter()
            .collect();
        if wanted.is_empty() {
            return;
        }
        m.faces_asked.extend(wanted.iter().cloned());
        let dir = self.store.root.join(catalog::CACHE_DIR).join(AVATARS_DIR);
        cx.spawn(async move |this, cx| {
            let faces: Vec<(String, Arc<Image>)> = cx
                .background_executor()
                .spawn(async move {
                    wanted
                        .into_iter()
                        .filter_map(|url| crate::news::image(&dir, &url).map(|i| (url, i)))
                        .collect()
                })
                .await;
            this.update(cx, |s, cx| {
                s.mp_page.faces.extend(faces);
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    fn face(&self, name: &str, url: Option<&str>, size: f32, p: Palette) -> Div {
        match url.and_then(|u| self.mp_page.faces.get(u)) {
            Some(image) => div()
                .size(px(size))
                .flex_shrink_0()
                .rounded_full()
                .overflow_hidden()
                .child(img(image.clone()).size_full()),
            None => monogram(name, size, p).rounded_full(),
        }
    }

    fn mp_poll_game(&mut self, cx: &mut Context<Self>) {
        let Some(dir) = self.install.as_ref().map(|i| i.dir.clone()) else {
            self.mp_page.game = None;
            self.mp_page.game_at = Some(Instant::now());
            return;
        };
        let running = !self.processes.is_empty();
        let data = self.store.root.clone();
        self.mp_page.game_polling = true;
        cx.spawn(async move |this, cx| {
            let status = cx
                .background_executor()
                .spawn(async move { read_game(&data, &dir, running) })
                .await;
            this.update(cx, |s, cx| {
                let m = &mut s.mp_page;
                m.game_polling = false;
                m.game_at = Some(Instant::now());
                if m.game != status {
                    m.game = status;
                    cx.notify();
                }
            })
            .ok();
        })
        .detach();
    }

    fn mp_poll_rooms(&mut self, cx: &mut Context<Self>) {
        let Some(token) = self.mp.as_ref().map(|m| m.token.clone()) else {
            return;
        };
        self.mp_page.rooms_polling = true;
        cx.spawn(async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move { Api::new(&token).rooms() })
                .await;
            this.update(cx, |s, cx| {
                let m = &mut s.mp_page;
                m.rooms_polling = false;
                m.rooms_at = Some(Instant::now());
                match result {
                    Ok(rooms) => {
                        m.rooms = rooms;
                        m.rooms_loaded = true;
                        m.rooms_error = None;
                    }
                    Err(e) => m.rooms_error = Some(e.message),
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    fn mp_read_digest(&mut self, cx: &mut Context<Self>) {
        let Some(dir) = self.install.as_ref().map(|i| i.dir.clone()) else {
            return;
        };
        let (store, profile) = (self.store.clone(), self.library.profile.name.clone());
        self.mp_page.digest_busy = true;
        cx.spawn(async move |this, cx| {
            let digest = cx
                .background_executor()
                .spawn(async move { read_local(&store, &profile, &dir).map(|l| l.digest()) })
                .await;
            this.update(cx, |s, cx| {
                s.mp_page.digest_busy = false;
                match digest {
                    Ok(d) => s.mp_page.digest = Some(d),
                    Err(e) => eprintln!("swofty-launcher: couldn't read this profile's addons: {e}"),
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    fn mp_game_state(&self) -> GameState {
        let status = match &self.mp_page.state {
            Some(state) if self.mp_page.session.is_some() => state.game.as_ref(),
            _ => self.mp_page.game.as_ref(),
        };
        GameState::of(!self.processes.is_empty(), status)
    }

    fn mp_start(&mut self, start: Start, cx: &mut Context<Self>) {
        if self.mp_page.busy() {
            return;
        }
        let Some(token) = self.mp.as_ref().map(|m| m.token.clone()) else {
            return;
        };
        let Some(dir) = self.install.as_ref().map(|i| i.dir.clone()) else {
            self.mp_page.error = Some("F1 25 was not found. Locate it from the play bar first.".into());
            cx.notify();
            return;
        };
        let (store, profile) = (self.store.clone(), self.library.profile.name.clone());
        let m = &mut self.mp_page;
        m.starting = Some(match &start {
            Start::Create(_) => "create".into(),
            Start::Join(code) => code.clone(),
        });
        m.error = None;
        cx.spawn(async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move {
                    let local = read_local(&store, &profile, &dir).inspect_err(|e| {
                        diag::line(format!("session not started: couldn't read the addons: {e}"));
                    })?;
                    let setup = Setup::new(Api::new(&token), &store.root, &dir, local);
                    Ok::<_, String>(match start {
                        Start::Create(settings) => Session::create(setup, settings),
                        Start::Join(code) => Session::join(setup, &code),
                    })
                })
                .await;
            this.update(cx, |s, cx| {
                let m = &mut s.mp_page;
                m.starting = None;
                match result {
                    Ok(session) => {
                        m.state = Some(session.state());
                        m.session = Some(session);
                        m.form = false;
                    }
                    Err(e) => m.error = Some(format!("Couldn't read your addons: {e}")),
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    fn mp_create(&mut self, cx: &mut Context<Self>) {
        let typed = self.mp_page.name.read(cx).value().trim().to_owned();
        let name = if typed.is_empty() {
            self.mp_default_name()
        } else {
            typed
        };
        let settings = RoomSettings {
            name: Some(name),
            public: self.mp_page.public,
            max_members: self.mp_page.max,
            ..RoomSettings::default()
        };
        self.mp_start(Start::Create(settings), cx);
    }

    fn mp_default_name(&self) -> String {
        let you = self.mp.as_ref().map_or("Your", |m| m.name.as_str());
        format!("{you}'s room")
    }

    fn mp_open_form(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.mp_page.form = !self.mp_page.form;
        if self.mp_page.form && self.mp_page.name.read(cx).value().is_empty() {
            let name = self.mp_default_name();
            self.mp_page
                .name
                .update(cx, |s, cx| s.set_value(name, window, cx));
        }
        cx.notify();
    }

    fn mp_join_code(&mut self, cx: &mut Context<Self>) {
        let code = self.mp_page.code.read(cx).value().trim().to_owned();
        if !code.is_empty() {
            self.mp_start(Start::Join(code), cx);
        }
    }

    pub(crate) fn mp_leave(&mut self, cx: &mut Context<Self>) {
        let Some(session) = &self.mp_page.session else {
            return;
        };
        session.leave();
        self.mp_page.leaving = true;
        cx.notify();
    }

    fn mp_fix(&mut self, cx: &mut Context<Self>) {
        let Some(session) = &self.mp_page.session else {
            return;
        };
        let steps = session.fix_plan();
        let (store, profile) = (self.store.clone(), self.library.profile.name.clone());
        let listing = self.browser.addons.clone();
        self.mp_page.fixing = true;
        self.mp_page.error = None;
        cx.spawn(async move |this, cx| {
            let (done, failed) = cx
                .background_executor()
                .spawn(async move {
                    let listing = if listing.is_empty() {
                        catalog::cached(&store.root).unwrap_or_default()
                    } else {
                        listing
                    };
                    apply_fix(&store, &profile, &listing, &steps)
                })
                .await;
            this.update(cx, |s, cx| {
                let m = &mut s.mp_page;
                m.fixing = false;
                m.refreshed = false;
                m.fixed = Some(Fixed { done, failed });
                s.reload(cx);
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    /// Drives the game again; after a fix, with this profile's fingerprint read afresh first.
    fn mp_retry(&mut self, cx: &mut Context<Self>) {
        let Some(session) = &self.mp_page.session else {
            return;
        };
        let dir = self.install.as_ref().map(|i| i.dir.clone());
        let Some(dir) = dir.filter(|_| self.mp_page.fixed.is_some()) else {
            session.retry();
            cx.notify();
            return;
        };
        let (store, profile) = (self.store.clone(), self.library.profile.name.clone());
        self.mp_page.refreshing = true;
        self.mp_page.error = None;
        cx.spawn(async move |this, cx| {
            let local = cx
                .background_executor()
                .spawn(async move { read_local(&store, &profile, &dir) })
                .await;
            this.update(cx, |s, cx| {
                let m = &mut s.mp_page;
                m.refreshing = false;
                if let Some(session) = &m.session {
                    match local {
                        Ok(local) => {
                            session.update_local(local);
                            m.refreshed = true;
                        }
                        Err(e) => m.error = Some(format!("Couldn't read your addons: {e}")),
                    }
                    session.retry();
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    fn mp_copy_code(&mut self, code: String, cx: &mut Context<Self>) {
        cx.write_to_clipboard(ClipboardItem::new_string(code));
        self.mp_page.copied = Some(Instant::now());
        cx.notify();
    }

    pub(crate) fn multiplayer_page(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let in_room = self.mp_page.session.is_some();
        match self.mp_page.state.clone() {
            Some(state) if in_room => self.mp_room(&state, p, cx),
            _ => self.mp_lobby(p, cx),
        }
    }

    fn game_status(&self, p: Palette) -> Div {
        h_flex()
            .gap(px(6.))
            .flex_shrink_0()
            .text_size(px(13.))
            .text_color(p.text_muted)
            .child(
                Icon::new(IconName::Gamepad2)
                    .size(px(15.))
                    .text_color(p.text_placeholder),
            )
            .child(self.mp_game_state().text())
    }

    fn section_head(&self, title: impl Into<SharedString>, p: Palette) -> Div {
        h_flex()
            .h(px(28.))
            .gap(px(10.))
            .items_center()
            .child(label(title, p))
    }

    fn mp_lobby(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let subtitle =
            "Race with players who use the same addons. The host picks tracks in F1 25's LAN lobby."
                .to_owned();
        let actions = h_flex()
            .gap(px(14.))
            .items_center()
            .child(self.game_status(p))
            .child(
                Button::new("mp-create-open")
                    .outline()
                    .icon(IconName::Plus)
                    .label("Create room")
                    .disabled(self.mp_page.form)
                    .on_click(cx.listener(|this, _, window, cx| this.mp_open_form(window, cx))),
            );
        let rooms: AnyElement = if !self.mp_page.rooms_loaded && self.mp_page.rooms_error.is_none() {
            div()
                .text_size(px(13.))
                .text_color(p.text_muted)
                .child("Loading rooms\u{2026}")
                .into_any_element()
        } else if self.mp_page.rooms.is_empty() {
            let body = match &self.mp_page.rooms_error {
                Some(_) => "The room list didn't load. It tries again in a few seconds.",
                None => "Create one, or join a private room with its invite code.",
            };
            v_flex()
                .py(px(40.))
                .items_center()
                .gap(px(8.))
                .rounded(px(6.))
                .border_1()
                .border_color(p.border)
                .child(badge(IconName::Users, 40., p))
                .child(
                    div()
                        .pt(px(6.))
                        .text_size(px(15.))
                        .font_weight(FontWeight::SEMIBOLD)
                        .child("No open rooms"),
                )
                .child(div().text_size(px(13.)).text_color(p.text_muted).child(body))
                .into_any_element()
        } else {
            let list = self.mp_page.rooms.clone();
            v_flex()
                .rounded(px(6.))
                .border_1()
                .border_color(p.border)
                .overflow_hidden()
                .children(
                    list.iter()
                        .enumerate()
                        .map(|(i, r)| self.room_row(r, i == 0, p, cx)),
                )
                .into_any_element()
        };
        let hint = self.mp_game_state().not_ready();
        v_flex()
            .size_full()
            .child(self.header(Page::Multiplayer.label(), subtitle, actions, p))
            .child(
                v_flex()
                    .flex_1()
                    .min_h_0()
                    .scroll_y("mp-lobby")
                    .px(px(GUTTER))
                    .pt(px(4.))
                    .pb(px(24.))
                    .gap(px(24.))
                    .when(self.mp_page.form, |d| d.child(self.create_form(p, cx)))
                    .child(
                        v_flex()
                            .gap(px(8.))
                            .child(
                                self.section_head(
                                    if self.mp_page.rooms_loaded {
                                        format!("OPEN ROOMS \u{00b7} {}", self.mp_page.rooms.len())
                                    } else {
                                        "OPEN ROOMS".to_owned()
                                    },
                                    p,
                                )
                                .child(div().flex_1())
                                .child(self.code_join(p, cx)),
                            )
                            .when_some(hint.filter(|_| !self.mp_page.form), |d, h| {
                                d.child(div().text_size(px(13.)).text_color(p.text_muted).child(h))
                            })
                            .when_some(self.mp_page.error.clone(), |d, e| {
                                d.child(div().text_size(px(13.)).text_color(p.error).child(e))
                            })
                            .child(rooms),
                    ),
            )
            .into_any_element()
    }

    fn code_join(&self, _p: Palette, cx: &mut Context<Self>) -> Div {
        let game = self.mp_game_state();
        let typed = !self.mp_page.code.read(cx).value().trim().is_empty();
        let joining = self
            .mp_page
            .starting
            .as_ref()
            .is_some_and(|s| s.as_str() != "create");
        h_flex()
            .gap(px(8.))
            .child(
                div().w(px(170.)).child(
                    Input::new(&self.mp_page.code)
                        .small()
                        .disabled(self.mp_page.busy()),
                ),
            )
            .child(
                Button::new("mp-join-code")
                    .outline()
                    .small()
                    .label("Join with code")
                    .loading(joining && typed)
                    .disabled(!typed || game.not_ready().is_some() || self.mp_page.busy())
                    .on_click(cx.listener(|this, _, _, cx| this.mp_join_code(cx))),
            )
    }

    fn room_row(&self, room: &RoomSummary, first: bool, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let status = RoomStatus::of(room, self.mp_page.digest.as_deref());
        let blocked = status == RoomStatus::Full || self.mp_game_state().not_ready().is_some();
        let joining = self.mp_page.starting.as_deref() == Some(room.code.as_str());
        let code = room.code.clone();
        let tracks = room.addons.as_ref().and_then(|a| tracks_text(&a.tracks));
        h_flex()
            .px(px(16.))
            .py(px(14.))
            .gap(px(14.))
            .items_center()
            .when(!first, |d| d.border_t_1().border_color(p.border_variant))
            .child(self.face(&room.host.name, room.host.avatar.as_deref(), 36., p))
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .gap(px(3.))
                    .child(
                        div()
                            .text_size(px(14.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .truncate()
                            .child(room.name.clone()),
                    )
                    .child(
                        div()
                            .text_size(px(13.))
                            .text_color(p.text_muted)
                            .truncate()
                            .child(room_meta(room)),
                    )
                    .when_some(tracks, |d, t| {
                        d.child(
                            h_flex()
                                .gap(px(6.))
                                .text_size(px(12.))
                                .text_color(p.text_placeholder)
                                .child(svg().data(TRACK).size(px(13.)).text_color(p.text_placeholder))
                                .child(div().truncate().child(t)),
                        )
                    }),
            )
            .child(
                div()
                    .flex_shrink_0()
                    .text_size(px(13.))
                    .text_color(if status == RoomStatus::Differ {
                        p.warning
                    } else {
                        p.text_muted
                    })
                    .child(status.text()),
            )
            .child(
                Button::new(SharedString::from(format!("mp-join-{}", room.code)))
                    .small()
                    .outline()
                    .label("Join")
                    .loading(joining)
                    .disabled(blocked || self.mp_page.busy())
                    .on_click(
                        cx.listener(move |this, _, _, cx| this.mp_start(Start::Join(code.clone()), cx)),
                    ),
            )
            .into_any_element()
    }

    fn create_form(&self, p: Palette, cx: &mut Context<Self>) -> Div {
        let m = &self.mp_page;
        let blocked = self.mp_game_state().not_ready();
        let creating = m.starting.as_deref() == Some("create");
        let shell = cx.entity().downgrade();
        let players = Button::new("mp-max")
            .outline()
            .small()
            .w(px(160.))
            .label(plural(usize::from(m.max), "player", "players"))
            .dropdown_caret(true)
            .dropdown_menu_with_anchor(Anchor::TopLeft, move |mut menu, _, _| {
                for n in MIN_PLAYERS..=MAX_PLAYERS {
                    let shell = shell.clone();
                    menu = menu.item(
                        PopupMenuItem::new(plural(usize::from(n), "player", "players")).on_click(
                            move |_, _, cx| {
                                shell
                                    .update(cx, |s, cx| {
                                        s.mp_page.max = n;
                                        cx.notify();
                                    })
                                    .ok();
                            },
                        ),
                    );
                }
                menu
            });
        let public = h_flex()
            .gap(px(10.))
            .child(
                Switch::new("mp-public")
                    .checked(m.public)
                    .small()
                    .on_click(cx.listener(|this, on: &bool, _, cx| {
                        this.mp_page.public = *on;
                        cx.notify();
                    })),
            )
            .child(
                div()
                    .text_size(px(13.))
                    .text_color(p.text_muted)
                    .child(if m.public {
                        "Listed: anyone can see it and join"
                    } else {
                        "Private: only people with the code can join"
                    }),
            );
        let row = |title: &'static str, control: AnyElement, first: bool| {
            h_flex()
                .px(px(16.))
                .py(px(10.))
                .gap(px(12.))
                .when(!first, |d| d.border_t_1().border_color(p.border_variant))
                .child(
                    div()
                        .w(px(120.))
                        .flex_shrink_0()
                        .text_size(px(13.))
                        .text_color(p.text_muted)
                        .child(title),
                )
                .child(h_flex().flex_1().min_w_0().child(control))
        };
        v_flex()
            .rounded(px(6.))
            .border_1()
            .border_color(p.border)
            .child(
                v_flex()
                    .px(px(16.))
                    .pt(px(14.))
                    .pb(px(12.))
                    .gap(px(3.))
                    .border_b_1()
                    .border_color(p.border_variant)
                    .child(
                        div()
                            .text_size(px(14.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child("New room"),
                    )
                    .child(
                        div()
                            .text_size(px(13.))
                            .text_color(p.text_muted)
                            .child("F1 25 opens its LAN lobby, where you pick the tracks and settings."),
                    ),
            )
            .child(row(
                "Name",
                div()
                    .w(px(320.))
                    .child(Input::new(&m.name).small())
                    .into_any_element(),
                true,
            ))
            .child(row("Visibility", public.into_any_element(), false))
            .child(row("Players", players.into_any_element(), false))
            .child(
                h_flex()
                    .px(px(16.))
                    .py(px(12.))
                    .gap(px(8.))
                    .border_t_1()
                    .border_color(p.border_variant)
                    .bg(p.surface)
                    .children(blocked.map(|why| {
                        div()
                            .flex_1()
                            .text_size(px(13.))
                            .text_color(p.text_muted)
                            .child(why)
                    }))
                    .when(blocked.is_none(), |d| d.child(div().flex_1()))
                    .child(
                        Button::new("mp-create-cancel")
                            .ghost()
                            .small()
                            .label("Cancel")
                            .on_click(cx.listener(|this, _, _, cx| {
                                this.mp_page.form = false;
                                cx.notify();
                            })),
                    )
                    .child(
                        Button::new("mp-create")
                            .primary()
                            .small()
                            .label("Create room")
                            .loading(creating)
                            .disabled(blocked.is_some() || m.busy())
                            .on_click(cx.listener(|this, _, _, cx| this.mp_create(cx))),
                    ),
            )
    }

    fn mp_card(&self, p: Palette) -> Div {
        v_flex()
            .p(px(16.))
            .gap(px(12.))
            .rounded(px(6.))
            .border_1()
            .border_color(p.border)
    }

    fn fixed_card(&self, f: &Fixed, p: Palette, cx: &mut Context<Self>) -> Div {
        let blocked = self.mp_game_state() != GameState::MainMenu;
        let m = &self.mp_page;
        let title = if f.failed.is_empty() {
            "Addons updated"
        } else {
            "Some changes didn't work"
        };
        self.mp_card(p)
            .child(
                v_flex()
                    .gap(px(4.))
                    .child(
                        div()
                            .text_size(px(14.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child(title),
                    )
                    .children(
                        f.done
                            .iter()
                            .map(|l| div().text_size(px(13.)).text_color(p.text_muted).child(l.clone())),
                    )
                    .children(f.failed.iter().map(|l| {
                        div()
                            .text_size(px(13.))
                            .text_color(p.warning)
                            .child(capitalised(l))
                    })),
            )
            .child(
                h_flex()
                    .gap(px(12.))
                    .pt(px(12.))
                    .border_t_1()
                    .border_color(p.border_variant)
                    .child(
                        div()
                            .flex_1()
                            .text_size(px(13.))
                            .child("Close F1 25 and press Play so it loads them, then join again."),
                    )
                    .child(
                        Button::new("mp-rejoin")
                            .small()
                            .primary()
                            .label("Join again")
                            .loading(m.refreshing)
                            .disabled(blocked || m.refreshing || m.leaving)
                            .on_click(cx.listener(|this, _, _, cx| this.mp_retry(cx))),
                    ),
            )
    }

    fn mp_room(&mut self, state: &SessionState, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let m = &self.mp_page;
        let over = state.phase.is_over();
        let room = state.room.clone();
        let title = room
            .as_ref()
            .map_or_else(|| "Multiplayer".to_owned(), |r| r.name.clone());
        let subtitle = room.as_ref().map_or_else(
            || "Setting up the room\u{2026}".to_owned(),
            |r| {
                let mut parts = vec![
                    format!("{}/{} players", state.members.len(), r.max_members),
                    if r.public { "Listed" } else { "Private" }.to_owned(),
                ];
                if state.host {
                    parts.push("You're the host".into());
                }
                parts.join(" \u{00b7} ")
            },
        );
        let code = room.as_ref().map(|r| r.code.clone());
        let actions = h_flex()
            .gap(px(8.))
            .items_center()
            .child(div().pr(px(6.)).child(self.game_status(p)))
            .when_some(code, |d, code| {
                let copy = code.clone();
                d.child(
                    Button::new("mp-copy-code")
                        .outline()
                        .icon(if m.copied.is_some() {
                            IconName::Check
                        } else {
                            IconName::Copy
                        })
                        .label(if m.copied.is_some() {
                            "Copied".to_owned()
                        } else {
                            code
                        })
                        .tooltip("Copy the invite code")
                        .on_click(cx.listener(move |this, _, _, cx| this.mp_copy_code(copy.clone(), cx))),
                )
            })
            .child(
                Button::new("mp-leave")
                    .outline()
                    .icon(IconName::LogOut)
                    .label(if m.leaving {
                        "Leaving\u{2026}"
                    } else if over {
                        "Back to rooms"
                    } else {
                        "Leave"
                    })
                    .disabled(m.leaving || m.fixing)
                    .on_click(cx.listener(|this, _, _, cx| this.mp_leave(cx))),
            );
        let (tone, headline, hint) = if m.leaving && !over {
            (Tone::Working, "Leaving the room\u{2026}".to_owned(), None)
        } else {
            status_text(state)
        };
        let (icon, color) = match tone {
            Tone::Working => (IconName::LoaderCircle, p.text_placeholder),
            Tone::Blocked => (IconName::CircleAlert, p.warning),
            Tone::Good => (IconName::CircleCheck, p.success),
            Tone::Over => (IconName::Info, p.text_placeholder),
            Tone::Failed => (IconName::CircleAlert, p.error),
        };
        let started = room.as_ref().is_some_and(|r| r.started);
        let race =
            (state.host && !over && !m.leaving && matches!(state.phase, Phase::InLobby | Phase::Racing))
                .then(|| {
                    Button::new("mp-race")
                        .outline()
                        .small()
                        .icon(IconName::Flag)
                        .label(if started { "Race over" } else { "Race started" })
                        .tooltip(if started {
                            "Show the room in the list again"
                        } else {
                            "Hide the room from the list while you race"
                        })
                        .on_click(cx.listener(move |this, _, _, cx| {
                            if let Some(s) = &this.mp_page.session {
                                s.mark_race(!started);
                            }
                            cx.notify();
                        }))
                });
        let retry = (retry_offered(state) && !m.leaving && m.fixed.is_none()).then(|| {
            Button::new("mp-retry")
                .outline()
                .small()
                .icon(IconName::RefreshCw)
                .label("Try again")
                .loading(m.refreshing)
                .disabled(m.refreshing)
                .on_click(cx.listener(|this, _, _, cx| this.mp_retry(cx)))
        });
        let trouble = state
            .last_error
            .as_ref()
            .filter(|_| !over && !m.leaving)
            .map(|_| "Having trouble reaching the other players. Still trying.");
        let status = self.mp_card(p).child(
            h_flex()
                .gap(px(12.))
                .items_center()
                .child(Icon::new(icon).size(px(18.)).text_color(color))
                .child(
                    v_flex()
                        .flex_1()
                        .min_w_0()
                        .gap(px(3.))
                        .child(
                            div()
                                .text_size(px(14.))
                                .font_weight(FontWeight::SEMIBOLD)
                                .child(headline),
                        )
                        .children(
                            hint.filter(|_| !m.leaving)
                                .map(|h| div().text_size(px(13.)).text_color(p.text_muted).child(h)),
                        )
                        .children(
                            trouble.map(|t| div().text_size(px(13.)).text_color(p.text_muted).child(t)),
                        ),
                )
                .children(race)
                .children(retry),
        );
        let differences = match &m.fixed {
            Some(f) => Some(self.fixed_card(f, p, cx)),
            None if !state.mismatches.is_empty() && !m.leaving && !over => {
                Some(self.mismatch_card(state, p, cx))
            }
            None => None,
        };
        let players: Vec<AnyElement> = state
            .members
            .iter()
            .enumerate()
            .map(|(i, member)| self.member_row(member, &state.phase, i == 0, p))
            .collect();
        v_flex()
            .size_full()
            .child(self.header(title, subtitle, actions, p))
            .child(
                v_flex()
                    .flex_1()
                    .min_h_0()
                    .scroll_y("mp-room")
                    .px(px(GUTTER))
                    .pt(px(4.))
                    .pb(px(24.))
                    .gap(px(24.))
                    .child(status)
                    .children(differences)
                    .child(
                        v_flex()
                            .gap(px(8.))
                            .child(self.section_head(format!("PLAYERS \u{00b7} {}", state.members.len()), p))
                            .child(if players.is_empty() {
                                div()
                                    .text_size(px(13.))
                                    .text_color(p.text_muted)
                                    .child("Nobody is here yet.")
                                    .into_any_element()
                            } else {
                                v_flex()
                                    .rounded(px(6.))
                                    .border_1()
                                    .border_color(p.border)
                                    .overflow_hidden()
                                    .children(players)
                                    .into_any_element()
                            }),
                    ),
            )
            .into_any_element()
    }

    fn mismatch_card(&self, state: &SessionState, p: Palette, cx: &mut Context<Self>) -> Div {
        let steps = swofty_core::multiplayer::session::fix_plan(&state.mismatches);
        let (auto, manual) = fixable(&steps);
        let fixing = self.mp_page.fixing;
        let note = if manual.is_empty() {
            "Fix & join installs the host's exact versions. F1 25 then needs a restart."
        } else if auto > 0 {
            "Fix & join changes what it can; the rest is up to you. F1 25 then needs a restart."
        } else {
            "Change these yourself, then join again."
        };
        self.mp_card(p)
            .child(
                div()
                    .text_size(px(14.))
                    .font_weight(FontWeight::SEMIBOLD)
                    .child("What's different from the host"),
            )
            .child(v_flex().gap(px(6.)).children(state.mismatches.iter().map(|mm| {
                h_flex()
                    .gap(px(10.))
                    .items_start()
                    .text_size(px(13.))
                    .child(
                        div()
                            .mt(px(7.))
                            .size(px(4.))
                            .flex_shrink_0()
                            .rounded_full()
                            .bg(p.text_placeholder),
                    )
                    .child(v_flex().flex_1().min_w_0().child(difference_text(mm)).when_some(
                        mm.differences.first(),
                        |d, first| {
                            d.child(
                                div()
                                    .text_size(px(12.))
                                    .text_color(p.text_muted)
                                    .child(first.clone()),
                            )
                        },
                    ))
            })))
            .child(
                h_flex()
                    .gap(px(12.))
                    .pt(px(12.))
                    .border_t_1()
                    .border_color(p.border_variant)
                    .child(
                        div()
                            .flex_1()
                            .text_size(px(13.))
                            .text_color(p.text_muted)
                            .child(note),
                    )
                    .when(auto > 0, |d| {
                        d.child(
                            Button::new("mp-fix")
                                .primary()
                                .small()
                                .label("Fix & join")
                                .loading(fixing)
                                .disabled(fixing)
                                .on_click(cx.listener(|this, _, _, cx| this.mp_fix(cx))),
                        )
                    }),
            )
    }

    fn member_row(&self, m: &MemberState, phase: &Phase, first: bool, p: Palette) -> AnyElement {
        let word = member_status(m, phase);
        h_flex()
            .px(px(16.))
            .py(px(12.))
            .gap(px(12.))
            .when(!first, |d| d.border_t_1().border_color(p.border_variant))
            .child(self.face(&m.name, m.avatar.as_deref(), 32., p))
            .child(
                h_flex()
                    .flex_1()
                    .min_w_0()
                    .gap(px(8.))
                    .child(
                        div()
                            .text_size(px(14.))
                            .font_weight(FontWeight::MEDIUM)
                            .truncate()
                            .child(m.name.clone()),
                    )
                    .when(m.host, |d| d.child(tag("Host", p)))
                    .when(m.me, |d| d.child(tag("You", p))),
            )
            .child(
                div()
                    .text_size(px(13.))
                    .text_color(if word == "Addons differ" {
                        p.warning
                    } else {
                        p.text_muted
                    })
                    .child(word),
            )
            .into_any_element()
    }
}

#[cfg(test)]
mod tests {
    use swofty_core::multiplayer::game::{GameStatus, Role, Screen};
    use swofty_core::multiplayer::rooms::{RoomAddons, RoomSummary, User};
    use swofty_core::multiplayer::session::{
        FixStep, MemberState, Phase, PrintMatch, RelayStatus, SessionState,
    };
    use swofty_core::profile::ProfileAddon;

    use super::{
        GameState, RoomStatus, Tone, fixable, member_status, reorder, retry_offered, room_meta, status_text,
        tracks_text,
    };

    fn status(screen: Screen) -> GameStatus {
        GameStatus {
            screen,
            in_session: false,
            role: Role::None,
            configured: false,
            self_member: 0,
            base: 0,
        }
    }

    fn room(members: u32, max: u32, digest: Option<&str>) -> RoomSummary {
        RoomSummary {
            code: "F1-7KQ2MX".into(),
            name: "Sunday league".into(),
            host: User {
                id: "u1".into(),
                name: "swofty".into(),
                avatar: None,
            },
            members,
            max_members: max,
            category: "f1-2026".into(),
            digest: digest.map(str::to_owned),
            addons: Some(RoomAddons {
                count: 4,
                tracks: Vec::new(),
            }),
            created_at: 0,
        }
    }

    fn session(phase: Phase, host: bool) -> SessionState {
        SessionState {
            phase,
            host,
            room: None,
            self_member: None,
            members: Vec::new(),
            mismatches: Vec::new(),
            lobby_connected: true,
            relay: None,
            base: None,
            bridge: None,
            game: None,
            last_error: None,
        }
    }

    fn member(connected: bool, print: PrintMatch, relay: RelayStatus) -> MemberState {
        MemberState {
            id: "u2".into(),
            name: "Max".into(),
            avatar: None,
            number: 2,
            ip: "10.77.0.2".into(),
            host: false,
            me: false,
            connected,
            fingerprint: print,
            relay,
            traffic: None,
        }
    }

    #[test]
    fn names_where_the_game_is() {
        assert_eq!(
            GameState::of(false, Some(&status(Screen::MainMenu))),
            GameState::NotRunning
        );
        assert_eq!(
            GameState::of(true, Some(&status(Screen::MainMenu))),
            GameState::MainMenu
        );
        assert_eq!(
            GameState::of(true, Some(&status(Screen::LanLobby))),
            GameState::LanLobby
        );
        assert_eq!(GameState::of(true, Some(&status(Screen::Other))), GameState::Busy);
        assert_eq!(GameState::of(true, None), GameState::Busy);
        assert_eq!(GameState::MainMenu.not_ready(), None);
        assert_eq!(
            GameState::NotRunning.not_ready(),
            Some("Start F1 25 to create or join a room.")
        );
    }

    #[test]
    fn gives_each_room_one_status() {
        assert_eq!(
            RoomStatus::of(&room(8, 8, Some("sha256:a")), Some("sha256:a")),
            RoomStatus::Full
        );
        assert_eq!(
            RoomStatus::of(&room(3, 8, Some("sha256:ABC")), Some("sha256:abc")),
            RoomStatus::Match
        );
        assert_eq!(
            RoomStatus::of(&room(3, 8, Some("sha256:a")), Some("sha256:b")),
            RoomStatus::Differ
        );
        assert_eq!(
            RoomStatus::of(&room(3, 8, None), Some("sha256:b")),
            RoomStatus::Unknown
        );
        assert_eq!(RoomStatus::Differ.text(), "Your addons differ");
    }

    #[test]
    fn describes_a_room() {
        assert_eq!(room_meta(&room(3, 8, None)), "swofty \u{00b7} 3/8 players");
        let tracks = ["Okayama", "Bahrain 2024", "Monza 1990", "Imola"].map(String::from);
        assert_eq!(
            tracks_text(&tracks).as_deref(),
            Some("Okayama, Bahrain 2024, Monza 1990 and 1 more")
        );
        assert_eq!(tracks_text(&[]), None);
    }

    #[test]
    fn words_the_room_for_the_player() {
        let refused = session(
            Phase::WaitingForGame(
                "the game didn't host the LAN session: timed out; retry or leave the room".into(),
            ),
            true,
        );
        let (tone, text, _) = status_text(&refused);
        assert_eq!(tone, Tone::Blocked);
        assert_eq!(
            text,
            "Couldn't start hosting. Go back to F1 25's main menu and try again."
        );
        let (_, text, _) = status_text(&session(Phase::Connecting("opening the relay".into()), false));
        assert_eq!(text, "Joining the room\u{2026}");
        let (tone, _, hint) = status_text(&session(Phase::InLobby, true));
        assert_eq!(tone, Tone::Good);
        assert!(hint.unwrap().contains("tracks"));
        let (_, text, _) = status_text(&session(Phase::Ended("the host closed the room".into()), false));
        assert_eq!(text, "The host closed the room");
    }

    #[test]
    fn gives_each_player_one_word() {
        let lobby = Phase::InLobby;
        assert_eq!(
            member_status(&member(true, PrintMatch::Matches, RelayStatus::Ready), &lobby),
            "Ready"
        );
        assert_eq!(
            member_status(&member(true, PrintMatch::Differs, RelayStatus::Ready), &lobby),
            "Addons differ"
        );
        assert_eq!(
            member_status(&member(false, PrintMatch::Matches, RelayStatus::Ready), &lobby),
            "Disconnected"
        );
        assert_eq!(
            member_status(&member(true, PrintMatch::Unknown, RelayStatus::Pending), &lobby),
            "Joining\u{2026}"
        );
    }

    #[test]
    fn splits_fixes_from_manual_steps() {
        let steps = vec![
            FixStep::Disable {
                id: "a".into(),
                name: "A".into(),
            },
            FixStep::Manual {
                line: "game build 1 is not the host's 2".into(),
            },
        ];
        assert_eq!(
            fixable(&steps),
            (1, vec!["Game build 1 is not the host's 2".to_owned()])
        );
    }

    #[test]
    fn puts_the_shared_addons_in_the_hosts_order() {
        let addon = |id: &str| ProfileAddon {
            id: id.into(),
            enabled: true,
            unknown: Default::default(),
        };
        let mut addons = vec![addon("core"), addon("b"), addon("x"), addon("a")];
        reorder(&["a".into(), "b".into()], &mut addons);
        let ids: Vec<&str> = addons.iter().map(|a| a.id.as_str()).collect();
        assert_eq!(ids, ["core", "a", "x", "b"]);
    }

    #[test]
    fn offers_retry_when_the_game_refused_but_not_for_addons() {
        let refused = session(
            Phase::WaitingForGame(
                "the game didn't join the LAN session: busy; retry or leave the room".into(),
            ),
            false,
        );
        assert!(retry_offered(&refused));
        assert!(!retry_offered(&session(Phase::InLobby, false)));
        assert!(!retry_offered(&session(
            Phase::Ended("you left the room".into()),
            false
        )));
        let mut failing = session(Phase::Connecting("opening the relay".into()), false);
        failing.last_error = Some("the relay refused".into());
        assert!(retry_offered(&failing));
    }
}
