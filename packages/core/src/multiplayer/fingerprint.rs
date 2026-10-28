//! What a profile plays, for a lobby to check that every player runs the same gameplay
//! content: the game build, each gameplay addon with its version and source, and the
//! values of their gameplay options. Cosmetic addons and options are left out.

use std::collections::BTreeMap;
use std::path::Path;

use semver::Version;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use sha2::{Digest, Sha256};

use crate::asset::Source as OpSource;
use crate::baseline::{Hashes, Identity, hex, sha256_bytes};
use crate::builtin::{CORE_DB, CORE_DLL, CORE_ID, CORE_MANIFEST, loader_dir, loader_files};
use crate::catalog::{self, Addon};
use crate::manifest::{AddonManifest, Kind, Multiplayer};
use crate::profile::Profile;
use crate::store::Store;
use crate::{Result, read_json, write_json_shared};

pub const VERSION: u32 = 1;
/// Digests of the addon files [`fingerprint`] hashed, kept apart from the launch-time ones.
pub const HASHES_FILE: &str = "state/fingerprint-hashes.json";
/// What the last build of each profile made the game load, addon by addon; see [`Output`].
pub const OUTPUTS_FILE: &str = "state/fingerprint-outputs.json";

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Fingerprint {
    pub version: u32,
    /// The launcher that made it, for showing; the core's `builtin` source covers it.
    pub launcher: String,
    /// The digest covers only `exe_sha256`; `build_id` is for showing.
    pub game_build: Identity,
    /// Sorted by id.
    pub addons: Vec<AddonPrint>,
    /// Addon id to option id to value.
    pub options: BTreeMap<String, BTreeMap<String, Value>>,
    /// Whether the player owns the game's DLC; `None` while the launcher can't tell.
    pub dlc: Option<bool>,
    /// [`Fingerprint::canonical_digest`] when the fingerprint was made.
    pub digest: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct AddonPrint {
    pub id: String,
    /// For showing; the digest leaves it out.
    pub name: String,
    pub version: Version,
    pub source: Source,
    /// The SHA-256 of the Addon Browser download the copy was installed from, which says what
    /// a fix installs; the digest and the comparison leave it out.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub registry: Option<String>,
    /// For a built-in addon, what its part of the profile's last build made the game load, as
    /// [`Source::Built`] has it; until the profile is built, for an addon with a build hook, a
    /// SHA-256 of what the hook reads: its addon data and its state in the profile.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub inputs: Option<String>,
    /// Its place in the load order among the gameplay addons that change game data, where
    /// a later one can override an earlier one; `None` for one that only loads code.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub order: Option<usize>,
    /// What its part of the last build is made of, for naming what differs from another copy;
    /// the digest leaves it out.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub detail: Option<Detail>,
}

/// The parts and files of an addon's [`Output`], each as the first 12 hex digits of its
/// SHA-256, so two copies whose outputs differ can name the part or file that does.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Detail {
    /// Part label (an operation file kind, `declared`, `picks`, `fields`, `hook` or `native`)
    /// to its hash.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub parts: BTreeMap<String, String>,
    /// The hash of the read list (which files, not their contents) and its length.
    pub read_list: String,
    pub read_count: usize,
    /// One hash per read, in the read list's order; left out of a blob that would be too big.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub reads: Vec<String>,
    /// The read paths, which stay on this side.
    #[serde(skip)]
    pub paths: Vec<String>,
}

/// What the installed copy of an addon is, as a SHA-256 in hex.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Source {
    /// Its installed files: each one's path and SHA-256, in path order.
    Content(String),
    /// A DB Editor addon: the database operations and Driver Manager picks the build makes of
    /// its edits.
    Edits(String),
    /// Built into the launcher: its version and manifest, and for the core its database
    /// operations and DLL.
    Builtin(String),
    /// What its part of the profile's last build made the game load: the [`Output`] it made,
    /// with every file that output reads hashed by content. Taken in place of `Content` and
    /// `Edits` once the profile has been built.
    Built(String),
}

impl Fingerprint {
    pub fn new(
        game_build: Identity,
        mut addons: Vec<AddonPrint>,
        options: BTreeMap<String, BTreeMap<String, Value>>,
        dlc: Option<bool>,
    ) -> Self {
        addons.sort_by(|a, b| a.id.cmp(&b.id));
        let mut f = Self {
            version: VERSION,
            launcher: env!("CARGO_PKG_VERSION").into(),
            game_build,
            addons,
            options,
            dlc,
            digest: String::new(),
        };
        f.digest = f.canonical_digest();
        f
    }

    /// SHA-256 of the fingerprint as JSON with sorted keys and addons, each `order` as its rank
    /// among the placed addons, without `launcher`, `game_build.build_id`, `name`, `registry`
    /// or `digest`.
    pub fn canonical_digest(&self) -> String {
        let mut placed: Vec<(usize, &str)> = self
            .addons
            .iter()
            .filter_map(|a| Some((a.order?, a.id.as_str())))
            .collect();
        placed.sort();
        let rank = |id: &str| placed.iter().position(|(_, p)| *p == id);
        let mut addons: Vec<&AddonPrint> = self.addons.iter().collect();
        addons.sort_by(|a, b| a.id.cmp(&b.id));
        let addons: Vec<Value> = addons
            .into_iter()
            .map(|a| {
                serde_json::json!({
                    "id": a.id,
                    "version": a.version.to_string(),
                    "source": a.source,
                    "inputs": a.inputs,
                    "order": rank(&a.id),
                })
            })
            .collect();
        let doc = serde_json::json!({
            "version": self.version,
            "game_build": { "exe_sha256": self.game_build.exe_sha256.to_ascii_lowercase() },
            "addons": addons,
            "options": self.options,
            "dlc": self.dlc,
        });
        sha256_bytes(&serde_json::to_vec(&sorted(doc)).expect("serialisable"))
    }
}

fn sorted(v: Value) -> Value {
    match v {
        Value::Object(map) => {
            let mut entries: Vec<(String, Value)> = map.into_iter().collect();
            entries.sort_by(|a, b| a.0.cmp(&b.0));
            Value::Object(entries.into_iter().map(|(k, v)| (k, sorted(v))).collect())
        }
        Value::Array(items) => Value::Array(items.into_iter().map(sorted).collect()),
        v => v,
    }
}

/// The fingerprint of `profile` as its build would load it, on the game `game_build`.
pub fn fingerprint(store: &Store, profile: &Profile, game_build: &Identity) -> Result<Fingerprint> {
    let installed = store.addons()?;
    let load = crate::resolver::resolve(profile, &installed)?.order;
    let mut hashes = Hashes::open(store.root.join(HASHES_FILE));
    let mut outputs: BTreeMap<String, BTreeMap<String, Output>> =
        read_json(&store.root.join(OUTPUTS_FILE)).unwrap_or_default();
    let built = outputs.remove(&profile.name).unwrap_or_default();
    let mut addons = Vec::new();
    let mut options = BTreeMap::new();
    let mut place = 0;
    for m in load.iter().map(|id| &installed[id]) {
        if m.multiplayer() == Multiplayer::Cosmetic {
            continue;
        }
        let order = changes_data(m).then(|| {
            place += 1;
            place - 1
        });
        let output = built
            .get(&m.id)
            .map(|o| o.digest(store, &m.id, &mut hashes))
            .transpose()?;
        let detail = output.as_ref().map(|(_, d)| d.clone());
        let builtin = store.is_builtin(&m.id);
        let (source, inputs) = match output.map(|(digest, _)| digest) {
            Some(o) if builtin => (source(store, m, &mut hashes)?, Some(o)),
            Some(o) => (Source::Built(o), None),
            None => (
                source(store, m, &mut hashes)?,
                hook_inputs(store, m, profile, &mut hashes)?,
            ),
        };
        addons.push(AddonPrint {
            id: m.id.clone(),
            name: m.name.clone(),
            version: m.version.clone(),
            source,
            registry: (!crate::dbedit::package::is_package(store, &m.id))
                .then(|| catalog::installed_from(store, m))
                .flatten(),
            inputs,
            order,
            detail,
        });
        let values: BTreeMap<String, Value> = m
            .options
            .iter()
            .filter(|o| o.multiplayer != Some(Multiplayer::Cosmetic))
            .map(|o| {
                let v = profile.option(m, o);
                (o.id.clone(), o.normalise(&v).unwrap_or(v))
            })
            .collect();
        if !values.is_empty() {
            options.insert(m.id.clone(), values);
        }
    }
    hashes.save();
    Ok(Fingerprint::new(game_build.clone(), addons, options, None))
}

fn changes_data(m: &AddonManifest) -> bool {
    let o = &m.operations;
    let ops = [&o.database, &o.erp, &o.ui, &o.assets]
        .iter()
        .any(|l| !l.is_empty())
        || !o.files.is_empty();
    ops || m.launcher.as_ref().is_some_and(|l| l.build) || m.kind.iter().any(|k| *k != Kind::Native)
}

fn source(store: &Store, m: &AddonManifest, hashes: &mut Hashes) -> Result<Source> {
    if store.is_builtin(&m.id) {
        let mut h = Sha256::new();
        h.update(format!("launcher {}\0", env!("CARGO_PKG_VERSION")));
        if m.id == CORE_ID {
            let dll = loader_dir().map(|d| d.join(CORE_DLL)).filter(|p| p.is_file());
            let dll = dll.map(|p| hashes.sha256(&p)).transpose()?;
            h.update(format!("dll {}\0", dll.as_deref().unwrap_or("none")));
            h.update(CORE_MANIFEST);
            h.update(CORE_DB);
        } else {
            h.update(serde_json::to_vec(m).expect("serialisable"));
        }
        return Ok(Source::Builtin(hex(&h.finalize())));
    }
    if crate::dbedit::package::is_package(store, &m.id)
        && let Ok(p) = crate::dbedit::package::Package::open(store, &m.id)
    {
        let built = serde_json::json!({
            "ops": p.edits.to_ops(),
            "picks": crate::dbedit::driver_manager::picks(&p.edits),
        });
        let text = serde_json::to_vec(&sorted(built)).expect("serialisable");
        return Ok(Source::Edits(sha256_bytes(&text)));
    }
    content_hash(&store.addons_dir().join(&m.id), hashes).map(Source::Content)
}

/// What one addon's part of a build made the game load: a SHA-256 of the operations, fields,
/// screens and plugin list it produced, and the files those read. A fingerprint hashes the
/// files by content, so an addon prints the same however it was installed, and files the
/// build never reads, such as logs, import reports and import history, don't count.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Output {
    pub made: String,
    pub reads: Vec<Read>,
    /// `made` split by part label, as [`Detail::parts`] carries them.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub parts: BTreeMap<String, String>,
}

/// A file or folder an [`Output`] reads, as the operation names it.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Read {
    pub addon: String,
    pub source: OpSource,
}

impl Output {
    /// The output's digest, and its [`Detail`]; `id` is the addon the output is of.
    fn digest(&self, store: &Store, id: &str, hashes: &mut Hashes) -> Result<(String, Detail)> {
        let mut h = Sha256::new();
        let mut list = Sha256::new();
        let mut detail = Detail {
            parts: self.parts.iter().map(|(k, v)| (k.clone(), short(v))).collect(),
            read_count: self.reads.len(),
            ..Detail::default()
        };
        h.update(&self.made);
        for r in &self.reads {
            let (root, rel) = (r.source.root, &r.source.path);
            let name = format!("\n{}\0{root:?}\0{rel}", r.addon);
            h.update(&name);
            list.update(&name);
            let mut one = Sha256::new();
            let path = r
                .source
                .resolve(&crate::build::source_dir(store, &r.addon, root))?;
            let mut feed = |text: String| {
                h.update(&text);
                one.update(&text);
            };
            if path.is_dir() {
                for (rel, file) in loader_files(&path)? {
                    if !rel.split('/').any(|c| c.starts_with('.')) {
                        feed(format!("\0{rel}\0{}", hashes.sha256(&file)?));
                    }
                }
            } else if path.is_file() {
                feed(format!("\0{}", hashes.sha256(&path)?));
            } else {
                feed("\0missing".into());
            }
            detail.reads.push(short(&hex(&one.finalize())));
            detail.paths.push(match r.addon == id {
                true => rel.clone(),
                false => format!("{}:{rel}", r.addon),
            });
        }
        detail.read_list = short(&hex(&list.finalize()));
        Ok((hex(&h.finalize()), detail))
    }
}

fn short(digest: &str) -> String {
    digest.get(..12).unwrap_or(digest).to_owned()
}

/// Collects an addon's [`Output`] as the build makes it.
#[derive(Default)]
pub struct OutputBuilder {
    made: Sha256,
    parts: BTreeMap<String, Sha256>,
    reads: BTreeMap<(String, String), OpSource>,
}

impl OutputBuilder {
    pub fn made(&mut self, what: &str, value: &impl Serialize) {
        let json = serde_json::to_vec(value).expect("serialisable");
        for h in [&mut self.made, self.parts.entry(what.to_owned()).or_default()] {
            h.update(format!("{what}\0"));
            h.update(&json);
            h.update(b"\n");
        }
    }

    pub fn reads(&mut self, addon: &str, source: &OpSource) {
        let key = format!("{:?}/{}", source.root, source.path);
        self.reads.insert((addon.to_owned(), key), source.clone());
    }

    pub fn finish(self) -> Output {
        Output {
            made: hex(&self.made.finalize()),
            reads: self
                .reads
                .into_iter()
                .map(|((addon, _), source)| Read { addon, source })
                .collect(),
            parts: self
                .parts
                .into_iter()
                .map(|(label, h)| (label, hex(&h.finalize())))
                .collect(),
        }
    }
}

/// Keeps what the build of `profile` made the game load, in place of its previous build's.
pub fn record_outputs(store: &Store, profile: &str, outputs: BTreeMap<String, Output>) -> Result<()> {
    let path = store.root.join(OUTPUTS_FILE);
    let mut all: BTreeMap<String, BTreeMap<String, Output>> = read_json(&path).unwrap_or_default();
    all.insert(profile.to_owned(), outputs);
    write_json_shared(&path, &all)
}

/// SHA-256 over each file under `dir` as `path\0sha256\n`, paths relative with `/`, in order.
pub fn content_hash(dir: &Path, hashes: &mut Hashes) -> Result<String> {
    let mut h = Sha256::new();
    for (rel, path) in loader_files(dir)? {
        h.update(format!("{rel}\0{}\n", hashes.sha256(&path)?));
    }
    Ok(hex(&h.finalize()))
}

/// The addon data files and profile state a build hook reads, leaving out hidden entries, `logs`
/// folders and `.log` and `.tmp` files. A JSON file or the state counts as [`settled`] JSON,
/// and one that holds only empty containers counts as absent.
fn hook_inputs(
    store: &Store,
    m: &AddonManifest,
    profile: &Profile,
    hashes: &mut Hashes,
) -> Result<Option<String>> {
    let builtin = store.builtins().iter().any(|b| b.addon.manifest().id == m.id);
    if !builtin && !m.launcher.as_ref().is_some_and(|l| l.build) {
        return Ok(None);
    }
    let mut h = Sha256::new();
    let dir = store.addon_data_dir(&m.id);
    let counters = m.id == crate::dbedit::driver_manager::ID;
    let volatile = |rel: &str| {
        rel.split('/').any(|c| c.starts_with('.') || c == "logs")
            || rel.ends_with(".log")
            || rel.ends_with(".tmp")
    };
    if dir.is_dir() {
        for (rel, path) in loader_files(&dir)?.into_iter().filter(|(rel, _)| !volatile(rel)) {
            let json = rel.ends_with(".json")
                && std::fs::metadata(&path).is_ok_and(|meta| meta.len() <= JSON_INPUT_LIMIT);
            let parsed = json
                .then(|| std::fs::read(&path).ok())
                .flatten()
                .and_then(|bytes| serde_json::from_slice::<Value>(&bytes).ok());
            let sha = match parsed {
                Some(v) => match settled(v, counters) {
                    Some(v) => sha256_bytes(&serde_json::to_vec(&v).expect("serialisable")),
                    None => continue,
                },
                None => hashes.sha256(&path)?,
            };
            h.update(format!("{rel}\0{sha}\n"));
        }
    }
    let state = profile
        .state
        .get(&m.id)
        .cloned()
        .and_then(|v| settled(v, counters));
    h.update(format!(
        "\0state\0{}",
        serde_json::to_string(&state).expect("serialisable")
    ));
    Ok(Some(hex(&h.finalize())))
}

/// JSON addon data bigger than this is hashed as bytes.
const JSON_INPUT_LIMIT: u64 = 4 * 1024 * 1024;

/// `v` with sorted keys, and with `counters` without its top-level `next_*` id counters, as
/// Driver Manager keeps them. `None` when, `schema` and `schema_version` aside, it holds only
/// empty arrays and objects.
fn settled(v: Value, counters: bool) -> Option<Value> {
    fn empty(v: &Value) -> bool {
        match v {
            Value::Array(a) => a.is_empty(),
            Value::Object(o) => o.values().all(empty),
            _ => false,
        }
    }
    let v = match v {
        Value::Object(mut o) => {
            o.retain(|k, _| !(counters && k.starts_with("next_")));
            let bare = o
                .iter()
                .all(|(k, v)| k == "schema" || k == "schema_version" || empty(v));
            if bare {
                return None;
            }
            Value::Object(o)
        }
        v if empty(&v) => return None,
        v => v,
    };
    Some(sorted(v))
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Mismatch {
    #[serde(flatten)]
    pub kind: MismatchKind,
    /// The launcher can fix it on its own: turn an addon off, set an option, reorder, or
    /// install the host's exact version from the Addon Browser.
    pub fixable: bool,
    /// For a copy or its data that differs, which part or file does, when both sides sent
    /// their [`Detail`].
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub differences: Vec<String>,
}

/// How the local fingerprint differs from the host's; `local` is this player's side.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum MismatchKind {
    GameBuild {
        local: Identity,
        host: Identity,
    },
    Dlc {
        local: Option<bool>,
        host: Option<bool>,
    },
    /// A built-in addon differs, so the launchers differ: players need the same launcher.
    Launcher {
        local: String,
        host: String,
    },
    /// `registry` in these is the host's download, the one a fix installs.
    MissingAddon {
        id: String,
        name: String,
        version: Version,
        source: Source,
        registry: Option<String>,
    },
    ExtraAddon {
        id: String,
        name: String,
        version: Version,
    },
    VersionDiffers {
        id: String,
        name: String,
        local: Version,
        host: Version,
        registry: Option<String>,
    },
    SourceDiffers {
        id: String,
        name: String,
        version: Version,
        local: Source,
        host: Source,
        registry: Option<String>,
    },
    /// What the addon's build hook reads differs, such as Driver Manager's drivers.
    InputsDiffer {
        id: String,
        name: String,
    },
    /// The data addons both sides have, in each side's load order.
    LoadOrder {
        local: Vec<String>,
        host: Vec<String>,
    },
    OptionDiffers {
        addon: String,
        option: String,
        local: Option<Value>,
        host: Option<Value>,
    },
}

impl Mismatch {
    pub fn line(&self) -> String {
        let value = |v: &Option<Value>| v.as_ref().map_or_else(|| "unset".to_owned(), Value::to_string);
        let text = match &self.kind {
            MismatchKind::GameBuild { local, host } => {
                format!("game build {} is not the host's {}", build(local), build(host))
            }
            MismatchKind::Dlc { local, host } => {
                format!("DLC is {}; the host's is {}", owned(*local), owned(*host))
            }
            MismatchKind::Launcher { local, host } if local == host => {
                format!("this Swofty Launcher {local} is a different build from the host's {host}")
            }
            MismatchKind::Launcher { local, host } => {
                format!(
                    "Swofty Launcher {local} is not the host's {host}; play with the same launcher version"
                )
            }
            MismatchKind::MissingAddon {
                id, name, version, ..
            } => format!("{name} ({id}) {version} is missing"),
            MismatchKind::ExtraAddon { id, name, version } => {
                format!("{name} ({id}) {version} is on; the host doesn't have it")
            }
            MismatchKind::VersionDiffers {
                id,
                name,
                local,
                host,
                ..
            } => format!("{name} ({id}) is {local}; the host has {host}"),
            MismatchKind::SourceDiffers {
                id, name, version, ..
            } => format!("{name} ({id}) {version} is not the same copy as the host's"),
            MismatchKind::InputsDiffer { id, name } => {
                format!("{name} ({id}) has other data or profile state than the host's")
            }
            MismatchKind::LoadOrder { local, host } => format!(
                "load order {} is not the host's {}",
                local.join(", "),
                host.join(", ")
            ),
            MismatchKind::OptionDiffers {
                addon,
                option,
                local,
                host,
            } => format!(
                "{addon}.{option} is {}; the host has {}",
                value(local),
                value(host)
            ),
        };
        if self.fixable {
            format!("{text} (fixable)")
        } else {
            text
        }
    }
}

/// Files named before the rest are counted.
const NAMED_FILES: usize = 5;

/// What differs between two copies of an addon, from their [`Detail`]s: its parts, which files
/// it reads, or the contents of those files, named by path.
pub fn differences(local: &AddonPrint, host: &AddonPrint) -> Vec<String> {
    let built = |s: &Source| matches!(s, Source::Built(_) | Source::Builtin(_));
    let (l, h) = match (&local.detail, &host.detail) {
        (Some(l), Some(h)) => (l, h),
        (Some(_), None) if !built(&host.source) => {
            return vec![
                "the host's game wasn't built with it, so its files count instead; the host needs to start F1 25 again"
                    .into(),
            ];
        }
        (None, Some(_)) if !built(&local.source) => {
            return vec![
                "your game wasn't built with it, so its files count instead; start F1 25 again".into(),
            ];
        }
        _ => return Vec::new(),
    };
    let mut out = Vec::new();
    let labels: std::collections::BTreeSet<&String> = l.parts.keys().chain(h.parts.keys()).collect();
    for label in labels {
        let (a, b) = (l.parts.get(label), h.parts.get(label));
        if a != b {
            let what = part_name(label);
            out.push(match (a, b) {
                (Some(_), None) => format!("{what}: only yours has them"),
                (None, Some(_)) => format!("{what}: only the host's has them"),
                _ => format!("{what} differ"),
            });
        }
    }
    if l.read_list != h.read_list || l.read_count != h.read_count {
        out.push(format!(
            "it reads other files: {} here, {} at the host",
            l.read_count, h.read_count
        ));
    } else if l.reads.len() == l.read_count && h.reads.len() == h.read_count {
        let differ: Vec<usize> = (0..l.read_count).filter(|&i| l.reads[i] != h.reads[i]).collect();
        let path = |i: usize| {
            l.paths
                .get(i)
                .cloned()
                .unwrap_or_else(|| format!("file {}", i + 1))
        };
        out.extend(
            differ
                .iter()
                .take(NAMED_FILES)
                .map(|&i| format!("{} differs", path(i))),
        );
        if differ.len() > NAMED_FILES {
            out.push(format!("and {} more file(s) differ", differ.len() - NAMED_FILES));
        }
    } else if out.is_empty() {
        out.push(format!(
            "one of its {} file(s) may differ; the per-file hashes were left out",
            l.read_count
        ));
    }
    if out.is_empty() {
        out.push("its parts and files match, but not their order".into());
    }
    out
}

fn part_name(label: &str) -> String {
    match label {
        "Database" => "its database operations".into(),
        "Erp" => "its archive operations".into(),
        "Assets" => "its asset operations".into(),
        "declared" => "its file operations".into(),
        "picks" => "its Driver Manager picks".into(),
        "fields" => "its build step's fields".into(),
        "hook" => "its build step's output".into(),
        "native" => "its plugin list".into(),
        other => format!("its {other}"),
    }
}

fn build(i: &Identity) -> String {
    format!(
        "{} (exe {})",
        i.build_id,
        i.exe_sha256.get(..12).unwrap_or(&i.exe_sha256)
    )
}

fn owned(on: Option<bool>) -> &'static str {
    match on {
        Some(true) => "owned",
        Some(false) => "not owned",
        None => "unknown",
    }
}

/// What `local` must change to match `host`, the game build and DLC first, then addons,
/// the load order and options. `catalog` is the Addon Browser's listing.
pub fn compare(local: &Fingerprint, host: &Fingerprint, catalog: &[Addon]) -> Vec<Mismatch> {
    let launcher = crate::update::current_version();
    let listed = |a: &AddonPrint| {
        a.registry.as_ref().is_some_and(|sha| {
            catalog
                .iter()
                .find(|c| c.id == a.id)
                .and_then(|c| c.listed().into_iter().find(|v| v.version == a.version))
                .is_some_and(|v| v.sha256.trim().eq_ignore_ascii_case(sha) && v.accepts(&launcher))
        })
    };
    let mut out = Vec::new();
    let mut push = |kind, fixable| {
        out.push(Mismatch {
            kind,
            fixable,
            differences: Vec::new(),
        })
    };
    if !local
        .game_build
        .exe_sha256
        .eq_ignore_ascii_case(&host.game_build.exe_sha256)
    {
        push(
            MismatchKind::GameBuild {
                local: local.game_build.clone(),
                host: host.game_build.clone(),
            },
            false,
        );
    }
    if local.dlc != host.dlc {
        push(
            MismatchKind::Dlc {
                local: local.dlc,
                host: host.dlc,
            },
            false,
        );
    }
    let mine: BTreeMap<&str, &AddonPrint> = local.addons.iter().map(|a| (a.id.as_str(), a)).collect();
    let theirs: BTreeMap<&str, &AddonPrint> = host.addons.iter().map(|a| (a.id.as_str(), a)).collect();
    for h in theirs.values() {
        let (id, name) = (h.id.clone(), h.name.clone());
        match mine.get(h.id.as_str()) {
            Some(l)
                if (l.version != h.version || l.source != h.source)
                    && [&l.source, &h.source]
                        .iter()
                        .any(|s| matches!(s, Source::Builtin(_))) =>
            {
                push(
                    MismatchKind::Launcher {
                        local: local.launcher.clone(),
                        host: host.launcher.clone(),
                    },
                    false,
                )
            }
            None => push(
                MismatchKind::MissingAddon {
                    id,
                    name,
                    version: h.version.clone(),
                    source: h.source.clone(),
                    registry: h.registry.clone(),
                },
                listed(h),
            ),
            Some(l) if l.version != h.version => push(
                MismatchKind::VersionDiffers {
                    id,
                    name,
                    local: l.version.clone(),
                    host: h.version.clone(),
                    registry: h.registry.clone(),
                },
                listed(h),
            ),
            Some(l) if l.source != h.source => push(
                MismatchKind::SourceDiffers {
                    id,
                    name,
                    version: h.version.clone(),
                    local: l.source.clone(),
                    host: h.source.clone(),
                    registry: h.registry.clone(),
                },
                listed(h),
            ),
            Some(_) => {}
        }
        if let Some(l) = mine.get(h.id.as_str())
            && l.inputs != h.inputs
        {
            push(
                MismatchKind::InputsDiffer {
                    id: h.id.clone(),
                    name: h.name.clone(),
                },
                false,
            );
        }
    }
    for l in mine.values().filter(|l| !theirs.contains_key(l.id.as_str())) {
        push(
            MismatchKind::ExtraAddon {
                id: l.id.clone(),
                name: l.name.clone(),
                version: l.version.clone(),
            },
            !matches!(l.source, Source::Builtin(_)),
        );
    }
    let shared_order = |f: &Fingerprint, other: &BTreeMap<&str, &AddonPrint>| -> Vec<String> {
        let mut placed: Vec<(usize, &str)> = f
            .addons
            .iter()
            .filter(|a| other.get(a.id.as_str()).is_some_and(|o| o.order.is_some()))
            .filter_map(|a| Some((a.order?, a.id.as_str())))
            .collect();
        placed.sort();
        placed.into_iter().map(|(_, id)| id.to_owned()).collect()
    };
    let (mine_order, their_order) = (shared_order(local, &theirs), shared_order(host, &mine));
    let placed_alike = mine.iter().all(|(id, l)| {
        theirs
            .get(id)
            .is_none_or(|h| l.order.is_some() == h.order.is_some())
    });
    if mine_order != their_order || !placed_alike {
        push(
            MismatchKind::LoadOrder {
                local: mine_order,
                host: their_order,
            },
            true,
        );
    }
    for (addon, values) in host
        .options
        .iter()
        .filter(|(id, _)| mine.contains_key(id.as_str()))
    {
        let ours = local.options.get(addon);
        for (option, h) in values {
            let l = ours.and_then(|o| o.get(option));
            if l != Some(h) {
                push(
                    MismatchKind::OptionDiffers {
                        addon: addon.clone(),
                        option: option.clone(),
                        local: l.cloned(),
                        host: Some(h.clone()),
                    },
                    true,
                );
            }
        }
    }
    for (addon, values) in local
        .options
        .iter()
        .filter(|(id, _)| theirs.contains_key(id.as_str()))
    {
        let hosts = host.options.get(addon);
        for (option, l) in values
            .iter()
            .filter(|(o, _)| hosts.is_none_or(|h| !h.contains_key(*o)))
        {
            push(
                MismatchKind::OptionDiffers {
                    addon: addon.clone(),
                    option: option.clone(),
                    local: Some(l.clone()),
                    host: None,
                },
                false,
            );
        }
    }
    for m in &mut out {
        if let MismatchKind::SourceDiffers { id, .. } | MismatchKind::InputsDiffer { id, .. } = &m.kind
            && let (Some(l), Some(h)) = (mine.get(id.as_str()), theirs.get(id.as_str()))
        {
            m.differences = differences(l, h);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::*;

    fn store(name: &str) -> Store {
        let root = std::env::temp_dir().join(format!("swofty-fingerprint-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        Store::new(root)
    }

    fn put(store: &Store, manifest: Value, files: &[(&str, &str)]) {
        let dir = store.addons_dir().join(manifest["id"].as_str().unwrap());
        write(&dir, manifest, files);
    }

    fn write(dir: &Path, manifest: Value, files: &[(&str, &str)]) {
        std::fs::create_dir_all(dir).unwrap();
        let mut m = json!({
            "schema_version": 1,
            "name": manifest["id"],
            "version": "1.0.0",
            "author": "a",
            "compat": { "game": "f1_25" },
        });
        m.as_object_mut()
            .unwrap()
            .extend(manifest.as_object().unwrap().clone());
        std::fs::write(dir.join("swofty.json"), m.to_string()).unwrap();
        for (rel, text) in files {
            let path = dir.join(rel);
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(path, text).unwrap();
        }
    }

    fn data(id: &str) -> Value {
        json!({ "id": id, "kind": ["database"], "operations": { "database": ["db/ops.json"] } })
    }

    fn code(id: &str) -> Value {
        json!({ "id": id, "kind": ["native"], "operations": { "native": [{ "dll": "native/x.dll" }] } })
    }

    fn profile(ids: &[&str]) -> Profile {
        let mut p = Profile::new("mp");
        for id in ids {
            p.set_enabled(id, true).unwrap();
        }
        p
    }

    fn game() -> Identity {
        Identity {
            build_id: "25251167".into(),
            exe_sha256: "a".repeat(64),
        }
    }

    fn ids(f: &Fingerprint) -> Vec<&str> {
        f.addons.iter().map(|a| a.id.as_str()).collect()
    }

    fn print(id: &str, version: &str, source: Source, order: Option<usize>) -> AddonPrint {
        AddonPrint {
            id: id.into(),
            name: format!("{id} name"),
            version: Version::parse(version).unwrap(),
            source,
            registry: None,
            inputs: None,
            order,
            detail: None,
        }
    }

    fn content(sha: &str) -> Source {
        Source::Content(sha.repeat(64))
    }

    fn installed_from(mut a: AddonPrint, sha: &str) -> AddonPrint {
        a.registry = Some(sha.repeat(64));
        a
    }

    fn options(entries: &[(&str, &str, Value)]) -> BTreeMap<String, BTreeMap<String, Value>> {
        let mut out: BTreeMap<String, BTreeMap<String, Value>> = BTreeMap::new();
        for (addon, option, v) in entries {
            out.entry((*addon).into())
                .or_default()
                .insert((*option).into(), v.clone());
        }
        out
    }

    fn host() -> Fingerprint {
        Fingerprint::new(
            game(),
            vec![
                installed_from(print("t.a", "1.0.0", content("a"), Some(0)), "1"),
                installed_from(print("t.b", "2.0.0", content("b"), Some(1)), "2"),
                print("t.code", "1.0.0", content("c"), None),
            ],
            options(&[("t.a", "laps", json!(5)), ("t.a", "mode", json!("full"))]),
            None,
        )
    }

    fn listing(id: &str, versions: &[(&str, &str)]) -> Addon {
        let versions: Vec<Value> = versions
            .iter()
            .map(|(v, sha)| json!({ "version": v, "size": 1, "sha256": sha.repeat(64), "url": "u" }))
            .collect();
        serde_json::from_value(json!({ "id": id, "name": id, "versions": versions })).unwrap()
    }

    fn kinds(found: &[Mismatch]) -> Vec<(String, bool)> {
        found
            .iter()
            .map(|m| {
                let v = serde_json::to_value(m).unwrap();
                (v["kind"].as_str().unwrap().to_owned(), m.fixable)
            })
            .collect()
    }

    #[test]
    fn digest_is_stable_and_ignores_names_and_listing_order() {
        let f = host();
        assert_eq!(
            f.digest,
            "9547802c5a3ed7f1f2ea3d12b22a21b6f96dbf61fe0f29b65f61fb860a96bbd9"
        );
        let mut shuffled = f.addons.clone();
        shuffled.reverse();
        shuffled[0].name = "Another name".into();
        let again = Fingerprint::new(game(), shuffled, f.options.clone(), None);
        assert_eq!(again.digest, f.digest);
        assert_eq!(ids(&again), ["t.a", "t.b", "t.code"]);

        let text = serde_json::to_string(&f).unwrap();
        let read: Fingerprint = serde_json::from_str(&text).unwrap();
        assert_eq!(read, f);
        assert_eq!(read.canonical_digest(), f.digest);

        let changed = |edit: &dyn Fn(&mut Fingerprint)| {
            let mut c = host();
            edit(&mut c);
            c.canonical_digest()
        };
        for d in [
            changed(&|c| c.addons[0].version = Version::new(1, 0, 1)),
            changed(&|c| c.addons[1].source = content("d")),
            changed(&|c| c.addons[1].inputs = Some("d".repeat(64))),
            changed(&|c| c.addons[0].order = Some(2)),
            changed(&|c| {
                c.options.get_mut("t.a").unwrap().insert("laps".into(), json!(6));
            }),
            changed(&|c| c.game_build.exe_sha256 = "b".repeat(64)),
            changed(&|c| c.dlc = Some(true)),
        ] {
            assert_ne!(d, f.digest);
        }
        assert_eq!(changed(&|c| c.addons[0].registry = None), f.digest);
    }

    #[test]
    fn fingerprints_a_profile_the_same_way_twice() {
        let store = store("stable");
        put(&store, data("t.data1"), &[("db/ops.json", "1")]);
        put(&store, data("t.data2"), &[("db/ops.json", "2")]);
        put(&store, code("t.code1"), &[("native/x.dll", "c1")]);
        put(&store, code("t.code2"), &[("native/x.dll", "c2")]);
        let p = profile(&["t.data1", "t.code1", "t.data2", "t.code2"]);
        let first = fingerprint(&store, &p, &game()).unwrap();
        assert_eq!(
            ids(&first),
            ["swofty.core", "t.code1", "t.code2", "t.data1", "t.data2"]
        );
        let order: Vec<(&str, Option<usize>)> =
            first.addons.iter().map(|a| (a.id.as_str(), a.order)).collect();
        assert_eq!(
            order,
            [
                ("swofty.core", Some(0)),
                ("t.code1", None),
                ("t.code2", None),
                ("t.data1", Some(1)),
                ("t.data2", Some(2)),
            ]
        );
        assert!(store.root.join(HASHES_FILE).is_file());
        assert_eq!(fingerprint(&store, &p, &game()).unwrap(), first);

        let code_swapped = profile(&["t.data1", "t.code2", "t.data2", "t.code1"]);
        assert_eq!(
            fingerprint(&store, &code_swapped, &game()).unwrap().digest,
            first.digest
        );
        let data_swapped = profile(&["t.data2", "t.code1", "t.data1", "t.code2"]);
        assert_ne!(
            fingerprint(&store, &data_swapped, &game()).unwrap().digest,
            first.digest
        );

        std::fs::write(store.addons_dir().join("t.code1/native/x.dll"), "c1 changed").unwrap();
        let edited = fingerprint(&store, &p, &game()).unwrap();
        assert_ne!(edited.addons[1].source, first.addons[1].source);
        assert_eq!(edited.addons[2].source, first.addons[2].source);
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn leaves_out_cosmetic_addons_and_options() {
        let store = store("cosmetic");
        let look = json!({
            "id": "t.look", "experimental_features": ["shaders"], "shader": { "lut": "look.cube" },
        });
        let passes = json!({
            "id": "t.passes", "experimental_features": ["postfx"], "postfx": [{ "shader": "fx/a.hlsl" }],
        });
        let mut helmets = data("t.helmets");
        helmets["category"] = json!("helmets");
        let mut shaders = data("t.shaders");
        shaders["category"] = json!("shaders");
        let mut kept_helmets = data("t.kept-helmets");
        kept_helmets["category"] = json!("helmets");
        kept_helmets["multiplayer"] = json!("gameplay");
        let mut quiet = data("t.quiet");
        quiet["multiplayer"] = json!("cosmetic");
        let mut cars = data("t.cars");
        cars["category"] = json!("cars");
        cars["options"] = json!([
            { "id": "laps", "type": "slider", "min": 1, "max": 10, "step": 1, "default": 5, "binds": "db" },
            { "id": "hud", "type": "toggle", "default": true, "binds": "native", "multiplayer": "cosmetic" },
        ]);
        for m in [look, passes, helmets, shaders, kept_helmets, quiet, cars] {
            put(&store, m, &[]);
        }
        let all = [
            "t.look",
            "t.passes",
            "t.helmets",
            "t.shaders",
            "t.kept-helmets",
            "t.quiet",
            "t.cars",
        ];
        let mut p = profile(&all);
        let installed = store.addons().unwrap();
        p.set_option(&installed["t.cars"], "laps", json!(7)).unwrap();
        let f = fingerprint(&store, &p, &game()).unwrap();
        assert_eq!(ids(&f), ["swofty.core", "t.cars", "t.kept-helmets"]);
        assert_eq!(f.options, options(&[("t.cars", "laps", json!(7))]));

        let without = fingerprint(&store, &profile(&["t.kept-helmets", "t.cars"]), &game()).unwrap();
        let mut seven = profile(&["t.kept-helmets", "t.cars"]);
        seven.set_option(&installed["t.cars"], "laps", json!(7)).unwrap();
        assert_ne!(without.digest, f.digest);
        assert_eq!(fingerprint(&store, &seven, &game()).unwrap().digest, f.digest);
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn manifest_multiplayer_defaults() {
        let m = |extra: Value| {
            let mut doc = json!({
                "schema_version": 1, "id": "t.m", "name": "m", "version": "1.0.0", "author": "a",
                "compat": { "game": "f1_25" },
            });
            doc.as_object_mut()
                .unwrap()
                .extend(extra.as_object().unwrap().clone());
            AddonManifest::parse(&doc.to_string()).unwrap().multiplayer()
        };
        use Multiplayer::{Cosmetic, Gameplay};
        assert_eq!(m(json!({})), Gameplay);
        assert_eq!(m(json!({ "category": "helmets" })), Cosmetic);
        assert_eq!(m(json!({ "category": "shaders" })), Cosmetic);
        assert_eq!(m(json!({ "category": "performance" })), Cosmetic);
        let bundled = include_str!("../../../../addons/re-optimization/swofty.json");
        assert_eq!(AddonManifest::parse(bundled).unwrap().multiplayer(), Cosmetic);
        assert_eq!(m(json!({ "category": "cars" })), Gameplay);
        assert_eq!(
            m(json!({ "category": "helmets", "multiplayer": "gameplay" })),
            Gameplay
        );
        assert_eq!(m(json!({ "multiplayer": "cosmetic" })), Cosmetic);
        let look = json!({ "experimental_features": ["shaders"], "shader": { "lut": "l.cube" } });
        assert_eq!(m(look.clone()), Cosmetic);
        let mut with_data = look;
        with_data["operations"] = json!({ "database": ["db/x.json"] });
        assert_eq!(m(with_data), Gameplay);
        assert!(AddonManifest::parse(r#"{"schema_version":1,"id":"t.m","name":"m","version":"1.0.0","author":"a","compat":{"game":"f1_25"},"multiplayer":"sometimes"}"#).is_err());
    }

    #[test]
    fn a_registry_install_is_recorded_beside_the_content_hash() {
        let store = store("registry");
        put(&store, data("t.a"), &[("db/ops.json", "1")]);
        let p = profile(&["t.a"]);
        let print = |store: &Store| fingerprint(store, &p, &game()).unwrap().addons[1].clone();
        let before = print(&store);
        assert!(matches!(before.source, Source::Content(_)));
        assert_eq!(before.registry, None);

        let mut v: catalog::Version = serde_json::from_value(json!({
            "version": "1.0.0", "size": 1, "sha256": "AB".repeat(32), "url": "u",
        }))
        .unwrap();
        catalog::record_install(&store, "t.a", &v).unwrap();
        let recorded = print(&store);
        assert_eq!(recorded.registry, Some("ab".repeat(32)));
        assert_eq!(recorded.source, before.source);

        v.version = Version::new(0, 9, 0);
        catalog::record_install(&store, "t.a", &v).unwrap();
        assert_eq!(print(&store).registry, None);

        v.version = Version::new(1, 0, 0);
        catalog::record_install(&store, "t.a", &v).unwrap();
        let copy = store.root.join("copy");
        write(&copy, data("t.a"), &[("db/ops.json", "1")]);
        store.install_addon(&copy).unwrap();
        assert!(!store.registry_install_path("t.a").exists());
        assert_eq!(print(&store), before);
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn the_same_files_match_however_they_were_installed() {
        let (dropped, downloaded) = (store("dropped"), store("downloaded"));
        for s in [&dropped, &downloaded] {
            put(s, data("t.a"), &[("db/ops.json", "1")]);
        }
        let v: catalog::Version = serde_json::from_value(json!({
            "version": "1.0.0", "size": 1, "sha256": "ab".repeat(32), "url": "u",
        }))
        .unwrap();
        catalog::record_install(&downloaded, "t.a", &v).unwrap();
        let p = profile(&["t.a"]);
        let (local, host) = (
            fingerprint(&dropped, &p, &game()).unwrap(),
            fingerprint(&downloaded, &p, &game()).unwrap(),
        );
        assert_eq!(host.addons[1].registry, Some("ab".repeat(32)));
        assert_eq!(local.digest, host.digest);
        assert!(compare(&local, &host, &[]).is_empty());
        for s in [dropped, downloaded] {
            std::fs::remove_dir_all(&s.root).ok();
        }
    }

    #[test]
    fn a_build_hook_prints_its_addon_data_and_profile_state() {
        let store = store("hook");
        let hook = json!({
            "id": "t.hook", "kind": ["database"],
            "launcher": { "program": { "linux": "bin/hook", "windows": "bin/hook.exe" }, "build": true },
        });
        put(&store, hook, &[]);
        put(&store, data("t.plain"), &[("db/ops.json", "1")]);
        let data_dir = store.addon_data_dir("t.hook");
        std::fs::create_dir_all(data_dir.join("logs")).unwrap();
        std::fs::write(data_dir.join("drivers.json"), r#"{"drivers":["a"]}"#).unwrap();
        let mut p = profile(&["t.hook", "t.plain"]);
        let inputs = |p: &Profile| {
            let f = fingerprint(&store, p, &game()).unwrap();
            assert_eq!(f.addons[2].inputs, None);
            f.addons[1].inputs.clone().unwrap()
        };
        let first = inputs(&p);

        std::fs::write(data_dir.join("logs/build.log"), "noise").unwrap();
        std::fs::write(data_dir.join("run.log"), "noise").unwrap();
        std::fs::create_dir_all(data_dir.join(".cache")).unwrap();
        std::fs::write(data_dir.join(".cache/view.json"), "noise").unwrap();
        assert_eq!(inputs(&p), first);

        std::fs::write(data_dir.join("drivers.json"), r#"{"drivers":["b"]}"#).unwrap();
        let other_drivers = inputs(&p);
        assert_ne!(other_drivers, first);

        std::fs::write(data_dir.join("drivers.json"), r#"{"drivers":["a"]}"#).unwrap();
        p.set_state("t.hook", json!({ "mods": ["x"] })).unwrap();
        let other_state = inputs(&p);
        assert_ne!(other_state, first);
        p.set_state("t.hook", json!({ "mods": ["y"] })).unwrap();
        assert_ne!(inputs(&p), other_state);
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn import_records_that_differ_only_in_their_hidden_sidecar_print_the_same() {
        let hook = json!({
            "id": "t.importer", "kind": ["database"],
            "launcher": { "program": { "linux": "bin/hook", "windows": "bin/hook.exe" }, "build": true },
        });
        let record = r#"{"mods":{"one":{"name":"One","converter":3,"made":{"id":"mod.one","name":"One"}}}}"#;
        let p = profile(&["t.importer"]);
        let prints: Vec<Fingerprint> = [(1792000000, "C:/Mods/one.zip"), (1795000000, "/home/b/one.zip")]
            .into_iter()
            .enumerate()
            .map(|(i, (at, file))| {
                let store = store(&format!("importer{i}"));
                put(&store, hook.clone(), &[]);
                let data = store.addon_data_dir("t.importer");
                std::fs::create_dir_all(&data).unwrap();
                std::fs::write(data.join("importer.json"), record).unwrap();
                let local = json!({ "mods": { "one": { "at": at } }, "last": { "at": at, "files": [file], "mods": ["one"] } });
                std::fs::write(data.join(".importer-local.json"), local.to_string()).unwrap();
                let f = fingerprint(&store, &p, &game()).unwrap();
                std::fs::remove_dir_all(&store.root).ok();
                f
            })
            .collect();
        assert!(prints[0].addons[1].inputs.is_some());
        assert_eq!(prints[0].digest, prints[1].digest);
    }

    fn detailed(id: &str, digest: &str, parts: &[(&str, &str)], reads: &[(&str, &str)]) -> AddonPrint {
        let mut a = print(id, "1.0.0", Source::Built(digest.repeat(32)), Some(0));
        let list: String = reads.iter().map(|(p, _)| *p).collect::<Vec<_>>().join("\n");
        a.detail = Some(Detail {
            parts: parts.iter().map(|(k, v)| ((*k).into(), (*v).into())).collect(),
            read_list: short(&sha256_bytes(list.as_bytes())),
            read_count: reads.len(),
            reads: reads.iter().map(|(_, h)| (*h).into()).collect(),
            paths: reads.iter().map(|(p, _)| (*p).into()).collect(),
        });
        a
    }

    fn with(addons: Vec<AddonPrint>) -> Fingerprint {
        Fingerprint::new(game(), addons, BTreeMap::new(), None)
    }

    #[test]
    fn a_different_copy_names_the_part_and_the_files_that_differ() {
        let reads = [("a.erp", "aaaa"), ("b.erp", "bbbb"), ("c.erp", "cccc")];
        let local = with(vec![detailed(
            "t.cars",
            "aa",
            &[("Database", "d1"), ("native", "n1")],
            &reads,
        )]);
        let mut theirs = reads;
        theirs[1].1 = "beef";
        let host = with(vec![detailed(
            "t.cars",
            "bb",
            &[("Database", "d2"), ("native", "n1")],
            &theirs,
        )]);
        let found = compare(&local, &host, &[]);
        assert_eq!(found.len(), 1, "{found:?}");
        assert!(matches!(found[0].kind, MismatchKind::SourceDiffers { .. }));
        assert_eq!(
            found[0].differences,
            ["its database operations differ", "b.erp differs"]
        );
        let blob = serde_json::to_value(&host).unwrap();
        assert!(
            blob.pointer("/addons/0/detail/paths").is_none(),
            "paths stay local"
        );
        assert_eq!(
            serde_json::from_value::<Fingerprint>(blob).unwrap().digest,
            host.digest
        );
    }

    #[test]
    fn a_different_read_list_is_named_with_both_counts() {
        let local = with(vec![detailed(
            "t.cars",
            "aa",
            &[],
            &[("a.erp", "aaaa"), ("b.erp", "bbbb")],
        )]);
        let host = with(vec![detailed("t.cars", "bb", &[], &[("a.erp", "aaaa")])]);
        assert_eq!(
            compare(&local, &host, &[])[0].differences,
            ["it reads other files: 2 here, 1 at the host"]
        );
        let many: Vec<(String, String)> = (0..8).map(|i| (format!("f{i}.erp"), format!("{i:04}"))).collect();
        let theirs: Vec<(String, String)> = many.iter().map(|(p, _)| (p.clone(), "ffff".into())).collect();
        fn refs(v: &[(String, String)]) -> Vec<(&str, &str)> {
            v.iter().map(|(a, b)| (a.as_str(), b.as_str())).collect()
        }
        let found = compare(
            &with(vec![detailed("t.cars", "aa", &[], &refs(&many))]),
            &with(vec![detailed("t.cars", "bb", &[], &refs(&theirs))]),
            &[],
        );
        assert_eq!(found[0].differences.len(), NAMED_FILES + 1);
        assert_eq!(found[0].differences.last().unwrap(), "and 3 more file(s) differ");
    }

    #[test]
    fn a_host_without_a_build_record_is_told_to_restart_and_an_old_peer_names_nothing() {
        let local = with(vec![detailed("t.cars", "aa", &[("Database", "d1")], &[])]);
        let unbuilt = with(vec![print("t.cars", "1.0.0", content("cc"), Some(0))]);
        assert_eq!(
            compare(&local, &unbuilt, &[])[0].differences,
            [
                "the host's game wasn't built with it, so its files count instead; the host needs to start F1 25 again"
            ]
        );
        let old = with(vec![print(
            "t.cars",
            "1.0.0",
            Source::Built("dd".repeat(32)),
            Some(0),
        )]);
        let found = compare(&local, &old, &[]);
        assert!(matches!(found[0].kind, MismatchKind::SourceDiffers { .. }));
        assert!(found[0].differences.is_empty());
        let json = serde_json::to_value(&found[0]).unwrap();
        assert!(json.get("differences").is_none());
    }

    #[test]
    fn different_build_hook_inputs_are_not_fixable() {
        let mut local = host();
        local.addons[1].inputs = Some("1".repeat(64));
        let mut h = host();
        h.addons[1].inputs = Some("2".repeat(64));
        let found = compare(&local, &h, &[]);
        assert_eq!(kinds(&found), [("inputs_differ".into(), false)]);
        assert_eq!(
            found[0].line(),
            "t.b name (t.b) has other data or profile state than the host's"
        );
    }

    #[test]
    fn matching_fingerprints_have_nothing_to_fix() {
        assert!(compare(&host(), &host(), &[]).is_empty());
    }

    #[test]
    fn game_build_and_dlc_differences_are_not_fixable() {
        let mut local = host();
        local.game_build.exe_sha256 = "b".repeat(64);
        assert_eq!(
            kinds(&compare(&local, &host(), &[])),
            [("game_build".into(), false)]
        );

        let mut local = host();
        local.game_build.build_id = format!("exe-{}", "a".repeat(12));
        assert!(compare(&local, &host(), &[]).is_empty());
        assert_eq!(local.canonical_digest(), host().digest);
        local.game_build.build_id = "25300000".into();
        local.game_build.exe_sha256 = "A".repeat(64);
        assert!(compare(&local, &host(), &[]).is_empty());
        assert_eq!(local.canonical_digest(), host().digest);

        let (mut local, mut h) = (host(), host());
        local.dlc = Some(false);
        assert_eq!(kinds(&compare(&local, &h, &[])), [("dlc".into(), false)]);
        assert_eq!(
            compare(&local, &h, &[])[0].line(),
            "DLC is not owned; the host's is unknown"
        );
        h.dlc = Some(false);
        assert!(compare(&local, &h, &[]).is_empty());
        h.dlc = Some(true);
        assert_eq!(kinds(&compare(&local, &h, &[])), [("dlc".into(), false)]);
    }

    #[test]
    fn a_different_launcher_is_reported_as_one() {
        let core = |f: &mut Fingerprint, sha: &str| {
            f.addons
                .push(print(CORE_ID, "0.2.13", Source::Builtin(sha.repeat(64)), Some(0)));
            f.addons.sort_by(|a, b| a.id.cmp(&b.id));
        };
        let (mut local, mut h) = (host(), host());
        core(&mut local, "1");
        core(&mut h, "2");
        local.launcher = "1.5.12".into();
        h.launcher = "1.5.13".into();
        let found = compare(&local, &h, &[]);
        assert_eq!(kinds(&found), [("launcher".into(), false)]);
        assert_eq!(
            found[0].line(),
            "Swofty Launcher 1.5.12 is not the host's 1.5.13; play with the same launcher version"
        );
        h.launcher = "1.5.12".into();
        assert_eq!(
            compare(&local, &h, &[])[0].line(),
            "this Swofty Launcher 1.5.12 is a different build from the host's 1.5.12"
        );
        let mut newer_core = local.clone();
        newer_core.addons[0].version = Version::new(0, 2, 14);
        assert_eq!(
            kinds(&compare(&local, &newer_core, &[])),
            [("launcher".into(), false)]
        );
    }

    #[test]
    fn the_core_print_covers_the_launcher_and_its_database_operations() {
        let store = store("core");
        let f = fingerprint(&store, &profile(&[]), &game()).unwrap();
        let Source::Builtin(sha) = &f.addons[0].source else {
            panic!("{:?}", f.addons[0].source);
        };
        let mut h = Sha256::new();
        h.update(format!("launcher {}\0", env!("CARGO_PKG_VERSION")));
        let dll = loader_dir().map(|d| d.join(CORE_DLL)).filter(|p| p.is_file());
        let dll = dll.map(|p| crate::baseline::sha256_file(&p).unwrap());
        h.update(format!("dll {}\0", dll.as_deref().unwrap_or("none")));
        h.update(CORE_MANIFEST);
        h.update(CORE_DB);
        assert_eq!(*sha, hex(&h.finalize()));
        assert_eq!(f.launcher, env!("CARGO_PKG_VERSION"));
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn a_missing_addon_is_fixable_when_the_registry_lists_that_download() {
        let mut local = host();
        local.addons.retain(|a| a.id != "t.b");
        let listed = [listing("t.b", &[("2.0.0", "2"), ("1.0.0", "e")])];
        let found = compare(&local, &host(), &listed);
        assert_eq!(
            found[0].kind,
            MismatchKind::MissingAddon {
                id: "t.b".into(),
                name: "t.b name".into(),
                version: Version::new(2, 0, 0),
                source: content("b"),
                registry: Some("2".repeat(64)),
            }
        );
        assert!(found[0].fixable);
        assert_eq!(found[0].line(), "t.b name (t.b) 2.0.0 is missing (fixable)");
        assert!(!compare(&local, &host(), &[listing("t.b", &[("2.0.0", "f")])])[0].fixable);
        assert!(!compare(&local, &host(), &[listing("t.b", &[("1.0.0", "2")])])[0].fixable);
        assert!(!compare(&local, &host(), &[])[0].fixable);

        let mut code_host = host();
        let mut code_local = host();
        code_local.addons.retain(|a| a.id != "t.code");
        let missing = compare(&code_local, &code_host, &[listing("t.code", &[("1.0.0", "c")])]);
        assert_eq!(kinds(&missing)[0], ("missing_addon".into(), false));
        code_host.addons.clear();
        assert!(
            kinds(&compare(&code_local, &code_host, &[]))
                .iter()
                .all(|(k, _)| k == "extra_addon")
        );
    }

    #[test]
    fn an_extra_addon_is_fixable_by_turning_it_off() {
        let mut local = host();
        local.addons.push(print(
            "t.extra",
            "1.0.0",
            Source::Content("e".repeat(64)),
            Some(2),
        ));
        local
            .options
            .insert("t.extra".into(), BTreeMap::from([("x".into(), json!(true))]));
        let found = compare(&local, &host(), &[]);
        assert_eq!(kinds(&found), [("extra_addon".into(), true)]);
        assert_eq!(
            found[0].line(),
            "t.extra name (t.extra) 1.0.0 is on; the host doesn't have it (fixable)"
        );
        let core = |f: &mut Fingerprint| {
            f.addons
                .push(print(CORE_ID, "0.2.13", Source::Builtin("1".repeat(64)), None))
        };
        let mut local = host();
        core(&mut local);
        assert_eq!(
            kinds(&compare(&local, &host(), &[])),
            [("extra_addon".into(), false)]
        );
    }

    #[test]
    fn a_version_difference_is_fixable_when_the_registry_has_the_hosts_version() {
        let mut local = host();
        local.addons[1].version = Version::new(1, 0, 0);
        local.addons[1].source = content("e");
        let found = compare(
            &local,
            &host(),
            &[listing("t.b", &[("2.0.0", "2"), ("1.0.0", "e")])],
        );
        assert_eq!(
            found[0].kind,
            MismatchKind::VersionDiffers {
                id: "t.b".into(),
                name: "t.b name".into(),
                local: Version::new(1, 0, 0),
                host: Version::new(2, 0, 0),
                registry: Some("2".repeat(64)),
            }
        );
        assert_eq!(kinds(&found), [("version_differs".into(), true)]);
        assert_eq!(
            kinds(&compare(&local, &host(), &[])),
            [("version_differs".into(), false)]
        );
    }

    #[test]
    fn a_source_difference_is_fixable_by_installing_the_hosts_download() {
        let mut local = host();
        local.addons[0].source = content("9");
        let listed = [listing("t.a", &[("1.0.0", "1")])];
        let found = compare(&local, &host(), &listed);
        assert_eq!(
            found[0].kind,
            MismatchKind::SourceDiffers {
                id: "t.a".into(),
                name: "t.a name".into(),
                version: Version::new(1, 0, 0),
                local: content("9"),
                host: content("a"),
                registry: Some("1".repeat(64)),
            }
        );
        assert_eq!(kinds(&found), [("source_differs".into(), true)]);
        let mut h = host();
        h.addons[0].registry = None;
        assert_eq!(
            kinds(&compare(&local, &h, &listed)),
            [("source_differs".into(), false)]
        );
    }

    #[test]
    fn option_differences_are_fixable() {
        let mut local = host();
        local.options = options(&[("t.a", "laps", json!(3))]);
        let found = compare(&local, &host(), &[]);
        assert_eq!(
            kinds(&found),
            [("option_differs".into(), true), ("option_differs".into(), true)]
        );
        assert_eq!(
            found[0].kind,
            MismatchKind::OptionDiffers {
                addon: "t.a".into(),
                option: "laps".into(),
                local: Some(json!(3)),
                host: Some(json!(5)),
            }
        );
        assert_eq!(found[0].line(), "t.a.laps is 3; the host has 5 (fixable)");
        assert_eq!(
            found[1].line(),
            "t.a.mode is unset; the host has \"full\" (fixable)"
        );

        let mut local = host();
        local.addons.retain(|a| a.id != "t.a");
        local.options.clear();
        assert_eq!(
            kinds(&compare(&local, &host(), &[])),
            [("missing_addon".into(), false)]
        );
    }

    #[test]
    fn a_load_order_difference_among_shared_data_addons_is_fixable() {
        let mut local = host();
        local.addons[0].order = Some(1);
        local.addons[1].order = Some(0);
        let found = compare(&local, &host(), &[]);
        assert_eq!(
            found[0].kind,
            MismatchKind::LoadOrder {
                local: vec!["t.b".into(), "t.a".into()],
                host: vec!["t.a".into(), "t.b".into()],
            }
        );
        assert_eq!(kinds(&found), [("load_order".into(), true)]);

        let mut local = host();
        local.addons.push(print(
            "t.extra",
            "1.0.0",
            Source::Content("e".repeat(64)),
            Some(0),
        ));
        local.addons[0].order = Some(1);
        local.addons[1].order = Some(2);
        assert_eq!(
            kinds(&compare(&local, &host(), &[])),
            [("extra_addon".into(), true)]
        );
    }

    #[test]
    fn db_editor_addons_print_the_operations_their_edits_build() {
        let package = |name: &str, tool: &str, edit: Value| {
            let s = store(name);
            let mut m = data("dbedit.grid");
            m["origin"] = json!({ "tool": "db-editor", "tool_version": tool });
            let marker = json!({
                "format": "swofty-db-package", "format_version": 1, "game_build": "25251167", "edits": [edit],
            })
            .to_string();
            let readme = format!("made with {tool}");
            put(
                &s,
                m,
                &[
                    ("dbedit.json", &marker),
                    ("database.json", tool),
                    ("README.txt", &readme),
                ],
            );
            let f = fingerprint(&s, &profile(&["dbedit.grid"]), &game()).unwrap();
            std::fs::remove_dir_all(&s.root).ok();
            f
        };
        let edit = |value: &str, stock: &str, path: &str| json!({ "key": "0x0000000000000001", "type": 0, "value": value, "stock": stock, "path": path });
        let older = package(
            "dbedit-older",
            "1.5.10",
            edit("33", "1", "entrants.drivers.driver_1.car_number"),
        );
        let newer = package("dbedit-newer", "1.5.12", edit("33", "2", "Car number"));
        assert_eq!(older.addons[0].id, "dbedit.grid");
        assert!(matches!(older.addons[0].source, Source::Edits(_)));
        assert_eq!(older.digest, newer.digest);
        assert!(compare(&older, &newer, &[]).is_empty());
        let other = package("dbedit-other", "1.5.12", edit("34", "1", "Car number"));
        assert_eq!(
            kinds(&compare(&older, &other, &[])),
            [("source_differs".into(), false)]
        );

        let copied = "entrants.drivers.driver[driver=x].car_number";
        let key = format!("0x{:016x}", swofty_formats::hash::fnv1a64(copied));
        let in_row = json!({ "key": key, "type": 0, "value": "33", "stock": null, "path": copied });
        let mut loose = in_row.clone();
        loose["path"] = Value::Null;
        let in_row = package("dbedit-in-row", "1.5.12", in_row);
        let loose = package("dbedit-loose", "1.5.12", loose);
        assert_ne!(in_row.digest, loose.digest);
    }

    #[test]
    fn driver_manager_inputs_ignore_id_counters_and_empty_files() {
        use crate::dbedit::driver_manager::ID as DM;
        let store = store("settled");
        let hook = json!({
            "id": DM, "kind": ["database"],
            "launcher": { "program": { "linux": "bin/hook", "windows": "bin/hook.exe" }, "build": true },
        });
        put(&store, hook, &[]);
        let mut p = profile(&[DM]);
        let inputs = |p: &Profile| {
            fingerprint(&store, p, &game()).unwrap().addons[1]
                .inputs
                .clone()
                .unwrap()
        };
        let none = inputs(&p);
        let data_dir = store.addon_data_dir(DM);
        std::fs::create_dir_all(&data_dir).unwrap();
        let roster = |next: u32, drivers: Value| {
            json!({ "schema": 1, "next_driver": next, "next_preset": next + 158, "drivers": drivers })
                .to_string()
        };
        std::fs::write(data_dir.join("drivers.json"), roster(209, json!([]))).unwrap();
        std::fs::write(data_dir.join("stock.json"), r#"{"schema":1,"drivers":{}}"#).unwrap();
        p.set_state(DM, json!({ "mods": [] })).unwrap();
        assert_eq!(inputs(&p), none);

        std::fs::write(
            data_dir.join("drivers.json"),
            roster(210, json!([{ "stat_id": 209 }])),
        )
        .unwrap();
        let one = inputs(&p);
        assert_ne!(one, none);
        std::fs::write(
            data_dir.join("drivers.json"),
            roster(300, json!([{ "stat_id": 209 }])),
        )
        .unwrap();
        assert_eq!(inputs(&p), one);
        std::fs::write(
            data_dir.join("stock.json"),
            r#"{"schema":1,"drivers":{"max":{"head":"h"}}}"#,
        )
        .unwrap();
        assert_ne!(inputs(&p), one);
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn other_build_hooks_keep_false_zero_empty_text_and_counters() {
        let store = store("generic-hook");
        let hook = json!({
            "id": "t.hook", "kind": ["database"],
            "launcher": { "program": { "linux": "bin/hook", "windows": "bin/hook.exe" }, "build": true },
        });
        put(&store, hook, &[]);
        let p = profile(&["t.hook"]);
        let data_dir = store.addon_data_dir("t.hook");
        std::fs::create_dir_all(&data_dir).unwrap();
        let inputs = || {
            fingerprint(&store, &p, &game()).unwrap().addons[1]
                .inputs
                .clone()
                .unwrap()
        };
        let none = inputs();
        std::fs::write(
            data_dir.join("items.json"),
            r#"{"schema":1,"items":[],"groups":{"a":[]}}"#,
        )
        .unwrap();
        assert_eq!(inputs(), none);
        for text in [
            r#"{"enabled":false}"#,
            r#"{"count":0}"#,
            r#"{"name":""}"#,
            r#"{"next_id":4}"#,
            r#"{"value":null}"#,
        ] {
            std::fs::write(data_dir.join("items.json"), text).unwrap();
            assert_ne!(inputs(), none, "{text}");
        }
        std::fs::write(data_dir.join("items.json"), r#"{"next_id":4}"#).unwrap();
        let four = inputs();
        std::fs::write(data_dir.join("items.json"), r#"{"next_id":5}"#).unwrap();
        assert_ne!(inputs(), four);
        std::fs::remove_dir_all(&store.root).ok();
    }

    #[test]
    fn equal_digests_and_an_empty_comparison_always_agree() {
        type Edit = Box<dyn Fn(&mut Fingerprint)>;
        let core = |sha: &'static str| -> Edit {
            Box::new(move |c| {
                c.addons
                    .push(print(CORE_ID, "0.2.13", Source::Builtin(sha.repeat(64)), None))
            })
        };
        let edits: Vec<Edit> = vec![
            Box::new(|c| c.addons[0].name = "Renamed".into()),
            Box::new(|c| c.addons[0].registry = None),
            Box::new(|c| c.launcher = "0.0.1".into()),
            Box::new(|c| c.game_build.build_id = format!("exe-{}", "a".repeat(12))),
            Box::new(|c| c.game_build.exe_sha256 = "A".repeat(64)),
            Box::new(|c| c.game_build.exe_sha256 = "b".repeat(64)),
            Box::new(|c| c.dlc = Some(true)),
            Box::new(|c| c.dlc = Some(false)),
            Box::new(|c| c.addons[0].version = Version::new(1, 0, 1)),
            Box::new(|c| c.addons[1].source = content("d")),
            Box::new(|c| c.addons[1].source = Source::Edits("b".repeat(64))),
            Box::new(|c| c.addons[1].inputs = Some("d".repeat(64))),
            Box::new(|c| {
                c.addons[0].order = Some(1);
                c.addons[1].order = Some(0);
            }),
            Box::new(|c| {
                c.addons[0].order = Some(5);
                c.addons[1].order = Some(9);
            }),
            Box::new(|c| c.addons[2].order = Some(2)),
            Box::new(|c| c.addons.retain(|a| a.id != "t.b")),
            Box::new(|c| c.addons.push(print("t.extra", "1.0.0", content("e"), None))),
            Box::new(|c| {
                c.options.get_mut("t.a").unwrap().insert("laps".into(), json!(6));
            }),
            Box::new(|c| {
                c.options
                    .get_mut("t.a")
                    .unwrap()
                    .insert("extra".into(), json!(true));
            }),
            Box::new(|c| {
                c.options.remove("t.a");
            }),
            core("1"),
            core("2"),
            Box::new(move |c| {
                core("1")(c);
                c.launcher = "1.5.13".into();
            }),
        ];
        let mut fixtures = vec![host()];
        for edit in &edits {
            let mut f = host();
            edit(&mut f);
            f.digest = f.canonical_digest();
            fixtures.push(f);
        }
        for a in &fixtures {
            for b in &fixtures {
                let found = compare(a, b, &[]);
                assert_eq!(a.digest == b.digest, found.is_empty(), "{a:?}\n{b:?}\n{found:?}");
            }
        }
    }
}
