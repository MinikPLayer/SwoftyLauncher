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

/// The folder in the data folder that keeps each launcher profile's copy of every addon's files,
/// `addon-profiles/<id>/<profile>/` (docs/std/launcher.md "Profiles"), outside the addon's own
/// storage so the game never sees it.
pub const PROFILES_DIR: &str = "addon-profiles";

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

// Each launcher profile has its own copy of every addon's storage. Between sessions the
// addon's storage (`addon-storage/<id>/`) holds the files of one profile, named in
// `addon-profiles/<id>/.current`; the copies of the others are in `addon-profiles/<id>/<profile>/`.
//
// - The first time 1.6 touches an addon's storage (no `.current`), it is the 1.5 storage that every
//   profile shared. It is copied to `.legacy` before anything else happens, and never deleted.
//   A profile that existed before 1.6 and has no copy starts from it; a profile created under
//   1.6 (listed in `addon-profiles/.created.json`) starts empty.
// - Switching profiles saves the storage as the current profile's copy, then swaps in the next
//   profile's copy: staged in `.incoming`, marked ready, renamed into place, `.current`
//   rewritten. A swap that stops half way is finished, or undone, by the next storage call.
// - A profile duplicated or renamed while its session runs follows that session: its writes are
//   saved under the new name when the session ends.

const CURRENT: &str = ".current";
const LEGACY: &str = ".legacy";
const INCOMING: &str = ".incoming";
const OUTGOING: &str = ".outgoing";
const SAVING: &str = ".saving";
const SAVED_OLD: &str = ".saved-old";
const FOLLOW: &str = ".follow.json";
const CREATED: &str = ".created.json";
/// `.current` when the storage holds nothing of any profile's: its profile was deleted.
const NOBODY: &str = ":none";

pub fn copies_dir(data: &Path, id: &str) -> PathBuf {
    data.join(PROFILES_DIR).join(id)
}

pub fn profile_dir(data: &Path, id: &str, profile: &str) -> PathBuf {
    copies_dir(data, id).join(profile)
}

fn ready(dir: &Path) -> PathBuf {
    let mut name = dir.file_name().unwrap_or_default().to_os_string();
    name.push(".ready");
    dir.with_file_name(name)
}

/// The addon's storage files, without [`MARK`].
fn shared_files(dir: &Path) -> Result<Vec<String>> {
    if !dir.is_dir() {
        return Ok(Vec::new());
    }
    Ok(files(dir)?.into_iter().filter(|rel| rel != MARK).collect())
}

fn copy_with_time(from: &Path, to: &Path) -> Result<()> {
    if let Some(dir) = to.parent() {
        std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    }
    std::fs::copy(from, to).map_err(io_err(to))?;
    if let Ok(t) = std::fs::metadata(from).and_then(|m| m.modified())
        && let Ok(f) = std::fs::OpenOptions::new().write(true).open(to)
    {
        f.set_modified(t).ok();
    }
    Ok(())
}

fn remove_tree(dir: &Path) -> Result<()> {
    if dir.exists() {
        with_retry(dir, || std::fs::remove_dir_all(dir))?;
    }
    Ok(())
}

fn remove_file(path: &Path) -> Result<()> {
    if path.exists() {
        with_retry(path, || std::fs::remove_file(path))?;
    }
    Ok(())
}

fn rename(from: &Path, to: &Path) -> Result<()> {
    with_retry(from, || std::fs::rename(from, to))
}

/// A complete copy of `source` (or an empty folder) at `staging`, replacing whatever was there.
fn stage(source: Option<&Path>, staging: &Path) -> Result<()> {
    remove_tree(staging)?;
    std::fs::create_dir_all(staging).map_err(io_err(staging))?;
    if let Some(src) = source {
        for rel in shared_files(src)? {
            copy_with_time(&src.join(&rel), &staging.join(&rel))?;
        }
    }
    Ok(())
}

/// Put the complete `staging` in place of `target`, through `aside`, with `label` in the ready
/// marker for [`finish`]. Nothing is lost if it stops at any point: [`heal`] finishes it.
fn swap(
    staging: &Path,
    target: &Path,
    aside: &Path,
    label: &str,
    done: &dyn Fn(&str) -> Result<()>,
) -> Result<()> {
    let mark = ready(staging);
    std::fs::write(&mark, label).map_err(io_err(&mark))?;
    finish(staging, target, aside, done)
}

fn finish(staging: &Path, target: &Path, aside: &Path, done: &dyn Fn(&str) -> Result<()>) -> Result<()> {
    let mark = ready(staging);
    let label = std::fs::read_to_string(&mark).map_err(io_err(&mark))?;
    if staging.exists() {
        if target.exists() {
            if aside.exists() {
                return Err(crate::Error::Other(format!(
                    "{} and {} both exist; move one away by hand",
                    target.display(),
                    aside.display()
                )));
            }
            rename(target, aside)?;
        }
        if let Some(dir) = target.parent() {
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
        }
        rename(staging, target)?;
    }
    done(label.trim())?;
    remove_file(&mark)?;
    remove_tree(aside)
}

/// Finish a swap that was marked ready, or drop a staging copy that wasn't.
fn heal(staging: &Path, target: &Path, aside: &Path, done: &dyn Fn(&str) -> Result<()>) -> Result<()> {
    if ready(staging).is_file() {
        return finish(staging, target, aside, done);
    }
    remove_tree(staging)?;
    if !target.exists() && aside.exists() {
        rename(aside, target)?;
    }
    remove_tree(aside)
}

fn read_text(path: &Path) -> Option<String> {
    std::fs::read_to_string(path)
        .ok()
        .map(|s| s.trim().to_owned())
        .filter(|s| !s.is_empty())
}

fn write_text(path: &Path, text: &str) -> Result<()> {
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    }
    let tmp = path.with_extension("tmp");
    std::fs::write(&tmp, text).map_err(io_err(&tmp))?;
    rename(&tmp, path)
}

fn current(data: &Path, id: &str) -> Option<String> {
    read_text(&copies_dir(data, id).join(CURRENT))
}

fn set_current(data: &Path, id: &str, profile: &str) -> Result<()> {
    write_text(&copies_dir(data, id).join(CURRENT), profile)
}

fn created(data: &Path) -> BTreeSet<String> {
    std::fs::read(data.join(PROFILES_DIR).join(CREATED))
        .ok()
        .and_then(|b| serde_json::from_slice(&b).ok())
        .unwrap_or_default()
}

fn follows(data: &Path, id: &str) -> std::collections::BTreeMap<String, String> {
    std::fs::read(copies_dir(data, id).join(FOLLOW))
        .ok()
        .and_then(|b| serde_json::from_slice(&b).ok())
        .unwrap_or_default()
}

fn set_follows(data: &Path, id: &str, f: &std::collections::BTreeMap<String, String>) -> Result<()> {
    let path = copies_dir(data, id).join(FOLLOW);
    match f.is_empty() {
        true => remove_file(&path),
        false => write_text(&path, &serde_json::to_string(f).expect("serialisable")),
    }
}

/// Finish or undo whatever a storage call left half done for the addon.
fn recover(data: &Path, id: &str) -> Result<()> {
    let copies = copies_dir(data, id);
    if !copies.is_dir() {
        return Ok(());
    }
    heal(
        &copies.join(INCOMING),
        &addon_dir(data, id),
        &copies.join(OUTGOING),
        &|p| set_current(data, id, p),
    )?;
    let (saving, old) = (copies.join(SAVING), copies.join(SAVED_OLD));
    match read_text(&ready(&saving)) {
        Some(label) => finish(&saving, &profile_dir(data, id, &label), &old, &|_| Ok(())),
        None => {
            remove_tree(&saving)?;
            remove_tree(&old)
        }
    }
}

/// Save `source` (the storage, or another copy) as `profile`'s copy.
fn save_as(data: &Path, id: &str, source: Option<&Path>, profile: &str) -> Result<()> {
    let copies = copies_dir(data, id);
    let saving = copies.join(SAVING);
    stage(source, &saving)?;
    swap(
        &saving,
        &profile_dir(data, id, profile),
        &copies.join(SAVED_OLD),
        profile,
        &|_| Ok(()),
    )
}

/// Before anything else touches a 1.5 storage, a copy of it as `.legacy`.
fn keep_legacy(data: &Path, id: &str) -> Result<()> {
    let copies = copies_dir(data, id);
    let legacy = copies.join(LEGACY);
    if legacy.is_dir() {
        return Ok(());
    }
    let storage = addon_dir(data, id);
    let saving = copies.join(SAVING);
    stage(storage.is_dir().then_some(storage.as_path()), &saving)?;
    swap(&saving, &legacy, &copies.join(SAVED_OLD), LEGACY, &|_| Ok(()))
}

/// Where `profile`'s files for the addon are now: the storage itself, a copy, or nowhere.
enum Holder {
    Storage,
    Copy(PathBuf),
    Empty,
}

fn holder(data: &Path, id: &str, profile: &str, now: Option<&str>) -> Holder {
    let copy = profile_dir(data, id, profile);
    if now == Some(profile) {
        return Holder::Storage;
    }
    if copy.is_dir() {
        return Holder::Copy(copy);
    }
    if created(data).contains(profile) {
        return Holder::Empty;
    }
    let legacy = copies_dir(data, id).join(LEGACY);
    match (now, legacy.is_dir()) {
        (None, _) => Holder::Storage,
        (_, true) => Holder::Copy(legacy),
        (_, false) => Holder::Empty,
    }
}

/// After a session: what the storage holds now becomes the copy of the profile it belongs to, and
/// of any profile duplicated or renamed from it while the session ran.
pub fn save_current(data: &Path, id: &str) -> Result<()> {
    recover(data, id)?;
    let Some(now) = current(data, id).filter(|p| p != NOBODY) else {
        return Ok(());
    };
    let storage = addon_dir(data, id);
    let source = storage.is_dir().then_some(storage.as_path());
    save_as(data, id, source, &now)?;
    let mut f = follows(data, id);
    let followers: Vec<String> = f
        .iter()
        .filter(|(_, of)| **of == now)
        .map(|(k, _)| k.clone())
        .collect();
    for name in &followers {
        save_as(data, id, source, name)?;
        f.remove(name);
    }
    set_follows(data, id, &f)
}

/// Before a session of `profile`: the addon's storage holds that profile's files. The files it
/// held are saved first as their profile's copy, or as `.legacy` when they are the 1.5 storage.
pub fn load_profile(data: &Path, id: &str, profile: &str) -> Result<bool> {
    recover(data, id)?;
    let now = current(data, id);
    if now.as_deref() == Some(profile) {
        return Ok(false);
    }
    let storage = addon_dir(data, id);
    match now.as_deref() {
        None => keep_legacy(data, id)?,
        Some(NOBODY) => {}
        Some(other) => {
            save_as(data, id, storage.is_dir().then_some(storage.as_path()), other)?;
        }
    }
    let source = match holder(data, id, profile, now.as_deref()) {
        Holder::Storage => {
            set_current(data, id, profile)?;
            return Ok(false);
        }
        Holder::Copy(p) => Some(p),
        Holder::Empty => None,
    };
    let copies = copies_dir(data, id);
    let incoming = copies.join(INCOMING);
    stage(source.as_deref(), &incoming)?;
    swap(&incoming, &storage, &copies.join(OUTGOING), profile, &|p| {
        set_current(data, id, p)
    })?;
    Ok(true)
}

/// The addons with storage or copies in the data folder.
fn stored(data: &Path) -> BTreeSet<String> {
    [dir(data), data.join(PROFILES_DIR)]
        .iter()
        .filter_map(|d| std::fs::read_dir(d).ok())
        .flatten()
        .flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| !n.starts_with('.') && valid(n))
        .collect()
}

fn session_of(data: &Path, profile: &str) -> bool {
    crate::journal::Journal::open(data).is_ok_and(|open| open.iter().any(|j| j.profile == profile))
}

/// A new profile: a copy of `from`'s storage, or empty storage of its own. While `from`'s session
/// runs, the new profile also gets that session's writes when it ends.
pub fn profile_created(data: &Path, name: &str, from: Option<&str>) -> Result<()> {
    let mut made = created(data);
    if made.insert(name.to_owned()) {
        write_text(
            &data.join(PROFILES_DIR).join(CREATED),
            &serde_json::to_string(&made).expect("serialisable"),
        )?;
    }
    let running = from.is_some_and(|f| session_of(data, f));
    for id in stored(data) {
        recover(data, &id)?;
        remove_tree(&profile_dir(data, &id, name))?;
        let Some(from) = from else {
            continue;
        };
        let now = current(data, &id);
        let source = match holder(data, &id, from, now.as_deref()) {
            Holder::Storage => {
                let storage = addon_dir(data, &id);
                storage.is_dir().then_some(storage)
            }
            Holder::Copy(p) => Some(p),
            Holder::Empty => None,
        };
        save_as(data, &id, source.as_deref(), name)?;
        if running {
            let mut f = follows(data, &id);
            f.insert(name.to_owned(), from.to_owned());
            set_follows(data, &id, &f)?;
        }
    }
    Ok(())
}

pub fn profile_renamed(data: &Path, from: &str, to: &str) -> Result<()> {
    profile_created(data, to, Some(from))?;
    profile_deleted(data, from)
}

/// A deleted profile's copies go. When the storage holds its files, they belong to the profile it
/// was renamed to while its session ran, or to nobody.
pub fn profile_deleted(data: &Path, name: &str) -> Result<()> {
    for id in stored(data) {
        recover(data, &id)?;
        remove_tree(&profile_dir(data, &id, name))?;
        let mut f = follows(data, &id);
        let heir = f.iter().find(|(_, of)| *of == name).map(|(k, _)| k.clone());
        f.retain(|k, of| k != name && of != name);
        if current(data, &id).as_deref() == Some(name) {
            set_current(data, &id, heir.as_deref().unwrap_or(NOBODY))?;
        }
        set_follows(data, &id, &f)?;
    }
    Ok(())
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
