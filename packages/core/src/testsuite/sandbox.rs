//! The test run folder (docs/design/std-2/test-suite.md "The test run"): the
//! one contract between `swofty test` and the core. A game start that runs
//! tests gets `<data>/test-runs/<run>/` with a `run.json` holding a fresh
//! token, and the game is started with [`RUN_ENV`] (the folder) and
//! [`TOKEN_ENV`] (the token). With both and a matching `run.json` the core
//! arms the test agent, whose mailbox is `agent/`; when `run.json` names
//! `saves`, the core also replaces the save device's Steam remote storage
//! with `saves/remote/` (native/src/core/save_sandbox.cpp), or quits the
//! game before the title screen when it can't, and writes [`STATUS_FILE`].
//! After the run the token is removed from `run.json`, so the folder alone
//! never switches test mode on again.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde::Deserialize;
use serde_json::{Value, json};

pub const RUN_ENV: &str = "SWOFTY_TEST_RUN";
pub const TOKEN_ENV: &str = "SWOFTY_TEST_TOKEN";
/// The loader loads the core at the game's first mutex instead of its
/// window, so the sandbox's hook is in place before the save device starts.
pub const EARLY_ENV: &str = "SWOFTY_EARLY_STAGE";
pub const RUNS_DIR: &str = "test-runs";
pub const RUN_FILE: &str = "run.json";
pub const STATUS_FILE: &str = "sandbox.json";
pub const SAVES: &str = "saves";
pub const REMOTE: &str = "remote";
pub const STORAGE: &str = "storage";
/// The test agent's mailbox inside the run folder.
pub const AGENT: &str = "agent";
pub const DLL_DIR: &str = "dll";

/// The bindings the core needs to install the sandbox. A career run is
/// refused unless the core's binding set for the game's exe carries them.
pub const BINDINGS: &[&str] = &[
    "Ne_SaveSystem2Win_Start",
    "Ne_SaveSystem2Win_Mount",
    "Ne_SaveSystem2Win_WriteInfoXml",
];

pub const HOOKED_LINE: &str = "[saves] test sandbox hooked";
pub const ARMED_LINE: &str = "[saves] test sandbox armed";
pub const FAILED_LINE: &str = "[saves] TEST SANDBOX FAILED";

pub fn runs_dir(data: &Path) -> PathBuf {
    data.join(RUNS_DIR)
}

#[derive(Debug, Clone)]
pub struct RunDir {
    pub root: PathBuf,
    pub id: String,
    token: String,
    options: RunOptions,
}

/// What a run folder holds besides its token.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct RunOptions {
    /// The save sandbox: `saves/remote/`, `"saves"` in `run.json` and the
    /// early load stage.
    pub sandbox: bool,
    pub exe_sha256: Option<String>,
    /// Test DLLs, copied into `agent/dll/<name>` before `run.json` is
    /// written: (source, name).
    pub dlls: Vec<(PathBuf, String)>,
}

/// What the core wrote into [`STATUS_FILE`].
#[derive(Debug, Clone, Deserialize, PartialEq)]
pub struct Status {
    pub state: String,
    #[serde(default)]
    pub why: String,
    #[serde(default)]
    pub remote: String,
    #[serde(default)]
    pub pid: u64,
}

impl Status {
    pub fn armed(&self) -> bool {
        self.state == "armed"
    }
}

fn token() -> std::io::Result<String> {
    use ring::rand::SecureRandom;
    let mut b = [0u8; 16];
    ring::rand::SystemRandom::new()
        .fill(&mut b)
        .map_err(|_| std::io::Error::other("no system randomness for the run token"))?;
    Ok(b.iter().map(|x| format!("{x:02x}")).collect())
}

fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && id != "."
        && id != ".."
        && id
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '-' | '_' | '.'))
}

impl RunDir {
    /// A fresh run folder: `agent/` with the test DLLs and an empty command
    /// file, `saves/remote/` for a sandboxed run, then `run.json`. An
    /// existing folder of the same id is replaced.
    pub fn create(data: &Path, id: &str, options: RunOptions) -> std::io::Result<RunDir> {
        if !valid_id(id) {
            return Err(std::io::Error::other(format!("{id:?} is not a usable run id")));
        }
        let root = runs_dir(data).join(id);
        if root.exists() {
            std::fs::remove_dir_all(&root)?;
        }
        let agent = root.join(AGENT);
        std::fs::create_dir_all(agent.join(DLL_DIR))?;
        for (src, name) in &options.dlls {
            if !valid_id(name) {
                return Err(std::io::Error::other(format!(
                    "{name:?} is not a usable test DLL name"
                )));
            }
            std::fs::copy(src, agent.join(DLL_DIR).join(name))?;
        }
        std::fs::write(agent.join(super::agent::COMMANDS), "")?;
        if options.sandbox {
            std::fs::create_dir_all(root.join(SAVES).join(REMOTE))?;
        }
        let run = RunDir {
            root,
            id: id.to_owned(),
            token: token()?,
            options,
        };
        run.write(true)?;
        Ok(run)
    }

    /// A finished run's folder, for `fixture capture`.
    pub fn open(data: &Path, id: &str) -> Option<RunDir> {
        let root = runs_dir(data).join(id);
        (valid_id(id) && root.join(SAVES).join(REMOTE).is_dir()).then(|| RunDir {
            root,
            id: id.to_owned(),
            token: String::new(),
            options: RunOptions {
                sandbox: true,
                ..RunOptions::default()
            },
        })
    }

    /// The newest run folder that has saves.
    pub fn latest(data: &Path) -> Option<RunDir> {
        let mut runs: Vec<(std::time::SystemTime, String)> = std::fs::read_dir(runs_dir(data))
            .ok()?
            .flatten()
            .filter(|e| e.path().join(SAVES).join(REMOTE).is_dir())
            .filter_map(|e| {
                let t = e.metadata().and_then(|m| m.modified()).ok()?;
                Some((t, e.file_name().to_string_lossy().into_owned()))
            })
            .collect();
        runs.sort();
        let (_, id) = runs.pop()?;
        RunDir::open(data, &id)
    }

    /// `run.json`: `{"schema":1,"run","token","saves"?,"exe_sha256"?,"agent":{"v","dlls"}}`.
    fn write(&self, with_token: bool) -> std::io::Result<()> {
        let dlls: Vec<String> = self
            .options
            .dlls
            .iter()
            .map(|(_, name)| format!("{DLL_DIR}/{name}"))
            .collect();
        let mut doc = json!({
            "schema": 1,
            "run": self.id,
            "agent": {"v": super::agent::PROTOCOL, "dlls": dlls},
        });
        if with_token {
            doc["token"] = Value::String(self.token.clone());
        }
        if self.options.sandbox {
            doc["saves"] = Value::String(SAVES.to_owned());
        }
        if let Some(s) = &self.options.exe_sha256 {
            doc["exe_sha256"] = Value::String(s.clone());
        }
        let path = self.root.join(RUN_FILE);
        let tmp = path.with_extension("tmp");
        std::fs::write(&tmp, serde_json::to_vec_pretty(&doc).expect("serialisable"))?;
        std::fs::rename(&tmp, &path)
    }

    pub fn sandboxed(&self) -> bool {
        self.options.sandbox
    }

    /// The test agent's mailbox: `commands.jsonl`, `results.jsonl`, `dll/`
    /// and `frames/`.
    pub fn agent(&self) -> PathBuf {
        self.root.join(AGENT)
    }

    pub fn saves(&self) -> PathBuf {
        self.root.join(SAVES)
    }

    pub fn remote(&self) -> PathBuf {
        self.saves().join(REMOTE)
    }

    /// The addon storage kept from this run, `storage/<id>/`.
    pub fn storage(&self) -> PathBuf {
        self.root.join(STORAGE)
    }

    /// The environment the game is started with: the run folder and its
    /// token, and for the sandbox the early load stage.
    pub fn env(&self) -> Vec<(String, String)> {
        let mut env = vec![
            (RUN_ENV.to_owned(), self.root.display().to_string()),
            (TOKEN_ENV.to_owned(), self.token.clone()),
        ];
        if self.options.sandbox {
            env.push((EARLY_ENV.to_owned(), "1".to_owned()));
        }
        env
    }

    pub fn status(&self) -> Option<Status> {
        let text = std::fs::read(self.root.join(STATUS_FILE)).ok()?;
        serde_json::from_slice(&text).ok()
    }

    /// Remove the token from `run.json`: the folder can't switch test mode
    /// on again and the agent stops. Saves and status stay for capture and
    /// the report.
    pub fn retire(&self) -> std::io::Result<()> {
        self.write(false)
    }

    /// Remove the whole folder: a run without the sandbox keeps nothing the
    /// batch folder doesn't already have.
    pub fn remove(&self) -> std::io::Result<()> {
        match std::fs::remove_dir_all(&self.root) {
            Err(e) if e.kind() != std::io::ErrorKind::NotFound => Err(e),
            _ => Ok(()),
        }
    }
}

/// The core's binding set for an exe: `bindings.json` for the KB build,
/// `bindings.<build>.json` for the others, chosen by the exe's sha256.
fn binding_sets(loader: &Path) -> Vec<PathBuf> {
    let core = loader.join("swofty/addons/core");
    let mut out: Vec<PathBuf> = std::fs::read_dir(&core)
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|p| {
            p.file_name()
                .and_then(|n| n.to_str())
                .is_some_and(|n| n.starts_with("bindings") && n.ends_with(".json"))
        })
        .collect();
    out.sort();
    out
}

/// Whether the core can install the sandbox on the exe whose sha256 is
/// `exe_sha256`: its binding set for that build carries every one of
/// [`BINDINGS`]. The gate that exports a binding (re/tools/bindgen.py) needs
/// an L2 finding with an independent `agree`, so this is the static proof;
/// the core still checks the device and fails closed in the game.
pub fn verified(loader: &Path, exe_sha256: &str) -> Result<String, String> {
    let sets = binding_sets(loader);
    if sets.is_empty() {
        return Err(format!(
            "the loader at {} has no binding sets (swofty/addons/core/bindings*.json)",
            loader.display()
        ));
    }
    for set in &sets {
        let Ok(doc) = std::fs::read(set)
            .map_err(|e| e.to_string())
            .and_then(|b| serde_json::from_slice::<Value>(&b).map_err(|e| e.to_string()))
        else {
            continue;
        };
        if doc["build"]["sha256"].as_str() != Some(exe_sha256) {
            continue;
        }
        let have: BTreeMap<&str, &str> = doc["bindings"]
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(|b| Some((b["id"].as_str()?, b["status"].as_str().unwrap_or("?"))))
            .collect();
        let missing: Vec<&str> = BINDINGS
            .iter()
            .copied()
            .filter(|id| !have.contains_key(id))
            .collect();
        if !missing.is_empty() {
            return Err(format!(
                "the core's bindings for this F1_25.exe ({}) lack {}: the save sandbox needs them verified (an \
                 independent agree row in re/db/verify.tsv) before a career test can run",
                set.file_name().unwrap_or_default().to_string_lossy(),
                missing.join(", ")
            ));
        }
        let status: Vec<String> = BINDINGS.iter().map(|id| format!("{id} {}", have[id])).collect();
        return Ok(format!(
            "{} ({})",
            set.file_name().unwrap_or_default().to_string_lossy(),
            status.join(", ")
        ));
    }
    Err(format!(
        "no binding set of the loader at {} is for this F1_25.exe (sha256 {}): the save sandbox is not verified for \
         this build",
        loader.display(),
        &exe_sha256[..exe_sha256.len().min(16)]
    ))
}

/// Names, sizes and modification times of the files in the player's Steam
/// save folder: only metadata, never contents. A sandboxed run compares it
/// before and after; any change is reported as the sandbox's failure.
pub fn listing(dir: &Path) -> BTreeMap<String, (u64, Option<std::time::SystemTime>)> {
    std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| {
            let m = e.metadata().ok()?;
            m.is_file().then(|| {
                (
                    e.file_name().to_string_lossy().into_owned(),
                    (m.len(), m.modified().ok()),
                )
            })
        })
        .collect()
}

/// What changed between two [`listing`]s, as text for the report.
pub fn changed(
    before: &BTreeMap<String, (u64, Option<std::time::SystemTime>)>,
    after: &BTreeMap<String, (u64, Option<std::time::SystemTime>)>,
) -> Vec<String> {
    let mut out = Vec::new();
    for (name, was) in before {
        match after.get(name) {
            None => out.push(format!("{name} was removed")),
            Some(now) if now != was => out.push(format!("{name} changed")),
            _ => {}
        }
    }
    for name in after.keys().filter(|n| !before.contains_key(*n)) {
        out.push(format!("{name} appeared"));
    }
    out
}
