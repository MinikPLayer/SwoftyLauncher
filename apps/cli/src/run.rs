use std::ffi::OsString;
use std::io::{IsTerminal, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitCode, ExitStatus, Stdio};
use std::time::{Duration, Instant, SystemTime};

use anyhow::{Context, Result, bail};
use clap::Args;
use swofty_core::app_control::Block;
use swofty_core::crash::{Event, LogScan, Symbols, fatal_name};
use swofty_core::install::{Game, Installer, PROXY, Source};
use swofty_core::launch;
use swofty_core::loader_check;
use swofty_core::offline::Artifacts;
use swofty_core::process::{
    descends_from, game_processes, game_running_in, games_in, steamvr_running, stop_game_in, stop_process,
};
use swofty_core::quarantine::{self, Attach, AttachWatch, Stage};
use swofty_core::recovery::{Scope, SessionLock};
use swofty_core::session::{
    Config, DEFAULT_WINEDEBUG, LOADER_FALLBACK, LOADER_LOG, LOADER_MARK, LaunchMode, LaunchRequest, LogMark,
    PERF_LOG, RUNTIME_LIBS_DIR, STOP_FILE, SessionEnd, SessionRecord, game_args, is_compat_layer,
    parse_cpu_list, parse_steam_command, request_stop, written_since,
};
use swofty_core::steam_client::{self, Ready, Report, State, Wait};
use swofty_core::store::Store;
use swofty_core::{GAME_EXE, STEAM_APP_ID, steam};

use crate::overlay::{self, Choice, Overlay};
use crate::time::{now_secs, stamp};

const KB: &str = include_str!("../../../re/db/functions.tsv");

#[derive(Args)]
pub struct RunArgs {
    #[arg(long, default_value = DEFAULT_WINEDEBUG, help = "WINEDEBUG channels for the game process")]
    winedebug: String,
    #[arg(long, help = "Close this copy's running F1 25 first")]
    restart: bool,
    #[arg(long, help = "Launch through Proton directly instead of asking Steam")]
    direct: bool,
    #[arg(
        long,
        alias = "no-start-steam",
        help = "Don't start Steam or the EA app when it's closed (the launch.start_store setting does the same)"
    )]
    no_start_store: bool,
    #[arg(long, hide = true)]
    via_ea: bool,
    #[arg(
        long,
        help = "Start F1 25 in VR, like Steam's VR launch option (passes -vr; on Linux, starts SteamVR first)"
    )]
    vr: bool,
    #[arg(
        long,
        help = "Build and install the profile (database, tracks) before the game starts, and roll it back when it exits"
    )]
    apply: bool,
    #[arg(
        long,
        help = "Linux: run the modded game from an OverlayFS mount instead of copying files into the game folder"
    )]
    use_overlayfs: bool,
    #[arg(long, help = crate::GAME_HELP)]
    game: Option<PathBuf>,
    #[arg(
        long,
        env = "SWOFTY_EXE_COPY",
        help = "A copy of F1_25.exe whose .pdata gives function bounds"
    )]
    exe: Option<PathBuf>,
    #[arg(
        last = true,
        help = "The Steam command (%command%); present when Steam runs the wrapper"
    )]
    steam_command: Vec<String>,
}

pub fn run(store: &Store, profile: &str, args: RunArgs) -> Result<ExitCode> {
    let wrapped = !args.steam_command.is_empty();
    let explicit = args
        .game
        .clone()
        .or_else(|| wrapped.then(|| std::env::current_dir().ok()).flatten());
    let g = crate::build::game(store, explicit)?;
    if wrapped {
        return wrapper(store, profile, &args, &g).inspect_err(|e| note_blocked(store, e));
    }
    crate::build::supported(&g)?;
    let game = g.dir.clone();
    let running = game_processes();
    if let Some(first) = running.first() {
        if !args.restart {
            return Err(already_running(
                store,
                first.pid,
                "close it first, or pass --restart to close it",
            ));
        }
        if let Some(other) = running.iter().find(|g| !g.is_in(&game)) {
            return Err(already_running(
                store,
                other.pid,
                &format!(
                    "it isn't the copy in {}, so --restart leaves it running; close it first",
                    game.display()
                ),
            ));
        }
        request_stop(&store.root);
        stop_game_in(&game).ok();
        while game_running_in(&game) {
            std::thread::sleep(Duration::from_millis(500));
        }
    }
    {
        let lock = crate::recover::lock(store, &info)?;
        crate::recover::stale(store, &lock, &g, Scope::All, &info)?;
    }
    // Modded sessions run without EA AntiCheat: the terms are accepted once,
    // and declining means the game is not launched.
    crate::offline::gate(store, std::io::stdin().is_terminal())?;
    if args.apply {
        crate::setup::gate(store)?;
    }
    if let Some(state) = swofty_core::steam::find_game()
        .filter(|i| swofty_core::copies::same_dir(&i.dir, &game))
        .and_then(|i| swofty_core::steam::busy(&i))
    {
        bail!("Steam is {state} F1 25; wait for it to finish, then press Play again");
    }
    match crate::ea::store_start(Source::detect(&game), args.direct) {
        crate::ea::StoreStart::EaApp => {
            if args.apply {
                crate::ea::dll_setting(store, &g, args.no_start_store)?;
            }
            crate::ea::ensure(store, args.no_start_store)?
        }
        crate::ea::StoreStart::Steam => ensure_steam(store, &game, args.no_start_store)?,
    }
    let id = format!("{:x}{:x}", std::process::id(), (now_secs() * 1e6) as u64);
    let request = LaunchRequest {
        id: id.clone(),
        winedebug: Some(args.winedebug.clone()),
        profile: Some(profile.into()),
        apply: args.apply,
    };
    write_json(&store.root.join("state/launch.json"), &request)?;
    let mode = if args.vr {
        LaunchMode::Vr
    } else {
        LaunchMode::Normal
    };
    if mode.is_vr() && !cfg!(windows) {
        ensure_steamvr();
    }
    let loader = overlay::log_root(store, &game, profile).join(LOADER_LOG);
    let before = LogMark::read(&loader);
    let asked = now_secs();
    let mut wrapper = launch(store, &game, args.direct, args.use_overlayfs, mode)
        .inspect_err(|e| note_blocked(store, e))?;
    let mut silence = Silence::new();
    let mut waiting_on_dialog = false;
    let dir = loop {
        if let Some(d) = store
            .latest_session()
            .filter(|d| std::fs::read_to_string(d.join("request_id")).is_ok_and(|s| s.trim() == id))
        {
            break d;
        }
        if let Some(report) = refused(store, asked) {
            settle(store, &g, &mut wrapper, None, false);
            eprintln!("swofty: {}", report.text());
            return Ok(ExitCode::from(quarantine::REFUSED_EXIT));
        }
        if let Some(status) = wrapper.exited() {
            let err = wrapper.failure(status, "before the game started");
            settle(store, &g, &mut wrapper, None, false);
            return Err(err);
        }
        if swofty_core::overlayfs::dialog_pending(&store.root) {
            if !waiting_on_dialog {
                waiting_on_dialog = true;
                say("info", "waiting for your answer in the OverlayFS dialog");
            }
            silence = Silence::new();
        } else {
            waiting_on_dialog = false;
        }
        if silence.of(&wrapper.log) > wrapper.timeout {
            match &wrapper.child {
                Some(_) => bail!(
                    "the wrapper has not started the game and has written nothing for {} min; it is still running, \
                     and its output is in {}",
                    wrapper.timeout.as_secs() / 60,
                    wrapper.log.display()
                ),
                None => bail!(
                    "the wrapper did not start within {} min; check the Steam launch option (swofty doctor)",
                    wrapper.timeout.as_secs() / 60
                ),
            }
        }
        std::thread::sleep(Duration::from_millis(500));
    };
    say("info", &format!("wrapper started, logging to {}", dir.display()));
    let mut relay = wrapper
        .child
        .as_ref()
        .map(|_| Follower::from_end(wrapper.log.clone()));
    let mut follow = Follow::new(&dir, Some((loader, before)), symbols(&game, args.exe.as_deref()));
    install_ctrl_c();
    let mut quiet = false;
    while !dir.join("exit_code").exists() {
        if stop_requested() {
            if wrapper.child.is_none() {
                say("info", "stopped following (the game keeps running)");
                return Ok(ExitCode::SUCCESS);
            }
            if quiet {
                say("info", LEFT);
                return Ok(ExitCode::FAILURE);
            }
            quiet = true;
            say(
                "info",
                "stopped following; F1 25 keeps running, and its game files are rolled back when it exits (Ctrl-C again to leave now)",
            );
        }
        if !quiet {
            follow.poll();
            if let Some(r) = relay.as_mut() {
                relay_lines(r);
            }
        }
        if let Some(status) = wrapper.exited()
            && !dir.join("exit_code").exists()
        {
            let err = wrapper.failure(status, "while the game was running");
            settle(store, &g, &mut wrapper, relay.as_mut(), false);
            return Err(err);
        }
        std::thread::sleep(Duration::from_millis(250));
    }
    if !quiet {
        std::thread::sleep(Duration::from_millis(1500));
        follow.poll();
    }
    let summary = follow.summary();
    settle(store, &g, &mut wrapper, relay.as_mut(), true);
    summary?;
    if let Some(failure) = launch::Failure::read(&store.root).filter(|f| f.at >= asked) {
        if relay.is_none() {
            say("warning", &format!("{}: {}", failure.title(), failure.text()));
        }
        say("info", &format!("launch failed: {}", failure.headline()));
        return Ok(ExitCode::FAILURE);
    }
    Ok(ExitCode::SUCCESS)
}

const LEFT: &str = "left without waiting; the next swofty run or launcher start rolls the game files back";
const SETTLE_TIMEOUT: Duration = Duration::from_secs(600);

fn info(line: &str) {
    say("info", line);
}

/// After the game: wait for the wrapper to finish rolling the session back,
/// relaying what it reports, then put back anything it did not (a wrapper
/// killed mid-rollback leaves its journal open).
fn settle(store: &Store, g: &Game, wrapper: &mut Wrapper, mut relay: Option<&mut Follower>, ended: bool) {
    if let Some(child) = wrapper.child.as_mut() {
        loop {
            if let Some(r) = relay.as_deref_mut() {
                relay_lines(r);
            }
            if !matches!(child.try_wait(), Ok(None)) {
                break;
            }
            if stop_requested() {
                say("info", LEFT);
                return;
            }
            std::thread::sleep(Duration::from_millis(250));
        }
        if let Some(r) = relay {
            relay_lines(r);
        }
    }
    let started = Instant::now();
    let lock = loop {
        match SessionLock::try_acquire(&store.root) {
            Ok(Some(lock)) => break lock,
            Ok(None) => {}
            Err(e) => {
                say("info", &format!("cannot check the session lock: {e}"));
                return;
            }
        }
        if wrapper.child.is_none() && game_running_in(&g.dir) {
            say(
                "info",
                "F1 25 is running again; its game files are rolled back when it exits",
            );
            return;
        }
        if started.elapsed() > SETTLE_TIMEOUT || stop_requested() {
            say(
                "info",
                "the session is still rolling back; the next swofty run or launcher start finishes it",
            );
            return;
        }
        std::thread::sleep(Duration::from_millis(250));
    };
    match swofty_core::recovery::recover(&lock, &store.root, g, Scope::All) {
        Ok(r) if r.recovered() => {
            say("info", "the wrapper ended without rolling the session back");
            crate::recover::announce(store, g, &r.lines(), &info);
        }
        Ok(r) => {
            r.lines().iter().for_each(|l| info(l));
            if ended {
                info("session over: its game files are rolled back");
            }
        }
        Err(e) => info(&format!("rollback failed, run swofty restore: {e:#}")),
    }
}

fn relay_lines(r: &mut Follower) {
    for line in r.lines() {
        let line = line.trim_end();
        if line.is_empty() {
            continue;
        }
        match line.strip_prefix('[').and_then(|l| l.split_once("] ")) {
            Some((kind, text)) if !kind.contains(' ') => say(kind, text),
            _ => say("wrapper", line.strip_prefix("swofty: ").unwrap_or(line)),
        }
    }
}

pub fn logs(store: &Store, dir: Option<PathBuf>, exe: Option<PathBuf>) -> Result<ExitCode> {
    let dir = match dir {
        Some(d) => d,
        None => store.latest_session().context("no previous session")?,
    };
    let game = steam::find_game().map(|g| g.dir).unwrap_or_default();
    let mut follow = Follow::new(&dir, None, symbols(&game, exe.as_deref()));
    follow.poll();
    follow.summary()?;
    if let Some(crash) = swofty_core::send_logs::newest_crash(store, Some(&game), std::slice::from_ref(&dir))
    {
        say("crash", &format!("crash report {}", crash.report.display()));
        match swofty_core::send_logs::summarise_crash(&crash.report) {
            Ok(lines) => {
                for l in lines {
                    say("crash", &l);
                }
            }
            Err(e) => say("crash", &format!("could not read it: {e}")),
        }
        if let Some(dump) = &crash.dump {
            say(
                "crash",
                &format!("minidump {} (open it in a debugger)", dump.display()),
            );
        }
    }
    Ok(ExitCode::SUCCESS)
}

fn wrapper(store: &Store, profile: &str, args: &RunArgs, g: &Game) -> Result<ExitCode> {
    if let Some(running) = game_processes().first() {
        return Err(already_running(
            store,
            running.pid,
            "close it before launching it again",
        ));
    }
    let lock = crate::recover::lock(store, &info)?;
    crate::recover::stale(store, &lock, g, Scope::All, &info)?;
    crate::offline::gate(store, false)?;
    if let Some(reason) = swofty_core::compat::elevation_blocker(&g.dir) {
        bail!("{reason}");
    }
    end_on_signals();
    let config: Config = std::fs::read(store.config_path())
        .ok()
        .and_then(|d| serde_json::from_slice(&d).ok())
        .unwrap_or_default();
    let request: LaunchRequest = std::fs::read(store.root.join("state/launch.json"))
        .ok()
        .and_then(|d| serde_json::from_slice(&d).ok())
        .unwrap_or_default();
    let profile = request.profile.clone().unwrap_or_else(|| profile.to_owned());
    if let Some(cpus) = config.launch.cpu_affinity.as_deref().and_then(parse_cpu_list) {
        set_affinity(&cpus);
    }
    let game = g.dir.clone();
    let apply = request.apply || args.apply;
    quarantine::Report::clear(&store.root).ok();
    launch::Failure::clear(&store.root).ok();
    let mut session = Session {
        store,
        game: g,
        request_id: request.id.clone(),
        _lock: lock,
        copy: false,
        overlay: None,
    };
    swofty_core::overlayfs::LiveSession::clear(&store.root);
    become_subreaper();
    let artifacts = crate::offline::ensure_extension(store)?;
    if let Some(note) = crate::offline::loader_note(&artifacts) {
        say("info", &format!("offline play: {note}"));
    }
    let probe = swofty_core::offline::probe_cached(&store.root, &game, &artifacts)?;
    if !probe.supported {
        bail!(
            "this F1 25 build is not supported by the offline play extension yet ({}); \
             the modded session was refused",
            probe.report
        );
    }
    say("info", &format!("game build supported: {}", probe.report));
    let mut overlay = None;
    if args.use_overlayfs {
        let proton = proton_path(&args.steam_command);
        match overlay::choose(store, &profile, g, &artifacts, proton.as_deref(), apply)? {
            Choice::Overlay(o) => overlay = Some(*o),
            Choice::Copy => {}
            Choice::Cancelled => return finish(session, Ok(ExitCode::from(overlay::CANCELLED_EXIT))),
        }
    }
    let mut installed = None;
    if overlay.is_none() {
        installed = start_copy(&mut session, &profile, apply, &artifacts)?;
    }
    session.overlay.clone_from(&overlay);
    let mut result = run_loop(
        store,
        args,
        &config,
        &request,
        &profile,
        g,
        match overlay.as_mut() {
            Some(o) => Mode::Overlay(o),
            None => Mode::Copy { installed },
        },
    );
    // The mount can still fail when the game is started: fall back before it runs.
    let failed = match (&result, &overlay) {
        (Err(e), Some(_)) => e.downcast_ref::<overlay::MountFailed>().map(|m| m.0.clone()),
        _ => None,
    };
    if let Some(reason) = failed {
        session.overlay = None;
        match overlay::fall_back(store, swofty_core::overlayfs::Kind::MountFailed, &reason) {
            Choice::Cancelled => result = Ok(ExitCode::from(overlay::CANCELLED_EXIT)),
            _ => {
                let installed = start_copy(&mut session, &profile, apply, &artifacts)?;
                result = run_loop(
                    store,
                    args,
                    &config,
                    &request,
                    &profile,
                    g,
                    Mode::Copy { installed },
                );
            }
        }
    }
    finish(session, result)
}

/// Copy mode: install the profile into the game folder and stage offline play
/// there, as before OverlayFS. Returns when the install finished.
fn start_copy(
    session: &mut Session,
    profile: &str,
    apply: bool,
    artifacts: &Artifacts,
) -> Result<Option<Instant>> {
    let (store, g) = (session.store, session.game);
    if swofty_core::compat::write_blocker(&g.dir).is_some() {
        bail!("{}", swofty_core::compat::write_blocked_text(&g.dir));
    }
    swofty_core::overlayfs::forget_last(&store.root);
    session.copy = true;
    if apply {
        say("info", &format!("installing profile {profile}"));
        crate::build::install(store, profile, g, true, &info)?;
    }
    let installed = apply.then(Instant::now);
    let journal = swofty_core::offline::journal_path(&store.root);
    for note in swofty_core::offline::stage(&g.dir, artifacts, &journal)? {
        say("info", &format!("offline play: {note}"));
    }
    say(
        "info",
        "staged offline play: EA AntiCheat is not started this session; multiplayer is unavailable",
    );
    Ok(installed)
}

/// The Proton the Steam command runs the game with.
fn proton_path(steam_command: &[String]) -> Option<PathBuf> {
    parse_steam_command(steam_command)
        .proton
        .map(PathBuf::from)
        .or_else(|| {
            let tools = std::env::var("STEAM_COMPAT_TOOL_PATHS").ok()?;
            Some(PathBuf::from(tools.split(':').next()?).join("proton"))
        })
}

/// A launch the loader check refused before the game started.
#[derive(Debug)]
struct Refused(quarantine::Report);

impl std::fmt::Display for Refused {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0.text())
    }
}

impl std::error::Error for Refused {}

/// End the session, which rolls the game folder back; a refused launch then
/// exits with its own code, so the launcher and `swofty run` can tell it apart.
fn finish(session: Session, result: Result<ExitCode>) -> Result<ExitCode> {
    drop(session);
    match result.map_err(|e| e.downcast::<Refused>()) {
        Err(Ok(refused)) => {
            eprintln!("swofty: {refused}");
            Ok(ExitCode::from(quarantine::REFUSED_EXIT))
        }
        Err(Err(e)) => Err(e),
        Ok(code) => Ok(code),
    }
}

/// Why a rollback could not touch the game files, when the copy's game is
/// still running and holds them.
fn still_running(game: &Path) -> String {
    match games_in(game).first() {
        Some(g) => format!(
            " because F1 25 ({GAME_EXE}, pid {}) is still running and holds the game files",
            g.pid
        ),
        None => String::new(),
    }
}

/// The wrapper's refusal of this launch (asked at `since`), once written.
fn refused(store: &Store, since: f64) -> Option<quarantine::Report> {
    quarantine::Report::read(&store.root).filter(|r| r.stage == Stage::BeforeLaunch && r.at >= since)
}

/// A wrapper session: it holds the session lock, and however it ends (the
/// game exits or crashes, an error, a panic, a stop signal) dropping it puts
/// the offline play staging and the profile's game files back, or in an
/// OverlayFS session, collects the game's logs.
struct Session<'a> {
    store: &'a Store,
    game: &'a Game,
    request_id: String,
    _lock: SessionLock,
    /// Files were (or are about to be) copied into the game folder.
    copy: bool,
    overlay: Option<Overlay>,
}

impl Drop for Session<'_> {
    fn drop(&mut self) {
        let logs = self
            .store
            .latest_session()
            .filter(|d| {
                std::fs::read_to_string(d.join("request_id")).is_ok_and(|s| s.trim() == self.request_id)
            })
            .map(|d| d.join("game-logs"))
            .unwrap_or_else(|| self.store.sessions_dir().join("orphaned-logs"));
        if let Some(o) = &self.overlay {
            swofty_core::overlayfs::LiveSession::clear(&self.store.root);
            match swofty_core::overlayfs::collect_logs(&o.upper, &self.store.logs_dir(), Some(&logs)) {
                Ok(0) => {}
                Ok(n) => info(&format!(
                    "copied {n} game log file(s) to {}",
                    self.store.logs_dir().display()
                )),
                Err(e) => info(&format!("could not collect the game's logs: {e:#}")),
            }
        }
        if !self.copy {
            return;
        }
        match swofty_core::offline::restore_pending(&self.store.root) {
            Ok(true) => info("offline play: the game folder is back to vanilla"),
            Ok(false) => {}
            Err(e) => info(&format!(
                "offline play restore failed{}, run swofty restore: {e:#}",
                still_running(&self.game.dir)
            )),
        }
        let inst = Installer::new(&self.store.root, self.game);
        match inst.roll_back(false) {
            Ok(done) => {
                if !done.is_empty() {
                    info("rolled the profile's game files back");
                }
                for d in done.iter().flat_map(|r| &r.drift) {
                    info(&format!("rollback: {d}"));
                }
            }
            Err(e) => info(&format!(
                "rollback failed{}; the next swofty run or launcher start retries it, or run swofty restore: {e:#}",
                still_running(&self.game.dir)
            )),
        }
        match inst.tidy(&logs) {
            Ok(0) => {}
            Ok(n) => info(&format!(
                "moved {n} log file(s) out of the game folder to {}",
                logs.display()
            )),
            Err(e) => info(&format!("the loader folder was left in the game folder: {e:#}")),
        }
    }
}

/// How a session gets the profile's files to the game.
enum Mode<'a> {
    /// Copied into the game folder; the install finished at `installed`.
    Copy { installed: Option<Instant> },
    /// Mounted over it.
    Overlay(&'a mut Overlay),
}

fn run_loop(
    store: &Store,
    args: &RunArgs,
    config: &Config,
    request: &LaunchRequest,
    profile: &str,
    g: &Game,
    mode: Mode,
) -> Result<ExitCode> {
    let (mut installed, mut overlay) = match mode {
        Mode::Copy { installed } => (installed, None),
        Mode::Overlay(o) => (None, Some(o)),
    };
    let profile = profile.to_owned();
    let game = g.dir.clone();
    let mut restarted = false;
    loop {
        if ending() {
            return Ok(ExitCode::FAILURE);
        }
        match overlay.as_deref_mut() {
            // The first start has just built the layers; a restart builds the
            // profile again, since the changes made in game are merged into it.
            Some(o) if restarted => {
                say("info", &format!("rebuilding the overlay of {profile}"));
                o.rebuild(store, g)?;
            }
            Some(_) => {}
            None => rebuild_if_needed(store, &profile, &game),
        }
        let code = play(
            store,
            args,
            config,
            request,
            g,
            Play {
                installed,
                restarted,
                overlay: overlay.as_deref(),
            },
        )?;
        let relaunch = match store.merge_requests(&profile, true) {
            Ok(Some(m)) => {
                for c in &m.changed {
                    say("info", &format!("in game: {c}"));
                }
                for c in &m.skipped {
                    say("info", &format!("in game, skipped: {c}"));
                }
                m.relaunch
            }
            Ok(None) => false,
            Err(e) => {
                say("info", &format!("in-game changes not merged: {e}"));
                false
            }
        };
        if !relaunch || ending() {
            return Ok(ExitCode::from(code.clamp(0, 255) as u8));
        }
        say("info", "Apply & restart: starting F1 25 again");
        restarted = true;
        if overlay.is_none() && installed.is_some() {
            say("info", &format!("reinstalling profile {profile}"));
            crate::build::install(store, &profile, g, true, &info)?;
            installed = Some(Instant::now());
        }
    }
}

fn rebuild_if_needed(store: &Store, profile: &str, game: &Path) {
    let pending = match (store.profile(profile), store.addons()) {
        (Ok(p), Ok(addons)) => p.rebuild_pending(&addons),
        _ => return,
    };
    if pending.is_empty() {
        return;
    }
    say(
        "info",
        &format!("rebuilding {profile} for {}", pending.join(", ")),
    );
    if let Err(e) = crate::build::build(store, profile, Some(game.to_path_buf()), false) {
        say("info", &format!("rebuild failed, launching anyway: {e:#}"));
    }
}

/// What a start of the game needs besides the session's settings.
struct Play<'a> {
    installed: Option<Instant>,
    restarted: bool,
    overlay: Option<&'a Overlay>,
}

fn play(
    store: &Store,
    args: &RunArgs,
    config: &Config,
    request: &LaunchRequest,
    g: &Game,
    Play {
        installed,
        restarted,
        overlay,
    }: Play,
) -> Result<i32> {
    let game = g.dir.as_path();
    // Where the game's own files (its logs) are on the host: the game folder,
    // or in an OverlayFS session the upper dir its writes land in.
    let root = overlay.map_or(game, |o| o.upper.as_path());
    let placed_loader = match overlay {
        Some(o) => o.prepared.layers.has_loader(),
        None => check_loader(store, game, installed).map_err(Refused)?,
    };
    // The profile build and the offline play download before the first
    // start can take minutes, time enough for F1 25 to be started from
    // Steam. On Apply & restart the previous copy is still on its way out.
    if let Some(pid) = running_copy(if restarted { RESTART_WAIT } else { Duration::ZERO }) {
        return Err(already_running(store, pid, "close it before launching it again"));
    }
    let dir = store.sessions_dir().join(stamp());
    std::fs::create_dir_all(&dir)?;
    let loader = root.join(LOADER_LOG);
    let before = LogMark::read(&loader);
    std::fs::write(dir.join(LOADER_MARK), serde_json::to_vec(&before)?)?;
    let fallback = store.root.join(LOADER_FALLBACK);
    for stale in [fallback.clone(), fallback.with_extension("prev.log")] {
        std::fs::remove_file(stale).ok();
    }
    let latest = store.sessions_dir().join("latest");
    std::fs::remove_file(&latest).ok();
    link(&dir, &latest);
    std::fs::write(store.sessions_dir().join("latest.txt"), dir.display().to_string())?;
    std::fs::write(dir.join("request_id"), format!("{}\n", request.id))?;
    std::fs::write(dir.join("steam_command.txt"), shell_join(&args.steam_command))?;
    let cmd = parse_steam_command(&args.steam_command);
    let mode = if args.vr {
        LaunchMode::Vr
    } else {
        LaunchMode::of(cmd.game_args())
    };
    let extra = game_args(cmd.game_args(), mode);
    let proton = proton_path(&args.steam_command);
    let winedebug = request
        .winedebug
        .clone()
        .unwrap_or(config.launch.winedebug.clone());
    // Proton's steam helper quote-wraps targets whose path contains spaces
    // and then fails to open the quoted name at all, so the game folder's
    // "F1 25" cannot be passed directly. A space-free symlink to the folder
    // gives an absolute target that survives untouched; the game and the
    // loader see the same files through it.
    let game_link = match overlay {
        Some(o) => link_to(&store.root.join("state/game-link-overlay"), &o.target)?,
        None => link_to(&store.root.join("state/game-link"), game)?,
    };
    // The game gets a curated environment: Proton's steam helper packs it
    // into a Windows environment block (32767 characters), and a build
    // shell's variables blow past that, which kills the game at startup.
    let (shell_vars, mut game_env) = {
        let shell: Vec<(String, String)> = std::env::vars().collect();
        if let Some((k, v)) = shell.iter().find(|(k, _)| is_compat_layer(k))
            && !config.launch.env.keys().any(|k| is_compat_layer(k))
        {
            say(
                "info",
                &format!("environment: not passing the Windows compatibility layer {k}={v} to the game"),
            );
        }
        let n = shell.iter().filter(|(k, _)| !is_compat_layer(k)).count();
        let env = swofty_core::session::game_env(shell, &config.launch.env);
        (n, env)
    };
    if game_env.len() != shell_vars {
        say(
            "info",
            &format!(
                "environment: passing {} of {} variables to the game (the Windows limit is 32K characters)",
                game_env.len(),
                shell_vars
            ),
        );
    }
    let library_path = proton.as_ref().and_then(|_| {
        let steam_root = std::env::var_os("STEAM_COMPAT_CLIENT_INSTALL_PATH")
            .map(PathBuf::from)
            .or_else(|| steam::find_game().map(|g| g.steam_root))?;
        swofty_core::session::sdl_library_path(
            game_env.get("LD_LIBRARY_PATH").map(String::as_str),
            &steam_root,
            &store.root.join(RUNTIME_LIBS_DIR),
        )
    });
    if library_path.is_some() {
        say(
            "info",
            "controllers: this system has no SDL2, so Proton reads wheels and pads through Steam's runtime copy",
        );
    }
    game_env.extend(
        [
            ("PROTON_LOG", "1".to_owned()),
            ("PROTON_LOG_DIR", dir.to_string_lossy().into_owned()),
            ("WINEDEBUG", winedebug),
            ("SteamAppId", STEAM_APP_ID.to_string()),
            ("SteamGameId", STEAM_APP_ID.to_string()),
        ]
        .into_iter()
        .chain(library_path.map(|p| ("LD_LIBRARY_PATH", p)))
        .map(|(k, v)| (k.to_owned(), v)),
    );
    // The prefix's registry override is not always what Wine reads at start,
    // so the session also names version.dll in WINEDLLOVERRIDES, which wins
    // over the registry.
    if proton.is_some() && placed_loader {
        let prefix = match g.user_reg.as_deref() {
            Some(reg)
                if std::fs::read_to_string(reg).is_ok_and(|t| swofty_core::install::has_override(&t)) =>
            {
                format!("{} has the version.dll override", reg.display())
            }
            Some(reg) => format!("{} has no version.dll override", reg.display()),
            None => "no Proton prefix found for this game folder".to_owned(),
        };
        let set = game_env.get("WINEDLLOVERRIDES").map(String::as_str);
        match swofty_core::session::with_version_override(set) {
            Some(v) => {
                game_env.insert("WINEDLLOVERRIDES".to_owned(), v);
                say(
                    "info",
                    &format!("wine: {prefix}; WINEDLLOVERRIDES loads the loader's version.dll"),
                );
            }
            None => say(
                "info",
                &format!(
                    "wine: {prefix}; WINEDLLOVERRIDES already names version.dll: {}",
                    set.unwrap_or_default()
                ),
            ),
        }
    }
    let listed: String = game_env.iter().map(|(k, v)| format!("{k}={v}\n")).collect();
    std::fs::write(dir.join("env.txt"), listed)?;
    let spawn = |exe: &str, log: &str| -> Result<std::process::Child> {
        if let Some(o) = overlay {
            let proton = proton
                .as_ref()
                .context("OverlayFS needs Proton to start the game")?;
            let mut run: Vec<OsString> = vec![proton.into(), "run".into(), game_link.join(exe).into()];
            run.extend(extra.iter().map(OsString::from));
            let mut child = o.spawn(&run, &game_env, &dir.join(log))?;
            if let Err(e) = o.record(store, child.id()) {
                child.kill().ok();
                child.wait().ok();
                return Err(e);
            }
            return Ok(child);
        }
        let out = std::fs::File::create(dir.join(log))?;
        let mut c = match &proton {
            Some(p) => {
                let mut c = Command::new(p);
                c.arg("run").arg(game_link.join(exe));
                c
            }
            None => Command::new(game.join(exe)),
        };
        c.args(&extra)
            .current_dir(game)
            .env_clear()
            .envs(&game_env)
            .stdout(out.try_clone()?)
            .stderr(out);
        start(&mut c)
    };
    let copies_before: Vec<u32> = game_processes().iter().map(|g| g.pid).collect();
    let launched = SystemTime::now();
    std::fs::write(dir.join("game_start"), format!("{}\n", now_secs()))?;
    // The offline-play session launcher (staged in place of the EA
    // AntiCheat launcher) creates the game suspended and patches the launch
    // guard before the game's first instruction: the check runs before any
    // window exists, so the in-process hook is too late to act on it.
    let target = if args.via_ea {
        GAME_EXE
    } else if overlay.is_some() {
        say(
            "info",
            "offline play: launching via the session launcher (OverlayFS)",
        );
        "EAAntiCheat.GameServiceLauncher.exe"
    } else if game.join("EAAntiCheat.GameServiceLauncher.exe.bak").is_file() {
        say("info", "offline play: launching via the session launcher");
        "EAAntiCheat.GameServiceLauncher.exe"
    } else {
        GAME_EXE
    };
    if mode.is_vr() {
        say("info", &format!("starting F1 25 in VR ({})", extra.join(" ")));
    }
    let usage = swofty_core::usage::Session::start(&store.root);
    let new_watch =
        || AttachWatch::new(loader.clone(), before.clone(), Instant::now()).with_fallback(fallback.clone());
    let mut watch = placed_loader.then(new_watch);
    let placed = quarantine::placed_dlls(&store.root)
        .unwrap_or_default()
        .into_iter()
        .find_map(|(rel, sha)| rel.eq_ignore_ascii_case(PROXY).then_some(sha));
    let missed = || {
        if let Some(o) = overlay {
            say(
                "loader",
                &format!(
                    "version.dll did not attach to F1 25; the OverlayFS session's loader log is {}",
                    o.upper.join(LOADER_LOG).display()
                ),
            );
            return;
        }
        let found = loader_check::collect(game, placed.as_deref(), launched, g.user_reg.as_deref());
        say(
            "loader",
            "version.dll did not attach to F1 25; what the session found:",
        );
        for line in found.lines(game) {
            say("loader", &format!("  {line}"));
        }
        // The antivirus explanation is a Windows one; under Proton a loader
        // that does not attach is a prefix or override problem instead.
        if cfg!(windows) {
            loader_missed(store, game, found.cause(game));
        }
    };
    let ea = match args.via_ea {
        true => Some(crate::ea::start(store, game, &extra, &dir, &copies_before)?),
        false => None,
    };
    if ea.is_some() {
        watch = placed_loader.then(new_watch);
    }
    let verdict = |state: Attach| match state {
        Attach::Missed => missed(),
        Attach::Attached => note_refused(&fallback),
        Attach::Waiting => {}
    };
    let tick = || {
        if let Some(w) = watch.as_mut()
            && w.state() == Attach::Waiting
        {
            verdict(w.poll(Instant::now()));
        }
    };
    let (code, unread) = match ea {
        Some(started) => {
            crate::ea::wait(game, &copies_before, started, tick);
            (None, EA_UNREAD.to_owned())
        }
        None => {
            let status = wait_game(&mut spawn(target, "game_stdout.log")?, tick)?;
            (status.code(), why_no_code(status))
        }
    };
    if let Some(w) = watch.as_mut()
        && w.state() == Attach::Waiting
    {
        verdict(w.ended(Instant::now()));
    }
    wait_relaunched(game, &copies_before, code, launched.elapsed().unwrap_or_default());
    keep_game_logs(root, &fallback, &dir, before.as_ref(), launched);
    swofty_core::overlayfs::LiveSession::clear(&store.root);
    let loader = std::fs::read(dir.join("loader.log"))
        .ok()
        .map(|b| launch::LoaderSeen::scan(&String::from_utf8_lossy(&b)));
    let stopped = if dir.join(STOP_FILE).exists() {
        Some("Stop in the launcher closed F1 25")
    } else if ending() {
        Some("the wrapper was asked to stop and closed F1 25")
    } else {
        None
    };
    let end = SessionEnd::resolve(code, &unread, stopped, loader.as_ref());
    say(
        "info",
        &format!("session end: {}, {}", end.end.as_str(), end.reason),
    );
    if let Some(usage) = usage {
        usage.end(&end);
    }
    let code = end.exit_code.unwrap_or(-1);
    end.write(&dir)?;
    std::fs::write(dir.join("exit_code"), format!("{code}\n"))?;
    std::fs::write(dir.join("game_end"), format!("{}\n", now_secs()))?;
    for g in game_processes()
        .into_iter()
        .filter(|g| descends_from(g.pid, std::process::id()))
    {
        stop_process(g.pid).ok();
    }
    note_failed_launch(store, loader, code, launched.elapsed().unwrap_or_default());
    Ok(code)
}

/// A space-free symlink at `link` to `dest`, made or repointed as needed.
fn link_to(link: &Path, dest: &Path) -> Result<PathBuf> {
    #[cfg(unix)]
    {
        match std::fs::symlink_metadata(link) {
            Ok(m) if m.file_type().is_symlink() => {
                if std::fs::read_link(link).ok().as_deref() != Some(dest) {
                    std::fs::remove_file(link).and_then(|_| std::os::unix::fs::symlink(dest, link))?;
                }
            }
            Ok(_) => bail!(
                "{} exists and is not the game link; remove it and try again",
                link.display()
            ),
            Err(_) => {
                if let Some(parent) = link.parent() {
                    std::fs::create_dir_all(parent)?;
                }
                std::os::unix::fs::symlink(dest, link)?;
            }
        }
    }
    #[cfg(not(unix))]
    let _ = dest;
    Ok(link.to_owned())
}

const EA_UNREAD: &str =
    "the EA app started F1 25, so the session has no handle on the game to read its exit code";

/// Why an exit status carries no code.
fn why_no_code(status: ExitStatus) -> String {
    #[cfg(unix)]
    if let Some(signal) = std::os::unix::process::ExitStatusExt::signal(&status) {
        return format!("the game's process was ended by signal {signal}, which leaves no exit code");
    }
    format!("the game's process ended without an exit code ({status})")
}

const RESTART_WAIT: Duration = Duration::from_secs(15);
const OTHERS_SETTLE: Duration = Duration::from_secs(3);
const RELAUNCH_SETTLE: Duration = Duration::from_secs(2);
const RELAUNCH_AFTER_FAILURE: Duration = Duration::from_secs(20);
const SHORT_RUN: Duration = Duration::from_secs(60);
const RELAUNCH_CAP: Duration = Duration::from_secs(12 * 3600);

/// After the launched game exits: the EA App build hands a launch it did not
/// get from the EA app back to the EA app, which starts F1 25 again through
/// the game folder's launcher. That copy is not the session's descendant and
/// still holds the staged files, so the session waits for it before the
/// game files are rolled back. A launch that failed quickly gets longer for
/// the relaunch to appear. Another copy's game is not waited for, and only
/// a game that is this copy's is closed.
fn wait_relaunched(game: &Path, before: &[u32], code: Option<i32>, ran: Duration) {
    let settle = if code.is_some_and(|c| c != 0) && ran < SHORT_RUN {
        RELAUNCH_AFTER_FAILURE
    } else {
        RELAUNCH_SETTLE
    };
    let start = Instant::now();
    let until = start + settle;
    let mut seen: Vec<u32> = Vec::new();
    let mut asked = false;
    loop {
        let running = game_processes();
        let others = swofty_core::process::started_elsewhere(&running, game, before, |pid| {
            descends_from(pid, std::process::id())
        });
        let new: Vec<u32> = others.iter().copied().filter(|p| !seen.contains(p)).collect();
        for pid in new {
            seen.push(pid);
            say(
                "info",
                &format!(
                    "waiting for a second F1 25 process (pid {pid}) started outside this session, usually by the \
                     EA app; the game files are rolled back once it closes"
                ),
            );
        }
        if others.is_empty() && (!seen.is_empty() || Instant::now() >= until) {
            break;
        }
        if start.elapsed() >= RELAUNCH_CAP {
            info(
                "stopped waiting for the second F1 25 process after 12 hours; rolling the game files back now",
            );
            return;
        }
        if !asked && ending() && !others.is_empty() {
            asked = true;
            info(
                "asked to stop: closing the F1 25 started outside this session, then rolling its game files back",
            );
            for g in running
                .iter()
                .filter(|g| others.contains(&g.pid) && g.is_in(game))
            {
                stop_process(g.pid).ok();
            }
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    if !seen.is_empty() {
        say("info", "the second F1 25 process has closed");
    }
}

/// A copy of F1 25 still running after `wait`.
fn running_copy(wait: Duration) -> Option<u32> {
    let until = Instant::now() + wait;
    loop {
        match game_processes().first() {
            None => return None,
            Some(g) if Instant::now() >= until => return Some(g.pid),
            Some(_) => std::thread::sleep(Duration::from_millis(500)),
        }
    }
}

/// Refuse a launch over a running copy, and record why for the launcher.
fn already_running(store: &Store, pid: u32, then: &str) -> anyhow::Error {
    let failure = launch::Failure::new(launch::Cause::AlreadyRunning { pid });
    if let Err(e) = failure.write(&store.root) {
        info(&format!("could not record it for the launcher: {e}"));
    }
    anyhow::anyhow!("F1 25 is already running (pid {pid}); {then}")
}

/// After the game exits: a clean exit before its window is a launch that
/// failed, which the session says and records instead of passing it off as
/// a normal quit.
fn note_failed_launch(store: &Store, loader: Option<launch::LoaderSeen>, code: i32, ran: Duration) {
    let ours = loader.and_then(|l| l.pid).filter(|_| cfg!(windows));
    let Some(cause) = launch::classify(code, ran, loader.as_ref(), || other_copies(ours)) else {
        return;
    };
    let failure = launch::Failure::new(cause);
    say("warning", &format!("{}: {}", failure.title(), failure.text()));
    if let Err(e) = failure.write(&store.root) {
        info(&format!("could not record it for the launcher: {e}"));
    }
}

/// The copies of F1 25 this session did not start. Ours has exited by now,
/// though its process can take a moment to leave the list; under Proton the
/// session's own processes are its descendants, on Windows the loader names
/// the game's pid.
fn other_copies(ours: Option<u32>) -> Vec<u32> {
    let until = Instant::now() + OTHERS_SETTLE;
    loop {
        let others: Vec<u32> = game_processes()
            .into_iter()
            .filter(|g| Some(g.pid) != ours && !descends_from(g.pid, std::process::id()))
            .map(|g| g.pid)
            .collect();
        if others.is_empty() || Instant::now() >= until {
            return others;
        }
        std::thread::sleep(Duration::from_millis(250));
    }
}

/// Before the game starts: the DLLs the session placed are still there with
/// the bytes written, on a first look and again a moment later, since a
/// scanner often takes a file only once it is closed. Whether the session
/// placed version.dll (so the loader should attach), or the report on the
/// missing ones, which refuses the launch.
fn check_loader(
    store: &Store,
    game: &Path,
    installed: Option<Instant>,
) -> std::result::Result<bool, quarantine::Report> {
    let dlls = quarantine::placed_dlls(&store.root).unwrap_or_default();
    let loader = dlls.iter().any(|(rel, _)| rel.eq_ignore_ascii_case(PROXY));
    let mut files = quarantine::damaged_install();
    let first = quarantine::missing(game, &dlls);
    if let Some(at) = installed.filter(|_| !dlls.is_empty()) {
        std::thread::sleep(quarantine::RECHECK_AFTER.saturating_sub(at.elapsed()));
    }
    for rel in first.into_iter().chain(quarantine::missing(game, &dlls)) {
        if !files.contains(&rel) {
            files.push(rel);
        }
    }
    if files.is_empty() {
        return Ok(loader);
    }
    let report = quarantine::Report::new(Stage::BeforeLaunch, files, game);
    report_blocked(store, &report);
    Err(report)
}

/// The game is running but the loader never attached: say so, and whether
/// version.dll is gone from the game folder by now or what else the session
/// found keeping it out.
fn loader_missed(store: &Store, game: &Path, cause: Option<quarantine::Cause>) {
    let mut report = quarantine::Report::new(Stage::InGame, vec![PROXY.to_owned()], game);
    report.removed = !game.join(PROXY).is_file();
    report.cause = cause.filter(|_| !report.removed).map(Box::new);
    say(
        "loader",
        &format!(
            "  found: {}",
            match &report.cause {
                Some(c) => c.what(),
                None if report.removed => format!("{PROXY} was removed from the game folder"),
                None => "nothing that explains it".into(),
            }
        ),
    );
    report_blocked(store, &report);
}

fn report_blocked(store: &Store, report: &quarantine::Report) {
    say("warning", &format!("{}: {}", report.title(), report.text()));
    if let Err(e) = report.write(&store.root) {
        info(&format!("could not record it for the launcher: {e}"));
    }
}

/// The loader attached but could not log in the game folder: say why, and
/// what that breaks.
fn note_refused(fallback: &Path) {
    let Some(why) = std::fs::read(fallback)
        .ok()
        .and_then(|b| quarantine::refused(&String::from_utf8_lossy(&b)))
    else {
        return;
    };
    say(
        "loader",
        &format!(
            "version.dll attached, but its log could not go in the game folder: {why}. The log is in {}",
            fallback.display()
        ),
    );
    say(
        "warning",
        &quarantine::refused_text(swofty_core::compat::elevated()),
    );
}

/// The session's loader.log: the game folder's when the loader wrote it,
/// else its fallback in the data folder, which a session starts without.
fn keep_game_logs(game: &Path, fallback: &Path, dir: &Path, before: Option<&LogMark>, launched: SystemTime) {
    let loader = game.join(LOADER_LOG);
    if LogMark::replaced(&loader, before) {
        std::fs::copy(&loader, dir.join("loader.log")).ok();
    } else if fallback.is_file() {
        std::fs::copy(fallback, dir.join("loader.log")).ok();
    }
    let perf = game.join(PERF_LOG);
    if written_since(&perf, launched) {
        std::fs::copy(&perf, dir.join("perf.log")).ok();
    }
}

/// Wait for the game; a stop signal to the wrapper (Steam's Stop button, a
/// kill, a closed terminal) closes the game it started instead of leaving
/// the session's files in the game folder.
fn wait_game(child: &mut Child, mut tick: impl FnMut()) -> Result<ExitStatus> {
    let mut asked = false;
    loop {
        if let Some(status) = child.try_wait()? {
            return Ok(status);
        }
        tick();
        if !asked && ending() {
            asked = true;
            info("asked to stop: closing F1 25, then rolling its game files back");
            let ours: Vec<u32> = game_processes()
                .into_iter()
                .filter(|g| descends_from(g.pid, std::process::id()))
                .map(|g| g.pid)
                .collect();
            if ours.is_empty() {
                child.kill().ok();
            }
            for pid in ours {
                stop_process(pid).ok();
            }
        }
        std::thread::sleep(Duration::from_millis(200));
    }
}

const DIRECT_START_TIMEOUT: Duration = Duration::from_secs(600);
const STEAM_START_TIMEOUT: Duration = Duration::from_secs(120);
const WRAPPER_LOG: &str = "state/wrapper-run.log";
const TAIL_LINES: usize = 12;

struct Wrapper {
    child: Option<Child>,
    log: PathBuf,
    timeout: Duration,
}

impl Wrapper {
    fn exited(&mut self) -> Option<ExitStatus> {
        self.child.as_mut()?.try_wait().ok().flatten()
    }

    /// The wrapper's error starts at its last `swofty: ` line, and an error
    /// over several lines leads with what failed: the reason runs from there
    /// to the end (else it is the last line), and the lines before it are
    /// echoed.
    fn failure(&self, status: ExitStatus, when: &str) -> anyhow::Error {
        let mut lines = tail(&self.log, TAIL_LINES);
        let from = lines
            .iter()
            .rposition(|l| l.starts_with("swofty: "))
            .unwrap_or(lines.len().saturating_sub(1));
        let reason = lines.split_off(from).join("\n");
        for l in &lines {
            say("wrapper", l);
        }
        if reason.is_empty() {
            return anyhow::anyhow!(
                "the wrapper exited ({status}) {when} without output; see {}",
                self.log.display()
            );
        }
        let reason = reason.strip_prefix("swofty: ").unwrap_or(&reason);
        anyhow::anyhow!("the wrapper exited ({status}) {when}: {reason}")
    }
}

/// How long the wrapper's log has gone without growing: an install of a
/// large profile takes minutes, and says how far it got as it goes.
struct Silence {
    len: u64,
    since: Instant,
}

impl Silence {
    fn new() -> Self {
        Self {
            len: 0,
            since: Instant::now(),
        }
    }

    fn of(&mut self, log: &Path) -> Duration {
        let len = std::fs::metadata(log).map_or(0, |m| m.len());
        if len != self.len {
            *self = Self {
                len,
                since: Instant::now(),
            };
        }
        self.since.elapsed()
    }
}

fn tail(path: &Path, n: usize) -> Vec<String> {
    let text = std::fs::read(path)
        .map(|b| String::from_utf8_lossy(&b).into_owned())
        .unwrap_or_default();
    let lines: Vec<String> = text
        .lines()
        .map(str::trim_end)
        .filter(|l| !l.is_empty())
        .map(str::to_owned)
        .collect();
    lines[lines.len().saturating_sub(n)..].to_vec()
}

fn spawn_logged(c: &mut Command, log: &Path) -> Result<Child> {
    if let Some(dir) = log.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let out = std::fs::File::create(log).with_context(|| log.display().to_string())?;
    start(c.stdin(Stdio::null()).stdout(out.try_clone()?).stderr(out))
}

/// Start `c`; a program Windows refused to start is an error that says what
/// blocked it.
fn start(c: &mut Command) -> Result<Child> {
    c.spawn()
        .map_err(|e| match Block::of(Path::new(c.get_program()), &e) {
            Some(b) => swofty_core::Error::Blocked(b).into(),
            None => e.into(),
        })
}

/// A program Windows refused to start: the log names it and the code, and
/// the launcher and Send Logs get it as the launch's failure.
fn note_blocked(store: &Store, e: &anyhow::Error) {
    let Some(swofty_core::Error::Blocked(b)) = e.downcast_ref::<swofty_core::Error>() else {
        return;
    };
    say(
        "warning",
        &format!(
            "Windows refused to start {} (os error {})",
            b.program.display(),
            b.code
        ),
    );
    if let Err(e) = launch::Failure::new(launch::Cause::Blocked(b.clone())).write(&store.root) {
        info(&format!("could not record it for the launcher: {e}"));
    }
}

const STEAMVR_START_TIMEOUT: Duration = Duration::from_secs(90);

/// Steam starts SteamVR before a `vr` launch option. Proton reads the VR
/// runtime when the game process starts, so a direct launch does the same.
fn ensure_steamvr() {
    if steamvr_running() {
        say("info", "VR: SteamVR is running");
        return;
    }
    if !steam::find_game().is_some_and(|g| steam::steamvr_installed(&g.steam_root)) {
        say(
            "info",
            "VR: SteamVR is not installed; F1 25 needs it to start in VR (install SteamVR from Steam)",
        );
        return;
    }
    say(
        "info",
        "VR: starting SteamVR first, as Steam does for its VR launch",
    );
    let asked = steam_command(&["-applaunch".to_owned(), steam::STEAMVR_APP_ID.to_string()])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn();
    if let Err(e) = asked {
        say("info", &format!("VR: could not ask Steam to start SteamVR: {e}"));
        return;
    }
    let started = Instant::now();
    while !steamvr_running() {
        if started.elapsed() > STEAMVR_START_TIMEOUT {
            say(
                "info",
                &format!(
                    "VR: SteamVR did not start within {} s; launching F1 25 anyway",
                    STEAMVR_START_TIMEOUT.as_secs()
                ),
            );
            return;
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    say("info", "VR: SteamVR is running");
}

#[derive(Debug, PartialEq)]
enum SteamPolicy {
    Start,
    Off,
    NotSteam,
    SteamSession,
}

fn steam_policy(on: bool, game: &Path, steam_session: bool) -> SteamPolicy {
    if !steam::in_steam_library(game) {
        SteamPolicy::NotSteam
    } else if steam_session {
        SteamPolicy::SteamSession
    } else if on {
        SteamPolicy::Start
    } else {
        SteamPolicy::Off
    }
}

/// The launcher's status line while the launch waits on Steam; gone once
/// the wait ends.
pub(crate) struct StageFile(pub(crate) PathBuf);

impl StageFile {
    pub(crate) fn set(&self, text: &str) {
        if let Some(dir) = self.0.parent() {
            std::fs::create_dir_all(dir).ok();
        }
        std::fs::write(&self.0, text).ok();
    }
}

impl Drop for StageFile {
    fn drop(&mut self) {
        std::fs::remove_file(&self.0).ok();
    }
}

fn steam_starter(game: Option<&Path>) -> Option<steam_client::Starter> {
    let root = steam::find_game().map(|g| g.steam_root);
    let hints: Vec<&Path> = root.as_deref().into_iter().chain(game).collect();
    steam_client::starter(&hints)
}

fn steam_command(args: &[String]) -> Command {
    steam_starter(None)
        .and_then(|s| s.command(args))
        .unwrap_or_else(|| {
            let mut c = Command::new("steam");
            c.args(args);
            c
        })
}

/// A Steam copy of the game needs the Steam client up and signed in; start
/// it when it is closed, as Steam's own Play button would.
fn ensure_steam(store: &Store, game: &Path, opted_out: bool) -> Result<()> {
    let on = store.config().launch.start_store && !opted_out;
    match steam_policy(on, game, steam_client::steam_session()) {
        SteamPolicy::NotSteam => say(
            "steam",
            "the game folder isn't in a Steam library; not checking Steam",
        ),
        SteamPolicy::SteamSession => say("steam", "Steam runs this session; not checking it"),
        SteamPolicy::Off => {
            let status = steam_client::status();
            say("steam", &format!("detected: {status}"));
            if !status.state.usable() {
                say(
                    "steam",
                    "starting Steam is turned off (launch.start_store or --no-start-store); \
                     open Steam if F1 25 doesn't start",
                );
            }
        }
        SteamPolicy::Start => bring_up_steam(store, Some(game))?,
    }
    Ok(())
}

fn bring_up_steam(store: &Store, game: Option<&Path>) -> Result<()> {
    let starter = steam_starter(game);
    let stage = StageFile(store.root.join(steam_client::STAGE_FILE));
    let ready = steam_client::ensure(
        steam_client::status,
        || {
            let starter = starter.as_ref().ok_or_else(|| {
                std::io::Error::new(
                    std::io::ErrorKind::NotFound,
                    "Steam was not found on this computer",
                )
            })?;
            say("steam", &format!("starting Steam: {starter}"));
            starter.start()
        },
        Wait::default(),
        |report| match report {
            Report::Detected(status) => say("steam", &format!("detected: {status}")),
            Report::Changed(status, after) if status.state == State::Ready => say(
                "steam",
                &format!("ready after {:.1} s ({})", after.as_secs_f64(), status.detail()),
            ),
            Report::Changed(status, after) => {
                say("steam", &format!("{status} at {:.1} s", after.as_secs_f64()))
            }
            Report::Stage(s) => stage.set(s.text()),
        },
    )?;
    if ready == Ready::Unconfirmed {
        say(
            "steam",
            &format!(
                "no sign-in seen after {} s; launching anyway",
                steam_client::READY_TIMEOUT.as_secs()
            ),
        );
    }
    Ok(())
}

/// `swofty steam`: what the launch sees, and with `start`, the same start
/// and wait as Play, without launching the game.
pub fn steam_check(store: &Store, start: bool) -> Result<ExitCode> {
    if start {
        bring_up_steam(store, None)?;
        return Ok(ExitCode::SUCCESS);
    }
    let status = steam_client::status();
    say("steam", &format!("detected: {status}"));
    if let Some(g) = swofty_core::steam::find_game() {
        say(
            "steam",
            &match swofty_core::steam::busy(&g) {
                Some(state) => format!("F1 25: Steam is {state} its files"),
                None => "F1 25: Steam isn't changing its files".to_owned(),
            },
        );
    }
    Ok(if status.state.usable() {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    })
}

fn steam_launch_args(mode: LaunchMode) -> Vec<String> {
    ["-applaunch".to_owned(), STEAM_APP_ID.to_string()]
        .into_iter()
        .chain(mode.args().iter().map(|a| (*a).to_owned()))
        .collect()
}

fn wrapper_command(proton: Option<&Path>, game: &Path, mode: LaunchMode) -> Vec<OsString> {
    let mut out: Vec<OsString> = vec!["--".into()];
    if let Some(p) = proton {
        out.push(p.join("proton").into());
        out.push("waitforexitandrun".into());
    }
    out.push(game.join(GAME_EXE).into());
    out.extend(mode.args().iter().map(OsString::from));
    out
}

fn launch(store: &Store, game: &Path, direct: bool, overlayfs: bool, mode: LaunchMode) -> Result<Wrapper> {
    let install = steam::find_game();
    let wrapped = install
        .as_ref()
        .filter(|g| swofty_core::copies::same_dir(&g.dir, game))
        .map(|g| {
            steam::launch_options(&g.steam_root)
                .iter()
                .any(|(_, o)| steam::wrapper_installed(o))
        })
        .unwrap_or(false);
    if crate::ea::store_start(Source::detect(game), direct) == crate::ea::StoreStart::EaApp {
        let mut c = Command::new(std::env::current_exe()?);
        c.arg("--data-dir")
            .arg(&store.root)
            .arg("run")
            .arg("--game")
            .arg(game)
            .arg("--via-ea")
            .args(wrapper_command(None, game, mode));
        #[cfg(unix)]
        std::os::unix::process::CommandExt::process_group(&mut c, 0);
        let log = store.root.join(WRAPPER_LOG);
        let child = spawn_logged(c.current_dir(game), &log)?;
        say(
            "info",
            &format!(
                "launching F1 25 through the EA app; waiting for the wrapper, output in {}",
                log.display()
            ),
        );
        return Ok(Wrapper {
            child: Some(child),
            log,
            timeout: DIRECT_START_TIMEOUT,
        });
    }
    if cfg!(windows) || direct || !wrapped {
        let me = std::env::current_exe()?;
        let mut c = Command::new(&me);
        c.arg("--data-dir")
            .arg(&store.root)
            .arg("run")
            .arg("--game")
            .arg(game);
        if overlayfs {
            c.arg("--use-overlayfs");
        }
        if let Some(g) = install.as_ref().filter(|_| !cfg!(windows)) {
            let tool = steam::compat_tool(&g.steam_root).unwrap_or_default();
            let proton = [
                g.steam_root.join("compatibilitytools.d").join(&tool),
                g.library.join("steamapps/common").join(&tool),
            ]
            .into_iter()
            .find(|p| p.join("proton").is_file())
            .context("could not find the Proton build Steam uses for F1 25")?;
            let runtime =
                [&g.library, &g.steam_root].map(|l| l.join("steamapps/common/SteamLinuxRuntime_sniper"));
            let runtime = runtime.iter().find(|p| p.is_dir()).unwrap_or(&runtime[0]);
            c.env(
                "STEAM_COMPAT_TOOL_PATHS",
                std::env::join_paths([&proton, runtime])?,
            )
            .env("STEAM_COMPAT_DATA_PATH", g.compatdata.clone().unwrap_or_default())
            .env("STEAM_COMPAT_CLIENT_INSTALL_PATH", &g.steam_root)
            .env("STEAM_COMPAT_INSTALL_PATH", game)
            .env("STEAM_COMPAT_LIBRARY_PATHS", g.library.join("steamapps"))
            .env("STEAM_COMPAT_APP_ID", STEAM_APP_ID.to_string())
            .env("SteamAppId", STEAM_APP_ID.to_string())
            .env("SteamGameId", STEAM_APP_ID.to_string());
            c.args(wrapper_command(Some(&proton), game, mode));
        } else {
            c.args(wrapper_command(None, game, mode));
        }
        if which("steam-run") {
            let mut fhs = Command::new("steam-run");
            fhs.arg(c.get_program())
                .args(c.get_args())
                .envs(c.get_envs().filter_map(|(k, v)| Some((k, v?))));
            c = fhs;
        }
        // The wrapper gets its own process group: a Ctrl-C in this terminal
        // is for this command, and must not kill the wrapper (and with it the
        // session's rollback) along with it.
        #[cfg(unix)]
        std::os::unix::process::CommandExt::process_group(&mut c, 0);
        let log = store.root.join(WRAPPER_LOG);
        let child = spawn_logged(c.current_dir(game), &log)?;
        say(
            "info",
            &format!(
                "launching F1 25 directly; waiting for the wrapper, output in {}",
                log.display()
            ),
        );
        Ok(Wrapper {
            child: Some(child),
            log,
            timeout: DIRECT_START_TIMEOUT,
        })
    } else {
        let mut ask = steam_command(&steam_launch_args(mode));
        #[cfg(unix)]
        std::os::unix::process::CommandExt::process_group(&mut ask, 0);
        ask.stdout(Stdio::null()).stderr(Stdio::null()).spawn()?;
        say(
            "info",
            if mode.is_vr() {
                "asked Steam to launch F1 25 in VR; waiting for the wrapper"
            } else {
                "asked Steam to launch F1 25; waiting for the wrapper"
            },
        );
        Ok(Wrapper {
            child: None,
            log: PathBuf::new(),
            timeout: STEAM_START_TIMEOUT,
        })
    }
}

struct Follower {
    path: PathBuf,
    fallback: Option<PathBuf>,
    pos: u64,
    buf: String,
    stale: Option<Option<LogMark>>,
}

impl Follower {
    fn new(path: PathBuf) -> Self {
        Self {
            path,
            fallback: None,
            pos: 0,
            buf: String::new(),
            stale: None,
        }
    }

    fn from_end(path: PathBuf) -> Self {
        let pos = std::fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
        Self {
            pos,
            ..Self::new(path)
        }
    }

    fn open(&self) -> Option<std::fs::File> {
        std::fs::File::open(&self.path)
            .ok()
            .or_else(|| std::fs::File::open(self.fallback.as_ref()?).ok())
    }

    fn replacing(path: PathBuf, before: Option<LogMark>) -> Self {
        Self {
            stale: Some(before),
            ..Self::new(path)
        }
    }

    fn lines(&mut self) -> Vec<String> {
        if let Some(before) = &self.stale {
            if !LogMark::replaced(&self.path, before.as_ref()) {
                return Vec::new();
            }
            self.stale = None;
        }
        let Some(mut f) = self.open() else {
            return Vec::new();
        };
        let size = f.metadata().map(|m| m.len()).unwrap_or(0);
        if size < self.pos {
            self.pos = 0;
        }
        let mut chunk = Vec::new();
        if f.seek(SeekFrom::Start(self.pos)).is_err() || f.read_to_end(&mut chunk).is_err() {
            return Vec::new();
        }
        self.pos += chunk.len() as u64;
        self.buf.push_str(&String::from_utf8_lossy(&chunk));
        let mut out: Vec<String> = self.buf.split('\n').map(str::to_owned).collect();
        self.buf = out.pop().unwrap_or_default();
        out
    }
}

struct Follow {
    dir: PathBuf,
    loader: Option<Follower>,
    wine: Follower,
    scan: LogScan,
    symbols: Symbols,
    last_loader: Option<String>,
}

impl Follow {
    fn new(dir: &Path, loader: Option<(PathBuf, Option<LogMark>)>, symbols: Symbols) -> Self {
        let loader = Some(match loader {
            Some((path, before)) => Follower {
                fallback: Some(dir.join("loader.log")),
                ..Follower::replacing(path, before)
            },
            None => Follower::new(dir.join("loader.log")),
        });
        let wine = Follower::new(dir.join(format!("steam-{STEAM_APP_ID}.log")));
        Self {
            dir: dir.into(),
            loader,
            wine,
            scan: LogScan::default(),
            symbols,
            last_loader: None,
        }
    }

    fn poll(&mut self) {
        for line in self.loader.as_mut().map(Follower::lines).unwrap_or_default() {
            if line.trim().is_empty() || line.contains("   section ") {
                continue;
            }
            let text = line.split_once("] ").map_or(line.as_str(), |(_, t)| t).to_owned();
            say(
                if text.starts_with("[core]") {
                    "core"
                } else {
                    "loader"
                },
                &text,
            );
            self.last_loader = Some(text);
        }
        for line in self.wine.lines() {
            match self.scan.feed(&line) {
                Some(Event::Fatal {
                    code,
                    addr,
                    first: true,
                }) => {
                    let name = fatal_name(code).unwrap_or("?");
                    say(
                        "crash",
                        &format!(
                            "{name} ({code:08x}) at {}",
                            self.scan.describe(addr, &self.symbols)
                        ),
                    );
                }
                Some(Event::Crash(text)) => say("crash", &text),
                Some(Event::Error(text)) => say("wine", &text.chars().take(240).collect::<String>()),
                _ => {}
            }
        }
    }

    fn summary(&self) -> Result<()> {
        if let Some(failure) = self.scan.start_failure {
            println!();
            say("info", &format!("logs in {}", self.dir.display()));
            bail!("{failure}");
        }
        let read = |f: &str| {
            std::fs::read_to_string(self.dir.join(f))
                .ok()
                .map(|s| s.trim().to_owned())
        };
        let code = read("exit_code").unwrap_or_else(|| "?".into());
        let secs = |f| read(f).and_then(|s| s.parse::<f64>().ok());
        let dur = match (secs("game_start"), secs("game_end")) {
            (Some(a), Some(b)) => format!("{:.1}s", b - a),
            _ => "?".into(),
        };
        let end = SessionRecord::read(&self.dir)
            .end
            .map(|e| format!(" ({})", e.end.as_str()))
            .unwrap_or_default();
        println!();
        say(
            "info",
            &format!(
                "game exited with code {code} after {dur}{end}; logs in {}",
                self.dir.display()
            ),
        );
        match &self.last_loader {
            Some(l) => say("info", &format!("last loader line: {l}")),
            None => {
                // The follower waits for the loader's rotation at attach, so
                // when the loader never attaches the session launcher's own
                // lines sit unnoticed in the log. Report them directly.
                let stub: Vec<String> = self
                    .loader
                    .as_ref()
                    .and_then(|f| {
                        let mut text = String::new();
                        f.open()?.read_to_string(&mut text).ok()?;
                        Some(text)
                    })
                    .map(|t| {
                        t.lines()
                            .filter(|l| l.contains("[offline]"))
                            .map(str::to_owned)
                            .collect()
                    })
                    .unwrap_or_default();
                if stub.is_empty() {
                    say(
                        "info",
                        "no loader log for this session: the loader never attached",
                    );
                } else {
                    say("info", "the loader never attached; the session launcher reports:");
                    for l in stub.iter().rev().take(8).rev() {
                        say("loader", l);
                    }
                }
            }
        }
        for (i, line) in self.scan.summary(&self.symbols).into_iter().enumerate() {
            say(
                if self.scan.exceptions.is_empty() && i == 0 {
                    "info"
                } else {
                    "crash"
                },
                &line,
            );
        }
        if !self.scan.errors.is_empty() {
            let total: usize = self.scan.errors.values().sum();
            say(
                "info",
                &format!(
                    "{} distinct wine err: lines ({total} total)",
                    self.scan.errors.len()
                ),
            );
        }
        Ok(())
    }
}

fn symbols(game: &Path, exe: Option<&Path>) -> Symbols {
    let mut s = Symbols::parse_kb(KB);
    let exe = exe.map(Path::to_path_buf).unwrap_or_else(|| game.join(GAME_EXE));
    s.load_pdata(&exe).ok();
    s
}

pub(crate) fn say(kind: &str, text: &str) {
    let colour = match kind {
        "loader" => "36",
        "core" => "32",
        "wine" => "33",
        "crash" => "31;1",
        _ => "90",
    };
    if std::io::stdout().is_terminal() {
        println!("\x1b[{colour}m[{kind}]\x1b[0m {text}");
    } else {
        println!("[{kind}] {text}");
    }
}

fn which(bin: &str) -> bool {
    std::env::var_os("PATH").is_some_and(|p| std::env::split_paths(&p).any(|d| d.join(bin).is_file()))
}

fn shell_join(args: &[String]) -> String {
    args.iter()
        .map(|a| {
            if a.chars()
                .all(|c| c.is_ascii_alphanumeric() || "-_./=:,+%@".contains(c))
            {
                a.clone()
            } else {
                format!("'{}'", a.replace('\'', "'\\''"))
            }
        })
        .collect::<Vec<_>>()
        .join(" ")
}

fn write_json(path: &Path, value: &impl serde::Serialize) -> Result<()> {
    Ok(swofty_core::write_json(path, value)?)
}

fn install_ctrl_c() {
    #[cfg(target_os = "linux")]
    unsafe {
        extern "C" fn on_int(_: libc::c_int) {
            STOP.store(true, std::sync::atomic::Ordering::Relaxed);
        }
        libc::signal(libc::SIGINT, on_int as *const () as libc::sighandler_t);
    }
}

static STOP: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

fn stop_requested() -> bool {
    STOP.swap(false, std::sync::atomic::Ordering::Relaxed)
}

fn end_on_signals() {
    #[cfg(target_os = "linux")]
    unsafe {
        extern "C" fn on_end(_: libc::c_int) {
            ENDING.store(true, std::sync::atomic::Ordering::Relaxed);
        }
        for sig in [libc::SIGINT, libc::SIGTERM, libc::SIGHUP] {
            libc::signal(sig, on_end as *const () as libc::sighandler_t);
        }
    }
}

static ENDING: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

pub(crate) fn ending() -> bool {
    ENDING.load(std::sync::atomic::Ordering::Relaxed)
}

#[cfg(target_os = "linux")]
fn set_affinity(cpus: &[usize]) {
    unsafe {
        let mut set: libc::cpu_set_t = std::mem::zeroed();
        for &c in cpus {
            libc::CPU_SET(c, &mut set);
        }
        libc::sched_setaffinity(0, std::mem::size_of::<libc::cpu_set_t>(), &set);
    }
}

#[cfg(not(target_os = "linux"))]
fn set_affinity(_: &[usize]) {}

#[cfg(target_os = "linux")]
fn become_subreaper() {
    unsafe {
        libc::prctl(libc::PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
    }
}

#[cfg(not(target_os = "linux"))]
fn become_subreaper() {}

#[cfg(unix)]
fn link(target: &Path, at: &Path) {
    std::os::unix::fs::symlink(target, at).ok();
}

#[cfg(not(unix))]
fn link(_: &Path, _: &Path) {}

#[cfg(all(test, unix))]
mod tests {
    use super::*;

    fn temp_log(name: &str) -> PathBuf {
        std::env::temp_dir()
            .join(format!("swofty-run-{}-{name}", std::process::id()))
            .join(WRAPPER_LOG)
    }

    #[test]
    fn reports_a_wrapper_that_exits_before_the_game_starts() {
        let log = temp_log("early");
        let child = spawn_logged(
            Command::new("sh").args([
                "-c",
                "echo '[info] installing profile Default'; \
                 echo 'swofty: version.dll already exists in the game folder; run swofty restore' >&2; exit 3",
            ]),
            &log,
        )
        .unwrap();
        let mut w = Wrapper {
            child: Some(child),
            log: log.clone(),
            timeout: DIRECT_START_TIMEOUT,
        };
        let status = loop {
            if let Some(s) = w.exited() {
                break s;
            }
            std::thread::sleep(Duration::from_millis(10));
        };
        assert_eq!(status.code(), Some(3));
        assert_eq!(
            w.failure(status, "before the game started").to_string(),
            "the wrapper exited (exit status: 3) before the game started: \
             version.dll already exists in the game folder; run swofty restore"
        );
        std::fs::remove_dir_all(log.ancestors().nth(2).unwrap()).ok();
    }

    #[test]
    fn an_error_over_several_lines_leads_with_what_failed() {
        let log = temp_log("lines");
        let child = spawn_logged(
            Command::new("sh").args([
                "-c",
                "echo 'downloading offline play 0.3.11 from https://x'; \
                 echo '[info] rolled the profile'\"'\"'s game files back'; \
                 echo 'swofty: downloading the offline play extension: offline play 0.3.11: the download was \
                 blocked on this PC or network, typically by antivirus HTTPS or web scanning, a VPN or proxy, a \
                 network filter or a Wi-Fi login page; turning that off or trying another network fixes it' >&2; \
                 echo '(could not download https://x/v0.3.11/f1op_hook.dll: io: received corrupt message of type \
                 InvalidContentType)' >&2; \
                 exit 1",
            ]),
            &log,
        )
        .unwrap();
        let mut w = Wrapper {
            child: Some(child),
            log: log.clone(),
            timeout: DIRECT_START_TIMEOUT,
        };
        let status = loop {
            if let Some(s) = w.exited() {
                break s;
            }
            std::thread::sleep(Duration::from_millis(10));
        };
        assert_eq!(
            w.failure(status, "before the game started").to_string(),
            "the wrapper exited (exit status: 1) before the game started: downloading the offline play extension: \
             offline play 0.3.11: the download was blocked on this PC or network, typically by antivirus HTTPS or \
             web scanning, a VPN or proxy, a network filter or a Wi-Fi login page; turning that off or trying \
             another network fixes it\n\
             (could not download https://x/v0.3.11/f1op_hook.dll: io: received corrupt message of type \
             InvalidContentType)"
        );
        std::fs::remove_dir_all(log.ancestors().nth(2).unwrap()).ok();
    }

    #[test]
    fn a_blocked_start_is_recorded_for_the_launcher() {
        let root = std::env::temp_dir().join(format!("swofty-run-{}-blocked", std::process::id()));
        let store = Store::new(&root);
        let missing = start(&mut Command::new(root.join("no-such-program"))).unwrap_err();
        assert!(missing.downcast_ref::<std::io::Error>().is_some(), "{missing}");
        note_blocked(&store, &missing);
        assert_eq!(launch::Failure::read(&root), None);
        let block = Block::from_code(&root.join("f1op-probe.exe"), 4551).unwrap();
        let blocked = anyhow::Error::from(swofty_core::Error::Blocked(block.clone()));
        assert_eq!(blocked.to_string(), block.text());
        note_blocked(&store, &blocked.context("staging offline play"));
        assert_eq!(
            launch::Failure::read(&root).map(|f| f.cause),
            Some(launch::Cause::Blocked(block))
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn names_the_log_when_the_wrapper_says_nothing() {
        let log = temp_log("silent");
        let child = spawn_logged(Command::new("sh").args(["-c", "exit 1"]), &log).unwrap();
        let mut w = Wrapper {
            child: Some(child),
            log: log.clone(),
            timeout: DIRECT_START_TIMEOUT,
        };
        let status = loop {
            if let Some(s) = w.exited() {
                break s;
            }
            std::thread::sleep(Duration::from_millis(10));
        };
        let msg = w.failure(status, "before the game started").to_string();
        assert!(
            msg.ends_with(&format!("without output; see {}", log.display())),
            "{msg}"
        );
        std::fs::remove_dir_all(log.ancestors().nth(2).unwrap()).ok();
    }

    const STALE: &str = "2026-10-17 21:44:08.228 [  416] swofty loader (version.dll proxy) attached, pid 412\n\
                         2026-10-17 21:45:23.672 [  620] [game] (error) from the previous run\n";

    fn session_dir(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-run-{}-{name}", std::process::id()));
        std::fs::create_dir_all(dir.join("game/swofty")).unwrap();
        dir
    }

    #[test]
    fn ignores_a_stale_loader_log_and_follows_its_replacement() {
        let dir = session_dir("rotate");
        let log = dir.join("game").join(LOADER_LOG);
        std::fs::write(&log, STALE).unwrap();
        let mut follower = Follower::replacing(log.clone(), LogMark::read(&log));
        assert!(follower.lines().is_empty());
        assert!(follower.lines().is_empty());
        std::fs::rename(&log, log.with_file_name("loader.prev.log")).unwrap();
        std::fs::write(
            &log,
            "2026-10-17 22:12:08.004 [  420] swofty loader (version.dll proxy) attached, pid 416\npar",
        )
        .unwrap();
        assert_eq!(
            follower.lines(),
            ["2026-10-17 22:12:08.004 [  420] swofty loader (version.dll proxy) attached, pid 416"]
        );
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn reports_a_game_process_proton_could_not_start() {
        let dir = session_dir("run-process");
        let log = dir.join("game").join(LOADER_LOG);
        std::fs::write(&log, STALE).unwrap();
        let before = LogMark::read(&log);
        std::fs::write(
            dir.join(format!("steam-{STEAM_APP_ID}.log")),
            include_str!("../../../packages/core/tests/fixtures/steam-run-process-731.txt"),
        )
        .unwrap();
        std::fs::write(dir.join("exit_code"), "1\n").unwrap();
        let mut follow = Follow::new(&dir, Some((log, before)), Symbols::default());
        follow.poll();
        assert_eq!(follow.last_loader, None);
        let err = follow.summary().unwrap_err().to_string();
        assert!(
            err.starts_with("Proton could not start the game process (CreateProcess error 731). "),
            "{err}"
        );
        assert!(err.ends_with("Nothing from SwoftyMod ran."), "{err}");
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn keeps_the_loader_and_perf_logs_this_session_wrote() {
        let dir = session_dir("keep-logs");
        let game = dir.join("game");
        let session = dir.join("session");
        std::fs::create_dir_all(&session).unwrap();
        let loader = game.join(LOADER_LOG);
        let perf = game.join(PERF_LOG);
        let touch = |path: &Path, at: SystemTime| {
            std::fs::File::options()
                .write(true)
                .open(path)
                .unwrap()
                .set_modified(at)
                .unwrap();
        };
        let launched = SystemTime::now();
        std::fs::write(&loader, STALE).unwrap();
        std::fs::write(&perf, "Swofty Launcher performance report\nold\n").unwrap();
        touch(&perf, launched - Duration::from_secs(600));
        let before = LogMark::read(&loader);
        let fallback = dir.join(LOADER_FALLBACK);
        keep_game_logs(&game, &fallback, &session, before.as_ref(), launched);
        assert!(!session.join("loader.log").exists());
        assert!(!session.join("perf.log").exists());

        std::fs::create_dir_all(fallback.parent().unwrap()).unwrap();
        std::fs::write(&fallback, "attached in the data folder\n").unwrap();
        keep_game_logs(&game, &fallback, &session, before.as_ref(), launched);
        assert_eq!(
            std::fs::read_to_string(session.join("loader.log")).unwrap(),
            "attached in the data folder\n"
        );

        std::fs::write(&loader, "fresh\n").unwrap();
        std::fs::write(&perf, "Swofty Launcher performance report\nnew\n").unwrap();
        touch(&perf, launched + Duration::from_secs(1));
        keep_game_logs(&game, &fallback, &session, before.as_ref(), launched);
        assert_eq!(
            std::fs::read_to_string(session.join("loader.log")).unwrap(),
            "fresh\n"
        );
        assert_eq!(
            std::fs::read_to_string(session.join("perf.log")).unwrap(),
            "Swofty Launcher performance report\nnew\n"
        );
        std::fs::remove_dir_all(&dir).ok();
    }

    fn strings(args: &[OsString]) -> Vec<String> {
        args.iter().map(|a| a.to_string_lossy().into_owned()).collect()
    }

    #[test]
    fn a_vr_launch_reaches_the_game_on_every_path() {
        let game = Path::new("/games/F1 25");
        let proton = Path::new("/steam/compatibilitytools.d/GE");
        let direct = strings(&wrapper_command(Some(proton), game, LaunchMode::Vr));
        assert_eq!(
            direct,
            [
                "--",
                "/steam/compatibilitytools.d/GE/proton",
                "waitforexitandrun",
                "/games/F1 25/F1_25.exe",
                "-vr"
            ]
        );
        let seen = parse_steam_command(&direct[1..]);
        assert_eq!(seen.game_args(), ["-vr"]);
        assert_eq!(LaunchMode::of(seen.game_args()), LaunchMode::Vr);
        assert_eq!(game_args(seen.game_args(), LaunchMode::Vr), ["-vr"]);

        let flat = strings(&wrapper_command(Some(proton), game, LaunchMode::Normal));
        assert!(parse_steam_command(&flat[1..]).game_args().is_empty());

        let native = strings(&wrapper_command(None, game, LaunchMode::Vr));
        assert_eq!(native, ["--", "/games/F1 25/F1_25.exe", "-vr"]);
        assert_eq!(parse_steam_command(&native[1..]).game_args(), ["-vr"]);

        assert_eq!(steam_launch_args(LaunchMode::Normal), ["-applaunch", "3059520"]);
        assert_eq!(
            steam_launch_args(LaunchMode::Vr),
            ["-applaunch", "3059520", "-vr"]
        );
    }

    #[test]
    fn starts_steam_only_for_a_steam_copy_with_the_setting_on() {
        let root = std::env::temp_dir().join(format!("swofty-run-{}-policy", std::process::id()));
        let game = root.join("lib/steamapps/common/F1 25");
        let other = root.join("Games/F1 25");
        std::fs::create_dir_all(&game).unwrap();
        std::fs::create_dir_all(&other).unwrap();
        std::fs::write(
            root.join(format!("lib/steamapps/appmanifest_{STEAM_APP_ID}.acf")),
            r#""AppState" { "installdir" "F1 25" }"#,
        )
        .unwrap();
        assert_eq!(steam_policy(true, &game, false), SteamPolicy::Start);
        assert_eq!(steam_policy(false, &game, false), SteamPolicy::Off);
        assert_eq!(steam_policy(true, &game, true), SteamPolicy::SteamSession);
        assert_eq!(steam_policy(true, &other, false), SteamPolicy::NotSteam);
        assert_eq!(steam_policy(false, &other, true), SteamPolicy::NotSteam);
        let stage = StageFile(root.join("data").join(steam_client::STAGE_FILE));
        stage.set(steam_client::Stage::SigningIn.text());
        assert_eq!(
            std::fs::read_to_string(&stage.0).unwrap(),
            "Waiting for Steam to sign in…"
        );
        let path = stage.0.clone();
        drop(stage);
        assert!(!path.exists());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn opting_out_is_a_flag() {
        use clap::Parser;
        #[derive(Parser)]
        struct Cli {
            #[command(flatten)]
            run: RunArgs,
        }
        assert!(!Cli::parse_from(["swofty"]).run.no_start_store);
        assert!(Cli::parse_from(["swofty", "--no-start-steam"]).run.no_start_store);
        assert!(Cli::parse_from(["swofty", "--no-start-store"]).run.no_start_store);
    }

    #[cfg(unix)]
    #[test]
    fn a_game_ended_by_a_signal_has_no_exit_code_and_says_why() {
        let status = Command::new("sh").args(["-c", "kill -9 $$"]).status().unwrap();
        assert_eq!(status.code(), None);
        let end = SessionEnd::resolve(status.code(), &why_no_code(status), None, None);
        assert_eq!(
            (end.end, end.exit_code),
            (swofty_core::session::Ending::Unknown, None)
        );
        assert_eq!(
            end.reason,
            "the game's process was ended by signal 9, which leaves no exit code"
        );
    }

    #[test]
    fn the_wait_for_the_wrapper_counts_from_its_last_output() {
        let log = temp_log("silence");
        std::fs::create_dir_all(log.parent().unwrap()).unwrap();
        let mut silence = Silence::new();
        std::thread::sleep(Duration::from_millis(30));
        assert!(silence.of(&log) >= Duration::from_millis(30));
        std::fs::write(&log, "[info] installing: 2000 of 7977 file(s) copied\n").unwrap();
        assert!(silence.of(&log) < Duration::from_millis(30));
        std::thread::sleep(Duration::from_millis(30));
        assert!(silence.of(&log) >= Duration::from_millis(30));
        std::fs::remove_dir_all(log.ancestors().nth(2).unwrap()).ok();
    }

    #[test]
    fn keeps_the_last_lines() {
        let log = temp_log("tail");
        std::fs::create_dir_all(log.parent().unwrap()).unwrap();
        std::fs::write(&log, "a\n\nb\r\nc\n").unwrap();
        assert_eq!(tail(&log, 2), ["b", "c"]);
        assert_eq!(tail(&log, 9), ["a", "b", "c"]);
        assert!(tail(&log.with_extension("missing"), 3).is_empty());
        std::fs::remove_dir_all(log.ancestors().nth(2).unwrap()).ok();
    }

    struct Modded {
        root: PathBuf,
        store: Store,
        game: Game,
    }

    impl Modded {
        fn new(name: &str) -> Self {
            let root = std::env::temp_dir().join(format!("swofty-run-{}-{name}", std::process::id()));
            std::fs::remove_dir_all(&root).ok();
            let dir = root.join("game");
            std::fs::create_dir_all(dir.join("game_package")).unwrap();
            std::fs::write(dir.join(GAME_EXE), "exe").unwrap();
            std::fs::write(dir.join("game_package/db.bin"), "db").unwrap();
            std::fs::create_dir_all(root.join("src")).unwrap();
            std::fs::write(root.join("src/db"), "modded").unwrap();
            std::fs::write(root.join("src/core"), "core").unwrap();
            let game = Game {
                identity: swofty_core::baseline::Identity::detect(&dir, Some(1)).unwrap(),
                dir,
                user_reg: None,
            };
            let store = Store::new(root.join("data"));
            let plan = swofty_core::install::Plan {
                profile: "MODDED".into(),
                data: [("game_package/db.bin".to_owned(), root.join("src/db"))].into(),
                loader: [(
                    "swofty/addons/core/swofty_core.dll".to_owned(),
                    root.join("src/core"),
                )]
                .into(),
                ..Default::default()
            };
            Installer {
                session: true,
                ..Installer::new(&store.root, &game)
            }
            .apply(&plan)
            .unwrap();
            std::fs::write(game.dir.join("swofty/loader.log"), "attached\n").unwrap();
            Self { root, store, game }
        }

        fn vanilla(&self) -> bool {
            std::fs::read_to_string(self.game.dir.join("game_package/db.bin")).unwrap() == "db"
                && !self.game.dir.join("swofty").exists()
                && swofty_core::journal::Journal::open(&self.store.root)
                    .unwrap()
                    .is_empty()
                && Installer::new(&self.store.root, &self.game)
                    .verify()
                    .unwrap()
                    .is_empty()
        }
    }

    impl Drop for Modded {
        fn drop(&mut self) {
            std::fs::remove_dir_all(&self.root).ok();
        }
    }

    #[test]
    fn refuses_to_start_the_game_when_an_antivirus_took_a_placed_dll() {
        use clap::Parser;
        #[derive(Parser)]
        struct Cli {
            #[command(flatten)]
            run: RunArgs,
        }
        let m = Modded::new("refused");
        let started = m.root.join("game-started");
        let exe = m.game.dir.join(GAME_EXE);
        std::fs::write(&exe, format!("#!/bin/sh\ntouch '{}'\n", started.display())).unwrap();
        std::fs::set_permissions(&exe, std::os::unix::fs::PermissionsExt::from_mode(0o755)).unwrap();
        let written = Some(Instant::now() - quarantine::RECHECK_AFTER);
        assert_eq!(check_loader(&m.store, &m.game.dir, written), Ok(false));
        assert_eq!(quarantine::Report::read(&m.store.root), None);
        let core = "swofty/addons/core/swofty_core.dll";
        std::fs::remove_file(m.game.dir.join(core)).unwrap();
        let session = Session {
            store: &m.store,
            game: &m.game,
            request_id: "refused".into(),
            _lock: SessionLock::try_acquire(&m.store.root).unwrap().unwrap(),
            copy: true,
            overlay: None,
        };
        let args = Cli::parse_from(["swofty"]).run;
        let request = LaunchRequest::default();
        let result = run_loop(
            &m.store,
            &args,
            &Config::default(),
            &request,
            "MODDED",
            &m.game,
            Mode::Copy { installed: written },
        );
        assert_eq!(
            finish(session, result).unwrap(),
            ExitCode::from(quarantine::REFUSED_EXIT)
        );
        assert!(!started.exists(), "the game was started");
        assert!(m.store.latest_session().is_none());
        assert!(m.vanilla());
        let report = quarantine::Report::read(&m.store.root).unwrap();
        assert_eq!(report.stage, Stage::BeforeLaunch);
        assert_eq!(report.files, [core]);
        assert_eq!(report.game, m.game.dir);
        assert_eq!(refused(&m.store, report.at - 1.0), Some(report.clone()));
        assert_eq!(refused(&m.store, report.at + 1.0), None);
        loader_missed(&m.store, &m.game.dir, Some(quarantine::Cause::Defender));
        let report = quarantine::Report::read(&m.store.root).unwrap();
        assert!(report.stage == Stage::InGame && report.removed);
        assert_eq!(report.cause, None);
        assert_eq!(refused(&m.store, 0.0), None);
        std::fs::write(m.game.dir.join(PROXY), "proxy").unwrap();
        loader_missed(&m.store, &m.game.dir, Some(quarantine::Cause::Defender));
        let report = quarantine::Report::read(&m.store.root).unwrap();
        assert!(!report.removed && report.cause.as_deref() == Some(&quarantine::Cause::Defender));
        assert!(
            report
                .text()
                .starts_with("F1 25 is running without mods: Microsoft Defender blocked version.dll."),
            "{}",
            report.text()
        );
    }

    #[test]
    fn rolls_back_a_session_whose_wrapper_was_killed_after_the_game_exited() {
        let m = Modded::new("killed");
        assert!(!m.vanilla());
        let log = m.store.root.join(WRAPPER_LOG);
        let child = spawn_logged(
            Command::new("sh").args(["-c", "echo '[info] in game: nothing changed'; kill -9 $$"]),
            &log,
        )
        .unwrap();
        let mut w = Wrapper {
            child: Some(child),
            log: log.clone(),
            timeout: DIRECT_START_TIMEOUT,
        };
        let mut relay = Follower::new(log);
        settle(&m.store, &m.game, &mut w, Some(&mut relay), true);
        assert!(m.vanilla());
        assert_eq!(
            std::fs::read_to_string(m.store.root.join("sessions/orphaned-logs/loader.log")).unwrap(),
            "attached\n"
        );
        let notice = swofty_core::recovery::Notice::read(&m.store.root).unwrap();
        assert!(notice.lines[0].ends_with("(5 change(s) for profile MODDED) that a session left open"));
    }

    #[test]
    fn waits_for_a_steam_wrapper_to_finish_rolling_back_before_recovering() {
        if !which("flock") {
            return;
        }
        let m = Modded::new("steam");
        let lock = swofty_core::recovery::lock_path(&m.store.root);
        let mut holder = Command::new("flock")
            .arg(&lock)
            .args(["sleep", "1"])
            .spawn()
            .unwrap();
        let started = Instant::now();
        while !SessionLock::held_elsewhere(&m.store.root) {
            assert!(started.elapsed() < Duration::from_secs(5));
            std::thread::sleep(Duration::from_millis(20));
        }
        let mut w = Wrapper {
            child: None,
            log: PathBuf::new(),
            timeout: STEAM_START_TIMEOUT,
        };
        settle(&m.store, &m.game, &mut w, None, true);
        assert!(
            holder.try_wait().unwrap().is_some(),
            "settle ran under a live session"
        );
        holder.wait().unwrap();
        assert!(m.vanilla());
    }

    #[test]
    fn relays_the_wrappers_lines_under_their_own_tags() {
        let log = temp_log("relay");
        std::fs::create_dir_all(log.parent().unwrap()).unwrap();
        std::fs::write(&log, "old\n").unwrap();
        let mut relay = Follower::from_end(log.clone());
        std::fs::write(
            &log,
            "old\n[info] rolled the profile's game files back\nswofty: oops\n",
        )
        .unwrap();
        assert_eq!(
            relay.lines(),
            ["[info] rolled the profile's game files back", "swofty: oops"]
        );
        std::fs::remove_dir_all(log.ancestors().nth(2).unwrap()).ok();
    }
}
