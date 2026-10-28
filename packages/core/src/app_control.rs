//! Programs Windows refuses to start. Smart App Control and other app control
//! policies block programs that are not code-signed, which Swofty's programs
//! are not, and an antivirus blocks a program it takes for a threat. A start
//! that fails this way names the program and the code, for the launcher to
//! show and for Send Logs; any other start error is left as it is.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::quarantine::is_virus_code;

pub const ERROR_INVALID_IMAGE_HASH: i32 = 577;
pub const ERROR_ACCESS_DISABLED_NO_SAFER_UI_BY_POLICY: i32 = 786;
pub const ERROR_ACCESS_DISABLED_BY_POLICY: i32 = 1260;
pub const ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION: i32 = 4551;
/// Smart App Control's own codes: a malicious, unwanted or dangerous file,
/// and a file whose reputation it could not check.
pub const SMART_APP_CONTROL: &[i32] = &[4556, 4557, 4558, 4559, 4580, 4581, 4582];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Blocker {
    /// One of Smart App Control's own codes.
    SmartAppControl,
    /// An app control policy: Smart App Control, or one set by whoever
    /// manages the PC.
    AppControl,
    /// A group policy (Software Restriction Policies or AppLocker).
    Policy,
    Antivirus,
}

impl Blocker {
    pub fn from_code(code: i32) -> Option<Self> {
        match code {
            c if SMART_APP_CONTROL.contains(&c) => Some(Self::SmartAppControl),
            ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION | ERROR_INVALID_IMAGE_HASH => Some(Self::AppControl),
            ERROR_ACCESS_DISABLED_BY_POLICY | ERROR_ACCESS_DISABLED_NO_SAFER_UI_BY_POLICY => {
                Some(Self::Policy)
            }
            c if is_virus_code(c) => Some(Self::Antivirus),
            _ => None,
        }
    }
}

/// A program Windows refused to start, and the error code it gave.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Block {
    pub program: PathBuf,
    pub code: i32,
}

impl Block {
    /// The block behind a failed start of `program`; the codes are Windows
    /// ones, so elsewhere there is none.
    pub fn of(program: &Path, e: &std::io::Error) -> Option<Self> {
        Self::from_code(program, e.raw_os_error().filter(|_| cfg!(windows))?)
    }

    pub fn from_code(program: &Path, code: i32) -> Option<Self> {
        Blocker::from_code(code)?;
        Some(Self {
            program: program.to_path_buf(),
            code,
        })
    }

    pub fn blocker(&self) -> Option<Blocker> {
        Blocker::from_code(self.code)
    }

    pub fn name(&self) -> String {
        let path = self.program.to_string_lossy();
        path.rsplit(['/', '\\']).next().unwrap_or(&path).to_owned()
    }

    fn folder(&self) -> String {
        let path = self.program.to_string_lossy();
        path.rsplit_once(['/', '\\'])
            .map_or_else(|| path.to_string(), |(dir, _)| dir.to_owned())
    }

    pub fn title(&self) -> &'static str {
        match self.blocker() {
            Some(Blocker::Antivirus) => "Your antivirus blocked a Swofty program",
            _ => "Windows blocked a Swofty program",
        }
    }

    /// What blocked which program, in one line.
    pub fn headline(&self) -> String {
        let name = self.name();
        match self.blocker() {
            Some(Blocker::SmartAppControl) => format!("Smart App Control blocked {name}"),
            Some(Blocker::Policy) => format!("a Windows policy on this PC blocked {name}"),
            Some(Blocker::Antivirus) => format!("an antivirus blocked {name} as a threat"),
            _ => format!("Smart App Control or an app control policy blocked {name}"),
        }
    }

    pub fn text(&self) -> String {
        let headline = self.headline();
        let what = match headline.split_at_checked(1) {
            Some((first, rest)) => format!("{}{rest}", first.to_ascii_uppercase()),
            None => headline,
        };
        let turn_off = "Swofty's programs aren't code-signed, and Smart App Control can't make an exception \
                        for one app. To use Swofty, turn Smart App Control off in Windows Security > App & \
                        browser control > Smart App Control settings. On some versions of Windows, it can \
                        only be turned back on by resetting Windows.";
        let fix = match self.blocker() {
            Some(Blocker::SmartAppControl) => turn_off.to_owned(),
            Some(Blocker::Policy) => format!(
                "Swofty's programs aren't code-signed, and this policy only lets approved programs run. \
                 Whoever manages this PC can allow the folder {}. Until then, Swofty can't run on it.",
                self.folder()
            ),
            Some(Blocker::Antivirus) => restore(&self.folder()),
            _ => format!(
                "{turn_off} If it is already off, an app control policy set by whoever manages this PC \
                 blocked it."
            ),
        };
        format!("{what} (error {}). {fix}", self.code)
    }
}

fn restore(folder: &str) -> String {
    format!(
        "Swofty's programs aren't code-signed, and some antivirus programs flag them for it. Restore it in \
         Windows Security > Virus & threat protection > Protection history, or in your antivirus, add an \
         exclusion for the folder {folder}, then try again."
    )
}

/// What to do about a Swofty program missing from its folder, which an
/// antivirus most likely removed.
pub fn missing(program: &Path) -> String {
    let path = program.to_string_lossy();
    let (folder, name) = path.rsplit_once(['/', '\\']).unwrap_or((".", &path));
    if !cfg!(windows) {
        return format!("{name} is missing from {folder}. Reinstall Swofty Launcher, then try again.");
    }
    format!(
        "{name} is missing from {folder}, so F1 25 can't be started. An antivirus most likely removed it. {} \
         If it isn't there, reinstall Swofty Launcher after adding the exclusion.",
        restore(folder)
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    const CLI: &str = r"C:\Users\Berni\AppData\Local\Programs\SwoftyLauncher\bin\swofty.exe";

    #[test]
    fn knows_the_codes_that_block_a_start() {
        for code in [4551, 577] {
            assert_eq!(Blocker::from_code(code), Some(Blocker::AppControl), "{code}");
        }
        for code in [4556, 4557, 4558, 4559, 4580, 4581, 4582] {
            assert_eq!(Blocker::from_code(code), Some(Blocker::SmartAppControl), "{code}");
        }
        for code in [1260, 786] {
            assert_eq!(Blocker::from_code(code), Some(Blocker::Policy), "{code}");
        }
        for code in [225, 226] {
            assert_eq!(Blocker::from_code(code), Some(Blocker::Antivirus), "{code}");
        }
        for code in [0, 2, 5, 32, 193, 740, 4550, 4552, 4560] {
            assert_eq!(Blocker::from_code(code), None, "{code}");
            assert_eq!(Block::from_code(Path::new(CLI), code), None, "{code}");
        }
    }

    #[test]
    fn smart_app_control_names_the_program_and_the_way_out() {
        let b = Block::from_code(Path::new(CLI), 4551).unwrap();
        assert_eq!(b.name(), "swofty.exe");
        assert_eq!(b.title(), "Windows blocked a Swofty program");
        assert_eq!(
            b.headline(),
            "Smart App Control or an app control policy blocked swofty.exe"
        );
        let text = b.text();
        assert!(
            text.starts_with("Smart App Control or an app control policy blocked swofty.exe (error 4551). "),
            "{text}"
        );
        assert!(text.contains("aren't code-signed"), "{text}");
        assert!(text.contains("can't make an exception for one app"), "{text}");
        assert!(
            text.contains("Windows Security > App & browser control > Smart App Control settings"),
            "{text}"
        );
        let own = Block::from_code(Path::new(CLI), 4556).unwrap();
        assert!(
            own.text()
                .starts_with("Smart App Control blocked swofty.exe (error 4556). ")
        );
    }

    #[test]
    fn a_policy_or_an_antivirus_names_the_folder() {
        let policy = Block::from_code(Path::new(CLI), 1260).unwrap();
        let text = policy.text();
        assert!(
            text.starts_with("A Windows policy on this PC blocked swofty.exe (error 1260). "),
            "{text}"
        );
        assert!(
            text.contains(r"allow the folder C:\Users\Berni\AppData\Local\Programs\SwoftyLauncher\bin."),
            "{text}"
        );
        let av = Block::from_code(Path::new("/data/extensions/offline-play/f1op-probe.exe"), 225).unwrap();
        assert_eq!(av.title(), "Your antivirus blocked a Swofty program");
        let text = av.text();
        assert!(
            text.starts_with("An antivirus blocked f1op-probe.exe as a threat (error 225). "),
            "{text}"
        );
        assert!(
            text.contains("add an exclusion for the folder /data/extensions/offline-play, then try again"),
            "{text}"
        );
    }

    #[test]
    fn a_missing_program_says_how_to_get_it_back() {
        let text = missing(Path::new(CLI));
        let folder = r"C:\Users\Berni\AppData\Local\Programs\SwoftyLauncher\bin";
        if cfg!(windows) {
            assert!(
                text.starts_with(&format!(
                    "swofty.exe is missing from {folder}, so F1 25 can't be started."
                )),
                "{text}"
            );
            assert!(text.contains("An antivirus most likely removed it."), "{text}");
            assert!(text.contains("Protection history"), "{text}");
            assert!(
                text.contains(&format!(
                    "add an exclusion for the folder {folder}, then try again"
                )),
                "{text}"
            );
            assert!(
                text.ends_with("reinstall Swofty Launcher after adding the exclusion."),
                "{text}"
            );
        } else {
            assert_eq!(
                missing(Path::new("/opt/swofty/bin/swofty")),
                "swofty is missing from /opt/swofty/bin. Reinstall Swofty Launcher, then try again."
            );
        }
    }

    #[test]
    fn only_windows_errors_block() {
        let program = Path::new(CLI);
        assert_eq!(Block::of(program, &std::io::Error::from_raw_os_error(2)), None);
        assert_eq!(Block::of(program, &std::io::Error::other("no code")), None);
        let blocked = Block::of(program, &std::io::Error::from_raw_os_error(4551));
        if cfg!(windows) {
            assert_eq!(blocked, Block::from_code(program, 4551));
        } else {
            assert_eq!(blocked, None);
        }
    }

    #[test]
    fn a_block_round_trips() {
        let b = Block::from_code(Path::new(CLI), 4551).unwrap();
        let json = serde_json::to_string(&b).unwrap();
        assert_eq!(serde_json::from_str::<Block>(&json).unwrap(), b);
    }
}
