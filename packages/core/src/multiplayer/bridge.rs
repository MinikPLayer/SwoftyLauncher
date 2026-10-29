//! The launcher's half of the netplay bridge.
//!
//! The in-game hook sends a datagram meant for member N to `127.0.0.1:BASE+N`; the bridge seals it
//! and sends it to N over a [`Transport`]. A datagram opened from member M goes to the game's port
//! from the `BASE+M` socket, which is how the hook tells who sent it. In echo mode there is no
//! transport: what the game sends is kept for inspection, and [`Bridge::inject`] plays a datagram
//! in as if a member had sent it.
//!
//! Only datagrams from the game's socket, `127.0.0.1:game_port`, are relayed. Opened datagrams go
//! to the port the game was last seen sending from, and until it has sent, to the configured
//! port; a port read from `netplay.json` is used once the game sends from it. Each start seals
//! under a new session id (see [`super::seal`]).
//!
//! A room grows while it is open: [`Bridge::add_member`] binds `BASE+N` for a member who joined
//! after the start, and [`Bridge::replace_transport`] moves the relay to a new transport (a fresh
//! TURN allocation) without touching the ports, so the game keeps its `BASE`.

use std::collections::{BTreeMap, VecDeque};
use std::io;
use std::net::{Ipv4Addr, SocketAddr, UdpSocket};
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU16, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, RwLock, RwLockReadGuard};
use std::thread::{self, JoinHandle};
use std::time::{Duration, SystemTime};

use ring::rand::{SecureRandom, SystemRandom};
use serde::{Deserialize, Serialize};
use zeroize::{Zeroize, ZeroizeOnDrop};

use super::direct::timed_out;
use super::seal::{Opener, Reject, Sealer, new_session_id};
use super::transport::{MAX_DATAGRAM, Transport};

/// The environment variable the launcher sets for the game, to [`Bridge::env_value`].
pub const ENV: &str = "SWOFTY_NETPLAY";
/// [`ENV`] for a game started before any room: the hooks go in and pass traffic through.
pub const ARMED: &str = "armed";
/// Whether a game started with [`ENV`] also gets the core's socket trace ([`TRACE_ENV`]).
pub const TRACE_WHEN_ARMED: bool = true;
/// The socket trace, capped at 16 MB, written to `<game>/swofty/net-trace.log`, and the game's own
/// network log channels at info in loader.log.
pub const TRACE_ENV: [(&str, &str); 3] = [
    ("SWOFTY_NET_TRACE", "1"),
    ("SWOFTY_NET_TRACE_CAP_KB", "16384"),
    ("SWOFTY_GAME_LOG_CHANNELS", "default,NETWORK*:info,SSL*:info"),
];
/// The game's `Network.Transport.SocketPort`.
pub const DEFAULT_GAME_PORT: u16 = 21414;
/// Where, under the game folder, the core reports the bridge once the game socket is bound.
pub const STATUS_FILE: &str = "swofty/netplay.json";
/// Payloads bigger than this are logged and counted in [`Stats::large`].
pub const LARGE_PAYLOAD: usize = 1200;
/// How many datagrams echo mode keeps for each member.
pub const ECHO_KEEP: usize = 64;

const POLL: Duration = Duration::from_millis(100);
const BASE_MIN: u16 = 30000;
const BASE_MAX: u16 = 60000;
const BIND_ATTEMPTS: usize = 64;
const BUF: usize = 65536;

/// The game's environment for [`ENV`] set to `value`: the trace too while [`TRACE_WHEN_ARMED`].
pub fn armed_env(value: &str) -> Vec<(&'static str, String)> {
    let mut env = vec![(ENV, value.to_owned())];
    if TRACE_WHEN_ARMED {
        env.extend(TRACE_ENV.iter().map(|(k, v)| (*k, (*v).to_owned())));
    }
    env
}

/// Wipes its key when dropped.
#[derive(Clone)]
pub struct BridgeConfig {
    /// This player's lobby number.
    pub self_member: u8,
    /// Every lobby member's number; this player's own is skipped.
    pub members: Vec<u8>,
    /// The lobby key the server hands out.
    pub key: [u8; 32],
    /// The game's socket port on loopback: the only source relayed, and where opened datagrams
    /// go until the game has sent from it.
    pub game_port: u16,
}

impl std::fmt::Debug for BridgeConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("BridgeConfig")
            .field("self_member", &self.self_member)
            .field("members", &self.members)
            .field("game_port", &self.game_port)
            .finish_non_exhaustive()
    }
}

impl Drop for BridgeConfig {
    fn drop(&mut self) {
        self.key.zeroize();
    }
}

impl ZeroizeOnDrop for BridgeConfig {}

impl BridgeConfig {
    fn remotes(&self, allow_empty: bool) -> io::Result<Vec<u8>> {
        let valid = |m: u8| (1..=254).contains(&m);
        if !valid(self.self_member) || !self.members.iter().all(|&m| valid(m)) {
            return Err(invalid("lobby members are numbered 1 to 254"));
        }
        let mut remotes: Vec<u8> = self
            .members
            .iter()
            .copied()
            .filter(|&m| m != self.self_member)
            .collect();
        remotes.sort_unstable();
        remotes.dedup();
        if remotes.is_empty() && !allow_empty {
            return Err(invalid("there are no other lobby members to bridge"));
        }
        Ok(remotes)
    }
}

/// Payload traffic with one member: out is what the game sent to it, in what reached the game
/// from it.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize)]
pub struct MemberStats {
    pub packets_out: u64,
    pub bytes_out: u64,
    pub packets_in: u64,
    pub bytes_in: u64,
}

/// Datagrams dropped, by reason.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize)]
pub struct Drops {
    pub malformed: u64,
    pub unknown_member: u64,
    pub wrong_sender: u64,
    pub bad_tag: u64,
    pub replay: u64,
    pub too_old: u64,
    pub old_session: u64,
    pub throttled: u64,
    /// A datagram reached a member port from a socket other than the game's. Echo mode keeps it
    /// and flags it.
    pub foreign_source: u64,
    /// The transport couldn't send a sealed datagram.
    pub send_failed: u64,
    /// An opened or injected datagram couldn't be sent to the game's port.
    pub deliver_failed: u64,
}

impl Drops {
    fn count(&mut self, reject: Reject) {
        *match reject {
            Reject::Malformed => &mut self.malformed,
            Reject::UnknownMember => &mut self.unknown_member,
            Reject::WrongSender => &mut self.wrong_sender,
            Reject::BadTag => &mut self.bad_tag,
            Reject::Replay => &mut self.replay,
            Reject::TooOld => &mut self.too_old,
            Reject::OldSession => &mut self.old_session,
            Reject::Throttled => &mut self.throttled,
        } += 1;
    }

    /// Each reason with its count, leaving out the ones at zero.
    pub fn nonzero(&self) -> Vec<(&'static str, u64)> {
        [
            ("malformed", self.malformed),
            ("unknown_member", self.unknown_member),
            ("wrong_sender", self.wrong_sender),
            ("bad_tag", self.bad_tag),
            ("replay", self.replay),
            ("too_old", self.too_old),
            ("old_session", self.old_session),
            ("throttled", self.throttled),
            ("foreign_source", self.foreign_source),
            ("send_failed", self.send_failed),
            ("deliver_failed", self.deliver_failed),
        ]
        .into_iter()
        .filter(|(_, n)| *n > 0)
        .collect()
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
pub struct Stats {
    /// Every remote member, by lobby number.
    pub members: BTreeMap<u8, MemberStats>,
    pub drops: Drops,
    /// Payloads over [`LARGE_PAYLOAD`] bytes, either way.
    pub large: u64,
    /// The first socket other than the game's that sent to a member port.
    pub foreign_from: Option<SocketAddr>,
}

/// A datagram the game sent to a member in echo mode.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Echoed {
    /// Counts up across members from 0.
    pub seq: u64,
    pub member: u8,
    pub at: SystemTime,
    /// The socket it came from.
    pub from: SocketAddr,
    /// Whether `from` isn't the game's socket.
    pub foreign: bool,
    pub payload: Vec<u8>,
}

/// What the core writes to [`STATUS_FILE`] once the game socket is bound.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct NetplayStatus {
    #[serde(default)]
    pub pid: u32,
    #[serde(rename = "self")]
    pub self_member: u8,
    pub base: u16,
    pub game_port: u16,
}

impl NetplayStatus {
    pub fn read(game_dir: &Path) -> Option<Self> {
        serde_json::from_slice(&std::fs::read(game_dir.join(STATUS_FILE)).ok()?).ok()
    }
}

pub struct Bridge {
    shared: Arc<Shared>,
    threads: Mutex<Vec<JoinHandle<()>>>,
}

struct Shared {
    self_member: u8,
    base: u16,
    sockets: RwLock<BTreeMap<u8, Arc<UdpSocket>>>,
    configured_port: u16,
    game_port: AtomicU16,
    seen_port: AtomicU16,
    stop: AtomicBool,
    stats: Mutex<Stats>,
    echo: Option<Mutex<Echo>>,
    link: Option<Link>,
}

/// The relay side: absent in echo mode.
struct Link {
    transport: RwLock<Arc<dyn Transport>>,
    sealer: Sealer,
    opener: Mutex<Opener>,
}

impl Link {
    fn transport(&self) -> Arc<dyn Transport> {
        self.transport.read().unwrap_or_else(|e| e.into_inner()).clone()
    }
}

#[derive(Default)]
struct Echo {
    next: u64,
    kept: BTreeMap<u8, VecDeque<Echoed>>,
}

impl Bridge {
    /// Bind a loopback port for each remote member and relay between them and `transport`. A
    /// room of one starts with no ports; [`Bridge::add_member`] adds them as members join.
    pub fn start(cfg: BridgeConfig, transport: Arc<dyn Transport>) -> io::Result<Bridge> {
        let remotes = cfg.remotes(true)?;
        let link = Link {
            transport: RwLock::new(transport),
            sealer: Sealer::new(
                &cfg.key,
                cfg.self_member,
                new_session_id(SystemTime::now(), random_u64()?),
            ),
            opener: Mutex::new(Opener::new(&cfg.key, cfg.self_member, remotes.iter().copied())),
        };
        let bridge = Self::bind(&cfg, &remotes, Some(link))?;
        for &m in &remotes {
            bridge.spawn_member(m)?;
        }
        let shared = bridge.shared.clone();
        bridge.spawn("netplay-in".into(), move || inbound(&shared))?;
        Ok(bridge)
    }

    /// Bind the loopback ports with no transport: keep what the game sends, and send it only
    /// what [`Bridge::inject`] is given.
    pub fn start_echo(cfg: BridgeConfig) -> io::Result<Bridge> {
        let remotes = cfg.remotes(false)?;
        let bridge = Self::bind(&cfg, &remotes, None)?;
        for &m in &remotes {
            bridge.spawn_member(m)?;
        }
        Ok(bridge)
    }

    fn bind(cfg: &BridgeConfig, remotes: &[u8], link: Option<Link>) -> io::Result<Bridge> {
        let (base, sockets) = bind_ports(remotes, cfg.game_port)?;
        for socket in sockets.values() {
            socket.set_read_timeout(Some(POLL))?;
        }
        let stats = Stats {
            members: remotes.iter().map(|&m| (m, MemberStats::default())).collect(),
            ..Stats::default()
        };
        let echo = link.is_none();
        Ok(Bridge {
            shared: Arc::new(Shared {
                self_member: cfg.self_member,
                base,
                sockets: RwLock::new(sockets.into_iter().map(|(m, s)| (m, Arc::new(s))).collect()),
                configured_port: cfg.game_port,
                game_port: AtomicU16::new(cfg.game_port),
                seen_port: AtomicU16::new(0),
                stop: AtomicBool::new(false),
                stats: Mutex::new(stats),
                echo: echo.then(Mutex::default),
                link,
            }),
            threads: Mutex::default(),
        })
    }

    fn spawn(&self, name: String, f: impl FnOnce() + Send + 'static) -> io::Result<()> {
        let handle = thread::Builder::new().name(name).spawn(f)?;
        self.threads
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .push(handle);
        Ok(())
    }

    fn spawn_member(&self, member: u8) -> io::Result<()> {
        let shared = self.shared.clone();
        let name = match shared.link {
            Some(_) => format!("netplay-out-{member}"),
            None => format!("netplay-echo-{member}"),
        };
        self.spawn(name, move || outbound(&shared, member))
    }

    /// Bind `127.0.0.1:BASE+member` for a member who joined after the start and relay it like
    /// the others. False when `member` is this player or already bridged.
    pub fn add_member(&self, member: u8) -> io::Result<bool> {
        if !(1..=254).contains(&member) {
            return Err(invalid("lobby members are numbered 1 to 254"));
        }
        if member == self.shared.self_member || self.shared.sockets().contains_key(&member) {
            return Ok(false);
        }
        let socket = UdpSocket::bind((Ipv4Addr::LOCALHOST, self.shared.base + u16::from(member)))?;
        socket.set_read_timeout(Some(POLL))?;
        if let Some(link) = &self.shared.link {
            link.opener
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .add_member(member);
        }
        self.shared.stats().members.entry(member).or_default();
        self.shared
            .sockets
            .write()
            .unwrap_or_else(|e| e.into_inner())
            .insert(member, Arc::new(socket));
        self.spawn_member(member)?;
        Ok(true)
    }

    /// Relay over `transport` from now on, keeping the ports, the session id and its counters,
    /// so the game notices nothing. Give it the members' addresses first. Does nothing in echo
    /// mode.
    pub fn replace_transport(&self, transport: Arc<dyn Transport>) {
        if let Some(link) = &self.shared.link {
            *link.transport.write().unwrap_or_else(|e| e.into_inner()) = transport;
        }
    }

    pub fn base(&self) -> u16 {
        self.shared.base
    }

    pub fn self_member(&self) -> u8 {
        self.shared.self_member
    }

    /// The remote members, in order.
    pub fn members(&self) -> Vec<u8> {
        self.shared.sockets().keys().copied().collect()
    }

    /// The loopback port the game sends to for `member`.
    pub fn port(&self, member: u8) -> Option<u16> {
        self.shared
            .sockets()
            .contains_key(&member)
            .then(|| self.shared.base + u16::from(member))
    }

    /// The value of [`ENV`] for the game: `self=N;base=BASE`.
    pub fn env_value(&self) -> String {
        format!("self={};base={}", self.shared.self_member, self.shared.base)
    }

    /// [`Self::env_value`] with `;name=<percent-encoded UTF-8>`, the signed-in name for the game's
    /// driver, when `name` leaves anything through [`super::game::driver_name`].
    pub fn env_value_named(&self, name: &str) -> String {
        let mut value = self.env_value();
        if let Some(name) = super::game::driver_name(name) {
            value.push_str(";name=");
            for b in name.bytes() {
                if b.is_ascii_alphanumeric() || matches!(b, b'-' | b'_' | b'.' | b'~') {
                    value.push(char::from(b));
                } else {
                    value.push_str(&format!("%{b:02X}"));
                }
            }
        }
        value
    }

    /// The port the game's socket is expected on; datagrams from any other are dropped.
    pub fn game_port(&self) -> u16 {
        self.shared.game_port.load(Ordering::Relaxed)
    }

    /// Expect the game's socket on `port`. Opened datagrams go there once the game sends from it.
    pub fn set_game_port(&self, port: u16) {
        self.shared.game_port.store(port, Ordering::Relaxed);
    }

    /// Where opened and injected datagrams go: the port the game was last seen sending from,
    /// else the configured one.
    pub fn delivery_port(&self) -> u16 {
        self.shared.delivery_port()
    }

    /// Expect the game port from `<game_dir>/swofty/netplay.json` when the core wrote it for this
    /// bridge's member and base, and return it.
    pub fn sync_game_port(&self, game_dir: &Path) -> Option<u16> {
        let status = NetplayStatus::read(game_dir)?;
        let ours = status.self_member == self.shared.self_member && status.base == self.shared.base;
        ours.then(|| {
            self.set_game_port(status.game_port);
            status.game_port
        })
    }

    pub fn stats(&self) -> Stats {
        self.shared.stats().clone()
    }

    /// In echo mode, the datagrams kept for every member, oldest first.
    pub fn echoed(&self) -> Vec<Echoed> {
        let Some(echo) = &self.shared.echo else {
            return Vec::new();
        };
        let echo = echo.lock().unwrap_or_else(|e| e.into_inner());
        let mut all: Vec<Echoed> = echo.kept.values().flatten().cloned().collect();
        all.sort_by_key(|e| e.seq);
        all
    }

    /// Send `payload` to the game's port from the `BASE+from` socket, as if member `from` had
    /// sent it.
    pub fn inject(&self, from: u8, payload: &[u8]) -> io::Result<()> {
        self.shared.deliver(from, payload)
    }

    /// Stop relaying, wait for the threads, and close the ports.
    pub fn stop(self) {
        self.shutdown();
    }

    fn shutdown(&self) {
        self.shared.stop.store(true, Ordering::Relaxed);
        let threads: Vec<JoinHandle<()>> = self
            .threads
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .drain(..)
            .collect();
        for t in threads {
            let _ = t.join();
        }
    }
}

impl Drop for Bridge {
    fn drop(&mut self) {
        self.shutdown();
    }
}

impl Shared {
    fn stats(&self) -> MutexGuard<'_, Stats> {
        self.stats.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn stopping(&self) -> bool {
        self.stop.load(Ordering::Relaxed)
    }

    fn sockets(&self) -> RwLockReadGuard<'_, BTreeMap<u8, Arc<UdpSocket>>> {
        self.sockets.read().unwrap_or_else(|e| e.into_inner())
    }

    fn sent(&self, member: u8, len: usize) {
        let mut stats = self.stats();
        let m = stats.members.entry(member).or_default();
        m.packets_out += 1;
        m.bytes_out += len as u64;
    }

    /// Take back a [`Shared::sent`] whose send failed.
    fn unsent(&self, member: u8, len: usize) {
        let mut stats = self.stats();
        let m = stats.members.entry(member).or_default();
        m.packets_out = m.packets_out.saturating_sub(1);
        m.bytes_out = m.bytes_out.saturating_sub(len as u64);
        stats.drops.send_failed += 1;
    }

    fn delivery_port(&self) -> u16 {
        match self.seen_port.load(Ordering::Relaxed) {
            0 => self.configured_port,
            port => port,
        }
    }

    /// Whether `from` is the game's socket, remembering its port for delivery when it is.
    fn is_game(&self, from: SocketAddr) -> bool {
        let game = from.ip() == Ipv4Addr::LOCALHOST && from.port() == self.game_port.load(Ordering::Relaxed);
        if game {
            self.seen_port.store(from.port(), Ordering::Relaxed);
        }
        game
    }

    fn deliver(&self, from: u8, payload: &[u8]) -> io::Result<()> {
        let socket = self.sockets().get(&from).cloned().ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::NotFound,
                format!("member {from} has no loopback port"),
            )
        })?;
        self.check_size(from, payload.len(), "from");
        let len = payload.len() as u64;
        {
            let mut stats = self.stats();
            let m = stats.members.entry(from).or_default();
            m.packets_in += 1;
            m.bytes_in += len;
        }
        if let Err(e) = socket.send_to(payload, (Ipv4Addr::LOCALHOST, self.delivery_port())) {
            let mut stats = self.stats();
            let m = stats.members.entry(from).or_default();
            m.packets_in = m.packets_in.saturating_sub(1);
            m.bytes_in = m.bytes_in.saturating_sub(len);
            stats.drops.deliver_failed += 1;
            return Err(e);
        }
        Ok(())
    }

    fn check_size(&self, member: u8, len: usize, direction: &str) {
        if len <= LARGE_PAYLOAD {
            return;
        }
        let count = {
            let mut stats = self.stats();
            stats.large += 1;
            stats.large
        };
        if count <= 10 || count.is_multiple_of(100) {
            let text = format!(
                "{len}-byte datagram {direction} member {member} is over {LARGE_PAYLOAD} bytes ({count} so far)"
            );
            super::diag::line(format!("bridge: {text}"));
            eprintln!("swofty-launcher: netplay: {text}");
        }
    }

    fn keep(&self, member: u8, from: SocketAddr, foreign: bool, payload: &[u8]) {
        let Some(echo) = &self.echo else { return };
        let mut echo = echo.lock().unwrap_or_else(|e| e.into_inner());
        let seq = echo.next;
        echo.next += 1;
        let kept = echo.kept.entry(member).or_default();
        if kept.len() == ECHO_KEEP {
            kept.pop_front();
        }
        kept.push_back(Echoed {
            seq,
            member,
            at: SystemTime::now(),
            from,
            foreign,
            payload: payload.to_vec(),
        });
    }
}

fn outbound(shared: &Shared, member: u8) {
    let Some(socket) = shared.sockets().get(&member).cloned() else {
        return;
    };
    let link = shared.link.as_ref();
    let mut buf = vec![0u8; BUF];
    let mut wire = Vec::with_capacity(MAX_DATAGRAM);
    while !shared.stopping() {
        let (n, from) = match socket.recv_from(&mut buf) {
            Ok(got) => got,
            Err(e) if timed_out(&e) || e.kind() == io::ErrorKind::ConnectionReset => continue,
            Err(_) => {
                thread::sleep(Duration::from_millis(5));
                continue;
            }
        };
        let payload = &buf[..n];
        let game = shared.is_game(from);
        if !game {
            let mut stats = shared.stats();
            stats.drops.foreign_source += 1;
            stats.foreign_from.get_or_insert(from);
            drop(stats);
            if link.is_some() {
                continue;
            }
        }
        shared.check_size(member, n, "to");
        if game {
            shared.sent(member, n);
        }
        match link {
            Some(link) => {
                link.sealer.seal(member, payload, &mut wire);
                if link.transport().send(member, &wire).is_err() {
                    shared.unsent(member, n);
                }
            }
            None => shared.keep(member, from, !game, payload),
        }
    }
}

fn inbound(shared: &Shared) {
    let Some(link) = shared.link.as_ref() else {
        return;
    };
    let mut buf = vec![0u8; BUF];
    while !shared.stopping() {
        let (sender, n) = match link.transport().recv(&mut buf, POLL) {
            Ok(Some(got)) => got,
            Ok(None) => continue,
            Err(_) => {
                thread::sleep(Duration::from_millis(5));
                continue;
            }
        };
        let mut opener = link.opener.lock().unwrap_or_else(|e| e.into_inner());
        match opener.open(sender, &mut buf[..n]) {
            Ok(plain) => {
                let _ = shared.deliver(sender, plain);
            }
            Err(reject) => shared.stats().drops.count(reject),
        }
    }
}

fn bind_ports(remotes: &[u8], game_port: u16) -> io::Result<(u16, BTreeMap<u8, UdpSocket>)> {
    let mut last = io::Error::new(io::ErrorKind::AddrInUse, "no free loopback port range");
    for _ in 0..BIND_ATTEMPTS {
        let base = BASE_MIN + (random_u64()? % u64::from(BASE_MAX - BASE_MIN)) as u16;
        if remotes.iter().any(|&m| base + u16::from(m) == game_port) {
            continue;
        }
        let bound: io::Result<BTreeMap<u8, UdpSocket>> = remotes
            .iter()
            .map(|&m| Ok((m, UdpSocket::bind((Ipv4Addr::LOCALHOST, base + u16::from(m)))?)))
            .collect();
        match bound {
            Ok(sockets) => return Ok((base, sockets)),
            Err(e) => last = e,
        }
    }
    Err(last)
}

fn random_u64() -> io::Result<u64> {
    let mut raw = [0u8; 8];
    SystemRandom::new()
        .fill(&mut raw)
        .map_err(|_| io::Error::other("no system randomness"))?;
    Ok(u64::from_le_bytes(raw))
}

fn invalid(msg: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, msg)
}

#[cfg(test)]
mod tests {
    use std::collections::HashMap;
    use std::time::Instant;

    use super::super::direct::DirectUdp;
    use super::super::seal::{HEADER_LEN, Sealer};
    use super::*;

    const KEY: [u8; 32] = [0x5a; 32];
    const WAIT: Duration = Duration::from_secs(3);

    fn fake_game() -> UdpSocket {
        let game = UdpSocket::bind("127.0.0.1:0").unwrap();
        game.set_read_timeout(Some(WAIT)).unwrap();
        game
    }

    fn port(socket: &UdpSocket) -> u16 {
        socket.local_addr().unwrap().port()
    }

    fn loopback(port: u16) -> SocketAddr {
        SocketAddr::from((Ipv4Addr::LOCALHOST, port))
    }

    fn recv(game: &UdpSocket) -> (Vec<u8>, SocketAddr) {
        let mut buf = [0u8; 4096];
        let (n, from) = game
            .recv_from(&mut buf)
            .expect("a datagram reaches the fake game");
        (buf[..n].to_vec(), from)
    }

    struct Player {
        bridge: Bridge,
        game: UdpSocket,
        transport: Arc<DirectUdp>,
    }

    fn lobby(members: &[u8]) -> Vec<Player> {
        let transports: Vec<Arc<DirectUdp>> = members
            .iter()
            .map(|_| Arc::new(DirectUdp::bind("127.0.0.1:0").unwrap()))
            .collect();
        let addrs: HashMap<u8, SocketAddr> = members
            .iter()
            .zip(&transports)
            .map(|(&m, t)| (m, t.local_addr()))
            .collect();
        members
            .iter()
            .zip(transports)
            .map(|(&me, transport)| {
                let mut peers = addrs.clone();
                peers.remove(&me);
                transport.set_peers(&peers).unwrap();
                let game = fake_game();
                let cfg = BridgeConfig {
                    self_member: me,
                    members: members.to_vec(),
                    key: KEY,
                    game_port: port(&game),
                };
                let bridge = Bridge::start(cfg, transport.clone()).unwrap();
                Player {
                    bridge,
                    game,
                    transport,
                }
            })
            .collect()
    }

    #[test]
    fn an_armed_game_also_gets_the_socket_trace() {
        let env = armed_env(ARMED);
        assert_eq!(env[0], (ENV, ARMED.to_owned()));
        assert_eq!(
            env.contains(&("SWOFTY_NET_TRACE", "1".to_owned())),
            TRACE_WHEN_ARMED
        );
        assert_eq!(
            env.contains(&("SWOFTY_NET_TRACE_CAP_KB", "16384".to_owned())),
            TRACE_WHEN_ARMED
        );
    }

    #[test]
    fn three_bridges_carry_datagrams_byte_identical_both_ways() {
        let lobby = lobby(&[1, 2, 3]);
        let payloads: Vec<Vec<u8>> = vec![
            b"handshake".to_vec(),
            Vec::new(),
            (0..=255u8).cycle().take(1264).collect(),
        ];
        for (i, from) in lobby.iter().enumerate() {
            let me = from.bridge.self_member();
            for (j, to) in lobby.iter().enumerate() {
                if i == j {
                    continue;
                }
                let them = to.bridge.self_member();
                for payload in &payloads {
                    let dest = loopback(from.bridge.port(them).unwrap());
                    from.game.send_to(payload, dest).unwrap();
                    let (got, src) = recv(&to.game);
                    assert_eq!(&got, payload, "{me} -> {them}");
                    assert_eq!(src, loopback(to.bridge.base() + u16::from(me)));
                }
            }
        }
        for p in &lobby {
            let stats = p.bridge.stats();
            assert_eq!(stats.members.len(), 2);
            for m in stats.members.values() {
                assert_eq!((m.packets_out, m.packets_in), (3, 3));
                assert_eq!((m.bytes_out, m.bytes_in), (1273, 1273));
            }
            assert_eq!(stats.drops, Drops::default());
            assert_eq!(stats.large, 4);
            assert_eq!(p.transport.unknown_dropped(), 0);
        }
        lobby.into_iter().for_each(|p| p.bridge.stop());
    }

    #[test]
    fn a_member_who_joins_after_the_start_gets_a_port_on_the_same_base() {
        let (host_link, guest_link) = (
            Arc::new(DirectUdp::bind("127.0.0.1:0").unwrap()),
            Arc::new(DirectUdp::bind("127.0.0.1:0").unwrap()),
        );
        let (host_game, guest_game) = (fake_game(), fake_game());
        let cfg = |me: u8, members: &[u8], game: &UdpSocket| BridgeConfig {
            self_member: me,
            members: members.to_vec(),
            key: KEY,
            game_port: port(game),
        };
        let host = Bridge::start(cfg(1, &[1], &host_game), host_link.clone()).unwrap();
        assert!(host.members().is_empty());
        let base = host.base();
        let guest = Bridge::start(cfg(2, &[1, 2], &guest_game), guest_link.clone()).unwrap();
        host_link
            .set_peers(&HashMap::from([(2, guest_link.local_addr())]))
            .unwrap();
        guest_link
            .set_peers(&HashMap::from([(1, host_link.local_addr())]))
            .unwrap();
        assert!(host.add_member(2).unwrap());
        assert!(!host.add_member(2).unwrap());
        assert!(!host.add_member(1).unwrap());
        assert!(host.add_member(0).is_err());
        assert_eq!((host.base(), host.port(2)), (base, Some(base + 2)));

        guest_game
            .send_to(b"hello host", loopback(guest.port(1).unwrap()))
            .unwrap();
        assert_eq!(recv(&host_game), (b"hello host".to_vec(), loopback(base + 2)));
        host_game.send_to(b"welcome", loopback(base + 2)).unwrap();
        assert_eq!(
            recv(&guest_game),
            (b"welcome".to_vec(), loopback(guest.base() + 1))
        );
        assert_eq!(host.stats().members[&2].packets_in, 1);
        host.stop();
        guest.stop();
    }

    #[test]
    fn a_new_transport_takes_over_without_moving_the_ports() {
        let lobby = lobby(&[1, 2]);
        let (one, two) = (&lobby[0], &lobby[1]);
        let to_two = loopback(one.bridge.port(2).unwrap());
        one.game.send_to(b"before", to_two).unwrap();
        assert_eq!(recv(&two.game).0, b"before");

        let fresh: Vec<Arc<DirectUdp>> = (0..2)
            .map(|_| Arc::new(DirectUdp::bind("127.0.0.1:0").unwrap()))
            .collect();
        fresh[0]
            .set_peers(&HashMap::from([(2, fresh[1].local_addr())]))
            .unwrap();
        fresh[1]
            .set_peers(&HashMap::from([(1, fresh[0].local_addr())]))
            .unwrap();
        let base = one.bridge.base();
        one.bridge.replace_transport(fresh[0].clone());
        two.bridge.replace_transport(fresh[1].clone());
        one.transport.set_peers(&HashMap::new()).unwrap();
        two.transport.set_peers(&HashMap::new()).unwrap();

        one.game.send_to(b"after", to_two).unwrap();
        assert_eq!(recv(&two.game).0, b"after");
        two.game
            .send_to(b"back", loopback(two.bridge.port(1).unwrap()))
            .unwrap();
        assert_eq!(recv(&one.game), (b"back".to_vec(), loopback(base + 2)));
        assert_eq!(one.bridge.base(), base);
        assert_eq!(one.bridge.stats().drops, Drops::default());
        assert_eq!(two.bridge.stats().drops, Drops::default());
    }

    fn wait_for(bridge: &Bridge, done: impl Fn(&Stats) -> bool) -> Stats {
        let deadline = Instant::now() + WAIT;
        loop {
            let stats = bridge.stats();
            if done(&stats) || Instant::now() > deadline {
                return stats;
            }
            thread::sleep(Duration::from_millis(10));
        }
    }

    #[test]
    fn drops_forged_replayed_misaddressed_and_stale_datagrams() {
        let attacker = Arc::new(DirectUdp::bind("127.0.0.1:0").unwrap());
        let transport = Arc::new(DirectUdp::bind("127.0.0.1:0").unwrap());
        transport
            .set_peers(&HashMap::from([(1, attacker.local_addr())]))
            .unwrap();
        attacker
            .set_peers(&HashMap::from([(2, transport.local_addr())]))
            .unwrap();
        let game = fake_game();
        let cfg = BridgeConfig {
            self_member: 2,
            members: vec![1, 2, 3],
            key: KEY,
            game_port: port(&game),
        };
        let bridge = Bridge::start(cfg, transport).unwrap();

        let one = Sealer::new(&KEY, 1, 7);
        let mut good = Vec::new();
        one.seal(2, b"genuine", &mut good);
        let mut for_three = Vec::new();
        one.seal(3, b"for three", &mut for_three);
        one.seal(3, b"for three, past our window top", &mut for_three);
        let mut foreign = Vec::new();
        Sealer::new(&[1; 32], 1, 8).seal(2, b"foreign", &mut foreign);
        let mut as_three = Vec::new();
        Sealer::new(&KEY, 3, 9).seal(2, b"spoofed", &mut as_three);
        let mut tampered = good.clone();
        tampered[HEADER_LEN - 1] = 20;
        for wire in [
            &good,
            &good,
            &for_three,
            &foreign,
            &as_three,
            &tampered,
            &good[..5].to_vec(),
        ] {
            attacker.send(2, wire).unwrap();
        }
        let mut late = Vec::new();
        one.seal_with_counter(2, 2000, b"newest", &mut late);
        attacker.send(2, &late).unwrap();

        assert_eq!(recv(&game).0, b"genuine");
        assert_eq!(recv(&game).0, b"newest");
        let want = Drops {
            malformed: 1,
            wrong_sender: 1,
            bad_tag: 3,
            replay: 1,
            ..Drops::default()
        };
        assert_eq!(wait_for(&bridge, |s| s.drops == want).drops, want);

        let mut old = Vec::new();
        one.seal_with_counter(2, 2000 - 1024, b"stale", &mut old);
        attacker.send(2, &old).unwrap();
        assert_eq!(wait_for(&bridge, |s| s.drops.too_old > 0).drops.too_old, 1);

        let mut restarted = Vec::new();
        Sealer::new(&KEY, 1, 8).seal(2, b"restarted", &mut restarted);
        attacker.send(2, &restarted).unwrap();
        assert_eq!(recv(&game).0, b"restarted");
        let mut earlier = Vec::new();
        one.seal(2, b"earlier session", &mut earlier);
        attacker.send(2, &earlier).unwrap();
        assert_eq!(
            wait_for(&bridge, |s| s.drops.old_session > 0).drops.old_session,
            1
        );
        assert_eq!(bridge.stats().members[&1].packets_in, 3);
        bridge.stop();
    }

    #[test]
    fn a_datagram_for_another_member_is_not_replayed_to_us() {
        let lobby = lobby(&[1, 2, 3]);
        let (one, three) = (&lobby[0], &lobby[2]);
        let spy = UdpSocket::bind("127.0.0.1:0").unwrap();
        spy.set_read_timeout(Some(WAIT)).unwrap();
        let peers = HashMap::from([(1, one.transport.local_addr()), (2, spy.local_addr().unwrap())]);
        three.transport.set_peers(&peers).unwrap();
        three
            .game
            .send_to(b"for two only", loopback(three.bridge.port(2).unwrap()))
            .unwrap();
        let mut wire = [0u8; 256];
        let n = spy.recv(&mut wire).unwrap();
        three.transport.send(1, &wire[..n]).unwrap();
        assert_eq!(wait_for(&one.bridge, |s| s.drops.bad_tag > 0).drops.bad_tag, 1);
        assert_eq!(one.bridge.stats().members[&3].packets_in, 0);
        one.game
            .set_read_timeout(Some(Duration::from_millis(200)))
            .unwrap();
        assert!(one.game.recv_from(&mut wire).is_err());
    }

    #[test]
    fn relays_only_what_the_game_socket_sends() {
        let lobby = lobby(&[1, 2]);
        let (a, b) = (&lobby[0], &lobby[1]);
        let to_b = loopback(a.bridge.port(2).unwrap());
        let stranger = UdpSocket::bind("127.0.0.1:0").unwrap();
        stranger.send_to(b"from a stranger", to_b).unwrap();
        a.game.send_to(b"from the game", to_b).unwrap();
        assert_eq!(recv(&b.game).0, b"from the game");
        let stats = a.bridge.stats();
        assert_eq!(stats.drops.foreign_source, 1);
        assert_eq!(stats.foreign_from, Some(stranger.local_addr().unwrap()));
        assert_eq!(stats.members[&2].packets_out, 1);
        b.game.set_read_timeout(Some(Duration::from_millis(200))).unwrap();
        assert!(b.game.recv_from(&mut [0u8; 64]).is_err());
    }

    #[test]
    fn delivers_to_a_new_game_port_only_once_the_game_sends_from_it() {
        let lobby = lobby(&[1, 2]);
        let (a, b) = (&lobby[0], &lobby[1]);
        let moved = fake_game();
        b.bridge.set_game_port(port(&moved));
        a.game
            .send_to(b"before", loopback(a.bridge.port(2).unwrap()))
            .unwrap();
        assert_eq!(recv(&b.game).0, b"before");
        assert_eq!(b.bridge.delivery_port(), port(&b.game));

        let to_a = loopback(b.bridge.port(1).unwrap());
        b.game.send_to(b"old socket", to_a).unwrap();
        moved.send_to(b"moved socket", to_a).unwrap();
        assert_eq!(recv(&a.game).0, b"moved socket");
        assert_eq!(b.bridge.stats().drops.foreign_source, 1);
        assert_eq!(b.bridge.delivery_port(), port(&moved));
        a.game
            .send_to(b"after", loopback(a.bridge.port(2).unwrap()))
            .unwrap();
        assert_eq!(recv(&moved).0, b"after");
    }

    fn wait_echoed(bridge: &Bridge, count: usize) -> Vec<Echoed> {
        let deadline = Instant::now() + WAIT;
        loop {
            let got = bridge.echoed();
            if got.len() >= count || Instant::now() > deadline {
                return got;
            }
            thread::sleep(Duration::from_millis(10));
        }
    }

    #[test]
    fn echo_mode_keeps_what_the_game_sends_and_injects_from_a_member_port() {
        let game = fake_game();
        let cfg = BridgeConfig {
            self_member: 2,
            members: vec![1, 2, 3],
            key: KEY,
            game_port: port(&game),
        };
        let bridge = Bridge::start_echo(cfg).unwrap();
        assert_eq!(bridge.members(), vec![1, 3]);
        assert_eq!(bridge.port(2), None);
        game.send_to(b"to host", loopback(bridge.port(1).unwrap()))
            .unwrap();
        wait_echoed(&bridge, 1);
        game.send_to(b"to three", loopback(bridge.port(3).unwrap()))
            .unwrap();
        let echoed = wait_echoed(&bridge, 2);
        assert_eq!(echoed.len(), 2);
        assert_eq!((echoed[0].seq, echoed[0].member), (0, 1));
        assert_eq!(echoed[0].payload, b"to host");
        assert_eq!(echoed[0].from, game.local_addr().unwrap());
        assert!(!echoed[0].foreign && !echoed[1].foreign);
        assert_eq!((echoed[1].seq, echoed[1].member), (1, 3));
        assert_eq!(echoed[1].payload, b"to three");

        bridge.inject(3, b"from three").unwrap();
        let (payload, src) = recv(&game);
        assert_eq!(payload, b"from three");
        assert_eq!(src, loopback(bridge.base() + 3));
        assert!(bridge.inject(2, b"self").is_err());

        let stranger = UdpSocket::bind("127.0.0.1:0").unwrap();
        stranger
            .send_to(b"stranger", loopback(bridge.port(1).unwrap()))
            .unwrap();
        let echoed = wait_echoed(&bridge, 3);
        assert_eq!(echoed[2].payload, b"stranger");
        assert!(echoed[2].foreign);
        assert_eq!(echoed[2].from, stranger.local_addr().unwrap());
        assert_eq!(bridge.stats().drops.foreign_source, 1);

        let stats = bridge.stats();
        assert_eq!(stats.members[&1].packets_out, 1);
        assert_eq!(stats.members[&3].packets_out, 1);
        assert_eq!(stats.members[&3].packets_in, 1);
        assert_eq!(stats.members[&3].bytes_in, 10);
        bridge.stop();
    }

    #[test]
    fn echo_mode_keeps_the_latest_datagrams_per_member() {
        let game = fake_game();
        let cfg = BridgeConfig {
            self_member: 1,
            members: vec![1, 2],
            key: KEY,
            game_port: port(&game),
        };
        let bridge = Bridge::start_echo(cfg).unwrap();
        let dest = loopback(bridge.port(2).unwrap());
        let total = ECHO_KEEP + 5;
        for i in 0..total {
            game.send_to(&(i as u32).to_be_bytes(), dest).unwrap();
        }
        let deadline = Instant::now() + WAIT;
        while bridge.stats().members[&2].packets_out < total as u64 && Instant::now() < deadline {
            thread::sleep(Duration::from_millis(10));
        }
        let echoed = bridge.echoed();
        assert_eq!(echoed.len(), ECHO_KEEP);
        assert_eq!(echoed[0].payload, 5u32.to_be_bytes());
        assert_eq!(echoed.last().unwrap().seq, total as u64 - 1);
    }

    #[test]
    fn names_its_ports_in_the_env_value_and_releases_them_on_stop() {
        let cfg = BridgeConfig {
            self_member: 3,
            members: vec![1, 2, 3, 2],
            key: KEY,
            game_port: DEFAULT_GAME_PORT,
        };
        let bridge = Bridge::start_echo(cfg).unwrap();
        let base = bridge.base();
        assert!((BASE_MIN..BASE_MAX).contains(&base));
        assert_eq!(bridge.env_value(), format!("self=3;base={base}"));
        assert_eq!(
            bridge.env_value_named(" Sw\u{f6}fty ;1\u{200b}"),
            format!("self=3;base={base};name=Sw%C3%B6fty%20%3B1")
        );
        assert_eq!(bridge.env_value_named("\u{7}"), format!("self=3;base={base}"));
        assert_eq!(bridge.members(), vec![1, 2]);
        assert_eq!(bridge.port(1), Some(base + 1));
        assert!(UdpSocket::bind(loopback(base + 1)).is_err());
        let sockets: Vec<std::sync::Weak<UdpSocket>> =
            bridge.shared.sockets().values().map(Arc::downgrade).collect();
        assert_eq!(sockets.len(), 2);
        let started = Instant::now();
        bridge.stop();
        assert!(started.elapsed() < Duration::from_secs(2));
        assert!(
            sockets.iter().all(|s| s.upgrade().is_none()),
            "stop closes every member socket, which frees its port; re-binding the port could race \
             another test's socket"
        );
    }

    #[test]
    fn rejects_lobbies_with_nothing_to_bridge_or_bad_numbers() {
        let cfg = |self_member, members: &[u8]| BridgeConfig {
            self_member,
            members: members.to_vec(),
            key: KEY,
            game_port: DEFAULT_GAME_PORT,
        };
        for bad in [cfg(1, &[1]), cfg(0, &[0, 1]), cfg(1, &[1, 255]), cfg(2, &[])] {
            let err = Bridge::start_echo(bad).err().unwrap();
            assert_eq!(err.kind(), io::ErrorKind::InvalidInput);
        }
    }

    #[test]
    fn takes_the_game_port_from_the_core_status_file() {
        let dir = std::env::temp_dir().join(format!("swofty-netplay-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("swofty")).unwrap();
        let cfg = BridgeConfig {
            self_member: 2,
            members: vec![1, 2],
            key: KEY,
            game_port: DEFAULT_GAME_PORT,
        };
        let bridge = Bridge::start_echo(cfg).unwrap();
        assert_eq!(bridge.sync_game_port(&dir), None);
        let status = |base: u16| format!(r#"{{"pid":4242,"self":2,"base":{base},"game_port":51000}}"#);
        std::fs::write(dir.join(STATUS_FILE), status(bridge.base() + 1)).unwrap();
        assert_eq!(bridge.sync_game_port(&dir), None);
        assert_eq!(bridge.game_port(), DEFAULT_GAME_PORT);
        std::fs::write(dir.join(STATUS_FILE), status(bridge.base())).unwrap();
        assert_eq!(bridge.sync_game_port(&dir), Some(51000));
        assert_eq!(bridge.game_port(), 51000);
        std::fs::remove_dir_all(&dir).ok();
    }
}
