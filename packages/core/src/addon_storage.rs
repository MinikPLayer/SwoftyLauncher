//! The files addons write through the storage API (`swofty/storage.hpp`).
//!
//! The core keeps them under `swofty/addons/<id>/` in the game folder, next
//! to the files an install places there. When the game can't create files
//! there, the core keeps that addon's files in `addon-storage/<id>/` in the
//! data folder for the session instead. They belong to the player, not to
//! the session: rolling back a session that placed an addon's files first
//! moves every other file in that addon's folder to `addon-storage/<id>/` in
//! the data folder, and the next install with that addon moves them back. A
//! restore moves them out of every addon folder before it clears `swofty/`.
//! Where both places hold a copy of a file, the newer one wins and the older
//! one goes. Otherwise a file is never deleted, so a file an addon removed
//! stays removed.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use crate::build::ADDONS_DIR;
use crate::install::LOADER_DIR;
use crate::{Result, io_err, placed, with_retry};

pub const DIR: &str = "addon-storage";

/// The file the core writes in an addon's storage when the game can't create
/// files in the addon's game folder: the game reads and writes that addon's
/// files in the data folder for the session. The next install removes it.
pub const MARK: &str = ".swofty-data-folder";

/// The loader's own folder under `swofty/addons/`.
const CORE_DIR: &str = "core";

pub fn dir(data: &Path) -> PathBuf {
    data.join(DIR)
}

pub fn addon_dir(data: &Path, id: &str) -> PathBuf {
    dir(data).join(id)
}

fn game_dir(game: &Path, id: &str) -> PathBuf {
    game.join(LOADER_DIR).join(ADDONS_DIR).join(id)
}

fn game_rel(id: &str, rel: &str) -> String {
    format!("{LOADER_DIR}/{ADDONS_DIR}/{id}/{rel}")
}

fn valid(id: &str) -> bool {
    !id.is_empty() && id != "." && id != ".." && id != CORE_DIR
}

/// The folder the game keeps the addon `id`'s files in: its storage in the
/// data folder when the core left [`MARK`] there, otherwise its game folder.
pub fn live_dir(data: &Path, game: &Path, id: &str) -> PathBuf {
    let kept = addon_dir(data, id);
    if kept.join(MARK).is_file() {
        kept
    } else {
        game_dir(game, id)
    }
}

/// The copy of the addon `id`'s file `rel` modified last, in its game folder
/// or in its storage in the data folder.
pub fn newest(data: &Path, game: &Path, id: &str, rel: &str) -> Option<PathBuf> {
    let (here, kept) = (game_dir(game, id).join(rel), addon_dir(data, id).join(rel));
    match (here.is_file(), kept.is_file()) {
        (true, true) if newer(&kept, &here) => Some(kept),
        (true, _) => Some(here),
        (false, true) => Some(kept),
        (false, false) => None,
    }
}

/// The addon whose folder holds the game path `rel`, `swofty/addons/<id>/…`;
/// never the core, whose folder holds no storage.
pub fn addon_of(rel: &str) -> Option<&str> {
    let mut parts = rel.split(['/', '\\']);
    match (parts.next(), parts.next(), parts.next(), parts.next()) {
        (Some(LOADER_DIR), Some(ADDONS_DIR), Some(id), Some(_)) if valid(id) => Some(id),
        _ => None,
    }
}

/// The addons that own any of the game paths `paths`.
pub fn ids<'a>(paths: impl IntoIterator<Item = &'a str>) -> BTreeSet<String> {
    paths
        .into_iter()
        .filter_map(addon_of)
        .map(str::to_owned)
        .collect()
}

/// Every addon folder in the game folder's `swofty/addons/`.
pub fn present(game: &Path) -> Result<BTreeSet<String>> {
    let addons = game.join(LOADER_DIR).join(ADDONS_DIR);
    let Ok(rd) = std::fs::read_dir(&addons) else {
        return Ok(BTreeSet::new());
    };
    let mut out = BTreeSet::new();
    for e in rd {
        let e = e.map_err(io_err(&addons))?;
        let name = e.file_name().to_string_lossy().into_owned();
        if e.file_type().is_ok_and(|t| t.is_dir()) && valid(&name) {
            out.insert(name);
        }
    }
    Ok(out)
}

/// Move every file in the addon's game folder whose game path is not one of
/// `managed` (keys as [`placed::key`] makes them) to the addon's storage,
/// over any older copy stored there, and remove the folders that leaves
/// empty. A stored copy newer than the game folder's, which the game wrote
/// there because it couldn't write in its folder, stays, and the game
/// folder's copy goes. Returns the game paths it moved or dropped.
pub fn keep(data: &Path, game: &Path, id: &str, managed: &BTreeSet<String>) -> Result<Vec<String>> {
    let from = game_dir(game, id);
    if !from.is_dir() {
        return Ok(Vec::new());
    }
    let to = addon_dir(data, id);
    let mut moved = Vec::new();
    for rel in files(&from)? {
        let at = game_rel(id, &rel);
        if managed.contains(&placed::key(&at)) {
            continue;
        }
        let (src, dst) = (from.join(&rel), to.join(&rel));
        if dst.is_file() && newer(&dst, &src) {
            with_retry(&src, || std::fs::remove_file(&src))?;
        } else {
            move_over(&src, &dst)?;
        }
        moved.push(at);
    }
    prune(&from)?;
    Ok(moved)
}

/// Move the addon's stored files into its game folder, except at a game path
/// in `planned` (keys as [`placed::key`] makes them), where the install's
/// file wins. Where the game folder already holds a file, which a session
/// that ended without handing it back left there, the newer copy wins: a
/// newer stored copy replaces it, and an older one goes. [`MARK`] goes too:
/// the core writes it again when the game still can't write in its folder.
/// Returns the game paths it moved.
pub fn put_back(data: &Path, game: &Path, id: &str, planned: &BTreeSet<String>) -> Result<Vec<String>> {
    let from = addon_dir(data, id);
    if !from.is_dir() {
        return Ok(Vec::new());
    }
    let to = game_dir(game, id);
    let mut moved = Vec::new();
    for rel in files(&from)? {
        let at = game_rel(id, &rel);
        let (src, dst) = (from.join(&rel), to.join(&rel));
        if rel == MARK {
            with_retry(&src, || std::fs::remove_file(&src))?;
            continue;
        }
        if planned.contains(&placed::key(&at)) {
            continue;
        }
        match std::fs::symlink_metadata(&dst) {
            Ok(m) if !m.is_file() => continue,
            Ok(_) if !newer(&src, &dst) => {
                with_retry(&src, || std::fs::remove_file(&src))?;
                continue;
            }
            _ => {}
        }
        move_over(&src, &dst)?;
        moved.push(at);
    }
    prune(&from)?;
    Ok(moved)
}

/// Whether `a` was modified after `b`.
fn newer(a: &Path, b: &Path) -> bool {
    let modified = |p: &Path| std::fs::metadata(p).and_then(|m| m.modified()).ok();
    matches!((modified(a), modified(b)), (Some(x), Some(y)) if x > y)
}

/// The files under `dir`, as paths relative to it with `/` between folders.
fn files(dir: &Path) -> Result<Vec<String>> {
    let mut out = Vec::new();
    let mut todo = vec![(dir.to_path_buf(), String::new())];
    while let Some((at, prefix)) = todo.pop() {
        for e in std::fs::read_dir(&at).map_err(io_err(&at))? {
            let e = e.map_err(io_err(&at))?;
            let name = format!("{prefix}{}", e.file_name().to_string_lossy());
            match e.file_type().map_err(io_err(e.path()))? {
                t if t.is_dir() => todo.push((e.path(), format!("{name}/"))),
                _ => out.push(name),
            }
        }
    }
    out.sort();
    Ok(out)
}

/// Remove the empty folders under `dir`, and `dir` when that empties it.
fn prune(dir: &Path) -> Result<bool> {
    let mut empty = true;
    for e in std::fs::read_dir(dir).map_err(io_err(dir))? {
        let e = e.map_err(io_err(dir))?;
        if !e.file_type().is_ok_and(|t| t.is_dir()) || !prune(&e.path())? {
            empty = false;
        }
    }
    if empty {
        with_retry(dir, || std::fs::remove_dir(dir))?;
    }
    Ok(empty)
}

/// Move `from` to `to`, replacing it. Across volumes the copy keeps the
/// modification time and is flushed under a temporary name and renamed over
/// `to` before `from` goes, so a crash leaves a whole file at both ends.
fn move_over(from: &Path, to: &Path) -> Result<()> {
    if let Some(dir) = to.parent() {
        std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    }
    let renamed = with_retry(from, || match std::fs::rename(from, to) {
        Ok(()) => Ok(true),
        Err(e) if e.kind() == std::io::ErrorKind::CrossesDevices => Ok(false),
        Err(e) => Err(e),
    })?;
    if renamed {
        return Ok(());
    }
    let mut name = to.file_name().unwrap_or_default().to_os_string();
    name.push(".swofty-tmp");
    let tmp = to.with_file_name(name);
    std::fs::copy(from, &tmp).map_err(io_err(&tmp))?;
    if let Ok(t) = std::fs::metadata(from).and_then(|m| m.modified())
        && let Ok(f) = std::fs::OpenOptions::new().write(true).open(&tmp)
    {
        f.set_modified(t).ok();
    }
    crate::sync_file(&tmp)?;
    with_retry(to, || std::fs::rename(&tmp, to))?;
    with_retry(from, || std::fs::remove_file(from))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_the_addon_that_owns_a_game_path() {
        assert_eq!(
            addon_of("swofty/addons/swofty.progressive/careers.json"),
            Some("swofty.progressive")
        );
        assert_eq!(addon_of("swofty/addons/a.b/native/x.dll"), Some("a.b"));
        assert_eq!(addon_of("swofty\\addons\\a.b\\x.dll"), Some("a.b"));
        assert_eq!(addon_of("swofty/addons/core/swofty_core.dll"), None);
        assert_eq!(addon_of("swofty/addons/a.b"), None);
        assert_eq!(addon_of("swofty/settings.json"), None);
        assert_eq!(addon_of("swofty/addons/../x"), None);
        assert_eq!(addon_of("2025_asset_groups/ui_package/persistent.erp"), None);
    }
}
