use std::path::Path;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use swofty_core::launcher::Progress;
use swofty_core::paths;
use swofty_core::process;
use swofty_core::recovery::{self, SessionLock};
use swofty_core::update;

const PARTS: [(&str, &str); 3] = [
    ("swofty-launcher", "Swofty Launcher"),
    ("swofty-ui", "Swofty UI"),
    ("swofty", "the swofty command line"),
];
pub const CLOSING: &str = "Closing Swofty Launcher";
const GRACE: Duration = Duration::from_secs(10);
const FORCED: Duration = Duration::from_secs(5);

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Running {
    pub pid: u32,
    pub part: &'static str,
}

impl Running {
    fn title(&self) -> &'static str {
        PARTS
            .iter()
            .find(|(n, _)| *n == self.part)
            .map_or(self.part, |(_, t)| *t)
    }
}

pub fn placement(folder: &str) -> String {
    let Ok(root) = crate::resolve(folder) else {
        return String::new();
    };
    if root.join("bin").join(update::exe("swofty-launcher")).is_file() {
        format!(
            "Swofty Launcher is already in {}. Installing updates it in place; your mods and settings stay.",
            root.display()
        )
    } else if root.is_dir() {
        format!("Installs into {}.", root.display())
    } else {
        format!("Creates {} and installs there.", root.display())
    }
}

pub fn game_block() -> Option<&'static str> {
    if process::game_running() {
        return Some("F1 25 is running. Close the game, then select Try again.");
    }
    let data = paths::data_dir();
    (recovery::lock_path(&data).is_file() && SessionLock::held_elsewhere(&data)).then_some(
        "Swofty Launcher is busy with the game folder. Wait for it to finish, then select Try again.",
    )
}

pub fn clear(root: &Path, close: bool, progress: &dyn Progress) -> Result<(), String> {
    if close {
        self::close(root)?;
        progress.report(0.0, crate::FINDING);
    }
    match game_block() {
        Some(why) => Err(why.into()),
        None => Ok(()),
    }
}

pub fn from(root: &Path) -> Vec<Running> {
    let bin = root.join("bin");
    let me = std::process::id();
    processes()
        .into_iter()
        .filter(|(pid, _)| *pid != me)
        .filter_map(|(pid, exe)| {
            Some(Running {
                pid,
                part: part_in(&exe, &bin)?,
            })
        })
        .collect()
}

pub fn anywhere() -> Vec<Running> {
    let me = std::process::id();
    processes()
        .into_iter()
        .filter(|(pid, _)| *pid != me)
        .filter_map(|(pid, exe)| {
            Some(Running {
                pid,
                part: part(&exe)?.0,
            })
        })
        .collect()
}

pub fn data_block() -> Option<String> {
    holding(&anywhere(), process::game_running())
}

pub fn holding(open: &[Running], game: bool) -> Option<String> {
    let mut names: Vec<&str> = PARTS
        .iter()
        .filter(|(n, _)| open.iter().any(|r| r.part == *n))
        .map(|(_, t)| *t)
        .collect();
    if game {
        names.push("F1 25");
    }
    let names = match names.as_slice() {
        [] => return None,
        [one] => (*one).to_owned(),
        [rest @ .., last] => format!("{} and {last}", rest.join(", ")),
    };
    Some(format!(
        "Close {names} before moving the data folder, then select Try again."
    ))
}

fn part(exe: &str) -> Option<(&'static str, &str)> {
    let exe = exe.strip_suffix(" (deleted)").unwrap_or(exe);
    let cut = exe.rfind(['/', '\\'])?;
    let file = &exe[cut + 1..];
    let lower = file.to_ascii_lowercase();
    let windows = lower.strip_suffix(".exe");
    let part = PARTS
        .iter()
        .map(|(n, _)| *n)
        .find(|n| file == *n || windows == Some(*n))?;
    Some((part, &exe[..cut.max(1)]))
}

fn part_in(exe: &str, bin: &Path) -> Option<&'static str> {
    let (part, dir) = part(exe)?;
    process::same_folder(Path::new(dir), bin).then_some(part)
}

pub fn describe(open: &[Running]) -> String {
    let titles: Vec<&str> = PARTS
        .iter()
        .filter(|(n, _)| open.iter().any(|r| r.part == *n))
        .map(|(_, t)| *t)
        .collect();
    let (names, verb) = match titles.as_slice() {
        [one] => (capital(one), "is"),
        [rest @ .., last] => (format!("{} and {last}", capital(&rest.join(", "))), "are"),
        [] => return String::new(),
    };
    format!("{names} {verb} open from this folder and must close before the files are replaced.")
}

fn capital(s: &str) -> String {
    let mut c = s.chars();
    c.next()
        .map(|f| f.to_uppercase().chain(c).collect())
        .unwrap_or_default()
}

pub fn close(root: &Path) -> Result<(), String> {
    let open = from(root);
    for r in &open {
        if !ask(r.pid) {
            force(r.pid);
        }
    }
    let left = wait(root, &open, GRACE);
    for r in &left {
        force(r.pid);
    }
    match wait(root, &left, FORCED).first() {
        None => Ok(()),
        Some(r) => Err(format!(
            "Could not close {} (process {}). Close it, then select Try again.",
            r.title(),
            r.pid
        )),
    }
}

fn wait(root: &Path, open: &[Running], timeout: Duration) -> Vec<Running> {
    let start = Instant::now();
    loop {
        let left: Vec<Running> = from(root).into_iter().filter(|r| open.contains(r)).collect();
        if left.is_empty() || start.elapsed() >= timeout {
            return left;
        }
        std::thread::sleep(Duration::from_millis(200));
    }
}

fn ask(pid: u32) -> bool {
    if cfg!(windows) {
        quiet("taskkill", &["/PID", &pid.to_string()])
    } else {
        quiet("kill", &["-TERM", &pid.to_string()])
    }
}

fn force(pid: u32) {
    if cfg!(windows) {
        quiet("taskkill", &["/F", "/PID", &pid.to_string()]);
    } else {
        quiet("kill", &["-KILL", &pid.to_string()]);
    }
}

fn quiet(program: &str, args: &[&str]) -> bool {
    let mut command = Command::new(program);
    command
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }
    command.status().is_ok_and(|s| s.success())
}

#[cfg(target_os = "linux")]
fn processes() -> Vec<(u32, String)> {
    let Ok(entries) = std::fs::read_dir("/proc") else {
        return Vec::new();
    };
    entries
        .flatten()
        .filter_map(|e| {
            let pid = e.file_name().to_str()?.parse().ok()?;
            let exe = std::fs::read_link(e.path().join("exe")).ok()?;
            Some((pid, exe.to_string_lossy().into_owned()))
        })
        .collect()
}

#[cfg(windows)]
fn processes() -> Vec<(u32, String)> {
    crate::win32::processes()
        .into_iter()
        .filter(|(_, file)| {
            PARTS
                .iter()
                .any(|(n, _)| file.eq_ignore_ascii_case(&format!("{n}.exe")))
        })
        .filter_map(|(pid, _)| Some((pid, crate::win32::image(pid)?)))
        .collect()
}

#[cfg(not(any(target_os = "linux", windows)))]
fn processes() -> Vec<(u32, String)> {
    Vec::new()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_the_launchers_parts_in_this_installs_bin_only() {
        let bin = Path::new("/home/a/SwoftyLauncher/bin");
        let part = |exe: &str| part_in(exe, bin);
        assert_eq!(
            part("/home/a/SwoftyLauncher/bin/swofty-launcher"),
            Some("swofty-launcher")
        );
        assert_eq!(part("/home/a/SwoftyLauncher/bin/swofty-ui"), Some("swofty-ui"));
        assert_eq!(part("/home/a/SwoftyLauncher/bin/swofty"), Some("swofty"));
        assert_eq!(
            part("/home/a/SwoftyLauncher/bin/swofty-launcher (deleted)"),
            Some("swofty-launcher"),
            "a launcher whose file an update replaced"
        );
        assert_eq!(part("/home/a/Other/bin/swofty-launcher"), None);
        assert_eq!(part("/home/a/SwoftyLauncher/swofty-launcher"), None);
        assert_eq!(part("/home/a/SwoftyLauncher/bin/old/swofty-launcher"), None);
        assert_eq!(part("/home/a/SwoftyLauncher/bin/swofty-launcher-old"), None);
        assert_eq!(part("/home/a/SwoftyLauncher/bin/swofty-launcher.sh"), None);
        assert_eq!(part("/home/a/SwoftyLauncher/bin/F1_25.exe"), None);
        assert_eq!(part("swofty-launcher"), None);
    }

    #[test]
    fn windows_images_match_whatever_the_case_and_slashes() {
        let bin = Path::new(r"C:\Users\A\AppData\Local\Programs\SwoftyLauncher\bin");
        let part = |exe: &str| part_in(exe, bin);
        assert_eq!(
            part(r"C:\Users\A\AppData\Local\Programs\SwoftyLauncher\bin\swofty-launcher.exe"),
            Some("swofty-launcher")
        );
        assert_eq!(
            part(r"c:\users\a\appdata\local\programs\swoftylauncher\bin\Swofty-UI.EXE"),
            Some("swofty-ui")
        );
        assert_eq!(
            part(r"\\?\C:/Users/A/AppData/Local/Programs/SwoftyLauncher/bin/swofty.exe"),
            Some("swofty")
        );
        assert_eq!(part(r"C:\Users\A\SwoftyLauncher\bin\swofty-launcher.exe"), None);
        assert_eq!(
            part(r"C:\Users\A\AppData\Local\Programs\SwoftyLauncher\bin\swofty-launcher.exe.bak"),
            None
        );
        assert_eq!(
            part(r"C:\Users\A\AppData\Local\Programs\SwoftyLauncher\bin\F1_25.exe"),
            None,
            "never the game"
        );
    }

    #[test]
    fn names_what_is_open() {
        let r = |part| Running { pid: 1, part };
        assert_eq!(
            describe(&[r("swofty-launcher")]),
            "Swofty Launcher is open from this folder and must close before the files are replaced."
        );
        assert_eq!(
            describe(&[r("swofty-ui"), r("swofty-launcher"), r("swofty-ui")]),
            "Swofty Launcher and Swofty UI are open from this folder and must close before the files are replaced."
        );
        assert_eq!(
            describe(&[r("swofty")]),
            "The swofty command line is open from this folder and must close before the files are replaced."
        );
        assert_eq!(describe(&[]), "");
    }

    #[test]
    fn names_everything_that_holds_the_data_folder() {
        let r = |part| Running { pid: 1, part };
        assert_eq!(holding(&[], false), None);
        assert_eq!(
            holding(&[], true).unwrap(),
            "Close F1 25 before moving the data folder, then select Try again."
        );
        assert_eq!(
            holding(&[r("swofty"), r("swofty-ui"), r("swofty-launcher")], true).unwrap(),
            "Close Swofty Launcher, Swofty UI, the swofty command line and F1 25 before moving the data folder, then select Try again."
        );
        assert_eq!(
            part(r"D:\Other\bin\swofty.exe").map(|p| p.0),
            Some("swofty"),
            "from any folder"
        );
    }

    #[cfg(any(target_os = "linux", windows))]
    #[test]
    fn closes_a_launcher_running_from_the_install_and_leaves_others() {
        let dir = std::env::temp_dir().join(format!("swofty-installer-running-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        let root = dir.join("SwoftyLauncher");
        let other = dir.join("Other");
        for r in [&root, &other] {
            std::fs::create_dir_all(r.join("bin")).unwrap();
        }
        let exe = std::env::current_exe().unwrap();
        let start = |r: &Path| {
            let copy = r.join("bin").join(update::exe("swofty-launcher"));
            std::fs::copy(&exe, &copy).unwrap();
            Command::new(copy)
                .args(["--ignored", "--exact", "running::tests::dummy_launcher"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .unwrap()
        };
        let mut ours = start(&root);
        let mut theirs = start(&other);
        let found = |r: &Path, pid: u32| {
            from(r)
                .iter()
                .any(|x| x.pid == pid && x.part == "swofty-launcher")
        };
        let seen = Instant::now();
        while !(found(&root, ours.id()) && found(&other, theirs.id())) {
            assert!(seen.elapsed() < Duration::from_secs(10), "{:?}", from(&root));
            std::thread::sleep(Duration::from_millis(50));
        }
        assert!(!found(&root, theirs.id()));
        close(&root).unwrap();
        ours.wait().unwrap();
        assert!(from(&root).is_empty());
        assert!(
            theirs.try_wait().unwrap().is_none(),
            "another install's launcher stays"
        );
        theirs.kill().ok();
        theirs.wait().ok();
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    #[ignore = "the dummy launcher the close test starts"]
    fn dummy_launcher() {
        std::thread::sleep(Duration::from_secs(60));
    }
}
