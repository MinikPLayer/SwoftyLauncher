//! Commands to the running game's netplay driver over core's file mailbox.
//!
//! Core reads `<game>/swofty/netplay/inbox/<seq>.json` (written as `.tmp`, then renamed), answers
//! each command in `outbox/<seq>.json` and keeps `status.json` current. `status`, `clear` and
//! `cancel` always answer, `cancel` ending a running `host` or `join` at its next step; `host`,
//! `join` and `leave` need `SWOFTY_NETPLAY` in the game's environment and the game on its main menu
//! (`host`, `join`) or in a LAN lobby (`leave`).

use std::fmt;
use std::net::Ipv4Addr;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};
use serde_json::{Value, json};

/// How long [`send`] waits for core's reply by default.
pub const REPLY_TIMEOUT: Duration = Duration::from_secs(10);
/// Host waits for the player's car category pick and the lobby.
pub const HOST_TIMEOUT: Duration = Duration::from_secs(150);
/// Join waits for the host's lobby.
pub const JOIN_TIMEOUT: Duration = Duration::from_secs(45);
/// `status.json` older than this means core is not running its netplay driver.
pub const STATUS_STALE: Duration = Duration::from_secs(20);

const POLL: Duration = Duration::from_millis(50);

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Category {
    #[serde(rename = "f1-2026")]
    F12026,
    #[serde(rename = "f1-2025")]
    F12025,
}

impl Category {
    pub fn as_str(self) -> &'static str {
        match self {
            Category::F12026 => "f1-2026",
            Category::F12025 => "f1-2025",
        }
    }
}

impl std::str::FromStr for Category {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, String> {
        match s {
            "f1-2026" | "f1_2026" | "2026" => Ok(Category::F12026),
            "f1-2025" | "f1_2025" | "2025" => Ok(Category::F12025),
            _ => Err(format!("unknown car category '{s}' (f1-2026 or f1-2025)")),
        }
    }
}

impl fmt::Display for Category {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Command {
    Status,
    Configure { self_member: u8, base: u16 },
    Clear,
    Cancel,
    Host { category: Category },
    Join { ip: Ipv4Addr },
    Leave,
}

impl Command {
    pub fn op(&self) -> &'static str {
        match self {
            Command::Status => "status",
            Command::Configure { .. } => "configure",
            Command::Clear => "clear",
            Command::Cancel => "cancel",
            Command::Host { .. } => "host",
            Command::Join { .. } => "join",
            Command::Leave => "leave",
        }
    }

    /// How long the game may take to answer: host and join wait for the LAN lobby.
    pub fn timeout(&self) -> Duration {
        match self {
            Command::Host { .. } => HOST_TIMEOUT,
            Command::Join { .. } => JOIN_TIMEOUT,
            _ => REPLY_TIMEOUT,
        }
    }

    pub fn to_json(&self, seq: u64) -> Value {
        let args = match self {
            Command::Host { category } => json!({ "category": category.as_str() }),
            Command::Join { ip } => json!({ "ip": ip.to_string() }),
            Command::Configure { self_member, base } => json!({ "self": self_member, "base": base }),
            Command::Status | Command::Clear | Command::Cancel | Command::Leave => json!({}),
        };
        json!({ "seq": seq, "op": self.op(), "args": args })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Screen {
    MainMenu,
    LanLobby,
    Other,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Role {
    None,
    Host,
    Client,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct GameStatus {
    pub screen: Screen,
    pub in_session: bool,
    pub role: Role,
    #[serde(default)]
    pub configured: bool,
    #[serde(default, rename = "self")]
    pub self_member: u8,
    #[serde(default)]
    pub base: u16,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Reply {
    pub seq: u64,
    pub op: String,
    pub ok: bool,
    pub error: Option<String>,
    #[serde(default)]
    pub detail: Option<String>,
    pub status: GameStatus,
    #[serde(default)]
    pub t: u64,
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("could not write the command to {path}: {source}")]
    Write { path: PathBuf, source: std::io::Error },
    #[error("the game did not answer within {0:?}; is it running with core loaded?")]
    Timeout(Duration),
    #[error("the game's reply {path} is not valid: {why}")]
    BadReply { path: PathBuf, why: String },
    #[error("the game refused {op}: {why}")]
    Refused {
        op: String,
        why: String,
        status: GameStatus,
    },
}

/// The netplay mailbox folder in a game folder: `<game>/swofty/netplay`.
pub fn mailbox(game_dir: &Path) -> PathBuf {
    game_dir.join("swofty").join("netplay")
}

fn next_seq() -> u64 {
    static LAST: AtomicU64 = AtomicU64::new(0);
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(1, |d| u64::try_from(d.as_millis()).unwrap_or(u64::MAX));
    let mut last = LAST.load(Ordering::Relaxed);
    loop {
        let seq = now.max(last + 1);
        match LAST.compare_exchange(last, seq, Ordering::Relaxed, Ordering::Relaxed) {
            Ok(_) => return seq,
            Err(seen) => last = seen,
        }
    }
}

/// Write `cmd` to the inbox and return its seq without waiting for the reply.
pub fn post(game_dir: &Path, cmd: &Command) -> Result<u64, Error> {
    let inbox = mailbox(game_dir).join("inbox");
    let seq = next_seq();
    let path = inbox.join(format!("{seq}.json"));
    let tmp = inbox.join(format!("{seq}.json.tmp"));
    let write = || -> std::io::Result<()> {
        std::fs::create_dir_all(&inbox)?;
        std::fs::write(&tmp, cmd.to_json(seq).to_string())?;
        std::fs::rename(&tmp, &path)
    };
    write().map_err(|source| {
        let _ = std::fs::remove_file(&tmp);
        Error::Write {
            path: path.clone(),
            source,
        }
    })?;
    Ok(seq)
}

/// Wait for the reply to `seq`, read it and delete it. On a timeout the command is withdrawn.
pub fn wait(game_dir: &Path, seq: u64, timeout: Duration) -> Result<Reply, Error> {
    let dir = mailbox(game_dir);
    let reply = dir.join("outbox").join(format!("{seq}.json"));
    let start = Instant::now();
    loop {
        if let Ok(bytes) = std::fs::read(&reply) {
            let _ = std::fs::remove_file(&reply);
            return serde_json::from_slice::<Reply>(&bytes).map_err(|e| Error::BadReply {
                path: reply.clone(),
                why: e.to_string(),
            });
        }
        if start.elapsed() >= timeout {
            let _ = std::fs::remove_file(dir.join("inbox").join(format!("{seq}.json")));
            return Err(Error::Timeout(timeout));
        }
        std::thread::sleep(POLL);
    }
}

/// Send `cmd` and wait for core's reply; a refused command is [`Error::Refused`].
pub fn send(game_dir: &Path, cmd: &Command, timeout: Duration) -> Result<Reply, Error> {
    let logged = *cmd != Command::Status;
    let started = Instant::now();
    let seq = post(game_dir, cmd).inspect_err(|e| {
        if logged {
            super::diag::line(format!("game {}: not sent: {e}", cmd.op()));
        }
    })?;
    if logged {
        super::diag::line(format!(
            "game {} {} sent (seq {seq})",
            cmd.op(),
            cmd.to_json(seq)["args"]
        ));
    }
    let result = wait(game_dir, seq, timeout).and_then(|reply| {
        if reply.ok {
            Ok(reply)
        } else {
            Err(Error::Refused {
                op: reply.op,
                why: reply.error.unwrap_or_else(|| "no reason given".into()),
                status: reply.status,
            })
        }
    });
    if logged {
        let ms = started.elapsed().as_millis();
        super::diag::line(match &result {
            Ok(r) => format!(
                "game {} seq {seq}: ok in {ms} ms{}, status {}",
                cmd.op(),
                r.detail.as_deref().map(|d| format!(" ({d})")).unwrap_or_default(),
                status_line(&r.status)
            ),
            Err(Error::Refused { why, status, .. }) => format!(
                "game {} seq {seq}: refused in {ms} ms: {why}, status {}",
                cmd.op(),
                status_line(status)
            ),
            Err(e) => format!("game {} seq {seq}: failed in {ms} ms: {e}", cmd.op()),
        });
    }
    result
}

pub fn status_line(s: &GameStatus) -> String {
    format!(
        "screen={:?} in_session={} role={:?} configured={} self={} base={}",
        s.screen, s.in_session, s.role, s.configured, s.self_member, s.base
    )
}

pub fn status(game_dir: &Path) -> Result<GameStatus, Error> {
    {
        let cmd = Command::Status;
        send(game_dir, &cmd, cmd.timeout())
    }
    .map(|r| r.status)
}

pub fn host(game_dir: &Path, category: Category) -> Result<GameStatus, Error> {
    {
        let cmd = Command::Host { category };
        send(game_dir, &cmd, cmd.timeout())
    }
    .map(|r| r.status)
}

pub fn join(game_dir: &Path, ip: Ipv4Addr) -> Result<GameStatus, Error> {
    {
        let cmd = Command::Join { ip };
        send(game_dir, &cmd, cmd.timeout())
    }
    .map(|r| r.status)
}

pub fn configure(game_dir: &Path, self_member: u8, base: u16) -> Result<GameStatus, Error> {
    {
        let cmd = Command::Configure { self_member, base };
        send(game_dir, &cmd, cmd.timeout())
    }
    .map(|r| r.status)
}

/// End a running `host` or `join`; it answers "cancelled by the launcher". Nothing running is fine.
pub fn cancel(game_dir: &Path) -> Result<GameStatus, Error> {
    send(game_dir, &Command::Cancel, Command::Cancel.timeout()).map(|r| r.status)
}

pub fn clear(game_dir: &Path) -> Result<GameStatus, Error> {
    {
        let cmd = Command::Clear;
        send(game_dir, &cmd, cmd.timeout())
    }
    .map(|r| r.status)
}

pub fn leave(game_dir: &Path) -> Result<GameStatus, Error> {
    {
        let cmd = Command::Leave;
        send(game_dir, &cmd, cmd.timeout())
    }
    .map(|r| r.status)
}

/// The last `status.json` core wrote, when it is fresh: no command round trip.
pub fn last_status(game_dir: &Path) -> Option<GameStatus> {
    let path = mailbox(game_dir).join("status.json");
    let fresh = std::fs::metadata(&path)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.elapsed().ok())
        .is_some_and(|age| age < STATUS_STALE);
    if !fresh {
        return None;
    }
    let doc: Value = serde_json::from_slice(&std::fs::read(&path).ok()?).ok()?;
    serde_json::from_value(doc.get("status")?.clone()).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_game(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-mp-game-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn responder(
        game: PathBuf,
        answer: impl Fn(&Value) -> Value + Send + 'static,
    ) -> std::thread::JoinHandle<()> {
        std::thread::spawn(move || {
            let dir = mailbox(&game);
            let start = Instant::now();
            while start.elapsed() < Duration::from_secs(5) {
                let entries = std::fs::read_dir(dir.join("inbox"))
                    .into_iter()
                    .flatten()
                    .flatten();
                for e in entries {
                    let name = e.file_name().to_string_lossy().into_owned();
                    if !name.ends_with(".json") {
                        continue;
                    }
                    let cmd: Value = serde_json::from_slice(&std::fs::read(e.path()).unwrap()).unwrap();
                    std::fs::create_dir_all(dir.join("outbox")).unwrap();
                    std::fs::write(dir.join("outbox").join(&name), answer(&cmd).to_string()).unwrap();
                    std::fs::remove_file(e.path()).unwrap();
                    return;
                }
                std::thread::sleep(Duration::from_millis(10));
            }
        })
    }

    fn status_value(screen: &str) -> Value {
        json!({ "screen": screen, "in_session": screen == "lan_lobby", "role": "none", "configured": true, "self": 1, "base": 40000 })
    }

    #[test]
    fn commands_serialise_as_core_reads_them() {
        assert_eq!(
            Command::Host {
                category: Category::F12026
            }
            .to_json(7),
            json!({ "seq": 7, "op": "host", "args": { "category": "f1-2026" } })
        );
        assert_eq!(
            Command::Join {
                ip: Ipv4Addr::new(10, 77, 0, 1)
            }
            .to_json(8),
            json!({ "seq": 8, "op": "join", "args": { "ip": "10.77.0.1" } })
        );
        assert_eq!(Command::Leave.to_json(9)["op"], "leave");
        assert_eq!(
            Command::Configure {
                self_member: 2,
                base: 40000
            }
            .to_json(11),
            json!({ "seq": 11, "op": "configure", "args": { "self": 2, "base": 40000 } })
        );
        assert_eq!(Command::Clear.to_json(12)["op"], "clear");
        assert_eq!(
            Command::Cancel.to_json(13),
            json!({ "seq": 13, "op": "cancel", "args": {} })
        );
        assert_eq!(
            Command::Host {
                category: Category::F12026
            }
            .timeout(),
            HOST_TIMEOUT
        );
        assert_eq!(
            Command::Join {
                ip: Ipv4Addr::LOCALHOST
            }
            .timeout(),
            JOIN_TIMEOUT
        );
        assert_eq!(Command::Status.timeout(), REPLY_TIMEOUT);
        assert_eq!(Command::Status.to_json(10)["args"], json!({}));
    }

    #[test]
    fn categories_parse_from_the_cli_spelling() {
        assert_eq!("f1-2026".parse::<Category>(), Ok(Category::F12026));
        assert_eq!("2025".parse::<Category>(), Ok(Category::F12025));
        assert!("f2".parse::<Category>().is_err());
    }

    #[test]
    fn seqs_increase() {
        let a = next_seq();
        let b = next_seq();
        assert!(b > a);
    }

    #[test]
    fn a_status_round_trip_reads_the_reply_and_cleans_up() {
        let game = temp_game("status");
        let t = responder(
            game.clone(),
            |cmd| json!({ "seq": cmd["seq"], "op": cmd["op"], "ok": true, "error": null, "status": status_value("main_menu"), "t": 1 }),
        );
        let s = status(&game).unwrap();
        t.join().unwrap();
        assert_eq!(s.screen, Screen::MainMenu);
        assert!(!s.in_session);
        assert_eq!((s.self_member, s.base), (1, 40000));
        assert_eq!(s.role, Role::None);
        let left = std::fs::read_dir(mailbox(&game).join("outbox")).unwrap().count();
        assert_eq!(left, 0, "the reply is deleted once read");
    }

    #[test]
    fn a_refused_host_carries_the_reason_and_status() {
        let game = temp_game("refused");
        let t = responder(game.clone(), |cmd| {
            assert_eq!(cmd["args"]["category"], "f1-2025");
            json!({ "seq": cmd["seq"], "op": "host", "ok": false, "error": "the game is not on its main menu",
                    "status": status_value("other"), "t": 2 })
        });
        let err = host(&game, Category::F12025).unwrap_err();
        t.join().unwrap();
        match err {
            Error::Refused { op, why, status } => {
                assert_eq!(op, "host");
                assert_eq!(why, "the game is not on its main menu");
                assert_eq!(status.screen, Screen::Other);
            }
            other => panic!("unexpected {other}"),
        }
    }

    #[test]
    fn no_reply_times_out_and_withdraws_the_command() {
        let game = temp_game("timeout");
        let seq = post(&game, &Command::Leave).unwrap();
        let inbox = mailbox(&game).join("inbox").join(format!("{seq}.json"));
        assert!(inbox.exists());
        let err = wait(&game, seq, Duration::from_millis(120)).unwrap_err();
        assert!(matches!(err, Error::Timeout(_)));
        assert!(!inbox.exists(), "a timed out command is withdrawn");
    }

    #[test]
    fn last_status_reads_a_fresh_status_file() {
        let game = temp_game("last");
        assert_eq!(last_status(&game), None);
        let dir = mailbox(&game);
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(
            dir.join("status.json"),
            json!({ "v": 1, "pid": 1, "written": 1, "status": status_value("lan_lobby") }).to_string(),
        )
        .unwrap();
        let s = last_status(&game).unwrap();
        assert_eq!(s.screen, Screen::LanLobby);
        assert!(s.in_session);
    }
}
