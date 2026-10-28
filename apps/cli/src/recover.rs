use std::time::Duration;

use anyhow::{Result, bail};
use swofty_core::busy::Busy;
use swofty_core::install::{Conflict, Game};
use swofty_core::process::game_processes;
use swofty_core::recovery::{self, Blocked, HEADLINE, Notice, Recovered, Scope, SessionLock};
use swofty_core::store::Store;

pub const LOCK_WAIT: Duration = Duration::from_secs(120);

/// Take the session lock, waiting while a previous session finishes rolling
/// back its game files. A session still building or copying the mods is
/// not waited for.
pub fn lock(store: &Store, out: &dyn Fn(&str)) -> Result<SessionLock> {
    if let Some(lock) = SessionLock::try_acquire(&store.root)? {
        return Ok(lock);
    }
    if let Some(b) = Busy::live(&store.root) {
        bail!("{}", b.text());
    }
    out("waiting for the previous session to finish rolling back its game files");
    let start = std::time::Instant::now();
    while start.elapsed() < LOCK_WAIT {
        if let Some(lock) = SessionLock::wait(&store.root, LOCK_POLL)? {
            return Ok(lock);
        }
        if let Some(b) = Busy::live(&store.root) {
            bail!("{}", b.text());
        }
    }
    bail!("{}", held(store, !game_processes().is_empty()))
}

const LOCK_POLL: Duration = Duration::from_secs(2);

pub(crate) fn held(store: &Store, game: bool) -> String {
    let path = recovery::lock_path(&store.root);
    match game {
        true => format!(
            "another swofty session is still running ({} is held); close F1 25, wait for it to finish, then try again",
            path.display()
        ),
        false => format!(
            "the last swofty session is still putting the game files back ({} is held); wait a minute, then try again",
            path.display()
        ),
    }
}

/// Roll back what unfinished sessions left before anything else happens,
/// and say so.
pub fn stale(
    store: &Store,
    lock: &SessionLock,
    g: &Game,
    scope: Scope,
    out: &dyn Fn(&str),
) -> Result<Recovered> {
    let r = recovery::recover(lock, &store.root, g, scope)?;
    if r.recovered() {
        announce(store, g, &r.lines(), out);
    } else {
        for l in r.lines() {
            out(&l);
        }
    }
    Ok(r)
}

/// The recovery headline, what was put back, and the doctor's report.
pub fn announce(store: &Store, g: &Game, lines: &[String], out: &dyn Fn(&str)) {
    out(HEADLINE);
    for l in lines {
        out(&format!("  {l}"));
    }
    let (report, _) = crate::doctor::report(store, Some(g));
    for l in report {
        out(&format!("doctor: {l}"));
    }
}

/// The error for files that block a profile and are not provably ours:
/// nothing was touched, and the message says what a restore would do.
pub fn blocked(store: &Store, g: &Game, found: &[Conflict]) -> anyhow::Error {
    let err = swofty_core::Error::Leftovers(found.to_vec());
    if found.iter().all(|c| c.owned) {
        return anyhow::anyhow!("{err}; swofty restore rolls the game folder back to the baseline");
    }
    match Blocked::record(&store.root, &g.dir, found) {
        Ok(b) => anyhow::anyhow!(
            "{err}; it was left untouched in case it is a mod copied in by hand. {}",
            b.explain(&store.root)
        ),
        Err(e) => anyhow::anyhow!("{err}; it was left untouched ({e})"),
    }
}

pub fn record_cleared(store: &Store, lines: &[String]) {
    Notice::record(&store.root, lines).ok();
}

#[cfg(test)]
mod tests {
    use super::*;
    use swofty_core::busy::Step;

    fn temp(name: &str) -> std::path::PathBuf {
        let d = std::env::temp_dir().join(format!("swofty-cli-lock-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn a_second_play_during_a_build_names_the_build_and_never_waits() {
        let root = temp("busy");
        let store = Store::new(&root);
        let holder = SessionLock::try_acquire(&store.root).unwrap().unwrap();
        let mut b = Busy::new("default", vec!["swofty.classic-seasons".into()]);
        b.pid = b.pid.wrapping_add(1);
        b.step = Step::Copying;
        b.done = 443;
        b.total = 2544;
        b.write(&store.root).unwrap();
        let start = std::time::Instant::now();
        let waited = std::cell::Cell::new(false);
        let err = lock(&store, &|_| waited.set(true)).unwrap_err().to_string();
        assert!(start.elapsed() < Duration::from_secs(5));
        assert!(!waited.get());
        assert!(
            err.starts_with("Swofty is still copying the mods into the game folder (443 of 2,544 files) for profile default"),
            "{err}"
        );
        assert!(err.contains("Play works when it finishes"), "{err}");
        assert!(!err.contains("close F1 25"), "{err}");
        drop(holder);
        Busy::clear(&store.root);
        let _lock = lock(&store, &|_| {}).unwrap();
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn only_a_running_game_is_asked_to_close() {
        let store = Store::new(temp("held"));
        assert!(held(&store, true).contains("close F1 25"));
        let rolling = held(&store, false);
        assert!(!rolling.contains("close F1 25"), "{rolling}");
        assert!(rolling.contains("putting the game files back"), "{rolling}");
        std::fs::remove_dir_all(&store.root).ok();
    }
}
