//! What installing an addon from the Addon Browser brings with it: every addon
//! its manifest `requires`, and theirs, at versions the catalog lists and this
//! launcher can install. Nothing installs when one of them can't be met.

use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet};
use std::fmt;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Condvar, Mutex, MutexGuard};

use semver::VersionReq;
use serde::Deserialize;

use super::{Addon, INSTALLING, Stage, Version, download, downloads_dir, file_name};
use crate::builtin::CORE_ID;
use crate::manifest::{AddonManifest, MANIFEST_FILE, Requirement};
use crate::store::Store;
use crate::{Error, Result};

const ROUNDS: usize = 32;
const SETTLE: usize = 4;
const MANIFEST_LIMIT: u64 = 4 * 1024 * 1024;

#[derive(Debug, Clone, PartialEq)]
pub struct Step {
    pub addon: Addon,
    pub version: Version,
    /// The installed version this one replaces.
    pub replaces: Option<semver::Version>,
}

impl Step {
    pub fn label(&self) -> String {
        format!("{} {}", self.addon.name, self.version.version)
    }
}

/// The addons an install puts in place, in install order: every requirement
/// before the addon that needs it, the addon itself last.
#[derive(Debug, Clone, PartialEq)]
pub struct Plan {
    pub steps: Vec<Step>,
}

impl Plan {
    pub fn addon(&self) -> &Step {
        self.steps.last().expect("a plan ends with its addon")
    }

    /// The other addons it installs or updates.
    pub fn also(&self) -> &[Step] {
        &self.steps[..self.steps.len().saturating_sub(1)]
    }

    pub fn size(&self) -> u64 {
        self.steps.iter().map(|s| s.version.size).sum()
    }
}

/// The addons a player has, for resolving requirements against.
#[derive(Debug, Clone, Default)]
pub struct Have<'a> {
    pub addons: BTreeMap<&'a str, &'a AddonManifest>,
    /// Ones the launcher ships: always present, never replaced.
    pub builtin: BTreeSet<&'a str>,
    /// Ones the Addon Browser never replaces: managed addons and DB Editor addons.
    pub kept: BTreeSet<&'a str>,
}

impl<'a> Have<'a> {
    pub fn new(addons: impl IntoIterator<Item = &'a AddonManifest>, builtin: impl Fn(&str) -> bool) -> Self {
        let mut have = Self::default();
        for m in addons {
            let id = m.id.as_str();
            have.addons.insert(id, m);
            if id == CORE_ID || builtin(id) {
                have.builtin.insert(id);
            } else if crate::managed::is_managed(id) || id.starts_with(crate::dbedit::package::ID_PREFIX) {
                have.kept.insert(id);
            }
        }
        have
    }

    pub fn of(store: &Store, addons: &'a BTreeMap<String, AddonManifest>) -> Self {
        Self::new(addons.values(), |id| store.is_builtin(id))
    }
}

/// One addon's requirement on another.
#[derive(Debug, Clone, PartialEq)]
pub struct Need {
    /// The name of the addon that requires it.
    pub by: String,
    pub version: VersionReq,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Why {
    /// The Addon Browser doesn't list it; the version installed, if one is.
    NotListed(Option<semver::Version>),
    /// None of the versions it lists, newest first, matches.
    NoMatch(Vec<semver::Version>),
    /// The versions that match need a newer launcher.
    NewerLauncher,
    /// It's built into this launcher at this version.
    BuiltIn(semver::Version),
    /// It's installed at this version, and the Addon Browser doesn't replace it.
    Kept(semver::Version),
    /// It's the addon being installed, at this version.
    Installing(semver::Version),
}

#[derive(Debug, Clone, PartialEq)]
pub enum Problem {
    Unmet {
        id: String,
        name: Option<String>,
        needs: Vec<Need>,
        why: Why,
    },
    /// Addons that require each other, each requiring the next and the last the first.
    Cycle(Vec<String>),
}

/// Why an addon can't be installed: the requirements nothing can meet.
#[derive(Debug, Clone, PartialEq)]
pub struct Refusal {
    pub addon: String,
    pub problems: Vec<Problem>,
}

impl Refusal {
    /// The addons it needs that the Addon Browser can't supply, by id.
    pub fn missing(&self) -> Vec<&str> {
        self.problems
            .iter()
            .filter_map(|p| match p {
                Problem::Unmet { id, .. } => Some(id.as_str()),
                Problem::Cycle(_) => None,
            })
            .collect()
    }
}

fn wanted(needs: &[Need], label: &str) -> String {
    let mut parts: Vec<String> = Vec::new();
    for by in needs.iter().map(|n| n.by.as_str()).collect::<BTreeSet<_>>() {
        let mut reqs: Vec<String> = Vec::new();
        for n in needs
            .iter()
            .filter(|n| n.by == by && n.version != VersionReq::STAR)
        {
            let req = n.version.to_string();
            if !reqs.contains(&req) {
                reqs.push(req);
            }
        }
        parts.push(match reqs.is_empty() {
            true => format!("{by} needs {label}"),
            false => format!("{by} needs {label} {}", reqs.join(", ")),
        });
    }
    and_list(&parts)
}

fn and_list(parts: &[String]) -> String {
    match parts {
        [] => String::new(),
        [one] => one.clone(),
        [rest @ .., last] => format!("{} and {last}", rest.join(", ")),
    }
}

impl fmt::Display for Problem {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Unmet { id, name, needs, why } => {
                let label = match name {
                    Some(name) if name != id => format!("{name} ({id})"),
                    _ => id.clone(),
                };
                f.write_str(&wanted(needs, &label))?;
                match why {
                    Why::NotListed(None) => write!(f, ", which isn't on the Addon Browser"),
                    Why::NotListed(Some(v)) => {
                        write!(f, ", which isn't on the Addon Browser; you have {v}")
                    }
                    Why::NoMatch(listed) => {
                        let listed: Vec<String> = listed.iter().map(|v| v.to_string()).collect();
                        write!(f, "; the Addon Browser has {}", and_list(&listed))
                    }
                    Why::NewerLauncher => write!(f, "; the versions that match need a newer launcher"),
                    Why::BuiltIn(v) => write!(f, "; this launcher has {v} built in"),
                    Why::Kept(v) => write!(f, "; you have {v}, which the Addon Browser doesn't replace"),
                    Why::Installing(v) => write!(f, "; this installs {v}"),
                }
            }
            Self::Cycle(ids) => match ids.as_slice() {
                [one] => write!(f, "{one} requires itself"),
                [a, b] => write!(f, "{a} and {b} require each other"),
                many => write!(f, "{} require each other in a loop", and_list(many)),
            },
        }
    }
}

impl fmt::Display for Refusal {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for p in &self.problems {
            write!(f, "{p}. ")?;
        }
        write!(f, "Nothing was installed.")
    }
}

impl std::error::Error for Refusal {}

struct Walk<'w> {
    catalog: &'w [Addon],
    have: &'w Have<'w>,
    launcher: &'w semver::Version,
    learned: &'w BTreeMap<String, Vec<Need>>,
    root: String,
    decided: BTreeMap<String, semver::Version>,
    planned: BTreeSet<String>,
    needs: BTreeMap<String, Vec<Need>>,
    stack: Vec<String>,
    steps: Vec<Step>,
    problems: Vec<Problem>,
    retry: Option<(String, Need)>,
}

type Reader<'r> = dyn FnMut(&Addon, &Version) -> Result<Vec<Requirement>> + 'r;

impl<'w> Walk<'w> {
    fn visit(&mut self, addon: &Addon, v: &Version, read: &mut Reader<'_>) -> Result<()> {
        self.stack.push(addon.id.clone());
        for r in read(addon, v)? {
            if self.retry.is_some() {
                break;
            }
            self.need(&r, &addon.name, read)?;
        }
        self.stack.pop();
        Ok(())
    }

    fn need(&mut self, r: &Requirement, by: &str, read: &mut Reader<'_>) -> Result<()> {
        let need = Need {
            by: by.to_owned(),
            version: r.version.clone(),
        };
        let id = r.id.as_str();
        if let Some(at) = self.stack.iter().position(|s| s == id) {
            let ring = self.stack[at..].to_vec();
            let mut key = ring.clone();
            key.sort();
            let known = self.problems.iter().any(|p| match p {
                Problem::Cycle(other) => {
                    let mut other = other.clone();
                    other.sort();
                    other == key
                }
                Problem::Unmet { .. } => false,
            });
            if !known {
                self.problems.push(Problem::Cycle(ring));
            }
            return Ok(());
        }
        if let Some(Problem::Unmet { needs, .. }) = self
            .problems
            .iter_mut()
            .find(|p| matches!(p, Problem::Unmet { id: other, .. } if other == id))
        {
            needs.push(need);
            return Ok(());
        }
        if let Some(at) = self.decided.get(id).cloned() {
            if r.version.matches(&at) {
                self.needs.entry(id.to_owned()).or_default().push(need);
                return Ok(());
            }
            let why = if id == self.root {
                Why::Installing(at)
            } else if self.have.builtin.contains(id) {
                Why::BuiltIn(at)
            } else if self.have.kept.contains(id) {
                Why::Kept(at)
            } else {
                self.retry = Some((id.to_owned(), need));
                return Ok(());
            };
            let mut needs = self.needs.get(id).cloned().unwrap_or_default();
            needs.push(need);
            self.unmet(id, needs, why);
            return Ok(());
        }
        let installed = self.have.addons.get(id).copied();
        let mut needs = self.learned.get(id).cloned().unwrap_or_default();
        if let Some(m) = installed {
            needs.extend(self.dependents(id, &m.version));
        }
        needs.push(need);
        let fits = |v: &semver::Version| needs.iter().all(|n| n.version.matches(v));
        if let Some(m) = installed {
            if fits(&m.version) {
                self.decided.insert(id.to_owned(), m.version.clone());
                self.needs.insert(id.to_owned(), needs);
                return Ok(());
            }
            let fixed = if self.have.builtin.contains(id) {
                Some(Why::BuiltIn(m.version.clone()))
            } else if self.have.kept.contains(id) {
                Some(Why::Kept(m.version.clone()))
            } else {
                None
            };
            if let Some(why) = fixed {
                self.unmet(id, needs, why);
                return Ok(());
            }
        }
        let Some(addon) = self.catalog.iter().find(|a| a.id == id) else {
            self.unmet(id, needs, Why::NotListed(installed.map(|m| m.version.clone())));
            return Ok(());
        };
        let listed = addon.listed();
        let matching: Vec<&Version> = listed.iter().copied().filter(|v| fits(&v.version)).collect();
        let Some(v) = matching
            .iter()
            .find(|v| v.accepts(self.launcher))
            .map(|v| (*v).clone())
        else {
            let why = match matching.is_empty() {
                true => Why::NoMatch(listed.iter().map(|v| v.version.clone()).collect()),
                false => Why::NewerLauncher,
            };
            self.unmet(id, needs, why);
            return Ok(());
        };
        let addon = addon.clone();
        self.decided.insert(id.to_owned(), v.version.clone());
        self.planned.insert(id.to_owned());
        self.needs.insert(id.to_owned(), needs);
        self.visit(&addon, &v, read)?;
        self.steps.push(Step {
            replaces: installed.map(|m| m.version.clone()),
            addon,
            version: v,
        });
        Ok(())
    }

    /// The requirements the other installed addons have on `id` that its
    /// installed version meets, so replacing it keeps them met.
    fn dependents(&self, id: &str, at: &semver::Version) -> Vec<Need> {
        self.have
            .addons
            .values()
            .filter(|m| m.id != self.root && !self.planned.contains(&m.id))
            .flat_map(|m| {
                m.requires
                    .iter()
                    .filter(|r| r.id == id && r.version.matches(at))
                    .map(|r| Need {
                        by: m.name.clone(),
                        version: r.version.clone(),
                    })
            })
            .collect()
    }

    fn unmet(&mut self, id: &str, needs: Vec<Need>, why: Why) {
        let name = self
            .catalog
            .iter()
            .find(|a| a.id == id)
            .map(|a| a.name.clone())
            .or_else(|| self.have.addons.get(id).map(|m| m.name.clone()));
        self.problems.push(Problem::Unmet {
            id: id.to_owned(),
            name,
            needs,
            why,
        });
    }
}

/// What installing `v` of `addon` installs. `read` gives a version's
/// requirements. A requirement the installed addons meet needs nothing; the
/// rest come from `catalog`, newest matching version first. Fails with
/// [`Error::Requires`] when one can't be met.
pub fn plan(
    addon: &Addon,
    v: &Version,
    catalog: &[Addon],
    have: &Have<'_>,
    launcher: &semver::Version,
    read: &mut Reader<'_>,
) -> Result<Plan> {
    let mut learned: BTreeMap<String, Vec<Need>> = BTreeMap::new();
    for _ in 0..ROUNDS {
        let mut walk = Walk {
            catalog,
            have,
            launcher,
            learned: &learned,
            root: addon.id.clone(),
            decided: BTreeMap::from([(addon.id.clone(), v.version.clone())]),
            planned: BTreeSet::new(),
            needs: BTreeMap::new(),
            stack: Vec::new(),
            steps: Vec::new(),
            problems: Vec::new(),
            retry: None,
        };
        walk.visit(addon, v, read)?;
        if let Some((id, need)) = walk.retry.take() {
            learned.entry(id).or_default().push(need);
            continue;
        }
        if !walk.problems.is_empty() {
            return Err(Error::Requires(Refusal {
                addon: addon.name.clone(),
                problems: walk.problems,
            }));
        }
        let mut steps = walk.steps;
        steps.push(Step {
            addon: addon.clone(),
            version: v.clone(),
            replaces: have.addons.get(addon.id.as_str()).map(|m| m.version.clone()),
        });
        return Ok(Plan { steps });
    }
    Err(Error::Other(format!(
        "{}'s requirements ask for versions that keep changing",
        addon.name
    )))
}

/// [`plan`] from the requirements the registry lists, or none when a version
/// it reaches lists none; the install then reads them from the download.
pub fn preview(
    addon: &Addon,
    v: &Version,
    catalog: &[Addon],
    have: &Have<'_>,
    launcher: &semver::Version,
) -> Option<Result<Plan>> {
    let mut unknown = false;
    let result = plan(addon, v, catalog, have, launcher, &mut |_, v| match &v.requires {
        Some(r) => Ok(r.clone()),
        None => {
            unknown = true;
            Ok(Vec::new())
        }
    });
    (!unknown).then_some(result)
}

#[derive(Deserialize)]
struct Head {
    id: String,
    version: semver::Version,
    #[serde(default)]
    requires: Vec<Requirement>,
}

fn zip_manifest(path: &Path) -> Result<Head> {
    let mut zip = zip::ZipArchive::new(std::fs::File::open(path).map_err(crate::io_err(path))?)?;
    let names: Vec<String> = zip
        .file_names()
        .filter_map(|n| n.ok())
        .map(|n| n.into_owned())
        .collect();
    let nested: Vec<&String> = names
        .iter()
        .filter(|n| {
            n.strip_suffix(MANIFEST_FILE)
                .and_then(|dir| dir.strip_suffix('/'))
                .is_some_and(|dir| !dir.is_empty() && !dir.contains('/'))
        })
        .collect();
    let name = match (names.iter().find(|n| *n == MANIFEST_FILE), nested.as_slice()) {
        (Some(root), _) => root.clone(),
        (None, [one]) => (*one).clone(),
        _ => {
            return Err(Error::Other(format!(
                "{} has no {MANIFEST_FILE} at its root",
                path.display()
            )));
        }
    };
    let mut text = String::new();
    zip.by_name(&name)?
        .take(MANIFEST_LIMIT)
        .read_to_string(&mut text)
        .map_err(crate::io_err(path))?;
    serde_json::from_str(&text).map_err(|e| Error::Manifest(format!("{name}: {e}")))
}

fn requires_in(path: &Path, addon: &Addon, v: &Version) -> Result<Vec<Requirement>> {
    let head = zip_manifest(path)?;
    if head.id != addon.id || head.version != v.version {
        return Err(Error::Other(format!(
            "the registry lists {} {}, the download holds {} {}",
            addon.id, v.version, head.id, head.version
        )));
    }
    Ok(head.requires)
}

/// How far [`install`] has got.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Event<'a> {
    /// What it will install, once the requirements are known.
    Planned(&'a Plan),
    /// The addon the next stages are about.
    Addon(&'a Addon, &'a Version),
    Stage(Stage),
}

type Zips = RefCell<BTreeMap<(String, semver::Version), PathBuf>>;

/// A download in the shared downloads folder that installs running at once use
/// together: one of them downloads it while the others wait, and the last one
/// done with it deletes it.
#[derive(Default)]
struct Shared {
    users: usize,
    busy: bool,
    ready: bool,
}

static SHARED: Mutex<BTreeMap<PathBuf, Shared>> = Mutex::new(BTreeMap::new());
static SHARED_CHANGED: Condvar = Condvar::new();
static PART: AtomicU64 = AtomicU64::new(0);

fn shared() -> MutexGuard<'static, BTreeMap<PathBuf, Shared>> {
    SHARED.lock().unwrap_or_else(|e| e.into_inner())
}

fn fetch(dir: &Path, zips: &Zips, addon: &Addon, v: &Version, progress: &dyn Fn(Event)) -> Result<PathBuf> {
    let key = (addon.id.clone(), v.version.clone());
    if let Some(path) = zips.borrow().get(&key) {
        return Ok(path.clone());
    }
    let path = dir.join(file_name(addon, v));
    zips.borrow_mut().insert(key, path.clone());
    let mut all = shared();
    all.entry(path.clone()).or_default().users += 1;
    loop {
        let slot = all.get_mut(&path).expect("held while this install uses it");
        if slot.ready {
            return Ok(path);
        }
        if !slot.busy {
            slot.busy = true;
            break;
        }
        all = SHARED_CHANGED.wait(all).unwrap_or_else(|e| e.into_inner());
    }
    drop(all);
    progress(Event::Addon(addon, v));
    let part = path.with_extension(format!(
        "zip.part-{}-{}",
        std::process::id(),
        PART.fetch_add(1, Ordering::Relaxed)
    ));
    let result = download(v, &part, &|s| progress(Event::Stage(s)))
        .and_then(|()| crate::with_retry(&path, || std::fs::rename(&part, &path)));
    if result.is_err() {
        std::fs::remove_file(&part).ok();
    }
    let mut all = shared();
    if let Some(slot) = all.get_mut(&path) {
        slot.busy = false;
        slot.ready = result.is_ok();
    }
    SHARED_CHANGED.notify_all();
    result.map(|()| path)
}

/// Lets go of the downloads one install used, deleting each that no other
/// running install still uses.
fn release(zips: Zips) {
    let mut all = shared();
    for path in zips.into_inner().into_values() {
        let Some(slot) = all.get_mut(&path) else {
            continue;
        };
        slot.users -= 1;
        if slot.users == 0 {
            all.remove(&path);
            std::fs::remove_file(&path).ok();
        }
    }
    SHARED_CHANGED.notify_all();
}

/// Downloads `v` of `addon` and every addon it requires that `store` lacks,
/// checks each download, then installs them, requirements first. Returns the
/// manifests it installed in install order, `addon`'s last; a requirement
/// another install put in place meanwhile is left out.
pub fn install(
    store: &Store,
    catalog: &[Addon],
    addon: &Addon,
    v: &Version,
    progress: &dyn Fn(Event),
) -> Result<Vec<AddonManifest>> {
    let dir = downloads_dir(store)?;
    let zips = Zips::default();
    let result = install_with(store, catalog, addon, v, &dir, &zips, progress);
    release(zips);
    result
}

fn install_with(
    store: &Store,
    catalog: &[Addon],
    addon: &Addon,
    v: &Version,
    dir: &Path,
    zips: &Zips,
    progress: &dyn Fn(Event),
) -> Result<Vec<AddonManifest>> {
    let addons = store.addons()?;
    let have = Have::of(store, &addons);
    let launcher = crate::update::current_version();
    let mut read = |a: &Addon, v: &Version| -> Result<Vec<Requirement>> {
        let saved = zips.borrow().get(&(a.id.clone(), v.version.clone())).cloned();
        match (saved, &v.requires) {
            (Some(path), _) => requires_in(&path, a, v),
            (None, Some(listed)) => Ok(listed.clone()),
            (None, None) => requires_in(&fetch(dir, zips, a, v, progress)?, a, v),
        }
    };
    let mut planned = plan(addon, v, catalog, &have, &launcher, &mut read)?;
    let mut settled = false;
    for _ in 0..SETTLE {
        progress(Event::Planned(&planned));
        for s in &planned.steps {
            fetch(dir, zips, &s.addon, &s.version, progress)?;
        }
        let again = plan(addon, v, catalog, &have, &launcher, &mut read)?;
        if again == planned {
            settled = true;
            break;
        }
        planned = again;
    }
    if !settled {
        return Err(Error::Other(format!(
            "{}'s downloads list other requirements than the registry",
            addon.name
        )));
    }
    let mut done = Vec::new();
    for s in &planned.steps {
        let path = zips.borrow()[&(s.addon.id.clone(), s.version.version.clone())].clone();
        progress(Event::Addon(&s.addon, &s.version));
        let installed = {
            let _one = INSTALLING.lock().unwrap_or_else(|e| e.into_inner());
            if s.addon.id != addon.id
                && store
                    .addons()?
                    .get(&s.addon.id)
                    .is_some_and(|m| m.version == s.version.version)
            {
                continue;
            }
            progress(Event::Stage(Stage::Installing));
            let installed = store.install_addon(&path);
            if let Ok(m) = &installed
                && m.id == s.addon.id
                && let Err(e) = super::record_install(store, &m.id, &s.version)
            {
                super::log(
                    &store.root,
                    &format!(
                        "{}: the install from {} went unrecorded: {e}",
                        m.id, s.version.url
                    ),
                );
            }
            installed
        };
        let m = match installed {
            Ok(m) => m,
            Err(e) if s.addon.id == addon.id => return Err(e),
            Err(e) => {
                return Err(Error::Other(format!(
                    "{}, which {} requires: {e}",
                    s.label(),
                    addon.name
                )));
            }
        };
        if m.id != s.addon.id {
            return Err(Error::Other(format!(
                "the registry lists {}, the download installed {}",
                s.addon.id, m.id
            )));
        }
        done.push(m);
    }
    Ok(done)
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use super::*;

    fn version(v: &str, requires: &[(&str, &str)]) -> Version {
        Version {
            version: semver::Version::parse(v).unwrap(),
            launcher: None,
            game: "f1_25".into(),
            size: 3,
            sha256: String::new(),
            url: format!("http://localhost/{v}"),
            published_at: String::new(),
            changelog: String::new(),
            native: false,
            yanked: false,
            requires: Some(
                requires
                    .iter()
                    .map(|(id, req)| Requirement {
                        id: (*id).into(),
                        version: VersionReq::parse(req).unwrap(),
                    })
                    .collect(),
            ),
        }
    }

    fn addon(id: &str, versions: Vec<Version>) -> Addon {
        Addon {
            id: id.into(),
            name: format!("{id} name"),
            author: "a".into(),
            summary: String::new(),
            description: String::new(),
            category: None,
            kind: Vec::new(),
            homepage: None,
            icon: None,
            publisher: None,
            publishers: Vec::new(),
            downloads: 0,
            downloads_30d: 0,
            stars: 0,
            trend: None,
            created_at: String::new(),
            updated_at: String::new(),
            latest: versions.first().cloned(),
            versions,
        }
    }

    fn manifest(id: &str, v: &str, requires: &[(&str, &str)]) -> AddonManifest {
        let requires: Vec<serde_json::Value> = requires
            .iter()
            .map(|(id, req)| serde_json::json!({ "id": id, "version": req }))
            .collect();
        serde_json::from_value(serde_json::json!({
            "schema_version": 1,
            "id": id,
            "name": format!("{id} name"),
            "version": v,
            "author": "a",
            "compat": { "game": "f1_25" },
            "requires": requires,
        }))
        .unwrap()
    }

    fn launcher() -> semver::Version {
        semver::Version::new(1, 4, 0)
    }

    fn core() -> AddonManifest {
        manifest(CORE_ID, "1.0.0", &[])
    }

    fn resolve(target: &Addon, catalog: &[Addon], installed: &[AddonManifest]) -> Result<Plan> {
        let have = Have::new(installed, |_| false);
        preview(
            target,
            target.offered(&launcher()).unwrap(),
            catalog,
            &have,
            &launcher(),
        )
        .unwrap()
    }

    fn labels(plan: &Plan) -> Vec<String> {
        plan.steps
            .iter()
            .map(|s| format!("{}@{}", s.addon.id, s.version.version))
            .collect()
    }

    fn refusal(result: Result<Plan>) -> Refusal {
        match result {
            Err(Error::Requires(r)) => r,
            other => panic!("expected a refusal, got {other:?}"),
        }
    }

    #[test]
    fn installs_every_requirement_first() {
        let a = addon("a", vec![version("1.0.0", &[("b", "^1"), ("c", ">=2.0.0")])]);
        let catalog = vec![
            a.clone(),
            addon(
                "b",
                vec![
                    version("2.0.0", &[]),
                    version("1.2.0", &[]),
                    version("1.0.0", &[]),
                ],
            ),
            addon("c", vec![version("2.1.0", &[]), version("1.0.0", &[])]),
        ];
        let plan = resolve(&a, &catalog, &[core()]).unwrap();
        assert_eq!(labels(&plan), ["b@1.2.0", "c@2.1.0", "a@1.0.0"]);
        assert_eq!(plan.addon().addon.id, "a");
        assert_eq!(plan.also().len(), 2);
        assert_eq!(plan.size(), 9);
    }

    #[test]
    fn follows_requirements_of_requirements() {
        let a = addon("a", vec![version("1.0.0", &[("b", "*")])]);
        let catalog = vec![
            a.clone(),
            addon("b", vec![version("1.0.0", &[("c", "^0.3")])]),
            addon("c", vec![version("0.3.4", &[("d", "*")])]),
            addon("d", vec![version("5.0.0", &[])]),
        ];
        let plan = resolve(&a, &catalog, &[core()]).unwrap();
        assert_eq!(labels(&plan), ["d@5.0.0", "c@0.3.4", "b@1.0.0", "a@1.0.0"]);
    }

    #[test]
    fn installed_and_built_in_requirements_need_nothing() {
        let a = addon(
            "a",
            vec![version(
                "1.0.0",
                &[("b", "^1"), (CORE_ID, ">=0.1.0"), ("tracks", "*")],
            )],
        );
        let catalog = vec![a.clone(), addon("b", vec![version("1.9.0", &[])])];
        let installed = [
            core(),
            manifest("b", "1.1.0", &[("not.anywhere", "*")]),
            manifest("tracks", "0.1.0", &[]),
        ];
        let have = Have::new(&installed, |id| id == "tracks");
        assert!(have.builtin.contains("tracks") && have.builtin.contains(CORE_ID));
        let plan = preview(&a, &a.versions[0], &catalog, &have, &launcher())
            .unwrap()
            .unwrap();
        assert_eq!(labels(&plan), ["a@1.0.0"]);
    }

    #[test]
    fn updates_an_installed_requirement_that_does_not_match() {
        let a = addon("a", vec![version("1.0.0", &[("b", ">=1.2.0")])]);
        let catalog = vec![
            a.clone(),
            addon("b", vec![version("1.3.0", &[("c", "*")]), version("1.0.0", &[])]),
            addon("c", vec![version("1.0.0", &[])]),
        ];
        let plan = resolve(&a, &catalog, &[core(), manifest("b", "1.0.0", &[])]).unwrap();
        assert_eq!(labels(&plan), ["c@1.0.0", "b@1.3.0", "a@1.0.0"]);
        assert_eq!(plan.steps[1].replaces, Some(semver::Version::new(1, 0, 0)));
        assert_eq!(plan.steps[0].replaces, None);
    }

    #[test]
    fn refuses_requirements_the_browser_does_not_have() {
        let a = addon(
            "a",
            vec![version("1.0.0", &[("gone", ">=1.0.0"), ("b", ">=3"), ("c", "*")])],
        );
        let catalog = vec![
            a.clone(),
            addon("b", vec![version("2.0.0", &[]), version("1.0.0", &[])]),
            addon("c", vec![version("1.0.0", &[("deep", "^2")])]),
        ];
        let r = refusal(resolve(&a, &catalog, &[core()]));
        assert_eq!(r.missing(), ["gone", "b", "deep"]);
        let text = r.to_string();
        assert!(
            text.contains("a name needs gone >=1.0.0, which isn't on the Addon Browser"),
            "{text}"
        );
        assert!(
            text.contains("a name needs b name (b) >=3; the Addon Browser has 2.0.0 and 1.0.0"),
            "{text}"
        );
        assert!(
            text.contains("c name needs deep ^2, which isn't on the Addon Browser"),
            "{text}"
        );
        assert!(text.ends_with("Nothing was installed."), "{text}");
    }

    #[test]
    fn refuses_an_installed_requirement_the_browser_cannot_replace() {
        let a = addon("a", vec![version("1.0.0", &[("local", "^2")])]);
        let r = refusal(resolve(
            &a,
            std::slice::from_ref(&a),
            &[core(), manifest("local", "1.0.0", &[])],
        ));
        assert!(
            r.to_string()
                .contains("local name (local) ^2, which isn't on the Addon Browser; you have 1.0.0"),
            "{r}"
        );
        let b = addon("b", vec![version("1.0.0", &[(CORE_ID, ">=9.0.0")])]);
        let core_listed = addon(CORE_ID, vec![version("9.0.0", &[])]);
        let r = refusal(resolve(&b, &[b.clone(), core_listed], &[core()]));
        assert!(r.to_string().contains("this launcher has 1.0.0 built in"), "{r}");
        let c = addon("c", vec![version("1.0.0", &[("teamsimplified.mods", "^3")])]);
        let simplified = addon("teamsimplified.mods", vec![version("3.0.0", &[])]);
        let installed = [core(), manifest("teamsimplified.mods", "2.0.0", &[])];
        let r = refusal(resolve(&c, &[c.clone(), simplified], &installed));
        assert!(
            r.to_string()
                .contains("you have 2.0.0, which the Addon Browser doesn't replace"),
            "{r}"
        );
    }

    #[test]
    fn skips_yanked_versions_and_ones_for_newer_launchers() {
        let a = addon("a", vec![version("1.0.0", &[("b", "^1")])]);
        let mut yanked = version("1.5.0", &[]);
        yanked.yanked = true;
        let mut newer = version("1.4.0", &[]);
        newer.launcher = Some(VersionReq::parse(">=9.0.0").unwrap());
        let catalog = vec![
            a.clone(),
            addon("b", vec![yanked, newer.clone(), version("1.1.0", &[])]),
        ];
        assert_eq!(
            labels(&resolve(&a, &catalog, &[core()]).unwrap()),
            ["b@1.1.0", "a@1.0.0"]
        );
        let only_newer = vec![a.clone(), addon("b", vec![newer])];
        let r = refusal(resolve(&a, &only_newer, &[core()]));
        assert!(r.to_string().contains("need a newer launcher"), "{r}");
    }

    #[test]
    fn refuses_cycles() {
        let a = addon("a", vec![version("1.0.0", &[("b", "*")])]);
        let catalog = vec![
            a.clone(),
            addon("b", vec![version("1.0.0", &[("c", "*")])]),
            addon("c", vec![version("1.0.0", &[("a", "*"), ("c", "*")])]),
        ];
        let r = refusal(resolve(&a, &catalog, &[core()]));
        assert_eq!(
            r.problems,
            [
                Problem::Cycle(vec!["a".into(), "b".into(), "c".into()]),
                Problem::Cycle(vec!["c".into()]),
            ]
        );
        let text = r.to_string();
        assert!(text.contains("a, b and c require each other in a loop"), "{text}");
        assert!(text.contains("c requires itself"), "{text}");
        let pair = vec![
            addon("x", vec![version("1.0.0", &[("y", "*")])]),
            addon("y", vec![version("1.0.0", &[("x", "*")])]),
        ];
        let r = refusal(resolve(&pair[0], &pair, &[core()]));
        assert_eq!(
            r.to_string(),
            "x and y require each other. Nothing was installed."
        );
    }

    #[test]
    fn picks_a_version_every_requirement_accepts() {
        let a = addon("a", vec![version("1.0.0", &[("b", "^1"), ("c", "*")])]);
        let catalog = vec![
            a.clone(),
            addon(
                "b",
                vec![
                    version("1.5.0", &[]),
                    version("1.1.0", &[]),
                    version("0.9.0", &[]),
                ],
            ),
            addon("c", vec![version("1.0.0", &[("b", "<1.2.0")])]),
        ];
        let plan = resolve(&a, &catalog, &[core()]).unwrap();
        assert_eq!(labels(&plan), ["b@1.1.0", "c@1.0.0", "a@1.0.0"]);
        let clash = vec![
            a.clone(),
            catalog[1].clone(),
            addon("c", vec![version("1.0.0", &[("b", "<1.0.0")])]),
        ];
        let r = refusal(resolve(&a, &clash, &[core()]));
        let text = r.to_string();
        assert!(
            text.contains("a name needs b name (b) ^1 and c name needs b name (b) <1.0.0"),
            "{text}"
        );
    }

    #[test]
    fn keeps_installed_addons_working() {
        let a = addon("a", vec![version("1.0.0", &[("b", "^2")])]);
        let catalog = vec![
            a.clone(),
            addon("b", vec![version("2.0.0", &[]), version("1.0.0", &[])]),
        ];
        let installed = [
            core(),
            manifest("b", "1.0.0", &[]),
            manifest("y", "1.0.0", &[("b", "^1")]),
        ];
        let r = refusal(resolve(&a, &catalog, &installed));
        assert!(r.to_string().contains("y name needs b name (b) ^1"), "{r}");
        let broken = [
            core(),
            manifest("b", "1.0.0", &[]),
            manifest("y", "1.0.0", &[("b", "^3")]),
        ];
        assert_eq!(
            labels(&resolve(&a, &catalog, &broken).unwrap()),
            ["b@2.0.0", "a@1.0.0"]
        );
    }

    #[test]
    fn previews_only_what_the_registry_lists() {
        let mut a = addon("a", vec![version("1.0.0", &[("b", "*")])]);
        let mut b = addon("b", vec![version("1.0.0", &[])]);
        b.versions[0].requires = None;
        let have = Have::new(&[], |_| false);
        let catalog = vec![a.clone(), b];
        assert!(preview(&a, &a.versions[0], &catalog, &have, &launcher()).is_none());
        a.versions[0].requires = Some(Vec::new());
        assert!(preview(&a, &a.versions[0], &catalog, &have, &launcher()).is_some());
    }

    #[test]
    fn reads_requires_from_the_registry_when_listed() {
        let listed = r#"{"version":"1.0.0","size":1,"sha256":"x","url":"u",
            "requires":[{"id":"b","version":">=1.0.0"},{"id":"c"}]}"#;
        let v: Version = serde_json::from_str(listed).unwrap();
        let reqs = v.requires.unwrap();
        assert_eq!(reqs.len(), 2);
        assert_eq!(reqs[1].version, VersionReq::STAR);
        let unlisted: Version =
            serde_json::from_str(r#"{"version":"1.0.0","size":1,"sha256":"x","url":"u"}"#).unwrap();
        assert!(unlisted.requires.is_none());
        let unreadable: Version = serde_json::from_str(
            r#"{"version":"1.0.0","size":1,"sha256":"x","url":"u","requires":[{"id":"b","version":"soon"}]}"#,
        )
        .unwrap();
        assert!(unreadable.requires.is_none());
    }

    fn zipped(prefix: &str, manifest: &AddonManifest) -> Vec<u8> {
        let mut z = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
        let opts = zip::write::SimpleFileOptions::default();
        z.start_file(format!("{prefix}{MANIFEST_FILE}"), opts).unwrap();
        std::io::Write::write_all(&mut z, serde_json::to_string(manifest).unwrap().as_bytes()).unwrap();
        z.finish().unwrap().into_inner()
    }

    fn serve(files: Vec<(String, Vec<u8>)>) -> String {
        use std::io::{BufRead, BufReader, Write};
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let base = format!("http://{}", listener.local_addr().unwrap());
        std::thread::spawn(move || {
            for mut stream in listener.incoming().flatten() {
                let mut reader = BufReader::new(stream.try_clone().unwrap());
                let mut request = String::new();
                reader.read_line(&mut request).ok();
                let path = request.split_whitespace().nth(1).unwrap_or_default().to_owned();
                loop {
                    let mut header = String::new();
                    if reader.read_line(&mut header).unwrap_or(0) <= 2 {
                        break;
                    }
                }
                let body = files.iter().find(|(p, _)| *p == path).map(|(_, b)| b.clone());
                let (status, body) = body.map_or((404, Vec::new()), |b| (200, b));
                let head = format!(
                    "HTTP/1.1 {status} X\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                    body.len()
                );
                stream.write_all(head.as_bytes()).ok();
                stream.write_all(&body).ok();
            }
        });
        base
    }

    fn hosted(base: &str, path: &str, zip: &[u8], listed: Option<&[(&str, &str)]>) -> Version {
        let mut v = version("1.0.0", listed.unwrap_or_default());
        v.requires = listed.map(|_| v.requires.clone().unwrap());
        v.size = zip.len() as u64;
        v.sha256 = crate::baseline::sha256_bytes(zip);
        v.url = format!("{base}{path}");
        v
    }

    /// Serves `files` from a thread per request, each answered after its
    /// delay in milliseconds, and counts the requests for each path.
    fn serve_timed(files: Vec<(String, Vec<u8>, u64)>) -> (String, Arc<Mutex<BTreeMap<String, usize>>>) {
        use std::io::{BufRead, BufReader, Write};
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let base = format!("http://{}", listener.local_addr().unwrap());
        let files = Arc::new(files);
        let hits = Arc::new(Mutex::new(BTreeMap::new()));
        let counted = hits.clone();
        std::thread::spawn(move || {
            for mut stream in listener.incoming().flatten() {
                let (files, hits) = (files.clone(), counted.clone());
                std::thread::spawn(move || {
                    let mut reader = BufReader::new(stream.try_clone().unwrap());
                    let mut request = String::new();
                    reader.read_line(&mut request).ok();
                    let path = request.split_whitespace().nth(1).unwrap_or_default().to_owned();
                    loop {
                        let mut header = String::new();
                        if reader.read_line(&mut header).unwrap_or(0) <= 2 {
                            break;
                        }
                    }
                    *hits.lock().unwrap().entry(path.clone()).or_insert(0) += 1;
                    let found = files.iter().find(|(p, _, _)| *p == path);
                    if let Some((_, _, ms)) = found {
                        std::thread::sleep(std::time::Duration::from_millis(*ms));
                    }
                    let (status, body) = found.map_or((404, Vec::new()), |(_, b, _)| (200, b.clone()));
                    let head = format!(
                        "HTTP/1.1 {status} X\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                        body.len()
                    );
                    stream.write_all(head.as_bytes()).ok();
                    stream.write_all(&body).ok();
                });
            }
        });
        (base, hits)
    }

    #[test]
    fn installs_at_once_share_a_requirement_download_and_install_it_once() {
        let root = std::env::temp_dir().join(format!("swofty-requires-shared-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let store = Store::new(&root);
        let a_zip = zipped("", &manifest("t.a", "1.0.0", &[("t.c", "^1")]));
        let b_zip = zipped("", &manifest("t.b", "1.0.0", &[("t.c", "^1")]));
        let c_zip = zipped("", &manifest("t.c", "1.0.0", &[]));
        let (base, hits) = serve_timed(vec![
            ("/a.zip".into(), a_zip.clone(), 300),
            ("/b.zip".into(), b_zip.clone(), 300),
            ("/c.zip".into(), c_zip.clone(), 300),
        ]);
        let a = addon(
            "t.a",
            vec![hosted(&base, "/a.zip", &a_zip, Some(&[("t.c", "^1")]))],
        );
        let b = addon(
            "t.b",
            vec![hosted(&base, "/b.zip", &b_zip, Some(&[("t.c", "^1")]))],
        );
        let c = addon("t.c", vec![hosted(&base, "/c.zip", &c_zip, Some(&[]))]);
        let catalog = vec![a.clone(), b.clone(), c];
        let installed = Arc::new(Mutex::new(Vec::new()));
        let start = Arc::new(std::sync::Barrier::new(2));
        let runs: Vec<_> = [a, b]
            .into_iter()
            .map(|target| {
                let (store, catalog, start, installed) =
                    (store.clone(), catalog.clone(), start.clone(), installed.clone());
                std::thread::spawn(move || {
                    start.wait();
                    let current = RefCell::new(String::new());
                    install(&store, &catalog, &target, &target.versions[0], &|e| match e {
                        Event::Addon(a, _) => *current.borrow_mut() = a.id.clone(),
                        Event::Stage(Stage::Installing) => {
                            installed.lock().unwrap().push(current.borrow().clone())
                        }
                        _ => {}
                    })
                })
            })
            .collect();
        let done: Vec<Vec<String>> = runs
            .into_iter()
            .map(|r| r.join().unwrap().unwrap().iter().map(|m| m.id.clone()).collect())
            .collect();
        assert!(
            done.iter().any(|d| *d == ["t.c", "t.a"] || *d == ["t.a"]),
            "{done:?}"
        );
        assert!(
            done.iter().any(|d| *d == ["t.c", "t.b"] || *d == ["t.b"]),
            "{done:?}"
        );
        assert_eq!(
            done.iter().flatten().filter(|id| *id == "t.c").count(),
            1,
            "{done:?}"
        );
        let mut order = installed.lock().unwrap().clone();
        order.sort();
        assert_eq!(order, ["t.a", "t.b", "t.c"]);
        assert_eq!(hits.lock().unwrap().get("/c.zip"), Some(&1));
        let addons = store.addons().unwrap();
        assert!(["t.a", "t.b", "t.c"].iter().all(|id| addons.contains_key(*id)));
        let downloads = store.root.join(super::super::CACHE_DIR).join("downloads");
        assert_eq!(std::fs::read_dir(&downloads).unwrap().count(), 0);
        assert!(shared().keys().all(|p| !p.starts_with(&root)));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_failed_install_leaves_a_shared_download_to_the_install_still_using_it() {
        let root = std::env::temp_dir().join(format!("swofty-requires-failed-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let store = Store::new(&root);
        let a_zip = zipped("", &manifest("t.a", "1.0.0", &[("t.c", "^1")]));
        let z_zip = zipped("", &manifest("t.z", "1.0.0", &[("t.c", "^1")]));
        let c_zip = zipped("", &manifest("t.c", "1.0.0", &[]));
        let (base, _) = serve_timed(vec![
            ("/a.zip".into(), a_zip.clone(), 600),
            ("/c.zip".into(), c_zip.clone(), 100),
        ]);
        let a = addon(
            "t.a",
            vec![hosted(&base, "/a.zip", &a_zip, Some(&[("t.c", "^1")]))],
        );
        let z = addon(
            "t.z",
            vec![hosted(&base, "/z-missing.zip", &z_zip, Some(&[("t.c", "^1")]))],
        );
        let c = addon("t.c", vec![hosted(&base, "/c.zip", &c_zip, Some(&[]))]);
        let catalog = vec![a.clone(), z.clone(), c];
        let start = Arc::new(std::sync::Barrier::new(2));
        let runs: Vec<_> = [a, z]
            .into_iter()
            .map(|target| {
                let (store, catalog, start) = (store.clone(), catalog.clone(), start.clone());
                std::thread::spawn(move || {
                    start.wait();
                    install(&store, &catalog, &target, &target.versions[0], &|_| {})
                })
            })
            .collect();
        let mut results: Vec<_> = runs.into_iter().map(|r| r.join().unwrap()).collect();
        let z_result = results.pop().unwrap();
        let a_result = results.pop().unwrap();
        let err = z_result.unwrap_err().to_string();
        assert!(err.contains("/z-missing.zip: HTTP 404"), "{err}");
        let ids: Vec<String> = a_result.unwrap().iter().map(|m| m.id.clone()).collect();
        assert_eq!(ids, ["t.c", "t.a"]);
        let downloads = store.root.join(super::super::CACHE_DIR).join("downloads");
        assert_eq!(std::fs::read_dir(&downloads).unwrap().count(), 0);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn installs_requirements_read_from_the_download() {
        let root = std::env::temp_dir().join(format!("swofty-requires-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let store = Store::new(&root);
        let a_zip = zipped("", &manifest("t.a", "1.0.0", &[("t.b", "^1")]));
        let b_zip = zipped("b/", &manifest("t.b", "1.0.0", &[]));
        let z_zip = zipped("", &manifest("t.z", "1.0.0", &[("t.gone", ">=2.0.0")]));
        let base = serve(vec![
            ("/a.zip".into(), a_zip.clone()),
            ("/b.zip".into(), b_zip.clone()),
            ("/z.zip".into(), z_zip.clone()),
        ]);
        let a = addon("t.a", vec![hosted(&base, "/a.zip", &a_zip, None)]);
        let b = addon("t.b", vec![hosted(&base, "/b.zip", &b_zip, Some(&[]))]);
        let z = addon("t.z", vec![hosted(&base, "/z.zip", &z_zip, None)]);
        let catalog = vec![a.clone(), b, z.clone()];
        let seen = RefCell::new(Vec::new());
        let done = install(&store, &catalog, &a, &a.versions[0], &|e| match e {
            Event::Planned(p) => seen.borrow_mut().push(format!("plan {}", labels(p).join(" "))),
            Event::Addon(a, _) => seen.borrow_mut().push(a.id.clone()),
            Event::Stage(Stage::Installing) => seen.borrow_mut().push("installing".into()),
            Event::Stage(_) => {}
        })
        .unwrap();
        let ids: Vec<&str> = done.iter().map(|m| m.id.as_str()).collect();
        assert_eq!(ids, ["t.b", "t.a"]);
        assert_eq!(
            *seen.borrow(),
            [
                "t.a",
                "plan t.b@1.0.0 t.a@1.0.0",
                "t.b",
                "t.b",
                "installing",
                "t.a",
                "installing"
            ]
        );
        let addons = store.addons().unwrap();
        assert!(addons.contains_key("t.a") && addons.contains_key("t.b"));
        let downloads = store.root.join(super::super::CACHE_DIR).join("downloads");
        assert_eq!(std::fs::read_dir(&downloads).unwrap().count(), 0);

        let err = install(&store, &catalog, &z, &z.versions[0], &|_| {}).unwrap_err();
        assert!(matches!(err, Error::Requires(_)), "{err}");
        assert!(!store.addons().unwrap().contains_key("t.z"));
        assert_eq!(std::fs::read_dir(&downloads).unwrap().count(), 0);
        std::fs::remove_dir_all(&root).ok();
    }
}
