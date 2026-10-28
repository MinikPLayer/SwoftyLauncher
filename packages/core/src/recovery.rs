//! Modded sessions and what an unfinished one leaves behind.
//!
//! A `swofty run` session holds [`SessionLock`] from before it installs the
//! profile until its game files are rolled back, so nothing else rolls back,
//! restores or installs underneath it. The operating system drops the lock
//! when the session's process ends for any reason, which is how a later start
//! tells an unfinished session (its journal still open, the lock free) from a
//! live one, and [`recover`] puts the game folder back.

use std::fs::{File, OpenOptions, TryLockError};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};

use crate::install::{Conflict, Drift, Game, Installer, RolledBack};
use crate::{Error, Result, io_err, read_json, write_json};

pub const HEADLINE: &str = "Recovered the game folder from an unfinished session";

pub fn lock_path(data: &Path) -> PathBuf {
    data.join("state/session.lock")
}

#[derive(Debug)]
pub struct SessionLock {
    _file: File,
}

impl SessionLock {
    /// Take the lock, or `None` while another process holds it. Where the
    /// file system cannot lock files at all, sessions go unguarded rather
    /// than refused.
    pub fn try_acquire(data: &Path) -> Result<Option<Self>> {
        let path = lock_path(data);
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
        }
        let file = OpenOptions::new()
            .create(true)
            .truncate(false)
            .write(true)
            .open(&path)
            .map_err(io_err(&path))?;
        match file.try_lock() {
            Err(TryLockError::WouldBlock) => Ok(None),
            Ok(()) | Err(TryLockError::Error(_)) => Ok(Some(Self { _file: file })),
        }
    }

    /// Take the lock once its holder lets go, giving up after `timeout`.
    pub fn wait(data: &Path, timeout: Duration) -> Result<Option<Self>> {
        let start = Instant::now();
        loop {
            if let Some(lock) = Self::try_acquire(data)? {
                return Ok(Some(lock));
            }
            if start.elapsed() >= timeout {
                return Ok(None);
            }
            std::thread::sleep(Duration::from_millis(200));
        }
    }

    pub fn held_elsewhere(data: &Path) -> bool {
        matches!(Self::try_acquire(data), Ok(None))
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Scope {
    /// Only what `swofty run` sessions opened; a `swofty apply` stays.
    Sessions,
    /// Every open journal, as a new session or install starts from vanilla.
    All,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Recovered {
    pub offline: bool,
    pub journals: Vec<RolledBack>,
    pub swept: Vec<String>,
    pub cleared: Vec<String>,
    pub problems: Vec<String>,
    pub logs: usize,
    /// The build the last session was running when its process ended.
    pub interrupted: Option<crate::busy::Busy>,
}

impl Recovered {
    /// Whether an unfinished session left anything that was put back.
    pub fn recovered(&self) -> bool {
        self.offline
            || self.journals.iter().any(|j| j.session)
            || !self.swept.is_empty()
            || !self.cleared.is_empty()
    }

    pub fn lines(&self) -> Vec<String> {
        let mut out = Vec::new();
        if self.offline {
            out.push("rolled back the offline play staging".to_owned());
        }
        for j in &self.journals {
            let what = if j.session {
                "a session left open"
            } else {
                "swofty apply installed"
            };
            out.push(format!(
                "rolled back journal {} ({} change(s) for profile {}) that {what}",
                j.id, j.changes, j.profile
            ));
            out.extend(j.drift.iter().map(ToString::to_string));
            if !j.kept.is_empty() {
                out.push(format!(
                    "kept {} file(s) addons wrote, for the next session with them",
                    j.kept.len()
                ));
            }
        }
        out.extend(self.swept.iter().cloned());
        out.extend(self.cleared.iter().cloned());
        out.extend(self.problems.iter().cloned());
        if let Some(b) = &self.interrupted {
            out.push(format!(
                "the last Play stopped while {}; Play starts the build again",
                b.progress()
            ));
        }
        out
    }
}

/// Put back what unfinished sessions left in the game folder: the offline
/// play staging, open journals in `scope`, files the placed-files record
/// proves are the launcher's, and the loader folder's leftover logs (moved to
/// `sessions/orphaned-logs`). Holding `lock` proves no session is live.
pub fn recover(lock: &SessionLock, data: &Path, game: &Game, scope: Scope) -> Result<Recovered> {
    let _ = lock;
    let mut r = Recovered::default();
    match crate::offline::restore_pending(data) {
        Ok(done) => r.offline = done,
        Err(e) => r.problems.push(format!("offline play staging: {e}")),
    }
    let inst = Installer::new(data, game);
    r.journals = inst.roll_back(scope == Scope::Sessions)?;
    r.swept = inst.sweep()?;
    r.logs = inst.tidy(&data.join("sessions/orphaned-logs"))?;
    r.interrupted = crate::busy::Busy::read(data);
    crate::busy::Busy::clear(data);
    crate::busy::clear_cancel(data);
    if r.recovered() {
        Notice::record(data, &r.lines())?;
    }
    Ok(r)
}

/// The last recovery, for the launcher to announce.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Notice {
    pub at: f64,
    pub lines: Vec<String>,
}

impl Notice {
    pub fn path(data: &Path) -> PathBuf {
        data.join("state/recovered.json")
    }

    pub fn record(data: &Path, lines: &[String]) -> Result<()> {
        write_json(
            &Self::path(data),
            &Self {
                at: now_secs(),
                lines: lines.to_vec(),
            },
        )
    }

    pub fn read(data: &Path) -> Option<Self> {
        read_json(&Self::path(data)).ok()
    }
}

/// Files of unknown provenance that stopped the last launch. They are only
/// ever moved aside, by an explicit restore, never deleted or overwritten.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Blocked {
    pub game: PathBuf,
    pub files: Vec<BlockedFile>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BlockedFile {
    pub path: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sha256: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub replaced: bool,
}

impl Blocked {
    pub fn path(data: &Path) -> PathBuf {
        data.join("state/blocked.json")
    }

    pub fn record(data: &Path, game: &Path, conflicts: &[Conflict]) -> Result<Self> {
        let b = Self {
            game: game.to_path_buf(),
            files: conflicts
                .iter()
                .filter(|c| !c.owned)
                .map(|c| BlockedFile {
                    path: c.path.clone(),
                    sha256: c.sha256.clone(),
                    replaced: c.replaced,
                })
                .collect(),
        };
        write_json(&Self::path(data), &b)?;
        Ok(b)
    }

    pub fn read(data: &Path) -> Option<Self> {
        read_json(&Self::path(data)).ok()
    }

    pub fn clear(data: &Path) -> Result<()> {
        let path = Self::path(data);
        match std::fs::remove_file(&path) {
            Err(e) if e.kind() != std::io::ErrorKind::NotFound => Err(io_err(&path)(e)),
            _ => Ok(()),
        }
    }

    /// What a restore does about these files, for the error a player sees.
    pub fn explain(&self, data: &Path) -> String {
        let replaced = self.files.iter().any(|f| f.replaced);
        format!(
            "Restore game folder (swofty restore) rolls back swofty's own changes and moves {} to {}, where you can get {} back{}",
            if self.files.len() == 1 {
                "this file"
            } else {
                "these files"
            },
            set_aside_dir(data).display(),
            if self.files.len() == 1 { "it" } else { "them" },
            if replaced {
                "; the game's original files go back in their place"
            } else {
                ""
            }
        )
    }
}

pub fn set_aside_dir(data: &Path) -> PathBuf {
    data.join("set-aside")
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Restored {
    pub notes: Vec<String>,
    pub set_aside: Vec<PathBuf>,
    pub left: Vec<Drift>,
}

/// `swofty restore`: roll back every journal and the offline play staging,
/// move the files that blocked the last launch aside, and put the game folder
/// back to the baseline.
pub fn restore(data: &Path, game: &Game) -> Result<Restored> {
    let Some(lock) = SessionLock::try_acquire(data)? else {
        return Err(Error::Other(SESSION_HELD.into()));
    };
    let out = restore_locked(&lock, data, game)?;
    drop(lock);
    Ok(out)
}

pub const SESSION_HELD: &str = "a modded session is still running or rolling back; close F1 25 and try again";

/// [`restore`] under a session lock the caller holds.
pub fn restore_locked(_lock: &SessionLock, data: &Path, game: &Game) -> Result<Restored> {
    let mut out = Restored::default();
    if crate::offline::restore_pending(data)? {
        out.notes.push("offline play staging".into());
    }
    if let Some(blocked) = Blocked::read(data) {
        if same_dir(&blocked.game, &game.dir) {
            out.set_aside = set_aside(data, game, &blocked)?;
        }
        Blocked::clear(data)?;
    }
    out.left = Installer::new(data, game).restore()?;
    Ok(out)
}

fn set_aside(data: &Path, game: &Game, blocked: &Blocked) -> Result<Vec<PathBuf>> {
    let stamp = format!("{}", now_secs() as u64);
    let mut moved = Vec::new();
    for f in &blocked.files {
        let from = game.dir.join(&f.path);
        if !from.is_file() {
            continue;
        }
        let to = set_aside_dir(data).join(&stamp).join(&f.path);
        if let Some(dir) = to.parent() {
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
        }
        crate::move_file(&from, &to)?;
        moved.push(to);
        if f.replaced {
            continue;
        }
        for dir in from.ancestors().skip(1) {
            if dir == game.dir || !dir.starts_with(&game.dir) {
                break;
            }
            if std::fs::remove_dir(dir).is_err() {
                break;
            }
        }
    }
    Ok(moved)
}

fn same_dir(a: &Path, b: &Path) -> bool {
    let canon = |p: &Path| std::fs::canonicalize(p).unwrap_or_else(|_| p.to_path_buf());
    let (a, b) = (canon(a), canon(b));
    if cfg!(windows) {
        a.to_string_lossy().eq_ignore_ascii_case(&b.to_string_lossy())
    } else {
        a == b
    }
}

fn now_secs() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}
