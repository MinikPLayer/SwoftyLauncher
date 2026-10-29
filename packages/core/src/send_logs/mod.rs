use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use serde_json::{Value, json};

use crate::manifest::AddonManifest;
use crate::session::{LOADER_FALLBACK, LOADER_LOG, PERF_LOG};
use crate::store::Store;
use crate::{Error, GAME_EXE, Result};

pub mod perf;
pub mod upload;

pub use upload::{LogServer, SendError, Stage};

pub const UNAVAILABLE: &str = "Sending logs isn't available in this build";
pub const NAME_MIN: usize = 2;
pub const NAME_MAX: usize = 32;
pub const MESSAGE_MIN: usize = 10;
pub const MESSAGE_MAX: usize = 1000;
pub const FILE_CAP: u64 = 5 << 20;
pub const TOTAL_CAP: u64 = 40 << 20;
pub const PART_LIMIT: usize = 9 << 20;
pub const SESSIONS: usize = 3;
pub const SNAPSHOT: &str = "swofty/addons/swofty.career-editor/snapshot.json";
const CAREER_EDITOR: &str = "swofty.career-editor";
pub const PERF_LINES: usize = 60;
const LOADER_HEAD: u64 = 16 << 10;
pub const LOADER_DIR: &str = "swofty";
pub const KEPT_DIR: &str = "logs";
pub const KEPT: usize = 10;
pub const LAUNCH_LOGS: [&str; 2] = ["state/launcher-run.log", "state/wrapper-run.log"];
pub const NET_TRACE: &str = "swofty/net-trace.log";
pub const NET_TRACE_CAP: u64 = 16 << 20;
/// How many saved fingerprint comparisons a bundle carries, newest first.
const PRINTS_SENT: usize = 3;

const REDACTED: &str = "[redacted]";
const SESSION_DEPTH: usize = 3;
const MIN_TAIL: u64 = 64 << 10;
const PERF_READ: u64 = 1 << 20;
const DUMP_CAP: u64 = 24 << 20;
const OVERLAY_READ: u64 = 32 << 20;

/// Where the logs go: `SWOFTY_LOGS_URL` at run time, else the address baked in
/// at build time, if any.
pub fn destination() -> Option<LogServer> {
    let runtime = std::env::var("SWOFTY_LOGS_URL").ok();
    LogServer::new(runtime.as_deref().or(option_env!("SWOFTY_LOGS_URL")))
}

pub fn available() -> bool {
    destination().is_some()
}

pub fn send(bundle: &Bundle, progress: &dyn Fn(Stage)) -> std::result::Result<usize, SendError> {
    destination()
        .ok_or(SendError::Unavailable)?
        .send(bundle, progress)
}

/// `$DATA/logs`, where the launcher and `swofty logs send` keep the newest
/// bundles for the player to share themselves.
pub fn kept_dir(store: &Store) -> PathBuf {
    store.root.join(KEPT_DIR)
}

/// Removes the bundles in `dir` beyond the newest `keep`, counting `newest`
/// and never removing it. Returns the removed files.
pub fn prune(dir: &Path, keep: usize, newest: &Path) -> Vec<PathBuf> {
    let Ok(read) = std::fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut older: Vec<_> = read
        .flatten()
        .map(|e| e.path())
        .filter(|p| p != newest && is_bundle(p))
        .map(|p| (modified(&p), p))
        .collect();
    older.sort_by(|a, b| b.cmp(a));
    older
        .into_iter()
        .skip(keep.saturating_sub(1))
        .filter_map(|(_, p)| std::fs::remove_file(&p).ok().map(|()| p))
        .collect()
}

fn is_bundle(path: &Path) -> bool {
    let name = file_name(path);
    name.starts_with("swofty-logs-") && name.ends_with(".zip") && path.is_file()
}

pub fn check_name(raw: &str) -> std::result::Result<String, String> {
    let name = raw.trim();
    match name.chars().count() {
        0 => Err("Enter your Discord username".into()),
        n if n < NAME_MIN => Err(format!("A Discord username has at least {NAME_MIN} characters")),
        n if n > NAME_MAX => Err(format!("A Discord username has at most {NAME_MAX} characters")),
        _ => Ok(name.to_owned()),
    }
}

pub fn check_message(raw: &str) -> std::result::Result<String, String> {
    let message = raw.trim();
    match message.chars().count() {
        n if n < MESSAGE_MIN => Err(format!(
            "Describe what went wrong in at least {MESSAGE_MIN} characters"
        )),
        n if n > MESSAGE_MAX => Err(format!("Keep it to {MESSAGE_MAX} characters")),
        _ => Ok(message.to_owned()),
    }
}

/// Whether the data folder holds a launch log or a game session, the logs
/// that show what happened when the game ran.
pub fn has_session(store: &Store) -> bool {
    LAUNCH_LOGS.iter().any(|l| store.root.join(l).is_file()) || !newest_sessions(store, 1).is_empty()
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct GameInfo {
    pub dir: Option<PathBuf>,
    pub build_id: Option<u64>,
    pub version: Option<String>,
}

impl GameInfo {
    /// The copy the launcher plays: the one picked in its game switcher,
    /// else Steam's.
    pub fn detect(store: &Store) -> Self {
        let Some(copy) = crate::copies::find_active(&store.config()) else {
            return Self::default();
        };
        let (dir, build_id) = (copy.dir.clone(), copy.build_id());
        Self {
            version: crate::exe_version::read(&dir.join(GAME_EXE)),
            dir: Some(dir),
            build_id,
        }
    }

    pub fn describe(&self) -> String {
        if self.dir.is_none() {
            return "not found".into();
        }
        match (&self.version, self.build_id) {
            (Some(v), Some(b)) => format!("{v} (Steam build {b})"),
            (Some(v), None) => v.clone(),
            (None, Some(b)) => format!("Steam build {b}"),
            (None, None) => "found, version unknown".into(),
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct Report {
    pub name: String,
    pub message: String,
    pub game: GameInfo,
    /// The running room's state, already redacted, sent as `multiplayer/session.json`.
    pub room: Option<Value>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Entry {
    pub path: String,
    pub data: Vec<u8>,
    pub note: Option<String>,
}

#[derive(Debug, Clone)]
pub struct Bundle {
    pub stem: String,
    pub name: String,
    pub message: String,
    pub launcher: String,
    pub os: String,
    pub game: String,
    pub entries: Vec<Entry>,
    pub skipped: Vec<String>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Part {
    pub file_name: String,
    pub data: Vec<u8>,
    pub index: usize,
    pub total: usize,
}

#[derive(Debug, Clone, Copy)]
pub struct Limits {
    pub file: u64,
    pub total: u64,
}

impl Default for Limits {
    fn default() -> Self {
        Self {
            file: FILE_CAP,
            total: TOTAL_CAP,
        }
    }
}

struct Collector {
    limits: Limits,
    used: u64,
    entries: Vec<Entry>,
    skipped: Vec<String>,
}

impl Collector {
    fn budget(&self) -> u64 {
        self.limits.total.saturating_sub(self.used)
    }

    fn push(&mut self, path: String, data: Vec<u8>, note: Option<String>) {
        self.used += data.len() as u64;
        self.entries.push(Entry { path, data, note });
    }

    /// A whole binary file (the minidump), up to `cap` and the remaining
    /// budget; too big to fit means it is left out with a note, never cut.
    fn binary(&mut self, path: String, from: &Path, cap: u64) {
        let len = match std::fs::metadata(from) {
            Ok(m) => m.len(),
            Err(e) => return self.skipped.push(format!("{path}: {e}")),
        };
        let room = cap.min(self.budget());
        if len > room {
            return self.skipped.push(format!(
                "{path} ({}): too big to send (limit {}), left out",
                size(len),
                size(room)
            ));
        }
        match std::fs::read(from) {
            Ok(data) => self.push(path, data, Some(size(len))),
            Err(e) => self.skipped.push(format!("{path}: {e}")),
        }
    }

    fn file(&mut self, path: String, from: &Path, redact: Option<fn(&str) -> String>) {
        self.file_capped(path, from, redact, self.limits.file);
    }

    fn file_capped(&mut self, path: String, from: &Path, redact: Option<fn(&str) -> String>, file_cap: u64) {
        let cap = file_cap.min(self.budget());
        let len = match std::fs::metadata(from) {
            Ok(m) => m.len(),
            Err(e) => return self.skipped.push(format!("{path}: {e}")),
        };
        if cap < len && cap < MIN_TAIL.min(self.limits.file) {
            return self.skipped.push(format!(
                "{path} ({}): the bundle reached its {} limit",
                size(len),
                size(self.limits.total)
            ));
        }
        match read_tail(from, cap) {
            Ok((data, cut)) => {
                let redacted = redact
                    .map(|f| f(&String::from_utf8_lossy(&data)))
                    .filter(|r| r.as_bytes() != data.as_slice());
                let changed = redacted.is_some();
                let mut out = Vec::new();
                if cut {
                    out.extend_from_slice(
                        format!("[swofty: kept the last {} of {}]\n", size(cap), size(len)).as_bytes(),
                    );
                }
                let note = match (cut, changed) {
                    (true, true) => Some(format!("last {} of {}, secrets redacted", size(cap), size(len))),
                    (true, false) => Some(format!("last {} of {}", size(cap), size(len))),
                    (false, true) => Some("secrets redacted".into()),
                    (false, false) => None,
                };
                out.extend_from_slice(redacted.as_ref().map_or(data.as_slice(), |r| r.as_bytes()));
                self.push(path, out, note);
            }
            Err(e) => self.skipped.push(format!("{path}: {e}")),
        }
    }
}

impl Bundle {
    pub fn collect(store: &Store, report: &Report) -> Self {
        Self::collect_with(store, report, Limits::default(), &utc_stamp(now_secs()))
    }

    pub fn collect_with(store: &Store, report: &Report, limits: Limits, stamp: &str) -> Self {
        let mut c = Collector {
            limits,
            used: 0,
            entries: Vec::new(),
            skipped: Vec::new(),
        };
        match std::fs::read(store.config_path()) {
            Ok(data) => {
                let (text, note) = match serde_json::from_slice::<Value>(&data) {
                    Ok(mut v) => {
                        let n = redact_json(&mut v);
                        let text = serde_json::to_string_pretty(&v).unwrap_or_default();
                        (text, (n > 0).then(|| "secrets redacted".to_owned()))
                    }
                    Err(e) => (
                        format!("config.json is not valid JSON ({e}); its contents were left out"),
                        Some("unreadable, left out".to_owned()),
                    ),
                };
                c.push("config.json".into(), text.into_bytes(), note);
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => c.skipped.push("config.json: none".into()),
            Err(e) => c.skipped.push(format!("config.json: {e}")),
        }
        let addons = serde_json::to_string_pretty(&addons_summary(store)).unwrap_or_default();
        c.push("addons.json".into(), addons.into_bytes(), None);
        let conflicts = crate::conflicts::path(store, &store.active_profile());
        if conflicts.is_file() {
            c.file("conflicts.json".into(), &conflicts, None);
        }
        if let Ok(p) = store.profile(&store.active_profile()) {
            for id in p
                .enabled()
                .filter(|id| crate::dbedit::package::is_package(store, id))
            {
                let source = store.addons_dir().join(id).join(crate::dbedit::package::MARKER);
                c.file(format!("db-packages/{id}.json"), &source, None);
            }
        }
        let sessions = newest_sessions(store, SESSIONS);
        let mut loader = Vec::new();
        let mut live_perf = None;
        if let Some(game) = &report.game.dir {
            let game = &crate::overlayfs::files_root(&store.root, game);
            for (name, rel) in [("loader.log", LOADER_LOG), ("perf.log", PERF_LOG)] {
                let live = game.join(rel);
                if !live.is_file()
                    || sessions
                        .first()
                        .is_some_and(|d| same_contents(&live, &d.join(name)))
                {
                    continue;
                }
                c.file(format!("game/{name}"), &live, None);
                if name == "perf.log" {
                    live_perf = Some(live);
                } else {
                    loader.extend(loader_line(&format!("game/{name}"), &live));
                }
            }
        }
        for name in crate::stdlib::reports::SENT {
            if let Some(file) = crate::stdlib::reports::found(&store.root, report.game.dir.as_deref(), name) {
                c.file(format!("game/registry/{name}"), &file, None);
            }
        }
        let careers = crate::stdlib::careers::data_path(&store.root);
        let careers = if careers.is_file() {
            Some(careers)
        } else {
            report
                .game
                .dir
                .as_ref()
                .map(|g| g.join(crate::stdlib::careers::GAME_PATH))
                .filter(|p| p.is_file())
        };
        if let Some(careers) = careers {
            c.file("game/careers/index.json".into(), &careers, None);
        }
        if let Some(game) = &report.game.dir {
            let game = &crate::overlayfs::files_root(&store.root, game);
            let status = game.join(crate::multiplayer::bridge::STATUS_FILE);
            if status.is_file() {
                c.file("game/netplay.json".into(), &status, None);
            }
            let trace = game.join(NET_TRACE);
            if trace.is_file() {
                c.file_capped("game/net-trace.log".into(), &trace, None, NET_TRACE_CAP);
            }
        }
        if let Some(room) = &report.room {
            let mut room = room.clone();
            redact_json(&mut room);
            let text =
                crate::multiplayer::diag::scrub(&serde_json::to_string_pretty(&room).unwrap_or_default());
            c.push("multiplayer/session.json".into(), text.into_bytes(), None);
        }
        let fallback = store.root.join(LOADER_FALLBACK);
        if !sessions
            .first()
            .is_some_and(|d| same_contents(&fallback, &d.join("loader.log")))
        {
            loader.extend(loader_line(LOADER_FALLBACK, &fallback));
        }
        for (id, log) in helper_logs(store) {
            c.file(format!("addons/{id}/{}", crate::helper::LOG_FILE), &log, None);
        }
        for (id, log) in addon_logs(store) {
            c.file(format!("addons/{id}/logs/{}", file_name(&log)), &log, None);
        }
        if let Some(snapshot) = report
            .game
            .dir
            .as_ref()
            .and_then(|d| {
                let root = crate::overlayfs::files_root(&store.root, d);
                if root == *d {
                    crate::addon_storage::newest(&store.root, d, CAREER_EDITOR, "snapshot.json")
                } else {
                    Some(root.join(SNAPSHOT))
                }
            })
            .filter(|snapshot| snapshot.is_file())
        {
            c.file("game/career-editor/snapshot.json".into(), &snapshot, None);
        }
        for log in state_logs(store) {
            let name = file_name(&log);
            let redact = name
                .starts_with("multiplayer")
                .then_some(crate::multiplayer::diag::scrub as fn(&str) -> String);
            c.file(format!("state/{name}"), &log, redact);
        }
        for print in crate::multiplayer::diag::newest_prints(&store.root, PRINTS_SENT) {
            c.file(
                format!("{}/{}", crate::multiplayer::diag::PRINTS_DIR, file_name(&print)),
                &print,
                Some(crate::multiplayer::diag::scrub as fn(&str) -> String),
            );
        }
        let not_stock = store.root.join("state/not-stock.json");
        if not_stock.is_file() {
            c.file("state/not-stock.json".into(), &not_stock, None);
        }
        let blocked = store.root.join("state/blocked.json");
        if blocked.is_file() {
            c.file("state/blocked.json".into(), &blocked, None);
        }
        for dir in &sessions {
            let session = file_name(dir);
            loader.extend(loader_line(
                &format!("sessions/{session}"),
                &dir.join("loader.log"),
            ));
            for (rel, file) in files_under(dir, SESSION_DEPTH) {
                // The crash report and its minidump are collected whole below, the dump as binary;
                // the generic walk reads files as redacted text tails, which would corrupt the dump.
                if is_crash_file(&file) {
                    continue;
                }
                let redact = (rel == "env.txt").then_some(redact_env as fn(&str) -> String);
                c.file(format!("sessions/{session}/{rel}"), &file, redact);
            }
        }
        let crash = newest_crash(store, report.game.dir.as_deref(), &sessions);
        let mut crash_report = None;
        if let Some(c0) = &crash {
            let name = file_name(&c0.report);
            c.file(format!("crash/{name}"), &c0.report, None);
            if let Some(dump) = &c0.dump {
                c.binary(format!("crash/{}", file_name(dump)), dump, DUMP_CAP);
            }
            crash_report = std::fs::read_to_string(&c0.report).ok();
        }
        let os = os_description();
        let mut bundle = Self {
            stem: format!("swofty-logs-{}-{stamp}", file_safe(&report.name)),
            name: report.name.clone(),
            message: report.message.clone(),
            launcher: crate::update::current_version().to_string(),
            os,
            game: report.game.describe(),
            entries: c.entries,
            skipped: c.skipped,
        };
        let perf = newest_perf(live_perf, &sessions);
        let summary = bundle.summary(store, report, stamp, perf, crash_report.as_deref(), &loader);
        bundle.entries.insert(
            0,
            Entry {
                path: "summary.txt".into(),
                data: summary.into_bytes(),
                note: None,
            },
        );
        bundle
    }

    fn summary(
        &self,
        store: &Store,
        report: &Report,
        stamp: &str,
        perf: Option<(String, PathBuf)>,
        crash: Option<&str>,
        loader: &[String],
    ) -> String {
        let mut s = String::new();
        let mut line = |k: &str, v: &str| s.push_str(&format!("{k:16} {v}\n"));
        line("Discord username", &self.name);
        line("Collected", &readable(stamp));
        line("Launcher", &self.launcher);
        line("OS", &self.os);
        line("Hardware", &hardware());
        if cfg!(windows) {
            line(
                "Administrator",
                if crate::compat::elevated() { "yes" } else { "no" },
            );
        }
        line("F1 25", &self.game);
        if let Some(dir) = &report.game.dir {
            line("Game folder", &dir.display().to_string());
        }
        line("Data folder", &store.root.display().to_string());
        if !has_session(store) {
            line(
                "Game sessions",
                "none found: no launch log in state/ and no session in sessions/",
            );
        }
        s.push_str("\nWhat went wrong\n");
        s.push_str(if self.message.is_empty() {
            "(not said)"
        } else {
            &self.message
        });
        s.push_str("\n\nMod loader\n");
        if let Some(r) = crate::quarantine::Report::read(&store.root) {
            s.push_str(&format!(
                "  last launch: {} ({})\n",
                r.title(),
                r.files.join(", ")
            ));
            if let Some(c) = &r.cause {
                s.push_str(&format!(
                    "  found: {} (the [loader] lines in state/wrapper-run.log say more)\n",
                    c.what()
                ));
            }
        }
        if let Some(f) = crate::launch::Failure::read(&store.root) {
            s.push_str(&format!("  last launch: {} ({})\n", f.title(), f.headline()));
            if let crate::launch::Cause::Blocked(b) = &f.cause {
                s.push_str(&format!(
                    "  blocked: {} (os error {})\n",
                    b.program.display(),
                    b.code
                ));
            }
        }
        if loader.is_empty() {
            s.push_str("  No loader log was found\n");
        }
        for l in loader {
            s.push_str(l);
            s.push('\n');
        }
        s.push_str("\nOverlay\n");
        let mut logs = Vec::new();
        if let Some(game) = &report.game.dir
            && self.entries.iter().any(|e| e.path == "game/loader.log")
        {
            logs.push((
                "game/loader.log".to_owned(),
                crate::overlayfs::files_root(&store.root, game).join(LOADER_LOG),
            ));
        }
        for dir in newest_sessions(store, SESSIONS) {
            logs.push((format!("sessions/{}", file_name(&dir)), dir.join("loader.log")));
        }
        for l in overlay_lines(store, &logs) {
            s.push_str(&l);
            s.push('\n');
        }
        s.push_str("\nCrash\n");
        match crash {
            Some(text) => {
                for l in crash_excerpt(text) {
                    s.push_str(&l);
                    s.push('\n');
                }
            }
            None => s.push_str("No crash report was found (the game did not write one this session)\n"),
        }
        s.push_str("\nGame files against Steam\n");
        s.push_str(&game_files_section(store, report.game.dir.as_deref()));
        s.push_str("\nPerformance\n");
        match perf {
            Some((label, path)) => {
                let text = read_head(&path, PERF_READ).map(|d| String::from_utf8_lossy(&d).into_owned());
                match text {
                    Ok(text) => {
                        s.push_str(&format!("From {label}\n"));
                        for l in perf::excerpt(&text, PERF_LINES) {
                            s.push_str(&l);
                            s.push('\n');
                        }
                    }
                    Err(e) => s.push_str(&format!("{label} could not be read: {e}\n")),
                }
            }
            None => s.push_str(
                "No performance report was found (an older launcher, or the game never reached the menus)\n",
            ),
        }
        s.push_str("\nAddons\n");
        s.push_str(&addon_lines(store));
        s.push_str("\nFiles\n");
        for e in &self.entries {
            s.push_str(&format!("  {:60} {:>9}", e.path, size(e.data.len() as u64)));
            if let Some(n) = &e.note {
                s.push_str(&format!("  ({n})"));
            }
            s.push('\n');
        }
        if !self.skipped.is_empty() {
            s.push_str("\nLeft out\n");
            for k in &self.skipped {
                s.push_str(&format!("  {k}\n"));
            }
        }
        s
    }

    pub fn listing(&self) -> Vec<String> {
        self.entries
            .iter()
            .map(|e| match &e.note {
                Some(n) => format!("{}  {}  ({n})", e.path, size(e.data.len() as u64)),
                None => format!("{}  {}", e.path, size(e.data.len() as u64)),
            })
            .collect()
    }

    pub fn file_name(&self) -> String {
        format!("{}.zip", self.stem)
    }

    pub fn zip(&self) -> Result<Vec<u8>> {
        zip_entries(self.entries.iter())
    }

    /// Writes the whole zip into `dir` as [`Bundle::file_name`], then prunes
    /// `dir` to the newest `keep` bundles.
    pub fn save(&self, dir: &Path, keep: usize) -> Result<PathBuf> {
        let data = self.zip()?;
        std::fs::create_dir_all(dir).map_err(crate::io_err(dir))?;
        let path = dir.join(self.file_name());
        let partial = dir.join(format!("{}.tmp", self.file_name()));
        std::fs::write(&partial, &data).map_err(crate::io_err(&partial))?;
        std::fs::rename(&partial, &path).map_err(crate::io_err(&path))?;
        prune(dir, keep, &path);
        Ok(path)
    }

    /// The zip files to upload, each at most `limit` bytes where possible:
    /// one file when everything fits, else the entries packed in order into
    /// standalone zips named `-part<n>`.
    pub fn parts(&self, limit: usize) -> Result<Vec<Part>> {
        let whole = self.zip()?;
        if whole.len() <= limit {
            return Ok(vec![Part {
                file_name: self.file_name(),
                data: whole,
                index: 1,
                total: 1,
            }]);
        }
        let mut groups: Vec<Vec<&Entry>> = vec![Vec::new()];
        let mut used = 0usize;
        for e in &self.entries {
            let cost = compressed_len(&e.data) + 2 * e.path.len() + 128;
            let group = groups.last_mut().expect("never empty");
            if !group.is_empty() && used + cost > limit {
                groups.push(Vec::new());
                used = 0;
            }
            groups.last_mut().expect("never empty").push(e);
            used += cost;
        }
        let total = groups.len();
        groups
            .into_iter()
            .enumerate()
            .map(|(i, g)| {
                Ok(Part {
                    file_name: format!("{}-part{}.zip", self.stem, i + 1),
                    data: zip_entries(g.into_iter())?,
                    index: i + 1,
                    total,
                })
            })
            .collect()
    }
}

fn zip_entries<'a>(entries: impl Iterator<Item = &'a Entry>) -> Result<Vec<u8>> {
    let mut z = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let opts = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated)
        .large_file(false);
    for e in entries {
        z.start_file(e.path.as_str(), opts)?;
        z.write_all(&e.data)
            .map_err(|e| Error::Other(format!("writing the logs zip: {e}")))?;
    }
    Ok(z.finish()?.into_inner())
}

fn compressed_len(data: &[u8]) -> usize {
    let mut enc = flate2::write::DeflateEncoder::new(Vec::new(), flate2::Compression::default());
    enc.write_all(data).ok();
    enc.finish().map_or(data.len(), |v| v.len())
}

fn read_tail(path: &Path, cap: u64) -> std::io::Result<(Vec<u8>, bool)> {
    let mut f = std::fs::File::open(path)?;
    let len = f.metadata()?.len();
    if len <= cap {
        let mut data = Vec::with_capacity(len as usize);
        f.read_to_end(&mut data)?;
        return Ok((data, false));
    }
    f.seek(SeekFrom::Start(len - cap))?;
    let mut data = Vec::with_capacity(cap as usize);
    f.take(cap).read_to_end(&mut data)?;
    if let Some(nl) = data
        .iter()
        .position(|&b| b == b'\n')
        .filter(|&i| i < data.len() / 4)
    {
        data.drain(..=nl);
    }
    Ok((data, true))
}

fn read_head(path: &Path, cap: u64) -> std::io::Result<Vec<u8>> {
    let mut data = Vec::new();
    std::fs::File::open(path)?.take(cap).read_to_end(&mut data)?;
    Ok(data)
}

fn same_contents(a: &Path, b: &Path) -> bool {
    let len = |p: &Path| std::fs::metadata(p).map(|m| m.len()).ok();
    len(a).is_some_and(|n| len(b) == Some(n))
        && matches!((std::fs::read(a), std::fs::read(b)), (Ok(x), Ok(y)) if x == y)
}

fn modified(path: &Path) -> Option<std::time::SystemTime> {
    std::fs::metadata(path).and_then(|m| m.modified()).ok()
}

/// Whether a session's loader.log shows the loader running. One with only the
/// offline play stub's lines means version.dll never loaded in the game. A
/// loader the game folder refused logged to the data folder, and says why.
fn loader_line(label: &str, log: &Path) -> Vec<String> {
    let Ok(head) = read_head(log, LOADER_HEAD) else {
        return Vec::new();
    };
    let head = String::from_utf8_lossy(&head);
    let state = match crate::quarantine::attached(&head) {
        Some(true) => "mod loader attached",
        Some(false) => "mod loader never attached (version.dll not loaded)",
        None => return Vec::new(),
    };
    let mut out = vec![format!("  {label:40} {state}")];
    out.extend(
        crate::quarantine::refused(&head).map(|why| format!("  {:40} logged in the data folder: {why}", "")),
    );
    out
}

/// Which addons draw over the game, and what each session's `[overlay]` lines say.
fn overlay_lines(store: &Store, logs: &[(String, PathBuf)]) -> Vec<String> {
    let mut out = Vec::new();
    let painters = crate::overlay_guard::painters(store, &store.active_profile());
    let names: Vec<String> = painters
        .iter()
        .map(|(id, name)| format!("{name} ({id})"))
        .collect();
    out.push(format!(
        "  Addons that draw over the game: {}",
        if names.is_empty() {
            "none on".to_owned()
        } else {
            names.join(", ")
        }
    ));
    for (label, log) in logs {
        let Ok(head) = read_head(log, OVERLAY_READ) else {
            continue;
        };
        let state = crate::overlay_guard::SessionOverlay::scan(&String::from_utf8_lossy(&head))
            .map_or_else(|| "no [overlay] lines".to_owned(), |o| o.describe());
        out.push(format!("  {label:40} {state}"));
    }
    out
}

fn newest_perf(live: Option<PathBuf>, sessions: &[PathBuf]) -> Option<(String, PathBuf)> {
    let kept = sessions.iter().map(|d| d.join("perf.log")).find(|p| p.is_file());
    match (live, kept) {
        (Some(live), Some(kept)) if modified(&live) <= modified(&kept) => Some((session_label(&kept), kept)),
        (Some(live), _) => Some(("game/perf.log".into(), live)),
        (None, Some(kept)) => Some((session_label(&kept), kept)),
        (None, None) => None,
    }
}

fn session_label(file: &Path) -> String {
    let session = file.parent().map(file_name).unwrap_or_default();
    format!("sessions/{session}/{}", file_name(file))
}

fn file_name(path: &Path) -> String {
    path.file_name()
        .map_or_else(String::new, |n| n.to_string_lossy().into_owned())
}

/// A crash report the core wrote (`crash-<time>.txt`) or its minidump
/// (`crash-<time>.dmp`).
fn is_crash_file(path: &Path) -> bool {
    let name = file_name(path);
    name.starts_with("crash-") && (name.ends_with(".txt") || name.ends_with(".dmp"))
}

#[derive(Debug, Clone, PartialEq)]
pub struct Crash {
    pub report: PathBuf,
    pub dump: Option<PathBuf>,
}

/// The newest crash report across the running game's loader folder and the
/// kept sessions, with its minidump beside it when one exists.
pub fn newest_crash(store: &Store, game: Option<&Path>, sessions: &[PathBuf]) -> Option<Crash> {
    let mut dirs: Vec<PathBuf> = Vec::new();
    if let Some(game) = game {
        dirs.push(game.join(LOADER_DIR));
        let root = crate::overlayfs::files_root(&store.root, game);
        if root != game {
            dirs.push(root.join(LOADER_DIR));
            dirs.push(store.logs_dir());
        }
    }
    for s in sessions {
        dirs.push(s.clone());
        dirs.push(s.join("game-logs"));
    }
    let mut reports: Vec<PathBuf> = dirs
        .iter()
        .flat_map(|d| std::fs::read_dir(d).into_iter().flatten().flatten())
        .map(|e| e.path())
        .filter(|p| {
            let n = file_name(p);
            n.starts_with("crash-") && n.ends_with(".txt") && p.is_file()
        })
        .collect();
    // The names carry a sortable UTC-ish stamp, so the last by name is the newest; modified time
    // breaks ties and covers a clock change.
    reports.sort_by(|a, b| {
        modified(a)
            .cmp(&modified(b))
            .then_with(|| file_name(a).cmp(&file_name(b)))
    });
    let report = reports.pop()?;
    let dump = report.with_extension("dmp");
    Some(Crash {
        dump: dump.is_file().then_some(dump),
        report,
    })
}

/// A short summary of the crash report at `path`, for `swofty logs`.
pub fn summarise_crash(path: &Path) -> std::io::Result<Vec<String>> {
    Ok(crash_excerpt(&String::from_utf8_lossy(&read_head(
        path, PERF_READ,
    )?)))
}

/// The head of a crash report, cut to its most useful lines: the header
/// fields, the exception and the first stack frames.
fn crash_excerpt(text: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut section = "";
    let mut stack = 0;
    let kept = |s: &str| {
        s == "Exception"
            || s == "Ne heap"
            || s == "Game state (from the black box)"
            || s.starts_with("Stack (")
    };
    for line in text.lines() {
        if let Some(name) = line.strip_prefix("== ") {
            section = name;
            if kept(section) {
                out.push(String::new());
                out.push(line.to_owned());
            }
            continue;
        }
        if out.len() > 400 {
            break;
        }
        if section.is_empty() {
            // The header block, up to the blank line before the first section.
            if line.is_empty() {
                continue;
            }
            out.push(line.to_owned());
        } else if section == "Exception"
            || section == "Ne heap"
            || section == "Game state (from the black box)"
        {
            out.push(line.to_owned());
        } else if section.starts_with("Stack (") && stack < 12 {
            out.push(line.to_owned());
            stack += 1;
        }
    }
    out
}

/// The stock check's last verdict and the EA AntiCheat launcher files in the
/// game root, so a blocked player's bundle shows which file failed and what EA
/// rewrote.
fn game_files_section(store: &Store, game: Option<&Path>) -> String {
    let mut s = String::new();
    match crate::stock::Report::read(&store.root) {
        Some(r) if !r.detail.is_empty() => {
            s.push_str(&format!("  last launch stopped: {}\n", r.title()));
            for f in &r.detail {
                s.push_str(&format!(
                    "  {} (measured {}, size {}, Steam {}, modified {})\n",
                    f.rel,
                    f.measured,
                    f.size.map_or("missing".to_owned(), |n| n.to_string()),
                    f.steam,
                    f.modified.map_or("unknown".to_owned(), |t| format!("{t:.0}"))
                ));
            }
        }
        Some(r) => s.push_str(&format!(
            "  last launch stopped: {} ({})\n",
            r.title(),
            r.files.join(", ")
        )),
        None => s.push_str("  No stock-check failure on record\n"),
    }
    let eac = game.map(eac_files).unwrap_or_default();
    if eac.is_empty() {
        s.push_str("  No EAAntiCheat.* files found in the game folder\n");
    }
    for line in eac {
        s.push_str(&format!("  {line}\n"));
    }
    if let Some(line) = game
        .and_then(crate::compat::virtual_store)
        .and_then(|v| virtualised(&v))
    {
        s.push_str(&format!("  {line}\n"));
    }
    s
}

/// The game files Windows virtualised for this user. A process it redirects
/// writes them to the VirtualStore, where F1 25 never reads them.
fn virtualised(copy: &Path) -> Option<String> {
    const SHOWN: usize = 6;
    let files = files_under(copy, 6);
    let names: Vec<&str> = files.iter().take(SHOWN).map(|(rel, _)| rel.as_str()).collect();
    (!files.is_empty()).then(|| {
        format!(
            "Windows keeps {} file(s) written to the game folder in {}, where F1 25 doesn't read them: {}{}",
            files.len(),
            copy.display(),
            names.join(", "),
            if files.len() > SHOWN { ", ..." } else { "" }
        )
    })
}

fn eac_files(game: &Path) -> Vec<String> {
    let mut out: Vec<String> = std::fs::read_dir(game)
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| {
            let name = e.file_name().to_string_lossy().into_owned();
            if !name.to_ascii_lowercase().starts_with("eaanticheat.") {
                return None;
            }
            let meta = e.metadata().ok();
            let size = meta.as_ref().map_or("?".to_owned(), |m| m.len().to_string());
            let modified = meta
                .as_ref()
                .and_then(|m| m.modified().ok())
                .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
                .map_or("?".to_owned(), |d| d.as_secs().to_string());
            Some(format!("{name} (size {size}, modified {modified})"))
        })
        .collect();
    out.sort();
    out
}

fn state_logs(store: &Store) -> Vec<PathBuf> {
    let mut out: Vec<PathBuf> = std::fs::read_dir(store.root.join("state"))
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|p| {
            p.extension().is_some_and(|x| x == "log")
                && std::fs::symlink_metadata(p).is_ok_and(|m| m.is_file())
        })
        .collect();
    out.sort_by_key(|p| {
        let name = file_name(p);
        (
            !matches!(name.as_str(), "launcher-run.log" | "wrapper-run.log"),
            name,
        )
    });
    out
}

pub fn newest_sessions(store: &Store, n: usize) -> Vec<PathBuf> {
    let mut dirs: Vec<PathBuf> = std::fs::read_dir(store.sessions_dir())
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
        .map(|e| e.path())
        .filter(|p| file_name(p).starts_with(|c: char| c.is_ascii_digit()))
        .collect();
    dirs.sort_by_key(|p| std::cmp::Reverse(file_name(p)));
    dirs.truncate(n);
    dirs
}

fn files_under(dir: &Path, depth: usize) -> Vec<(String, PathBuf)> {
    fn walk(root: &Path, dir: &Path, depth: usize, out: &mut Vec<(String, PathBuf)>) {
        let Ok(rd) = std::fs::read_dir(dir) else {
            return;
        };
        let mut items: Vec<_> = rd.flatten().collect();
        items.sort_by_key(|e| e.file_name());
        for e in items {
            let Ok(t) = e.file_type() else { continue };
            let path = e.path();
            if t.is_file() {
                let rel = path.strip_prefix(root).unwrap_or(&path);
                let rel = rel
                    .components()
                    .map(|c| c.as_os_str().to_string_lossy())
                    .collect::<Vec<_>>()
                    .join("/");
                out.push((rel, path));
            } else if t.is_dir() && depth > 1 {
                walk(root, &path, depth - 1, out);
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, depth, &mut out);
    out
}

/// Converter and other addon logs sent per addon, newest first.
const ADDON_LOGS: usize = 5;

fn helper_logs(store: &Store) -> Vec<(String, PathBuf)> {
    let mut out: Vec<(String, PathBuf)> = std::fs::read_dir(store.root.join("cache/addons"))
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| {
            (
                e.file_name().to_string_lossy().into_owned(),
                e.path().join(crate::helper::LOG_FILE),
            )
        })
        .filter(|(_, log)| log.is_file())
        .collect();
    out.sort();
    out
}

/// The newest `.log` files addons keep in `addon-data/<id>/logs/`, such as
/// the Simplified Importer's converter logs.
fn addon_logs(store: &Store) -> Vec<(String, PathBuf)> {
    let mut out = Vec::new();
    for e in std::fs::read_dir(store.root.join("addon-data"))
        .into_iter()
        .flatten()
        .flatten()
    {
        let id = e.file_name().to_string_lossy().into_owned();
        let mut logs: Vec<(std::time::SystemTime, PathBuf)> = std::fs::read_dir(e.path().join("logs"))
            .into_iter()
            .flatten()
            .flatten()
            .map(|f| f.path())
            .filter(|p| p.extension().is_some_and(|x| x == "log") && p.is_file())
            .filter_map(|p| Some((std::fs::metadata(&p).ok()?.modified().ok()?, p)))
            .collect();
        logs.sort_by_key(|l| std::cmp::Reverse(l.0));
        out.extend(logs.into_iter().take(ADDON_LOGS).map(|(_, p)| (id.clone(), p)));
    }
    out.sort();
    out
}

fn addons_summary(store: &Store) -> Value {
    let manifests = store.addons();
    let installed = match &manifests {
        Ok(addons) => Value::Array(
            addons
                .values()
                .map(|m| {
                    json!({
                        "id": m.id,
                        "name": m.name,
                        "version": m.version.to_string(),
                        "builtin": store.is_builtin(&m.id),
                        "source": addon_source(store, m),
                        "category": m.category,
                        "kind": m.kind,
                        "origin": m.origin,
                        "experimental_features": m.experimental_features,
                    })
                })
                .collect(),
        ),
        Err(e) => json!({ "error": e.to_string() }),
    };
    let name = |id: &str| {
        manifests
            .as_ref()
            .ok()
            .and_then(|a| a.get(id))
            .map(|m| m.name.clone())
    };
    let active = store.active_profile();
    let profiles = match store.profile_names() {
        Ok(names) => Value::Array(
            names
                .iter()
                .map(|n| match store.profile(n) {
                    Ok(p) => json!({
                        "name": n,
                        "active": *n == active,
                        "addons": p.addons.iter().map(|a| {
                            let mut entry = json!({ "id": a.id, "name": name(&a.id), "enabled": a.enabled });
                            if let Some(options) = p.options.get(&a.id) {
                                entry["options"] = json!(options);
                            }
                            entry
                        }).collect::<Vec<_>>(),
                    }),
                    Err(e) => json!({ "name": n, "error": e.to_string() }),
                })
                .collect(),
        ),
        Err(e) => json!({ "error": e.to_string() }),
    };
    let unreadable: Vec<Value> = store
        .unreadable_addons()
        .iter()
        .map(|u| json!({ "id": u.id, "name": u.name, "version": u.version, "reason": u.reason }))
        .collect();
    let mut out = json!({ "installed": installed, "profiles": profiles });
    if !unreadable.is_empty() {
        out["unreadable"] = Value::Array(unreadable);
    }
    redact_json(&mut out);
    out
}

/// Where an addon came from, as the Addons page tags it.
fn addon_source(store: &Store, m: &AddonManifest) -> String {
    let tool = m.origin.as_ref().map(|o| o.tool.as_str());
    if store.is_builtin(&m.id) || m.id == crate::managed::ID {
        "built in".into()
    } else if tool == Some(crate::dbedit::package::ORIGIN_TOOL)
        || crate::dbedit::package::is_package(store, &m.id)
    {
        "DB Editor".into()
    } else if tool == Some(crate::managed::ID) {
        match m.origin.as_ref().and_then(|o| o.file.as_deref()) {
            Some(file) => format!("imported from {file}"),
            None => "imported".into(),
        }
    } else {
        "installed".into()
    }
}

fn setting_text(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        other => other.to_string(),
    }
}

/// The active profile's addons by name: those on in load order, then the rest,
/// each with its source and the settings changed from their defaults.
fn addon_lines(store: &Store) -> String {
    let addons = match store.addons() {
        Ok(a) => a,
        Err(e) => return format!("  could not list the addons: {e}\n"),
    };
    let active = store.active_profile();
    let p = match store.profile(&active) {
        Ok(p) => p,
        Err(e) => {
            let mut s = format!("  Active profile: {active} (could not be read: {e})\n");
            for m in addons.values() {
                s.push_str(&format!("  {} {} ({})\n", m.name, m.version, m.id));
            }
            return s;
        }
    };
    let row = |id: &str| match addons.get(id) {
        Some(m) => {
            let mut row = format!(
                "{} {} ({}), {}\n",
                m.name,
                m.version,
                m.id,
                addon_source(store, m)
            );
            let stored = p.options.get(id);
            let declared = m.options.iter().map(|o| (o.id.as_str(), Some(o)));
            let stale = stored
                .into_iter()
                .flat_map(|v| v.keys())
                .filter(|k| !m.options.iter().any(|o| o.id == **k))
                .map(|k| (k.as_str(), None));
            for (option, o) in declared.chain(stale) {
                let Some(value) = stored.and_then(|v| v.get(option)) else {
                    continue;
                };
                let shown = |v: &Value| match secret_name(option) {
                    true => REDACTED.to_owned(),
                    false => setting_text(v),
                };
                let label = o.and_then(|o| o.label.as_deref()).unwrap_or(option);
                let default = o.map_or_else(String::new, |o| format!(" (default {})", shown(&o.default)));
                row.push_str(&format!("          {label}: {}{default}\n", shown(value)));
            }
            row
        }
        None => format!("{id} (not installed)\n"),
    };
    let on: Vec<&str> = p.enabled().collect();
    let mut s = format!(
        "  Active profile: {active}, {} of {} addons on\n",
        on.len(),
        addons.len()
    );
    s.push_str("  On, in load order (a later addon wins where two change the same thing):\n");
    for (i, id) in on.iter().enumerate() {
        s.push_str(&format!("  {:>4}. {}", i + 1, row(id)));
    }
    let off: Vec<&str> = addons
        .keys()
        .map(String::as_str)
        .filter(|id| !on.contains(id))
        .collect();
    if !off.is_empty() {
        s.push_str("  Off:\n");
        for id in off {
            s.push_str(&format!("        {}", row(id)));
        }
    }
    let others: Vec<String> = store
        .profile_names()
        .unwrap_or_default()
        .into_iter()
        .filter(|n| *n != active)
        .map(|n| match store.profile(&n) {
            Ok(o) => format!("{n} ({} on)", o.enabled().count()),
            Err(_) => n,
        })
        .collect();
    let unreadable = store.unreadable_addons();
    if !unreadable.is_empty() {
        s.push_str("  Can't be used:\n");
        for u in &unreadable {
            s.push_str(&format!("        {}\n", u.line()));
        }
    }
    if !others.is_empty() {
        s.push_str(&format!("  Other profiles: {}\n", others.join(", ")));
    }
    s
}

/// A field or variable name that may hold a secret.
pub fn secret_name(name: &str) -> bool {
    let n = name.to_ascii_lowercase();
    const PARTS: [&str; 10] = [
        "token",
        "secret",
        "passw",
        "passphrase",
        "key",
        "credential",
        "cookie",
        "private",
        "webhook",
        "bearer",
    ];
    PARTS.iter().any(|p| n.contains(p))
        || n == "auth"
        || n.starts_with("auth_")
        || n.ends_with("_auth")
        || n.contains("_auth_")
        || n.contains("oauth")
}

fn secret_value(value: &str) -> bool {
    let v = value.to_ascii_lowercase();
    v.contains("/api/webhooks/") || v.contains("discord.com/api") && v.contains("token")
}

/// Replace every secret-looking value in a JSON document; returns how many.
pub fn redact_json(v: &mut Value) -> usize {
    match v {
        Value::Object(map) => map
            .iter_mut()
            .map(|(k, v)| {
                if secret_name(k) && !v.is_null() {
                    *v = Value::String(REDACTED.into());
                    1
                } else {
                    redact_json(v)
                }
            })
            .sum(),
        Value::Array(items) => items.iter_mut().map(redact_json).sum(),
        Value::String(s) if secret_value(s) => {
            *s = REDACTED.into();
            1
        }
        _ => 0,
    }
}

/// `NAME=value` lines with a secret-looking name or value get `NAME=[redacted]`.
pub fn redact_env(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    for line in text.split_inclusive('\n') {
        let body = line.trim_end_matches(['\r', '\n']);
        let end = &line[body.len()..];
        match body.split_once('=') {
            Some((name, value)) if secret_name(name) || secret_value(value) => {
                out.push_str(name);
                out.push('=');
                out.push_str(REDACTED);
                out.push_str(end);
            }
            _ => out.push_str(line),
        }
    }
    out
}

fn file_safe(name: &str) -> String {
    let s: String = name
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-') {
                c
            } else {
                '_'
            }
        })
        .collect();
    let s = s.trim_matches(['.', '_']).to_owned();
    if s.is_empty() { "player".into() } else { s }
}

pub fn size(bytes: u64) -> String {
    match bytes {
        b if b < 1024 => format!("{b} B"),
        b if b < 1 << 20 => format!("{:.1} KB", b as f64 / 1024.),
        b => format!("{:.1} MB", b as f64 / f64::from(1 << 20)),
    }
}

fn now_secs() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs() as i64)
}

/// `YYYYMMDD-HHMMSS` in UTC.
pub fn utc_stamp(secs: i64) -> String {
    let (days, rem) = (secs.div_euclid(86_400), secs.rem_euclid(86_400));
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + i64::from(m <= 2);
    format!(
        "{y:04}{m:02}{d:02}-{:02}{:02}{:02}",
        rem / 3600,
        rem % 3600 / 60,
        rem % 60
    )
}

fn readable(stamp: &str) -> String {
    match (
        stamp.get(..4),
        stamp.get(4..6),
        stamp.get(6..8),
        stamp.get(9..11),
        stamp.get(11..13),
        stamp.get(13..15),
    ) {
        (Some(y), Some(mo), Some(d), Some(h), Some(mi), Some(s)) if stamp.len() == 15 => {
            format!("{y}-{mo}-{d} {h}:{mi}:{s} UTC")
        }
        _ => stamp.to_owned(),
    }
}

pub fn os_description() -> String {
    if cfg!(windows) {
        return windows_version();
    }
    let release = std::fs::read_to_string("/etc/os-release").unwrap_or_default();
    let field = |k: &str| {
        release
            .lines()
            .find_map(|l| l.strip_prefix(k)?.strip_prefix('='))
            .map(|v| v.trim().trim_matches('"').to_owned())
            .filter(|v| !v.is_empty())
    };
    let distro = field("PRETTY_NAME")
        .or_else(|| field("NAME"))
        .unwrap_or_else(|| std::env::consts::OS.to_owned());
    let kernel = std::fs::read_to_string("/proc/sys/kernel/osrelease")
        .map(|k| k.trim().to_owned())
        .unwrap_or_default();
    let flatpak = std::path::Path::new("/.flatpak-info").exists();
    let mut s = format!("Linux · {distro}");
    if !kernel.is_empty() {
        s.push_str(&format!(" (kernel {kernel})"));
    }
    if flatpak {
        s.push_str(" · Flatpak");
    }
    s
}

fn windows_version() -> String {
    let ver = crate::compat::windows_tool("cmd", &["/C", "ver"]).unwrap_or_default();
    let number = ver
        .split_once("Version ")
        .map(|(_, v)| v.trim().trim_end_matches(']').trim().to_owned())
        .unwrap_or_default();
    let build: u32 = number.split('.').nth(2).and_then(|b| b.parse().ok()).unwrap_or(0);
    let display = crate::compat::windows_tool(
        "reg",
        &[
            "query",
            r"HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion",
            "/v",
            "DisplayVersion",
        ],
    )
    .and_then(|out| {
        out.lines()
            .find_map(|l| l.split("REG_SZ").nth(1).map(|v| v.trim().to_owned()))
    })
    .filter(|v| !v.is_empty());
    let name = if build >= 22_000 {
        "Windows 11"
    } else {
        "Windows 10"
    };
    match (number.is_empty(), display) {
        (true, _) => "Windows".into(),
        (false, Some(d)) => format!("{name} {d} ({number})"),
        (false, None) => format!("{name} ({number})"),
    }
}

fn hardware() -> String {
    let threads = std::thread::available_parallelism().map_or(0, |n| n.get());
    let cpu = if cfg!(windows) {
        std::env::var("PROCESSOR_IDENTIFIER").ok()
    } else {
        std::fs::read_to_string("/proc/cpuinfo").ok().and_then(|c| {
            c.lines().find_map(|l| {
                l.strip_prefix("model name")?
                    .split_once(':')
                    .map(|(_, v)| v.trim().to_owned())
            })
        })
    }
    .unwrap_or_else(|| "unknown CPU".into());
    let memory = windows_memory()
        .map(|(ram, commit)| memory_text(ram, Some(commit)))
        .or_else(|| {
            let kb: u64 = std::fs::read_to_string("/proc/meminfo")
                .ok()?
                .lines()
                .find_map(|l| l.strip_prefix("MemTotal:"))?
                .trim()
                .trim_end_matches("kB")
                .trim()
                .parse()
                .ok()?;
            Some(memory_text(kb * 1024, None))
        });
    format!("{cpu} ({threads} threads){}", memory.unwrap_or_default())
}

/// `ram` and Windows' commit limit (`commit`, RAM plus the page file), in bytes.
fn memory_text(ram: u64, commit: Option<u64>) -> String {
    let gb = |b: u64| format!("{:.0} GB", b as f64 / 1024. / 1024. / 1024.);
    match commit {
        Some(c) => format!(
            ", {} memory, {} page file ({} commit limit)",
            gb(ram),
            gb(c.saturating_sub(ram)),
            gb(c)
        ),
        None => format!(", {} memory", gb(ram)),
    }
}

/// The PC's RAM and commit limit in bytes.
#[cfg(windows)]
fn windows_memory() -> Option<(u64, u64)> {
    #[repr(C)]
    #[derive(Default)]
    struct MemoryStatus {
        length: u32,
        load: u32,
        total_phys: u64,
        avail_phys: u64,
        total_page_file: u64,
        avail_page_file: u64,
        total_virtual: u64,
        avail_virtual: u64,
        avail_extended_virtual: u64,
    }
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GlobalMemoryStatusEx(status: *mut MemoryStatus) -> i32;
    }
    let mut status = MemoryStatus {
        length: std::mem::size_of::<MemoryStatus>() as u32,
        ..MemoryStatus::default()
    };
    // SAFETY: `status` is a MEMORYSTATUSEX with its length set, and it
    // outlives the call.
    let ok = unsafe { GlobalMemoryStatusEx(&mut status) } != 0;
    ok.then_some((status.total_phys, status.total_page_file))
}

#[cfg(not(windows))]
fn windows_memory() -> Option<(u64, u64)> {
    None
}

#[cfg(test)]
mod tests;
