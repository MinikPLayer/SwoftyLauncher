//! `swofty test`: plan and run addon tests. A run takes the game lock,
//! builds a throwaway profile per batch, starts the game through
//! `swofty run --apply --direct`, waits for the environment, runs the steps
//! through the in-game test agent's mailbox, checks the results and the
//! session's logs and writes the reports. `swofty test shell` starts the
//! game in test mode and sends the agent the commands a person types.

use std::collections::{BTreeMap, BTreeSet};
use std::io::BufRead;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitCode, Stdio};
use std::time::{Duration, Instant, SystemTime};

use anyhow::{Result, bail};
use clap::{Args, Subcommand};
use serde_json::{Value, json};
use swofty_core::addon_storage;
use swofty_core::builtin::CORE_ID;
use swofty_core::install::Game;
use swofty_core::journal::Journal;
use swofty_core::process::{descends_from, game_processes, games_in, stop_process};
use swofty_core::recovery::SessionLock;
use swofty_core::session::{LOADER_LOG, LogMark, SessionRecord, request_stop};
use swofty_core::store::Store;
use swofty_core::testsuite::agent::{self, Client, Outcome};
use swofty_core::testsuite::bench;
use swofty_core::testsuite::career;
use swofty_core::testsuite::checks::{self, Evidence, Failure};
use swofty_core::testsuite::fixture::{self, Fixture};
use swofty_core::testsuite::lock::{self, GameLock};
use swofty_core::testsuite::plan::{self, Batch, Plan};
use swofty_core::testsuite::report::{self, BatchReport, Report, Status, StepReport, TestReport, exit};
use swofty_core::testsuite::sandbox::{self, RunDir, RunOptions};
use swofty_core::testsuite::suite::{self, Env, Kind, Selector, Suite, Test};

use crate::time::{now_secs, stamp};

#[derive(Subcommand)]
pub enum TestCommand {
    #[command(
        about = "Show the tests a run would start, grouped by environment, and how many game starts it takes"
    )]
    Plan(PlanArgs),
    #[command(
        about = "Start F1 25 in each test's environment, run its steps, check what happened and write the report"
    )]
    Run(RunArgs),
    #[command(
        about = "Start F1 25 in test mode and send the test agent the commands you type (route, read, wait, pad, capture, career load, quit...)"
    )]
    Shell(ShellArgs),
    #[command(
        subcommand,
        about = "Capture, list and check fixtures: saved games in a known state"
    )]
    Fixture(FixtureCommand),
}

#[derive(Args)]
pub struct ShellArgs {
    #[arg(
        long = "addon",
        value_name = "ID",
        help = "An installed addon to switch on in the shell's profile (repeatable)"
    )]
    addons: Vec<String>,
    #[arg(
        long,
        help = "Close a running F1 25 first, by its exact process id, and wait for its game files to be put back"
    )]
    take_over: bool,
    #[arg(
        long,
        value_name = "TIME",
        default_value = "30m",
        value_parser = parse_wait,
        help = "How long to wait for the game to be free: 0, 90s, 30m, 2h"
    )]
    wait: Duration,
    #[arg(long, help = crate::GAME_HELP)]
    game: Option<PathBuf>,
    #[arg(
        long,
        value_name = "DIR",
        help = "The machine's game lock folder (as for swofty test run)"
    )]
    lock: Option<PathBuf>,
    #[arg(
        long,
        value_name = "FIXTURE",
        help = "Start in the save sandbox with this fixture's saves (a fixture folder or a name in the fixture store), so career load, advance and save now work"
    )]
    fixture: Option<String>,
}

#[derive(Subcommand)]
pub enum FixtureCommand {
    #[command(
        about = "Copy a test run's sandbox saves and addon storage into a new fixture in the fixture store"
    )]
    Capture(CaptureArgs),
    #[command(about = "List the fixtures in the fixture store and check their files")]
    List(StoreArgs),
    #[command(about = "Check a fixture's files against its fixture.json")]
    Check(CheckArgs),
}

#[derive(Args)]
pub struct StoreArgs {
    #[arg(
        long,
        value_name = "DIR",
        help = "The fixture store (default: $SWOFTY_TEST_FIXTURES, else test-fixtures in the data folder)"
    )]
    store: Option<PathBuf>,
}

#[derive(Args)]
pub struct CaptureArgs {
    #[arg(help = "The new fixture's name (letters, digits, - _ .)")]
    name: String,
    #[arg(
        long,
        value_name = "RUN",
        conflicts_with = "from",
        help = "The test run whose sandbox to capture, as named under test-runs/ (default: the newest)"
    )]
    run: Option<String>,
    #[arg(
        long,
        value_name = "DIR",
        help = "Capture a save folder instead of a test run: a Steam remote/ folder or the userdata/<app> folder holding \
                one, such as a copy of a backup. Never the live folder"
    )]
    from: Option<PathBuf>,
    #[arg(long, help = "What the fixture holds, in a sentence")]
    description: Option<String>,
    #[arg(
        long = "addon",
        value_name = "ID=VERSION",
        help = "An addon the fixture's careers need (repeatable; a run's addons are recorded on their own)"
    )]
    addons: Vec<String>,
    #[arg(
        long = "career",
        value_name = "MODE[:SLOT]",
        help = "A career the fixture holds, by its game mode and save slot (repeatable)"
    )]
    careers: Vec<String>,
    #[arg(long, help = "Free text for fixture.json's holds.notes")]
    notes: Option<String>,
    #[command(flatten)]
    store: StoreArgs,
}

#[derive(Args)]
pub struct CheckArgs {
    #[arg(help = "A fixture name in the store or a fixture folder")]
    fixture: String,
    #[command(flatten)]
    store: StoreArgs,
}

#[derive(Args)]
pub struct Select {
    #[arg(
        required = true,
        help = "An addon folder, its tests folder or a .test.json file"
    )]
    paths: Vec<PathBuf>,
    #[arg(
        short = 't',
        long = "tag",
        value_name = "TAG",
        help = "Only tests with this tag (repeatable)"
    )]
    tags: Vec<String>,
    #[arg(
        long = "id",
        value_name = "ID",
        help = "Only this test, by id or suite/id (repeatable)"
    )]
    ids: Vec<String>,
}

#[derive(Args)]
pub struct PlanArgs {
    #[command(flatten)]
    select: Select,
    #[arg(long, help = "Print the plan as JSON")]
    json: bool,
}

#[derive(Args)]
pub struct RunArgs {
    #[command(flatten)]
    select: Select,
    #[arg(
        long,
        help = "Folder for the report (default: swofty-test/<run> in this folder)"
    )]
    out: Option<PathBuf>,
    #[arg(
        long,
        help = "Close a running F1 25 first, by its exact process id, and wait for its game files to be put back"
    )]
    take_over: bool,
    #[arg(
        long,
        value_name = "TIME",
        default_value = "30m",
        value_parser = parse_wait,
        help = "How long to wait for the game to be free: 0, 90s, 30m, 2h"
    )]
    wait: Duration,
    #[arg(long, help = crate::GAME_HELP)]
    game: Option<PathBuf>,
    #[arg(
        long,
        value_name = "DIR",
        help = "The machine's game lock folder (default: $SWOFTY_GAME_LOCK, else /mnt/work/.f1-game.lock.d where /mnt/work exists, else swofty-game.lock.d next to the data folder)"
    )]
    lock: Option<PathBuf>,
    #[arg(long, help = "Print report.json instead of the summary")]
    json: bool,
}

fn parse_wait(s: &str) -> std::result::Result<Duration, String> {
    let s = s.trim();
    let (num, unit) = match s.find(|c: char| !c.is_ascii_digit()) {
        Some(at) => s.split_at(at),
        None => (s, "s"),
    };
    let n: u64 = num
        .parse()
        .map_err(|_| format!("{s}: expected a time such as 0, 90s, 30m or 2h"))?;
    let secs = match unit {
        "s" => n,
        "m" => n * 60,
        "h" => n * 3600,
        _ => return Err(format!("{s}: expected a time such as 0, 90s, 30m or 2h")),
    };
    Ok(Duration::from_secs(secs))
}

pub fn command(store: &Store, cmd: TestCommand) -> Result<ExitCode> {
    match cmd {
        TestCommand::Plan(a) => plan_command(&a),
        TestCommand::Run(a) => run_command(store, a),
        TestCommand::Shell(a) => shell_command(store, a),
        TestCommand::Fixture(c) => fixture_command(store, c),
    }
}

/// The folder of each suite's test file: a test's `fixture` path is relative to it.
fn bases(suites: &[Suite]) -> BTreeMap<String, PathBuf> {
    suites
        .iter()
        .map(|s| {
            (
                s.id.clone(),
                s.file.parent().map(Path::to_path_buf).unwrap_or_default(),
            )
        })
        .collect()
}

fn load(select: &Select) -> std::result::Result<(Vec<Suite>, Plan), String> {
    let mut suites: Vec<Suite> = Vec::new();
    for p in &select.paths {
        for s in suite::load(p).map_err(|e| e.0)? {
            if let Some(o) = suites.iter().find(|o| o.id == s.id) {
                return Err(format!(
                    "{}: suite {} is also in {}",
                    s.file.display(),
                    s.id,
                    o.file.display()
                ));
            }
            suites.push(s);
        }
    }
    let selector = Selector {
        tags: select.tags.clone(),
        ids: select.ids.clone(),
    };
    let plan = plan::plan(&suites, &selector);
    if plan.tests() == 0 {
        let mut want: Vec<String> = select.tags.iter().map(|t| format!("-t {t}")).collect();
        want.extend(select.ids.iter().map(|i| format!("--id {i}")));
        return Err(match want.is_empty() {
            true => "the test files hold no tests".to_owned(),
            false => format!("no test matches {}", want.join(" ")),
        });
    }
    Ok((suites, plan))
}

fn infra(e: impl std::fmt::Display) -> ExitCode {
    eprintln!("swofty: {e}");
    ExitCode::from(exit::INFRA)
}

fn plan_command(a: &PlanArgs) -> Result<ExitCode> {
    let (_, plan) = match load(&a.select) {
        Ok(p) => p,
        Err(e) => return Ok(infra(e)),
    };
    if a.json {
        println!("{}", serde_json::to_string_pretty(&plan.json())?);
    } else {
        print!("{}", plan.text());
    }
    Ok(ExitCode::SUCCESS)
}

fn say(text: &str) {
    eprintln!("[test] {text}");
}

fn run_command(store: &Store, a: RunArgs) -> Result<ExitCode> {
    let started = Instant::now();
    let (suites, plan) = match load(&a.select) {
        Ok(p) => p,
        Err(e) => return Ok(infra(e)),
    };
    let run_id = stamp();
    let out = a
        .out
        .clone()
        .unwrap_or_else(|| PathBuf::from("swofty-test").join(&run_id));
    if let Err(e) = std::fs::create_dir_all(&out) {
        return Ok(infra(format!("{}: {e}", out.display())));
    }
    let mut report = Report::new(&run_id, &out.display().to_string());
    report.game_starts = plan.game_starts();
    let busy = if plan.game_starts() == 0 {
        Harness::skip_all(&plan, &mut report);
        false
    } else {
        let ids: Vec<String> = suites.iter().map(|s| s.id.clone()).collect();
        match run_with_game(
            store,
            &a,
            &plan,
            &run_id,
            &ids,
            &bases(&suites),
            &out,
            &mut report,
        ) {
            Ok(()) => false,
            Err(Stop::Busy(why)) => {
                report.errors.push(why);
                Harness::not_run(&plan, &mut report, "the game was not free");
                true
            }
            Err(Stop::Infra(why)) => {
                report.errors.push(why);
                Harness::not_run(&plan, &mut report, "the run could not start the game");
                false
            }
        }
    };
    report.ms = started.elapsed().as_millis() as u64;
    report.exit_code = if busy { exit::BUSY } else { report.compute_exit() };
    if let Err(e) = write_reports(&out, &report) {
        eprintln!("swofty: could not write the report in {}: {e}", out.display());
    }
    if a.json {
        print!("{}", report.json());
    } else {
        print!("{}", report.summary());
    }
    Ok(ExitCode::from(report.exit_code))
}

fn write_reports(out: &Path, r: &Report) -> std::io::Result<()> {
    std::fs::write(out.join(report::JSON_FILE), r.json())?;
    std::fs::write(out.join(report::JUNIT_FILE), r.junit())?;
    std::fs::write(out.join(report::SUMMARY_FILE), r.summary())
}

#[derive(Debug)]
enum Stop {
    Busy(String),
    Infra(String),
}

const MIN_FREE: u64 = 1 << 30;

#[allow(clippy::too_many_arguments)]
fn run_with_game(
    store: &Store,
    a: &RunArgs,
    plan: &Plan,
    run_id: &str,
    suites: &[String],
    bases: &BTreeMap<String, PathBuf>,
    out: &Path,
    report: &mut Report,
) -> std::result::Result<(), Stop> {
    let g = crate::build::game(store, a.game.clone()).map_err(|e| Stop::Infra(format!("{e:#}")))?;
    crate::build::supported(&g).ok();
    report.game_build = swofty_core::exe_version::read(&g.dir.join(swofty_core::GAME_EXE));
    preflight(store, &g, plan, out).map_err(Stop::Infra)?;
    let exe_sha = sandbox_preflight(store, &g, plan, bases).map_err(Stop::Infra)?;
    let live_saves = exe_sha
        .as_ref()
        .and_then(|_| swofty_core::steam::find_game())
        .and_then(|i| swofty_core::steam::save_dir(&i.steam_root));
    let dir = a.lock.clone().unwrap_or_else(|| lock::default_path(&store.root));
    let owner = lock::owner_line(run_id, suites, &hh_mm());
    let lock = take_lock(&dir, &owner, a.wait)?;
    say(&format!("holding the game lock {}", dir.display()));
    free_the_game(store, a.take_over, a.wait)?;
    let launch_json = store.root.join("state/launch.json");
    let saved = std::fs::read(&launch_json).ok();
    let profile = format!("test-{run_id}");
    let mut launcher = SwoftyRun {
        data: store.root.clone(),
        game: g.dir.clone(),
    };
    let mut h = Harness {
        store,
        game: &g.dir,
        out,
        profile: &profile,
        launcher: &mut launcher,
        times: Times::default(),
        run_id,
        bases: bases.clone(),
        exe_sha,
        live_saves,
    };
    h.run(plan, report);
    if store.profile_names().is_ok_and(|n| n.contains(&profile))
        && let Err(e) = store.delete_profile(&profile)
    {
        report
            .errors
            .push(format!("could not delete the test profile {profile}: {e}"));
    }
    let put_back = match saved {
        Some(bytes) => std::fs::write(&launch_json, bytes),
        None => std::fs::remove_file(&launch_json).or_else(|e| match e.kind() {
            std::io::ErrorKind::NotFound => Ok(()),
            _ => Err(e),
        }),
    };
    if let Err(e) = put_back {
        report
            .errors
            .push(format!("could not put {} back: {e}", launch_json.display()));
    }
    if let Err(e) = lock.release() {
        report
            .errors
            .push(format!("could not release the game lock {}: {e}", dir.display()));
    }
    Ok(())
}

/// Checks before the lock: nothing here changes anything.
fn preflight(store: &Store, g: &Game, plan: &Plan, out: &Path) -> std::result::Result<(), String> {
    terms(store)?;
    if !swofty_core::setup::done(&store.root) {
        return Err(swofty_core::setup::NEEDED.to_owned());
    }
    if swofty_core::steam::in_steam_library(&g.dir) {
        let status = swofty_core::steam_client::status();
        if !status.state.usable() {
            return Err(format!(
                "Steam is not ready ({status}); start Steam and sign in, then run the tests again"
            ));
        }
        if let Some(state) = swofty_core::steam::find_game()
            .filter(|i| swofty_core::copies::same_dir(&i.dir, &g.dir))
            .and_then(|i| swofty_core::steam::busy(&i))
        {
            return Err(format!("Steam is {state} F1 25; wait for it to finish"));
        }
    }
    if let Some(free) = swofty_core::disk::free_space(out)
        && free < MIN_FREE
    {
        return Err(format!(
            "{} has {} free; a test run needs at least {}",
            out.display(),
            swofty_core::disk::gb(free),
            swofty_core::disk::gb(MIN_FREE)
        ));
    }
    addons_installed(store, plan)
}

/// A career environment runs only in the save sandbox, and only on an exe
/// whose core bindings carry the sandbox's install point (verified in the
/// KB). Its fixture must resolve and match its fixture.json. Returns the
/// exe's sha256 when a batch needs the sandbox.
fn sandbox_preflight(
    store: &Store,
    g: &Game,
    plan: &Plan,
    bases: &BTreeMap<String, PathBuf>,
) -> std::result::Result<Option<String>, String> {
    let need: Vec<&Batch> = plan
        .batches
        .iter()
        .filter(|b| sandboxed(b) && b.starts_game())
        .collect();
    if need.is_empty() {
        return Ok(None);
    }
    let mut hashes = swofty_core::baseline::Hashes::load(&store.root);
    let exe = g.dir.join(swofty_core::GAME_EXE);
    let sha = hashes
        .sha256(&exe)
        .map_err(|e| format!("could not hash {}: {e}", exe.display()))?;
    hashes.save();
    let loader = swofty_core::builtin::loader_dir();
    check_sandbox(
        loader.as_deref(),
        &sha,
        &need,
        bases,
        &fixture::store(&store.root),
    )?;
    Ok(Some(sha))
}

pub(crate) fn sandboxed(b: &Batch) -> bool {
    b.env.kind == Kind::Career
}

pub(crate) fn check_sandbox(
    loader: Option<&Path>,
    exe_sha: &str,
    batches: &[&Batch],
    bases: &BTreeMap<String, PathBuf>,
    store: &Path,
) -> std::result::Result<(), String> {
    let Some(loader) = loader else {
        return Err(
            "a career environment needs the save sandbox, and the launcher's loader (version.dll and the \
                    core) was not found"
                .to_owned(),
        );
    };
    let proof = sandbox::verified(loader, exe_sha)
        .map_err(|e| format!("a career environment needs the save sandbox: {e}"))?;
    say(&format!("save sandbox: verified for this F1_25.exe by {proof}"));
    for b in batches {
        let f = batch_fixture(b, bases, store).map_err(|e| format!("{}: {e}", b.label()))?;
        if f.manifest.exe.sha256 != exe_sha {
            say(&format!(
                "{}: fixture {} was saved on another F1_25.exe ({}); older saves normally load forward",
                b.label(),
                f.manifest.name,
                &f.manifest.exe.sha256[..f.manifest.exe.sha256.len().min(16)]
            ));
        }
    }
    Ok(())
}

fn batch_fixture(
    b: &Batch,
    bases: &BTreeMap<String, PathBuf>,
    store: &Path,
) -> std::result::Result<Fixture, String> {
    let spec = b
        .env
        .str("fixture")
        .ok_or("a career environment names no fixture")?;
    let base = b
        .tests
        .first()
        .and_then(|t| bases.get(&t.suite))
        .map(PathBuf::as_path);
    fixture::load(&fixture::resolve(spec, base, store)?)
}

/// The offline play terms are the player's to accept; a test run never
/// accepts them.
fn terms(store: &Store) -> std::result::Result<(), String> {
    match swofty_core::offline::is_accepted(&store.root) {
        true => Ok(()),
        false => Err(format!(
            "the offline play terms are not accepted in {}; a test run never accepts them for you: run swofty \
             offline accept yourself, then run the tests again",
            store.root.display()
        )),
    }
}

fn addons_installed(store: &Store, plan: &Plan) -> std::result::Result<(), String> {
    let installed = store.addons().map_err(|e| e.to_string())?;
    for b in plan.batches.iter().filter(|b| b.starts_game()) {
        if let Some(id) = b.addons.iter().find(|id| !installed.contains_key(*id)) {
            return Err(format!(
                "{id} is not installed, and the tests in {} need it: install it with swofty addons install <folder>",
                b.label()
            ));
        }
    }
    Ok(())
}

fn take_lock(dir: &Path, owner: &str, wait: Duration) -> std::result::Result<GameLock, Stop> {
    let running = || !game_processes().is_empty();
    match GameLock::acquire(dir, owner, wait, LOCK_POLL, |b| {
        say(&format!("waiting for the game: {}", b.text(running())))
    }) {
        Ok(Ok(lock)) => Ok(lock),
        Ok(Err(busy)) => Err(Stop::Busy(busy.text(running()))),
        Err(e) => Err(Stop::Infra(format!(
            "{e}; set SWOFTY_GAME_LOCK or --lock to a folder this user can create"
        ))),
    }
}

const LOCK_POLL: Duration = Duration::from_secs(5);
const CLOSE_WAIT: Duration = Duration::from_secs(120);
const ROLLBACK_WAIT: Duration = Duration::from_secs(600);

/// A game running without the lock is a player's: wait for it to close, or
/// with `--take-over`, close it by its exact pid. Then wait for its session
/// to put the game files back.
fn free_the_game(store: &Store, take_over: bool, wait: Duration) -> std::result::Result<(), Stop> {
    let running = game_processes();
    if let Some(first) = running.first() {
        if take_over {
            request_stop(&store.root);
            for g in &running {
                say(&format!("--take-over: closing F1 25 (pid {})", g.pid));
                stop_process(g.pid).ok();
            }
            if !until(CLOSE_WAIT, || game_processes().is_empty()) {
                return Err(Stop::Infra(format!(
                    "F1 25 (pid {}) did not close within {} s",
                    first.pid,
                    CLOSE_WAIT.as_secs()
                )));
            }
        } else {
            say(&format!(
                "F1 25 is running (pid {}); waiting for it to close (--take-over closes it)",
                first.pid
            ));
            if !until(wait, || game_processes().is_empty()) {
                return Err(Stop::Busy(format!(
                    "F1 25 is running (pid {}); close it, or pass --take-over to close it",
                    first.pid
                )));
            }
        }
    }
    if SessionLock::held_elsewhere(&store.root) {
        say("waiting for the last session to put its game files back");
        let limit = if take_over {
            ROLLBACK_WAIT
        } else {
            wait.min(ROLLBACK_WAIT)
        };
        if !until(limit, || !SessionLock::held_elsewhere(&store.root)) {
            let why =
                "the last session is still putting the game files back (its session lock is held)".to_owned();
            return Err(if take_over {
                Stop::Infra(why)
            } else {
                Stop::Busy(why)
            });
        }
    }
    Ok(())
}

fn until(limit: Duration, mut done: impl FnMut() -> bool) -> bool {
    let start = Instant::now();
    loop {
        if done() {
            return true;
        }
        if start.elapsed() >= limit {
            return false;
        }
        std::thread::sleep(Duration::from_millis(500).min(limit.saturating_sub(start.elapsed())));
    }
}

/// Local `HH:MM` for the lock's owner line.
fn hh_mm() -> String {
    #[cfg(target_os = "linux")]
    {
        let t = now_secs() as libc::time_t;
        let mut tm: libc::tm = unsafe { std::mem::zeroed() };
        // SAFETY: localtime_r writes only into `tm`.
        if !unsafe { libc::localtime_r(&t, &mut tm) }.is_null() {
            return format!("{:02}:{:02}", tm.tm_hour, tm.tm_min);
        }
    }
    let s = now_secs() as u64 % 86_400;
    format!("{:02}:{:02}", s / 3600, s % 3600 / 60)
}

/// What starts the game for a batch: `swofty run`, or a stand-in in tests.
pub(crate) trait Launcher {
    fn launch(
        &mut self,
        profile: &str,
        game_args: &[String],
        env: &[(String, String)],
        dir: &Path,
    ) -> Result<Box<dyn Running>>;
}

/// A started batch.
pub(crate) trait Running {
    /// The game has exited and its game files are back.
    fn finished(&mut self) -> bool;
    /// Ask the game to close, by its exact pid.
    fn stop(&mut self);
    /// Give up: end the launcher process itself.
    fn kill(&mut self);
    /// The session folder with the session's logs, once known.
    fn session(&mut self) -> Option<PathBuf>;
}

struct SwoftyRun {
    data: PathBuf,
    game: PathBuf,
}

impl Launcher for SwoftyRun {
    fn launch(
        &mut self,
        profile: &str,
        game_args: &[String],
        env: &[(String, String)],
        dir: &Path,
    ) -> Result<Box<dyn Running>> {
        let log = std::fs::File::create(dir.join("launcher.log"))?;
        let mut c = Command::new(std::env::current_exe()?);
        c.arg("--data-dir")
            .arg(&self.data)
            .arg("--profile")
            .arg(profile)
            .arg("run")
            .arg("--apply")
            .arg("--direct")
            .arg("--no-start-store")
            .arg("--game")
            .arg(&self.game);
        for a in game_args {
            c.arg(format!("--game-arg={a}"));
        }
        c.env("SWOFTY_KEEP_FOCUS", "1")
            .envs(env.iter().map(|(k, v)| (k, v)))
            .stdin(Stdio::null())
            .stdout(log.try_clone()?)
            .stderr(log);
        #[cfg(unix)]
        std::os::unix::process::CommandExt::process_group(&mut c, 0);
        let before = std::fs::read(self.data.join("state/launch.json")).ok();
        let child = c.spawn()?;
        Ok(Box::new(SwoftyRunning {
            child,
            data: self.data.clone(),
            game: self.game.clone(),
            launched: SystemTime::now(),
            before,
            exited: false,
            session: None,
        }))
    }
}

struct SwoftyRunning {
    child: Child,
    data: PathBuf,
    game: PathBuf,
    launched: SystemTime,
    before: Option<Vec<u8>>,
    exited: bool,
    session: Option<PathBuf>,
}

impl Running for SwoftyRunning {
    fn finished(&mut self) -> bool {
        if !self.exited {
            self.exited = !matches!(self.child.try_wait(), Ok(None));
        }
        self.exited && !SessionLock::held_elsewhere(&self.data)
    }

    fn stop(&mut self) {
        request_stop(&self.data);
        let ours = games_in(&self.game).into_iter().filter(|g| {
            descends_from(g.pid, self.child.id()) || g.started.is_some_and(|s| s >= self.launched)
        });
        for g in ours {
            stop_process(g.pid).ok();
        }
    }

    fn kill(&mut self) {
        stop_process(self.child.id()).ok();
    }

    fn session(&mut self) -> Option<PathBuf> {
        if self.session.is_some() {
            return self.session.clone();
        }
        let now = std::fs::read(self.data.join("state/launch.json")).ok()?;
        if Some(&now) == self.before.as_ref() {
            return None;
        }
        let id = serde_json::from_slice::<serde_json::Value>(&now).ok()?["id"]
            .as_str()?
            .to_owned();
        let store = Store::new(&self.data);
        let dir = store.latest_session()?;
        let record = SessionRecord::read(&dir);
        if record.request_id.as_deref() == Some(id.as_str()) {
            self.session = Some(dir);
        }
        self.session.clone()
    }
}

pub(crate) struct Times {
    pub start: Duration,
    pub step: Duration,
    pub test: Duration,
    pub race: Duration,
    pub finish: Duration,
    pub restop: Duration,
    pub poll: Duration,
    /// How long past a command's own time limit the CLI waits for its answer.
    pub grace: Duration,
    /// How long the game gets to close after the agent's quit.
    pub quit: Duration,
    /// How often the title screen gets a pad press when the agent can't
    /// pass it without input.
    pub title_press: Duration,
    /// How long a career environment's `career load` may take.
    pub career: Duration,
}

impl Default for Times {
    fn default() -> Self {
        Times {
            start: Duration::from_secs(900),
            step: Duration::from_secs(30),
            test: Duration::from_secs(300),
            race: Duration::from_secs(900),
            finish: Duration::from_secs(600),
            restop: Duration::from_secs(10),
            poll: Duration::from_millis(250),
            grace: Duration::from_secs(10),
            quit: Duration::from_secs(30),
            title_press: Duration::from_secs(3),
            career: Duration::from_secs(900),
        }
    }
}

pub(crate) struct Harness<'a> {
    pub store: &'a Store,
    pub game: &'a Path,
    pub out: &'a Path,
    pub profile: &'a str,
    pub launcher: &'a mut dyn Launcher,
    pub times: Times,
    pub run_id: &'a str,
    /// Each suite's test file folder, for fixture paths.
    pub bases: BTreeMap<String, PathBuf>,
    /// The game exe's sha256 when a batch needs the save sandbox.
    pub exe_sha: Option<String>,
    /// The player's Steam save folder: a sandboxed batch compares its
    /// listing (names, sizes, times; never contents) before and after.
    pub live_saves: Option<PathBuf>,
}

/// A batch's save sandbox: the fixture copied into the run folder's saves
/// and the player's save folder listing before the game started.
struct Sandbox {
    fixture: String,
    /// The fixture's careers' game mode, when it tells which to continue.
    mode: Option<String>,
    storage_ids: BTreeSet<String>,
    live_before: Option<BTreeMap<String, (u64, Option<SystemTime>)>>,
}

/// A batch's test run folder and its line to the test agent.
struct Agent {
    run: RunDir,
    client: Client,
    hello: bool,
    /// Values the steps read, by their `as` name, and sampled series.
    values: BTreeMap<String, Value>,
    series: BTreeMap<String, Vec<f64>>,
    /// Where results and frames are kept: the batch folder.
    keep_dir: PathBuf,
}

/// The benchmark XML staged in the game folder for one batch, and removed
/// when the batch ends.
struct Staged {
    path: PathBuf,
    made_dir: Option<PathBuf>,
}

impl Staged {
    fn write(game: &Path, b: &Batch) -> std::io::Result<Option<Staged>> {
        if b.env.kind != Kind::Benchmark {
            return Ok(None);
        }
        let dir = game.join(bench::DIR);
        let made_dir = match dir.is_dir() {
            true => None,
            false => {
                std::fs::create_dir_all(&dir)?;
                Some(dir.clone())
            }
        };
        let path = dir.join(bench::FILE);
        std::fs::write(&path, bench::xml(&b.env))?;
        Ok(Some(Staged { path, made_dir }))
    }

    fn remove(&mut self) -> std::io::Result<()> {
        match std::fs::remove_file(&self.path) {
            Err(e) if e.kind() != std::io::ErrorKind::NotFound => return Err(e),
            _ => {}
        }
        if let Some(d) = self.made_dir.take() {
            std::fs::remove_dir(d).ok();
        }
        Ok(())
    }
}

impl Drop for Staged {
    fn drop(&mut self) {
        self.remove().ok();
    }
}

fn ms(since: Instant) -> u64 {
    since.elapsed().as_millis() as u64
}

fn read(path: &Path) -> Option<String> {
    std::fs::read(path)
        .ok()
        .map(|b| String::from_utf8_lossy(&b).into_owned())
}

/// The live loader.log once this session's game has replaced the last one.
struct Live {
    path: PathBuf,
    mark: Option<LogMark>,
    last: String,
}

impl Live {
    fn text(&mut self) -> &str {
        if LogMark::replaced(&self.path, self.mark.as_ref())
            && let Some(t) = read(&self.path)
            && t.len() >= self.last.len()
        {
            self.last = t;
        }
        &self.last
    }
}

impl Harness<'_> {
    fn blank(b: &Batch, index: usize) -> (BatchReport, Vec<TestReport>) {
        let label = b.label();
        let batch = BatchReport {
            index,
            env: label.clone(),
            key: b.key.clone(),
            addons: b.addons.clone(),
            started_game: false,
            ready_ms: None,
            ms: None,
            session: None,
            end: None,
            error: None,
            clean: None,
            notes: Vec::new(),
        };
        let tests = b
            .tests
            .iter()
            .map(|t| TestReport {
                suite: t.suite.clone(),
                id: t.id.clone(),
                env: label.clone(),
                batch: index,
                status: Status::Skipped,
                ms: None,
                reason: t.skip_reason().map(|r| r.to_owned()),
                failures: Vec::new(),
                steps: Vec::new(),
            })
            .collect();
        (batch, tests)
    }

    fn skip_all(plan: &Plan, report: &mut Report) {
        for (i, b) in plan.batches.iter().enumerate() {
            let (batch, tests) = Self::blank(b, i + 1);
            report.batches.push(batch);
            report.tests.extend(tests);
        }
    }

    fn not_run(plan: &Plan, report: &mut Report, why: &str) {
        Self::skip_all(plan, report);
        for t in report.tests.iter_mut().filter(|t| t.reason.is_none()) {
            t.status = Status::NotRun;
            t.reason = Some(why.to_owned());
        }
    }

    pub fn run(&mut self, plan: &Plan, report: &mut Report) {
        for (i, b) in plan.batches.iter().enumerate() {
            let (batch, tests) = self.batch(i + 1, b);
            report.batches.push(batch);
            report.tests.extend(tests);
        }
    }

    fn profile(&self, b: &Batch, name: &str) -> Result<()> {
        let installed = self.store.addons()?;
        let registry = self.store.registry()?;
        let mut p = self.store.profile(name)?;
        let keep = |id: &str| {
            id == CORE_ID || b.addons.iter().any(|a| a == id) || registry.get(id).is_some_and(|e| e.locked)
        };
        let listed: Vec<String> = p.addons.iter().map(|a| a.id.clone()).collect();
        for id in listed {
            p.set_enabled(&id, keep(&id))?;
        }
        for id in &b.addons {
            if !installed.contains_key(id) {
                bail!("{id} is not installed: install it with swofty addons install <folder>");
            }
            p.set_enabled(id, true)?;
        }
        p.options.clear();
        for (id, options) in &b.settings {
            let Some(m) = installed.get(id) else {
                bail!("the settings name {id}, which is not installed");
            };
            for (option, value) in options {
                p.set_option(m, option, value.clone())?;
            }
        }
        self.store.save_profile(&p)?;
        Ok(())
    }

    fn batch(&mut self, index: usize, b: &Batch) -> (BatchReport, Vec<TestReport>) {
        let (mut br, mut reports) = Self::blank(b, index);
        let runnable: Vec<usize> = (0..b.tests.len())
            .filter(|&i| reports[i].reason.is_none())
            .collect();
        if runnable.is_empty() {
            return (br, reports);
        }
        let fail_all = |reports: &mut [TestReport], why: &str| {
            for &i in &runnable {
                reports[i].status = Status::Error;
                reports[i].reason = Some(why.to_owned());
            }
        };
        let dir = self.out.join("batches").join(index.to_string());
        if let Err(e) = std::fs::create_dir_all(&dir) {
            br.error = Some(format!("{}: {e}", dir.display()));
            fail_all(&mut reports, "the batch did not start");
            return (br, reports);
        }
        let sandboxed = sandboxed(b);
        let profile = match sandboxed {
            true => format!("{}-{index}", self.profile),
            false => self.profile.to_owned(),
        };
        if let Err(e) = self.profile(b, &profile) {
            br.error = Some(format!("the test profile: {e:#}"));
            fail_all(&mut reports, "the batch did not start");
            self.drop_profile(sandboxed, &profile, &mut br);
            return (br, reports);
        }
        let tests_need_agent = sandboxed || runnable.iter().any(|&i| b.tests[i].needs_agent());
        let needs_agent = tests_need_agent || b.env.kind == Kind::Boot;
        let mut agent = match needs_agent {
            false => None,
            true => match self.open_run(b, index, &dir, sandboxed) {
                Ok(a) => Some(a),
                Err(e) => {
                    br.error = Some(format!("the test run folder: {e}"));
                    fail_all(&mut reports, "the batch did not start");
                    self.drop_profile(sandboxed, &profile, &mut br);
                    return (br, reports);
                }
            },
        };
        let sandbox = match (sandboxed, agent.as_ref()) {
            (true, Some(a)) => match batch_fixture(b, &self.bases, &fixture::store(&self.store.root))
                .and_then(|f| self.open_sandbox(f, b, &profile, &a.run))
            {
                Ok(s) => {
                    br.notes.push(format!(
                        "save sandbox {} with fixture {}",
                        a.run.saves().display(),
                        s.fixture
                    ));
                    Some(s)
                }
                Err(e) => {
                    br.error = Some(format!("the save sandbox: {e}"));
                    fail_all(&mut reports, "the batch did not start");
                    self.close_run(agent.take(), &mut br);
                    self.drop_profile(sandboxed, &profile, &mut br);
                    return (br, reports);
                }
            },
            _ => None,
        };
        let env = agent.as_ref().map(|a| a.run.env()).unwrap_or_default();
        let mut staged = match Staged::write(self.game, b) {
            Ok(s) => s,
            Err(e) => {
                br.error = Some(format!("could not write the benchmark XML: {e}"));
                fail_all(&mut reports, "the batch did not start");
                if let Some(s) = sandbox {
                    self.close_sandbox(
                        s,
                        agent.as_ref().map(|a| &a.run),
                        b,
                        &profile,
                        &mut br,
                        &mut reports,
                        &runnable,
                    );
                }
                self.close_run(agent.take(), &mut br);
                self.drop_profile(sandboxed, &profile, &mut br);
                return (br, reports);
            }
        };
        let mut live = Live {
            path: self.game.join(LOADER_LOG),
            mark: LogMark::read(&self.game.join(LOADER_LOG)),
            last: String::new(),
        };
        let args = match b.env.kind {
            Kind::Benchmark => bench::args(),
            _ => Vec::new(),
        };
        say(&format!("batch {index}: starting F1 25 ({})", br.env));
        let started = Instant::now();
        let mut run = match self.launcher.launch(&profile, &args, &env, &dir) {
            Ok(r) => r,
            Err(e) => {
                br.error = Some(format!("could not start the game: {e:#}"));
                fail_all(&mut reports, "the batch did not start");
                if let Some(s) = sandbox {
                    self.close_sandbox(
                        s,
                        agent.as_ref().map(|a| &a.run),
                        b,
                        &profile,
                        &mut br,
                        &mut reports,
                        &runnable,
                    );
                }
                self.close_run(agent.take(), &mut br);
                self.drop_profile(sandboxed, &profile, &mut br);
                return (br, reports);
            }
        };
        br.started_game = true;
        let kind = b.env.kind;
        let mut told_title = false;
        let mut pressed: Option<Instant> = None;
        let mut title: Option<u64> = None;
        let mut by_pad = false;
        let ready = loop {
            if let Some(a) = agent.as_mut()
                && let Err(why) = a.keep_up()
            {
                break Err(why);
            }
            let text = live.text();
            if text.contains(checks::ready_line(kind)) {
                break Ok(());
            }
            if agent.is_some()
                && let Some(line) = text.lines().find(|l| l.contains(sandbox::FAILED_LINE))
            {
                break Err(format!("the core refused the test run and the game quit: {line}"));
            }
            let at_title = matches!(kind, Kind::Boot | Kind::Route) && text.contains(checks::TITLE);
            if let Some(a) = agent.as_mut().filter(|a| a.hello && at_title) {
                match title {
                    None => title = a.client.send("title", Value::Null, None).ok(),
                    Some(seq) => {
                        if let Some(r) = a.client.take_reply(seq) {
                            by_pad = r.outcome != Outcome::Ok;
                            if by_pad {
                                say(&format!("title: {}; pressing the virtual pad instead", r.text()));
                            }
                        }
                    }
                }
                if by_pad && pressed.is_none_or(|p| p.elapsed() >= self.times.title_press) {
                    pressed = Some(Instant::now());
                    a.client
                        .send("pad", json!({ "press": "a", "ms": 150 }), None)
                        .ok();
                }
            } else if at_title && agent.is_none() && !told_title {
                told_title = true;
                say("at the title screen: it waits for a button press before the main menu");
            }
            if run.finished() {
                break Err(format!("the game exited before {}", checks::ready_text(kind)));
            }
            if started.elapsed() >= self.times.start {
                break Err(format!(
                    "the game did not reach {} within {} s",
                    checks::ready_text(kind),
                    self.times.start.as_secs()
                ));
            }
            std::thread::sleep(self.times.poll);
        };
        let mut dropped_agent = false;
        let ready = ready.and_then(|()| match agent.as_mut() {
            None => Ok(()),
            Some(a) => {
                let left = self.times.start.saturating_sub(started.elapsed());
                match a.hello(left, self.times.poll, &mut *run) {
                    Err(why) if !tests_need_agent => {
                        br.notes.push(format!("no test agent for this boot batch: {why}"));
                        dropped_agent = true;
                        return Ok(());
                    }
                    other => other?,
                }
                self.env_routes(&b.env, a, &mut *run)?;
                self.env_career(
                    &b.env,
                    sandbox.as_ref().and_then(|s| s.mode.as_deref()),
                    a,
                    &mut *run,
                )
            }
        });
        if dropped_agent {
            self.close_run(agent.take(), &mut br);
        }
        match &ready {
            Err(why) => {
                br.error = Some(why.clone());
                fail_all(&mut reports, &format!("the batch did not start: {why}"));
            }
            Ok(()) => {
                br.ready_ms = Some(ms(started));
                say(&format!("batch {index}: ready at {}", checks::ready_text(kind)));
                let race_start = Instant::now();
                for &i in &runnable {
                    let t0 = Instant::now();
                    let t = &b.tests[i];
                    self.steps(t, &mut reports[i], &mut live, &mut *run, agent.as_mut());
                    if reports[i].failures.is_empty()
                        && let (Some(f), Some(a)) = (&t.function, agent.as_mut())
                    {
                        self.function(t, f, &mut reports[i], a, &mut *run);
                    }
                    if let Some(a) = agent.as_mut() {
                        for c in &t.checks {
                            if let suite::Check::Agent { text } = c
                                && let Err(f) = self.agent_check(text, a, &mut *run)
                            {
                                reports[i].failures.push(f);
                            }
                        }
                    }
                    reports[i].ms = Some(ms(t0));
                }
                if kind == Kind::Benchmark {
                    let limit = runnable
                        .iter()
                        .filter_map(|&i| b.tests[i].timeout_s)
                        .max()
                        .map_or(self.times.race, Duration::from_secs);
                    if until_polled(limit, self.times.poll, || run.finished()) {
                        for &i in &runnable {
                            reports[i].ms = Some(ms(race_start));
                        }
                    } else {
                        let why = format!("the benchmark race did not end within {} s", limit.as_secs());
                        br.error = Some(why.clone());
                        fail_all(&mut reports, &why);
                    }
                }
            }
        }
        if let Some(a) = agent.as_mut().filter(|a| a.hello) {
            a.client.send("quit", Value::Null, None).ok();
            until_polled(self.times.quit, self.times.poll, || run.finished());
        }
        if let Err(why) = self.finish(&mut *run) {
            br.notes.push(why);
        }
        br.ms = Some(ms(started));
        if let Some(a) = agent.as_mut() {
            a.client.poll();
            if let Err(e) = a.keep() {
                br.notes
                    .push(format!("could not keep the test agent's results: {e}"));
            }
        }
        let unstaged = staged.as_mut().map(|s| s.remove());
        drop(staged);
        let session = run.session();
        br.session = session.as_ref().map(|s| s.display().to_string());
        let record = session.as_deref().map(SessionRecord::read);
        let final_log = session
            .as_deref()
            .and_then(|s| read(&s.join("loader.log")))
            .unwrap_or_else(|| live.text().to_owned());
        let perf = session.as_deref().and_then(|s| read(&s.join("perf.log")));
        if let Some(s) = &session {
            for f in [
                "loader.log",
                "perf.log",
                "end.json",
                "exit_code",
                "game_stdout.log",
            ] {
                std::fs::copy(s.join(f), dir.join(f)).ok();
            }
        } else {
            std::fs::write(dir.join("loader.log"), &final_log).ok();
        }
        br.end = record
            .as_ref()
            .and_then(|r| r.end.as_ref())
            .map(|e| format!("{}: {}", e.end.as_str(), e.reason));
        let crash = record.as_ref().filter(|r| r.crashed()).map(|r| match &r.end {
            Some(e) => format!("the game crashed ({})", e.reason),
            None => format!("the game crashed (exit code {})", r.exit_code.unwrap_or(-1)),
        });
        br.notes.extend(crash.clone());
        let rel = |f: &str| format!("batches/{index}/{f}");
        let ev = Evidence {
            log: final_log,
            perf,
            crash,
            log_path: Some(rel("loader.log")),
            perf_path: Some(rel("perf.log")),
        };
        for &i in &runnable {
            let r = &mut reports[i];
            if r.status == Status::Error {
                continue;
            }
            for c in &b.tests[i].checks {
                if matches!(c, suite::Check::Agent { .. }) && agent.is_some() {
                    continue;
                }
                if let Err(f) = checks::evaluate(c, &ev) {
                    r.failures.push(f);
                }
            }
            r.status = match r.failures.is_empty() {
                true => Status::Pass,
                false => Status::Fail,
            };
        }
        if let Some(s) = sandbox {
            self.close_sandbox(
                s,
                agent.as_ref().map(|a| &a.run),
                b,
                &profile,
                &mut br,
                &mut reports,
                &runnable,
            );
        }
        self.close_run(agent.take(), &mut br);
        self.drop_profile(sandboxed, &profile, &mut br);
        let (clean, notes) = self.clean(unstaged);
        br.clean = Some(clean);
        br.notes.extend(notes);
        (br, reports)
    }

    /// The fixture's saves in the run folder's sandbox and the fixture's
    /// addon storage as the batch profile's copy, which the session loads
    /// into the addons' storage when it starts.
    fn open_sandbox(
        &self,
        f: Fixture,
        b: &Batch,
        profile: &str,
        run: &RunDir,
    ) -> std::result::Result<Sandbox, String> {
        let data = &self.store.root;
        let ids: BTreeSet<String> = f
            .storage_ids()
            .into_iter()
            .chain(b.addons.iter().cloned())
            .collect();
        for id in &ids {
            let d = addon_storage::profile_dir(data, id, profile);
            if d.exists() {
                std::fs::remove_dir_all(&d).map_err(|e| format!("{}: {e}", d.display()))?;
            }
        }
        let copied = f.install(&run.remote(), &|id| addon_storage::profile_dir(data, id, profile))?;
        let installed = self.store.addons().map_err(|e| e.to_string())?;
        let addons: BTreeMap<String, String> = b
            .addons
            .iter()
            .filter_map(|id| Some((id.clone(), installed.get(id)?.version.to_string())))
            .collect();
        std::fs::write(
            run.root.join("addons.json"),
            serde_json::to_vec_pretty(&addons).expect("serialisable"),
        )
        .map_err(|e| format!("{}: {e}", run.root.display()))?;
        for id in f.storage_ids().iter().filter(|id| !b.addons.contains(id)) {
            say(&format!(
                "fixture {}: it carries storage for {id}, which the batch does not enable; the game won't see it",
                f.manifest.name
            ));
        }
        say(&format!(
            "save sandbox {}: fixture {} ({} file(s)) copied in",
            run.saves().display(),
            f.manifest.name,
            copied.len()
        ));
        Ok(Sandbox {
            fixture: f.manifest.name.clone(),
            mode: career_mode(&f, b.env.uint("slot")),
            storage_ids: ids,
            live_before: self.live_saves.as_deref().map(sandbox::listing),
        })
    }

    /// After the batch: keep its addon storage in the run folder for a
    /// capture, check the core armed the sandbox and the player's save folder
    /// did not change, and retire the run token.
    #[allow(clippy::too_many_arguments)]
    fn close_sandbox(
        &self,
        s: Sandbox,
        run: Option<&RunDir>,
        b: &Batch,
        profile: &str,
        br: &mut BatchReport,
        reports: &mut [TestReport],
        runnable: &[usize],
    ) {
        let data = &self.store.root;
        let Some(run) = run else { return };
        for id in &s.storage_ids {
            let from = addon_storage::profile_dir(data, id, profile);
            let to = run.storage().join(id);
            if from.is_dir() && copy_tree(&from, &to).is_err() {
                br.notes
                    .push(format!("could not keep {id}'s storage in {}", to.display()));
            }
        }
        let status = run.status();
        let mut why = match &status {
            None if br.started_game => Some(
                "the core never reported the save sandbox (no sandbox.json): the save device may not have started"
                    .to_owned(),
            ),
            Some(st) if !st.armed() => Some(format!("the save sandbox did not arm: {}", st.why)),
            _ => None,
        };
        if let (Some(before), Some(dir)) = (&s.live_before, &self.live_saves) {
            let changed = sandbox::changed(before, &sandbox::listing(dir));
            if !changed.is_empty() {
                why = Some(format!(
                    "THE PLAYER'S STEAM SAVE FOLDER CHANGED during a sandboxed run ({}): {}",
                    dir.display(),
                    changed.join(", ")
                ));
            }
        }
        if let Some(why) = why {
            say(&format!("{}: {why}", b.label()));
            br.error = Some(why.clone());
            for &i in runnable {
                reports[i].status = Status::Error;
                reports[i].reason = Some(why.clone());
            }
        }
    }

    fn drop_profile(&self, sandboxed: bool, profile: &str, br: &mut BatchReport) {
        if sandboxed
            && self
                .store
                .profile_names()
                .is_ok_and(|n| n.contains(&profile.to_owned()))
            && let Err(e) = self.store.delete_profile(profile)
        {
            br.notes
                .push(format!("could not delete the batch profile {profile}: {e}"));
        }
    }

    fn steps(
        &self,
        t: &Test,
        r: &mut TestReport,
        live: &mut Live,
        run: &mut dyn Running,
        mut agent: Option<&mut Agent>,
    ) {
        let test_limit = t.timeout_s.map_or(self.times.test, Duration::from_secs);
        let started = Instant::now();
        for s in &t.steps {
            let limit = s
                .timeout_s
                .map_or(self.times.step, Duration::from_secs)
                .min(test_limit.saturating_sub(started.elapsed()));
            let by_agent =
                s.host.is_none() || matches!(s.host, Some(suite::Wait::Event(_))) && agent.is_some();
            if by_agent {
                let Some(a) = agent.as_deref_mut() else {
                    r.steps.push(StepReport {
                        step: s.text.clone(),
                        status: format!("skipped: {}", suite::NEEDS_AGENT),
                        ms: None,
                    });
                    continue;
                };
                let t0 = Instant::now();
                let done = self.agent_step(s, limit, a, run);
                r.steps.push(StepReport {
                    step: s.text.clone(),
                    status: match &done {
                        Ok(text) => text.clone(),
                        Err(f) => f.actual.clone(),
                    },
                    ms: Some(ms(t0)),
                });
                if let Err(f) = done {
                    r.failures.push(f);
                    return;
                }
                continue;
            }
            let Some(wait) = &s.host else { continue };
            let t0 = Instant::now();
            let seen = loop {
                if checks::waited(live.text(), wait) {
                    break true;
                }
                if t0.elapsed() >= limit || run.finished() {
                    break checks::waited(live.text(), wait);
                }
                std::thread::sleep(self.times.poll);
            };
            r.steps.push(StepReport {
                step: s.text.clone(),
                status: if seen { "ok" } else { "timed out" }.to_owned(),
                ms: Some(ms(t0)),
            });
            if !seen {
                let why = if run.finished() {
                    "none before the game exited".to_owned()
                } else {
                    format!("none within {} s", limit.as_secs())
                };
                r.failures.push(Failure {
                    check: format!("wait {}", checks::wait_text(wait)),
                    expected: checks::wait_text(wait),
                    actual: why,
                    evidence: None,
                });
                return;
            }
        }
    }

    /// Close the game unless it has closed, and wait for the session to put
    /// the game files back, asking again now and then in case the game
    /// started late.
    fn finish(&self, run: &mut dyn Running) -> std::result::Result<(), String> {
        let start = Instant::now();
        let mut asked: Option<Instant> = None;
        while !run.finished() {
            if start.elapsed() >= self.times.finish {
                run.kill();
                return Err(format!(
                    "the session did not end within {} s of being asked to; its launcher was stopped, so run swofty \
                     restore if the game folder is not stock",
                    self.times.finish.as_secs()
                ));
            }
            if asked.is_none_or(|a| a.elapsed() >= self.times.restop) {
                run.stop();
                asked = Some(Instant::now());
            }
            std::thread::sleep(self.times.poll);
        }
        Ok(())
    }

    /// The game folder is back as it was: nothing staged is left, the
    /// session lock is free and no session journal is open.
    fn clean(&self, unstaged: Option<std::io::Result<()>>) -> (bool, Vec<String>) {
        let mut notes = Vec::new();
        if let Some(Err(e)) = unstaged {
            notes.push(format!("could not remove the benchmark XML: {e}"));
        }
        let xml = self.game.join(bench::DIR).join(bench::FILE);
        if xml.exists() {
            notes.push(format!("{} is still in the game folder", xml.display()));
        }
        let data = &self.store.root;
        if SessionLock::held_elsewhere(data) {
            notes.push("the session is still putting the game files back (its session lock is held)".into());
        }
        match Journal::open(data) {
            Ok(open) => {
                let n = open.iter().filter(|j| j.session).count();
                if n > 0 {
                    notes.push(format!(
                        "{n} session journal(s) are still open: the game files were not all put back; run swofty restore"
                    ));
                }
            }
            Err(e) => notes.push(format!("could not read the install journals: {e}")),
        }
        if swofty_core::offline::journal_path(data).exists() {
            notes.push("the offline play staging is still in the game folder; run swofty restore".into());
        }
        (notes.is_empty(), notes)
    }
}

impl Agent {
    /// Read what the agent has written since the last call.
    fn keep_up(&mut self) -> std::result::Result<(), String> {
        self.client.poll();
        self.hello |= self.client.hello.is_some();
        match &self.client.refused {
            Some(why) => Err(format!("the test agent refused the run: {why}")),
            None => Ok(()),
        }
    }

    fn hello(
        &mut self,
        limit: Duration,
        poll: Duration,
        run: &mut dyn Running,
    ) -> std::result::Result<(), String> {
        let hello = self.client.wait_hello(limit, poll, || !run.finished(), || {})?;
        self.hello = true;
        say(&format!(
            "test agent: hello (pid {}, protocol {})",
            hello["pid"], hello["v"]
        ));
        Ok(())
    }

    fn call(
        &mut self,
        cmd: &str,
        args: Value,
        limit: Duration,
        times: &Times,
        run: &mut dyn Running,
    ) -> std::result::Result<agent::Reply, String> {
        let r = self
            .client
            .call(
                cmd,
                args,
                Some(limit),
                times.grace,
                times.poll.min(Duration::from_millis(50)),
                || !run.finished(),
            )
            .map_err(|e| e.to_string())?;
        if cmd == "capture"
            && r.outcome == Outcome::Ok
            && let Some(f) = r.value.get("file").and_then(Value::as_str)
            && let Some(name) = Path::new(f).file_name()
        {
            let frames = self.keep_dir.join("frames");
            std::fs::create_dir_all(&frames).ok();
            std::fs::copy(self.run.agent().join(f), frames.join(name)).ok();
        }
        Ok(r)
    }

    /// Keep what the agent wrote in the batch folder: a run folder without
    /// the sandbox is removed after the batch.
    fn keep(&self) -> std::io::Result<()> {
        let mut text = self.client.lines.join("\n");
        text.push('\n');
        std::fs::write(self.keep_dir.join("agent.jsonl"), text)
    }
}

fn agent_failure(check: String, expected: String, actual: String) -> Failure {
    Failure {
        check,
        expected,
        actual,
        evidence: Some("agent.jsonl".into()),
    }
}

impl Harness<'_> {
    /// The batch's test run folder (`<data>/test-runs/<run>-<batch>/`):
    /// run.json with a fresh token, the test DLLs in `agent/dll/` and, for a
    /// career, the save sandbox's `saves/remote/`.
    fn open_run(&self, b: &Batch, index: usize, keep_dir: &Path, sandbox: bool) -> std::io::Result<Agent> {
        let mut dlls: Vec<(PathBuf, String)> = Vec::new();
        for t in &b.tests {
            if let Some(dll) = t.dll() {
                let name = format!("{}.dll", t.suite);
                if !dlls.iter().any(|(_, n)| *n == name) {
                    dlls.push((dll, name));
                }
            } else if let Some(f) = &t.function {
                return Err(std::io::Error::other(format!(
                    "{} runs fn {f}, and there is no native/tests.dll next to {}",
                    t.full_id(),
                    t.file.display()
                )));
            }
        }
        let options = RunOptions {
            sandbox,
            exe_sha256: self.exe_sha.clone(),
            dlls,
        };
        let run = RunDir::create(&self.store.root, &format!("{}-{index}", self.run_id), options)?;
        let client = Client::new(&run.agent());
        Ok(Agent {
            run,
            client,
            hello: false,
            values: BTreeMap::new(),
            series: BTreeMap::new(),
            keep_dir: keep_dir.to_path_buf(),
        })
    }

    /// After the batch: retire the run's token, which stops the agent, and
    /// remove a run folder that holds no saves to capture.
    fn close_run(&self, agent: Option<Agent>, br: &mut BatchReport) {
        let Some(a) = agent else { return };
        let done = match a.run.sandboxed() {
            true => a.run.retire(),
            false => a.run.remove(),
        };
        if let Err(e) = done {
            br.notes.push(format!(
                "could not retire the test run in {}: {e}",
                a.run.root.display()
            ));
        }
    }

    /// A career environment: the agent continues the fixture's career to
    /// the place the test names and logs `[test] career ready at <at>`.
    fn env_career(
        &self,
        env: &Env,
        mode: Option<&str>,
        a: &mut Agent,
        run: &mut dyn Running,
    ) -> std::result::Result<(), String> {
        if env.kind != Kind::Career {
            return Ok(());
        }
        let (cmd, args) = career::agent_command(env, mode);
        let slot = args["slot"]
            .as_str()
            .map_or_else(|| args["slot"].to_string(), str::to_owned);
        say(&format!("career load {slot} at {}", career::at(env)));
        let reply = a.call(&cmd, args, self.times.career, &self.times, run)?;
        match reply.outcome {
            Outcome::Ok => {
                say(&format!("career ready at {} ({})", career::at(env), reply.value));
                Ok(())
            }
            _ => Err(reply.text()),
        }
    }

    /// A route environment: fire its routes from the home screen in order.
    fn env_routes(&self, env: &Env, a: &mut Agent, run: &mut dyn Running) -> std::result::Result<(), String> {
        if env.kind != Kind::Route {
            return Ok(());
        }
        let routes: Vec<String> = env
            .params
            .get("routes")
            .and_then(Value::as_array)
            .map(|r| r.iter().filter_map(Value::as_str).map(str::to_owned).collect())
            .unwrap_or_default();
        for r in routes {
            let started = Instant::now();
            let reply = loop {
                let reply = a.call("route", json!({ "route": r }), self.times.step, &self.times, run)?;
                let not_yet =
                    reply.outcome == Outcome::Unavailable && reply.text().contains("no home view model");
                if !not_yet || started.elapsed() >= self.times.step * 6 {
                    break reply;
                }
                std::thread::sleep(self.times.poll.max(Duration::from_millis(500)));
            };
            if reply.outcome != Outcome::Ok {
                return Err(format!("route {r}: {}", reply.text()));
            }
        }
        Ok(())
    }

    fn agent_step(
        &self,
        s: &suite::Step,
        limit: Duration,
        a: &mut Agent,
        run: &mut dyn Running,
    ) -> std::result::Result<String, Failure> {
        let fail =
            |actual: String| agent_failure(s.action.clone(), format!("{} to succeed", s.action), actual);
        let v: Value = serde_json::from_str(&s.text).unwrap_or_default();
        if s.action == "sample" {
            let o = &v["sample"];
            let path = o["read"].as_str().unwrap_or_default().to_owned();
            let name = o["as"].as_str().unwrap_or_default().to_owned();
            let every = Duration::from_millis(o["every_ms"].as_u64().unwrap_or(500).max(10));
            let span =
                Duration::from_secs(o["for_s"].as_u64().unwrap_or(10)).min(limit.max(Duration::from_secs(1)));
            let start = Instant::now();
            let mut got = Vec::new();
            while start.elapsed() < span {
                let r = a
                    .call("read", json!({ "path": path }), self.times.step, &self.times, run)
                    .map_err(fail)?;
                match (r.outcome, agent::number(&r.value)) {
                    (Outcome::Ok, Some(n)) => got.push(n),
                    (Outcome::Ok, None) => {
                        return Err(fail(format!("read {path} is not a number: {}", r.value)));
                    }
                    _ => return Err(fail(format!("read {path}: {}", r.text()))),
                }
                std::thread::sleep(every);
            }
            let n = got.len();
            a.series.insert(name.clone(), got);
            return Ok(format!("ok: {n} samples as {name}"));
        }
        let cmds = agent::step_commands(s).map_err(|e| fail(e.0))?;
        let mut text = Vec::new();
        for (cmd, args) in cmds {
            let r = a.call(&cmd, args, limit, &self.times, run).map_err(fail)?;
            if r.outcome != Outcome::Ok {
                return Err(fail(r.text()));
            }
            if s.action == "read"
                && let Some(name) = v.get("as").and_then(Value::as_str)
            {
                a.values.insert(name.to_owned(), r.value.clone());
            }
            text.push(r.text());
        }
        Ok(text.join("; "))
    }

    /// A `fn` test: the agent runs the test DLL's coroutine and answers with
    /// its expectations.
    fn function(&self, t: &Test, f: &str, r: &mut TestReport, a: &mut Agent, run: &mut dyn Running) {
        let limit = t.timeout_s.map_or(self.times.test, Duration::from_secs);
        let t0 = Instant::now();
        let reply = a.call("run_test", json!({ "fn": f }), limit, &self.times, run);
        let status = match &reply {
            Ok(rep) => rep.text(),
            Err(e) => e.clone(),
        };
        r.steps.push(StepReport {
            step: format!("fn {f}"),
            status: status.clone(),
            ms: Some(ms(t0)),
        });
        let rep = match reply {
            Ok(rep) => rep,
            Err(e) => {
                r.failures
                    .push(agent_failure(format!("fn {f}"), "the test to finish".into(), e));
                return;
            }
        };
        let expects = rep
            .value
            .get("expects")
            .and_then(Value::as_array)
            .cloned()
            .unwrap_or_default();
        for e in expects.iter().filter(|e| e["ok"] == json!(false)) {
            r.failures.push(agent_failure(
                format!("fn {f}"),
                e["what"].as_str().unwrap_or("an expectation").to_owned(),
                "false".into(),
            ));
        }
        if rep.outcome != Outcome::Ok && r.failures.is_empty() {
            r.failures.push(agent_failure(
                format!("fn {f}"),
                "the test to pass".into(),
                rep.text(),
            ));
        }
    }

    fn operand(&self, v: &Value, a: &mut Agent, run: &mut dyn Running) -> std::result::Result<Value, String> {
        let Some(name) = v.as_str() else {
            return Ok(v.clone());
        };
        if let Some(stored) = a.values.get(name) {
            return Ok(stored.clone());
        }
        let r = a.call("read", json!({ "path": name }), self.times.step, &self.times, run)?;
        match r.outcome {
            Outcome::Ok => Ok(r.value),
            _ => Err(format!("read {name}: {}", r.text())),
        }
    }

    /// `value` and `series` checks, on what the steps read or a read now.
    fn agent_check(
        &self,
        text: &str,
        a: &mut Agent,
        run: &mut dyn Running,
    ) -> std::result::Result<(), Failure> {
        let c: Value = serde_json::from_str(text).unwrap_or_default();
        let fail = |expected: String, actual: String| agent_failure(text.to_owned(), expected, actual);
        if let Some(name) = c.get("series").and_then(Value::as_str) {
            let Some(s) = a.series.get(name).filter(|s| !s.is_empty()) else {
                return Err(fail(format!("a sampled series {name}"), "no samples".into()));
            };
            let change = s[s.len() - 1] - s[0];
            if let Some(n) = c.get("rises_by").and_then(Value::as_f64)
                && change < n
            {
                return Err(fail(
                    format!("{name} to rise by {n}"),
                    format!("a change of {change}"),
                ));
            }
            if let Some(n) = c.get("falls_by").and_then(Value::as_f64)
                && -change < n
            {
                return Err(fail(
                    format!("{name} to fall by {n}"),
                    format!("a change of {change}"),
                ));
            }
            return Ok(());
        }
        let Some(subject) = c.get("value") else {
            return Err(fail("a value or series check".into(), "neither".into()));
        };
        let got = self
            .operand(subject, a, run)
            .map_err(|e| fail("a value".into(), e))?;
        let num = agent::number(&got);
        let want_num =
            |key: &str, a: &mut Agent, run: &mut dyn Running| -> std::result::Result<Option<f64>, Failure> {
                match c.get(key) {
                    None => Ok(None),
                    Some(v) => {
                        let v = self.operand(v, a, run).map_err(|e| fail(key.into(), e))?;
                        agent::number(&v)
                            .map(Some)
                            .ok_or_else(|| fail(format!("{key} a number"), v.to_string()))
                    }
                }
            };
        let shown = num.map_or_else(|| got.to_string(), |n| n.to_string());
        if let Some(range) = c.get("between").and_then(Value::as_array)
            && range.len() == 2
        {
            let (lo, hi) = (
                range[0].as_f64().unwrap_or(f64::MIN),
                range[1].as_f64().unwrap_or(f64::MAX),
            );
            if !num.is_some_and(|n| n >= lo && n <= hi) {
                return Err(fail(format!("between {lo} and {hi}"), shown));
            }
        }
        if let Some(n) = want_num("greater_than", a, run)?
            && !num.is_some_and(|x| x > n)
        {
            return Err(fail(format!("greater than {n}"), shown));
        }
        if let Some(n) = want_num("less_than", a, run)?
            && !num.is_some_and(|x| x < n)
        {
            return Err(fail(format!("less than {n}"), shown));
        }
        if let Some(want) = c.get("equals") {
            let want = self.operand(want, a, run).map_err(|e| fail("equals".into(), e))?;
            let same = match (agent::number(&want), num) {
                (Some(x), Some(y)) => x == y,
                _ => strip(&want) == strip(&got),
            };
            if !same {
                return Err(fail(format!("equal to {want}"), shown));
            }
        }
        Ok(())
    }
}

fn strip(v: &Value) -> Value {
    v.get("value").cloned().unwrap_or_else(|| v.clone())
}

fn shell_command(store: &Store, a: ShellArgs) -> Result<ExitCode> {
    let run_id = format!("shell-{}", stamp());
    let g = match crate::build::game(store, a.game.clone()) {
        Ok(g) => g,
        Err(e) => return Ok(infra(format!("{e:#}"))),
    };
    if let Err(e) = terms(store) {
        return Ok(infra(e));
    }
    if !swofty_core::setup::done(&store.root) {
        return Ok(infra(swofty_core::setup::NEEDED));
    }
    let dir = a.lock.clone().unwrap_or_else(|| lock::default_path(&store.root));
    let owner = lock::owner_line(&run_id, &["shell".to_owned()], &hh_mm());
    let lock = match take_lock(&dir, &owner, a.wait) {
        Ok(l) => l,
        Err(Stop::Busy(why)) => {
            eprintln!("swofty: {why}");
            return Ok(ExitCode::from(exit::BUSY));
        }
        Err(Stop::Infra(why)) => return Ok(infra(why)),
    };
    let code = shell_session(store, &g.dir, &a, &run_id);
    lock.release().ok();
    code
}

fn shell_session(store: &Store, game: &Path, a: &ShellArgs, run_id: &str) -> Result<ExitCode> {
    if let Err(Stop::Busy(why) | Stop::Infra(why)) = free_the_game(store, a.take_over, a.wait) {
        return Ok(infra(why));
    }
    let fixture = match &a.fixture {
        None => None,
        Some(spec) => {
            let cwd = std::env::current_dir()?;
            let path = match Path::new(spec).join(fixture::FILE).is_file() {
                true => cwd.join(spec),
                false => match fixture::resolve(spec, Some(&cwd), &fixture::store(&store.root)) {
                    Ok(p) => p,
                    Err(e) => return Ok(infra(e)),
                },
            };
            match fixture::load(&path) {
                Ok(f) => Some(f),
                Err(e) => return Ok(infra(e)),
            }
        }
    };
    let exe_sha = match &fixture {
        None => None,
        Some(_) => {
            let mut hashes = swofty_core::baseline::Hashes::load(&store.root);
            let sha = hashes.sha256(&game.join(swofty_core::GAME_EXE))?;
            hashes.save();
            let loader = swofty_core::builtin::loader_dir();
            match loader.as_deref().map(|l| sandbox::verified(l, &sha)) {
                Some(Ok(proof)) => say(&format!("save sandbox: verified for this F1_25.exe by {proof}")),
                Some(Err(e)) => return Ok(infra(format!("the shell's save sandbox: {e}"))),
                None => {
                    return Ok(infra(
                        "the shell's save sandbox needs the launcher's loader, which was not found",
                    ));
                }
            }
            Some(sha)
        }
    };
    let live_saves = exe_sha
        .as_ref()
        .and_then(|_| swofty_core::steam::find_game())
        .and_then(|i| swofty_core::steam::save_dir(&i.steam_root));
    let launch_json = store.root.join("state/launch.json");
    let saved = std::fs::read(&launch_json).ok();
    let profile = format!("test-{run_id}");
    let out = PathBuf::from("swofty-test").join(run_id);
    std::fs::create_dir_all(&out)?;
    let mut launcher = SwoftyRun {
        data: store.root.clone(),
        game: game.to_path_buf(),
    };
    let h = Harness {
        store,
        game,
        out: &out,
        profile: &profile,
        launcher: &mut launcher,
        times: Times::default(),
        run_id,
        bases: BTreeMap::new(),
        exe_sha,
        live_saves,
    };
    let b = Batch {
        key: "shell".into(),
        env: Env {
            kind: match fixture {
                Some(_) => Kind::Career,
                None => Kind::Boot,
            },
            params: Default::default(),
        },
        addons: a.addons.clone(),
        settings: Default::default(),
        tests: Vec::new(),
    };
    let code = shell_loop(h, &b, &out, fixture);
    if store.profile_names().is_ok_and(|n| n.contains(&profile)) {
        store.delete_profile(&profile).ok();
    }
    match saved {
        Some(bytes) => std::fs::write(&launch_json, bytes).ok(),
        None => std::fs::remove_file(&launch_json).ok(),
    };
    code
}

fn shell_loop(h: Harness, b: &Batch, out: &Path, fixture: Option<Fixture>) -> Result<ExitCode> {
    h.profile(b, h.profile)?;
    let (mut br, mut reports) = Harness::blank(b, 1);
    let mut agent = h.open_run(b, 1, out, fixture.is_some())?;
    let sandbox = match fixture {
        None => None,
        Some(f) => match h.open_sandbox(f, b, h.profile, &agent.run) {
            Ok(s) => Some(s),
            Err(e) => {
                h.close_run(Some(agent), &mut br);
                return Ok(infra(format!("the save sandbox: {e}")));
            }
        },
    };
    let env = agent.run.env();
    say(&format!(
        "starting F1 25 in test mode (run folder {}{})",
        agent.run.root.display(),
        if sandbox.is_some() { ", save sandbox" } else { "" }
    ));
    let mut run = h.launcher.launch(h.profile, &[], &env, out)?;
    let times = Times::default();
    let mut ok = agent
        .hello(times.start, times.poll, &mut *run)
        .map_err(anyhow::Error::msg);
    if let Err(e) = &ok {
        eprintln!("swofty: {e}");
    } else {
        eprintln!(
            "type a command (title, route <name>, read <path>, wait <event> [s], pad <button>, capture [name], career load <slot|newest> [at <place>], save now, quit); end with Ctrl-D"
        );
    }
    let stdin = std::io::stdin();
    let mut lines = stdin.lock().lines();
    while ok.is_ok() && !run.finished() {
        eprint!("> ");
        let Some(Ok(line)) = lines.next() else { break };
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        if matches!(line, "exit" | "quit") {
            break;
        }
        let (cmd, args, timeout) = match agent::parse_shell(line) {
            Ok(c) => c,
            Err(e) => {
                eprintln!("{e}");
                continue;
            }
        };
        let limit = timeout.map_or(
            match cmd.as_str() {
                "career" => times.career,
                _ => Duration::from_secs(60),
            },
            Duration::from_millis,
        );
        let before = agent.client.events.len();
        match agent.call(&cmd, args, limit, &times, &mut *run) {
            Ok(r) => println!("{}", r.text()),
            Err(e) => println!("{e}"),
        }
        for (name, v) in &agent.client.events[before..] {
            println!("  [{name}] {v}");
        }
        ok = Ok(());
    }
    if agent.hello {
        agent.client.send("quit", Value::Null, None).ok();
        until_polled(times.quit, times.poll, || run.finished());
    }
    h.finish(&mut *run).ok();
    agent.client.poll();
    agent.keep().ok();
    br.started_game = true;
    let sandboxed = sandbox.is_some();
    if let Some(s) = sandbox {
        h.close_sandbox(s, Some(&agent.run), b, h.profile, &mut br, &mut reports, &[]);
    }
    let id = agent.run.id.clone();
    h.close_run(Some(agent), &mut br);
    for n in br.error.iter().chain(&br.notes) {
        eprintln!("swofty: {n}");
    }
    if sandboxed {
        eprintln!("capture its saves with: swofty test fixture capture <name> --run {id}");
    }
    eprintln!("results: {}", out.display());
    Ok(match br.error {
        Some(_) => ExitCode::from(exit::INFRA),
        None => ExitCode::SUCCESS,
    })
}

/// The game mode of the career a career environment continues, from the
/// fixture's `holds`: the career in the test's slot, else the only one.
fn career_mode(f: &Fixture, slot: Option<u64>) -> Option<String> {
    let careers = &f.manifest.holds.careers;
    let c = match slot {
        Some(n) => careers.iter().find(|c| c.slot == Some(n)),
        None if careers.len() == 1 => careers.first(),
        None => None,
    }?;
    let m = c.mode.to_ascii_lowercase();
    if m.contains("my_team") || m.contains("myteam") {
        Some("my_team_career".into())
    } else if m.contains("driver") {
        Some("driver_career".into())
    } else {
        None
    }
}

fn copy_tree(from: &Path, to: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(to)?;
    for e in std::fs::read_dir(from)? {
        let e = e?;
        let (src, dst) = (e.path(), to.join(e.file_name()));
        if e.file_type()?.is_dir() {
            copy_tree(&src, &dst)?;
        } else {
            std::fs::copy(&src, &dst)?;
        }
    }
    Ok(())
}

fn until_polled(limit: Duration, poll: Duration, mut done: impl FnMut() -> bool) -> bool {
    let start = Instant::now();
    loop {
        if done() {
            return true;
        }
        if start.elapsed() >= limit {
            return false;
        }
        std::thread::sleep(poll);
    }
}

fn store_dir(store: &Store, a: &StoreArgs) -> PathBuf {
    a.store.clone().unwrap_or_else(|| fixture::store(&store.root))
}

fn fixture_command(store: &Store, cmd: FixtureCommand) -> Result<ExitCode> {
    match cmd {
        FixtureCommand::Capture(a) => capture_command(store, a),
        FixtureCommand::List(a) => {
            let dir = store_dir(store, &a);
            let all = fixture::list(&dir);
            if all.is_empty() {
                println!("no fixtures in {}", dir.display());
            }
            let mut bad = false;
            for f in all {
                match f {
                    Ok(f) => println!(
                        "{:<32} {} file(s), exe {}, {}",
                        f.manifest.name,
                        f.manifest.files.len(),
                        &f.manifest.exe.sha256[..f.manifest.exe.sha256.len().min(16)],
                        f.manifest.description
                    ),
                    Err(e) => {
                        bad = true;
                        println!("BROKEN: {e}");
                    }
                }
            }
            Ok(if bad { ExitCode::FAILURE } else { ExitCode::SUCCESS })
        }
        FixtureCommand::Check(a) => {
            let dir = store_dir(store, &a.store);
            let path = match Path::new(&a.fixture).join(fixture::FILE).is_file() {
                true => PathBuf::from(&a.fixture),
                false => match fixture::resolve(&a.fixture, None, &dir) {
                    Ok(p) => p,
                    Err(e) => return Ok(infra(e)),
                },
            };
            match fixture::load(&path) {
                Ok(f) => {
                    println!(
                        "{}: {} file(s) match fixture.json ({})",
                        f.manifest.name,
                        f.manifest.files.len(),
                        path.display()
                    );
                    Ok(ExitCode::SUCCESS)
                }
                Err(e) => Ok(infra(e)),
            }
        }
    }
}

fn careers(specs: &[String]) -> std::result::Result<Vec<fixture::Career>, String> {
    specs
        .iter()
        .map(|c| {
            let (mode, slot) = match c.split_once(':') {
                Some((m, s)) => (
                    m,
                    Some(
                        s.parse::<u64>()
                            .map_err(|_| format!("--career {c}: the slot is not a number"))?,
                    ),
                ),
                None => (c.as_str(), None),
            };
            Ok(fixture::Career {
                mode: mode.to_owned(),
                slot,
                ..Default::default()
            })
        })
        .collect()
}

/// The saves under a folder given to `--from`: the folder itself when it
/// holds the profile save, else its `remote/`.
fn remote_in(dir: &Path) -> PathBuf {
    match dir.join(fixture::PROFILE_FILES[0]).is_file() {
        true => dir.to_path_buf(),
        false => dir.join(sandbox::REMOTE),
    }
}

fn capture_command(store: &Store, a: CaptureArgs) -> Result<ExitCode> {
    let dest = store_dir(store, &a.store);
    let mut addons: BTreeMap<String, String> = BTreeMap::new();
    let mut exe = fixture::Exe::default();
    let (remote, storage, source) = match &a.from {
        Some(dir) => {
            if let Some(live) =
                swofty_core::steam::find_game().and_then(|i| swofty_core::steam::save_dir(&i.steam_root))
                && let (Ok(x), Ok(y)) = (dir.canonicalize(), live.canonicalize())
                && (x == y || y.starts_with(&x))
            {
                return Ok(infra(format!(
                    "{} is the player's live Steam save folder; capture from a copy of it, never the live folder",
                    dir.display()
                )));
            }
            (remote_in(dir), Vec::new(), format!("folder {}", dir.display()))
        }
        None => {
            let run = match &a.run {
                Some(id) => RunDir::open(&store.root, id),
                None => RunDir::latest(&store.root),
            };
            let Some(run) = run else {
                return Ok(infra(format!(
                    "no test run with saves in {}; name one with --run or a save folder with --from",
                    sandbox::runs_dir(&store.root).display()
                )));
            };
            if let Ok(b) = std::fs::read(run.root.join("addons.json"))
                && let Ok(m) = serde_json::from_slice::<BTreeMap<String, String>>(&b)
            {
                addons = m;
            }
            if let Ok(b) = std::fs::read(run.root.join(sandbox::RUN_FILE))
                && let Ok(v) = serde_json::from_slice::<serde_json::Value>(&b)
                && let Some(sha) = v["exe_sha256"].as_str()
            {
                exe.sha256 = sha.to_owned();
            }
            let storage: Vec<(String, PathBuf)> = std::fs::read_dir(run.storage())
                .into_iter()
                .flatten()
                .flatten()
                .map(|e| (e.file_name().to_string_lossy().into_owned(), e.path()))
                .collect();
            (run.remote(), storage, format!("test run {}", run.id))
        }
    };
    for spec in &a.addons {
        let Some((id, v)) = spec.split_once('=') else {
            return Ok(infra(format!("--addon {spec}: expected ID=VERSION")));
        };
        addons.insert(id.to_owned(), v.to_owned());
    }
    if exe.sha256.is_empty()
        && let Ok(g) = crate::build::game(store, None)
    {
        let mut hashes = swofty_core::baseline::Hashes::load(&store.root);
        if let Ok(sha) = hashes.sha256(&g.dir.join(swofty_core::GAME_EXE)) {
            exe.sha256 = sha;
            exe.version = swofty_core::exe_version::read(&g.dir.join(swofty_core::GAME_EXE));
        }
        hashes.save();
    }
    let holds = fixture::Holds {
        careers: match careers(&a.careers) {
            Ok(c) => c,
            Err(e) => return Ok(infra(e)),
        },
        notes: a.notes.clone().unwrap_or_default(),
    };
    let meta = fixture::Meta {
        description: a.description.clone().unwrap_or_default(),
        created: stamp(),
        exe,
        addons,
        holds,
        source,
    };
    if let Err(e) = std::fs::create_dir_all(&dest) {
        return Ok(infra(format!("{}: {e}", dest.display())));
    }
    match fixture::capture(&a.name, &dest, &remote, &storage, meta) {
        Ok(f) => {
            println!(
                "fixture {} captured: {} file(s) in {}",
                f.manifest.name,
                f.manifest.files.len(),
                f.dir.display()
            );
            for rel in f.manifest.files.keys() {
                println!("  {rel}");
            }
            Ok(ExitCode::SUCCESS)
        }
        Err(e) => Ok(infra(e)),
    }
}

#[cfg(test)]
mod tests;
