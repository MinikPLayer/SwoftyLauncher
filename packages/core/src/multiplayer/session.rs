//! One multiplayer room per launcher: the server lobby, the TURN relay, the netplay bridge and
//! the game's LAN session, driven by a worker thread. The UI polls [`Session::state`].
//!
//! Creating: the game must be on its main menu; the launcher creates the room, opens the lobby
//! websocket, mints TURN credentials and allocates a relay, announces its relayed address, starts
//! the bridge (which picks `BASE`) with the room key, then tells the game `configure {self, base}`
//! and `host {category}`. Joining is the same with `join 10.77.0.1`, once this player's
//! fingerprint matches the host's and the host's relayed address is known. Members who join later
//! get a bridge port and a relay permission as the lobby reports them; when the TURN allocation
//! dies, a fresh one replaces it under the bridge, so the game keeps its `BASE`.

use std::collections::{BTreeMap, HashMap};
use std::net::{Ipv4Addr, SocketAddr};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, mpsc};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use serde::Serialize;
use serde_json::{Value, json};
use zeroize::Zeroize;

use super::bridge::{self, Bridge, BridgeConfig, MemberStats, Stats};
use super::diag;
use super::direct::DirectUdp;
use super::fingerprint::{self, Fingerprint, Mismatch, MismatchKind};
use super::game::{self, Category, Command, GameStatus, Screen};
use super::rooms::{Api, ApiError, Lobby, RoomKey, RoomSettings, User};
use super::transport::Transport;
use super::turn::TurnTransport;
use super::ws::{self, ConnectError};
use crate::baseline::Identity;
use crate::catalog::{self, Addon};
use crate::profile::Profile;
use crate::store::Store;

/// The host's virtual IP, which every joiner's game joins.
pub const HOST_IP: Ipv4Addr = Ipv4Addr::new(10, 77, 0, 1);

const TICK: Duration = Duration::from_millis(25);
const HOUSEKEEPING: Duration = Duration::from_secs(1);
const GAME_RETRY: Duration = Duration::from_secs(1);
/// The longest a status round trip, `leave`, `clear` or `cancel` may take, so leaving a room
/// never hangs on a game that stopped answering.
const QUICK_TIMEOUT: Duration = Duration::from_secs(2);
/// How long leaving waits for the lobby connection's thread before leaving it behind.
const SOCKET_STOP: Duration = Duration::from_secs(1);
const WS_CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
const WS_BACKOFF_MAX: Duration = Duration::from_secs(10);
const RELAY_BACKOFF: Duration = Duration::from_secs(5);
const RELAY_BACKOFF_MAX: Duration = Duration::from_secs(60);
const JOIN_SLOW: Duration = Duration::from_secs(30);
const STATS_EVERY: Duration = Duration::from_secs(30);

static ACTIVE: Mutex<Option<std::sync::Weak<Inner>>> = Mutex::new(None);

/// The game's netplay command channel, as the session uses it.
pub trait GameControl: Send + Sync {
    fn status(&self) -> Result<GameStatus, game::Error>;
    fn configure(&self, self_member: u8, base: u16) -> Result<GameStatus, game::Error>;
    fn host(&self, category: Category) -> Result<GameStatus, game::Error>;
    fn join(&self, ip: Ipv4Addr) -> Result<GameStatus, game::Error>;
    fn leave(&self) -> Result<GameStatus, game::Error>;
    fn clear(&self) -> Result<GameStatus, game::Error>;
    /// End a running host or join at its next step; it then answers with an error.
    fn cancel(&self) -> Result<GameStatus, game::Error>;
}

/// The running game's file mailbox (`<game>/swofty/netplay`).
pub struct Mailbox {
    dir: PathBuf,
}

impl Mailbox {
    pub fn new(game_dir: &Path) -> Self {
        Self {
            dir: game_dir.to_path_buf(),
        }
    }
}

impl GameControl for Mailbox {
    fn status(&self) -> Result<GameStatus, game::Error> {
        match game::last_status(&self.dir) {
            Some(status) => Ok(status),
            None => game::send(&self.dir, &Command::Status, QUICK_TIMEOUT).map(|r| r.status),
        }
    }

    fn configure(&self, self_member: u8, base: u16) -> Result<GameStatus, game::Error> {
        game::configure(&self.dir, self_member, base)
    }

    fn host(&self, category: Category) -> Result<GameStatus, game::Error> {
        game::host(&self.dir, category)
    }

    fn join(&self, ip: Ipv4Addr) -> Result<GameStatus, game::Error> {
        game::join(&self.dir, ip)
    }

    fn leave(&self) -> Result<GameStatus, game::Error> {
        game::send(&self.dir, &Command::Leave, QUICK_TIMEOUT).map(|r| r.status)
    }

    fn clear(&self) -> Result<GameStatus, game::Error> {
        game::send(&self.dir, &Command::Clear, QUICK_TIMEOUT).map(|r| r.status)
    }

    fn cancel(&self) -> Result<GameStatus, game::Error> {
        game::send(&self.dir, &Command::Cancel, QUICK_TIMEOUT).map(|r| r.status)
    }
}

/// How the session reaches the other members.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Relay {
    /// A Cloudflare TURN allocation from `POST /v1/mp/turn`.
    Turn,
    /// Plain UDP on this address, announced as the relay: for tests and a server on this machine
    /// without TURN.
    Direct(SocketAddr),
}

/// This player's fingerprint, the blob the lobby shows the others, and the Addon Browser listing
/// the comparison uses to tell what a fix can install.
#[derive(Debug, Clone)]
pub struct Local {
    pub fingerprint: Fingerprint,
    pub blob: Value,
    pub catalog: Vec<Addon>,
}

impl Local {
    /// `categories` maps addon id to its category, carried in the blob for display only.
    pub fn new(fingerprint: Fingerprint, categories: &BTreeMap<String, String>, catalog: Vec<Addon>) -> Self {
        let blob = blob(&fingerprint, categories);
        Self {
            fingerprint,
            blob,
            catalog,
        }
    }

    /// The fingerprint of `profile` on the game `game`, with the cached Addon Browser listing.
    pub fn read(store: &Store, profile: &Profile, game: &Identity) -> crate::Result<Local> {
        let fingerprint = fingerprint::fingerprint(store, profile, game)?;
        let installed = store.addons()?;
        let categories = fingerprint
            .addons
            .iter()
            .filter_map(|a| {
                let category = installed.get(&a.id)?.category?;
                let name = serde_json::to_value(category).ok()?.as_str()?.to_owned();
                Some((a.id.clone(), name))
            })
            .collect();
        let catalog = catalog::cached(&store.root).unwrap_or_default();
        Ok(Self::new(fingerprint, &categories, catalog))
    }

    /// The digest the lobby compares.
    pub fn digest(&self) -> String {
        format!("sha256:{}", self.fingerprint.digest)
    }
}

/// The largest blob a session sends; the server takes up to 64 KB.
pub const BLOB_LIMIT: usize = 60 << 10;

/// The fingerprint as JSON with each addon's `category` added, outside the digest, kept under
/// [`BLOB_LIMIT`] by [`trim_blob`].
pub fn blob(fingerprint: &Fingerprint, categories: &BTreeMap<String, String>) -> Value {
    let mut doc = serde_json::to_value(fingerprint).expect("serialisable");
    if let Some(addons) = doc.get_mut("addons").and_then(Value::as_array_mut) {
        for addon in addons {
            let category = addon
                .get("id")
                .and_then(Value::as_str)
                .and_then(|id| categories.get(id))
                .cloned();
            if let (Some(obj), Some(category)) = (addon.as_object_mut(), category) {
                obj.insert("category".into(), Value::String(category));
            }
        }
    }
    let dropped = trim_blob(&mut doc, BLOB_LIMIT);
    if !dropped.is_empty() {
        diag::line(format!(
            "fingerprint: per-file hashes left out for {} to keep it under {} KB",
            dropped.join(", "),
            BLOB_LIMIT >> 10
        ));
    }
    doc
}

/// Drops the per-file hashes of the addons that read the most files, one addon at a time,
/// until `doc` serialises to at most `limit` bytes; part hashes stay. Returns the ids it
/// dropped them from.
pub fn trim_blob(doc: &mut Value, limit: usize) -> Vec<String> {
    let size = |d: &Value| serde_json::to_vec(d).map_or(0, |v| v.len());
    let reads = |a: &Value| {
        a.pointer("/detail/reads")
            .and_then(Value::as_array)
            .map_or(0, Vec::len)
    };
    let mut dropped = Vec::new();
    while size(doc) > limit {
        let Some(addons) = doc.get_mut("addons").and_then(Value::as_array_mut) else {
            break;
        };
        let Some(i) = (0..addons.len())
            .filter(|&i| reads(&addons[i]) > 0)
            .max_by_key(|&i| (reads(&addons[i]), std::cmp::Reverse(i)))
        else {
            break;
        };
        if let Some(detail) = addons[i].get_mut("detail").and_then(Value::as_object_mut) {
            detail.remove("reads");
        }
        dropped.push(
            addons[i]
                .get("id")
                .and_then(Value::as_str)
                .unwrap_or("?")
                .to_owned(),
        );
    }
    dropped
}

/// Everything a session needs.
pub struct Setup {
    pub api: Api,
    pub game_dir: PathBuf,
    pub game: Arc<dyn GameControl>,
    pub local: Local,
    pub relay: Relay,
}

impl Setup {
    /// A TURN-relayed session driving the game in `game_dir` through its mailbox.
    pub fn new(api: Api, game_dir: &Path, local: Local) -> Self {
        Self {
            api,
            game_dir: game_dir.to_path_buf(),
            game: Arc::new(Mailbox::new(game_dir)),
            local,
            relay: Relay::Turn,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(tag = "phase", content = "reason", rename_all = "snake_case")]
pub enum Phase {
    /// Signing in, opening the room, the relay or the bridge; the text names the step.
    Connecting(String),
    /// The game is not where the next step needs it, or the addons don't match the host's.
    WaitingForGame(String),
    InLobby,
    Racing,
    /// The room is over for this player; the text says why.
    Ended(String),
    Failed(String),
}

impl Phase {
    pub fn is_over(&self) -> bool {
        matches!(self, Phase::Ended(_) | Phase::Failed(_))
    }
}

impl std::fmt::Display for Phase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Phase::Connecting(step) => write!(f, "connecting: {step}"),
            Phase::WaitingForGame(why) => write!(f, "waiting for the game: {why}"),
            Phase::InLobby => f.write_str("in the lobby"),
            Phase::Racing => f.write_str("racing"),
            Phase::Ended(why) => write!(f, "ended: {why}"),
            Phase::Failed(why) => write!(f, "failed: {why}"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct RoomInfo {
    pub code: String,
    pub name: String,
    pub public: bool,
    pub category: String,
    pub max_members: u32,
    pub started: bool,
    /// The host's user id.
    pub host: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PrintMatch {
    Unknown,
    Matches,
    Differs,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RelayStatus {
    /// No relayed address yet.
    None,
    /// Announced; the relay has not accepted a permission for it yet.
    Pending,
    Ready,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct MemberState {
    pub id: String,
    pub name: String,
    pub avatar: Option<String>,
    /// The member number: the last octet of `ip`.
    pub number: u8,
    pub ip: String,
    pub host: bool,
    pub me: bool,
    pub connected: bool,
    pub fingerprint: PrintMatch,
    pub relay: RelayStatus,
    /// Game traffic with this member through the bridge.
    pub traffic: Option<MemberStats>,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct SessionState {
    pub phase: Phase,
    /// This launcher created the room.
    pub host: bool,
    pub room: Option<RoomInfo>,
    pub self_member: Option<u8>,
    pub members: Vec<MemberState>,
    /// What this player must change to match the host; empty when they match or before the
    /// host's fingerprint arrived.
    pub mismatches: Vec<Mismatch>,
    /// The lobby websocket is open.
    pub lobby_connected: bool,
    /// This launcher's relayed address.
    pub relay: Option<String>,
    pub base: Option<u16>,
    pub bridge: Option<Stats>,
    pub game: Option<GameStatus>,
    pub last_error: Option<String>,
}

impl SessionState {
    fn new(host: bool) -> Self {
        Self {
            phase: Phase::Connecting("starting".into()),
            host,
            room: None,
            self_member: None,
            members: Vec::new(),
            mismatches: Vec::new(),
            lobby_connected: false,
            relay: None,
            base: None,
            bridge: None,
            game: None,
            last_error: None,
        }
    }
}

/// One step of making this player match the host.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(tag = "action", rename_all = "snake_case")]
pub enum FixStep {
    /// Install this exact version from the Addon Browser; `sha256` is the host's download.
    Install {
        id: String,
        name: String,
        version: semver::Version,
        sha256: Option<String>,
    },
    Disable {
        id: String,
        name: String,
    },
    SetOption {
        addon: String,
        option: String,
        value: Option<Value>,
    },
    Reorder {
        order: Vec<String>,
    },
    /// Not something the launcher can change on its own.
    Manual {
        line: String,
    },
}

/// The steps that make a player match the host, from [`SessionState::mismatches`]. Nothing is
/// installed or changed here.
pub fn fix_plan(mismatches: &[Mismatch]) -> Vec<FixStep> {
    mismatches
        .iter()
        .map(|m| match (&m.kind, m.fixable) {
            (
                MismatchKind::MissingAddon {
                    id,
                    name,
                    version,
                    registry,
                    ..
                },
                true,
            ) => FixStep::Install {
                id: id.clone(),
                name: name.clone(),
                version: version.clone(),
                sha256: registry.clone(),
            },
            (
                MismatchKind::VersionDiffers {
                    id,
                    name,
                    host,
                    registry,
                    ..
                },
                true,
            ) => FixStep::Install {
                id: id.clone(),
                name: name.clone(),
                version: host.clone(),
                sha256: registry.clone(),
            },
            (
                MismatchKind::SourceDiffers {
                    id,
                    name,
                    version,
                    registry,
                    ..
                },
                true,
            ) => FixStep::Install {
                id: id.clone(),
                name: name.clone(),
                version: version.clone(),
                sha256: registry.clone(),
            },
            (MismatchKind::ExtraAddon { id, name, .. }, true) => FixStep::Disable {
                id: id.clone(),
                name: name.clone(),
            },
            (
                MismatchKind::OptionDiffers {
                    addon, option, host, ..
                },
                true,
            ) => FixStep::SetOption {
                addon: addon.clone(),
                option: option.clone(),
                value: host.clone(),
            },
            (MismatchKind::LoadOrder { host, .. }, true) => FixStep::Reorder { order: host.clone() },
            _ => FixStep::Manual { line: m.line() },
        })
        .collect()
}

enum Intent {
    Create(RoomSettings),
    Join(String),
}

enum Action {
    Race(bool),
    Retry,
    Reconnect,
    Local(Box<Local>),
}

struct Inner {
    state: Mutex<SessionState>,
    stop: AtomicBool,
}

/// The launcher's one active room. Dropping it leaves the room like [`Session::leave`].
pub struct Session {
    inner: Arc<Inner>,
    actions: mpsc::Sender<Action>,
    worker: Option<JoinHandle<()>>,
}

impl Session {
    /// Create a room and host it in the game. Returns at once; progress shows in [`Session::state`].
    pub fn create(setup: Setup, settings: RoomSettings) -> Session {
        Self::spawn(setup, Intent::Create(settings))
    }

    /// Join the room with invite code `code`. Returns at once; progress shows in
    /// [`Session::state`].
    pub fn join(setup: Setup, code: &str) -> Session {
        Self::spawn(setup, Intent::Join(code.trim().to_owned()))
    }

    fn spawn(setup: Setup, intent: Intent) -> Session {
        let host = matches!(intent, Intent::Create(_));
        diag::secret("session-token", &[setup.api.token()]);
        diag::line(format!(
            "session start: {}, launcher {}, relay {}, fingerprint {}",
            match &intent {
                Intent::Create(r) => format!(
                    "create room (public {}, max {}, category {})",
                    r.public, r.max_members, r.category
                ),
                Intent::Join(code) => format!("join room {code}"),
            },
            crate::update::current_version(),
            match setup.relay {
                Relay::Turn => "turn".to_owned(),
                Relay::Direct(a) => format!("direct {a}"),
            },
            diag::digest_prefix(&setup.local.digest()),
        ));
        let inner = Arc::new(Inner {
            state: Mutex::new(SessionState::new(host)),
            stop: AtomicBool::new(false),
        });
        *lock(&ACTIVE) = Some(Arc::downgrade(&inner));
        let (actions, inbox) = mpsc::channel();
        let worker = Worker::new(inner.clone(), setup, intent, inbox);
        let spawned = thread::Builder::new()
            .name("mp-session".into())
            .spawn(move || worker.run());
        let worker = match spawned {
            Ok(handle) => Some(handle),
            Err(e) => {
                lock(&inner.state).phase = Phase::Failed(format!("couldn't start the session thread: {e}"));
                None
            }
        };
        Session {
            inner,
            actions,
            worker,
        }
    }

    pub fn state(&self) -> SessionState {
        lock(&self.inner.state).clone()
    }

    /// Host only: mark the room's race started (hidden from the room list) or over.
    pub fn mark_race(&self, started: bool) {
        let _ = self.actions.send(Action::Race(started));
    }

    /// Drive the game again: after it left the LAN session, or after the game refused or failed
    /// to host or join.
    pub fn retry(&self) {
        let _ = self.actions.send(Action::Retry);
    }

    /// Replace the relay now, as when the network changed.
    pub fn reconnect_relay(&self) {
        let _ = self.actions.send(Action::Reconnect);
    }

    /// Use a fresh fingerprint, as after Fix & join and a game restart: the lobby gets it, the
    /// comparison with the host's runs again, and a joiner that now matches goes on to join.
    pub fn update_local(&self, local: Local) {
        let _ = self.actions.send(Action::Local(Box::new(local)));
    }

    /// The steps that would make this player match the host.
    pub fn fix_plan(&self) -> Vec<FixStep> {
        fix_plan(&self.state().mismatches)
    }

    /// Start leaving and return at once: the worker cancels a running host or join, takes the
    /// game out of its LAN session, clears its room settings, stops the bridge and the relay and
    /// leaves the room on the server, each step giving up after a few seconds. When it is done
    /// the phase is [`Phase::Ended`] (or [`Phase::Failed`]) and [`Session::is_finished`] is true.
    pub fn leave(&self) {
        if !self.inner.stop.swap(true, Ordering::Relaxed) {
            diag::line("session: leave requested");
        }
    }

    /// The worker has stopped: everything is torn down.
    pub fn is_finished(&self) -> bool {
        self.worker.as_ref().is_none_or(JoinHandle::is_finished)
    }
}

/// The state of the session this process runs, while one exists.
pub fn active_state() -> Option<SessionState> {
    let inner = lock(&ACTIVE).as_ref()?.upgrade()?;
    Some(lock(&inner.state).clone())
}

impl Drop for Session {
    /// Starts leaving like [`Session::leave`] and lets the worker finish on its own.
    fn drop(&mut self) {
        self.leave();
    }
}

enum Halt {
    /// Not yet: the step named, retried shortly.
    Step(String, Duration),
    /// The game is not ready for the next step, or the addons differ; retried every second.
    Game(String),
    /// Something went wrong that trying again can fix.
    Retry(String, Duration),
    Fail(String),
}

enum RelayLink {
    Turn(Arc<TurnTransport>),
    Direct(Arc<DirectUdp>),
}

impl RelayLink {
    fn transport(&self) -> Arc<dyn Transport> {
        match self {
            RelayLink::Turn(t) => t.clone(),
            RelayLink::Direct(d) => d.clone(),
        }
    }

    fn needs_reconnect(&self) -> bool {
        match self {
            RelayLink::Turn(t) => t.needs_reconnect(),
            RelayLink::Direct(_) => false,
        }
    }
}

struct Worker {
    inner: Arc<Inner>,
    setup: Setup,
    intent: Intent,
    actions: mpsc::Receiver<Action>,
    events: mpsc::Receiver<WsEvent>,
    events_tx: mpsc::Sender<WsEvent>,
    phase: Phase,
    me: Option<User>,
    code: Option<String>,
    lobby: Option<Lobby>,
    prints: HashMap<String, Value>,
    socket: Option<LobbySocket>,
    lobby_connected: bool,
    relay: Option<RelayLink>,
    relay_retry: Instant,
    relay_backoff: Duration,
    peers: HashMap<u8, SocketAddr>,
    key: Option<[u8; 32]>,
    bridge: Option<Bridge>,
    stats: Option<Stats>,
    game_status: Option<GameStatus>,
    configured: bool,
    driven: Option<Instant>,
    entering: Option<mpsc::Receiver<Result<GameStatus, game::Error>>>,
    seen_in_session: bool,
    paused: Option<String>,
    mismatches: Vec<Mismatch>,
    left_stale: bool,
    over: bool,
    teardown: bool,
    server_gone: bool,
    last_error: Option<String>,
    next_step: Instant,
    next_housekeeping: Instant,
    failures: u32,
    logged: std::cell::RefCell<Option<SessionState>>,
    print_note: String,
    prints_saved: String,
    next_stats: Instant,
    game_port: Option<u16>,
}

impl Worker {
    fn new(inner: Arc<Inner>, setup: Setup, intent: Intent, actions: mpsc::Receiver<Action>) -> Self {
        let (events_tx, events) = mpsc::channel();
        let now = Instant::now();
        Self {
            inner,
            setup,
            intent,
            actions,
            events,
            events_tx,
            phase: Phase::Connecting("starting".into()),
            me: None,
            code: None,
            lobby: None,
            prints: HashMap::new(),
            socket: None,
            lobby_connected: false,
            relay: None,
            relay_retry: now,
            relay_backoff: RELAY_BACKOFF,
            peers: HashMap::new(),
            key: None,
            bridge: None,
            stats: None,
            game_status: None,
            configured: false,
            driven: None,
            entering: None,
            seen_in_session: false,
            paused: None,
            mismatches: Vec::new(),
            left_stale: false,
            over: false,
            teardown: false,
            server_gone: false,
            last_error: None,
            next_step: now,
            next_housekeeping: now,
            failures: 0,
            logged: std::cell::RefCell::new(None),
            print_note: String::new(),
            prints_saved: String::new(),
            next_stats: now + STATS_EVERY,
            game_port: None,
        }
    }

    fn hosting(&self) -> bool {
        matches!(self.intent, Intent::Create(_))
    }

    fn run(mut self) {
        while !self.inner.stop.load(Ordering::Relaxed) && !self.teardown {
            self.handle_actions();
            self.handle_events();
            let now = Instant::now();
            if now >= self.next_housekeeping {
                self.housekeeping();
                self.next_housekeeping = now + HOUSEKEEPING;
            }
            if !self.over && now >= self.next_step {
                self.step();
            }
            self.publish();
            thread::sleep(TICK);
        }
        let user_left = !self.over;
        diag::line(format!(
            "session: tearing down ({})",
            if user_left {
                "the player left"
            } else {
                "the room ended"
            }
        ));
        self.cleanup();
        if user_left {
            self.phase = Phase::Ended("you left the room".into());
        }
        self.publish();
        diag::line("session: finished");
    }

    fn step(&mut self) {
        let now = Instant::now();
        match self.advance() {
            Ok(()) => {
                self.failures = 0;
                let started = self.lobby.as_ref().is_some_and(Lobby::started);
                self.phase = if started { Phase::Racing } else { Phase::InLobby };
            }
            Err(Halt::Step(step, after)) => {
                self.phase = Phase::Connecting(step);
                self.next_step = now + after;
            }
            Err(Halt::Game(why)) => {
                self.phase = Phase::WaitingForGame(why);
                let after = if self.entering.is_some() {
                    TICK * 4
                } else {
                    GAME_RETRY
                };
                self.next_step = now + after;
            }
            Err(Halt::Retry(why, after)) => {
                self.failures += 1;
                self.last_error = Some(why);
                self.next_step = now + after;
            }
            Err(Halt::Fail(why)) => {
                self.phase = Phase::Failed(why);
                self.over = true;
                self.teardown = true;
            }
        }
    }

    fn backoff(&self) -> Duration {
        Duration::from_secs(1 << self.failures.min(5)).min(Duration::from_secs(30))
    }

    fn advance(&mut self) -> Result<(), Halt> {
        if self.code.is_none() {
            self.require_main_menu()?;
            self.open_room()?;
        }
        if self.relay.is_none() {
            let link = self.new_relay()?;
            self.install_relay(link);
        }
        if self.bridge.is_none() {
            self.start_bridge()?;
        }
        if self.driven.is_none() {
            if let Some(why) = &self.paused {
                return Err(Halt::Game(why.clone()));
            }
            self.drive_game()?;
        }
        Ok(())
    }

    fn require_main_menu(&mut self) -> Result<GameStatus, Halt> {
        let status = match self.setup.game.status() {
            Ok(s) => s,
            Err(e) => {
                self.game_status = None;
                return Err(Halt::Game(format!(
                    "start F1 25 from the launcher with multiplayer on ({e})"
                )));
            }
        };
        self.game_status = Some(status.clone());
        if status.in_session {
            return Err(Halt::Game(
                "the game is already in a LAN session; leave it in the game".into(),
            ));
        }
        if status.screen != Screen::MainMenu {
            return Err(Halt::Game("go to the game's main menu".into()));
        }
        Ok(status)
    }

    fn api_halt(&self, e: &ApiError) -> Halt {
        if e.transient() {
            let after = e.retry_after.map_or(self.backoff(), Duration::from_secs);
            Halt::Retry(e.message.clone(), after)
        } else {
            Halt::Fail(e.message.clone())
        }
    }

    fn open_room(&mut self) -> Result<(), Halt> {
        if self.me.is_none() {
            self.phase = Phase::Connecting("signing in".into());
            self.publish();
            let me = self.setup.api.me().map_err(|e| {
                diag::line(format!("server: sign-in check failed: {e}"));
                self.api_halt(&e)
            })?;
            self.me = Some(me);
        }
        let opened = match &self.intent {
            Intent::Create(settings) => {
                self.phase = Phase::Connecting("creating the room".into());
                self.publish();
                self.setup.api.create(settings)
            }
            Intent::Join(code) => {
                self.phase = Phase::Connecting(format!("joining {code}"));
                self.publish();
                self.setup.api.join(code)
            }
        };
        let lobby = match opened {
            Ok(lobby) => lobby,
            Err(e) if e.reason == "in_lobby" && !self.left_stale => {
                self.left_stale = true;
                diag::line(format!(
                    "server: still in an earlier room {}; leaving it",
                    e.lobby.as_deref().unwrap_or("?")
                ));
                if let Some(old) = &e.lobby {
                    let _ = self.setup.api.leave(old);
                }
                return Err(Halt::Step("leaving an earlier room".into(), Duration::ZERO));
            }
            Err(e) => {
                diag::line(format!(
                    "server: opening the room failed: {e} (reason {}, status {})",
                    e.reason, e.status
                ));
                return Err(self.api_halt(&e));
            }
        };
        diag::line(format!(
            "room {} opened: {} member(s), category {}, max {}",
            lobby.code,
            lobby.members.len(),
            lobby.category,
            lobby.max_members
        ));
        let url = self
            .setup
            .api
            .ws_url(&lobby.code)
            .map_err(|e| Halt::Fail(e.message))?;
        self.code = Some(lobby.code.clone());
        let hello = vec![self.fingerprint_message().to_string()];
        self.apply_lobby(lobby);
        match LobbySocket::spawn(
            url,
            self.setup.api.token().to_owned(),
            hello,
            self.events_tx.clone(),
        ) {
            Ok(socket) => self.socket = Some(socket),
            Err(e) => return Err(Halt::Fail(format!("couldn't start the lobby connection: {e}"))),
        }
        Ok(())
    }

    fn fingerprint_message(&self) -> Value {
        json!({
            "type": "fingerprint",
            "digest": self.setup.local.digest(),
            "blob": self.setup.local.blob,
        })
    }

    fn relay_message(&self) -> Option<Value> {
        let link = self.relay.as_ref()?;
        Some(json!({ "type": "relay", "relay": link.transport().local_addr().to_string() }))
    }

    fn new_relay(&mut self) -> Result<RelayLink, Halt> {
        self.phase = Phase::Connecting("connecting to the relay".into());
        self.publish();
        match self.setup.relay {
            Relay::Direct(addr) => DirectUdp::bind(addr)
                .map(|d| RelayLink::Direct(Arc::new(d)))
                .map_err(|e| Halt::Fail(format!("couldn't bind {addr}: {e}"))),
            Relay::Turn => {
                let creds = self.setup.api.turn().map_err(|e| {
                    diag::line(format!("server: TURN credentials refused: {e}"));
                    self.api_halt(&e)
                })?;
                diag::secret("turn", &[&creds.username, &creds.credential]);
                diag::line(format!(
                    "turn: credentials minted for {} (expires_at {:?})",
                    creds.urls.join(", "),
                    creds.expires_at
                ));
                TurnTransport::connect(&creds.urls, &creds.username, &creds.credential)
                    .map(|t| RelayLink::Turn(Arc::new(t)))
                    .map_err(|e| {
                        diag::line(format!("turn: no allocation: {e}"));
                        Halt::Retry(format!("couldn't reach the relay: {e}"), RELAY_BACKOFF)
                    })
            }
        }
    }

    /// Use `link` from now on: give it the members, hand it to the bridge, and announce it.
    fn install_relay(&mut self, link: RelayLink) {
        let transport = link.transport();
        diag::line(format!(
            "relay installed: relayed {}, peers {}",
            transport.local_addr(),
            peer_list(&self.peers)
        ));
        if let Err(e) = transport.set_peers(&self.peers) {
            diag::line(format!("relay peers: {e}"));
            self.last_error = Some(e.to_string());
        }
        if let Some(bridge) = &self.bridge {
            bridge.replace_transport(transport);
        }
        self.relay = Some(link);
        if let Some(msg) = self.relay_message() {
            self.announce(&msg);
        }
    }

    /// Send `msg` now, and keep the fingerprint and relay messages for every reconnect.
    fn announce(&self, msg: &Value) {
        if let Some(socket) = &self.socket {
            let mut hello = vec![self.fingerprint_message().to_string()];
            hello.extend(self.relay_message().map(|r| r.to_string()));
            socket.set_hello(hello);
            socket.send(msg);
        }
    }

    fn update_local(&mut self, local: Local) {
        self.setup.local = local;
        self.announce(&self.fingerprint_message());
        if !self.hosting() {
            self.compare_with_host();
        }
        self.next_step = Instant::now();
    }

    fn self_number(&self) -> Option<u8> {
        let me = self.me.as_ref()?;
        self.lobby.as_ref()?.member(&me.id)?.number()
    }

    fn start_bridge(&mut self) -> Result<(), Halt> {
        let Some(key) = self.key else {
            return Err(Halt::Step(
                "waiting for the room key".into(),
                Duration::from_millis(200),
            ));
        };
        let me = self
            .self_number()
            .ok_or_else(|| Halt::Fail("the server gave this player no virtual IP".into()))?;
        let members = self.member_numbers();
        let transport = self
            .relay
            .as_ref()
            .map(RelayLink::transport)
            .expect("relay first");
        let cfg = BridgeConfig {
            self_member: me,
            members,
            key,
            game_port: bridge::DEFAULT_GAME_PORT,
        };
        let bridge = Bridge::start(cfg, transport).map_err(|e| {
            diag::line(format!("bridge: couldn't start: {e}"));
            Halt::Fail(format!("couldn't start the netplay bridge: {e}"))
        })?;
        diag::line(format!(
            "bridge started: self {me}, members {:?}, base {}",
            bridge.members(),
            bridge.base()
        ));
        self.bridge = Some(bridge);
        Ok(())
    }

    fn member_numbers(&self) -> Vec<u8> {
        self.lobby
            .iter()
            .flat_map(|l| &l.members)
            .filter_map(|m| m.number())
            .collect()
    }

    /// Configure the game's room, then host or join on a thread of its own: the game answers
    /// those only once its LAN lobby is up (the host first picks a car category), and the lobby
    /// and bridge must keep up with members meanwhile.
    fn drive_game(&mut self) -> Result<(), Halt> {
        let what = if self.hosting() { "host" } else { "join" };
        if let Some(pending) = &self.entering {
            let answer = match pending.try_recv() {
                Ok(answer) => answer,
                Err(mpsc::TryRecvError::Empty) => {
                    return Err(Halt::Game(if self.hosting() {
                        "pick the car category in the game to open the LAN lobby".into()
                    } else {
                        "joining the host's LAN lobby in the game".into()
                    }));
                }
                Err(mpsc::TryRecvError::Disconnected) => Err(game::Error::Timeout(Duration::ZERO)),
            };
            self.entering = None;
            return match answer {
                Ok(status) => {
                    self.game_status = Some(status);
                    self.driven = Some(Instant::now());
                    self.seen_in_session = false;
                    Ok(())
                }
                Err(e) => {
                    let (why, status) = refused(e);
                    self.game_status = status.or(self.game_status.take());
                    let why =
                        format!("the game didn't {what} the LAN session: {why}; retry or leave the room");
                    self.paused = Some(why.clone());
                    Err(Halt::Game(why))
                }
            };
        }
        if !self.hosting() {
            self.check_host()?;
        }
        self.require_main_menu()?;
        let me = self.self_number().expect("bridge started");
        let base = self.bridge.as_ref().map(Bridge::base).expect("bridge started");
        self.configured = true;
        match self.setup.game.configure(me, base) {
            Ok(status) => self.game_status = Some(status),
            Err(e) => {
                let (why, status) = refused(e);
                self.game_status = status.or(self.game_status.take());
                return Err(Halt::Game(format!(
                    "the game didn't take the room settings: {why}"
                )));
            }
        }
        let (tx, rx) = mpsc::channel();
        let game = self.setup.game.clone();
        let command = if self.hosting() { None } else { Some(HOST_IP) };
        let category = self.category();
        let spawned = thread::Builder::new().name("mp-game".into()).spawn(move || {
            let answer = match command {
                None => game.host(category),
                Some(ip) => game.join(ip),
            };
            let _ = tx.send(answer);
        });
        if let Err(e) = spawned {
            return Err(Halt::Retry(
                format!("couldn't start the game command: {e}"),
                GAME_RETRY,
            ));
        }
        self.entering = Some(rx);
        Err(Halt::Step(format!("asking the game to {what}"), Duration::ZERO))
    }

    fn category(&self) -> Category {
        let from_lobby = self.lobby.as_ref().and_then(|l| l.category.parse().ok());
        match &self.intent {
            Intent::Create(settings) => from_lobby.unwrap_or(settings.category),
            Intent::Join(_) => from_lobby.unwrap_or(Category::F12026),
        }
    }

    /// A joiner may join in-game once its fingerprint matches the host's and the host's relay is
    /// installed.
    fn check_host(&mut self) -> Result<(), Halt> {
        let host = self.lobby.as_ref().and_then(Lobby::host_member).cloned();
        let Some(host) = host else {
            return Err(Halt::Step(
                "waiting for the host".into(),
                Duration::from_millis(200),
            ));
        };
        match &host.digest {
            None => return Err(Halt::Game("waiting for the host's addon list".into())),
            Some(d) if *d != self.setup.local.digest() => {
                self.compare_with_host();
                return Err(Halt::Game(
                    "your addons don't match the host's; fix them to join".into(),
                ));
            }
            Some(_) => self.mismatches.clear(),
        }
        let ready = host.number().is_some_and(|n| self.permitted(n));
        if !ready {
            return Err(Halt::Step(
                "waiting for the host's relay".into(),
                Duration::from_millis(200),
            ));
        }
        Ok(())
    }

    /// The relay currently lets datagrams through to `member`; asked each time, since TURN
    /// retries refused permissions in the background.
    fn permitted(&self, member: u8) -> bool {
        self.peers.contains_key(&member)
            && self
                .relay
                .as_ref()
                .is_some_and(|r| r.transport().permitted(member))
    }

    fn compare_with_host(&mut self) {
        let Some(lobby) = &self.lobby else { return };
        let host_digest = lobby.host_member().and_then(|h| h.digest.clone());
        if host_digest.as_deref() == Some(self.setup.local.digest().as_str()) {
            self.mismatches.clear();
            self.note_print(host_digest.as_deref(), true);
            return;
        }
        let host_blob = self.prints.get(&lobby.host).cloned();
        let host_print = host_blob
            .as_ref()
            .and_then(|blob| serde_json::from_value::<Fingerprint>(blob.clone()).ok());
        let compared = host_print.is_some();
        self.mismatches = match host_print {
            Some(host) => {
                fingerprint::compare(&self.setup.local.fingerprint, &host, &self.setup.local.catalog)
            }
            None => Vec::new(),
        };
        self.note_print(host_digest.as_deref(), compared);
        if let Some(blob) = host_blob
            && !self.mismatches.is_empty()
        {
            self.save_comparison(&blob, host_digest.as_deref());
        }
    }

    /// Keeps both sides' fingerprints and what differs in `state/multiplayer-prints`, once per
    /// pair of digests, and logs each named difference.
    fn save_comparison(&mut self, host_blob: &Value, host_digest: Option<&str>) {
        let key = format!("{} {}", self.setup.local.digest(), host_digest.unwrap_or("?"));
        if key == self.prints_saved {
            return;
        }
        self.prints_saved = key;
        for m in &self.mismatches {
            let id = serde_json::to_value(&m.kind)
                .ok()
                .and_then(|v| Some(v.get("id")?.as_str()?.to_owned()))
                .unwrap_or_default();
            for d in &m.differences {
                diag::line(format!("fingerprint: {id}: {d}"));
            }
        }
        let local_paths: BTreeMap<&str, &Vec<String>> = self
            .setup
            .local
            .fingerprint
            .addons
            .iter()
            .filter_map(|a| Some((a.id.as_str(), &a.detail.as_ref()?.paths)))
            .collect();
        let room = self.code.clone().unwrap_or_else(|| "room".into());
        let name = format!("{room}-{}", diag::file_stamp());
        diag::save_prints(
            &name,
            &json!({
                "room": room,
                "local": self.setup.local.blob,
                "local_paths": local_paths,
                "host": host_blob,
                "mismatches": self.mismatches,
            }),
        );
        diag::line(format!(
            "fingerprint: both fingerprints saved as {}/{name}.json",
            diag::PRINTS_DIR
        ));
    }

    fn note_print(&mut self, host: Option<&str>, compared: bool) {
        let local = self.setup.local.digest();
        let note = match host {
            None => format!(
                "fingerprint: local {}, host's not known yet",
                diag::digest_prefix(&local)
            ),
            Some(h) if h == local => format!(
                "fingerprint: local {} matches the host",
                diag::digest_prefix(&local)
            ),
            Some(h) if !compared => format!(
                "fingerprint: local {} differs from host {}; the host's addon list hasn't arrived",
                diag::digest_prefix(&local),
                diag::digest_prefix(h)
            ),
            Some(h) => format!(
                "fingerprint: local {} differs from host {}: {} mismatch(es): {}",
                diag::digest_prefix(&local),
                diag::digest_prefix(h),
                self.mismatches.len(),
                mismatch_list(&self.mismatches)
            ),
        };
        if note != self.print_note {
            diag::line(&note);
            self.print_note = note;
        }
    }

    fn handle_actions(&mut self) {
        while let Ok(action) = self.actions.try_recv() {
            match action {
                Action::Race(started) if self.hosting() => {
                    diag::line(format!(
                        "session: race marked {}",
                        if started { "started" } else { "over" }
                    ));
                    if let Some(socket) = &self.socket {
                        socket.send(&json!({ "type": if started { "start" } else { "reopen" } }));
                    }
                }
                Action::Race(_) => {
                    self.last_error = Some("only the host marks a race started or over".into());
                }
                Action::Retry => {
                    diag::line("session: retry requested");
                    self.paused = None;
                    self.driven = None;
                    self.next_step = Instant::now();
                }
                Action::Reconnect => {
                    diag::line("relay: reconnect requested");
                    self.reconnect_relay();
                }
                Action::Local(local) => {
                    diag::line(format!(
                        "session: fresh fingerprint {}",
                        diag::digest_prefix(&format!("sha256:{}", local.fingerprint.digest))
                    ));
                    self.update_local(*local);
                }
            }
        }
    }

    fn handle_events(&mut self) {
        while let Ok(event) = self.events.try_recv() {
            match event {
                WsEvent::Connected => {
                    self.lobby_connected = true;
                }
                WsEvent::Disconnected(why) => {
                    self.lobby_connected = false;
                    self.last_error = Some(why);
                }
                WsEvent::Gone(why) => {
                    self.lobby_connected = false;
                    self.end(why, false);
                }
                WsEvent::Message(msg) => self.handle_message(msg),
            }
        }
    }

    fn handle_message(&mut self, msg: Value) {
        let kind = msg.get("type").and_then(Value::as_str).unwrap_or("");
        match kind {
            "lobby" => {
                let Some(lobby) = msg
                    .get("lobby")
                    .and_then(|l| serde_json::from_value::<Lobby>(l.clone()).ok())
                else {
                    return;
                };
                if self.lobby.as_ref().is_some_and(|l| lobby.rev < l.rev) {
                    return;
                }
                self.apply_lobby(lobby);
            }
            "fingerprint" => {
                let (Some(member), Some(blob)) = (msg.get("member").and_then(Value::as_str), msg.get("blob"))
                else {
                    return;
                };
                diag::line(format!("lobby: fingerprint from user {member}"));
                self.prints.insert(member.to_owned(), blob.clone());
                if self.lobby.as_ref().is_some_and(|l| l.host == member) && !self.hosting() {
                    self.compare_with_host();
                }
            }
            "start" => {
                diag::line("lobby: 'start' message");
                if let Some(key) = msg.get("key").and_then(Value::as_str) {
                    self.take_key(&RoomKey::from(key));
                }
            }
            "error" => {
                let text = msg
                    .get("error")
                    .and_then(Value::as_str)
                    .unwrap_or("the lobby refused a message");
                diag::line(format!("lobby: error message: {text}"));
                self.last_error = Some(text.to_owned());
            }
            "kicked" | "left" | "replaced" | "closed" => {
                diag::line(format!(
                    "lobby: '{kind}' message{}",
                    msg.get("reason")
                        .and_then(Value::as_str)
                        .map(|r| format!(", reason {r}"))
                        .unwrap_or_default()
                ));
                self.handle_end(kind, &msg);
            }
            _ => {}
        }
    }

    fn handle_end(&mut self, kind: &str, msg: &Value) {
        match kind {
            "kicked" => self.end("the host removed you from the room".into(), true),
            "left" => self.end("you left the room".into(), true),
            "replaced" => self.end("the room was opened in another launcher".into(), true),
            "closed" => {
                let reason = msg.get("reason").and_then(Value::as_str).unwrap_or("");
                let why = match reason {
                    "host_left" => "the host closed the room",
                    "host_gone" => "the host lost their connection",
                    "idle" => "the room closed after being idle",
                    "shutdown" => "the multiplayer server restarted",
                    _ => "the room closed",
                };
                self.end(why.into(), false);
            }
            _ => {}
        }
    }

    /// The room is over for this player. With `teardown` the game, bridge and relay stop now;
    /// otherwise they keep running (a race can go on without the lobby server) until
    /// [`Session::leave`].
    fn end(&mut self, why: String, teardown: bool) {
        if !self.over {
            self.phase = Phase::Ended(why);
            self.over = true;
        }
        self.server_gone = true;
        self.teardown |= teardown;
    }

    fn take_key(&mut self, key: &RoomKey) {
        let Some(mut bytes) = key.bytes() else {
            self.last_error = Some("the server sent a room key that isn't 32 bytes".into());
            return;
        };
        match self.key {
            Some(current) if current != bytes => {
                diag::line("room key: the server sent a different key; kept the first");
                self.last_error = Some("the room key changed; leave and rejoin the room".into());
            }
            Some(_) => {}
            None => {
                diag::line("room key: received");
                self.key = Some(bytes);
            }
        }
        bytes.zeroize();
    }

    fn apply_lobby(&mut self, lobby: Lobby) {
        if let Some(key) = &lobby.key {
            self.take_key(key);
        }
        let me = self.me.as_ref().map(|u| u.id.clone());
        let peers: HashMap<u8, SocketAddr> = lobby
            .members
            .iter()
            .filter(|m| Some(&m.id) != me.as_ref())
            .filter_map(|m| Some((m.number()?, m.relay.as_deref()?.parse().ok()?)))
            .collect();
        let numbers: Vec<u8> = lobby.members.iter().filter_map(|m| m.number()).collect();
        let host_digest_changed = self.lobby.as_ref().and_then(|l| l.host_member()?.digest.clone())
            != lobby.host_member().and_then(|h| h.digest.clone());
        self.lobby = Some(lobby);
        if peers != self.peers {
            diag::line(format!("relay peers: {}", peer_list(&peers)));
            self.peers = peers;
            if let Some(link) = &self.relay
                && let Err(e) = link.transport().set_peers(&self.peers)
            {
                diag::line(format!("relay peers: {e}"));
                self.last_error = Some(e.to_string());
            }
        }
        if let Some(bridge) = &self.bridge {
            for n in numbers {
                match bridge.add_member(n) {
                    Ok(true) => diag::line(format!(
                        "bridge: port {} opened for member {n}",
                        bridge.port(n).map_or_else(|| "?".to_owned(), |p| p.to_string())
                    )),
                    Ok(false) => {}
                    Err(e) => {
                        diag::line(format!("bridge: couldn't open the port for member {n}: {e}"));
                        self.last_error = Some(format!("couldn't open the bridge port for member {n}: {e}"));
                    }
                }
            }
        }
        if host_digest_changed && !self.hosting() {
            self.compare_with_host();
        }
    }

    fn housekeeping(&mut self) {
        if let Some(bridge) = &self.bridge {
            let port = bridge.sync_game_port(&self.setup.game_dir);
            if port.is_some() && port != self.game_port {
                diag::line(format!(
                    "bridge: game port {} from swofty/netplay.json",
                    port.unwrap_or_default()
                ));
                self.game_port = port;
            }
            let stats = bridge.stats();
            if Instant::now() >= self.next_stats {
                diag::line(stats_line(&stats));
                self.next_stats = Instant::now() + STATS_EVERY;
            }
            self.stats = Some(stats);
        }
        if self.relay.as_ref().is_some_and(RelayLink::needs_reconnect) && Instant::now() >= self.relay_retry {
            diag::line("relay: the allocation is unusable; reconnecting");
            self.reconnect_relay();
        }
        if let Some(driven) = self.driven {
            match self.setup.game.status() {
                Ok(status) => {
                    if status.in_session {
                        self.seen_in_session = true;
                    } else if self.seen_in_session && !self.over {
                        self.driven = None;
                        self.paused = Some("the game left the LAN session; rejoin or leave the room".into());
                    } else if driven.elapsed() > JOIN_SLOW && !self.seen_in_session {
                        self.last_error = Some("the game hasn't reached the LAN lobby yet".into());
                    }
                    self.game_status = Some(status);
                }
                Err(e) => self.last_error = Some(format!("the game isn't answering: {e}")),
            }
        }
    }

    /// Mint fresh credentials, allocate a new relay, give it the members, swap it in under the
    /// bridge and announce the new relayed address. The bridge keeps its ports, so the game is
    /// untouched.
    fn reconnect_relay(&mut self) {
        if self.relay.is_none() || self.over {
            return;
        }
        let phase = self.phase.clone();
        let fresh = self.new_relay();
        self.phase = phase;
        match fresh {
            Ok(link) => {
                self.install_relay(link);
                self.relay_backoff = RELAY_BACKOFF;
            }
            Err(Halt::Fail(why) | Halt::Retry(why, _) | Halt::Step(why, _) | Halt::Game(why)) => {
                diag::line(format!(
                    "relay: reconnect failed: {why}; next try in {}s",
                    self.relay_backoff.as_secs()
                ));
                self.last_error = Some(format!("the relay dropped and reconnecting failed: {why}"));
                self.relay_retry = Instant::now() + self.relay_backoff;
                self.relay_backoff = (self.relay_backoff * 2).min(RELAY_BACKOFF_MAX);
            }
        }
    }

    /// Every game command here gives up after [`QUICK_TIMEOUT`] and the server call after a few
    /// seconds, so leaving finishes even with the game or the network gone.
    fn cleanup(&mut self) {
        if let Some(pending) = self.entering.take() {
            if let Err(e) = self.setup.game.cancel() {
                self.last_error = Some(format!("the game didn't cancel its host or join: {e}"));
            }
            if let Ok(Ok(status)) = pending.recv_timeout(QUICK_TIMEOUT) {
                self.game_status = Some(status);
            }
        }
        if self.configured || self.driven.is_some() {
            let in_session = self.setup.game.status().is_ok_and(|s| s.in_session);
            if in_session && let Err(e) = self.setup.game.leave() {
                self.last_error = Some(format!("the game didn't leave the LAN session: {e}"));
            }
        }
        if self.configured
            && let Err(e) = self.setup.game.clear()
        {
            self.last_error = Some(format!("the game didn't clear the room settings: {e}"));
        }
        self.configured = false;
        self.driven = None;
        if let Some(bridge) = self.bridge.take() {
            let stats = bridge.stats();
            diag::line(format!("bridge stopping, final {}", stats_line(&stats)));
            self.stats = Some(stats);
            bridge.stop();
        }
        self.relay = None;
        drop(self.socket.take());
        self.lobby_connected = false;
        if !self.server_gone
            && let Some(code) = &self.code
        {
            match self.setup.api.leave(code) {
                Ok(_) => diag::line(format!("room {code}: left on the server")),
                Err(e) if e.status == 404 => diag::line(format!("room {code}: already gone on the server")),
                Err(e) => {
                    diag::line(format!("room {code}: couldn't leave on the server: {e}"));
                    self.last_error = Some(format!("couldn't leave the room on the server: {e}"));
                }
            }
        }
    }

    fn publish(&self) {
        let me = self.me.as_ref().map(|u| u.id.as_str());
        let lobby = self.lobby.as_ref();
        let members = lobby
            .map(|l| {
                l.members
                    .iter()
                    .filter_map(|m| {
                        let number = m.number()?;
                        let is_me = Some(m.id.as_str()) == me;
                        let fingerprint = match (&m.digest, m.matches) {
                            (None, _) => PrintMatch::Unknown,
                            (Some(_), true) => PrintMatch::Matches,
                            (Some(_), false) => PrintMatch::Differs,
                        };
                        let relay = match (&m.relay, is_me) {
                            (None, _) => RelayStatus::None,
                            (Some(_), true) => RelayStatus::Ready,
                            (Some(_), false) if self.permitted(number) => RelayStatus::Ready,
                            (Some(_), false) => RelayStatus::Pending,
                        };
                        Some(MemberState {
                            id: m.id.clone(),
                            name: m.name.clone(),
                            avatar: m.avatar.clone(),
                            number,
                            ip: m.ip.clone(),
                            host: m.host || m.id == l.host,
                            me: is_me,
                            connected: m.connected,
                            fingerprint,
                            relay,
                            traffic: self.stats.as_ref().and_then(|s| s.members.get(&number).copied()),
                        })
                    })
                    .collect()
            })
            .unwrap_or_default();
        let state = SessionState {
            phase: self.phase.clone(),
            host: self.hosting(),
            room: lobby.map(|l| RoomInfo {
                code: l.code.clone(),
                name: l.name.clone(),
                public: l.public,
                category: l.category.clone(),
                max_members: l.max_members,
                started: l.started(),
                host: l.host.clone(),
            }),
            self_member: self.self_number(),
            members,
            mismatches: self.mismatches.clone(),
            lobby_connected: self.lobby_connected,
            relay: self
                .relay
                .as_ref()
                .map(|r| r.transport().local_addr().to_string()),
            base: self.bridge.as_ref().map(Bridge::base),
            bridge: self.stats.clone(),
            game: self.game_status.clone(),
            last_error: self.last_error.clone(),
        };
        let mut logged = self.logged.borrow_mut();
        if logged.as_ref() != Some(&state) {
            log_changes(logged.as_ref(), &state);
            *logged = Some(state.clone());
        }
        *lock(&self.inner.state) = state;
    }
}

impl Drop for Worker {
    fn drop(&mut self) {
        self.key.zeroize();
    }
}

enum WsEvent {
    Connected,
    Message(Value),
    Disconnected(String),
    /// The lobby is gone for this player; no reconnect.
    Gone(String),
}

/// The lobby websocket on its own thread: reconnects after drops, and sends `hello` (the
/// fingerprint and relay messages) on every connect.
struct LobbySocket {
    out: mpsc::Sender<String>,
    hello: Arc<Mutex<Vec<String>>>,
    stop: Arc<AtomicBool>,
    thread: Option<JoinHandle<()>>,
}

impl LobbySocket {
    fn spawn(
        url: String,
        token: String,
        hello: Vec<String>,
        events: mpsc::Sender<WsEvent>,
    ) -> std::io::Result<LobbySocket> {
        let (out, outbox) = mpsc::channel();
        let hello = Arc::new(Mutex::new(hello));
        let stop = Arc::new(AtomicBool::new(false));
        let thread = {
            let (hello, stop) = (hello.clone(), stop.clone());
            thread::Builder::new()
                .name("mp-lobby".into())
                .spawn(move || lobby_loop(&url, &token, &hello, &outbox, &events, &stop))?
        };
        Ok(LobbySocket {
            out,
            hello,
            stop,
            thread: Some(thread),
        })
    }

    fn send(&self, msg: &Value) {
        let _ = self.out.send(msg.to_string());
    }

    fn set_hello(&self, hello: Vec<String>) {
        *lock(&self.hello) = hello;
    }
}

impl Drop for LobbySocket {
    /// Waits up to [`SOCKET_STOP`] for the thread, which may be stuck connecting, then leaves
    /// it to finish on its own.
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        let Some(t) = self.thread.take() else { return };
        let deadline = Instant::now() + SOCKET_STOP;
        while !t.is_finished() && Instant::now() < deadline {
            thread::sleep(Duration::from_millis(10));
        }
        if t.is_finished() {
            let _ = t.join();
        }
    }
}

fn lobby_loop(
    url: &str,
    token: &str,
    hello: &Mutex<Vec<String>>,
    outbox: &mpsc::Receiver<String>,
    events: &mpsc::Sender<WsEvent>,
    stop: &AtomicBool,
) {
    let mut backoff = Duration::from_secs(1);
    let mut pending: Option<String> = None;
    let mut attempt = 0u32;
    while !stop.load(Ordering::Relaxed) {
        attempt += 1;
        let started = Instant::now();
        match ws::Socket::connect(url, token, WS_CONNECT_TIMEOUT) {
            Ok(mut socket) => {
                diag::line(format!(
                    "lobby: connected in {} ms{}",
                    started.elapsed().as_millis(),
                    if attempt > 1 {
                        format!(" (attempt {attempt})")
                    } else {
                        String::new()
                    }
                ));
                attempt = 0;
                let since = Instant::now();
                backoff = Duration::from_secs(1);
                let _ = events.send(WsEvent::Connected);
                let greeting = lock(hello).clone();
                let mut alive = greeting.iter().all(|h| socket.send_text(h).is_ok());
                while alive && !stop.load(Ordering::Relaxed) {
                    while let Some(text) = pending.take().or_else(|| outbox.try_recv().ok()) {
                        if socket.send_text(&text).is_err() {
                            pending = Some(text);
                            alive = false;
                            break;
                        }
                    }
                    if !alive {
                        break;
                    }
                    match socket.read() {
                        Ok(None) => {}
                        Ok(Some(ws::Event::Text(text))) => {
                            if let Ok(msg) = serde_json::from_str::<Value>(&text) {
                                let _ = events.send(WsEvent::Message(msg));
                            }
                        }
                        Ok(Some(ws::Event::Closed { code, .. })) => {
                            diag::line(format!(
                                "lobby: closed by the server with code {} after {}s",
                                code.map_or_else(|| "none".to_owned(), |c| c.to_string()),
                                since.elapsed().as_secs()
                            ));
                            if let Some(why) = final_close(code) {
                                let _ = events.send(WsEvent::Gone(why.into()));
                                return;
                            }
                            alive = false;
                        }
                        Err(e) => {
                            diag::line(format!(
                                "lobby: connection dropped after {}s: {e}",
                                since.elapsed().as_secs()
                            ));
                            let _ = events.send(WsEvent::Disconnected(format!(
                                "the lobby connection dropped: {e}"
                            )));
                            alive = false;
                        }
                    }
                }
                if stop.load(Ordering::Relaxed) {
                    diag::line("lobby: closing the connection (leaving)");
                    socket.close();
                    return;
                }
                if pending.is_some() {
                    diag::line("lobby: a send failed; reconnecting");
                }
                let _ = events.send(WsEvent::Disconnected(
                    "the lobby connection closed; reconnecting".into(),
                ));
            }
            Err(ConnectError::Refused { status, body }) if matches!(status, 401 | 403 | 404) => {
                let why = if status == 404 {
                    "the room closed".to_owned()
                } else {
                    ApiError::from_body(status, &body).message
                };
                diag::line(format!(
                    "lobby: refused with HTTP {status}: {why}; not reconnecting"
                ));
                let _ = events.send(WsEvent::Gone(why));
                return;
            }
            Err(e) => {
                diag::line(format!(
                    "lobby: connect attempt {attempt} failed after {} ms: {e}; retry in {}s",
                    started.elapsed().as_millis(),
                    backoff.as_secs()
                ));
                let _ = events.send(WsEvent::Disconnected(format!("couldn't reach the lobby: {e}")));
            }
        }
        let until = Instant::now() + backoff;
        while Instant::now() < until && !stop.load(Ordering::Relaxed) {
            thread::sleep(Duration::from_millis(50));
        }
        backoff = (backoff * 2).min(WS_BACKOFF_MAX);
    }
}

fn peer_list(peers: &HashMap<u8, SocketAddr>) -> String {
    if peers.is_empty() {
        return "none".into();
    }
    let mut list: Vec<_> = peers.iter().collect();
    list.sort();
    list.iter()
        .map(|(m, a)| format!("{m}={a}"))
        .collect::<Vec<_>>()
        .join(" ")
}

fn mismatch_list(mismatches: &[Mismatch]) -> String {
    mismatches
        .iter()
        .map(|m| {
            let v = serde_json::to_value(&m.kind).unwrap_or_default();
            let kind = v.get("kind").and_then(Value::as_str).unwrap_or("?");
            let fix = if m.fixable { "" } else { " (not fixable)" };
            let source = |side: &str| {
                v.get(side)
                    .and_then(Value::as_object)
                    .and_then(|o| o.keys().next().cloned())
            };
            let sources = match (source("local"), source("host")) {
                (Some(l), Some(h)) if kind == "source_differs" => format!(" (yours {l}, host's {h})"),
                _ => String::new(),
            };
            match v.get("id").and_then(Value::as_str) {
                Some(id) => format!("{kind} {id}{sources}{fix}"),
                None => format!("{kind}{fix}"),
            }
        })
        .collect::<Vec<_>>()
        .join(", ")
}

pub fn stats_line(stats: &Stats) -> String {
    let members = if stats.members.is_empty() {
        "no members".to_owned()
    } else {
        stats
            .members
            .iter()
            .map(|(n, m)| {
                format!(
                    "member {n} in {}p/{}B out {}p/{}B",
                    m.packets_in, m.bytes_in, m.packets_out, m.bytes_out
                )
            })
            .collect::<Vec<_>>()
            .join("; ")
    };
    let drops = stats.drops.nonzero();
    let drops = if drops.is_empty() {
        "none".to_owned()
    } else {
        drops
            .iter()
            .map(|(k, v)| format!("{k}={v}"))
            .collect::<Vec<_>>()
            .join(" ")
    };
    format!(
        "bridge stats: {members}; drops {drops}; large {}; foreign_from {}",
        stats.large,
        stats
            .foreign_from
            .map_or_else(|| "none".to_owned(), |a| a.to_string())
    )
}

fn log_changes(old: Option<&SessionState>, new: &SessionState) {
    if old.map(|o| &o.phase) != Some(&new.phase) {
        diag::line(format!("phase: {}", new.phase));
    }
    let old_room = old.and_then(|o| o.room.as_ref());
    match (old_room, &new.room) {
        (None, Some(r)) => diag::line(format!(
            "room {}: category {}, public {}, max {}, started {}, {}",
            r.code,
            r.category,
            r.public,
            r.max_members,
            r.started,
            if new.host { "hosting" } else { "joined" }
        )),
        (Some(a), Some(b)) if a.started != b.started => {
            diag::line(format!("room {}: started {}", b.code, b.started));
        }
        (Some(a), None) => diag::line(format!("room {}: no longer shown", a.code)),
        _ => {}
    }
    if old.and_then(|o| o.self_member) != new.self_member
        && let Some(n) = new.self_member
    {
        diag::line(format!("self: member {n} (10.77.0.{n})"));
    }
    let before: BTreeMap<u8, &MemberState> = old
        .map(|o| o.members.iter().map(|m| (m.number, m)).collect())
        .unwrap_or_default();
    let after: BTreeMap<u8, &MemberState> = new.members.iter().map(|m| (m.number, m)).collect();
    for (n, m) in &after {
        match before.get(n) {
            None => diag::line(format!(
                "member {n} joined: ip {}, host {}, me {}, connected {}, fingerprint {:?}, relay {:?}",
                m.ip, m.host, m.me, m.connected, m.fingerprint, m.relay
            )),
            Some(b) => {
                if b.connected != m.connected {
                    diag::line(format!("member {n}: connected {}", m.connected));
                }
                if b.fingerprint != m.fingerprint {
                    diag::line(format!("member {n}: fingerprint {:?}", m.fingerprint));
                }
                if b.relay != m.relay {
                    diag::line(format!("member {n}: relay {:?}", m.relay));
                }
                if b.host != m.host {
                    diag::line(format!("member {n}: host {}", m.host));
                }
            }
        }
    }
    for (n, b) in &before {
        if !after.contains_key(n) {
            diag::line(format!("member {n} left (ip {})", b.ip));
        }
    }
    if old.map(|o| &o.relay) != Some(&new.relay)
        && let Some(r) = &new.relay
    {
        diag::line(format!("relay: relayed address {r}"));
    }
    if old.and_then(|o| o.base) != new.base
        && let Some(b) = new.base
    {
        diag::line(format!("bridge: base {b}"));
    }
    if old.and_then(|o| o.game.as_ref()) != new.game.as_ref()
        && let Some(g) = &new.game
    {
        diag::line(format!("game status: {}", game::status_line(g)));
    }
    if old.and_then(|o| o.last_error.as_ref()) != new.last_error.as_ref()
        && let Some(e) = &new.last_error
    {
        diag::line(format!("error: {e}"));
    }
}

/// The session state for a sent logs bundle: member avatars left out, secrets redacted.
pub fn snapshot(state: &SessionState) -> Value {
    let mut v = serde_json::to_value(state).unwrap_or_default();
    if let Some(members) = v.get_mut("members").and_then(Value::as_array_mut) {
        for m in members {
            if let Some(obj) = m.as_object_mut() {
                obj.remove("avatar");
            }
        }
    }
    crate::send_logs::redact_json(&mut v);
    v
}

/// Close codes after which the lobby is gone for this player.
fn final_close(code: Option<u16>) -> Option<&'static str> {
    match code? {
        4000 => Some("the room was opened in another launcher"),
        4001 => Some("the host removed you from the room"),
        4002 => Some("the room closed"),
        4003 => Some("you left the room"),
        1001 => Some("the multiplayer server restarted"),
        _ => None,
    }
}

fn refused(e: game::Error) -> (String, Option<GameStatus>) {
    match e {
        game::Error::Refused { why, status, .. } => (why, Some(status)),
        e => (e.to_string(), None),
    }
}

fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

#[cfg(test)]
mod tests {
    use std::net::UdpSocket;

    use semver::Version;

    use super::super::fingerprint::{AddonPrint, Source};
    use super::super::game::Role;
    use super::super::testserver::FakeServer;
    use super::*;

    const WAIT: Duration = Duration::from_secs(10);

    struct FakeGame {
        dir: PathBuf,
        socket: UdpSocket,
        status: Mutex<GameStatus>,
        log: Mutex<Vec<String>>,
        host_delay: Mutex<Duration>,
        refuse_host: AtomicBool,
        configure_fails: AtomicBool,
        cancelled: AtomicBool,
        host_attempts: std::sync::atomic::AtomicU32,
    }

    impl FakeGame {
        fn new(name: &str) -> Arc<FakeGame> {
            let dir = std::env::temp_dir().join(format!("swofty-mp-session-{name}-{}", std::process::id()));
            let _ = std::fs::remove_dir_all(&dir);
            std::fs::create_dir_all(dir.join("swofty")).unwrap();
            let socket = UdpSocket::bind("127.0.0.1:0").unwrap();
            socket.set_read_timeout(Some(Duration::from_millis(200))).unwrap();
            Arc::new(FakeGame {
                dir,
                socket,
                status: Mutex::new(GameStatus {
                    screen: Screen::MainMenu,
                    in_session: false,
                    role: Role::None,
                    configured: false,
                    self_member: 0,
                    base: 0,
                }),
                log: Mutex::new(Vec::new()),
                host_delay: Mutex::new(Duration::ZERO),
                refuse_host: AtomicBool::new(false),
                configure_fails: AtomicBool::new(false),
                cancelled: AtomicBool::new(false),
                host_attempts: std::sync::atomic::AtomicU32::new(0),
            })
        }

        fn log(&self) -> Vec<String> {
            lock(&self.log).clone()
        }

        fn set_screen(&self, screen: Screen) {
            lock(&self.status).screen = screen;
        }

        fn refuse(&self, op: &str, why: &str) -> game::Error {
            game::Error::Refused {
                op: op.into(),
                why: why.into(),
                status: lock(&self.status).clone(),
            }
        }

        fn enter(&self, op: String, role: Role) -> Result<GameStatus, game::Error> {
            let mut s = lock(&self.status);
            if !s.configured || s.screen != Screen::MainMenu || s.in_session {
                drop(s);
                return Err(self.refuse(&op, "the game is not on its main menu"));
            }
            s.screen = Screen::LanLobby;
            s.in_session = true;
            s.role = role;
            lock(&self.log).push(op);
            Ok(s.clone())
        }
    }

    impl GameControl for FakeGame {
        fn status(&self) -> Result<GameStatus, game::Error> {
            Ok(lock(&self.status).clone())
        }

        fn configure(&self, self_member: u8, base: u16) -> Result<GameStatus, game::Error> {
            let mut s = lock(&self.status);
            s.configured = true;
            s.self_member = self_member;
            s.base = base;
            let port = self.socket.local_addr().unwrap().port();
            let netplay = json!({"pid": 1, "self": self_member, "base": base, "game_port": port});
            std::fs::write(self.dir.join(bridge::STATUS_FILE), netplay.to_string()).unwrap();
            lock(&self.log).push(format!("configure {self_member} {base}"));
            if self.configure_fails.load(Ordering::Relaxed) {
                return Err(game::Error::Timeout(Duration::from_secs(10)));
            }
            Ok(s.clone())
        }

        fn host(&self, category: Category) -> Result<GameStatus, game::Error> {
            self.host_attempts.fetch_add(1, Ordering::Relaxed);
            let started = Instant::now();
            while started.elapsed() < *lock(&self.host_delay) {
                if self.cancelled.swap(false, Ordering::Relaxed) {
                    return Err(self.refuse("host", "cancelled by the launcher"));
                }
                thread::sleep(Duration::from_millis(10));
            }
            if self.refuse_host.load(Ordering::Relaxed) {
                return Err(self.refuse("host", "the player backed out of Car Category"));
            }
            self.enter(format!("host {category}"), Role::Host)
        }

        fn join(&self, ip: Ipv4Addr) -> Result<GameStatus, game::Error> {
            self.enter(format!("join {ip}"), Role::Client)
        }

        fn leave(&self) -> Result<GameStatus, game::Error> {
            let mut s = lock(&self.status);
            s.screen = Screen::MainMenu;
            s.in_session = false;
            s.role = Role::None;
            lock(&self.log).push("leave".into());
            Ok(s.clone())
        }

        fn clear(&self) -> Result<GameStatus, game::Error> {
            let mut s = lock(&self.status);
            s.configured = false;
            s.self_member = 0;
            s.base = 0;
            lock(&self.log).push("clear".into());
            Ok(s.clone())
        }

        fn cancel(&self) -> Result<GameStatus, game::Error> {
            self.cancelled.store(true, Ordering::Relaxed);
            lock(&self.log).push("cancel".into());
            Ok(lock(&self.status).clone())
        }
    }

    fn print(extra: &[&str]) -> Fingerprint {
        let addon = |id: &str| AddonPrint {
            id: id.into(),
            name: format!("{id} name"),
            version: Version::new(1, 0, 0),
            source: Source::Content("00".repeat(32)),
            registry: None,
            inputs: None,
            order: Some(0),
            detail: None,
        };
        let mut addons = vec![addon("okayama")];
        addons.extend(extra.iter().map(|id| addon(id)));
        Fingerprint::new(
            Identity {
                build_id: "1".into(),
                exe_sha256: "ab".repeat(32),
            },
            addons,
            BTreeMap::new(),
            None,
        )
    }

    fn setup(url: &str, token: &str, game: &Arc<FakeGame>, fingerprint: Fingerprint) -> Setup {
        let categories = BTreeMap::from([("okayama".to_owned(), "tracks".to_owned())]);
        Setup {
            api: Api::with_server(url, token),
            game_dir: game.dir.clone(),
            game: game.clone(),
            local: Local::new(fingerprint, &categories, Vec::new()),
            relay: Relay::Direct("127.0.0.1:0".parse().unwrap()),
        }
    }

    fn server() -> FakeServer {
        FakeServer::start(&[("host-token", "1", "Host"), ("guest-token", "2", "Ana")])
    }

    fn wait_until(session: &Session, what: &str, done: impl Fn(&SessionState) -> bool) -> SessionState {
        let deadline = Instant::now() + WAIT;
        loop {
            let state = session.state();
            if done(&state) {
                return state;
            }
            assert!(
                Instant::now() < deadline,
                "timed out waiting for {what}: {state:#?}"
            );
            thread::sleep(Duration::from_millis(20));
        }
    }

    /// A session that leaves and waits for its worker when the test lets go of it, so no worker
    /// outlives its test and sends to ports another test has taken.
    struct Finished(Session);

    impl std::ops::Deref for Finished {
        type Target = Session;

        fn deref(&self) -> &Session {
            &self.0
        }
    }

    impl Drop for Finished {
        fn drop(&mut self) {
            self.0.leave();
            let deadline = Instant::now() + WAIT;
            while !self.0.is_finished() && Instant::now() < deadline {
                thread::sleep(Duration::from_millis(10));
            }
        }
    }

    fn create(setup: Setup, settings: RoomSettings) -> Finished {
        Finished(Session::create(setup, settings))
    }

    fn join(setup: Setup, code: &str) -> Finished {
        Finished(Session::join(setup, code))
    }

    /// Leave and wait for the worker to finish; `leave` itself returns at once.
    fn leave(session: &Session) -> SessionState {
        let asked = Instant::now();
        session.leave();
        assert!(asked.elapsed() < Duration::from_millis(50), "leave doesn't block");
        let deadline = Instant::now() + WAIT;
        while !session.is_finished() {
            assert!(Instant::now() < deadline, "the session never finished leaving");
            thread::sleep(Duration::from_millis(10));
        }
        session.state()
    }

    fn in_lobby(s: &SessionState) -> bool {
        s.phase == Phase::InLobby
    }

    fn both_relays_ready(s: &SessionState) -> bool {
        s.members.len() == 2 && s.members.iter().all(|m| m.relay == RelayStatus::Ready)
    }

    /// Send from one fake game to a member's loopback port until the other fake game gets it.
    fn deliver(from: &FakeGame, port: u16, to: &FakeGame, payload: &[u8]) -> SocketAddr {
        let deadline = Instant::now() + WAIT;
        let mut buf = [0u8; 2048];
        loop {
            from.socket.send_to(payload, ("127.0.0.1", port)).unwrap();
            if let Ok((n, src)) = to.socket.recv_from(&mut buf)
                && &buf[..n] == payload
            {
                return src;
            }
            assert!(Instant::now() < deadline, "the datagram never arrived");
        }
    }

    fn base(game: &FakeGame) -> u16 {
        lock(&game.status).base
    }

    /// Let each bridge see its game's port: until the game has sent, a bridge delivers to 21414.
    fn prime(host_game: &FakeGame, guest_game: &FakeGame) -> (u16, u16) {
        thread::sleep(HOUSEKEEPING + Duration::from_millis(200));
        let (host_base, guest_base) = (base(host_game), base(guest_game));
        host_game
            .socket
            .send_to(b"prime", ("127.0.0.1", host_base + 2))
            .unwrap();
        guest_game
            .socket
            .send_to(b"prime", ("127.0.0.1", guest_base + 1))
            .unwrap();
        (host_base, guest_base)
    }

    #[test]
    fn host_and_joiner_reach_the_lobby_and_carry_game_traffic() {
        let server = server();
        let (host_game, guest_game) = (FakeGame::new("host"), FakeGame::new("guest"));
        let settings = RoomSettings {
            name: Some("Sunday league".into()),
            ..RoomSettings::default()
        };
        let host = create(setup(&server.url, "host-token", &host_game, print(&[])), settings);
        let state = wait_until(&host, "the host's lobby", in_lobby);
        let room = state.room.clone().unwrap();
        assert_eq!(
            (room.name.as_str(), room.category.as_str()),
            ("Sunday league", "f1-2026")
        );
        assert_eq!(state.self_member, Some(1));
        assert_eq!(
            host_game.log(),
            vec![
                format!("configure 1 {}", state.base.unwrap()),
                "host f1-2026".into()
            ]
        );

        let guest = join(
            setup(&server.url, "guest-token", &guest_game, print(&[])),
            &room.code,
        );
        let guest_state = wait_until(&guest, "the guest's lobby", in_lobby);
        assert_eq!(guest_state.self_member, Some(2));
        assert!(guest_state.mismatches.is_empty());
        assert_eq!(
            guest_game.log(),
            vec![
                format!("configure 2 {}", guest_state.base.unwrap()),
                "join 10.77.0.1".into()
            ]
        );
        let host_state = wait_until(&host, "the guest's relay at the host", both_relays_ready);
        assert!(
            host_state
                .members
                .iter()
                .all(|m| m.fingerprint == PrintMatch::Matches)
        );
        assert!(host_state.members.iter().any(|m| m.me && m.host && m.number == 1));
        wait_until(&guest, "the host's relay at the guest", both_relays_ready);

        let (host_base, guest_base) = prime(&host_game, &guest_game);
        let from = deliver(&guest_game, guest_base + 1, &host_game, b"hello host");
        assert_eq!(from.port(), host_base + 2);
        let from = deliver(&host_game, host_base + 2, &guest_game, b"welcome");
        assert_eq!(from.port(), guest_base + 1);

        host.mark_race(true);
        wait_until(&host, "the race at the host", |s| s.phase == Phase::Racing);
        wait_until(&guest, "the race at the guest", |s| s.phase == Phase::Racing);
        guest.mark_race(true);
        host.mark_race(false);
        wait_until(&guest, "the race's end at the guest", in_lobby);
        let traffic = wait_until(&host, "traffic stats", |s| {
            s.members
                .iter()
                .any(|m| m.traffic.is_some_and(|t| t.packets_in > 0))
        });
        assert!(traffic.bridge.is_some());

        let left = leave(&guest);
        assert_eq!(left.phase, Phase::Ended("you left the room".into()));
        let log = guest_game.log();
        assert_eq!(&log[log.len() - 2..], ["leave", "clear"]);
        wait_until(&host, "the guest gone at the host", |s| s.members.len() == 1);
        assert_eq!(server.member_count(), 1);

        leave(&host);
        assert!(!server.has_room());
        assert_eq!(host_game.log().last().unwrap(), "clear");
    }

    #[test]
    fn a_joiner_whose_addons_differ_waits_with_the_differences() {
        let server = server();
        let (host_game, guest_game) = (FakeGame::new("host-mm"), FakeGame::new("guest-mm"));
        let host = create(
            setup(&server.url, "host-token", &host_game, print(&[])),
            RoomSettings::default(),
        );
        let code = wait_until(&host, "the host's lobby", in_lobby).room.unwrap().code;
        let guest = join(
            setup(&server.url, "guest-token", &guest_game, print(&["extra"])),
            &code,
        );
        let state = wait_until(&guest, "the mismatch", |s| {
            !s.mismatches.is_empty()
                && matches!(&s.phase, Phase::WaitingForGame(why) if why.contains("match"))
        });
        assert!(matches!(&state.mismatches[0].kind, MismatchKind::ExtraAddon { id, .. } if id == "extra"));
        assert_eq!(
            guest.fix_plan(),
            vec![FixStep::Disable {
                id: "extra".into(),
                name: "extra name".into()
            }]
        );
        let host_view = wait_until(&host, "the guest's digest at the host", |s| {
            s.members
                .iter()
                .any(|m| !m.me && m.fingerprint == PrintMatch::Differs)
        });
        assert_eq!(host_view.members.len(), 2);
        assert!(!guest_game.log().iter().any(|l| l.starts_with("join")));
        leave(&guest);
        assert!(!guest_game.log().iter().any(|l| l == "leave"));
    }

    #[test]
    fn a_fresh_fingerprint_after_a_fix_lets_the_joiner_in() {
        let server = server();
        let (host_game, guest_game) = (FakeGame::new("host-fix"), FakeGame::new("guest-fix"));
        let host = create(
            setup(&server.url, "host-token", &host_game, print(&[])),
            RoomSettings::default(),
        );
        let code = wait_until(&host, "the host's lobby", in_lobby).room.unwrap().code;
        let guest = join(
            setup(&server.url, "guest-token", &guest_game, print(&["extra"])),
            &code,
        );
        wait_until(&guest, "the mismatch", |s| !s.mismatches.is_empty());
        guest.retry();
        thread::sleep(Duration::from_millis(300));
        assert!(
            !guest.state().mismatches.is_empty(),
            "retry alone can't clear a mismatch"
        );
        let categories = BTreeMap::from([("okayama".to_owned(), "tracks".to_owned())]);
        guest.update_local(Local::new(print(&[]), &categories, Vec::new()));
        let state = wait_until(&guest, "the guest's lobby", in_lobby);
        assert!(state.mismatches.is_empty());
        wait_until(&host, "the guest's new digest at the host", |s| {
            s.members.len() == 2 && s.members.iter().all(|m| m.fingerprint == PrintMatch::Matches)
        });
        assert_eq!(guest_game.log().last().unwrap(), "join 10.77.0.1");
    }

    #[test]
    fn leave_returns_at_once_and_finishes_with_the_server_stalled() {
        let server = server();
        let game = FakeGame::new("stall");
        let host = create(
            setup(&server.url, "host-token", &game, print(&[])),
            RoomSettings::default(),
        );
        wait_until(&host, "the lobby", in_lobby);
        server.stall_leave(Duration::from_secs(30));
        let started = Instant::now();
        let state = leave(&host);
        assert!(
            started.elapsed() < Duration::from_secs(8),
            "{:?}",
            started.elapsed()
        );
        assert_eq!(state.phase, Phase::Ended("you left the room".into()));
        assert!(
            state
                .last_error
                .unwrap_or_default()
                .contains("couldn't leave the room on the server")
        );
        assert_eq!(&game.log()[2..], ["leave", "clear"]);

        let other = Session::create(
            setup(&server.url, "guest-token", &FakeGame::new("drop"), print(&[])),
            RoomSettings::default(),
        );
        thread::sleep(Duration::from_millis(200));
        let dropped = Instant::now();
        drop(other);
        assert!(
            dropped.elapsed() < Duration::from_millis(50),
            "drop doesn't block"
        );
    }

    #[test]
    fn leaving_while_the_game_opens_its_lobby_cancels_and_clears() {
        let server = server();
        let game = FakeGame::new("cancel");
        *lock(&game.host_delay) = Duration::from_secs(30);
        let host = create(
            setup(&server.url, "host-token", &game, print(&[])),
            RoomSettings::default(),
        );
        wait_until(
            &host,
            "the category pick",
            |s| matches!(&s.phase, Phase::WaitingForGame(why) if why.contains("car category")),
        );
        let state = leave(&host);
        assert_eq!(state.phase, Phase::Ended("you left the room".into()));
        let log = game.log();
        assert!(log[0].starts_with("configure 1 "), "{log:?}");
        assert_eq!(&log[1..], ["cancel", "clear"]);
        assert!(!server.has_room());
    }

    #[test]
    fn a_configure_that_fails_late_is_still_cleared() {
        let server = server();
        let game = FakeGame::new("late-configure");
        game.configure_fails.store(true, Ordering::Relaxed);
        let host = create(
            setup(&server.url, "host-token", &game, print(&[])),
            RoomSettings::default(),
        );
        wait_until(
            &host,
            "the refused room settings",
            |s| matches!(&s.phase, Phase::WaitingForGame(why) if why.contains("room settings")),
        );
        leave(&host);
        assert_eq!(game.log().last().unwrap(), "clear");
        assert!(!lock(&game.status).configured);
    }

    #[test]
    fn a_refused_host_waits_for_retry() {
        let server = server();
        let game = FakeGame::new("refused");
        game.refuse_host.store(true, Ordering::Relaxed);
        let host = create(
            setup(&server.url, "host-token", &game, print(&[])),
            RoomSettings::default(),
        );
        let state = wait_until(
            &host,
            "the refusal",
            |s| matches!(&s.phase, Phase::WaitingForGame(why) if why.contains("backed out")),
        );
        assert!(matches!(&state.phase, Phase::WaitingForGame(why) if why.contains("retry")));
        thread::sleep(GAME_RETRY * 2 + Duration::from_millis(500));
        assert_eq!(
            game.host_attempts.load(Ordering::Relaxed),
            1,
            "a refusal is not re-sent on its own"
        );
        game.refuse_host.store(false, Ordering::Relaxed);
        host.retry();
        wait_until(&host, "the lobby after retry", in_lobby);
        assert_eq!(game.host_attempts.load(Ordering::Relaxed), 2);
    }

    #[test]
    fn waits_for_the_main_menu_before_creating_the_room() {
        let server = server();
        let game = FakeGame::new("menu");
        game.set_screen(Screen::Other);
        let host = create(
            setup(&server.url, "host-token", &game, print(&[])),
            RoomSettings::default(),
        );
        let state = wait_until(&host, "the main menu wait", |s| {
            matches!(s.phase, Phase::WaitingForGame(_))
        });
        assert_eq!(
            state.phase,
            Phase::WaitingForGame("go to the game's main menu".into())
        );
        thread::sleep(Duration::from_millis(300));
        assert!(!server.has_room());
        game.set_screen(Screen::MainMenu);
        wait_until(&host, "the lobby", in_lobby);
        assert!(server.has_room());
    }

    #[test]
    fn the_session_keeps_up_while_the_game_opens_its_lobby() {
        let server = server();
        let (host_game, guest_game) = (FakeGame::new("host-slow"), FakeGame::new("guest-slow"));
        *lock(&host_game.host_delay) = Duration::from_secs(60);
        let host = create(
            setup(&server.url, "host-token", &host_game, print(&[])),
            RoomSettings::default(),
        );
        let state = wait_until(
            &host,
            "the category pick",
            |s| matches!(&s.phase, Phase::WaitingForGame(why) if why.contains("car category")),
        );
        let code = state.room.unwrap().code;
        let guest = join(setup(&server.url, "guest-token", &guest_game, print(&[])), &code);
        let during = wait_until(&host, "the guest's relay while hosting", both_relays_ready);
        assert!(matches!(during.phase, Phase::WaitingForGame(_)));
        *lock(&host_game.host_delay) = Duration::ZERO;
        wait_until(&host, "the host's lobby", in_lobby);
        wait_until(&guest, "the guest's lobby", in_lobby);
    }

    #[test]
    fn the_host_closing_the_room_ends_the_joiner_session() {
        let server = server();
        let (host_game, guest_game) = (FakeGame::new("host-close"), FakeGame::new("guest-close"));
        let host = create(
            setup(&server.url, "host-token", &host_game, print(&[])),
            RoomSettings::default(),
        );
        let code = wait_until(&host, "the host's lobby", in_lobby).room.unwrap().code;
        let guest = join(setup(&server.url, "guest-token", &guest_game, print(&[])), &code);
        wait_until(&guest, "the guest's lobby", in_lobby);
        leave(&host);
        let state = wait_until(&guest, "the end", |s| s.phase.is_over());
        assert_eq!(state.phase, Phase::Ended("the host closed the room".into()));
        leave(&guest);
        let log = guest_game.log();
        assert_eq!(&log[log.len() - 2..], ["leave", "clear"]);
    }

    #[test]
    fn a_new_relay_takes_over_under_the_bridge() {
        let server = server();
        let (host_game, guest_game) = (FakeGame::new("host-relay"), FakeGame::new("guest-relay"));
        let host = create(
            setup(&server.url, "host-token", &host_game, print(&[])),
            RoomSettings::default(),
        );
        let code = wait_until(&host, "the host's lobby", in_lobby).room.unwrap().code;
        let guest = join(setup(&server.url, "guest-token", &guest_game, print(&[])), &code);
        let before = wait_until(&guest, "the guest's lobby", in_lobby);
        wait_until(&host, "the relays", both_relays_ready);
        let old_relay = before.relay.clone().unwrap();

        guest.reconnect_relay();
        let after = wait_until(&guest, "a new relay", |s| {
            s.relay.as_ref().is_some_and(|r| *r != old_relay)
        });
        assert_eq!(after.base, before.base);
        assert_eq!(guest_game.log().len(), 2, "the game is not driven again");
        let new_relay = after.relay.unwrap();
        wait_until(&host, "the new relay at the host", |s| {
            s.members.iter().any(|m| !m.me && m.relay == RelayStatus::Ready)
                && server_relay_is(&server, &new_relay)
        });

        let (host_base, guest_base) = prime(&host_game, &guest_game);
        deliver(&guest_game, guest_base + 1, &host_game, b"after the swap");
        deliver(&host_game, host_base + 2, &guest_game, b"and back");
    }

    /// The host and a joiner against a real SwoftyLauncherServer on this machine:
    /// `SWOFTY_MP_E2E_SERVER` is its URL and `SWOFTY_MP_E2E_TOKENS` two session tokens,
    /// `host,guest`, from rows in its registry's `sessions` table. The server needs
    /// `MP_ALLOW_PRIVATE_RELAY=1`, since the relays are UDP sockets on loopback.
    #[test]
    #[ignore = "needs a local SwoftyLauncherServer; see the comment"]
    fn against_a_local_lobby_server() {
        let url = std::env::var("SWOFTY_MP_E2E_SERVER").expect("SWOFTY_MP_E2E_SERVER");
        let tokens = std::env::var("SWOFTY_MP_E2E_TOKENS").expect("SWOFTY_MP_E2E_TOKENS");
        let (host_token, guest_token) = tokens.split_once(',').expect("host,guest");
        let (host_game, guest_game) = (FakeGame::new("host-e2e"), FakeGame::new("guest-e2e"));
        let host = create(
            setup(&url, host_token, &host_game, print(&[])),
            RoomSettings::default(),
        );
        let code = wait_until(&host, "the host's lobby", in_lobby).room.unwrap().code;
        let listed = Api::with_server(&url, guest_token).rooms().unwrap();
        let room = listed
            .iter()
            .find(|r| r.code == code)
            .expect("the room is listed");
        assert_eq!(
            room.addons.as_ref().map(|a| a.tracks.clone()),
            Some(vec!["okayama name".to_owned()])
        );
        let guest = join(setup(&url, guest_token, &guest_game, print(&[])), &code);
        wait_until(&guest, "the guest's lobby", in_lobby);
        wait_until(&host, "the relays at the host", both_relays_ready);
        wait_until(&guest, "the relays at the guest", both_relays_ready);
        let (host_base, guest_base) = prime(&host_game, &guest_game);
        deliver(&guest_game, guest_base + 1, &host_game, b"hello host");
        deliver(&host_game, host_base + 2, &guest_game, b"welcome");
        host.mark_race(true);
        wait_until(&guest, "the race at the guest", |s| s.phase == Phase::Racing);
        host.mark_race(false);
        wait_until(&guest, "the race's end at the guest", in_lobby);
        leave(&guest);
        wait_until(&host, "the guest gone at the host", |s| s.members.len() == 1);
        assert_eq!(leave(&host).phase, Phase::Ended("you left the room".into()));
    }

    fn server_relay_is(server: &FakeServer, relay: &str) -> bool {
        let api = Api::with_server(&server.url, "host-token");
        api.lobby("F1-TEST22")
            .is_ok_and(|l| l.member("2").and_then(|m| m.relay.clone()).as_deref() == Some(relay))
    }

    #[test]
    fn the_session_log_names_each_step_and_never_a_secret() {
        let tap = diag::tap();
        let server = FakeServer::start(&[
            ("log-host-token-1", "u-log-host", "Host"),
            ("log-guest-token-2", "u-log-guest", "Guest"),
        ]);
        let (host_game, guest_game) = (FakeGame::new("log-host"), FakeGame::new("log-guest"));
        let host = create(
            setup(&server.url, "log-host-token-1", &host_game, print(&[])),
            RoomSettings::default(),
        );
        let state = wait_until(&host, "the host's lobby", in_lobby);
        let code = state.room.unwrap().code;
        let guest = join(
            setup(&server.url, "log-guest-token-2", &guest_game, print(&[])),
            &code,
        );
        wait_until(&guest, "the guest's lobby", in_lobby);
        wait_until(&host, "the guest's relay at the host", both_relays_ready);
        let snapshot = snapshot(&guest.state()).to_string();
        leave(&guest);
        leave(&host);
        let text = tap.text();
        for want in [
            "session start: create room".to_owned(),
            format!("session start: join room {code}"),
            format!("room {code} opened"),
            "lobby: connected".to_owned(),
            "room key: received".to_owned(),
            "relay installed: relayed 127.0.0.1:".to_owned(),
            "bridge started: self 1".to_owned(),
            "phase: in the lobby".to_owned(),
            "member 2 joined: ip 10.77.0.2".to_owned(),
            "matches the host".to_owned(),
            "bridge stopping, final bridge stats".to_owned(),
            format!("room {code}: left on the server"),
            "session: finished".to_owned(),
        ] {
            assert!(text.contains(&want), "no '{want}' in:\n{text}");
        }
        for secret in [
            "log-host-token-1",
            "log-guest-token-2",
            super::super::testserver::KEY,
        ] {
            assert!(!text.contains(secret), "{secret} in the log:\n{text}");
            assert!(!snapshot.contains(secret), "{secret} in the snapshot: {snapshot}");
        }
        assert!(!snapshot.contains("avatar"), "{snapshot}");
    }

    #[test]
    fn the_stats_line_lists_each_member_and_the_drops() {
        let mut stats = Stats::default();
        stats.members.insert(
            2,
            MemberStats {
                packets_out: 3,
                bytes_out: 300,
                packets_in: 4,
                bytes_in: 400,
            },
        );
        stats.drops.bad_tag = 5;
        stats.large = 1;
        assert_eq!(
            stats_line(&stats),
            "bridge stats: member 2 in 4p/400B out 3p/300B; drops bad_tag=5; large 1; foreign_from none"
        );
    }

    #[test]
    fn a_big_blob_drops_per_file_hashes_from_the_biggest_addons_first() {
        let mut doc = json!({ "addons": [
            { "id": "small", "detail": { "parts": { "Database": "aaaa" }, "read_list": "x", "read_count": 2, "reads": ["1", "2"] } },
            { "id": "big", "detail": { "parts": { "Database": "bbbb" }, "read_list": "y", "read_count": 3000, "reads": vec!["0123456789ab"; 3000] } },
        ]});
        let full = serde_json::to_vec(&doc).unwrap().len();
        assert!(trim_blob(&mut doc, full).is_empty());
        assert_eq!(trim_blob(&mut doc, 10_000), ["big"]);
        assert!(doc.pointer("/addons/1/detail/reads").is_none());
        assert_eq!(
            doc.pointer("/addons/1/detail/parts/Database"),
            Some(&json!("bbbb"))
        );
        assert!(doc.pointer("/addons/0/detail/reads").is_some());
        assert_eq!(trim_blob(&mut doc, 10), ["small"]);
        assert!(trim_blob(&mut doc, 10).is_empty());
    }

    #[test]
    fn the_blob_names_each_addons_category_outside_the_digest() {
        let f = print(&["extra"]);
        let doc = blob(&f, &BTreeMap::from([("okayama".to_owned(), "tracks".to_owned())]));
        assert_eq!(doc["addons"][1]["category"], "tracks");
        assert!(doc["addons"][0].get("category").is_none());
        let back: Fingerprint = serde_json::from_value(doc).unwrap();
        assert_eq!(back.canonical_digest(), f.digest);
    }

    #[test]
    fn the_mailbox_reads_fresh_status_and_sends_room_settings() {
        let dir = std::env::temp_dir().join(format!("swofty-mp-mailbox-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let mailbox = game::mailbox(&dir);
        std::fs::create_dir_all(&mailbox).unwrap();
        let status = json!({"screen": "main_menu", "in_session": false, "role": "none", "configured": false});
        std::fs::write(
            mailbox.join("status.json"),
            json!({"v": 1, "status": status}).to_string(),
        )
        .unwrap();
        let control = Mailbox::new(&dir);
        assert_eq!(control.status().unwrap().screen, Screen::MainMenu);

        let responder = {
            let mailbox = mailbox.clone();
            thread::spawn(move || {
                let deadline = Instant::now() + WAIT;
                while Instant::now() < deadline {
                    let entries = std::fs::read_dir(mailbox.join("inbox"))
                        .into_iter()
                        .flatten()
                        .flatten();
                    for e in entries {
                        let name = e.file_name().to_string_lossy().into_owned();
                        if !name.ends_with(".json") {
                            continue;
                        }
                        let cmd: Value = serde_json::from_slice(&std::fs::read(e.path()).unwrap()).unwrap();
                        let reply = json!({"seq": cmd["seq"], "op": cmd["op"], "ok": true, "error": null, "t": 1,
                            "status": {"screen": "main_menu", "in_session": false, "role": "none", "configured": true,
                                       "self": cmd["args"]["self"], "base": cmd["args"]["base"]}});
                        std::fs::create_dir_all(mailbox.join("outbox")).unwrap();
                        std::fs::write(mailbox.join("outbox").join(&name), reply.to_string()).unwrap();
                        std::fs::remove_file(e.path()).unwrap();
                        return cmd;
                    }
                    thread::sleep(Duration::from_millis(10));
                }
                panic!("no command arrived");
            })
        };
        let s = control.configure(3, 41000).unwrap();
        assert_eq!((s.configured, s.self_member, s.base), (true, 3, 41000));
        let cmd = responder.join().unwrap();
        assert_eq!(cmd["args"], json!({"self": 3, "base": 41000}));
        std::fs::remove_dir_all(&dir).ok();
    }
}
