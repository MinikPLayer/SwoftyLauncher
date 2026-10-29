use std::collections::{BTreeSet, HashMap};
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::mpsc::{Receiver, RecvTimeoutError, channel};
use std::sync::{Arc, Mutex, MutexGuard, TryLockError};
use std::time::Duration;

use serde::Serialize;
use serde::de::DeserializeOwned;
use serde_json::Value;
use swofty_addon::asset::{AssetBatch, Op, Origin};
use swofty_addon::helper::{
    ADDON_CALLS, API_VERSION, Built, Call, Hello, HostCall, Incoming, MOVE_ADDON, PROFILE_HEADS, RESTART,
    Reply, Request, TASKS, Welcome,
};
use swofty_addon::launcher::{
    Addon, BuildCx, Claim, CommandSpec, Cx, Importer, ProfileInfo, Progress, Summary,
};
use swofty_addon::manifest::{AddonManifest, Launcher};
use swofty_addon::view::{Body, Button, Effect, Event, Notice, SectionSpec, Style, Tone, View};

use crate::app_control::Block;

type Result<T> = swofty_addon::Result<T>;

pub const LOG_FILE: &str = "helper.log";

#[derive(Debug, Clone, Copy)]
pub struct Timeouts {
    pub quick: Duration,
    pub long: Duration,
}

impl Default for Timeouts {
    fn default() -> Self {
        Self {
            quick: Duration::from_secs(5),
            long: Duration::from_secs(300),
        }
    }
}

pub struct HelperAddon {
    manifest: AddonManifest,
    spec: Launcher,
    dir: PathBuf,
    timeouts: Timeouts,
    run: Mutex<Run>,
    summaries: Mutex<HashMap<String, Option<Summary>>>,
    stale: Arc<Mutex<Option<Stale>>>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Stale {
    All,
    Sections(BTreeSet<String>),
}

impl Stale {
    pub fn covers(&self, section: &str) -> bool {
        match self {
            Self::All => true,
            Self::Sections(s) => s.contains(section),
        }
    }

    fn add(slot: &mut Option<Stale>, section: Option<String>) {
        *slot = match (slot.take(), section) {
            (Some(Self::All), _) | (_, None) => Some(Self::All),
            (Some(Self::Sections(mut s)), Some(section)) => {
                s.insert(section);
                Some(Self::Sections(s))
            }
            (None, Some(section)) => Some(Self::Sections(BTreeSet::from([section]))),
        };
    }
}

// An invalidate notification may arrive at any time, between calls too, so the
// reader thread takes it out of the line stream before any exchange sees it.
fn invalidation(line: &str) -> Option<Option<String>> {
    if !line.contains("invalidate") {
        return None;
    }
    let message: Value = serde_json::from_str(line).ok()?;
    if message.get("id").is_some() || message["method"] != "invalidate" {
        return None;
    }
    Some(message["params"]["section"].as_str().map(str::to_owned))
}

enum Run {
    Idle,
    Live(Live),
    Dead(String),
}

struct Live {
    child: Child,
    stdin: ChildStdin,
    lines: Receiver<String>,
    next: u64,
    profile: ProfileInfo,
}

impl Drop for Live {
    fn drop(&mut self) {
        self.child.kill().ok();
        self.child.wait().ok();
    }
}

impl HelperAddon {
    pub fn new(manifest: AddonManifest, dir: impl Into<PathBuf>) -> Option<Self> {
        let spec = manifest.launcher.clone()?;
        Some(Self {
            manifest,
            spec,
            dir: dir.into(),
            timeouts: Timeouts::default(),
            run: Mutex::new(Run::Idle),
            summaries: Mutex::new(HashMap::new()),
            stale: Arc::default(),
        })
    }

    pub fn with_timeouts(mut self, timeouts: Timeouts) -> Self {
        self.timeouts = timeouts;
        self
    }

    pub fn dir(&self) -> &Path {
        &self.dir
    }

    pub fn running(&self) -> bool {
        matches!(*self.lock(), Run::Live(_))
    }

    pub fn failure(&self) -> Option<String> {
        match &*self.lock() {
            Run::Dead(e) => Some(e.clone()),
            _ => None,
        }
    }

    pub fn stop(&self) {
        *self.lock() = Run::Idle;
    }

    /// The section's view, or None while another call holds the plugin, such as an
    /// import or a build, instead of waiting for it to finish.
    pub fn try_view(&self, cx: &Cx, section: &str) -> Option<Result<View>> {
        let run = match self.run.try_lock() {
            Ok(run) => run,
            Err(TryLockError::Poisoned(e)) => e.into_inner(),
            Err(TryLockError::WouldBlock) => return None,
        };
        if let Run::Dead(e) = &*run {
            return Some(Ok(self.dead_view(e.clone())));
        }
        let call = Call::View {
            section: section.into(),
        };
        Some(
            self.call_with(run, cx, call, self.timeouts.quick, None)
                .or_else(|e| self.failure().map(|e| self.dead_view(e)).ok_or(e)),
        )
    }

    /// Sends a tick to a running, idle plugin. None when it isn't running or another
    /// call holds it; a tick never starts the plugin or waits for it.
    pub fn try_tick(&self, cx: &Cx, section: &str) -> Option<Result<()>> {
        let run = match self.run.try_lock() {
            Ok(run) => run,
            Err(TryLockError::Poisoned(e)) => e.into_inner(),
            Err(TryLockError::WouldBlock) => return None,
        };
        if !matches!(*run, Run::Live(_)) {
            return None;
        }
        let call = Call::Tick {
            section: section.into(),
        };
        Some(
            self.call_with::<Value>(run, cx, call, self.timeouts.quick, None)
                .map(drop),
        )
    }

    pub fn busy(&self) -> bool {
        matches!(self.run.try_lock(), Err(TryLockError::WouldBlock))
    }

    pub fn take_stale(&self) -> Option<Stale> {
        self.stale.lock().unwrap_or_else(|e| e.into_inner()).take()
    }

    pub fn mark_stale(&self, section: Option<&str>) {
        let mut stale = self.stale.lock().unwrap_or_else(|e| e.into_inner());
        Stale::add(&mut stale, section.map(str::to_owned));
    }

    fn lock(&self) -> MutexGuard<'_, Run> {
        self.run.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn name(&self) -> &str {
        &self.manifest.name
    }

    fn call<T: DeserializeOwned>(
        &self,
        cx: &Cx,
        call: Call,
        wait: Duration,
        progress: Option<&dyn Progress>,
    ) -> Result<T> {
        self.call_with(self.lock(), cx, call, wait, progress)
    }

    fn call_with<T: DeserializeOwned>(
        &self,
        mut run: MutexGuard<'_, Run>,
        cx: &Cx,
        call: Call,
        wait: Duration,
        progress: Option<&dyn Progress>,
    ) -> Result<T> {
        if let Run::Dead(e) = &*run {
            return Err(swofty_addon::Error::Other(e.clone()));
        }
        if matches!(*run, Run::Idle) {
            *run = Run::Live(self.start(cx).map_err(|e| self.fail(&mut run, e))?);
        }
        let Run::Live(live) = &mut *run else {
            unreachable!("started above");
        };
        let exchange = self.exchange(live, cx, call, wait, progress);
        match exchange {
            Ok(Ok(value)) => serde_json::from_value(value)
                .map_err(|e| self.fail(&mut run, format!("sent a bad result: {e}"))),
            Ok(Err(e)) => Err(swofty_addon::Error::Other(format!("{}: {e}", self.name()))),
            Err(e) => Err(self.fail(&mut run, e)),
        }
    }

    fn fail(&self, run: &mut Run, e: String) -> swofty_addon::Error {
        let e = format!("{} {e}", self.name());
        *run = Run::Dead(e.clone());
        swofty_addon::Error::Other(e)
    }

    // Runs `<program> claims <path>` on its own, apart from the live session, since
    // a drop is routed before any profile is at hand. None when it fails or stalls,
    // and the extensions in imports decide instead.
    fn ask_claims(&self, path: &Path) -> Option<Claim> {
        let program = self.dir.join(self.spec.program()?);
        executable(&program).ok()?;
        let mut command = Command::new(&program);
        command
            .arg("claims")
            .arg(path)
            .current_dir(&self.dir)
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::null());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x0800_0000);
        }
        let mut child = command.spawn().ok()?;
        let stdout = child.stdout.take()?;
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let mut line = String::new();
            BufReader::new(stdout).read_line(&mut line).ok();
            tx.send(line).ok();
        });
        let line = rx.recv_timeout(self.timeouts.quick);
        if line.is_err() {
            child.kill().ok();
        }
        child.wait().ok();
        match line.ok()?.trim() {
            "yes" => Some(Claim::Yes),
            "maybe" => Some(Claim::Maybe),
            "no" => Some(Claim::No),
            _ => None,
        }
    }

    fn removed(&self, rel: &str) -> String {
        let then = match crate::managed::is_managed(&self.manifest.id) {
            true => "press Play again to download it again".to_owned(),
            false => format!("reinstall {}", self.name()),
        };
        format!(
            "could not start: its program {rel} is missing, most likely removed by an antivirus. Allow {} in your \
             antivirus, then {then}",
            self.dir.display()
        )
    }

    fn start(&self, cx: &Cx) -> std::result::Result<Live, String> {
        let rel = self
            .spec
            .program()
            .ok_or_else(|| format!("has no helper for {}", std::env::consts::OS))?;
        let program = self.dir.join(rel);
        executable(&program).map_err(|e| match e.kind() {
            std::io::ErrorKind::NotFound if self.dir.is_dir() => self.removed(rel),
            _ => format!("could not start {}: {e}", program.display()),
        })?;
        let log = std::fs::File::create(cx.cache_dir.join(LOG_FILE)).map(Stdio::from);
        let mut command = Command::new(&program);
        command
            .current_dir(&self.dir)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(log.unwrap_or_else(|_| Stdio::null()));
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x0800_0000);
        }
        let mut child = command.spawn().map_err(|e| match Block::of(&program, &e) {
            Some(b) => format!("could not start: {}", b.text()),
            None => format!("could not start {}: {e}", program.display()),
        })?;
        let (stdin, stdout) = match (child.stdin.take(), child.stdout.take()) {
            (Some(i), Some(o)) => (i, o),
            _ => return Err("has no pipes".into()),
        };
        let (tx, lines) = channel();
        let stale = self.stale.clone();
        std::thread::spawn(move || {
            for line in BufReader::new(stdout).lines() {
                let Ok(line) = line else { break };
                if let Some(section) = invalidation(&line) {
                    Stale::add(&mut stale.lock().unwrap_or_else(|e| e.into_inner()), section);
                    continue;
                }
                if tx.send(line).is_err() {
                    break;
                }
            }
        });
        let mut live = Live {
            child,
            stdin,
            lines,
            next: 0,
            profile: cx.profile.clone(),
        };
        let hello = Call::Hello(Hello {
            api: API_VERSION,
            addon: cx.addon.to_owned(),
            data_dir: cx.data_dir.to_path_buf(),
            cache_dir: cx.cache_dir.to_path_buf(),
            profile: cx.profile.clone(),
            launcher: Some(env!("CARGO_PKG_VERSION").into()),
            host_calls: host_calls(cx),
        });
        let welcome = self
            .exchange(&mut live, cx, hello, self.timeouts.quick, None)?
            .map_err(|e| format!("refused hello: {e}"))?;
        let welcome: Welcome =
            serde_json::from_value(welcome).map_err(|e| format!("sent a bad hello: {e}"))?;
        if welcome.api != API_VERSION {
            return Err(format!(
                "speaks launcher API {}; this launcher speaks {API_VERSION}",
                welcome.api
            ));
        }
        Ok(live)
    }

    fn exchange(
        &self,
        live: &mut Live,
        cx: &Cx,
        call: Call,
        wait: Duration,
        progress: Option<&dyn Progress>,
    ) -> std::result::Result<std::result::Result<Value, String>, String> {
        if live.profile != *cx.profile && !matches!(call, Call::Hello(_)) {
            live.profile = cx.profile.clone();
            send(
                live,
                &Request {
                    id: None,
                    call: Call::Profile(cx.profile.clone()),
                },
            )?;
        }
        let id = live.next;
        live.next += 1;
        send(live, &Request { id: Some(id), call })?;
        loop {
            let line = match live.lines.recv_timeout(wait) {
                Ok(line) => line,
                Err(RecvTimeoutError::Timeout) => {
                    return Err(format!("did not answer within {} s", wait.as_secs_f32()));
                }
                Err(RecvTimeoutError::Disconnected) => {
                    let status = live
                        .child
                        .wait()
                        .map_or_else(|e| e.to_string(), |s| s.to_string());
                    return Err(format!("stopped ({status})"));
                }
            };
            if line.trim().is_empty() {
                continue;
            }
            let message: Incoming =
                serde_json::from_str(&line).map_err(|e| format!("sent a bad message: {e}"))?;
            match message {
                Incoming::Reply(Reply::Ok { id: got, result }) if got == id => return Ok(Ok(result)),
                Incoming::Reply(Reply::Err { id: got, error }) if got == id => return Ok(Err(error)),
                Incoming::Reply(_) => return Err("answered a request it was not sent".into()),
                Incoming::Call(Request {
                    id: None,
                    call: HostCall::Progress { done, text },
                }) => {
                    if let Some(p) = progress {
                        p.report(done, &text);
                    }
                }
                Incoming::Call(Request {
                    id: None,
                    call: HostCall::Tasks(list),
                }) => {
                    cx.tasks(&list);
                }
                Incoming::Call(Request {
                    id: None,
                    call: HostCall::Invalidate(i),
                }) => Stale::add(
                    &mut self.stale.lock().unwrap_or_else(|e| e.into_inner()),
                    i.section,
                ),
                Incoming::Call(Request { id: Some(host), call }) => {
                    let reply = match host_call(cx, call) {
                        Ok(result) => Reply::Ok { id: host, result },
                        Err(error) => Reply::Err { id: host, error },
                    };
                    send(live, &reply)?;
                }
                Incoming::Call(Request { id: None, .. }) => {
                    return Err("sent a host call without an id".into());
                }
            }
        }
    }

    fn dead_view(&self, error: String) -> View {
        let mut restart = Button::new(RESTART, "Restart");
        restart.style = Style::Primary;
        View {
            subtitle: String::new(),
            actions: Vec::new(),
            notices: vec![Notice {
                text: error,
                tone: Tone::Bad,
                action: Some(restart),
            }],
            body: Body::Empty {
                title: format!("{} is not running", self.name()),
                text: "Restart it to load this page.".into(),
                action: None,
            },
            footer: None,
            tick: false,
        }
    }
}

fn to_value(v: impl Serialize) -> Result<Value> {
    serde_json::to_value(v).map_err(|e| swofty_addon::Error::Other(e.to_string()))
}

// Tasks is listed always, since a helper keeps its hello across calls; lists sent
// where nothing shows them are dropped.
fn host_calls(cx: &Cx) -> Vec<String> {
    let mut calls: Vec<&str> = match cx.addons() {
        Some(_) => ADDON_CALLS.into_iter().chain([MOVE_ADDON]).collect(),
        None => Vec::new(),
    };
    calls.push(TASKS);
    if cx.game.is_some() {
        calls.push(PROFILE_HEADS);
    }
    calls.into_iter().map(String::from).collect()
}

fn host_call(cx: &Cx, call: HostCall) -> std::result::Result<Value, String> {
    let game = || {
        cx.game
            .ok_or_else(|| swofty_addon::Error::Other("the game is not available".into()))
    };
    let addons = || {
        cx.addons()
            .ok_or_else(|| swofty_addon::Error::Other("addons can't be managed from here".into()))
    };
    match call {
        HostCall::GetState => cx.state::<Option<Value>>().map(|v| v.unwrap_or(Value::Null)),
        HostCall::SetState(value) => cx.set_state(&value).map(|()| Value::Null),
        HostCall::GameBuildKey => Ok(cx.game.map_or(Value::Null, |g| Value::String(g.build_key()))),
        HostCall::GameExeVersion => Ok(cx
            .game
            .and_then(|g| g.exe_version())
            .map_or(Value::Null, Value::String)),
        HostCall::GameIsStock { rel } => Ok(Value::Bool(cx.game.is_some_and(|g| g.is_stock(&rel)))),
        HostCall::GameOriginal { rel } => game()
            .and_then(|g| g.original(&rel))
            .map(|p| Value::String(p.to_string_lossy().into_owned())),
        HostCall::GameOriginalLang { code } => game()
            .and_then(|g| g.original_lang(&code))
            .map(|p| Value::String(p.to_string_lossy().into_owned())),
        HostCall::GameDir => Ok(path_value(cx.game.and_then(|g| g.dir()))),
        HostCall::AddonGameDir => match cx.game {
            Some(g) => g.addon_dir(cx.addon).map(path_value),
            None => Ok(Value::Null),
        },
        HostCall::RegistryDir => Ok(path_value(Some(crate::stdlib::reports::dir(cx.data_dir)))),
        HostCall::GameStatus => {
            let status = cx
                .game
                .map_or_else(|| crate::process::game_status(None), |g| g.status());
            serde_json::to_value(status).map_err(|e| swofty_addon::Error::Other(e.to_string()))
        }
        HostCall::SteamSaveDir => Ok(path_value(cx.game.and_then(|g| g.steam_save_dir()))),
        HostCall::ProfileTracks => match cx.game {
            Some(g) => g
                .profile_tracks(&cx.profile.name)
                .and_then(|t| serde_json::to_value(t).map_err(|e| swofty_addon::Error::Other(e.to_string()))),
            None => Ok(Value::Null),
        },
        HostCall::ProfileHeads => match cx.game {
            Some(g) => g.profile_heads(&cx.profile.name).and_then(to_value),
            None => Ok(Value::Null),
        },
        HostCall::Addons => addons().and_then(|a| a.list()).and_then(to_value),
        HostCall::InstallAddon { path } => addons().and_then(|a| a.install(&path)).and_then(to_value),
        HostCall::SetAddonEnabled { id, on } => addons()
            .and_then(|a| a.set_enabled(&id, on))
            .map(|()| Value::Null),
        HostCall::RemoveAddon { id } if id == cx.addon => {
            Err(swofty_addon::Error::Other(format!("{id} can't remove itself")))
        }
        HostCall::RemoveAddon { id } => addons().and_then(|a| a.remove(&id)).map(|()| Value::Null),
        HostCall::MoveAddon { id, delta } => addons()
            .and_then(|a| a.move_addon(&id, delta))
            .map(|()| Value::Null),
        HostCall::Progress { .. } => Err(swofty_addon::Error::Other("progress takes no id".into())),
        HostCall::Tasks(_) => Err(swofty_addon::Error::Other("tasks takes no id".into())),
        HostCall::Invalidate(_) => Err(swofty_addon::Error::Other("invalidate takes no id".into())),
    }
    .map_err(|e| e.to_string())
}

fn path_value(path: Option<PathBuf>) -> Value {
    path.map_or(Value::Null, |p| Value::String(p.to_string_lossy().into_owned()))
}

fn send(live: &mut Live, message: &impl Serialize) -> std::result::Result<(), String> {
    let mut line = serde_json::to_vec(message).map_err(|e| e.to_string())?;
    line.push(b'\n');
    live.stdin
        .write_all(&line)
        .and_then(|()| live.stdin.flush())
        .map_err(|e| format!("stopped reading ({e})"))
}

#[cfg(unix)]
fn executable(path: &Path) -> std::io::Result<()> {
    use std::os::unix::fs::PermissionsExt;
    let mut perms = std::fs::metadata(path)?.permissions();
    if perms.mode() & 0o111 == 0 {
        perms.set_mode(perms.mode() | 0o755);
        std::fs::set_permissions(path, perms)?;
    }
    Ok(())
}

#[cfg(not(unix))]
fn executable(path: &Path) -> std::io::Result<()> {
    std::fs::metadata(path).map(|_| ())
}

impl Addon for HelperAddon {
    fn manifest(&self) -> &AddonManifest {
        &self.manifest
    }

    fn sections(&self) -> Vec<SectionSpec> {
        self.spec.sections.clone()
    }

    fn view(&self, cx: &Cx, section: &str) -> Result<View> {
        if let Some(e) = self.failure() {
            return Ok(self.dead_view(e));
        }
        let call = Call::View {
            section: section.into(),
        };
        self.call(cx, call, self.timeouts.quick, None)
            .or_else(|e| self.failure().map(|e| self.dead_view(e)).ok_or(e))
    }

    fn event(&self, cx: &Cx, section: &str, event: Event) -> Result<Effect> {
        if matches!(&event, Event::Click { id } if id == RESTART) {
            self.stop();
            return Ok(Effect::Reload);
        }
        let call = Call::Event {
            section: section.into(),
            event,
        };
        self.call(cx, call, self.timeouts.long, None)
    }

    fn importer(&self) -> Option<&dyn Importer> {
        (self.spec.claims || !self.spec.imports.is_empty()).then_some(self as &dyn Importer)
    }

    fn build(&self, cx: &BuildCx) -> Result<AssetBatch> {
        if !self.spec.build {
            return Ok(AssetBatch::default());
        }
        let call = Call::Build {
            options: cx.options.clone(),
            features: cx.features.clone(),
        };
        let built: Built = self.call(&cx.cx, call, self.timeouts.long, None)?;
        let mut items = built.items.into_iter();
        Ok(AssetBatch {
            ops: built
                .ops
                .into_iter()
                .map(|op| Op {
                    op,
                    origin: Origin {
                        addon: self.manifest.id.clone(),
                        item: items.next().flatten(),
                    },
                })
                .collect(),
            lines: built.lines,
            warnings: built.warnings,
            fields: built.fields,
        })
    }

    fn commands(&self) -> Vec<CommandSpec> {
        self.spec.commands.clone()
    }

    fn command(&self, cx: &Cx, path: &[String], args: &[String]) -> Result<String> {
        let call = Call::Command {
            path: path.to_vec(),
            args: args.to_vec(),
        };
        self.call(cx, call, self.timeouts.long, None)
    }

    fn summary(&self, cx: &Cx) -> Option<Summary> {
        let mut last = self.summaries.lock().unwrap_or_else(|e| e.into_inner());
        let run = match self.run.try_lock() {
            Ok(run) => run,
            Err(TryLockError::Poisoned(e)) => e.into_inner(),
            Err(TryLockError::WouldBlock) => return last.get(&cx.profile.name).cloned().flatten(),
        };
        if !matches!(*run, Run::Live(_)) {
            last.clear();
            return None;
        }
        let summary = self
            .call_with(run, cx, Call::Summary, self.timeouts.quick, None)
            .ok()
            .flatten();
        last.insert(cx.profile.name.clone(), summary.clone());
        summary
    }
}

impl Importer for HelperAddon {
    fn claims(&self, path: &Path) -> Claim {
        if self.spec.claims
            && let Some(claim) = self.ask_claims(path)
        {
            return claim;
        }
        let ext = path.extension().and_then(|e| e.to_str()).unwrap_or_default();
        if path.is_file() && self.spec.imports.iter().any(|i| i.eq_ignore_ascii_case(ext)) {
            Claim::Yes
        } else {
            Claim::No
        }
    }

    fn import(&self, cx: &Cx, paths: &[PathBuf], progress: &dyn Progress) -> Result<Vec<String>> {
        let call = Call::Import {
            paths: paths.to_vec(),
        };
        self.call(cx, call, self.timeouts.long, Some(progress))
    }
}

#[derive(Default)]
pub struct Helpers(Mutex<Vec<Arc<HelperAddon>>>);

impl std::fmt::Debug for Helpers {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("Helpers")
    }
}

impl Helpers {
    pub fn get(&self, manifest: &AddonManifest, dir: &Path) -> Option<Arc<HelperAddon>> {
        let mut all = self.0.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(h) = all.iter().find(|h| h.manifest == *manifest && h.dir == dir) {
            return Some(h.clone());
        }
        let helper = Arc::new(HelperAddon::new(manifest.clone(), dir)?);
        all.retain(|h| h.manifest.id != manifest.id);
        all.push(helper.clone());
        Some(helper)
    }

    pub fn forget(&self, id: &str) {
        let mut all = self.0.lock().unwrap_or_else(|e| e.into_inner());
        for h in all.iter().filter(|h| h.manifest.id == id) {
            h.stop();
        }
        all.retain(|h| h.manifest.id != id);
    }
}

#[cfg(all(test, unix))]
mod tests {
    use std::collections::BTreeMap;
    use std::sync::Mutex;

    use swofty_addon::asset::{AssetOp, Features};
    use swofty_addon::launcher::{GameFiles, GameStatus, StateStore};

    use super::*;
    use crate::import::{Route, route};
    use crate::store::Store;

    const MANIFEST: &str = r#"{"schema_version":1,"id":"t.helper","name":"Helper","version":"1.0.0","author":"t",
        "compat":{"game":"f1_25"},
        "launcher":{"program":{"linux":"bin/helper","macos":"bin/helper"},
            "sections":[{"id":"items","title":"Items","icon":"package"}],"imports":["exmod"],"build":true}}"#;

    #[derive(Default)]
    struct Memory(Mutex<BTreeMap<String, Value>>);

    impl StateStore for Memory {
        fn get(&self, addon: &str) -> Result<Option<Value>> {
            Ok(self.0.lock().unwrap().get(addon).cloned())
        }

        fn set(&self, addon: &str, value: Value) -> Result<()> {
            self.0.lock().unwrap().insert(addon.into(), value);
            Ok(())
        }
    }

    #[derive(Default)]
    struct Seen(Mutex<Vec<(f32, String)>>);

    impl Progress for Seen {
        fn report(&self, done: f32, text: &str) {
            self.0.lock().unwrap().push((done, text.into()));
        }
    }

    fn tmp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-helper-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        dir
    }

    // The script is written by a child process: a write fd open in this
    // process leaks into whatever another test spawns at that moment, and
    // exec then fails with "Text file busy" until that child has exec'd.
    fn addon(dir: &Path, script: &str) {
        std::fs::create_dir_all(dir.join("bin")).unwrap();
        std::fs::write(dir.join("swofty.json"), MANIFEST).unwrap();
        let mut cat = Command::new("sh")
            .args(["-c", r#"cat > "$0""#])
            .arg(dir.join("bin/helper"))
            .stdin(Stdio::piped())
            .spawn()
            .unwrap();
        let mut stdin = cat.stdin.take().unwrap();
        write!(stdin, "#!/bin/sh\n{script}").unwrap();
        drop(stdin);
        assert!(cat.wait().unwrap().success());
    }

    // Long enough that only a hung helper reaches it, however loaded the
    // machine running the tests is.
    const PATIENT: Duration = Duration::from_secs(60);
    const HUNG: Duration = Duration::from_millis(500);

    fn helper(dir: &Path) -> HelperAddon {
        helper_waiting(dir, PATIENT)
    }

    fn helper_waiting(dir: &Path, wait: Duration) -> HelperAddon {
        let manifest = AddonManifest::load(dir).unwrap();
        HelperAddon::new(manifest, dir).unwrap().with_timeouts(Timeouts {
            quick: wait,
            long: wait,
        })
    }

    fn profile(name: &str) -> ProfileInfo {
        ProfileInfo {
            name: name.into(),
            vanilla: false,
            enabled: true,
        }
    }

    const ROUND_TRIP: &str = r#"
answer() { read -r l; echo "$l" >> received; echo "$1"; }
answer '{"id":0,"result":{"api":1}}'
answer '{"id":9,"method":"get_state"}'
answer '{"id":10,"method":"set_state","params":["b"]}'
answer '{"id":1,"result":{"subtitle":"s","body":{"blocks":[]}}}'
answer '{"method":"progress","params":{"done":0.5,"text":"half"}}'
echo '{"id":2,"result":["imported a.exmod"]}'
read -r l; echo "$l" >> received
answer '{"id":3,"result":{"ops":[{"op":"file.put","source":"a.dds","target":"b.dds"},{"op":"file.put","source":"c.dds","target":"d.dds"}],"items":["mod.a"],"lines":["one"],"fields":{"t.r":["a"]}}}'
answer '{"id":4,"error":"nope"}'
read -r l
"#;

    #[test]
    fn talks_json_lines_and_answers_host_calls() {
        let root = tmp("round-trip");
        let dir = root.join("addon");
        addon(&dir, ROUND_TRIP);
        let h = helper(&dir);
        let state = Memory::default();
        state.set("t.helper", serde_json::json!(["a"])).unwrap();
        let (data, cache) = (root.join("data"), root.join("cache"));
        std::fs::create_dir_all(&cache).unwrap();
        let first = profile("default");
        let cx = Cx::new("t.helper", &first, &data, &cache, None, &state);
        assert!(h.summary(&cx).is_none() && !h.running());
        let view = h.view(&cx, "items").unwrap();
        assert_eq!(view.subtitle, "s");
        assert!(h.running());
        assert_eq!(state.get("t.helper").unwrap(), Some(serde_json::json!(["b"])));
        let file = root.join("a.exmod");
        std::fs::write(&file, "x").unwrap();
        assert_eq!(h.importer().unwrap().claims(&file), Claim::Yes);
        assert_eq!(h.claims(&root.join("a.txt")), Claim::No);
        let seen = Seen::default();
        let lines = h.import(&cx, std::slice::from_ref(&file), &seen).unwrap();
        assert_eq!(lines, ["imported a.exmod"]);
        assert_eq!(*seen.0.lock().unwrap(), [(0.5, "half".to_owned())]);
        let second = profile("other");
        let cx = Cx::new("t.helper", &second, &data, &cache, None, &state);
        let options = BTreeMap::new();
        let batch = h
            .build(&BuildCx {
                cx: Cx::new("t.helper", &second, &data, &cache, None, &state),
                options: &options,
                features: &Features::default(),
            })
            .unwrap();
        assert!(matches!(batch.ops[0].op, AssetOp::File(_)));
        assert_eq!(batch.ops[0].origin.addon, "t.helper");
        assert_eq!(batch.ops[0].origin.item.as_deref(), Some("mod.a"));
        assert_eq!(batch.ops[1].origin.item, None);
        assert_eq!(batch.lines, ["one"]);
        assert_eq!(batch.fields["t.r"], ["a"]);
        let e = h.command(&cx, &["x".into()], &[]).unwrap_err();
        assert_eq!(e.to_string(), "Helper: nope");
        assert!(h.running());
        let received = std::fs::read_to_string(dir.join("received")).unwrap();
        let received: Vec<Value> = received
            .lines()
            .map(|l| serde_json::from_str(l).unwrap())
            .collect();
        let methods: Vec<&str> = received
            .iter()
            .map(|m| m["method"].as_str().unwrap_or("reply"))
            .collect();
        assert_eq!(
            methods,
            [
                "hello", "view", "reply", "reply", "import", "profile", "build", "command"
            ]
        );
        assert_eq!(received[0]["params"]["api"], API_VERSION);
        assert_eq!(received[0]["params"]["addon"], "t.helper");
        assert_eq!(received[2], serde_json::json!({"id": 9, "result": ["a"]}));
        assert_eq!(received[5]["params"]["name"], "other");
        h.stop();
        assert!(!h.running());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn removing_an_addon_stops_its_helper_before_deleting_it() {
        let root = tmp("remove");
        let store = Store::new(root.join("data"));
        let dir = store.addons_dir().join("t.helper");
        addon(
            &dir,
            r#"echo $$ > pid
read -r l; echo '{"id":0,"result":{"api":1}}'
read -r l; echo '{"id":1,"result":{"subtitle":"s","body":{"blocks":[]}}}'
exec sleep 60
"#,
        );
        let h = store
            .helpers()
            .get(&AddonManifest::load(&dir).unwrap(), &dir)
            .unwrap();
        let state = Memory::default();
        let (data, cache) = (root.join("state"), root.join("cache"));
        std::fs::create_dir_all(&cache).unwrap();
        let p = profile("default");
        h.view(&Cx::new("t.helper", &p, &data, &cache, None, &state), "items")
            .unwrap();
        assert!(h.running());
        let pid = std::fs::read_to_string(dir.join("pid")).unwrap();
        store
            .remove_addon_with("t.helper", |d| {
                assert!(!h.running());
                let alive = Command::new("kill")
                    .args(["-0", pid.trim()])
                    .stderr(Stdio::null())
                    .status()
                    .unwrap();
                assert!(!alive.success(), "the helper still runs");
                std::fs::remove_dir_all(d)
            })
            .unwrap();
        assert!(!dir.exists());
        std::fs::remove_dir_all(&root).ok();
    }

    struct FakeGame(PathBuf);

    impl GameFiles for FakeGame {
        fn build_key(&self) -> String {
            "25251167-abc".into()
        }

        fn exe_version(&self) -> Option<String> {
            None
        }

        fn is_stock(&self, _: &str) -> bool {
            false
        }

        fn original(&self, rel: &str) -> Result<PathBuf> {
            Err(swofty_addon::Error::Other(rel.into()))
        }

        fn original_lang(&self, code: &str) -> Result<PathBuf> {
            Err(swofty_addon::Error::Other(code.into()))
        }

        fn dir(&self) -> Option<PathBuf> {
            Some(self.0.clone())
        }

        fn status(&self) -> GameStatus {
            GameStatus {
                running: true,
                pid: Some(7),
                since: Some(100.5),
                session: Some("s1".into()),
            }
        }

        fn steam_save_dir(&self) -> Option<PathBuf> {
            Some(self.0.join("remote"))
        }
    }

    const LIVE: &str = r#"
answer() { read -r l; echo "$l" >> received; echo "$1"; }
answer '{"id":0,"result":{"api":1}}'
answer '{"id":0,"method":"game_dir"}'
answer '{"id":1,"method":"addon_game_dir"}'
answer '{"id":2,"method":"game_status"}'
answer '{"id":3,"method":"steam_save_dir"}'
read -r l; echo "$l" >> received
echo '{"method":"invalidate","params":{"section":"items"}}'
echo '{"id":1,"result":{"subtitle":"s","body":{"blocks":[]},"tick":true}}'
read -r l; echo "$l" >> received
echo '{"method":"invalidate","params":{}}'
echo '{"id":2,"result":null}'
answer '{"id":3,"error":"nope"}'
echo '{"method":"invalidate","params":{"section":"late"}}'
answer '{"id":4,"result":null}'
read -r l
"#;

    #[test]
    fn ticks_invalidates_and_answers_the_game_host_calls() {
        let root = tmp("live");
        let dir = root.join("addon");
        addon(&dir, LIVE);
        let h = helper(&dir);
        let (data, cache) = (root.join("data"), root.join("cache"));
        std::fs::create_dir_all(&cache).unwrap();
        let state = Memory::default();
        let p = profile("default");
        let game = FakeGame(root.join("F1 25"));
        let cx = Cx::new("t.helper", &p, &data, &cache, Some(&game), &state);
        assert!(
            h.try_tick(&cx, "items").is_none(),
            "a tick never starts the plugin"
        );
        let view = h.view(&cx, "items").unwrap();
        assert!(view.tick);
        assert_eq!(
            h.take_stale(),
            Some(Stale::Sections(BTreeSet::from(["items".to_owned()])))
        );
        assert_eq!(h.take_stale(), None);
        assert!(!h.busy());
        h.try_tick(&cx, "items").unwrap().unwrap();
        assert_eq!(h.take_stale(), Some(Stale::All));
        let e = h.try_tick(&cx, "items").unwrap().unwrap_err();
        assert_eq!(e.to_string(), "Helper: nope");
        assert!(h.running());
        h.try_tick(&cx, "items").unwrap().unwrap();
        assert_eq!(
            h.take_stale(),
            Some(Stale::Sections(BTreeSet::from(["late".to_owned()]))),
            "an invalidate between replies is taken before the next reply"
        );
        let received: Vec<Value> = std::fs::read_to_string(dir.join("received"))
            .unwrap()
            .lines()
            .map(|l| serde_json::from_str(l).unwrap())
            .collect();
        assert_eq!(received[0]["params"]["launcher"], env!("CARGO_PKG_VERSION"));
        assert_eq!(
            received[0]["params"]["host_calls"],
            serde_json::json!(["tasks", PROFILE_HEADS])
        );
        let game_dir = root.join("F1 25");
        assert_eq!(received[2]["result"], game_dir.to_string_lossy().as_ref());
        assert_eq!(
            received[3]["result"],
            game_dir.join("swofty/addons/t.helper").to_string_lossy().as_ref()
        );
        assert_eq!(
            received[4]["result"],
            serde_json::json!({"running": true, "pid": 7, "since": 100.5, "session": "s1"})
        );
        assert_eq!(
            received[5]["result"],
            game_dir.join("remote").to_string_lossy().as_ref()
        );
        assert_eq!(received[6]["method"], "tick");
        assert_eq!(received[6]["params"]["section"], "items");
        h.stop();
        let none = Cx::new("t.helper", &p, &data, &cache, None, &state);
        assert_eq!(host_call(&none, HostCall::GameDir).unwrap(), Value::Null);
        assert_eq!(host_call(&none, HostCall::AddonGameDir).unwrap(), Value::Null);
        assert_eq!(host_call(&none, HostCall::ProfileTracks).unwrap(), Value::Null);
        assert!(host_call(&none, HostCall::Invalidate(Default::default())).is_err());
        let e = host_call(&none, HostCall::Addons).unwrap_err();
        assert!(e.contains("can't be managed"), "{e}");
        let e = host_call(
            &none,
            HostCall::RemoveAddon {
                id: "t.helper".into(),
            },
        )
        .unwrap_err();
        assert!(e.contains("itself"), "{e}");
        std::fs::remove_dir_all(&root).ok();
    }

    #[derive(Default)]
    struct Moves(Mutex<Vec<(String, isize)>>);

    impl swofty_addon::launcher::AddonStore for Moves {
        fn list(&self) -> Result<Vec<swofty_addon::launcher::AddonInfo>> {
            Ok(Vec::new())
        }

        fn install(&self, _: &std::path::Path) -> Result<swofty_addon::launcher::AddonInfo> {
            Err(swofty_addon::Error::Other("no".into()))
        }

        fn set_enabled(&self, _: &str, _: bool) -> Result<()> {
            Ok(())
        }

        fn remove(&self, _: &str) -> Result<()> {
            Ok(())
        }

        fn move_addon(&self, id: &str, delta: isize) -> Result<()> {
            self.0.lock().unwrap().push((id.into(), delta));
            Ok(())
        }
    }

    #[test]
    fn lists_and_answers_move_addon_beside_the_addon_calls() {
        let root = tmp("move");
        let (data, cache) = (root.join("data"), root.join("cache"));
        let state = Memory::default();
        let p = profile("default");
        let moves = Moves::default();
        let none = Cx::new("t.helper", &p, &data, &cache, None, &state);
        assert_eq!(host_calls(&none), ["tasks"]);
        let e = host_call(
            &none,
            HostCall::MoveAddon {
                id: "t.a".into(),
                delta: 1,
            },
        )
        .unwrap_err();
        assert!(e.contains("can't be managed"), "{e}");
        let cx = Cx::new("t.helper", &p, &data, &cache, None, &state).with_addons(&moves);
        assert_eq!(
            host_calls(&cx),
            [
                "addons",
                "install_addon",
                "set_addon_enabled",
                "remove_addon",
                "move_addon",
                "tasks"
            ]
        );
        let call: HostCall = serde_json::from_value(
            serde_json::json!({"method": "move_addon", "params": {"id": "t.a", "delta": -3}}),
        )
        .unwrap();
        assert_eq!(host_call(&cx, call).unwrap(), Value::Null);
        assert_eq!(*moves.0.lock().unwrap(), [("t.a".to_owned(), -3)]);
    }

    #[derive(Default)]
    struct Shown(Mutex<Vec<swofty_addon::launcher::TaskList>>);

    impl swofty_addon::launcher::Tasks for Shown {
        fn show(&self, list: &swofty_addon::launcher::TaskList) {
            self.0.lock().unwrap().push(list.clone());
        }
    }

    const TASK_LISTS: &str = r#"
answer() { read -r l; echo "$l" >> received; echo "$1"; }
answer '{"id":0,"result":{"api":1}}'
read -r l
echo '{"method":"tasks","params":{"title":"T","items":[{"id":"a","label":"A","state":"running"}]}}'
echo '{"method":"tasks","params":{"title":"T","items":[{"id":"a","label":"A","state":"failed","note":"bad"}]}}'
echo '{"id":1,"result":{"notice":"done"}}'
read -r l
echo '{"method":"tasks","params":{"title":"T","items":[]}}'
echo '{"id":2,"result":"none"}'
read -r l
"#;

    #[test]
    fn forwards_task_lists_to_the_event_that_shows_them() {
        let root = tmp("tasks");
        let dir = root.join("addon");
        addon(&dir, TASK_LISTS);
        let h = helper(&dir);
        let (data, cache) = (root.join("data"), root.join("cache"));
        std::fs::create_dir_all(&cache).unwrap();
        let (state, shown, p) = (Memory::default(), Shown::default(), profile("default"));
        let cx = Cx::new("t.helper", &p, &data, &cache, None, &state).with_tasks(&shown);
        let effect = h.event(&cx, "items", Event::Startup {}).unwrap();
        assert_eq!(effect, Effect::Notice("done".into()));
        let states: Vec<_> = shown.0.lock().unwrap().iter().map(|l| l.items[0].state).collect();
        assert_eq!(
            states,
            [
                swofty_addon::launcher::TaskState::Running,
                swofty_addon::launcher::TaskState::Failed
            ]
        );
        let plain = Cx::new("t.helper", &p, &data, &cache, None, &state);
        assert_eq!(h.event(&plain, "items", Event::Startup {}).unwrap(), Effect::None);
        assert_eq!(shown.0.lock().unwrap().len(), 2);
        assert!(h.running());
        h.stop();
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn stale_sections_merge_until_taken() {
        let mut slot = None;
        Stale::add(&mut slot, Some("a".into()));
        Stale::add(&mut slot, Some("b".into()));
        let both = slot.clone().unwrap();
        assert!(both.covers("a") && both.covers("b") && !both.covers("c"));
        Stale::add(&mut slot, None);
        Stale::add(&mut slot, Some("c".into()));
        assert_eq!(slot, Some(Stale::All));
        assert_eq!(
            invalidation(r#"{"method":"invalidate","params":{"section":"x"}}"#),
            Some(Some("x".into()))
        );
        assert_eq!(invalidation(r#"{"method":"invalidate"}"#), Some(None));
        assert_eq!(invalidation(r#"{"id":4,"method":"invalidate"}"#), None);
        assert_eq!(invalidation(r#"{"id":4,"result":"invalidate"}"#), None);
        assert_eq!(invalidation("not json, invalidate"), None);
    }

    #[test]
    fn a_busy_helper_keeps_its_page_and_summary_without_waiting() {
        let root = tmp("busy");
        let dir = root.join("addon");
        addon(
            &dir,
            r#"
read -r l; echo '{"id":0,"result":{"api":1}}'
read -r l; echo '{"id":1,"result":{"subtitle":"s","body":{"blocks":[]}}}'
read -r l; echo '{"id":2,"result":{"label":"MODS","items":["A"]}}'
read -r l; sleep 1; echo '{"id":3,"result":"none"}'
read -r l; echo '{"id":4,"result":{"subtitle":"t","body":{"blocks":[]}}}'
read -r l
"#,
        );
        let manifest = AddonManifest::load(&dir).unwrap();
        let h = Arc::new(HelperAddon::new(manifest, &dir).unwrap().with_timeouts(Timeouts {
            quick: Duration::from_secs(2),
            long: Duration::from_secs(5),
        }));
        let (data, cache) = (root.join("data"), root.join("cache"));
        std::fs::create_dir_all(&cache).unwrap();
        let state = Memory::default();
        let p = profile("default");
        let cx = Cx::new("t.helper", &p, &data, &cache, None, &state);
        assert_eq!(h.try_view(&cx, "items").unwrap().unwrap().subtitle, "s");
        let summary = h.summary(&cx).unwrap();
        assert_eq!(summary.items, ["A"]);
        let busy = {
            let (h, data, cache) = (h.clone(), data.clone(), cache.clone());
            std::thread::spawn(move || {
                let state = Memory::default();
                let p = profile("default");
                let cx = Cx::new("t.helper", &p, &data, &cache, None, &state);
                h.event(&cx, "items", Event::Click { id: "x".into() }).unwrap()
            })
        };
        while !h.busy() && !busy.is_finished() {
            std::thread::yield_now();
        }
        let asked = std::time::Instant::now();
        assert!(h.try_view(&cx, "items").is_none());
        assert_eq!(h.summary(&cx), Some(summary));
        assert!(asked.elapsed() < Duration::from_millis(200));
        assert_eq!(busy.join().unwrap(), Effect::None);
        assert_eq!(h.try_view(&cx, "items").unwrap().unwrap().subtitle, "t");
        h.stop();
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn contains_hangs_crashes_and_bad_helpers() {
        let root = tmp("failures");
        let hello = r#"read -r l; echo '{"id":0,"result":{"api":1}}'"#;
        let cases = [
            (
                "hang",
                format!("{hello}\nread -r l; exec sleep 5"),
                "did not answer within 0.5 s",
            ),
            (
                "crash",
                format!("{hello}\nread -r l; exit 3"),
                "stopped (exit status: 3)",
            ),
            (
                "junk",
                format!("{hello}\nread -r l; echo hello there; read -r l"),
                "sent a bad message",
            ),
            (
                "shape",
                format!("{hello}\nread -r l; echo '{{\"id\":1,\"result\":7}}'; read -r l"),
                "sent a bad result",
            ),
            (
                "old",
                r#"read -r l; echo '{"id":0,"result":{"api":0}}'; read -r l"#.into(),
                "speaks launcher API 0",
            ),
        ];
        for (name, script, _) in &cases {
            addon(&root.join(name), script);
        }
        let state = Memory::default();
        let p = profile("default");
        let cache = root.join("cache");
        std::fs::create_dir_all(&cache).unwrap();
        let cx = Cx::new("t.helper", &p, &root, &cache, None, &state);
        for (name, _, error) in cases {
            let wait = if name == "hang" { HUNG } else { PATIENT };
            let h = helper_waiting(&root.join(name), wait);
            let view = h.view(&cx, "items").unwrap();
            let notice = &view.notices[0];
            assert!(
                notice.text.starts_with(&format!("Helper {error}")),
                "{name}: {}",
                notice.text
            );
            assert_eq!(notice.action.as_ref().unwrap().id, RESTART);
            assert!(matches!(view.body, Body::Empty { .. }));
            assert!(!h.running() && h.failure().is_some());
            let click = Event::Click { id: "x".into() };
            assert!(h.event(&cx, "items", click).is_err(), "{name}");
            let restart = Event::Click { id: RESTART.into() };
            assert_eq!(h.event(&cx, "items", restart).unwrap(), Effect::Reload);
            assert!(h.failure().is_none());
        }
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_missing_program_names_the_antivirus() {
        let root = tmp("missing");
        let dir = root.join("t.helper");
        addon(&dir, "read -r l");
        std::fs::remove_file(dir.join("bin/helper")).unwrap();
        let state = Memory::default();
        let p = profile("default");
        let cache = root.join("cache");
        std::fs::create_dir_all(&cache).unwrap();
        let cx = Cx::new("t.helper", &p, &root, &cache, None, &state);
        let view = helper(&dir).view(&cx, "items").unwrap();
        assert_eq!(
            view.notices[0].text,
            format!(
                "Helper could not start: its program bin/helper is missing, most likely removed by an antivirus. \
                 Allow {} in your antivirus, then reinstall Helper",
                dir.display()
            )
        );
        let manifest = AddonManifest::load(&dir).unwrap();
        std::fs::remove_dir_all(&dir).unwrap();
        let view = HelperAddon::new(manifest, &dir)
            .unwrap()
            .view(&cx, "items")
            .unwrap();
        assert!(
            view.notices[0].text.starts_with("Helper could not start "),
            "{}",
            view.notices[0].text
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn the_managed_addon_comes_back_on_play() {
        let root = tmp("managed-missing");
        let dir = root.join(crate::managed::ID);
        addon(&dir, "read -r l");
        std::fs::remove_file(dir.join("bin/helper")).unwrap();
        let manifest = AddonManifest {
            id: crate::managed::ID.into(),
            name: crate::managed::NAME.into(),
            ..AddonManifest::load(&dir).unwrap()
        };
        let h = HelperAddon::new(manifest, &dir).unwrap();
        assert!(
            h.removed("bin/helper")
                .ends_with("then press Play again to download it again"),
            "{}",
            h.removed("bin/helper")
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn routes_drops_to_a_helper_without_starting_it() {
        let root = tmp("routes");
        let store = Store::new(root.join("data"));
        addon(&store.addons_dir().join("t.helper"), "exit 1");
        std::fs::write(root.join("car.exmod"), "x").unwrap();
        std::fs::write(root.join("car.txt"), "x").unwrap();
        let reg = store.registry().unwrap();
        let entry = reg.get("t.helper").unwrap();
        assert!(entry.helper && !entry.declared);
        assert_eq!(entry.addon.sections()[0].id, "items");
        assert_eq!(
            route(&reg, &root.join("car.exmod")),
            Some(Route::Importer("t.helper".into()))
        );
        assert_eq!(route(&reg, &root.join("car.txt")), None);
        let again = store.registry().unwrap();
        assert!(std::ptr::addr_eq(
            Arc::as_ptr(&entry.addon),
            Arc::as_ptr(&again.get("t.helper").unwrap().addon)
        ));
        assert!(
            !store
                .helpers()
                .get(
                    &entry.addon.manifest().clone(),
                    &store.addons_dir().join("t.helper")
                )
                .unwrap()
                .running()
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn asks_a_helper_that_claims_about_each_drop() {
        let root = tmp("claims");
        let dir = root.join("addon");
        let script = r#"case "$1:$2" in
  claims:*.serps|claims:*/mod) echo yes ;;
  claims:*.zip) echo maybe ;;
  claims:*.slow) exec sleep 5 ;;
  claims:*) echo no ;;
esac"#;
        addon(&dir, script);
        let manifest = MANIFEST.replace(r#""imports":["exmod"]"#, r#""imports":["exmod"],"claims":true"#);
        std::fs::write(dir.join("swofty.json"), manifest).unwrap();
        let h = helper(&dir);
        let hung = helper_waiting(&dir, HUNG);
        let file = |name: &str| {
            let p = root.join(name);
            std::fs::write(&p, "x").unwrap();
            p
        };
        assert_eq!(h.claims(&file("a.serps")), Claim::Yes);
        assert_eq!(h.claims(&file("a.zip")), Claim::Maybe);
        assert_eq!(h.claims(&file("a.txt")), Claim::No);
        std::fs::create_dir_all(root.join("mod")).unwrap();
        assert_eq!(h.claims(&root.join("mod")), Claim::Yes);
        assert_eq!(h.claims(&file("a.exmod")), Claim::No, "the helper's answer wins");
        assert_eq!(
            hung.claims(&file("b.slow")),
            Claim::No,
            "a stalled helper claims nothing"
        );
        assert!(!h.running(), "claims runs apart from the live session");
        std::fs::remove_dir_all(&root).ok();
    }
}
