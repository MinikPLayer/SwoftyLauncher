//! Loader files an antivirus took away. Windows Defender and other scanners
//! delete, quarantine or block version.dll and the core DLLs, often a moment
//! after they are written, and F1 25 then runs with no mods at all. The
//! session checks the placed DLLs before the game starts and watches the
//! game folder's loader.log after it starts; what it finds is recorded here
//! for the launcher to show and for Send Logs. Missing files before the game
//! starts refuse the launch: the session rolls back and the game never runs.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};

use crate::baseline::sha256_file;
use crate::builtin::{CORE_DLL, LOADER_ENV};
use crate::install::PROXY;
use crate::placed::Placed;
use crate::session::LogMark;
use crate::{Result, io_err, read_json, write_json};

pub const ERROR_VIRUS_INFECTED: i32 = 225;
pub const ERROR_VIRUS_DELETED: i32 = 226;
/// The exit code of a session that refused to start the game over missing
/// loader files: clear of the 0/1 the CLI uses and of the game's own codes.
pub const REFUSED_EXIT: u8 = 225;
/// The loader's first line once version.dll is running in the game.
pub const ATTACHED: &str = "(version.dll proxy) attached";
/// The tail of the attach line that survives when a concurrent writer
/// overwrites the start of it.
const ATTACHED_TAIL: &str = "proxy) attached";
/// The offline play stub's line when it has the game image and resumes it.
const PATCHING: &str = "game image at ";
/// The loader's line in its fallback log naming the game folder's log it
/// could not open.
const REFUSED: &str = " could not be opened (error ";
/// A second look at the placed DLLs this long after they were written: a
/// scanner often removes a file only once it has been closed.
pub const RECHECK_AFTER: Duration = Duration::from_secs(2);
/// How long after the stub's patching line the loader may take to attach.
pub const ATTACH_WINDOW: Duration = Duration::from_secs(45);
/// The window when no stub line shows up (a launch without offline play).
const ATTACH_WINDOW_NO_STUB: Duration = Duration::from_secs(90);
/// A game that exits sooner than this without the loader failed to start,
/// which the crash report covers.
const SHORTEST_RUN: Duration = Duration::from_secs(10);
const POLL: Duration = Duration::from_secs(1);

pub fn is_virus_code(code: i32) -> bool {
    matches!(code, ERROR_VIRUS_INFECTED | ERROR_VIRUS_DELETED)
}

/// Windows refuses to copy or open a file it detected as a threat with these
/// two codes; elsewhere the numbers mean nothing.
pub fn is_virus_error(e: &std::io::Error) -> bool {
    cfg!(windows) && e.raw_os_error().is_some_and(is_virus_code)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Stage {
    BeforeLaunch,
    InGame,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Report {
    pub at: f64,
    pub stage: Stage,
    /// Paths relative to the game folder, or to the launcher folder for a
    /// loader file missing from the install itself.
    pub files: Vec<String>,
    /// In game: version.dll was gone from the game folder when checked.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub removed: bool,
    pub game: PathBuf,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub launcher: Option<PathBuf>,
    /// In game: what the session found keeping version.dll out of the game.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cause: Option<Box<Cause>>,
}

/// What kept version.dll from running in the game, from what the session
/// found once the loader missed it (see `loader_check`).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Cause {
    /// Smart App Control blocked it (`seen`: a Code Integrity event says
    /// so), or is on and nothing else explains the miss.
    SmartAppControl { seen: bool },
    /// An App Control (WDAC) policy other than Smart App Control blocked it.
    Policy { name: Option<String> },
    /// F1_25.exe runs with Code integrity guard: Microsoft-signed DLLs only.
    SignedOnly,
    /// Microsoft Defender blocked it: a detection or an attack surface
    /// reduction rule.
    Defender,
    /// The registry lists version.dll as a KnownDLL.
    KnownDll,
    /// The game holds a version.dll from another folder and not the game
    /// folder's (`prefer_system32`: its exploit protection asks for that).
    OtherCopy { path: String, prefer_system32: bool },
    /// The game holds another version.dll, and F1_25.exe has Exploit
    /// protection program settings.
    ExploitProtection { path: String },
    /// The game holds another version.dll, and compatibility settings are on
    /// for F1_25.exe or Windows loaded its compatibility shims into it.
    /// `from`: the program that started the game with them.
    CompatLayer {
        path: String,
        layers: Vec<String>,
        shims: Vec<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        from: Option<Carrier>,
    },
    /// The game holds another version.dll, and an antivirus other than
    /// Microsoft Defender is on.
    Antivirus { path: String, names: Vec<String> },
    /// The game holds another version.dll and nothing the session can see
    /// explains it; `hooks` are the programs hooked into the game.
    Unexplained { path: String, hooks: Vec<String> },
    /// Windows denied the session a look inside the game process, and no
    /// block event or other antivirus explains the miss.
    Unreadable,
}

/// A program that hands compatibility layers on to the game it starts.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Carrier {
    /// Its file name, such as EADesktop.exe.
    pub name: String,
    /// Its path, as the setting stores it or as it runs.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub exe: Option<String>,
    /// The settings on it, as `<layers> (for this user|for all users)`;
    /// empty when it runs with shims and has no setting of its own.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub layers: Vec<String>,
}

impl Carrier {
    fn ea_app(&self) -> bool {
        [crate::ea_app::PROCESS, crate::ea_app::LAUNCHER_EXE]
            .iter()
            .any(|n| n.eq_ignore_ascii_case(&self.name))
    }

    fn launcher(&self) -> bool {
        self.name.eq_ignore_ascii_case("swofty-launcher.exe")
    }
}

impl Cause {
    pub fn what(&self) -> String {
        match self {
            Self::SmartAppControl { seen: true } => format!("Smart App Control blocked {PROXY}"),
            Self::SmartAppControl { seen: false } => {
                format!("Smart App Control is on and most likely blocked {PROXY}")
            }
            Self::Policy { name } => format!(
                "a Windows App Control policy{} blocked {PROXY}",
                name.as_ref().map(|n| format!(" ({n})")).unwrap_or_default()
            ),
            Self::SignedOnly => format!(
                "Exploit protection lets F1_25.exe load only Microsoft-signed DLLs, so Windows refused {PROXY}"
            ),
            Self::Defender => format!("Microsoft Defender blocked {PROXY}"),
            Self::KnownDll => format!(
                "{PROXY} is listed under KnownDLLs, so Windows loads its own copy instead of the game folder's"
            ),
            Self::OtherCopy {
                path,
                prefer_system32: true,
            } => format!(
                "the game loaded {path} instead of the game folder's {PROXY}: Exploit protection makes F1_25.exe \
                 prefer System32 DLLs"
            ),
            Self::OtherCopy { path, .. } => format!(
                "the game loaded {path} instead of the game folder's {PROXY}. Security software on this PC most \
                 likely redirected it"
            ),
            Self::ExploitProtection { path } => format!(
                "the game loaded {path} instead of the game folder's {PROXY}, and F1_25.exe has its own Exploit \
                 protection settings in Windows Security, the most likely reason"
            ),
            Self::CompatLayer {
                path,
                layers,
                shims,
                from,
            } => {
                let why = match from {
                    _ if !layers.is_empty() => format!(
                        "compatibility settings are on for F1_25.exe ({})",
                        layers.join(", ")
                    ),
                    Some(c) if c.layers.is_empty() => format!(
                        "Windows applied compatibility shims to it ({}), passed on from {}, which runs with them too",
                        shims.join(", "),
                        c.name
                    ),
                    Some(c) => format!(
                        "Windows applied compatibility shims to it ({}), passed on from the compatibility settings \
                         on {} ({})",
                        shims.join(", "),
                        c.name,
                        c.layers.join(", ")
                    ),
                    None => format!("Windows applied compatibility shims to it ({})", shims.join(", ")),
                };
                format!(
                    "the game loaded {path} instead of the game folder's {PROXY}, and {why}, the most likely reason"
                )
            }
            Self::Antivirus { path, names } => format!(
                "the game loaded {path} instead of the game folder's {PROXY}. {}, the antivirus on this PC, most \
                 likely redirected it",
                names.join(" and ")
            ),
            Self::Unexplained { path, .. } => format!(
                "the game loaded {path} instead of the game folder's {PROXY}, and Swofty Launcher couldn't tell why"
            ),
            Self::Unreadable => format!(
                "Windows didn't let Swofty Launcher look inside the game (access denied), so it can't tell what kept \
                 {PROXY} out. Windows Security and Smart App Control show no block"
            ),
        }
    }

    /// Whether the session found nothing that explains the miss, so the
    /// player is pointed at Send Logs and the support Discord.
    pub fn unexplained(&self) -> bool {
        matches!(self, Self::Unexplained { .. } | Self::Unreadable)
    }

    /// The fix, when it is not the antivirus one.
    fn fix(&self, game: &Path) -> Option<String> {
        Some(match self {
            Self::SmartAppControl { .. } => "To fix it, close F1 25, turn off Smart App Control in Windows \
                 Security > App & browser control > Smart App Control settings, then launch again."
                .to_owned(),
            Self::Policy { .. } => format!(
                "To fix it, close F1 25, ask whoever manages this PC to allow {PROXY} in the game folder ({}), \
                 then launch again.",
                game.display()
            ),
            Self::SignedOnly
            | Self::OtherCopy {
                prefer_system32: true,
                ..
            } => "To fix it, close F1 25, remove the F1_25.exe settings in Windows Security > App & browser \
                 control > Exploit protection > Program settings, then launch again."
                .to_owned(),
            Self::KnownDll => format!(
                "To fix it, close F1 25, remove {PROXY} from HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\\
                 Session Manager\\KnownDLLs in the registry, restart Windows, then launch again."
            ),
            Self::OtherCopy { .. } => "To fix it, close F1 25, turn off your antivirus's DLL or behaviour \
                 protection for F1_25.exe (a folder exclusion is often not enough), then launch again."
                .to_owned(),
            Self::ExploitProtection { .. } => "To fix it, close F1 25, open Windows Security > App & browser \
                 control > Exploit protection settings > Program settings, remove every F1_25.exe entry, restart \
                 the PC, then launch again."
                .to_owned(),
            Self::CompatLayer { layers, from, .. } => {
                const UNTICK: &str = "open Properties > Compatibility and untick everything, then select Change \
                     settings for all users and untick everything there too";
                const QUIT_EA: &str = "fully quit the EA app (right-click its icon in the system tray > Exit)";
                let at = |c: &Carrier| c.exe.as_ref().map(|e| format!(" ({e})")).unwrap_or_default();
                match from {
                    _ if !layers.is_empty() => format!(
                        "To fix it, close F1 25, right-click F1_25.exe in the game folder ({}), {UNTICK}, then \
                         launch again.",
                        game.display()
                    ),
                    Some(c) if c.layers.is_empty() => format!(
                        "To fix it, close F1 25, {QUIT_EA}, then press Play in Swofty Launcher without starting \
                         the EA app first. If mods still don't load, right-click {}{}, {UNTICK}.",
                        c.name,
                        at(c)
                    ),
                    Some(c) if c.launcher() => format!(
                        "To fix it, close F1 25 and Swofty Launcher, {QUIT_EA}, right-click {}{}, {UNTICK}, then \
                         start Swofty Launcher and launch again.",
                        c.name,
                        at(c)
                    ),
                    Some(c) if c.ea_app() => format!(
                        "To fix it, close F1 25, {QUIT_EA}, right-click {}{}, {UNTICK}, then launch again.",
                        c.name,
                        at(c)
                    ),
                    Some(c) => format!(
                        "To fix it, close F1 25, right-click {}{}, {UNTICK}, then launch again.",
                        c.name,
                        at(c)
                    ),
                    None => "To fix it, close F1 25, fully quit the EA app or Steam (right-click its icon in the \
                         system tray > Exit), then press Play in Swofty Launcher without starting them first. If \
                         mods still don't load, press Send Logs to Developers and ask in the support Discord."
                        .to_owned(),
                }
            }
            Self::Antivirus { names, .. } => format!(
                "To fix it, close F1 25, open {}, restore or allow {PROXY} and F1_25.exe if it blocked them, add \
                 exclusions for the game folder ({}) and the launcher folder, and turn off its DLL or behaviour \
                 protection for F1_25.exe (a folder exclusion is often not enough), then launch again.",
                names[0],
                game.display()
            ),
            Self::Unexplained { hooks, .. } => {
                let close = if hooks.is_empty() {
                    String::new()
                } else {
                    format!("Close {} before you press Play and try again. ", hooks.join(" and "))
                };
                format!(
                    "{close}If mods still don't load, press Send Logs to Developers and ask in the support Discord, \
                     so the developers can look at your PC's setup."
                )
            }
            Self::Unreadable => "To fix it, close F1 25, fully quit the EA app or Steam from the system tray, then \
                 launch again. If mods still don't load, press Send Logs to Developers and ask in the support \
                 Discord."
                .to_owned(),
            Self::Defender => return None,
        })
    }
}

impl Report {
    pub fn new(stage: Stage, files: Vec<String>, game: &Path) -> Self {
        Self {
            at: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map_or(0.0, |d| d.as_secs_f64()),
            stage,
            files,
            removed: false,
            game: game.to_path_buf(),
            launcher: launcher_dir(),
            cause: None,
        }
    }

    pub fn path(data: &Path) -> PathBuf {
        data.join("state/mods-blocked.json")
    }

    pub fn write(&self, data: &Path) -> Result<()> {
        write_json(&Self::path(data), self)
    }

    pub fn read(data: &Path) -> Option<Self> {
        read_json(&Self::path(data)).ok()
    }

    pub fn clear(data: &Path) -> Result<()> {
        let path = Self::path(data);
        match std::fs::remove_file(&path) {
            Err(e) if e.kind() != std::io::ErrorKind::NotFound => Err(io_err(&path)(e)),
            _ => Ok(()),
        }
    }

    fn no_loader_folder(&self) -> bool {
        self.files.iter().any(|f| f == LOADER_FOLDER)
    }

    pub fn title(&self) -> &'static str {
        match self.stage {
            Stage::BeforeLaunch if self.no_loader_folder() => "Swofty Launcher's loader folder is missing",
            Stage::BeforeLaunch => "Your antivirus removed mod files",
            Stage::InGame => "Mods did not load",
        }
    }

    pub fn text(&self) -> String {
        if self.stage == Stage::BeforeLaunch && self.no_loader_folder() {
            let launcher = self
                .launcher
                .as_ref()
                .map(|l| format!(" ({})", l.display()))
                .unwrap_or_default();
            return format!(
                "F1 25 was not started: your Swofty Launcher install has no loader folder, the part that puts the \
                 mods into the game. To fix it, reinstall Swofty Launcher with the installer from f1.swofty.net; \
                 your addons and settings are kept. If the folder goes missing again, your antivirus removed it: \
                 restore it from the antivirus's quarantine and add the launcher folder{launcher} to its \
                 exclusions."
            );
        }
        let names = names(&self.files);
        let one = self.files.len() <= 1;
        let it = if one { "it" } else { "them" };
        let cause = self
            .cause
            .as_deref()
            .filter(|_| self.stage == Stage::InGame && !self.removed);
        let what = match (self.stage, cause) {
            (Stage::BeforeLaunch, _) => {
                format!("F1 25 was not started: an antivirus most likely deleted or quarantined {names}.")
            }
            (Stage::InGame, _) if self.removed => format!(
                "F1 25 is running without mods: {names} {} removed from the game folder. An antivirus most likely deleted or quarantined {it}.",
                if one { "was" } else { "were" }
            ),
            (Stage::InGame, Some(c)) => format!("F1 25 is running without mods: {}.", c.what()),
            (Stage::InGame, None) => {
                format!("F1 25 is running without mods. An antivirus most likely blocked {names}.")
            }
        };
        let fix = cause
            .and_then(|c| c.fix(&self.game))
            .unwrap_or_else(|| self.fix());
        format!("{what} {fix}")
    }

    fn fix(&self) -> String {
        let it = if self.files.len() <= 1 { "it" } else { "them" };
        let folders = match &self.launcher {
            Some(l) => format!(
                "the game folder ({}) and the launcher folder ({})",
                self.game.display(),
                l.display()
            ),
            None => format!(
                "the game folder ({}) and the launcher folder",
                self.game.display()
            ),
        };
        format!(
            "To fix it, {}restore {it} in Windows Security > Virus & threat protection > \
             Protection history, add exclusions for {folders}, then {}.",
            match self.stage {
                Stage::BeforeLaunch => "",
                Stage::InGame => "close F1 25, ",
            },
            match self.stage {
                Stage::BeforeLaunch => "press Play again",
                Stage::InGame => "launch again",
            }
        )
    }
}

fn names(files: &[String]) -> String {
    let mut names: Vec<&str> = Vec::new();
    for f in files {
        let name = f.rsplit(['/', '\\']).next().unwrap_or(f);
        if !names.iter().any(|n| n.eq_ignore_ascii_case(name)) {
            names.push(name);
        }
    }
    match names.as_slice() {
        [] => PROXY.to_owned(),
        [a] => (*a).to_owned(),
        [a, b] => format!("{a} and {b}"),
        [rest @ .., last] if rest.len() < 4 => format!("{} and {last}", rest.join(", ")),
        _ => format!("{} and {} more", names[..3].join(", "), names.len() - 3),
    }
}

/// The folder the launcher is installed in, for the exclusion.
fn launcher_dir() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    Some(exe.parent()?.to_path_buf())
}

/// A failed copy into the game folder; one Windows refused as a threat says
/// so and what to do about it.
pub(crate) fn copy_error(rel: &str, path: &Path, e: std::io::Error) -> crate::Error {
    if !is_virus_error(&e) {
        return io_err(path)(e);
    }
    let game = path
        .ancestors()
        .nth(rel.split(['/', '\\']).count())
        .unwrap_or(path);
    crate::Error::Other(format!(
        "Windows blocked {} as a threat ({e}). {}",
        names(&[rel.to_owned()]),
        Report::new(Stage::BeforeLaunch, vec![rel.to_owned()], game).fix()
    ))
}

/// The DLLs the launcher wrote into the game folder, with the bytes it wrote.
pub fn placed_dlls(data: &Path) -> Result<Vec<(String, String)>> {
    Ok(Placed::load(data)?
        .files
        .into_values()
        .filter(|f| !f.replaced && f.path.to_ascii_lowercase().ends_with(".dll"))
        .map(|f| (f.path, f.sha256))
        .collect())
}

/// The placed DLLs that are gone from the game folder, cannot be read, or no
/// longer hold the bytes written there.
pub fn missing(game: &Path, dlls: &[(String, String)]) -> Vec<String> {
    dlls.iter()
        .filter(|(rel, sha)| !sha256_file(&game.join(rel)).is_ok_and(|now| now == *sha))
        .map(|(rel, _)| rel.clone())
        .collect()
}

/// The report's file list for an install with no loader folder at all.
pub const LOADER_FOLDER: &str = "loader/";

/// A loader folder of the install that lost version.dll or the core DLL, or
/// an installed launcher (in a `bin` folder) with no loader folder at all:
/// the build then finds no loader and the game starts without one.
pub fn damaged_install() -> Vec<String> {
    if crate::builtin::loader_dir().is_some() || std::env::var_os(LOADER_ENV).is_some() {
        return Vec::new();
    }
    let Some(bin) = launcher_dir() else {
        return Vec::new();
    };
    damaged_in(&bin)
}

fn damaged_in(bin: &Path) -> Vec<String> {
    let dirs = [bin.parent().map(|p| p.join("loader")), Some(bin.join("loader"))];
    match dirs.into_iter().flatten().find(|d| d.is_dir()) {
        Some(dir) => damaged_loader(&dir),
        None if bin.file_name().is_some_and(|n| n == "bin") => vec![LOADER_FOLDER.to_owned()],
        None => Vec::new(),
    }
}

fn damaged_loader(dir: &Path) -> Vec<String> {
    [PROXY, CORE_DLL]
        .into_iter()
        .filter(|rel| !dir.join(rel).is_file())
        .map(|rel| format!("loader/{rel}"))
        .collect()
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Attach {
    Waiting,
    Attached,
    Missed,
}

/// Whether a loader.log shows the loader running: `Some(false)` for a log
/// with lines but no attach line (only the offline play stub's), `None` for
/// an empty one.
pub fn attached(text: &str) -> Option<bool> {
    if text.contains(ATTACHED_TAIL) {
        return Some(true);
    }
    (!text.trim().is_empty()).then_some(false)
}

/// Why the loader wrote its fallback log: the game folder's log it could not
/// open, with the error, and the rights the game runs with.
pub fn refused(fallback: &str) -> Option<String> {
    let mut lines = fallback.lines();
    let log = lines.find(|l| l.contains(REFUSED))?;
    let log = &log[log.find("log: ")? + "log: ".len()..];
    let log = &log[..log.find(';').unwrap_or(log.len())];
    let rights = lines
        .find_map(|l| Some(&l[l.find("process: ")? + "process: ".len()..]))
        .map(|p| format!("; the game runs as {}", p.trim_end()))
        .unwrap_or_default();
    Some(format!("{log}{rights}"))
}

/// What the session says when the loader attached but logs to its fallback.
/// The EA app can start F1 25 without administrator rights even when an
/// elevated launcher started the EA app, so it names no restart as the fix.
pub fn refused_text() -> &'static str {
    "F1 25 can't create files in its folder, so this session the loader logs to the data folder and addons keep \
     their files in addon-storage in the data folder. An addon that writes in the game folder by itself can't save \
     there; moving F1 25 to a folder outside Program Files fixes that."
}

/// Follows the game folder's loader.log after the game starts. The stub
/// writes its lines first; the loader moves them to loader.prev.log when it
/// attaches and starts a fresh log with its attach line, so a log still
/// equal to the one from before the launch says nothing. A loader the game
/// folder refuses logs to the fallback in the data folder instead, which a
/// session starts without.
pub struct AttachWatch {
    log: PathBuf,
    before: Option<LogMark>,
    fallback: Option<PathBuf>,
    started: Instant,
    patched: Option<Instant>,
    next: Instant,
    state: Attach,
}

impl AttachWatch {
    pub fn new(log: PathBuf, before: Option<LogMark>, now: Instant) -> Self {
        Self {
            log,
            before,
            fallback: None,
            started: now,
            patched: None,
            next: now,
            state: Attach::Waiting,
        }
    }

    pub fn with_fallback(self, fallback: PathBuf) -> Self {
        Self {
            fallback: Some(fallback),
            ..self
        }
    }

    pub fn state(&self) -> Attach {
        self.state
    }

    /// The state after a look at the log; a verdict, once reached, stays.
    pub fn poll(&mut self, now: Instant) -> Attach {
        if self.state != Attach::Waiting || now < self.next {
            return self.state;
        }
        self.next = now + POLL;
        if self.read(now) == Some(true) {
            self.state = Attach::Attached;
            return self.state;
        }
        let (from, window) = match self.patched {
            Some(at) => (at, ATTACH_WINDOW),
            None => (self.started, ATTACH_WINDOW_NO_STUB),
        };
        if now.duration_since(from) > window {
            self.state = Attach::Missed;
        }
        self.state
    }

    /// The verdict when the game has exited: a game that ran a while and
    /// never got the loader missed it.
    pub fn ended(&mut self, now: Instant) -> Attach {
        if self.state == Attach::Waiting {
            self.state = if self.read(now) == Some(true) {
                Attach::Attached
            } else if now.duration_since(self.started) >= SHORTEST_RUN {
                Attach::Missed
            } else {
                Attach::Waiting
            };
        }
        self.state
    }

    fn read(&mut self, now: Instant) -> Option<bool> {
        if let Some(fallback) = self.fallback.as_ref().and_then(|f| std::fs::read(f).ok())
            && attached(&String::from_utf8_lossy(&fallback)) == Some(true)
        {
            return Some(true);
        }
        let text = std::fs::read(&self.log).ok()?;
        let text = String::from_utf8_lossy(&text);
        if self.patched.is_none() && text.contains(PATCHING) {
            self.patched = Some(now);
        }
        if !LogMark::replaced(&self.log, self.before.as_ref()) {
            return None;
        }
        attached(&text)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const STUB: &str = "01:12:12.575 [stub] [offline] session launcher invoked, args: <none>\n\
                        01:12:13.901 [stub] [offline] game image at 0x140000000, patching\n";
    const LOADED: &str =
        "2026-10-20 21:16:05.789 [ 2548] swofty loader (version.dll proxy) attached, pid 14088\n";
    const FALLBACK: &str = "2026-10-24 16:34:40.020 [ 1416] swofty loader (version.dll proxy) attached, pid 24676\r\n\
         2026-10-24 16:34:40.020 [ 1416] log: C:\\Program Files\\EA Games\\F1 25\\swofty\\loader.log could not be \
         opened (error 5); this is the fallback log C:\\Users\\id\\AppData\\Local\\Swofty\\state\\loader-fallback.log\r\n\
         2026-10-24 16:34:40.021 [ 1416] process: user id (S-1-5-21-1-2-3-1001), medium integrity, not elevated, \
         UAC virtualization off, __COMPAT_LAYER=(unset)\r\n";

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-quarantine-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn knows_the_two_windows_virus_codes() {
        assert!(is_virus_code(225));
        assert!(is_virus_code(226));
        assert!(!is_virus_code(5));
        assert!(!is_virus_code(32));
    }

    #[test]
    fn finds_a_placed_dll_that_was_deleted_or_changed() {
        let game = temp("missing");
        std::fs::create_dir_all(game.join("swofty/addons/core")).unwrap();
        std::fs::write(game.join("version.dll"), b"proxy").unwrap();
        std::fs::write(game.join(CORE_DLL), b"core").unwrap();
        let dlls = vec![
            (
                "version.dll".to_owned(),
                sha256_file(&game.join("version.dll")).unwrap(),
            ),
            (CORE_DLL.to_owned(), sha256_file(&game.join(CORE_DLL)).unwrap()),
        ];
        assert!(missing(&game, &dlls).is_empty());
        std::fs::remove_file(game.join("version.dll")).unwrap();
        std::fs::write(game.join(CORE_DLL), b"emptied by a scanner").unwrap();
        assert_eq!(missing(&game, &dlls), ["version.dll", CORE_DLL]);
        std::fs::remove_dir_all(&game).ok();
    }

    #[test]
    fn lists_only_the_dlls_it_placed() {
        let data = temp("placed");
        let mut placed = Placed::load(&data).unwrap();
        placed.insert("version.dll", "a".into(), false);
        placed.insert(CORE_DLL, "b".into(), false);
        placed.insert("swofty/addons/core/symbols.tsv", "c".into(), false);
        placed.insert("2025_asset_groups/ui_package/databases.erp", "d".into(), true);
        placed.save(&data).unwrap();
        let mut got = placed_dlls(&data).unwrap();
        got.sort();
        assert_eq!(
            got,
            [
                (CORE_DLL.to_owned(), "b".to_owned()),
                ("version.dll".to_owned(), "a".to_owned())
            ]
        );
        std::fs::remove_dir_all(&data).ok();
    }

    #[test]
    fn names_what_an_install_loader_folder_lost() {
        let dir = temp("install");
        std::fs::create_dir_all(dir.join("swofty/addons/core")).unwrap();
        std::fs::write(dir.join(CORE_DLL), b"core").unwrap();
        assert_eq!(damaged_loader(&dir), ["loader/version.dll"]);
        std::fs::write(dir.join(PROXY), b"proxy").unwrap();
        assert!(damaged_loader(&dir).is_empty());
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn tells_a_stub_only_log_from_a_loaded_one() {
        assert_eq!(attached(STUB), Some(false));
        assert_eq!(
            attached(&format!(
                "{LOADED}2026-10-20 21:16:05.789 [ 2548] host: F1_25.exe\n"
            )),
            Some(true)
        );
        assert_eq!(attached(""), None);
    }

    #[test]
    fn reads_an_attach_line_with_a_clobbered_start() {
        let clobbered = ".dll proxy) attached, pid 12316\r\n\
                         2026-10-27 18:31:45.051 [ 2416] host: F1_25.exe\n";
        assert_eq!(attached(clobbered), Some(true));
    }

    #[test]
    fn misses_the_loader_a_while_after_the_stub_patched_the_game() {
        let dir = temp("watch-miss");
        let log = dir.join("loader.log");
        let t0 = Instant::now();
        let mut w = AttachWatch::new(log.clone(), None, t0);
        assert_eq!(w.poll(t0), Attach::Waiting);
        std::fs::write(&log, STUB).unwrap();
        assert_eq!(w.poll(t0 + Duration::from_secs(2)), Attach::Waiting);
        assert_eq!(w.poll(t0 + Duration::from_secs(20)), Attach::Waiting);
        let late = t0 + Duration::from_secs(3) + ATTACH_WINDOW;
        assert_eq!(w.poll(late), Attach::Missed);
        std::fs::write(&log, LOADED).unwrap();
        assert_eq!(w.poll(late + Duration::from_secs(5)), Attach::Missed);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn sees_the_loader_attach_and_ignores_a_stale_log() {
        let dir = temp("watch-ok");
        let log = dir.join("loader.log");
        std::fs::write(&log, LOADED).unwrap();
        let t0 = Instant::now();
        let mut w = AttachWatch::new(log.clone(), LogMark::read(&log), t0);
        assert_eq!(w.poll(t0), Attach::Waiting);
        std::fs::remove_file(&log).unwrap();
        std::fs::write(&log, STUB).unwrap();
        assert_eq!(w.poll(t0 + Duration::from_secs(2)), Attach::Waiting);
        std::fs::write(
            &log,
            "2026-10-21 01:12:14.001 [  420] swofty loader (version.dll proxy) attached, pid 1976\n",
        )
        .unwrap();
        assert_eq!(w.poll(t0 + Duration::from_secs(4)), Attach::Attached);
        assert_eq!(w.ended(t0 + Duration::from_secs(600)), Attach::Attached);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn sees_the_loader_attach_in_its_fallback_log() {
        let dir = temp("watch-fallback");
        let log = dir.join("loader.log");
        let fallback = dir.join("loader-fallback.log");
        std::fs::write(&log, LOADED).unwrap();
        let t0 = Instant::now();
        let mut plain = AttachWatch::new(log.clone(), LogMark::read(&log), t0);
        let mut w = AttachWatch::new(log.clone(), LogMark::read(&log), t0).with_fallback(fallback.clone());
        assert_eq!(w.poll(t0), Attach::Waiting);
        std::fs::write(&fallback, FALLBACK).unwrap();
        assert_eq!(w.poll(t0 + Duration::from_secs(2)), Attach::Attached);
        let late = t0 + Duration::from_secs(1) + ATTACH_WINDOW_NO_STUB;
        assert_eq!(plain.poll(late), Attach::Missed);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn says_why_the_loader_logged_to_its_fallback() {
        assert_eq!(
            refused(FALLBACK).as_deref(),
            Some(
                "C:\\Program Files\\EA Games\\F1 25\\swofty\\loader.log could not be opened (error 5); the game runs as \
                 user id (S-1-5-21-1-2-3-1001), medium integrity, not elevated, UAC virtualization off, \
                 __COMPAT_LAYER=(unset)"
            )
        );
        assert_eq!(refused(LOADED), None);
        assert_eq!(refused(STUB), None);
        assert!(refused_text().contains("addon-storage"));
        assert!(!refused_text().contains("administrator"));
    }

    #[test]
    fn a_session_that_ends_without_the_loader_missed_it() {
        let dir = temp("watch-end");
        let log = dir.join("loader.log");
        std::fs::write(&log, STUB).unwrap();
        let t0 = Instant::now();
        let mut quick = AttachWatch::new(log.clone(), None, t0);
        assert_eq!(quick.ended(t0 + Duration::from_secs(3)), Attach::Waiting);
        let mut long = AttachWatch::new(log, None, t0);
        assert_eq!(long.ended(t0 + Duration::from_secs(13 * 60)), Attach::Missed);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn says_what_happened_and_how_to_fix_it() {
        let mut r = Report {
            at: 1.0,
            stage: Stage::BeforeLaunch,
            files: vec!["version.dll".into(), CORE_DLL.into()],
            removed: false,
            game: PathBuf::from(r"C:\Steam\steamapps\common\F1 25"),
            launcher: Some(PathBuf::from(r"C:\Programs\SwoftyLauncher")),
            cause: None,
        };
        assert_eq!(
            r.text(),
            "F1 25 was not started: an antivirus most likely deleted or quarantined version.dll and \
             swofty_core.dll. To fix it, restore them in Windows Security > Virus & threat protection > \
             Protection history, add exclusions for the game folder (C:\\Steam\\steamapps\\common\\F1 25) \
             and the launcher folder (C:\\Programs\\SwoftyLauncher), then press Play again."
        );
        r.stage = Stage::InGame;
        r.files = vec!["version.dll".into()];
        r.removed = true;
        assert!(r.text().starts_with(
            "F1 25 is running without mods: version.dll was removed from the game folder. An antivirus \
             most likely deleted or quarantined it. To fix it, close F1 25, restore it in"
        ));
        r.removed = false;
        assert!(r.text().starts_with(
            "F1 25 is running without mods. An antivirus most likely blocked version.dll. To fix it"
        ));
        assert_eq!(r.title(), "Mods did not load");
    }

    #[test]
    fn names_what_kept_the_loader_out_when_the_session_found_it() {
        let mut r = Report {
            at: 1.0,
            stage: Stage::InGame,
            files: vec!["version.dll".into()],
            removed: false,
            game: PathBuf::from(r"E:\SteamLibrary\steamapps\common\F1 25"),
            launcher: Some(PathBuf::from(r"C:\Programs\SwoftyLauncher")),
            cause: None,
        };
        let fallback = r.text();
        assert!(fallback.starts_with(
            "F1 25 is running without mods. An antivirus most likely blocked version.dll. To fix it, close F1 25, \
             restore it in Windows Security"
        ));
        let text = |r: &mut Report, cause: Cause| {
            r.cause = Some(Box::new(cause));
            r.text()
        };
        assert_eq!(
            text(&mut r, Cause::SmartAppControl { seen: true }),
            "F1 25 is running without mods: Smart App Control blocked version.dll. To fix it, close F1 25, turn \
             off Smart App Control in Windows Security > App & browser control > Smart App Control settings, then \
             launch again."
        );
        assert!(
            text(&mut r, Cause::SmartAppControl { seen: false }).starts_with(
                "F1 25 is running without mods: Smart App Control is on and most likely blocked"
            )
        );
        assert_eq!(
            text(
                &mut r,
                Cause::Policy {
                    name: Some("Contoso".into())
                }
            ),
            "F1 25 is running without mods: a Windows App Control policy (Contoso) blocked version.dll. To fix \
             it, close F1 25, ask whoever manages this PC to allow version.dll in the game folder \
             (E:\\SteamLibrary\\steamapps\\common\\F1 25), then launch again."
        );
        let signed = text(&mut r, Cause::SignedOnly);
        assert!(
            signed.contains("load only Microsoft-signed DLLs")
                && signed.contains("Exploit protection > Program settings"),
            "{signed}"
        );
        assert_eq!(
            text(&mut r, Cause::Defender),
            "F1 25 is running without mods: Microsoft Defender blocked version.dll. To fix it, close F1 25, \
             restore it in Windows Security > Virus & threat protection > Protection history, add exclusions for \
             the game folder (E:\\SteamLibrary\\steamapps\\common\\F1 25) and the launcher folder \
             (C:\\Programs\\SwoftyLauncher), then launch again."
        );
        let known = text(&mut r, Cause::KnownDll);
        assert!(
            known.contains(r"HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDLLs"),
            "{known}"
        );
        assert_eq!(
            text(
                &mut r,
                Cause::OtherCopy {
                    path: r"C:\WINDOWS\SYSTEM32\VERSION.dll".into(),
                    prefer_system32: false
                }
            ),
            "F1 25 is running without mods: the game loaded C:\\WINDOWS\\SYSTEM32\\VERSION.dll instead of the game \
             folder's version.dll. Security software on this PC most likely redirected it. To fix it, close F1 25, \
             turn off your antivirus's DLL or behaviour protection for F1_25.exe (a folder exclusion is often not \
             enough), then launch again."
        );
        let prefer = text(
            &mut r,
            Cause::OtherCopy {
                path: r"C:\WINDOWS\SYSTEM32\VERSION.dll".into(),
                prefer_system32: true,
            },
        );
        assert!(
            prefer.contains("Exploit protection makes F1_25.exe prefer System32 DLLs. To fix it"),
            "{prefer}"
        );
        let system = || r"C:\WINDOWS\SYSTEM32\VERSION.dll".to_owned();
        let exploit = text(&mut r, Cause::ExploitProtection { path: system() });
        assert!(
            exploit.contains("has its own Exploit protection settings")
                && exploit.contains("remove every F1_25.exe entry, restart the PC"),
            "{exploit}"
        );
        let layers = text(
            &mut r,
            Cause::CompatLayer {
                path: system(),
                layers: vec!["~ WIN8RTM (for this user)".into()],
                shims: vec!["AcLayers.dll".into()],
                from: None,
            },
        );
        assert!(
            layers.contains("compatibility settings are on for F1_25.exe (~ WIN8RTM (for this user))")
                && layers.contains("Change settings for all users"),
            "{layers}"
        );
        let shims = text(
            &mut r,
            Cause::CompatLayer {
                path: system(),
                layers: Vec::new(),
                shims: vec!["AcLayers.dll".into()],
                from: None,
            },
        );
        assert!(
            shims.contains("Windows applied compatibility shims to it (AcLayers.dll)")
                && shims.contains("fully quit the EA app or Steam")
                && !shims.contains("right-click F1_25.exe"),
            "{shims}"
        );
        const EA: &str = r"C:\Program Files\Electronic Arts\EA Desktop\EA Desktop\EADesktop.exe";
        let shimmed = |from: Carrier| Cause::CompatLayer {
            path: system(),
            layers: Vec::new(),
            shims: vec!["AcLayers.DLL".into(), "AcGenral.DLL".into()],
            from: Some(from),
        };
        let ea = text(
            &mut r,
            shimmed(Carrier {
                name: "EADesktop.exe".into(),
                exe: Some(EA.into()),
                layers: vec!["~ RUNASADMIN (for all users)".into()],
            }),
        );
        assert!(
            ea.contains(
                "Windows applied compatibility shims to it (AcLayers.DLL, AcGenral.DLL), passed on from the \
                 compatibility settings on EADesktop.exe (~ RUNASADMIN (for all users))"
            ) && ea.contains(&format!(
                "fully quit the EA app (right-click its icon in the system tray > Exit), right-click EADesktop.exe \
                 ({EA}), open Properties > Compatibility and untick everything"
            )) && ea.contains("Change settings for all users")
                && !ea.contains("F1_25.exe in the game folder"),
            "{ea}"
        );
        let running = text(
            &mut r,
            shimmed(Carrier {
                name: "EADesktop.exe".into(),
                exe: Some(EA.into()),
                layers: Vec::new(),
            }),
        );
        assert!(
            running.contains("passed on from EADesktop.exe, which runs with them too")
                && running.contains("then press Play in Swofty Launcher without starting the EA app first"),
            "{running}"
        );
        let launcher = text(
            &mut r,
            shimmed(Carrier {
                name: "swofty-launcher.exe".into(),
                exe: Some(r"C:\Swofty\swofty-launcher.exe".into()),
                layers: vec!["~ WIN7RTM (for this user)".into()],
            }),
        );
        assert!(
            launcher.contains("close F1 25 and Swofty Launcher")
                && launcher.contains(r"right-click swofty-launcher.exe (C:\Swofty\swofty-launcher.exe)")
                && launcher.contains("then start Swofty Launcher and launch again"),
            "{launcher}"
        );
        let av = text(
            &mut r,
            Cause::Antivirus {
                path: system(),
                names: vec!["Reason Cybersecurity".into()],
            },
        );
        assert!(
            av.contains("Reason Cybersecurity, the antivirus on this PC, most likely redirected it")
                && av.contains("open Reason Cybersecurity, restore or allow version.dll"),
            "{av}"
        );
        let unexplained = text(
            &mut r,
            Cause::Unexplained {
                path: system(),
                hooks: vec!["Medal".into()],
            },
        );
        assert!(
            unexplained.contains("Swofty Launcher couldn't tell why")
                && unexplained.contains("Close Medal before you press Play")
                && unexplained.contains("support Discord")
                && !unexplained.contains("antivirus"),
            "{unexplained}"
        );
        assert!(
            Cause::Unexplained {
                path: system(),
                hooks: Vec::new()
            }
            .unexplained()
        );
        let unreadable = text(&mut r, Cause::Unreadable);
        assert!(
            unreadable.contains("access denied")
                && unreadable.contains("Send Logs")
                && !unreadable.contains("antivirus"),
            "{unreadable}"
        );
        assert!(Cause::Unreadable.unexplained());
        r.removed = true;
        assert!(
            r.text()
                .starts_with("F1 25 is running without mods: version.dll was removed")
        );
        r.cause = None;
        r.removed = false;
        assert_eq!(r.text(), fallback);
        assert_eq!(r.title(), "Mods did not load");
    }

    #[test]
    fn an_install_without_a_loader_folder_says_reinstall() {
        let root = temp("no-loader");
        let bin = root.join("bin");
        std::fs::create_dir_all(&bin).unwrap();
        assert_eq!(damaged_in(&bin), [LOADER_FOLDER]);
        assert!(damaged_in(&root.join("debug")).is_empty(), "a source build");
        std::fs::create_dir_all(root.join("loader")).unwrap();
        assert_eq!(
            damaged_in(&bin),
            ["loader/version.dll", "loader/swofty/addons/core/swofty_core.dll"]
        );
        let r = Report::new(
            Stage::BeforeLaunch,
            vec![LOADER_FOLDER.into()],
            Path::new("/games/F1 25"),
        );
        assert_eq!(r.title(), "Swofty Launcher's loader folder is missing");
        assert!(r.text().contains("reinstall Swofty Launcher"), "{}", r.text());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_report_keeps_its_cause_and_reads_one_without() {
        let mut r = Report::new(
            Stage::InGame,
            vec!["version.dll".into()],
            Path::new("/games/F1 25"),
        );
        r.cause = Some(Box::new(Cause::OtherCopy {
            path: r"C:\WINDOWS\SYSTEM32\VERSION.dll".into(),
            prefer_system32: false,
        }));
        let json = serde_json::to_value(&r).unwrap();
        assert_eq!(json["cause"]["kind"], "other_copy");
        assert_eq!(serde_json::from_value::<Report>(json).unwrap(), r);
        let old = r#"{"at":1.0,"stage":"in_game","files":["version.dll"],"game":"/games/F1 25"}"#;
        assert_eq!(serde_json::from_str::<Report>(old).unwrap().cause, None);
        r.cause = None;
        assert!(serde_json::to_value(&r).unwrap().get("cause").is_none());
    }

    #[test]
    fn a_report_survives_a_round_trip_and_clears() {
        let data = temp("report");
        let r = Report::new(
            Stage::InGame,
            vec!["version.dll".into()],
            Path::new("/games/F1 25"),
        );
        r.write(&data).unwrap();
        let back = Report::read(&data).unwrap();
        assert!((back.at - r.at).abs() < 1e-3);
        assert_eq!(Report { at: r.at, ..back }, r);
        Report::clear(&data).unwrap();
        assert_eq!(Report::read(&data), None);
        Report::clear(&data).unwrap();
        std::fs::remove_dir_all(&data).ok();
    }
}
