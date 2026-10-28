//! Launches that fail without a crash. F1 25 runs one copy at a time: a copy
//! started while another runs finds the other's single-instance mutex and
//! closes itself, cleanly and before its window opens. To the player that
//! looks like the mods never loaded, since the copy left on screen is one the
//! launcher did not start. The session refuses to start the game while a copy
//! is running, and after a clean exit before the window it works out why the
//! game closed. What it finds is recorded here for the launcher to show and
//! for Send Logs.

use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};

use crate::app_control::Block;
use crate::quarantine::ATTACHED;
use crate::{Result, io_err, read_json, write_json};

/// A clean exit this soon after the start, before the window, is a launch
/// that failed. A player quitting from the menu has been in the game longer.
pub const EARLY_EXIT: Duration = Duration::from_secs(15);
/// The loader's line when the game's single-instance mutex already existed.
pub const SECOND_COPY: &str = "single instance: ";
/// The loader's lines once the game reached its window: the window stage
/// after an early load, or the window trigger itself.
const WINDOW: &[&str] = &[
    "stage: window (",
    "trigger: USER32!",
    "trigger: main window visible",
];
const PID: &str = "attached, pid ";
/// The core's line for an exception no handler took, and the session
/// launcher's line once the game it created has exited.
const UNHANDLED: &str = "[crash] unhandled ";
const TOP_LEVEL: &str = "reached the top-level filter";
const STUB_EXIT: &str = "[stub] [offline] game exited with code ";
/// The loader's line for F1 25's dialog when a drive it writes to is nearly full.
pub const LOW_DISK: &str = "dialog shown: Insufficient Storage: ";

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Cause {
    /// A copy of F1 25 was running when the session was about to start one.
    AlreadyRunning { pid: u32 },
    /// The game closed itself as it started because another copy was running.
    SecondCopy {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        pid: Option<u32>,
    },
    /// The game closed itself cleanly before its window, with no other copy.
    ClosedEarly { after: f64 },
    /// Windows refused to start one of Swofty's programs.
    Blocked(Block),
    /// The player closed the OverlayFS fallback dialog with Exit.
    OverlayCancelled {
        fallback: crate::overlayfs::Kind,
        reason: String,
    },
    /// An EA app repair replaced offline play's file, so the game started
    /// without mods; the session put the file back.
    EaRepaired,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Failure {
    pub at: f64,
    pub cause: Cause,
}

impl Failure {
    pub fn new(cause: Cause) -> Self {
        Self {
            at: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map_or(0.0, |d| d.as_secs_f64()),
            cause,
        }
    }

    pub fn path(data: &Path) -> PathBuf {
        data.join("state/launch-failed.json")
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

    pub fn title(&self) -> &'static str {
        match &self.cause {
            Cause::AlreadyRunning { .. } => "F1 25 is already running",
            Cause::SecondCopy { .. } => "F1 25 was opened twice",
            Cause::ClosedEarly { .. } => "F1 25 closed as it started",
            Cause::Blocked(b) => b.title(),
            Cause::OverlayCancelled { .. } => "F1 25 was not started",
            Cause::EaRepaired => "The EA app repaired F1 25",
        }
    }

    /// One line for the launcher's status bar.
    pub fn headline(&self) -> String {
        match &self.cause {
            Cause::AlreadyRunning { .. } => {
                "F1 25 was already running, so the modded copy was not started".into()
            }
            Cause::SecondCopy { .. } => "another copy of F1 25 was running, so the modded copy closed".into(),
            Cause::ClosedEarly { .. } => "F1 25 closed itself before its window opened".into(),
            Cause::Blocked(b) => b.headline(),
            Cause::OverlayCancelled { .. } => {
                "the launch was cancelled in the OverlayFS dialog, so F1 25 was not started".into()
            }
            Cause::EaRepaired => {
                "the EA app's repair replaced offline play's file, so F1 25 started without mods".into()
            }
        }
    }

    pub fn text(&self) -> String {
        let close = if cfg!(windows) {
            "Close F1 25 and check in Task Manager that no F1_25.exe is left"
        } else {
            "Close F1 25 and check that no F1_25.exe process is left"
        };
        match &self.cause {
            Cause::AlreadyRunning { pid } => format!(
                "The modded session was not started: another copy of F1 25 is running (process {pid}), \
                 and F1 25 runs one copy at a time. {close}, then press Play again."
            ),
            Cause::SecondCopy { pid } => format!(
                "The modded copy closed itself as it started, because another copy of F1 25 was already \
                 running{}. F1 25 runs one copy at a time, so the game you see has no mods. This happens \
                 when F1 25 is started again, from Steam or a shortcut, while the launcher is getting \
                 ready. {close}, then press Play once and wait for the game window.",
                pid.map(|p| format!(" (process {p})")).unwrap_or_default()
            ),
            Cause::ClosedEarly { after } => format!(
                "F1 25 closed itself {after:.0} s after it started, before its window opened, without an \
                 error. No other copy of F1 25 was running. Press Play again. If it keeps closing, use \
                 Send Logs to Developers right after it happens."
            ),
            Cause::Blocked(b) => b.text(),
            Cause::OverlayCancelled { reason, .. } => format!(
                "The launch was cancelled: OverlayFS could not be used ({reason}), and Exit was chosen \
                 instead of copying the mod files into the game folder. Nothing was changed. Press Play \
                 again and choose Continue to launch by copying files, or remove --use-overlayfs from the \
                 Steam launch option."
            ),
            Cause::EaRepaired => {
                "The EA app's repair replaced offline play's file in the F1 25 folder, so F1 25 \
                 started without mods. The launcher put the file back. Close F1 25, then press Play in the \
                 launcher again."
                    .into()
            }
        }
    }
}

/// What the session's loader.log shows about the game's start and end.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LoaderSeen {
    pub attached: bool,
    /// The game process's id as Windows sees it (a Wine id under Proton).
    pub pid: Option<u32>,
    pub window: bool,
    pub second_copy: bool,
    /// The code of the first exception that reached the top-level filter.
    pub unhandled: Option<u32>,
    /// The game's exit code as the session launcher read it (its last line).
    pub stub_exit: Option<u32>,
    /// The game showed its own Insufficient Storage dialog.
    pub low_disk: bool,
}

impl LoaderSeen {
    pub fn scan(text: &str) -> Self {
        let mut seen = Self::default();
        for line in text.lines() {
            if line.contains(ATTACHED) {
                seen.attached = true;
                seen.pid = line
                    .split_once(PID)
                    .and_then(|(_, rest)| rest.split(|c: char| !c.is_ascii_digit()).next()?.parse().ok());
            }
            seen.window |= WINDOW.iter().any(|w| line.contains(w));
            seen.second_copy |= line.contains(SECOND_COPY);
            seen.low_disk |= line.contains(LOW_DISK);
            if seen.unhandled.is_none() && line.contains(UNHANDLED) && line.contains(TOP_LEVEL) {
                seen.unhandled = line.split_once("(0x").and_then(|(_, rest)| {
                    u32::from_str_radix(rest.split(|c: char| !c.is_ascii_hexdigit()).next()?, 16).ok()
                });
            }
            if let Some((_, code)) = line.split_once(STUB_EXIT) {
                seen.stub_exit = code.trim().parse().ok().or(seen.stub_exit);
            }
        }
        seen
    }
}

/// Whether a game that exited with `code` after `ran` failed to launch, and
/// why. `others` lists the copies of F1 25 the session did not start; it is
/// only called for an exit that looks like a failed launch.
pub fn classify(
    code: i32,
    ran: Duration,
    loader: Option<&LoaderSeen>,
    others: impl FnOnce() -> Vec<u32>,
) -> Option<Cause> {
    let loader = loader.copied().unwrap_or_default();
    if loader.window {
        return None;
    }
    if loader.second_copy {
        return Some(Cause::SecondCopy {
            pid: others().first().copied(),
        });
    }
    if code != 0 || ran >= EARLY_EXIT {
        return None;
    }
    Some(match others().first() {
        Some(&pid) => Cause::SecondCopy { pid: Some(pid) },
        None => Cause::ClosedEarly {
            after: ran.as_secs_f64(),
        },
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const EARLY_LOG: &str = "\
[14:13:47.100] swofty loader (version.dll proxy) attached, pid 25512
[14:13:47.300] trigger: KERNEL32!CreateMutexW from 0x140178667 (.xtls) name 'Codemasters F1 25' on thread 29952, 120 ms after attach; loading addons now (early stage)
[14:13:47.905] addons: done, 4 addon(s) loaded, 4 initialised
[14:13:47.929] [crash] ExitProcess(0) on thread 29952, 0 s after load, before the game's window; caller stack:
";

    fn secs(s: f64) -> Duration {
        Duration::from_secs_f64(s)
    }

    #[test]
    fn reads_the_loader_log() {
        let seen = LoaderSeen::scan(EARLY_LOG);
        assert_eq!(
            seen,
            LoaderSeen {
                attached: true,
                pid: Some(25512),
                window: false,
                second_copy: false,
                unhandled: None,
                stub_exit: None,
                low_disk: false,
            }
        );
        let window = format!(
            "{EARLY_LOG}[14:13:52.000] stage: window (USER32!RegisterClassExW from 0x14000 (.xtls)), addons were loaded at the early stage\n"
        );
        assert!(LoaderSeen::scan(&window).window);
        let late = "trigger: USER32!RegisterClassExW from 0x1400 (.xtls) class 'EGO_WindowClass' on thread 1, 9 ms after attach; loading addons now";
        assert!(LoaderSeen::scan(late).window);
        let second = format!(
            "{EARLY_LOG}single instance: the game's mutex 'Codemasters F1 25' already exists, so another copy of F1 25 is running; F1 25 closes a second copy\n"
        );
        assert!(LoaderSeen::scan(&second).second_copy);
    }

    #[test]
    fn reads_the_crash_and_the_session_launchers_exit_code() {
        let crashed = "\
11:52:12.282 [stub] [offline] session launcher invoked, args: <none>
2026-10-23 19:09:16.184 [22104] [crash] #1 first-chance (the game may still handle it) ACCESS_VIOLATION (0xc0000005) on thread 22104, 500 s after load, at F1_25.exe+0xb9ae23 .xtls fn base+0xb9a810+0x613
2026-10-23 19:09:16.185 [22104] [crash] unhandled ACCESS_VIOLATION (0xc0000005) at F1_25.exe+0xb9ae23 .xtls fn base+0xb9a810+0x613 reached the top-level filter; passing it to the game's filter
2026-10-23 19:09:16.185 [39408] [crash] writing a crash report: unhandled ACCESS_VIOLATION (0xc0000005) reading 0x80 at F1_25.exe+0xb9ae23 .xtls fn base+0xb9a810+0x613
2026-10-23 19:09:16.190 [22104] [crash] unhandled STACK_OVERFLOW (0xc00000fd) at F1_25.exe+0x10 reached the top-level filter; passing it to the game's filter
2026-10-23 19:09:24.102 [22104] [crash] TerminateProcess(4294967295) on thread 22104, 508 s after load; caller stack:
19:09:27.164 [stub] [offline] game exited with code 4294967295
";
        let seen = LoaderSeen::scan(crashed);
        assert_eq!(seen.unhandled, Some(0xc000_0005));
        assert_eq!(seen.stub_exit, Some(0xffff_ffff));
        let quit = "\
2026-10-22 22:36:34.564 [ 7928] [crash] TerminateProcess(0) on thread 7928, 4576 s after load
22:36:36.278 [stub] [offline] game exited with code 0
";
        assert_eq!(LoaderSeen::scan(quit).unhandled, None);
        assert_eq!(LoaderSeen::scan(quit).stub_exit, Some(0));
        let relaunched = "15:44:16.470 [stub] [offline] game exited with code 1\n\
                          15:50:02.001 [stub] [offline] game exited with code 0\n";
        assert_eq!(LoaderSeen::scan(relaunched).stub_exit, Some(0));
        assert_eq!(LoaderSeen::scan(EARLY_LOG).stub_exit, None);
    }

    #[test]
    fn an_early_clean_exit_with_another_copy_is_a_second_copy() {
        let seen = LoaderSeen::scan(EARLY_LOG);
        assert_eq!(
            classify(0, secs(4.1), Some(&seen), || vec![31337]),
            Some(Cause::SecondCopy { pid: Some(31337) })
        );
    }

    #[test]
    fn the_loader_seeing_the_mutex_taken_settles_it() {
        let seen = LoaderSeen {
            second_copy: true,
            ..LoaderSeen::scan(EARLY_LOG)
        };
        assert_eq!(
            classify(0, secs(40.0), Some(&seen), Vec::new),
            Some(Cause::SecondCopy { pid: None })
        );
        assert_eq!(
            classify(1, secs(2.0), Some(&seen), || vec![8]),
            Some(Cause::SecondCopy { pid: Some(8) })
        );
    }

    #[test]
    fn an_early_clean_exit_alone_closed_early() {
        assert_eq!(
            classify(0, secs(4.1), Some(&LoaderSeen::scan(EARLY_LOG)), Vec::new),
            Some(Cause::ClosedEarly { after: 4.1 })
        );
        assert_eq!(
            classify(0, secs(3.0), None, Vec::new),
            Some(Cause::ClosedEarly { after: 3.0 })
        );
    }

    #[test]
    fn normal_quits_and_crashes_are_not_launch_failures() {
        let played = LoaderSeen {
            window: true,
            ..LoaderSeen::scan(EARLY_LOG)
        };
        let never = || -> Vec<u32> { panic!("no process check for an exit that is not a failed launch") };
        assert_eq!(classify(0, secs(3600.0), Some(&played), never), None);
        assert_eq!(classify(0, secs(5.0), Some(&played), never), None);
        assert_eq!(classify(0, secs(20.0), None, never), None);
        assert_eq!(
            classify(0, EARLY_EXIT, Some(&LoaderSeen::scan(EARLY_LOG)), never),
            None
        );
        assert_eq!(classify(-1073741819, secs(3.0), None, never), None);
        assert_eq!(
            classify(255, secs(3.0), Some(&LoaderSeen::scan(EARLY_LOG)), never),
            None
        );
    }

    #[test]
    fn texts_name_the_process_and_the_fix() {
        let f = Failure::new(Cause::SecondCopy { pid: Some(25600) });
        assert_eq!(f.title(), "F1 25 was opened twice");
        assert!(
            f.text()
                .contains("another copy of F1 25 was already running (process 25600)")
        );
        assert!(f.text().contains("press Play once and wait for the game window"));
        assert!(
            !Failure::new(Cause::SecondCopy { pid: None })
                .text()
                .contains("(process")
        );
        let r = Failure::new(Cause::AlreadyRunning { pid: 4242 });
        assert!(r.text().starts_with(
            "The modded session was not started: another copy of F1 25 is running (process 4242)"
        ));
        let c = Failure::new(Cause::ClosedEarly { after: 4.1 });
        assert!(
            c.text()
                .starts_with("F1 25 closed itself 4 s after it started, before its window opened")
        );
    }

    #[test]
    fn round_trips_through_the_state_file() {
        let root = std::env::temp_dir().join(format!("swofty-launch-failed-{}", std::process::id()));
        std::fs::create_dir_all(root.join("state")).unwrap();
        let f = Failure {
            at: 1_792_594_036.5,
            cause: Cause::SecondCopy { pid: Some(7) },
        };
        f.write(&root).unwrap();
        assert_eq!(Failure::read(&root), Some(f));
        let text = std::fs::read_to_string(Failure::path(&root)).unwrap();
        assert!(text.contains("\"kind\": \"second_copy\""), "{text}");
        Failure::clear(&root).unwrap();
        assert_eq!(Failure::read(&root), None);
        Failure::clear(&root).unwrap();
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_blocked_program_names_itself_and_round_trips() {
        let root = std::env::temp_dir().join(format!("swofty-launch-blocked-{}", std::process::id()));
        std::fs::create_dir_all(root.join("state")).unwrap();
        let block = Block::from_code(Path::new(r"C:\Swofty\bin\swofty.exe"), 4551).unwrap();
        let f = Failure {
            at: 1_792_594_036.5,
            cause: Cause::Blocked(block.clone()),
        };
        assert_eq!(f.title(), "Windows blocked a Swofty program");
        assert_eq!(f.headline(), block.headline());
        assert_eq!(f.text(), block.text());
        f.write(&root).unwrap();
        assert_eq!(Failure::read(&root), Some(f));
        let text = std::fs::read_to_string(Failure::path(&root)).unwrap();
        assert!(text.contains("\"kind\": \"blocked\""), "{text}");
        assert!(text.contains("\"code\": 4551"), "{text}");
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn an_ea_app_repair_says_what_to_do_and_round_trips() {
        let root = std::env::temp_dir().join(format!("swofty-launch-ea-repaired-{}", std::process::id()));
        std::fs::create_dir_all(root.join("state")).unwrap();
        let f = Failure {
            at: 1_792_594_036.5,
            cause: Cause::EaRepaired,
        };
        assert_eq!(f.title(), "The EA app repaired F1 25");
        assert!(f.text().contains("started without mods"), "{}", f.text());
        assert!(
            f.text().ends_with("press Play in the launcher again."),
            "{}",
            f.text()
        );
        f.write(&root).unwrap();
        assert_eq!(Failure::read(&root), Some(f));
        let text = std::fs::read_to_string(Failure::path(&root)).unwrap();
        assert!(text.contains("\"kind\": \"ea_repaired\""), "{text}");
        std::fs::remove_dir_all(&root).ok();
    }
}
