//! `swofty run --use-overlayfs`: choosing the mode, building and checking
//! the mount, starting the game through `overlay-exec`, and the dialog that
//! asks before falling back to copying files.

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant};

use anyhow::{Context, Result};
use swofty_core::install::{Game, Source};
use swofty_core::launch;
use swofty_core::offline::Artifacts;
use swofty_core::overlayfs::{self, Anchor, Fallback, HostProc, Kind, LiveSession, Prepared};
use swofty_core::store::Store;

use crate::run::say;

/// How long `overlay-exec` has to report the mount.
const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(30);
/// The wrapper's exit code when the player chose Exit in the dialog.
pub const CANCELLED_EXIT: u8 = 75;
const DIALOG_LOG_EVERY: Duration = Duration::from_secs(30);

/// A session that runs the game from an OverlayFS mount.
#[derive(Debug, Clone)]
pub struct Overlay {
    pub profile: String,
    pub game: PathBuf,
    pub prepared: Prepared,
    pub upper: PathBuf,
    pub work: PathBuf,
    pub target: PathBuf,
    /// The offline play stub and hook, which the common layer carries.
    pub stub: PathBuf,
    pub hook: PathBuf,
}

/// What the wrapper does for a launch that asked for OverlayFS.
pub enum Choice {
    Overlay(Box<Overlay>),
    /// Copy the files into the game folder, today's way.
    Copy,
    /// The player chose Exit.
    Cancelled,
}

/// The OverlayFS mount failed after the layers were accepted; the wrapper
/// falls back before the game starts.
#[derive(Debug)]
pub struct MountFailed(pub String);

impl std::fmt::Display for MountFailed {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for MountFailed {}

/// Why this launch can't use OverlayFS at all, if it can't.
pub fn blocker(store: &Store, game: &Path, proton: Option<&Path>, apply: bool) -> Option<String> {
    if !cfg!(target_os = "linux") {
        return Some("OverlayFS is only available on Linux".into());
    }
    if proton.is_none() {
        return Some("the launch has no Proton in its Steam command".into());
    }
    if Source::detect(game) != Source::Steam {
        return Some("OverlayFS sessions support Steam copies only; this isn't one".into());
    }
    if !apply {
        return Some("this launch doesn't apply the profile".into());
    }
    let journals = swofty_core::journal::Journal::open(&store.root).map_or(0, |j| j.len());
    let placed = swofty_core::placed::Placed::load(&store.root).map_or(0, |p| p.files.len());
    if journals > 0 || placed > 0 {
        return Some(
            "mod files were installed in the game folder with swofty apply; run swofty restore first".into(),
        );
    }
    None
}

/// Build the layers for `profile` (the whole profile build happens here, once
/// per launch: it is cached by its build hash).
fn prepare(store: &Store, profile: &str, g: &Game, stub: &Path, hook: &Path) -> Result<Prepared> {
    let built = crate::build::build_profile(store, profile, g)?;
    crate::build::report(profile, &built);
    let mut plan = built.plan.clone();
    if !plan.loader.is_empty() {
        // The install places the core's state file with the loader; here it joins the layers, so
        // the core's reports and stat ids go to the data folder in an OverlayFS session too.
        let file = swofty_core::stdlib::state::stage(&store.root)?;
        plan.loader
            .insert(swofty_core::stdlib::state::FILE.to_owned(), file);
    }
    let layers = overlayfs::split(&plan, swofty_core::builtin::loader_dir().as_deref(), stub, hook);
    let prepared = overlayfs::prepare(
        store,
        profile,
        &built.buildhash,
        layers,
        &swofty_core::disk::free_space,
    )?;
    for dir in &prepared.rebuilt {
        say("info", &format!("overlayfs: built the layer {}", dir.display()));
    }
    Ok(prepared)
}

impl Overlay {
    pub fn new(store: &Store, profile: &str, game: &Path, prepared: Prepared, a: &Artifacts) -> Self {
        Self {
            stub: a.stub.clone(),
            hook: a.hook.clone(),
            profile: profile.to_owned(),
            game: game.to_owned(),
            prepared,
            upper: store.overlay_upper_dir(profile),
            work: store.overlay_work_dir(profile),
            target: store.overlay_target(),
        }
    }

    /// Build the layers again, after the profile changed. Only while no
    /// mount of them is live.
    pub fn rebuild(&mut self, store: &Store, g: &Game) -> Result<()> {
        self.prepared = prepare(store, &self.profile, g, &self.stub, &self.hook)?;
        Ok(())
    }

    /// Highest first: the common layer, the profile's, the game folder.
    pub fn lower(&self) -> Vec<PathBuf> {
        vec![
            self.prepared.common.clone(),
            self.prepared.profile.clone(),
            self.game.clone(),
        ]
    }

    fn command(&self, tail: &[&str], run: &[OsString]) -> Result<Command> {
        let mut c = Command::new(std::env::current_exe().context("finding the swofty program")?);
        c.arg("overlay-exec");
        for l in self.lower() {
            c.arg("--lower").arg(l);
        }
        c.arg("--upper")
            .arg(&self.upper)
            .arg("--work")
            .arg(&self.work)
            .arg("--target")
            .arg(&self.target)
            .args(tail);
        if !run.is_empty() {
            c.arg("--").args(run);
        }
        Ok(c)
    }

    /// Mount once to see that it works, then let the mount go.
    pub fn check(&self) -> std::result::Result<(), String> {
        let out = self
            .command(&["--check"], &[])
            .map_err(|e| format!("{e:#}"))?
            .stdin(Stdio::null())
            .output()
            .map_err(|e| format!("could not start the mount helper: {e}"))?;
        if out.status.success() {
            return Ok(());
        }
        Err(reason_of(
            &String::from_utf8_lossy(&out.stderr),
            out.status.code(),
        ))
    }

    /// Start `run` inside the mount. Its output (and the game's) goes to
    /// `log`; the helper's stdout only carries the handshake, which is
    /// waited for here, so a mount that fails is known before the game starts.
    pub fn spawn(
        &self,
        run: &[OsString],
        envs: &std::collections::BTreeMap<String, String>,
        log: &Path,
    ) -> Result<Child> {
        let out = std::fs::File::create(log).with_context(|| log.display().to_string())?;
        let mut c = self.command(&[], run)?;
        c.current_dir(&self.game)
            .env_clear()
            .envs(envs)
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(out);
        let mut child = c.spawn().context("starting the mount helper")?;
        let stdout = child.stdout.take().context("the helper has no output")?;
        let (tx, rx) = std::sync::mpsc::channel();
        std::thread::spawn(move || {
            use std::io::BufRead;
            let mut line = String::new();
            let read = std::io::BufReader::new(stdout).read_line(&mut line);
            tx.send(read.map(|_| line)).ok();
        });
        match rx.recv_timeout(HANDSHAKE_TIMEOUT) {
            Ok(Ok(line)) if line.trim() == overlayfs::MOUNTED => Ok(child),
            other => {
                child.kill().ok();
                child.wait().ok();
                let why = match other {
                    Err(_) => "the mount helper didn't report the mount in time".to_owned(),
                    _ => reason_of(&std::fs::read_to_string(log).unwrap_or_default(), None),
                };
                Err(MountFailed(why).into())
            }
        }
    }

    /// Record the live session for the launcher, now that the anchor is in
    /// the mount namespace and before the game could use the bridge.
    pub fn record(&self, store: &Store, anchor_pid: u32) -> Result<()> {
        let anchor = Anchor::of(anchor_pid, &HostProc).context("reading the mount helper's process")?;
        LiveSession {
            schema: 1,
            game: self.game.clone(),
            profile: self.profile.clone(),
            target: self.target.clone(),
            upper: self.upper.clone(),
            anchor,
        }
        .write(&store.root)?;
        Ok(())
    }

    /// Say if the game rewrote files Swofty placed, which then shadow them.
    pub fn warn_shadowing(&self) {
        let found = overlayfs::shadowing(&self.upper, &self.lower()[..2]);
        if let Some(first) = found.first() {
            say(
                "warning",
                &format!(
                    "overlayfs: {} file(s) in {} shadow files of the layers (for example {first}); \
                     delete them if a rebuilt profile doesn't show up in game",
                    found.len(),
                    self.upper.display()
                ),
            );
        }
    }
}

/// Where the game's own files (its loader log) are on the host when `swofty
/// run` follows a launch it didn't start itself: the profile's upper dir when
/// the launch option asks for OverlayFS or the last session used it, else the
/// game folder.
pub fn log_root(store: &Store, game: &Path, profile: &str) -> PathBuf {
    let used = LiveSession::read(&store.root)
        .map(|s| s.game)
        .or_else(|| overlayfs::LastSession::read(&store.root).map(|s| s.game))
        .is_some_and(|g| swofty_core::copies::same_dir(&g, game));
    let asked = cfg!(target_os = "linux")
        && swofty_core::steam::find_game()
            .filter(|i| swofty_core::copies::same_dir(&i.dir, game))
            .is_some_and(|i| {
                swofty_core::steam::launch_options(&i.steam_root)
                    .iter()
                    .any(|(_, o)| swofty_core::steam::wrapper_installed(o) && o.contains("--use-overlayfs"))
            });
    if used || asked {
        store.overlay_upper_dir(profile)
    } else {
        game.to_owned()
    }
}

/// The helper's last `swofty: overlayfs:` line, else its exit code.
fn reason_of(stderr: &str, code: Option<i32>) -> String {
    stderr
        .lines()
        .rev()
        .find_map(|l| l.strip_prefix("swofty: overlayfs: "))
        .map(str::to_owned)
        .unwrap_or_else(|| match code {
            Some(c) => format!("the mount helper exited with code {c}"),
            None => "the mount helper failed".to_owned(),
        })
}

/// Try one mount of three empty layers in a scratch folder, for the doctor's
/// report: whether the system lets Swofty mount at all.
pub fn doctor_check(store: &Store) -> std::result::Result<(), String> {
    let dir = store.overlay_state_dir().join("doctor");
    let layer = |name: &str| dir.join(name);
    let result = (|| {
        for name in ["common", "profile", "game"] {
            std::fs::create_dir_all(layer(name)).map_err(|e| e.to_string())?;
        }
        let o = Overlay {
            profile: String::new(),
            game: layer("game"),
            prepared: Prepared {
                common: layer("common"),
                profile: layer("profile"),
                layers: Default::default(),
                rebuilt: Vec::new(),
            },
            upper: layer("upper"),
            work: layer("work"),
            target: layer("target"),
            stub: PathBuf::new(),
            hook: PathBuf::new(),
        };
        o.check()
    })();
    overlayfs::unlock(&dir);
    std::fs::remove_dir_all(&dir).ok();
    result
}

/// Pick the mode for a launch that asked for OverlayFS: build the layers and
/// try the mount, or fall back to copying with the player's say.
pub fn choose(
    store: &Store,
    profile: &str,
    g: &Game,
    artifacts: &Artifacts,
    proton: Option<&Path>,
    apply: bool,
) -> Result<Choice> {
    if let Some(reason) = blocker(store, &g.dir, proton, apply) {
        return Ok(fall_back(store, Kind::Unsupported, &reason));
    }
    let prepared = match prepare(store, profile, g, &artifacts.stub, &artifacts.hook) {
        Ok(p) => p,
        Err(e) => {
            return Ok(fall_back(
                store,
                Kind::MountFailed,
                &format!("the layers couldn't be built: {e:#}"),
            ));
        }
    };
    let overlay = Overlay::new(store, profile, &g.dir, prepared, artifacts);
    if let Err(reason) = overlay.check() {
        return Ok(fall_back(store, Kind::MountFailed, &reason));
    }
    overlay.warn_shadowing();
    say(
        "info",
        &format!(
            "overlayfs: the game runs from a mount of {} layer(s) over its folder; nothing is copied into it",
            overlay.lower().len()
        ),
    );
    Ok(Choice::Overlay(Box::new(overlay)))
}

/// Falling back to copying: record why for the launcher, and ask unless the
/// player turned this kind of question off.
pub fn fall_back(store: &Store, kind: Kind, reason: &str) -> Choice {
    let record = Fallback::new(kind, reason);
    if let Err(e) = record.write(&store.root) {
        say("info", &format!("could not record it for the launcher: {e}"));
    }
    say(
        "warning",
        &format!("overlayfs: {reason}; copying files into the game folder this session"),
    );
    if store.config().news_seen.contains(kind.key()) {
        return Choice::Copy;
    }
    match ask(store, kind, reason) {
        Answer::Stopped => {
            say("info", "overlayfs: stopped while waiting in the fallback dialog");
            Choice::Cancelled
        }
        Answer::Exit => {
            say("info", "overlayfs: launch cancelled in the fallback dialog");
            let failure = launch::Failure::new(launch::Cause::OverlayCancelled {
                fallback: kind,
                reason: reason.to_owned(),
            });
            if let Err(e) = failure.write(&store.root) {
                say("info", &format!("could not record it for the launcher: {e}"));
            }
            Choice::Cancelled
        }
        Answer::Continue | Answer::Unasked => Choice::Copy,
    }
}

/// What the player said in the dialog.
enum Answer {
    Continue,
    Exit,
    /// The wrapper was asked to stop (Steam's Stop button) while it waited.
    Stopped,
    /// The dialog couldn't be shown or failed; the launch goes on as before.
    Unasked,
}

/// Show the dialog and wait for its answer.
fn ask(store: &Store, kind: Kind, reason: &str) -> Answer {
    if std::env::var_os("DISPLAY").is_none() && std::env::var_os("WAYLAND_DISPLAY").is_none() {
        say(
            "info",
            "overlayfs: no display for the fallback dialog; continuing",
        );
        return Answer::Unasked;
    }
    let launcher = std::env::current_exe()
        .ok()
        .map(|exe| exe.with_file_name("swofty-launcher"))
        .into_iter()
        .chain([store.bin_dir().join("swofty-launcher")])
        .find(|p| p.is_file());
    let Some(launcher) = launcher else {
        say(
            "info",
            "overlayfs: the launcher program isn't here to show the dialog; continuing",
        );
        return Answer::Unasked;
    };
    let pending = overlayfs::dialog_pending_path(&store.root);
    if let Some(dir) = pending.parent() {
        std::fs::create_dir_all(dir).ok();
    }
    std::fs::write(&pending, format!("{}\n", std::process::id())).ok();
    let _clear = Clear(pending);
    let mut child = match Command::new(&launcher)
        .arg("--overlayfs-fallback-dialog")
        .arg(kind.arg())
        .arg(reason)
        .arg("--data-dir")
        .arg(&store.root)
        .stdin(Stdio::null())
        .spawn()
    {
        Ok(c) => c,
        Err(e) => {
            say(
                "info",
                &format!("overlayfs: could not show the fallback dialog ({e}); continuing"),
            );
            return Answer::Unasked;
        }
    };
    say(
        "info",
        "overlayfs: waiting for your answer in the OverlayFS dialog",
    );
    let mut logged = Instant::now();
    loop {
        match child.try_wait() {
            Ok(Some(status)) => {
                return match status.code() {
                    Some(0) => Answer::Continue,
                    Some(1) => Answer::Exit,
                    _ if crate::run::ending() => Answer::Stopped,
                    _ => {
                        say(
                            "info",
                            &format!("overlayfs: the fallback dialog failed ({status}); continuing"),
                        );
                        Answer::Unasked
                    }
                };
            }
            Ok(None) => {}
            Err(_) => return Answer::Unasked,
        }
        if crate::run::ending() {
            child.kill().ok();
            child.wait().ok();
            return Answer::Stopped;
        }
        if logged.elapsed() >= DIALOG_LOG_EVERY {
            logged = Instant::now();
            say(
                "info",
                "overlayfs: still waiting for your answer in the OverlayFS dialog",
            );
        }
        std::thread::sleep(Duration::from_millis(250));
    }
}

struct Clear(PathBuf);

impl Drop for Clear {
    fn drop(&mut self) {
        std::fs::remove_file(&self.0).ok();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_helper_reason_is_its_last_overlayfs_line() {
        let err = "noise\nswofty: overlayfs: unshare: Operation not permitted\n";
        assert_eq!(reason_of(err, Some(90)), "unshare: Operation not permitted");
        assert_eq!(reason_of("", Some(92)), "the mount helper exited with code 92");
        assert_eq!(reason_of("", None), "the mount helper failed");
    }

    #[test]
    fn layers_are_mounted_highest_first() {
        let store = Store::new(PathBuf::from("/d"));
        let prepared = Prepared {
            common: "/d/overlay/common".into(),
            profile: "/d/profiles/p/overlay".into(),
            layers: Default::default(),
            rebuilt: Vec::new(),
        };
        let o = Overlay {
            profile: "p".into(),
            game: "/g/F1 25".into(),
            prepared,
            upper: store.overlay_upper_dir("p"),
            work: store.overlay_work_dir("p"),
            target: store.overlay_target(),
            stub: PathBuf::new(),
            hook: PathBuf::new(),
        };
        assert_eq!(
            o.lower(),
            [
                PathBuf::from("/d/overlay/common"),
                "/d/profiles/p/overlay".into(),
                "/g/F1 25".into()
            ]
        );
        assert_eq!(o.upper, PathBuf::from("/d/profiles/p/writes"));
        assert_eq!(o.work, PathBuf::from("/d/profiles/p/workdir"));
        assert_eq!(o.target, PathBuf::from("/d/state/overlay/game"));
    }

    #[test]
    fn nothing_but_a_proton_steam_apply_launch_may_mount() {
        let root = std::env::temp_dir().join(format!("swofty-ovl-blocker-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let game = root.join("steamapps/common/F1 25");
        std::fs::create_dir_all(&game).unwrap();
        std::fs::write(
            root.join(format!("steamapps/appmanifest_{}.acf", swofty_core::STEAM_APP_ID)),
            "",
        )
        .unwrap();
        let store = Store::new(root.join("data"));
        let proton = Path::new("/p/proton");
        let none = blocker(&store, &game, None, true).unwrap();
        assert!(none.contains("Proton"), "{none}");
        // A folder in a Steam library is a Steam copy.
        let apply = blocker(&store, &game, Some(proton), false).unwrap();
        assert!(apply.contains("apply"), "{apply}");
        if cfg!(target_os = "linux") {
            assert_eq!(blocker(&store, &game, Some(proton), true), None);
        }
        std::fs::create_dir_all(root.join("data/state")).unwrap();
        std::fs::write(
            root.join("data/state/placed.json"),
            r#"{"schema_version":1,"files":{"version.dll":{"path":"version.dll","sha256":"x"}}}"#,
        )
        .unwrap();
        let installed = blocker(&store, &game, Some(proton), true).unwrap_or_default();
        assert!(installed.contains("swofty apply"), "{installed}");
        let other = root.join("somewhere/F1 25");
        std::fs::create_dir_all(&other).unwrap();
        let ea = blocker(&store, &other, Some(proton), true).unwrap();
        assert!(ea.contains("Steam copies"), "{ea}");
        std::fs::remove_dir_all(&root).ok();
    }
}
