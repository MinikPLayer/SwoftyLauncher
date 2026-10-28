use std::path::Path;
use std::process::{Child, Stdio};
use std::time::{Duration, Instant};

use anyhow::{Result, anyhow, bail};
use swofty_core::ea_app::{self, Handoff};
use swofty_core::install::{Game, Source};
use swofty_core::launch;
use swofty_core::offline::{self, Slot};
use swofty_core::process::{GameProcess, game_processes, stop_process};
use swofty_core::recovery::Scope;
use swofty_core::steam_client;
use swofty_core::store::Store;

use crate::run::{StageFile, ending, say};

const APP_START_TIMEOUT: Duration = Duration::from_secs(90);
// A link handed over while the EA app is still starting can be dropped:
// under Proton one sent a second after EADesktop.exe appeared never arrived.
const APP_SETTLE: Duration = Duration::from_secs(10);
const HANDOFF_TIMEOUT: Duration = Duration::from_secs(180);
#[cfg(not(windows))]
const SERVER_STOP_TIMEOUT: Duration = Duration::from_secs(30);
const PRESS_PLAY_TIMEOUT: Duration = Duration::from_secs(600);
const HANDOFF_LOG: &str = "ea_handoff.log";

/// Which store Play brings up first for a copy of the game.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StoreStart {
    Steam,
    EaApp,
}

pub fn store_start(source: Source, direct: bool) -> StoreStart {
    match source {
        Source::EaApp if !direct => StoreStart::EaApp,
        _ => StoreStart::Steam,
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Policy {
    NoHandoff,
    Running,
    Start,
    Off,
}

pub fn policy(handoff: bool, on: bool, running: bool) -> Policy {
    match (handoff, running, on) {
        (false, _, _) => Policy::NoHandoff,
        (true, true, _) => Policy::Running,
        (true, false, true) => Policy::Start,
        (true, false, false) => Policy::Off,
    }
}

fn handoff(store: &Store) -> Option<Handoff> {
    ea_app::handoff(store.config().launch.ea_launch_command.as_deref())
}

/// An EA app copy needs the EA app: start it when it is closed and wait for
/// it, as Steam is for a Steam copy. On Linux the launch.ea_launch_command
/// setting, run without a link, starts it; a command that needs the link
/// leaves the start to the link.
pub fn ensure(store: &Store, opted_out: bool) -> Result<()> {
    let h = handoff(store);
    let on = store.config().launch.start_store && !opted_out;
    match policy(h.is_some(), on, ea_app::running()) {
        Policy::NoHandoff => bail!("{}", ea_app::no_handoff()),
        Policy::Running => say("ea app", "detected: running"),
        Policy::Off => bail!(
            "the EA app is closed and starting it is turned off (launch.start_store or --no-start-store); \
             open the EA app and press Play again"
        ),
        Policy::Start => match h.as_ref().and_then(ea_app::opener) {
            Some(mut open) => {
                say("ea app", "detected: closed; starting the EA app");
                let stage = StageFile(store.root.join(steam_client::STAGE_FILE));
                stage.set("Starting the EA app…");
                let mut child = detach(&mut open).map_err(|e| {
                    anyhow!("could not start the EA app ({e}); open the EA app and press Play again")
                })?;
                let since = Instant::now();
                match wait_for_app(&mut child, || since.elapsed() >= APP_START_TIMEOUT) {
                    AppStart::Running => {
                        say(
                            "ea app",
                            &format!("running after {:.1} s", since.elapsed().as_secs_f64()),
                        );
                        std::thread::sleep(APP_SETTLE);
                    }
                    AppStart::CommandFailed(exit) => say(
                        "ea app",
                        &format!("the command did not open the EA app ({exit}); the launch link starts it"),
                    ),
                    AppStart::TimedOut => bail!(
                        "the EA app didn't start within {} s; open the EA app and press Play again",
                        APP_START_TIMEOUT.as_secs()
                    ),
                }
            }
            None => say("ea app", "detected: closed; the launch link starts it"),
        },
    }
    Ok(())
}

/// Wine reads a prefix's user.reg only when its wineserver starts, and the
/// EA app's wineserver also runs F1 25. So the version.dll override goes into
/// the EA app's prefix while the EA app is closed, and stays there: without
/// Swofty's version.dll in the game folder Wine loads its own.
#[cfg(not(windows))]
pub fn dll_setting(store: &Store, g: &Game, opted_out: bool) -> Result<()> {
    use swofty_core::install::{add_override, has_override};
    let Some((reg, pfx)) = g.user_reg.as_deref().and_then(|r| Some((r, r.parent()?))) else {
        return Ok(());
    };
    if has_override(&std::fs::read_to_string(reg)?) {
        return Ok(());
    }
    if let Some(server) = ea_app::prefix_server(pfx) {
        if !store.config().launch.start_store || opted_out {
            bail!(
                "close the EA app and press Play again: Wine loads Swofty's version.dll in F1 25 only when the EA \
                 app starts after Swofty sets it up in the EA app's prefix"
            );
        }
        say(
            "ea app",
            "closing the EA app so Wine loads Swofty's version.dll in F1 25; it starts again with the setting",
        );
        stop_process(server)?;
        let since = Instant::now();
        while ea_app::prefix_server(pfx).is_some() || ea_app::running() {
            if since.elapsed() >= SERVER_STOP_TIMEOUT {
                bail!(
                    "the EA app didn't close within {} s; close it and press Play again",
                    SERVER_STOP_TIMEOUT.as_secs()
                );
            }
            std::thread::sleep(steam_client::POLL_INTERVAL);
        }
    }
    if let Some(out) = add_override(&std::fs::read_to_string(reg)?) {
        std::fs::write(reg, out)?;
    }
    say(
        "ea app",
        &format!("{} has Wine load Swofty's version.dll in F1 25", reg.display()),
    );
    Ok(())
}

#[cfg(windows)]
pub fn dll_setting(_: &Store, _: &Game, _: bool) -> Result<()> {
    Ok(())
}

#[derive(Debug, PartialEq)]
enum AppStart {
    Running,
    CommandFailed(std::process::ExitStatus),
    TimedOut,
}

/// Wait for EADesktop.exe after `child` was asked to open the EA app. A
/// command that exits with an error opened nothing, so waiting is pointless.
fn wait_for_app(child: &mut Child, timed_out: impl Fn() -> bool) -> AppStart {
    loop {
        if ea_app::running() {
            return AppStart::Running;
        }
        if let Ok(Some(exit)) = child.try_wait()
            && !exit.success()
        {
            return AppStart::CommandFailed(exit);
        }
        if timed_out() {
            return AppStart::TimedOut;
        }
        std::thread::sleep(steam_client::POLL_INTERVAL);
    }
}

fn detach(c: &mut std::process::Command) -> std::io::Result<Child> {
    c.stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null());
    #[cfg(unix)]
    std::os::unix::process::CommandExt::process_group(c, 0);
    #[cfg(windows)]
    std::os::windows::process::CommandExt::creation_flags(c, 0x0000_0008 | 0x0000_0200);
    c.spawn()
}

/// A game started since `before` that is not another copy's than `game`'s.
fn new_copy(game: &Path, before: &[u32]) -> Option<u32> {
    game_processes()
        .into_iter()
        .find(|g| !before.contains(&g.pid) && !g.elsewhere(game))
        .map(|g| g.pid)
}

fn wait_for_game(
    game: &Path,
    before: &[u32],
    timeout: Duration,
    handoff: &mut Option<Child>,
) -> Result<Option<u32>> {
    let since = Instant::now();
    loop {
        if let Some(pid) = new_copy(game, before) {
            return Ok(Some(pid));
        }
        if ending() {
            bail!("asked to stop before F1 25 started");
        }
        if let Some(c) = handoff.as_mut()
            && let Ok(Some(exit)) = c.try_wait()
        {
            *handoff = None;
            if !exit.success() {
                say("ea app", &format!("the hand-off command exited with {exit}"));
                return Ok(None);
            }
        }
        if since.elapsed() >= timeout {
            return Ok(None);
        }
        std::thread::sleep(Duration::from_millis(500));
    }
}

/// The session's game, launched by the EA app: hand it the launch link and
/// wait for F1 25 to start. The game files are staged before this.
pub fn start(store: &Store, game: &Path, args: &[String], session: &Path, before: &[u32]) -> Result<Started> {
    let url = ea_app::install_url(game, args).map_err(|e| anyhow!(e))?;
    let h = handoff(store).ok_or_else(|| anyhow!("{}", ea_app::no_handoff()))?;
    let stage = StageFile(store.root.join(steam_client::STAGE_FILE));
    stage.set("Starting F1 25 through the EA app…");
    say("ea app", &format!("asking the EA app to launch F1 25: {url}"));
    let log = std::fs::File::create(session.join(HANDOFF_LOG))?;
    let mut c = h.command(&url);
    c.stdin(Stdio::null()).stdout(log.try_clone()?).stderr(log);
    #[cfg(unix)]
    std::os::unix::process::CommandExt::process_group(&mut c, 0);
    let mut handoff = match c.spawn() {
        Ok(child) => Some(child),
        Err(e) => {
            say("ea app", &format!("could not run {h}: {e}"));
            None
        }
    };
    let pid = match wait_for_game(game, before, HANDOFF_TIMEOUT, &mut handoff)? {
        Some(pid) => pid,
        None => {
            if let Some(mut open) = ea_app::opener(&h) {
                detach(&mut open).ok();
            }
            match offline::slot(&offline::journal_path(&store.root)) {
                Ok(Some(Slot::Staged)) => say("ea app", "offline play's file is in place"),
                Ok(Some(Slot::Replaced)) => say("ea app", "the EA app put its own file over offline play's"),
                Ok(Some(Slot::Missing)) => say("ea app", "offline play's file is gone from the game folder"),
                Ok(Some(Slot::Unknown)) => {
                    say("ea app", "offline play's file was changed by another program")
                }
                Ok(None) => {}
                Err(e) => say("ea app", &format!("could not check offline play's file: {e}")),
            }
            say(
                "ea app",
                "F1 25 did not start from the launch link; press Play on F1 25 in the EA app",
            );
            stage.set("Press Play on F1 25 in the EA app");
            wait_for_game(game, before, PRESS_PLAY_TIMEOUT, &mut None)?.ok_or_else(|| {
                anyhow!(
                    "F1 25 was not started from the EA app within {} min; the game files are rolled back",
                    PRESS_PLAY_TIMEOUT.as_secs() / 60
                )
            })?
        }
    };
    say("ea app", &format!("F1 25 started (pid {pid})"));
    restage(store);
    Ok(Started { pid, handoff })
}

/// An EA app repair puts EA's anti-cheat launcher over offline play's file,
/// and the game it then starts has no mods. Put the file back for the next
/// Play and tell the player.
fn restage(store: &Store) {
    let journal = offline::journal_path(&store.root);
    let done = offline::artifacts(&store.root).and_then(|a| offline::restage(&a, &journal));
    match done {
        Ok(Some(Slot::Staged) | None) => {}
        Ok(Some(Slot::Replaced | Slot::Missing)) => {
            let failure = launch::Failure::new(launch::Cause::EaRepaired);
            say("warning", &format!("{}: {}", failure.title(), failure.text()));
            if let Err(e) = failure.write(&store.root) {
                say("info", &format!("could not record it for the launcher: {e}"));
            }
        }
        Ok(Some(Slot::Unknown)) => say(
            "ea app",
            "EAAntiCheat.GameServiceLauncher.exe changed after staging and is neither offline play's nor EA's; \
             left as is",
        ),
        Err(e) => say("ea app", &format!("could not put offline play's file back: {e}")),
    }
}

/// An EA app copy started by the EA app as it is installed, with nothing
/// staged: the profile and offline play stay out, so EA AntiCheat runs as
/// it does from the EA app's own Play button.
pub fn launch_installed(store: &Store, g: &Game, vr: bool, opted_out: bool) -> Result<()> {
    if Source::detect(&g.dir) != Source::EaApp {
        bail!(
            "{} is not an EA app copy; start a Steam copy from Steam, or pick the EA app copy with swofty game use ea",
            g.dir.display()
        );
    }
    if let Some(running) = game_processes().first() {
        bail!("F1 25 is already running (pid {}); close it first", running.pid);
    }
    {
        let out = |l: &str| say("info", l);
        let lock = crate::recover::lock(store, &out)?;
        crate::recover::stale(store, &lock, g, Scope::All, &out)?;
    }
    ensure(store, opted_out)?;
    let args: Vec<String> = if vr { vec!["-vr".into()] } else { Vec::new() };
    let url = ea_app::install_url(&g.dir, &args).map_err(|e| anyhow!(e))?;
    let h = handoff(store).ok_or_else(|| anyhow!("{}", ea_app::no_handoff()))?;
    say("ea app", &format!("asking the EA app to launch F1 25: {url}"));
    let state = store.root.join("state");
    std::fs::create_dir_all(&state)?;
    let log = std::fs::File::create(state.join(HANDOFF_LOG))?;
    let mut c = h.command(&url);
    c.stdin(Stdio::null()).stdout(log.try_clone()?).stderr(log);
    #[cfg(unix)]
    std::os::unix::process::CommandExt::process_group(&mut c, 0);
    let mut child = Some(c.spawn().map_err(|e| anyhow!("could not run {h}: {e}"))?);
    let pid = wait_for_game(&g.dir, &[], HANDOFF_TIMEOUT, &mut child)?.ok_or_else(|| {
        anyhow!(
            "F1 25 did not start within {} min of the launch link; press Play on F1 25 in the EA app",
            HANDOFF_TIMEOUT.as_secs() / 60
        )
    })?;
    say("ea app", &format!("F1 25 started (pid {pid})"));
    if let Some(line) = command_line(pid) {
        say("ea app", &format!("command line: {line}"));
    }
    Ok(())
}

#[cfg(target_os = "linux")]
fn command_line(pid: u32) -> Option<String> {
    let raw = std::fs::read(format!("/proc/{pid}/cmdline")).ok()?;
    let words: Vec<String> = raw
        .split(|b| *b == 0)
        .filter(|w| !w.is_empty())
        .map(|w| String::from_utf8_lossy(w).into_owned())
        .collect();
    (!words.is_empty()).then(|| words.join(" "))
}

#[cfg(not(target_os = "linux"))]
fn command_line(_: u32) -> Option<String> {
    None
}

pub struct Started {
    pid: u32,
    handoff: Option<Child>,
}

/// Until every game of this copy started since the launch has closed. Only
/// the game the EA app started for the session, and games that are this
/// copy's, are closed on a stop. The EA app owns the game's process, so its
/// exit code is not the session's to read.
pub fn wait(game: &Path, before: &[u32], started: Started, mut tick: impl FnMut()) {
    let mut asked = false;
    loop {
        let running: Vec<GameProcess> = game_processes()
            .into_iter()
            .filter(|g| !before.contains(&g.pid) && !g.elsewhere(game))
            .collect();
        if running.is_empty() {
            break;
        }
        tick();
        if !asked && ending() {
            asked = true;
            say(
                "info",
                "asked to stop: closing F1 25, then rolling its game files back",
            );
            for g in running.iter().filter(|g| g.pid == started.pid || g.is_in(game)) {
                stop_process(g.pid).ok();
            }
        }
        std::thread::sleep(Duration::from_millis(200));
    }
    if let Some(mut c) = started.handoff {
        std::thread::spawn(move || c.wait().ok());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_active_copy_picks_the_store_to_start() {
        assert_eq!(store_start(Source::Steam, false), StoreStart::Steam);
        assert_eq!(store_start(Source::EaApp, false), StoreStart::EaApp);
        assert_eq!(store_start(Source::EaApp, true), StoreStart::Steam);
        assert_eq!(store_start(Source::Unknown, false), StoreStart::Steam);
    }

    #[test]
    fn starts_the_ea_app_only_when_it_can_and_may() {
        assert_eq!(policy(false, true, true), Policy::NoHandoff);
        assert_eq!(policy(true, false, true), Policy::Running);
        assert_eq!(policy(true, true, false), Policy::Start);
        assert_eq!(policy(true, false, false), Policy::Off);
    }

    #[test]
    fn stops_waiting_when_the_open_command_fails() {
        if ea_app::running() {
            return;
        }
        let mut fail = if cfg!(windows) {
            let mut c = std::process::Command::new("cmd");
            c.args(["/c", "exit 3"]);
            c
        } else {
            std::process::Command::new("false")
        };
        let mut child = fail.spawn().unwrap();
        assert!(matches!(
            wait_for_app(&mut child, || false),
            AppStart::CommandFailed(_)
        ));
        let mut ok = if cfg!(windows) {
            let mut c = std::process::Command::new("cmd");
            c.args(["/c", "exit 0"]);
            c
        } else {
            std::process::Command::new("true")
        };
        let mut child = ok.spawn().unwrap();
        let polls = std::cell::Cell::new(0);
        let timed_out = || {
            polls.set(polls.get() + 1);
            polls.get() > 3
        };
        assert_eq!(wait_for_app(&mut child, timed_out), AppStart::TimedOut);
    }

    #[cfg(unix)]
    #[test]
    fn hands_the_link_to_the_configured_command() {
        let root = std::env::temp_dir().join(format!("swofty-ea-handoff-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let game = root.join("F1 25");
        std::fs::create_dir_all(game.join("__Installer")).unwrap();
        std::fs::write(
            game.join(ea_app::MARKER),
            "<contentIDs><contentID>16425845</contentID><contentID>16425845_oa</contentID></contentIDs>",
        )
        .unwrap();
        let store = Store::new(root.join("data"));
        std::fs::create_dir_all(&store.root).unwrap();
        let got = root.join("url.txt");
        let mut config = store.config();
        config.launch.ea_launch_command = Some(format!("printf %s {{url}} > '{}'", got.display()));
        store.save_config(&config).unwrap();
        let url = ea_app::install_url(&game, &["-vr".into()]).unwrap();
        let mut c = handoff(&store).unwrap().command(&url);
        assert!(c.status().unwrap().success());
        assert_eq!(
            std::fs::read_to_string(&got).unwrap(),
            "origin2://game/launch?offerIds=16425845,16425845_oa&autoDownload=1&cmdParams=-vr"
        );
        std::fs::remove_dir_all(&root).ok();
    }
}
