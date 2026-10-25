use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

use crate::baseline::{
    Baseline, DepotHashes, Hashes, Identity, Objects, sha256_bytes, sha256_file, sha256_opt,
};
use crate::journal::{Journal, Op, State};
use crate::placed::{self, Placed};
use crate::profile::VANILLA;
use crate::{Error, Result, io_err, with_retry};

pub const PROXY: &str = "version.dll";
pub const LOADER_DIR: &str = "swofty";
pub const OVERRIDE_HEADER: &str = "[Software\\\\Wine\\\\AppDefaults\\\\F1_25.exe\\\\DllOverrides]";
pub const MOVE_ASIDE_EXTENSION: &str = "swofty-orig";
const OVERRIDE_VALUE: &str = "\"version\"=\"native,builtin\"";

#[derive(Debug, Clone)]
pub struct Game {
    pub dir: PathBuf,
    pub user_reg: Option<PathBuf>,
    pub identity: Identity,
}

impl Game {
    /// The game in `dir`, with the Proton prefix's user.reg when Steam
    /// installed it there, or the EA app's prefix from `launch.ea_prefix`.
    pub fn locate(data: &Path, dir: PathBuf) -> Result<Self> {
        let found = crate::steam::find_game().filter(|g| g.dir == dir);
        let user_reg = found
            .as_ref()
            .and_then(|g| g.prefix())
            .map(|p| p.join("user.reg"))
            .filter(|p| p.is_file())
            .or_else(|| {
                (found.is_none() && !cfg!(windows) && Source::detect(&dir) == Source::EaApp)
                    .then(|| {
                        crate::store::Store::new(data.to_path_buf())
                            .config()
                            .launch
                            .ea_prefix
                    })
                    .flatten()
                    .and_then(|p| crate::ea_app::prefix_user_reg(&p))
            });
        let identity = Identity::detect_cached(data, &dir, found.and_then(|g| g.build_id))?;
        Ok(Self {
            dir,
            user_reg,
            identity,
        })
    }
}

/// The store a game folder came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Source {
    Steam,
    EaApp,
    Unknown,
}

impl Source {
    pub fn detect(dir: &Path) -> Self {
        if dir.join("EAJavelinInstaller_installscript.vdf").is_file() || crate::steam::in_steam_library(dir) {
            Self::Steam
        } else if dir.join("__Installer/installerdata.xml").is_file() {
            Self::EaApp
        } else {
            Self::Unknown
        }
    }
}

pub const EA_APP_EXPERIMENTAL: &str = "this is the EA app version of F1 25; Swofty's support for it is \
     experimental, so modded play may have bugs. Report them on the Swofty Developments Discord";

/// The `config.news_seen` key for the one-time EA app warning.
pub const EA_APP_SEEN: &str = "ea-app-experimental";

#[derive(Debug, Clone, Default)]
pub struct Plan {
    pub profile: String,
    pub data: BTreeMap<String, PathBuf>,
    pub added: BTreeMap<String, PathBuf>,
    pub loader: BTreeMap<String, PathBuf>,
    pub registry_override: bool,
}

impl Plan {
    pub fn vanilla() -> Self {
        Self {
            profile: VANILLA.into(),
            ..Self::default()
        }
    }

    pub fn is_vanilla(&self) -> bool {
        self.data.is_empty() && self.added.is_empty() && self.loader.is_empty() && !self.registry_override
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum Drift {
    Changed(String),
    Missing(String),
    Addition(String),
    RegistryOverride,
    OpenJournal(String),
    NewBuild { baseline: String, current: String },
    LeftInPlace(String),
    NoOriginal(String),
}

impl std::fmt::Display for Drift {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Drift::Changed(p) => write!(f, "{p} differs from the baseline"),
            Drift::Missing(p) => write!(f, "{p} is missing"),
            Drift::Addition(p) => write!(f, "{p} is not part of the game"),
            Drift::RegistryOverride => write!(f, "the prefix has the version.dll override"),
            Drift::OpenJournal(id) => write!(f, "journal {id} is open"),
            Drift::NewBuild { baseline, current } => {
                write!(f, "game build {current} differs from baseline {baseline}")
            }
            Drift::LeftInPlace(p) => write!(
                f,
                "{p} changed since it was written (game update?); left in place"
            ),
            Drift::NoOriginal(p) => write!(
                f,
                "{p} still holds a file swofty wrote and its original is not on record; verify the game files in Steam"
            ),
        }
    }
}

/// A file in the way of a plan: a file the plan adds that is already there,
/// or a game file the plan replaces that is not the original. `owned` means
/// it is provably the launcher's own (the bytes it recorded writing there, or
/// the bytes it is about to write), so it can be cleared without asking.
#[derive(Debug, Clone, PartialEq)]
pub struct Conflict {
    pub path: String,
    pub sha256: Option<String>,
    pub owned: bool,
    pub replaced: bool,
}

impl std::fmt::Display for Conflict {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let p = &self.path;
        match (self.replaced, self.owned) {
            (true, true) => write!(f, "{p} still holds what an unfinished session wrote"),
            (false, true) => write!(f, "{p} was left behind by an unfinished session"),
            (true, false) => write!(f, "{p} is not the original, and swofty did not write it"),
            (false, false) => write!(
                f,
                "{p} is already in the game folder, and swofty did not put it there"
            ),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct RolledBack {
    pub id: String,
    pub profile: String,
    pub session: bool,
    pub changes: usize,
    pub drift: Vec<Drift>,
}

pub struct Installer<'a> {
    pub data: &'a Path,
    pub game: &'a Game,
    pub depot: &'a dyn DepotHashes,
    pub stop_after: Option<usize>,
    pub session: bool,
    /// Where a long install says how far it got.
    pub out: &'a dyn Fn(&str),
    pub free_space: fn(&Path) -> Option<u64>,
}

const PROGRESS_EVERY: std::time::Duration = std::time::Duration::from_secs(10);
/// Room on the data folder's volume per file an install places, for the
/// journal, `placed.json` and the hash cache, each rewritten through a
/// temporary copy.
const STATE_PER_FILE: u64 = 2048;

fn quiet(_: &str) {}

/// Linux places the built files as symlinks into the data folder
/// Other platforms fall back to the old copying method.
const FLAG_USE_SYMLINKS: bool = cfg!(target_os = "linux");

fn is_symlink(path: &Path) -> bool {
    std::fs::symlink_metadata(path).is_ok_and(|m| m.file_type().is_symlink())
}

fn is_plain_file(path: &Path) -> bool {
    std::fs::symlink_metadata(path).is_ok_and(|m| m.is_file())
}

fn moved_aside_path(path: &Path) -> PathBuf {
    path.with_file_name(format!(
        ".{}.{MOVE_ASIDE_EXTENSION}",
        path.file_name().unwrap_or_default().to_string_lossy()
    ))
}

#[cfg(unix)]
fn create_symlink(src: &Path, dst: &Path) -> std::io::Result<()> {
    std::os::unix::fs::symlink(src, dst)
}

#[cfg(not(unix))]
fn create_symlink(_: &Path, _: &Path) -> std::io::Result<()> {
    Err(std::io::ErrorKind::Unsupported.into())
}

#[cfg(unix)]
fn set_writable(path: &Path, writable: bool, hashes: &mut Hashes) {
    use std::os::unix::fs::PermissionsExt;
    let Ok(meta) = std::fs::symlink_metadata(path) else {
        return;
    };
    let set = |hashes: &mut Hashes| {
        let mode = meta.permissions().mode();
        let want = if writable { mode | 0o200 } else { mode & !0o222 };
        if want != mode {
            std::fs::set_permissions(path, std::fs::Permissions::from_mode(want)).ok();
            hashes.restamp(path);
        }
    };
    if meta.is_dir() {
        if writable {
            set(hashes);
        }
        for e in std::fs::read_dir(path).into_iter().flatten().flatten() {
            set_writable(&e.path(), writable, hashes);
        }
        if !writable {
            set(hashes);
        }
    } else if meta.is_file() {
        set(hashes);
    }
}

#[cfg(not(unix))]
fn set_writable(_: &Path, _: bool, _: &mut Hashes) {}

fn remove_readonly_link(path: &Path, hashes: &mut Hashes) -> Result<()> {
    if let Ok(target) = std::fs::read_link(path) {
        set_writable(&target, true, hashes);
    }
    with_retry(path, || std::fs::remove_file(path))
}

fn list_directory_files_recursive(dir: &Path, prefix: &str, out: &mut BTreeSet<String>) -> Option<()> {
    let mut any = false;
    for e in std::fs::read_dir(dir).ok()?.flatten() {
        any = true;
        let rel = format!("{prefix}{}", e.file_name().to_string_lossy());
        if e.file_type().ok()?.is_dir() {
            list_directory_files_recursive(&e.path(), &format!("{rel}/"), out)?;
        } else {
            out.insert(rel);
        }
    }
    any.then_some(())
}

/// Returns the source directory in the data folder that `dir` can be symlinked
/// to as a whole, or `None` if the files must be linked individually.
///
/// Linking is possible when all files in `list` come from the same source
/// directory, with the same relative paths below it as below `dir`, that
/// directory contains no other files, and nothing else is being placed under
/// `dir` by the data or loader entries of the plan.
fn get_linkable_directory(dir: &str, list: &[&str], plan: &Plan) -> Option<PathBuf> {
    let mut root: Option<PathBuf> = None;
    let mut want = BTreeSet::new();
    for &rel in list {
        let src = plan.added.get(rel)?;
        let inner = &rel[dir.len() + 1..];
        if !src.ends_with(inner) {
            return None;
        }
        let top = src.ancestors().nth(inner.split('/').count())?;
        if root.get_or_insert_with(|| top.to_path_buf()) != top {
            return None;
        }
        want.insert(inner.to_owned());
    }
    let root = root?;
    let prefix = format!("{dir}/");
    if plan.data.keys().any(|k| k.starts_with(&prefix)) || plan.loader.keys().any(|k| k.starts_with(&prefix))
    {
        return None;
    }
    let mut have = BTreeSet::new();
    list_directory_files_recursive(&root, "", &mut have)?;
    (have == want).then_some(root)
}

fn rel_parts(rel: &str) -> Vec<String> {
    let parts: Vec<&str> = rel.split('/').collect();
    (1..parts.len()).map(|i| parts[..i].join("/")).collect()
}

fn remove_section(text: &str, header: &str) -> Option<String> {
    let start = text.find(header)?;
    let from = if start > 0 && text.as_bytes()[start - 1] == b'\n' {
        start - 1
    } else {
        start
    };
    let end = text[start + header.len()..]
        .find("\n[")
        .map_or(text.len(), |i| start + header.len() + i);
    let mut out = String::with_capacity(text.len());
    out.push_str(&text[..from]);
    out.push_str(&text[end..]);
    if !text[end..].is_empty() && from == start {
        out = format!("{}{}", &text[..from], &text[end + 1..]);
    }
    if text.ends_with('\n') && !out.ends_with('\n') && !out.is_empty() {
        out.push('\n');
    }
    Some(out)
}

fn override_section(text: &str) -> Option<std::ops::Range<usize>> {
    let start = text.find(OVERRIDE_HEADER)?;
    let end = text[start..].find("\n[").map_or(text.len(), |i| start + i);
    Some(start..end)
}

fn version_value(line: &str) -> Option<&str> {
    let (name, value) = line.split_once('=')?;
    let name = name.trim().trim_matches('"').trim_start_matches('*');
    name.eq_ignore_ascii_case("version")
        .then(|| value.trim().trim_matches('"'))
}

/// Whether a prefix's user.reg has F1_25.exe load a native version.dll
/// first. A section for F1_25.exe without a version.dll value leaves Wine
/// on its builtin one.
pub fn has_override(text: &str) -> bool {
    override_section(text).is_some_and(|s| {
        text[s]
            .lines()
            .skip(1)
            .filter_map(version_value)
            .any(|v| v.starts_with('n'))
    })
}

/// user.reg with the version.dll override, in F1_25.exe's section when the
/// prefix has one; None when that section already sets version.dll.
pub fn add_override(text: &str) -> Option<String> {
    let Some(s) = override_section(text) else {
        let mut out = text.to_owned();
        if !out.ends_with('\n') && !out.is_empty() {
            out.push('\n');
        }
        out.push_str(&format!("\n{OVERRIDE_HEADER}\n{OVERRIDE_VALUE}\n"));
        return Some(out);
    };
    if text[s.clone()]
        .lines()
        .skip(1)
        .any(|l| version_value(l).is_some())
    {
        return None;
    }
    let mut at = text[s.clone()].find('\n').map_or(s.end, |i| s.start + i + 1);
    while text[at..s.end].starts_with('#') {
        at = text[at..s.end].find('\n').map_or(s.end, |i| at + i + 1);
    }
    let sep = if at == text.len() && !text.ends_with('\n') {
        "\n"
    } else {
        ""
    };
    Some(format!("{}{sep}{OVERRIDE_VALUE}\n{}", &text[..at], &text[at..]))
}

/// user.reg without the version.dll override, and without F1_25.exe's
/// section when nothing else is set in it; None when the section sets other
/// DLLs but not version.dll.
fn remove_override(text: &str) -> Option<String> {
    let s = override_section(text)?;
    let lines = || text[s.clone()].lines().skip(1).map(str::trim);
    if !lines().any(|l| !l.is_empty() && !l.starts_with('#') && version_value(l).is_none()) {
        return remove_section(text, OVERRIDE_HEADER);
    }
    if !lines().any(|l| version_value(l).is_some()) {
        return None;
    }
    let kept: String = text[s.clone()]
        .split_inclusive('\n')
        .enumerate()
        .filter(|(i, l)| *i == 0 || version_value(l).is_none())
        .map(|(_, l)| l)
        .collect();
    Some(format!("{}{kept}{}", &text[..s.start], &text[s.end..]))
}

impl<'a> Installer<'a> {
    pub fn new(data: &'a Path, game: &'a Game) -> Self {
        Self {
            data,
            game,
            depot: &(),
            stop_after: None,
            session: false,
            out: &quiet,
            free_space: crate::disk::free_space,
        }
    }

    fn objects(&self) -> Objects {
        Objects {
            root: self.data.join("baseline/objects"),
        }
    }

    fn path(&self, rel: &str) -> PathBuf {
        self.game.dir.join(rel)
    }

    pub fn baseline(&self) -> Result<Baseline> {
        Baseline::load_or_new(self.data, &self.game.identity)
    }

    pub fn recover(&self) -> Result<Vec<Drift>> {
        Ok(self.roll_back(false)?.into_iter().flat_map(|r| r.drift).collect())
    }

    /// Roll back every open journal, or only the ones a `swofty run` session
    /// opened, oldest first.
    pub fn roll_back(&self, sessions_only: bool) -> Result<Vec<RolledBack>> {
        let mut hashes = Hashes::load(self.data);
        let out = self.roll_back_with(sessions_only, &mut hashes);
        hashes.save();
        out
    }

    fn roll_back_with(&self, sessions_only: bool, hashes: &mut Hashes) -> Result<Vec<RolledBack>> {
        let mut out = Vec::new();
        for j in Journal::open(self.data)? {
            if sessions_only && !j.session {
                continue;
            }
            let (id, profile, session) = (j.id.clone(), j.profile.clone(), j.session);
            let changes = j.entries.len();
            let drift = self.rollback(j, hashes)?;
            out.push(RolledBack {
                id,
                profile,
                session,
                changes,
                drift,
            });
        }
        Ok(out)
    }

    fn rollback(&self, j: Journal, hashes: &mut Hashes) -> Result<Vec<Drift>> {
        let mut report = Vec::new();
        let same_build = j.build_id == self.game.identity.build_id;
        for e in j.entries.iter().rev() {
            match e.op {
                Op::ReplaceFile => {
                    let path = self.path(&e.path);
                    let before = e.before.as_deref().expect("replace-file has before");
                    let moved = moved_aside_path(&path);
                    let parked = is_plain_file(&moved);
                    let linked = is_symlink(&path);
                    if parked && (linked || std::fs::symlink_metadata(&path).is_err()) {
                        // The original is at its hidden name, with our link over
                        // its place or, after a crash between the two renames,
                        // nothing there at all.
                        self.check_and_restore_original_file(before, &path, hashes)?;
                        continue;
                    }
                    let now = hashes.sha256_opt(&path)?;
                    if parked {
                        // Something else stands in the original's place; what
                        // was moved aside is out of date.
                        std::fs::remove_file(&moved).ok();
                    }
                    if now.as_deref() == Some(before) && !linked {
                        continue;
                    }
                    let ours = linked || now == e.after || e.state == State::Pending;
                    if ours && (same_build || linked) {
                        self.check_and_restore_original_file(before, &path, hashes)?;
                    } else {
                        report.push(Drift::LeftInPlace(e.path.clone()));
                    }
                }
                Op::CreateFile => {
                    let path = self.path(&e.path);
                    if is_symlink(&path) {
                        remove_readonly_link(&path, hashes)?;
                        continue;
                    }
                    match hashes.sha256_opt(&path)? {
                        None => {}
                        Some(now) if e.state == State::Pending || Some(&now) == e.after.as_ref() => {
                            with_retry(&path, || std::fs::remove_file(&path))?;
                        }
                        Some(_) => report.push(Drift::LeftInPlace(e.path.clone())),
                    }
                }
                Op::LinkDir => {
                    let path = self.path(&e.path);
                    if is_symlink(&path) {
                        remove_readonly_link(&path, hashes)?;
                    }
                }
                Op::CreateDir => {
                    let path = self.path(&e.path);
                    if !is_symlink(&path)
                        && path.is_dir()
                        && std::fs::read_dir(&path).map_err(io_err(&path))?.next().is_none()
                    {
                        with_retry(&path, || std::fs::remove_dir(&path))?;
                    }
                }
                Op::RegistryOverride => {
                    let path = PathBuf::from(&e.path);
                    let text = std::fs::read_to_string(&path).map_err(io_err(&path))?;
                    if let Some(out) = remove_override(&text) {
                        std::fs::write(&path, out).map_err(io_err(&path))?;
                    }
                }
            }
        }
        let mut placed = Placed::load(self.data)?;
        for e in &j.entries {
            match e.op {
                Op::ReplaceFile | Op::CreateFile => placed.remove(&e.path),
                Op::LinkDir => {
                    let prefix = placed::key(&format!("{}/", e.path));
                    placed.files.retain(|k, _| !k.starts_with(&prefix));
                }
                Op::RegistryOverride => placed.registry = None,
                Op::CreateDir => {}
            }
        }
        placed.save(self.data)?;
        j.close()?;
        Ok(report)
    }

    fn check_and_restore_original_file(
        &self,
        file_sha: &str,
        path: &Path,
        hashes: &mut Hashes,
    ) -> Result<()> {
        if let Ok(target) = std::fs::read_link(path) {
            set_writable(&target, true, hashes);
        }
        let moved = moved_aside_path(path);
        let size = std::fs::metadata(self.objects().path(file_sha))
            .ok()
            .map(|m| m.len());
        let intact = std::fs::symlink_metadata(&moved)
            .is_ok_and(|m| m.is_file() && size.is_none_or(|n| n == m.len()))
            && hashes.unchanged(&moved) == Some(true);
        if intact {
            with_retry(path, || hashes.rename(&moved, path))?;
            crate::sync_dir(path.parent());
            return Ok(());
        }
        std::fs::remove_file(&moved).ok();
        self.restore_original(file_sha, path)?;
        hashes.wrote(path, file_sha);
        Ok(())
    }

    /// Settle the originals left aside by a session that lost its journal
    fn settle_asides(&self, hashes: &mut Hashes, links: bool) -> Result<()> {
        if !Journal::open(self.data)?.is_empty() {
            return Ok(());
        }
        let Ok(baseline) = self.baseline() else {
            return Ok(());
        };
        for (rel, rec) in &baseline.files {
            let path = self.path(rel);
            let moved = moved_aside_path(&path);
            if !is_plain_file(&moved) {
                continue;
            }
            let linked = is_symlink(&path);
            if linked && !links {
                continue;
            }
            if linked || std::fs::symlink_metadata(&path).is_err() {
                self.check_and_restore_original_file(&rec.sha256, &path, hashes)?;
            } else {
                with_retry(&moved, || std::fs::remove_file(&moved))?;
            }
        }
        Ok(())
    }

    fn restore_original(&self, sha: &str, path: &Path) -> Result<()> {
        const DELAYS_MS: [u64; 5] = [50, 150, 300, 600, 1000];
        let mut attempt = 0;
        loop {
            match self.objects().restore_to(sha, path) {
                Err(Error::Io { source, .. })
                    if crate::is_locked_file_error(&source) && attempt < DELAYS_MS.len() =>
                {
                    std::thread::sleep(std::time::Duration::from_millis(DELAYS_MS[attempt]));
                    attempt += 1;
                }
                r => return r,
            }
        }
    }

    /// Clear the conflicts a failed [`apply`](Self::apply) reported that are
    /// provably the launcher's own: originals go back over replaced files and
    /// added files are removed, with any folders that held only them. Anything
    /// not owned is left alone.
    pub fn clear(&self, conflicts: &[Conflict]) -> Result<Vec<String>> {
        let mut hashes = Hashes::load(self.data);
        let out = self.clear_with(conflicts, &mut hashes);
        hashes.save();
        out
    }

    fn clear_with(&self, conflicts: &[Conflict], hashes: &mut Hashes) -> Result<Vec<String>> {
        let baseline = self.baseline()?;
        let mut placed = Placed::load(self.data)?;
        let mut notes = Vec::new();
        for c in conflicts.iter().filter(|c| c.owned) {
            let path = self.path(&c.path);
            if c.replaced {
                let rec = baseline
                    .files
                    .get(&c.path)
                    .ok_or_else(|| Error::Other(format!("{} has no backed-up original", c.path)))?;
                self.check_and_restore_original_file(&rec.sha256, &path, hashes)?;
                notes.push(format!("put the original {} back", c.path));
            } else {
                self.remove_ours(&c.path, hashes)?;
                self.prune_dirs(&c.path)?;
                notes.push(format!("removed {}", c.path));
            }
            placed.remove(&c.path);
        }
        placed.save(self.data)?;
        Ok(notes)
    }

    /// Clean up files the placed-files record says the launcher wrote and no
    /// open journal accounts for: a session that lost its journal left them.
    /// Only files that still hold exactly the recorded bytes are touched.
    pub fn sweep(&self) -> Result<Vec<String>> {
        let mut hashes = Hashes::load(self.data);
        let out = self.sweep_with(&mut hashes);
        hashes.save();
        out
    }

    fn sweep_with(&self, hashes: &mut Hashes) -> Result<Vec<String>> {
        let mut placed = Placed::load(self.data)?;
        if placed.files.is_empty() && placed.registry.is_none() {
            self.settle_asides(hashes, true)?;
            return Ok(Vec::new());
        }
        let open = Journal::open(self.data)?;
        let journaled: BTreeSet<String> = open
            .iter()
            .flat_map(|j| j.entries.iter().map(|e| placed::key(&e.path)))
            .collect();
        let baseline = self.baseline()?;
        let mut notes = Vec::new();
        let overrides = open
            .iter()
            .flat_map(|j| &j.entries)
            .any(|e| e.op == Op::RegistryOverride);
        if let Some(reg) = placed.registry.clone().filter(|_| !overrides) {
            let path = PathBuf::from(&reg);
            if let Some(out) = std::fs::read_to_string(&path)
                .ok()
                .and_then(|t| remove_override(&t))
            {
                std::fs::write(&path, out).map_err(io_err(&path))?;
                notes.push("removed the version.dll override from the Proton prefix".to_owned());
            }
            placed.registry = None;
        }
        for (key, f) in placed.files.clone() {
            if journaled.contains(&key) {
                continue;
            }
            let path = self.path(&f.path);
            if sha256_opt(&path)?.as_deref() == Some(f.sha256.as_str()) {
                if f.replaced {
                    match baseline.files.get(&f.path) {
                        Some(rec) if self.objects().has(&rec.sha256) => {
                            self.check_and_restore_original_file(&rec.sha256, &path, hashes)?;
                            notes.push(format!("put the original {} back", f.path));
                        }
                        _ => {
                            notes.push(format!(
                                "{} holds an unfinished session's file and has no backed-up original; left in place",
                                f.path
                            ));
                            continue;
                        }
                    }
                } else {
                    self.remove_ours(&f.path, hashes)?;
                    self.prune_dirs(&f.path)?;
                    notes.push(format!("removed {}", f.path));
                }
            }
            placed.files.remove(&key);
        }
        placed.save(self.data)?;
        self.settle_asides(hashes, true)?;
        Ok(notes)
    }

    /// Remove an added file of ours. One inside a folder that is a link goes
    /// with the link: removing the file itself would delete it from the data
    /// folder.
    fn remove_ours(&self, rel: &str, hashes: &mut Hashes) -> Result<()> {
        let path = self.path(rel);
        let link = rel_parts(rel)
            .iter()
            .map(|d| self.path(d))
            .find(|d| is_symlink(d))
            .or_else(|| is_symlink(&path).then_some(path.clone()));
        match link {
            Some(link) => remove_readonly_link(&link, hashes),
            None if path.is_file() => with_retry(&path, || std::fs::remove_file(&path)),
            None => Ok(()),
        }
    }

    fn prune_dirs(&self, rel: &str) -> Result<()> {
        for dir in rel_parts(rel).iter().rev() {
            let path = self.path(dir);
            let empty = path.is_dir() && std::fs::read_dir(&path).map_err(io_err(&path))?.next().is_none();
            if !empty {
                break;
            }
            with_retry(&path, || std::fs::remove_dir(&path))?;
        }
        Ok(())
    }

    /// Move what the session left in the loader folder (its logs, diagnostic
    /// dumps and crash reports with their minidumps) to `logs`, and remove the
    /// folders it leaves empty, once no journal is open. Returns how many files
    /// moved.
    pub fn tidy(&self, logs: &Path) -> Result<usize> {
        let mut hashes = Hashes::load(self.data);
        let settled = self.settle_asides(&mut hashes, true);
        hashes.save();
        settled?;
        let loader = self.path(LOADER_DIR);
        let prefix = format!("{LOADER_DIR}/");
        if !loader.is_dir()
            || !Journal::open(self.data)?.is_empty()
            || self.baseline()?.files.keys().any(|k| k.starts_with(&prefix))
        {
            return Ok(0);
        }
        let mut moved = 0;
        for e in std::fs::read_dir(&loader).map_err(io_err(&loader))?.flatten() {
            let path = e.path();
            let name = e.file_name();
            let name = name.to_string_lossy();
            let crash = name.starts_with("crash-") && (name.ends_with(".txt") || name.ends_with(".dmp"));
            let output = crash
                || path
                    .extension()
                    .is_some_and(|x| x.eq_ignore_ascii_case("log") || x.eq_ignore_ascii_case("dump"));
            if output && path.is_file() {
                std::fs::create_dir_all(logs).map_err(io_err(logs))?;
                crate::move_file(&path, &logs.join(e.file_name()))?;
                moved += 1;
            }
        }
        remove_empty(&loader)?;
        Ok(moved)
    }

    /// Whether the file at `rel`, which the baseline has no original for, may
    /// be recorded as one. Never when it holds exactly what the launcher wrote
    /// there in place of an original (a session whose journal and baseline
    /// were both lost); and while a damaged baseline is being rebuilt, only
    /// when its bytes are already a backed-up original.
    fn may_record(&self, baseline: &Baseline, objects: &Objects, placed: &Placed, rel: &str) -> Result<bool> {
        let ours = placed.get(rel).filter(|p| p.replaced);
        if ours.is_none() && !baseline.checking() {
            return Ok(true);
        }
        let now = sha256_file(&self.path(rel))?;
        if ours.is_some_and(|p| p.sha256 == now) {
            return Ok(false);
        }
        Ok(!baseline.checking() || objects.has(&now))
    }

    fn step(&self, n: &mut usize) -> Result<()> {
        *n += 1;
        match self.stop_after {
            Some(limit) if *n > limit => Err(Error::Other(format!("stopped after {limit} step(s)"))),
            _ => Ok(()),
        }
    }

    pub fn apply(&self, plan: &Plan) -> Result<()> {
        let mut hashes = Hashes::load(self.data);
        let out = self.apply_with(plan, &mut hashes);
        hashes.save();
        out
    }

    fn apply_with(&self, plan: &Plan, hashes: &mut Hashes) -> Result<()> {
        self.roll_back_with(false, hashes)?;
        self.settle_asides(hashes, false)?;
        if plan.is_vanilla() {
            return Ok(());
        }
        let loaded = self.baseline()?;
        let space = self.space(plan, &loaded);
        for (dir, v, need) in &space {
            if let Some(free) = (self.free_space)(dir).filter(|free| need > free) {
                return Err(Error::NoSpace {
                    drive: v.name.clone(),
                    need: *need,
                    free,
                });
            }
        }
        self.place(plan, hashes, loaded).map_err(|e| self.full(e, &space))
    }

    /// The bytes an install of `plan` writes on each volume: its copies, less
    /// the files they replace, on the game folder's; the originals it backs
    /// up for the first time and its state files on the data folder's.
    fn space(&self, plan: &Plan, baseline: &Baseline) -> Vec<(PathBuf, crate::disk::Volume, u64)> {
        let size = |p: &Path| std::fs::metadata(p).map_or(0, |m| m.len());
        let (mut game, mut data) = (0, 0);
        for (rel, src) in &plan.data {
            let now = size(&self.path(rel));
            if !FLAG_USE_SYMLINKS {
                game += size(src).saturating_sub(now);
            }
            if !baseline.files.contains_key(rel) {
                data += now;
            }
        }
        let copied = if FLAG_USE_SYMLINKS {
            None
        } else {
            Some(plan.added.values())
        };
        for src in copied.into_iter().flatten().chain(plan.loader.values()) {
            game += size(src);
        }
        data += (plan.data.len() + plan.added.len() + plan.loader.len()) as u64 * STATE_PER_FILE;
        crate::disk::volumes([(self.game.dir.as_path(), game), (self.data, data)])
    }

    /// A write that failed on a full volume, as the volume and the room the
    /// install needed there.
    fn full(&self, e: Error, space: &[(PathBuf, crate::disk::Volume, u64)]) -> Error {
        let Error::Io { path, source } = &e else {
            return e;
        };
        let on = crate::disk::volume(path).filter(|_| crate::disk::is_full(source));
        match on.and_then(|on| space.iter().find(|(_, v, _)| v.id == on.id)) {
            Some((dir, v, need)) => Error::NoSpace {
                drive: v.name.clone(),
                need: *need,
                free: (self.free_space)(dir).unwrap_or(0),
            },
            None => e,
        }
    }

    fn place(&self, plan: &Plan, hashes: &mut Hashes, loaded: Baseline) -> Result<()> {
        let objects = self.objects();
        let mut baseline = loaded.clone();
        let mut placed = Placed::load(self.data)?;
        let mut after = BTreeMap::new();
        for (rel, src) in plan.data.iter().chain(&plan.added).chain(&plan.loader) {
            after.insert(rel.as_str(), hashes.sha256(src)?);
        }
        let mut conflicts = Vec::new();
        let mut unproven = Vec::new();
        for rel in plan.data.keys() {
            let path = self.path(rel);
            if !baseline.files.contains_key(rel)
                && let Some(left) = placed.get(rel).filter(|p| !p.replaced)
                && hashes.sha256_opt(&path)?.as_deref() == Some(left.sha256.as_str())
            {
                conflicts.push(Conflict {
                    path: rel.clone(),
                    sha256: Some(left.sha256.clone()),
                    owned: true,
                    replaced: false,
                });
                continue;
            }
            if !baseline.files.contains_key(rel) && !self.may_record(&baseline, &objects, &placed, rel)? {
                unproven.push(rel.clone());
                continue;
            }
            let rec = baseline
                .record(&objects, &self.game.dir, rel, self.depot)?
                .clone();
            let now = hashes.sha256(&path)?;
            if now != rec.sha256 {
                conflicts.push(Conflict {
                    owned: placed.wrote(rel, &now) || after[rel.as_str()] == now,
                    path: rel.clone(),
                    sha256: Some(now),
                    replaced: true,
                });
            }
        }
        if !unproven.is_empty() {
            let rebuilt = baseline.rebuild.is_some();
            if let Some(r) = baseline.rebuild.as_mut() {
                r.told_to_verify = true;
            }
            baseline.save(self.data)?;
            let first = &unproven[0];
            let more = match unproven.len() {
                1 => String::new(),
                n => format!(" (and {} more)", n - 1),
            };
            return Err(Error::Other(if rebuilt {
                format!(
                    "{}. {first}{more} could not be checked against a backed-up original; nothing in the game folder was changed",
                    crate::baseline::DAMAGED
                )
            } else {
                format!(
                    "{}. {first}{more} still holds a file swofty wrote, and no original is on record for it; nothing in the game folder was changed",
                    crate::baseline::DAMAGED
                )
            }));
        }
        if baseline != loaded {
            baseline.save(self.data)?;
        }
        for rel in plan.added.keys().chain(plan.loader.keys()) {
            let path = self.path(rel);
            if std::fs::symlink_metadata(&path).is_err() {
                continue;
            }
            let now = hashes.sha256_opt(&path)?;
            let stock = baseline.files.contains_key(rel);
            conflicts.push(Conflict {
                owned: now
                    .as_deref()
                    .is_some_and(|now| placed.wrote(rel, now) || (!stock && after[rel.as_str()] == now)),
                path: rel.clone(),
                sha256: now,
                replaced: false,
            });
        }
        if !conflicts.is_empty() {
            return Err(Error::Leftovers(conflicts));
        }
        let mut j = Journal::create(
            self.data,
            &plan.profile,
            &self.game.identity.build_id,
            self.session,
        )?;
        for rel in plan.data.keys() {
            placed.insert(rel, after[rel.as_str()].clone(), true);
        }
        for rel in plan.added.keys().chain(plan.loader.keys()) {
            placed.insert(rel, after[rel.as_str()].clone(), false);
        }
        let registry = match &self.game.user_reg {
            Some(reg) if plan.registry_override => {
                let text = std::fs::read_to_string(reg).map_err(io_err(reg))?;
                let name = reg.display().to_string();
                let ours = placed.registry.as_deref() == Some(name.as_str());
                match add_override(&text) {
                    Some(out) => Some((reg, name, text, out)),
                    None => ours.then(|| (reg, name, text.clone(), text)),
                }
            }
            _ => None,
        };
        if let Some((_, name, _, _)) = &registry {
            placed.registry = Some(name.clone());
        }
        placed.save(self.data)?;
        let mut steps = Vec::new();
        for rel in plan.data.keys() {
            let before = baseline.files[rel].sha256.clone();
            steps.push((
                Op::ReplaceFile,
                rel.clone(),
                Some(before),
                Some(after[rel.as_str()].clone()),
            ));
        }
        let mut links = BTreeMap::new();
        if FLAG_USE_SYMLINKS {
            let rels: Vec<&str> = plan
                .added
                .keys()
                .chain(plan.loader.keys())
                .map(String::as_str)
                .collect();
            self.link_level("", &rels, plan, &after, &mut steps, &mut links);
        } else {
            let mut dirs = BTreeSet::new();
            for rel in plan.added.keys().chain(plan.loader.keys()) {
                for dir in rel_parts(rel) {
                    if !self.path(&dir).is_dir() && dirs.insert(dir.clone()) {
                        steps.push((Op::CreateDir, dir, None, None));
                    }
                }
                steps.push((
                    Op::CreateFile,
                    rel.clone(),
                    None,
                    Some(after[rel.as_str()].clone()),
                ));
            }
        }
        let registry = match registry {
            Some((reg, name, text, out)) => {
                let (before, now) = (objects.put_file(reg)?, sha256_bytes(out.as_bytes()));
                steps.push((Op::RegistryOverride, name, Some(before), Some(now)));
                Some((reg, text, out))
            }
            None => None,
        };
        j.begin_all(steps)?;
        let sources: BTreeMap<&str, &PathBuf> = plan
            .data
            .iter()
            .chain(&plan.added)
            .chain(&plan.loader)
            .map(|(rel, src)| (rel.as_str(), src))
            .collect();
        let (mut n, mut copied, mut told) = (0, 0, std::time::Instant::now());
        for i in 0..j.entries.len() {
            self.step(&mut n)?;
            let e = &j.entries[i];
            let path = self.path(&e.path);
            match e.op {
                Op::ReplaceFile if FLAG_USE_SYMLINKS => {
                    let src = std::path::absolute(sources[e.path.as_str()]).map_err(io_err(&path))?;
                    let tmp = path.with_file_name(format!(
                        ".{}.swofty-link",
                        path.file_name().unwrap_or_default().to_string_lossy()
                    ));
                    std::fs::remove_file(&tmp).ok();
                    create_symlink(&src, &tmp).map_err(io_err(&tmp))?;
                    // The stock file moves aside by rename, so nothing is written
                    // now or when it comes back. A crash between the two renames
                    // leaves it at the hidden name with the entry pending, which
                    // the rollback renames back.
                    let moved = moved_aside_path(&path);
                    hashes.rename(&path, &moved).map_err(io_err(&moved))?;
                    std::fs::rename(&tmp, &path).map_err(io_err(&path))?;
                    set_writable(&src, false, hashes);
                }
                Op::ReplaceFile => {
                    std::fs::copy(sources[e.path.as_str()], &path)
                        .map_err(|err| crate::quarantine::copy_error(&e.path, &path, err))?;
                }
                Op::LinkDir => {
                    let src = std::path::absolute(&links[e.path.as_str()]).map_err(io_err(&path))?;
                    create_symlink(&src, &path).map_err(io_err(&path))?;
                    set_writable(&src, false, hashes);
                }
                Op::CreateFile if FLAG_USE_SYMLINKS && !plan.loader.contains_key(&e.path) => {
                    let src = std::path::absolute(sources[e.path.as_str()]).map_err(io_err(&path))?;
                    create_symlink(&src, &path).map_err(io_err(&path))?;
                    set_writable(&src, false, hashes);
                }
                Op::CreateDir => std::fs::create_dir(&path).map_err(io_err(&path))?,
                // Windows refused the file as a threat. The entry stays pending, so
                // the rollback clears whatever was left, and the check before the
                // game starts finds the file missing and refuses the launch with
                // the antivirus report instead of a bare copy error here.
                Op::CreateFile => match std::fs::copy(sources[e.path.as_str()], &path) {
                    Err(err) if crate::quarantine::is_virus_error(&err) => continue,
                    r => {
                        r.map_err(io_err(&path))?;
                    }
                },
                Op::RegistryOverride => {
                    if let Some((reg, text, out)) = &registry
                        && out != text
                    {
                        std::fs::write(reg, out).map_err(io_err(*reg))?;
                    }
                }
            }
            if matches!(e.op, Op::ReplaceFile | Op::CreateFile) {
                hashes.wrote(&path, e.after.as_deref().expect("a placed file has after"));
                copied += 1;
            }
            j.done(i)?;
            if told.elapsed() >= PROGRESS_EVERY {
                (self.out)(&format!(
                    "installing: {copied} of {} file(s) copied",
                    sources.len()
                ));
                told = std::time::Instant::now();
            }
        }
        j.commit()
    }

    /// The steps that put the added files of `entries` under `dir`, linking as
    /// little as possible: a file is linked on its own, a folder the game
    /// folder lacks is linked whole, and a folder it has is gone into.
    fn link_level(
        &self,
        dir: &str,
        entries: &[&str],
        plan: &Plan,
        after: &BTreeMap<&str, String>,
        steps: &mut Vec<(Op, String, Option<String>, Option<String>)>,
        links: &mut BTreeMap<String, PathBuf>,
    ) {
        let prefix = if dir.is_empty() {
            String::new()
        } else {
            format!("{dir}/")
        };
        let mut subs: BTreeMap<&str, Vec<&str>> = BTreeMap::new();
        for &rel in entries {
            match rel[prefix.len()..].split_once('/') {
                None => steps.push((Op::CreateFile, rel.to_owned(), None, Some(after[rel].clone()))),
                Some((d, _)) => subs.entry(d).or_default().push(rel),
            }
        }
        for (d, list) in subs {
            let child = format!("{prefix}{d}");
            if self.path(&child).is_dir() {
                self.link_level(&child, &list, plan, after, steps, links);
            } else if let Some(src) = get_linkable_directory(&child, &list, plan) {
                steps.push((Op::LinkDir, child.clone(), None, None));
                links.insert(child, src);
            } else {
                steps.push((Op::CreateDir, child.clone(), None, None));
                self.link_level(&child, &list, plan, after, steps, links);
            }
        }
    }

    pub fn restore(&self) -> Result<Vec<Drift>> {
        let mut report = self.recover()?;
        self.sweep()?;
        let proxy = self.path(PROXY);
        let baseline = self.baseline()?;
        if proxy.is_file() && !baseline.files.contains_key(PROXY) {
            with_retry(&proxy, || std::fs::remove_file(&proxy))?;
        }
        let loader = self.path(LOADER_DIR);
        if loader.is_dir() {
            let logs = self.data.join("sessions/orphaned-logs");
            for e in std::fs::read_dir(&loader).map_err(io_err(&loader))?.flatten() {
                if e.path().extension().is_some_and(|x| x == "log") {
                    std::fs::create_dir_all(&logs).map_err(io_err(&logs))?;
                    crate::move_file(&e.path(), &logs.join(e.file_name()))?;
                }
            }
            with_retry(&loader, || std::fs::remove_dir_all(&loader))?;
        }
        if let Some(reg) = &self.game.user_reg
            && let Ok(text) = std::fs::read_to_string(reg)
            && let Some(out) = remove_override(&text)
        {
            self.objects().put_file(reg)?;
            std::fs::write(reg, out).map_err(io_err(reg))?;
        }
        for (rel, rec) in &baseline.files {
            let path = self.path(rel);
            if sha256_opt(&path)?.as_deref() != Some(rec.sha256.as_str()) {
                self.objects().restore_to(&rec.sha256, &path)?;
            }
        }
        let mut hashes = Hashes::load(self.data);
        let settled = self.settle_asides(&mut hashes, true);
        hashes.save();
        settled?;
        report.extend(self.verify()?);
        Ok(report)
    }

    pub fn verify(&self) -> Result<Vec<Drift>> {
        let mut out: Vec<Drift> = Journal::open(self.data)?
            .into_iter()
            .map(|j| Drift::OpenJournal(j.id))
            .collect();
        let baseline = self.baseline()?;
        let idle = out.is_empty();
        for (rel, rec) in &baseline.files {
            // Outside a session a stock file set aside is a leftover.
            if idle && is_plain_file(&moved_aside_path(&self.path(rel))) {
                let (dir, name) = rel.rsplit_once('/').map_or(("", rel.as_str()), |(d, n)| (d, n));
                let sep = if dir.is_empty() { "" } else { "/" };
                out.push(Drift::Addition(format!(
                    "{dir}{sep}.{name}.{MOVE_ASIDE_EXTENSION}"
                )));
            }
            match sha256_opt(&self.path(rel))? {
                None => out.push(Drift::Missing(rel.clone())),
                Some(s) if s != rec.sha256 => out.push(Drift::Changed(rel.clone())),
                Some(_) => {}
            }
        }
        for rel in [PROXY, LOADER_DIR] {
            if self.path(rel).exists() && !baseline.files.contains_key(rel) {
                out.push(Drift::Addition(rel.into()));
            }
        }
        for f in Placed::load(self.data)?.files.values() {
            if f.replaced
                && !baseline.files.contains_key(&f.path)
                && sha256_opt(&self.path(&f.path))?.as_deref() == Some(f.sha256.as_str())
            {
                out.push(Drift::NoOriginal(f.path.clone()));
            }
        }
        if let Some(reg) = &self.game.user_reg
            && std::fs::read_to_string(reg).is_ok_and(|t| has_override(&t))
        {
            out.push(Drift::RegistryOverride);
        }
        let latest = std::fs::read_dir(self.data.join("baseline"))
            .into_iter()
            .flatten()
            .flatten()
            .filter_map(|e| crate::read_json::<Baseline>(&e.path().join("baseline.json")).ok())
            .find(|b| {
                b.build_id != self.game.identity.build_id && b.files.keys().any(|k| self.path(k).exists())
            });
        if let Some(old) = latest
            && baseline.files.is_empty()
        {
            out.push(Drift::NewBuild {
                baseline: old.build_id,
                current: self.game.identity.build_id.clone(),
            });
        }
        Ok(out)
    }
}

fn remove_empty(dir: &Path) -> Result<bool> {
    let mut empty = true;
    for e in std::fs::read_dir(dir).map_err(io_err(dir))?.flatten() {
        let path = e.path();
        let is_dir = e.file_type().is_ok_and(|t| t.is_dir());
        if !is_dir || !remove_empty(&path)? {
            empty = false;
        }
    }
    if empty {
        with_retry(dir, || std::fs::remove_dir(dir))?;
    }
    Ok(empty)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tells_steam_and_ea_app_folders_apart() {
        let root = std::env::temp_dir().join(format!("swofty-source-{}", std::process::id()));
        let touch = |rel: &str| {
            let p = root.join(rel);
            std::fs::create_dir_all(p.parent().unwrap()).unwrap();
            std::fs::write(p, "x").unwrap();
        };
        touch("ea/F1 25/F1_25.exe");
        touch("ea/F1 25/__Installer/installerdata.xml");
        touch("steam/F1 25/F1_25.exe");
        touch("steam/F1 25/EAJavelinInstaller_installscript.vdf");
        touch("lib/steamapps/appmanifest_3059520.acf");
        touch("lib/steamapps/common/F1 25/F1_25.exe");
        touch("lib/steamapps/common/F1 25/__Installer/installerdata.xml");
        touch("bare/F1 25/F1_25.exe");
        assert_eq!(Source::detect(&root.join("ea/F1 25")), Source::EaApp);
        assert_eq!(Source::detect(&root.join("steam/F1 25")), Source::Steam);
        assert_eq!(
            Source::detect(&root.join("lib/steamapps/common/F1 25")),
            Source::Steam
        );
        assert_eq!(Source::detect(&root.join("bare/F1 25")), Source::Unknown);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_full_volume_is_named_with_what_the_install_needed_there() {
        let root = std::env::temp_dir().join(format!("swofty-full-{}", std::process::id()));
        let game = Game {
            dir: root.join("game"),
            user_reg: None,
            identity: Identity {
                build_id: "1".into(),
                exe_sha256: String::new(),
            },
        };
        let inst = Installer {
            free_space: |_| Some(5 << 20),
            ..Installer::new(&root, &game)
        };
        let space = crate::disk::volumes([(game.dir.as_path(), 3 << 30)]);
        let v = space[0].1.clone();
        let full = |source: std::io::Error| Error::Io {
            path: root.join("state/journal/x.json.tmp"),
            source,
        };
        match inst.full(full(std::io::ErrorKind::StorageFull.into()), &space) {
            Error::NoSpace { drive, need, free } => {
                assert_eq!((drive, need, free), (v.name, 3 << 30, 5 << 20))
            }
            e => panic!("{e}"),
        }
        assert!(matches!(
            inst.full(full(std::io::ErrorKind::PermissionDenied.into()), &space),
            Error::Io { .. }
        ));
        assert!(matches!(
            inst.full(full(std::io::ErrorKind::StorageFull.into()), &[]),
            Error::Io { .. }
        ));
        let shown = Error::NoSpace {
            drive: "C:".into(),
            need: 3 << 30,
            free: 1 << 29,
        };
        assert_eq!(
            shown.to_string(),
            "not enough free space on C: this profile needs about 3.0 GB, 0.5 GB free"
        );
    }

    #[test]
    fn removes_appended_section_exactly() {
        let orig = "WINE REGISTRY Version 2\n\n[Software\\\\Wine] 1\n\"a\"=\"b\"\n";
        let with = format!("{orig}\n{OVERRIDE_HEADER}\n{OVERRIDE_VALUE}\n");
        assert_eq!(remove_section(&with, OVERRIDE_HEADER).unwrap(), orig);
        let middle = format!("{orig}\n{OVERRIDE_HEADER}\n{OVERRIDE_VALUE}\n\n[Other] 2\n\"x\"=\"y\"\n");
        assert_eq!(
            remove_section(&middle, OVERRIDE_HEADER).unwrap(),
            format!("{orig}\n[Other] 2\n\"x\"=\"y\"\n")
        );
    }

    #[test]
    fn an_empty_section_gets_the_override_and_loses_it_again() {
        let orig = format!(
            "WINE REGISTRY Version 2\n\n{OVERRIDE_HEADER} 1792606023\n#time=1dd6186fa06f04c\n\n[Other] 2\n\"x\"=\"y\"\n"
        );
        assert!(!has_override(&orig));
        let with = add_override(&orig).unwrap();
        assert_eq!(
            with,
            format!(
                "WINE REGISTRY Version 2\n\n{OVERRIDE_HEADER} 1792606023\n#time=1dd6186fa06f04c\n{OVERRIDE_VALUE}\n\n[Other] 2\n\"x\"=\"y\"\n"
            )
        );
        assert!(has_override(&with));
        assert_eq!(add_override(&with), None);
        assert_eq!(
            remove_override(&with).unwrap(),
            "WINE REGISTRY Version 2\n\n[Other] 2\n\"x\"=\"y\"\n"
        );
        assert_eq!(
            remove_override(&orig).unwrap(),
            "WINE REGISTRY Version 2\n\n[Other] 2\n\"x\"=\"y\"\n"
        );
    }

    #[test]
    fn the_override_joins_and_leaves_a_section_with_other_dlls() {
        let orig = format!("WINE REGISTRY Version 2\n\n{OVERRIDE_HEADER} 5\n\"d3d11\"=\"native\"\n");
        let with = add_override(&orig).unwrap();
        assert_eq!(
            with,
            format!(
                "WINE REGISTRY Version 2\n\n{OVERRIDE_HEADER} 5\n{OVERRIDE_VALUE}\n\"d3d11\"=\"native\"\n"
            )
        );
        assert!(has_override(&with));
        assert_eq!(remove_override(&with).unwrap(), orig);
        assert_eq!(remove_override(&orig), None);
        let header_only = format!("WINE REGISTRY Version 2\n\n{OVERRIDE_HEADER}");
        assert_eq!(
            add_override(&header_only).unwrap(),
            format!("{header_only}\n{OVERRIDE_VALUE}\n")
        );
    }

    #[test]
    fn a_version_value_of_the_prefix_is_left_alone() {
        for (value, native) in [
            ("\"version\"=\"native,builtin\"", true),
            ("\"*version\"=\"n,b\"", true),
            ("\"Version\"=\"builtin\"", false),
            ("\"version\"=\"\"", false),
        ] {
            let text = format!("WINE REGISTRY Version 2\n\n{OVERRIDE_HEADER} 5\n{value}\n");
            assert_eq!(has_override(&text), native, "{value}");
            assert_eq!(add_override(&text), None, "{value}");
        }
        let fresh = add_override("WINE REGISTRY Version 2\n").unwrap();
        assert_eq!(
            fresh,
            format!("WINE REGISTRY Version 2\n\n{OVERRIDE_HEADER}\n{OVERRIDE_VALUE}\n")
        );
        assert_eq!(remove_override(&fresh).unwrap(), "WINE REGISTRY Version 2\n");
    }

    #[test]
    fn dir_prefixes() {
        assert_eq!(
            rel_parts("swofty/addons/core/x.dll"),
            ["swofty", "swofty/addons", "swofty/addons/core"]
        );
        assert!(rel_parts("version.dll").is_empty());
    }
}
