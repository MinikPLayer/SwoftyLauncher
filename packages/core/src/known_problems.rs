//! Crash causes the Send Logs triage keeps finding, recognised from a crashed
//! session's own files: the `[crash]`, `[overlay]` and `[offline] module:`
//! lines of its loader.log, the crash report the core wrote, and the files in
//! the game folder. The launcher shows the cause and its fix when one of the
//! rules here matches, and Send Logs and the support Discord when none does.
//! An addon whose changes don't apply to an F1 25 older than the newest
//! update is recognised from the build's error, for the launch's failure.

use std::collections::BTreeMap;
use std::io::Read;
use std::path::Path;

use crate::conflicts::Pair;
use crate::install::LOADER_DIR;
use crate::launch::LOW_DISK;
use crate::manifest::{AddonManifest, Category};
use crate::overlay_guard::SessionOverlay;
use crate::session::SessionRecord;
use crate::store::Store;

/// The most of a loader.log or crash report that is read.
const READ_CAP: u64 = 32 << 20;
/// The stack frames, nearest first, that count as the crash's own.
const NEAR_FRAMES: usize = 8;
/// The files a step names before it says how many more there are.
const NAMED_FILES: usize = 6;

/// The newest F1 25 update the launcher knows, as `F1_25.exe`'s file
/// version: Steam build 25251167, and the EA app's copy of the same update.
pub const LATEST_GAME_VERSION: &str = "1.0.141.7865";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Diagnosis {
    /// A stable name for the rule that matched, for logs and Send Logs.
    pub id: &'static str,
    pub title: String,
    pub what: String,
    pub steps: Vec<String>,
}

impl Diagnosis {
    /// What happened, then its steps, as one paragraph.
    pub fn text(&self) -> String {
        std::iter::once(&self.what)
            .chain(&self.steps)
            .map(String::as_str)
            .collect::<Vec<_>>()
            .join(" ")
    }
}

/// A game version's four numbers, as in `1.0.141.7865`.
fn version_numbers(version: &str) -> Option<[u32; 4]> {
    let mut parts = version.trim().split('.');
    let mut numbers = [0; 4];
    for n in &mut numbers {
        let part = parts.next()?;
        if part.is_empty() || !part.bytes().all(|b| b.is_ascii_digit()) {
            return None;
        }
        *n = part.parse().ok()?;
    }
    parts.next().is_none().then_some(numbers)
}

/// Whether game version `version` is older than `than`, number by number.
/// None when either isn't a version.
pub fn older_game(version: &str, than: &str) -> Option<bool> {
    Some(version_numbers(version)? < version_numbers(than)?)
}

/// The addon whose changes failed to apply, from a build error such as
/// `applying swofty.classic-seasons: eaid://…: no entry [Name=…]`, on its
/// own or after what was being done when it failed.
fn failed_addon(error: &str) -> Option<&str> {
    let rest = error
        .strip_prefix("applying ")
        .or_else(|| error.split_once(": applying ").map(|(_, rest)| rest))?;
    let id = rest.split([':', ' ', '\n']).next()?;
    let valid = !id.is_empty()
        && id
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '_'));
    valid.then_some(id)
}

/// An addon whose changes failed to apply to an F1 25 older than
/// [`LATEST_GAME_VERSION`]: it needs that update. None when `error` isn't an
/// addon's changes failing, or when `version`, the game's own, is unknown or
/// not older.
pub fn outdated_game(
    error: &str,
    version: Option<&str>,
    names: &BTreeMap<String, String>,
) -> Option<Diagnosis> {
    let version = version?.trim();
    if older_game(version, LATEST_GAME_VERSION) != Some(true) {
        return None;
    }
    let id = failed_addon(error)?;
    let name = names.get(id).map_or(id, String::as_str);
    Some(Diagnosis {
        id: "outdated_game",
        title: format!("{name} needs the latest F1 25"),
        what: format!(
            "{name} needs F1 25 {LATEST_GAME_VERSION}, the latest update. Your F1 25 is {version}."
        ),
        steps: vec!["Update F1 25 in Steam or the EA app, then press Play again.".to_owned()],
    })
}

/// An F1 25 older than [`LATEST_GAME_VERSION`], before a modded launch: the
/// mod loader can't hook into it, so addons may not load. None when the
/// version is unknown or not older.
pub fn outdated_exe(version: Option<&str>) -> Option<Diagnosis> {
    let version = version?.trim();
    if older_game(version, LATEST_GAME_VERSION) != Some(true) {
        return None;
    }
    Some(Diagnosis {
        id: "outdated_exe",
        title: "F1 25 needs the latest update".to_owned(),
        what: format!(
            "Your F1 25 is {version}. Mods are made for {LATEST_GAME_VERSION}, the latest update, and may not \
             load on an older one."
        ),
        steps: vec![format!(
            "Update F1 25 in Steam or the EA app. If Steam says it is up to date, verify the game files \
             ({}), then press Play again.",
            crate::stock::STEAM_VERIFY_URL
        )],
    })
}

/// What a crashed session left behind, as the rules read it.
#[derive(Debug, Clone, Default)]
pub struct Evidence {
    /// The session's loader.log.
    pub loader: String,
    /// The crash report the core wrote for the session, when it can be found.
    pub report: Option<String>,
    /// The session's perf.log.
    pub perf: Option<String>,
    /// The names of the files and folders in the game folder.
    pub game_files: Vec<String>,
    /// Addon names by id, for addons the rules name.
    pub names: BTreeMap<String, String>,
    /// The profile's enabled addons in load order, from its last build.
    pub enabled: Vec<String>,
    /// The enabled addons that are tracks.
    pub tracks: Vec<String>,
    /// Which addon overrode which in the profile's last build.
    pub conflicts: Vec<Pair>,
    /// Per addon id, the game's entrant lists it renamed or removed that the last build kept.
    pub kept_lists: BTreeMap<String, Vec<String>>,
    /// The game's exit code, or the crash code the session recorded.
    pub code: Option<u32>,
    /// The game exe's version, when it can be read.
    pub game_version: Option<String>,
}

impl Evidence {
    pub fn read(session: &SessionRecord, game: Option<&Path>, names: BTreeMap<String, String>) -> Self {
        let loader = ["loader.log", "game-logs/loader.log"]
            .iter()
            .map(|rel| session.dir.join(rel))
            .find(|p| p.is_file())
            .and_then(|p| read_capped(&p))
            .unwrap_or_default();
        let perf = read_perf(&session.dir);
        let report = report_name(&loader).and_then(|name| {
            let mut dirs = vec![session.dir.clone(), session.dir.join("game-logs")];
            if let Some(game) = game {
                dirs.insert(0, game.join(LOADER_DIR));
            }
            dirs.iter()
                .map(|d| d.join(&name))
                .find(|p| p.is_file())
                .and_then(|p| read_capped(&p))
        });
        let game_files = game
            .and_then(|g| std::fs::read_dir(g).ok())
            .into_iter()
            .flatten()
            .flatten()
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect();
        let code = session
            .end
            .as_ref()
            .and_then(|e| e.crash_code.or(e.exit_code.map(|c| c as u32)))
            .or(session.exit_code.map(|c| c as u32));
        let game_version = game.and_then(|g| crate::exe_version::read(&g.join(crate::GAME_EXE)));
        Self {
            loader,
            report,
            perf,
            game_files,
            names,
            code,
            game_version,
            ..Default::default()
        }
    }

    /// The profile's addons from its last build in `store`.
    pub fn with_profile(mut self, store: &Store, addons: &BTreeMap<String, AddonManifest>) -> Self {
        if let Some(report) = crate::conflicts::last(store, &store.active_profile()) {
            self.tracks = report
                .order
                .iter()
                .filter(|id| {
                    addons
                        .get(*id)
                        .is_some_and(|m| m.category == Some(Category::Tracks))
                })
                .cloned()
                .collect();
            self.enabled = report.order;
            self.conflicts = report.pairs;
            self.kept_lists = report.kept_lists;
        }
        self
    }
}

/// The cause of the crash `session` ended with, when a rule recognises it.
pub fn crash(session: &SessionRecord, game: Option<&Path>, store: &Store) -> Option<Diagnosis> {
    diagnose(&evidence(session, game, store))
}

/// Why `session`, which ended without a crash, never got going, when a rule
/// recognises it.
pub fn stall(session: &SessionRecord, game: Option<&Path>, store: &Store) -> Option<Diagnosis> {
    perf_start_screen(&read_perf(&session.dir)?).filter(|s| *s >= BOOT_STALL_SECS)?;
    stalled(&evidence(session, game, store))
}

fn evidence(session: &SessionRecord, game: Option<&Path>, store: &Store) -> Evidence {
    let addons = store.addons().unwrap_or_default();
    let names = addons
        .iter()
        .map(|(id, m)| (id.clone(), m.name.clone()))
        .collect();
    Evidence::read(session, game, names).with_profile(store, &addons)
}

fn read_perf(dir: &Path) -> Option<String> {
    ["perf.log", "game-logs/perf.log"]
        .iter()
        .find_map(|rel| read_capped(&dir.join(rel)))
}

fn read_capped(path: &Path) -> Option<String> {
    let mut bytes = Vec::new();
    std::fs::File::open(path)
        .ok()?
        .take(READ_CAP)
        .read_to_end(&mut bytes)
        .ok()?;
    Some(String::from_utf8_lossy(&bytes).into_owned())
}

/// The crash report's file name from the loader's `crash report swofty\crash-….txt:` line.
fn report_name(loader: &str) -> Option<String> {
    loader.lines().rev().find_map(|line| {
        let rest = &line[line.find("[crash] crash report ")? + "[crash] crash report ".len()..];
        let path = rest.split(':').next()?;
        let name = path.rsplit(['\\', '/']).next()?;
        (name.starts_with("crash-") && name.ends_with(".txt")).then(|| name.to_owned())
    })
}

/// The fatal exception as the loader log and the crash report describe it.
#[derive(Debug, Clone, Default, PartialEq)]
struct Fatal {
    /// The module the exception happened in, lower case.
    module: Option<String>,
    /// The modules on the crashing thread's stack, nearest first, lower case.
    stack: Vec<String>,
    /// Modules whose return addresses the crash report found on the stack.
    returns: Vec<String>,
    /// The loader's record of the exception: registers, code and stack.
    record: Vec<String>,
    /// Where the record starts among the loader's crash lines.
    at: Option<usize>,
}

impl Fatal {
    fn near(&self) -> impl Iterator<Item = &str> {
        self.module
            .iter()
            .chain(self.stack.iter().take(NEAR_FRAMES))
            .map(String::as_str)
    }

    fn all(&self) -> impl Iterator<Item = &str> {
        self.near().chain(self.returns.iter().map(String::as_str))
    }
}

/// The module in `text` before its first `+0x`, as in `F1_25.exe+0x39182a4`.
fn module_at(text: &str) -> Option<String> {
    let at = text.find("+0x")?;
    let name = text[..at].rsplit([' ', '\t']).next()?;
    (!name.is_empty() && !name.starts_with("0x")).then(|| name.to_ascii_lowercase())
}

/// The loader's crash lines, without their time and thread.
fn crash_lines(loader: &str) -> Vec<&str> {
    loader
        .lines()
        .filter_map(|l| Some(&l[l.find("[crash] ")? + "[crash] ".len()..]))
        .collect()
}

/// The exception code in a first-chance line, as in `ACCESS_VIOLATION (0xc0000005)`.
fn exception_code(line: &str) -> Option<u32> {
    line.match_indices("(0x").find_map(|(i, _)| {
        let hex = &line[i + 3..];
        u32::from_str_radix(&hex[..hex.find(')')?], 16).ok()
    })
}

fn record_at<'a>(crash: &[&'a str], i: usize) -> impl Iterator<Item = &'a str> {
    crash[i + 1..].iter().copied().take_while(|l| l.starts_with("  "))
}

fn fatal(e: &Evidence) -> Fatal {
    let mut f = Fatal::default();
    let crash = crash_lines(&e.loader);
    let unhandled = crash
        .iter()
        .find(|l| l.starts_with("unhandled ") && l.contains("reached the top-level filter"));
    f.module = unhandled.and_then(|l| module_at(&l[l.find(" at ")? + 4..]));
    let address = unhandled.and_then(|l| {
        let at = &l[l.find(" at ")? + 4..];
        Some(at.split_whitespace().next()?.to_owned())
    });
    f.at = match &address {
        Some(address) => crash
            .iter()
            .position(|l| l.contains("first-chance") && l.contains(address.as_str())),
        None if e.report.is_none() => crash
            .iter()
            .rposition(|l| l.contains("first-chance"))
            .filter(|&i| e.code.is_some() && exception_code(crash[i]) == e.code),
        None => None,
    };
    if let Some(i) = f.at {
        if f.module.is_none() {
            f.module = crash[i]
                .find(" at ")
                .and_then(|at| module_at(&crash[i][at + 4..]));
        }
        f.record = record_at(&crash, i).map(str::to_owned).collect();
        f.stack = f
            .record
            .iter()
            .filter(|l| l.trim_start().starts_with('#'))
            .filter_map(|l| module_at(l))
            .collect();
    }
    if let Some(report) = &e.report {
        let mut section = "";
        let mut stack = Vec::new();
        for line in report.lines() {
            if let Some(name) = line.strip_prefix("== ") {
                section = name;
                continue;
            }
            if section == "Exception" && f.module.is_none() {
                if let Some(at) = line.find(" at ") {
                    f.module = module_at(&line[at + 4..]);
                }
            } else if section.starts_with("Stack (") {
                stack.extend(line.split_whitespace().nth(3).and_then(module_at));
            } else if section.starts_with("Return addresses")
                && let Some(m) = line.split_whitespace().nth(2).and_then(module_at)
                && !f.returns.contains(&m)
            {
                f.returns.push(m);
            }
        }
        if f.stack.is_empty() {
            f.stack = stack;
        }
    }
    f
}

/// The modules the loader and the offline play hook saw in the game process.
fn loaded_modules(loader: &str) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    for line in loader.lines() {
        let Some(rest) = ["[offline] module: ", "[loader] module: "]
            .iter()
            .find_map(|tag| Some(&line[line.find(tag)? + tag.len()..]))
        else {
            continue;
        };
        let name = rest.split(" @").next().unwrap_or(rest).trim();
        if !name.is_empty() && !out.iter().any(|o| o.eq_ignore_ascii_case(name)) {
            out.push(name.to_owned());
        }
    }
    out
}

/// Whether the crash's lines (and the crash report) hold any of `codes`, the
/// HRESULTs the game hands its fatal abort, as in `rdi=00000000887a0005`.
fn crash_holds(e: &Evidence, codes: &[&str]) -> bool {
    let hit = |l: &str| {
        let l = l.to_ascii_lowercase();
        codes.iter().any(|c| l.contains(c))
    };
    e.loader
        .lines()
        .filter(|l| l.contains("[crash]") || l.contains("[overlay] the GPU was removed"))
        .any(hit)
        || e.report.as_deref().is_some_and(|r| r.lines().any(hit))
}

fn gpu_removed(e: &Evidence) -> bool {
    crash_holds(
        e,
        &[
            "887a0005",
            "887a0006",
            "887a0007",
            "dxgi_error_device_removed",
            "dxgi_error_device_hung",
            "dxgi_error_device_reset",
        ],
    )
}

/// Whether the game ran under Wine or Proton, where vkd3d reports its own
/// failures (a pipeline it can't build) with the same codes as a Windows
/// driver out of memory or lost.
fn wine(e: &Evidence, loaded: &[String]) -> bool {
    e.report
        .as_deref()
        .is_some_and(|r| r.lines().any(|l| l.starts_with("runtime") && l.contains("Wine")))
        || loaded.iter().any(|m| {
            let m = m.to_ascii_lowercase();
            m.starts_with("winex11") || m.starts_with("winewayland") || m.starts_with("winevulkan")
        })
}

fn out_of_memory(e: &Evidence) -> bool {
    crash_holds(e, &["8007000e", "e_outofmemory"])
}

/// The names of the addons that drew with Swofty's overlay in the session.
fn painters(e: &Evidence, overlay: &SessionOverlay) -> Vec<String> {
    overlay
        .painters
        .iter()
        .map(|id| e.names.get(id).cloned().unwrap_or_else(|| id.clone()))
        .collect()
}

/// Programs that hook into games as they run, by the start of their DLL's name.
struct Hook {
    prefix: &'static str,
    product: &'static str,
    step: &'static str,
    /// Only when the exception happened in it: the DLL also sits harmlessly
    /// on stacks of crashes it had no part in.
    own_only: bool,
}

const HOOKS: &[Hook] = &[
    Hook {
        prefix: "rtsshooks",
        product: "RivaTuner Statistics Server (RTSS), which comes with MSI Afterburner",
        step: "Close RivaTuner Statistics Server and MSI Afterburner, including from the system tray, before you \
               press Play. Or, in RTSS, add F1_25.exe and set its Application detection level to None.",
        own_only: false,
    },
    Hook {
        prefix: "windhawk",
        product: "Windhawk",
        step: "Quit Windhawk from its tray icon before you press Play, or add F1_25.exe to the processes \
               Windhawk doesn't inject into in its settings.",
        own_only: false,
    },
    Hook {
        prefix: "medal-hook",
        product: "Medal",
        step: "Close Medal, including from the system tray, before you press Play.",
        own_only: false,
    },
    Hook {
        prefix: "discordhook",
        product: "Discord's in-game overlay",
        step: "Turn off Discord's in-game overlay in Discord's settings (Game Overlay).",
        own_only: false,
    },
    Hook {
        prefix: "graphics-hook",
        product: "OBS Studio's game capture",
        step: "In OBS Studio, capture F1 25 with Window Capture or Display Capture instead of Game Capture, or \
               close OBS while you play.",
        own_only: false,
    },
    Hook {
        prefix: "nvpresent",
        product: "the NVIDIA app's overlay and frame tools",
        step: "Turn off the in-game overlay and Smooth Motion for F1 25 in the NVIDIA app.",
        own_only: true,
    },
    Hook {
        prefix: "nvspcap",
        product: "the NVIDIA app's overlay",
        step: "Turn off the in-game overlay in the NVIDIA app's settings.",
        own_only: true,
    },
    Hook {
        prefix: "gameoverlayrenderer",
        product: "the Steam overlay",
        step: "Turn off the Steam overlay for F1 25: right-click F1 25 in Steam, Properties, General, and untick \
               Enable the Steam Overlay while in-game.",
        own_only: true,
    },
    Hook {
        prefix: "specialk",
        product: "Special K",
        step: "Turn Special K off for F1 25, or remove it from the game folder.",
        own_only: false,
    },
    Hook {
        prefix: "pid.dll",
        product: "Windows' force feedback driver for your wheel",
        step: "Update your wheel's driver and software (for example Logitech G HUB, Fanatec, Thrustmaster or \
               Moza), plug the wheel straight into the PC, and make sure only one wheel program is running.",
        own_only: true,
    },
];

/// Graphics drivers' user-mode DLLs, by the start of their name.
const DRIVERS: &[(&str, &str)] = &[
    ("nvwgf2um", "NVIDIA"),
    ("nvd3dum", "NVIDIA"),
    ("nvoglv", "NVIDIA"),
    ("nvlddmkm", "NVIDIA"),
    ("amdxc64", "AMD"),
    ("amdxx64", "AMD"),
    ("atidxx64", "AMD"),
    ("igd12umd", "Intel"),
    ("igd10iumd", "Intel"),
    ("igdumdim", "Intel"),
    ("igc64", "Intel"),
];

/// The game's frame generation DLLs.
const FRAME_GEN: &[(&str, &str)] = &[
    ("nvngx_dlssg", "DLSS Frame Generation"),
    ("sl.dlss_g", "DLSS Frame Generation"),
    ("libxess_fg", "XeSS frame generation"),
];

/// Modules that mean the crash happened in graphics code.
fn graphics(module: &str) -> bool {
    module.starts_with("sl.")
        || module.starts_with("nvngx")
        || module.starts_with("dxgi")
        || module.starts_with("d3d1")
        || module.starts_with("dxcore")
        || module.starts_with("libxess")
        || module.starts_with("amd_fidelityfx")
        || DRIVERS.iter().any(|(p, _)| module.starts_with(p))
}

/// Names in `loaded` that only graphics mods use; Windows and the drivers
/// ship DLLs named like the others (dxgi.dll, nvapi64.dll).
fn distinctive(name: &str) -> bool {
    let f = name.to_ascii_lowercase();
    f.starts_with("reshade")
        || f.starts_with("optiscaler")
        || f.starts_with("specialk")
        || f.starts_with("dlssg_to_fsr3")
        || f.ends_with(".asi")
        || f.ends_with(".addon64")
        || f.ends_with(".addon")
}

/// Files in the game folder that F1 25 doesn't ship and that hook its
/// graphics, plus the distinctive ones among the game's loaded modules,
/// grouped by the tool they come from.
fn graphics_mods(game_files: &[String], loaded: &[String]) -> Vec<(&'static str, Vec<String>)> {
    let mut files = game_files.to_vec();
    for m in loaded.iter().filter(|m| distinctive(m)) {
        if !files.iter().any(|f| f.eq_ignore_ascii_case(m)) {
            files.push(m.clone());
        }
    }
    let files = &files;
    let mut groups: Vec<(&'static str, Vec<String>)> = Vec::new();
    let mut add = |tool: &'static str, name: &str| match groups.iter_mut().find(|(t, _)| *t == tool) {
        Some((_, names)) => names.push(name.to_owned()),
        None => groups.push((tool, vec![name.to_owned()])),
    };
    let reshade = files.iter().any(|f| {
        let f = f.to_ascii_lowercase();
        f == "reshade.ini" || f.starts_with("reshade64") || f.ends_with(".addon64") || f.ends_with(".addon")
    });
    let optiscaler = files
        .iter()
        .any(|f| f.to_ascii_lowercase().starts_with("optiscaler"));
    for name in files {
        let f = name.to_ascii_lowercase();
        let tool = if f.starts_with("optiscaler")
            || f == "nvngx.dll"
            || f == "fakenvapi.ini"
            || f == "nvapi64.dll"
            || f.starts_with("dlssg_to_fsr3")
            || f == "amdxc64.dll"
            || f == "libxess_dx11.dll"
        {
            "OptiScaler"
        } else if f == "reshade.ini"
            || f.starts_with("reshade64")
            || f.ends_with(".addon64")
            || f.ends_with(".addon")
            || f == "reshade-shaders"
        {
            "ReShade"
        } else if f == "dxgi.dll" || f == "d3d12.dll" || f == "d3d11.dll" {
            if optiscaler {
                "OptiScaler"
            } else if reshade {
                "ReShade"
            } else {
                "a graphics mod"
            }
        } else if f.starts_with("specialk") {
            "Special K"
        } else if f.ends_with(".asi") || f == "winmm.dll" || f == "dinput8.dll" {
            "an ASI loader"
        } else {
            continue;
        };
        add(tool, name);
    }
    groups
}

fn list(names: &[String]) -> String {
    let shown: Vec<&str> = names.iter().take(NAMED_FILES).map(String::as_str).collect();
    let more = names.len().saturating_sub(NAMED_FILES);
    match (shown.as_slice(), more) {
        ([one], 0) => (*one).to_owned(),
        (_, 0) => {
            let (last, rest) = shown.split_last().expect("at least two names");
            format!("{} and {last}", rest.join(", "))
        }
        _ => format!("{} and {more} more", shown.join(", ")),
    }
}

fn tools(groups: &[(&'static str, Vec<String>)]) -> String {
    let names: Vec<String> = groups.iter().map(|(t, _)| (*t).to_owned()).collect();
    list(&names)
}

fn move_out(groups: &[(&'static str, Vec<String>)]) -> String {
    let files: Vec<String> = groups.iter().flat_map(|(_, f)| f.iter().cloned()).collect();
    format!(
        "Close Swofty Launcher, then move {} out of the F1 25 folder into a folder of their own, so you can put \
         them back later.",
        list(&files)
    )
}

const VERIFY: &str = "Verify F1 25's files: in Steam, right-click F1 25, Properties, Installed Files, Verify integrity \
                      of game files. For the EA app copy, use Repair in the EA app.";

fn driver_steps() -> Vec<String> {
    vec![
        "Update your graphics driver, or reinstall it cleanly. If you installed a new driver in the last few days, \
         try the one before it."
            .to_owned(),
        "Set any graphics card overclock or undervolt (MSI Afterburner, the NVIDIA app's tuning, AMD Adrenalin) \
         back to stock."
            .to_owned(),
        "Turn off frame generation (DLSS, FSR or XeSS frame generation) in F1 25's graphics settings.".to_owned(),
        "On a graphics card with 8 GB of memory or less, lower Texture Quality, or play with fewer car, livery \
         and track addons."
            .to_owned(),
    ]
}

/// The exit code of a game Windows stopped from loading a file it doesn't
/// trust (STATUS_SYSTEM_INTEGRITY_POLICY_VIOLATION).
const APP_CONTROL: u32 = 0xc0e9_0002;
/// F1 25's hang watchdog limit in milliseconds, which it holds in r15 when it
/// closes the game.
const WATCHDOG_MS: &str = "r15=0000000000007530";
/// How long before the watchdog fired the game had surely stopped moving on.
const STALL_SECS: f64 = 28.0;
/// The earliest before the watchdog fired that the freeze can have started.
const FREEZE_SECS: f64 = 36.0;
/// The least time between two log lines of one addon on one thread, across
/// the freeze, that counts as the addon keeping that thread busy.
const BUSY_GAP_SECS: f64 = 20.0;
/// The lines an addon logs in the stalled time to count as the one busy.
const BUSY_LINES: usize = 5;
/// The archive resources two car packs must both replace to count as full ones.
const CAR_PACK_RESOURCES: usize = 1000;
/// Imported Sepang, the heaviest imported track.
const SEPANG: &str = "mod.bahrain24";
/// Manual Pit Lane, and where F1 25 crashes when it takes the car as the
/// game returns it to the garage.
const PITLANE: &str = "kizi.pitlane";
const PITLANE_CRASH: &str = "F1_25.exe+0x389a1d7 ";
/// The entrant list search by name (fn 0x141738eb0) that runs off the end of the
/// game's loaded lists when the name is missing.
const ENTRANT_LIST_SEARCH: &str = "F1_25.exe+0x1738f30 ";
/// How long before the crash Manual Pit Lane's lines count, and after it.
const PITLANE_SECS: f64 = 1.5;
const PITLANE_AFTER_SECS: f64 = 0.5;
/// The highest control mode a car really has; Manual Pit Lane prints a
/// garbage one when it read the car as the game moved it.
const PITLANE_MODES: u64 = 0xffff;
/// Addons known to keep F1 25 on Loading... after Press any button, so it never
/// reaches the main menu.
const BOOT_STALLS: &[&str] = &["crew8006.bp-ultimate-livery"];
/// The screen F1 25 shows after Press any button while it loads the main menu.
const START_SCREEN: &str = "splash_transition";
/// How long a session that ended on that screen had been on it to count as stuck.
const BOOT_STALL_SECS: f64 = 45.0;

fn name(e: &Evidence, id: &str) -> String {
    e.names.get(id).cloned().unwrap_or_else(|| id.to_owned())
}

/// When a loader line was written, in seconds, from its `2026-10-27 15:00:08.518` start.
fn stamp(line: &str) -> Option<f64> {
    let mut date = line.get(..10)?.split('-').map(|n| n.parse::<u32>().ok());
    let (y, m, d) = (date.next()??, date.next()??, date.next()??);
    let mut time = line.get(11..23)?.split([':', '.']).map(|n| n.parse::<u32>().ok());
    let (h, min, sec, ms) = (time.next()??, time.next()??, time.next()??, time.next()??);
    let day = f64::from((y * 12 + m) * 31 + d);
    Some(day * 86400.0 + f64::from(h * 3600 + min * 60 + sec) + f64::from(ms) / 1000.0)
}

/// The fatal engine heap error the game ended with: its code and message.
fn heap_error(e: &Evidence) -> Option<(u32, String)> {
    let parse = |l: &str| {
        let rest = &l[l.find("Ne heap error 0x")? + "Ne heap error 0x".len()..];
        let hex: String = rest.chars().take_while(char::is_ascii_hexdigit).collect();
        let fatal =
            l.contains("fatal Ne heap error") || rest[hex.len()..].trim_start().starts_with("(fatal)");
        fatal.then(|| Some((u32::from_str_radix(&hex, 16).ok()?, rest.to_owned())))?
    };
    e.loader
        .lines()
        .rev()
        .filter(|l| l.contains("[crash] "))
        .find_map(parse)
        .or_else(|| {
            e.report
                .as_deref()?
                .lines()
                .find(|l| l.starts_with("reason"))
                .and_then(parse)
        })
}

fn windows_out_of_memory(code: u32, message: &str) -> bool {
    matches!(code, 0x5 | 0x58)
        || message.contains("useAddressSpace")
        || message.contains("Out of physical memory")
}

/// The enabled Classic Seasons year packs.
fn classic_years(e: &Evidence) -> usize {
    e.enabled
        .iter()
        .filter(|id| {
            id.strip_prefix("swofty.classic-seasons-")
                .is_some_and(|y| !y.is_empty() && y.bytes().all(|b| b.is_ascii_digit()))
        })
        .count()
}

fn years(n: usize) -> String {
    format!("{n} Classic Seasons year{}", if n == 1 { "" } else { "s" })
}

/// Whether the game's own hang watchdog closed it: it writes to address 1
/// with its 30 s limit in r15.
fn hang_watchdog(e: &Evidence, f: &Fatal) -> bool {
    let trap = |lines: &mut dyn Iterator<Item = &str>| {
        let (mut write, mut rax, mut limit) = (false, false, false);
        for l in lines {
            let t = l.trim_start();
            write |= t == "writing 0x1" || t.starts_with("writing 0x1:");
            rax |= t.starts_with("rax=0000000000000001 ");
            limit |= l.contains(WATCHDOG_MS);
        }
        write && rax && limit
    };
    let report = e.report.as_deref().is_some_and(|r| {
        let mut section = "";
        trap(&mut r.lines().filter(|l| {
            if let Some(name) = l.strip_prefix("== ") {
                section = name;
            }
            section == "Exception" || section == "Registers"
        }))
    });
    trap(&mut f.record.iter().map(String::as_str)) || report
}

/// The loader's crash lines as written, with their time.
fn raw_crash_lines(loader: &str) -> Vec<&str> {
    loader.lines().filter(|l| l.contains("[crash] ")).collect()
}

/// The addon that kept a game thread busy while the game froze: one whose
/// consecutive lines on a thread are far apart across the stall, or the one
/// that logged the most in it.
fn busy_addon(e: &Evidence, f: &Fatal) -> Option<String> {
    let end = stamp(raw_crash_lines(&e.loader).get(f.at?)?)?;
    let ids: Vec<&String> = if e.enabled.is_empty() {
        e.names.keys().collect()
    } else {
        e.enabled.iter().collect()
    };
    let addon_of = |rest: &str| -> Option<&String> {
        let tag = rest.strip_prefix('[')?.split(']').next()?;
        if let Some((_, api)) = rest.split_once("] api: ") {
            let id = api.split(' ').next()?;
            return ids.iter().find(|i| i.as_str() == id).copied();
        }
        ids.iter()
            .find(|id| id.as_str() == tag || id.rsplit('.').next() == Some(tag))
            .copied()
    };
    let mut counts: BTreeMap<&String, usize> = BTreeMap::new();
    let mut last: BTreeMap<&str, (f64, &String)> = BTreeMap::new();
    let mut gap = None;
    for line in e.loader.lines() {
        let Some(t) = stamp(line) else { continue };
        if t > end {
            break;
        }
        let Some((thread, rest)) = line.get(24..).and_then(|r| r.split_once("] ")) else {
            continue;
        };
        match addon_of(rest) {
            Some(a) => {
                if t >= end - STALL_SECS {
                    *counts.entry(a).or_default() += 1;
                    if last
                        .get(thread)
                        .is_some_and(|(at, b)| *b == a && t - at > BUSY_GAP_SECS && end - at <= FREEZE_SECS)
                    {
                        gap = Some(a);
                    }
                }
                last.insert(thread, (t, a));
            }
            None => {
                last.remove(thread);
            }
        }
    }
    gap.or_else(|| {
        counts
            .into_iter()
            .filter(|(_, n)| *n >= BUSY_LINES)
            .max_by_key(|(_, n)| *n)
            .map(|(a, _)| a)
    })
    .cloned()
}

/// A graphics driver error the game hit in the minute before it froze: the
/// module, its vendor and the frame generation on its stack.
fn driver_error_before(e: &Evidence, f: &Fatal) -> Option<(String, &'static str, Option<&'static str>)> {
    let at = f.at?;
    let crash = crash_lines(&e.loader);
    let raw = raw_crash_lines(&e.loader);
    let end = stamp(raw.get(at)?)?;
    (0..at).rev().find_map(|i| {
        let l = crash[i];
        if !l.contains("first-chance") || stamp(raw[i]).is_none_or(|t| end - t > 2.0 * STALL_SECS) {
            return None;
        }
        let module = module_at(&l[l.find(" at ")? + 4..])?;
        let vendor = DRIVERS.iter().find(|(p, _)| module.starts_with(p))?.1;
        let frame_gen = record_at(&crash, i)
            .filter_map(module_at)
            .find_map(|m| FRAME_GEN.iter().find(|(p, _)| m.starts_with(p)).map(|(_, w)| *w));
        Some((module, vendor, frame_gen))
    })
}

/// Whether the game's graphics settings had frame generation on, as Swofty's
/// overlay last read them.
fn frame_gen_on(loader: &str) -> bool {
    loader
        .lines()
        .rev()
        .find(|l| l.contains("[overlay] the game's hardware settings"))
        .is_some_and(|l| l.contains("have frame generation on"))
}

/// Two or more full car packs that replace each other's cars in the build, by name.
fn car_packs(e: &Evidence) -> Vec<String> {
    let mut packs: Vec<&String> = Vec::new();
    for p in e.conflicts.iter().filter(|p| {
        p.resources >= CAR_PACK_RESOURCES && p.items.iter().any(|i| i.contains("vehicle_package/teams/"))
    }) {
        for id in [&p.winner, &p.loser] {
            if !packs.contains(&id) {
                packs.push(id);
            }
        }
    }
    if packs.len() < 2 {
        return Vec::new();
    }
    packs.sort_by_key(|id| e.enabled.iter().position(|x| x == *id));
    packs.iter().map(|id| name(e, id)).collect()
}

/// What the addons in the profile and the game's settings add to a graphics
/// card crash: sentences for what happened, and their steps.
fn graphics_causes(e: &Evidence) -> (String, Vec<String>) {
    let mut what = String::new();
    let mut steps = Vec::new();
    let packs = car_packs(e);
    if !packs.is_empty() {
        what.push_str(&format!(
            " {} are all full car packs, and they replace each other's cars. Several on at once can load more \
             than the graphics card can take.",
            list(&packs)
        ));
        steps.push(format!("Keep only one of {} on in your profile.", list(&packs)));
    }
    if frame_gen_on(&e.loader) {
        what.push_str(" Frame generation was on in F1 25's graphics settings.");
        steps.push(FRAME_GEN_OFF.to_owned());
    }
    (what, steps)
}

const FRAME_GEN_OFF: &str =
    "Turn off frame generation (DLSS, FSR or XeSS frame generation) in F1 25's graphics settings.";

fn app_control() -> Diagnosis {
    Diagnosis {
        id: "app_control",
        title: "Windows blocked Swofty's files from loading".to_owned(),
        what: "Windows stopped F1 25 from loading Swofty's version.dll (error 0xc0e90002, Bad Image), so the game \
               closed before it started. Smart App Control or an app control policy does this to files it doesn't \
               know."
            .to_owned(),
        steps: vec![
            "Turn off Smart App Control: open Windows Security, App & browser control, Smart App Control settings, \
             and choose Off."
                .to_owned(),
            "On a work or school PC, an app control policy from your organisation does this instead. Ask whoever \
             manages the PC to allow F1 25's folder."
                .to_owned(),
            "Press Play again.".to_owned(),
        ],
    }
}

fn saving(e: &Evidence) -> bool {
    e.report
        .as_deref()
        .is_some_and(|r| r.contains("Game_SaveData_WriteBag"))
}

fn game_heap(e: &Evidence) -> Diagnosis {
    let mut tracks: Vec<&String> = e.tracks.iter().collect();
    tracks.sort_by_key(|id| id.as_str() != SEPANG);
    let tracks: Vec<String> = tracks.iter().map(|id| name(e, id)).collect();
    let mut heavy = tracks.clone();
    let years_on = classic_years(e);
    if years_on > 0 {
        heavy.push(years(years_on));
    }
    let mut what = if saving(e) {
        "F1 25 keeps a fixed amount of memory for game data, and it filled up while the game was saving your \
         career, so the game closed. More memory in your PC doesn't change that."
    } else {
        "F1 25 keeps a fixed amount of memory for game data, and it filled up, so the game closed. More \
         memory in your PC doesn't change that."
    }
    .to_owned();
    if !heavy.is_empty() {
        what.push_str(&format!(
            " Imported tracks and season packs use a lot of it, and your profile has {} on.",
            list(&heavy)
        ));
    }
    let mut steps = Vec::new();
    if let Some(first) = tracks.first() {
        let which = if e.tracks.iter().any(|t| t == SEPANG) {
            first.clone()
        } else {
            list(&tracks)
        };
        steps.push(format!(
            "Turn off {which} in your profile, or make a lighter profile without it for the sessions it crashes in."
        ));
    }
    steps.push(if years_on > 0 {
        "Turn on fewer imported tracks and only the Classic Seasons years you race.".to_owned()
    } else {
        "Play with fewer imported tracks and season addons on at once.".to_owned()
    });
    Diagnosis {
        id: "game_heap",
        title: "F1 25's game memory filled up".to_owned(),
        what,
        steps,
    }
}

fn windows_memory(e: &Evidence) -> Diagnosis {
    let years_on = classic_years(e);
    let fewer = if years_on > 0 {
        format!(
            "Turn on only the Classic Seasons years you race; your profile has {} on.",
            years(years_on)
        )
    } else {
        "Play with fewer car, livery and track addons on at once.".to_owned()
    };
    Diagnosis {
        id: "windows_out_of_memory",
        title: "Windows ran out of memory".to_owned(),
        what: "Windows had no memory left to give F1 25, so the game closed. This is your PC's memory (RAM and the \
               page file), not the graphics card's."
            .to_owned(),
        steps: vec![
            "Let Windows manage the page file: search Windows for View advanced system settings, open Performance \
             Settings, Advanced, Virtual memory, Change, and tick Automatically manage paging file size for all \
             drives. Keep 20 GB or more free on drive C."
                .to_owned(),
            "Close other programs, especially web browsers, while you play.".to_owned(),
            fewer,
        ],
    }
}

fn hang(e: &Evidence, f: &Fatal, mods: &[(&'static str, Vec<String>)]) -> Diagnosis {
    let mut what =
        "F1 25 stopped responding for 30 seconds, so its own watchdog closed it. It froze rather than \
                    crashed."
            .to_owned();
    let mut steps = Vec::new();
    if let Some(addon) = busy_addon(e, f) {
        let addon = name(e, &addon);
        what.push_str(&format!(" {addon} kept the game busy while it froze."));
        steps.push(format!("Turn off {addon} in your profile, then play again."));
    } else if let Some((addon, _)) = boot_stall(e) {
        what.push_str(&format!(
            " It never got past Loading... after Press any button, which {addon} is known to cause."
        ));
        steps.push(format!("Turn off {addon} in your profile, then play again."));
    } else if let Some((module, vendor, frame_gen)) = driver_error_before(e, f) {
        let during = frame_gen.map(|w| format!(" during {w}")).unwrap_or_default();
        what.push_str(&format!(
            " Just before, F1 25 hit an error in {module}, part of the {vendor} graphics driver{during}."
        ));
        if frame_gen.is_some() {
            steps.push(FRAME_GEN_OFF.to_owned());
        }
        steps.extend(
            driver_steps()
                .into_iter()
                .filter(|s| frame_gen.is_none() || !s.starts_with("Turn off frame generation")),
        );
    }
    if !mods.is_empty() {
        what.push_str(&format!(
            " Your game folder has {} installed, which hooks F1 25's graphics.",
            tools(mods)
        ));
        steps.insert(0, move_out(mods));
    }
    if steps.is_empty() {
        steps.push(
            "Press Play again. If F1 25 freezes at the same place again, turn off the addons you turned on last."
                .to_owned(),
        );
        steps.push(
            "Close other programs while you play, especially ones that record or draw over the screen."
                .to_owned(),
        );
    }
    Diagnosis {
        id: "hang_watchdog",
        title: "F1 25 froze and closed itself".to_owned(),
        what,
        steps,
    }
}

/// A row of perf.log's timeline: when it started, in seconds after the core
/// attached, and what the game was doing.
fn timeline_row(line: &str) -> Option<(f64, &str)> {
    let mut rest = line;
    let mut at = None;
    for i in 0..6 {
        let (field, more) = rest.trim_start().split_once(' ')?;
        match i {
            0 => {
                let mut hms = field.split(':').map(|n| n.parse::<u32>().ok());
                let (h, m, s) = (hms.next()??, hms.next()??, hms.next()??);
                at = Some(f64::from(h * 3600 + m * 60 + s));
            }
            _ if field != "-" && field.parse::<f64>().is_err() => return None,
            _ => {}
        }
        rest = more;
    }
    let what = rest.trim_start();
    let what = what.split("  [").next().unwrap_or(what);
    Some((at?, what.trim_end().trim_end_matches(" (current)")))
}

/// How long the game had been on the start screen when perf.log was last
/// written, when that is the last screen its timeline shows.
fn perf_start_screen(perf: &str) -> Option<f64> {
    let uptime: f64 = perf
        .lines()
        .find_map(|l| l.strip_prefix("uptime: "))?
        .split(' ')
        .next()?
        .parse()
        .ok()?;
    let mut timeline = false;
    let mut last = None;
    for line in perf.lines() {
        if let Some(section) = line.strip_prefix("== ") {
            timeline = section.starts_with("Timeline");
            continue;
        }
        let Some((at, what)) = timeline.then(|| timeline_row(line)).flatten() else {
            continue;
        };
        if let Some(screen) = what.split("; ").filter_map(|w| w.strip_prefix("screen ")).last() {
            last = Some((at, screen));
        }
    }
    let (at, screen) = last?;
    (screen == START_SCREEN).then_some(uptime - at)
}

/// How long the game had been on the start screen when it ended: from the
/// crash report's black box, or else perf.log.
fn start_screen_secs(e: &Evidence) -> Option<f64> {
    let screen = e.report.as_deref().and_then(|r| {
        r.lines()
            .find_map(|l| l.trim_start().strip_prefix("last screen"))
            .map(str::trim_start)
    });
    match screen {
        Some(screen) => {
            let rest = screen
                .strip_prefix(START_SCREEN)?
                .trim_start()
                .strip_prefix('(')?;
            rest.split(' ').next()?.parse().ok()
        }
        None => perf_start_screen(e.perf.as_deref()?),
    }
}

/// An addon known to stall the boot, when the game sat on the start screen for
/// [`BOOT_STALL_SECS`] or more and never left it: its name and the seconds.
fn boot_stall(e: &Evidence) -> Option<(String, f64)> {
    let secs = start_screen_secs(e).filter(|s| *s >= BOOT_STALL_SECS)?;
    let on = |id: &str| match e.enabled.is_empty() {
        true => e.names.contains_key(id),
        false => e.enabled.iter().any(|on| on == id),
    };
    let id = BOOT_STALLS.iter().find(|id| on(id))?;
    Some((name(e, id), secs))
}

fn minutes(secs: f64) -> String {
    match (secs / 60.0).round() as u64 {
        0 | 1 => format!("{} seconds", secs.round() as u64),
        n => format!("{n} minutes"),
    }
}

/// The cause of a session that ended without a crash, from the first rule that
/// recognises it.
pub fn stalled(e: &Evidence) -> Option<Diagnosis> {
    let (addon, secs) = boot_stall(e)?;
    Some(Diagnosis {
        id: "boot_stall",
        title: format!("{addon} kept F1 25 on the loading screen"),
        what: format!(
            "F1 25 stayed on Loading... after Press any button for {} and never reached the main menu. \
             {addon} is known to cause this.",
            minutes(secs)
        ),
        steps: vec![format!(
            "Turn off {addon} in your profile, then press Play again."
        )],
    })
}

/// F1 25's Insufficient Storage dialog: the drive it names, and the MiB free
/// and needed there.
fn low_disk(loader: &str) -> Option<(String, u64, u64)> {
    loader.lines().rev().find_map(|l| {
        let rest = &l[l.find(LOW_DISK)? + LOW_DISK.len()..];
        let (free, rest) = rest.split_once('/')?;
        let (need, rest) = rest.split_once(" MiB available on ")?;
        let drive = rest
            .trim_start_matches(['\'', '"'])
            .get(..2)
            .filter(|d| d.ends_with(':') && d.starts_with(|c: char| c.is_ascii_alphabetic()))
            .map_or_else(
                || "the drive F1 25 saves to".to_owned(),
                |d| format!("drive {}", d[..1].to_ascii_uppercase()),
            );
        Some((drive, free.trim().parse().ok()?, need.trim().parse().ok()?))
    })
}

fn gb(mib: u64, round: fn(f64) -> f64) -> String {
    let tenths = round(mib as f64 / 102.4);
    if tenths % 10.0 == 0.0 {
        format!("{:.0} GB", tenths / 10.0)
    } else {
        format!("{:.1} GB", tenths / 10.0)
    }
}

fn low_disk_space(drive: &str, free: u64, need: u64) -> Diagnosis {
    let mut what = format!(
        "F1 25 needs {} free on {drive} and found {}, so it showed Insufficient Storage and closed.",
        gb(need, f64::round),
        gb(free, f64::floor)
    );
    if drive == "drive C" {
        what.push_str(" It checks drive C even when the game is installed on another drive.");
    }
    Diagnosis {
        id: "low_disk",
        title: format!("F1 25 needs more free space on {drive}"),
        what,
        steps: vec![
            format!(
                "Free up at least {} on {drive}: empty the Recycle Bin, and delete temporary files in Windows \
                 Settings, System, Storage, Temporary files.",
                gb(need.saturating_sub(free).max(1), f64::ceil)
            ),
            "Press Play again.".to_owned(),
        ],
    }
}

/// When the game's fatal exception happened, from its first-chance line or
/// else the line where it reached the top-level filter.
fn crash_time(e: &Evidence, f: &Fatal) -> Option<f64> {
    f.at.and_then(|at| raw_crash_lines(&e.loader).get(at).copied())
        .or_else(|| {
            e.loader
                .lines()
                .find(|l| l.contains("[crash] unhandled ") && l.contains("reached the top-level filter"))
        })
        .and_then(stamp)
}

/// Whether the game crashed as Manual Pit Lane took the car in the pit lane:
/// it decoded the car's control mode to garbage around the crash, or kept
/// control just before the game crashed where this crash happens.
fn pitlane_crash(e: &Evidence, f: &Fatal) -> bool {
    if f.module.as_deref() != Some("f1_25.exe") {
        return false;
    }
    let Some(end) = crash_time(e, f) else {
        return false;
    };
    let within = |l: &str, after: f64| stamp(l).is_some_and(|t| t >= end - PITLANE_SECS && t <= end + after);
    let (mut kept, mut garbage) = (false, false);
    for l in e.loader.lines().filter(|l| l.contains("] [pitlane] ")) {
        kept |= l.contains("] [pitlane] pit lane: control kept")
            && !l.contains("the AI keeps the car")
            && within(l, 0.0);
        garbage |= l
            .split_once(", mode ")
            .and_then(|(_, m)| m.split_once(" -> "))
            .and_then(|(_, to)| to.trim().parse::<u64>().ok())
            .is_some_and(|mode| mode > PITLANE_MODES)
            && within(l, PITLANE_AFTER_SECS);
    }
    let at = e
        .loader
        .lines()
        .any(|l| l.contains("[crash] unhandled ") && l.contains(PITLANE_CRASH));
    garbage || (kept && at)
}

fn entrant_list_crash(e: &Evidence, f: &Fatal) -> bool {
    let at = |l: &str| l.contains("unhandled ") && l.contains(ENTRANT_LIST_SEARCH);
    f.module.as_deref() == Some("f1_25.exe")
        && (crash_lines(&e.loader).into_iter().any(at) || e.report.iter().flat_map(|r| r.lines()).any(at))
}

/// The text `rdi` points at in the crash report's memory dump, the name the game looked up.
fn searched_name(report: &str) -> Option<String> {
    let mut lines = report
        .lines()
        .skip_while(|l| !l.starts_with("== Memory at the registers"))
        .skip_while(|l| !l.trim_start().starts_with("rdi="));
    let rdi = u64::from_str_radix(lines.next()?.trim_start().get(4..20)?, 16).ok()?;
    let mut dump = BTreeMap::new();
    for l in lines.take_while(|l| l.starts_with("    ")) {
        let t = l.trim_start();
        let Some(addr) = t.get(..16).and_then(|a| u64::from_str_radix(a, 16).ok()) else {
            continue;
        };
        let bytes: Vec<u8> = t
            .get(17..)
            .unwrap_or_default()
            .split_whitespace()
            .take(16)
            .map_while(|b| u8::from_str_radix(b, 16).ok())
            .collect();
        dump.insert(addr, bytes);
    }
    let mut name = Vec::new();
    let mut addr = rdi;
    loop {
        let byte = *dump.get(&(addr & !0xf))?.get((addr & 0xf) as usize)?;
        if byte == 0 {
            break;
        }
        name.push(byte);
        if name.len() > 64 {
            return None;
        }
        addr += 1;
    }
    let ok = !name.is_empty()
        && name
            .iter()
            .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || *b == b'_');
    ok.then(|| String::from_utf8_lossy(&name).into_owned())
}

fn missing_entrant_list(e: &Evidence) -> Diagnosis {
    let list = e.report.as_deref().and_then(searched_name);
    let addon = e
        .kept_lists
        .iter()
        .find(|(_, lists)| list.as_ref().is_none_or(|l| lists.contains(l)))
        .map(|(id, _)| name(e, id));
    let which = list
        .as_ref()
        .map_or("an entrant list".to_owned(), |l| format!("the entrant list {l}"));
    let by = addon.as_ref().map_or(String::new(), |a| {
        format!(" The last build found {a} renaming or removing it.")
    });
    let mut steps = vec![match &addon {
        Some(a) => format!("Turn off {a}, or ask its author for a version made for this F1 25 update."),
        None => "Turn off the database addons you added most recently, such as imported team packs, one at a time \
                 until the crash stops."
            .to_owned(),
    }];
    steps.push("Press Play again.".to_owned());
    Diagnosis {
        id: "missing_entrant_list",
        title: format!("F1 25 crashed looking for {which}"),
        what: format!(
            "F1 25 looks up {which} by name when it builds a menu such as the F1 World Showroom, and the game's \
             database has no such list. A database addon renames or removes it, often one made for an older F1 25 \
             update.{by}"
        ),
        steps,
    }
}

fn pitlane(e: &Evidence) -> Diagnosis {
    let addon = e
        .names
        .get(PITLANE)
        .cloned()
        .unwrap_or_else(|| "Manual Pit Lane".to_owned());
    Diagnosis {
        id: "pitlane_garage",
        title: format!("{addon} crashed F1 25 in the pit lane"),
        what: format!(
            "F1 25 crashed as {addon} took over your car in the pit lane while the game was moving the car \
             itself, for example for Return to Garage or a flashback."
        ),
        steps: vec![
            format!("Turn off {addon} in your profile until its author fixes this crash."),
            "If you keep it on, don't use Return to Garage or flashbacks while you are in the pit lane."
                .to_owned(),
        ],
    }
}

const QUIT_SECS: f64 = 10.0;

/// Whether the crash report's black box shows the game was already in its
/// Exiting flow, with a first-chance access violation in the game's own code
/// just before the process ended.
fn quit_fault(e: &Evidence) -> bool {
    let Some(report) = &e.report else {
        return false;
    };
    let secs_before = |line: &str| -> Option<f64> {
        let rest = &line[line.find('(')? + 1..];
        rest.split(" s before").next()?.parse().ok()
    };
    let state = |key: &str| report.lines().find(|l| l.trim_start().starts_with(key));
    let exiting = state("last flow")
        .is_some_and(|l| l.contains(" Exiting ") && secs_before(l).is_some_and(|s| s <= QUIT_SECS));
    let violation = state("last exception")
        .is_some_and(|l| l.contains("ACCESS_VIOLATION") && l.to_ascii_lowercase().contains(" at f1_25.exe+"));
    exiting && violation
}

fn quit_fault_diagnosis() -> Diagnosis {
    Diagnosis {
        id: "quit_fault",
        title: "F1 25 hit an error as it closed".to_owned(),
        what: "F1 25 was already closing when it hit an error and ended itself. This is a fault in the game's own \
               shutdown, not a crash during play, and nothing you were doing is lost."
            .to_owned(),
        steps: vec!["Nothing to fix. Press Play when you want to play again.".to_owned()],
    }
}

/// The cause of a crash, from the first rule that recognises it. A crash no
/// rule recognises, on an F1 25 older than [`LATEST_GAME_VERSION`], is put down
/// to the old build.
pub fn diagnose(e: &Evidence) -> Option<Diagnosis> {
    matched(e).or_else(|| {
        let mut d = outdated_exe(e.game_version.as_deref())?;
        d.what = format!(
            "Your F1 25 is {}. Mods are made for {LATEST_GAME_VERSION}, the latest update, and F1 25 crashed on \
             the older one.",
            e.game_version.as_deref().unwrap_or_default().trim()
        );
        Some(d)
    })
}

fn matched(e: &Evidence) -> Option<Diagnosis> {
    if let Some((drive, free, need)) = low_disk(&e.loader) {
        return Some(low_disk_space(&drive, free, need));
    }
    if e.code == Some(APP_CONTROL) {
        return Some(app_control());
    }
    if quit_fault(e) {
        return Some(quit_fault_diagnosis());
    }
    let f = fatal(e);
    let loaded = loaded_modules(&e.loader);
    let mods = graphics_mods(&e.game_files, &loaded);
    for hook in HOOKS {
        let own = f.module.as_deref().is_some_and(|m| m.starts_with(hook.prefix));
        let near = !hook.own_only && f.near().any(|m| m.starts_with(hook.prefix));
        if !own && !near {
            continue;
        }
        let module = f
            .near()
            .find(|m| m.starts_with(hook.prefix))
            .unwrap_or(hook.prefix)
            .to_owned();
        return Some(Diagnosis {
            id: "third_party_hook",
            title: format!("F1 25 crashed inside {}", hook.product),
            what: format!(
                "The crash happened in {module}, which belongs to {}. It hooks into games while they run, and here \
                 it took F1 25 down with it.",
                hook.product
            ),
            steps: vec![hook.step.to_owned(), "Press Play again.".to_owned()],
        });
    }
    let windows = !wine(e, &loaded);
    if let Some((code, message)) = heap_error(e) {
        if code == 0xc && message.contains("'GAME_DEFAULT'") {
            return Some(game_heap(e));
        }
        if windows && windows_out_of_memory(code, &message) {
            return Some(windows_memory(e));
        }
    }
    if hang_watchdog(e, &f) {
        return Some(hang(e, &f, &mods));
    }
    if pitlane_crash(e, &f) {
        return Some(pitlane(e));
    }
    if entrant_list_crash(e, &f) {
        return Some(missing_entrant_list(e));
    }
    if windows && gpu_removed(e) {
        let overlay = SessionOverlay::scan(&e.loader).unwrap_or_default();
        if overlay.removed.is_some() && overlay.drew && !overlay.painters.is_empty() {
            let addons = painters(e, &overlay);
            let mut steps = vec![format!(
                "Turn off {} in your profile, then play again.",
                list(&addons)
            )];
            steps.extend(driver_steps().into_iter().take(2));
            return Some(Diagnosis {
                id: "gpu_removed_overlay",
                title: "Your graphics card stopped responding while Swofty's overlay was drawing".to_owned(),
                what: format!(
                    "Windows reset the graphics driver (DXGI_ERROR_DEVICE_REMOVED) shortly after Swofty's in-game \
                     overlay drew for {}, and F1 25 can't recover from that, so it closed. The overlay is \
                     experimental.",
                    list(&addons)
                ),
                steps,
            });
        }
        let (causes, mut steps) = graphics_causes(e);
        if !mods.is_empty() {
            steps.insert(0, move_out(&mods));
        }
        let frame_gen = steps.iter().any(|s| s == FRAME_GEN_OFF);
        steps.extend(
            driver_steps()
                .into_iter()
                .filter(|s| !frame_gen || !s.starts_with("Turn off frame generation")),
        );
        let also = if mods.is_empty() {
            String::new()
        } else {
            format!(
                " Your game folder also has {} installed, which hooks F1 25's graphics.",
                tools(&mods)
            )
        };
        let cause = if causes.is_empty() {
            " This comes from the graphics card or its driver, not from Swofty's code."
        } else {
            causes.as_str()
        };
        return Some(Diagnosis {
            id: "gpu_removed",
            title: "Your graphics driver stopped responding".to_owned(),
            what: format!(
                "Windows reset the graphics driver while F1 25 was running (DXGI_ERROR_DEVICE_REMOVED). F1 25 can't \
                 recover from that, so it closed.{cause}{also}"
            ),
            steps,
        });
    }
    if windows && out_of_memory(e) {
        let (causes, mut steps) = graphics_causes(e);
        steps.extend([
            "Lower Texture Quality, and the resolution if it's above your screen's, in F1 25's graphics settings."
                .to_owned(),
            "Play with fewer car, livery and track addons on at once, for example only the Classic Seasons years \
             you race."
                .to_owned(),
            "Close other programs, especially web browsers, while you play.".to_owned(),
            "On a laptop with integrated graphics, give the graphics more memory if its settings allow it (often \
             called UMA frame buffer size)."
                .to_owned(),
        ]);
        let what = if causes.is_empty() {
            " Car, livery and track addons use a lot of graphics memory, and several at once can use more than the \
             graphics card has."
        } else {
            causes.as_str()
        };
        return Some(Diagnosis {
            id: "out_of_memory",
            title: "F1 25 ran out of graphics memory".to_owned(),
            what: format!(
                "The game couldn't get the memory it asked for (E_OUTOFMEMORY), so it closed.{what}"
            ),
            steps,
        });
    }
    let started = crate::launch::LoaderSeen::scan(&e.loader).window;
    if !mods.is_empty() && (!started || f.all().any(graphics)) {
        let when = if started {
            "in its graphics code"
        } else {
            "while it was setting up its graphics, before its window opened"
        };
        return Some(Diagnosis {
            id: "graphics_mods",
            title: format!("{} in your game folder crashed F1 25", tools(&mods)),
            what: format!(
                "Your F1 25 folder has {} installed. It changes how F1 25 draws, and F1 25 crashed {when}.",
                tools(&mods)
            ),
            steps: vec![
                move_out(&mods),
                VERIFY.to_owned(),
                "Press Play. If F1 25 starts, you can put the files back one at a time to find the one that breaks \
                 it."
                    .to_owned(),
            ],
        });
    }
    if let Some((module, vendor)) = f.module.as_deref().filter(|_| windows).and_then(|m| {
        DRIVERS
            .iter()
            .find(|(p, _)| m.starts_with(p))
            .map(|(_, v)| (m, *v))
    }) {
        return Some(Diagnosis {
            id: "graphics_driver",
            title: format!("F1 25 crashed inside your {vendor} graphics driver"),
            what: format!("The crash happened in {module}, part of the {vendor} graphics driver."),
            steps: driver_steps(),
        });
    }
    if let Some((module, what)) = f.module.as_deref().and_then(|m| {
        FRAME_GEN
            .iter()
            .find(|(p, _)| m.starts_with(p))
            .map(|(_, w)| (m, *w))
    }) {
        let overlay = SessionOverlay::scan(&e.loader).unwrap_or_default();
        let mut steps = vec![format!(
            "Turn off {what} in F1 25's graphics settings, then play again."
        )];
        let mut also = String::new();
        if overlay.drew && !overlay.painters.is_empty() {
            let addons = list(&painters(e, &overlay));
            also = format!(
                " Swofty's in-game overlay was drawing for {addons} at the time, and it is experimental."
            );
            steps.insert(0, format!("Turn off {addons} in your profile."));
        }
        steps.push("Update your graphics driver.".to_owned());
        return Some(Diagnosis {
            id: "frame_generation",
            title: format!("F1 25 crashed in {what}"),
            what: format!("The crash happened in {module}, the game's {what} code.{also}"),
            steps,
        });
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    const GPU: &str = "\
2026-10-24 02:52:26.633 [33008] [crash] #1 first-chance (the game may still handle it) ACCESS_VIOLATION (0xc0000005) on thread 33008, 463 s after load, at F1_25.exe+0x39182a4 .xtls Ne_FatalAbort+0x4
2026-10-24 02:52:26.633 [33008] [crash]   writing 0x1
2026-10-24 02:52:26.633 [33008] [crash]   rsi=00000001c3603440 rdi=00000000887a0005 rbp=00000000887a002b rsp=000000000015be50
2026-10-24 02:52:26.633 [33008] [crash]   stack:
2026-10-24 02:52:26.633 [33008] [crash]   #0  F1_25.exe+0x39182a4 .xtls Ne_FatalAbort+0x4
2026-10-24 02:52:26.633 [33008] [crash]   #1  F1_25.exe+0x39b8802 .xtls fn base+0x39b8510+0x2f2
2026-10-24 02:52:27.289 [20344] [crash] crash report swofty\\crash-20261024-025226.txt: the game aborted itself
2026-10-24 02:52:27.289 [33008] [crash] unhandled ACCESS_VIOLATION (0xc0000005) at F1_25.exe+0x39182a4 .xtls Ne_FatalAbort+0x4 reached the top-level filter; passing it to the game's filter
";

    fn crash_in(module: &str) -> String {
        format!(
            "2026-10-26 14:01:42.000 [1] [crash] #1 first-chance (the game may still handle it) BREAKPOINT (0x80000003) on thread 1, 21 s after load, at {module}+0x178fd
2026-10-26 14:01:42.000 [1] [crash]   stack:
2026-10-26 14:01:42.000 [1] [crash]   #0  {module}+0x178fd
2026-10-26 14:01:42.000 [1] [crash]   #1  F1_25.exe+0x1234 .xtls fn base+0x1200+0x34
2026-10-26 14:01:42.100 [1] [crash] unhandled BREAKPOINT (0x80000003) at {module}+0x178fd reached the top-level filter; passing it to the game's filter
"
        )
    }

    fn ev(loader: &str, files: &[&str]) -> Evidence {
        Evidence {
            loader: loader.to_owned(),
            game_files: files.iter().map(|f| (*f).to_owned()).collect(),
            ..Default::default()
        }
    }

    #[test]
    fn reads_the_fatal_exception_and_its_stack() {
        let f = fatal(&ev(GPU, &[]));
        assert_eq!(f.module.as_deref(), Some("f1_25.exe"));
        assert_eq!(f.stack, ["f1_25.exe", "f1_25.exe"]);
        assert_eq!(report_name(GPU).as_deref(), Some("crash-20261024-025226.txt"));
    }

    #[test]
    fn a_removed_gpu_is_the_driver() {
        let d = diagnose(&ev(GPU, &["F1_25.exe", "sl.common.dll"])).unwrap();
        assert_eq!(d.id, "gpu_removed");
        assert!(d.what.contains("not from Swofty's code"), "{}", d.what);
        assert!(d.steps[0].starts_with("Update your graphics driver"));
    }

    #[test]
    fn a_removed_gpu_with_graphics_mods_names_them_first() {
        let d = diagnose(&ev(
            GPU,
            &["F1_25.exe", "dxgi.dll", "ReShade.ini", "dlss5-feed.addon64"],
        ))
        .unwrap();
        assert_eq!(d.id, "gpu_removed");
        assert!(d.what.contains("ReShade"), "{}", d.what);
        assert!(
            d.steps[0].contains("dxgi.dll, ReShade.ini and dlss5-feed.addon64"),
            "{}",
            d.steps[0]
        );
    }

    #[test]
    fn a_removed_gpu_after_the_overlay_drew_names_the_addons() {
        let log = format!(
            "{GPU}2026-10-24 02:52:26.000 [1] [overlay] api: kizi.pitlane on_paint -> ok\n\
             2026-10-24 02:52:26.000 [1] [overlay] renderer up: d3d12 swapchain 1920x1080\n\
             2026-10-24 02:52:26.500 [1] [overlay] the GPU was removed (present 0x887a0005, reason 0x887a0006) 4.8 s after the overlay last drew; the overlay stops drawing\n"
        );
        let mut e = ev(&log, &[]);
        e.names.insert("kizi.pitlane".into(), "Manual Pit Lane".into());
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "gpu_removed_overlay");
        assert_eq!(
            d.steps[0],
            "Turn off Manual Pit Lane in your profile, then play again."
        );
    }

    #[test]
    fn a_crash_inside_a_hook_names_its_program() {
        let d = diagnose(&ev(&crash_in("windhawk.dll"), &[])).unwrap();
        assert_eq!(d.id, "third_party_hook");
        assert_eq!(d.title, "F1 25 crashed inside Windhawk");
        let d = diagnose(&ev(&crash_in("RTSSHooks64.dll"), &[])).unwrap();
        assert!(d.title.contains("RivaTuner"), "{}", d.title);
        let d = diagnose(&ev(&crash_in("pid.dll"), &[])).unwrap();
        assert!(d.title.contains("force feedback"), "{}", d.title);
    }

    #[test]
    fn the_steam_overlay_counts_only_when_the_crash_is_in_it() {
        let log = crash_in("F1_25.exe").replace(
            "#1  F1_25.exe+0x1234 .xtls fn base+0x1200+0x34",
            "#1  GameOverlayRenderer64.dll+0x1234",
        );
        assert_eq!(diagnose(&ev(&log, &[])), None);
        let d = diagnose(&ev(&crash_in("GameOverlayRenderer64.dll"), &[])).unwrap();
        assert!(d.title.contains("Steam overlay"));
    }

    #[test]
    fn graphics_mods_before_the_window_are_named() {
        let d = diagnose(&ev(
            &crash_in("sl.common.dll"),
            &[
                "F1_25.exe",
                "OptiScaler.ini",
                "nvngx.dll",
                "dxgi.dll",
                "OptiPatcher.asi",
            ],
        ))
        .unwrap();
        assert_eq!(d.id, "graphics_mods");
        assert!(d.title.starts_with("OptiScaler and an ASI loader"), "{}", d.title);
        assert!(d.what.contains("before its window opened"), "{}", d.what);
        assert!(d.steps[0].contains("OptiScaler.ini, nvngx.dll, dxgi.dll and OptiPatcher.asi"));
    }

    #[test]
    fn graphics_mods_play_no_part_in_a_game_code_crash_after_the_window() {
        let log = format!(
            "2026-10-26 14:00:00.000 [1] trigger: main window visible\n{}",
            crash_in("F1_25.exe")
        );
        assert_eq!(
            diagnose(&ev(&log, &["F1_25.exe", "dxgi.dll", "ReShade.ini"])),
            None
        );
    }

    #[test]
    fn a_crash_in_a_graphics_driver_names_the_vendor() {
        let d = diagnose(&ev(&crash_in("amdxc64.dll"), &["F1_25.exe"])).unwrap();
        assert_eq!(d.id, "graphics_driver");
        assert_eq!(d.title, "F1 25 crashed inside your AMD graphics driver");
        let d = diagnose(&ev(
            &crash_in("amdxc64.dll"),
            &["F1_25.exe", "amdxc64.dll", "OptiScaler.ini"],
        ))
        .unwrap();
        assert_eq!(d.id, "graphics_mods", "OptiScaler's own amdxc64.dll");
    }

    #[test]
    fn a_crash_in_frame_generation_says_turn_it_off() {
        let log = format!(
            "2026-10-26 14:00:00.000 [1] trigger: main window visible\n{}",
            crash_in("libxess_fg.dll")
        );
        let d = diagnose(&ev(&log, &["F1_25.exe"])).unwrap();
        assert_eq!(d.id, "frame_generation");
        assert_eq!(
            d.steps[0],
            "Turn off XeSS frame generation in F1 25's graphics settings, then play again."
        );
    }

    #[test]
    fn running_out_of_memory_says_so() {
        let d = diagnose(&ev(&GPU.replace("887a0005", "8007000e"), &[])).unwrap();
        assert_eq!(d.id, "out_of_memory");
    }

    #[test]
    fn under_wine_the_windows_driver_rules_stay_out() {
        let log = format!(
            "{}2026-10-21 07:05:50.000 [1] [offline] module: winex11.drv @ 0x6ffffde80000\n",
            GPU.replace("887a0005", "8007000e")
        );
        assert_eq!(diagnose(&ev(&log, &[])), None);
    }

    #[test]
    fn a_plain_game_crash_has_no_diagnosis() {
        assert_eq!(
            diagnose(&ev(&crash_in("F1_25.exe"), &["F1_25.exe", "sl.common.dll"])),
            None
        );
        assert_eq!(diagnose(&ev("", &[])), None);
    }

    const WATCHDOG: &str = "\
2026-10-27 15:00:08.518 [3376] [crash] #1 first-chance (the game may still handle it) ACCESS_VIOLATION (0xc0000005) on thread 3376, 1083 s after load, at F1_25.exe+0x43e1113 .xtls fn base+0x43e10d0+0x43
2026-10-27 15:00:08.518 [3376] [crash]   writing 0x1
2026-10-27 15:00:08.518 [3376] [crash]   rax=0000000000000001 rbx=0000000000222200 rcx=00000000000077ae rdx=0000000000002065
2026-10-27 15:00:08.518 [3376] [crash]   r12=000000014604b4f0 r13=000000000351fe88 r14=00000000000077ae r15=0000000000007530
2026-10-27 15:00:08.519 [3376] [crash]   code: C7 00 BE CC 4B 28 48 83 C4 38 C3 CC CC 50 B8 01
2026-10-27 15:00:08.519 [3376] [crash]   stack:
2026-10-27 15:00:08.519 [3376] [crash]   #0  F1_25.exe+0x43e1113 .xtls fn base+0x43e10d0+0x43
2026-10-27 15:00:08.519 [3376] [crash]   #1  KERNEL32.DLL+0x2dda7
2026-10-27 15:00:08.520 [3376] [crash] unhandled ACCESS_VIOLATION (0xc0000005) at F1_25.exe+0x43e1113 .xtls fn base+0x43e10d0+0x43 reached the top-level filter; passing it to the game's filter
";

    fn with_names(mut e: Evidence, names: &[(&str, &str)]) -> Evidence {
        e.names = names
            .iter()
            .map(|(id, n)| ((*id).to_owned(), (*n).to_owned()))
            .collect();
        e.enabled = e.names.keys().cloned().collect();
        e
    }

    #[test]
    fn the_hang_watchdog_is_a_freeze() {
        let d = diagnose(&ev(WATCHDOG, &["F1_25.exe"])).unwrap();
        assert_eq!(d.id, "hang_watchdog");
        assert_eq!(d.title, "F1 25 froze and closed itself");
        assert!(d.what.contains("stopped responding for 30 seconds"), "{}", d.what);
        assert!(d.steps[0].starts_with("Press Play again"), "{:?}", d.steps);
        let other = WATCHDOG.replace("r15=0000000000007530", "r15=0000000000000000");
        assert_eq!(diagnose(&ev(&other, &["F1_25.exe"])), None);
    }

    #[test]
    fn the_hang_watchdog_names_the_addon_that_kept_the_game_busy() {
        let mut log = String::new();
        for s in 0..8 {
            log.push_str(&format!(
                "2026-10-27 14:59:{:02}.807 [21888] [helmet-exchange] thumbnail requested for helmet {s}\n",
                45 + s
            ));
        }
        log.push_str("2026-10-27 14:59:58.000 [5672] [progressive] standings read\n");
        log.push_str(WATCHDOG);
        let e = with_names(
            ev(&log, &["F1_25.exe"]),
            &[
                ("giggles.helmet-exchange", "Helmet Exchange"),
                ("swofty.progressive", "Progressive Career"),
            ],
        );
        let d = diagnose(&e).unwrap();
        assert!(
            d.what.contains("Helmet Exchange kept the game busy"),
            "{}",
            d.what
        );
        assert_eq!(
            d.steps[0],
            "Turn off Helmet Exchange in your profile, then play again."
        );

        let gap = format!(
            "2026-10-27 14:59:37.896 [14804] [storage] api: swofty.progressive (progressive_cards.dll) read data/years.json -> ok\n\
             2026-10-27 14:59:53.095 [28800] [game] WARN (warn): The signal timed out\n\
             2026-10-27 15:00:06.111 [14804] [progressive] gamedb labels loaded\n{WATCHDOG}"
        );
        let e = with_names(ev(&gap, &[]), &[("swofty.progressive", "Progressive Career")]);
        assert!(
            diagnose(&e)
                .unwrap()
                .what
                .contains("Progressive Career kept the game busy")
        );

        let idle = gap.replace("14:59:37.896", "14:58:20.000");
        let e = with_names(ev(&idle, &[]), &[("swofty.progressive", "Progressive Career")]);
        assert!(!diagnose(&e).unwrap().what.contains("kept the game busy"));
    }

    #[test]
    fn the_hang_watchdog_names_a_driver_error_and_graphics_mods() {
        let log = format!(
            "2026-10-27 14:59:36.800 [9000] [crash] #1 first-chance (the game may still handle it) ACCESS_VIOLATION (0xc0000005) on thread 9000, 1050 s after load, at nvwgf2umx.dll+0x93d4c2\n\
             2026-10-27 14:59:36.800 [9000] [crash]   stack:\n\
             2026-10-27 14:59:36.800 [9000] [crash]   #0  nvwgf2umx.dll+0x93d4c2\n\
             2026-10-27 14:59:36.800 [9000] [crash]   #1  sl.dlss_g.dll+0x1234\n{}",
            WATCHDOG.replace("#1 first-chance", "#2 first-chance")
        );
        let d = diagnose(&ev(&log, &["F1_25.exe"])).unwrap();
        assert_eq!(d.id, "hang_watchdog");
        assert!(
            d.what
                .contains("nvwgf2umx.dll, part of the NVIDIA graphics driver during DLSS Frame Generation"),
            "{}",
            d.what
        );
        assert_eq!(d.steps[0], FRAME_GEN_OFF);
        let d = diagnose(&ev(WATCHDOG, &["F1_25.exe", "dxgi.dll", "OptiScaler.ini"])).unwrap();
        assert!(d.what.contains("OptiScaler"), "{}", d.what);
        assert!(
            d.steps[0].contains("dxgi.dll and OptiScaler.ini"),
            "{}",
            d.steps[0]
        );
    }

    fn perf(rows: &[&str], uptime: u32) -> String {
        format!(
            "Swofty Launcher performance report (format 1)\n\
             == Summary\n\
             uptime: {uptime} s\n\
             \n\
             == Timeline (last 15; each row runs until the next one)\n\
             at          took ms   frames   slow   worst ms  swofty ms  what\n{}\n",
            rows.join("\n")
        )
    }

    const SPLASH: &[&str] = &[
        "00:00:37      44811     2157      4        624      33.71  screen splash  [core flow tick 20.39 ms]",
        "00:01:27       1772      105      0         18      57.27  flow #a4d3a4b2 Exiting; flow #04fc8ffa Entering  [core flow tick 50.48 ms]",
        "00:01:29     232600    13654      7       1866     331.45  screen splash_transition  [core flow tick 306.18 ms]",
        "00:05:21      86418     5061      3       1219          -  flow #04fc8ffa Exiting (current)",
    ];

    const LIVERY: (&str, &str) = ("crew8006.bp-ultimate-livery", "BP Ultimate Livery");

    fn stuck(rows: &[&str], uptime: u32, names: &[(&str, &str)]) -> Evidence {
        with_names(
            Evidence {
                perf: Some(perf(rows, uptime)),
                ..Default::default()
            },
            names,
        )
    }

    #[test]
    fn reads_the_last_screen_from_the_perf_timeline() {
        assert_eq!(perf_start_screen(&perf(SPLASH, 408)), Some(319.0));
        let menu = [
            SPLASH[2],
            "00:01:45      27019     2404     15        271     189.46  flow #04fc8ffa Exiting; screen playercard  [x]",
            "00:02:12        653       31      0         50      11.96  screen fe_home (current)",
        ];
        assert_eq!(perf_start_screen(&perf(&menu, 408)), None);
        assert_eq!(
            timeline_row(
                "00:04:44    2585578   469557      7       1420          -  screen splash_transition (current)"
            ),
            Some((284.0, "screen splash_transition"))
        );
        assert_eq!(
            timeline_row("at          took ms   frames   slow   worst ms  swofty ms  what"),
            None
        );
    }

    #[test]
    fn a_boot_stuck_on_loading_names_the_addon_known_to_cause_it() {
        let d = stalled(&stuck(
            SPLASH,
            408,
            &[LIVERY, ("giggles.helmet-exchange", "Helmet Exchange")],
        ))
        .unwrap();
        assert_eq!(d.id, "boot_stall");
        assert_eq!(d.title, "BP Ultimate Livery kept F1 25 on the loading screen");
        assert!(d.what.contains("for 5 minutes"), "{}", d.what);
        assert_eq!(
            d.steps,
            ["Turn off BP Ultimate Livery in your profile, then press Play again."]
        );
        assert_eq!(
            stalled(&stuck(
                SPLASH,
                408,
                &[("giggles.helmet-exchange", "Helmet Exchange")]
            )),
            None
        );
        assert_eq!(
            stalled(&stuck(SPLASH, 120, &[LIVERY])),
            None,
            "30 s is a normal load"
        );
        let mut off = stuck(SPLASH, 408, &[LIVERY]);
        off.enabled = vec!["giggles.helmet-exchange".into()];
        assert_eq!(stalled(&off), None, "installed but off");
        let menu = [
            SPLASH[2],
            "00:05:21        653       31      0         50      11.96  screen fe_home (current)",
        ];
        assert_eq!(stalled(&stuck(&menu, 408, &[LIVERY])), None);
    }

    #[test]
    fn the_hang_watchdog_on_the_loading_screen_names_the_addon_known_to_stall_it() {
        let report = "== Game state (from the black box)\n  last flow      #04fc8ffa Entering (2584.6 s before this report)\n  \
                      last screen    splash_transition (2581.5 s before this report)\n";
        let mut e = with_names(ev(WATCHDOG, &["F1_25.exe"]), &[LIVERY]);
        e.report = Some(report.to_owned());
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "hang_watchdog");
        assert!(
            d.what
                .contains("never got past Loading... after Press any button, which BP Ultimate Livery"),
            "{}",
            d.what
        );
        assert_eq!(
            d.steps[0],
            "Turn off BP Ultimate Livery in your profile, then play again."
        );
        e.report = Some(report.replace("splash_transition", "fe_home"));
        e.perf = Some(perf(SPLASH, 408));
        assert!(!diagnose(&e).unwrap().what.contains("BP Ultimate Livery"));
    }

    const GAME_HEAP: &str = "\
2026-10-27 14:21:24.880 [17796] [crash] Ne heap error 0xc (fatal) on thread 17796: Allocation error. Requested size:3528, Free Memory:829408, Largest Free Block:1392, HeapSize:204472320, Alignment: 8; heap 00000000636F4590 'GAME_DEFAULT', capacity 204472320
2026-10-27 14:21:24.880 [16100] [crash] writing a crash report: fatal Ne heap error 0xc in heap 'GAME_DEFAULT': Allocation error. Requested size:3528
2026-10-27 14:21:25.000 [17796] [crash] unhandled ACCESS_VIOLATION (0xc0000005) at F1_25.exe+0x39182a4 .xtls Ne_FatalAbort+0x4 reached the top-level filter; passing it to the game's filter
";

    #[test]
    fn a_full_game_heap_while_saving_says_so() {
        let mut e = ev(GAME_HEAP, &[]);
        assert!(!diagnose(&e).unwrap().what.contains("saving"));
        e.report = Some(
            "== Stack\n  #9  00000001439340f9 F1_25.exe+0x39340f9 .xtls Game_SaveData_WriteBag+0x39\n"
                .to_owned(),
        );
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "game_heap");
        assert!(
            d.what.contains("while the game was saving your career"),
            "{}",
            d.what
        );
    }

    const QUIT_REPORT: &str = "== Game state (from the black box)\n  last flow      #e11edb87 Exiting (1.8 s before this report)\n  \
        last exception first-chance ACCESS_VIOLATION at F1_25.exe+0x537db28 .xtls fn base+0x537db18+0x10 (0.0 s before this report)\n";

    #[test]
    fn an_access_violation_in_the_exiting_flow_is_a_quit_fault() {
        let mut e = ev("", &[]);
        e.report = Some(QUIT_REPORT.to_owned());
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "quit_fault");
        assert!(!d.what.contains("crashed"), "{}", d.what);
        e.report = Some(QUIT_REPORT.replace("Exiting (1.8", "Exiting (400.0"));
        assert_eq!(diagnose(&e), None, "an exit long before the fault");
        e.report = Some(QUIT_REPORT.replace("Exiting", "Entering"));
        assert_eq!(diagnose(&e), None, "not exiting");
        e.report = Some(QUIT_REPORT.replace("F1_25.exe+", "other.dll+"));
        assert_eq!(diagnose(&e), None, "not in the game's code");
    }

    #[test]
    fn a_full_game_heap_names_sepang_first() {
        let mut e = with_names(
            ev(GAME_HEAP, &[]),
            &[
                ("mod.hockenheim", "Hockenheim"),
                ("mod.bahrain24", "Sepang"),
                ("swofty.classic-seasons", "Classic Seasons"),
                ("swofty.classic-seasons-2016", "Classic Seasons 2016"),
                ("swofty.classic-seasons-2017", "Classic Seasons 2017"),
            ],
        );
        e.tracks = vec!["mod.hockenheim".into(), "mod.bahrain24".into()];
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "game_heap");
        assert!(
            d.what
                .contains("your profile has Sepang, Hockenheim and 2 Classic Seasons years on"),
            "{}",
            d.what
        );
        assert!(
            d.steps[0].starts_with("Turn off Sepang in your profile"),
            "{}",
            d.steps[0]
        );
        let d = diagnose(&ev(GAME_HEAP, &[])).unwrap();
        assert_eq!(d.id, "game_heap");
        assert_eq!(d.steps.len(), 1);
    }

    const COMMIT: &str = "\
2026-10-27 13:35:41.000 [15812] [crash] Ne heap error 0x58 (fatal) on thread 15812: FATAL ERROR : allocateAligned / useAddressSpace(block = 0xcdfe3a80, allocSize = 2088976) failed.Out of physical memory or Virtual memory problem; heap 0000000064243050 'RENDERER_INTERNAL'
2026-10-27 13:35:41.100 [15812] [crash] unhandled ACCESS_VIOLATION (0xc0000005) at F1_25.exe+0x39182a4 .xtls Ne_FatalAbort+0x4 reached the top-level filter; passing it to the game's filter
";

    #[test]
    fn windows_running_out_of_memory_is_not_the_graphics_card() {
        let mut e = with_names(
            ev(COMMIT, &[]),
            &[("swofty.classic-seasons-2019", "Classic Seasons 2019")],
        );
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "windows_out_of_memory");
        assert!(d.what.contains("not the graphics card's"), "{}", d.what);
        assert!(d.steps[0].contains("page file"), "{}", d.steps[0]);
        assert!(d.steps[2].contains("1 Classic Seasons year on"), "{}", d.steps[2]);
        let warned = COMMIT
            .replace("0x58 (fatal)", "0x58 (not fatal)")
            .replace("Ne_FatalAbort", "Other");
        e.loader = warned;
        assert_eq!(diagnose(&e), None);
        e.loader =
            format!("{COMMIT}2026-10-21 07:05:50.000 [1] [offline] module: winex11.drv @ 0x6ffffde80000\n");
        assert_eq!(diagnose(&e), None);
    }

    #[test]
    fn exit_code_0xc0e90002_is_app_control() {
        let mut e = ev("", &[]);
        e.code = Some(APP_CONTROL);
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "app_control");
        assert!(
            d.steps[0].contains("Smart App Control settings, and choose Off"),
            "{}",
            d.steps[0]
        );
    }

    const RTSS: &str = "\
2026-10-27 17:37:40.100 [1] [crash] #1 first-chance (the game may still handle it) ACCESS_VIOLATION (0xc0000005) on thread 1, 3 s after load, at ntdll.dll+0x4a612
2026-10-27 17:37:40.100 [1] [crash]   reading 0xffffffffffffffff
2026-10-27 17:37:40.100 [1] [crash]   stack:
2026-10-27 17:37:40.100 [1] [crash]   #0  ntdll.dll+0x4a612
2026-10-27 17:37:40.100 [1] [crash]   #1  ntdll.dll+0x1d3b2
2026-10-27 17:37:40.100 [1] [crash]   #2  RTSSHooks64.dll+0x2a1b0
2026-10-27 17:37:40.100 [1] [crash]   #3  RTSSHooks64.dll+0x2a0f0
";

    #[test]
    fn a_first_chance_crash_without_an_unhandled_line_still_counts() {
        let mut e = ev(RTSS, &[]);
        e.code = Some(0xc000_0005);
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "third_party_hook");
        assert!(d.title.contains("RivaTuner"), "{}", d.title);
        e.code = Some(0xc000_0409);
        assert_eq!(diagnose(&e), None);
        e.code = None;
        assert_eq!(diagnose(&e), None);
    }

    fn car_pack(winner: &str, loser: &str, resources: usize) -> Pair {
        Pair {
            winner: winner.into(),
            loser: loser.into(),
            resources,
            items: vec![
                "2025_asset_groups/f1_2025_vehicle_package/teams/ferrari/wep/ferrari.erp:eaid://ferrari.idf"
                    .into(),
            ],
            ..Pair::default()
        }
    }

    #[test]
    fn a_removed_gpu_names_car_packs_and_frame_generation() {
        let mut e = with_names(
            ev(GPU, &["F1_25.exe"]),
            &[
                ("mod.f1-2016", "F1 2016 Cars"),
                ("mod.f1-2018", "F1 2018 Cars"),
                ("mod.livery", "Livery"),
            ],
        );
        e.conflicts = vec![
            car_pack("mod.f1-2018", "mod.f1-2016", 8902),
            car_pack("mod.livery", "mod.f1-2018", 12),
        ];
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "gpu_removed");
        assert!(!d.what.contains("not from Swofty's code"), "{}", d.what);
        assert!(
            d.what
                .contains("F1 2016 Cars and F1 2018 Cars are all full car packs"),
            "{}",
            d.what
        );
        assert_eq!(
            d.steps[0],
            "Keep only one of F1 2016 Cars and F1 2018 Cars on in your profile."
        );
        e.conflicts.clear();
        e.loader = format!(
            "2026-10-24 02:50:00.000 [1] [overlay] the game's hardware settings (x.xml) have frame generation on (frame_gen mode 1, multi_frame_gen 1)\n{GPU}"
        );
        let d = diagnose(&e).unwrap();
        assert!(d.what.contains("Frame generation was on"), "{}", d.what);
        assert_eq!(d.steps[0], FRAME_GEN_OFF);
        assert_eq!(
            d.steps.iter().filter(|s| s.contains("frame generation")).count(),
            1
        );
    }

    #[test]
    fn the_games_low_disk_dialog_names_the_drive_and_the_space() {
        let log = "2026-10-27 22:08:24.555 [ 3820] dialog shown: Insufficient Storage: 652/2048 MiB available on \
                   'C:\\', please free some space.\n22:08:32.240 [stub] [offline] game exited with code 4294967295\n";
        let d = diagnose(&ev(log, &[])).unwrap();
        assert_eq!(d.id, "low_disk");
        assert_eq!(d.title, "F1 25 needs more free space on drive C");
        assert_eq!(
            d.what,
            "F1 25 needs 2 GB free on drive C and found 0.6 GB, so it showed Insufficient Storage and closed. It \
             checks drive C even when the game is installed on another drive."
        );
        assert!(
            d.steps[0].starts_with("Free up at least 1.4 GB on drive C: empty"),
            "{}",
            d.steps[0]
        );
        let other = log.replace("652/2048", "2000/2048").replace("'C:", "'d:");
        let d = diagnose(&ev(&other, &[])).unwrap();
        assert_eq!(
            d.what,
            "F1 25 needs 2 GB free on drive D and found 1.9 GB, so it showed Insufficient Storage and closed."
        );
        assert!(
            d.steps[0].starts_with("Free up at least 0.1 GB on drive D: empty"),
            "{}",
            d.steps[0]
        );
        let path = log.replace("'C:\\'", "'\\\\server\\share'");
        assert!(
            diagnose(&ev(&path, &[]))
                .unwrap()
                .what
                .contains("on the drive F1 25 saves to")
        );
        assert_eq!(diagnose(&ev(&log.replace("652/2048", "lots"), &[])), None);
    }

    fn pitlane_log(kept: &str, mode: &str, address: &str) -> String {
        format!(
            "2026-10-27 19:26:15.735 [30872] [pitlane] your car: flags 0 -> 0x1, mode 0 -> 1
2026-10-27 19:26:15.741 [30872] [pitlane] pit lane: control kept (7 this session){kept}
2026-10-27 19:26:15.761 [30872] [pitlane] your car: flags 0x1 -> 0, mode 1 -> {mode}
2026-10-27 19:26:15.767 [12860] [crash] #1 first-chance (the game may still handle it) ACCESS_VIOLATION (0xc0000005) on thread 12860, 2079 s after load, at F1_25.exe+{address} .xtls fn base+0x389a120+0xb7
2026-10-27 19:26:15.767 [12860] [crash]   reading 0x2d3bf4f40
2026-10-27 19:26:15.767 [12860] [crash]   stack:
2026-10-27 19:26:15.767 [12860] [crash]   #0  F1_25.exe+{address} .xtls fn base+0x389a120+0xb7
2026-10-27 19:26:15.767 [12860] [crash]   #1  F1_25.exe+0x445b18c .xtls fn base+0x445aea0+0x2ec
2026-10-27 19:26:15.768 [12860] [crash] unhandled ACCESS_VIOLATION (0xc0000005) at F1_25.exe+{address} .xtls fn base+0x389a120+0xb7 reached the top-level filter; passing it to the game's filter
"
        )
    }

    #[test]
    fn a_crash_as_manual_pit_lane_takes_the_car_names_it() {
        let e = with_names(
            ev(&pitlane_log("", "1474065524", "0x389a1d7"), &[]),
            &[("kizi.pitlane", "Manual Pit Lane")],
        );
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "pitlane_garage");
        assert_eq!(d.title, "Manual Pit Lane crashed F1 25 in the pit lane");
        assert!(d.what.contains("Return to Garage or a flashback"), "{}", d.what);
        assert_eq!(
            d.steps[0],
            "Turn off Manual Pit Lane in your profile until its author fixes this crash."
        );
        let older_game = pitlane_log("", "1159247034", "0x38873a7");
        assert_eq!(diagnose(&ev(&older_game, &[])).unwrap().id, "pitlane_garage");
        let kept_only = pitlane_log("", "0", "0x389a1d7");
        assert_eq!(diagnose(&ev(&kept_only, &[])).unwrap().id, "pitlane_garage");
        let ai_kept = pitlane_log(
            " - another flag is set too, so the AI keeps the car for now",
            "0",
            "0x389a1d7",
        );
        assert_eq!(diagnose(&ev(&ai_kept, &[])), None);
        assert_eq!(diagnose(&ev(&pitlane_log("", "0", "0x1234567"), &[])), None);
        let later = pitlane_log("", "1474065524", "0x389a1d7").replace("19:26:15.7", "19:26:19.7");
        let later = later.replacen("19:26:19.7", "19:26:15.7", 3);
        assert_eq!(diagnose(&ev(&later, &[])), None);
    }

    /// The game folder's file names among the crash report's loaded modules.
    fn game_folder_modules(report: &str) -> Vec<String> {
        let exe = report
            .lines()
            .find_map(|l| l.strip_prefix("exe")?.trim().split(", ").next())
            .unwrap_or_default();
        let Some((folder, _)) = exe.rsplit_once('\\') else {
            return Vec::new();
        };
        report
            .lines()
            .filter_map(|l| l.trim().split_once("  ").and(l.find(folder)).map(|i| &l[i..]))
            .filter_map(|p| p.strip_prefix(folder)?.strip_prefix('\\'))
            .filter(|rest| !rest.contains('\\'))
            .map(str::to_owned)
            .collect()
    }

    const LIST_SEARCH: &str = "\
2026-10-28 01:49:51.119 [15588] [crash] #1 first-chance (the game may still handle it) ACCESS_VIOLATION (0xc0000005) on thread 15588, 237 s after load, at F1_25.exe+0x1738f30 .xtls fn base+0x1738eb0+0x80
2026-10-28 01:49:51.119 [15588] [crash]   rsi=000000006b18b9ca rdi=0000000021500d09 rbp=0000000000000000 rsp=000000006b18b940
2026-10-28 01:49:51.119 [15588] [crash]   #0  F1_25.exe+0x1738f30 .xtls fn base+0x1738eb0+0x80
2026-10-28 01:49:51.119 [15588] [crash]   #1  F1_25.exe+0x17384ec .xtls fn base+0x17382b0+0x23c
2026-10-28 01:49:51.120 [15588] [crash] unhandled ACCESS_VIOLATION (0xc0000005) at F1_25.exe+0x1738f30 .xtls fn base+0x1738eb0+0x80 reached the top-level filter; passing it to the game's filter
";

    const LIST_REPORT: &str = "\
== Exception
  ACCESS_VIOLATION (0xc0000005), flags 0x0, at F1_25.exe+0x1738f30 .xtls fn base+0x1738eb0+0x80

== Memory at the registers
  rcx=000000000715a9d1: committed private region 0x715a000 +0x238a6000 (allocation 0x6c90000), read/write
    000000000715a9d0> 66 31 5f 32 30 32 36 5f 64 72 69 76 65 72 5f 70  f1_2026_driver_p
  rdi=0000000021500d09: committed private region 0x21500000 +0x9500000 (allocation 0x6c90000), read/write
    0000000021500cf0  64 00 95 9e 01 00 00 00 f0 40 6a 21 00 00 00 00  d........@j!....
    0000000021500d00> 66 17 e9 2a 01 00 07 00 c0 66 32 5f 32 30 32 36  f..*.....f2_2026
    0000000021500d10  00 6e 5f 70 61 63 6b 61 67 65 00 17 00 00 00 00  .n_package......
  r14=cb42b7f752c18f53: not a valid address
";

    #[test]
    fn a_missing_entrant_list_names_the_list_and_the_addon_the_build_saw() {
        let mut e = ev(LIST_SEARCH, &["F1_25.exe"]);
        e.report = Some(LIST_REPORT.to_owned());
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "missing_entrant_list");
        assert_eq!(d.title, "F1 25 crashed looking for the entrant list f2_2026");
        assert!(
            d.steps[0].starts_with("Turn off the database addons"),
            "{:?}",
            d.steps
        );

        e = with_names(e, &[("mod.old-team-pack", "Old Team Pack")]);
        e.kept_lists = BTreeMap::from([("mod.old-team-pack".to_owned(), vec!["f2_2026".to_owned()])]);
        let d = diagnose(&e).unwrap();
        assert!(
            d.what
                .ends_with("The last build found Old Team Pack renaming or removing it."),
            "{}",
            d.what
        );
        assert_eq!(
            d.steps[0],
            "Turn off Old Team Pack, or ask its author for a version made for this F1 25 update."
        );

        e.report = None;
        let d = diagnose(&e).unwrap();
        assert_eq!(d.title, "F1 25 crashed looking for an entrant list");
        let other = LIST_SEARCH.replace("+0x1738f30", "+0x1738f31");
        assert_ne!(
            diagnose(&ev(&other, &["F1_25.exe"])).map(|d| d.id),
            Some("missing_entrant_list")
        );
    }

    /// `SWOFTY_KP_CASES=<folder of extracted Send Logs bundles> cargo test -p swofty-core --lib
    /// diagnoses_real_bundles -- --ignored --nocapture` prints each crashed session's diagnosis,
    /// and each other session a rule recognises.
    #[test]
    fn unmatched_crash_on_old_build_names_the_update() {
        let mut e = ev("", &[]);
        assert_eq!(diagnose(&e), None);
        e.game_version = Some("1.0.136.6820".to_owned());
        let d = diagnose(&e).unwrap();
        assert_eq!(d.id, "outdated_exe");
        assert!(d.what.contains("1.0.136.6820"));
        assert!(d.text().contains("Steam or the EA app"));
        e.game_version = Some(LATEST_GAME_VERSION.to_owned());
        assert_eq!(diagnose(&e), None);
    }

    #[test]
    #[ignore]
    fn diagnoses_real_bundles() {
        let Some(root) = std::env::var_os("SWOFTY_KP_CASES") else {
            return;
        };
        let dirs = |p: &Path| -> Vec<std::path::PathBuf> {
            let mut v: Vec<_> = std::fs::read_dir(p)
                .into_iter()
                .flatten()
                .flatten()
                .map(|e| e.path())
                .filter(|p| p.is_dir())
                .collect();
            v.sort();
            v
        };
        let json = |p: &Path| -> serde_json::Value {
            read_capped(p)
                .and_then(|t| serde_json::from_str(&t).ok())
                .unwrap_or_default()
        };
        for case in dirs(Path::new(&root)) {
            let logs = case.join("logs");
            let addons = json(&logs.join("addons.json"));
            let installed = addons["installed"].as_array().cloned().unwrap_or_default();
            let names = installed
                .iter()
                .filter_map(|a| Some((a["id"].as_str()?.to_owned(), a["name"].as_str()?.to_owned())))
                .collect();
            let conflicts: crate::conflicts::Report =
                serde_json::from_value(json(&logs.join("conflicts.json"))).unwrap_or_default();
            let tracks = conflicts
                .order
                .iter()
                .filter(|id| {
                    installed
                        .iter()
                        .any(|a| a["id"].as_str() == Some(id.as_str()) && a["category"] == "tracks")
                })
                .cloned()
                .collect();
            for session in dirs(&logs.join("sessions")) {
                let record = SessionRecord::read(&session);
                let loader = ["loader.log", "game-logs/loader.log"]
                    .iter()
                    .find_map(|rel| read_capped(&session.join(rel)))
                    .unwrap_or_default();
                let crashed = record.crashed()
                    || loader.contains("reached the top-level filter")
                    || loader.contains("[crash] crash report ")
                    || loader.contains(LOW_DISK);
                let report = report_name(&loader).and_then(|n| read_capped(&logs.join("crash").join(n)));
                let game_files = report.as_deref().map(game_folder_modules).unwrap_or_default();
                let mut e = Evidence::read(&record, None, BTreeMap::new());
                let game_version = read_capped(&logs.join("summary.txt")).and_then(|t| {
                    t.lines()
                        .find_map(|l| l.strip_prefix("F1 25 "))
                        .map(|v| v.trim().to_owned())
                });
                e = Evidence {
                    game_version,
                    loader,
                    report,
                    game_files,
                    names: BTreeMap::clone(&names),
                    enabled: conflicts.order.clone(),
                    tracks: Vec::clone(&tracks),
                    conflicts: conflicts.pairs.clone(),
                    kept_lists: conflicts.kept_lists.clone(),
                    ..e
                };
                let found = match crashed {
                    true => diagnose(&e),
                    false => stalled(&e),
                };
                if !crashed && found.is_none() {
                    continue;
                }
                let found = found.map_or_else(
                    || "-".to_owned(),
                    |d| format!("{}\t{}\t{}", d.id, d.title, d.text()),
                );
                println!(
                    "KP\t{}\t{}\t{found}",
                    case.file_name().unwrap().to_string_lossy(),
                    session.file_name().unwrap().to_string_lossy()
                );
            }
        }
    }

    const APPLY_FAILED: &str = "applying swofty.classic-seasons: eaid://ui_package/databases/overrides.uidb: \
                                overrides.Contents.teams.Contents.mp_team_select_logos.Contents: no entry \
                                [Name=vanamersfoort_26]";

    fn classic_seasons() -> BTreeMap<String, String> {
        BTreeMap::from([("swofty.classic-seasons".to_owned(), "Classic Seasons".to_owned())])
    }

    #[test]
    fn game_versions_compare_by_number() {
        assert_eq!(older_game("1.0.138.5601", LATEST_GAME_VERSION), Some(true));
        assert_eq!(older_game("1.0.136.6820", "1.0.141.7865"), Some(true));
        assert_eq!(older_game("1.0.141.7865", LATEST_GAME_VERSION), Some(false));
        assert_eq!(older_game("1.0.142.1", LATEST_GAME_VERSION), Some(false));
        assert_eq!(older_game("1.0.141.10000", "1.0.141.7865"), Some(false));
        assert_eq!(older_game("1.0.99.9999", "1.0.141.7865"), Some(true));
        assert_eq!(older_game(" 1.0.137.2811 ", LATEST_GAME_VERSION), Some(true));
        for unknown in [
            "",
            "1.0.138",
            "1.0.138.5601.2",
            "1.0.x.5601",
            "25251167",
            "1..138.5601",
            "1.0.+8.5601",
        ] {
            assert_eq!(older_game(unknown, LATEST_GAME_VERSION), None, "{unknown}");
        }
    }

    #[test]
    fn an_addon_that_does_not_apply_to_an_old_game_names_the_update() {
        let d = outdated_game(APPLY_FAILED, Some("1.0.136.6820"), &classic_seasons()).unwrap();
        assert_eq!(d.id, "outdated_game");
        assert_eq!(d.title, "Classic Seasons needs the latest F1 25");
        assert_eq!(
            d.text(),
            "Classic Seasons needs F1 25 1.0.141.7865, the latest update. Your F1 25 is 1.0.136.6820. Update F1 \
             25 in Steam or the EA app, then press Play again."
        );
        let wrapped = format!("the wrapper exited (exit code: 1) before the game started: {APPLY_FAILED}");
        let d = outdated_game(&wrapped, Some("1.0.138.5601"), &BTreeMap::new()).unwrap();
        assert!(
            d.what
                .starts_with("swofty.classic-seasons needs F1 25 1.0.141.7865"),
            "{}",
            d.what
        );
        let item = APPLY_FAILED.replace("classic-seasons:", "classic-seasons (2018):");
        let d = outdated_game(&item, Some("1.0.137.2811"), &classic_seasons()).unwrap();
        assert!(d.what.starts_with("Classic Seasons needs"), "{}", d.what);
    }

    #[test]
    fn other_games_and_other_errors_keep_their_own_message() {
        let names = classic_seasons();
        assert_eq!(
            outdated_game(APPLY_FAILED, Some(LATEST_GAME_VERSION), &names),
            None
        );
        assert_eq!(outdated_game(APPLY_FAILED, Some("1.0.142.1"), &names), None);
        assert_eq!(outdated_game(APPLY_FAILED, None, &names), None);
        assert_eq!(outdated_game(APPLY_FAILED, Some("unknown"), &names), None);
        for other in [
            "version.dll already exists in the game folder; run swofty restore",
            "building profile default: reading x: missing",
            "downloading the offline play extension: offline play 0.3.11: the download was blocked",
            "applying : no entry",
        ] {
            assert_eq!(
                outdated_game(other, Some("1.0.136.6820"), &names),
                None,
                "{other}"
            );
        }
    }

    #[test]
    fn an_older_game_is_told_to_update_before_play() {
        let d = outdated_exe(Some("1.0.138.5601")).unwrap();
        assert_eq!(d.id, "outdated_exe");
        assert!(d.what.starts_with("Your F1 25 is 1.0.138.5601."), "{}", d.what);
        assert!(
            d.text().contains("Update F1 25 in Steam or the EA app"),
            "{}",
            d.text()
        );
        assert!(d.text().contains("steam://validate/3059520"), "{}", d.text());
        assert_eq!(outdated_exe(Some(LATEST_GAME_VERSION)), None);
        assert_eq!(outdated_exe(Some("1.0.142.1")), None);
        assert_eq!(outdated_exe(Some("unknown")), None);
        assert_eq!(outdated_exe(None), None);
    }

    #[test]
    fn modules_the_offline_hook_lists_are_read() {
        let log = "2026-10-25 19:18:28.000 [1] [offline] module: RTSSHooks64.dll @ 0x7dd50000\n";
        assert_eq!(loaded_modules(log), ["RTSSHooks64.dll"]);
    }
}
