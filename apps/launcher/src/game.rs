use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use swofty_core::app_control::{self, Block};
use swofty_core::busy::Busy;
use swofty_core::copies::{self, Found, GameCopy};
use swofty_core::crash::{LogScan, StartFailure, Symbols, fatal_name};
use swofty_core::install::{Game, Source};
use swofty_core::launch;
use swofty_core::multiplayer::bridge::{ARMED, armed_env};
use swofty_core::process::{GameProcess, game_processes};
use swofty_core::quarantine;
use swofty_core::recovery::{self, Blocked, Notice, Scope, SessionLock};
use swofty_core::session::{LaunchMode, SessionRecord};
use swofty_core::stock;
use swofty_core::store::Store;
use swofty_core::{GAME_EXE, steam, steam_client};

const KB: &str = include_str!("../../../re/db/functions.tsv");

#[derive(Debug, Clone, PartialEq)]
pub struct Install {
    pub dir: PathBuf,
    pub build_id: Option<u64>,
    pub exe_version: Option<String>,
    pub mode: String,
    pub via_steam: bool,
    pub manual: bool,
    pub source: Source,
}

impl Install {
    pub fn version(&self) -> Option<String> {
        self.exe_version
            .clone()
            .or_else(|| self.build_id.map(|b| b.to_string()))
    }
}

/// Whose a running F1 25 is: the active copy's, another listed copy's, or
/// none the launcher can tell.
#[derive(Debug, Clone, PartialEq)]
pub enum Owner {
    Active,
    Other(Install),
    Unknown,
}

pub fn owner(g: &GameProcess, active: Option<&Install>, copies: &[Install]) -> Owner {
    if active.is_some_and(|a| g.is_in(&a.dir)) {
        return Owner::Active;
    }
    copies
        .iter()
        .find(|c| g.is_in(&c.dir))
        .cloned()
        .map_or(Owner::Unknown, Owner::Other)
}

/// Every copy found, and the one Play uses.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Copies {
    pub all: Vec<Install>,
    pub active: Option<Install>,
    pub missing: Vec<PathBuf>,
}

pub struct Probe {
    pub install: Option<Copies>,
    pub processes: Vec<GameProcess>,
    pub session: Option<SessionRecord>,
    pub requests: Option<SystemTime>,
    pub merged: bool,
    pub recovered: Option<Notice>,
    pub launch_stage: Option<String>,
    pub mods_blocked: Option<quarantine::Report>,
    pub not_stock: Option<stock::Report>,
    pub blocked: Option<Blocked>,
    pub blocked_at: Option<f64>,
    pub launch_failed: Option<launch::Failure>,
    pub overlay_fallback: Option<swofty_core::overlayfs::Fallback>,
    pub busy: Option<Busy>,
}

pub fn probe(store: &Store, rediscover: bool, seen: Option<SystemTime>) -> Probe {
    let processes = game_processes();
    let (requests, merged) = merge_requests(store, processes.is_empty(), seen);
    Probe {
        install: rediscover.then(|| discover_all(store)),
        processes,
        session: store.latest_session().map(|d| SessionRecord::read(&d)),
        requests,
        merged,
        recovered: Notice::read(&store.root),
        mods_blocked: quarantine::Report::read(&store.root),
        not_stock: stock::Report::read(&store.root),
        blocked: Blocked::read(&store.root),
        blocked_at: blocked_at(&store.root),
        launch_failed: launch::Failure::read(&store.root),
        overlay_fallback: swofty_core::overlayfs::Fallback::read(&store.root),
        busy: build_state(store),
        launch_stage: std::fs::read_to_string(store.root.join(steam_client::STAGE_FILE))
            .ok()
            .map(|s| s.trim().to_owned())
            .filter(|s| !s.is_empty()),
    }
}

/// The build a Play is running. One whose process ended is rolled back
/// here, as at launcher start.
fn build_state(store: &Store) -> Option<Busy> {
    Busy::read(&store.root)?;
    let live = Busy::live(&store.root);
    if live.is_none() {
        match recover_stale(store) {
            Ok(lines) => lines.iter().for_each(|l| eprintln!("swofty-launcher: {l}")),
            Err(e) => eprintln!("swofty-launcher: could not clean up an unfinished build: {e}"),
        }
        if let Ok(Some(_lock)) = SessionLock::try_acquire(&store.root) {
            Busy::clear(&store.root);
        }
    }
    live
}

pub fn blocked_at(data: &Path) -> Option<f64> {
    std::fs::metadata(Blocked::path(data))
        .and_then(|m| m.modified())
        .ok()?
        .duration_since(UNIX_EPOCH)
        .ok()
        .map(|d| d.as_secs_f64())
}

/// At launcher start: put back what an unfinished `swofty run` session left
/// in the game folder, unless a session is still live. A `swofty apply`
/// install stays.
pub fn recover_stale(store: &Store) -> Result<Vec<String>, String> {
    if !game_processes().is_empty() {
        return Ok(Vec::new());
    }
    let Some(install) = discover(store) else {
        return Ok(Vec::new());
    };
    let Some(lock) = SessionLock::try_acquire(&store.root).map_err(|e| e.to_string())? else {
        return Ok(Vec::new());
    };
    let g = Game::locate(&store.root, install.dir).map_err(|e| e.to_string())?;
    let r = recovery::recover(&lock, &store.root, &g, Scope::Sessions).map_err(|e| e.to_string())?;
    Ok(r.lines())
}

/// The Restore game folder button: what `swofty restore` does.
pub fn restore(store: &Store, dir: PathBuf) -> Result<String, String> {
    let g = Game::locate(&store.root, dir).map_err(|e| e.to_string())?;
    let done = recovery::restore(&store.root, &g).map_err(|e| e.to_string())?;
    if !done.left.is_empty() {
        let left: Vec<String> = done.left.iter().map(ToString::to_string).collect();
        return Err(format!("Not everything could be restored: {}", left.join("; ")));
    }
    let mut text = "The game folder matches the original game files.".to_owned();
    if !done.set_aside.is_empty() {
        text.push_str(&format!(
            " {} moved to {}.",
            match done.set_aside.len() {
                1 => "1 file was".to_owned(),
                n => format!("{n} files were"),
            },
            recovery::set_aside_dir(&store.root).display()
        ));
    }
    Ok(text)
}

fn merge_requests(store: &Store, idle: bool, seen: Option<SystemTime>) -> (Option<SystemTime>, bool) {
    let profile = store.active_profile();
    let Some(written) = std::fs::metadata(store.requests_path(&profile))
        .and_then(|m| m.modified())
        .ok()
    else {
        return (None, false);
    };
    let settled = idle && written.elapsed().is_ok_and(|age| age > Duration::from_secs(10));
    if seen == Some(written) && !settled {
        return (seen, false);
    }
    let merged = store.merge_requests(&profile, settled).ok().flatten();
    (Some(written), merged.is_some_and(|m| !m.changed.is_empty()))
}

pub fn discover(store: &Store) -> Option<Install> {
    discover_all(store).active
}

pub fn discover_all(store: &Store) -> Copies {
    let config = store.config();
    let found = copies::discover(&config);
    let active = copies::active(&found, &config).map(|c| c.dir.clone());
    let all: Vec<Install> = found.iter().map(install).collect();
    Copies {
        active: active.and_then(|dir| all.iter().find(|i| i.dir == dir).cloned()),
        missing: copies::missing(&found, &config),
        all,
    }
}

fn install(copy: &GameCopy) -> Install {
    let steam_root = copy.steam.as_ref().map(|g| g.steam_root.clone());
    let via_steam = steam_root.as_deref().is_some_and(|root| {
        steam::launch_options(root)
            .iter()
            .any(|(_, o)| steam::wrapper_installed(o))
    });
    let mode = if cfg!(windows) {
        "Windows".to_owned()
    } else {
        match steam_root.as_deref().and_then(steam::compat_tool) {
            Some(tool) => format!("Proton · {tool}"),
            None => "Proton".to_owned(),
        }
    };
    Install {
        dir: copy.dir.clone(),
        build_id: copy.build_id(),
        exe_version: copy.version(),
        mode,
        via_steam,
        manual: copy.found == Found::Added,
        source: copy.source,
    }
}

pub struct Launch {
    child: Child,
    pub since: Instant,
    pub at: f64,
    pub log: PathBuf,
    pub saw_game: bool,
    pub stage: Option<String>,
}

impl Launch {
    pub fn start(
        store: &Store,
        profile: &str,
        install: &Install,
        mode: LaunchMode,
        netplay: bool,
    ) -> Result<Self, String> {
        let cli = cli_path(store).ok_or_else(|| app_control::missing(&expected_cli(store)))?;
        let at = now_secs();
        let log = store.root.join("state/launcher-run.log");
        let out = std::fs::create_dir_all(store.root.join("state"))
            .and_then(|_| File::create(&log))
            .map_err(|e| format!("{}: {e}", log.display()))?;
        let err = out.try_clone().map_err(|e| e.to_string())?;
        std::fs::remove_file(store.root.join(steam_client::STAGE_FILE)).ok();
        let mut c = Command::new(&cli);
        c.arg("--data-dir")
            .arg(&store.root)
            .arg("--profile")
            .arg(profile)
            .arg("run")
            .arg("--apply");
        if mode.is_vr() {
            c.arg("--vr");
        }
        c.arg("--game").arg(&install.dir);
        if netplay {
            c.envs(armed_env(ARMED));
        }
        #[cfg(windows)]
        std::os::windows::process::CommandExt::creation_flags(&mut c, 0x0800_0000);
        let child = c
            .stdin(Stdio::null())
            .stdout(out)
            .stderr(err)
            .spawn()
            .map_err(|e| not_started(store, &log, &cli, &e))?;
        Ok(Self {
            child,
            since: Instant::now(),
            at,
            log,
            saw_game: false,
            stage: None,
        })
    }

    pub fn finished(&mut self) -> Option<bool> {
        self.child.try_wait().ok().flatten().map(|s| s.success())
    }

    pub fn failure(&self) -> String {
        failure_line(&std::fs::read_to_string(&self.log).unwrap_or_default())
            .unwrap_or_else(|| "the launch exited before the game started".into())
    }
}

/// A CLI that would not start: the error goes in the launch log. One Windows
/// refused is also recorded for Send Logs, and says what blocked it.
fn not_started(store: &Store, log: &Path, cli: &Path, e: &std::io::Error) -> String {
    let block = Block::of(cli, e);
    let mut lines = format!("swofty-launcher: could not start {}: {e}\n", cli.display());
    if let Some(b) = &block {
        lines.push_str(&format!("swofty-launcher: {}\n", b.headline()));
        launch::Failure::new(launch::Cause::Blocked(b.clone()))
            .write(&store.root)
            .ok();
    }
    std::fs::write(log, lines).ok();
    block.map_or_else(|| e.to_string(), |b| b.text())
}

/// The wrapper's last error, from its `swofty: ` line, or else the last line it
/// wrote. An error over several lines gives its first, which says what failed.
fn failure_line(log: &str) -> Option<String> {
    let lines: Vec<&str> = log.lines().filter(|l| !l.trim().is_empty()).collect();
    lines
        .iter()
        .rev()
        .find_map(|l| l.strip_prefix("swofty: "))
        .or_else(|| lines.last().copied())
        .map(|l| l.trim_end_matches(':').to_owned())
}

/// A failure as the play bar leads with it: a wrapper's error with its
/// innermost cause first and what the wrapper was doing after it, and any
/// other as it is.
pub fn cause_first(failure: &str) -> String {
    let Some(reason) = failure
        .strip_prefix("the wrapper exited (")
        .and_then(|rest| rest.split_once(") "))
        .and_then(|(_, rest)| rest.split_once(": "))
        .map(|(_, reason)| reason)
    else {
        return failure.to_owned();
    };
    match reason.rsplit_once(": ") {
        Some((doing, cause)) if !cause.trim().is_empty() => format!("{cause} ({doing})"),
        _ => reason.to_owned(),
    }
}

const CLI: &str = if cfg!(windows) { "swofty.exe" } else { "swofty" };

fn beside_launcher() -> Option<PathBuf> {
    std::env::current_exe()
        .ok()
        .and_then(|e| Some(e.parent()?.join(CLI)))
}

fn cli_path(store: &Store) -> Option<PathBuf> {
    let path = std::env::var_os("PATH").unwrap_or_default();
    beside_launcher()
        .into_iter()
        .chain([store.bin_dir().join(CLI)])
        .chain(std::env::split_paths(&path).map(|d| d.join(CLI)))
        .find(|p| p.is_file())
}

fn expected_cli(store: &Store) -> PathBuf {
    beside_launcher().unwrap_or_else(|| store.bin_dir().join(CLI))
}

pub fn crash_summary(session: &SessionRecord, game: Option<&Path>) -> Result<String, StartFailure> {
    let mut symbols = Symbols::parse_kb(KB);
    if let Some(dir) = game {
        symbols.load_pdata(&dir.join(GAME_EXE)).ok();
    }
    let mut scan = LogScan::default();
    if let Ok(f) = File::open(session.wine_log()) {
        for line in BufReader::new(f).split(b'\n').map_while(Result::ok) {
            scan.feed(&String::from_utf8_lossy(&line));
        }
    }
    if let Some(failure) = scan.start_failure {
        return Err(failure);
    }
    let code = session.exit_code.unwrap_or_default();
    Ok(match scan.last_fatal {
        Some((c, addr)) => format!(
            "exit code {code}, last {} at {}",
            fatal_name(c).unwrap_or("exception"),
            scan.describe(addr, &symbols)
        ),
        None => format!("exit code {code}"),
    })
}

pub fn now_secs() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

pub fn ago(secs: f64) -> String {
    let s = secs.max(0.0) as u64;
    match s {
        0..60 => "just now".into(),
        60..3600 => format!("{} min ago", s / 60),
        3600..86_400 => format!("{} h ago", s / 3600),
        _ => format!("{} d ago", s / 86_400),
    }
}

pub fn duration(secs: u64) -> String {
    match secs {
        0..60 => format!("{secs} s"),
        60..3600 => format!("{} min", secs / 60),
        _ => format!("{} h {} min", secs / 3600, secs % 3600 / 60),
    }
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use swofty_core::app_control::Block;
    use swofty_core::launch;
    use swofty_core::store::Store;

    use super::{cause_first, failure_line, not_started};

    #[test]
    fn a_cli_that_does_not_start_says_so_in_the_launch_log() {
        let root = std::env::temp_dir().join(format!("swofty-launcher-not-started-{}", std::process::id()));
        std::fs::create_dir_all(root.join("state")).unwrap();
        let store = Store::new(&root);
        let log = root.join("state/launcher-run.log");
        let cli = Path::new(r"C:\Swofty\bin\swofty.exe");
        let missing = std::io::Error::from_raw_os_error(2);
        assert_eq!(not_started(&store, &log, cli, &missing), missing.to_string());
        let written = std::fs::read_to_string(&log).unwrap();
        assert_eq!(
            written,
            format!("swofty-launcher: could not start {}: {missing}\n", cli.display())
        );
        assert_eq!(launch::Failure::read(&root), None);
        let blocked = std::io::Error::from_raw_os_error(4551);
        let text = not_started(&store, &log, cli, &blocked);
        let written = std::fs::read_to_string(&log).unwrap();
        match Block::of(cli, &blocked) {
            Some(b) => {
                assert_eq!(text, b.text());
                assert!(
                    written.ends_with(&format!("swofty-launcher: {}\n", b.headline())),
                    "{written}"
                );
                assert_eq!(
                    launch::Failure::read(&root).map(|f| f.cause),
                    Some(launch::Cause::Blocked(b))
                );
            }
            None => assert_eq!(text, blocked.to_string()),
        }
        assert!(written.contains("could not start"), "{written}");
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_failed_launch_shows_the_first_line_of_the_wrappers_error() {
        let log = "[info] installing profile default\nswofty: 2 UI edit(s) break rules the game's UI loader enforces:\n  a.erp: b.ui: page.kid: on pressed -> missing page.gone\n  a.erp: b.ui: page.kid: id\n";
        assert_eq!(
            failure_line(log).as_deref(),
            Some("2 UI edit(s) break rules the game's UI loader enforces")
        );
        assert_eq!(failure_line("[info] starting\nexit\n").as_deref(), Some("exit"));
        assert_eq!(failure_line("\n"), None);
    }

    #[test]
    fn a_wrapper_failure_leads_with_its_innermost_cause() {
        let blocked = "the download was blocked on this PC or network, typically by antivirus HTTPS or web \
                       scanning, a VPN or proxy, a network filter or a Wi-Fi login page; turning that off or trying \
                       another network fixes it";
        let log = format!(
            "[info] building profile default\n\
             [wrapper] downloading offline play 0.3.11 from https://x\n\
             swofty: the wrapper exited (exit code: 1) before the game started: downloading the offline play \
             extension: offline play 0.3.11: {blocked}\n\
             (could not download https://x/v0.3.11/f1op_hook.dll: io: received corrupt message of type \
             InvalidContentType)\n"
        );
        let failure = failure_line(&log).unwrap();
        assert!(
            failure.starts_with("the wrapper exited (exit code: 1)"),
            "{failure}"
        );
        assert_eq!(
            cause_first(&failure),
            format!("{blocked} (downloading the offline play extension: offline play 0.3.11)")
        );
        assert_eq!(
            cause_first(
                "the wrapper exited (exit status: 3) before the game started: version.dll already exists in the \
                 game folder; run swofty restore"
            ),
            "version.dll already exists in the game folder; run swofty restore"
        );
        assert_eq!(
            cause_first(
                "the wrapper exited (signal: 9 (SIGKILL)) while the game was running: installing profile default: \
                 /games/F1 25/swofty/x.erp: Permission denied (os error 13)"
            ),
            "Permission denied (os error 13) (installing profile default: /games/F1 25/swofty/x.erp)"
        );
        for kept in [
            r"the wrapper exited (exit code: 1) before the game started without output; see C:\Swofty\state\wrapper-run.log",
            "the wrapper did not start within 10 min; check the Steam launch option (swofty doctor)",
            "building profile default: reading x: missing",
            "an antivirus removed mod files, so F1 25 was not started",
        ] {
            assert_eq!(cause_first(kept), kept);
        }
    }

    #[test]
    fn an_addon_that_needs_a_newer_game_shows_the_update() {
        let outdated = "Classic Seasons needs F1 25 1.0.141.7865, the latest update. Your F1 25 is 1.0.136.6820. \
                        Update F1 25 in Steam or the EA app, then press Play again.";
        let log = format!(
            "[info] launching F1 25 directly; waiting for the wrapper, output in C:\\Swofty\\state/wrapper-run.log\n\
             [wrapper] [info] installing profile default\n\
             swofty: the wrapper exited (exit code: 1) before the game started: {outdated}\n\
             (applying swofty.classic-seasons: eaid://ui_package/databases/overrides.uidb: \
             overrides.Contents.teams.Contents.mp_team_select_logos.Contents: no entry [Name=vanamersfoort_26])\n"
        );
        let failure = failure_line(&log).unwrap();
        assert_eq!(
            failure,
            format!("the wrapper exited (exit code: 1) before the game started: {outdated}")
        );
        assert_eq!(cause_first(&failure), outdated);
    }
}
