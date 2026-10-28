//! What the session looks at once version.dll did not attach to a running
//! game: the game folder's copy against the one the launcher placed, which
//! F1_25.exe runs and which version.dll it holds, and on Windows the DLL
//! policies on the game process, KnownDLLs, Smart App Control, the Code
//! Integrity and Defender events since the game started, and the antivirus
//! registered with Windows. Under Proton it is the prefix's override and the
//! version.dll Wine mapped. Every check is read-only, bounded in time, and a
//! check that fails says why and the rest go on.

use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use crate::GAME_EXE;
use crate::baseline::sha256_file;
use crate::compat::AdminLayer;
use crate::install::{PROXY, has_override};
use crate::process::{GameProcess, same_folder};
use crate::quarantine::{Carrier, Cause};
use crate::session::LOADER_LOG;

pub type Probe<T> = std::result::Result<T, String>;

/// Each Windows tool gets this long, and they run side by side.
#[cfg_attr(not(windows), allow(dead_code))]
const TOOL_WAIT: Duration = Duration::from_secs(2);
/// Events from a little before the game started count too.
#[cfg_attr(not(windows), allow(dead_code))]
const EVENT_MARGIN: Duration = Duration::from_secs(120);
const MAX_GAMES: usize = 3;
const MAX_OTHERS: usize = 24;
const MAX_EVENTS: usize = 6;

pub const CI_LOG: &str = "Microsoft-Windows-CodeIntegrity/Operational";
pub const DEFENDER_LOG: &str = "Microsoft-Windows-Windows Defender/Operational";
/// Signing level not met (and its audit), App Control policy block (and audit).
#[cfg_attr(not(windows), allow(dead_code))]
const CI_IDS: &[u32] = &[3033, 3034, 3076, 3077];
/// Threat detected and acted on, attack surface reduction block and audit,
/// Controlled folder access block and audit.
#[cfg_attr(not(windows), allow(dead_code))]
const DEFENDER_IDS: &[u32] = &[1116, 1117, 1121, 1122, 1123, 1124];
const SIGNING_LEVEL: u32 = 3033;
const POLICY_BLOCK: u32 = 3077;
/// The events that stop a file loading, as opposed to auditing it.
const BLOCKS: &[u32] = &[3033, 3077, 1116, 1117, 1121];

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Findings {
    /// The game folder's version.dll.
    pub proxy: Proxy,
    /// The size of the game folder's F1_25.exe.
    pub exe_size: Option<u64>,
    /// The pid the offline play stub created, from its loader.log line.
    pub created: Option<u32>,
    pub games: Vec<Seen>,
    /// loader.prev.log was written after the game started: the loader ran
    /// and moved the log aside.
    pub rotated: bool,
    pub search: Option<Probe<Search>>,
    /// The value names under F1_25.exe's Image File Execution Options key,
    /// where Exploit protection keeps its program settings.
    pub exec_options: Option<Probe<Vec<String>>>,
    /// The compatibility settings on the game folder's F1_25.exe, as
    /// `<layers> (for this user|for all users)`.
    pub layers: Option<Probe<Vec<String>>>,
    /// The compatibility settings on the programs that start the game
    /// (`compat::starter_layers`).
    pub starter_layers: Option<Vec<AdminLayer>>,
    /// The EA app processes running, and the compatibility shims in each.
    pub ea_app: Vec<EaApp>,
    /// VerifiedAndReputablePolicyState: 0 off, 1 on, 2 evaluation.
    pub smart_app_control: Option<Probe<Option<u32>>>,
    pub logs: Vec<EventLog>,
    pub antivirus: Option<Probe<Vec<String>>>,
    /// Proton: the prefix's user.reg holds the version.dll override.
    pub wine_override: Option<bool>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub enum Proxy {
    #[default]
    Missing,
    /// `same`: whether it holds the bytes the launcher placed, when there is
    /// a record of them; `zone`: its Zone.Identifier mark.
    Present {
        size: u64,
        same: Option<bool>,
        zone: Option<u32>,
    },
    Unreadable(String),
}

#[derive(Debug, Clone, PartialEq)]
pub struct Seen {
    pub pid: u32,
    pub image: Option<String>,
    pub image_size: Option<u64>,
    /// The image is the game folder's F1_25.exe.
    pub ours: bool,
    pub modules: Probe<Modules>,
    pub mitigation: Option<Probe<Mitigation>>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct EaApp {
    pub pid: u32,
    pub image: Option<String>,
    pub shims: Probe<Vec<String>>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Modules {
    pub count: usize,
    /// Every version.dll the process holds, by full path.
    pub version: Vec<String>,
    /// The file names of modules from outside the game and Windows folders.
    pub others: Vec<String>,
    /// The compatibility shim DLLs Windows loaded, which a compatibility
    /// setting or a shim database brings in.
    pub shims: Vec<String>,
}

/// The process's DLL load policies (GetProcessMitigationPolicy).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Mitigation {
    pub microsoft_signed: bool,
    pub store_signed: bool,
    pub prefer_system32: bool,
    pub no_remote: bool,
    pub no_low_label: bool,
}

impl Mitigation {
    pub fn names(&self) -> Vec<&'static str> {
        [
            (self.microsoft_signed, "Microsoft-signed DLLs only"),
            (self.store_signed, "Store-signed DLLs only"),
            (self.prefer_system32, "prefer System32 DLLs"),
            (self.no_remote, "no DLLs from network shares"),
            (self.no_low_label, "no low-integrity DLLs"),
        ]
        .into_iter()
        .filter_map(|(on, name)| on.then_some(name))
        .collect()
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Search {
    pub known_dll: bool,
    pub safe_search: Option<u32>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct EventLog {
    pub name: &'static str,
    pub read: Probe<Vec<Event>>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Event {
    pub id: u32,
    /// The UTC clock time it was logged.
    pub at: Option<String>,
    pub file: Option<String>,
    pub process: Option<String>,
    pub policy: Option<String>,
    pub threat: Option<String>,
    /// It names version.dll.
    pub proxy: bool,
}

impl Event {
    pub fn blocks(&self) -> bool {
        BLOCKS.contains(&self.id)
    }

    fn what(&self) -> &'static str {
        match self.id {
            3033 => "blocked: below the signing level the process requires",
            3034 => "audit: would be blocked for its signing level",
            3076 => "audit: an App Control policy would block it",
            3077 => "blocked by an App Control policy",
            1116 => "detected as a threat",
            1117 => "acted on as a threat",
            1121 => "blocked by an attack surface reduction rule",
            1122 => "audit: an attack surface reduction rule would block it",
            1123 => "blocked by Controlled folder access",
            1124 => "audit: Controlled folder access would block it",
            _ => "logged",
        }
    }
}

/// Look at what could keep version.dll out of the running game. `placed` is
/// the SHA-256 of the version.dll the launcher placed, `since` when the game
/// was started, `user_reg` the Proton prefix's user.reg.
pub fn collect(game: &Path, placed: Option<&str>, since: SystemTime, user_reg: Option<&Path>) -> Findings {
    let log = game.join(LOADER_LOG);
    let stub = std::fs::read(&log)
        .map(|b| String::from_utf8_lossy(&b).into_owned())
        .unwrap_or_default();
    let mut f = Findings {
        proxy: proxy(&game.join(PROXY), placed),
        exe_size: std::fs::metadata(game.join(GAME_EXE)).ok().map(|m| m.len()),
        created: created_pid(&stub),
        rotated: std::fs::metadata(log.with_file_name("loader.prev.log"))
            .and_then(|m| m.modified())
            .is_ok_and(|t| t >= since),
        wine_override: user_reg
            .filter(|_| !cfg!(windows))
            .map(|r| std::fs::read_to_string(r).is_ok_and(|t| has_override(&t))),
        ..Findings::default()
    };
    let running = crate::process::game_processes();
    platform::fill(&mut f, game, &running, since);
    f
}

fn proxy(path: &Path, placed: Option<&str>) -> Proxy {
    let size = match std::fs::metadata(path) {
        Ok(m) => m.len(),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Proxy::Missing,
        Err(e) => return Proxy::Unreadable(e.to_string()),
    };
    match sha256_file(path) {
        Ok(sha) => Proxy::Present {
            size,
            same: placed.map(|p| p.eq_ignore_ascii_case(&sha)),
            zone: zone(path),
        },
        Err(e) => Proxy::Unreadable(e.to_string()),
    }
}

/// The ZoneId of a file's Mark of the Web (3 is the internet).
fn zone(path: &Path) -> Option<u32> {
    if !cfg!(windows) {
        return None;
    }
    let mut stream = path.as_os_str().to_owned();
    stream.push(":Zone.Identifier");
    zone_id(&std::fs::read_to_string(PathBuf::from(stream)).ok()?)
}

fn zone_id(text: &str) -> Option<u32> {
    text.lines()
        .find_map(|l| l.trim().strip_prefix("ZoneId=")?.trim().parse().ok())
}

/// The pid in the offline play stub's last "game created (pid N)" line.
pub fn created_pid(log: &str) -> Option<u32> {
    log.lines().rev().find_map(|l| {
        let rest = &l[l.find("game created (pid ")? + "game created (pid ".len()..];
        rest[..rest.find(')')?].trim().parse().ok()
    })
}

fn norm(path: &str) -> String {
    let p = path.strip_prefix(r"\\?\").unwrap_or(path);
    if p.as_bytes().get(1) == Some(&b':') || p.starts_with('\\') {
        p.replace('/', "\\").trim_end_matches('\\').to_lowercase()
    } else {
        p.trim_end_matches('/').to_owned()
    }
}

/// Whether `path` is inside `dir`, whatever its case and slashes.
fn under(path: &str, dir: &str) -> bool {
    let (p, d) = (norm(path), norm(dir));
    let sep = if d.contains('\\') { '\\' } else { '/' };
    !d.is_empty() && p.len() > d.len() && p.starts_with(&d) && p[d.len()..].starts_with(sep)
}

fn file_name(path: &str) -> &str {
    path.rsplit(['\\', '/']).next().unwrap_or(path)
}

/// Whether `path` is a file directly in `dir`.
fn in_folder(path: &str, dir: &Path) -> bool {
    let cut = path.rfind(['\\', '/']).unwrap_or(0);
    same_folder(Path::new(&path[..cut]), dir)
}

/// A path with the user's home folder shortened, so no user name is logged.
fn tidy(path: &str) -> String {
    let home = std::env::var(if cfg!(windows) { "USERPROFILE" } else { "HOME" }).ok();
    tidy_with(path, home.as_deref())
}

fn tidy_with(path: &str, home: Option<&str>) -> String {
    let short = if cfg!(windows) { "%USERPROFILE%" } else { "~" };
    let p = path.strip_prefix(r"\\?\").unwrap_or(path);
    let Some(h) = home
        .map(|h| h.trim_end_matches(['\\', '/']))
        .filter(|h| h.len() > 3)
    else {
        return path.to_owned();
    };
    let slashes = |s: &str| s.replace('/', "\\");
    match p.get(..h.len()).zip(p.get(h.len()..)) {
        Some((head, rest))
            if slashes(head).eq_ignore_ascii_case(&slashes(h)) && rest.starts_with(['\\', '/']) =>
        {
            format!("{short}{rest}")
        }
        _ => path.to_owned(),
    }
}

/// Windows' compatibility shim DLLs. apphelp.dll is left out: most processes
/// load it whether a shim applies or not.
const SHIMS: &[&str] = &[
    "AcLayers.dll",
    "AcGenral.dll",
    "AcSpecfc.dll",
    "AcXtrnal.dll",
    "AcWinRT.dll",
];

/// Programs that hook into games, by the start of their DLL's name.
const HOOKS: &[(&str, &str)] = &[
    ("medal-hook", "Medal"),
    ("rtsshooks", "RivaTuner Statistics Server"),
    ("discordhook", "Discord's overlay"),
    ("graphics-hook", "OBS Studio"),
    ("windhawk", "Windhawk"),
];

/// Exploit protection's program settings among the Image File Execution
/// Options value names. MaxLoaderThreads alone is common and changes nothing.
const EXPLOIT_PROTECTION: &[&str] = &["MitigationOptions", "MitigationAuditOptions", "EAFModules"];

/// The version.dll files and outside modules among a process's module paths.
pub fn modules(paths: &[String], game: &Path, windows: Option<&str>) -> Modules {
    let game = game.to_string_lossy();
    let mut others: Vec<String> = paths
        .iter()
        .filter(|p| !under(p, &game) && !windows.is_some_and(|w| under(p, w)))
        .map(|p| file_name(p).to_owned())
        .collect();
    others.sort_by_key(|n| n.to_lowercase());
    others.dedup_by(|a, b| a.eq_ignore_ascii_case(b));
    others.truncate(MAX_OTHERS);
    let mut shims: Vec<String> = Vec::new();
    for p in paths {
        let name = file_name(p);
        if SHIMS.iter().any(|s| s.eq_ignore_ascii_case(name))
            && !shims.iter().any(|s| s.eq_ignore_ascii_case(name))
        {
            shims.push(name.to_owned());
        }
    }
    Modules {
        shims,
        count: paths.len(),
        version: paths
            .iter()
            .filter(|p| file_name(p).eq_ignore_ascii_case(PROXY))
            .cloned()
            .collect(),
        others,
    }
}

/// The programs that hook into games among a process's outside modules.
fn hooks(others: &[String]) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    for m in others {
        let m = m.to_ascii_lowercase();
        if let Some((_, product)) = HOOKS.iter().find(|(prefix, _)| m.starts_with(prefix))
            && !out.iter().any(|p| p == product)
        {
            out.push((*product).to_owned());
        }
    }
    out
}

/// The DLLs a Wine process has mapped, from `/proc/<pid>/maps`.
pub fn mapped_modules(maps: &str, game: &Path) -> Modules {
    let mut paths: Vec<String> = Vec::new();
    for line in maps.lines() {
        let Some(at) = line.find('/') else { continue };
        let path = line[at..].trim_end_matches(" (deleted)");
        if path.to_ascii_lowercase().ends_with(".dll") && !paths.iter().any(|p| p == path) {
            paths.push(path.to_owned());
        }
    }
    Modules {
        others: Vec::new(),
        shims: Vec::new(),
        ..modules(&paths, game, None)
    }
}

/// The Code Integrity or Defender events in `wevtutil qe /f:xml` output that
/// name version.dll, F1_25.exe or the game folder.
pub fn events(xml: &str, game: &Path) -> Vec<Event> {
    let folder = norm(&game.to_string_lossy()).to_lowercase();
    let tail = folder
        .split_once(':')
        .map_or(folder.as_str(), |(_, t)| t)
        .to_owned();
    xml.split("<Event xmlns")
        .skip(1)
        .filter_map(|chunk| {
            let lower = chunk.to_lowercase();
            let proxy = lower.contains(&PROXY.to_lowercase());
            let game = lower.contains(&GAME_EXE.to_lowercase()) || (tail.len() > 1 && lower.contains(&tail));
            if !proxy && !game {
                return None;
            }
            let id = tag_text(chunk, "EventID")?.trim().parse().ok()?;
            let data = data_fields(chunk);
            let field = |names: &[&str]| {
                data.iter()
                    .find(|(n, v)| names.contains(&n.as_str()) && !v.is_empty())
                    .map(|(_, v)| v.clone())
            };
            Some(Event {
                id,
                at: attr(chunk, "SystemTime").and_then(clock),
                file: field(&["filename", "filenamebuffer", "path"]),
                process: field(&["processname", "processnamebuffer"]),
                policy: field(&["policyname"]),
                threat: field(&["threatname"]),
                proxy,
            })
        })
        .collect()
}

fn tag_text<'a>(xml: &'a str, tag: &str) -> Option<&'a str> {
    let open = xml.find(&format!("<{tag}"))?;
    let body = &xml[open + tag.len() + 1..];
    let body = &body[body.find('>')? + 1..];
    Some(&body[..body.find('<')?])
}

fn attr<'a>(xml: &'a str, name: &str) -> Option<&'a str> {
    let rest = &xml[xml.find(&format!("{name}="))? + name.len() + 1..];
    let quote = rest.chars().next().filter(|c| *c == '\'' || *c == '"')?;
    let rest = &rest[1..];
    Some(&rest[..rest.find(quote)?])
}

fn clock(stamp: &str) -> Option<String> {
    let time = stamp.split_once('T')?.1.get(..8)?;
    Some(format!("{time} UTC"))
}

/// The `<Data Name='…'>…</Data>` pairs, names lowercased without spaces.
fn data_fields(xml: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    let mut rest = xml;
    while let Some(at) = rest.find("<Data Name=") {
        rest = &rest[at + "<Data Name=".len()..];
        let Some(quote) = rest.chars().next().filter(|c| *c == '\'' || *c == '"') else {
            continue;
        };
        let Some(end) = rest[1..].find(quote) else { break };
        let name: String = rest[1..1 + end]
            .chars()
            .filter(|c| !c.is_whitespace())
            .flat_map(char::to_lowercase)
            .collect();
        rest = &rest[end + 2..];
        let value = if rest.trim_start().starts_with("/>") {
            String::new()
        } else {
            let Some(open) = rest.find('>') else { break };
            let body = &rest[open + 1..];
            let Some(close) = body.find("</Data>") else { break };
            unescape(&body[..close])
        };
        out.push((name, value));
    }
    out
}

fn unescape(s: &str) -> String {
    s.replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&apos;", "'")
        .replace("&amp;", "&")
}

/// The registered antivirus products as `name (on)`/`name (off)`, from a
/// product state (WSC_SECURITY_PRODUCT_STATE in bits 12-15).
pub fn antivirus_name(name: &str, state: Option<u32>) -> String {
    match state {
        Some(s) if s & 0x1000 != 0 => format!("{name} (on)"),
        Some(_) => format!("{name} (off)"),
        None => name.to_owned(),
    }
}

impl Findings {
    /// What kept version.dll out of the game, from the strongest evidence:
    /// a blocking event about it, the game's DLL policy, KnownDLLs, another
    /// copy of it in the game, then Smart App Control being on.
    pub fn cause(&self, game: &Path) -> Option<Cause> {
        let sac = matches!(self.smart_app_control, Some(Ok(Some(1))));
        let blocked = self
            .logs
            .iter()
            .filter_map(|l| l.read.as_ref().ok())
            .flatten()
            .find(|e| e.proxy && e.blocks());
        if let Some(e) = blocked {
            return Some(match e.id {
                SIGNING_LEVEL => Cause::SignedOnly,
                POLICY_BLOCK => match &e.policy {
                    Some(p) if p.to_ascii_lowercase().contains("verifiedandreputable") => {
                        Cause::SmartAppControl { seen: true }
                    }
                    None if sac => Cause::SmartAppControl { seen: true },
                    name => Cause::Policy { name: name.clone() },
                },
                _ => Cause::Defender,
            });
        }
        let policy = |g: &Seen| match &g.mitigation {
            Some(Ok(m)) => *m,
            _ => Mitigation::default(),
        };
        if self
            .games
            .iter()
            .any(|g| policy(g).microsoft_signed || policy(g).store_signed)
        {
            return Some(Cause::SignedOnly);
        }
        if matches!(self.search, Some(Ok(Search { known_dll: true, .. }))) {
            return Some(Cause::KnownDll);
        }
        for g in &self.games {
            let Ok(m) = &g.modules else { continue };
            if m.version.iter().any(|p| in_folder(p, game)) {
                continue;
            }
            let Some(path) = m.version.first().map(|p| tidy(p)) else {
                continue;
            };
            if policy(g).prefer_system32 {
                return Some(Cause::OtherCopy {
                    path,
                    prefer_system32: true,
                });
            }
            if self.exploit_protection() {
                return Some(Cause::ExploitProtection { path });
            }
            let layers = match &self.layers {
                Some(Ok(l)) => l.clone(),
                _ => Vec::new(),
            };
            if !layers.is_empty() || !m.shims.is_empty() {
                return Some(Cause::CompatLayer {
                    path,
                    from: if layers.is_empty() { self.carrier() } else { None },
                    layers,
                    shims: m.shims.clone(),
                });
            }
            let names = self.third_party_antivirus();
            if !names.is_empty() {
                return Some(Cause::Antivirus { path, names });
            }
            return Some(Cause::Unexplained {
                path,
                hooks: hooks(&m.others),
            });
        }
        if sac {
            return Some(Cause::SmartAppControl { seen: false });
        }
        let unreadable = !self.games.is_empty() && self.games.iter().all(|g| g.modules.is_err());
        (unreadable && self.third_party_antivirus().is_empty()).then_some(Cause::Unreadable)
    }

    /// The program that most likely started the game with compatibility
    /// layers: the first one with a setting of its own, else an EA app that
    /// runs with shims.
    fn carrier(&self) -> Option<Carrier> {
        if let Some(first) = self.starter_layers.iter().flatten().next() {
            let on = |l: &&AdminLayer| l.name == first.name;
            return Some(Carrier {
                name: first.name.clone(),
                exe: Some(first.exe.clone()),
                layers: self
                    .starter_layers
                    .iter()
                    .flatten()
                    .filter(on)
                    .map(|l| format!("{} ({})", l.layers.trim(), l.scope()))
                    .collect(),
            });
        }
        self.ea_app
            .iter()
            .find(|a| matches!(&a.shims, Ok(s) if !s.is_empty()))
            .map(|a| Carrier {
                name: crate::ea_app::PROCESS.to_owned(),
                exe: a.image.as_deref().map(tidy),
                layers: Vec::new(),
            })
    }

    fn exploit_protection(&self) -> bool {
        matches!(&self.exec_options, Some(Ok(names)) if names.iter().any(|n| {
            let value = n.rsplit('\\').next().unwrap_or(n);
            EXPLOIT_PROTECTION.iter().any(|k| value.eq_ignore_ascii_case(k))
        }))
    }

    /// The antivirus products registered with Windows and on, other than
    /// Microsoft Defender.
    fn third_party_antivirus(&self) -> Vec<String> {
        let Some(Ok(listed)) = &self.antivirus else {
            return Vec::new();
        };
        let mut out: Vec<String> = Vec::new();
        for entry in listed {
            let Some(name) = entry.strip_suffix(" (on)") else {
                continue;
            };
            if !name.contains("Microsoft Defender")
                && !name.contains("Windows Defender")
                && !out.iter().any(|n| n == name)
            {
                out.push(name.to_owned());
            }
        }
        out
    }

    /// The findings as log lines.
    pub fn lines(&self, game: &Path) -> Vec<String> {
        let mut out = Vec::new();
        out.push(match &self.proxy {
            Proxy::Missing => format!("{PROXY}: gone from the game folder"),
            Proxy::Present { size, same, zone } => format!(
                "{PROXY}: in the game folder, {size} bytes, {}{}",
                match same {
                    Some(true) => "the bytes the launcher placed",
                    Some(false) => "NOT the bytes the launcher placed",
                    None => "no record of what the launcher placed",
                },
                zone.map(|z| format!(", marked as downloaded (Zone.Identifier ZoneId={z})"))
                    .unwrap_or_default()
            ),
            Proxy::Unreadable(e) => format!("{PROXY}: in the game folder but unreadable ({e})"),
        });
        if self.rotated {
            out.push(
                "loader.prev.log was written after the game started: the loader ran and moved its log aside"
                    .into(),
            );
        }
        if self.games.is_empty() {
            out.push(format!("game: no {GAME_EXE} is running"));
        }
        if let Some(pid) = self.created.filter(|p| !self.games.iter().any(|g| g.pid == *p)) {
            out.push(format!(
                "game: the offline play stub created pid {pid}, which is not running"
            ));
        }
        for g in &self.games {
            out.extend(self.game_lines(g, game));
        }
        match &self.search {
            Some(Ok(s)) => out.push(format!(
                "DLL search: {PROXY} is {}a KnownDLL, SafeDllSearchMode {}",
                if s.known_dll { "" } else { "not " },
                s.safe_search.map_or("default (on)".into(), |v| v.to_string())
            )),
            Some(Err(e)) => out.push(format!("DLL search: not readable ({e})")),
            None => {}
        }
        match &self.exec_options {
            Some(Ok(names)) if names.is_empty() => {
                out.push(format!("Image File Execution Options for {GAME_EXE}: none"))
            }
            Some(Ok(names)) => out.push(format!(
                "Image File Execution Options for {GAME_EXE}: {}",
                names.join(", ")
            )),
            Some(Err(e)) => out.push(format!(
                "Image File Execution Options for {GAME_EXE}: not readable ({e})"
            )),
            None => {}
        }
        match &self.layers {
            Some(Ok(layers)) if layers.is_empty() => {
                out.push(format!("compatibility settings on {GAME_EXE}: none"))
            }
            Some(Ok(layers)) => out.push(format!(
                "compatibility settings on {GAME_EXE}: {}",
                layers.join(", ")
            )),
            Some(Err(e)) => out.push(format!(
                "compatibility settings on {GAME_EXE}: not readable ({e})"
            )),
            None => {}
        }
        match &self.starter_layers {
            Some(found) if found.is_empty() => out.push(format!(
                "compatibility settings on {} and the offline play stub: none",
                crate::compat::STARTERS.join(", ")
            )),
            Some(found) => out.extend(found.iter().map(|l| {
                format!(
                    "compatibility settings on {}: {} ({}) on {}",
                    l.name,
                    l.layers.trim(),
                    l.scope(),
                    l.exe
                )
            })),
            None => {}
        }
        for a in &self.ea_app {
            let pid = a.pid;
            let name = crate::ea_app::PROCESS;
            out.push(match &a.shims {
                Ok(s) if s.is_empty() => format!("{name} pid {pid}: no Windows compatibility shims"),
                Ok(s) => format!(
                    "{name} pid {pid}: Windows compatibility shims loaded: {}",
                    s.join(", ")
                ),
                Err(e) => format!("{name} pid {pid}: modules not readable ({e})"),
            });
        }
        match &self.smart_app_control {
            Some(Ok(state)) => out.push(format!(
                "Smart App Control: {}",
                match state {
                    None => "not set".into(),
                    Some(0) => "off".into(),
                    Some(1) => "on".into(),
                    Some(2) => "evaluation".into(),
                    Some(n) => format!("state {n}"),
                }
            )),
            Some(Err(e)) => out.push(format!("Smart App Control: not readable ({e})")),
            None => {}
        }
        for log in &self.logs {
            match &log.read {
                Ok(events) if events.is_empty() => out.push(format!(
                    "{} log: nothing about {PROXY} or {GAME_EXE} since the game started",
                    log.name
                )),
                Ok(events) => {
                    out.push(format!(
                        "{} log: {} event(s) about {PROXY} or {GAME_EXE} since the game started",
                        log.name,
                        events.len()
                    ));
                    for e in events.iter().take(MAX_EVENTS) {
                        out.push(format!(
                            "  {} {}{}: {}{}: {}{}{}",
                            log.name,
                            e.id,
                            e.at.as_ref().map(|t| format!(" at {t}")).unwrap_or_default(),
                            e.file.as_deref().map(tidy).unwrap_or_else(|| "?".into()),
                            e.process
                                .as_deref()
                                .map(|p| format!(" in {}", file_name(p)))
                                .unwrap_or_default(),
                            e.what(),
                            e.policy.as_ref().map(|p| format!(" ({p})")).unwrap_or_default(),
                            e.threat.as_ref().map(|t| format!(" ({t})")).unwrap_or_default(),
                        ));
                    }
                }
                Err(e) => out.push(format!("{} log: not readable ({e})", log.name)),
            }
        }
        match &self.antivirus {
            Some(Ok(names)) if names.is_empty() => out.push("antivirus registered with Windows: none".into()),
            Some(Ok(names)) => out.push(format!("antivirus registered with Windows: {}", names.join(", "))),
            Some(Err(e)) => out.push(format!("antivirus registered with Windows: not readable ({e})")),
            None => {}
        }
        match self.wine_override {
            Some(true) => out.push(format!("Proton prefix: user.reg has the {PROXY} override")),
            Some(false) => out.push(format!(
                "Proton prefix: user.reg lacks the {PROXY} override, so Wine uses its builtin {PROXY}"
            )),
            None => {}
        }
        out
    }

    fn game_lines(&self, g: &Seen, game: &Path) -> Vec<String> {
        let mut out = Vec::new();
        let pid = g.pid;
        out.push(format!(
            "game pid {pid}{}: {}{}, {}",
            if self.created == Some(pid) {
                " (the one the offline play stub created)"
            } else {
                ""
            },
            g.image
                .as_deref()
                .map(tidy)
                .unwrap_or_else(|| "exe path unknown".into()),
            g.image_size.map(|s| format!(" ({s} bytes)")).unwrap_or_default(),
            match (g.ours, self.exe_size) {
                (true, _) => format!("the game folder's {GAME_EXE}"),
                (false, Some(s)) => format!("NOT the game folder's {GAME_EXE} ({s} bytes)"),
                (false, None) => format!("NOT the game folder's {GAME_EXE}"),
            }
        ));
        match &g.modules {
            Ok(m) => {
                let (ours, others): (Vec<&String>, Vec<&String>) =
                    m.version.iter().partition(|p| in_folder(p, game));
                let mut held: Vec<String> = Vec::new();
                if !ours.is_empty() {
                    held.push("the game folder's (the loader)".into());
                }
                held.extend(others.iter().map(|p| tidy(p)));
                out.push(format!(
                    "pid {pid}: {} modules, {PROXY} from {}",
                    m.count,
                    if held.is_empty() {
                        "nowhere (none loaded)".into()
                    } else {
                        held.join(" and ")
                    }
                ));
                if !m.others.is_empty() {
                    out.push(format!(
                        "pid {pid}: modules from outside the game and Windows folders: {}",
                        m.others.join(", ")
                    ));
                }
                if !m.shims.is_empty() {
                    out.push(format!(
                        "pid {pid}: Windows compatibility shims loaded: {}",
                        m.shims.join(", ")
                    ));
                }
            }
            Err(e) => out.push(format!("pid {pid}: modules not readable ({e})")),
        }
        match &g.mitigation {
            Some(Ok(m)) => {
                let names = m.names();
                out.push(format!(
                    "pid {pid}: DLL load policies: {}",
                    if names.is_empty() {
                        "none".into()
                    } else {
                        names.join(", ")
                    }
                ));
            }
            Some(Err(e)) => out.push(format!("pid {pid}: DLL load policies not readable ({e})")),
            None => {}
        }
        out
    }
}

fn seen_base(g: &GameProcess, game: &Path, modules: Probe<Modules>) -> Seen {
    let image = g.origin.image.clone();
    let image_size = image
        .as_deref()
        .and_then(|p| std::fs::metadata(p.strip_prefix(r"\\?\").unwrap_or(p)).ok())
        .map(|m| m.len());
    Seen {
        pid: g.pid,
        image,
        image_size,
        ours: g.is_in(game),
        modules,
        mitigation: None,
    }
}

#[cfg(target_os = "linux")]
mod platform {
    use super::*;

    pub fn fill(f: &mut Findings, game: &Path, running: &[GameProcess], _: SystemTime) {
        f.games = running
            .iter()
            .take(MAX_GAMES)
            .map(|g| {
                let maps = std::fs::read_to_string(format!("/proc/{}/maps", g.pid))
                    .map(|m| mapped_modules(&m, game))
                    .map_err(|e| e.to_string());
                seen_base(g, game, maps)
            })
            .collect();
    }
}

#[cfg(not(any(target_os = "linux", windows)))]
mod platform {
    use super::*;

    pub fn fill(_: &mut Findings, _: &Path, _: &[GameProcess], _: SystemTime) {}
}

#[cfg(windows)]
mod platform {
    use super::*;
    use crate::compat::windows_tool_within;

    const SESSION_MANAGER: &str = r"SYSTEM\CurrentControlSet\Control\Session Manager";
    const EXEC_OPTIONS: &str =
        r"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\F1_25.exe";
    const CI_POLICY: &str = r"SYSTEM\CurrentControlSet\Control\CI\Policy";
    const ANTIVIRUS: &str = r"SOFTWARE\Microsoft\Security Center\Provider\Av";

    pub fn fill(f: &mut Findings, game: &Path, running: &[GameProcess], since: SystemTime) {
        std::thread::scope(|s| {
            let ci = s.spawn(|| read_log(CI_LOG, CI_IDS, since, game));
            let defender = s.spawn(|| read_log(DEFENDER_LOG, DEFENDER_IDS, since, game));
            let antivirus = s.spawn(antivirus);
            let windows = std::env::var("SystemRoot").ok();
            f.games = running
                .iter()
                .take(MAX_GAMES)
                .map(|g| seen(g, game, windows.as_deref()))
                .collect();
            f.search = Some(search());
            f.exec_options = Some(exec_options());
            f.layers = Some(Ok(crate::compat::layers(&game.join(GAME_EXE))
                .into_iter()
                .map(|l| format!("{} ({})", l.layers.trim(), l.scope()))
                .collect()));
            f.starter_layers = Some(crate::compat::starter_layers(game));
            f.ea_app = crate::process::named(crate::ea_app::PROCESS)
                .iter()
                .take(MAX_GAMES)
                .map(|a| ea_app(a.pid, game, windows.as_deref()))
                .collect();
            f.smart_app_control = Some(smart_app_control());
            let joined = |h: std::thread::ScopedJoinHandle<'_, Probe<Vec<Event>>>| {
                h.join().unwrap_or_else(|_| Err("the check failed".into()))
            };
            f.logs = vec![
                EventLog {
                    name: "Code Integrity",
                    read: joined(ci),
                },
                EventLog {
                    name: "Defender",
                    read: joined(defender),
                },
            ];
            f.antivirus = Some(
                antivirus
                    .join()
                    .unwrap_or_else(|_| Err("the check failed".into())),
            );
        });
    }

    fn error(code: i32) -> String {
        match code {
            2 => "not found".into(),
            5 => "access denied".into(),
            299 => "only part of it could be read".into(),
            n => format!("Windows error {n}"),
        }
    }

    fn seen(g: &GameProcess, game: &Path, windows: Option<&str>) -> Seen {
        let (modules, mitigation) = match win32::Process::open(g.pid) {
            Ok(p) => (
                p.modules()
                    .map(|paths| modules(&paths, game, windows))
                    .map_err(error),
                p.mitigation().map_err(error),
            ),
            Err(code) => (Err(error(code)), Err(error(code))),
        };
        Seen {
            mitigation: Some(mitigation),
            ..seen_base(g, game, modules)
        }
    }

    fn ea_app(pid: u32, game: &Path, windows: Option<&str>) -> EaApp {
        let paths = win32::Process::open(pid).and_then(|p| p.modules()).map_err(error);
        EaApp {
            pid,
            image: paths.as_ref().ok().and_then(|p| p.first().cloned()),
            shims: paths.map(|p| modules(&p, game, windows).shims),
        }
    }

    fn search() -> Probe<Search> {
        let known = registry::Key::open(&format!(r"{SESSION_MANAGER}\KnownDLLs")).map_err(error)?;
        let known_dll = known.is_some_and(|k| {
            k.values()
                .iter()
                .any(|(_, v)| matches!(v, registry::Value::Text(t) if t.eq_ignore_ascii_case(PROXY)))
        });
        let safe_search = registry::Key::open(SESSION_MANAGER)
            .map_err(error)?
            .and_then(|k| k.dword("SafeDllSearchMode"));
        Ok(Search {
            known_dll,
            safe_search,
        })
    }

    fn exec_options() -> Probe<Vec<String>> {
        let Some(key) = registry::Key::open(EXEC_OPTIONS).map_err(error)? else {
            return Ok(Vec::new());
        };
        let mut names: Vec<String> = key.values().into_iter().map(|(n, _)| n).collect();
        for sub in key.subkeys() {
            if let Ok(Some(k)) = registry::Key::open(&format!(r"{EXEC_OPTIONS}\{sub}")) {
                names.extend(k.values().into_iter().map(|(n, _)| format!(r"{sub}\{n}")));
            }
        }
        Ok(names)
    }

    fn smart_app_control() -> Probe<Option<u32>> {
        Ok(registry::Key::open(CI_POLICY)
            .map_err(error)?
            .and_then(|k| k.dword("VerifiedAndReputablePolicyState")))
    }

    fn antivirus() -> Probe<Vec<String>> {
        let listed: Vec<String> = match registry::Key::open(ANTIVIRUS) {
            Ok(Some(key)) => key
                .subkeys()
                .iter()
                .filter_map(|sub| {
                    let k = registry::Key::open(&format!(r"{ANTIVIRUS}\{sub}")).ok()??;
                    let name = k.text("DISPLAYNAME")?;
                    Some(antivirus_name(&name, k.dword("STATE")))
                })
                .collect(),
            _ => Vec::new(),
        };
        if !listed.is_empty() {
            return Ok(listed);
        }
        let out = windows_tool_within(
            "powershell",
            &[
                "-NoProfile",
                "-NonInteractive",
                "-Command",
                "Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct | \
                 ForEach-Object { $_.displayName + '|' + $_.productState }",
            ],
            TOOL_WAIT,
        )?;
        Ok(out
            .lines()
            .filter_map(|l| {
                let (name, state) = l.trim().rsplit_once('|')?;
                Some(antivirus_name(name.trim(), state.trim().parse().ok()))
            })
            .collect())
    }

    fn read_log(log: &str, ids: &[u32], since: SystemTime, game: &Path) -> Probe<Vec<Event>> {
        let window = since.elapsed().unwrap_or_default() + EVENT_MARGIN;
        let ids = ids
            .iter()
            .map(|i| format!("EventID={i}"))
            .collect::<Vec<_>>()
            .join(" or ");
        let query = format!(
            "/q:*[System[({ids}) and TimeCreated[timediff(@SystemTime) <= {}]]]",
            window.as_millis()
        );
        let out = windows_tool_within(
            "wevtutil",
            &["qe", log, &query, "/f:xml", "/rd:true", "/c:50"],
            TOOL_WAIT,
        )?;
        Ok(events(&out, game))
    }

    mod win32 {
        use std::ffi::c_void;

        const PROCESS_QUERY_INFORMATION: u32 = 0x0400;
        const PROCESS_VM_READ: u32 = 0x0010;
        const LIST_MODULES_ALL: u32 = 0x03;
        const PROCESS_SIGNATURE_POLICY: i32 = 8;
        const PROCESS_IMAGE_LOAD_POLICY: i32 = 10;
        const ERROR_PARTIAL_COPY: i32 = 299;

        #[link(name = "kernel32")]
        unsafe extern "system" {
            fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
            fn CloseHandle(handle: *mut c_void) -> i32;
            fn K32EnumProcessModulesEx(
                process: *mut c_void,
                modules: *mut *mut c_void,
                size: u32,
                needed: *mut u32,
                filter: u32,
            ) -> i32;
            fn K32GetModuleFileNameExW(
                process: *mut c_void,
                module: *mut c_void,
                name: *mut u16,
                size: u32,
            ) -> u32;
            fn GetProcessMitigationPolicy(
                process: *mut c_void,
                policy: i32,
                buffer: *mut c_void,
                size: usize,
            ) -> i32;
        }

        fn last_error() -> i32 {
            std::io::Error::last_os_error().raw_os_error().unwrap_or(0)
        }

        pub struct Process(*mut c_void);

        impl Process {
            pub fn open(pid: u32) -> Result<Self, i32> {
                // SAFETY: OpenProcess takes no pointers; a null handle is
                // the failure, read before any other call.
                let handle = unsafe { OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 0, pid) };
                if handle.is_null() {
                    return Err(last_error());
                }
                Ok(Self(handle))
            }

            /// The full paths of the modules loaded in the process. A
            /// process still mapping its modules can give a partial copy,
            /// which a second look a moment later gets past.
            pub fn modules(&self) -> Result<Vec<String>, i32> {
                let mut tries = 0;
                loop {
                    match self.module_handles() {
                        Err(ERROR_PARTIAL_COPY) if tries < 3 => {
                            tries += 1;
                            std::thread::sleep(std::time::Duration::from_millis(50));
                        }
                        Err(e) => return Err(e),
                        Ok(handles) => {
                            return Ok(handles.into_iter().filter_map(|m| self.module_path(m)).collect());
                        }
                    }
                }
            }

            fn module_handles(&self) -> Result<Vec<*mut c_void>, i32> {
                let mut handles: Vec<*mut c_void> = vec![std::ptr::null_mut(); 1024];
                loop {
                    let size = (handles.len() * std::mem::size_of::<*mut c_void>()) as u32;
                    let mut needed = 0u32;
                    // SAFETY: `handles` holds `size` bytes of module slots,
                    // and `needed` is a valid out pointer.
                    let ok = unsafe {
                        K32EnumProcessModulesEx(
                            self.0,
                            handles.as_mut_ptr(),
                            size,
                            &mut needed,
                            LIST_MODULES_ALL,
                        )
                    };
                    if ok == 0 {
                        return Err(last_error());
                    }
                    let count = needed as usize / std::mem::size_of::<*mut c_void>();
                    if needed <= size {
                        handles.truncate(count);
                        return Ok(handles);
                    }
                    handles.resize(count + 64, std::ptr::null_mut());
                }
            }

            fn module_path(&self, module: *mut c_void) -> Option<String> {
                let mut name = vec![0u16; 32_768];
                // SAFETY: `name` holds the u16 count passed, and the call
                // writes at most that many including the NUL.
                let len =
                    unsafe { K32GetModuleFileNameExW(self.0, module, name.as_mut_ptr(), name.len() as u32) };
                (len > 0).then(|| String::from_utf16_lossy(&name[..len as usize]))
            }

            pub fn mitigation(&self) -> Result<super::Mitigation, i32> {
                let flags = |policy: i32| -> Result<u32, i32> {
                    let mut flags = 0u32;
                    // SAFETY: both policies fill a single DWORD of flags,
                    // the size passed.
                    let ok = unsafe {
                        GetProcessMitigationPolicy(self.0, policy, (&mut flags as *mut u32).cast(), 4)
                    };
                    if ok == 0 { Err(last_error()) } else { Ok(flags) }
                };
                let signature = flags(PROCESS_SIGNATURE_POLICY)?;
                let image = flags(PROCESS_IMAGE_LOAD_POLICY)?;
                Ok(super::Mitigation {
                    microsoft_signed: signature & 0x1 != 0,
                    store_signed: signature & 0x2 != 0,
                    no_remote: image & 0x1 != 0,
                    no_low_label: image & 0x2 != 0,
                    prefer_system32: image & 0x4 != 0,
                })
            }
        }

        impl Drop for Process {
            fn drop(&mut self) {
                // SAFETY: the handle came from OpenProcess and is closed once.
                unsafe { CloseHandle(self.0) };
            }
        }
    }

    /// Read-only access to HKEY_LOCAL_MACHINE (the 64-bit view).
    mod registry {
        use std::ffi::c_void;

        const HKEY_LOCAL_MACHINE: isize = 0x8000_0002u32 as i32 as isize;
        const KEY_READ: u32 = 0x20019;
        const KEY_WOW64_64KEY: u32 = 0x0100;
        const ERROR_FILE_NOT_FOUND: i32 = 2;
        const REG_SZ: u32 = 1;
        const REG_EXPAND_SZ: u32 = 2;
        const REG_DWORD: u32 = 4;

        #[link(name = "advapi32")]
        unsafe extern "system" {
            fn RegOpenKeyExW(
                key: *mut c_void,
                sub: *const u16,
                options: u32,
                access: u32,
                out: *mut *mut c_void,
            ) -> i32;
            fn RegCloseKey(key: *mut c_void) -> i32;
            fn RegEnumValueW(
                key: *mut c_void,
                index: u32,
                name: *mut u16,
                name_len: *mut u32,
                reserved: *mut u32,
                kind: *mut u32,
                data: *mut u8,
                data_len: *mut u32,
            ) -> i32;
            fn RegEnumKeyExW(
                key: *mut c_void,
                index: u32,
                name: *mut u16,
                name_len: *mut u32,
                reserved: *mut u32,
                class: *mut u16,
                class_len: *mut u32,
                written: *mut u64,
            ) -> i32;
        }

        pub enum Value {
            Text(String),
            Dword(u32),
            Other,
        }

        pub struct Key(*mut c_void);

        impl Key {
            /// The key under HKEY_LOCAL_MACHINE, None when it does not exist.
            pub fn open(path: &str) -> Result<Option<Self>, i32> {
                let wide: Vec<u16> = path.encode_utf16().chain([0]).collect();
                let mut key = std::ptr::null_mut();
                // SAFETY: `wide` is NUL-terminated and `key` a valid out
                // pointer; the predefined root needs no opening.
                let status = unsafe {
                    RegOpenKeyExW(
                        HKEY_LOCAL_MACHINE as *mut c_void,
                        wide.as_ptr(),
                        0,
                        KEY_READ | KEY_WOW64_64KEY,
                        &mut key,
                    )
                };
                match status {
                    0 => Ok(Some(Self(key))),
                    ERROR_FILE_NOT_FOUND => Ok(None),
                    e => Err(e),
                }
            }

            pub fn values(&self) -> Vec<(String, Value)> {
                let mut out = Vec::new();
                let mut name = vec![0u16; 16_384];
                let mut data = vec![0u8; 1 << 16];
                for index in 0.. {
                    let mut name_len = name.len() as u32;
                    let mut data_len = data.len() as u32;
                    let mut kind = 0u32;
                    // SAFETY: the buffers hold the lengths passed, which the
                    // call updates to what it wrote.
                    let status = unsafe {
                        RegEnumValueW(
                            self.0,
                            index,
                            name.as_mut_ptr(),
                            &mut name_len,
                            std::ptr::null_mut(),
                            &mut kind,
                            data.as_mut_ptr(),
                            &mut data_len,
                        )
                    };
                    if status != 0 {
                        break;
                    }
                    let bytes = &data[..data_len as usize];
                    let value = match kind {
                        REG_SZ | REG_EXPAND_SZ => {
                            let wide: Vec<u16> = bytes
                                .chunks_exact(2)
                                .map(|c| u16::from_le_bytes([c[0], c[1]]))
                                .take_while(|&c| c != 0)
                                .collect();
                            Value::Text(String::from_utf16_lossy(&wide))
                        }
                        REG_DWORD if bytes.len() >= 4 => {
                            Value::Dword(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
                        }
                        _ => Value::Other,
                    };
                    out.push((String::from_utf16_lossy(&name[..name_len as usize]), value));
                }
                out
            }

            pub fn subkeys(&self) -> Vec<String> {
                let mut out = Vec::new();
                let mut name = vec![0u16; 256];
                for index in 0.. {
                    let mut len = name.len() as u32;
                    // SAFETY: `name` holds `len` u16s; the class and time
                    // out pointers may be null.
                    let status = unsafe {
                        RegEnumKeyExW(
                            self.0,
                            index,
                            name.as_mut_ptr(),
                            &mut len,
                            std::ptr::null_mut(),
                            std::ptr::null_mut(),
                            std::ptr::null_mut(),
                            std::ptr::null_mut(),
                        )
                    };
                    if status != 0 {
                        break;
                    }
                    out.push(String::from_utf16_lossy(&name[..len as usize]));
                }
                out
            }

            pub fn dword(&self, name: &str) -> Option<u32> {
                self.values().into_iter().find_map(|(n, v)| match v {
                    Value::Dword(d) if n.eq_ignore_ascii_case(name) => Some(d),
                    _ => None,
                })
            }

            pub fn text(&self, name: &str) -> Option<String> {
                self.values().into_iter().find_map(|(n, v)| match v {
                    Value::Text(t) if n.eq_ignore_ascii_case(name) => Some(t),
                    _ => None,
                })
            }
        }

        impl Drop for Key {
            fn drop(&mut self) {
                // SAFETY: the key came from RegOpenKeyExW and is closed once.
                unsafe { RegCloseKey(self.0) };
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const GAME: &str = r"E:\SteamLibrary\steamapps\common\F1 25";
    const SYSTEM: &str = r"C:\WINDOWS\SYSTEM32\VERSION.dll";

    fn ci(id: u32, file: &str, policy: Option<&str>) -> String {
        format!(
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System>\
             <Provider Name='Microsoft-Windows-CodeIntegrity'/><EventID>{id}</EventID><Level>2</Level>\
             <TimeCreated SystemTime='2026-10-23T16:27:09.6291234Z'/><Computer>MATTHIJS</Computer>\
             <Security UserID='S-1-5-21-1-2-3-1001'/></System><EventData>\
             <Data Name='File Name Length'>63</Data><Data Name='File Name'>{file}</Data>\
             <Data Name='Process Name'>\\Device\\HarddiskVolume3\\SteamLibrary\\steamapps\\common\\F1 25\\F1_25.exe</Data>\
             <Data Name='Requested Signing Level'>8</Data><Data Name='PolicyID'/>{}</EventData></Event>",
            policy
                .map(|p| format!("<Data Name='PolicyName'>{p}</Data>"))
                .unwrap_or_default()
        )
    }

    const PROXY_PATH: &str = r"\Device\HarddiskVolume3\SteamLibrary\steamapps\common\F1 25\version.dll";

    fn game() -> PathBuf {
        PathBuf::from(GAME)
    }

    fn running(version: &[&str], mitigation: Mitigation) -> Seen {
        Seen {
            pid: 27488,
            image: Some(format!(r"{GAME}\F1_25.exe")),
            image_size: Some(423_518_208),
            ours: true,
            modules: Ok(Modules {
                count: 212,
                version: version.iter().map(|v| (*v).to_owned()).collect(),
                others: Vec::new(),
                shims: Vec::new(),
            }),
            mitigation: Some(Ok(mitigation)),
        }
    }

    fn found(events: Vec<Event>, games: Vec<Seen>) -> Findings {
        Findings {
            games,
            logs: vec![EventLog {
                name: "Code Integrity",
                read: Ok(events),
            }],
            search: Some(Ok(Search::default())),
            smart_app_control: Some(Ok(Some(0))),
            ..Findings::default()
        }
    }

    #[test]
    fn reads_the_pid_the_stub_created() {
        let log = "18:27:06.887 [stub] [offline] session launcher invoked, args: <none>\n\
                   18:27:08.903 [stub] [offline] game created (pid 19420)\n\
                   18:27:09.903 [stub] [offline] game created (pid 27488)\n\
                   18:27:09.375 [stub] [offline] image not listed while suspended; resuming\n";
        assert_eq!(created_pid(log), Some(27488));
        assert_eq!(created_pid("nothing here\n"), None);
    }

    #[test]
    fn reads_code_integrity_and_defender_events_about_the_game() {
        let defender = format!(
            "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System><EventID>1116</EventID>\
             <TimeCreated SystemTime=\"2026-10-23T16:27:11.000Z\"/></System><EventData>\
             <Data Name='Threat Name'>Trojan:Win64/Example!MTB</Data>\
             <Data Name='Path'>file:_{GAME}\\version.dll</Data><Data Name='Process Name'>Unknown</Data>\
             <Data Name='Detection User'></Data></EventData></Event>"
        );
        let other = ci(
            3077,
            r"\Device\HarddiskVolume2\Tools\thing.dll",
            Some("Other &amp; Co"),
        )
        .replace("steamapps\\common\\F1 25\\F1_25.exe", "Tools\\thing.exe")
        .replace("SteamLibrary\\", "");
        let xml = format!(
            "{}{other}{}{defender}",
            ci(3077, PROXY_PATH, Some("VerifiedAndReputableDesktop")),
            ci(
                3033,
                r"\Device\HarddiskVolume3\SteamLibrary\steamapps\common\F1 25\swofty\addons\core\swofty_core.dll",
                None
            )
        );
        let got = events(&xml, &game());
        assert_eq!(got.len(), 3, "{got:#?}");
        assert_eq!(
            got[0],
            Event {
                id: 3077,
                at: Some("16:27:09 UTC".into()),
                file: Some(PROXY_PATH.into()),
                process: Some(
                    r"\Device\HarddiskVolume3\SteamLibrary\steamapps\common\F1 25\F1_25.exe".into()
                ),
                policy: Some("VerifiedAndReputableDesktop".into()),
                threat: None,
                proxy: true,
            }
        );
        assert_eq!(got[1].id, 3033);
        assert!(!got[1].proxy && got[1].policy.is_none());
        assert_eq!(got[2].id, 1116);
        assert_eq!(got[2].at.as_deref(), Some("16:27:11 UTC"));
        assert_eq!(got[2].threat.as_deref(), Some("Trojan:Win64/Example!MTB"));
        assert_eq!(got[2].file, Some(format!(r"file:_{GAME}\version.dll")));
        assert!(got[2].proxy && got[2].blocks());
        assert!(events("", &game()).is_empty());
        assert!(
            events(
                "<Event xmlns='x'><System><EventID>nope</EventID>version.dll",
                &game()
            )
            .is_empty()
        );
    }

    #[test]
    fn sorts_a_process_module_list() {
        let paths: Vec<String> = [
            format!(r"{GAME}\F1_25.exe"),
            format!(r"{GAME}\version.dll"),
            format!(r"{GAME}\swofty\addons\core\swofty_core.dll"),
            SYSTEM.to_owned(),
            r"C:\WINDOWS\System32\DriverStore\FileRepository\nv\nvwgf2umx.dll".to_owned(),
            r"C:\Program Files\Avast Software\Avast\aswhook.dll".to_owned(),
            r"C:\Program Files (x86)\Steam\GameOverlayRenderer64.dll".to_owned(),
            r"c:\program files\avast software\avast\ASWHOOK.DLL".to_owned(),
        ]
        .into();
        let m = modules(&paths, &game(), Some(r"C:\Windows"));
        assert_eq!(m.count, 8);
        assert_eq!(m.version, [format!(r"{GAME}\version.dll"), SYSTEM.to_owned()]);
        assert_eq!(m.others, ["aswhook.dll", "GameOverlayRenderer64.dll"]);
    }

    #[test]
    fn reads_the_version_dll_wine_mapped() {
        let maps = "140000000-140001000 r--p 00000000 103:02 1 /games/F1 25/F1_25.exe\n\
                    6ffff0000-6ffff1000 r--p 00000000 103:02 2 /steam/proton/files/lib/wine/x86_64-windows/version.dll\n\
                    6ffff1000-6ffff2000 r-xp 00001000 103:02 2 /steam/proton/files/lib/wine/x86_64-windows/version.dll\n\
                    6fffe0000-6fffe1000 r--p 00000000 103:02 3 /steam/proton/files/lib/wine/x86_64-windows/kernel32.dll\n\
                    7ffe0000-7fff0000 rw-p 00000000 00:00 0 [stack]\n";
        let m = mapped_modules(maps, Path::new("/games/F1 25"));
        assert_eq!(m.count, 2);
        assert_eq!(
            m.version,
            ["/steam/proton/files/lib/wine/x86_64-windows/version.dll"]
        );
        let native = "6ffff0000-6ffff1000 r--p 00000000 103:02 2 /games/F1 25/version.dll\n";
        let m = mapped_modules(native, Path::new("/games/F1 25"));
        assert!(in_folder(&m.version[0], Path::new("/games/F1 25")));
    }

    #[test]
    fn shortens_the_home_folder_in_paths() {
        let short = if cfg!(windows) { "%USERPROFILE%" } else { "~" };
        assert_eq!(
            tidy_with(
                r"C:\Users\Matthijs Prinsen\AppData\Local\x.dll",
                Some(r"C:\Users\Matthijs Prinsen")
            ),
            format!(r"{short}\AppData\Local\x.dll")
        );
        assert_eq!(
            tidy_with(
                r"c:/users/matthijs prinsen/x.dll",
                Some(r"C:\Users\Matthijs Prinsen\")
            ),
            format!("{short}/x.dll")
        );
        assert_eq!(tidy_with(SYSTEM, Some(r"C:\Users\Matthijs Prinsen")), SYSTEM);
        assert_eq!(
            tidy_with(
                r"C:\Users\Matthijs Prinsen2\x.dll",
                Some(r"C:\Users\Matthijs Prinsen")
            ),
            r"C:\Users\Matthijs Prinsen2\x.dll"
        );
        assert_eq!(tidy_with("/home/a/x.dll", None), "/home/a/x.dll");
    }

    #[test]
    fn reads_the_download_mark() {
        assert_eq!(
            zone_id("[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://x\r\n"),
            Some(3)
        );
        assert_eq!(zone_id("[ZoneTransfer]\r\n"), None);
    }

    #[test]
    fn names_the_antivirus_and_whether_it_is_on() {
        assert_eq!(
            antivirus_name("Avast Antivirus", Some(0x41000)),
            "Avast Antivirus (on)"
        );
        assert_eq!(
            antivirus_name("Microsoft Defender Antivirus", Some(0x60100)),
            "Microsoft Defender Antivirus (off)"
        );
        assert_eq!(antivirus_name("Norton", None), "Norton");
    }

    #[test]
    fn a_blocking_event_about_version_dll_names_the_cause() {
        let xml = ci(3077, PROXY_PATH, Some("VerifiedAndReputableDesktop"));
        let f = found(
            events(&xml, &game()),
            vec![running(&[SYSTEM], Mitigation::default())],
        );
        assert_eq!(f.cause(&game()), Some(Cause::SmartAppControl { seen: true }));
        let xml = ci(3077, PROXY_PATH, Some("Contoso Games Policy"));
        let f = found(events(&xml, &game()), Vec::new());
        assert_eq!(
            f.cause(&game()),
            Some(Cause::Policy {
                name: Some("Contoso Games Policy".into())
            })
        );
        let mut f = found(events(&ci(3077, PROXY_PATH, None), &game()), Vec::new());
        assert_eq!(f.cause(&game()), Some(Cause::Policy { name: None }));
        f.smart_app_control = Some(Ok(Some(1)));
        assert_eq!(f.cause(&game()), Some(Cause::SmartAppControl { seen: true }));
        let f = found(events(&ci(3033, PROXY_PATH, None), &game()), Vec::new());
        assert_eq!(f.cause(&game()), Some(Cause::SignedOnly));
        let defender = Event {
            id: 1121,
            file: Some(format!(r"{GAME}\version.dll")),
            proxy: true,
            ..Event::default()
        };
        assert_eq!(
            found(vec![defender], Vec::new()).cause(&game()),
            Some(Cause::Defender)
        );
    }

    #[test]
    fn audits_and_events_about_other_files_name_no_cause() {
        let audit = events(
            &ci(3076, PROXY_PATH, Some("VerifiedAndReputableDesktop")),
            &game(),
        );
        let core = events(
            &ci(
                3033,
                r"\Device\HarddiskVolume3\SteamLibrary\steamapps\common\F1 25\swofty\x.dll",
                None,
            ),
            &game(),
        );
        let loaded = running(&[&format!(r"{GAME}\version.dll"), SYSTEM], Mitigation::default());
        let f = found([audit, core].concat(), vec![loaded]);
        assert_eq!(f.cause(&game()), None);
    }

    #[test]
    fn the_game_process_names_the_cause_when_no_event_does() {
        let signed = Mitigation {
            microsoft_signed: true,
            ..Mitigation::default()
        };
        let f = found(Vec::new(), vec![running(&[SYSTEM], signed)]);
        assert_eq!(f.cause(&game()), Some(Cause::SignedOnly));
        let mut f = found(Vec::new(), vec![running(&[SYSTEM], Mitigation::default())]);
        f.search = Some(Ok(Search {
            known_dll: true,
            safe_search: None,
        }));
        assert_eq!(f.cause(&game()), Some(Cause::KnownDll));
        let f = found(Vec::new(), vec![running(&[SYSTEM], Mitigation::default())]);
        assert_eq!(
            f.cause(&game()),
            Some(Cause::Unexplained {
                path: SYSTEM.into(),
                hooks: Vec::new()
            })
        );
        let prefer = Mitigation {
            prefer_system32: true,
            ..Mitigation::default()
        };
        let f = found(Vec::new(), vec![running(&[SYSTEM], prefer)]);
        assert_eq!(
            f.cause(&game()),
            Some(Cause::OtherCopy {
                path: SYSTEM.into(),
                prefer_system32: true
            })
        );
    }

    #[test]
    fn a_game_it_cannot_look_inside_is_not_blamed_on_an_antivirus() {
        let denied = Seen {
            image: None,
            image_size: None,
            ours: false,
            modules: Err("access denied".into()),
            mitigation: Some(Err("access denied".into())),
            ..running(&[], Mitigation::default())
        };
        let mut f = found(Vec::new(), vec![denied]);
        assert_eq!(f.cause(&game()), Some(Cause::Unreadable));
        f.antivirus = Some(Ok(vec!["Microsoft Defender Antivirus (on)".into()]));
        assert_eq!(f.cause(&game()), Some(Cause::Unreadable));
        f.antivirus = Some(Ok(vec!["Bitdefender Antivirus (on)".into()]));
        assert_eq!(f.cause(&game()), None);
        f.antivirus = None;
        f.smart_app_control = Some(Ok(Some(1)));
        assert_eq!(f.cause(&game()), Some(Cause::SmartAppControl { seen: false }));
        assert_eq!(found(Vec::new(), Vec::new()).cause(&game()), None);
    }

    fn system_copy() -> Findings {
        found(Vec::new(), vec![running(&[SYSTEM], Mitigation::default())])
    }

    fn with_modules(f: &mut Findings, others: &[&str], shims: &[&str]) {
        let Ok(m) = &mut f.games[0].modules else {
            panic!("modules read")
        };
        m.others = others.iter().map(|o| (*o).to_owned()).collect();
        m.shims = shims.iter().map(|o| (*o).to_owned()).collect();
    }

    #[test]
    fn exploit_protection_settings_explain_another_copy() {
        let mut f = system_copy();
        f.exec_options = Some(Ok(vec!["MaxLoaderThreads".into()]));
        assert!(
            matches!(f.cause(&game()), Some(Cause::Unexplained { .. })),
            "MaxLoaderThreads alone"
        );
        f.exec_options = Some(Ok(vec![
            "MaxLoaderThreads".into(),
            "MitigationOptions".into(),
            "MitigationAuditOptions".into(),
            "EAFModules".into(),
        ]));
        assert_eq!(
            f.cause(&game()),
            Some(Cause::ExploitProtection { path: SYSTEM.into() })
        );
        f.exec_options = Some(Ok(vec![r"0\MitigationOptions".into()]));
        assert_eq!(
            f.cause(&game()),
            Some(Cause::ExploitProtection { path: SYSTEM.into() })
        );
    }

    #[test]
    fn compatibility_settings_or_shims_explain_another_copy() {
        let mut f = system_copy();
        f.layers = Some(Ok(vec!["~ HIGHDPIAWARE (for all users)".into()]));
        assert_eq!(
            f.cause(&game()),
            Some(Cause::CompatLayer {
                path: SYSTEM.into(),
                layers: vec!["~ HIGHDPIAWARE (for all users)".into()],
                shims: Vec::new(),
                from: None
            })
        );
        let mut f = system_copy();
        f.layers = Some(Ok(Vec::new()));
        with_modules(&mut f, &[], &["AcLayers.dll", "AcGenral.dll"]);
        assert!(matches!(f.cause(&game()), Some(Cause::CompatLayer { shims, .. }) if shims.len() == 2));
        let paths: Vec<String> = [
            r"C:\WINDOWS\SYSTEM32\apphelp.dll",
            r"C:\WINDOWS\SYSTEM32\AcLayers.DLL",
            SYSTEM,
        ]
        .map(String::from)
        .to_vec();
        assert_eq!(
            modules(&paths, &game(), Some(r"C:\WINDOWS")).shims,
            ["AcLayers.DLL"]
        );
    }

    fn starter(name: &str, exe: &str, layers: &str, all_users: bool) -> AdminLayer {
        AdminLayer {
            all_users,
            name: name.into(),
            exe: exe.into(),
            layers: layers.into(),
        }
    }

    #[test]
    fn shims_are_traced_to_the_program_that_starts_the_game() {
        const EA: &str = r"C:\Program Files\Electronic Arts\EA Desktop\EA Desktop\EADesktop.exe";
        let carrier = |f: &Findings| match f.cause(&game()) {
            Some(Cause::CompatLayer { from, .. }) => from,
            other => panic!("{other:?}"),
        };
        let mut f = system_copy();
        f.layers = Some(Ok(Vec::new()));
        with_modules(&mut f, &[], &["AcLayers.DLL", "AcGenral.DLL"]);
        f.starter_layers = Some(Vec::new());
        assert_eq!(carrier(&f), None);

        f.ea_app = vec![
            EaApp {
                pid: 900,
                image: None,
                shims: Err("access denied".into()),
            },
            EaApp {
                pid: 901,
                image: Some(EA.into()),
                shims: Ok(vec!["AcLayers.DLL".into()]),
            },
        ];
        assert_eq!(
            carrier(&f),
            Some(Carrier {
                name: "EADesktop.exe".into(),
                exe: Some(EA.into()),
                layers: Vec::new(),
            })
        );

        f.starter_layers = Some(vec![
            starter("EADesktop.exe", EA, "~ RUNASADMIN", false),
            starter("EADesktop.exe", EA, "WIN8RTM", true),
            starter(
                "swofty-launcher.exe",
                r"C:\Swofty\swofty-launcher.exe",
                "~ WIN7RTM",
                false,
            ),
        ]);
        assert_eq!(
            carrier(&f),
            Some(Carrier {
                name: "EADesktop.exe".into(),
                exe: Some(EA.into()),
                layers: vec![
                    "~ RUNASADMIN (for this user)".into(),
                    "WIN8RTM (for all users)".into()
                ],
            })
        );
        f.starter_layers = Some(vec![starter(
            "swofty-launcher.exe",
            r"C:\Swofty\swofty-launcher.exe",
            "~ WIN7RTM",
            false,
        )]);
        assert!(matches!(carrier(&f), Some(c) if c.name == "swofty-launcher.exe"));

        f.layers = Some(Ok(vec!["~ HIGHDPIAWARE (for this user)".into()]));
        assert_eq!(carrier(&f), None, "a setting on F1_25.exe itself comes first");

        let lines = f.lines(&game());
        assert!(
            lines.contains(&r"compatibility settings on swofty-launcher.exe: ~ WIN7RTM (for this user) on C:\Swofty\swofty-launcher.exe".to_owned()),
            "{lines:#?}"
        );
        assert!(lines.contains(&"EADesktop.exe pid 900: modules not readable (access denied)".to_owned()));
        assert!(
            lines.contains(
                &"EADesktop.exe pid 901: Windows compatibility shims loaded: AcLayers.DLL".to_owned()
            )
        );
        f.starter_layers = Some(Vec::new());
        assert!(f.lines(&game()).contains(
            &"compatibility settings on EADesktop.exe, EALauncher.exe, swofty-launcher.exe and the offline play stub: none"
                .to_owned()
        ));
    }

    #[test]
    fn only_a_third_party_antivirus_is_named() {
        let mut f = system_copy();
        f.antivirus = Some(Ok(vec!["Microsoft Defender Antivirus (on)".into()]));
        assert!(matches!(f.cause(&game()), Some(Cause::Unexplained { .. })));
        f.antivirus = Some(Ok(vec![
            "Microsoft Defender Antivirus (off)".into(),
            "Reason Cybersecurity (on)".into(),
            "Norton Security (off)".into(),
        ]));
        assert_eq!(
            f.cause(&game()),
            Some(Cause::Antivirus {
                path: SYSTEM.into(),
                names: vec!["Reason Cybersecurity".into()]
            })
        );
        f.antivirus = Some(Ok(vec!["Bitdefender Antivirus (on)".into()]));
        assert!(matches!(f.cause(&game()), Some(Cause::Antivirus { .. })));
    }

    #[test]
    fn an_unexplained_copy_names_the_hooks_in_the_game() {
        let mut f = system_copy();
        with_modules(
            &mut f,
            &["medal-hook64-v2.dll", "RTSSHooks64.dll", "160_E658703.bin"],
            &[],
        );
        assert_eq!(
            f.cause(&game()),
            Some(Cause::Unexplained {
                path: SYSTEM.into(),
                hooks: vec!["Medal".into(), "RivaTuner Statistics Server".into()]
            })
        );
    }

    #[test]
    fn smart_app_control_on_is_the_cause_only_when_nothing_else_is() {
        let ours = format!(r"{GAME}\version.dll");
        let mut f = found(Vec::new(), vec![running(&[&ours, SYSTEM], Mitigation::default())]);
        assert_eq!(f.cause(&game()), None);
        f.smart_app_control = Some(Ok(Some(1)));
        assert_eq!(f.cause(&game()), Some(Cause::SmartAppControl { seen: false }));
        f.smart_app_control = Some(Ok(Some(2)));
        assert_eq!(f.cause(&game()), None);
        f.games = vec![running(&[SYSTEM], Mitigation::default())];
        f.smart_app_control = Some(Ok(Some(1)));
        assert!(matches!(f.cause(&game()), Some(Cause::Unexplained { .. })));
        assert_eq!(Findings::default().cause(&game()), None);
    }

    #[test]
    fn writes_the_findings_as_lines() {
        let mut g = running(&[SYSTEM], Mitigation::default());
        if let Ok(m) = g.modules.as_mut() {
            m.others = vec!["aswhook.dll".into()];
        }
        let f = Findings {
            proxy: Proxy::Present {
                size: 412_160,
                same: Some(true),
                zone: Some(3),
            },
            exe_size: Some(423_518_208),
            created: Some(27488),
            games: vec![g],
            search: Some(Ok(Search::default())),
            exec_options: Some(Ok(Vec::new())),
            smart_app_control: Some(Ok(None)),
            logs: vec![
                EventLog {
                    name: "Code Integrity",
                    read: Ok(events(
                        &ci(3077, PROXY_PATH, Some("VerifiedAndReputableDesktop")),
                        &game(),
                    )),
                },
                EventLog {
                    name: "Defender",
                    read: Err("access denied".into()),
                },
            ],
            antivirus: Some(Ok(vec!["Avast Antivirus (on)".into()])),
            ..Findings::default()
        };
        assert_eq!(
            f.lines(&game()),
            [
                "version.dll: in the game folder, 412160 bytes, the bytes the launcher placed, marked as downloaded \
                 (Zone.Identifier ZoneId=3)"
                    .to_owned(),
                format!(
                    r"game pid 27488 (the one the offline play stub created): {GAME}\F1_25.exe (423518208 bytes), the game folder's F1_25.exe"
                ),
                format!("pid 27488: 212 modules, version.dll from {SYSTEM}"),
                "pid 27488: modules from outside the game and Windows folders: aswhook.dll".to_owned(),
                "pid 27488: DLL load policies: none".to_owned(),
                "DLL search: version.dll is not a KnownDLL, SafeDllSearchMode default (on)".to_owned(),
                "Image File Execution Options for F1_25.exe: none".to_owned(),
                "Smart App Control: not set".to_owned(),
                "Code Integrity log: 1 event(s) about version.dll or F1_25.exe since the game started".to_owned(),
                format!(
                    "  Code Integrity 3077 at 16:27:09 UTC: {PROXY_PATH} in F1_25.exe: blocked by an App Control \
                     policy (VerifiedAndReputableDesktop)"
                ),
                "Defender log: not readable (access denied)".to_owned(),
                "antivirus registered with Windows: Avast Antivirus (on)".to_owned(),
            ]
        );
    }

    #[test]
    fn writes_what_proton_shows() {
        let f = Findings {
            proxy: Proxy::Present {
                size: 10,
                same: Some(false),
                zone: None,
            },
            created: Some(4242),
            wine_override: Some(false),
            ..Findings::default()
        };
        assert_eq!(
            f.lines(Path::new("/games/F1 25")),
            [
                "version.dll: in the game folder, 10 bytes, NOT the bytes the launcher placed",
                "game: no F1_25.exe is running",
                "game: the offline play stub created pid 4242, which is not running",
                "Proton prefix: user.reg lacks the version.dll override, so Wine uses its builtin version.dll",
            ]
        );
        let ours = Seen {
            pid: 7,
            image: None,
            image_size: None,
            ours: false,
            modules: Ok(Modules {
                count: 3,
                version: vec!["/games/F1 25/version.dll".into()],
                others: Vec::new(),
                shims: Vec::new(),
            }),
            mitigation: None,
        };
        let f = Findings {
            proxy: Proxy::Missing,
            games: vec![ours],
            ..Findings::default()
        };
        assert_eq!(
            f.lines(Path::new("/games/F1 25")),
            [
                "version.dll: gone from the game folder",
                "game pid 7: exe path unknown, NOT the game folder's F1_25.exe",
                "pid 7: 3 modules, version.dll from the game folder's (the loader)",
            ]
        );
    }

    #[test]
    fn collects_what_a_game_folder_shows() {
        let dir = std::env::temp_dir().join(format!("swofty-loader-check-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(dir.join("game/swofty")).unwrap();
        let game = dir.join("game");
        std::fs::write(game.join(PROXY), b"proxy").unwrap();
        std::fs::write(game.join(GAME_EXE), b"MZ exe").unwrap();
        std::fs::write(
            game.join(LOADER_LOG),
            "18:27:08.903 [stub] [offline] game created (pid 27488)\n",
        )
        .unwrap();
        let reg = dir.join("user.reg");
        std::fs::write(
            &reg,
            format!(
                "WINE REGISTRY Version 2\n\n{}\n\"version\"=\"native,builtin\"\n",
                crate::install::OVERRIDE_HEADER
            ),
        )
        .unwrap();
        let placed = sha256_file(&game.join(PROXY)).unwrap();
        let before = SystemTime::now() - Duration::from_secs(5);
        let f = collect(&game, Some(&placed), before, Some(&reg));
        assert_eq!(
            f.proxy,
            Proxy::Present {
                size: 5,
                same: Some(true),
                zone: None
            }
        );
        assert_eq!(f.exe_size, Some(6));
        assert_eq!(f.created, Some(27488));
        assert!(!f.rotated);
        assert_eq!(f.wine_override, (!cfg!(windows)).then_some(true));
        std::fs::write(game.join("swofty/loader.prev.log"), "moved aside").unwrap();
        std::fs::write(game.join(PROXY), b"changed").unwrap();
        std::fs::write(
            &reg,
            format!(
                "WINE REGISTRY Version 2\n\n{} 1792606023\n#time=1dd6186fa06f04c\n",
                crate::install::OVERRIDE_HEADER
            ),
        )
        .unwrap();
        let f = collect(&game, Some(&placed), before, Some(&reg));
        assert!(f.rotated);
        assert!(matches!(
            f.proxy,
            Proxy::Present {
                same: Some(false),
                ..
            }
        ));
        assert_eq!(f.wine_override, (!cfg!(windows)).then_some(false));
        std::fs::remove_file(game.join(PROXY)).unwrap();
        assert_eq!(collect(&game, None, before, None).proxy, Proxy::Missing);
        std::fs::remove_dir_all(&dir).ok();
    }
}
