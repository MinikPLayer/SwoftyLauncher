//! Linux: the modded game from an OverlayFS mount. A session mounts three
//! layers over the game folder (the common layer shared by every profile, the
//! profile's mod files, and the game folder itself) in a private mount
//! namespace, so nothing is copied into, backed up from or restored to the
//! real folder. This module builds the layers, records the live session, and
//! tells the launcher where the game's view of an addon folder is; the mount
//! itself is `swofty overlay-exec`.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use crate::baseline::hex;
use crate::install::{LOADER_DIR, Plan};
use crate::{Error, Result, io_err, read_json, write_json};

/// The `config.news_seen` key that hides the dialog for a launch that can't
/// use OverlayFS (not Proton, not a Steam copy, a persistent install, ...).
pub const UNSUPPORTED_KEY: &str = "overlayfs-unsupported";
/// The `config.news_seen` key that hides the dialog for a mount that failed.
pub const FALLBACK_KEY: &str = "overlayfs-fallback";

pub const STUB_REL: &str = "EAAntiCheat.GameServiceLauncher.exe";
pub const HOOK_REL: &str = "swofty/mods/core/f1op_hook.dll";

/// The one-line handshake `overlay-exec` prints once the mount is up.
pub const MOUNTED: &str = "mounted";
/// Exit codes of `overlay-exec`'s failing steps.
pub const EXIT_UNSHARE: u8 = 90;
pub const EXIT_MAPS: u8 = 91;
pub const EXIT_MOUNT: u8 = 92;
pub const EXIT_CHDIR: u8 = 93;
pub const EXIT_SPAWN: u8 = 94;

/// A test-only variable that makes `overlay-exec` fail its mount, to try the
/// fallback without changing the system.
pub const FAIL_ENV: &str = "SWOFTY_OVERLAYFS_FAIL";

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

/// The files of the two layers Swofty builds, by game-relative path.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Layers {
    /// Shared by every profile: highest in the stack.
    pub common: BTreeMap<String, PathBuf>,
    pub profile: BTreeMap<String, PathBuf>,
}

impl Layers {
    /// Whether the common layer carries the loader (`version.dll`).
    pub fn has_loader(&self) -> bool {
        self.common.contains_key(crate::install::PROXY)
    }
}

/// Split a plan into the layers. The loader's own files, the offline stub
/// that stands in for the EA AntiCheat launcher, and its hook are common;
/// everything else the plan places is the profile's.
pub fn split(plan: &Plan, loader_root: Option<&Path>, stub: &Path, hook: &Path) -> Layers {
    let mut out = Layers::default();
    out.common.insert(STUB_REL.into(), stub.to_owned());
    out.common.insert(HOOK_REL.into(), hook.to_owned());
    for (rel, src) in &plan.loader {
        let bundled = loader_root.is_some_and(|root| src.starts_with(root));
        if bundled && !out.common.contains_key(rel) {
            out.common.insert(rel.clone(), src.clone());
        }
    }
    for (rel, src) in plan.data.iter().chain(&plan.added).chain(&plan.loader) {
        if !out.common.contains_key(rel) {
            out.profile.insert(rel.clone(), src.clone());
        }
    }
    out
}

fn stamp_path(dir: &Path) -> PathBuf {
    let mut name = dir.as_os_str().to_owned();
    name.push(".stamp");
    name.into()
}

fn side(dir: &Path, ext: &str) -> PathBuf {
    let mut name = dir.as_os_str().to_owned();
    name.push(".");
    name.push(ext);
    name.into()
}

/// What a layer was built from: every file's place, source, size and time.
fn stamp_of(entries: &BTreeMap<String, PathBuf>, extra: &str) -> Result<String> {
    let mut h = Sha256::new();
    h.update(extra.as_bytes());
    for (rel, src) in entries {
        let meta = std::fs::metadata(src).map_err(io_err(src))?;
        h.update(format!(
            "\n{rel}|{}|{}|{:?}",
            src.display(),
            meta.len(),
            meta.modified().ok()
        ));
    }
    Ok(hex(&h.finalize()))
}

/// Build the layer in `dir` from `entries` unless it already matches. Returns
/// whether it was (re)built: a warm launch finds the stamp unchanged and
/// writes nothing.
///
/// The new tree is assembled next to `dir` and swapped in by renames, so a
/// crash leaves at most a stale `.new` or `.old` that the next call removes.
/// Only plain files are written: no links, no permission changes.
pub fn materialise(
    dir: &Path,
    entries: &BTreeMap<String, PathBuf>,
    extra: &str,
    free: &dyn Fn(&Path) -> Option<u64>,
) -> Result<bool> {
    let stamp = stamp_of(entries, extra)?;
    let stamp_file = stamp_path(dir);
    if dir.is_dir() && std::fs::read_to_string(&stamp_file).is_ok_and(|s| s.trim() == stamp) {
        return Ok(false);
    }
    let (new, old) = (side(dir, "new"), side(dir, "old"));
    for stale in [&new, &old] {
        if stale.exists() {
            std::fs::remove_dir_all(stale).map_err(io_err(stale))?;
        }
    }
    let mut need = 0;
    for src in entries.values() {
        need += std::fs::metadata(src).map_err(io_err(src))?.len();
    }
    let parent = dir.parent().unwrap_or(dir);
    std::fs::create_dir_all(parent).map_err(io_err(parent))?;
    if let Some(have) = free(parent).filter(|have| need > *have) {
        let drive = crate::disk::volume(parent).map_or_else(|| parent.display().to_string(), |v| v.name);
        return Err(Error::NoSpace {
            drive,
            need,
            free: have,
        });
    }
    let built = (|| -> Result<()> {
        std::fs::create_dir_all(&new).map_err(io_err(&new))?;
        for (rel, src) in entries {
            let to = new.join(rel);
            if let Some(p) = to.parent() {
                std::fs::create_dir_all(p).map_err(io_err(p))?;
            }
            std::fs::copy(src, &to).map_err(|e| Error::Io {
                path: src.clone(),
                source: e,
            })?;
        }
        Ok(())
    })();
    if let Err(e) = built {
        std::fs::remove_dir_all(&new).ok();
        return Err(match e {
            Error::Io { path, source } if crate::disk::is_full(&source) => Error::NoSpace {
                drive: crate::disk::volume(&path).map_or_else(|| path.display().to_string(), |v| v.name),
                need,
                free: free(parent).unwrap_or(0),
            },
            e => e,
        });
    }
    // A stamp that outlives the swap would vouch for the wrong tree.
    std::fs::remove_file(&stamp_file).ok();
    if dir.exists() {
        std::fs::rename(dir, &old).map_err(io_err(dir))?;
    }
    if let Err(e) = std::fs::rename(&new, dir) {
        if old.exists() {
            std::fs::rename(&old, dir).ok();
        }
        return Err(io_err(dir)(e));
    }
    std::fs::remove_dir_all(&old).ok();
    std::fs::write(&stamp_file, &stamp).map_err(io_err(&stamp_file))?;
    Ok(true)
}

/// The layers a session mounts, built and ready.
#[derive(Debug, Clone, PartialEq)]
pub struct Prepared {
    pub common: PathBuf,
    pub profile: PathBuf,
    pub layers: Layers,
    pub rebuilt: Vec<PathBuf>,
}

/// Build both layers for `profile`. Only call this while no mount of them is
/// live: OverlayFS doesn't allow changing a lower layer under a mount.
pub fn prepare(
    store: &crate::store::Store,
    profile: &str,
    buildhash: &str,
    layers: Layers,
    free: &dyn Fn(&Path) -> Option<u64>,
) -> Result<Prepared> {
    let common = store.overlay_common_dir(layers.has_loader());
    let profile_dir = store.profile_layer_dir(profile);
    let version = crate::offline::EXTENSION_VERSION;
    let mut rebuilt = Vec::new();
    if materialise(&common, &layers.common, version, free)? {
        rebuilt.push(common.clone());
    }
    if materialise(&profile_dir, &layers.profile, buildhash, free)? {
        rebuilt.push(profile_dir.clone());
    }
    Ok(Prepared {
        common,
        profile: profile_dir,
        layers,
        rebuilt,
    })
}

/// Make a profile's folder removable: the kernel creates the work dir's
/// `work` and `index` folders without any permission.
pub fn unlock(dir: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let Ok(rd) = std::fs::read_dir(dir) else {
            return;
        };
        for e in rd.flatten() {
            let path = e.path();
            if path.is_dir() && !path.is_symlink() {
                std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o700)).ok();
                unlock(&path);
            }
        }
    }
    #[cfg(not(unix))]
    let _ = dir;
}

// ---------------------------------------------------------------------------
// The mount options
// ---------------------------------------------------------------------------

/// A path inside a mount option: `\`, `:` and `,` are escaped.
pub fn escape(path: &Path) -> String {
    let mut out = String::new();
    for c in path.to_string_lossy().chars() {
        if matches!(c, '\\' | ':' | ',') {
            out.push('\\');
        }
        out.push(c);
    }
    out
}

/// The `data` string of the mount call. `lower` is highest first.
pub fn options(lower: &[PathBuf], upper: &Path, work: &Path, userxattr: bool) -> String {
    let lowers: Vec<String> = lower.iter().map(|p| escape(p)).collect();
    format!(
        "lowerdir={},upperdir={},workdir={}{}",
        lowers.join(":"),
        escape(upper),
        escape(work),
        if userxattr { ",userxattr" } else { "" }
    )
}

// ---------------------------------------------------------------------------
// Fallback record
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Kind {
    /// The launch can't use OverlayFS at all (not Proton, not a Steam copy, ...).
    Unsupported,
    /// OverlayFS was tried and failed (layers, user namespace, mount).
    MountFailed,
}

impl Kind {
    /// The `news_seen` key of this kind's "Don't show again".
    pub fn key(self) -> &'static str {
        match self {
            Kind::Unsupported => UNSUPPORTED_KEY,
            Kind::MountFailed => FALLBACK_KEY,
        }
    }

    pub fn title(self) -> &'static str {
        match self {
            Kind::Unsupported => "OverlayFS can't be used for this launch",
            Kind::MountFailed => "OverlayFS mount failed",
        }
    }

    pub fn arg(self) -> &'static str {
        match self {
            Kind::Unsupported => "unsupported",
            Kind::MountFailed => "mount-failed",
        }
    }

    pub fn from_arg(s: &str) -> Option<Self> {
        [Kind::Unsupported, Kind::MountFailed]
            .into_iter()
            .find(|k| k.arg() == s)
    }
}

/// Why a launch fell back to copying files, for the launcher's note.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Fallback {
    pub at: f64,
    pub kind: Kind,
    pub reason: String,
}

impl Fallback {
    pub fn new(kind: Kind, reason: impl Into<String>) -> Self {
        Self {
            at: now_secs(),
            kind,
            reason: reason.into(),
        }
    }

    pub fn path(data: &Path) -> PathBuf {
        data.join("state/overlayfs-fallback.json")
    }

    pub fn write(&self, data: &Path) -> Result<()> {
        write_json(&Self::path(data), self)
    }

    pub fn read(data: &Path) -> Option<Self> {
        read_json(&Self::path(data)).ok()
    }

    /// The launcher note's text.
    pub fn note(&self) -> String {
        format!(
            "OverlayFS wasn't used for the last launch: {}. Swofty copied the mod files into the game folder instead",
            self.reason
        )
    }
}

/// The wrapper writes this while the fallback dialog is open, so the
/// launcher and `swofty run` know the wrapper isn't stuck.
pub fn dialog_pending_path(data: &Path) -> PathBuf {
    data.join("state/overlayfs-dialog.pending")
}

/// Whether the wrapper is waiting for the player's answer in the dialog.
pub fn dialog_pending(data: &Path) -> bool {
    dialog_pending_path(data).is_file()
}

fn now_secs() -> f64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0.0, |d| d.as_secs_f64())
}

// ---------------------------------------------------------------------------
// Live and last sessions
// ---------------------------------------------------------------------------

/// The process that keeps the session's mount namespace alive.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Anchor {
    pub pid: u32,
    pub start_time: u64,
    pub mnt_ns: String,
}

/// What the kernel says about a process.
pub trait Proc {
    /// Field 22 of `/proc/<pid>/stat`.
    fn start_time(&self, pid: u32) -> Option<u64>;
    /// The target of `/proc/<pid>/ns/mnt`.
    fn mnt_ns(&self, pid: u32) -> Option<String>;
    /// Whether the folder is there; an error is why it can't be told.
    fn is_dir(&self, path: &Path) -> std::io::Result<bool>;
}

/// The running system's `/proc`.
pub struct HostProc;

impl Proc for HostProc {
    fn start_time(&self, pid: u32) -> Option<u64> {
        let stat = std::fs::read_to_string(format!("/proc/{pid}/stat")).ok()?;
        // The name in parentheses may hold spaces; the fields follow the last ')'.
        let rest = &stat[stat.rfind(')')? + 1..];
        rest.split_whitespace().nth(19)?.parse().ok()
    }

    fn mnt_ns(&self, pid: u32) -> Option<String> {
        std::fs::read_link(format!("/proc/{pid}/ns/mnt"))
            .ok()
            .map(|p| p.to_string_lossy().into_owned())
    }

    fn is_dir(&self, path: &Path) -> std::io::Result<bool> {
        match std::fs::metadata(path) {
            Ok(m) => Ok(m.is_dir()),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(false),
            Err(e) => Err(e),
        }
    }
}

impl Anchor {
    pub fn of(pid: u32, proc: &dyn Proc) -> Option<Self> {
        Some(Self {
            pid,
            start_time: proc.start_time(pid)?,
            mnt_ns: proc.mnt_ns(pid)?,
        })
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct LiveSession {
    pub schema: u32,
    pub game: PathBuf,
    pub profile: String,
    pub target: PathBuf,
    pub upper: PathBuf,
    pub anchor: Anchor,
}

impl LiveSession {
    pub fn path(data: &Path) -> PathBuf {
        data.join("state/overlay/session.json")
    }

    pub fn read(data: &Path) -> Option<Self> {
        read_json(&Self::path(data)).ok()
    }

    pub fn write(&self, data: &Path) -> Result<()> {
        write_json(&Self::path(data), self)?;
        LastSession {
            game: self.game.clone(),
            profile: self.profile.clone(),
            upper: self.upper.clone(),
        }
        .write(data)
    }

    pub fn clear(data: &Path) {
        std::fs::remove_file(Self::path(data)).ok();
    }
}

/// The last overlay session, kept after it ends.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct LastSession {
    pub game: PathBuf,
    pub profile: String,
    pub upper: PathBuf,
}

impl LastSession {
    pub fn path(data: &Path) -> PathBuf {
        data.join("state/overlay/last.json")
    }

    pub fn read(data: &Path) -> Option<Self> {
        read_json(&Self::path(data)).ok()
    }

    pub fn write(&self, data: &Path) -> Result<()> {
        write_json(&Self::path(data), self)
    }
}

/// The last session wasn't an overlay one: the game's files are in the game
/// folder again, and so is the addon storage.
pub fn forget_last(data: &Path) {
    std::fs::remove_file(LastSession::path(data)).ok();
}

/// The mount target when `session.json` names `game`, else none. The game
/// runs from there, so it counts as the same folder.
pub fn target_of(data: &Path, game: &Path) -> Option<PathBuf> {
    let live = LiveSession::read(data)?;
    crate::copies::same_dir(&live.game, game).then_some(live.target)
}

// ---------------------------------------------------------------------------
// Addon folders, seen from the launcher
// ---------------------------------------------------------------------------

/// Why the launcher can't reach the game's view of an addon folder.
#[derive(Debug, Clone, PartialEq)]
pub struct Unreachable(pub String);

impl std::fmt::Display for Unreachable {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

/// `<root of the anchor>/<target>`: the mount as the game sees it.
pub fn mount_view(pid: u32, target: &Path) -> PathBuf {
    let mut p = PathBuf::from(format!("/proc/{pid}/root"));
    p.push(target.strip_prefix("/").unwrap_or(target));
    p
}

/// The folder of addon `id` in an overlay session, as the game sees it:
/// - `Ok(Some)` with the path through the live session's mount namespace, or
///   with the profile's upper dir when the last session was an overlay one
///   and none is live;
/// - `Ok(None)` when the game folder's own is right (copy mode, Windows);
/// - `Err` when a session is live but its view can't be reached. Writing the
///   real folder or a live upper dir would corrupt the bridge instead.
pub fn addon_dir(
    data: &Path,
    game: &Path,
    profile: &str,
    id: &str,
    proc: &dyn Proc,
) -> std::result::Result<Option<PathBuf>, Unreachable> {
    let addon = Path::new(LOADER_DIR).join(crate::build::ADDONS_DIR).join(id);
    if let Some(live) = LiveSession::read(data) {
        if !crate::copies::same_dir(&live.game, game) {
            return Ok(None);
        }
        let a = &live.anchor;
        let alive =
            proc.start_time(a.pid) == Some(a.start_time) && proc.mnt_ns(a.pid).as_deref() == Some(&a.mnt_ns);
        if alive {
            let view = mount_view(a.pid, &live.target);
            return match proc.is_dir(&view.join(LOADER_DIR)) {
                Ok(true) => Ok(Some(view.join(addon))),
                Ok(false) => {
                    LiveSession::clear(data);
                    last_dir(data, game, profile, &addon)
                }
                Err(e) => Err(Unreachable(format!(
                    "the game runs in an OverlayFS session the launcher can't reach \
                     (/proc/{}/root: {e})",
                    a.pid
                ))),
            };
        }
        LiveSession::clear(data);
    }
    last_dir(data, game, profile, &addon)
}

fn last_dir(
    data: &Path,
    game: &Path,
    profile: &str,
    addon: &Path,
) -> std::result::Result<Option<PathBuf>, Unreachable> {
    let Some(last) = LastSession::read(data).filter(|l| crate::copies::same_dir(&l.game, game)) else {
        return Ok(None);
    };
    let upper = if last.profile == profile {
        last.upper
    } else {
        data.join("profiles").join(profile).join("writes")
    };
    Ok(Some(upper.join(addon)))
}

// ---------------------------------------------------------------------------
// Logs
// ---------------------------------------------------------------------------

/// Whether a file of the loader folder is output the session keeps: logs,
/// diagnostic dumps, crash reports and their minidumps.
pub fn is_output(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    (lower.starts_with("crash-") && (lower.ends_with(".txt") || lower.ends_with(".dmp")))
        || lower.ends_with(".log")
        || lower.ends_with(".dump")
}

/// Copy the game's logs from the upper dir to the data folder's `logs` and
/// the session's folder. The upper dir isn't changed: the loader rotates its
/// own log, so it stays bounded. Returns how many files were copied.
pub fn collect_logs(upper: &Path, logs: &Path, session: Option<&Path>) -> Result<usize> {
    let loader = upper.join(LOADER_DIR);
    let Ok(rd) = std::fs::read_dir(&loader) else {
        return Ok(0);
    };
    let mut copied = 0;
    for e in rd.flatten() {
        let (path, name) = (e.path(), e.file_name());
        if !path.is_file() || !is_output(&name.to_string_lossy()) {
            continue;
        }
        for dir in std::iter::once(logs).chain(session) {
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
            std::fs::copy(&path, dir.join(&name)).map_err(io_err(&path))?;
        }
        copied += 1;
    }
    Ok(copied)
}

/// Regular files in the upper dir that also exist in a lower layer: the game
/// rewrote a file Swofty placed, which copied it up and now shadows future
/// builds of it. Returned for a one-line warning; nothing is deleted.
pub fn shadowing(upper: &Path, lower: &[PathBuf]) -> Vec<String> {
    fn walk(dir: &Path, prefix: &str, out: &mut Vec<String>) {
        let Ok(rd) = std::fs::read_dir(dir) else {
            return;
        };
        for e in rd.flatten() {
            let rel = format!("{prefix}{}", e.file_name().to_string_lossy());
            match e.file_type() {
                Ok(t) if t.is_dir() => walk(&e.path(), &format!("{rel}/"), out),
                Ok(t) if t.is_file() => out.push(rel),
                _ => {}
            }
        }
    }
    let mut found = Vec::new();
    walk(upper, "", &mut found);
    found.retain(|rel| lower.iter().any(|l| l.join(rel).is_file()));
    found
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-ofs-{}-{name}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn file(path: &Path, text: &str) -> PathBuf {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, text).unwrap();
        path.to_owned()
    }

    fn plan(root: &Path) -> (Plan, PathBuf) {
        let loader = root.join("loader");
        let mut plan = Plan::default();
        plan.loader
            .insert("version.dll".into(), file(&loader.join("version.dll"), "v"));
        plan.loader.insert(
            "swofty/addons/core/core.dll".into(),
            file(&loader.join("swofty/addons/core/core.dll"), "c"),
        );
        let own = root.join("own");
        plan.loader.insert(
            "swofty/addons/a.b/native/x.dll".into(),
            file(&own.join("x.dll"), "x"),
        );
        plan.loader.insert(
            "swofty/settings.json".into(),
            file(&own.join("settings.json"), "{}"),
        );
        plan.loader
            .insert("swofty/ui/home.json".into(), file(&own.join("home.json"), "{}"));
        plan.data
            .insert("2025_asset_groups/a.erp".into(), file(&own.join("a.erp"), "erp"));
        plan.added.insert(
            "cs_2020_vehicle_package/n.erp".into(),
            file(&own.join("n.erp"), "new"),
        );
        (plan, loader)
    }

    #[test]
    fn plan_split_puts_the_shared_files_in_common() {
        let root = temp("split");
        let (plan, loader) = plan(&root);
        let stub = file(&root.join("ext/stub.exe"), "stub");
        let hook = file(&root.join("ext/hook.dll"), "hook");
        let layers = split(&plan, Some(&loader), &stub, &hook);
        let common: Vec<&str> = layers.common.keys().map(String::as_str).collect();
        assert_eq!(
            common,
            [
                "EAAntiCheat.GameServiceLauncher.exe",
                "swofty/addons/core/core.dll",
                "swofty/mods/core/f1op_hook.dll",
                "version.dll"
            ]
        );
        let profile: Vec<&str> = layers.profile.keys().map(String::as_str).collect();
        assert_eq!(
            profile,
            [
                "2025_asset_groups/a.erp",
                "cs_2020_vehicle_package/n.erp",
                "swofty/addons/a.b/native/x.dll",
                "swofty/settings.json",
                "swofty/ui/home.json"
            ]
        );
        assert!(layers.common.keys().all(|k| !layers.profile.contains_key(k)));
        assert!(layers.has_loader());
        let vanilla = split(&Plan::vanilla(), None, &stub, &hook);
        assert!(!vanilla.has_loader());
        assert_eq!(vanilla.common.len(), 2);
        assert!(vanilla.profile.is_empty());
        std::fs::remove_dir_all(&root).ok();
    }

    fn entries(root: &Path) -> BTreeMap<String, PathBuf> {
        BTreeMap::from([
            ("a/one.txt".to_owned(), file(&root.join("src/one.txt"), "one")),
            ("two.txt".to_owned(), file(&root.join("src/two.txt"), "two")),
        ])
    }

    fn plenty(_: &Path) -> Option<u64> {
        Some(u64::MAX)
    }

    #[test]
    fn materialise_builds_once_and_then_writes_nothing() {
        let root = temp("warm");
        let e = entries(&root);
        let dir = root.join("layer");
        assert!(materialise(&dir, &e, "h1", &plenty).unwrap());
        assert_eq!(std::fs::read_to_string(dir.join("a/one.txt")).unwrap(), "one");
        // A file only a rebuild would restore: the second call leaves it alone.
        std::fs::remove_file(dir.join("two.txt")).unwrap();
        assert!(!materialise(&dir, &e, "h1", &plenty).unwrap());
        assert!(!dir.join("two.txt").exists());
        // The stamp sits beside the layer, never inside it.
        assert!(stamp_path(&dir).is_file());
        assert!(!dir.join("layer.stamp").exists());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn materialise_rebuilds_for_a_changed_source_or_build() {
        let root = temp("rebuild");
        let e = entries(&root);
        let dir = root.join("layer");
        materialise(&dir, &e, "h1", &plenty).unwrap();
        assert!(materialise(&dir, &e, "h2", &plenty).unwrap());
        std::fs::write(&e["two.txt"], "longer").unwrap();
        assert!(materialise(&dir, &e, "h2", &plenty).unwrap());
        assert_eq!(std::fs::read_to_string(dir.join("two.txt")).unwrap(), "longer");
        let mut fewer = e.clone();
        fewer.remove("two.txt");
        assert!(materialise(&dir, &fewer, "h2", &plenty).unwrap());
        assert!(!dir.join("two.txt").exists());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn materialise_clears_an_interrupted_build() {
        let root = temp("interrupted");
        let e = entries(&root);
        let dir = root.join("layer");
        file(&side(&dir, "new").join("half.txt"), "half");
        file(&side(&dir, "old").join("older.txt"), "older");
        assert!(materialise(&dir, &e, "h", &plenty).unwrap());
        assert!(!side(&dir, "new").exists());
        assert!(!side(&dir, "old").exists());
        assert!(!dir.join("half.txt").exists());
        std::fs::remove_dir_all(&root).ok();
    }

    #[cfg(unix)]
    #[test]
    fn materialise_writes_plain_files_only() {
        let root = temp("plain");
        let e = entries(&root);
        let dir = root.join("layer");
        materialise(&dir, &e, "h", &plenty).unwrap();
        fn walk(dir: &Path) {
            for e in std::fs::read_dir(dir).unwrap().flatten() {
                let t = e.file_type().unwrap();
                assert!(!t.is_symlink());
                if t.is_dir() {
                    walk(&e.path());
                } else {
                    assert_eq!(std::os::unix::fs::MetadataExt::nlink(&e.metadata().unwrap()), 1);
                }
            }
        }
        walk(&dir);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn materialise_with_no_room_keeps_the_old_layer() {
        let root = temp("full");
        let e = entries(&root);
        let dir = root.join("layer");
        materialise(&dir, &e, "h1", &plenty).unwrap();
        let err = materialise(&dir, &e, "h2", &|_| Some(1)).unwrap_err();
        assert!(matches!(err, Error::NoSpace { .. }), "{err}");
        assert_eq!(std::fs::read_to_string(dir.join("two.txt")).unwrap(), "two");
        assert!(!side(&dir, "new").exists());
        // The old stamp still vouches for the old tree.
        assert!(!materialise(&dir, &e, "h1", &plenty).unwrap());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn materialise_names_a_missing_source() {
        let root = temp("missing");
        let mut e = entries(&root);
        e.insert("gone".into(), root.join("nope"));
        let err = materialise(&root.join("layer"), &e, "h", &plenty).unwrap_err();
        assert!(err.to_string().contains("nope"), "{err}");
        assert!(!root.join("layer").exists());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn mount_options_escape_separators() {
        assert_eq!(escape(Path::new("/a b/c:d,e\\f")), "/a b/c\\:d\\,e\\\\f");
        let o = options(
            &[PathBuf::from("/c"), PathBuf::from("/p:q"), PathBuf::from("/g g")],
            Path::new("/u"),
            Path::new("/w"),
            true,
        );
        assert_eq!(o, "lowerdir=/c:/p\\:q:/g g,upperdir=/u,workdir=/w,userxattr");
        assert!(
            !options(&[PathBuf::from("/c")], Path::new("/u"), Path::new("/w"), false).contains("userxattr")
        );
    }

    #[test]
    fn fallback_round_trips_with_its_kind() {
        let root = temp("fallback");
        assert_eq!(Fallback::read(&root), None);
        let f = Fallback::new(Kind::MountFailed, "mount: Operation not permitted");
        f.write(&root).unwrap();
        assert_eq!(Fallback::read(&root), Some(f));
        assert_ne!(Kind::Unsupported.key(), Kind::MountFailed.key());
        for k in [Kind::Unsupported, Kind::MountFailed] {
            assert_eq!(Kind::from_arg(k.arg()), Some(k));
        }
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn each_kind_checks_only_its_own_key() {
        let mut seen = std::collections::BTreeSet::new();
        seen.insert(Kind::Unsupported.key().to_owned());
        assert!(seen.contains(Kind::Unsupported.key()));
        assert!(!seen.contains(Kind::MountFailed.key()));
    }

    struct FakeProc {
        alive: Vec<(u32, u64, String)>,
        denied: bool,
        mounted: bool,
    }

    impl Proc for FakeProc {
        fn start_time(&self, pid: u32) -> Option<u64> {
            self.alive.iter().find(|a| a.0 == pid).map(|a| a.1)
        }

        fn mnt_ns(&self, pid: u32) -> Option<String> {
            self.alive.iter().find(|a| a.0 == pid).map(|a| a.2.clone())
        }

        fn is_dir(&self, _: &Path) -> std::io::Result<bool> {
            if self.denied {
                Err(std::io::Error::from(std::io::ErrorKind::PermissionDenied))
            } else {
                Ok(self.mounted)
            }
        }
    }

    fn live(data: &Path, game: &Path) -> LiveSession {
        let s = LiveSession {
            schema: 1,
            game: game.to_owned(),
            profile: "default".into(),
            target: data.join("state/overlay/game"),
            upper: data.join("profiles/default/writes"),
            anchor: Anchor {
                pid: 4242,
                start_time: 99,
                mnt_ns: "mnt:[1]".into(),
            },
        };
        s.write(data).unwrap();
        s
    }

    fn proc_with(alive: bool) -> FakeProc {
        FakeProc {
            alive: if alive {
                vec![(4242, 99, "mnt:[1]".into())]
            } else {
                vec![]
            },
            denied: false,
            mounted: true,
        }
    }

    #[test]
    fn addon_dir_goes_through_the_live_mount_namespace() {
        let data = temp("addon-live");
        let game = data.join("game");
        std::fs::create_dir_all(&game).unwrap();
        let s = live(&data, &game);
        let got = addon_dir(&data, &game, "default", "swofty.career-editor", &proc_with(true)).unwrap();
        let want = mount_view(4242, &s.target).join("swofty/addons/swofty.career-editor");
        assert_eq!(got, Some(want.clone()));
        assert!(want.starts_with("/proc/4242/root"));
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn a_dead_or_replaced_anchor_makes_the_session_stale() {
        let data = temp("addon-stale");
        let game = data.join("game");
        std::fs::create_dir_all(&game).unwrap();
        for p in [
            proc_with(false),
            FakeProc {
                alive: vec![(4242, 100, "mnt:[1]".into())],
                ..proc_with(false)
            },
            FakeProc {
                alive: vec![(4242, 99, "mnt:[2]".into())],
                ..proc_with(false)
            },
        ] {
            let s = live(&data, &game);
            let got = addon_dir(&data, &game, "default", "x.y", &p).unwrap();
            assert_eq!(got, Some(s.upper.join("swofty/addons/x.y")));
            assert!(!LiveSession::path(&data).exists());
        }
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn without_an_overlay_session_the_game_folder_is_right() {
        let data = temp("addon-none");
        let game = data.join("game");
        std::fs::create_dir_all(&game).unwrap();
        assert_eq!(
            addon_dir(&data, &game, "default", "x.y", &proc_with(true)).unwrap(),
            None
        );
        let s = live(&data, &game);
        // A copy-mode session since forgets the overlay.
        LiveSession::clear(&data);
        forget_last(&data);
        assert_eq!(
            addon_dir(&data, &game, "default", "x.y", &proc_with(true)).unwrap(),
            None
        );
        // A session for another game folder doesn't concern this one.
        let other = data.join("other");
        std::fs::create_dir_all(&other).unwrap();
        live(&data, &other);
        assert_eq!(
            addon_dir(&data, &game, "default", "x.y", &proc_with(true)).unwrap(),
            None
        );
        let _ = s;
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn another_profile_gets_its_own_upper_dir() {
        let data = temp("addon-profile");
        let game = data.join("game");
        std::fs::create_dir_all(&game).unwrap();
        live(&data, &game);
        LiveSession::clear(&data);
        let got = addon_dir(&data, &game, "grid", "x.y", &proc_with(false)).unwrap();
        assert_eq!(got, Some(data.join("profiles/grid/writes/swofty/addons/x.y")));
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn an_unreachable_view_is_an_error_not_the_game_folder() {
        let data = temp("addon-denied");
        let game = data.join("game");
        std::fs::create_dir_all(&game).unwrap();
        live(&data, &game);
        let p = FakeProc {
            denied: true,
            ..proc_with(true)
        };
        let e = addon_dir(&data, &game, "default", "x.y", &p).unwrap_err();
        assert!(e.0.contains("can't reach"), "{e}");
        assert!(LiveSession::path(&data).exists());
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn the_target_counts_as_the_game_only_while_a_session_names_it() {
        let data = temp("target");
        let game = data.join("game");
        std::fs::create_dir_all(&game).unwrap();
        assert_eq!(target_of(&data, &game), None);
        let s = live(&data, &game);
        assert_eq!(target_of(&data, &game), Some(s.target));
        let other = data.join("other");
        std::fs::create_dir_all(&other).unwrap();
        assert_eq!(target_of(&data, &other), None);
        LiveSession::clear(&data);
        assert_eq!(target_of(&data, &game), None);
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn logs_are_copied_out_of_the_upper_dir() {
        let root = temp("logs");
        let upper = root.join("writes");
        for (name, text) in [
            ("loader.log", "l"),
            ("loader.prev.log", "p"),
            ("crash-1.txt", "c"),
            ("crash-1.dmp", "d"),
            ("settings.json", "{}"),
        ] {
            file(&upper.join("swofty").join(name), text);
        }
        let (logs, session) = (root.join("logs"), root.join("session"));
        assert_eq!(collect_logs(&upper, &logs, Some(&session)).unwrap(), 4);
        assert!(logs.join("crash-1.dmp").is_file());
        assert!(session.join("loader.log").is_file());
        assert!(!logs.join("settings.json").exists());
        // The upper dir keeps what it had.
        assert!(upper.join("swofty/loader.log").is_file());
        assert_eq!(collect_logs(&root.join("none"), &logs, None).unwrap(), 0);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn copied_up_files_are_reported() {
        let root = temp("shadow");
        let (upper, lower) = (root.join("u"), root.join("l"));
        file(&upper.join("swofty/loader.log"), "log");
        file(&upper.join("swofty/settings.json"), "{}");
        file(&lower.join("swofty/settings.json"), "{}");
        assert_eq!(shadowing(&upper, &[lower]), ["swofty/settings.json"]);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_profile_folder_with_a_locked_workdir_can_be_removed() {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let root = temp("unlock");
            let work = root.join("workdir/work");
            std::fs::create_dir_all(&work).unwrap();
            std::fs::set_permissions(&work, std::fs::Permissions::from_mode(0o0)).unwrap();
            unlock(&root);
            std::fs::remove_dir_all(&root).unwrap();
        }
    }
}
