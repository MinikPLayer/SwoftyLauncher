use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use swofty_formats::gvdb::Gvdb;

use crate::activation::db::{Registry, TrackHost, activate};
use crate::activation::ers::{self, ErsMap, build_ers};
use crate::activation::grid::grid_size;
use crate::activation::place::{
    PlaceInput, SLOTS, place, slot_asset_group, slot_asset_root, slot_file, track_group,
};
use crate::activation::ui::{Kept, MapUi, build_ui, plan_lists, stamp as ui_stamp};
use crate::asset::{
    AssetOp, FileKind, FileOp, GroupOp, ItemOp, LangOp, Op, OpFile, Origin, Root, Source, TrackOp,
};
use crate::baseline::{Baseline, Objects, hex, sha256_file, sha256_opt};
use crate::builtin::{CORE_DB, CORE_ID, LOADER_ENV, core_db_ops, loader_dir, loader_files};
use crate::conflicts::Report;
use crate::dbedit::driver_manager;
use crate::erpbuild::{ErpBuild, Held, Resized, SliceMerge, reads, unsupported, writes};
use crate::install::{Game, LOADER_DIR, Plan};
use crate::launcher::BuildCx;
use crate::manifest::{AddonManifest, PassValue, PostFx, Strength, relative_inside};
use crate::map::{TrackPackage, Unusable};
use crate::multiplayer::fingerprint::{self, OutputBuilder};
use crate::owned;
use crate::placed::Placed;
use crate::planner::{
    Conflict, Features, Kind, Planner, Route, Target, kept_lists_line, load_features, stock_removals_line,
    track_removals_line, waiting_lines,
};
use crate::profile::Profile;
use crate::registry::Host;
use crate::resolver::resolve;
use crate::runtime::{Prebuilt, Runtime, db_edits, overlay_fits, runtime_record_room};
use crate::settings::{
    SessionMod, SessionSettings, SessionUi, render_ops, render_text, screen_file, screens,
};
use crate::store::Store;
use crate::ui::{ERPS as MENU_ERPS, READS as MENU_READS, ui_file};
use crate::{Error, Result, io_err};

pub const GVDB: &str = "game_package/gamedatabases/gamedatabases.2025.bin";
pub const PLAN_FILE: &str = "plan.json";
pub const SAVED_FILE: &str = "built.json";
const SAVED_VERSION: u32 = 2;
const PACKED_AGE: std::time::Duration = std::time::Duration::from_secs(30 * 24 * 3600);
const ARCHIVE_AGE: std::time::Duration = std::time::Duration::from_secs(3 * 24 * 3600);
const ARCHIVE_BYTES: u64 = 10 << 30;
pub const ADDONS_DIR: &str = "addons";
const LOCK_FILE: &str = "build.lock";

#[derive(Debug, Clone, Default)]
pub struct Built {
    pub buildhash: String,
    pub overlay: PathBuf,
    pub order: Vec<String>,
    pub plan: Plan,
    pub lines: Vec<String>,
    pub warnings: Vec<String>,
    /// Every side-data finding of the add-row copies, tagged; see [`crate::dbedit::deps::Report`].
    pub side_data: Vec<String>,
    pub settings: Option<SessionSettings>,
    pub conflicts: Vec<Conflict>,
    pub waiting: Vec<Op>,
    pub report: Report,
}

/// The recorded originals of the last baseline read, by its path and when it
/// was written: a build looks originals up dozens of times.
type Originals = (
    PathBuf,
    (std::time::SystemTime, u64),
    std::sync::Arc<BTreeMap<String, String>>,
);
static ORIGINALS: std::sync::Mutex<Option<Originals>> = std::sync::Mutex::new(None);

fn originals(data: &Path, game: &Game) -> std::sync::Arc<BTreeMap<String, String>> {
    let path = Baseline::path(data, &game.identity);
    let stamp = std::fs::metadata(&path)
        .and_then(|m| Ok((m.modified()?, m.len())))
        .ok();
    let mut cached = ORIGINALS.lock().unwrap_or_else(|e| e.into_inner());
    if let Some((p, s, files)) = cached.as_ref()
        && *p == path
        && Some(*s) == stamp
    {
        return files.clone();
    }
    let files: std::sync::Arc<BTreeMap<String, String>> = std::sync::Arc::new(
        Baseline::load_or_new(data, &game.identity)
            .map(|b| b.files.into_iter().map(|(rel, rec)| (rel, rec.sha256)).collect())
            .unwrap_or_default(),
    );
    *cached = stamp.map(|stamp| (path, stamp, files.clone()));
    files
}

pub fn original(data: &Path, game: &Game, rel: &str) -> PathBuf {
    match originals(data, game).get(rel) {
        Some(sha256) => Objects {
            root: data.join("baseline/objects"),
        }
        .path(sha256),
        None => game.dir.join(rel),
    }
}

pub type TrackPackages = (Vec<(PathBuf, TrackPackage, Origin)>, Vec<(Origin, Unusable)>);

/// The packages `track.add` ops add, and the ones a build leaves out.
pub fn track_packages(store: &Store, ops: Vec<Op>) -> Result<TrackPackages> {
    let mut out: Vec<(PathBuf, TrackPackage, Origin)> = Vec::new();
    let mut left_out = Vec::new();
    for op in ops {
        let AssetOp::Track(TrackOp::Add { source }) = op.op else {
            continue;
        };
        let dir = source.resolve(&source_dir(store, &op.origin.addon, source.root))?;
        let pkg = match crate::map::read_package(&dir) {
            Ok(pkg) => pkg,
            Err(why) => {
                left_out.push((op.origin, why));
                continue;
            }
        };
        if out.iter().any(|(_, p, _)| p.track_code == pkg.track_code) {
            return Err(Error::Map(format!("track {} is added twice", pkg.track_code)));
        }
        out.push((dir, pkg, op.origin));
    }
    Ok((out, left_out))
}

// Page screens have no file: the ui build generates them.
pub fn screen_files(order: &[&AddonManifest], addons_dir: &Path) -> Result<BTreeMap<String, PathBuf>> {
    Ok(screens(order.iter().copied())?
        .into_iter()
        .filter(|(_, s)| s.page.is_none())
        .map(|(m, s)| {
            (
                format!("{LOADER_DIR}/{}", screen_file(&s.name)),
                addons_dir.join(&m.id).join(&s.file),
            )
        })
        .collect())
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Plugins {
    pub files: BTreeMap<String, PathBuf>,
    pub dlls: Vec<String>,
}

pub fn plugin_files<'a>(
    store: &Store,
    order: impl IntoIterator<Item = &'a AddonManifest>,
    taken: impl IntoIterator<Item = String>,
) -> Result<Plugins> {
    let mut out = Plugins::default();
    let mut names: BTreeMap<String, String> = taken
        .into_iter()
        .map(|n| (n.to_ascii_lowercase(), CORE_ID.to_owned()))
        .collect();
    for m in order
        .into_iter()
        .filter(|m| !m.operations.native.is_empty() && !store.is_builtin(&m.id))
    {
        let dir = store.addons_dir().join(&m.id);
        for n in &m.operations.native {
            let name = n.dll.rsplit('/').next().unwrap_or(&n.dll);
            if let Some(other) = names.insert(name.to_ascii_lowercase(), m.id.clone()) {
                return Err(Error::Other(format!(
                    "{}: plugin {name} has the same file name as a plugin of {other}; rename one",
                    m.id
                )));
            }
            out.dlls.push(format!("{ADDONS_DIR}/{}/{}", m.id, n.dll));
            for rel in std::iter::once(&n.dll).chain(&n.files) {
                let rel = rel.trim_end_matches('/');
                let src = dir.join(rel);
                let found = if src.is_dir() {
                    loader_files(&src)?
                        .into_iter()
                        .filter(|(r, _)| !r.split('/').any(|p| p.starts_with('.')))
                        .map(|(r, p)| (format!("{rel}/{r}"), p))
                        .collect()
                } else if src.is_file() {
                    BTreeMap::from([(rel.to_owned(), src)])
                } else {
                    return Err(Error::Other(format!(
                        "{}: {rel} is missing; build the plugin and install the addon again",
                        m.id
                    )));
                };
                for (rel, src) in found {
                    out.files
                        .insert(format!("{LOADER_DIR}/{ADDONS_DIR}/{}/{rel}", m.id), src);
                }
            }
        }
    }
    Ok(out)
}

pub fn source_dir(store: &Store, addon: &str, root: Root) -> PathBuf {
    match root {
        Root::Addon => store.addons_dir().join(addon),
        Root::Data => store.addon_data_dir(addon),
        Root::Cache => store.addon_cache_dir(addon),
    }
}

/// The `.cube` a shader pack's settings pick, and its strength from 0 to 100.
fn shader_look(m: &AddonManifest, p: &Profile) -> Result<(String, f64)> {
    let s = m.shader.as_ref().expect("a shader pack");
    let rel = render_text(&s.lut, m, p)?;
    if !relative_inside(&rel) {
        return Err(Error::Other(format!(
            "{}: shader lut {rel} must be a relative path inside the addon",
            m.name
        )));
    }
    let strength = match &s.strength {
        None => 100.0,
        Some(Strength::Percent(v)) => *v,
        Some(Strength::Option(t)) => render_text(t, m, p)?
            .trim()
            .parse::<f64>()
            .ok()
            .filter(|v| (0.0..=100.0).contains(v))
            .ok_or_else(|| Error::Other(format!("{}: shader strength {t} is not from 0 to 100", m.name)))?,
    };
    Ok((rel, strength))
}

/// The values a pass reads with `option(i)`, with `{{id}}` options filled from the profile's settings.
fn pass_options(m: &AddonManifest, pass: &PostFx, p: &Profile) -> Result<Vec<f64>> {
    pass.options
        .iter()
        .map(|v| match v {
            PassValue::Number(n) => Ok(*n),
            PassValue::Option(t) => {
                let text = render_text(t, m, p)?;
                match text.trim() {
                    "true" => Ok(1.0),
                    "false" => Ok(0.0),
                    v => v.parse::<f64>().map_err(|_| {
                        Error::Other(format!("{}: postfx option {t} is {v:?}, not a number", m.name))
                    }),
                }
            }
        })
        .collect()
}

fn render_file(
    path: &Path,
    kind: FileKind,
    m: &AddonManifest,
    p: &Profile,
    hasher: &mut Sha256,
) -> Result<OpFile> {
    let json = |source| Error::Json {
        path: path.to_owned(),
        source,
    };
    let raw = std::fs::read(path).map_err(io_err(path))?;
    let mut doc: serde_json::Value = serde_json::from_slice(&raw).map_err(json)?;
    render_ops(&mut doc, m, p)?;
    hasher.update(serde_json::to_vec(&doc).expect("serialisable"));
    OpFile::from_value(doc, kind).map_err(json)
}

/// A DB Editor package's ops, generated from its `dbedit.json` rather than
/// read from its `database.json`, and the Driver Manager drivers its values pick.
fn package_ops(store: &Store, id: &str) -> Result<(OpFile, Vec<String>)> {
    let edits = crate::dbedit::package::Package::open(store, id)?.edits;
    let ops = edits.to_ops();
    let file = OpFile {
        schema_version: ops.schema_version,
        fields: ops.fields,
        strict: ops.strict,
        side_data: ops.side_data,
        ops: ops.ops.into_iter().map(AssetOp::Db).collect(),
    };
    Ok((file, driver_manager::picks(&edits)))
}

fn unbuilt<'a>(ops: impl IntoIterator<Item = &'a AssetOp>) -> Option<&'static str> {
    ops.into_iter().find_map(|op| match op {
        AssetOp::Erp(op) => unsupported(op),
        _ => None,
    })
}

fn hash_sources(hasher: &mut Sha256, store: &Store, addon: &str, ops: &[AssetOp]) -> Result<()> {
    for src in ops.iter().flat_map(AssetOp::sources) {
        let path = src.resolve(&source_dir(store, addon, src.root))?;
        let meta = std::fs::metadata(&path).map_err(io_err(&path))?;
        hasher.update(format!("{}|{}|{:?}", src.path, meta.len(), meta.modified().ok()));
        if meta.is_dir() {
            for (rel, p) in loader_files(&path)? {
                let meta = std::fs::metadata(&p).map_err(io_err(&p))?;
                hasher.update(format!("{rel}|{}|{:?}", meta.len(), meta.modified().ok()));
            }
        }
    }
    Ok(())
}

fn stamp(hasher: &mut Sha256, label: &str, path: &Path) {
    match std::fs::metadata(path) {
        Ok(meta) => hasher.update(format!("{label}|{}|{:?};", meta.len(), meta.modified().ok())),
        Err(_) => hasher.update(format!("{label}|missing;")),
    }
}

fn stamp_tree(hasher: &mut Sha256, label: &str, path: &Path) -> Result<()> {
    if path.is_dir() {
        for (rel, p) in loader_files(path)? {
            stamp(hasher, &format!("{label}/{rel}"), &p);
        }
    } else {
        stamp(hasher, label, path);
    }
    Ok(())
}

fn hash_unread(
    hasher: &mut Sha256,
    store: &Store,
    game: &Game,
    order: &[&AddonManifest],
    erp_ops: &[Op],
    resolve: &dyn Fn(&str) -> PathBuf,
) -> Result<()> {
    hasher.update(format!("game {};", game.identity.key()));
    hasher.update(format!("pipeline {};", env!("SWOFTY_PIPELINE")));
    if let Some(dir) = loader_dir() {
        stamp(
            hasher,
            "loader features",
            &dir.join(crate::planner::FEATURES_FILE),
        );
    }
    for m in order.iter().filter(|m| !store.is_builtin(&m.id)) {
        let dir = store.addons_dir().join(&m.id);
        for n in &m.operations.native {
            for rel in std::iter::once(&n.dll).chain(&n.files) {
                let rel = rel.trim_end_matches('/');
                stamp_tree(hasher, &format!("{}/{rel}", m.id), &dir.join(rel))?;
            }
        }
    }
    let read: std::collections::BTreeSet<&str> = erp_ops
        .iter()
        .filter_map(|op| match &op.op {
            AssetOp::Erp(e) => Some(reads(e)),
            _ => None,
        })
        .flatten()
        .collect();
    for rel in read {
        stamp(hasher, rel, &resolve(rel));
    }
    Ok(())
}

#[derive(Serialize, Deserialize)]
struct Saved {
    version: u32,
    buildhash: String,
    data: BTreeMap<String, PathBuf>,
    added: BTreeMap<String, PathBuf>,
    loader: BTreeMap<String, PathBuf>,
    registry_override: bool,
    sizes: BTreeMap<PathBuf, u64>,
    settings: Option<SessionSettings>,
    lines: Vec<String>,
    warnings: Vec<String>,
    conflicts: Vec<Conflict>,
    waiting: Vec<Op>,
    #[serde(default)]
    report: Option<Report>,
}

impl Saved {
    fn of(built: &Built, lines: usize, warnings: usize) -> Result<Self> {
        let mut sizes = BTreeMap::new();
        for src in [&built.plan.data, &built.plan.added]
            .into_iter()
            .flat_map(|m| m.values())
        {
            let meta = std::fs::metadata(src).map_err(io_err(src))?;
            sizes.insert(src.clone(), meta.len());
        }
        Ok(Self {
            version: SAVED_VERSION,
            buildhash: built.buildhash.clone(),
            data: built.plan.data.clone(),
            added: built.plan.added.clone(),
            loader: built_files(&built.plan.loader, loader_dir().as_deref()),
            registry_override: built.plan.registry_override,
            sizes,
            settings: built.settings.clone(),
            lines: built.lines[lines..].to_vec(),
            warnings: built.warnings[warnings..].to_vec(),
            conflicts: built.conflicts.clone(),
            waiting: built.waiting.clone(),
            report: Some(built.report.clone()),
        })
    }

    fn load(overlay: &Path, buildhash: &str) -> Option<Self> {
        let saved: Self = crate::read_json(&overlay.join(SAVED_FILE)).ok()?;
        (saved.version == SAVED_VERSION && saved.buildhash == buildhash).then_some(saved)
    }

    // The game folder decides whether a file is replaced or added, so a saved
    // plan only holds while each file it adds is still absent and each file it
    // replaces is still a game file.
    fn usable(&self, store: &Store, game: &Game) -> bool {
        let sources = self
            .sizes
            .iter()
            .all(|(p, len)| std::fs::metadata(p).is_ok_and(|m| m.is_file() && m.len() == *len));
        if !sources
            || !self.loader.values().all(|p| p.is_file())
            || self
                .added
                .keys()
                .any(|rel| std::fs::symlink_metadata(game.dir.join(rel)).is_ok())
        {
            return false;
        }
        let Ok(baseline) = Baseline::load_or_new(&store.root, &game.identity) else {
            return false;
        };
        self.data
            .keys()
            .all(|rel| baseline.files.contains_key(rel) || game.dir.join(rel).is_file())
    }

    /// The loader's own files come from the launcher's loader folder as it is
    /// now: a launcher update changes them without changing what the build
    /// wrote. The rest of the placed files, such as screens, runtime assets and
    /// addon plugins, are the saved ones.
    fn restore(self, built: &mut Built) -> Result<()> {
        built.plan.data = self.data;
        built.plan.added = self.added;
        built.plan.loader = self.loader;
        if let Some(dir) = loader_dir() {
            built.plan.loader.extend(loader_files(&dir)?);
        }
        built.plan.registry_override = self.registry_override;
        built.settings = self.settings;
        built.lines.push(format!(
            "build: nothing changed since overlay {} was built; reusing it",
            built.buildhash
        ));
        built.lines.extend(self.lines);
        built.warnings.extend(self.warnings);
        built.report = match self.report {
            Some(report) => report,
            None => crate::conflicts::summarise(&built.plan.profile, &self.conflicts, &|_| None).0,
        };
        built.report.built_at = crate::conflicts::now();
        built.conflicts = self.conflicts;
        built.waiting = self.waiting;
        Ok(())
    }
}

/// The placed files that do not come from the loader folder `loader`.
fn built_files(placed: &BTreeMap<String, PathBuf>, loader: Option<&Path>) -> BTreeMap<String, PathBuf> {
    placed
        .iter()
        .filter(|(_, src)| loader.is_none_or(|dir| !src.starts_with(dir)))
        .map(|(rel, src)| (rel.clone(), src.clone()))
        .collect()
}

/// Held while a build writes a profile's overlay; another build of the
/// profile, in this process or another, waits until it is dropped.
pub struct BuildLock {
    _file: std::fs::File,
}

pub fn lock(store: &Store, profile: &str) -> Result<BuildLock> {
    let dir = store.overlays_dir(profile);
    std::fs::create_dir_all(&dir).map_err(io_err(&dir))?;
    let path = dir.join(LOCK_FILE);
    let file = std::fs::OpenOptions::new()
        .create(true)
        .truncate(false)
        .write(true)
        .open(&path)
        .map_err(io_err(&path))?;
    file.lock().map_err(io_err(&path))?;
    Ok(BuildLock { _file: file })
}

pub fn build(store: &Store, profile: &str, game: &Game) -> Result<Built> {
    build_as(store, profile, game, false)
}

/// A build or install that stopped on a missing or cut-short file, as the
/// addon or game file it is and what to do about it. Any other error is
/// returned as it is.
pub fn named(store: &Store, game: &Path, e: Error) -> Error {
    let (Error::Io { path, source } | Error::Formats(swofty_formats::Error::IoAt { path, source })) = &e
    else {
        return e;
    };
    let gone = source.kind() == std::io::ErrorKind::NotFound;
    if !gone && source.kind() != std::io::ErrorKind::UnexpectedEof {
        return e;
    }
    let rel = |rest: &Path| {
        rest.components()
            .map(|c| c.as_os_str().to_string_lossy())
            .collect::<Vec<_>>()
            .join("/")
    };
    if let Ok(rest) = path.strip_prefix(store.addons_dir())
        && let Some(id) = rest
            .components()
            .next()
            .map(|c| c.as_os_str().to_string_lossy().into_owned())
        && let Ok(file) = rest.strip_prefix(&id)
        && !file.as_os_str().is_empty()
    {
        let name = store
            .addons()
            .ok()
            .and_then(|a| a.get(&id).map(|m| m.name.clone()))
            .unwrap_or_else(|| id.clone());
        let file = rel(file);
        let what = match gone {
            true => format!("{name} ({id}) is missing its file {file}"),
            false => format!("{name} ({id}) has an incomplete or damaged {file}"),
        };
        return Error::Other(format!(
            "{what}. Turn {name} off in this profile, or remove it on the Addons page and install or import it again."
        ));
    }
    if let Ok(rest) = path.strip_prefix(game) {
        let file = rel(rest);
        let version = crate::exe_version::read(&game.join(crate::GAME_EXE));
        return Error::Other(match gone {
            true => format!(
                "{file} is missing from the game folder; {}, then try again",
                crate::stock::missing_fix(1, version.as_deref())
            ),
            false => format!(
                "{file} in the game folder is incomplete or damaged; verify the game files in Steam (open {}) \
                 or repair the game in the EA app, then try again",
                crate::stock::STEAM_VERIFY_URL
            ),
        });
    }
    e
}

/// A `checking` build gives addon build hooks a host that lists addons but
/// cannot install, remove, turn on or move them.
pub(crate) fn build_as(store: &Store, profile: &str, game: &Game, checking: bool) -> Result<Built> {
    let registry = store.registry()?;
    let addons = registry.manifests();
    let mut p = store.profile(profile)?;
    let mut skipped = Vec::new();
    for u in store.unreadable_addons() {
        if !addons.contains_key(&u.id) && p.enabled().any(|id| id == u.id) {
            p.set_enabled(&u.id, false)?;
            skipped.push(format!("{} is left out: it {}", u.name, u.reason));
        }
    }
    let resolved = resolve(&p, &addons)?;
    let mut built = Built {
        order: resolved.order.clone(),
        warnings: skipped
            .into_iter()
            .chain(resolved.warnings.iter().cloned())
            .collect(),
        plan: Plan {
            profile: profile.into(),
            ..Plan::default()
        },
        ..Built::default()
    };
    built.report = Report {
        profile: profile.into(),
        built_at: crate::conflicts::now(),
        pairs: Vec::new(),
        order: p.enabled().map(str::to_owned).collect(),
        stock_removals: BTreeMap::new(),
        kept_lists: BTreeMap::new(),
    };
    if p.is_vanilla() {
        built.plan = Plan::vanilla();
        return Ok(built);
    }
    let _lock = lock(store, profile)?;
    crate::now_doing("reading the addons");
    let mut hasher = Sha256::new();
    let packed = store.root.join("cache").join(crate::cache::PACKED_DIR);
    swofty_formats::erp::cache_compressed(Some(packed.clone()));
    let features = loader_dir()
        .map(|d| load_features(&d))
        .transpose()?
        .unwrap_or_default();
    let config = store.config();
    let packages: BTreeMap<String, String> = resolved
        .order
        .iter()
        .filter(|id| crate::dbedit::package::is_package(store, id))
        .map(|id| (id.clone(), addons[id].name.clone()))
        .collect();
    let overlay_names = |origin: &Origin, src: &Source| -> Vec<String> {
        src.resolve(&source_dir(store, &origin.addon, src.root))
            .ok()
            .and_then(|p| swofty_formats::erp::ErpFile::open(&p).ok())
            .map(|f| f.erp.resources.into_iter().map(|r| r.name).collect())
            .unwrap_or_default()
    };
    let mut lenient = packages.clone();
    if let Some(core) = addons.get(CORE_ID) {
        lenient.insert(CORE_ID.to_owned(), core.name.clone());
    }
    let mut planner = Planner::new(&features, &config.runtime_assets)
        .build_only(&config.build_assets)
        .lenient(lenient)
        .overlays(&overlay_names);
    if features != Features::default() {
        hasher.update(format!("runtime {features:?} {:?};", config.runtime_assets));
        if !config.build_assets.is_empty() {
            hasher.update(format!("build {:?};", config.build_assets));
        }
    }
    let mut runtime = Runtime::new(planner.features());
    let mut driver_picks: Vec<(String, Vec<String>)> = Vec::new();
    let mut experimental: BTreeMap<&str, Vec<&str>> = BTreeMap::new();
    for id in &resolved.order {
        for f in &addons[id].experimental_features {
            experimental.entry(f.name()).or_default().push(&addons[id].name);
        }
    }
    for (feature, by) in &experimental {
        runtime.enable(feature);
        hasher.update(format!("feature {feature};"));
        built.lines.push(format!(
            "features: the experimental {feature} feature is on for {}",
            by.join(", ")
        ));
    }
    let looks: Vec<&AddonManifest> = resolved
        .order
        .iter()
        .map(|id| &addons[id])
        .filter(|m| m.shader.is_some())
        .collect();
    if let Some((m, earlier)) = looks.split_last() {
        for e in earlier {
            built.warnings.push(format!(
                "{}: its look is not applied; one shader look applies at a time, and {} loads after it",
                e.name, m.name
            ));
        }
        let (rel, strength) = shader_look(m, &p)?;
        let path = store.addons_dir().join(&m.id).join(&rel);
        let text =
            std::fs::read_to_string(&path).map_err(|e| Error::Other(format!("{}: {rel}: {e}", m.name)))?;
        let cube = swofty_formats::cube::Cube::parse(&text)
            .map_err(|e| Error::Other(format!("{}: {rel}: {e}", m.name)))?;
        hasher.update(format!("shader {} {rel} {strength};", m.id));
        hasher.update(text.as_bytes());
        runtime.shader(cube.pack(), strength / 100.0, &m.name);
        built.lines.push(format!(
            "shaders: {} grades the picture with {rel} ({n}x{n}x{n}) at {strength}% strength",
            m.name,
            n = cube.size
        ));
    }
    let mut passes: Vec<(i32, usize, &AddonManifest, &PostFx)> = Vec::new();
    for id in &resolved.order {
        for pass in &addons[id].postfx {
            passes.push((pass.order, passes.len(), &addons[id], pass));
        }
    }
    passes.sort_by_key(|(order, at, ..)| (*order, *at));
    for (_, _, m, pass) in passes {
        let label = pass.label();
        let options = pass_options(m, pass, &p)?;
        let source = store.addons_dir().join(&m.id).join(&pass.shader);
        let budget = pass.budget_ms.unwrap_or(1.5);
        hasher.update(format!(
            "postfx {} {} {options:?} {} {budget};",
            m.id, pass.shader, pass.order
        ));
        match crate::postfx::compile(&source, &store.addon_cache_dir(&m.id).join("postfx")) {
            Ok(crate::postfx::Compiled { dxil, .. }) => {
                hasher.update(&dxil);
                built.lines.push(format!(
                    "postfx: {}'s pass {label} runs as pass {} after the tonemap ({} bytes of DXIL)",
                    m.name,
                    runtime.assets.postfx.len() + 1,
                    dxil.len()
                ));
                runtime.postfx(dxil, &label, &m.name, options, budget);
            }
            Err(e) => {
                hasher.update(format!("skipped {e};"));
                built
                    .warnings
                    .push(format!("{}: pass {label} is skipped: {e}", m.name));
            }
        }
    }
    let mut head_puts = crate::heads::Puts::default();
    let mut outputs = BTreeMap::new();
    for id in &resolved.order {
        let m = &addons[id];
        let mut out = OutputBuilder::default();
        hasher.update(format!("{id}@{};", m.version));
        let dir = store.addons_dir().join(id);
        let origin = Origin {
            addon: id.clone(),
            item: None,
        };
        let mut files = Vec::new();
        let package = packages.get(id);
        if let Some(name) = package {
            match package_ops(store, id) {
                Ok((file, picks)) => {
                    hasher.update(serde_json::to_vec(&file).expect("serialisable"));
                    out.made("picks", &picks);
                    files.push((FileKind::Database, file));
                    driver_picks.push((name.clone(), picks));
                }
                Err(e) => {
                    hasher.update(format!("unread {e};"));
                    built.warnings.push(format!("{name}: package skipped: {e}"));
                }
            }
        }
        if id == CORE_ID {
            hasher.update(CORE_DB);
            files.push((FileKind::Database, core_db_ops()));
        }
        for (kind, rel) in m.operations.op_files() {
            if package.is_some() && kind == FileKind::Database {
                continue;
            }
            let file = render_file(&dir.join(rel), kind, m, &p, &mut hasher)?;
            hash_sources(&mut hasher, store, id, &file.ops)?;
            files.push((kind, file));
        }
        let declared: Vec<AssetOp> = m.operations.file_ops().collect();
        hash_sources(&mut hasher, store, id, &declared)?;
        if let Some(op) = unbuilt(files.iter().flat_map(|(_, f)| &f.ops)) {
            built
                .warnings
                .push(format!("{id}: ERP operations skipped; {op} is not built yet"));
            for (_, f) in &mut files {
                f.ops
                    .retain(|op| !matches!(op, AssetOp::Erp(_) | AssetOp::Warn { .. }));
            }
        }
        let roots = |root: Root| source_dir(store, id, root);
        for (kind, file) in files {
            out.made(&format!("{kind:?}"), &file);
            for op in &file.ops {
                op.sources().into_iter().for_each(|src| out.reads(id, src));
                head_puts.op(id, op, &roots);
            }
            planner.push_file(&origin, kind, file);
        }
        for op in declared {
            out.made("declared", &op);
            op.sources().into_iter().for_each(|src| out.reads(id, src));
            head_puts.op(id, &op, &roots);
            planner.push(Op {
                op,
                origin: origin.clone(),
            });
        }
        if let Some(entry) = registry.get(id).filter(|e| !e.declared) {
            let host = Host::new(store, id, &p, Some(game))?.read_only(checking);
            let options = m.options.iter().map(|o| (o.id.clone(), p.option(m, o))).collect();
            let batch = entry
                .addon
                .build(&BuildCx {
                    cx: host.cx(),
                    options: &options,
                    features: &features,
                })
                .map_err(|e| Error::Other(format!("building {id}: {e}")))?;
            hasher.update(serde_json::to_vec(&batch.ops).expect("serialisable"));
            if !batch.fields.is_empty() {
                hasher.update(serde_json::to_vec(&batch.fields).expect("serialisable"));
            }
            let ops: Vec<AssetOp> = batch.ops.iter().map(|op| op.op.clone()).collect();
            hash_sources(&mut hasher, store, id, &ops)?;
            built
                .lines
                .extend(batch.lines.iter().map(|l| format!("{id}: {l}")));
            built
                .warnings
                .extend(batch.warnings.iter().map(|w| format!("{id}: {w}")));
            let mut ops = batch.ops;
            if let Some(op) = unbuilt(ops.iter().map(|o| &o.op)) {
                built
                    .warnings
                    .push(format!("{id}: ERP operations skipped; {op} is not built yet"));
                ops.retain(|o| !matches!(o.op, AssetOp::Erp(_)));
            }
            out.made("fields", &batch.fields);
            planner.add_fields(id, batch.fields);
            for op in ops {
                out.made("hook", &op);
                op.op
                    .sources()
                    .into_iter()
                    .for_each(|src| out.reads(&op.origin.addon, src));
                head_puts.op(&op.origin.addon, &op.op, &|root| {
                    source_dir(store, &op.origin.addon, root)
                });
                planner.push(op);
            }
        }
        for s in m.operations.screens.iter().filter(|s| s.page.is_none()) {
            out.reads(id, &Source::addon(s.file.clone()));
            let path = dir.join(&s.file);
            let data = std::fs::read(&path).map_err(io_err(&path))?;
            if swofty_formats::pkg::is_pkg(&data) {
                let (cat, engine) = (crate::ui::catalog::stock(), crate::ui::catalog::engine());
                let none = crate::ui::catalog::Scenes::default();
                let resource = format!("{id}/{}", s.file);
                let input = crate::ui::check::UiInput {
                    resource: &resource,
                    frag: "temp",
                    data: &data,
                    stock: None,
                };
                let report = crate::ui::check::ui_edit(&input, &none, cat, engine)?;
                if !report.hard.is_empty() {
                    let lines: Vec<String> = report.hard.iter().map(|h| h.to_string()).collect();
                    return Err(Error::Other(format!(
                        "{id}: screen {} breaks rules the game's UI loader enforces:\n  {}",
                        s.name,
                        lines.join("\n  ")
                    )));
                }
                built
                    .warnings
                    .extend(report.warnings.iter().map(|w| format!("{id}: ui: {w}")));
            }
            hasher.update(&data);
        }
        let pending = m.operations.ui.len();
        if pending > 0 {
            built.warnings.push(format!(
                "{id}: {pending} UI operation file(s) skipped; that stage is not built yet"
            ));
        }
        if !store.is_builtin(id) {
            for n in &m.operations.native {
                out.made("native", n);
                for rel in std::iter::once(&n.dll).chain(&n.files) {
                    out.reads(id, &Source::addon(rel.trim_end_matches('/')));
                }
            }
        }
        outputs.insert(id.clone(), out.finish());
    }
    if head_puts.pending() {
        let path = original(&store.root, game, GVDB);
        let stock = crate::heads::labels(&Gvdb::parse(&std::fs::read(&path).map_err(io_err(&path))?)?);
        let added = crate::heads::ops(&head_puts.new_heads(&stock), &stock, head_puts.registered());
        hasher.update(serde_json::to_vec(&added.ops).expect("serialisable"));
        let name = |id: &str| addons.get(id).map_or(id, |m| m.name.as_str()).to_owned();
        built
            .lines
            .extend(added.lines.iter().map(|(id, l)| format!("{}: {l}", name(id))));
        built
            .warnings
            .extend(added.warnings.iter().map(|(id, w)| format!("{}: {w}", name(id))));
        for op in added.ops {
            planner.push(op);
        }
    }
    let (tracks, left_out) = track_packages(store, planner.take_tracks())?;
    for (origin, why) in &left_out {
        let name = addons
            .get(&origin.addon)
            .map_or(origin.addon.as_str(), |m| m.name.as_str());
        built
            .warnings
            .push(crate::tracks::left_out_line(name, &origin.addon, why));
        planner.add_lenient(&origin.addon, name);
    }
    let left_codes: Vec<&str> = left_out
        .iter()
        .filter_map(|(_, why)| why.code.as_deref())
        .filter(|c| !tracks.iter().any(|(_, p, _)| p.track_code == *c))
        .collect();
    let maps: Vec<(PathBuf, TrackPackage)> = tracks.iter().map(|(d, p, _)| (d.clone(), p.clone())).collect();
    let mods: Vec<SessionMod> = planner
        .take_items()
        .into_iter()
        .filter_map(|op| match op.op {
            AssetOp::Item(ItemOp::Show {
                id,
                name,
                author,
                version,
                description,
            }) => Some(SessionMod {
                id,
                name,
                version,
                author,
                description,
            }),
            _ => None,
        })
        .collect();
    let natural = planner.route(Kind::Group) == Route::Runtime;
    if !natural && maps.len() > SLOTS.len() {
        return Err(Error::Map(format!(
            "at most {} tracks can be enabled at once",
            SLOTS.len()
        )));
    }
    let slots: Vec<Option<&str>> = (0..maps.len()).map(|i| (!natural).then(|| SLOTS[i])).collect();
    let reg = Registry::builtin();
    hasher.update(crate::activation::db::REGISTRY_JSON);
    for (dir, pkg) in &maps {
        hasher.update(serde_json::to_vec(pkg).expect("serialisable"));
        let erp = dir.join(&pkg.erp);
        let meta = std::fs::metadata(&erp).map_err(io_err(&erp))?;
        hasher.update(format!("{}|{:?}", meta.len(), meta.modified().ok()));
    }
    let resolve = |rel: &str| original(&store.root, game, rel);
    let ui_maps: Vec<MapUi> = maps
        .iter()
        .zip(&slots)
        .filter(|((_, pkg), _)| pkg.ui.is_some())
        .map(|((dir, pkg), slot)| MapUi {
            pkg,
            dir,
            asset_group: match slot {
                Some(slot) => slot_asset_group(slot),
                None => track_group(&pkg.track_code, &pkg.track_code),
            },
        })
        .collect();
    if !ui_maps.is_empty() {
        hasher.update(ui_stamp(&ui_maps, &resolve)?);
    }
    let mut caches = Vec::new();
    let mut session = store.session_settings(profile)?;
    session.set_maps(maps.iter().map(|(_, pkg)| pkg));
    session.set_mods(mods.into_iter());
    hasher.update(serde_json::to_vec(&session).expect("serialisable"));
    let menus = loader_dir().is_some();
    if menus {
        for rel in MENU_ERPS.iter().chain(MENU_READS) {
            let p = resolve(&ui_file(rel));
            let meta = std::fs::metadata(&p).map_err(io_err(&p))?;
            hasher.update(format!("{rel}|{}|{:?}", meta.len(), meta.modified().ok()));
        }
    }
    let db_src = original(&store.root, game, GVDB);
    hasher.update(sha256_file(&db_src)?);
    let order: Vec<&AddonManifest> = resolved.order.iter().map(|id| &addons[id]).collect();
    hash_unread(&mut hasher, store, game, &order, planner.erp_ops(), &resolve)?;
    built.buildhash = hex(&hasher.finalize())[..16].to_owned();
    built.overlay = store.overlay_dir(profile, &built.buildhash);
    let (lines_at, warnings_at) = (built.lines.len(), built.warnings.len());
    if let Some(saved) = Saved::load(&built.overlay, &built.buildhash).filter(|s| s.usable(store, game)) {
        saved.restore(&mut built)?;
        built.report.order = p.enabled().map(str::to_owned).collect();
        crate::write_json(&crate::conflicts::path(store, profile), &built.report)?;
        if !checking {
            fingerprint::record_outputs(store, profile, outputs).ok();
        }
        return Ok(built);
    }
    let cancel = || match checking {
        true => Ok(()),
        false => crate::busy::check(&store.root),
    };
    cancel()?;
    crate::now_doing("editing the game database");
    let ui_lists = if ui_maps.is_empty() {
        Default::default()
    } else {
        plan_lists(&ui_maps, &resolve)?.0
    };
    let mut stock_removals = BTreeMap::new();
    let mut kept_lists = BTreeMap::new();
    if !maps.is_empty() || planner.has_db() {
        let shipped = std::fs::read(&db_src).map_err(io_err(&db_src))?;
        let mut db = Gvdb::parse(&shipped)?;
        let stock: HashSet<u64> = db.records.iter().map(|r| r.key).collect();
        for ((_, pkg), slot) in maps.iter().zip(&slots) {
            let names = slot.map(|s| (slot_asset_group(s), format!("{s}.assetgroup")));
            let root = slot_asset_root();
            let route = match &names {
                Some((asset_group, key)) => TrackHost::Slot {
                    asset_group,
                    asset_root: &root,
                    key,
                },
                None => TrackHost::Natural,
            };
            let lists = ui_lists.get(&pkg.track_code).cloned().unwrap_or_default();
            let report = activate(&mut db, pkg, &reg, Some(&route), &lists)?;
            for l in report.lines {
                built.lines.push(format!("{}: {l}", pkg.track_code));
            }
            for w in report.warnings {
                built.warnings.push(format!("{}: {w}", pkg.track_code));
            }
        }
        let tracked: HashSet<u64> = db
            .records
            .iter()
            .map(|r| r.key)
            .filter(|k| !stock.contains(k))
            .collect();
        let shipped_db = Gvdb::parse(&shipped)?;
        let track_changes = match maps.is_empty() {
            true => Vec::new(),
            false => crate::tracks::changed_records(&db, &shipped_db),
        };
        let staged = (planner.route(Kind::Db) == Route::Runtime && planner.has_db()).then(|| db.clone());
        let mut db_report = crate::ops::DbReport::default();
        planner.keep_entrant_lists(crate::entrant_lists::StockLists::new(&shipped_db));
        let added = planner.apply_db_report(&mut db, &stock, &tracked, &mut db_report)?;
        built.lines.extend(db_report.lines);
        built.warnings.extend(db_report.warnings);
        built.warnings.extend(planner.take_skipped());
        let mut kept: BTreeMap<String, usize> = BTreeMap::new();
        for key in crate::tracks::keep_changes(&mut db, &shipped_db, &track_changes) {
            let by = planner
                .record_writer(key)
                .map_or("database", |id| addons.get(id).map_or(id, |m| m.name.as_str()));
            *kept.entry(by.to_owned()).or_default() += 1;
        }
        for (by, n) in kept {
            built.lines.push(format!(
                "{by}: kept the tracks' changes to {n} database value(s) it sets back to the game's own"
            ));
        }
        if !left_codes.is_empty() {
            let weekends = crate::tracks::restore_weekends(&mut db, &shipped_db, &left_codes);
            built.warnings.extend(weekends);
        }
        if let Ok(cat) = crate::dbedit::Catalog::open(store, game) {
            for t in crate::dbedit::rows::trim_row_counts(&cat, &mut db) {
                let by = planner
                    .record_writer(t.key)
                    .map_or("database", |id| addons.get(id).map_or(id, |m| m.name.as_str()));
                built.warnings.push(t.line(by));
            }
        }
        for u in crate::heads::keep_wearable(&mut db, &shipped_db) {
            let by = planner
                .record_writer(u.key)
                .map_or("database", |id| addons.get(id).map_or(id, |m| m.name.as_str()));
            built.warnings.push(u.line(by));
        }
        stock_removals = planner.take_stock_removals();
        for (id, n) in &stock_removals {
            let name = addons.get(id).map_or(id.as_str(), |m| m.name.as_str());
            built.warnings.push(stock_removals_line(name, *n));
        }
        for (id, n) in planner.take_track_removals() {
            let name = addons.get(&id).map_or(id.as_str(), |m| m.name.as_str());
            built.warnings.push(track_removals_line(name, n));
        }
        kept_lists = planner.take_kept_lists();
        for (id, lists) in &kept_lists {
            let name = addons.get(id).map_or(id.as_str(), |m| m.name.as_str());
            built.warnings.push(kept_lists_line(name, lists));
        }
        let mut copies = planner.take_copies();
        for c in &mut copies {
            c.quiet = c.addon == CORE_ID;
        }
        let addon_name = |id: &str| addons.get(id).map_or(id, |m| m.name.as_str()).to_owned();
        let side = crate::dbedit::deps::check(&db, &copies, &addon_name);
        built.warnings.extend(side.warnings);
        built.side_data = side.findings;
        if driver_picks.iter().any(|(_, p)| !p.is_empty()) {
            let why = match resolved.order.iter().any(|id| id == driver_manager::ID) {
                true => driver_manager::Why::NotAdded,
                false => driver_manager::Why::Off,
            };
            let names = driver_manager::names(store);
            built
                .warnings
                .extend(driver_manager::warnings(&db, &driver_picks, &names, why));
        }
        if !added.is_empty() {
            let found = owned::owned_items(&db, &Gvdb::parse(&shipped)?, &added);
            built.lines.extend(found.lines);
            built.warnings.extend(found.warnings);
            session.owned_items = found.items;
        }
        let cars = grid_size(&db);
        if let Some(staged) = staged {
            let room = runtime_record_room(staged.records.len());
            let created = db.records.len().saturating_sub(staged.records.len());
            match db_edits(&staged, &db) {
                Ok(_) if created > room => built.lines.push(format!(
                    "database: built into the game file; the addons add {created} record(s) and the game's \
                     database takes {room} more once it has loaded"
                )),
                Ok(edits) => {
                    runtime.db(edits);
                    db = staged;
                }
                Err(why) => built
                    .warnings
                    .push(format!("database: built into the game file; {why}")),
            }
        }
        let bytes = db.to_bytes();
        if bytes == shipped {
            built.lines.push("database: unchanged".into());
        } else {
            let out = built.overlay.join(GVDB);
            let dir = out.parent().expect("has parent");
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
            std::fs::write(&out, bytes).map_err(io_err(&out))?;
            built.plan.data.insert(GVDB.into(), out);
        }
        for ((dir, pkg, origin), slot) in tracks.iter().zip(&slots) {
            let slot_original = slot.map(|s| original(&store.root, game, &slot_file(s)));
            let placement = place(
                &PlaceInput {
                    pkg,
                    dir,
                    game: &game.dir,
                    slot: slot.zip(slot_original.as_deref()),
                    cars,
                },
                &store.root.join("cache/maps").join(&pkg.track_code),
            )?;
            caches.push(placement.dir.clone());
            let at = match slot {
                Some(slot) => format!("slot {slot}"),
                None => placement.archive.clone(),
            };
            built.lines.push(format!(
                "{}: track archive goes in {at}; {} texture(s) use its own mip files",
                pkg.track_code, placement.mips
            ));
            if !placement.groups.is_empty() {
                built.lines.push(format!(
                    "{}: {} asset group(s) registered in game",
                    pkg.track_code,
                    placement.groups.len()
                ));
            }
            for group in placement.groups {
                planner.push(Op {
                    op: AssetOp::Group(GroupOp::Register {
                        group,
                        erp: Some(placement.archive.clone()),
                        erps: Vec::new(),
                        deps: Vec::new(),
                        template: None,
                    }),
                    origin: origin.clone(),
                });
            }
            if let Some(g) = &placement.grid {
                built.lines.push(format!(
                    "{}: world extended from {} to {} grid slots, {} to {} garages and {} to {} pit boxes for {cars} cars",
                    pkg.track_code,
                    g.before.grid_slots,
                    g.after.grid_slots,
                    g.before.garages,
                    g.after.garages,
                    g.before.pit_boxes,
                    g.after.pit_boxes
                ));
            }
            for (alias, to) in &placement.aliases {
                built
                    .lines
                    .push(format!("{}: world {alias} stands in with {to}", pkg.track_code));
            }
            if !placement.repaired.is_empty() {
                built.lines.push(format!(
                    "{}: {}",
                    pkg.track_code,
                    crate::erpbuild::repaired_note(pkg.display_name(), &placement.repaired)
                ));
            }
            if !placement.spaced.is_empty() {
                built.lines.push(format!(
                    "{}: {}",
                    pkg.track_code,
                    crate::activation::ailine::fixed_note(pkg.display_name(), &placement.spaced)
                ));
            }
            built
                .warnings
                .extend(placement.notes.iter().map(|n| format!("{}: {n}", pkg.track_code)));
            let (left, replaced): (Vec<_>, Vec<_>) = placement
                .replaced
                .into_iter()
                .partition(|(rel, _)| left_over(store, game, rel));
            for (rel, src) in replaced {
                if built.plan.data.insert(rel.clone(), src).is_some() {
                    return Err(Error::Map(format!("{rel} is replaced twice")));
                }
            }
            for (rel, src) in placement.added.into_iter().chain(left) {
                if built.plan.added.insert(rel.clone(), src).is_some() {
                    return Err(Error::Map(format!("{rel} is placed by two tracks")));
                }
            }
        }
    }
    let mut menu_kept = Kept::new();
    if !ui_maps.is_empty() {
        let ui = build_ui(&ui_maps, &resolve, &store.root.join("cache/menus"))?;
        caches.push(ui.dir.clone());
        menu_kept = ui.kept;
        built
            .warnings
            .extend(ui.notes.iter().map(|n| format!("menus: {n}")));
        for (code, lists) in &ui.lists {
            built.lines.push(format!(
                "{code}: menu pictures in {} list(s), minimap and track-select scenes",
                lists.len()
            ));
        }
        for (rel, src) in ui.replaced {
            if built.plan.data.insert(rel.clone(), src).is_some() {
                return Err(Error::Map(format!("{rel} is replaced twice")));
            }
        }
    }
    let ers_maps: Vec<ErsMap> = maps
        .iter()
        .filter(|(_, pkg)| !pkg.ers.is_empty())
        .map(|(_, pkg)| ErsMap {
            code: &pkg.track_code,
            base: pkg.activation.base_track().unwrap_or(&pkg.track_code),
            values: &pkg.ers,
        })
        .collect();
    if !ers_maps.is_empty() {
        let ers = build_ers(&ers_maps, &game.dir, &resolve, &store.root.join("cache/ers"))?;
        caches.extend(ers.dir.clone());
        built.lines.push(format!(
            "ers: {} handling file(s) in {} car archive(s) get the tracks' settings",
            ers.handling_files,
            ers.replaced.len()
        ));
        built
            .warnings
            .extend(ers.notes.iter().map(|n| format!("ers: {n}")));
        for (rel, src) in ers.replaced {
            if built.plan.data.insert(rel.clone(), src).is_some() {
                return Err(Error::Map(format!("{rel} is replaced twice")));
            }
        }
    }
    let file_ops = planner.take_files();
    let mut erp_ops = planner.take_erp();
    let rank = |o: &Origin| {
        resolved
            .order
            .iter()
            .position(|id| *id == o.addon)
            .unwrap_or(usize::MAX)
    };
    let puts = archive_puts(store, &file_ops, &erp_ops, &rank)?;
    for (rel, put) in &puts {
        planner.put_archive(rel, &put.origin);
    }
    erp_ops.retain_mut(|op| {
        let rank = rank(&op.origin);
        after_puts(op, &puts, rank)
    });
    let mut mipmaps = BTreeMap::new();
    let mut built_over = BTreeSet::new();
    let name = |origin: &str| match origin.split_once(" (") {
        Some((id, item)) => match addons.get(id) {
            Some(m) => format!("{} ({item}", m.name),
            None => origin.to_owned(),
        },
        None => addons
            .get(origin)
            .map_or_else(|| origin.to_owned(), |m| m.name.clone()),
    };
    if !erp_ops.is_empty() {
        let staged = built.plan.data.clone();
        let base = |rel: &str| {
            staged
                .get(rel)
                .or_else(|| puts.get(rel).map(|p| &p.path))
                .cloned()
                .unwrap_or_else(|| resolve(rel))
        };
        let mut absent: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
        erp_ops.retain(|op| {
            let AssetOp::Erp(e) = &op.op else {
                return true;
            };
            let gone: Vec<&str> = reads(e)
                .into_iter()
                .filter(|rel| relative_inside(rel) && !base(rel).is_file())
                .collect();
            if gone.is_empty() {
                return true;
            }
            absent
                .entry(op.origin.addon.clone())
                .or_default()
                .extend(gone.into_iter().map(str::to_owned));
            false
        });
        for (id, rels) in &absent {
            built.warnings.push(missing_archives(&name(id), rels));
        }
        for (rel, put) in &puts {
            if erp_ops
                .iter()
                .any(|op| matches!(&op.op, AssetOp::Erp(e) if writes(e).contains(&rel.as_str())))
            {
                built.lines.push(format!(
                    "erp: {rel}: later changes apply to the whole archive {} puts",
                    put.origin
                ));
            }
        }
        let mut erps = ErpBuild::new(&base);
        for (erp, resource) in menu_kept.keys() {
            erps.keep(erp, resource);
        }
        let runtime_route = planner.route(Kind::Erp) == Route::Runtime;
        // Every origin that writes, in load order: the later wins.
        let mut ranks: Vec<String> = Vec::new();
        let mut prebuilt: Vec<Prebuilt> = Vec::new();
        let slices = slice_merges(&erp_ops);
        for (at, op) in erp_ops.iter().enumerate() {
            cancel()?;
            crate::now_doing(format!("applying {}", op.origin));
            let id = &op.origin.addon;
            let resolve = |s: &Source| Ok(s.resolve(&source_dir(store, id, s.root))?);
            let origin = op.origin.to_string();
            if matches!(op.op, AssetOp::Erp(_)) && !ranks.contains(&origin) {
                ranks.push(origin.clone());
            }
            match &op.op {
                AssetOp::Erp(crate::ops::ErpOp::Overlay {
                    erps: targets,
                    source,
                }) if runtime_route => {
                    let path = resolve(source)?;
                    let file = swofty_formats::erp::ErpFile::open(&path)?;
                    let stale = crate::erpbuild::stale_stats_in(&file)?.len();
                    let menus = file
                        .erp
                        .resources
                        .iter()
                        .any(|r| targets.iter().any(|t| erps.keeps(t, &r.name)));
                    let fits = match merged_later(&slices, at, targets, &path)? {
                        Some(why) => Err(why),
                        None if menus => Err("it carries the track menu pictures".to_owned()),
                        None if targets.iter().any(|t| !crate::erpbuild::served_at_runtime(t)) => {
                            Err("the in-game loader patches only archives under 2025_asset_groups".to_owned())
                        }
                        None if stale > 0 => {
                            Err(format!("{stale} resource(s) in it need their size headers fixed"))
                        }
                        None if !ers_maps.is_empty() && carries_handling(&path, targets)? => Err(
                            "it carries car handling files, which get the tracks' ERS settings".to_owned(),
                        ),
                        None => overlay_fits(&path, targets, &base)?,
                    };
                    match fits {
                        Ok(resources) => prebuilt.push(Prebuilt {
                            origin,
                            path,
                            erps: targets.clone(),
                            resources,
                        }),
                        Err(why) => {
                            built.lines.push(format!(
                                "erp: {origin}: its overlay archive goes in resource by resource; {why}"
                            ));
                            erps.apply_for(
                                &crate::ops::ErpOp::Overlay {
                                    erps: targets.clone(),
                                    source: source.clone(),
                                },
                                &origin,
                                &resolve,
                            )
                            .map_err(|e| Error::Other(format!("applying {}: {e}", op.origin)))?;
                        }
                    }
                }
                AssetOp::Erp(erp) => {
                    erps.apply_for(erp, &op.origin.to_string(), &resolve)
                        .map_err(|e| Error::Other(format!("applying {}: {e}", op.origin)))?;
                    for m in erps.take_merges() {
                        record_merge(&mut built, &mut planner, &op.origin, m);
                    }
                }
                AssetOp::Warn { message } => built.warnings.push(format!("{}: {message}", op.origin)),
                _ => {}
            }
        }
        if !ers_maps.is_empty() {
            let edited =
                erps.edit_written(|rel, r| match rel.starts_with(ers::TEAMS) && r.kind() == "VTF" {
                    true => ers::edit_vtf(r, &ers_maps),
                    false => Ok(None),
                })?;
            if edited > 0 {
                built.lines.push(format!(
                    "ers: {edited} handling file(s) other addons change get the tracks' settings too"
                ));
            }
        }
        let keeper = |h: &Held| {
            let codes = menu_kept.get(&(h.erp.clone(), h.resource.clone()))?;
            tracks
                .iter()
                .filter(|(_, p, _)| codes.contains(&p.track_code))
                .map(|(_, _, o)| o)
                .max_by_key(|o| rank(o))
                .cloned()
        };
        let origin = |s: &str| {
            erp_ops
                .iter()
                .find(|op| op.origin.to_string() == s)
                .map(|op| op.origin.clone())
        };
        keep_menus(
            &mut built,
            &mut planner,
            erps.take_held(),
            &keeper,
            &origin,
            &name,
        );
        for n in erps.take_notes() {
            built.lines.push(format!("erp: {n}"));
        }
        for w in erps.check_ui(&name)? {
            built.warnings.push(format!("ui: {w}"));
        }
        let stats = erps.repair_stats();
        let by_origin = |list: &[(String, String)]| {
            let mut out: Vec<(String, Vec<String>)> = Vec::new();
            for (origin, r) in list {
                match out.iter_mut().find(|(o, _)| o == origin) {
                    Some((_, rs)) => rs.push(r.clone()),
                    None => out.push((origin.clone(), vec![r.clone()])),
                }
            }
            out
        };
        if let Some((origin, files)) = by_origin(&stats.broken).first() {
            let who = name(origin);
            let fix = format!("Turn {who} off in this profile.");
            return Err(Error::Other(crate::erpbuild::unrepairable_message(
                &who, files, &fix,
            )));
        }
        for (origin, files) in by_origin(&stats.repaired) {
            built.lines.push(format!(
                "erp: {}",
                crate::erpbuild::repaired_note(&name(&origin), &files)
            ));
        }
        mipmaps = erps.take_mipmaps();
        if let Some(note) = planner.route_note(Kind::Erp) {
            built.lines.push(format!("erp: {note}"));
        }
        let resized = if planner.route(Kind::Erp) == Route::Runtime {
            let outside = erps.take_outside(&built.overlay)?;
            for rel in &outside {
                built.lines.push(format!(
                    "erp: {rel} built into the game files; the in-game loader patches only archives under 2025_asset_groups"
                ));
            }
            let resized = erps.take_resized(&built.overlay)?;
            for r in &resized {
                let (uri, why) = &r.resources[0];
                built.lines.push(format!(
                    "erp: {} built into the game files; {} resource(s) change their stock layout, which the in-game loader cannot load, such as {uri}: {why}",
                    r.erp,
                    r.resources.len()
                ));
            }
            let built_in: Vec<String> = resized.iter().map(|r| r.erp.clone()).chain(outside).collect();
            if !prebuilt.is_empty() {
                built.lines.push(format!(
                    "erp: {} ready overlay archive(s) load as they are",
                    prebuilt.len()
                ));
            }
            let notes = runtime.resources(erps.resources(), &ranks, &prebuilt, &built.overlay)?;
            built.warnings.extend(notes);
            stage_erps(&mut built, &staged, built_in)?;
            resized
        } else {
            let resized = erps.resized();
            let archives = store.root.join("cache").join(crate::cache::ARCHIVES_DIR);
            let (written, reused) = erps.write_cached(&built.overlay, Some(&archives), &cancel)?;
            if reused > 0 {
                built.lines.push(format!(
                    "erp: {reused} of {} archive(s) unchanged since an earlier build, linked from the cache",
                    written.len()
                ));
            }
            stage_erps(&mut built, &staged, written)?;
            resized
        };
        built.warnings.extend(stock_holders(&game.dir, &resized));
        built_over = puts
            .keys()
            .filter(|rel| built.plan.data.contains_key(*rel) && !staged.contains_key(*rel))
            .cloned()
            .collect();
    }
    cancel()?;
    crate::now_doing("placing files");
    put_files(&mut built, store, game, mipmaps, file_ops, &built_over, &ers_maps)?;
    match loader_dir() {
        Some(dir) => {
            built.plan.loader = loader_files(&dir)?;
            built.plan.registry_override = true;
            built.lines.push(format!(
                "loader: {} file(s) from {}",
                built.plan.loader.len(),
                dir.display()
            ));
            if menus {
                let lang = planner.route(Kind::Lang) == Route::Runtime;
                let writer = |erp: &str, res: &str| {
                    let targets = [
                        Target::Resource {
                            erp: erp.to_owned(),
                            uri: res.to_owned(),
                        },
                        Target::File(erp.to_owned()),
                    ];
                    planner.last_writer(&targets).map(|o| name(&o.to_string()))
                };
                for (key, value) in build_menus(&mut built, &mut session, &resolve, &writer, lang)? {
                    planner.push(Op {
                        op: AssetOp::Lang(LangOp::Set {
                            language: "*".into(),
                            key,
                            value,
                        }),
                        origin: Origin {
                            addon: CORE_ID.into(),
                            item: None,
                        },
                    });
                }
            }
            let files = screen_files(&order, &store.addons_dir())?;
            if !files.is_empty() {
                built.lines.push(format!("ui: {} screen(s) under {LOADER_DIR}/ui", files.len()));
            }
            for (rel, src) in files {
                if built.plan.loader.insert(rel.clone(), src).is_some() {
                    return Err(Error::Other(format!("{rel} is placed twice")));
                }
            }
            let core = format!("{LOADER_DIR}/{ADDONS_DIR}/core/");
            let taken: Vec<String> = built
                .plan
                .loader
                .keys()
                .filter_map(|rel| rel.strip_prefix(&core))
                .filter(|n| n.ends_with(".dll") && !n.contains('/'))
                .map(str::to_owned)
                .collect();
            let plugins = plugin_files(store, order.iter().copied(), taken)?;
            if !plugins.dlls.is_empty() {
                built.lines.push(format!(
                    "native: {} plugin(s), {} file(s) under {LOADER_DIR}/{ADDONS_DIR}",
                    plugins.dlls.len(),
                    plugins.files.len()
                ));
            }
            for (rel, src) in plugins.files {
                if built.plan.loader.insert(rel.clone(), src).is_some() {
                    return Err(Error::Other(format!("{rel} is placed twice")));
                }
            }
            session.plugins = plugins.dlls;
        }
        None => built.warnings.push(format!(
            "{CORE_ID}: the in-game loader was not found (native/dist, loader/ beside the swofty command, or {LOADER_ENV}); the game starts without it"
        )),
    }
    built.settings = Some(session);
    let mut changes = planner.changes();
    let planned = planner.finish();
    for op in planned.runtime {
        let resolve = |op: &Op, s: &Source| Ok(s.resolve(&source_dir(store, &op.origin.addon, s.root))?);
        runtime.push(op, &resolve)?;
    }
    if let Some(runtime) = runtime.finish(&built.overlay)? {
        built.lines.push(runtime.summary());
        for (rel, src) in runtime.files {
            if built.plan.loader.insert(rel.clone(), src).is_some() {
                return Err(Error::Other(format!("{rel} is placed twice")));
            }
        }
    }
    built.warnings.extend(waiting_lines(&planned.waiting, &features));
    for (_, _, origin) in &tracks {
        changes.entry(origin.addon.clone()).or_default().tracks += 1;
    }
    let name = |id: &str| addons.get(id).map_or_else(|| id.to_owned(), |m| m.name.clone());
    for id in resolved.order.iter().filter(|id| *id != CORE_ID) {
        if let Some(c) = changes.get(id).filter(|c| !c.is_empty()) {
            built.lines.push(format!("{}: {c}", name(id)));
        }
    }
    let records = planned
        .conflicts
        .iter()
        .any(|c| matches!(c.target, Target::Record(_)));
    let catalog = records
        .then(|| crate::dbedit::Catalog::open(store, game).ok())
        .flatten();
    let record = |key: u64| {
        catalog
            .as_deref()
            .and_then(|c| crate::conflicts::record_name(c, key))
    };
    let (report, within) = crate::conflicts::summarise(profile, &planned.conflicts, &record);
    built.warnings.extend(report.pairs.iter().map(|p| p.line(&name)));
    built.warnings.extend(within);
    built.report = Report {
        order: built.report.order.clone(),
        stock_removals,
        kept_lists,
        ..report
    };
    built.conflicts = planned.conflicts;
    built.waiting = planned.waiting;
    if !built.plan.is_vanilla() {
        let plan_tracks: Vec<crate::tracks::PlanTrack> = tracks
            .iter()
            .map(|(_, pkg, origin)| crate::tracks::PlanTrack {
                code: pkg.track_code.clone(),
                name: pkg.track_name.clone().filter(|n| !n.trim().is_empty()),
                addon: Some(origin.addon.clone()),
            })
            .collect();
        let listing = serde_json::json!({
            "profile": profile,
            "buildhash": built.buildhash,
            "tracks": plan_tracks,
            "replace": built.plan.data,
            "add": built.plan.added,
            "loader": built.plan.loader,
        });
        crate::write_json(&built.overlay.join(PLAN_FILE), &listing)?;
    }
    cancel()?;
    crate::now_doing("saving the build");
    let saved = (!built.plan.is_vanilla())
        .then(|| Saved::of(&built, lines_at, warnings_at))
        .transpose()?;
    caches.push(built.overlay.clone());
    caches.extend(
        [&built.plan.data, &built.plan.added, &built.plan.loader]
            .into_iter()
            .flat_map(|m| m.values().cloned()),
    );
    let removed = crate::cache::prune(&store.root.join("cache"), profile, &caches);
    if removed > 0 {
        built
            .lines
            .push(format!("cache: {removed} stale build folder(s) removed"));
    }
    crate::cache::prune_packed(&packed, PACKED_AGE);
    crate::cache::prune_archives(
        &store.root.join("cache").join(crate::cache::ARCHIVES_DIR),
        ARCHIVE_AGE,
        ARCHIVE_BYTES,
    );
    if let Some(saved) = saved {
        crate::write_json(&built.overlay.join(SAVED_FILE), &saved)?;
    }
    crate::write_json(&crate::conflicts::path(store, profile), &built.report)?;
    if !checking {
        fingerprint::record_outputs(store, profile, outputs).ok();
    }
    Ok(built)
}

/// A file an earlier session created that still holds exactly what it wrote
/// is a leftover to clear, not a game file to replace.
fn left_over(store: &Store, game: &Game, rel: &str) -> bool {
    let Ok(placed) = Placed::load(&store.root) else {
        return false;
    };
    let Some(p) = placed.get(rel).filter(|p| !p.replaced) else {
        return false;
    };
    let stock = Baseline::load_or_new(&store.root, &game.identity).is_ok_and(|b| b.files.contains_key(rel));
    !stock && sha256_opt(&game.dir.join(rel)).ok().flatten().as_deref() == Some(p.sha256.as_str())
}

fn slice_merges(ops: &[Op]) -> Vec<(usize, &str, &str)> {
    ops.iter()
        .enumerate()
        .filter_map(|(i, op)| match &op.op {
            AssetOp::Erp(crate::ops::ErpOp::MergeSlices { erp, resource, .. }) => {
                Some((i, erp.as_str(), resource.as_str()))
            }
            _ => None,
        })
        .collect()
}

// A ready overlay loads apart from the build's archives, so a later merge-slices
// into one of its resources needs it to go in resource by resource.
fn merged_later(
    slices: &[(usize, &str, &str)],
    at: usize,
    targets: &[String],
    path: &Path,
) -> Result<Option<String>> {
    let later: Vec<_> = slices
        .iter()
        .filter(|(i, erp, _)| *i > at && targets.iter().any(|t| t == erp))
        .collect();
    if later.is_empty() {
        return Ok(None);
    }
    let names = crate::erpbuild::overlay_names(path)?;
    Ok(later
        .iter()
        .find(|(_, _, uri)| names.iter().any(|n| n == uri))
        .map(|(_, erp, uri)| format!("a later erp.merge-slices merges into {uri} in {erp}")))
}

fn kept_menus_line(name: &str) -> String {
    format!("{name} ships its own track menu pictures; the launcher's track menus are kept")
}

fn keep_menus(
    built: &mut Built,
    planner: &mut Planner,
    held: Vec<Held>,
    keeper: &dyn Fn(&Held) -> Option<Origin>,
    origin: &dyn Fn(&str) -> Option<Origin>,
    name: &dyn Fn(&str) -> String,
) {
    let mut warned = Vec::new();
    for h in held {
        let line = kept_menus_line(&name(&h.origin));
        if !warned.contains(&line) {
            built.warnings.push(line.clone());
            warned.push(line);
        }
        if let (Some(keeper), Some(lost)) = (keeper(&h), origin(&h.origin))
            && keeper.addon != lost.addon
        {
            let target = Target::Resource {
                erp: h.erp,
                uri: h.resource,
            };
            planner.kept(target, &keeper, &lost);
        }
    }
}

fn record_merge(built: &mut Built, planner: &mut Planner, origin: &Origin, m: SliceMerge) {
    built
        .warnings
        .extend(m.notes.iter().map(|n| format!("erp: {origin}: {n}")));
    let slice = |slice: usize| Target::Slice {
        erp: m.erp.clone(),
        uri: m.resource.clone(),
        slice,
    };
    match (m.whole, m.slices) {
        (true, 0) => planner.replaced(
            Target::Resource {
                erp: m.erp.clone(),
                uri: m.resource.clone(),
            },
            origin,
        ),
        (true, n) => (0..n).for_each(|i| planner.replaced(slice(i), origin)),
        (false, n) => {
            built.lines.push(format!(
                "erp: {origin}: {} of {n} slice(s) of {} merged into {}",
                m.changed.len(),
                m.resource,
                m.erp
            ));
            m.changed.iter().for_each(|&i| planner.replaced(slice(i), origin));
        }
    }
}

/// Whether the overlay archive at `path` holds a car handling file for one of
/// the 2026 team archives in `targets`.
fn carries_handling(path: &Path, targets: &[String]) -> Result<bool> {
    if !targets.iter().any(|t| t.starts_with(ers::TEAMS)) {
        return Ok(false);
    }
    let file = swofty_formats::erp::ErpFile::open(path)?;
    Ok(file.erp.resources.iter().any(|r| r.kind() == "VTF"))
}

fn stage_erps(built: &mut Built, staged: &BTreeMap<String, PathBuf>, rels: Vec<String>) -> Result<()> {
    for rel in rels {
        let src = built.overlay.join(&rel);
        if built.plan.data.insert(rel.clone(), src).is_some() && !staged.contains_key(&rel) {
            return Err(Error::Other(format!("{rel} is replaced twice")));
        }
    }
    Ok(())
}

/// The warning for an addon's archive changes a build skips because the
/// archives they change aren't in the game folder.
fn missing_archives(name: &str, rels: &BTreeSet<String>) -> String {
    let first = rels.first().map_or("", String::as_str);
    let what = match rels.len() {
        1 => format!("{first} is not a file in F1 25"),
        n => format!("{n} of the files it changes are not in F1 25, such as {first}"),
    };
    format!(
        "{name}: archive changes skipped; {what}. Turn {name} off if it doesn't work, and tell its author"
    )
}

/// An archive a `file.put` replaces whole while ERP ops change it too: its last
/// put in load order.
struct ArchivePut {
    rank: usize,
    origin: Origin,
    path: PathBuf,
}

fn archive_puts(
    store: &Store,
    files: &[Op],
    erps: &[Op],
    rank: &dyn Fn(&Origin) -> usize,
) -> Result<BTreeMap<String, ArchivePut>> {
    let changed: BTreeSet<&str> = erps
        .iter()
        .flat_map(|op| match &op.op {
            AssetOp::Erp(e) => writes(e),
            _ => Vec::new(),
        })
        .collect();
    let mut puts = BTreeMap::new();
    for op in files {
        let AssetOp::File(FileOp::Put { source, target }) = &op.op else {
            continue;
        };
        if changed.contains(target.as_str()) {
            let path = source.resolve(&source_dir(store, &op.origin.addon, source.root))?;
            puts.insert(
                target.clone(),
                ArchivePut {
                    rank: rank(&op.origin),
                    origin: op.origin.clone(),
                    path,
                },
            );
        }
    }
    Ok(puts)
}

/// Leaves out of `op` the archives a put replaces later in load order; false
/// when none is left.
fn after_puts(op: &mut Op, puts: &BTreeMap<String, ArchivePut>, rank: usize) -> bool {
    let replaced = |erp: &str| puts.get(erp).is_some_and(|p| p.rank > rank);
    match &mut op.op {
        AssetOp::Erp(crate::ops::ErpOp::Overlay { erps, .. }) => {
            erps.retain(|e| !replaced(e));
            !erps.is_empty()
        }
        AssetOp::Erp(e) => !writes(e).into_iter().any(replaced),
        _ => true,
    }
}

fn index_of(path: &Path) -> Option<Vec<u8>> {
    use std::io::{Read, Seek, SeekFrom};
    let mut f = std::fs::File::open(path).ok()?;
    let mut head = [0u8; 0x30];
    f.read_exact(&mut head).ok()?;
    if &head[..4] != b"ERPK" {
        return None;
    }
    let at = u64::from_le_bytes(head[0x10..0x18].try_into().ok()?);
    let len = u64::from_le_bytes(head[0x18..0x20].try_into().ok()?);
    if len > 1 << 28 {
        return None;
    }
    let mut index = vec![0; len as usize];
    f.seek(SeekFrom::Start(at)).ok()?;
    f.read_exact(&mut index).ok()?;
    Some(index)
}

fn erp_files(dir: &Path, rel: &str, out: &mut Vec<String>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for e in entries.flatten() {
        let name = e.file_name().to_string_lossy().into_owned();
        let path = e.path();
        let child = format!("{rel}/{name}");
        if path.is_dir() {
            erp_files(&path, &child, out);
        } else if name.to_ascii_lowercase().ends_with(".erp") {
            out.push(child);
        }
    }
}

// The game loads a resource from whichever archive reads it first, so a
// resized resource that another stock archive also carries can still show
// the stock copy. Each name is matched as it sits in an archive index.
fn stock_holders(game_dir: &Path, resized: &[Resized]) -> Vec<String> {
    let mut by_root: BTreeMap<String, Vec<&str>> = BTreeMap::new();
    let mut written = std::collections::BTreeSet::new();
    for r in resized {
        let root: Vec<&str> = r.erp.splitn(3, '/').take(2).collect();
        for (uri, _) in &r.resources {
            written.insert((r.erp.to_ascii_lowercase(), uri.as_str()));
            by_root.entry(root.join("/")).or_default().push(uri);
        }
    }
    let mut out = Vec::new();
    for (root, mut uris) in by_root {
        uris.sort_unstable();
        uris.dedup();
        let needles: Vec<(&str, Vec<u8>)> = uris
            .iter()
            .map(|u| {
                let mut n = ((u.len() + 1) as u16).to_le_bytes().to_vec();
                n.extend_from_slice(u.as_bytes());
                n.push(0);
                (*u, n)
            })
            .collect();
        let mut files = Vec::new();
        erp_files(&game_dir.join(&root), &root, &mut files);
        files.sort();
        for rel in files {
            let Some(index) = index_of(&game_dir.join(&rel)) else {
                continue;
            };
            for (uri, needle) in &needles {
                let held = index
                    .windows(needle.len())
                    .any(|w| w[0] == needle[0] && w == needle.as_slice());
                if held && !written.contains(&(rel.to_ascii_lowercase(), *uri)) {
                    out.push(format!(
                        "erp: {uri} is resized, but {rel} also carries it and keeps the stock copy; replace it there too, or the game may show either one"
                    ));
                }
            }
        }
    }
    out
}

fn put_files(
    built: &mut Built,
    store: &Store,
    game: &Game,
    mipmaps: BTreeMap<String, PathBuf>,
    ops: Vec<Op>,
    built_over: &BTreeSet<String>,
    ers_maps: &[ErsMap],
) -> Result<()> {
    let mips = mipmaps.len();
    let mut put: BTreeMap<String, (PathBuf, Option<Origin>)> =
        mipmaps.into_iter().map(|(rel, src)| (rel, (src, None))).collect();
    for op in ops {
        let AssetOp::File(FileOp::Put { source, target }) = op.op else {
            continue;
        };
        if !relative_inside(&target) {
            return Err(Error::Resolve(format!(
                "{target} must be a relative path inside the game folder"
            )));
        }
        let src = source.resolve(&source_dir(store, &op.origin.addon, source.root))?;
        put.insert(target, (src, Some(op.origin)));
    }
    if put.is_empty() {
        return Ok(());
    }
    let baseline = Baseline::load_or_new(&store.root, &game.identity).ok();
    let stock = |rel: &str| {
        baseline.as_ref().is_some_and(|b| b.files.contains_key(rel))
            || (game.dir.join(rel).is_file() && !left_over(store, game, rel))
    };
    let (mut replaced, mut added, mut handling) = (0, 0, 0);
    for (rel, (mut src, origin)) in put {
        let car = !ers_maps.is_empty() && ers::is_car_archive(&rel);
        if car && !built_over.contains(&rel) {
            let out = built.overlay.join("ers").join(&rel);
            let n = ers::edit_archive(&src, ers_maps, &out)?;
            if n > 0 {
                handling += n;
                src = out;
            }
        }
        let is_stock = stock(&rel);
        if is_stock {
            replaced += 1;
        } else {
            added += 1;
        }
        if built_over.contains(&rel) {
            if !is_stock && let Some(archive) = built.plan.data.remove(&rel) {
                built.plan.added.insert(rel, archive);
            }
            continue;
        }
        let into = match is_stock {
            true => &mut built.plan.data,
            false => &mut built.plan.added,
        };
        if into.insert(rel.clone(), src).is_some() && !car {
            return Err(Error::Other(match origin {
                Some(o) => format!(
                    "{rel} is replaced twice: {o} puts the whole file, and an added track changes it too"
                ),
                None => format!("{rel} is replaced twice"),
            }));
        }
    }
    built
        .lines
        .push(format!("files: {replaced} game file(s) replaced, {added} added"));
    if handling > 0 {
        built.lines.push(format!(
            "ers: {handling} handling file(s) in car archives addons put get the tracks' settings"
        ));
    }
    if mips > 0 {
        built
            .lines
            .push(format!("mipmaps: {mips} mip file(s) go in the game folder"));
    }
    Ok(())
}

pub fn build_menus(
    built: &mut Built,
    session: &mut SessionSettings,
    original: &dyn Fn(&str) -> PathBuf,
    writer: &crate::ui::Writer,
    lang: bool,
) -> Result<Vec<(String, String)>> {
    let input = |rel: &str| built.plan.data.get(rel).cloned().unwrap_or_else(|| original(rel));
    let writer = |erp: &str, res: &str| {
        built
            .plan
            .data
            .contains_key(erp)
            .then(|| writer(erp, res))
            .flatten()
    };
    let menus = crate::ui::build(session, &input, &writer, &built.overlay, lang)?;
    built.lines.push(format!(
        "ui: ADDON SETTINGS with {} screen(s) under {LOADER_DIR}/ui, {} game archive(s) patched",
        menus.session.len(),
        menus.erps.len()
    ));
    if !menus.strings.is_empty() {
        built.lines.push(format!(
            "ui: the online services popup reads its text from {} language string(s)",
            menus.strings.len()
        ));
    }
    built.lines.extend(menus.lines);
    built.warnings.extend(menus.warnings);
    built.plan.data.extend(menus.erps);
    for (rel, src) in menus.screens {
        if built.plan.loader.insert(rel.clone(), src).is_some() {
            return Err(Error::Other(format!("{rel} is placed twice")));
        }
    }
    let ui = session.ui.get_or_insert_with(|| SessionUi {
        register: true,
        strict: false,
        recover: true,
        screens: Vec::new(),
    });
    if let Some(s) = ui
        .screens
        .iter()
        .find(|s| menus.session.iter().any(|m| m.name == s.name))
    {
        return Err(Error::Other(format!(
            "screen {} is reserved for ADDON SETTINGS; rename it in its addon",
            s.name
        )));
    }
    ui.screens.retain(|s| s.page.is_none());
    ui.screens.splice(0..0, menus.session);
    Ok(menus.strings)
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use super::*;
    use crate::asset::{AssetBatch, Op, Origin};
    use crate::asset::{FileOp, Root, Source};
    use crate::baseline::Identity;
    use crate::launcher::{Addon, Builtin};

    #[test]
    fn a_missing_or_cut_short_file_names_its_addon_or_the_game() {
        let root = std::env::temp_dir().join(format!("swofty-named-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let store = Store::new(root.join("data"));
        let game = root.join("game");
        let addon = store.addons_dir().join("mod.miami24");
        std::fs::create_dir_all(addon.join("track")).unwrap();
        std::fs::write(
            addon.join("swofty.json"),
            r#"{"schema_version":1,"id":"mod.miami24","name":"Igora Drive","version":"0.9.13","author":"t","compat":{"game":"f1_25"}}"#,
        )
        .unwrap();
        let io = |path: PathBuf, kind| Error::Io {
            path,
            source: std::io::Error::from(kind),
        };
        let gone = named(
            &store,
            &game,
            io(
                addon.join("track").join("miami24.erp"),
                std::io::ErrorKind::NotFound,
            ),
        );
        assert_eq!(
            gone.to_string(),
            "Igora Drive (mod.miami24) is missing its file track/miami24.erp. Turn Igora Drive off in this \
             profile, or remove it on the Addons page and install or import it again."
        );
        let short = named(
            &store,
            &game,
            Error::Formats(swofty_formats::Error::IoAt {
                path: addon.join("track/mipmaps/a.mipmaps"),
                source: std::io::Error::from(std::io::ErrorKind::UnexpectedEof),
            }),
        );
        assert!(
            short.to_string().starts_with(
                "Igora Drive (mod.miami24) has an incomplete or damaged track/mipmaps/a.mipmaps."
            ),
            "{short}"
        );
        let stock = named(
            &store,
            &game,
            io(
                game.join("2025_asset_groups/a.erp"),
                std::io::ErrorKind::UnexpectedEof,
            ),
        );
        assert!(
            stock
                .to_string()
                .starts_with("2025_asset_groups/a.erp in the game folder is incomplete or damaged; verify"),
            "{stock}"
        );
        let missing = named(
            &store,
            &game,
            io(
                game.join("character_package/a.mipmaps"),
                std::io::ErrorKind::NotFound,
            ),
        );
        assert!(
            missing
                .to_string()
                .starts_with("character_package/a.mipmaps is missing from the game folder; update F1 25"),
            "{missing}"
        );
        let denied = named(
            &store,
            &game,
            io(
                addon.join("track/miami24.erp"),
                std::io::ErrorKind::PermissionDenied,
            ),
        );
        assert!(matches!(denied, Error::Io { .. }), "{denied}");
        std::fs::remove_dir_all(&root).ok();
    }

    struct Hook(AddonManifest);

    impl Addon for Hook {
        fn manifest(&self) -> &AddonManifest {
            &self.0
        }

        fn build(&self, cx: &BuildCx) -> swofty_addon::Result<AssetBatch> {
            let origin = Origin {
                addon: cx.cx.addon.into(),
                item: None,
            };
            Ok(AssetBatch {
                ops: vec![
                    Op {
                        op: AssetOp::Warn {
                            message: format!("{} cars", cx.options["cars"]),
                        },
                        origin: origin.clone(),
                    },
                    Op {
                        op: AssetOp::Erp(crate::ops::ErpOp::PatchPkg {
                            erp: "2025_asset_groups/missing.erp".into(),
                            resource: "eaid://missing".into(),
                            fragment: 0,
                            edits: vec![crate::ops::PkgEdit::SetText {
                                node: "n".into(),
                                value: "v".into(),
                                localise: false,
                            }],
                        }),
                        origin,
                    },
                ],
                lines: vec![
                    format!("profile {}", cx.cx.profile.name),
                    match cx
                        .cx
                        .addons()
                        .map(|a| (a.list(), a.set_enabled(cx.cx.addon, true)))
                    {
                        Some((Ok(_), Ok(()))) => "turned on".to_owned(),
                        Some((listed, turned)) => format!("{:?} {:?}", listed.err(), turned.err()),
                        None => "no addons".to_owned(),
                    },
                ],
                ..AssetBatch::default()
            })
        }
    }

    #[test]
    fn warns_about_stock_archives_that_still_carry_a_resized_resource() {
        use swofty_formats::erp::{Erp, Fragment, HEADER_LEN, MAGIC, Resource, STORED};
        let dir = std::env::temp_dir().join(format!("swofty-build-holders-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        let ui = "2025_asset_groups/ui_package";
        let scene = "eaid://ui_package/art/screens/career/rnd_overview.idf";
        let archive = |names: &[&str]| {
            let mut header = [0u8; HEADER_LEN];
            header[..4].copy_from_slice(MAGIC);
            header[4..8].copy_from_slice(&3u32.to_le_bytes());
            Erp {
                header,
                version: 3,
                resources: names
                    .iter()
                    .map(|n| Resource {
                        name: (*n).into(),
                        kind: swofty_formats::erp::kind("UILayout").unwrap(),
                        unk: vec![0; 4],
                        fragments: vec![Fragment {
                            name: *b"temp",
                            offset: 0,
                            size: 1,
                            flags: 0x10,
                            compression: STORED,
                            packed: 1,
                            raw: vec![0],
                        }],
                        hash: [0; 16],
                    })
                    .collect(),
            }
            .to_bytes()
        };
        for (rel, names) in [
            ("flow_career_hub.erp", vec![scene]),
            ("flow_team_hq.erp", vec!["eaid://ui_package/x.idf", scene]),
            ("screens/laptop.erp", vec![scene]),
            (
                "screens/other.erp",
                vec!["eaid://ui_package/art/screens/career/rnd_overview.idf_b"],
            ),
        ] {
            let path = dir.join(ui).join(rel);
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(path, archive(&names)).unwrap();
        }
        std::fs::write(dir.join(ui).join("notes.txt"), b"ERPK").unwrap();
        let resized = |erp: &str| Resized {
            erp: format!("{ui}/{erp}"),
            resources: vec![(scene.into(), "fragment 0 is 2 bytes, the stock one 1".into())],
        };
        assert_eq!(
            stock_holders(
                &dir,
                &[resized("flow_career_hub.erp"), resized("screens/laptop.erp")]
            ),
            [format!(
                "erp: {scene} is resized, but {ui}/flow_team_hq.erp also carries it and keeps the stock copy; replace it there too, or the game may show either one"
            )]
        );
        assert!(
            stock_holders(
                &dir,
                &[
                    resized("flow_career_hub.erp"),
                    resized("flow_team_hq.erp"),
                    resized("screens/laptop.erp")
                ]
            )
            .is_empty()
        );
        std::fs::remove_dir_all(&dir).ok();
    }

    fn logo_archive(name: &str, slices: &[u8]) -> Vec<u8> {
        use swofty_formats::erp::{Erp, Fragment, HEADER_LEN, MAGIC, Resource, STORED};
        let mut head = vec![0u8; 40];
        head[12..16].copy_from_slice(&2u32.to_le_bytes());
        head[28..32].copy_from_slice(&(slices.len() as u32).to_le_bytes());
        let pixels: Vec<u8> = slices.iter().flat_map(|&b| [b; 4]).collect();
        let fragment = |raw: Vec<u8>| Fragment {
            name: *b"temp",
            offset: 0,
            size: raw.len() as u64,
            flags: 0x10,
            compression: STORED,
            packed: raw.len() as u64,
            raw,
        };
        let mut header = [0u8; HEADER_LEN];
        header[..4].copy_from_slice(MAGIC);
        header[4..8].copy_from_slice(&3u32.to_le_bytes());
        Erp {
            header,
            version: 3,
            resources: vec![Resource {
                name: name.into(),
                kind: swofty_formats::erp::kind("GfxSurfaceRes").unwrap(),
                unk: vec![0; 4],
                fragments: vec![fragment(head), fragment(pixels)],
                hash: [0; 16],
            }],
        }
        .to_bytes()
    }

    #[test]
    fn merged_slices_conflict_one_slice_at_a_time() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-slices-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        let ui = "2025_asset_groups/ui_package/common_front_end.erp";
        let logos = "eaid://ui_package/art/texture_arrays/t.texarray.image";
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::create_dir_all(dir.join("2025_asset_groups/ui_package")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let records = vec![Record::new("entrants.teams._team_instancecount_", 0, "0")];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        std::fs::write(dir.join(ui), logo_archive(logos, &[0, 1, 2, 3])).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        for (id, slices) in [
            ("t.a", [0, 9, 2, 3]),
            ("t.b", [0, 1, 2, 8]),
            ("t.c", [0, 7, 2, 3]),
            ("t.same", [0, 1, 2, 3]),
        ] {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(&src).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{id}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(
                    r#"{{ "schema_version": 1, "ops": [{{ "op": "erp.merge-slices", "erp": "{ui}", "resource": "{logos}", "source": "logos.erp" }}] }}"#
                ),
            )
            .unwrap();
            std::fs::write(src.join("logos.erp"), logo_archive(logos, &slices)).unwrap();
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        }
        let built = build(&store, "default", &game).unwrap();
        let conflicts: Vec<String> = built.conflicts.iter().map(ToString::to_string).collect();
        assert_eq!(
            conflicts,
            [format!("slice 1 of {logos} in {ui}: t.c overrides t.a")]
        );
        for (id, n) in [("t.a", 1), ("t.b", 1), ("t.c", 1)] {
            let line = format!("erp: {id}: {n} of 4 slice(s) of {logos} merged into {ui}");
            assert!(built.lines.contains(&line), "{line}: {:?}", built.lines);
        }
        let file = swofty_formats::erp::ErpFile::open(&built.plan.data[ui]).unwrap();
        let merged = file.load(&file.erp.resources[0]).unwrap();
        let slices: Vec<u8> = merged.fragments[1]
            .data()
            .unwrap()
            .chunks(4)
            .map(|c| c[0])
            .collect();
        assert_eq!(slices, [0, 7, 2, 8]);
        store
            .update_profile("default", |p| {
                ["t.a", "t.b", "t.c"]
                    .iter()
                    .try_for_each(|id| p.set_enabled(id, false))
            })
            .unwrap();
        let same = build(&store, "default", &game).unwrap();
        assert!(!same.plan.data.contains_key(ui), "{:?}", same.plan.data);
        assert!(!same.overlay.join(ui).exists());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn overlays_go_in_resource_by_resource_before_a_later_merge_into_them() {
        let root = std::env::temp_dir().join(format!("swofty-build-merged-later-{}", std::process::id()));
        std::fs::create_dir_all(&root).unwrap();
        let path = root.join("ready.erp");
        let logos = "eaid://ui_package/t.texarray.image";
        std::fs::write(&path, logo_archive(logos, &[1, 2])).unwrap();
        let op = |op: &str| Op {
            op: serde_json::from_str(op).unwrap(),
            origin: Origin {
                addon: "t".into(),
                item: None,
            },
        };
        let ops = [
            op(r#"{"op":"erp.overlay","erps":["a.erp","b.erp"],"source":"ready.erp"}"#),
            op(&format!(
                r#"{{"op":"erp.merge-slices","erp":"b.erp","resource":"{logos}","source":"x.erp"}}"#
            )),
            op(&format!(
                r#"{{"op":"erp.merge-slices","erp":"c.erp","resource":"{logos}","source":"x.erp"}}"#
            )),
        ];
        let slices = slice_merges(&ops);
        assert_eq!(slices, [(1, "b.erp", logos), (2, "c.erp", logos)]);
        let targets = ["a.erp".to_owned(), "b.erp".to_owned()];
        assert_eq!(
            merged_later(&slices, 0, &targets, &path).unwrap(),
            Some(format!("a later erp.merge-slices merges into {logos} in b.erp"))
        );
        assert_eq!(merged_later(&slices, 1, &targets, &path).unwrap(), None);
        assert_eq!(merged_later(&slices, 0, &targets[..1], &path).unwrap(), None);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn runs_the_build_hooks_of_builtin_addons() {
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-hook-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let manifest = AddonManifest::parse(
            r#"{ "schema_version": 1, "id": "t.hook", "name": "Hook", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" },
                 "options": [{ "id": "cars", "type": "slider", "label": "Cars", "min": 22, "max": 30, "step": 2, "default": 22, "binds": "db" }] }"#,
        )
        .unwrap();
        let store = Store::new(root.join("data")).with_builtins(vec![Builtin {
            addon: Arc::new(Hook(manifest)),
            locked: false,
            default_on: true,
        }]);
        let checked = build_as(&store, "default", &game, true).unwrap();
        assert!(
            checked.lines.contains(
                &"t.hook: None Some(Other(\"addons can't be changed while conflicts are checked\"))"
                    .to_owned()
            ),
            "{:?}",
            checked.lines
        );
        let built = build(&store, "default", &game).unwrap();
        assert_eq!(built.order, [CORE_ID, "t.hook"]);
        assert!(built.lines.contains(&"t.hook: profile default".to_owned()));
        assert!(built.lines.contains(&"t.hook: turned on".to_owned()));
        assert!(built.warnings.contains(&"t.hook: 22 cars".to_owned()));
        assert!(
            built
                .warnings
                .contains(&"t.hook: ERP operations skipped; set-text is not built yet".to_owned())
        );
        assert!(store.addon_data_dir("t.hook").is_dir());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn fingerprints_count_what_the_build_made_not_the_rest_of_the_folders() {
        if loader_dir().is_some() {
            return;
        }
        let hook = AddonManifest::parse(
            r#"{ "schema_version": 1, "id": "t.hook", "name": "Hook", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" },
                 "options": [{ "id": "cars", "type": "slider", "label": "Cars", "min": 22, "max": 30, "step": 2, "default": 22, "binds": "db" }] }"#,
        )
        .unwrap();
        let prints = |name: &str, zip: &str, texture: &str| {
            let root = std::env::temp_dir().join(format!("swofty-build-print-{name}-{}", std::process::id()));
            std::fs::remove_dir_all(&root).ok();
            let dir = root.join("game");
            std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
            std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
            std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
            let game = Game {
                identity: Identity::detect(&dir, Some(1)).unwrap(),
                dir,
                user_reg: None,
            };
            let store = Store::new(root.join("data")).with_builtins(vec![Builtin {
                addon: Arc::new(Hook(hook.clone())),
                locked: false,
                default_on: true,
            }]);
            let src = root.join("src");
            std::fs::create_dir_all(&src).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "mod.track", "name": "Track", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }},
                         "operations": {{ "assets": ["ops.json"], "files": [{{ "source": "a.dds", "target": "x/a.dds" }}] }},
                         "origin": {{ "tool": "teamsimplified.mods", "tool_version": "0.11.6", "file": "{zip}", "sha256": "ab" }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(src.join("ops.json"), r#"{ "schema_version": 1, "ops": [] }"#).unwrap();
            std::fs::write(src.join("a.dds"), texture).unwrap();
            std::fs::write(
                src.join("import-report.txt"),
                format!("{zip} -> mod.track (Track)"),
            )
            .unwrap();
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled("mod.track", true))
                .unwrap();
            let history = store.addon_data_dir("t.hook");
            std::fs::create_dir_all(&history).unwrap();
            std::fs::write(
                history.join("importer.json"),
                format!(r#"{{ "mods": {{ "{name}": {{ "made": {{ "id": "mod.{name}" }} }} }} }}"#),
            )
            .unwrap();
            let print = || {
                let p = store.profile("default").unwrap();
                fingerprint::fingerprint(&store, &p, &game.identity).unwrap()
            };
            let before = print();
            build(&store, "default", &game).unwrap();
            let after = print();
            std::fs::remove_dir_all(&root).ok();
            (before, after)
        };
        let (a, b) = (
            prints("a", "track.zip", "dds"),
            prints("b", "track(1).zip", "dds"),
        );
        assert_ne!(a.0.digest, b.0.digest);
        assert_eq!(a.1.digest, b.1.digest, "{:#?}\n{:#?}", a.1.addons, b.1.addons);
        let track = a.1.addons.iter().find(|x| x.id == "mod.track").unwrap();
        assert!(matches!(track.source, fingerprint::Source::Built(_)), "{track:?}");
        let detail = track.detail.as_ref().unwrap();
        assert!(
            detail.parts.contains_key("Assets") && detail.parts.contains_key("declared"),
            "{detail:?}"
        );
        assert_eq!(detail.paths, ["a.dds"]);
        let (_, other) = prints("c", "track.zip", "another texture");
        assert_ne!(other.digest, a.1.digest);
        let found = fingerprint::compare(&other, &a.1, &[]);
        assert!(
            found
                .iter()
                .any(|m| m.differences == ["a.dds differs".to_owned()]),
            "{found:?}"
        );
    }

    struct Rows(AddonManifest);

    impl Addon for Rows {
        fn manifest(&self) -> &AddonManifest {
            &self.0
        }

        fn build(&self, cx: &BuildCx) -> swofty_addon::Result<AssetBatch> {
            let reply: swofty_addon::helper::Built = serde_json::from_str(
                r#"{ "ops": [{ "op": "db.add-row", "table": "entrants.teams",
                               "from": "entrants.teams.team[_metadata_=mclaren_26]",
                               "set": { "_metadata_": "team12_26" } }],
                     "fields": { "entrants.teams.team": ["_metadata_", "stat_id"] } }"#,
            )
            .expect("valid reply");
            let origin = Origin {
                addon: cx.cx.addon.into(),
                item: None,
            };
            Ok(AssetBatch {
                ops: reply
                    .ops
                    .into_iter()
                    .map(|op| Op {
                        op,
                        origin: origin.clone(),
                    })
                    .collect(),
                fields: reply.fields,
                ..AssetBatch::default()
            })
        }
    }

    #[test]
    fn build_reply_fields_name_the_columns_add_row_copies() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-fields-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let mut records = vec![Record::new("entrants.teams._team_instancecount_", 0, "2")];
        for (i, label) in ["mclaren_26", "gm_cadillac_26"].iter().enumerate() {
            let row = format!("entrants.teams.team_{i}");
            records.push(Record::new(&format!("{row}._metadata_"), 4, *label));
            records.push(Record::new(&format!("{row}.stat_id"), 0, (100 + i).to_string()));
        }
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let manifest = AddonManifest::parse(
            r#"{ "schema_version": 1, "id": "t.rows", "name": "Rows", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" } }"#,
        )
        .unwrap();
        let store = Store::new(root.join("data")).with_builtins(vec![Builtin {
            addon: Arc::new(Rows(manifest)),
            locked: false,
            default_on: true,
        }]);
        let built = build(&store, "default", &game).unwrap();
        let db = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(db.instance_count("entrants.teams", "team"), Some(3));
        assert_eq!(
            db.get_text("entrants.teams.team_2._metadata_").as_deref(),
            Some("team12_26")
        );
        assert_eq!(
            db.get_text("entrants.teams.team_2.stat_id").as_deref(),
            Some("102")
        );
        assert!(
            built
                .lines
                .iter()
                .any(|l| l.ends_with("stat_id 102 (the source's is 100)")),
            "{:?}",
            built.lines
        );
        std::fs::remove_dir_all(&root).ok();
    }

    struct Drivers(AddonManifest);

    impl Addon for Drivers {
        fn manifest(&self) -> &AddonManifest {
            &self.0
        }

        fn build(&self, cx: &BuildCx) -> swofty_addon::Result<AssetBatch> {
            let reply: swofty_addon::helper::Built = serde_json::from_str(
                r#"{ "ops": [
                       { "op": "db.add-row", "table": "entrants.drivers",
                         "from": "entrants.drivers.driver[_metadata_=max_verstappen]",
                         "set": { "_metadata_": "dm_208", "stat_id": "208" } },
                       { "op": "db.add-row", "table": "entrants.entrant_lists.entrants",
                         "from": "entrants.entrant_lists.entrants.entrant[_metadata_=max_verstappen]",
                         "set": { "_metadata_": "dm_208", "driver": "dm_208" } }],
                     "fields": { "entrants.drivers.driver": ["_metadata_", "stat_id"],
                                 "entrants.entrant_lists.entrants.entrant": ["_metadata_", "driver"] } }"#,
            )
            .expect("valid reply");
            let origin = Origin {
                addon: cx.cx.addon.into(),
                item: None,
            };
            Ok(AssetBatch {
                ops: reply
                    .ops
                    .into_iter()
                    .map(|op| Op {
                        op,
                        origin: origin.clone(),
                    })
                    .collect(),
                fields: reply.fields,
                ..AssetBatch::default()
            })
        }
    }

    #[test]
    fn warns_when_a_db_editor_addon_picks_a_driver_driver_manager_does_not_add() {
        use crate::dbedit::driver_manager::tests::{database, fixture};
        use crate::dbedit::package::Package;
        use crate::dbedit::{Catalog, module};
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-dm-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let cat = Catalog::from_gvdb(database(), String::new());
        std::fs::write(dir.join(GVDB), database().to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let manifest = AddonManifest::parse(&format!(
            r#"{{ "schema_version": 1, "id": "{}", "name": "Driver Manager", "version": "1.0.0",
                 "author": "t", "compat": {{ "game": "f1_25" }} }}"#,
            driver_manager::ID
        ))
        .unwrap();
        let store = Store::new(root.join("data")).with_builtins(vec![Builtin {
            addon: Arc::new(Drivers(manifest)),
            locked: false,
            default_on: true,
        }]);
        fixture(&store.addon_data_dir(driver_manager::ID));
        let lineups = module("lineups").unwrap();
        let seat = lineups
            .fields
            .iter()
            .position(|f| f.id == "entrant_reference")
            .unwrap();
        let view = lineups.view(&cat, &Default::default(), None).unwrap();
        let mut seats = Package::create(&store, "Seats").unwrap();
        seats
            .edits
            .set(&cat, view.rows[0].cells[seat].key.unwrap(), "dm_208")
            .unwrap();
        let driver = swofty_formats::hash::fnv1a64("entrants.entrant_lists.entrants.entrant_0.driver");
        seats.edits.set(&cat, driver, "dm_209").unwrap();
        seats.save(&store, &cat).unwrap();
        store
            .update_profile("default", |p| p.set_enabled(&seats.id, true))
            .unwrap();

        let tail = "the game would read a driver and entrant its database doesn't have";
        let picks = |built: &Built| -> Vec<String> {
            built
                .warnings
                .iter()
                .filter(|w| w.contains(" picks "))
                .cloned()
                .collect()
        };
        let built = build(&store, "default", &game).unwrap();
        let db = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(
            db.get_text("entrants.entrant_lists.entrant_list_0.entrant_0.entrant_reference")
                .as_deref(),
            Some("dm_208")
        );
        assert_eq!(
            picks(&built),
            [format!(
                "Seats: picks Ret Ired (dm_209), which Driver Manager doesn't add (its page marks the drivers it leaves out); {tail}"
            )]
        );

        store
            .update_profile("default", |p| p.set_enabled(driver_manager::ID, false))
            .unwrap();
        assert_eq!(
            picks(&build(&store, "default", &game).unwrap()),
            [format!(
                "Seats: picks Jacob Swofty (dm_208), Ret Ired (dm_209), Driver Manager drivers, and Driver Manager is off in this profile; {tail}"
            )]
        );
        std::fs::remove_dir_all(&root).ok();
    }

    const PUT: &str = "environment_package/tracks/t/mipmaps/a.mipmaps";

    struct Putter(AddonManifest);

    impl Addon for Putter {
        fn manifest(&self) -> &AddonManifest {
            &self.0
        }

        fn build(&self, cx: &BuildCx) -> swofty_addon::Result<AssetBatch> {
            let mut batch = Rows(self.0.clone()).build(cx)?;
            batch.ops.push(Op {
                op: AssetOp::File(FileOp::Put {
                    source: Source {
                        root: Root::Data,
                        path: "mip".into(),
                    },
                    target: PUT.into(),
                }),
                origin: Origin {
                    addon: cx.cx.addon.into(),
                    item: None,
                },
            });
            Ok(batch)
        }
    }

    #[test]
    fn reuses_an_unchanged_build_until_an_input_or_the_game_folder_changes() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-reuse-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let db = |label: &str| {
            let mut records = vec![Record::new("entrants.teams._team_instancecount_", 0, "1")];
            records.push(Record::new("entrants.teams.team_0._metadata_", 4, "mclaren_26"));
            records.push(Record::new("entrants.teams.team_0.stat_id", 0, label));
            Gvdb { records }.to_bytes()
        };
        std::fs::write(dir.join(GVDB), db("100")).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let manifest = AddonManifest::parse(
            r#"{ "schema_version": 1, "id": "t.put", "name": "Put", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" } }"#,
        )
        .unwrap();
        let store = Store::new(root.join("data")).with_builtins(vec![Builtin {
            addon: Arc::new(Putter(manifest)),
            locked: false,
            default_on: true,
        }]);
        std::fs::create_dir_all(store.addon_data_dir("t.put")).unwrap();
        std::fs::write(store.addon_data_dir("t.put").join("mip"), "mip").unwrap();
        let reused = |b: &Built| b.lines.iter().any(|l| l.starts_with("build: nothing changed"));

        let first = build(&store, "default", &game).unwrap();
        assert!(!reused(&first));
        assert_eq!(first.plan.added[PUT], store.addon_data_dir("t.put").join("mip"));
        let again = build(&store, "default", &game).unwrap();
        assert!(reused(&again), "{:?}", again.lines);
        assert_eq!(again.buildhash, first.buildhash);
        assert_eq!(
            (&again.plan.data, &again.plan.added, &again.plan.loader),
            (&first.plan.data, &first.plan.added, &first.plan.loader)
        );
        assert_eq!(again.settings, first.settings);
        assert_eq!(again.warnings, first.warnings);

        // A file copied in by hand where the plan adds one makes the build
        // decide again, and it is now a game file to replace.
        std::fs::create_dir_all(dir.join(PUT).parent().unwrap()).unwrap();
        std::fs::write(dir.join(PUT), "by hand").unwrap();
        let by_hand = build(&store, "default", &game).unwrap();
        assert!(!reused(&by_hand));
        assert!(by_hand.plan.data.contains_key(PUT) && !by_hand.plan.added.contains_key(PUT));
        std::fs::remove_dir_all(dir.join("environment_package")).unwrap();

        let built = build(&store, "default", &game).unwrap();
        std::fs::write(&built.plan.data[GVDB], b"torn").unwrap();
        assert!(!reused(&build(&store, "default", &game).unwrap()));

        std::fs::write(dir.join(GVDB), db("101")).unwrap();
        let changed = build(&store, "default", &game).unwrap();
        assert!(!reused(&changed));
        assert_ne!(changed.buildhash, first.buildhash);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_cancelled_build_stops_and_the_next_one_starts_fresh() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-cancel-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let records = vec![
            Record::new("entrants.teams._team_instancecount_", 0, "1"),
            Record::new("entrants.teams.team_0._metadata_", 4, "mclaren_26"),
            Record::new("entrants.teams.team_0.stat_id", 0, "100"),
        ];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let manifest = AddonManifest::parse(
            r#"{ "schema_version": 1, "id": "t.put", "name": "Put", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" } }"#,
        )
        .unwrap();
        let store = Store::new(root.join("data")).with_builtins(vec![Builtin {
            addon: Arc::new(Putter(manifest)),
            locked: false,
            default_on: true,
        }]);
        std::fs::create_dir_all(store.addon_data_dir("t.put")).unwrap();
        std::fs::write(store.addon_data_dir("t.put").join("mip"), "mip").unwrap();
        let reused = |b: &Built| b.lines.iter().any(|l| l.starts_with("build: nothing changed"));

        crate::busy::request_cancel(&store.root).unwrap();
        assert!(matches!(build(&store, "default", &game), Err(Error::Cancelled)));
        crate::busy::clear_cancel(&store.root);
        let fresh = build(&store, "default", &game).unwrap();
        assert!(!reused(&fresh), "{:?}", fresh.lines);
        assert!(reused(&build(&store, "default", &game).unwrap()));
        crate::busy::request_cancel(&store.root).unwrap();
        build_as(&store, "default", &game, true).unwrap();
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_reused_build_keeps_its_screens_assets_and_plugins() {
        let root = std::env::temp_dir().join(format!("swofty-build-saved-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let loader = root.join("loader");
        let placed: BTreeMap<String, PathBuf> = [
            (
                "swofty/ui/swofty_settings.ui",
                root.join("overlay/swofty/ui/swofty_settings.ui"),
            ),
            ("swofty/assets.json", root.join("overlay/swofty/assets.json")),
            (
                "swofty/addons/t.x/native/x.dll",
                root.join("addons/t.x/native/x.dll"),
            ),
            (
                "swofty/addons/core/swofty_core.dll",
                loader.join("swofty/addons/core/swofty_core.dll"),
            ),
            ("version.dll", loader.join("version.dll")),
        ]
        .into_iter()
        .map(|(rel, src)| (rel.to_owned(), src))
        .collect();
        for src in placed.values() {
            std::fs::create_dir_all(src.parent().unwrap()).unwrap();
            std::fs::write(src, "x").unwrap();
        }

        let kept = built_files(&placed, Some(&loader));
        assert_eq!(
            kept.keys().map(String::as_str).collect::<Vec<_>>(),
            [
                "swofty/addons/t.x/native/x.dll",
                "swofty/assets.json",
                "swofty/ui/swofty_settings.ui"
            ]
        );
        assert_eq!(built_files(&placed, None), placed);

        let mut built = Built::default();
        built.plan.loader = kept.clone();
        let saved = Saved::of(&built, 0, 0).unwrap();
        let store = Store::new(root.join("data"));
        let game = Game {
            identity: Identity {
                build_id: "1".into(),
                exe_sha256: "a".repeat(64),
            },
            dir: root.join("game"),
            user_reg: None,
        };
        assert!(saved.usable(&store, &game));
        let mut reused = Built::default();
        Saved::of(&built, 0, 0).unwrap().restore(&mut reused).unwrap();
        for rel in kept.keys() {
            assert_eq!(reused.plan.loader.get(rel), kept.get(rel), "{rel}");
        }

        std::fs::remove_file(&kept["swofty/ui/swofty_settings.ui"]).unwrap();
        assert!(!saved.usable(&store, &game));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn applies_db_editor_packages_leniently_in_load_order() {
        use crate::dbedit::Catalog;
        use crate::dbedit::package::Package;
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-dbedit-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let records = vec![
            Record::new("entrants.teams._team_instancecount_", 0, "1"),
            Record::new("entrants.teams.team_0._metadata_", 4, "mclaren_26"),
            Record::new("entrants.teams.team_0.stat_id", 0, "100"),
            Record::new("entrants.teams.team_0.prestige", 0, "5"),
        ];
        let stock = Gvdb { records }.to_bytes();
        std::fs::write(dir.join(GVDB), &stock).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let cat = Catalog::load(&stock).unwrap();
        let key = |p: &str| swofty_formats::hash::fnv1a64(p);
        let mut first = Package::create(&store, "First").unwrap();
        first
            .edits
            .set(&cat, key("entrants.teams.team_0.stat_id"), "7")
            .unwrap();
        first
            .edits
            .set(&cat, key("entrants.teams.team_0.prestige"), "6")
            .unwrap();
        first.save(&store, &cat).unwrap();
        let mut second = Package::create(&store, "Second").unwrap();
        second
            .edits
            .set(&cat, key("entrants.teams.team_0.prestige"), "9")
            .unwrap();
        second.save(&store, &cat).unwrap();
        store
            .update_profile("default", |p| {
                p.set_enabled(&first.id, true)?;
                p.set_enabled(&second.id, true)
            })
            .unwrap();

        let built = build(&store, "default", &game).unwrap();
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(
            out.get_text("entrants.teams.team_0.stat_id").as_deref(),
            Some("7")
        );
        assert_eq!(
            out.get_text("entrants.teams.team_0.prestige").as_deref(),
            Some("9")
        );
        assert!(build(&store, "vanilla", &game).unwrap().lines.is_empty());

        first.edits.edits[0].select = Some("entrants.teams.team[_metadata_=gone_26]".into());
        first
            .edits
            .set(&cat, key("entrants.teams.team_0._metadata_"), "mclaren_27")
            .unwrap();
        first.save(&store, &cat).unwrap();
        let built = build(&store, "default", &game).unwrap();
        assert!(
            built
                .warnings
                .iter()
                .any(|w| w.starts_with("First: change skipped: ") && w.contains("gone_26")),
            "{:?}",
            built.warnings
        );
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(
            out.get_text("entrants.teams.team_0.stat_id").as_deref(),
            Some("100")
        );
        assert_eq!(
            out.get_text("entrants.teams.team_0._metadata_").as_deref(),
            Some("mclaren_27")
        );

        let put = |path: &str, value: &str| serde_json::json!({"op": "put-record", "key": format!("0x{:016x}", key(path)), "tag": 0, "value": value});
        let hidden = serde_json::json!({"schema_version": 1, "ops": [
            put("entrants.teams.team_0.prestige", "77"), put("entrants.teams.team_0.stat_id", "55")]});
        let addon = store.addons_dir().join(&second.id);
        std::fs::write(addon.join("database.json"), hidden.to_string()).unwrap();
        std::fs::write(
            store.addons_dir().join(&first.id).join("dbedit.json"),
            "{\"format\": \"swofty-db-pa",
        )
        .unwrap();
        let built = build(&store, "default", &game).unwrap();
        assert!(
            built
                .warnings
                .iter()
                .any(|w| w.starts_with("First: package skipped: ")),
            "{:?}",
            built.warnings
        );
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        for (path, want) in [
            ("entrants.teams.team_0.prestige", "9"),
            ("entrants.teams.team_0.stat_id", "100"),
            ("entrants.teams.team_0._metadata_", "mclaren_26"),
        ] {
            assert_eq!(out.get_text(path).as_deref(), Some(want), "{path}");
        }
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_row_count_past_the_rows_there_stays_at_the_rows() {
        use crate::dbedit::Catalog;
        use crate::dbedit::package::Package;
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-short-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let list = "schedule.track_lists.track_event_list_0";
        let count = format!("{list}._fixed_event_reference_instancecount_");
        let mut records = vec![
            Record::new("schedule.track_lists._track_event_list_instancecount_", 0, "1"),
            Record::new(&format!("{list}._metadata_"), 2, "F1_2025_16"),
            Record::new(&count, 0, "2"),
        ];
        for (i, e) in ["rw_bahrain", "rw_suzuka"].iter().enumerate() {
            let row = format!("{list}.fixed_event_reference_{i}");
            records.push(Record::new(&format!("{row}._metadata_"), 2, format!("ref_{e}")));
            records.push(Record::new(&format!("{row}.event_name"), 4, *e));
        }
        let stock = Gvdb { records }.to_bytes();
        std::fs::write(dir.join(GVDB), &stock).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let cat = Catalog::load(&stock).unwrap();
        let mut p = Package::create(&store, "2017 v2").unwrap();
        let key = |p: &str| swofty_formats::hash::fnv1a64(p);
        let second = format!("{list}.fixed_event_reference_1.event_name");
        p.edits.set(&cat, key(&count), "4").unwrap();
        p.edits.set(&cat, key(&second), "rw_bahrain").unwrap();
        p.save(&store, &cat).unwrap();
        store
            .update_profile("default", |pr| pr.set_enabled(&p.id, true))
            .unwrap();

        let built = build(&store, "default", &game).unwrap();
        assert!(
            built.warnings.contains(&format!(
                "2017 v2: {list}.fixed_event_reference has 2 rows, not 4; the count stays at 2 (the game crashes on rows that don't exist)"
            )),
            "{:?}",
            built.warnings
        );
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(out.get_text(&count).as_deref(), Some("2"));
        assert_eq!(out.get_text(&second).as_deref(), Some("rw_bahrain"));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn adds_a_driver_head_row_and_its_groups_for_a_head_archive_no_row_names() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-heads-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let rows = "entrants.animation.driver_heads.driver_head";
        let stock_heads = [
            ("lando_norris", "79", "male_driver", "False"),
            ("kimi_raikkonen", "45", "male_driver", "False"),
            ("male_ls222", "198", "male_ea", "True"),
            ("female_ls112", "214", "female_ea", "True"),
        ];
        let mut records = vec![Record::new(
            "entrants.animation.driver_heads._driver_head_instancecount_",
            0,
            stock_heads.len().to_string(),
        )];
        for (i, (label, id, folder, custom)) in stock_heads.iter().enumerate() {
            let female = folder.starts_with("female");
            let model =
                format!("eaid://[CHARACTER_PACKAGE]/heads/{folder}/{label}/idf/{label}_rig_head_morph_v1");
            let (model, female_rig) = match female {
                true => (String::new(), model),
                false => (model, String::new()),
            };
            let body = if female { "generic_female" } else { "generic_male" };
            for (field, tag, value) in [
                ("_metadata_", 2, *label),
                ("asset_group_path_format", 4, "driver_head_format"),
                (
                    "asset_group_path_format_female_on_male_rig",
                    4,
                    "driver_head_format_female_male_rig",
                ),
                ("available_for_customisation", 5, *custom),
                ("body_type", 4, body),
                ("esports_name", 2, ""),
                ("face_morph_asset_group_path_format", 4, "face_morph_format"),
                (
                    "face_morph_asset_group_path_format_female_on_male_rig",
                    4,
                    "face_morph_format_female_male_rig",
                ),
                ("has_morph_data", 5, "True"),
                ("model_path", 2, model.as_str()),
                ("model_path_female_rig", 2, female_rig.as_str()),
                ("stat_id", 0, *id),
            ] {
                records.push(Record::new(&format!("{rows}_{i}.{field}"), tag, value));
            }
        }
        records.extend([
            Record::new("entrants.drivers._driver_instancecount_", 0, "1"),
            Record::new("entrants.drivers.driver_0._metadata_", 2, "lando_norris"),
            Record::new("entrants.drivers.driver_0.head", 4, "lando_norris"),
        ]);
        let stock = Gvdb { records };
        std::fs::write(dir.join(GVDB), stock.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let heads = "2025_asset_groups/animation_package/scenes/cinematics/drivers/heads";
        let morphs = "2025_asset_groups/animation_package/scenes/cinematics/drivers/morph_poses";
        let model_in = |folder: &str, label: &str, rig: &str| {
            archive_of(
                "GfxModelRes",
                &[&format!(
                    "eaid://character_package/heads/{folder}/{label}/idf/{label}_rig_head_morph_v1{rig}.idf?model"
                )],
                1,
            )
        };
        let model = |label: &str| model_in("male_driver", label, "");
        let install = |id: &str, name: &str, files: &[(&str, Vec<u8>)], extra: &str| {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(src.join("files")).unwrap();
            let mut ops: Vec<String> = Vec::new();
            for (i, (target, bytes)) in files.iter().enumerate() {
                std::fs::write(src.join(format!("files/{i}.erp")), bytes).unwrap();
                ops.push(format!(
                    r#"{{"op":"file.put","source":"files/{i}.erp","target":"{target}"}}"#
                ));
            }
            if !extra.is_empty() {
                ops.push(extra.to_owned());
            }
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(r#"{{ "schema_version": 1, "ops": [{}] }}"#, ops.join(",")),
            )
            .unwrap();
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        };
        install(
            "mod.rio",
            "Rio facemod",
            &[
                (&format!("{heads}/rio_haryanto.erp"), model("rio_haryanto")),
                (
                    &format!("{morphs}/rio_haryanto.erp"),
                    archive_of("AnimSet", &["x"], 2),
                ),
                (&format!("{heads}/nomodel.erp"), archive(&["x"], 3)),
                (&format!("{heads}/lando_norris.erp"), model("lando_norris")),
            ],
            "",
        );
        install(
            "mod.amy",
            "Amy facemod",
            &[
                (&format!("{heads}/amy.erp"), model_in("female_driver", "amy", "")),
                (
                    &format!("{heads}/amy_malerig.erp"),
                    model_in("female_driver", "amy", "_malerig"),
                ),
                (&format!("{morphs}/amy.erp"), archive_of("AnimSet", &["x"], 4)),
                (
                    &format!("{morphs}/amy_malerig.erp"),
                    archive_of("AnimSet", &["x"], 5),
                ),
            ],
            "",
        );
        install(
            "t.own",
            "Own rows",
            &[(&format!("{heads}/own_head.erp"), model("own_head"))],
            &format!(
                r#"{{"op":"db.add-row","table":"entrants.animation.driver_heads","from":"{rows}[_metadata_=kimi_raikkonen]","set":{{"_metadata_":"own_head"}}}}"#
            ),
        );
        install(
            "t.pick",
            "Pick",
            &[],
            r#"{"op":"db.set","select":"entrants.drivers.driver[_metadata_=lando_norris]","field":"head","value":"rio_haryanto"}"#,
        );

        let built = build(&store, "default", &game).unwrap();
        let archive_path = format!("{heads}/rio_haryanto.erp");
        for line in [
            format!(
                "Rio facemod: adds driver head rio_haryanto (male, copied from male_ls222) for {archive_path} and registers 2 asset group(s)"
            ),
            format!(
                "Amy facemod: adds driver head amy (female, copied from female_ls112) for {heads}/amy.erp and registers 4 asset group(s)"
            ),
        ] {
            assert!(built.lines.contains(&line), "{line}: {:?}", built.lines);
        }
        assert!(
            built.warnings.contains(&format!(
                "Rio facemod: {heads}/nomodel.erp gets no driver head row: it holds no head model"
            )),
            "{:?}",
            built.warnings
        );
        assert!(
            !built.warnings.iter().any(|w| w.contains("side-data")),
            "{:?}",
            built.warnings
        );
        assert_eq!(
            built
                .lines
                .iter()
                .filter(|l| l.contains("adds driver head"))
                .count(),
            2
        );

        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        let n: usize = out
            .get_text("entrants.animation.driver_heads._driver_head_instancecount_")
            .unwrap()
            .parse()
            .unwrap();
        assert_eq!(n, 7);
        let field_of = |label: &str, f: &str| {
            let row = (0..n)
                .find(|i| out.get_text(&format!("{rows}_{i}._metadata_")).as_deref() == Some(label))
                .unwrap_or_else(|| panic!("a {label} row"));
            out.get_text(&format!("{rows}_{row}.{f}")).unwrap_or_default()
        };
        let field = |f: &str| field_of("rio_haryanto", f);
        assert_eq!(
            field("model_path"),
            "eaid://[CHARACTER_PACKAGE]/heads/male_driver/rio_haryanto/idf/rio_haryanto_rig_head_morph_v1"
        );
        assert_eq!(field("model_path_female_rig"), "");
        assert_eq!(field("has_morph_data"), "True");
        assert_eq!(field("body_type"), "generic_male");
        assert_eq!(field("available_for_customisation"), "False");
        assert_eq!(
            field_of("amy", "model_path"),
            "eaid://[CHARACTER_PACKAGE]/heads/female_driver/amy/idf/amy_rig_head_morph_v1_malerig"
        );
        assert_eq!(
            field_of("amy", "model_path_female_rig"),
            "eaid://[CHARACTER_PACKAGE]/heads/female_driver/amy/idf/amy_rig_head_morph_v1"
        );
        assert_eq!(field_of("amy", "body_type"), "generic_female");
        let ids: BTreeSet<String> = (0..n)
            .filter_map(|i| out.get_text(&format!("{rows}_{i}.stat_id")))
            .collect();
        assert_eq!(ids.len(), n, "every head keeps its own id: {ids:?}");

        let groups = |addon: &str| -> Vec<(String, Option<String>, Option<String>)> {
            built
                .waiting
                .iter()
                .filter_map(|op| match &op.op {
                    AssetOp::Group(GroupOp::Register {
                        group, erp, template, ..
                    }) if op.origin.addon == addon => Some((group.clone(), erp.clone(), template.clone())),
                    _ => None,
                })
                .collect()
        };
        let g = "asset_groups/animation_package/scenes/cinematics/drivers";
        assert_eq!(
            groups("mod.rio"),
            [
                (
                    format!("{g}/heads/rio_haryanto.assetgroup"),
                    Some(archive_path.clone()),
                    Some(format!("{g}/heads/lando_norris.assetgroup"))
                ),
                (
                    format!("{g}/morph_poses/rio_haryanto.assetgroup"),
                    Some(format!("{morphs}/rio_haryanto.erp")),
                    Some(format!("{g}/morph_poses/lando_norris.assetgroup"))
                ),
            ]
        );
        let amy: Vec<String> = groups("mod.amy")
            .into_iter()
            .map(|(group, _, template)| format!("{group} < {}", template.unwrap_or_default()))
            .collect();
        assert_eq!(
            amy,
            [
                format!("{g}/heads/amy.assetgroup < {g}/heads/female_ls493.assetgroup"),
                format!("{g}/heads/amy_malerig.assetgroup < {g}/heads/female_ls493_malerig.assetgroup"),
                format!("{g}/morph_poses/amy.assetgroup < {g}/morph_poses/female_ls493.assetgroup"),
                format!(
                    "{g}/morph_poses/amy_malerig.assetgroup < {g}/morph_poses/female_ls493_malerig.assetgroup"
                ),
            ]
        );

        let listed = crate::heads::for_profile(&store, &game, "default").unwrap().heads;
        let labels: Vec<&str> = listed.iter().map(|h| h.label.as_str()).collect();
        assert_eq!(labels, ["amy", "lando_norris", "own_head", "rio_haryanto"]);
        let rio = listed.iter().find(|h| h.label == "rio_haryanto").unwrap();
        assert_eq!(rio.addon.as_deref(), Some("mod.rio"));
        assert_eq!(rio.addon_name.as_deref(), Some("Rio facemod"));
        assert!(!rio.female);
        assert!(listed.iter().find(|h| h.label == "amy").unwrap().female);
        assert_eq!(crate::heads::installed(&store).len(), 4);
        store
            .update_profile("default", |p| p.set_enabled("mod.rio", false))
            .unwrap();
        let listed = crate::heads::for_profile(&store, &game, "default").unwrap().heads;
        assert_eq!(
            listed.iter().map(|h| h.label.as_str()).collect::<Vec<_>>(),
            ["amy", "own_head"]
        );

        let lando = "entrants.drivers.driver_0.head";
        assert_eq!(out.get_text(lando).as_deref(), Some("rio_haryanto"));
        assert!(!built.warnings.iter().any(|w| w.contains("wears head")));
        let built = build(&store, "default", &game).unwrap();
        assert!(
            built.warnings.contains(
                &"Pick: lando_norris wears head rio_haryanto, which no addon on in this profile adds, so it keeps the game's head lando_norris"
                    .to_owned()
            ),
            "{:?}",
            built.warnings
        );
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(out.get_text(lando).as_deref(), Some("lando_norris"));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn skips_removals_of_stock_records_so_runtime_edits_still_apply() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-stockremove-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let mut records = vec![
            Record::new("entrants.teams._team_instancecount_", 0, "1"),
            Record::new("entrants.teams.team_0._metadata_", 4, "mclaren_26"),
            Record::new("entrants.teams.team_0.stat_id", 0, "100"),
        ];
        records.extend((0..5).map(|i| Record::new(&format!("stock.key_{i}"), 0, i.to_string())));
        let stock = Gvdb { records };
        std::fs::write(dir.join(GVDB), stock.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let install = |id: &str, name: &str, ops: &str| {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(&src).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(r#"{{ "schema_version": 1, "ops": [{ops}] }}"#),
            )
            .unwrap();
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        };
        let put = |key: &str, value: &str| {
            format!(r#"{{"op":"db.put-record","key":"{key}","type":0,"value":"{value}"}},"#)
        };
        let remove = |key: &str| format!(r#"{{"op":"db.remove-record","key":"{key}"}}"#);
        let mut ops = put("entrants.teams.team_0.stat_id", "3135");
        for i in 0..3 {
            ops += &put(&format!("mod.key_{i}"), "3130");
        }
        for i in 0..5 {
            ops += &remove(&format!("stock.key_{i}"));
            ops += ",";
        }
        ops += &remove("entrants.teams.team_0.stat_id");
        ops += ",";
        ops += &remove("mod.key_0");
        install("t.peugeot", "Peugeot TotalEnergies F1 Team", &ops);

        let built = build(&store, "default", &game).unwrap();
        let line = "Peugeot TotalEnergies F1 Team: 6 removal(s) of stock database records skipped; \
                    the game crashes when a record it reads is missing";
        assert_eq!(
            built
                .warnings
                .iter()
                .filter(|w| w.contains("stock database records"))
                .collect::<Vec<_>>(),
            [line],
            "{:?}",
            built.warnings
        );
        assert_eq!(
            built.report.stock_removals,
            BTreeMap::from([("t.peugeot".to_owned(), 6)])
        );
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        for r in &stock.records {
            assert!(out.get_key(r.key).is_some(), "{:#018x}", r.key);
        }
        assert_eq!(
            out.get_text("entrants.teams.team_0.stat_id").as_deref(),
            Some("15")
        );
        assert!(out.get("mod.key_0").is_none());
        assert_eq!(out.get_text("mod.key_1").as_deref(), Some("10"));
        assert_eq!(db_edits(&stock, &out).map(|e| e.len()), Ok(3));

        install(
            "t.author",
            "Author",
            &format!(
                r#"{}, {{"op":"db.remove-record","key":"stock.key_4","allow_stock_removal":true}}"#,
                remove("mod.key_1")
            ),
        );
        let built = build(&store, "default", &game).unwrap();
        assert_eq!(
            built.report.stock_removals,
            BTreeMap::from([("t.peugeot".to_owned(), 6)])
        );
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert!(out.get("mod.key_1").is_none());
        assert!(out.get("stock.key_4").is_none());
        assert!(out.get("stock.key_3").is_some());
        assert!(db_edits(&stock, &out).is_err());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn keeps_the_entrant_lists_an_addon_renames_or_removes() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-keeplists-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let lists = "entrants.entrant_lists";
        let whitelist = "content_sets.content_set_0.available_entrant_lists_0.entrant_lists";
        let mut records = vec![
            Record::new(&format!("{lists}._entrant_list_instancecount_"), 0, "3"),
            Record::new("content_sets._content_set_instancecount_", 0, "1"),
            Record::new(
                "content_sets.content_set_0._available_entrant_lists_instancecount_",
                0,
                "1",
            ),
            Record::new(whitelist, 13, "f1_2026,f2_2026,f2_2025"),
        ];
        for (i, (name, id)) in [("f1_2026", "67"), ("f2_2026", "104"), ("f2_2025", "95")]
            .into_iter()
            .enumerate()
        {
            records.push(Record::new(
                &format!("{lists}.entrant_list_{i}._metadata_"),
                4,
                name,
            ));
            records.push(Record::new(&format!("{lists}.entrant_list_{i}.stat_id"), 0, id));
            records.push(Record::new(
                &format!("{lists}.entrant_list_{i}.display_name"),
                4,
                format!("lng_{name}"),
            ));
        }
        let stock = Gvdb { records };
        std::fs::write(dir.join(GVDB), stock.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let src = root.join("src").join("t.old");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(
            src.join("swofty.json"),
            r#"{ "schema_version": 1, "id": "t.old", "name": "Old Team Pack", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" }, "operations": { "assets": ["ops.json"] } }"#,
        )
        .unwrap();
        let put = |key: &str, value: &str| {
            format!(
                r#"{{"op":"db.put-record","key":"{key}","type":0,"value":"{}"}}"#,
                hex(value.as_bytes())
            )
        };
        let ops = [
            put(&format!("{lists}._entrant_list_instancecount_"), "2"),
            put(&format!("{lists}.entrant_list_1._metadata_"), "f2_2025"),
            put(&format!("{lists}.entrant_list_1.stat_id"), "95"),
            put(whitelist, "f1_2026,f2_2025"),
        ]
        .join(",");
        std::fs::write(
            src.join("ops.json"),
            format!(r#"{{ "schema_version": 1, "ops": [{ops}] }}"#),
        )
        .unwrap();
        store.install_addon(&src).unwrap();
        store
            .update_profile("default", |p| p.set_enabled("t.old", true))
            .unwrap();

        let built = build(&store, "default", &game).unwrap();
        let line = "Old Team Pack: kept the entrant list(s) f2_2026 that it renames or removes; \
                    the game crashes when a list it looks up by name is missing";
        assert!(built.warnings.iter().any(|w| w == line), "{:?}", built.warnings);
        assert_eq!(
            built.report.kept_lists,
            BTreeMap::from([("t.old".to_owned(), vec!["f2_2026".to_owned()])])
        );
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(
            out.get_text(&format!("{lists}._entrant_list_instancecount_"))
                .as_deref(),
            Some("4")
        );
        assert_eq!(
            out.get_text(&format!("{lists}.entrant_list_1._metadata_"))
                .as_deref(),
            Some("f2_2025")
        );
        for (field, value) in [
            ("_metadata_", "f2_2026"),
            ("stat_id", "104"),
            ("display_name", "lng_f2_2026"),
        ] {
            assert_eq!(
                out.get_text(&format!("{lists}.entrant_list_3.{field}"))
                    .as_deref(),
                Some(value)
            );
        }
        assert_eq!(
            out.get_text(whitelist).as_deref(),
            Some("f1_2026,f2_2025,f2_2026")
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn skips_removals_of_the_records_a_track_adds() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-trackremove-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let slot = dir.join(slot_file(SLOTS[0]));
        std::fs::create_dir_all(slot.parent().unwrap()).unwrap();
        std::fs::write(&slot, archive(&["eaid://ui_package/slot.uil"], 0)).unwrap();
        let res = "tracks.track_resource.TrackResource_0";
        let set = "track_data.game_setup.TrackDataSet_0";
        let records = vec![
            Record::new("tracks._Track_instancecount_", 0, "1"),
            Record::new("tracks.Track_0._metadata_", 2, "melbourne"),
            Record::new("tracks.Track_0.statId", 0, "5"),
            Record::new("tracks.Track_0.track_resource", 2, "2019"),
            Record::new("tracks.track_resource._TrackResource_instancecount_", 0, "1"),
            Record::new(&format!("{res}._metadata_"), 2, "2019"),
            Record::new(&format!("{res}.iblAssetGroupsRoot"), 2, "probes/"),
            Record::new(&format!("{res}.iblKey"), 2, "key_2019.assetgroup"),
            Record::new("track_data.game_setup._TrackDataSet_instancecount_", 0, "1"),
            Record::new(&format!("{set}._metadata_"), 2, "f1"),
            Record::new(&format!("{set}._TrackData_instancecount_"), 0, "1"),
            Record::new(&format!("{set}.TrackData_0._metadata_"), 2, "melbourne"),
            Record::new(&format!("{set}.TrackData_0.track"), 2, "melbourne"),
            Record::new("stock.key_0", 0, "0"),
        ];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let install = |id: &str, name: &str, ops: &str, track: bool| {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(src.join("track")).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(r#"{{ "schema_version": 1, "ops": [{ops}] }}"#),
            )
            .unwrap();
            if track {
                std::fs::write(
                    src.join("track/manifest.json"),
                    r#"{ "track_code": "testring", "track_name": "Test Ring", "erp": "testring.erp",
                         "activation": { "copy_from_track": "melbourne", "copy_slot": "melbourne" } }"#,
                )
                .unwrap();
                let world = "eaid://environment_package/tracks/testring/wep/testring_common.wep";
                std::fs::write(src.join("track/testring.erp"), archive_of("WEP", &[world], 0)).unwrap();
            }
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        };
        let track_keys = [
            "tracks.Track_1._metadata_",
            "tracks.Track_1.statId",
            "tracks.track_resource.TrackResource_1.iblKey",
            &format!("{set}.TrackData_1.track"),
        ];
        let remove =
            |key: &str| format!(r#"{{"op":"db.remove-record","key":"{key}","allow_stock_removal":true}},"#);
        let mut ops: String = track_keys.iter().map(|k| remove(k)).collect();
        ops += &remove("stock.key_0");
        ops += r#"{"op":"db.put-record","key":"mod.key_0","type":0,"value":"3130"},"#;
        ops += r#"{"op":"db.remove-record","key":"mod.key_0"}"#;
        install("t.orlen", "Scuderia AlphaTauri ORLEN", &ops, false);
        install(
            "mod.testring",
            "Test Ring",
            r#"{"op":"track.add","source":"track"}"#,
            true,
        );

        let built = build(&store, "default", &game).unwrap();
        let cloned = "testring: testring: new track row cloned from melbourne".to_owned();
        assert!(built.lines.contains(&cloned), "{:?}", built.lines);
        let line = "Scuderia AlphaTauri ORLEN: 4 removal(s) of database records the tracks add skipped; \
                    the game crashes when a record it reads is missing";
        assert_eq!(
            built
                .warnings
                .iter()
                .filter(|w| w.contains("removal(s)"))
                .collect::<Vec<_>>(),
            [line],
            "{:?}",
            built.warnings
        );
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        for key in track_keys {
            assert!(out.get(key).is_some(), "{key}");
        }
        assert_eq!(out.get_text("tracks._Track_instancecount_").as_deref(), Some("2"));
        assert!(out.get("stock.key_0").is_none());
        assert!(out.get("mod.key_0").is_none());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn keeps_the_track_changes_a_whole_database_mod_carries_back() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-trackkeep-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let slot = dir.join(slot_file(SLOTS[0]));
        std::fs::create_dir_all(slot.parent().unwrap()).unwrap();
        std::fs::write(&slot, archive(&["eaid://ui_package/slot.uil"], 0)).unwrap();
        let res = "tracks.track_resource.TrackResource_0";
        let set = "track_data.game_setup.TrackDataSet_0";
        let list = "content_sets.content_set_0.available_tracks_0.tracks";
        let records = vec![
            Record::new("tracks._Track_instancecount_", 0, "1"),
            Record::new("tracks.Track_0._metadata_", 2, "melbourne"),
            Record::new("tracks.Track_0.statId", 0, "5"),
            Record::new("tracks.Track_0.track_resource", 2, "2019"),
            Record::new("tracks.track_resource._TrackResource_instancecount_", 0, "1"),
            Record::new(&format!("{res}._metadata_"), 2, "2019"),
            Record::new(&format!("{res}.iblAssetGroupsRoot"), 2, "probes/"),
            Record::new(&format!("{res}.iblKey"), 2, "key_2019.assetgroup"),
            Record::new("track_data.game_setup._TrackDataSet_instancecount_", 0, "1"),
            Record::new(&format!("{set}._metadata_"), 2, "f1"),
            Record::new(&format!("{set}._TrackData_instancecount_"), 0, "1"),
            Record::new(&format!("{set}.TrackData_0._metadata_"), 2, "melbourne"),
            Record::new(&format!("{set}.TrackData_0.track"), 2, "melbourne"),
            Record::new("content_sets._content_set_instancecount_", 0, "1"),
            Record::new("content_sets.content_set_0._metadata_", 2, "2025"),
            Record::new(
                "content_sets.content_set_0._available_tracks_instancecount_",
                0,
                "1",
            ),
            Record::new(list, 13, "melbourne"),
            Record::new("stock.key_0", 0, "0"),
        ];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let install = |id: &str, name: &str, ops: &str, track: bool| {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(src.join("track")).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(r#"{{ "schema_version": 1, "ops": [{ops}] }}"#),
            )
            .unwrap();
            if track {
                std::fs::write(
                    src.join("track/manifest.json"),
                    r#"{ "track_code": "testring", "track_name": "Test Ring", "erp": "testring.erp",
                         "activation": { "copy_from_track": "melbourne", "copy_slot": "melbourne" } }"#,
                )
                .unwrap();
                let world = "eaid://environment_package/tracks/testring/wep/testring_common.wep";
                std::fs::write(src.join("track/testring.erp"), archive_of("WEP", &[world], 0)).unwrap();
            }
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        };
        install(
            "mod.testring",
            "Test Ring",
            r#"{"op":"track.add","source":"track"}"#,
            true,
        );
        let put = |key: &str, ty: u8, value: &str| {
            let hex: String = value.bytes().map(|b| format!("{b:02x}")).collect();
            format!(r#"{{"op":"db.put-record","key":"{key}","type":{ty},"value":"{hex}"}}"#)
        };
        let ops = [
            put(list, 13, "melbourne"),
            put("tracks._Track_instancecount_", 0, "1"),
            put("stock.key_0", 0, "7"),
        ]
        .join(",");
        install("t.orlen", "Scuderia AlphaTauri ORLEN", &ops, false);

        let built = build(&store, "default", &game).unwrap();
        let line =
            "Scuderia AlphaTauri ORLEN: kept the tracks' changes to 2 database value(s) it sets back to the \
                    game's own"
                .to_owned();
        assert!(built.lines.contains(&line), "{:?}", built.lines);
        let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(out.get_text(list).as_deref(), Some("melbourne,testring"));
        assert_eq!(out.get_text("tracks._Track_instancecount_").as_deref(), Some("2"));
        assert_eq!(out.get_text("stock.key_0").as_deref(), Some("7"));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn track_ers_settings_survive_an_addon_that_writes_the_handling_file() {
        use swofty_formats::bxml::{Document, Element, Kind};
        use swofty_formats::erp::{Erp, ErpFile, Fragment, HEADER_LEN, MAGIC, Resource, STORED};
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-ers-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let slot = dir.join(slot_file(SLOTS[0]));
        std::fs::create_dir_all(slot.parent().unwrap()).unwrap();
        std::fs::write(&slot, archive(&["eaid://ui_package/slot.uil"], 0)).unwrap();
        let res = "tracks.track_resource.TrackResource_0";
        let set = "track_data.game_setup.TrackDataSet_0";
        let records = vec![
            Record::new("tracks._Track_instancecount_", 0, "1"),
            Record::new("tracks.Track_0._metadata_", 2, "melbourne"),
            Record::new("tracks.Track_0.statId", 0, "5"),
            Record::new("tracks.Track_0.track_resource", 2, "2019"),
            Record::new("tracks.track_resource._TrackResource_instancecount_", 0, "1"),
            Record::new(&format!("{res}._metadata_"), 2, "2019"),
            Record::new(&format!("{res}.iblAssetGroupsRoot"), 2, "probes/"),
            Record::new(&format!("{res}.iblKey"), 2, "key_2019.assetgroup"),
            Record::new("track_data.game_setup._TrackDataSet_instancecount_", 0, "1"),
            Record::new(&format!("{set}._metadata_"), 2, "f1"),
            Record::new(&format!("{set}._TrackData_instancecount_"), 0, "1"),
            Record::new(&format!("{set}.TrackData_0._metadata_"), 2, "melbourne"),
            Record::new(&format!("{set}.TrackData_0.track"), 2, "melbourne"),
        ];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let el = |name: &str, attrs: &[(&str, &str)], children: Vec<Element>| Element {
            kind: Kind::Element,
            pad: 0,
            name: name.as_bytes().to_vec(),
            attrs: attrs
                .iter()
                .map(|(k, v)| (k.as_bytes().to_vec(), v.as_bytes().to_vec()))
                .collect(),
            children,
            end_pad: 0,
        };
        let uri = "eaid://f1_2026_vehicle_package/teams/mclaren/handling/mclaren.vtf";
        let handling = |deploy: &str| {
            let doc = Document {
                root: el(
                    "VTF",
                    &[],
                    vec![el(
                        "EnergyRecoverySystem",
                        &[("numTrackSettingsTypes", "1")],
                        vec![el(
                            "TrackSettings",
                            &[("name", "0")],
                            vec![
                                el("Track", &[("value", "melbourne")], Vec::new()),
                                el("MediumDeployFactor", &[("value", deploy)], Vec::new()),
                                el("OverrideDetectionLapProgress", &[("value", "0.8")], Vec::new()),
                                el("OverrideActivationLapProgress", &[("value", "0.85")], Vec::new()),
                            ],
                        )],
                    )],
                ),
                tail: Vec::new(),
            };
            let data = doc.to_bytes();
            let mut header = [0u8; HEADER_LEN];
            header[..4].copy_from_slice(MAGIC);
            header[4..8].copy_from_slice(&3u32.to_le_bytes());
            Erp {
                header,
                version: 3,
                resources: vec![Resource {
                    name: uri.into(),
                    kind: swofty_formats::erp::kind("VTF").unwrap(),
                    unk: vec![0; 4],
                    fragments: vec![Fragment {
                        name: *b"temp",
                        offset: 0,
                        size: data.len() as u64,
                        flags: 0x10,
                        compression: STORED,
                        packed: data.len() as u64,
                        raw: data,
                    }],
                    hash: [0; 16],
                }],
            }
            .to_bytes()
        };
        let team = "2025_asset_groups/f1_2026_vehicle_package/teams/mclaren/wep/mclaren.erp";
        std::fs::create_dir_all(dir.join(team).parent().unwrap()).unwrap();
        std::fs::write(dir.join(team), handling("0.8")).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let install = |id: &str, ops: &str, files: &[(&str, Vec<u8>)]| {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(src.join("track")).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{id}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(r#"{{ "schema_version": 1, "ops": [{ops}] }}"#),
            )
            .unwrap();
            for (rel, data) in files {
                std::fs::write(src.join(rel), data).unwrap();
            }
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        };
        let world = "eaid://environment_package/tracks/testring/wep/testring_common.wep";
        install(
            "mod.testring",
            r#"{"op":"track.add","source":"track"}"#,
            &[
                (
                    "track/manifest.json",
                    br#"{ "track_code": "testring", "track_name": "Test Ring", "erp": "testring.erp",
                         "activation": { "copy_from_track": "melbourne", "copy_slot": "melbourne" },
                         "ers": { "OverrideDetectionLapProgress": 0.91, "OverrideActivationLapProgress": 0.95 } }"#
                        .to_vec(),
                ),
                ("track/testring.erp", archive_of("WEP", &[world], 0)),
            ],
        );
        install(
            "t.handling",
            &format!(r#"{{"op":"erp.put-resource","erp":"{team}","resource":"{uri}","source":"h.erp"}}"#),
            &[("h.erp", handling("0.5"))],
        );
        let season = "2025_asset_groups/cs_2019_vehicle_package/teams/mclaren/wep/mclaren.erp";
        install(
            "t.season",
            &format!(r#"{{"op":"file.put","source":"cs.erp","target":"{season}"}}"#),
            &[("cs.erp", handling("0.6"))],
        );

        let built = build(&store, "default", &game).unwrap();
        for line in [
            "ers: 1 handling file(s) other addons change get the tracks' settings too",
            "ers: 1 handling file(s) in car archives addons put get the tracks' settings",
        ] {
            assert!(built.lines.contains(&line.to_owned()), "{:?}", built.lines);
        }
        let value = |archive: &Path, track: &str, key: &str| {
            let file = ErpFile::open(archive).unwrap();
            let r = file.erp.resources.iter().find(|r| r.name == uri).unwrap();
            let doc = Document::parse(&file.load(r).unwrap().fragments[0].data().unwrap()).unwrap();
            doc.root
                .child(b"EnergyRecoverySystem")
                .unwrap()
                .children
                .iter()
                .find(|c| c.child(b"Track").and_then(|t| t.attr(b"value")) == Some(track.as_bytes()))
                .and_then(|c| c.child(key.as_bytes()))
                .and_then(|f| f.attr(b"value"))
                .map(|v| String::from_utf8_lossy(v).into_owned())
        };
        let car = &built.plan.data[team];
        assert_eq!(
            value(car, "melbourne", "MediumDeployFactor").as_deref(),
            Some("0.5")
        );
        assert_eq!(
            value(car, "testring", "MediumDeployFactor").as_deref(),
            Some("0.5")
        );
        assert_eq!(
            value(car, "testring", "OverrideDetectionLapProgress").as_deref(),
            Some("0.91")
        );
        assert_eq!(
            value(car, "testring", "OverrideActivationLapProgress").as_deref(),
            Some("0.95")
        );
        let car = &built.plan.added[season];
        assert_eq!(
            value(car, "melbourne", "MediumDeployFactor").as_deref(),
            Some("0.6")
        );
        assert_eq!(
            value(car, "testring", "OverrideActivationLapProgress").as_deref(),
            Some("0.95")
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn the_tracks_keep_their_menu_pictures_over_an_addon_that_ships_its_own() {
        use swofty_formats::erp::{Erp, ErpFile, Fragment, HEADER_LEN, MAGIC, Resource, STORED};
        use swofty_formats::gvdb::Record;
        use swofty_formats::pkg::{Arr, Node, Obj, Pkg, Val};
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-menus-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let write = |rel: &str, data: Vec<u8>| {
            let p = dir.join(rel);
            std::fs::create_dir_all(p.parent().unwrap()).unwrap();
            std::fs::write(p, data).unwrap();
        };
        for slot in [SLOTS[0], SLOTS[1], crate::activation::ui::PICTURE_SLOT] {
            write(&slot_file(slot), archive(&["eaid://ui_package/slot.uil"], 0));
        }
        let res = "tracks.track_resource.TrackResource_0";
        let set = "track_data.game_setup.TrackDataSet_0";
        let records = vec![
            Record::new("tracks._Track_instancecount_", 0, "1"),
            Record::new("tracks.Track_0._metadata_", 2, "melbourne"),
            Record::new("tracks.Track_0.statId", 0, "5"),
            Record::new("tracks.Track_0.track_resource", 2, "2019"),
            Record::new("tracks.track_resource._TrackResource_instancecount_", 0, "1"),
            Record::new(&format!("{res}._metadata_"), 2, "2019"),
            Record::new(&format!("{res}.iblAssetGroupsRoot"), 2, "probes/"),
            Record::new(&format!("{res}.iblKey"), 2, "key_2019.assetgroup"),
            Record::new("track_data.game_setup._TrackDataSet_instancecount_", 0, "1"),
            Record::new(&format!("{set}._metadata_"), 2, "f1"),
            Record::new(&format!("{set}._TrackData_instancecount_"), 0, "1"),
            Record::new(&format!("{set}.TrackData_0._metadata_"), 2, "melbourne"),
            Record::new(&format!("{set}.TrackData_0.track"), 2, "melbourne"),
        ];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();

        let resource = |name: &str, kind: &str, fragments: Vec<Vec<u8>>| {
            let mut r = Resource {
                name: name.into(),
                kind: swofty_formats::erp::kind(kind).unwrap(),
                unk: vec![0; 4],
                fragments: fragments
                    .into_iter()
                    .map(|data| Fragment {
                        name: *b"temp",
                        offset: 0,
                        size: data.len() as u64,
                        flags: 0x10,
                        compression: STORED,
                        packed: data.len() as u64,
                        raw: data,
                    })
                    .collect(),
                hash: [0; 16],
            };
            r.hash = r.content_hash().unwrap();
            r
        };
        let surface = |name: &str, slices: &[u8]| {
            let mut head = vec![0u8; 40];
            head[12..16].copy_from_slice(&2u32.to_le_bytes());
            head[16..20].copy_from_slice(&2u32.to_le_bytes());
            head[28..32].copy_from_slice(&(slices.len() as u32).to_le_bytes());
            let data = slices.iter().flat_map(|b| [*b; 4]).collect();
            resource(&format!("{name}.image"), "GfxSurfaceRes", vec![head, data])
        };
        let view = |name: &str, slices: u32| {
            let mut v = vec![0u8; 0x18];
            v[0x14..].copy_from_slice(&slices.to_le_bytes());
            resource(name, "GfxSurfaceRes", vec![v])
        };
        let erp_of = |resources: Vec<Resource>| {
            let mut header = [0u8; HEADER_LEN];
            header[..4].copy_from_slice(MAGIC);
            header[4..8].copy_from_slice(&3u32.to_le_bytes());
            Erp {
                header,
                version: 3,
                resources,
            }
            .to_bytes()
        };
        let obj = |pairs: Vec<(&str, Node)>| {
            Node::Obj(Obj::new(
                pairs.into_iter().map(|(k, v)| (Val::str(k), v)).collect(),
            ))
        };
        let list = |names: &[&str]| {
            let items = names
                .iter()
                .map(|n| {
                    obj(vec![
                        ("Name", Node::Val(Val::str(*n))),
                        ("Value", Node::Val(Val::str("ui_package/stock.tif"))),
                    ])
                })
                .collect();
            obj(vec![("Contents", Node::Arr(Arr::new(items)))])
        };
        let in_contents = |key: &str, inner: Node| obj(vec![(key, obj(vec![("Contents", inner)]))]);
        let pkg = |name: &str, root: Node| {
            let data = Pkg {
                name: *b"UIDB",
                root,
                vca: Vec::new(),
                layout: None,
            }
            .to_bytes()
            .unwrap();
            resource(name, "UIDB", vec![data])
        };
        const OVERRIDES: &str = "eaid://ui_package/databases/overrides.uidb";
        const MINIMAP: &str = "eaid://ui_package/databases/minimap.uidb";
        const UIDTA: &str = "eaid://ui_package/art/texture_arrays/ui_dynamic_texture_loading.uidta";
        const FLAGS: &str =
            "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.tracks.flags_circle.texarray";
        const TRACKS: &str =
            "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.fe_world.tracks.texarray";
        const LOGO: &str = "eaid://ui_package/art/textures/team_logo.tif";
        let stock = ["bahrain", "blank", "melbourne"];
        let overrides = obj(vec![(
            "overrides",
            obj(vec![(
                "Contents",
                obj(vec![
                    (
                        "tracks",
                        obj(vec![("Contents", obj(vec![("flags_circle", list(&stock))]))]),
                    ),
                    (
                        "fe_world",
                        obj(vec![("Contents", obj(vec![("tracks", list(&stock))]))]),
                    ),
                ]),
            )]),
        )]);
        let entry = obj(vec![("Name", Node::Val(Val::str("bahrain")))]);
        let minimap = in_contents(
            "hud",
            in_contents("minimap", in_contents("tracks", Node::Arr(Arr::new(vec![entry])))),
        );
        let ui = |rel: &str| format!("2025_asset_groups/ui_package/{rel}");
        let (front, hub) = (ui("common_front_end.erp"), ui("flow_career_hub.erp"));
        for rel in crate::activation::ui::UI_ERPS {
            let resources = match *rel {
                "databases.erp" => vec![pkg(OVERRIDES, overrides.clone()), pkg(MINIMAP, minimap.clone())],
                "flow_race.erp" => vec![pkg(MINIMAP, minimap.clone())],
                "persistent.erp" => vec![pkg(
                    UIDTA,
                    obj(vec![("TextureArrays", Node::Arr(Arr::new(Vec::new())))]),
                )],
                "common_front_end.erp" => vec![
                    view(FLAGS, 3),
                    surface(FLAGS, &[0, 1, 2]),
                    view(TRACKS, 3),
                    surface(TRACKS, &[0, 1, 2]),
                    surface(LOGO, &[0]),
                ],
                "flow_career_hub.erp" => vec![view(FLAGS, 3), surface(FLAGS, &[0, 1, 2])],
                _ => vec![view("eaid://ui_package/other.tif", 1)],
            };
            write(&ui(rel), erp_of(resources));
        }
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let install = |id: &str, ops: &str, files: Vec<(&str, Vec<u8>)>| {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(src.join("track")).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{id}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(r#"{{ "schema_version": 1, "ops": [{ops}] }}"#),
            )
            .unwrap();
            for (rel, data) in files {
                std::fs::write(src.join(rel), data).unwrap();
            }
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        };
        for (code, pixel) in [("ring_a", 0xa0u8), ("ring_b", 0xb0)] {
            let picture =
                |key: &str| format!("eaid://ui_package/art/textures/track_installer/{code}/{key}.tif");
            let images = serde_json::json!({ "resources": {
                "flag_circle": picture("flag_circle"),
                "preview_small": picture("preview_small"),
            } });
            let manifest = serde_json::json!({
                "track_code": code,
                "track_name": code,
                "erp": format!("{code}.erp"),
                "activation": { "copy_from_track": "melbourne", "copy_slot": "melbourne" },
                "ui": { "images_file": "images.json" },
            });
            let world = format!("eaid://environment_package/tracks/{code}/wep/{code}_common.wep");
            let archive = erp_of(vec![
                resource(&world, "WEP", vec![vec![0]]),
                view(&picture("flag_circle"), 1),
                surface(&picture("flag_circle"), &[pixel]),
                view(&picture("preview_small"), 1),
                surface(&picture("preview_small"), &[pixel + 1]),
            ]);
            install(
                &format!("mod.{code}"),
                r#"{"op":"track.add","source":"track"}"#,
                vec![
                    ("track/manifest.json", manifest.to_string().into_bytes()),
                    ("track/images.json", images.to_string().into_bytes()),
                    (&format!("track/{code}.erp"), archive),
                ],
            );
        }
        install(
            "t.pack",
            &format!(
                r#"{{"op":"erp.overlay","erps":["{front}"],"source":"front.erp"}},
                   {{"op":"erp.overlay","erps":["{hub}"],"source":"hub.erp"}}"#
            ),
            vec![
                (
                    "front.erp",
                    erp_of(vec![
                        view(FLAGS, 4),
                        surface(FLAGS, &[0, 1, 2, 9]),
                        view(TRACKS, 2),
                        surface(TRACKS, &[0, 9]),
                        surface(LOGO, &[7]),
                    ]),
                ),
                (
                    "hub.erp",
                    erp_of(vec![view(FLAGS, 4), surface(FLAGS, &[0, 1, 2, 9])]),
                ),
            ],
        );

        let built = build(&store, "default", &game).unwrap();
        assert!(
            built.warnings.contains(&kept_menus_line("t.pack")),
            "{:#?}",
            built.warnings
        );
        let slices = |rel: &str, name: &str| {
            let file = ErpFile::open(&built.plan.data[rel]).unwrap();
            let r = file.load(file.erp.find(name).unwrap()).unwrap();
            let data = r.fragments.last().unwrap().data().unwrap();
            match name.ends_with(".image") {
                true => data.chunks(4).map(|c| c[0]).collect::<Vec<u8>>(),
                false => data[0x14..0x18].to_vec(),
            }
        };
        let image = |name: &str| format!("{name}.image");
        assert_eq!(slices(&front, &image(FLAGS)), [0, 1, 2, 0xa0, 0xb0]);
        assert_eq!(slices(&front, FLAGS), 5u32.to_le_bytes());
        assert_eq!(slices(&front, &image(TRACKS)), [0, 1, 2, 0xa1, 0xb1]);
        assert_eq!(slices(&front, TRACKS), 5u32.to_le_bytes());
        assert_eq!(slices(&hub, &image(FLAGS)), [0, 1, 2, 0xa0, 0xb0]);
        assert_eq!(slices(&front, &image(LOGO)), [7]);
        let db = ErpFile::open(&built.plan.data[&ui("databases.erp")]).unwrap();
        let res = db.load(db.erp.find(OVERRIDES).unwrap()).unwrap();
        let pkg = Pkg::parse(&res.fragments[0].data().unwrap()).unwrap();
        let names = |a: &str, b: &str| -> Vec<String> {
            let mut cur = &pkg.root;
            for p in ["overrides", "Contents", a, "Contents", b, "Contents"] {
                cur = cur.as_obj().unwrap().get(p).unwrap();
            }
            cur.as_arr()
                .unwrap()
                .items
                .iter()
                .map(|n| {
                    let name = n.as_obj().unwrap().get("Name").unwrap();
                    name.as_val().unwrap().as_str().unwrap().to_owned()
                })
                .collect()
        };
        let indexed = ["bahrain", "blank", "melbourne", "ring_a", "ring_b"];
        assert_eq!(names("tracks", "flags_circle"), indexed);
        assert_eq!(names("fe_world", "tracks"), indexed);
        let pair = built
            .report
            .pairs
            .iter()
            .find(|p| p.loser == "t.pack")
            .unwrap_or_else(|| panic!("{:#?}", built.report.pairs));
        assert_eq!((pair.winner.as_str(), pair.resources), ("mod.ring_b", 6));
        assert!(pair.items.contains(&format!("{front}:{}", image(FLAGS))));
        assert_eq!(
            crate::conflicts::last(&store, "default").unwrap().pairs,
            built.report.pairs
        );
        std::fs::remove_dir_all(&root).ok();
    }

    fn archive(names: &[&str], byte: u8) -> Vec<u8> {
        archive_of("UILayout", names, byte)
    }

    fn archive_of(kind: &str, names: &[&str], byte: u8) -> Vec<u8> {
        use swofty_formats::erp::{Erp, Fragment, HEADER_LEN, MAGIC, Resource, STORED};
        let mut header = [0u8; HEADER_LEN];
        header[..4].copy_from_slice(MAGIC);
        header[4..8].copy_from_slice(&3u32.to_le_bytes());
        Erp {
            header,
            version: 3,
            resources: names
                .iter()
                .map(|n| Resource {
                    name: (*n).into(),
                    kind: swofty_formats::erp::kind(kind).unwrap(),
                    unk: vec![0; 4],
                    fragments: vec![Fragment {
                        name: *b"temp",
                        offset: 0,
                        size: 1,
                        flags: 0x10,
                        compression: STORED,
                        packed: 1,
                        raw: vec![byte],
                    }],
                    hash: [0; 16],
                })
                .collect(),
        }
        .to_bytes()
    }

    #[test]
    fn skips_archive_changes_to_files_the_game_does_not_have() {
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-absent-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::create_dir_all(dir.join("2025_asset_groups")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb::default().to_bytes()).unwrap();
        let erp = "2025_asset_groups/t.erp";
        let uri = "eaid://t/a.idf";
        std::fs::write(dir.join(erp), archive(&[uri], 0)).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let src = root.join("src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(
            src.join("swofty.json"),
            r#"{ "schema_version": 1, "id": "t.cams", "name": "Cams", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" }, "operations": { "assets": ["ops.json"] } }"#,
        )
        .unwrap();
        let put = |erp: &str| {
            format!(r#"{{"op":"erp.put-resource","erp":"{erp}","resource":"{uri}","source":"a.erp"}}"#)
        };
        std::fs::write(
            src.join("ops.json"),
            format!(
                r#"{{ "schema_version": 1, "ops": [{}, {}] }}"#,
                put(erp),
                put("2025_asset_groups/game_package/t.erp")
            ),
        )
        .unwrap();
        std::fs::write(src.join("a.erp"), archive(&[uri], 7)).unwrap();
        store.install_addon(&src).unwrap();
        store
            .update_profile("default", |p| p.set_enabled("t.cams", true))
            .unwrap();
        let built = build(&store, "default", &game).unwrap();
        assert!(
            built.warnings.contains(
                &"Cams: archive changes skipped; 2025_asset_groups/game_package/t.erp is not a file in F1 25. \
                  Turn Cams off if it doesn't work, and tell its author"
                    .to_owned()
            ),
            "{:?}",
            built.warnings
        );
        assert!(built.plan.data.contains_key(erp), "{:?}", built.plan.data);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn missing_archive_warnings_count_the_files() {
        let rels: BTreeSet<String> = ["b/x.erp", "a/x.erp"].map(str::to_owned).into();
        assert_eq!(
            missing_archives("Cams", &rels),
            "Cams: archive changes skipped; 2 of the files it changes are not in F1 25, such as a/x.erp. \
             Turn Cams off if it doesn't work, and tell its author"
        );
    }

    #[test]
    fn records_and_reports_the_conflicts_between_addons() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-conflicts-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::create_dir_all(dir.join("2025_asset_groups")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let records = vec![
            Record::new("entrants.teams._team_instancecount_", 0, "1"),
            Record::new("entrants.teams.team_0._metadata_", 4, "mclaren_26"),
            Record::new("entrants.teams.team_0.stat_id", 0, "100"),
        ];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let erp = "2025_asset_groups/t.erp";
        let uri = "eaid://t/a.idf";
        std::fs::write(dir.join(erp), archive(&[uri, "eaid://t/b.idf"], 0)).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let stat = format!(
            "0x{:016x}",
            swofty_formats::hash::fnv1a64("entrants.teams.team_0.stat_id")
        );
        for (id, name, erp_op) in [
            (
                "t.first",
                "First",
                format!(r#"{{"op":"erp.put-resource","erp":"{erp}","resource":"{uri}","source":"a.erp"}}"#),
            ),
            (
                "t.second",
                "Second",
                format!(r#"{{"op":"erp.overlay","erps":["{erp}"],"source":"a.erp"}}"#),
            ),
        ] {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(&src).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }},
                         "operations": {{ "assets": ["ops.json"], "files": [{{ "source": "a.dds", "target": "x/a.dds" }}] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(
                    r#"{{ "schema_version": 1, "ops": [{{"op":"db.put-record","key":"{stat}","type":0,"value":"3135"}}, {erp_op}] }}"#
                ),
            )
            .unwrap();
            std::fs::write(src.join("a.erp"), archive(&[uri], 7)).unwrap();
            std::fs::write(src.join("a.dds"), name).unwrap();
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        }
        let listing = |d: &Path| {
            let mut all: Vec<_> = loader_files(d).unwrap().into_iter().collect();
            all.sort();
            all
        };
        let before = listing(&dir);
        let held = lock(&store, "default").unwrap();
        let (tx, rx) = std::sync::mpsc::channel();
        std::thread::scope(|s| {
            s.spawn(|| {
                tx.send(crate::conflicts::check(&store, "default", &game))
                    .unwrap()
            });
            let waited = rx.recv_timeout(std::time::Duration::from_millis(300));
            assert!(waited.is_err(), "the check ran while another build held the lock");
            drop(held);
        });
        let checked = rx.recv().unwrap().unwrap();
        assert_eq!(listing(&dir), before);
        let built = build(&store, "default", &game).unwrap();
        assert!(
            built
                .lines
                .contains(&"First: 1 database value, 1 archive resource, 1 file".to_owned()),
            "{:?}",
            built.lines
        );
        assert!(
            built.warnings.contains(
                &"Second overrides First on 1 database value, 1 archive resource and 1 file".to_owned()
            ),
            "{:?}",
            built.warnings
        );
        assert!(
            !built.warnings.iter().any(|w| w.starts_with("conflict:")),
            "{:?}",
            built.warnings
        );
        let report = crate::conflicts::last(&store, "default").unwrap();
        assert!(report.is_current(&store.profile("default").unwrap()));
        assert_eq!(report.pairs, checked.pairs);
        assert_eq!(report.pairs, built.report.pairs);
        let pair = &report.pairs[0];
        assert_eq!(
            (
                pair.winner.as_str(),
                pair.loser.as_str(),
                pair.database,
                pair.resources,
                pair.files
            ),
            ("t.second", "t.first", 1, 1, 1)
        );
        assert_eq!(
            pair.items,
            [
                format!("{erp}:{uri}"),
                "x/a.dds".to_owned(),
                "entrants.teams.team[_metadata_=mclaren_26].stat_id".to_owned()
            ]
        );
        std::fs::remove_file(crate::conflicts::path(&store, "default")).unwrap();
        let again = build(&store, "default", &game).unwrap();
        assert!(
            again
                .lines
                .iter()
                .any(|l| l.starts_with("build: nothing changed"))
        );
        assert_eq!(
            crate::conflicts::last(&store, "default").unwrap().pairs,
            report.pairs
        );
        assert!(crate::conflicts::last(&store, "vanilla").is_none());
        assert!(
            crate::conflicts::check(&store, "vanilla", &game)
                .unwrap()
                .pairs
                .is_empty()
        );
        std::fs::remove_dir_all(&root).ok();
    }

    // One addon puts `t.erp` whole and another replaces a resource in it with a
    // bigger one; `edit_first` loads the edit before the put.
    fn build_put_and_edit(name: &str, edit_first: bool) -> (Built, PathBuf, PathBuf) {
        let root = std::env::temp_dir().join(format!("swofty-build-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::create_dir_all(dir.join("2025_asset_groups")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let erp = "2025_asset_groups/t.erp";
        std::fs::write(
            dir.join(erp),
            archive_of("Data", &["eaid://t/a.idf", "eaid://t/b.idf"], 0),
        )
        .unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let pack = (
            "t.pack",
            format!(r#""files": [{{ "source": "whole.erp", "target": "{erp}" }}]"#),
        );
        let edit = ("t.edit", r#""assets": ["ops.json"]"#.to_owned());
        let order = match edit_first {
            true => [edit, pack],
            false => [pack, edit],
        };
        for (id, operations) in order {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(&src).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{id}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ {operations} }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(
                    r#"{{ "schema_version": 1, "ops": [{{"op":"erp.replace-resource","erp":"{erp}","resource":"eaid://t/a.idf","source":"a.bin"}}] }}"#
                ),
            )
            .unwrap();
            std::fs::write(src.join("a.bin"), [9, 9]).unwrap();
            std::fs::write(
                src.join("whole.erp"),
                archive_of("Data", &["eaid://t/a.idf", "eaid://t/b.idf", "eaid://t/c.idf"], 5),
            )
            .unwrap();
            store.install_addon(&src).unwrap();
            store
                .update_profile("default", |p| p.set_enabled(id, true))
                .unwrap();
        }
        let built = build(&store, "default", &game).unwrap();
        let whole = store.addons_dir().join("t.pack").join("whole.erp");
        (built, root, whole)
    }

    #[test]
    fn a_resource_edit_that_leaves_stale_tsmd_stats_gets_them_rebuilt() {
        use swofty_formats::bxml::{Document, Element, Kind};
        use swofty_formats::erp::{Erp, Fragment, HEADER_LEN, MAGIC, Resource, STORED};
        let root = std::env::temp_dir().join(format!("swofty-build-tsmd-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        let erp = "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_common.erp";
        std::fs::create_dir_all(dir.join(erp).parent().unwrap()).unwrap();
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let spline = |gate: &str| Document {
            root: Element {
                kind: Kind::Element,
                pad: 0,
                name: b"track".to_vec(),
                attrs: vec![(b"name".to_vec(), gate.as_bytes().to_vec())],
                children: Vec::new(),
                end_pad: 0,
            },
            tail: Vec::new(),
        };
        let fragment = |raw: Vec<u8>| Fragment {
            name: *b"temp",
            offset: 0,
            size: raw.len() as u64,
            flags: 0x10,
            compression: STORED,
            packed: raw.len() as u64,
            raw,
        };
        let res = "eaid://environment_package/tracks/bahrain/ai/bahrain_ai_track.aispline";
        let mut header = [0u8; HEADER_LEN];
        header[..4].copy_from_slice(MAGIC);
        header[4..8].copy_from_slice(&3u32.to_le_bytes());
        let stock = spline("default");
        let bytes = Erp {
            header,
            version: 3,
            resources: vec![Resource {
                name: res.into(),
                kind: swofty_formats::erp::kind("AISplineData").unwrap(),
                unk: vec![0; 4],
                fragments: vec![fragment(stock.to_bytes()), fragment(stock.string_stats(1))],
                hash: [0; 16],
            }],
        }
        .to_bytes();
        std::fs::write(dir.join(erp), bytes).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let src = root.join("src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(
            src.join("swofty.json"),
            r#"{ "schema_version": 1, "id": "t.ai", "name": "More Aggressive AI", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" }, "operations": { "erp": ["ops.json"] } }"#,
        )
        .unwrap();
        std::fs::write(
            src.join("ops.json"),
            format!(
                r#"{{ "schema_version": 1, "ops": [{{"op":"replace-resource","erp":"{erp}","resource":"{res}","fragment":0,"source":"line.aispline"}}] }}"#
            ),
        )
        .unwrap();
        let edited = spline("a racing line edited by hand");
        std::fs::write(src.join("line.aispline"), edited.to_bytes()).unwrap();
        store.install_addon(&src).unwrap();
        store
            .update_profile("default", |p| p.set_enabled("t.ai", true))
            .unwrap();
        let built = build(&store, "default", &game).unwrap();
        assert!(
            built.lines.iter().any(|l| l
                == "erp: fixed the size headers of 1 AI racing line(s) in More Aggressive AI so F1 25 reads them safely"),
            "{:?}",
            built.lines
        );
        assert!(
            !built
                .warnings
                .iter()
                .any(|w| w.contains("aispline") || w.contains("size header"))
        );
        if let Some(path) = built.plan.data.get(erp) {
            let f = swofty_formats::erp::ErpFile::open(path).unwrap();
            let r = f.load(f.erp.find(res).unwrap()).unwrap();
            assert_eq!(r.fragments[1].data().unwrap(), edited.string_stats(1));
        }

        let mut cut = edited.to_bytes();
        cut.truncate(cut.len() - 3);
        std::fs::write(src.join("line.aispline"), cut).unwrap();
        store.install_addon(&src).unwrap();
        let Err(e) = build(&store, "default", &game) else {
            panic!("the build should stop");
        };
        assert_eq!(
            e.to_string(),
            "More Aggressive AI would crash F1 25: its bahrain_ai_track.aispline doesn't match the size header the \
             game reads with it, and the launcher can't read the file to fix the header. Turn More Aggressive AI \
             off in this profile."
        );
        std::fs::remove_dir_all(&root).ok();
    }

    fn fragments(path: &Path) -> Vec<(String, Vec<u8>)> {
        let f = swofty_formats::erp::ErpFile::open(path).unwrap();
        f.erp
            .resources
            .iter()
            .map(|r| (r.name.clone(), f.load(r).unwrap().fragments[0].raw.clone()))
            .collect()
    }

    #[test]
    fn a_later_resource_edit_applies_to_an_archive_put_whole() {
        if loader_dir().is_some() {
            return;
        }
        let (built, root, _) = build_put_and_edit("put-then-edit", false);
        let erp = "2025_asset_groups/t.erp";
        assert_eq!(
            fragments(&built.plan.data[erp]),
            [
                ("eaid://t/a.idf".to_owned(), vec![9, 9]),
                ("eaid://t/b.idf".to_owned(), vec![5]),
                ("eaid://t/c.idf".to_owned(), vec![5]),
            ]
        );
        assert!(!built.plan.added.contains_key(erp));
        let pair = &built.report.pairs[0];
        assert_eq!(
            (pair.winner.as_str(), pair.loser.as_str(), pair.resources),
            ("t.edit", "t.pack", 1)
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn an_archive_put_whole_replaces_earlier_resource_edits() {
        if loader_dir().is_some() {
            return;
        }
        let (built, root, whole) = build_put_and_edit("edit-then-put", true);
        assert_eq!(built.plan.data["2025_asset_groups/t.erp"], whole);
        let pair = &built.report.pairs[0];
        assert_eq!(
            (pair.winner.as_str(), pair.loser.as_str(), pair.resources),
            ("t.pack", "t.edit", 1)
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn an_addon_turns_on_the_experimental_features_it_lists() {
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-features-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let src = root.join("src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(
            src.join("swofty.json"),
            r#"{ "schema_version": 1, "id": "t.hud", "name": "Race Control", "version": "1.0.0", "author": "t",
                 "compat": { "game": "f1_25" }, "experimental_features": ["overlay"] }"#,
        )
        .unwrap();
        store.install_addon(&src).unwrap();
        let assets = |built: &Built| {
            built
                .plan
                .loader
                .get("swofty/assets.json")
                .map(|p| serde_json::from_slice::<serde_json::Value>(&std::fs::read(p).unwrap()).unwrap())
        };
        let off = build(&store, "default", &game).unwrap();
        assert_eq!(assets(&off), None);
        store
            .update_profile("default", |p| p.set_enabled("t.hud", true))
            .unwrap();
        let on = build(&store, "default", &game).unwrap();
        assert_eq!(assets(&on).unwrap()["features"], serde_json::json!(["overlay"]));
        assert!(
            on.lines
                .contains(&"features: the experimental overlay feature is on for Race Control".to_owned()),
            "{:?}",
            on.lines
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn an_enabled_addon_for_a_newer_launcher_is_left_out_with_a_warning() {
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-future-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let future = store.addons_dir().join("w3.hud");
        std::fs::create_dir_all(&future).unwrap();
        std::fs::write(
            future.join(crate::manifest::MANIFEST_FILE),
            r#"{ "schema_version": 1, "id": "w3.hud", "name": "Race HUD", "version": "0.1.0", "author": "t",
                 "compat": { "game": "f1_25" }, "hud": { "widgets": [] } }"#,
        )
        .unwrap();
        store
            .update_profile("default", |p| p.set_enabled("w3.hud", true))
            .unwrap();
        let built = build(&store, "default", &game).unwrap();
        assert!(!built.order.iter().any(|id| id == "w3.hud"), "{:?}", built.order);
        assert!(
            built.warnings.contains(
                &"Race HUD is left out: it needs a newer Swofty Launcher (unknown field `widgets`)"
                    .to_owned()
            ),
            "{:?}",
            built.warnings
        );
        assert!(
            store
                .profile("default")
                .unwrap()
                .enabled()
                .any(|id| id == "w3.hud")
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_shader_pack_grades_with_the_look_its_settings_pick() {
        use swofty_formats::cube::Cube;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-shaders-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let pack = |id: &str, name: &str, warm: &str| {
            let src = root.join(id);
            std::fs::create_dir_all(src.join("looks")).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "1.0.0", "author": "t",
                     "category": "shaders", "experimental_features": ["shaders"], "compat": {{ "game": "f1_25" }},
                     "options": [
                       {{ "id": "look", "type": "choice", "default": "mono", "values": ["mono", "warm"], "binds": "db" }},
                       {{ "id": "strength", "type": "slider", "min": 0, "max": 100, "step": 10, "default": 50,
                         "binds": "db" }}
                     ],
                     "shader": {{ "lut": "looks/{{{{look}}}}.cube", "strength": "{{{{strength}}}}" }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(src.join("looks/mono.cube"), Cube::identity(2).to_text()).unwrap();
            std::fs::write(src.join("looks/warm.cube"), warm).unwrap();
            store.install_addon(&src).unwrap()
        };
        let looks = pack("t.looks", "Looks", &Cube::identity(3).to_text());
        store
            .update_profile("default", |p| p.set_enabled("t.looks", true))
            .unwrap();
        let built = build(&store, "default", &game).unwrap();
        let assets: serde_json::Value =
            serde_json::from_slice(&std::fs::read(&built.plan.loader["swofty/assets.json"]).unwrap())
                .unwrap();
        assert_eq!(assets["features"], serde_json::json!(["shaders"]));
        assert_eq!(
            assets["shader"],
            serde_json::json!({ "lut": "shaders/look.lut", "strength": 0.5, "addon": "Looks" })
        );
        let packed = std::fs::read(&built.plan.loader["swofty/shaders/look.lut"]).unwrap();
        assert_eq!(Cube::unpack(&packed).unwrap().table, Cube::identity(2).table);
        let line = |file: &str, size: usize, strength: u32| {
            format!(
                "shaders: Looks grades the picture with looks/{file} ({size}x{size}x{size}) at {strength}% strength"
            )
        };
        assert!(
            built.lines.contains(&line("mono.cube", 2, 50)),
            "{:?}",
            built.lines
        );

        store
            .update_profile("default", |p| {
                p.set_option(&looks, "look", "warm".into())?;
                p.set_option(&looks, "strength", 100.into()).map(drop)
            })
            .unwrap();
        let built = build(&store, "default", &game).unwrap();
        assert!(
            built.lines.contains(&line("warm.cube", 3, 100)),
            "{:?}",
            built.lines
        );

        let later = pack("t.later", "Later Looks", "LUT_3D_SIZE 2\n0 0 0\n");
        store
            .update_profile("default", |p| {
                p.set_enabled("t.later", true)?;
                p.set_option(&later, "look", "warm".into()).map(drop)
            })
            .unwrap();
        let err = build(&store, "default", &game).unwrap_err().to_string();
        assert!(
            err.contains("Later Looks: looks/warm.cube: 1 colour line(s), but LUT_3D_SIZE 2 needs 8"),
            "{err}"
        );
        store
            .update_profile("default", |p| {
                p.set_option(&later, "look", "mono".into()).map(drop)
            })
            .unwrap();
        let built = build(&store, "default", &game).unwrap();
        let skipped = "Looks: its look is not applied; one shader look applies at a time, and Later Looks loads after it";
        assert!(
            built.warnings.contains(&skipped.to_owned()),
            "{:?}",
            built.warnings
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn the_template_shader_pack_builds() {
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-template-look-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let template = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../templates/shader-pack");
        let m = store.install_addon(&template).unwrap();
        store
            .update_profile("default", |p| p.set_enabled(&m.id, true))
            .unwrap();
        for look in ["monochrome", "overcast", "noir"] {
            store
                .update_profile("default", |p| p.set_option(&m, "look", look.into()).map(drop))
                .unwrap();
            let built = build(&store, "default", &game).unwrap();
            let line = format!(
                "shaders: Monochrome Looks grades the picture with looks/{look}.cube (17x17x17) at 100% strength"
            );
            assert!(built.lines.contains(&line), "{:?}", built.lines);
        }
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn the_template_postfx_pass_compiles_into_the_build() {
        if loader_dir().is_some() || crate::postfx::dxc().is_none() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-postfx-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let template = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../templates/postfx-pass");
        let m = store.install_addon(&template).unwrap();
        store
            .update_profile("default", |p| p.set_enabled(&m.id, true))
            .unwrap();
        store
            .update_profile("default", |p| {
                p.set_option(&m, "saturation", serde_json::json!(40)).map(drop)
            })
            .unwrap();
        let built = build(&store, "default", &game).unwrap();
        let assets: serde_json::Value =
            serde_json::from_slice(&std::fs::read(&built.plan.loader["swofty/assets.json"]).unwrap())
                .unwrap();
        assert_eq!(assets["features"], serde_json::json!(["postfx"]));
        assert_eq!(
            assets["postfx"],
            serde_json::json!([{ "shader": "postfx/00.dxil", "name": "vignette", "addon": "Vignette Pass",
                                 "options": [60.0, 40.0], "budget_ms": 1.5 }])
        );
        let dxil = std::fs::read(&built.plan.loader["swofty/postfx/00.dxil"]).unwrap();
        assert!(dxil.starts_with(b"DXBC"));
        assert!(
            built
                .lines
                .iter()
                .any(|l| l
                    .starts_with("postfx: Vignette Pass's pass vignette runs as pass 1 after the tonemap")),
            "{:?}",
            built.lines
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_built_addon_installs_and_builds() {
        use crate::builder::AddonBuilder;
        use crate::manifest::Category;
        use crate::ops::{DbOp, ErpOp};
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-builder-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::create_dir_all(dir.join("2025_asset_groups")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let records = vec![Record::new("rules.laps", 0, "5")];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let (erp, uri) = ("2025_asset_groups/t.erp", "eaid://t/a.idf");
        std::fs::write(dir.join(erp), archive(&[uri], 0)).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let src = root.join("src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("a.erp"), archive(&[uri], 9)).unwrap();
        std::fs::write(src.join("a.dds"), "dds").unwrap();
        let mut b = AddonBuilder::new("t.generated", "Generated", "t");
        b.category = Some(Category::Helmets);
        b.db(DbOp::PutRecord {
            key: "rules.laps".into(),
            tag: 0,
            value: "3132".into(),
        })
        .erp(ErpOp::PutResource {
            erp: erp.into(),
            resource: uri.into(),
            source: Source::addon("erp/a.erp"),
            rename: None,
        })
        .source("erp/a.erp", src.join("a.erp"))
        .text("*", "lng_laps", "Laps")
        .file(src.join("a.dds"), "x/a.dds");
        b.write(&root.join("out")).unwrap();
        let store = Store::new(root.join("data"));
        let m = store.install_addon(&root.join("out")).unwrap();
        assert_eq!(m.category, Some(Category::Helmets));
        store
            .update_profile("default", |p| p.set_enabled("t.generated", true))
            .unwrap();
        let built = build(&store, "default", &game).unwrap();
        let db = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(db.get_text("rules.laps").as_deref(), Some("12"));
        assert!(built.plan.data.contains_key(erp));
        assert_eq!(
            built.plan.added["x/a.dds"],
            store.addons_dir().join("t.generated/files/x/a.dds")
        );
        assert!(
            built.lines.contains(
                &"Generated: 1 database value, 1 archive resource, 1 file, 1 text string".to_owned()
            ),
            "{:?}",
            built.lines
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn places_screens_under_the_loader_ui_dir() {
        let m = AddonManifest::parse(
            r#"{ "schema_version": 1, "id": "a.pages", "name": "S", "version": "1.0.0", "author": "a",
                 "compat": { "game": "f1_25" },
                 "operations": { "screens": [{ "name": "swofty_addon_0", "file": "ui/page0.ui" },
                   { "name": "generated", "page": "options", "flow": "flow_options", "anchor": "settings_container" }] } }"#,
        )
        .unwrap();
        let files = screen_files(&[&m], Path::new("/addons")).unwrap();
        assert_eq!(
            files,
            BTreeMap::from([(
                "swofty/ui/swofty_addon_0.ui".to_owned(),
                PathBuf::from("/addons/a.pages/ui/page0.ui")
            )])
        );
        assert!(screen_files(&[&m, &m], Path::new("/addons")).is_err());
    }

    #[test]
    fn leaves_out_a_track_package_without_a_base_track() {
        use swofty_formats::gvdb::Record;
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-build-left-out-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(crate::GAME_EXE), "exe").unwrap();
        let weekend = "schedule.activities.activity_race_weekend_0.track";
        let records = vec![
            Record::new("tracks._Track_instancecount_", 0, "1"),
            Record::new("tracks.Track_0._metadata_", 2, "melbourne"),
            Record::new("tracks.Track_0.laps", 0, "58"),
            Record::new(
                "schedule.activities._activity_race_weekend_instancecount_",
                0,
                "1",
            ),
            Record::new(
                "schedule.activities.activity_race_weekend_0._metadata_",
                2,
                "rw_melbourne",
            ),
            Record::new(weekend, 2, "melbourne"),
            Record::new("entrants.teams._team_instancecount_", 0, "1"),
            Record::new("entrants.teams.team_0._metadata_", 2, "mclaren_26"),
            Record::new("entrants.teams.team_0.stat_id", 0, "100"),
        ];
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir: dir.clone(),
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        let addon = |id: &str, name: &str, ops: &str, track: Option<&str>| {
            let src = root.join("src").join(id);
            std::fs::create_dir_all(&src).unwrap();
            std::fs::write(
                src.join("swofty.json"),
                format!(
                    r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "1.0.0", "author": "t",
                         "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
                ),
            )
            .unwrap();
            std::fs::write(
                src.join("ops.json"),
                format!(r#"{{ "schema_version": 1, "ops": [{ops}] }}"#),
            )
            .unwrap();
            if let Some(manifest) = track {
                std::fs::create_dir_all(src.join("track")).unwrap();
                std::fs::write(src.join("track/manifest.json"), manifest).unwrap();
            }
            store.install_addon(&src).unwrap();
        };
        addon(
            "mod.portimao",
            "Portimao",
            r#"{"op":"track.add","source":"track"},
               {"op":"db.set","select":"tracks.Track[_metadata_=portimao]","field":"laps","value":"66"}"#,
            Some(r#"{ "track_code": "portimao", "track_name": "Portimao", "erp": "portimao.erp" }"#),
        );
        addon(
            "t.calendar",
            "Calendar",
            r#"{"op":"db.set","select":"schedule.activities.activity_race_weekend[_metadata_=rw_melbourne]","field":"track","value":"portimao"},
               {"op":"db.set","select":"entrants.teams.team[_metadata_=mclaren_26]","field":"stat_id","value":"7"}"#,
            None,
        );
        let left_out = "Portimao (mod.portimao): track Portimao is left out; its manifest.json doesn't say which track it replaces or copies. Import a fixed version, or ask its author for one";
        let restored =
            "race weekend rw_melbourne: races at melbourne again; track portimao is left out of this build";

        store
            .update_profile("default", |p| p.set_enabled("t.calendar", true))
            .unwrap();
        let without = build(&store, "default", &game).unwrap();
        assert!(
            !without.warnings.iter().any(|w| w.contains("left out")),
            "{:?}",
            without.warnings
        );
        let out = Gvdb::parse(&std::fs::read(&without.plan.data[GVDB]).unwrap()).unwrap();
        assert_eq!(out.get_text(weekend).as_deref(), Some("portimao"));

        store
            .update_profile("default", |p| p.set_enabled("mod.portimao", true))
            .unwrap();
        for pass in ["built", "reused"] {
            let built = build(&store, "default", &game).unwrap();
            let reused = built.lines.iter().any(|l| l.ends_with("reusing it"));
            assert_eq!(reused, pass == "reused");
            let warnings = &built.warnings;
            assert!(warnings.iter().any(|w| w == left_out), "{pass}: {warnings:?}");
            assert!(warnings.iter().any(|w| w == restored), "{pass}: {warnings:?}");
            assert!(
                warnings
                    .iter()
                    .any(|w| w.starts_with("Portimao: change skipped: ") && w.contains("portimao")),
                "{pass}: {warnings:?}"
            );
            let out = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
            assert_eq!(out.get_text(weekend).as_deref(), Some("melbourne"), "{pass}");
            assert_eq!(
                out.get_text("tracks.Track_0.laps").as_deref(),
                Some("58"),
                "{pass}"
            );
            assert_eq!(
                out.get_text("tracks._Track_instancecount_").as_deref(),
                Some("1"),
                "{pass}"
            );
            assert_eq!(
                out.get_text("entrants.teams.team_0.stat_id").as_deref(),
                Some("7"),
                "{pass}"
            );
            let plan: serde_json::Value = crate::read_json(&built.overlay.join(PLAN_FILE)).unwrap();
            assert_eq!(plan["tracks"], serde_json::json!([]), "{pass}");
        }
        std::fs::remove_dir_all(&root).ok();
    }
}
