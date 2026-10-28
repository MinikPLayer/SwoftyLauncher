//! The Windows "Run this program as an administrator" setting on the game.
//!
//! Windows will not start an exe that is set to run as administrator from an
//! unelevated process (CreateProcess error 740), and the offline play session
//! launcher does not elevate the game, so a modded session cannot start while
//! the setting is on and the launcher runs unelevated. The setting is a
//! RUNASADMIN layer on the exe's path under `AppCompatFlags\Layers`, per user
//! (HKCU) or for all users (HKLM). This only reads it; the registry is never
//! changed.

use std::path::{Path, PathBuf};

use crate::GAME_EXE;

pub const LAYERS_KEY: &str = r"Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers";

/// The exes a modded session starts: the game, and the session launcher in
/// the anti-cheat launcher's slot.
pub const SESSION_EXES: &[&str] = &[GAME_EXE, "EAAntiCheat.GameServiceLauncher.exe"];

/// The programs that start F1 25, wherever they are installed. Windows hands
/// a program's compatibility layers on to the programs it starts.
pub const STARTERS: &[&str] = &[
    crate::ea_app::PROCESS,
    crate::ea_app::LAUNCHER_EXE,
    "swofty-launcher.exe",
];

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AdminLayer {
    pub all_users: bool,
    /// The file name of the exe the session starts.
    pub name: String,
    /// The exe path as the setting stores it.
    pub exe: String,
    /// The layers set on it, e.g. `~ RUNASADMIN`.
    pub layers: String,
}

impl AdminLayer {
    pub fn scope(&self) -> &'static str {
        if self.all_users {
            "for all users"
        } else {
            "for this user"
        }
    }
}

pub fn has_run_as_admin(layers: &str) -> bool {
    layers
        .split_whitespace()
        .any(|l| l.eq_ignore_ascii_case("RUNASADMIN"))
}

/// The string values `reg query <key>` lists, as (name, data).
pub fn parse_reg_query(out: &str) -> Vec<(String, String)> {
    out.lines()
        .filter_map(|line| {
            let rest = line.trim_end_matches('\r').strip_prefix("    ")?;
            ["    REG_SZ", "    REG_EXPAND_SZ"].iter().find_map(|ty| {
                let (name, data) = rest.split_once(ty)?;
                let data = data.strip_prefix("    ").or(data.is_empty().then_some(""))?;
                Some((name.to_owned(), data.to_owned()))
            })
        })
        .collect()
}

/// A REG_DWORD value `reg query <key>` lists, by name.
pub fn reg_dword(out: &str, name: &str) -> Option<u32> {
    out.lines().find_map(|line| {
        let rest = line.trim_end_matches('\r').strip_prefix("    ")?;
        let (value, data) = rest.split_once("    REG_DWORD    ")?;
        value.eq_ignore_ascii_case(name).then_some(())?;
        u32::from_str_radix(data.trim().strip_prefix("0x")?, 16).ok()
    })
}

/// The setting keeps the path as Explorer saw it, which need not be the
/// spelling the launcher found (case, separators, a linked library folder).
fn same_file(a: &Path, b: &Path) -> bool {
    let norm = |p: &Path| p.to_string_lossy().replace('/', "\\");
    norm(a).eq_ignore_ascii_case(&norm(b))
        || matches!(
            (std::fs::canonicalize(a), std::fs::canonicalize(b)),
            (Ok(x), Ok(y)) if x == y
        )
}

/// Whether `layers` sets any compatibility layer, not just the `~` marker.
pub fn has_layers(layers: &str) -> bool {
    layers.split_whitespace().any(|l| l != "~")
}

#[cfg(test)]
fn matching(listing: &str, all_users: bool, exe: &Path) -> Vec<AdminLayer> {
    matching_with(listing, all_users, exe, has_run_as_admin)
}

fn matching_with(listing: &str, all_users: bool, exe: &Path, keep: fn(&str) -> bool) -> Vec<AdminLayer> {
    let path = exe.to_string_lossy();
    let name = path.rsplit(['\\', '/']).next().unwrap_or(&path);
    parse_reg_query(listing)
        .into_iter()
        .filter(|(stored, data)| keep(data) && same_file(Path::new(stored), exe))
        .map(|(stored, layers)| AdminLayer {
            all_users,
            name: name.to_owned(),
            exe: stored,
            layers,
        })
        .collect()
}

/// Every run-as-administrator setting on `exe`, per user first.
pub fn admin_layers(exe: &Path) -> Vec<AdminLayer> {
    layer_settings(exe, has_run_as_admin)
}

/// Every compatibility setting on `exe` (the Compatibility tab of its
/// Properties, run as administrator included), per user first.
pub fn layers(exe: &Path) -> Vec<AdminLayer> {
    layer_settings(exe, has_layers)
}

fn layer_settings(exe: &Path, keep: fn(&str) -> bool) -> Vec<AdminLayer> {
    listings()
        .into_iter()
        .flat_map(|(listing, all_users)| matching_with(&listing, all_users, exe, keep))
        .collect()
}

/// The per-user and all-users `reg query` listings of [`LAYERS_KEY`].
fn listings() -> Vec<(String, bool)> {
    [("HKCU", false), ("HKLM", true)]
        .into_iter()
        .filter_map(|(hive, all_users)| {
            let key = format!(r"{hive}\{LAYERS_KEY}");
            Some((windows_tool("reg", &["query", &key, "/reg:64"])?, all_users))
        })
        .collect()
}

/// The compatibility settings on the programs that start the game in `game`:
/// [`STARTERS`], and the session launcher in the game folder.
pub fn starter_layers(game: &Path) -> Vec<AdminLayer> {
    starter_layers_in(&listings(), game)
}

/// [`starter_layers`] from the listings given, in [`STARTERS`] order with the
/// session launcher last, per user first.
pub fn starter_layers_in(listings: &[(String, bool)], game: &Path) -> Vec<AdminLayer> {
    let stub = game.join(SESSION_EXES[1]);
    let mut found: Vec<AdminLayer> = listings
        .iter()
        .flat_map(|(listing, all_users)| {
            parse_reg_query(listing)
                .into_iter()
                .filter(|(_, data)| has_layers(data))
                .filter_map(|(stored, layers)| {
                    let file = stored.rsplit(['\\', '/']).next().unwrap_or(&stored);
                    let name = STARTERS
                        .iter()
                        .find(|n| n.eq_ignore_ascii_case(file))
                        .map(|n| (*n).to_owned())
                        .or_else(|| {
                            same_file(Path::new(&stored), &stub).then(|| SESSION_EXES[1].to_owned())
                        })?;
                    Some(AdminLayer {
                        all_users: *all_users,
                        name,
                        exe: stored,
                        layers,
                    })
                })
                .collect::<Vec<_>>()
        })
        .collect();
    let rank = |l: &AdminLayer| {
        STARTERS
            .iter()
            .position(|n| *n == l.name)
            .unwrap_or(STARTERS.len())
    };
    found.sort_by_key(|l| (rank(l), l.all_users));
    found
}

/// True when this process runs with a high (or system) integrity level.
pub fn elevated() -> bool {
    windows_tool("whoami", &["/groups", "/fo", "csv", "/nh"])
        .is_some_and(|groups| groups.contains("S-1-16-12288") || groups.contains("S-1-16-16384"))
}

#[cfg(windows)]
pub(crate) fn windows_tool(program: &str, args: &[&str]) -> Option<String> {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    let out = std::process::Command::new(program)
        .args(args)
        .creation_flags(CREATE_NO_WINDOW)
        .output()
        .ok()?;
    Some(String::from_utf8_lossy(&out.stdout).into_owned())
}

#[cfg(not(windows))]
pub(crate) fn windows_tool(_: &str, _: &[&str]) -> Option<String> {
    None
}

/// A Windows tool's output, or why there is none: it could not start,
/// failed (its first error line), or did not finish `within` and was ended.
#[cfg(windows)]
pub(crate) fn windows_tool_within(
    program: &str,
    args: &[&str],
    within: std::time::Duration,
) -> std::result::Result<String, String> {
    use std::io::Read;
    use std::os::windows::process::CommandExt;
    use std::process::Stdio;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    let mut child = std::process::Command::new(program)
        .args(args)
        .creation_flags(CREATE_NO_WINDOW)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| format!("{program} did not start ({e})"))?;
    fn drain(pipe: Option<impl Read + Send + 'static>) -> std::thread::JoinHandle<Vec<u8>> {
        std::thread::spawn(move || {
            let mut data = Vec::new();
            if let Some(mut p) = pipe {
                p.read_to_end(&mut data).ok();
            }
            data
        })
    }
    let (out, err) = (drain(child.stdout.take()), drain(child.stderr.take()));
    let until = std::time::Instant::now() + within;
    let status = loop {
        match child.try_wait() {
            Ok(Some(status)) => break status,
            Ok(None) if std::time::Instant::now() < until => {
                std::thread::sleep(std::time::Duration::from_millis(20))
            }
            Ok(None) => {
                child.kill().ok();
                child.wait().ok();
                return Err(format!("{program} gave no answer within {} s", within.as_secs()));
            }
            Err(e) => return Err(format!("{program}: {e}")),
        }
    };
    let out = String::from_utf8_lossy(&out.join().unwrap_or_default()).into_owned();
    if status.success() {
        return Ok(out);
    }
    let err = String::from_utf8_lossy(&err.join().unwrap_or_default()).into_owned();
    let first = err
        .lines()
        .chain(out.lines())
        .map(str::trim)
        .find(|l| !l.is_empty());
    Err(match first {
        Some(line) => line.to_owned(),
        None => format!("{program} failed ({status})"),
    })
}

/// The settings on the exes a modded session in `game` starts.
pub fn session_admin_layers(game: &Path) -> Vec<AdminLayer> {
    SESSION_EXES
        .iter()
        .flat_map(|exe| admin_layers(&game.join(exe)))
        .collect()
}

/// Why a modded session in `game` cannot start, when one of its exes is set
/// to run as administrator and this process is not elevated.
pub fn elevation_blocker(game: &Path) -> Option<String> {
    let found = session_admin_layers(game);
    if found.is_empty() || elevated() {
        return None;
    }
    Some(explain(&found, game))
}

/// The folder under the game folder a modded session writes in first.
pub const FIRST_WRITE: &str = "2025_asset_groups/ui_package";

/// The first of the game folder and [`FIRST_WRITE`] in it that this process
/// can't create files in, such as a folder under Program Files that Windows
/// lets only administrators change.
pub fn unwritable_dir(game: &Path) -> Option<PathBuf> {
    [game.to_path_buf(), game.join(FIRST_WRITE)]
        .into_iter()
        .find(|dir| dir.is_dir() && !can_create_in(dir))
}

fn can_create_in(dir: &Path) -> bool {
    can_create_in_with(dir, virtual_store)
}

/// A file Windows virtualised into the VirtualStore is one the game never
/// sees, so it counts as one this process can't create.
fn can_create_in_with(dir: &Path, store: impl Fn(&Path) -> Option<PathBuf>) -> bool {
    let probe = dir.join(format!(".swofty-write-check-{}", std::process::id()));
    match std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&probe)
    {
        Ok(file) => {
            drop(file);
            let virtualised = store(&probe).is_some_and(|v| v.is_file());
            std::fs::remove_file(&probe).ok();
            !virtualised
        }
        Err(e) => !matches!(
            e.kind(),
            std::io::ErrorKind::PermissionDenied | std::io::ErrorKind::ReadOnlyFilesystem
        ),
    }
}

/// Where Windows keeps this user's copy of `path` when it virtualises a write
/// to a folder only administrators can change, such as Program Files:
/// `%LOCALAPPDATA%\VirtualStore` with the path's drive left out.
pub fn virtual_store(path: &Path) -> Option<PathBuf> {
    if !cfg!(windows) {
        return None;
    }
    virtual_store_in(Path::new(&std::env::var_os("LOCALAPPDATA")?), path)
}

fn virtual_store_in(local: &Path, path: &Path) -> Option<PathBuf> {
    let p = path.to_string_lossy();
    let b = p.as_bytes();
    (b.len() > 3 && b[0].is_ascii_alphabetic() && b[1] == b':' && matches!(b[2], b'\\' | b'/'))
        .then(|| local.join("VirtualStore").join(&p[3..]))
}

/// The folder a modded session in `game` can't write in, when this process
/// isn't elevated; None when it can, or when it runs as administrator.
pub fn write_blocker(game: &Path) -> Option<PathBuf> {
    unwritable_dir(game).filter(|_| !elevated())
}

/// Why a modded session can't put its files in `game`, and what to do.
pub fn write_blocked_text(game: &Path) -> String {
    write_blocked_text_for(game, cfg!(windows))
}

/// [`write_blocked_text`] as Windows, or another system, puts it.
pub fn write_blocked_text_for(game: &Path, windows: bool) -> String {
    if windows {
        format!(
            "F1 25 is installed in {}, a folder Windows lets only administrators change, so Swofty \
             can't add your mods there. Run Swofty Launcher as administrator, or move F1 25 to a \
             folder outside Program Files",
            game.display()
        )
    } else {
        format!(
            "Swofty can't write in the F1 25 folder {}, so it can't add your mods there. Give your \
             user write access to it",
            game.display()
        )
    }
}

pub fn explain(found: &[AdminLayer], game: &Path) -> String {
    let mut names: Vec<&str> = found.iter().map(|l| l.name.as_str()).collect();
    names.dedup();
    let (is, it) = if names.len() > 1 {
        ("are", "them")
    } else {
        ("is", "it")
    };
    let names = names.join(" and ");
    let all_users = found.iter().any(|l| l.all_users);
    format!(
        "{names} {is} set to run as administrator ({}), and Windows will not start {it} for a modded \
         session while Swofty Launcher runs unelevated (error 740). Right-click {names} in {}, \
         open Properties > Compatibility and untick \"Run this program as an administrator\"{}, \
         then press Play again, or run Swofty Launcher as administrator",
        if all_users {
            "for all users"
        } else {
            "for this user"
        },
        game.display(),
        if all_users {
            " under \"Change settings for all users\""
        } else {
            ""
        },
    )
}

#[cfg(windows)]
mod shell {
    use std::ffi::c_void;
    use std::os::windows::ffi::OsStrExt;

    #[repr(C)]
    struct ShellExecuteInfoW {
        size: u32,
        mask: u32,
        hwnd: *mut c_void,
        verb: *const u16,
        file: *const u16,
        parameters: *const u16,
        directory: *const u16,
        show: i32,
        instance: *mut c_void,
        id_list: *mut c_void,
        class: *const u16,
        key_class: *mut c_void,
        hot_key: u32,
        icon_or_monitor: *mut c_void,
        process: *mut c_void,
    }

    const SEE_MASK_NOCLOSEPROCESS: u32 = 0x40;
    const SEE_MASK_NOASYNC: u32 = 0x100;
    const SW_SHOWNORMAL: i32 = 1;
    const ERROR_CANCELLED: i32 = 1223;

    #[link(name = "shell32")]
    unsafe extern "system" {
        fn ShellExecuteExW(info: *mut ShellExecuteInfoW) -> i32;
    }

    const COINIT_APARTMENTTHREADED: u32 = 0x2;
    const COINIT_DISABLE_OLE1DDE: u32 = 0x4;

    #[link(name = "ole32")]
    unsafe extern "system" {
        fn CoInitializeEx(reserved: *mut c_void, coinit: u32) -> i32;
        fn CoUninitialize();
    }

    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn CloseHandle(handle: *mut c_void) -> i32;
        fn GetProcessId(process: *mut c_void) -> u32;
        fn WaitForSingleObject(handle: *mut c_void, ms: u32) -> u32;
        fn GetExitCodeProcess(process: *mut c_void, code: *mut u32) -> i32;
    }

    const WAIT_OBJECT_0: u32 = 0;

    pub struct Process(*mut c_void);

    // SAFETY: a process handle can be waited on and closed from any thread.
    unsafe impl Send for Process {}

    impl Process {
        pub fn pid(&self) -> u32 {
            // SAFETY: the handle is open until drop.
            unsafe { GetProcessId(self.0) }
        }

        pub fn exited_within(&self, wait: std::time::Duration) -> Option<u32> {
            let ms = u32::try_from(wait.as_millis()).unwrap_or(u32::MAX);
            // SAFETY: the handle is open until drop, and `code` outlives the call.
            unsafe {
                if WaitForSingleObject(self.0, ms) != WAIT_OBJECT_0 {
                    return None;
                }
                let mut code = 0;
                (GetExitCodeProcess(self.0, &mut code) != 0).then_some(code)
            }
        }
    }

    impl Drop for Process {
        fn drop(&mut self) {
            // SAFETY: the handle came from ShellExecuteExW with NOCLOSEPROCESS and is closed once.
            unsafe { CloseHandle(self.0) };
        }
    }

    fn wide(s: &std::ffi::OsStr) -> Vec<u16> {
        s.encode_wide().chain(std::iter::once(0)).collect()
    }

    fn quote(arg: &str) -> String {
        if !arg.is_empty() && !arg.contains([' ', '\t', '"']) {
            return arg.to_owned();
        }
        let mut inner = arg.replace('"', "\\\"");
        let trailing = inner.chars().rev().take_while(|&c| c == '\\').count();
        inner.push_str(&"\\".repeat(trailing));
        format!("\"{inner}\"")
    }

    pub fn relaunch_elevated() -> Result<Option<Process>, String> {
        let exe = std::env::current_exe().map_err(|e| e.to_string())?;
        let args: Vec<String> = std::env::args().skip(1).map(|a| quote(&a)).collect();
        let (verb, file) = (wide("runas".as_ref()), wide(exe.as_os_str()));
        let parameters = wide(args.join(" ").as_ref());
        let directory = std::env::current_dir().ok().map(|d| wide(d.as_os_str()));
        let mut info = ShellExecuteInfoW {
            size: std::mem::size_of::<ShellExecuteInfoW>() as u32,
            mask: SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC,
            hwnd: std::ptr::null_mut(),
            verb: verb.as_ptr(),
            file: file.as_ptr(),
            parameters: parameters.as_ptr(),
            directory: directory.as_ref().map_or(std::ptr::null(), |d| d.as_ptr()),
            show: SW_SHOWNORMAL,
            instance: std::ptr::null_mut(),
            id_list: std::ptr::null_mut(),
            class: std::ptr::null(),
            key_class: std::ptr::null_mut(),
            hot_key: 0,
            icon_or_monitor: std::ptr::null_mut(),
            process: std::ptr::null_mut(),
        };
        // SAFETY: COM is initialised for this thread only and uninitialised after the call when it succeeded,
        // and every string the struct points at outlives the call.
        let ok = unsafe {
            let com = CoInitializeEx(
                std::ptr::null_mut(),
                COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE,
            ) >= 0;
            let ok = ShellExecuteExW(&mut info) != 0;
            if com {
                CoUninitialize();
            }
            ok
        };
        if !ok {
            let e = std::io::Error::last_os_error();
            return Err(if e.raw_os_error() == Some(ERROR_CANCELLED) {
                "the administrator prompt was declined".to_owned()
            } else {
                e.to_string()
            });
        }
        Ok((!info.process.is_null()).then_some(Process(info.process)))
    }

    #[cfg(test)]
    #[test]
    fn quotes_arguments_with_spaces() {
        assert_eq!(quote("run"), "run");
        assert_eq!(quote("C:\\Program Files\\F1 25"), "\"C:\\Program Files\\F1 25\"");
        assert_eq!(quote(""), "\"\"");
        assert_eq!(quote("C:\\My Games\\"), "\"C:\\My Games\\\\\"");
    }
}

/// The copy of this program [`relaunch_elevated`] started as administrator.
pub struct Elevated {
    #[cfg(windows)]
    process: Option<shell::Process>,
}

impl Elevated {
    pub fn pid(&self) -> Option<u32> {
        #[cfg(windows)]
        return self.process.as_ref().map(shell::Process::pid);
        #[cfg(not(windows))]
        None
    }

    /// Its exit code, when it closed within `wait`.
    pub fn exited_within(&self, wait: std::time::Duration) -> Option<u32> {
        #[cfg(windows)]
        return self.process.as_ref()?.exited_within(wait);
        #[cfg(not(windows))]
        {
            let _ = wait;
            None
        }
    }
}

/// Starts this program again as administrator, with the same arguments,
/// through the Windows elevation prompt. Ok with the new process once it has
/// started; the caller waits a moment before it exits.
pub fn relaunch_elevated() -> Result<Elevated, String> {
    #[cfg(windows)]
    return shell::relaunch_elevated().map(|process| Elevated { process });
    #[cfg(not(windows))]
    Err("restarting as administrator is only for Windows".to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;

    const LISTING: &str = concat!(
        "\r\n",
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers\r\n",
        "    C:\\Program Files (x86)\\Steam\\steamapps\\common\\F1 25\\F1_25.exe    REG_SZ    ~ RUNASADMIN\r\n",
        "    C:\\Games\\Other\\other.exe    REG_SZ    ~ HIGHDPIAWARE\r\n",
        "    C:\\Games\\Tool\\tool.exe    REG_EXPAND_SZ    RUNASADMIN WIN7RTM\r\n",
        "    C:\\Games\\Empty\\empty.exe    REG_SZ    \r\n",
        "    C:\\Games\\Flag\\flag.exe    REG_DWORD    0x1\r\n",
        "\r\n",
    );

    #[test]
    fn parses_reg_query_listings() {
        let values = parse_reg_query(LISTING);
        assert_eq!(
            values,
            [
                (
                    r"C:\Program Files (x86)\Steam\steamapps\common\F1 25\F1_25.exe",
                    "~ RUNASADMIN"
                ),
                (r"C:\Games\Other\other.exe", "~ HIGHDPIAWARE"),
                (r"C:\Games\Tool\tool.exe", "RUNASADMIN WIN7RTM"),
                (r"C:\Games\Empty\empty.exe", ""),
            ]
            .map(|(n, d)| (n.to_owned(), d.to_owned()))
        );
        assert!(
            parse_reg_query("ERROR: The system was unable to find the specified registry key or value.\r\n")
                .is_empty()
        );
    }

    #[test]
    fn reads_dword_values() {
        let listing = "\r\nHKEY_CURRENT_USER\\Software\\Valve\\Steam\\ActiveProcess\r\n    \
                       pid    REG_DWORD    0x4c8\r\n    ActiveUser    REG_DWORD    0x0\r\n    \
                       Universe    REG_SZ    Public\r\n";
        assert_eq!(reg_dword(listing, "pid"), Some(0x4c8));
        assert_eq!(reg_dword(listing, "activeuser"), Some(0));
        assert_eq!(reg_dword(listing, "Universe"), None);
        assert_eq!(reg_dword(listing, "SteamPID"), None);
    }

    #[test]
    fn reads_the_runasadmin_layer() {
        assert!(has_run_as_admin("~ RUNASADMIN"));
        assert!(has_run_as_admin("RUNASADMIN HIGHDPIAWARE"));
        assert!(has_run_as_admin("~ HIGHDPIAWARE runasadmin"));
        assert!(!has_run_as_admin("~ HIGHDPIAWARE"));
        assert!(!has_run_as_admin("~ RUNASINVOKER"));
        assert!(!has_run_as_admin("~ NORUNASADMIN"));
        assert!(!has_run_as_admin(""));
    }

    #[test]
    fn reads_any_compatibility_layer() {
        assert!(has_layers("~ HIGHDPIAWARE"));
        assert!(has_layers("WIN7RTM"));
        assert!(!has_layers("~"));
        assert!(!has_layers(""));
        let other = matching_with(LISTING, false, Path::new(r"C:\Games\Other\other.exe"), has_layers);
        assert_eq!(other.len(), 1);
        assert_eq!(other[0].layers, "~ HIGHDPIAWARE");
        assert!(matching(LISTING, false, Path::new(r"C:\Games\Other\other.exe")).is_empty());
        assert!(matching_with(LISTING, false, Path::new(r"C:\Games\Empty\empty.exe"), has_layers).is_empty());
    }

    #[test]
    fn finds_the_layers_on_the_programs_that_start_the_game() {
        let game = Path::new(r"D:\Games\F1 25");
        let user = concat!(
            "\r\n",
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers\r\n",
            "    C:\\Tools\\Swofty Launcher\\swofty-launcher.exe    REG_SZ    ~ WIN8RTM\r\n",
            "    D:\\Games\\F1 25\\F1_25.exe    REG_SZ    ~ HIGHDPIAWARE\r\n",
            "    D:\\Battlefield\\EAAntiCheat.GameServiceLauncher.exe    REG_SZ    ~ RUNASADMIN\r\n",
            "    d:\\games\\f1 25\\eaanticheat.gameservicelauncher.exe    REG_SZ    WIN7RTM\r\n",
            "    C:\\Other\\EADesktop.exe.bak    REG_SZ    ~ RUNASADMIN\r\n",
            "    C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop\\EALauncher.exe    REG_SZ    ~\r\n",
        );
        let machine = concat!(
            "\r\n",
            "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers\r\n",
            "    C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop\\eadesktop.exe    REG_SZ    ~ RUNASADMIN\r\n",
        );
        let found = starter_layers_in(&[(user.into(), false), (machine.into(), true)], game);
        let seen: Vec<(&str, &str, bool)> = found
            .iter()
            .map(|l| (l.name.as_str(), l.layers.as_str(), l.all_users))
            .collect();
        assert_eq!(
            seen,
            [
                ("EADesktop.exe", "~ RUNASADMIN", true),
                ("swofty-launcher.exe", "~ WIN8RTM", false),
                ("EAAntiCheat.GameServiceLauncher.exe", "WIN7RTM", false),
            ]
        );
        assert_eq!(
            found[0].exe,
            r"C:\Program Files\Electronic Arts\EA Desktop\EA Desktop\eadesktop.exe"
        );
        assert!(starter_layers_in(&[(LISTING.into(), false)], game).is_empty());
        assert!(starter_layers_in(&[], game).is_empty());
    }

    #[test]
    fn matches_the_game_however_the_path_is_spelled() {
        let game = Path::new(r"c:\program files (x86)\steam\steamapps\common\F1 25");
        let found = matching(LISTING, false, &game.join(GAME_EXE));
        assert_eq!(found.len(), 1, "{found:?}");
        assert_eq!(found[0].name, GAME_EXE);
        assert_eq!(found[0].layers, "~ RUNASADMIN");
        assert!(!found[0].all_users);
        assert!(matching(LISTING, false, Path::new(r"C:\Games\Other\other.exe")).is_empty());
        let slashes = Path::new("C:/Program Files (x86)/Steam/steamapps/common/F1 25/F1_25.exe");
        assert_eq!(matching(LISTING, true, slashes).len(), 1);
    }

    #[cfg(unix)]
    #[test]
    fn matches_the_game_through_a_linked_folder() {
        let d = std::env::temp_dir().join(format!("swofty-compat-{}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(d.join("F1 25")).unwrap();
        std::fs::write(d.join("F1 25").join(GAME_EXE), "MZ").unwrap();
        std::os::unix::fs::symlink(d.join("F1 25"), d.join("library")).unwrap();
        let linked = d.join("library").join(GAME_EXE);
        let listing = format!("    {}    REG_SZ    ~ RUNASADMIN\n", linked.display());
        let found = matching(&listing, false, &d.join("F1 25").join(GAME_EXE));
        assert_eq!(found.len(), 1, "{found:?}");
        assert_eq!(found[0].exe, linked.display().to_string());
        std::fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn says_what_to_untick() {
        let game = Path::new(r"C:\Games\F1 25");
        let user = AdminLayer {
            all_users: false,
            name: GAME_EXE.into(),
            exe: r"C:\Games\F1 25\F1_25.exe".into(),
            layers: "~ RUNASADMIN".into(),
        };
        let text = explain(std::slice::from_ref(&user), game);
        assert!(
            text.starts_with("F1_25.exe is set to run as administrator (for this user)"),
            "{text}"
        );
        assert!(
            text.contains(r"Right-click F1_25.exe in C:\Games\F1 25"),
            "{text}"
        );
        assert!(
            text.contains("untick \"Run this program as an administrator\", then press Play"),
            "{text}"
        );
        assert!(
            text.ends_with("or run Swofty Launcher as administrator"),
            "{text}"
        );
        let all = AdminLayer {
            all_users: true,
            ..user.clone()
        };
        let text = explain(&[user, all.clone()], game);
        assert!(
            text.starts_with("F1_25.exe is set to run as administrator (for all users)"),
            "{text}"
        );
        assert!(text.contains("under \"Change settings for all users\""), "{text}");
        let both = AdminLayer {
            name: "EAAntiCheat.GameServiceLauncher.exe".into(),
            exe: r"C:\Games\F1 25\EAAntiCheat.GameServiceLauncher.exe".into(),
            ..all.clone()
        };
        let text = explain(&[all, both], game);
        assert!(
            text.starts_with("F1_25.exe and EAAntiCheat.GameServiceLauncher.exe are set to run"),
            "{text}"
        );
        assert!(text.contains("will not start them"), "{text}");
    }

    #[cfg(not(windows))]
    #[test]
    #[cfg(unix)]
    fn finds_a_game_folder_this_process_cannot_write_in() {
        use std::os::unix::fs::PermissionsExt;
        let game = std::env::temp_dir().join(format!("swofty-unwritable-{}", std::process::id()));
        std::fs::remove_dir_all(&game).ok();
        let ui = game.join(FIRST_WRITE);
        std::fs::create_dir_all(&ui).unwrap();
        assert_eq!(unwritable_dir(&game), None);
        assert_eq!(write_blocker(&game), None);
        let lock = |dir: &Path, mode: u32| {
            std::fs::set_permissions(dir, std::fs::Permissions::from_mode(mode)).unwrap()
        };
        lock(&ui, 0o555);
        let root_can_write = can_create_in(&ui);
        if !root_can_write {
            assert_eq!(unwritable_dir(&game), Some(ui.clone()));
            assert_eq!(write_blocker(&game), Some(ui.clone()));
            lock(&game, 0o555);
            assert_eq!(unwritable_dir(&game), Some(game.clone()));
            lock(&game, 0o755);
        }
        lock(&ui, 0o755);
        assert_eq!(unwritable_dir(&game), None);
        assert_eq!(
            std::fs::read_dir(&ui).unwrap().count(),
            0,
            "the check leaves nothing behind"
        );
        assert_eq!(unwritable_dir(&game.join("missing")), None);
        std::fs::remove_dir_all(&game).ok();
    }

    #[test]
    fn a_write_windows_virtualised_is_one_it_cannot_make() {
        let dir = std::env::temp_dir().join(format!("swofty-virtualised-{}", std::process::id()));
        let store = dir.join("VirtualStore");
        std::fs::create_dir_all(&store).unwrap();
        assert!(can_create_in_with(&dir, |_| None));
        let shadow = |probe: &Path| {
            let copy = store.join(probe.file_name()?);
            std::fs::write(&copy, b"").ok()?;
            Some(copy)
        };
        assert!(!can_create_in_with(&dir, shadow));
        assert!(
            !dir.join(format!(".swofty-write-check-{}", std::process::id()))
                .exists()
        );
        assert_eq!(
            virtual_store_in(
                Path::new(r"C:\Users\id\AppData\Local"),
                Path::new(r"C:\Program Files\EA Games\F1 25")
            )
            .map(|p| p.to_string_lossy().replace('/', "\\")),
            Some(r"C:\Users\id\AppData\Local\VirtualStore\Program Files\EA Games\F1 25".to_owned())
        );
        assert_eq!(
            virtual_store_in(Path::new("/local"), Path::new("/games/F1 25")),
            None
        );
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn names_the_folder_and_the_fix() {
        let text = write_blocked_text(Path::new("/games/F1 25"));
        assert!(text.contains("/games/F1 25"), "{text}");
        if cfg!(windows) {
            assert!(text.contains("Run Swofty Launcher as administrator"), "{text}");
        } else {
            assert!(text.contains("write access"), "{text}");
        }
    }

    #[test]
    fn never_blocks_off_windows() {
        assert!(elevation_blocker(Path::new("/games/F1 25")).is_none());
    }
}
