//! A TURN client (RFC 8656) over UDP that carries member datagrams through relays.
//!
//! One allocation per launcher; its relayed address is what the other members send to. Each remote
//! member gets a permission for its relayed IP and the channel `0x4000 + N`. Datagrams go as
//! ChannelData once the channel is bound and as Send indications before that. A background thread
//! keeps the allocation, permissions and channels alive; a reader thread routes everything the
//! server sends.
//!
//! The credentials and the relayed address are fixed for the life of a [`TurnTransport`]. When the
//! credentials expire (TURN REST credentials carry a TTL), the server rejects the next refresh, or
//! when the server loses the allocation (restart, failover, NAT rebinding), the transport turns
//! fatal: every call fails, [`TurnTransport::needs_reconnect`] reports true, and the error kind says
//! why (`PermissionDenied`: the credentials were rejected; `ConnectionAborted`: the allocation is
//! gone or the server stopped answering). Either way the only recovery is fresh credentials, a new
//! `connect`, and re-announcing the new relayed address to the lobby.

use std::collections::{HashMap, HashSet, VecDeque};
use std::io;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, ToSocketAddrs, UdpSocket};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard, PoisonError, mpsc};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use super::diag;
use super::stun::{self, Builder, Class, Kind, Message, attr, method};
use super::transport::{MAX_DATAGRAM, Transport};

const CHANNEL_BASE: u16 = 0x4000;
const READ_TICK: Duration = Duration::from_millis(100);
const INBOX_LIMIT: usize = 1024;
const RETRY_SOON: Duration = Duration::from_secs(5);
/// How long to wait before asking again for a peer the server forbids (403).
const RETRY_FORBIDDEN: Duration = Duration::from_secs(60);
/// A channel may not be rebound to another address until this long after its binding expired.
const CHANNEL_QUARANTINE: Duration = Duration::from_secs(300);
const DEFAULT_LIFETIME: u32 = 600;
/// How long a TURN permission lasts once installed (RFC 8656 §9).
const PERMISSION_LIFETIME: Duration = Duration::from_secs(300);
/// MD5 in PASSWORD-ALGORITHM: algorithm 1, no parameters.
const ALGORITHM_MD5: [u8; 4] = [0, 1, 0, 0];

#[derive(Clone, Copy, Debug)]
struct Timing {
    permission_every: Duration,
    channel_every: Duration,
    channel_lifetime: Duration,
    /// Refresh at least this often: the answer proves the allocation still exists for this
    /// 5-tuple, and the request keeps the NAT mapping open.
    liveness: Duration,
    tick: Duration,
    channels: bool,
}

const TIMING: Timing = Timing {
    permission_every: Duration::from_secs(240),
    channel_every: Duration::from_secs(540),
    channel_lifetime: Duration::from_secs(600),
    liveness: Duration::from_secs(20),
    tick: Duration::from_secs(1),
    channels: true,
};

/// Request retransmission (RFC 8489 §6.2.1): `sends` requests with the wait doubling from `rto`,
/// then `last × rto` for the final response.
#[derive(Clone, Copy, Debug)]
struct Retry {
    rto: Duration,
    sends: u32,
    last: u32,
    abortable: bool,
}

/// The RFC defaults: 39.5 s before giving up.
const PATIENT: Retry = Retry {
    rto: Duration::from_millis(500),
    sends: 7,
    last: 16,
    abortable: true,
};

/// 7.5 s, while a caller waits (picking a server, `set_peers`).
const QUICK: Retry = Retry {
    rto: Duration::from_millis(500),
    sends: 4,
    last: 8,
    abortable: true,
};

/// The release on drop: one retransmission, then let the allocation lapse on the server.
const PARTING: Retry = Retry {
    rto: Duration::from_millis(250),
    sends: 2,
    last: 2,
    abortable: false,
};

#[derive(Debug)]
enum Fail {
    Code(u16, String),
    Timeout,
    /// Answers came back but none carried a valid MESSAGE-INTEGRITY.
    Unsigned,
    Rejected(String),
    Stopped,
    Io(io::Error),
}

impl From<Fail> for io::Error {
    fn from(f: Fail) -> io::Error {
        match f {
            Fail::Code(code @ (401 | 441), reason) => io::Error::new(
                io::ErrorKind::PermissionDenied,
                format!("TURN credentials expired or rejected ({code} {reason})"),
            ),
            Fail::Code(code, reason) => io::Error::other(format!("TURN error {code} {reason}")),
            Fail::Timeout => io::Error::new(io::ErrorKind::TimedOut, "the TURN server didn't answer"),
            Fail::Unsigned => io::Error::new(
                io::ErrorKind::InvalidData,
                "the TURN server's answers failed the integrity check",
            ),
            Fail::Rejected(why) => io::Error::new(io::ErrorKind::InvalidData, why),
            Fail::Stopped => io::Error::new(io::ErrorKind::Interrupted, "the TURN client is closing"),
            Fail::Io(e) => e,
        }
    }
}

/// What to do with an answer that matched a pending transaction.
#[derive(Debug, PartialEq, Eq)]
enum Verdict {
    Accept,
    Ignore { unsigned: bool },
    Reject(String),
}

/// With credentials in play, everything but the 401 and 438 challenges must carry a valid
/// MESSAGE-INTEGRITY; the method must match the request; unknown comprehension-required
/// attributes fail the transaction.
fn judge(msg: &Message, method: u16, key: Option<&[u8; 16]>) -> Verdict {
    if msg.method != method {
        return Verdict::Ignore { unsigned: false };
    }
    let challenge = msg.class == Class::Error && matches!(msg.error(), Some((401 | 438, _)));
    if let Some(key) = key
        && !challenge
        && !msg.check_integrity(key)
    {
        return Verdict::Ignore { unsigned: true };
    }
    match msg.unknown_required() {
        Some(t) => Verdict::Reject(format!(
            "TURN answer has an unknown comprehension-required attribute 0x{t:04x}"
        )),
        None => Verdict::Accept,
    }
}

#[derive(Default)]
struct Auth {
    realm: String,
    nonce: String,
    key: Option<[u8; 16]>,
    algorithms: Option<Vec<u8>>,
}

struct Channel {
    addr: SocketAddr,
    bound: Option<Instant>,
    next: Instant,
}

#[derive(Default)]
struct Routes {
    members: HashMap<u8, SocketAddr>,
    by_addr: HashMap<SocketAddr, u8>,
    next_permission: HashMap<IpAddr, Instant>,
    /// When the server last accepted a permission (or a channel binding) for each peer IP.
    granted: HashMap<IpAddr, Instant>,
    channels: HashMap<u16, Channel>,
}

impl Routes {
    fn live_channel(&self, addr: SocketAddr, lifetime: Duration, now: Instant) -> Option<u16> {
        self.channels
            .iter()
            .find(|(_, c)| c.addr == addr && c.bound.is_some_and(|t| now < t + lifetime))
            .map(|(ch, _)| *ch)
    }

    fn is_member(&self, member: u8, addr: SocketAddr) -> bool {
        self.members.get(&member) == Some(&addr)
    }
}

/// Counters for diagnostics and tests.
#[derive(Clone, Copy, Debug, Default)]
pub struct TurnStats {
    pub refreshes: u64,
    pub permissions: u64,
    pub bindings: u64,
    pub channel_sends: u64,
    pub indication_sends: u64,
    pub stale_nonces: u64,
}

#[derive(Default)]
struct Counters {
    refreshes: AtomicU64,
    permissions: AtomicU64,
    bindings: AtomicU64,
    channel_sends: AtomicU64,
    indication_sends: AtomicU64,
    stale_nonces: AtomicU64,
}

struct Shared {
    socket: UdpSocket,
    username: String,
    password: String,
    timing: Timing,
    auth: Mutex<Auth>,
    pending: Mutex<HashMap<[u8; 12], mpsc::Sender<Message>>>,
    routes: Mutex<Routes>,
    inbox: Mutex<VecDeque<(u8, SocketAddr, Vec<u8>)>>,
    arrived: Condvar,
    fatal: Mutex<Option<(io::ErrorKind, String)>>,
    stop: AtomicBool,
    closed: AtomicBool,
    nudged: Mutex<bool>,
    nudge: Condvar,
    counters: Counters,
    failing: Mutex<HashSet<(&'static str, IpAddr)>>,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}

fn transaction_id() -> [u8; 12] {
    let mut t = [0u8; 12];
    crate::mpauth::getrandom(&mut t);
    t
}

fn transient(e: &io::Error) -> bool {
    matches!(
        e.kind(),
        io::ErrorKind::WouldBlock
            | io::ErrorKind::TimedOut
            | io::ErrorKind::Interrupted
            | io::ErrorKind::ConnectionRefused
            | io::ErrorKind::ConnectionReset
    )
}

struct PendingGuard<'a> {
    shared: &'a Shared,
    transaction: [u8; 12],
}

impl Drop for PendingGuard<'_> {
    fn drop(&mut self) {
        lock(&self.shared.pending).remove(&self.transaction);
    }
}

impl Shared {
    fn new(socket: UdpSocket, username: &str, password: &str, timing: Timing) -> Shared {
        Shared {
            socket,
            username: username.to_owned(),
            password: password.to_owned(),
            timing,
            auth: Mutex::default(),
            pending: Mutex::default(),
            routes: Mutex::default(),
            inbox: Mutex::default(),
            arrived: Condvar::new(),
            fatal: Mutex::default(),
            stop: AtomicBool::new(false),
            closed: AtomicBool::new(false),
            nudged: Mutex::new(false),
            nudge: Condvar::new(),
            counters: Counters::default(),
            failing: Mutex::default(),
        }
    }

    fn server(&self) -> String {
        self.socket
            .peer_addr()
            .map_or_else(|_| "?".to_owned(), |a| a.to_string())
    }

    fn track(&self, what: &'static str, peer: SocketAddr, failed: Option<&Fail>) {
        match failed {
            Some(Fail::Stopped) => {}
            Some(f) => {
                if lock(&self.failing).insert((what, peer.ip())) {
                    diag::line(format!(
                        "turn {}: {what} for {peer} failed: {}",
                        self.server(),
                        describe(f)
                    ));
                }
            }
            None => {
                if lock(&self.failing).remove(&(what, peer.ip())) {
                    diag::line(format!("turn {}: {what} for {peer} recovered", self.server()));
                }
            }
        }
    }

    fn check(&self) -> io::Result<()> {
        match &*lock(&self.fatal) {
            Some((kind, msg)) => Err(io::Error::new(*kind, msg.clone())),
            None => Ok(()),
        }
    }

    fn set_fatal(&self, kind: io::ErrorKind, msg: impl Into<String>) {
        let msg = msg.into();
        {
            let mut fatal = lock(&self.fatal);
            if fatal.is_none() && !self.stop.load(Ordering::Relaxed) {
                diag::line(format!("turn {}: allocation unusable: {msg}", self.server()));
            }
            fatal.get_or_insert((kind, msg));
        }
        let _inbox = lock(&self.inbox);
        self.arrived.notify_all();
    }

    fn fatal_from(&self, fail: &Fail) -> bool {
        match fail {
            Fail::Code(code @ (401 | 441), reason) => self.set_fatal(
                io::ErrorKind::PermissionDenied,
                format!("TURN credentials expired or rejected ({code} {reason})"),
            ),
            Fail::Code(437, reason) => self.set_fatal(
                io::ErrorKind::ConnectionAborted,
                format!("TURN allocation lost (437 {reason})"),
            ),
            _ => return false,
        }
        true
    }

    fn poke(&self) {
        *lock(&self.nudged) = true;
        self.nudge.notify_all();
    }

    fn nap(&self, wait: Duration) {
        let guard = lock(&self.nudged);
        let (mut guard, _) = self
            .nudge
            .wait_timeout_while(guard, wait, |n| !*n && !self.stop.load(Ordering::Relaxed))
            .unwrap_or_else(PoisonError::into_inner);
        *guard = false;
    }

    fn deliver(&self, member: u8, from: SocketAddr, data: &[u8]) {
        let mut inbox = lock(&self.inbox);
        if inbox.len() >= INBOX_LIMIT {
            inbox.pop_front();
        }
        inbox.push_back((member, from, data.to_vec()));
        self.arrived.notify_one();
    }

    fn dispatch(&self, buf: &[u8]) {
        match stun::kind(buf) {
            Kind::ChannelData => {
                let Some((channel, data)) = stun::parse_channel_data(buf) else {
                    return;
                };
                let source = {
                    let routes = lock(&self.routes);
                    routes
                        .channels
                        .get(&channel)
                        .and_then(|c| Some((*routes.by_addr.get(&c.addr)?, c.addr)))
                };
                if let Some((member, from)) = source {
                    self.deliver(member, from, data);
                }
            }
            Kind::Stun => {
                let Some(msg) = Message::parse(buf).filter(Message::fingerprint_ok) else {
                    return;
                };
                match msg.class {
                    Class::Success | Class::Error => {
                        if let Some(tx) = lock(&self.pending).get(&msg.transaction) {
                            let _ = tx.send(msg);
                        }
                    }
                    Class::Indication if msg.method == method::DATA => {
                        let (Some(peer), Some(data)) =
                            (msg.address(attr::XOR_PEER_ADDRESS), msg.get(attr::DATA))
                        else {
                            return;
                        };
                        let member = lock(&self.routes).by_addr.get(&peer).copied();
                        if let Some(member) = member {
                            self.deliver(member, peer, data);
                        }
                    }
                    _ => {}
                }
            }
            Kind::Other => {}
        }
    }

    fn read_loop(&self) {
        let mut buf = vec![0u8; 4096];
        while !self.closed.load(Ordering::Relaxed) {
            match self.socket.recv(&mut buf) {
                Ok(n) => self.dispatch(&buf[..n]),
                Err(e) if transient(&e) => {}
                Err(_) => std::thread::sleep(Duration::from_millis(10)),
            }
        }
    }

    fn exchange(
        &self,
        method: u16,
        bytes: &[u8],
        transaction: [u8; 12],
        key: Option<[u8; 16]>,
        retry: Retry,
    ) -> Result<Message, Fail> {
        let (tx, rx) = mpsc::channel();
        lock(&self.pending).insert(transaction, tx);
        let _guard = PendingGuard {
            shared: self,
            transaction,
        };
        let mut wait = retry.rto;
        let mut unsigned = false;
        for attempt in 0..retry.sends {
            match self.socket.send(bytes) {
                Ok(_) => {}
                Err(e) if transient(&e) => {}
                Err(e) => return Err(Fail::Io(e)),
            }
            let span = if attempt + 1 == retry.sends {
                retry.rto * retry.last
            } else {
                wait
            };
            let deadline = Instant::now() + span;
            loop {
                if retry.abortable && self.stop.load(Ordering::Relaxed) {
                    return Err(Fail::Stopped);
                }
                let left = deadline.saturating_duration_since(Instant::now());
                if left.is_zero() {
                    break;
                }
                match rx.recv_timeout(left.min(READ_TICK)) {
                    Ok(msg) => match judge(&msg, method, key.as_ref()) {
                        Verdict::Accept => return Ok(msg),
                        Verdict::Reject(why) => return Err(Fail::Rejected(why)),
                        Verdict::Ignore { unsigned: u } => unsigned |= u,
                    },
                    Err(mpsc::RecvTimeoutError::Timeout) => {}
                    Err(mpsc::RecvTimeoutError::Disconnected) => return Err(Fail::Stopped),
                }
            }
            wait *= 2;
        }
        Err(if unsigned { Fail::Unsigned } else { Fail::Timeout })
    }

    /// Run one request to completion, answering the 401 challenge and 438 stale nonces.
    fn transact(
        &self,
        method: u16,
        body: &dyn Fn(Builder) -> Builder,
        retry: Retry,
    ) -> Result<Message, Fail> {
        let mut last = Fail::Timeout;
        for _ in 0..4 {
            let (bytes, transaction, key, nonce) = {
                let auth = lock(&self.auth);
                let transaction = transaction_id();
                let mut b = body(Builder::new(method, Class::Request, transaction));
                if let Some(key) = auth.key {
                    if let Some(algorithms) = &auth.algorithms {
                        b = b
                            .attr(attr::PASSWORD_ALGORITHMS, algorithms)
                            .attr(attr::PASSWORD_ALGORITHM, &ALGORITHM_MD5);
                    }
                    b = b
                        .attr(attr::USERNAME, self.username.as_bytes())
                        .attr(attr::REALM, auth.realm.as_bytes())
                        .attr(attr::NONCE, auth.nonce.as_bytes())
                        .integrity(&key);
                }
                (
                    b.fingerprint().finish(),
                    transaction,
                    auth.key,
                    auth.nonce.clone(),
                )
            };
            let resp = self.exchange(method, &bytes, transaction, key, retry)?;
            if resp.class == Class::Success {
                return Ok(resp);
            }
            let (code, reason) = resp.error().unwrap_or((0, String::new()));
            let fresh = resp.text(attr::NONCE).filter(|n| *n != nonce);
            match (code, fresh) {
                (401, _) if key.is_none() => self.challenge(&resp)?,
                (401 | 438, Some(_)) => {
                    self.challenge(&resp)?;
                    self.counters.stale_nonces.fetch_add(1, Ordering::Relaxed);
                }
                _ => return Err(Fail::Code(code, reason)),
            }
            last = Fail::Code(code, reason);
        }
        Err(last)
    }

    fn challenge(&self, resp: &Message) -> Result<(), Fail> {
        let mut auth = lock(&self.auth);
        let (Some(realm), Some(nonce)) = (
            resp.text(attr::REALM)
                .or_else(|| Some(auth.realm.clone()).filter(|r| !r.is_empty())),
            resp.text(attr::NONCE),
        ) else {
            return Err(Fail::Code(401, "challenge without realm or nonce".to_owned()));
        };
        if let Some(algorithms) = resp.get(attr::PASSWORD_ALGORITHMS) {
            let md5 = algorithms
                .chunks(4)
                .any(|a| a.len() >= 2 && u16::from_be_bytes([a[0], a[1]]) == 1);
            if !md5 {
                return Err(Fail::Code(401, "server doesn't offer MD5 passwords".to_owned()));
            }
            auth.algorithms = Some(algorithms.to_vec());
        }
        auth.key = Some(stun::long_term_key(&self.username, &realm, &self.password));
        auth.realm = realm;
        auth.nonce = nonce;
        Ok(())
    }

    fn permit_one(&self, peer: SocketAddr, retry: Retry) -> Result<(), Fail> {
        let result = self.transact(
            method::CREATE_PERMISSION,
            &|b| b.address(attr::XOR_PEER_ADDRESS, peer),
            retry,
        );
        self.track("permission", peer, result.as_ref().err());
        let now = Instant::now();
        let next = match &result {
            Ok(_) => now + self.timing.permission_every,
            Err(Fail::Code(403, _)) => now + RETRY_FORBIDDEN,
            Err(_) => now + RETRY_SOON,
        };
        let mut routes = lock(&self.routes);
        routes.next_permission.insert(peer.ip(), next);
        if result.is_ok() {
            routes.granted.insert(peer.ip(), now);
            self.counters.permissions.fetch_add(1, Ordering::Relaxed);
        }
        drop(routes);
        result.map(|_| ())
    }

    /// One CreatePermission per IP, all at once, so a peer the server forbids doesn't take the
    /// others down with it. Returns the ones that failed.
    fn permit(&self, peers: &[SocketAddr], retry: Retry) -> Vec<(SocketAddr, Fail)> {
        std::thread::scope(|s| {
            let asks: Vec<_> = peers
                .iter()
                .map(|p| (*p, s.spawn(move || self.permit_one(*p, retry))))
                .collect();
            asks.into_iter()
                .filter_map(|(p, ask)| match ask.join() {
                    Ok(Ok(())) => None,
                    Ok(Err(f)) => Some((p, f)),
                    Err(_) => Some((p, Fail::Stopped)),
                })
                .collect()
        })
    }

    fn due_permissions(&self, now: Instant) -> Vec<SocketAddr> {
        let routes = lock(&self.routes);
        let mut due: Vec<SocketAddr> = Vec::new();
        for a in routes.members.values() {
            let expiring = routes.next_permission.get(&a.ip()).is_none_or(|t| now >= *t);
            if expiring && !due.iter().any(|d| d.ip() == a.ip()) {
                due.push(*a);
            }
        }
        due
    }

    fn keep_permissions(&self) -> Result<(), Fail> {
        let due = self.due_permissions(Instant::now());
        for (_, f) in self.permit(&due, PATIENT) {
            if self.fatal_from(&f) || matches!(f, Fail::Stopped) {
                return Err(f);
            }
        }
        Ok(())
    }

    fn keep_channels(&self) -> Result<(), Fail> {
        let t = self.timing;
        let now = Instant::now();
        let work: Vec<(u16, SocketAddr)> = {
            let mut routes = lock(&self.routes);
            let wanted: Vec<SocketAddr> = routes.members.values().copied().collect();
            routes.channels.retain(|_, c| {
                wanted.contains(&c.addr)
                    || c.bound
                        .is_some_and(|b| now < b + t.channel_lifetime + CHANNEL_QUARANTINE)
            });
            let members: Vec<(u8, SocketAddr)> = routes.members.iter().map(|(m, a)| (*m, *a)).collect();
            let mut work = Vec::new();
            for (member, addr) in members {
                let existing = routes
                    .channels
                    .iter()
                    .find(|(_, c)| c.addr == addr)
                    .map(|(ch, c)| (*ch, c.next));
                match existing {
                    Some((ch, next)) if now >= next => work.push((ch, addr)),
                    Some(_) => {}
                    None => {
                        let ch = CHANNEL_BASE + u16::from(member);
                        let taken = routes.channels.get(&ch).is_some_and(|c| c.addr != addr);
                        if !taken {
                            routes.channels.insert(
                                ch,
                                Channel {
                                    addr,
                                    bound: None,
                                    next: now,
                                },
                            );
                            work.push((ch, addr));
                        }
                    }
                }
            }
            work
        };
        for (ch, addr) in work {
            if self.stop.load(Ordering::Relaxed) {
                return Err(Fail::Stopped);
            }
            let result = self.transact(
                method::CHANNEL_BIND,
                &|b| b.channel(ch).address(attr::XOR_PEER_ADDRESS, addr),
                PATIENT,
            );
            self.track("channel bind", addr, result.as_ref().err());
            let now = Instant::now();
            let mut routes = lock(&self.routes);
            if let Some(c) = routes.channels.get_mut(&ch).filter(|c| c.addr == addr) {
                match &result {
                    Ok(_) => {
                        c.bound = Some(now);
                        c.next = now + t.channel_every;
                    }
                    Err(Fail::Code(403, _)) => c.next = now + RETRY_FORBIDDEN,
                    Err(_) => c.next = now + RETRY_SOON,
                }
            }
            if result.is_ok() {
                routes.next_permission.insert(addr.ip(), now + t.permission_every);
                routes.granted.insert(addr.ip(), now);
                self.counters.bindings.fetch_add(1, Ordering::Relaxed);
            }
            drop(routes);
            if let Err(f) = result
                && (self.fatal_from(&f) || matches!(f, Fail::Stopped))
            {
                return Err(f);
            }
        }
        Ok(())
    }

    fn refresh_every(&self, lifetime: Duration) -> Duration {
        refresh_after(lifetime).min(self.timing.liveness)
    }

    fn maintain(&self, mut lifetime: Duration) {
        let mut granted = Instant::now();
        let mut next_refresh = granted + self.refresh_every(lifetime);
        loop {
            self.nap(self.timing.tick);
            if self.stop.load(Ordering::Relaxed) {
                return;
            }
            let now = Instant::now();
            if now >= next_refresh {
                let asked = lifetime.as_secs() as u32;
                match self.transact(method::REFRESH, &|b| b.u32(attr::LIFETIME, asked), PATIENT) {
                    Ok(resp) => {
                        let secs = resp.u32(attr::LIFETIME).unwrap_or(asked);
                        lifetime = Duration::from_secs(u64::from(secs));
                        granted = now;
                        next_refresh = now + self.refresh_every(lifetime);
                        self.counters.refreshes.fetch_add(1, Ordering::Relaxed);
                    }
                    Err(Fail::Stopped) => return,
                    Err(f) if self.fatal_from(&f) => return,
                    Err(f @ Fail::Code(..)) if Instant::now() < granted + lifetime => {
                        diag::line(format!(
                            "turn {}: refresh failed: {}; retrying while the allocation lives",
                            self.server(),
                            describe(&f)
                        ));
                        next_refresh = Instant::now() + RETRY_SOON.min(lifetime / 4);
                    }
                    Err(f) => {
                        self.set_fatal(
                            io::ErrorKind::ConnectionAborted,
                            format!("TURN allocation lost: {}", io::Error::from(f)),
                        );
                        return;
                    }
                }
            }
            if self.keep_permissions().is_err() && self.check().is_err() {
                return;
            }
            if self.timing.channels && self.keep_channels().is_err() && self.check().is_err() {
                return;
            }
        }
    }
}

fn describe(f: &Fail) -> String {
    match f {
        Fail::Code(code, reason) => format!("TURN error {code} {reason}"),
        Fail::Timeout => "the TURN server didn't answer".to_owned(),
        Fail::Unsigned => "the TURN server's answers failed the integrity check".to_owned(),
        Fail::Rejected(why) => why.clone(),
        Fail::Stopped => "the TURN client is closing".to_owned(),
        Fail::Io(e) => e.to_string(),
    }
}

/// Refresh a minute before expiry, or halfway through a lifetime shorter than two minutes.
fn refresh_after(lifetime: Duration) -> Duration {
    lifetime - Duration::from_secs(60).min(lifetime / 2)
}

/// `(host, port)` of each `turn:` URL that runs over UDP, 3478 first, then 443, then the rest.
fn udp_servers(urls: &[String]) -> Vec<(String, u16)> {
    let mut servers: Vec<(String, u16)> = Vec::new();
    for url in urls {
        let Some(rest) = url.trim().strip_prefix("turn:") else {
            continue;
        };
        let (hostport, query) = rest.split_once('?').unwrap_or((rest, ""));
        let transport = query
            .split('&')
            .find_map(|kv| kv.strip_prefix("transport="))
            .unwrap_or("udp");
        if !transport.eq_ignore_ascii_case("udp") {
            continue;
        }
        let (host, port) = match hostport.strip_prefix('[') {
            Some(v6) => match v6.split_once(']') {
                Some((h, p)) => (h, p.strip_prefix(':')),
                None => continue,
            },
            None => match hostport.rsplit_once(':') {
                Some((h, p)) => (h, Some(p)),
                None => (hostport, None),
            },
        };
        let Some(port) = port.map_or(Some(3478), |p| p.parse().ok()) else {
            continue;
        };
        if !host.is_empty() && !servers.iter().any(|(h, p)| h == host && *p == port) {
            servers.push((host.to_owned(), port));
        }
    }
    servers.sort_by_key(|(_, port)| match port {
        3478 => 0,
        443 => 1,
        _ => 2,
    });
    servers
}

/// A [`Transport`] over one TURN allocation.
pub struct TurnTransport {
    shared: Arc<Shared>,
    server: SocketAddr,
    relayed: SocketAddr,
    mapped: Option<SocketAddr>,
    reader: Option<JoinHandle<()>>,
    keeper: Option<JoinHandle<()>>,
}

impl TurnTransport {
    /// Allocate a relay on the first reachable `turn:` UDP URL (port 3478, then 443) with the
    /// TURN REST `username` and `credential`.
    pub fn connect(urls: &[String], username: &str, credential: &str) -> io::Result<TurnTransport> {
        Self::connect_with(urls, username, credential, TIMING)
    }

    fn connect_with(
        urls: &[String],
        username: &str,
        credential: &str,
        timing: Timing,
    ) -> io::Result<TurnTransport> {
        let servers = udp_servers(urls);
        if servers.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "no turn: URL with transport=udp",
            ));
        }
        let mut last = None;
        for (host, port) in servers {
            let mut addrs: Vec<SocketAddr> = match (host.as_str(), port).to_socket_addrs() {
                Ok(a) => a.collect(),
                Err(e) => {
                    diag::line(format!("turn: couldn't resolve {host}:{port}: {e}"));
                    last = Some(e);
                    continue;
                }
            };
            addrs.sort_by_key(SocketAddr::is_ipv6);
            addrs.dedup_by_key(|a| a.is_ipv6());
            for server in addrs {
                let started = Instant::now();
                match Self::allocate(server, username, credential, timing) {
                    Ok(t) => {
                        diag::line(format!(
                            "turn {server}: allocated in {} ms, relayed {}",
                            started.elapsed().as_millis(),
                            t.relayed
                        ));
                        return Ok(t);
                    }
                    Err(e) => {
                        diag::line(format!(
                            "turn {server}: allocate failed after {} ms: {e}",
                            started.elapsed().as_millis()
                        ));
                        if e.kind() == io::ErrorKind::PermissionDenied {
                            return Err(e);
                        }
                        last = Some(e);
                    }
                }
            }
        }
        Err(last.unwrap_or_else(|| io::Error::new(io::ErrorKind::NotFound, "no TURN server address")))
    }

    fn allocate(
        server: SocketAddr,
        username: &str,
        credential: &str,
        timing: Timing,
    ) -> io::Result<TurnTransport> {
        let any: IpAddr = if server.is_ipv4() {
            Ipv4Addr::UNSPECIFIED.into()
        } else {
            Ipv6Addr::UNSPECIFIED.into()
        };
        let socket = UdpSocket::bind(SocketAddr::new(any, 0))?;
        socket.connect(server)?;
        socket.set_read_timeout(Some(READ_TICK))?;
        let shared = Arc::new(Shared::new(socket, username, credential, timing));
        let reader = {
            let shared = shared.clone();
            std::thread::Builder::new()
                .name("turn-reader".into())
                .spawn(move || shared.read_loop())?
        };
        let mut transport = TurnTransport {
            shared: shared.clone(),
            server,
            relayed: SocketAddr::new(any, 0),
            mapped: None,
            reader: Some(reader),
            keeper: None,
        };
        let resp = shared.transact(
            method::ALLOCATE,
            &|b| b.attr(attr::REQUESTED_TRANSPORT, &stun::TRANSPORT_UDP),
            QUICK,
        )?;
        transport.relayed = resp.address(attr::XOR_RELAYED_ADDRESS).ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                "Allocate answer has no relayed address",
            )
        })?;
        transport.mapped = resp.address(attr::XOR_MAPPED_ADDRESS);
        let lifetime = Duration::from_secs(u64::from(resp.u32(attr::LIFETIME).unwrap_or(DEFAULT_LIFETIME)));
        transport.keeper = Some(
            std::thread::Builder::new()
                .name("turn-keeper".into())
                .spawn(move || shared.maintain(lifetime))?,
        );
        Ok(transport)
    }

    /// The TURN server this allocation lives on.
    pub fn server_addr(&self) -> SocketAddr {
        self.server
    }

    /// This launcher's public address as the server saw it (XOR-MAPPED-ADDRESS).
    pub fn mapped_addr(&self) -> Option<SocketAddr> {
        self.mapped
    }

    /// True once the allocation is unusable (credentials expired or rejected, allocation lost,
    /// server gone). Mint fresh credentials, `connect` again and announce the new relayed address.
    pub fn needs_reconnect(&self) -> bool {
        self.shared.check().is_err()
    }

    pub fn stats(&self) -> TurnStats {
        let c = &self.shared.counters;
        TurnStats {
            refreshes: c.refreshes.load(Ordering::Relaxed),
            permissions: c.permissions.load(Ordering::Relaxed),
            bindings: c.bindings.load(Ordering::Relaxed),
            channel_sends: c.channel_sends.load(Ordering::Relaxed),
            indication_sends: c.indication_sends.load(Ordering::Relaxed),
            stale_nonces: c.stale_nonces.load(Ordering::Relaxed),
        }
    }

    /// Whether the server currently permits datagrams to and from `member`'s relayed address. A
    /// permission refused at [`Transport::set_peers`] is retried in the background; this turns
    /// true once a retry succeeds.
    pub fn permitted(&self, member: u8) -> bool {
        let routes = lock(&self.shared.routes);
        routes.members.get(&member).is_some_and(|a| {
            routes
                .granted
                .get(&a.ip())
                .is_some_and(|at| at.elapsed() < PERMISSION_LIFETIME)
        })
    }

    /// Whether datagrams to `member` currently go over a bound channel.
    pub fn channel_bound(&self, member: u8) -> bool {
        let routes = lock(&self.shared.routes);
        routes.members.get(&member).is_some_and(|a| {
            routes
                .live_channel(*a, self.shared.timing.channel_lifetime, Instant::now())
                .is_some()
        })
    }
}

impl Transport for TurnTransport {
    fn local_addr(&self) -> SocketAddr {
        self.relayed
    }

    fn permitted(&self, member: u8) -> bool {
        TurnTransport::permitted(self, member)
    }

    /// Installs every peer it can; an `AddrNotAvailable` error names the members whose
    /// permission the server refused (they are retried in the background), while the others
    /// work. A credential or allocation failure is fatal instead.
    fn set_peers(&self, peers: &HashMap<u8, SocketAddr>) -> io::Result<()> {
        self.shared.check()?;
        let due: Vec<SocketAddr> = {
            let mut routes = lock(&self.shared.routes);
            routes.members = peers.clone();
            routes.by_addr = peers.iter().map(|(m, a)| (*a, *m)).collect();
            routes
                .next_permission
                .retain(|ip, _| peers.values().any(|a| a.ip() == *ip));
            routes
                .granted
                .retain(|ip, _| peers.values().any(|a| a.ip() == *ip));
            let mut due: Vec<SocketAddr> = Vec::new();
            for a in peers.values() {
                if !routes.next_permission.contains_key(&a.ip()) && !due.iter().any(|d| d.ip() == a.ip()) {
                    due.push(*a);
                }
            }
            due
        };
        lock(&self.shared.inbox).retain(|(m, from, _)| peers.get(m) == Some(from));
        let failures = self.shared.permit(&due, QUICK);
        self.shared.poke();
        let mut refused = Vec::new();
        for (addr, f) in failures {
            if self.shared.fatal_from(&f) {
                return self.shared.check();
            }
            let mut members: Vec<u8> = peers
                .iter()
                .filter(|(_, a)| a.ip() == addr.ip())
                .map(|(m, _)| *m)
                .collect();
            members.sort_unstable();
            let error = io::Error::from(f);
            refused.extend(members.iter().map(|m| format!("member {m} ({addr}): {error}")));
        }
        if refused.is_empty() {
            Ok(())
        } else {
            Err(io::Error::new(
                io::ErrorKind::AddrNotAvailable,
                format!("no TURN permission for {}", refused.join("; ")),
            ))
        }
    }

    fn send(&self, to: u8, payload: &[u8]) -> io::Result<()> {
        self.shared.check()?;
        if payload.len() > MAX_DATAGRAM {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!(
                    "{} bytes is over the {MAX_DATAGRAM}-byte datagram limit",
                    payload.len()
                ),
            ));
        }
        let (addr, channel) = {
            let routes = lock(&self.shared.routes);
            let Some(addr) = routes.members.get(&to).copied() else {
                return Err(io::Error::new(
                    io::ErrorKind::NotFound,
                    format!("member {to} isn't a peer"),
                ));
            };
            let lifetime = self.shared.timing.channel_lifetime;
            (addr, routes.live_channel(addr, lifetime, Instant::now()))
        };
        let c = &self.shared.counters;
        let packet = match channel {
            Some(ch) => {
                c.channel_sends.fetch_add(1, Ordering::Relaxed);
                stun::channel_data(ch, payload, false)
            }
            None => {
                c.indication_sends.fetch_add(1, Ordering::Relaxed);
                Builder::new(method::SEND, Class::Indication, transaction_id())
                    .address(attr::XOR_PEER_ADDRESS, addr)
                    .attr(attr::DATA, payload)
                    .finish()
            }
        };
        match self.shared.socket.send(&packet) {
            Ok(_) => Ok(()),
            Err(e) if transient(&e) => Ok(()),
            Err(e) => Err(e),
        }
    }

    fn recv(&self, buf: &mut [u8], timeout: Duration) -> io::Result<Option<(u8, usize)>> {
        let deadline = Instant::now().checked_add(timeout);
        let mut inbox = lock(&self.shared.inbox);
        loop {
            while let Some((member, from, data)) = inbox.pop_front() {
                if !lock(&self.shared.routes).is_member(member, from) {
                    continue;
                }
                let n = data.len().min(buf.len());
                buf[..n].copy_from_slice(&data[..n]);
                return Ok(Some((member, n)));
            }
            self.shared.check()?;
            let left = match deadline {
                Some(d) => d.saturating_duration_since(Instant::now()),
                None => Duration::from_secs(3600),
            };
            if left.is_zero() {
                return Ok(None);
            }
            inbox = self
                .shared
                .arrived
                .wait_timeout(inbox, left)
                .unwrap_or_else(PoisonError::into_inner)
                .0;
        }
    }
}

impl Drop for TurnTransport {
    fn drop(&mut self) {
        self.shared.stop.store(true, Ordering::Relaxed);
        self.shared.poke();
        let allocated = self.keeper.take().map(|k| k.join()).is_some();
        if allocated && self.shared.check().is_ok() {
            let _ = self
                .shared
                .transact(method::REFRESH, &|b| b.u32(attr::LIFETIME, 0), PARTING);
        }
        self.shared.closed.store(true, Ordering::Relaxed);
        if let Some(r) = self.reader.take() {
            let _ = r.join();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn udp_servers_prefer_3478_then_443() {
        let urls: Vec<String> = [
            "stun:stun.cloudflare.com:3478",
            "turn:turn.cloudflare.com:443?transport=udp",
            "turn:turn.cloudflare.com:3478?transport=tcp",
            "turns:turn.cloudflare.com:5349?transport=tcp",
            "turn:turn.cloudflare.com:3478?transport=udp",
            "turn:turn.cloudflare.com:80?transport=tcp",
            "turn:[2001:db8::1]:3479",
            "turn:relay.example",
        ]
        .map(String::from)
        .to_vec();
        assert_eq!(
            udp_servers(&urls),
            vec![
                ("turn.cloudflare.com".to_owned(), 3478),
                ("relay.example".to_owned(), 3478),
                ("turn.cloudflare.com".to_owned(), 443),
                ("2001:db8::1".to_owned(), 3479),
            ]
        );
    }

    #[test]
    fn refresh_runs_before_expiry() {
        assert_eq!(refresh_after(Duration::from_secs(600)), Duration::from_secs(540));
        assert_eq!(refresh_after(Duration::from_secs(10)), Duration::from_secs(5));
    }

    #[test]
    fn connect_needs_a_udp_turn_url() {
        let urls = vec!["turns:turn.cloudflare.com:443?transport=tcp".to_owned()];
        let err = TurnTransport::connect(&urls, "u", "p").err().unwrap();
        assert_eq!(err.kind(), io::ErrorKind::InvalidInput);
    }

    struct Coturn {
        name: &'static str,
        url: Vec<String>,
    }

    impl Coturn {
        fn start(name: &'static str, port: u16, extra: &[&str]) -> Coturn {
            let _ = std::process::Command::new("docker")
                .args(["rm", "-f", name])
                .output();
            let port_s = port.to_string();
            let (lo, hi) = ((port + 10).to_string(), (port + 60).to_string());
            let out = std::process::Command::new("docker")
                .args(["run", "-d", "--name", name, "--network", "host", "coturn/coturn"])
                .args([
                    "-n",
                    "--lt-cred-mech",
                    "--user",
                    "u:p",
                    "--realm",
                    "test",
                    "--fingerprint",
                ])
                .args(["--listening-ip", "127.0.0.1", "--relay-ip", "127.0.0.1"])
                .args(["--listening-port", &port_s, "--min-port", &lo, "--max-port", &hi])
                .args(["--allow-loopback-peers", "--no-tls", "--no-cli"])
                .args(["--cli-password", "unused", "--log-file", "stdout"])
                .args(extra)
                .output()
                .expect("docker");
            assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
            Coturn {
                name,
                url: vec![format!("turn:127.0.0.1:{port}?transport=udp")],
            }
        }

        fn connect(&self, timing: Timing) -> TurnTransport {
            let mut last = None;
            for _ in 0..3 {
                match TurnTransport::connect_with(&self.url, "u", "p", timing) {
                    Ok(t) => return t,
                    Err(e) => last = Some(e),
                }
            }
            let logs = std::process::Command::new("docker")
                .args(["logs", "--tail", "20", self.name])
                .output()
                .map(|o| String::from_utf8_lossy(&o.stderr).into_owned())
                .unwrap_or_default();
            panic!("allocate on coturn: {last:?}\n{logs}");
        }
    }

    impl Drop for Coturn {
        fn drop(&mut self) {
            let _ = std::process::Command::new("docker")
                .args(["rm", "-f", self.name])
                .output();
        }
    }

    fn pair(a: &TurnTransport, b: &TurnTransport) {
        a.set_peers(&HashMap::from([(2, b.local_addr())])).unwrap();
        b.set_peers(&HashMap::from([(1, a.local_addr())])).unwrap();
    }

    fn expect(t: &TurnTransport, from: u8, payload: &[u8]) {
        let mut buf = [0u8; MAX_DATAGRAM];
        let got = t.recv(&mut buf, Duration::from_secs(3)).unwrap();
        assert_eq!(got, Some((from, payload.len())));
        assert_eq!(&buf[..payload.len()], payload);
    }

    fn wait_bound(t: &TurnTransport, member: u8) {
        let start = Instant::now();
        while !t.channel_bound(member) {
            assert!(
                start.elapsed() < Duration::from_secs(5),
                "channel to {member} never bound"
            );
            std::thread::sleep(Duration::from_millis(20));
        }
    }

    fn offline() -> TurnTransport {
        let socket = UdpSocket::bind("127.0.0.1:0").unwrap();
        let addr = socket.local_addr().unwrap();
        TurnTransport {
            shared: Arc::new(Shared::new(socket, "u", "p", TIMING)),
            server: addr,
            relayed: addr,
            mapped: None,
            reader: None,
            keeper: None,
        }
    }

    fn data_indication(from: SocketAddr, payload: &[u8]) -> Vec<u8> {
        Builder::new(method::DATA, Class::Indication, transaction_id())
            .address(attr::XOR_PEER_ADDRESS, from)
            .attr(attr::DATA, payload)
            .finish()
    }

    #[test]
    fn queued_datagrams_follow_the_address_not_the_slot() {
        let t = offline();
        let x: SocketAddr = "198.51.100.1:5000".parse().unwrap();
        let y: SocketAddr = "198.51.100.2:6000".parse().unwrap();
        let far = Instant::now() + Duration::from_secs(3600);
        let permitted = |ip: SocketAddr| {
            lock(&t.shared.routes).next_permission.insert(ip.ip(), far);
        };
        permitted(x);
        t.set_peers(&HashMap::from([(3, x)])).unwrap();
        t.shared.dispatch(&data_indication(x, b"from x"));
        permitted(y);
        t.set_peers(&HashMap::from([(3, y)])).unwrap();
        let mut buf = [0u8; 64];
        assert_eq!(t.recv(&mut buf, Duration::from_millis(20)).unwrap(), None);

        t.shared.deliver(3, x, b"raced the change");
        assert_eq!(t.recv(&mut buf, Duration::from_millis(20)).unwrap(), None);
        t.shared.dispatch(&data_indication(y, b"from y"));
        assert_eq!(t.recv(&mut buf, Duration::from_millis(20)).unwrap(), Some((3, 6)));
        assert_eq!(&buf[..6], b"from y");

        for (ch, addr) in [(0x4003, x), (0x4004, y)] {
            let bound = Channel {
                addr,
                bound: Some(Instant::now()),
                next: far,
            };
            lock(&t.shared.routes).channels.insert(ch, bound);
        }
        t.shared.dispatch(&stun::channel_data(0x4003, b"old", false));
        t.shared.dispatch(&stun::channel_data(0x4004, b"new", false));
        assert_eq!(t.recv(&mut buf, Duration::from_millis(20)).unwrap(), Some((3, 3)));
        assert_eq!(&buf[..3], b"new");
        assert_eq!(t.recv(&mut buf, Duration::from_millis(20)).unwrap(), None);
    }

    #[test]
    fn a_member_is_permitted_only_while_its_permission_lives() {
        let t = offline();
        let x: SocketAddr = "198.51.100.1:5000".parse().unwrap();
        let y: SocketAddr = "198.51.100.2:6000".parse().unwrap();
        let far = Instant::now() + Duration::from_secs(3600);
        for a in [x, y] {
            lock(&t.shared.routes).next_permission.insert(a.ip(), far);
        }
        t.set_peers(&HashMap::from([(2, x), (3, y)])).unwrap();
        assert!(!t.permitted(2) && !t.permitted(3) && !t.permitted(4));
        lock(&t.shared.routes).granted.insert(x.ip(), Instant::now());
        assert!(t.permitted(2), "a background retry that succeeds shows at once");
        assert!(!t.permitted(3));
        if let Some(old) = Instant::now().checked_sub(PERMISSION_LIFETIME + Duration::from_secs(1)) {
            lock(&t.shared.routes).granted.insert(y.ip(), old);
            assert!(!t.permitted(3), "an expired permission doesn't count");
        }
        t.set_peers(&HashMap::from([(3, y)])).unwrap();
        assert!(!t.permitted(2));
        assert!(!lock(&t.shared.routes).granted.contains_key(&x.ip()));
    }

    #[test]
    fn answers_need_integrity_and_the_right_method() {
        let key = stun::long_term_key("u", "test", "p");
        let tid = [9u8; 12];
        let relayed: SocketAddr = "203.0.113.5:50000".parse().unwrap();
        let allocated = || {
            Builder::new(method::ALLOCATE, Class::Success, tid).address(attr::XOR_RELAYED_ADDRESS, relayed)
        };
        let parse = |b: Builder| Message::parse(&b.finish()).unwrap();
        let error = |code: u16| {
            let value = [0, 0, (code / 100) as u8, (code % 100) as u8];
            Builder::new(method::REFRESH, Class::Error, tid).attr(attr::ERROR_CODE, &value)
        };
        let signed = parse(allocated().integrity(&key));
        let unsigned = Verdict::Ignore { unsigned: true };

        assert_eq!(judge(&signed, method::ALLOCATE, Some(&key)), Verdict::Accept);
        assert_eq!(judge(&parse(allocated()), method::ALLOCATE, Some(&key)), unsigned);
        assert_eq!(
            judge(
                &parse(allocated().integrity(b"other")),
                method::ALLOCATE,
                Some(&key)
            ),
            unsigned
        );
        assert_eq!(
            judge(&signed, method::REFRESH, Some(&key)),
            Verdict::Ignore { unsigned: false }
        );
        assert_eq!(judge(&parse(error(437)), method::REFRESH, Some(&key)), unsigned);
        assert_eq!(
            judge(&parse(error(437).integrity(&key)), method::REFRESH, Some(&key)),
            Verdict::Accept
        );
        assert_eq!(
            judge(&parse(error(401)), method::REFRESH, Some(&key)),
            Verdict::Accept
        );
        assert_eq!(
            judge(&parse(error(438)), method::REFRESH, Some(&key)),
            Verdict::Accept
        );
        assert_eq!(
            judge(&parse(allocated()), method::ALLOCATE, None),
            Verdict::Accept
        );
        let unknown = parse(allocated().attr(0x0031, b"?").integrity(&key));
        assert!(matches!(
            judge(&unknown, method::ALLOCATE, Some(&key)),
            Verdict::Reject(_)
        ));
    }

    #[test]
    #[ignore = "needs docker and the coturn/coturn image"]
    fn docker_coturn_members_exchange_datagrams() {
        let server = Coturn::start("swofty-turn-test-exchange", 34780, &[]);

        let unbound = Timing {
            channels: false,
            ..TIMING
        };
        let (a, b) = (server.connect(unbound), server.connect(unbound));
        assert_eq!(a.local_addr().ip(), IpAddr::from([127, 0, 0, 1]));
        assert_ne!(a.local_addr(), b.local_addr());
        let wrong = TurnTransport::connect_with(&server.url, "u", "wrong", TIMING)
            .err()
            .unwrap();
        assert_eq!(wrong.kind(), io::ErrorKind::PermissionDenied, "{wrong}");
        pair(&a, &b);
        a.send(2, b"over a send indication").unwrap();
        expect(&b, 1, b"over a send indication");
        b.send(1, b"and back as a data indication").unwrap();
        expect(&a, 2, b"and back as a data indication");
        assert_eq!(a.stats().channel_sends, 0);
        drop((a, b));

        let (a, b) = (server.connect(TIMING), server.connect(TIMING));
        pair(&a, &b);
        wait_bound(&a, 2);
        wait_bound(&b, 1);
        for len in [1usize, 2, 3, 4, 5, 63, MAX_DATAGRAM] {
            let payload: Vec<u8> = (0..len).map(|i| (i * 7 + len) as u8).collect();
            a.send(2, &payload).unwrap();
            expect(&b, 1, &payload);
            b.send(1, &payload).unwrap();
            expect(&a, 2, &payload);
        }
        for i in 0..200u32 {
            a.send(2, &i.to_be_bytes()).unwrap();
        }
        for i in 0..200u32 {
            expect(&b, 1, &i.to_be_bytes());
        }
        assert!(a.stats().channel_sends >= 207);
        assert_eq!(a.stats().indication_sends, 0);

        let mut buf = [0u8; 64];
        let start = Instant::now();
        assert_eq!(a.recv(&mut buf, Duration::from_millis(150)).unwrap(), None);
        assert!(start.elapsed() >= Duration::from_millis(150));
        assert_eq!(a.send(9, b"x").unwrap_err().kind(), io::ErrorKind::NotFound);
        assert!(a.send(2, &[0u8; MAX_DATAGRAM + 1]).is_err());

        b.set_peers(&HashMap::new()).unwrap();
        a.send(2, b"to a member that left").unwrap();
        assert_eq!(b.recv(&mut buf, Duration::from_millis(500)).unwrap(), None);
    }

    fn ping_pong(a: &TurnTransport, b: &TurnTransport, run: Duration) {
        let start = Instant::now();
        let mut round = 0u32;
        while start.elapsed() < run {
            a.send(2, &round.to_be_bytes()).unwrap();
            expect(b, 1, &round.to_be_bytes());
            b.send(1, &round.to_be_bytes()).unwrap();
            expect(a, 2, &round.to_be_bytes());
            round += 1;
            std::thread::sleep(Duration::from_millis(250));
        }
    }

    fn until_fatal(t: &TurnTransport, within: Duration) -> io::Error {
        let start = Instant::now();
        loop {
            match t.send(2, b"is anyone there") {
                Err(e) => return e,
                Ok(()) => {
                    assert!(start.elapsed() < within, "the failure never surfaced");
                    std::thread::sleep(Duration::from_millis(100));
                }
            }
        }
    }

    #[test]
    #[ignore = "needs docker and the coturn/coturn image"]
    fn docker_coturn_refresh_keeps_short_lifetimes_alive() {
        let server = Coturn::start(
            "swofty-turn-test-refresh",
            34880,
            &[
                "--max-allocate-lifetime=6",
                "--permission-lifetime=6",
                "--channel-lifetime=6",
                "--stale-nonce=3",
            ],
        );
        let short = Timing {
            permission_every: Duration::from_secs(3),
            channel_every: Duration::from_secs(3),
            channel_lifetime: Duration::from_secs(6),
            liveness: Duration::from_secs(3),
            tick: Duration::from_millis(250),
            channels: true,
        };
        let (a, b) = (server.connect(short), server.connect(short));
        pair(&a, &b);
        wait_bound(&a, 2);
        wait_bound(&b, 1);
        ping_pong(&a, &b, Duration::from_secs(20));
        let stats = a.stats();
        assert!(stats.refreshes >= 4, "{stats:?}");
        assert!(stats.bindings >= 4, "{stats:?}");
        assert!(stats.stale_nonces >= 2, "{stats:?}");
        assert_eq!(stats.indication_sends, 0, "{stats:?}");
        assert!(a.channel_bound(2) && b.channel_bound(1));
    }

    #[test]
    #[ignore = "needs docker and the coturn/coturn image"]
    fn docker_coturn_forbidden_peer_does_not_block_the_others() {
        let server = Coturn::start(
            "swofty-turn-test-forbidden",
            34980,
            &["--permission-lifetime=6", "--denied-peer-ip=10.99.0.1"],
        );
        let timing = Timing {
            permission_every: Duration::from_secs(3),
            tick: Duration::from_millis(250),
            channels: false,
            ..TIMING
        };
        let (a, b) = (server.connect(timing), server.connect(timing));
        let bogus: SocketAddr = "10.99.0.1:5000".parse().unwrap();
        let err = a
            .set_peers(&HashMap::from([(2, b.local_addr()), (3, bogus)]))
            .unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::AddrNotAvailable, "{err}");
        assert!(err.to_string().contains("member 3"), "{err}");
        assert!(!err.to_string().contains("member 2"), "{err}");
        assert!(a.permitted(2) && !a.permitted(3));
        b.set_peers(&HashMap::from([(1, a.local_addr())])).unwrap();
        ping_pong(&a, &b, Duration::from_secs(14));
        assert!(a.stats().permissions >= 4, "{:?}", a.stats());
        assert!(!a.needs_reconnect());
    }

    #[test]
    #[ignore = "needs docker and the coturn/coturn image"]
    fn docker_coturn_lost_allocation_surfaces_within_seconds() {
        let server = Coturn::start("swofty-turn-test-liveness", 35180, &[]);
        let timing = Timing {
            liveness: Duration::from_secs(2),
            tick: Duration::from_millis(250),
            ..TIMING
        };
        let (a, b) = (server.connect(timing), server.connect(timing));
        pair(&a, &b);
        wait_bound(&a, 2);
        ping_pong(&a, &b, Duration::from_secs(1));
        let restart = std::process::Command::new("docker")
            .args(["restart", "-t", "0", server.name])
            .output()
            .unwrap();
        assert!(restart.status.success());
        let err = until_fatal(&a, Duration::from_secs(10));
        assert_eq!(err.kind(), io::ErrorKind::ConnectionAborted, "{err}");
        assert!(a.needs_reconnect());
        let mut buf = [0u8; 16];
        assert!(a.recv(&mut buf, Duration::from_secs(1)).is_err());
    }

    fn fake_turn_rejecting_refreshes() -> SocketAddr {
        let socket = UdpSocket::bind("127.0.0.1:0").unwrap();
        let addr = socket.local_addr().unwrap();
        socket.set_read_timeout(Some(Duration::from_secs(10))).unwrap();
        std::thread::spawn(move || {
            let mut buf = [0u8; 2048];
            let mut nonces = 0u32;
            while let Ok((n, from)) = socket.recv_from(&mut buf) {
                let Some(req) = Message::parse(&buf[..n]).filter(|m| m.class == Class::Request) else {
                    continue;
                };
                let reply = if req.method == method::ALLOCATE {
                    Builder::new(req.method, Class::Success, req.transaction)
                        .address(attr::XOR_RELAYED_ADDRESS, "127.0.0.1:40000".parse().unwrap())
                        .u32(attr::LIFETIME, 600)
                        .finish()
                } else {
                    nonces += 1;
                    Builder::new(req.method, Class::Error, req.transaction)
                        .attr(attr::ERROR_CODE, b"\0\0\x04\x01Unauthorized")
                        .attr(attr::REALM, b"fake")
                        .attr(attr::NONCE, format!("nonce-{nonces}").as_bytes())
                        .finish()
                };
                let _ = socket.send_to(&reply, from);
            }
        });
        addr
    }

    #[test]
    fn rejected_credentials_on_refresh_need_a_reconnect() {
        let server = fake_turn_rejecting_refreshes();
        let timing = Timing {
            liveness: Duration::from_millis(200),
            tick: Duration::from_millis(50),
            ..TIMING
        };
        let urls = [format!("turn:{server}?transport=udp")];
        let t = TurnTransport::connect_with(&urls, "u", "expired", timing).unwrap();
        let start = Instant::now();
        while !t.needs_reconnect() {
            assert!(start.elapsed() < Duration::from_secs(5), "{:?}", t.stats());
            std::thread::sleep(Duration::from_millis(20));
        }
        let err = t.send(1, b"x").unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::PermissionDenied, "{err}");
        let mut buf = [0u8; 8];
        assert_eq!(
            t.recv(&mut buf, Duration::ZERO).unwrap_err().kind(),
            io::ErrorKind::PermissionDenied
        );
    }
}
