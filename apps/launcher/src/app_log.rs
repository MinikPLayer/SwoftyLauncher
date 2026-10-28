//! The launcher's own log, `state/launcher-app.log` in the data folder: a
//! line when it starts, what Restart as administrator did, every action in
//! the window that failed, and a panic that closed it. Send Logs collects it
//! with the other state logs.

use std::io::Write;
use std::path::{Path, PathBuf};

pub(crate) const FILE: &str = "state/launcher-app.log";
/// A log past this size starts over.
const CAP: u64 = 256 << 10;

fn path() -> PathBuf {
    swofty_core::paths::data_dir().join(FILE)
}

fn line(text: &str) -> String {
    let secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs() as i64);
    format!(
        "{} [{}] {text}\n",
        swofty_core::send_logs::utc_stamp(secs),
        std::process::id()
    )
}

fn append(path: &Path, text: &str) {
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).ok();
    }
    let full = std::fs::metadata(path).is_ok_and(|m| m.len() > CAP);
    std::fs::OpenOptions::new()
        .create(true)
        .append(!full)
        .write(true)
        .truncate(full)
        .open(path)
        .and_then(|mut f| f.write_all(line(text).as_bytes()))
        .ok();
}

pub(crate) fn note(text: &str) {
    append(&path(), text);
}

static GAME: std::sync::Mutex<Option<PathBuf>> = std::sync::Mutex::new(None);

/// The game folder failed actions name their files against.
pub(crate) fn set_game(dir: Option<&Path>) {
    if let Ok(mut game) = GAME.lock() {
        *game = dir.map(Path::to_path_buf);
    }
}

/// An action in the launcher window, logged when it fails with the addon it
/// was for and the files it used. Files show relative to the data or game
/// folder, else by name, and the error never carries a full user path.
#[derive(Default)]
pub(crate) struct Action<'a> {
    name: &'a str,
    addon: Option<&'a str>,
    files: Vec<&'a Path>,
}

pub(crate) fn action(name: &str) -> Action<'_> {
    Action {
        name,
        ..Action::default()
    }
}

impl<'a> Action<'a> {
    pub(crate) fn addon(mut self, id: &'a str) -> Self {
        self.addon = Some(id);
        self
    }

    pub(crate) fn file(mut self, path: &'a Path) -> Self {
        self.files.push(path);
        self
    }

    pub(crate) fn files(mut self, paths: &'a [PathBuf]) -> Self {
        self.files.extend(paths.iter().map(PathBuf::as_path));
        self
    }

    pub(crate) fn failed(&self, error: impl std::fmt::Display) {
        note(&self.line(&Roots::now(), &error.to_string()));
    }

    fn line(&self, roots: &Roots, error: &str) -> String {
        let mut line = format!("failed: {}", self.name);
        if let Some(id) = self.addon {
            line.push_str(&format!("; addon {id}"));
        }
        if !self.files.is_empty() {
            let files: Vec<String> = self.files.iter().map(|f| roots.file(f)).collect();
            line.push_str(&format!("; file {}", files.join(", ")));
        }
        format!("{line}: {}", roots.scrub(error))
    }
}

struct Roots {
    data: PathBuf,
    game: Option<PathBuf>,
    home: Option<PathBuf>,
}

impl Roots {
    fn now() -> Self {
        let home = if cfg!(windows) { "USERPROFILE" } else { "HOME" };
        Self {
            data: swofty_core::paths::data_dir(),
            game: GAME.lock().ok().and_then(|g| g.clone()),
            home: std::env::var_os(home).map(PathBuf::from),
        }
    }

    fn named(&self) -> [(Option<&Path>, &'static str); 3] {
        [
            (Some(self.data.as_path()), "<data>"),
            (self.game.as_deref(), "<game>"),
            (self.home.as_deref(), "~"),
        ]
    }

    fn file(&self, path: &Path) -> String {
        for (root, tag) in self.named().into_iter().take(2) {
            if let Some(rel) = root.and_then(|r| path.strip_prefix(r).ok()) {
                let parts: Vec<String> = rel
                    .components()
                    .map(|c| c.as_os_str().to_string_lossy().into_owned())
                    .collect();
                return format!("{tag}/{}", parts.join("/"));
            }
        }
        path.file_name()
            .map_or_else(String::new, |n| n.to_string_lossy().into_owned())
    }

    fn scrub(&self, text: &str) -> String {
        let mut text = text.to_owned();
        for (root, tag) in self.named() {
            let root = root.map(|r| r.display().to_string()).unwrap_or_default();
            let root = root.trim_end_matches(['/', '\\']);
            if root.len() > 3 {
                text = text.replace(root, tag);
            }
        }
        text
    }
}

static STEP: std::sync::Mutex<(&'static str, Option<std::time::Instant>)> =
    std::sync::Mutex::new(("start", None));
static SHOWN: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// One startup step reached, with the time since the process began.
pub(crate) fn step(name: &'static str) {
    let since = {
        let mut last = STEP.lock().unwrap_or_else(|e| e.into_inner());
        let began = *last.1.get_or_insert_with(std::time::Instant::now);
        *last = (name, Some(began));
        began.elapsed().as_millis()
    };
    note(&format!("startup: {name} (+{since} ms)"));
}

fn last_step() -> &'static str {
    STEP.lock().unwrap_or_else(|e| e.into_inner()).0
}

/// The window drew its first frame: the startup watchdog stops.
pub(crate) fn window_shown() {
    if !SHOWN.swap(true, std::sync::atomic::Ordering::Relaxed) {
        step("window drawn");
    }
}

fn stalled_line(last: &str, secs: u64) -> String {
    format!("startup: no window after {secs} s, last step was \"{last}\"")
}

fn watch_window() {
    std::thread::Builder::new()
        .name("startup-watch".into())
        .spawn(|| {
            for n in 1..=6u64 {
                std::thread::sleep(std::time::Duration::from_secs(10));
                if SHOWN.load(std::sync::atomic::Ordering::Relaxed) {
                    return;
                }
                note(&stalled_line(last_step(), n * 10));
            }
        })
        .ok();
}

/// The start line, then every panic, before the default hook prints it.
pub(crate) fn start() {
    let path = path();
    let args: Vec<String> = std::env::args().skip(1).collect();
    let started = format!(
        "started {}{}",
        env!("CARGO_PKG_VERSION"),
        if args.is_empty() {
            String::new()
        } else {
            format!(" with {}", args.join(" "))
        }
    );
    append(&path, &started);
    step("process start");
    watch_window();
    let at = path.clone();
    std::thread::spawn(move || {
        let elevated = if swofty_core::compat::elevated() {
            "yes"
        } else {
            "no"
        };
        append(&at, &format!("administrator: {elevated}"));
    });
    let previous = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        let thread = std::thread::current();
        let backtrace = std::backtrace::Backtrace::force_capture();
        append(
            &path,
            &format!(
                "panic on thread {}: {info}\n{backtrace}",
                thread.name().unwrap_or("unnamed")
            ),
        );
        previous(info);
    }));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stalled_line_names_the_last_step() {
        assert_eq!(
            stalled_line("opening the window", 20),
            "startup: no window after 20 s, last step was \"opening the window\""
        );
    }

    #[test]
    fn appends_lines_and_starts_over_when_full() {
        let dir = std::env::temp_dir().join(format!("swofty-app-log-{}", std::process::id()));
        let log = dir.join(FILE);
        append(&log, "started 1.0.0");
        append(&log, "administrator: no");
        let text = std::fs::read_to_string(&log).unwrap();
        let lines: Vec<&str> = text.lines().collect();
        assert_eq!(lines.len(), 2, "{text}");
        assert!(lines[0].ends_with(&format!("[{}] started 1.0.0", std::process::id())));
        assert!(lines[1].ends_with("administrator: no"));
        std::fs::write(&log, vec![b'x'; CAP as usize + 1]).unwrap();
        append(&log, "started 1.0.1");
        let text = std::fs::read_to_string(&log).unwrap();
        assert_eq!(text.lines().count(), 1, "{text}");
        assert!(text.ends_with("started 1.0.1\n"));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn a_failed_action_names_its_files_without_a_user_path() {
        let home = PathBuf::from("/home/someone");
        let roots = Roots {
            data: home.join(".local/share/swofty"),
            game: Some(PathBuf::from("/games/F1 25")),
            home: Some(home.clone()),
        };
        let dropped = home.join("Downloads/Sepang Simplified.zip");
        let line = action("import")
            .addon("swofty.simplified-importer")
            .file(&dropped)
            .file(&roots.data.join("addons/swofty.sepang/addon.json"))
            .line(
                &roots,
                &format!(
                    "could not read {}: The system cannot find the path specified. (os error 3)",
                    home.join(".local/share/swofty/imports/sepang/track.erp")
                        .display()
                ),
            );
        assert_eq!(
            line,
            "failed: import; addon swofty.simplified-importer; file Sepang Simplified.zip, \
             <data>/addons/swofty.sepang/addon.json: could not read <data>/imports/sepang/track.erp: \
             The system cannot find the path specified. (os error 3)"
        );
        let line = action("restore the game folder")
            .file(Path::new("/games/F1 25/2025_asset_groups/x.erp"))
            .line(&roots, &format!("{}/Desktop/save.bin is in use", home.display()));
        assert_eq!(
            line,
            "failed: restore the game folder; file <game>/2025_asset_groups/x.erp: ~/Desktop/save.bin is in use"
        );
        assert!(!line.contains("someone"));
    }

    #[test]
    fn a_failed_action_is_written_to_the_log() {
        let dir = std::env::temp_dir().join(format!("swofty-app-log-failed-{}", std::process::id()));
        let log = dir.join(FILE);
        let roots = Roots {
            data: dir.clone(),
            game: None,
            home: None,
        };
        append(
            &log,
            &action("install addon")
                .addon("swofty.classic-seasons")
                .line(&roots, "no space left"),
        );
        let text = std::fs::read_to_string(&log).unwrap();
        assert!(
            text.trim_end()
                .ends_with("failed: install addon; addon swofty.classic-seasons: no space left"),
            "{text}"
        );
        std::fs::remove_dir_all(&dir).ok();
    }
}
