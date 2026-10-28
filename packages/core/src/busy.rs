//! A `swofty run` session builds the profile and copies it into the game
//! folder under the session lock. `state/build.json` says how far it got, and
//! `state/cancel-build` asks it to stop at the next safe point.

use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};

use crate::recovery::SessionLock;
use crate::{Error, Result, io_err};

pub const CANCELLED: &str = "the build was cancelled; the game folder is back as it was";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Step {
    #[default]
    Building,
    Copying,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize, Deserialize)]
pub struct Busy {
    pub pid: u32,
    pub profile: String,
    #[serde(default)]
    pub addons: Vec<String>,
    #[serde(default)]
    pub step: Step,
    #[serde(default)]
    pub done: u64,
    #[serde(default)]
    pub total: u64,
    pub since: f64,
}

impl Busy {
    pub fn new(profile: &str, addons: Vec<String>) -> Self {
        Self {
            pid: std::process::id(),
            profile: profile.to_owned(),
            addons,
            step: Step::Building,
            done: 0,
            total: 0,
            since: now_secs(),
        }
    }

    pub fn path(data: &Path) -> PathBuf {
        data.join("state/build.json")
    }

    pub fn write(&self, data: &Path) -> Result<()> {
        let path = Self::path(data);
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
        }
        let tmp = path.with_extension(format!("json.{}.tmp", std::process::id()));
        let json = serde_json::to_vec_pretty(self).expect("serialisable");
        std::fs::write(&tmp, json).map_err(io_err(&tmp))?;
        std::fs::rename(&tmp, &path)
            .map_err(io_err(&path))
            .inspect_err(|_| {
                std::fs::remove_file(&tmp).ok();
            })
    }

    pub fn read(data: &Path) -> Option<Self> {
        let text = std::fs::read(Self::path(data)).ok()?;
        serde_json::from_slice(&text).ok()
    }

    pub fn clear(data: &Path) {
        std::fs::remove_file(Self::path(data)).ok();
    }

    /// The build another process is running, while it holds the session
    /// lock. One whose process ended is left for the next session's
    /// recovery to clean up.
    pub fn live(data: &Path) -> Option<Self> {
        Self::read(data).filter(|b| b.pid != std::process::id() && SessionLock::held_elsewhere(data))
    }

    pub fn holds(&self, id: &str) -> bool {
        self.addons.iter().any(|a| a == id)
    }

    pub fn minutes(&self) -> u64 {
        ((now_secs() - self.since).max(0.0) / 60.0) as u64
    }

    /// How far it got, as "building the mods (3,155 files so far)".
    pub fn progress(&self) -> String {
        match self.step {
            Step::Building if self.done > 0 => {
                format!(
                    "building the mods ({} {} so far)",
                    thousands(self.done),
                    if self.done == 1 { "file" } else { "files" }
                )
            }
            Step::Building => "building the mods".to_owned(),
            Step::Copying => format!(
                "copying the mods into the game folder ({} of {} files)",
                thousands(self.done),
                thousands(self.total)
            ),
        }
    }

    /// What a second Play says while this build runs.
    pub fn text(&self) -> String {
        format!(
            "Swofty is still {} for profile {}, started {}; Play works when it finishes, or cancel the build in the launcher",
            self.progress(),
            self.profile,
            ago(self.minutes())
        )
    }
}

fn ago(minutes: u64) -> String {
    match minutes {
        0 => "under a minute ago".to_owned(),
        m if m < 60 => format!("{m} min ago"),
        m => format!("{} h {} min ago", m / 60, m % 60),
    }
}

fn thousands(n: u64) -> String {
    let s = n.to_string();
    let mut out = String::new();
    for (i, c) in s.chars().enumerate() {
        if i > 0 && (s.len() - i).is_multiple_of(3) {
            out.push(',');
        }
        out.push(c);
    }
    out
}

/// Refuse to install over, update or remove an addon a running build reads.
pub fn refuse_change(data: &Path, id: &str, name: &str) -> Result<()> {
    match Busy::live(data).filter(|b| b.holds(id)) {
        Some(_) => Err(Error::Other(format!(
            "{name} is in the build that is running; change it when the build finishes, or cancel the build"
        ))),
        None => Ok(()),
    }
}

pub fn cancel_path(data: &Path) -> PathBuf {
    data.join("state/cancel-build")
}

pub fn request_cancel(data: &Path) -> Result<()> {
    let path = cancel_path(data);
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    }
    std::fs::write(&path, format!("{}\n", now_secs())).map_err(io_err(&path))
}

pub fn cancel_requested(data: &Path) -> bool {
    cancel_path(data).exists()
}

pub fn clear_cancel(data: &Path) {
    std::fs::remove_file(cancel_path(data)).ok();
}

/// A safe point: stop here when the build was asked to.
pub fn check(data: &Path) -> Result<()> {
    match cancel_requested(data) {
        true => Err(Error::Cancelled),
        false => Ok(()),
    }
}

fn now_secs() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("swofty-busy-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn says_how_far_a_build_and_a_copy_got() {
        let mut b = Busy::new("default", vec!["swofty.classic-seasons".into()]);
        assert_eq!(b.progress(), "building the mods");
        b.done = 3155;
        assert_eq!(b.progress(), "building the mods (3,155 files so far)");
        b.step = Step::Copying;
        b.done = 443;
        b.total = 2544;
        assert_eq!(
            b.progress(),
            "copying the mods into the game folder (443 of 2,544 files)"
        );
        b.since = now_secs() - 9_400.0;
        let text = b.text();
        assert!(
            text.contains("for profile default, started 2 h 36 min ago"),
            "{text}"
        );
        assert!(!text.contains("close F1 25"), "{text}");
    }

    #[test]
    fn a_build_counts_only_while_its_process_holds_the_lock() {
        let dir = temp("lock");
        let data = dir.as_path();
        let b = Busy {
            pid: std::process::id().wrapping_add(1),
            since: 100.0,
            ..Busy::new("default", vec!["mod.a".into()])
        };
        b.write(data).unwrap();
        assert_eq!(Busy::read(data), Some(b.clone()));
        assert_eq!(Busy::live(data), None, "nothing holds the lock");
        let lock = SessionLock::try_acquire(data).unwrap().unwrap();
        assert_eq!(Busy::live(data), Some(b.clone()));
        assert!(refuse_change(data, "mod.a", "Mod A").is_err());
        refuse_change(data, "mod.b", "Mod B").unwrap();
        drop(lock);
        assert_eq!(Busy::live(data), None, "its process let go of the lock");
        refuse_change(data, "mod.a", "Mod A").unwrap();
        let own = Busy::new("default", vec!["mod.a".into()]);
        own.write(data).unwrap();
        let _lock = SessionLock::try_acquire(data).unwrap().unwrap();
        assert_eq!(Busy::live(data), None, "this process's own build");
    }

    #[test]
    fn a_cancel_request_stops_at_the_next_check() {
        let dir = temp("cancel");
        check(&dir).unwrap();
        request_cancel(&dir).unwrap();
        assert!(matches!(check(&dir), Err(Error::Cancelled)));
        clear_cancel(&dir);
        check(&dir).unwrap();
    }
}
