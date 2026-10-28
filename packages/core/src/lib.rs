pub use swofty_addon::{GAME_ID, asset, builder, launcher, manifest, ops, view};

pub mod activation;
pub mod app_control;
pub mod baseline;
pub mod build;
pub mod builtin;
pub mod cache;
pub mod catalog;
pub mod compat;
pub mod conflicts;
pub mod copies;
pub mod crash;
pub mod dbedit;
pub mod disk;
pub mod ea_app;
pub mod erpbuild;
pub mod exe_version;
pub mod helper;
pub mod import;
pub mod install;
pub mod journal;
pub mod launch;
pub mod loader_check;
pub mod managed;
pub mod map;
pub mod offline;
pub mod overlay_guard;
pub mod overlayfs;
pub mod owned;
pub mod package;
pub mod paths;
pub mod pkgedit;
pub mod placed;
pub mod planner;
pub mod postfx;
pub mod process;
pub mod profile;
pub mod quarantine;
pub mod recovery;
pub mod registry;
pub mod resolver;
pub mod rowkeys;
pub mod runtime;
pub mod saves;
pub mod send_logs;
pub mod session;
pub mod settings;
pub mod setup;
pub mod steam;
pub mod steam_client;
pub mod stock;
pub mod store;
pub mod tracks;
pub mod ui;
pub mod update;
pub mod updater;
pub mod usage;
pub mod vdf;

pub const STEAM_APP_ID: u32 = 3_059_520;
pub const GAME_EXE: &str = "F1_25.exe";
pub const IMAGE_BASE: u64 = 0x1_4000_0000;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("{path}: {source}")]
    Io {
        path: std::path::PathBuf,
        source: std::io::Error,
    },
    #[error("{path}: {source}")]
    Json {
        path: std::path::PathBuf,
        source: serde_json::Error,
    },
    #[error(transparent)]
    Zip(#[from] zip::result::ZipError),
    #[error(transparent)]
    Formats(#[from] swofty_formats::Error),
    #[error(transparent)]
    Addon(#[from] swofty_addon::Error),
    #[error("invalid manifest: {0}")]
    Manifest(String),
    #[error("invalid track package: {0}")]
    Map(String),
    #[error("{0}")]
    Resolve(String),
    #[error("{}", leftovers(.0))]
    Leftovers(Vec<install::Conflict>),
    #[error(
        "{} {} not the copy Steam installed, usually after a mod was copied into the game folder by hand; nothing in the game folder was changed; verify the game files in Steam to put them back (open {}, or F1 25 > Properties > Installed Files > Verify integrity of game files), then try again",
        stock::names(.0),
        if .0.len() == 1 { "is" } else { "are" },
        stock::STEAM_VERIFY_URL
    )]
    NotStock(Vec<String>),
    #[error("{}", .0.text())]
    Blocked(app_control::Block),
    #[error("{0}")]
    Requires(catalog::requires::Refusal),
    #[error("{}", no_space(drive, *need, *free))]
    NoSpace { drive: String, need: u64, free: u64 },
    #[error("{0}")]
    Other(String),
}

fn no_space(drive: &str, need: u64, free: u64) -> String {
    let colon = if drive.ends_with(':') { "" } else { ":" };
    format!(
        "not enough free space on {drive}{colon} this profile needs about {} GB, {} GB free",
        disk::gb(need),
        disk::gb(free)
    )
}

fn leftovers(found: &[install::Conflict]) -> String {
    let unknown: Vec<_> = found.iter().filter(|c| !c.owned).collect();
    let (first, rest) = match unknown.first() {
        Some(c) => (c.to_string(), unknown.len() - 1),
        None => (found[0].to_string(), found.len() - 1),
    };
    match rest {
        0 => first,
        n => format!("{first} (and {n} more)"),
    }
}

pub type Result<T> = std::result::Result<T, Error>;

pub(crate) fn io_err(path: impl Into<std::path::PathBuf>) -> impl FnOnce(std::io::Error) -> Error {
    let path = path.into();
    move |source| Error::Io { path, source }
}

/// A filesystem error worth retrying: a freshly unloaded DLL stays briefly
/// locked while Windows Defender (or another antivirus) scans it or the loader
/// releases its handle, so a delete or rename right after the game exits fails
/// with Access Denied (os error 5) or a sharing violation (os error 32).
pub(crate) fn is_locked_file_error(e: &std::io::Error) -> bool {
    e.kind() == std::io::ErrorKind::PermissionDenied || e.raw_os_error() == Some(32)
}

/// Run a filesystem mutation, retrying a locked-file error with growing backoff
/// before giving up. On a lasting lock the returned error names the likely
/// locker (an antivirus holding the game folder) and what to do about it, so a
/// half-modded folder can be cleaned rather than leaving a bare "os error 5".
pub(crate) fn with_retry<T>(path: &std::path::Path, mut op: impl FnMut() -> std::io::Result<T>) -> Result<T> {
    const DELAYS_MS: [u64; 5] = [50, 150, 300, 600, 1000];
    let mut attempt = 0usize;
    loop {
        match op() {
            Ok(v) => return Ok(v),
            Err(e) if is_locked_file_error(&e) && attempt < DELAYS_MS.len() => {
                std::thread::sleep(std::time::Duration::from_millis(DELAYS_MS[attempt]));
                attempt += 1;
            }
            Err(e) if is_locked_file_error(&e) => {
                return Err(Error::Other(format!(
                    "{}: {e} -- still locked after {} retries. Close F1 25 and the EA app if \
                     they are open; if Windows Defender or another antivirus is scanning the game \
                     folder it can hold the file. Add an exclusion for the game folder, then run \
                     `swofty restore` again.",
                    path.display(),
                    DELAYS_MS.len()
                )));
            }
            Err(e) => return Err(io_err(path.to_path_buf())(e)),
        }
    }
}

pub(crate) fn read_json<T: serde::de::DeserializeOwned>(path: &std::path::Path) -> Result<T> {
    let data = std::fs::read(path).map_err(io_err(path))?;
    serde_json::from_slice(&data).map_err(|source| Error::Json {
        path: path.into(),
        source,
    })
}

/// A state file as [`load_json`] found it.
#[derive(Debug)]
pub(crate) enum Loaded<T> {
    Missing,
    Found(T),
    /// The file was not JSON at all (empty, zero-filled, truncated or
    /// garbage) and was moved to this path.
    Damaged(std::path::PathBuf),
}

/// Read a state file the launcher wrote. A file that is not JSON at all is
/// what a crash or power loss during a write leaves; it is moved aside to
/// `<name>.corrupt-<unix time>` (never deleted) with a warning, so the
/// caller can fall back. Valid JSON in a shape this build cannot read (a
/// newer launcher's file, say) is left where it is and reported as an error.
pub(crate) fn load_json<T: serde::de::DeserializeOwned>(path: &std::path::Path) -> Result<Loaded<T>> {
    let data = match std::fs::read(path) {
        Ok(d) => d,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Loaded::Missing),
        Err(e) => return Err(io_err(path)(e)),
    };
    match serde_json::from_slice(&data) {
        Ok(v) => Ok(Loaded::Found(v)),
        Err(e)
            if matches!(
                e.classify(),
                serde_json::error::Category::Syntax | serde_json::error::Category::Eof
            ) =>
        {
            let moved = set_aside_damaged(path)?;
            eprintln!(
                "swofty: warning: {} was damaged ({} bytes, {e}); moved it to {}",
                path.display(),
                data.len(),
                moved.display()
            );
            Ok(Loaded::Damaged(moved))
        }
        Err(source) => Err(Error::Json {
            path: path.into(),
            source,
        }),
    }
}

pub(crate) const DAMAGED_SUFFIX: &str = ".corrupt-";

fn set_aside_damaged(path: &std::path::Path) -> Result<std::path::PathBuf> {
    let secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    let name = path
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .into_owned();
    let mut to = path.with_file_name(format!("{name}{DAMAGED_SUFFIX}{secs}"));
    let mut n = 1;
    while to.exists() {
        to = path.with_file_name(format!("{name}{DAMAGED_SUFFIX}{secs}-{n}"));
        n += 1;
    }
    with_retry(path, || std::fs::rename(path, &to))?;
    sync_dir(path.parent());
    Ok(to)
}

/// State files that were found damaged and set aside, for `swofty doctor`.
pub fn damaged_state(data: &std::path::Path) -> Vec<std::path::PathBuf> {
    let mut dirs = vec![
        data.to_path_buf(),
        data.join("state"),
        data.join("state/journal"),
        data.join("profiles"),
    ];
    if let Ok(rd) = std::fs::read_dir(data.join("baseline")) {
        dirs.extend(rd.flatten().map(|e| e.path()).filter(|p| p.is_dir()));
    }
    let mut out: Vec<_> = dirs
        .iter()
        .filter_map(|d| std::fs::read_dir(d).ok())
        .flat_map(|rd| rd.flatten())
        .map(|e| e.path())
        .filter(|p| {
            p.is_file()
                && p.file_name()
                    .is_some_and(|n| n.to_string_lossy().contains(&format!(".json{DAMAGED_SUFFIX}")))
        })
        .collect();
    out.sort();
    out
}

/// Flush a file's data to disk. Windows only flushes a handle opened for
/// writing, so there the file is opened that way (not truncated), lifting a
/// read-only attribute the copy carried over for as long as the open takes,
/// and retrying while an antivirus holds the new file.
pub(crate) fn sync_file(path: &std::path::Path) -> Result<()> {
    #[cfg(windows)]
    let file = with_retry(path, || {
        let open = || std::fs::OpenOptions::new().write(true).open(path);
        match open() {
            Err(e) if e.kind() == std::io::ErrorKind::PermissionDenied => {
                let mut perms = std::fs::metadata(path)?.permissions();
                if !perms.readonly() {
                    return Err(e);
                }
                #[allow(
                    clippy::permissions_set_readonly_false,
                    reason = "Windows only: clears the attribute"
                )]
                perms.set_readonly(false);
                std::fs::set_permissions(path, perms.clone())?;
                let file = open();
                perms.set_readonly(true);
                std::fs::set_permissions(path, perms)?;
                file
            }
            r => r,
        }
    })?;
    #[cfg(not(windows))]
    let file = std::fs::File::open(path).map_err(io_err(path))?;
    file.sync_all().map_err(io_err(path))
}

/// Make a rename or a new entry in `dir` durable. Unix needs the directory
/// itself flushed; NTFS journals the rename, and Windows cannot open a
/// directory this way. Best effort: some file systems refuse it.
pub(crate) fn sync_dir(dir: Option<&std::path::Path>) {
    #[cfg(unix)]
    if let Some(dir) = dir.filter(|d| !d.as_os_str().is_empty())
        && let Ok(d) = std::fs::File::open(dir)
    {
        d.sync_all().ok();
    }
    #[cfg(not(unix))]
    let _ = dir;
}

pub(crate) fn move_file(from: &std::path::Path, to: &std::path::Path) -> Result<()> {
    match std::fs::rename(from, to) {
        Err(e) if e.kind() == std::io::ErrorKind::CrossesDevices => {
            std::fs::copy(from, to).map_err(io_err(to))?;
            sync_file(to)?;
            std::fs::remove_file(from).map_err(io_err(from))
        }
        r => r.map_err(io_err(from)),
    }
}

/// Write `value` as JSON so that a crash or power loss at any point leaves
/// either the old file or the new one, never a torn or zero-filled one: the
/// temporary file's data is flushed before it is renamed over the target,
/// and on Unix the directory is flushed after.
pub fn write_json<T: serde::Serialize>(path: &std::path::Path, value: &T) -> Result<()> {
    write_json_via(path, value, &path.with_extension("json.tmp"))
}

/// [`write_json`] through a temporary name unique to this process and call,
/// for files more than one process (the launcher and the CLI) may write.
pub(crate) fn write_json_shared<T: serde::Serialize>(path: &std::path::Path, value: &T) -> Result<()> {
    static NEXT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let n = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let tmp = path.with_extension(format!("json.{}-{n}.tmp", std::process::id()));
    write_json_via(path, value, &tmp).inspect_err(|_| {
        std::fs::remove_file(&tmp).ok();
    })
}

fn write_json_via<T: serde::Serialize>(
    path: &std::path::Path,
    value: &T,
    tmp: &std::path::Path,
) -> Result<()> {
    use std::io::Write;
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    }
    let mut data = serde_json::to_vec_pretty(value).expect("serialisable");
    data.push(b'\n');
    std::fs::File::create(tmp)
        .and_then(|mut f| {
            f.write_all(&data)?;
            f.sync_all()
        })
        .map_err(io_err(tmp))?;
    std::fs::rename(tmp, path).map_err(io_err(path))?;
    sync_dir(path.parent());
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-lib-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        dir
    }

    #[test]
    fn write_json_replaces_the_file_whole_and_leaves_no_temporary() {
        let dir = tmp("write");
        let path = dir.join("state/x.json");
        write_json(&path, &vec![1, 2, 3]).unwrap();
        assert_eq!(read_json::<Vec<u32>>(&path).unwrap(), [1, 2, 3]);
        std::fs::write(path.with_extension("json.tmp"), b"half a write from a crash").unwrap();
        write_json(&path, &vec![4]).unwrap();
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "[\n  4\n]\n");
        let names: Vec<_> = std::fs::read_dir(path.parent().unwrap())
            .unwrap()
            .flatten()
            .map(|e| e.file_name())
            .collect();
        assert_eq!(names, ["x.json"]);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn load_json_sets_damage_aside_and_leaves_other_shapes() {
        let dir = tmp("load");
        let path = dir.join("x.json");
        assert!(matches!(load_json::<Vec<u32>>(&path).unwrap(), Loaded::Missing));
        write_json(&path, &vec![7]).unwrap();
        assert!(matches!(load_json::<Vec<u32>>(&path).unwrap(), Loaded::Found(v) if v == [7]));
        for (i, damage) in [&b""[..], b"\0\0\0\0", b"[7,", b"]"].into_iter().enumerate() {
            std::fs::write(&path, damage).unwrap();
            let Loaded::Damaged(moved) = load_json::<Vec<u32>>(&path).unwrap() else {
                panic!("{damage:?} is damage");
            };
            assert!(!path.exists());
            assert_eq!(std::fs::read(&moved).unwrap(), damage);
            assert_eq!(damaged_state(&dir).len(), i + 1);
        }
        std::fs::write(&path, br#"{"a":1}"#).unwrap();
        assert!(matches!(load_json::<Vec<u32>>(&path), Err(Error::Json { .. })));
        assert!(path.exists());
        std::fs::remove_dir_all(&dir).ok();
    }
}
