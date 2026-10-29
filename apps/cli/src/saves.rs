use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::time::{Duration, SystemTime};

use anyhow::{Result, bail};
use clap::Subcommand;
use swofty_core::install::{Game, Source};
use swofty_core::save_backup::{self, Backup};
use swofty_core::store::Store;

use crate::run::say;

#[derive(Subcommand)]
pub enum SavesCommand {
    #[command(
        about = "List the profile save backups taken before sessions without an addon that affects saves"
    )]
    List,
    #[command(
        about = "Copy a backup back over the profile save (F1 25 and Steam must be closed); backs up the current save first"
    )]
    Restore {
        #[arg(help = "The backup's name, as `swofty saves list` prints it")]
        backup: String,
    },
}

fn steam_save_dir(game: &Path) -> Option<PathBuf> {
    swofty_core::steam::find_game()
        .filter(|g| swofty_core::copies::same_dir(&g.dir, game))
        .and_then(|g| swofty_core::steam::save_dir(&g.steam_root))
        .or_else(swofty_core::steam::find_save_dir)
}

/// Backs up the profile save before a session whose profile leaves out an
/// installed addon with `saves.affects`.
pub fn before_session(store: &Store, profile: &str, g: &Game) -> Option<Backup> {
    let absent = match store
        .profile(profile)
        .and_then(|p| save_backup::absent_in(store, &p))
    {
        Ok(a) if a.is_empty() => return None,
        Ok(a) => a,
        Err(e) => {
            say(
                "warning",
                &format!("saves: could not check the profile's addons: {e}"),
            );
            return None;
        }
    };
    let ids = absent.join(", ");
    if Source::detect(&g.dir) == Source::EaApp {
        say(
            "warning",
            &format!(
                "saves: no backup: the launcher does not know where the EA app copy keeps its saves, and this session \
                 runs without {ids}"
            ),
        );
        return None;
    }
    let Some(dir) = steam_save_dir(&g.dir) else {
        say(
            "info",
            &format!("saves: no Steam save folder found; nothing to back up before a session without {ids}"),
        );
        return None;
    };
    match save_backup::take(
        &store.root,
        &dir,
        profile,
        &absent,
        &save_backup::now_stamp(),
        "session",
    ) {
        Ok(Some(b)) => {
            say(
                "info",
                &format!(
                    "saves: backed up the profile save before a session without {ids} (their careers are removed if \
                     the game saves): {}",
                    b.dir.display()
                ),
            );
            Some(b)
        }
        Ok(None) => None,
        Err(e) => {
            say(
                "warning",
                &format!("saves: could not back up the profile save: {e}"),
            );
            None
        }
    }
}

/// Records and reports a profile save the session changed and shrank.
pub fn after_session(store: &Store, backup: Option<&Backup>) {
    let Some(b) = backup else {
        return;
    };
    match save_backup::compare(b, &b.manifest.save_dir) {
        Ok(Some(s)) => {
            if let Err(e) = save_backup::write_notice(&store.root, &s) {
                say(
                    "warning",
                    &format!("saves: could not record the shrunk save: {e}"),
                );
            }
            say("warning", &format!("saves: {}", s.text()));
        }
        Ok(None) => {}
        Err(e) => say(
            "warning",
            &format!("saves: could not compare the profile save with {}: {e}", b.name),
        ),
    }
}

/// The shrunk-save notice the session that started at `since` recorded.
pub fn notice_since(store: &Store, since: f64) -> Option<String> {
    let path = store.root.join("state/save-notice.json");
    let at = std::fs::metadata(&path).and_then(|m| m.modified()).ok()?;
    let since = SystemTime::UNIX_EPOCH + Duration::from_secs_f64(since.max(0.0));
    if at < since {
        return None;
    }
    save_backup::read_notice(&store.root).map(|s| s.text())
}

pub fn command(store: &Store, cmd: SavesCommand) -> Result<ExitCode> {
    match cmd {
        SavesCommand::List => {
            let backups = save_backup::list(&store.root)?;
            if backups.is_empty() {
                println!(
                    "no profile save backups in {}",
                    save_backup::backups_dir(&store.root).display()
                );
            }
            for b in backups {
                let without = match b.manifest.absent.is_empty() {
                    true => String::new(),
                    false => format!(", without {}", b.manifest.absent.join(", ")),
                };
                println!(
                    "{}  profile {}, {}{without}, {} file(s), {} bytes",
                    b.name,
                    b.manifest.profile,
                    b.manifest.reason,
                    b.manifest.files.len(),
                    b.manifest.total()
                );
            }
        }
        SavesCommand::Restore { backup } => {
            if let Some(why) = save_backup::restore_blocker() {
                bail!("{why}");
            }
            let b = save_backup::find(&store.root, &backup)?;
            let dir = match b.manifest.save_dir.is_dir() {
                true => b.manifest.save_dir.clone(),
                false => match swofty_core::steam::find_save_dir() {
                    Some(d) => d,
                    None => bail!("no Steam save folder found to restore into"),
                },
            };
            let before = save_backup::restore(&store.root, &backup, &dir, &save_backup::now_stamp())?;
            if let Some(before) = before {
                println!("backed up the current profile save as {}", before.name);
            }
            println!("restored {backup} into {}", dir.display());
            println!(
                "start Steam to upload the restored save; if it asks which copy to keep, keep the local files (this computer)"
            );
        }
    }
    Ok(ExitCode::SUCCESS)
}
