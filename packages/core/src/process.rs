use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

use swofty_addon::launcher::GameStatus;

use crate::GAME_EXE;
use crate::session::SessionRecord;

#[derive(Debug, Clone, PartialEq)]
pub struct GameProcess {
    pub pid: u32,
    pub started: Option<SystemTime>,
    pub origin: Origin,
}

impl GameProcess {
    /// Whether this is the game in `dir`. A process that tells nothing about
    /// where it was started from belongs to no copy.
    pub fn is_in(&self, dir: &Path) -> bool {
        in_folder(&self.origin.folders(), dir, overlay_target(dir).as_deref())
    }

    /// Whether this is the game of a folder other than `dir`.
    pub fn elsewhere(&self, dir: &Path) -> bool {
        let folders = self.origin.folders();
        !folders.is_empty() && !in_folder(&folders, dir, overlay_target(dir).as_deref())
    }
}

/// Whether one of the `folders` a game was started from is `dir`, or `alias`,
/// the OverlayFS mount that stands in for `dir` while a session runs from it.
fn in_folder(folders: &[PathBuf], dir: &Path, alias: Option<&Path>) -> bool {
    folders
        .iter()
        .any(|f| same_folder(f, dir) || alias.is_some_and(|a| same_folder(f, a)))
}

/// The mount a live OverlayFS session runs the game of `dir` from.
fn overlay_target(dir: &Path) -> Option<PathBuf> {
    cfg!(unix)
        .then(|| crate::overlayfs::target_of(&crate::paths::data_dir(), dir))
        .flatten()
}

/// What a running F1_25.exe tells about which copy of the game it is: the
/// exe file (the process image on Windows, the file mapped at the image base
/// under Wine), and under Wine the Windows path it was started with and the
/// prefix that maps that path's drive.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Origin {
    pub image: Option<String>,
    pub command: Option<String>,
    pub prefix: Option<PathBuf>,
}

impl Origin {
    pub fn folders(&self) -> Vec<PathBuf> {
        let image = self.image.as_deref().and_then(exe_folder);
        let command = self
            .command
            .as_deref()
            .and_then(|c| host_path(c, self.prefix.as_deref()))
            .and_then(|p| exe_folder(&p.to_string_lossy()));
        image.into_iter().chain(command).collect()
    }
}

fn exe_folder(path: &str) -> Option<PathBuf> {
    let cut = path.rfind(['\\', '/'])?;
    path[cut + 1..]
        .eq_ignore_ascii_case(GAME_EXE)
        .then(|| PathBuf::from(&path[..cut.max(1)]))
}

/// A path as Wine shows it (`Z:\home\...`, `D:\F1 25\...`) on this computer:
/// `Z:` is the root, and any other drive is the prefix's `dosdevices` link.
fn host_path(windows: &str, prefix: Option<&Path>) -> Option<PathBuf> {
    if windows.starts_with('/') {
        return Some(PathBuf::from(windows));
    }
    let windows = windows.strip_prefix(r"\\?\").unwrap_or(windows);
    let (drive, rest) = windows.split_once(':')?;
    let drive = drive.to_ascii_lowercase();
    if drive.len() != 1 || !rest.starts_with(['\\', '/']) {
        return None;
    }
    let linked = prefix.and_then(|p| {
        let devices = p.join("dosdevices");
        let target = std::fs::read_link(devices.join(format!("{drive}:"))).ok()?;
        Some(devices.join(target))
    });
    let root = linked.or_else(|| (drive == "z").then(|| PathBuf::from("/")))?;
    Some(
        rest.split(['\\', '/'])
            .filter(|c| !c.is_empty())
            .fold(root, |p, c| p.join(c)),
    )
}

/// Whether two game folders are the same: Windows paths match whatever their
/// case and slashes, and links are followed.
pub fn same_folder(a: &Path, b: &Path) -> bool {
    folder_key(a) == folder_key(b) || crate::copies::same_dir(a, b)
}

fn folder_key(p: &Path) -> String {
    let s = p.to_string_lossy();
    let s = s.strip_prefix(r"\\?\").unwrap_or(&s);
    if s.as_bytes().get(1) == Some(&b':') {
        s.replace('/', "\\").trim_end_matches('\\').to_lowercase()
    } else {
        s.trim_end_matches('/').to_owned()
    }
}

pub fn game_running() -> bool {
    !game_processes().is_empty()
}

/// The running games of the copy in `dir`.
pub fn games_in(dir: &Path) -> Vec<GameProcess> {
    game_processes().into_iter().filter(|g| g.is_in(dir)).collect()
}

pub fn game_running_in(dir: &Path) -> bool {
    !games_in(dir).is_empty()
}

/// The running game for a launcher plugin's game_status: its process, and the
/// latest swofty session under `data` while that session is still live.
pub fn game_status(data: Option<&Path>) -> GameStatus {
    status_of(&game_processes(), data)
}

fn status_of(processes: &[GameProcess], data: Option<&Path>) -> GameStatus {
    let Some(first) = processes.first() else {
        return GameStatus::default();
    };
    let session = data
        .and_then(|d| std::fs::read_to_string(d.join("sessions/latest.txt")).ok())
        .map(|dir| SessionRecord::read(Path::new(dir.trim())))
        .filter(|s| s.started.is_some() && s.ended.is_none())
        .and_then(|s| Some(s.dir.file_name()?.to_string_lossy().into_owned()));
    GameStatus {
        running: true,
        pid: Some(first.pid),
        since: first
            .started
            .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
            .map(|d| d.as_secs_f64()),
        session,
    }
}

pub fn game_processes() -> Vec<GameProcess> {
    // A process's exe never changes, and reading a running game's maps
    // takes its memory-map lock, so a game's origin is read once under Wine.
    static SEEN: std::sync::Mutex<Vec<(u32, Origin)>> = std::sync::Mutex::new(Vec::new());
    let found = named(GAME_EXE);
    let mut seen = SEEN.lock().unwrap_or_else(|e| e.into_inner());
    seen.retain(|(pid, _)| found.iter().any(|g| g.pid == *pid));
    found
        .into_iter()
        .map(|g| {
            let origin = match seen.iter().find(|(pid, _)| *pid == g.pid) {
                Some((_, o)) => o.clone(),
                None => {
                    let o = origin(g.pid);
                    if cfg!(target_os = "linux") && o.image.is_some() {
                        seen.push((g.pid, o.clone()));
                    }
                    o
                }
            };
            GameProcess { origin, ..g }
        })
        .collect()
}

/// The games in `running` that were not there before a launch (`before`),
/// that the session did not start (`ours`) and that are not another copy's
/// than the one in `dir`: the EA App build hands a launch back to the EA
/// app, which starts the game again outside the session.
pub fn started_elsewhere(
    running: &[GameProcess],
    dir: &Path,
    before: &[u32],
    ours: impl Fn(u32) -> bool,
) -> Vec<u32> {
    running
        .iter()
        .filter(|g| !before.contains(&g.pid) && !ours(g.pid) && !g.elsewhere(dir))
        .map(|g| g.pid)
        .collect()
}

pub fn steamvr_running() -> bool {
    !named(if cfg!(windows) { "vrserver.exe" } else { "vrserver" }).is_empty()
}

#[cfg(target_os = "linux")]
pub(crate) fn named(name: &str) -> Vec<GameProcess> {
    let Ok(entries) = std::fs::read_dir("/proc") else {
        return Vec::new();
    };
    entries
        .flatten()
        .filter_map(|e| {
            let pid = e.file_name().to_str()?.parse().ok()?;
            let comm = std::fs::read_to_string(e.path().join("comm")).ok()?;
            (comm.trim_end() == name).then(|| GameProcess {
                pid,
                started: started(pid),
                origin: Origin::default(),
            })
        })
        .collect()
}

#[cfg(target_os = "linux")]
pub fn descends_from(pid: u32, ancestor: u32) -> bool {
    std::iter::successors(parent(pid), |&p| parent(p))
        .take_while(|&p| p > 1)
        .any(|p| p == ancestor)
}

#[cfg(not(target_os = "linux"))]
pub fn descends_from(_: u32, _: u32) -> bool {
    false
}

#[cfg(target_os = "linux")]
fn parent(pid: u32) -> Option<u32> {
    let stat = std::fs::read_to_string(format!("/proc/{pid}/stat")).ok()?;
    stat.rsplit_once(')')?.1.split_whitespace().nth(1)?.parse().ok()
}

#[cfg(target_os = "linux")]
pub(crate) fn started(pid: u32) -> Option<SystemTime> {
    const TICKS_PER_SEC: f64 = 100.0;
    let stat = std::fs::read_to_string(format!("/proc/{pid}/stat")).ok()?;
    let ticks: f64 = stat
        .rsplit_once(')')?
        .1
        .split_whitespace()
        .nth(19)?
        .parse()
        .ok()?;
    let uptime: f64 = std::fs::read_to_string("/proc/uptime")
        .ok()?
        .split_whitespace()
        .next()?
        .parse()
        .ok()?;
    let age = (uptime - ticks / TICKS_PER_SEC).max(0.0);
    SystemTime::now().checked_sub(std::time::Duration::from_secs_f64(age))
}

/// The processes whose exe file is `name`, read from the system's process
/// list; tasklist answers only when that list cannot be taken.
#[cfg(windows)]
pub(crate) fn named(name: &str) -> Vec<GameProcess> {
    win32::process_ids(name)
        .unwrap_or_else(|| tasklist(name))
        .into_iter()
        .map(|pid| GameProcess {
            pid,
            started: None,
            origin: Origin::default(),
        })
        .collect()
}

#[cfg(windows)]
fn tasklist(name: &str) -> Vec<u32> {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    let filter = format!("IMAGENAME eq {name}");
    let Ok(out) = Command::new("tasklist")
        .args(["/FI", &filter, "/FO", "CSV", "/NH"])
        .creation_flags(CREATE_NO_WINDOW)
        .output()
    else {
        return Vec::new();
    };
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|line| {
            let mut cols = line.split("\",\"").map(|c| c.trim_matches('"'));
            cols.next()?.eq_ignore_ascii_case(name).then_some(())?;
            cols.next()?.parse().ok()
        })
        .collect()
}

#[cfg(windows)]
mod win32 {
    use std::ffi::c_void;

    const TH32CS_SNAPPROCESS: u32 = 0x2;
    const INVALID_HANDLE_VALUE: *mut c_void = -1isize as *mut c_void;
    const MAX_PATH: usize = 260;

    #[repr(C)]
    struct ProcessEntry32W {
        size: u32,
        usage: u32,
        process_id: u32,
        default_heap_id: usize,
        module_id: u32,
        threads: u32,
        parent_process_id: u32,
        priority: i32,
        flags: u32,
        exe_file: [u16; MAX_PATH],
    }

    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn CreateToolhelp32Snapshot(flags: u32, pid: u32) -> *mut c_void;
        fn Process32FirstW(snapshot: *mut c_void, entry: *mut ProcessEntry32W) -> i32;
        fn Process32NextW(snapshot: *mut c_void, entry: *mut ProcessEntry32W) -> i32;
        fn CloseHandle(handle: *mut c_void) -> i32;
    }

    /// The pids of the processes whose exe file name is `name` in any case,
    /// or None when the process list could not be taken.
    pub fn process_ids(name: &str) -> Option<Vec<u32>> {
        // SAFETY: the snapshot handle is checked before use and closed once;
        // the entry is zeroed with its size set, as Process32FirstW requires,
        // and exe_file is read only up to its NUL inside the array.
        unsafe {
            let snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if snapshot == INVALID_HANDLE_VALUE || snapshot.is_null() {
                return None;
            }
            let mut entry: ProcessEntry32W = std::mem::zeroed();
            entry.size = std::mem::size_of::<ProcessEntry32W>() as u32;
            let mut out = Vec::new();
            let mut more = Process32FirstW(snapshot, &mut entry) != 0;
            while more {
                let len = entry.exe_file.iter().position(|&c| c == 0).unwrap_or(MAX_PATH);
                if String::from_utf16_lossy(&entry.exe_file[..len]).eq_ignore_ascii_case(name) {
                    out.push(entry.process_id);
                }
                more = Process32NextW(snapshot, &mut entry) != 0;
            }
            CloseHandle(snapshot);
            Some(out)
        }
    }
}

#[cfg(not(any(target_os = "linux", windows)))]
pub(crate) fn named(_: &str) -> Vec<GameProcess> {
    Vec::new()
}

#[cfg(target_os = "linux")]
fn origin(pid: u32) -> Origin {
    use std::io::BufRead;
    let proc = PathBuf::from(format!("/proc/{pid}"));
    let image = std::fs::File::open(proc.join("maps")).ok().and_then(|f| {
        std::io::BufReader::new(f)
            .lines()
            .map_while(Result::ok)
            .find_map(|l| mapped_exe(&l))
    });
    let command = std::fs::read(proc.join("cmdline")).ok().and_then(|raw| {
        let first = raw.split(|b| *b == 0).next()?;
        (!first.is_empty()).then(|| String::from_utf8_lossy(first).into_owned())
    });
    let prefix = std::fs::read(proc.join("environ"))
        .ok()
        .and_then(|raw| wine_prefix(&raw));
    Origin {
        image,
        command,
        prefix,
    }
}

/// The game's exe in a `/proc/<pid>/maps` line: Wine maps the exe's headers
/// from the file, under the path the kernel resolved when it was opened.
#[cfg(any(target_os = "linux", test))]
fn mapped_exe(line: &str) -> Option<String> {
    let path = &line[line.find('/')?..];
    exe_folder(path).map(|_| path.to_owned())
}

#[cfg(any(target_os = "linux", test))]
fn wine_prefix(environ: &[u8]) -> Option<PathBuf> {
    let var = |name: &str| {
        environ.split(|b| *b == 0).find_map(|kv| {
            let value = kv.strip_prefix(name.as_bytes())?.strip_prefix(b"=")?;
            (!value.is_empty()).then(|| PathBuf::from(String::from_utf8_lossy(value).into_owned()))
        })
    };
    var("WINEPREFIX").or_else(|| var("STEAM_COMPAT_DATA_PATH").map(|d| d.join("pfx")))
}

#[cfg(windows)]
fn origin(pid: u32) -> Origin {
    Origin {
        image: image_path(pid),
        ..Origin::default()
    }
}

#[cfg(not(any(target_os = "linux", windows)))]
fn origin(_: u32) -> Origin {
    Origin::default()
}

#[cfg(windows)]
fn image_path(pid: u32) -> Option<String> {
    use std::ffi::c_void;
    const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
        fn QueryFullProcessImageNameW(
            process: *mut c_void,
            flags: u32,
            name: *mut u16,
            size: *mut u32,
        ) -> i32;
        fn CloseHandle(handle: *mut c_void) -> i32;
    }
    let mut name = vec![0u16; 32_768];
    let mut len = name.len() as u32;
    // SAFETY: the handle is checked before use and closed once, and `len`
    // tells the call how many u16s `name` holds.
    let ok = unsafe {
        let process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
        if process.is_null() {
            return None;
        }
        let ok = QueryFullProcessImageNameW(process, 0, name.as_mut_ptr(), &mut len);
        CloseHandle(process);
        ok
    };
    (ok != 0).then(|| String::from_utf16_lossy(&name[..len as usize]))
}

pub fn stop_process(pid: u32) -> std::io::Result<()> {
    let pid = pid.to_string();
    if cfg!(windows) {
        Command::new("taskkill").args(["/PID", &pid]).status()?;
    } else {
        Command::new("kill").args(["-TERM", &pid]).status()?;
    }
    Ok(())
}

/// Ask the running games of the copy in `dir` to close, and return their
/// pids. A game that cannot be told to be that copy's is left running.
pub fn stop_game_in(dir: &Path) -> std::io::Result<Vec<u32>> {
    let games = games_in(dir);
    for g in &games {
        stop_process(g.pid)?;
    }
    Ok(games.into_iter().map(|g| g.pid).collect())
}

#[cfg(test)]
mod status_tests {
    use super::*;

    #[test]
    fn reports_the_live_session_of_a_running_game() {
        let root = std::env::temp_dir().join(format!("swofty-status-{}", std::process::id()));
        let dir = root.join("sessions/2026-10-20T14-22-33");
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(root.join("sessions/latest.txt"), format!("{}\n", dir.display())).unwrap();
        std::fs::write(dir.join("game_start"), "100.5\n").unwrap();
        let game = [GameProcess {
            pid: 48211,
            started: Some(UNIX_EPOCH + std::time::Duration::from_secs(100)),
            origin: Origin::default(),
        }];
        let status = status_of(&game, Some(&root));
        assert_eq!(
            status,
            GameStatus {
                running: true,
                pid: Some(48211),
                since: Some(100.0),
                session: Some("2026-10-20T14-22-33".into()),
            }
        );
        std::fs::write(dir.join("game_end"), "200\n").unwrap();
        assert_eq!(status_of(&game, Some(&root)).session, None);
        assert_eq!(status_of(&[], Some(&root)), GameStatus::default());
        assert_eq!(status_of(&game, None).session, None);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn picks_only_this_copys_games_the_session_neither_saw_nor_started() {
        let ea = Path::new(r"D:\EA Games\F1 25");
        let mut running = [11076, 15472, 9960, 4242, 5150].map(|pid| GameProcess {
            pid,
            started: None,
            origin: Origin {
                image: Some(r"D:\EA Games\F1 25\F1_25.exe".into()),
                ..Origin::default()
            },
        });
        running[1].origin = Origin::default();
        running[4].origin.image = Some(r"C:\Steam\steamapps\common\F1 25\F1_25.exe".into());
        let ours = |pid| pid == 9960;
        assert_eq!(started_elsewhere(&running, ea, &[4242], ours), vec![11076, 15472]);
        assert_eq!(
            started_elsewhere(&running[2..], ea, &[4242], ours),
            Vec::<u32>::new()
        );
        assert_eq!(started_elsewhere(&[], ea, &[], ours), Vec::<u32>::new());
    }
}

#[cfg(test)]
mod origin_tests {
    use super::*;

    fn seen(image: Option<&str>, command: Option<&str>, prefix: Option<&Path>) -> GameProcess {
        GameProcess {
            pid: 1,
            started: None,
            origin: Origin {
                image: image.map(str::to_owned),
                command: command.map(str::to_owned),
                prefix: prefix.map(Path::to_path_buf),
            },
        }
    }

    #[test]
    fn windows_image_paths_name_their_copy_whatever_the_case_and_slashes() {
        let steam = Path::new(r"c:\program files (x86)\steam\steamapps\common\F1 25");
        let ea = Path::new(r"D:\EA Games\F1 25\");
        let g = seen(
            Some(r"C:\Program Files (x86)\Steam\steamapps\common\F1 25\F1_25.exe"),
            None,
            None,
        );
        assert!(g.is_in(steam) && !g.elsewhere(steam));
        assert!(!g.is_in(ea) && g.elsewhere(ea));
        let g = seen(Some(r"\\?\D:/EA Games/F1 25/F1_25.EXE"), None, None);
        assert!(g.is_in(ea) && g.elsewhere(steam));
        let other = seen(Some(r"C:\Windows\explorer.exe"), None, None);
        assert!(!other.is_in(steam) && !other.elsewhere(steam));
    }

    #[test]
    fn a_game_that_tells_nothing_is_no_copys() {
        let g = seen(None, None, None);
        let dir = Path::new("/games/F1 25");
        assert!(!g.is_in(dir) && !g.elsewhere(dir));
        let g = seen(None, Some("F1_25.exe"), None);
        assert!(!g.is_in(dir) && !g.elsewhere(dir));
        let g = seen(None, Some(r"D:\F1 25\F1_25.exe"), None);
        assert!(
            !g.is_in(dir) && !g.elsewhere(dir),
            "a drive without a prefix maps nowhere"
        );
    }

    #[test]
    fn reads_the_exe_from_a_maps_line_and_the_prefix_from_the_environment() {
        let line = "140000000-140001000 r--p 00000000 103:02 21500751                        \
                    /mnt/gaming/EAApp/Games/F1 25/F1_25.exe";
        assert_eq!(
            mapped_exe(line).as_deref(),
            Some("/mnt/gaming/EAApp/Games/F1 25/F1_25.exe")
        );
        let lib = "7f0000000000-7f0000001000 r--p 00000000 103:02 1 /usr/lib/libc.so.6";
        assert_eq!(mapped_exe(lib), None);
        assert_eq!(
            mapped_exe("7ffe0000-7fff0000 rw-p 00000000 00:00 0 [stack]"),
            None
        );
        let env = b"HOME=/home/a\0WINEPREFIX=/mnt/gaming/EAApp/pfx/pfx/\0STEAM_COMPAT_DATA_PATH=/x\0";
        assert_eq!(
            wine_prefix(env),
            Some(PathBuf::from("/mnt/gaming/EAApp/pfx/pfx/"))
        );
        let env = b"STEAM_COMPAT_DATA_PATH=/steam/compatdata/3059520\0";
        assert_eq!(
            wine_prefix(env),
            Some(PathBuf::from("/steam/compatdata/3059520/pfx"))
        );
        assert_eq!(wine_prefix(b"WINEPREFIXES=/a\0"), None);
    }

    #[cfg(unix)]
    struct Fixture(PathBuf);

    #[cfg(unix)]
    impl Fixture {
        fn new(name: &str) -> Self {
            let dir = std::env::temp_dir().join(format!("swofty-origin-{name}-{}", std::process::id()));
            std::fs::remove_dir_all(&dir).ok();
            std::fs::create_dir_all(&dir).unwrap();
            Self(dir)
        }

        fn game(&self, rel: &str) -> PathBuf {
            let d = self.0.join(rel);
            std::fs::create_dir_all(&d).unwrap();
            std::fs::write(d.join(GAME_EXE), b"MZ").unwrap();
            d
        }

        fn windows(&self, rel: &str) -> String {
            format!(r"Z:{}\{rel}", self.0.display()).replace('/', r"\")
        }
    }

    #[cfg(unix)]
    impl Drop for Fixture {
        fn drop(&mut self) {
            std::fs::remove_dir_all(&self.0).ok();
        }
    }

    #[cfg(unix)]
    #[test]
    fn proton_z_paths_name_their_copy() {
        let f = Fixture::new("proton");
        let steam = f.game("steamapps/common/F1 25");
        let ea = f.game("EAApp/Games/F1 25");
        let g = seen(None, Some(&f.windows(r"steamapps\common\F1 25\F1_25.exe")), None);
        assert_eq!(g.origin.folders(), std::slice::from_ref(&steam));
        assert!(g.is_in(&steam) && g.elsewhere(&ea));
        let mapped = format!("{}/F1_25.exe", ea.display());
        let g = seen(Some(&mapped), Some(r"D:\F1 25\F1_25.exe"), None);
        assert!(g.is_in(&ea) && g.elsewhere(&steam));
    }

    #[cfg(unix)]
    #[test]
    fn ea_drive_letters_map_through_the_prefix() {
        let f = Fixture::new("drives");
        let steam = f.game("steamapps/common/F1 25");
        let ea = f.game("EAApp/Games/F1 25");
        let c = f.game("EAApp/pfx/pfx/drive_c/Games/F1 25");
        let prefix = f.0.join("EAApp/pfx/pfx");
        std::fs::create_dir_all(prefix.join("dosdevices")).unwrap();
        std::os::unix::fs::symlink(f.0.join("EAApp/Games"), prefix.join("dosdevices/d:")).unwrap();
        std::os::unix::fs::symlink("../drive_c", prefix.join("dosdevices/c:")).unwrap();
        std::os::unix::fs::symlink("/", prefix.join("dosdevices/z:")).unwrap();
        let g = seen(None, Some(r"D:\F1 25\F1_25.exe"), Some(&prefix));
        assert!(g.is_in(&ea) && g.elsewhere(&steam) && !g.is_in(&steam));
        let g = seen(None, Some(r"c:\Games\F1 25\F1_25.exe"), Some(&prefix));
        assert!(g.is_in(&c) && g.elsewhere(&ea));
        let g = seen(
            None,
            Some(&f.windows(r"steamapps\common\F1 25\F1_25.exe")),
            Some(&prefix),
        );
        assert!(g.is_in(&steam) && g.elsewhere(&ea));
        let g = seen(None, Some(r"E:\F1 25\F1_25.exe"), Some(&prefix));
        assert!(
            !g.is_in(&ea) && !g.elsewhere(&ea),
            "an unmapped drive tells nothing"
        );
    }

    #[cfg(unix)]
    #[test]
    fn the_game_link_folder_is_the_copy_it_points_at() {
        let f = Fixture::new("link");
        let steam = f.game("steamapps/common/F1 25");
        let ea = f.game("EAApp/Games/F1 25");
        std::fs::create_dir_all(f.0.join("data/state")).unwrap();
        std::os::unix::fs::symlink(&steam, f.0.join("data/state/game-link")).unwrap();
        let g = seen(None, Some(&f.windows(r"data\state\game-link\F1_25.exe")), None);
        assert!(g.is_in(&steam) && g.elsewhere(&ea));
    }

    #[cfg(unix)]
    #[test]
    fn the_overlay_target_stands_in_for_the_game_folder() {
        let f = Fixture::new("overlay-target");
        let steam = f.game("steamapps/common/F1 25");
        let ea = f.game("EAApp/Games/F1 25");
        let target = f.game("data/state/overlay/game");
        let g = seen(None, Some(&f.windows(r"data\state\overlay\game\F1_25.exe")), None);
        let folders = g.origin.folders();
        assert!(!in_folder(&folders, &steam, None) && !folders.is_empty());
        assert!(in_folder(&folders, &steam, Some(&target)));
        assert!(!in_folder(&folders, &ea, Some(&target.join("..").join("other"))));
    }
}

#[cfg(all(test, windows))]
mod windows_tests {
    #[test]
    fn finds_this_test_by_its_exe_name_in_any_case() {
        let exe = std::env::current_exe().unwrap();
        let name = exe.file_name().unwrap().to_str().unwrap().to_owned();
        let me = std::process::id();
        let pids = super::win32::process_ids(&name).expect("the process list");
        assert!(pids.contains(&me), "{name}: {pids:?} lacks {me}");
        let shouted = super::named(&name.to_uppercase());
        assert!(shouted.iter().any(|g| g.pid == me), "{shouted:?}");
        assert!(super::named("no-such-process-swofty.exe").is_empty());
    }
}

#[cfg(all(test, target_os = "linux"))]
mod tests {
    #[test]
    fn traces_ancestry() {
        let mut child = std::process::Command::new("sleep").arg("5").spawn().unwrap();
        assert!(super::descends_from(child.id(), std::process::id()));
        assert!(!super::descends_from(std::process::id(), child.id()));
        child.kill().ok();
        child.wait().ok();
    }

    #[test]
    fn reads_own_start_time() {
        let age = super::started(std::process::id())
            .unwrap()
            .elapsed()
            .unwrap_or_default();
        assert!(age.as_secs() < 3600, "{age:?}");
    }
}
