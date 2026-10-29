//! The machine's game lock: one F1 25 at a time. It's a folder made with
//! `mkdir` (atomic on every platform) holding an `owner` line, the same
//! protocol the scripts and agents on a test machine already use, so they
//! queue on each other. A lock is never cleared for its holder, even when
//! it looks stale.

use std::io::ErrorKind;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime};

pub const ENV: &str = "SWOFTY_GAME_LOCK";
pub const OWNER: &str = "owner";
/// A lock older than this with no game running is probably left over.
pub const STALE_AFTER: Duration = Duration::from_secs(3 * 3600);

/// Where the lock lives: `SWOFTY_GAME_LOCK`, else the shared test machine's
/// lock when its work disk is there, else next to the data folder.
pub fn default_path(data: &Path) -> PathBuf {
    if let Some(p) = std::env::var_os(ENV).filter(|p| !p.is_empty()) {
        return PathBuf::from(p);
    }
    let shared = Path::new("/mnt/work");
    if cfg!(target_os = "linux") && shared.is_dir() {
        return shared.join(".f1-game.lock.d");
    }
    let parent = data.parent().unwrap_or(data);
    parent.join("swofty-game.lock.d")
}

#[derive(Debug)]
pub struct GameLock {
    dir: PathBuf,
    held: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Busy {
    pub dir: PathBuf,
    pub owner: Option<String>,
    pub age: Option<Duration>,
}

impl Busy {
    pub fn text(&self, game_running: bool) -> String {
        let who = match &self.owner {
            Some(o) => format!("held by \"{o}\""),
            None => "held (no owner line)".to_owned(),
        };
        let mut out = format!("the game lock {} is {who}", self.dir.display());
        if let Some(age) = self.age
            && age >= STALE_AFTER
            && !game_running
        {
            out.push_str(&format!(
                "; it is {} h old and no game is running, so it may be left over, but only its holder clears it",
                age.as_secs() / 3600
            ));
        }
        out
    }
}

impl GameLock {
    /// Take the lock now, or say who holds it.
    pub fn try_acquire(dir: &Path, owner: &str) -> std::io::Result<Result<GameLock, Busy>> {
        match std::fs::create_dir(dir) {
            Ok(()) => {
                let lock = GameLock {
                    dir: dir.to_path_buf(),
                    held: true,
                };
                std::fs::write(dir.join(OWNER), format!("{}\n", owner.trim()))?;
                Ok(Ok(lock))
            }
            Err(e) if e.kind() == ErrorKind::AlreadyExists => Ok(Err(Self::holder(dir))),
            Err(e) => Err(std::io::Error::new(
                e.kind(),
                format!("could not take the game lock {}: {e}", dir.display()),
            )),
        }
    }

    /// Take the lock, waiting up to `wait` for its holder to let go.
    /// `waiting` hears who holds it, once and then every few minutes.
    pub fn acquire(
        dir: &Path,
        owner: &str,
        wait: Duration,
        poll: Duration,
        mut waiting: impl FnMut(&Busy),
    ) -> std::io::Result<Result<GameLock, Busy>> {
        let start = Instant::now();
        let mut told: Option<Instant> = None;
        loop {
            let busy = match Self::try_acquire(dir, owner)? {
                Ok(lock) => return Ok(Ok(lock)),
                Err(b) => b,
            };
            if start.elapsed() >= wait {
                return Ok(Err(busy));
            }
            if told.is_none_or(|t| t.elapsed() >= Duration::from_secs(300)) {
                waiting(&busy);
                told = Some(Instant::now());
            }
            std::thread::sleep(
                poll.min(wait.saturating_sub(start.elapsed()))
                    .max(Duration::from_millis(1)),
            );
        }
    }

    pub fn holder(dir: &Path) -> Busy {
        let owner = std::fs::read_to_string(dir.join(OWNER))
            .ok()
            .map(|s| s.trim().to_owned())
            .filter(|s| !s.is_empty());
        let age = std::fs::metadata(dir)
            .and_then(|m| m.modified())
            .ok()
            .and_then(|t| SystemTime::now().duration_since(t).ok());
        Busy {
            dir: dir.to_path_buf(),
            owner,
            age,
        }
    }

    pub fn dir(&self) -> &Path {
        &self.dir
    }

    pub fn release(mut self) -> std::io::Result<()> {
        self.held = false;
        Self::remove(&self.dir)
    }

    fn remove(dir: &Path) -> std::io::Result<()> {
        match std::fs::remove_file(dir.join(OWNER)) {
            Err(e) if e.kind() != ErrorKind::NotFound => return Err(e),
            _ => {}
        }
        std::fs::remove_dir(dir)
    }
}

impl Drop for GameLock {
    fn drop(&mut self) {
        if self.held {
            Self::remove(&self.dir).ok();
        }
    }
}

/// The owner line: `swofty-test <run> <suites> HH:MM`.
pub fn owner_line(run: &str, suites: &[String], hh_mm: &str) -> String {
    let suites = if suites.is_empty() {
        "-".to_owned()
    } else {
        suites.join(",")
    };
    format!("swofty-test {run} {suites} {hh_mm}")
}
