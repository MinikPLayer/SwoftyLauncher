use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::{Path, PathBuf};

use swofty_formats::erp::{self, Entry, Erp, ErpFile, Resource};
use swofty_formats::pkg::{Node, Pkg, Value};
use swofty_formats::texarray::Array;

use crate::asset::Source;
use crate::builtin::loader_files;
use crate::manifest::relative_inside;
use crate::ops::{ErpOp, PkgTarget};
use crate::pkgedit::{apply_edits, edit_name, sources, supported};
use crate::runtime::{foreign_argument, layout_change};
use crate::ui::node::component_id;
use crate::{Error, Result};

const SCENE: &str = "UILayout";
const ASSET_GROUPS: &str = "2025_asset_groups";

pub fn served_at_runtime(rel: &str) -> bool {
    rel.starts_with(ASSET_GROUPS)
}

type SourceKey = (String, String, usize);

enum Slot {
    Stock(usize),
    Owned(Resource),
}

struct Archive {
    file: ErpFile,
    slots: Vec<Slot>,
}

impl Archive {
    fn entry<'s>(&'s self, slot: &'s Slot) -> &'s Resource {
        match slot {
            Slot::Stock(i) => &self.file.erp.resources[*i],
            Slot::Owned(r) => r,
        }
    }

    fn name<'s>(&'s self, slot: &'s Slot) -> &'s str {
        &self.entry(slot).name
    }

    fn position(&self, name: &str) -> Result<Option<usize>> {
        let mut hits = (0..self.slots.len()).filter(|&i| self.name(&self.slots[i]) == name);
        match (hits.next(), hits.next()) {
            (Some(_), Some(_)) => Err(fail(format!("{name}: more than one resource"))),
            (hit, _) => Ok(hit),
        }
    }

    fn find(&self, name: &str) -> Result<usize> {
        self.position(name)?
            .ok_or_else(|| fail(format!("{name}: not found")))
    }

    // Read for a stock copy only: every slot still the game's, in its place.
    fn untouched(&self) -> bool {
        self.slots.len() == self.file.erp.resources.len()
            && self
                .slots
                .iter()
                .enumerate()
                .all(|(i, s)| matches!(s, Slot::Stock(j) if *j == i))
    }

    fn owned(&mut self, i: usize) -> Result<&mut Resource> {
        if let Slot::Stock(j) = self.slots[i] {
            self.slots[i] = Slot::Owned(self.file.load(&self.file.erp.resources[j])?);
        }
        match &mut self.slots[i] {
            Slot::Owned(r) => Ok(r),
            Slot::Stock(_) => unreachable!("loaded above"),
        }
    }
}

pub struct ErpBuild<'a> {
    original: &'a dyn Fn(&str) -> PathBuf,
    erps: BTreeMap<String, Archive>,
    // The origin of the op that last wrote each resource, by archive and name,
    // and the origins in the order their ops came.
    writers: HashMap<(String, String), String>,
    origins: Vec<String>,
    sources: HashMap<SourceKey, Node>,
    archives: HashMap<PathBuf, ErpFile>,
    mipmaps: BTreeMap<String, PathBuf>,
    stock: HashMap<String, Option<Resource>>,
    merges: Vec<SliceMerge>,
    notes: Vec<String>,
    kept: HashSet<(String, String)>,
    held: Vec<Held>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Held {
    pub erp: String,
    pub resource: String,
    pub origin: String,
}

// What one merge-slices op did: the stock slice count (0 without a stock array), the
// slices it wrote, whether it replaced the whole resource, and why, as warnings.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct SliceMerge {
    pub erp: String,
    pub resource: String,
    pub slices: usize,
    pub changed: Vec<usize>,
    pub whole: bool,
    pub notes: Vec<String>,
}

fn fail(msg: impl Into<String>) -> Error {
    Error::Resolve(msg.into())
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct StatsRepair {
    pub repaired: Vec<(String, String)>,
    pub broken: Vec<(String, String)>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Resized {
    pub erp: String,
    pub resources: Vec<(String, String)>,
}

fn scene_ids(data: &[u8]) -> Option<Vec<u64>> {
    let pkg = Pkg::parse(data).ok()?;
    let list = pkg.root.as_obj()?.get("scenelist")?.as_arr()?;
    let ids: Vec<u64> = list
        .items
        .iter()
        .filter_map(|s| match &s.as_obj()?.get("name")?.as_val()?.value {
            Value::Raw(b) if b.len() == 8 => Some(u64::from_le_bytes(b[..].try_into().ok()?)),
            _ => None,
        })
        .collect();
    (!ids.is_empty()).then_some(ids)
}

fn check_scene(uri: &str, data: &[u8]) -> std::result::Result<(), String> {
    let want = component_id(uri.strip_suffix(".idf").unwrap_or(uri));
    match scene_ids(data) {
        None => Err("the source is not a scene: a UILayout resource holds a !pkg with a scenelist".into()),
        Some(ids) if ids.contains(&want) => Ok(()),
        Some(ids) => Err(format!(
            "the source is scene {:#018x}, this resource is scene {want:#018x}; it was saved from another .idf",
            ids[0]
        )),
    }
}

pub fn stale_stats_in(file: &ErpFile) -> Result<Vec<String>> {
    let mut out = Vec::new();
    for r in file.erp.resources.iter().filter(|r| r.has_stats()) {
        if file.load(r)?.stale_stats()? {
            out.push(r.name.clone());
        }
    }
    Ok(out)
}

// The resources of `file` with stale TSMD stats: rewritten, by index, and the names
// of those whose stats cannot be rewritten.
pub type ArchiveRepair = (Vec<(usize, Resource)>, Vec<String>);

pub fn repair_stats_in(file: &ErpFile) -> Result<ArchiveRepair> {
    let (mut fixed, mut broken) = (Vec::new(), Vec::new());
    for (i, r) in file
        .erp
        .resources
        .iter()
        .enumerate()
        .filter(|(_, r)| r.has_stats())
    {
        let mut res = file.load(r)?;
        match res.repair_stats() {
            Ok(true) => fixed.push((i, res)),
            Ok(false) => {}
            Err(_) => broken.push(r.name.clone()),
        }
    }
    Ok((fixed, broken))
}

fn file_name(uri: &str) -> &str {
    let path = uri.split('?').next().unwrap_or(uri);
    path.rsplit('/').next().unwrap_or(path)
}

pub fn repaired_note(who: &str, resources: &[String]) -> String {
    let what = match resources.iter().all(|r| r.ends_with(".aispline")) {
        true => "AI racing line(s)",
        false => "file(s)",
    };
    format!(
        "fixed the size headers of {} {what} in {who} so F1 25 reads them safely",
        resources.len()
    )
}

pub fn unrepairable_message(who: &str, resources: &[String], fix: &str) -> String {
    let first = resources.first().map_or("", |r| file_name(r));
    let what = match resources.len() {
        1 => format!(
            "its {first} doesn't match the size header the game reads with it, and the launcher can't read the file to fix the header"
        ),
        n => format!(
            "its {first} and {} more file(s) don't match the size headers the game reads with them, and the launcher can't read the files to fix the headers",
            n - 1
        ),
    };
    format!("{who} would crash F1 25: {what}. {fix}")
}

/// A hash of everything that decides an archive's bytes: the build pipeline,
/// the source archive (path, size and modified time) and every slot, stock by
/// index and owned by all of its fields and fragment bytes.
fn archive_key(rel: &str, a: &Archive) -> Option<String> {
    use sha2::{Digest, Sha256};
    let meta = std::fs::metadata(&a.file.path).ok()?;
    let mut h = Sha256::new();
    let mut field = |b: &[u8]| {
        h.update((b.len() as u64).to_le_bytes());
        h.update(b);
    };
    field(env!("SWOFTY_PIPELINE").as_bytes());
    field(rel.as_bytes());
    field(a.file.path.to_string_lossy().as_bytes());
    field(&meta.len().to_le_bytes());
    field(format!("{:?}", meta.modified().ok()?).as_bytes());
    field(&a.file.erp.header);
    field(&a.file.erp.version.to_le_bytes());
    for slot in &a.slots {
        match slot {
            Slot::Stock(i) => {
                field(b"stock");
                field(&(*i as u64).to_le_bytes());
            }
            Slot::Owned(r) => {
                field(b"owned");
                field(r.name.as_bytes());
                field(&r.kind);
                field(&r.unk);
                field(&r.hash);
                for f in &r.fragments {
                    field(&f.name);
                    field(&f.offset.to_le_bytes());
                    field(&f.size.to_le_bytes());
                    field(&f.flags.to_le_bytes());
                    field(&[f.compression]);
                    field(&f.packed.to_le_bytes());
                    field(&f.raw);
                }
                field(b"end");
            }
        }
    }
    Some(h.finalize().iter().map(|b| format!("{b:02x}")).collect())
}

/// Write `archive` to `out`, or link it from `cache` when an earlier build
/// wrote the same bytes; a newly written one is linked into the cache. True
/// when it came from the cache. Without hard links nothing is cached.
fn write_archive(out: &Path, rel: &str, archive: Archive, cache: Option<&Path>) -> Result<bool> {
    let path = out.join(rel);
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(crate::io_err(dir))?;
    }
    let cached = cache.and_then(|dir| {
        let key = archive_key(rel, &archive)?;
        Some(dir.join(&key[..2]).join(format!("{key}.erp")))
    });
    if let Some(c) = cached.as_ref().filter(|c| c.is_file())
        && std::fs::hard_link(c, &path).is_ok()
    {
        if let Ok(f) = std::fs::File::options().write(true).open(c) {
            f.set_modified(std::time::SystemTime::now()).ok();
        }
        return Ok(true);
    }
    let Archive { file, slots } = archive;
    let entries = slots
        .into_iter()
        .map(|s| match s {
            Slot::Stock(i) => Entry::Copy(&file.erp.resources[i]),
            Slot::Owned(r) => Entry::Owned(r),
        })
        .collect();
    file.write(entries, &path)
        .map_err(|e| fail(format!("{rel}: {e}")))?;
    if let Some(c) = cached {
        if let Some(dir) = c.parent() {
            std::fs::create_dir_all(dir).ok();
        }
        std::fs::hard_link(&path, &c).ok();
    }
    Ok(false)
}

fn key(t: &PkgTarget) -> SourceKey {
    (t.erp.clone(), t.resource.clone(), t.fragment)
}

fn checked(rel: &str) -> Result<&str> {
    if relative_inside(rel) {
        Ok(rel)
    } else {
        Err(fail(format!(
            "{rel} must be a relative path inside the game folder"
        )))
    }
}

pub fn reads(op: &ErpOp) -> Vec<&str> {
    match op {
        ErpOp::PatchPkg { erp, edits, .. } => std::iter::once(erp.as_str())
            .chain(sources(edits).map(|t| t.erp.as_str()))
            .collect(),
        ErpOp::CopyResource { from_erp, erp, .. } => {
            std::iter::once(erp.as_str()).chain(from_erp.as_deref()).collect()
        }
        ErpOp::PutResource { erp, .. }
        | ErpOp::ReplaceResource { erp, .. }
        | ErpOp::AddResource { erp, .. }
        | ErpOp::RemoveResource { erp, .. }
        | ErpOp::MergeSlices { erp, .. }
        | ErpOp::SetMaterial { erp, .. } => vec![erp.as_str()],
        ErpOp::Overlay { erps, .. } => erps.iter().map(String::as_str).collect(),
        ErpOp::AddMipmaps { .. } => Vec::new(),
    }
}

/// The archives `op` changes.
pub fn writes(op: &ErpOp) -> Vec<&str> {
    match op {
        ErpOp::PutResource { erp, .. }
        | ErpOp::ReplaceResource { erp, .. }
        | ErpOp::AddResource { erp, .. }
        | ErpOp::CopyResource { erp, .. }
        | ErpOp::RemoveResource { erp, .. }
        | ErpOp::PatchPkg { erp, .. }
        | ErpOp::MergeSlices { erp, .. }
        | ErpOp::SetMaterial { erp, .. } => vec![erp.as_str()],
        ErpOp::Overlay { erps, .. } => erps.iter().map(String::as_str).collect(),
        ErpOp::AddMipmaps { .. } => Vec::new(),
    }
}

fn replaces(op: &ErpOp) -> Option<(&str, &str)> {
    match op {
        ErpOp::PutResource {
            erp,
            resource,
            rename,
            ..
        }
        | ErpOp::CopyResource {
            erp,
            resource,
            rename,
            ..
        } => Some((erp, rename.as_deref().unwrap_or(resource))),
        ErpOp::ReplaceResource { erp, resource, .. } | ErpOp::RemoveResource { erp, resource } => {
            Some((erp, resource))
        }
        _ => None,
    }
}

pub fn unsupported(op: &ErpOp) -> Option<&'static str> {
    match op {
        ErpOp::PatchPkg { edits, .. } => edits.iter().find(|e| !supported(e)).map(edit_name),
        _ => None,
    }
}

fn read(path: &Path) -> Result<Vec<u8>> {
    std::fs::read(path).map_err(crate::io_err(path))
}

fn read_resource(path: &Path, t: &PkgTarget) -> Result<Node> {
    let found = Erp::read_matching(path, |n| n == t.resource).map_err(|e| fail(format!("{}: {e}", t.erp)))?;
    let [res] = found.as_slice() else {
        return Err(fail(format!(
            "{}: {} found {} times",
            t.erp,
            t.resource,
            found.len()
        )));
    };
    let frag = res
        .fragments
        .get(t.fragment)
        .ok_or_else(|| fail(format!("{}: no fragment {}", t.resource, t.fragment)))?;
    Ok(Pkg::parse(&frag.data()?)?.root)
}

pub fn overlay_names(path: &Path) -> Result<Vec<String>> {
    let file = ErpFile::open(path).map_err(|e| fail(format!("{}: {e}", path.display())))?;
    Ok(file.erp.resources.iter().map(|r| r.name.clone()).collect())
}

impl<'a> ErpBuild<'a> {
    pub fn new(original: &'a dyn Fn(&str) -> PathBuf) -> Self {
        Self {
            original,
            erps: BTreeMap::new(),
            writers: HashMap::new(),
            origins: Vec::new(),
            sources: HashMap::new(),
            archives: HashMap::new(),
            mipmaps: BTreeMap::new(),
            stock: HashMap::new(),
            merges: Vec::new(),
            notes: Vec::new(),
            kept: HashSet::new(),
            held: Vec::new(),
        }
    }

    pub fn keep(&mut self, erp: &str, resource: &str) {
        self.kept.insert((erp.to_owned(), resource.to_owned()));
    }

    pub fn keeps(&self, erp: &str, resource: &str) -> bool {
        !self.kept.is_empty() && self.kept.contains(&(erp.to_owned(), resource.to_owned()))
    }

    pub fn take_held(&mut self) -> Vec<Held> {
        std::mem::take(&mut self.held)
    }

    fn erp(&mut self, rel: &str) -> Result<&mut Archive> {
        if !self.erps.contains_key(rel) {
            let path = (self.original)(checked(rel)?);
            let file = ErpFile::open(&path).map_err(|e| fail(format!("{rel}: {e}")))?;
            let slots = (0..file.erp.resources.len()).map(Slot::Stock).collect();
            self.erps.insert(rel.to_owned(), Archive { file, slots });
        }
        Ok(self.erps.get_mut(rel).expect("inserted above"))
    }

    // Applies an op for an origin, such as an addon or a mod inside one. The
    // runtime route puts each origin's resources in an overlay archive of its own.
    pub fn apply_for(
        &mut self,
        op: &ErpOp,
        origin: &str,
        resolve: &dyn Fn(&Source) -> Result<PathBuf>,
    ) -> Result<()> {
        // An overlay off the runtime route is a put-resource of each resource in it.
        if let ErpOp::Overlay { erps, source } = op {
            let path = resolve(source)?;
            let names = overlay_names(&path)?;
            for erp in erps {
                for name in &names {
                    let put = ErpOp::PutResource {
                        erp: erp.clone(),
                        resource: name.clone(),
                        source: source.clone(),
                        rename: None,
                    };
                    self.apply_for(&put, origin, resolve)?;
                }
            }
            return Ok(());
        }
        if let Some((erp, resource)) = replaces(op).filter(|(e, r)| self.keeps(e, r)) {
            self.held.push(Held {
                erp: erp.to_owned(),
                resource: resource.to_owned(),
                origin: origin.to_owned(),
            });
            return Ok(());
        }
        self.apply(op, resolve)?;
        let written = match op {
            ErpOp::PatchPkg { erp, resource, .. }
            | ErpOp::ReplaceResource { erp, resource, .. }
            | ErpOp::AddResource { erp, resource, .. }
            | ErpOp::SetMaterial { erp, resource, .. } => Some((erp, resource.as_str())),
            ErpOp::CopyResource {
                erp,
                resource,
                rename,
                ..
            }
            | ErpOp::PutResource {
                erp,
                resource,
                rename,
                ..
            } => Some((erp, rename.as_deref().unwrap_or(resource))),
            ErpOp::RemoveResource { erp, resource } => {
                self.writers.remove(&(erp.clone(), resource.clone()));
                None
            }
            ErpOp::MergeSlices { erp, resource, .. } => self
                .merges
                .last()
                .filter(|m| m.whole || !m.changed.is_empty())
                .map(|_| (erp, resource.as_str())),
            ErpOp::AddMipmaps { .. } | ErpOp::Overlay { .. } => None,
        };
        if let Some((erp, name)) = written {
            self.writers
                .insert((erp.clone(), name.to_owned()), origin.to_owned());
            if !self.origins.iter().any(|o| o == origin) {
                self.origins.push(origin.to_owned());
            }
        }
        Ok(())
    }

    pub fn origins(&self) -> &[String] {
        &self.origins
    }

    pub fn apply(&mut self, op: &ErpOp, resolve: &dyn Fn(&Source) -> Result<PathBuf>) -> Result<()> {
        if let Some(name) = unsupported(op) {
            return Err(fail(format!("{name} is not built yet")));
        }
        match op {
            ErpOp::PatchPkg {
                erp,
                resource,
                fragment,
                edits,
            } => {
                if edits.is_empty() {
                    return Ok(());
                }
                for t in sources(edits) {
                    if !self.sources.contains_key(&key(t)) {
                        let node = read_resource(&(self.original)(checked(&t.erp)?), t)?;
                        self.sources.insert(key(t), node);
                    }
                }
                let srcs = std::mem::take(&mut self.sources);
                let result = self.patch(erp, resource, *fragment, |root| {
                    apply_edits(root, edits, &|t| srcs.get(&key(t)))
                });
                self.sources = srcs;
                result
            }
            ErpOp::CopyResource {
                from_erp,
                source,
                erp,
                resource,
                rename,
            } => {
                let path = match (from_erp, source) {
                    (Some(rel), None) => (self.original)(checked(rel)?),
                    (None, Some(src)) => resolve(src)?,
                    _ => {
                        return Err(fail(format!(
                            "copy-resource {resource}: give exactly one of from_erp and source"
                        )));
                    }
                };
                self.copy(&path, resource, erp, rename.as_deref().unwrap_or(resource), false)
            }
            ErpOp::PutResource {
                erp,
                resource,
                source,
                rename,
            } => {
                let path = resolve(source)?;
                self.copy(&path, resource, erp, rename.as_deref().unwrap_or(resource), true)
            }
            ErpOp::SetMaterial {
                erp,
                resource,
                params,
            } => {
                let e = self.erp(erp)?;
                let i = e.find(resource).map_err(|err| fail(format!("{erp}: {err}")))?;
                let res = e.owned(i)?;
                let mut body = res
                    .fragments
                    .first()
                    .ok_or_else(|| fail(format!("{resource}: no fragment 0")))?
                    .data()?;
                for (name, value) in params {
                    let values = value
                        .values()
                        .map_err(|err| fail(format!("{erp}: {resource}: {name}: {err}")))?;
                    swofty_formats::material::set(&mut body, name, &values)
                        .map_err(|err| fail(format!("{erp}: {resource}: {err}")))?;
                }
                res.set_fragment(0, &body)
                    .map_err(|err| fail(format!("{erp}: {err}")))
            }
            ErpOp::ReplaceResource {
                erp,
                resource,
                fragment,
                source,
            } => {
                let data = read(&resolve(source)?)?;
                let e = self.erp(erp)?;
                let i = e.find(resource).map_err(|err| fail(format!("{erp}: {err}")))?;
                let res = e.owned(i)?;
                if res.kind() == SCENE && *fragment == 0 {
                    check_scene(resource, &data).map_err(|err| fail(format!("{erp}: {resource}: {err}")))?;
                }
                res.set_fragment(*fragment, &data)
                    .map_err(|err| fail(format!("{erp}: {err}")))
            }
            ErpOp::AddResource {
                erp,
                resource,
                kind,
                sources,
            } => {
                let data = sources
                    .iter()
                    .map(|s| read(&resolve(s)?))
                    .collect::<Result<Vec<_>>>()?;
                self.add(erp, resource, kind, &data)
            }
            ErpOp::RemoveResource { erp, resource } => {
                match self
                    .erp(erp)?
                    .position(resource)
                    .map_err(|err| fail(format!("{erp}: {err}")))?
                {
                    Some(i) => {
                        self.erp(erp)?.slots.remove(i);
                    }
                    None => self.notes.push(format!("{erp}: {resource} already removed")),
                }
                Ok(())
            }
            ErpOp::MergeSlices {
                erp,
                resource,
                source,
            } => {
                let path = resolve(source)?;
                let merge = self.merge_slices(erp, resource, &path)?;
                self.merges.push(merge);
                Ok(())
            }
            ErpOp::Overlay { erps, source } => {
                let path = resolve(source)?;
                for name in overlay_names(&path)? {
                    for erp in erps {
                        self.copy(&path, &name, erp, &name, true)?;
                    }
                }
                Ok(())
            }
            ErpOp::AddMipmaps { source, target } => {
                let from = resolve(source)?;
                let target = checked(target.trim_end_matches('/'))?;
                if from.is_file() {
                    self.mipmaps.insert(target.to_owned(), from);
                    return Ok(());
                }
                if !from.is_dir() {
                    return Err(fail(format!("add-mipmaps: {} is missing", source.path)));
                }
                let files = loader_files(&from)?;
                if files.is_empty() {
                    return Err(fail(format!("add-mipmaps: {} has no files", source.path)));
                }
                for (rel, path) in files {
                    self.mipmaps.insert(format!("{target}/{rel}"), path);
                }
                Ok(())
            }
        }
    }

    fn add(&mut self, erp: &str, name: &str, kind: &str, data: &[Vec<u8>]) -> Result<()> {
        let wanted = erp::kind(kind).map_err(|e| fail(format!("add-resource {name}: {e}")))?;
        let target = self.erp(erp)?;
        if target
            .position(name)
            .map_err(|e| fail(format!("{erp}: {e}")))?
            .is_some()
        {
            return Err(fail(format!(
                "{erp}: {name} already exists; put-resource replaces a resource"
            )));
        }
        let like = target
            .slots
            .iter()
            .map(|s| target.entry(s))
            .find(|r| r.kind == wanted && r.fragments.len() == data.len())
            .ok_or_else(|| {
                fail(format!(
                    "{erp}: add-resource {name} takes its layout from a {kind} resource with {} fragment(s) in the archive, and it has none",
                    data.len()
                ))
            })?;
        let res = Resource::like(name, like, data).map_err(|e| fail(format!("{erp}: {e}")))?;
        target.slots.push(Slot::Owned(res));
        Ok(())
    }

    fn load_from(&mut self, from: &Path, resource: &str) -> Result<Resource> {
        if !self.archives.contains_key(from) {
            let file = ErpFile::open(from).map_err(|e| fail(format!("{}: {e}", from.display())))?;
            self.archives.insert(from.to_path_buf(), file);
        }
        let file = &self.archives[from];
        let found = file
            .erp
            .find(resource)
            .map_err(|e| fail(format!("{}: {e}", from.display())))?;
        Ok(file.load(found)?)
    }

    fn place(&mut self, erp: &str, res: Resource, replace: bool) -> Result<()> {
        let name = res.name.clone();
        let target = self.erp(erp)?;
        match target.position(&name).map_err(|e| fail(format!("{erp}: {e}")))? {
            Some(i) if replace => target.slots[i] = Slot::Owned(res),
            Some(_) => return Err(fail(format!("{erp}: {name} already exists"))),
            None => target.slots.push(Slot::Owned(res)),
        }
        Ok(())
    }

    fn copy(&mut self, from: &Path, resource: &str, erp: &str, name: &str, replace: bool) -> Result<()> {
        let mut res = self.load_from(from, resource)?;
        res.name = name.to_owned();
        self.place(erp, res, replace)
    }

    // The game's own copy of `resource`: in `erp`, else in the first archive directly
    // in the asset group folder its name starts with, such as ui_package.
    fn stock_copy(&mut self, erp: &str, resource: &str) -> Result<Option<Resource>> {
        let file = &self.erp(erp)?.file;
        if let Some(r) = file.erp.resources.iter().find(|r| r.name == resource) {
            return Ok(Some(file.load(r)?));
        }
        if let Some(found) = self.stock.get(resource) {
            return Ok(found.clone());
        }
        let mut found = None;
        let package = resource.strip_prefix("eaid://").and_then(|r| r.split('/').next());
        if let Some(package) = package.filter(|p| relative_inside(p)) {
            let dir = format!("{ASSET_GROUPS}/{package}");
            let mut names: Vec<String> = std::fs::read_dir((self.original)(&dir))
                .into_iter()
                .flatten()
                .filter_map(|e| e.ok()?.file_name().into_string().ok())
                .filter(|n| n.to_ascii_lowercase().ends_with(".erp"))
                .collect();
            names.sort();
            for name in names {
                let path = (self.original)(&format!("{dir}/{name}"));
                let hits = Erp::read_matching(&path, |n| n == resource)
                    .map_err(|e| fail(format!("{dir}/{name}: {e}")))?;
                if let Some(r) = hits.into_iter().next() {
                    found = Some(r);
                    break;
                }
            }
        }
        self.stock.insert(resource.to_owned(), found.clone());
        Ok(found)
    }

    fn current(&mut self, erp: &str, resource: &str) -> Result<Option<Resource>> {
        let target = self.erp(erp)?;
        let Some(i) = target
            .position(resource)
            .map_err(|e| fail(format!("{erp}: {e}")))?
        else {
            return Ok(None);
        };
        Ok(Some(match &target.slots[i] {
            Slot::Owned(r) => r.clone(),
            Slot::Stock(j) => target.file.load(&target.file.erp.resources[*j])?,
        }))
    }

    fn merge_slices(&mut self, erp: &str, resource: &str, from: &Path) -> Result<SliceMerge> {
        let mut merge = SliceMerge {
            erp: erp.to_owned(),
            resource: resource.to_owned(),
            ..SliceMerge::default()
        };
        let source = self.load_from(from, resource)?;
        let stock = self.stock_copy(erp, resource)?;
        let Some((stock, base)) = stock.as_ref().and_then(|r| Array::read(r).map(|a| (r, a))) else {
            merge.whole = true;
            merge.notes.push(match stock {
                Some(_) => format!(
                    "the game's {resource} is not a texture array; it replaces the whole resource in {erp}"
                ),
                None => format!("the game has no {resource}; it replaces the whole resource in {erp}"),
            });
            self.place(erp, source, true)?;
            return Ok(merge);
        };
        merge.slices = base.count;
        let theirs = match Array::read(&source) {
            Some(a) if a.fits(&base) => a,
            _ => {
                merge.whole = true;
                merge.notes.push(format!(
                    "{resource} differs in format or slice size from the game's array; it replaces the whole resource in {erp}"
                ));
                self.place(erp, source, true)?;
                return Ok(merge);
            }
        };
        if theirs.count > base.count {
            merge.notes.push(format!(
                "{resource} has {} slices, the game's {}; the slices past {} are left out",
                theirs.count, base.count, base.count
            ));
        }
        let (mut res, mut into) = match self.current(erp, resource)? {
            Some(r) => match Array::read(&r).filter(|a| a.same_layout(&base)) {
                Some(a) => (r, a),
                None => {
                    merge.notes.push(format!(
                        "{resource} in {erp} has another layout from an earlier addon; the slices merge over the game's array instead"
                    ));
                    (stock.clone(), base.clone())
                }
            },
            None => (stock.clone(), base.clone()),
        };
        merge.changed = into.merge(&base, &theirs);
        if merge.changed.is_empty() {
            merge.notes.clear();
            return Ok(merge);
        }
        res.set_fragment(1, &into.data)
            .map_err(|e| fail(format!("{erp}: {e}")))?;
        self.place(erp, res, true)?;
        Ok(merge)
    }

    pub fn take_merges(&mut self) -> Vec<SliceMerge> {
        std::mem::take(&mut self.merges)
    }

    pub fn take_notes(&mut self) -> Vec<String> {
        std::mem::take(&mut self.notes)
    }

    // Rewrites the TSMD stats of every resource the ops left with a bxml fragment its
    // stats no longer describe, with the origin that last wrote each.
    /// Puts `edit`'s copy in place of every resource the ops wrote that it
    /// returns one for, and counts them.
    pub fn edit_written(
        &mut self,
        mut edit: impl FnMut(&str, &Resource) -> Result<Option<Resource>>,
    ) -> Result<usize> {
        let mut n = 0;
        for (rel, archive) in &mut self.erps {
            for slot in &mut archive.slots {
                let Slot::Owned(r) = slot else { continue };
                if let Some(new) = edit(rel, r)? {
                    *r = new;
                    n += 1;
                }
            }
        }
        Ok(n)
    }

    pub fn repair_stats(&mut self) -> StatsRepair {
        let mut out = StatsRepair::default();
        for (rel, archive) in &mut self.erps {
            for slot in &mut archive.slots {
                let Slot::Owned(r) = slot else { continue };
                let origin = self.writers.get(&(rel.clone(), r.name.clone()));
                let at = (origin.cloned().unwrap_or_default(), r.name.clone());
                match r.repair_stats() {
                    Ok(true) => out.repaired.push(at),
                    Ok(false) => {}
                    Err(_) => out.broken.push(at),
                }
            }
        }
        out
    }

    fn patch(
        &mut self,
        erp: &str,
        resource: &str,
        fragment: usize,
        edit: impl FnOnce(&mut Node) -> Result<usize>,
    ) -> Result<()> {
        let e = self.erp(erp)?;
        let i = e.find(resource).map_err(|err| fail(format!("{erp}: {err}")))?;
        let res = e.owned(i)?;
        let body = res
            .fragments
            .get(fragment)
            .ok_or_else(|| fail(format!("{resource}: no fragment {fragment}")))?
            .data()?;
        let mut pkg = Pkg::parse(&body).map_err(|err| fail(format!("{resource}: {err}")))?;
        edit(&mut pkg.root).map_err(|err| fail(format!("{resource}: {err}")))?;
        res.set_fragment(fragment, &pkg.to_bytes()?)?;
        Ok(())
    }

    pub fn changed(&self) -> impl Iterator<Item = &str> {
        self.erps
            .iter()
            .filter(|(_, a)| !a.untouched())
            .map(|(rel, _)| rel.as_str())
    }

    pub fn take_mipmaps(&mut self) -> BTreeMap<String, PathBuf> {
        std::mem::take(&mut self.mipmaps)
    }

    pub fn resource(&self, erp: &str, resource: &str, fragment: usize) -> Result<Option<Pkg>> {
        let Some(e) = self.erps.get(erp) else {
            return Ok(None);
        };
        let i = e.find(resource).map_err(|err| fail(format!("{erp}: {err}")))?;
        let loaded;
        let res = match &e.slots[i] {
            Slot::Owned(r) => r,
            Slot::Stock(j) => {
                loaded = e.file.load(&e.file.erp.resources[*j])?;
                &loaded
            }
        };
        let frag = res
            .fragments
            .get(fragment)
            .ok_or_else(|| fail(format!("{resource}: no fragment {fragment}")))?;
        Ok(Some(Pkg::parse(&frag.data()?)?))
    }

    pub fn resources(&self) -> impl Iterator<Item = (&str, &Resource, &Erp, &str)> {
        self.erps.iter().flat_map(move |(rel, a)| {
            a.slots.iter().filter_map(move |s| match s {
                Slot::Owned(r) => {
                    let origin = self
                        .writers
                        .get(&(rel.clone(), r.name.clone()))
                        .map_or("", String::as_str);
                    Some((rel.as_str(), r, &a.file.erp, origin))
                }
                Slot::Stock(_) => None,
            })
        })
    }

    // The in-game loader reads a replaced resource with the stock entry's
    // layout, so an archive whose changes alter a stock entry's type, argument
    // or fragment sizes is rewritten whole instead, and leaves the runtime set.
    pub fn resized(&self) -> Vec<Resized> {
        let mut found = Vec::new();
        for (rel, a) in &self.erps {
            let stock = &a.file.erp;
            let resources: Vec<(String, String)> = a
                .slots
                .iter()
                .filter_map(|s| match s {
                    Slot::Owned(r) if !foreign_argument(r, stock) => {
                        layout_change(r, stock).map(|why| (r.name.clone(), why))
                    }
                    _ => None,
                })
                .collect();
            if !resources.is_empty() {
                found.push(Resized {
                    erp: rel.clone(),
                    resources,
                });
            }
        }
        found
    }

    // The in-game loader patches only archives the asset groups list, through the
    // manifest's name map; the engine opens any other archive, such as the shader
    // packages Simplified Studio packs mods into, through a hashed path an overlay never
    // reaches. Those are written whole: the stock archive with every addon's resources.
    pub fn take_outside(&mut self, out: &Path) -> Result<Vec<String>> {
        let outside: Vec<String> = self
            .erps
            .iter()
            .filter(|(rel, a)| !served_at_runtime(rel) && !a.untouched())
            .map(|(rel, _)| rel.clone())
            .collect();
        for rel in &outside {
            let archive = self.erps.remove(rel).expect("listed above");
            write_archive(out, rel, archive, None)?;
        }
        Ok(outside)
    }

    pub fn take_resized(&mut self, out: &Path) -> Result<Vec<Resized>> {
        let found = self.resized();
        for r in &found {
            let archive = self.erps.remove(&r.erp).expect("found above");
            write_archive(out, &r.erp, archive, None)?;
        }
        Ok(found)
    }

    /// Checks the archives with UI resources that ops wrote against the rules
    /// the game's UI loader enforces. Each finding starts with the origin that
    /// last wrote its resource, as `name` gives it. A name that no longer
    /// matches its stored hash is a warning: the game finds the node, event or
    /// signal by the stored hash, as it did before the rename.
    pub fn check_ui(&self, name: &dyn Fn(&str) -> String) -> Result<Vec<String>> {
        use crate::ui::check::{ArchiveUi, Finding, STALE_HASH};
        let ui = |r: &Resource| matches!(r.kind().as_str(), "UIDF" | SCENE) && !r.name.contains('?');
        let frags = |r: &Resource| {
            r.fragments
                .iter()
                .map(|f| f.data())
                .collect::<swofty_formats::Result<Vec<_>>>()
        };
        let (mut edited, mut stock) = (Vec::new(), Vec::new());
        for (rel, a) in &self.erps {
            if !a.slots.iter().any(|s| matches!(s, Slot::Owned(r) if ui(r))) {
                continue;
            }
            let mut was = ArchiveUi {
                archive: rel.clone(),
                ..Default::default()
            };
            for r in a.file.erp.resources.iter().filter(|r| ui(r)) {
                let loaded = a.file.load(r)?;
                was.resources.insert(r.name.clone(), (r.kind(), frags(&loaded)?));
            }
            let mut now = ArchiveUi {
                archive: rel.clone(),
                ..Default::default()
            };
            for s in &a.slots {
                let r = a.entry(s);
                if !ui(r) {
                    continue;
                }
                let entry = match (s, was.resources.get(&r.name)) {
                    (Slot::Stock(_), Some(stock)) => stock.clone(),
                    _ => (r.kind(), frags(r)?),
                };
                now.resources.insert(r.name.clone(), entry);
            }
            edited.push(now);
            stock.push(was);
        }
        if edited.is_empty() {
            return Ok(Vec::new());
        }
        let report = crate::ui::check::archives(
            &edited,
            &stock,
            crate::ui::catalog::stock(),
            crate::ui::catalog::engine(),
        )
        .map_err(|e| fail(format!("checking UI edits: {e}")))?;
        let by = |f: &Finding| {
            self.writers
                .iter()
                .find(|((erp, res), _)| f.at.starts_with(&format!("{erp}: {res}: ")))
                .map_or_else(|| "the game's own copy".to_owned(), |(_, origin)| name(origin))
        };
        let (renamed, hard): (Vec<Finding>, Vec<Finding>) =
            report.hard.into_iter().partition(|f| f.code == STALE_HASH);
        if !hard.is_empty() {
            let lines: Vec<String> = hard.iter().map(|f| format!("{}: {f}", by(f))).collect();
            return Err(fail(format!(
                "{} UI edit(s) break rules the game's UI loader enforces:\n  {}",
                lines.len(),
                lines.join("\n  ")
            )));
        }
        Ok(renamed
            .iter()
            .map(|f| {
                format!(
                    "{}: {}: {}: the name no longer matches its stored hash, so the game still finds it by the hash, as before the rename",
                    by(f),
                    f.at,
                    f.message
                )
            })
            .chain(report.warnings.iter().map(|f| format!("{}: {f}", by(f))))
            .collect())
    }

    pub fn write(self, out: &Path) -> Result<Vec<String>> {
        Ok(self.write_cached(out, None, &|| Ok(()))?.0)
    }

    /// Write the changed archives to `out`, linking from `cache` the ones an
    /// earlier build wrote the same; the archives written and how many came
    /// from the cache. `cancel` runs before each archive.
    pub fn write_cached(
        self,
        out: &Path,
        cache: Option<&Path>,
        cancel: &dyn Fn() -> Result<()>,
    ) -> Result<(Vec<String>, usize)> {
        let mut written = Vec::new();
        let mut reused = 0;
        for (rel, archive) in self.erps.into_iter().filter(|(_, a)| !a.untouched()) {
            cancel()?;
            crate::now_doing(format!("writing {rel}"));
            reused += usize::from(write_archive(out, &rel, archive, cache)?);
            written.push(rel);
        }
        Ok((written, reused))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::asset::{AssetOp, FileKind, OpFile};
    use crate::runtime::Runtime;
    use swofty_formats::erp::{Fragment, HEADER_LEN, MAGIC, Resource, STORED, ZSTD, compress};
    use swofty_formats::pkg::{Arr, Obj, Val};

    fn erp_ops(json: &str) -> Vec<ErpOp> {
        let file = OpFile::from_value(serde_json::from_str(json).unwrap(), FileKind::Erp).unwrap();
        file.ops
            .into_iter()
            .filter_map(|op| match op {
                AssetOp::Erp(op) => Some(op),
                _ => None,
            })
            .collect()
    }

    fn apply(b: &mut ErpBuild, ops: &[ErpOp], dir: &Path) -> Result<()> {
        ops.iter()
            .try_for_each(|op| b.apply(op, &|s: &Source| Ok(s.resolve(dir)?)))
    }

    fn layer(names: &[&str]) -> Vec<u8> {
        let items = names
            .iter()
            .map(|n| {
                Node::Obj(Obj::new(vec![
                    (Val::str("name"), Node::Val(Val::str(*n))),
                    (Val::str("id"), Node::Val(Val::u32(1))),
                ]))
            })
            .collect();
        Pkg {
            name: *b"INST",
            root: Node::Obj(Obj::new(vec![(
                Val::str("instances"),
                Node::Arr(Arr::new(items)),
            )])),
            vca: Vec::new(),
            layout: None,
        }
        .to_bytes()
        .unwrap()
    }

    fn erp(resources: &[(&str, Vec<u8>)]) -> Vec<u8> {
        typed_erp("WOInstances", resources)
    }

    fn typed_erp(kind: &str, resources: &[(&str, Vec<u8>)]) -> Vec<u8> {
        let mut header = [0u8; HEADER_LEN];
        header[..4].copy_from_slice(MAGIC);
        header[4..8].copy_from_slice(&4u32.to_le_bytes());
        let resources = resources
            .iter()
            .map(|(name, body)| {
                let packed = compress(body).unwrap();
                let mut res = Resource {
                    name: (*name).into(),
                    kind: erp::kind(kind).unwrap(),
                    unk: vec![0; 6],
                    fragments: vec![Fragment {
                        name: *b"temp",
                        offset: 0,
                        size: body.len() as u64,
                        flags: 0,
                        compression: ZSTD,
                        packed: packed.len() as u64,
                        raw: packed,
                    }],
                    hash: [0; 16],
                };
                res.hash = res.content_hash().unwrap();
                res
            })
            .collect();
        Erp {
            header,
            version: 4,
            resources,
        }
        .to_bytes()
    }

    fn spline(gates: &str) -> swofty_formats::bxml::Document {
        use swofty_formats::bxml::{Document, Element, Kind};
        let gate = |id: usize| Element {
            kind: Kind::Element,
            pad: 0,
            name: b"gate".to_vec(),
            attrs: vec![(b"name".to_vec(), format!("{gates}_{id:03}").into_bytes())],
            children: Vec::new(),
            end_pad: 0,
        };
        Document {
            root: Element {
                kind: Kind::Element,
                pad: 0,
                name: b"track".to_vec(),
                attrs: Vec::new(),
                children: (0..3).map(gate).collect(),
                end_pad: 0,
            },
            tail: Vec::new(),
        }
    }

    #[test]
    fn the_build_rewrites_stale_tsmd_stats_of_a_replaced_bxml_fragment() {
        let root = std::env::temp_dir().join(format!("swofty-erpbuild-tsmd-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(game.join("tracks")).unwrap();
        std::fs::create_dir_all(addon.join("ai")).unwrap();
        const SPLINE: &str = "eaid://environment_package/tracks/bahrain/ai/bahrain_ai_track.aispline";
        let stock = spline("ai_gate_track");
        let fragment = |raw: Vec<u8>| Fragment {
            name: *b"temp",
            offset: 0,
            size: raw.len() as u64,
            flags: 0,
            compression: STORED,
            packed: raw.len() as u64,
            raw,
        };
        let mut res = Resource {
            name: SPLINE.into(),
            kind: erp::kind("AISplineData").unwrap(),
            unk: vec![0; 6],
            fragments: vec![fragment(stock.to_bytes()), fragment(stock.string_stats(1))],
            hash: [0; 16],
        };
        res.hash = res.content_hash().unwrap();
        let mut header = [0u8; HEADER_LEN];
        header[..4].copy_from_slice(MAGIC);
        header[4..8].copy_from_slice(&4u32.to_le_bytes());
        let mut archive = Erp {
            header,
            version: 4,
            resources: vec![res],
        };
        std::fs::write(game.join("tracks/bahrain_common.erp"), archive.to_bytes()).unwrap();
        assert!(
            stale_stats_in(&ErpFile::open(&game.join("tracks/bahrain_common.erp")).unwrap())
                .unwrap()
                .is_empty()
        );
        let edited = spline("ai_gate_track_edited_by_hand");
        let mut cut = edited.to_bytes();
        cut.truncate(cut.len() - 3);
        std::fs::write(addon.join("ai/stock.aispline"), stock.to_bytes()).unwrap();
        std::fs::write(addon.join("ai/line.aispline"), edited.to_bytes()).unwrap();
        std::fs::write(addon.join("ai/line.tsmd"), edited.string_stats(1)).unwrap();
        std::fs::write(addon.join("ai/cut.aispline"), cut).unwrap();
        let original = |rel: &str| game.join(rel);
        let resolve = |s: &Source| Ok(s.resolve(&addon)?);
        let replace = |fragment: usize, source: &str| {
            erp_ops(&format!(
                r#"{{"schema_version":1,"ops":[{{"op":"replace-resource","erp":"tracks/bahrain_common.erp","resource":"{SPLINE}","fragment":{fragment},"source":"{source}"}}]}}"#
            ))
            .remove(0)
        };
        let build = |ops: &[(usize, &str)]| {
            let mut b = ErpBuild::new(&original);
            for (fragment, source) in ops {
                b.apply_for(&replace(*fragment, source), "kizi.ai", &resolve)
                    .unwrap();
            }
            let repair = b.repair_stats();
            let stats = b
                .resources()
                .find(|(_, r, _, _)| r.name == SPLINE)
                .map(|(_, r, _, _)| r.fragments[1].data().unwrap());
            (repair, stats, b.repair_stats())
        };
        let at = vec![("kizi.ai".to_owned(), SPLINE.to_owned())];

        let (repair, stats, again) = build(&[(0, "ai/line.aispline")]);
        assert_eq!(repair.repaired, at);
        assert_eq!(stats.unwrap(), edited.string_stats(1));
        assert_eq!(again, StatsRepair::default());

        let (repair, stats, _) = build(&[(0, "ai/stock.aispline")]);
        assert_eq!(repair, StatsRepair::default());
        assert_eq!(stats.unwrap(), stock.string_stats(1));

        let (repair, _, _) = build(&[(0, "ai/line.aispline"), (1, "ai/line.tsmd")]);
        assert_eq!(repair, StatsRepair::default());

        let (repair, stats, _) = build(&[(1, "ai/line.tsmd")]);
        assert_eq!(repair.repaired, at);
        assert_eq!(stats.unwrap(), stock.string_stats(1));

        let (repair, _, _) = build(&[(0, "ai/cut.aispline")]);
        assert_eq!(repair.broken, at);

        archive.resources[0].set_fragment(0, &edited.to_bytes()).unwrap();
        std::fs::write(root.join("broken.erp"), archive.to_bytes()).unwrap();
        let file = ErpFile::open(&root.join("broken.erp")).unwrap();
        assert_eq!(stale_stats_in(&file).unwrap(), [SPLINE]);
        let (fixed, broken) = repair_stats_in(&file).unwrap();
        assert_eq!((fixed.len(), fixed[0].0, broken.len()), (1, 0, 0));
        assert_eq!(fixed[0].1.fragments[1].data().unwrap(), edited.string_stats(1));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn the_stats_notes_name_the_files_and_the_fix() {
        let spline = |t: &str| format!("eaid://environment_package/tracks/{t}/ai/{t}_ai_track.aispline");
        let lines: Vec<String> = (0..20).map(|i| spline(&format!("t{i}"))).collect();
        assert_eq!(
            repaired_note("More AI", &lines),
            "fixed the size headers of 20 AI racing line(s) in More AI so F1 25 reads them safely"
        );
        let quadtree = "eaid://t/x.trackspacespline?context=quadtree".to_owned();
        assert_eq!(
            repaired_note("Sepang", &[quadtree.clone(), spline("x")]),
            "fixed the size headers of 2 file(s) in Sepang so F1 25 reads them safely"
        );
        assert_eq!(
            unrepairable_message(
                "More AI",
                &[spline("bahrain")],
                "Turn More AI off in this profile."
            ),
            "More AI would crash F1 25: its bahrain_ai_track.aispline doesn't match the size header the game \
             reads with it, and the launcher can't read the file to fix the header. Turn More AI off in this profile."
        );
        assert_eq!(
            unrepairable_message("Sepang", &[quadtree, spline("x")], "Fix."),
            "Sepang would crash F1 25: its x.trackspacespline and 1 more file(s) don't match the size headers the \
             game reads with them, and the launcher can't read the files to fix the headers. Fix."
        );
    }

    #[test]
    fn a_ready_overlay_loads_as_it_is_or_goes_in_resource_by_resource() {
        let root = std::env::temp_dir().join(format!("swofty-erpbuild-ready-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(game.join("ui")).unwrap();
        std::fs::create_dir_all(addon.join("mods")).unwrap();
        let body = |s: &str| layer(&[s]);
        std::fs::write(game.join("ui/a.erp"), erp(&[("eaid://stock", body("stock"))])).unwrap();
        std::fs::write(addon.join("mods/a.erp"), erp(&[("eaid://one", body("a1"))])).unwrap();
        std::fs::write(
            addon.join("mods/ready.erp"),
            erp(&[("eaid://one", body("c1")), ("eaid://two", body("c2"))]),
        )
        .unwrap();
        let original = |rel: &str| game.join(rel);
        let resolve = |s: &Source| Ok(s.resolve(&addon)?);
        let put = &erp_ops(
            r#"{"schema_version":1,"ops":[{"op":"put-resource","erp":"ui/a.erp","resource":"eaid://one","source":"mods/a.erp"}]}"#,
        )[0];
        let ready = &erp_ops(
            r#"{"schema_version":1,"ops":[{"op":"overlay","erps":["ui/a.erp"],"source":"mods/ready.erp"}]}"#,
        )[0];
        let path = addon.join("mods/ready.erp");
        assert_eq!(
            crate::runtime::overlay_fits(&path, &["ui/a.erp".into()], &original).unwrap(),
            Ok(2)
        );

        // Loaded as it is: listed before the earlier origin's overlay, so it wins.
        let mut b = ErpBuild::new(&original);
        b.apply_for(put, "mods (a)", &resolve).unwrap();
        let prebuilt = [crate::runtime::Prebuilt {
            origin: "mods (c)".into(),
            path: path.clone(),
            erps: vec!["ui/a.erp".into()],
            resources: 2,
        }];
        let overlay = root.join("overlay");
        let mut rt = Runtime::new(Vec::new());
        let ranks = ["mods (a)".to_owned(), "mods (c)".to_owned()];
        rt.resources(b.resources(), &ranks, &prebuilt, &overlay).unwrap();
        let listed: Vec<(&str, &str)> = rt
            .assets
            .resource_erps
            .iter()
            .map(|e| (e.file.as_str(), e.origin.as_str()))
            .collect();
        assert_eq!(
            listed,
            [
                ("assets/resources/p0.erp", "mods (c)"),
                ("assets/resources/0.erp", "mods (a)")
            ]
        );
        assert_eq!(rt.files["swofty/assets/resources/p0.erp"], path);

        // Off the runtime route it is a put-resource of each of its resources.
        let mut b = ErpBuild::new(&original);
        b.apply_for(put, "mods (a)", &resolve).unwrap();
        b.apply_for(ready, "mods (c)", &resolve).unwrap();
        let mut left: Vec<(String, String)> = b
            .resources()
            .map(|(_, r, _, origin)| (r.name.clone(), origin.to_owned()))
            .collect();
        left.sort();
        assert_eq!(
            left,
            [
                ("eaid://one".to_owned(), "mods (c)".to_owned()),
                ("eaid://two".to_owned(), "mods (c)".to_owned())
            ]
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn archives_outside_the_asset_groups_are_written_whole() {
        let root = std::env::temp_dir().join(format!("swofty-erpbuild-outside-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (game, addon, out) = (root.join("game"), root.join("addon"), root.join("out"));
        let shader = "shader_package_2025/object_flag.nefx2.sm51.erp";
        let group = "2025_asset_groups/vfx_package/common.erp";
        std::fs::create_dir_all(game.join("shader_package_2025")).unwrap();
        std::fs::create_dir_all(game.join("2025_asset_groups/vfx_package")).unwrap();
        std::fs::create_dir_all(addon.join("mods")).unwrap();
        let body = |s: &str| layer(&[s]);
        std::fs::write(game.join(shader), erp(&[("eaid://shader", body("shader"))])).unwrap();
        std::fs::write(game.join(group), erp(&[("eaid://vfx", body("vfx"))])).unwrap();
        std::fs::write(addon.join("mods/car.erp"), erp(&[("eaid://car", body("car"))])).unwrap();
        let put = |to: &str| {
            format!(r#"{{"op":"put-resource","erp":"{to}","resource":"eaid://car","source":"mods/car.erp"}}"#)
        };
        let ops = erp_ops(&format!(
            r#"{{"schema_version":1,"ops":[{},{}]}}"#,
            put(shader),
            put(group)
        ));
        let original = |rel: &str| game.join(rel);
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &ops, &addon).unwrap();

        assert!(served_at_runtime(group) && !served_at_runtime(shader));
        assert_eq!(b.take_outside(&out).unwrap(), [shader]);
        let written = Erp::parse(&std::fs::read(out.join(shader)).unwrap()).unwrap();
        let names: Vec<&str> = written.resources.iter().map(|r| r.name.as_str()).collect();
        assert_eq!(names, ["eaid://shader", "eaid://car"]);
        let left: Vec<&str> = b.resources().map(|(rel, ..)| rel).collect();
        assert_eq!(left, [group]);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn each_origin_gets_an_overlay_archive_of_its_own() {
        let root = std::env::temp_dir().join(format!("swofty-erpbuild-origins-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(game.join("ui")).unwrap();
        std::fs::create_dir_all(addon.join("mods")).unwrap();
        let body = |s: &str| layer(&[s]);
        std::fs::write(game.join("ui/a.erp"), erp(&[("eaid://stock", body("stock"))])).unwrap();
        std::fs::write(game.join("ui/boot.erp"), erp(&[("eaid://boot", body("boot"))])).unwrap();
        std::fs::write(
            addon.join("mods/a.erp"),
            erp(&[("eaid://one", body("a1")), ("eaid://shared", body("from a"))]),
        )
        .unwrap();
        std::fs::write(
            addon.join("mods/b.erp"),
            erp(&[("eaid://two", body("b2")), ("eaid://shared", body("from b"))]),
        )
        .unwrap();
        let put = |res: &str, from: &str| {
            format!(
                r#"{{"op":"put-resource","erp":"ui/a.erp","resource":"{res}","source":"mods/{from}.erp"}}"#
            )
        };
        let ops = |list: &[String]| erp_ops(&format!(r#"{{"schema_version":1,"ops":[{}]}}"#, list.join(",")));
        let original = |rel: &str| game.join(rel);
        let mut b = ErpBuild::new(&original);
        let resolve = |s: &Source| Ok(s.resolve(&addon)?);
        for op in ops(&[put("eaid://one", "a"), put("eaid://shared", "a")]) {
            b.apply_for(&op, "mods (a)", &resolve).unwrap();
        }
        let to_boot =
            r#"{"op":"put-resource","erp":"ui/boot.erp","resource":"eaid://three","source":"mods/b3.erp"}"#;
        std::fs::write(addon.join("mods/b3.erp"), erp(&[("eaid://three", body("b3"))])).unwrap();
        for op in ops(&[
            put("eaid://two", "b"),
            put("eaid://shared", "b"),
            to_boot.to_owned(),
        ]) {
            b.apply_for(&op, "mods (b)", &resolve).unwrap();
        }
        assert_eq!(b.origins(), ["mods (a)", "mods (b)"]);
        let overlay = root.join("overlay");
        let mut rt = Runtime::new(Vec::new());
        assert!(
            rt.resources(b.resources(), b.origins(), &[], &overlay)
                .unwrap()
                .is_empty()
        );
        let rt = rt.finish(&overlay).unwrap().unwrap();
        let erps: Vec<(&str, &str, Vec<String>)> = rt
            .assets
            .resource_erps
            .iter()
            .map(|e| {
                let file = Erp::parse(&std::fs::read(overlay.join("swofty").join(&e.file)).unwrap()).unwrap();
                let names = file.resources.iter().map(|r| r.name.clone()).collect();
                (e.file.as_str(), e.origin.as_str(), names)
            })
            .collect();
        // The game reads the first listed overlay first, so the later origin comes first.
        assert_eq!(
            erps,
            [
                (
                    "assets/resources/1.erp",
                    "mods (b)",
                    vec![
                        "eaid://shared".to_owned(),
                        "eaid://three".to_owned(),
                        "eaid://two".to_owned()
                    ]
                ),
                (
                    "assets/resources/0.erp",
                    "mods (a)",
                    vec!["eaid://one".to_owned()]
                ),
            ]
        );
        let targets: Vec<&Vec<String>> = rt.assets.resource_erps.iter().map(|e| &e.erps).collect();
        assert_eq!(
            targets,
            [
                &vec!["ui/a.erp".to_owned(), "ui/boot.erp".to_owned()],
                &vec!["ui/a.erp".to_owned()]
            ]
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn patches_layers_and_writes_only_changed_archives() {
        let game = std::env::temp_dir().join(format!("swofty-erpbuild-{}", std::process::id()));
        let wep = game.join("tracks/t/wep");
        std::fs::create_dir_all(&wep).unwrap();
        let world = erp(&[
            ("eaid://t-a.layer", layer(&["a$PitBox12"])),
            ("eaid://t-b.layer", layer(&["b$X"])),
        ]);
        std::fs::write(wep.join("t_world.erp"), &world).unwrap();
        std::fs::write(
            wep.join("t_common.erp"),
            erp(&[("eaid://t-c.layer", layer(&["c$GridSlot23"]))]),
        )
        .unwrap();
        let ops = erp_ops(
            r#"{"schema_version":1,"ops":[
            {"op":"patch-pkg","erp":"tracks/t/wep/t_world.erp","resource":"eaid://t-a.layer","edits":[
              {"op":"clone-entry","at":"instances","src":"[name=a$PitBox12]","as":"a$PitBox13","set":{"id":13}},
              {"op":"clone-entry","at":"instances","from":{"erp":"tracks/t/wep/t_common.erp","resource":"eaid://t-c.layer"},
               "src":"[name=c$GridSlot23]","as":"a$GridSlot25","set":{"id":25}}]},
            {"op":"patch-pkg","erp":"tracks/t/wep/t_common.erp","resource":"eaid://t-c.layer","edits":[]},
            {"op":"warn","message":"room for 26 cars"}]}"#,
        );
        let original = |rel: &str| game.join(rel);
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &ops, &game).unwrap();
        assert_eq!(b.changed().collect::<Vec<_>>(), ["tracks/t/wep/t_world.erp"]);
        let out = game.join("out");
        assert_eq!(b.write(&out).unwrap(), ["tracks/t/wep/t_world.erp"]);
        let written = Erp::parse(&std::fs::read(out.join("tracks/t/wep/t_world.erp")).unwrap()).unwrap();
        let res = written.find("eaid://t-a.layer").unwrap();
        assert_eq!(res.content_hash().unwrap(), res.hash);
        let pkg = Pkg::parse(&res.fragments[0].data().unwrap()).unwrap();
        let names: Vec<String> = pkg
            .root
            .as_obj()
            .unwrap()
            .get("instances")
            .unwrap()
            .as_arr()
            .unwrap()
            .items
            .iter()
            .map(|n| {
                n.as_obj()
                    .unwrap()
                    .get("name")
                    .unwrap()
                    .as_val()
                    .unwrap()
                    .as_str()
                    .unwrap()
                    .to_owned()
            })
            .collect();
        assert_eq!(names, ["a$PitBox12", "a$PitBox13", "a$GridSlot25"]);
        let mut whole = Erp::parse(&world).unwrap();
        let patched = res.fragments[0].data().unwrap();
        whole
            .find_mut("eaid://t-a.layer")
            .unwrap()
            .set_fragment(0, &patched)
            .unwrap();
        assert_eq!(
            whole.to_bytes(),
            std::fs::read(out.join("tracks/t/wep/t_world.erp")).unwrap()
        );
        let untouched = Erp::parse(&world).unwrap();
        assert_eq!(
            written.find("eaid://t-b.layer").unwrap().fragments[0].raw,
            untouched.find("eaid://t-b.layer").unwrap().fragments[0].raw
        );
        let mut bad = ErpBuild::new(&original);
        let text = erp_ops(
            r#"{"schema_version":1,"ops":[{"op":"patch-pkg","erp":"x.erp","resource":"r","edits":[
            {"op":"set-text","node":"n","value":"v"}]}]}"#,
        );
        assert_eq!(unsupported(&text[0]), Some("set-text"));
        assert!(apply(&mut bad, &text, &game).is_err());
        let escape = erp_ops(
            r#"{"schema_version":1,"ops":[{"op":"patch-pkg","erp":"../x.erp","resource":"r","edits":[
            {"op":"set-value","path":"a","value":1}]}]}"#,
        );
        assert!(apply(&mut bad, &escape, &game).is_err());
        std::fs::remove_dir_all(&game).ok();
    }

    #[test]
    fn copies_resources_from_game_and_mod_archives() {
        let root = std::env::temp_dir().join(format!("swofty-erpcopy-{}", std::process::id()));
        let game = root.join("game");
        let addon_dir = root.join("addon");
        std::fs::create_dir_all(game.join("cars")).unwrap();
        std::fs::create_dir_all(addon_dir.join("erp")).unwrap();
        std::fs::write(
            game.join("cars/slot.erp"),
            erp(&[("eaid://slot-a.layer", layer(&["a"]))]),
        )
        .unwrap();
        std::fs::write(
            game.join("cars/stock.erp"),
            erp(&[("eaid://stock-b.layer", layer(&["b"]))]),
        )
        .unwrap();
        let src = erp(&[
            ("eaid://car.wep", layer(&["w"])),
            ("eaid://car-model.layer", layer(&["m"])),
        ]);
        std::fs::write(addon_dir.join("erp/src.erp"), &src).unwrap();
        let ops = erp_ops(
            r#"{"schema_version":1,"ops":[
            {"op":"copy-resource","source":"erp/src.erp","erp":"cars/slot.erp","resource":"eaid://car.wep","as":"eaid://car_alt.wep"},
            {"op":"copy-resource","source":"erp/src.erp","erp":"cars/slot.erp","resource":"eaid://car-model.layer"},
            {"op":"copy-resource","from_erp":"cars/stock.erp","erp":"cars/slot.erp","resource":"eaid://stock-b.layer"},
            {"op":"patch-pkg","erp":"cars/slot.erp","resource":"eaid://car_alt.wep","edits":[
              {"op":"set-value","path":"instances[0].name","value":"w_alt"}]}]}"#,
        );
        assert!(ops.iter().all(|op| unsupported(op).is_none()));
        let named: Vec<String> = ops
            .iter()
            .flat_map(|op| {
                AssetOp::Erp(op.clone())
                    .sources()
                    .into_iter()
                    .map(|s| s.path.clone())
                    .collect::<Vec<_>>()
            })
            .collect();
        assert_eq!(named, ["erp/src.erp", "erp/src.erp"]);
        let original = |rel: &str| game.join(rel);
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &ops, &addon_dir).unwrap();
        let out = root.join("out");
        assert_eq!(b.write(&out).unwrap(), ["cars/slot.erp"]);
        let written = Erp::parse(&std::fs::read(out.join("cars/slot.erp")).unwrap()).unwrap();
        let names: Vec<&str> = written.resources.iter().map(|r| r.name.as_str()).collect();
        assert_eq!(
            names,
            [
                "eaid://slot-a.layer",
                "eaid://car_alt.wep",
                "eaid://car-model.layer",
                "eaid://stock-b.layer"
            ]
        );
        let source = Erp::parse(&src).unwrap();
        let copied = written.find("eaid://car-model.layer").unwrap();
        assert_eq!(
            copied.fragments[0].raw,
            source.find("eaid://car-model.layer").unwrap().fragments[0].raw
        );
        assert_eq!(copied.hash, copied.content_hash().unwrap());
        let wep = written.find("eaid://car_alt.wep").unwrap();
        assert_eq!(wep.hash, wep.content_hash().unwrap());
        let pkg = Pkg::parse(&wep.fragments[0].data().unwrap()).unwrap();
        let name = pkg
            .root
            .as_obj()
            .unwrap()
            .get("instances")
            .unwrap()
            .as_arr()
            .unwrap()
            .items[0]
            .as_obj()
            .unwrap()
            .get("name")
            .unwrap()
            .as_val()
            .unwrap()
            .as_str()
            .unwrap()
            .to_owned();
        assert_eq!(name, "w_alt");
        let twice = erp_ops(
            r#"{"schema_version":1,"ops":[
            {"op":"copy-resource","source":"erp/src.erp","erp":"cars/slot.erp","resource":"eaid://car.wep"},
            {"op":"copy-resource","source":"erp/src.erp","erp":"cars/slot.erp","resource":"eaid://car.wep"}]}"#,
        );
        assert!(apply(&mut ErpBuild::new(&original), &twice, &addon_dir).is_err());
        let both = erp_ops(
            r#"{"schema_version":1,"ops":[
            {"op":"copy-resource","source":"erp/src.erp","from_erp":"cars/stock.erp","erp":"cars/slot.erp","resource":"eaid://car.wep"}]}"#,
        );
        assert!(apply(&mut ErpBuild::new(&original), &both, &addon_dir).is_err());
        let escape = erp_ops(
            r#"{"schema_version":1,"ops":[
            {"op":"copy-resource","source":"../game/cars/stock.erp","erp":"cars/slot.erp","resource":"eaid://stock-b.layer"}]}"#,
        );
        assert!(apply(&mut ErpBuild::new(&original), &escape, &addon_dir).is_err());
        let put = erp_ops(
            r#"{"schema_version":1,"ops":[
            {"op":"put-resource","source":"erp/src.erp","erp":"cars/slot.erp","resource":"eaid://car.wep","as":"eaid://slot-a.layer"},
            {"op":"put-resource","source":"erp/src.erp","erp":"cars/slot.erp","resource":"eaid://car-model.layer"}]}"#,
        );
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &put, &addon_dir).unwrap();
        let out = root.join("put");
        b.write(&out).unwrap();
        let written = Erp::parse(&std::fs::read(out.join("cars/slot.erp")).unwrap()).unwrap();
        let names: Vec<&str> = written.resources.iter().map(|r| r.name.as_str()).collect();
        assert_eq!(names, ["eaid://slot-a.layer", "eaid://car-model.layer"]);
        assert_eq!(
            written.resources[0].fragments[0].raw,
            source.find("eaid://car.wep").unwrap().fragments[0].raw
        );
        std::fs::remove_dir_all(&root).ok();
    }

    fn surface(name: &str, head: &[u8], pixels: &[u8]) -> Resource {
        let fragment = |compression: u8, data: &[u8]| Fragment {
            name: *b"temp",
            offset: 0,
            size: data.len() as u64,
            flags: 0x10,
            compression,
            packed: 0,
            raw: match compression {
                ZSTD => compress(data).unwrap(),
                _ => data.to_vec(),
            },
        };
        let mut res = Resource {
            name: name.into(),
            kind: erp::kind("GfxSurfaceRes").unwrap(),
            unk: 9u32.to_le_bytes().to_vec(),
            fragments: vec![fragment(ZSTD, head), fragment(STORED, pixels)],
            hash: [0; 16],
        };
        for f in &mut res.fragments {
            f.packed = f.raw.len() as u64;
        }
        res.hash = res.content_hash().unwrap();
        res
    }

    fn archive(resources: Vec<Resource>) -> Vec<u8> {
        let mut header = [0u8; HEADER_LEN];
        header[..4].copy_from_slice(MAGIC);
        header[4..8].copy_from_slice(&3u32.to_le_bytes());
        Erp {
            header,
            version: 3,
            resources,
        }
        .to_bytes()
    }

    fn data(res: &Resource) -> Vec<Vec<u8>> {
        res.fragments.iter().map(|f| f.data().unwrap()).collect()
    }

    #[test]
    fn an_unchanged_archive_is_linked_from_the_cache() {
        let root = std::env::temp_dir().join(format!("swofty-erpcache-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let game = root.join("game");
        let addon = root.join("addon");
        std::fs::create_dir_all(game.join("ui")).unwrap();
        std::fs::create_dir_all(addon.join("raw")).unwrap();
        let x = "eaid://ui_package/x.tif.image";
        std::fs::write(
            game.join("ui/a.erp"),
            archive(vec![surface(x, b"HEADER-X", b"PIXELS-X")]),
        )
        .unwrap();
        let put = |bytes: &[u8]| {
            std::fs::write(addon.join("raw/pixels.bin"), bytes).unwrap();
            erp_ops(
                r#"{"schema_version":1,"ops":[{"op":"replace-resource","erp":"ui/a.erp",
                "resource":"eaid://ui_package/x.tif.image","fragment":1,"source":"raw/pixels.bin"}]}"#,
            )
        };
        let original = |rel: &str| game.join(rel);
        let cache = root.join("cache");
        let build = |n: &str, bytes: &[u8]| {
            let ops = put(bytes);
            let mut b = ErpBuild::new(&original);
            apply(&mut b, &ops, &addon).unwrap();
            let out = root.join(n);
            let (written, reused) = b.write_cached(&out, Some(&cache), &|| Ok(())).unwrap();
            assert_eq!(written, ["ui/a.erp"]);
            (std::fs::read(out.join("ui/a.erp")).unwrap(), reused)
        };
        let (first, reused) = build("one", b"PAINTED!");
        assert_eq!(reused, 0);
        let (again, reused) = build("two", b"PAINTED!");
        assert_eq!(
            (again == first, reused),
            (true, 1),
            "the same build links the cached archive"
        );
        let (other, reused) = build("three", b"REPAINT!");
        assert_eq!(
            (other == first, reused),
            (false, 0),
            "different fragment bytes write a new archive"
        );
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &put(b"PAINTED!"), &addon).unwrap();
        b.write(&root.join("plain")).unwrap();
        assert_eq!(
            std::fs::read(root.join("plain/ui/a.erp")).unwrap(),
            first,
            "the cache changes no bytes"
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn replaces_adds_and_removes_resources_with_the_same_bytes_on_both_routes() {
        let root = std::env::temp_dir().join(format!("swofty-erpraw-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let game = root.join("game");
        let addon = root.join("addon");
        std::fs::create_dir_all(game.join("ui")).unwrap();
        std::fs::create_dir_all(addon.join("raw")).unwrap();
        let (x, y, z) = (
            "eaid://ui_package/x.tif.image",
            "eaid://ui_package/y.tif.image",
            "eaid://ui_package/z.tif.image",
        );
        let mut l = Erp::parse(&erp(&[("eaid://ui_package/l.layer", layer(&["a"]))]))
            .unwrap()
            .resources
            .remove(0);
        l.unk = 1u32.to_le_bytes().to_vec();
        let stock = archive(vec![
            surface(x, b"HEADER-X", b"PIXELS-X"),
            surface(y, b"HEADER-Y", b"PIXELS-Y"),
            l,
        ]);
        std::fs::write(game.join("ui/a.erp"), &stock).unwrap();
        std::fs::write(addon.join("raw/pixels.bin"), b"PAINTED!").unwrap();
        std::fs::write(addon.join("raw/head.bin"), b"HEADER-Z").unwrap();
        std::fs::write(addon.join("raw/z.bin"), b"NEW PIXELS").unwrap();
        let replace_add = r#"
            {"op":"replace-resource","erp":"ui/a.erp","resource":"eaid://ui_package/x.tif.image","fragment":1,"source":"raw/pixels.bin"},
            {"op":"add-resource","erp":"ui/a.erp","resource":"eaid://ui_package/z.tif.image","type":"GfxSurfaceRes",
             "sources":["raw/head.bin","raw/z.bin"]}"#;
        let ops = erp_ops(&format!(
            r#"{{"schema_version":1,"ops":[{replace_add},
            {{"op":"remove-resource","erp":"ui/a.erp","resource":"eaid://ui_package/y.tif.image"}}]}}"#
        ));
        assert!(ops.iter().all(|op| unsupported(op).is_none()));
        let original = |rel: &str| game.join(rel);
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &ops, &addon).unwrap();
        let out = root.join("build");
        assert_eq!(b.write(&out).unwrap(), ["ui/a.erp"]);
        let built = std::fs::read(out.join("ui/a.erp")).unwrap();

        let mut expected = Erp::parse(&stock).unwrap();
        expected
            .find_mut(x)
            .unwrap()
            .set_fragment(1, b"PAINTED!")
            .unwrap();
        let like = expected.find(x).unwrap().clone();
        expected.resources.retain(|r| r.name != y);
        expected
            .resources
            .push(Resource::like(z, &like, &[b"HEADER-Z".to_vec(), b"NEW PIXELS".to_vec()]).unwrap());
        assert_eq!(built, expected.to_bytes());

        let written = Erp::parse(&built).unwrap();
        let names: Vec<&str> = written.resources.iter().map(|r| r.name.as_str()).collect();
        assert_eq!(names, [x, "eaid://ui_package/l.layer", z]);
        let before = Erp::parse(&stock).unwrap();
        let (old, new) = (before.find(x).unwrap(), written.find(x).unwrap());
        assert_eq!(new.fragments[0].raw, old.fragments[0].raw);
        assert_eq!(new.fragments[1].raw, b"PAINTED!");
        assert_eq!(new.fragments[1].compression, STORED);
        assert_eq!(new.hash, new.content_hash().unwrap());
        assert_ne!(new.hash, old.hash);
        let added = written.find(z).unwrap();
        assert_eq!((added.kind(), &added.unk), ("GfxSurfaceRes".into(), &old.unk));
        assert_eq!(data(added), [b"HEADER-Z".to_vec(), b"NEW PIXELS".to_vec()]);
        assert_eq!(
            added
                .fragments
                .iter()
                .map(|f| (f.name, f.flags, f.compression))
                .collect::<Vec<_>>(),
            old.fragments
                .iter()
                .map(|f| (f.name, f.flags, f.compression))
                .collect::<Vec<_>>()
        );
        assert_eq!(added.hash, added.content_hash().unwrap());

        let runtime_ops = erp_ops(&format!(r#"{{"schema_version":1,"ops":[{replace_add}]}}"#));
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &runtime_ops, &addon).unwrap();
        let overlay = root.join("overlay");
        let mut rt = Runtime::new(Vec::new());
        assert!(
            rt.resources(b.resources(), b.origins(), &[], &overlay)
                .unwrap()
                .is_empty()
        );
        let rt = rt.finish(&overlay).unwrap().unwrap();
        assert_eq!(rt.assets.resource_erps.len(), 1);
        assert_eq!(rt.assets.resource_erps[0].erps, ["ui/a.erp"]);
        let lay = Erp::parse(
            &std::fs::read(overlay.join("swofty").join(&rt.assets.resource_erps[0].file)).unwrap(),
        )
        .unwrap();
        let names: Vec<&str> = lay.resources.iter().map(|r| r.name.as_str()).collect();
        assert_eq!(names, [x, z]);
        for r in &lay.resources {
            let b = written.find(&r.name).unwrap();
            assert_eq!((r.kind, &r.unk, r.hash), (b.kind, &b.unk, b.hash));
            for (f, g) in r.fragments.iter().zip(&b.fragments) {
                assert_eq!(
                    (f.name, f.size, f.flags, f.compression, f.packed, &f.raw),
                    (g.name, g.size, g.flags, g.compression, g.packed, &g.raw)
                );
            }
        }

        let fails = |json: &str| {
            let ops = erp_ops(&format!(r#"{{"schema_version":1,"ops":[{json}]}}"#));
            apply(&mut ErpBuild::new(&original), &ops, &addon)
                .unwrap_err()
                .to_string()
        };
        assert_eq!(
            fails(
                r#"{"op":"replace-resource","erp":"ui/a.erp","resource":"eaid://ui_package/w","source":"raw/head.bin"}"#
            ),
            "ui/a.erp: eaid://ui_package/w: not found"
        );
        assert_eq!(
            fails(&format!(
                r#"{{"op":"replace-resource","erp":"ui/a.erp","resource":"{x}","fragment":2,"source":"raw/head.bin"}}"#
            )),
            format!("ui/a.erp: {x}: no fragment 2")
        );
        assert!(
            fails(&format!(
                r#"{{"op":"replace-resource","erp":"ui/a.erp","resource":"{x}","source":"raw/missing.bin"}}"#
            ))
            .contains("missing.bin")
        );
        assert_eq!(
            fails(&format!(
                r#"{{"op":"add-resource","erp":"ui/a.erp","resource":"{y}","type":"GfxSurfaceRes","sources":["raw/head.bin","raw/z.bin"]}}"#
            )),
            format!("ui/a.erp: {y} already exists; put-resource replaces a resource")
        );
        assert!(
            fails(&format!(
                r#"{{"op":"add-resource","erp":"ui/a.erp","resource":"{z}","type":"GfxSurfaceRes","sources":["raw/head.bin"]}}"#
            ))
            .contains("takes its layout from a GfxSurfaceRes resource with 1 fragment(s)")
        );
        assert!(
            fails(&format!(
                r#"{{"op":"add-resource","erp":"ui/a.erp","resource":"{z}","type":"GfxMeshRes","sources":["raw/head.bin"]}}"#
            ))
            .contains("GfxMeshRes")
        );
        assert!(
            fails(&format!(
                r#"{{"op":"add-resource","erp":"ui/a.erp","resource":"{z}","type":"SixteenBytesLong","sources":["raw/head.bin"]}}"#
            ))
            .contains("is not a resource type")
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn removing_a_resource_twice_is_idempotent() {
        let root = std::env::temp_dir().join(format!("swofty-erpremove-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(game.join("ui")).unwrap();
        std::fs::create_dir_all(&addon).unwrap();
        std::fs::write(
            game.join("ui/a.erp"),
            erp(&[("eaid://one", layer(&["a"])), ("eaid://two", layer(&["b"]))]),
        )
        .unwrap();
        let ops = erp_ops(
            r#"{"schema_version":1,"ops":[
            {"op":"remove-resource","erp":"ui/a.erp","resource":"eaid://two"},
            {"op":"remove-resource","erp":"ui/a.erp","resource":"eaid://two"}]}"#,
        );
        let original = |rel: &str| game.join(rel);
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &ops, &addon).unwrap();
        assert_eq!(b.take_notes(), ["ui/a.erp: eaid://two already removed"]);
        let out = root.join("out");
        assert_eq!(b.write(&out).unwrap(), ["ui/a.erp"]);
        let written = Erp::parse(&std::fs::read(out.join("ui/a.erp")).unwrap()).unwrap();
        let names: Vec<&str> = written.resources.iter().map(|r| r.name.as_str()).collect();
        assert_eq!(names, ["eaid://one"]);
        std::fs::remove_dir_all(&root).ok();
    }

    fn scene(uri: &str, tiles: &[&str]) -> Vec<u8> {
        let id = component_id(uri.strip_suffix(".idf").unwrap());
        let named = |n: &str| Node::Obj(Obj::new(vec![(Val::str("name"), Node::Val(Val::str(n)))]));
        let root = Obj::new(vec![
            (Val::str("name"), Node::Val(Val::str("Scene Root"))),
            (
                Val::str("children"),
                Node::Arr(Arr::new(tiles.iter().map(|t| named(t)).collect())),
            ),
        ]);
        let entry = Obj::new(vec![
            (Val::str("scene"), Node::Obj(root)),
            (Val::str("name"), Node::Val(Val::raw(*b"ui64", id.to_le_bytes()))),
        ]);
        Pkg {
            name: [0; 4],
            root: Node::Obj(Obj::new(vec![(
                Val::str("scenelist"),
                Node::Arr(Arr::new(vec![Node::Obj(entry)])),
            )])),
            vca: Vec::new(),
            layout: None,
        }
        .to_bytes()
        .unwrap()
    }

    #[test]
    fn a_resized_scene_rewrites_its_archive_instead_of_the_runtime_overlay() {
        let root = std::env::temp_dir().join(format!("swofty-erpscene-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(game.join("ui")).unwrap();
        std::fs::create_dir_all(addon.join("ui")).unwrap();
        let uri = "eaid://ui_package/art/screens/career/rnd_overview.idf";
        let other = "eaid://ui_package/art/screens/career/rnd_dept.idf";
        let four = scene(uri, &["tile_1", "tile_2", "tile_3", "tile_4"]);
        let five = scene(uri, &["tile_1", "tile_2", "tile_3", "tile_4", "tile_5"]);
        let stock = typed_erp(
            "UILayout",
            &[(uri, four.clone()), (other, scene(other, &["dept"]))],
        );
        std::fs::write(game.join("ui/hub.erp"), &stock).unwrap();
        let side = "eaid://ui_package/art/screens/career/rnd_side.idf";
        std::fs::write(
            game.join("ui/hq.erp"),
            typed_erp("UILayout", &[(side, scene(side, &["tile_a"]))]),
        )
        .unwrap();
        std::fs::write(addon.join("ui/side.idf"), scene(side, &["tile_b"])).unwrap();
        std::fs::write(addon.join("ui/same.idf"), &four).unwrap();
        std::fs::write(addon.join("ui/five.idf"), &five).unwrap();
        std::fs::write(addon.join("ui/dept.idf"), scene(other, &["dept", "dept_5"])).unwrap();
        std::fs::write(addon.join("ui/layer.bin"), layer(&["a"])).unwrap();
        std::fs::write(addon.join("ui/text.txt"), b"not a scene").unwrap();
        let replace = |erp: &str, res: &str, src: &str| {
            format!(r#"{{"op":"replace-resource","erp":"{erp}","resource":"{res}","source":"ui/{src}"}}"#)
        };
        let ops = |list: &[String]| erp_ops(&format!(r#"{{"schema_version":1,"ops":[{}]}}"#, list.join(",")));
        let original = |rel: &str| game.join(rel);

        let mut b = ErpBuild::new(&original);
        apply(&mut b, &ops(&[replace("ui/hub.erp", uri, "same.idf")]), &addon).unwrap();
        assert!(b.resized().is_empty());
        assert_eq!(b.resources().count(), 1);

        let mut b = ErpBuild::new(&original);
        apply(
            &mut b,
            &ops(&[
                replace("ui/hub.erp", uri, "five.idf"),
                replace("ui/hq.erp", side, "side.idf"),
            ]),
            &addon,
        )
        .unwrap();
        let why = format!(
            "fragment 0 is {} bytes, the stock one {}; stock archives that bind to the replacement read it with the stock layout",
            five.len(),
            four.len()
        );
        let expected = vec![Resized {
            erp: "ui/hub.erp".into(),
            resources: vec![(uri.into(), why)],
        }];
        assert_eq!(b.resized(), expected);
        let out = root.join("out");
        assert_eq!(b.take_resized(&out).unwrap(), expected);
        let left: Vec<(&str, &str)> = b.resources().map(|(e, r, _, _)| (e, r.name.as_str())).collect();
        assert_eq!(left, [("ui/hq.erp", side)]);
        assert_eq!(b.write(&root.join("rest")).unwrap(), ["ui/hq.erp"]);

        let built = std::fs::read(out.join("ui/hub.erp")).unwrap();
        let written = Erp::parse(&built).unwrap();
        let before = Erp::parse(&stock).unwrap();
        let res = written.find(uri).unwrap();
        assert_eq!(res.fragments[0].data().unwrap(), five);
        assert_eq!(res.fragments[0].size, five.len() as u64);
        assert_eq!(res.hash, res.content_hash().unwrap());
        assert_eq!(
            (res.kind, &res.unk),
            (before.find(uri).unwrap().kind, &before.find(uri).unwrap().unk)
        );
        assert_eq!(
            written.find(other).unwrap().fragments[0].raw,
            before.find(other).unwrap().fragments[0].raw
        );
        let mut again = Erp::parse(&built).unwrap();
        assert_eq!(again.to_bytes(), built);

        let fails = |src: &str, res: &str| {
            apply(
                &mut ErpBuild::new(&original),
                &ops(&[replace("ui/hub.erp", res, src)]),
                &addon,
            )
            .unwrap_err()
            .to_string()
        };
        assert_eq!(
            fails("dept.idf", uri),
            format!(
                "ui/hub.erp: {uri}: the source is scene {:#018x}, this resource is scene {:#018x}; it was saved from another .idf",
                component_id(other.strip_suffix(".idf").unwrap()),
                component_id(uri.strip_suffix(".idf").unwrap())
            )
        );
        for src in ["layer.bin", "text.txt"] {
            assert!(
                fails(src, uri).ends_with(
                    "the source is not a scene: a UILayout resource holds a !pkg with a scenelist"
                )
            );
        }
        apply(
            &mut ErpBuild::new(&original),
            &ops(&[replace("ui/hub.erp", other, "dept.idf")]),
            &addon,
        )
        .unwrap();
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn add_mipmaps_places_files_and_folders_at_game_paths() {
        let root = std::env::temp_dir().join(format!("swofty-erpmips-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let addon = root.join("addon");
        std::fs::create_dir_all(addon.join("mips/sub")).unwrap();
        std::fs::write(addon.join("mips/a.tif.mipmaps"), b"a").unwrap();
        std::fs::write(addon.join("mips/sub/b.tif.mipmaps"), b"b").unwrap();
        std::fs::write(addon.join("one.tif.mipmaps"), b"1").unwrap();
        let ops = erp_ops(
            r#"{"schema_version":1,"ops":[
            {"op":"add-mipmaps","source":"mips/","target":"livery_package/textures/"},
            {"op":"add-mipmaps","source":"one.tif.mipmaps","target":"livery_package/one.tif.mipmaps"}]}"#,
        );
        assert!(ops.iter().all(|op| unsupported(op).is_none()));
        let original = |rel: &str| root.join("game").join(rel);
        let mut b = ErpBuild::new(&original);
        apply(&mut b, &ops, &addon).unwrap();
        assert_eq!(b.changed().count(), 0);
        assert_eq!(
            b.take_mipmaps(),
            BTreeMap::from([
                (
                    "livery_package/one.tif.mipmaps".to_owned(),
                    addon.join("one.tif.mipmaps")
                ),
                (
                    "livery_package/textures/a.tif.mipmaps".to_owned(),
                    addon.join("mips/").join("a.tif.mipmaps")
                ),
                (
                    "livery_package/textures/sub/b.tif.mipmaps".to_owned(),
                    addon.join("mips/").join("sub/b.tif.mipmaps")
                ),
            ])
        );
        for bad in [
            r#"{"op":"add-mipmaps","source":"mips","target":"../outside"}"#,
            r#"{"op":"add-mipmaps","source":"missing","target":"x"}"#,
        ] {
            let ops = erp_ops(&format!(r#"{{"schema_version":1,"ops":[{bad}]}}"#));
            assert!(apply(&mut ErpBuild::new(&original), &ops, &addon).is_err());
        }
        std::fs::remove_dir_all(&root).ok();
    }

    const LOGOS: &str =
        "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.team_logos.f1_2026.texarray.image";
    const UI: &str = "2025_asset_groups/ui_package/common_front_end.erp";
    const BOOT: &str = "2025_asset_groups/vfx_package/common.erp";

    fn array(width: u32, slices: &[[u8; 4]]) -> Resource {
        let mut head = vec![0u8; 40];
        head[12..16].copy_from_slice(&width.to_le_bytes());
        head[16..20].copy_from_slice(&2u32.to_le_bytes());
        head[28..32].copy_from_slice(&(slices.len() as u32).to_le_bytes());
        let fragment = |data: Vec<u8>| Fragment {
            name: *b"temp",
            offset: 0,
            size: data.len() as u64,
            flags: 0x10,
            compression: STORED,
            packed: data.len() as u64,
            raw: data,
        };
        let mut r = Resource {
            name: LOGOS.into(),
            kind: erp::kind("GfxSurfaceRes").unwrap(),
            unk: vec![0; 4],
            fragments: vec![fragment(head), fragment(slices.concat())],
            hash: [0; 16],
        };
        r.hash = r.content_hash().unwrap();
        r
    }

    fn texture_erp(resources: Vec<Resource>) -> Vec<u8> {
        let mut header = [0u8; HEADER_LEN];
        header[..4].copy_from_slice(MAGIC);
        header[4..8].copy_from_slice(&3u32.to_le_bytes());
        Erp {
            header,
            version: 3,
            resources,
        }
        .to_bytes()
    }

    fn slices_in(r: &Resource) -> Vec<u8> {
        r.fragments[1].data().unwrap().chunks(4).map(|c| c[0]).collect()
    }

    fn texture_game(name: &str) -> PathBuf {
        let root = std::env::temp_dir().join(format!("swofty-erpbuild-slices-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let game = root.join("game");
        for (rel, body) in [
            (
                "2025_asset_groups/ui_package/a_other.erp",
                texture_erp(vec![Resource {
                    name: "eaid://ui_package/x.image".into(),
                    ..array(2, &[[0; 4], [0; 4]])
                }]),
            ),
            (UI, texture_erp(vec![array(2, &[[0; 4], [1; 4], [2; 4], [3; 4]])])),
            (
                BOOT,
                texture_erp(vec![Resource {
                    name: "eaid://vfx_package/y.image".into(),
                    ..array(2, &[[0; 4], [0; 4]])
                }]),
            ),
        ] {
            std::fs::create_dir_all(game.join(rel).parent().unwrap()).unwrap();
            std::fs::write(game.join(rel), body).unwrap();
        }
        root
    }

    fn merge_op(erp: &str, source: &str) -> ErpOp {
        ErpOp::MergeSlices {
            erp: erp.into(),
            resource: LOGOS.into(),
            source: Source::addon(source),
        }
    }

    #[test]
    fn merges_changed_slices_over_the_array_in_place_in_load_order() {
        let root = texture_game("order");
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(&addon).unwrap();
        let mods: [(&str, Resource); 7] = [
            ("a", array(2, &[[0; 4], [9; 4], [2; 4], [3; 4]])),
            ("b", array(2, &[[0; 4], [7; 4], [2; 4], [8; 4]])),
            ("c", array(2, &[[0; 4], [1; 4], [2; 4], [3; 4]])),
            ("d", array(2, &[[6; 4], [1; 4]])),
            ("e", array(2, &[[0; 4], [1; 4], [5; 4], [3; 4], [7; 4]])),
            ("f", array(4, &[[4; 4], [4; 4]])),
            ("g", array(2, &[[0; 4], [1; 4], [2; 4], [4; 4]])),
        ];
        for (id, r) in &mods {
            std::fs::write(addon.join(format!("{id}.erp")), texture_erp(vec![r.clone()])).unwrap();
        }
        let original = |rel: &str| game.join(rel);
        let resolve = |s: &Source| Ok(s.resolve(&addon)?);
        let mut b = ErpBuild::new(&original);
        let mut seen = Vec::new();
        let mut current = Vec::new();
        for (id, _) in &mods {
            b.apply_for(&merge_op(BOOT, &format!("{id}.erp")), id, &resolve)
                .unwrap();
            let [m] = b.take_merges().try_into().unwrap();
            seen.push((m.changed, m.whole, m.notes));
            let res = b
                .resources()
                .find(|(_, r, _, _)| r.name == LOGOS)
                .map(|(_, r, _, o)| (slices_in(r), o.to_owned()));
            current.push(res);
        }
        let none: Vec<String> = Vec::new();
        assert_eq!(seen[0], (vec![1], false, none.clone()));
        assert_eq!(seen[1], (vec![1, 3], false, none.clone()));
        assert_eq!(seen[2], (vec![], false, none.clone()));
        assert_eq!(seen[3], (vec![0], false, none.clone()));
        assert_eq!(
            seen[4],
            (
                vec![2],
                false,
                vec![format!(
                    "{LOGOS} has 5 slices, the game's 4; the slices past 4 are left out"
                )]
            )
        );
        assert_eq!(
            seen[5],
            (
                vec![],
                true,
                vec![format!(
                    "{LOGOS} differs in format or slice size from the game's array; it replaces the whole resource in {BOOT}"
                )]
            )
        );
        assert_eq!(
            seen[6],
            (
                vec![3],
                false,
                vec![format!(
                    "{LOGOS} in {BOOT} has another layout from an earlier addon; the slices merge over the game's array instead"
                )]
            )
        );
        let at = |i: usize| current[i].clone().unwrap();
        assert_eq!(at(0), (vec![0, 9, 2, 3], "a".to_owned()));
        assert_eq!(at(1), (vec![0, 7, 2, 8], "b".to_owned()));
        assert_eq!(at(2), (vec![0, 7, 2, 8], "b".to_owned()));
        assert_eq!(at(4), (vec![6, 7, 5, 8], "e".to_owned()));
        assert_eq!(at(5), (vec![4, 4], "f".to_owned()));
        assert_eq!(at(6), (vec![0, 1, 2, 4], "g".to_owned()));
        assert_eq!(b.origins(), ["a", "b", "d", "e", "f", "g"]);
        assert!(b.resized().is_empty());
        let out = root.join("out");
        assert_eq!(b.write(&out).unwrap(), [BOOT]);
        let file = ErpFile::open(&out.join(BOOT)).unwrap();
        let names: Vec<&str> = file.erp.resources.iter().map(|r| r.name.as_str()).collect();
        assert_eq!(names, ["eaid://vfx_package/y.image", LOGOS]);
        let merged = file.load(file.erp.find(LOGOS).unwrap()).unwrap();
        assert_eq!(slices_in(&merged), [0, 1, 2, 4]);
        assert_eq!(
            merged.fragments[0].data().unwrap(),
            array(2, &[[0; 4]; 4]).fragments[0].data().unwrap()
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_merge_into_the_stock_archive_keeps_its_layout_for_the_runtime_route() {
        let root = texture_game("runtime");
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(&addon).unwrap();
        std::fs::write(
            addon.join("a.erp"),
            texture_erp(vec![array(2, &[[0; 4], [1; 4], [9; 4], [3; 4]])]),
        )
        .unwrap();
        std::fs::write(addon.join("wide.erp"), texture_erp(vec![array(4, &[[5; 4]; 3])])).unwrap();
        let original = |rel: &str| game.join(rel);
        let resolve = |s: &Source| Ok(s.resolve(&addon)?);
        let mut b = ErpBuild::new(&original);
        b.apply_for(&merge_op(UI, "a.erp"), "a", &resolve).unwrap();
        assert!(b.resized().is_empty());
        let mut rt = Runtime::new(Vec::new());
        let out = root.join("overlay");
        let notes = rt.resources(b.resources(), &["a".into()], &[], &out).unwrap();
        assert!(notes.is_empty(), "{notes:?}");
        let overlay = Erp::parse(&std::fs::read(out.join("swofty/assets/resources/0.erp")).unwrap()).unwrap();
        assert_eq!(slices_in(&overlay.resources[0]), [0, 1, 9, 3]);
        assert_eq!(rt.assets.resource_erps[0].erps, [UI]);

        b.apply_for(&merge_op(UI, "wide.erp"), "wide", &resolve).unwrap();
        let resized = b.resized();
        assert_eq!(resized.len(), 1);
        assert_eq!(resized[0].erp, UI);
        assert!(b.take_merges().iter().all(|m| m.slices == 4));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn kept_resources_stay_when_an_addon_replaces_them() {
        let root = std::env::temp_dir().join(format!("swofty-erpbuild-kept-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(game.join(UI).parent().unwrap()).unwrap();
        std::fs::create_dir_all(&addon).unwrap();
        let view = LOGOS.strip_suffix(".image").unwrap();
        let other = "eaid://ui_package/art/textures/team_logo.image";
        let named = |name: &str, r: Resource| Resource {
            name: name.into(),
            ..r
        };
        let built = array(2, &[[0; 4], [1; 4], [7; 4], [8; 4]]);
        std::fs::write(
            game.join(UI),
            texture_erp(vec![
                built.clone(),
                named(view, array(2, &[[4; 4]])),
                named(other, array(2, &[[0; 4]])),
            ]),
        )
        .unwrap();
        std::fs::write(
            addon.join("pack.erp"),
            texture_erp(vec![
                array(2, &[[0; 4], [1; 4], [5; 4]]),
                named(view, array(2, &[[3; 4]])),
                named(other, array(2, &[[9; 4]])),
            ]),
        )
        .unwrap();
        let original = |rel: &str| game.join(rel);
        let resolve = |s: &Source| Ok(s.resolve(&addon)?);
        let mut b = ErpBuild::new(&original);
        b.keep(UI, LOGOS);
        b.keep(UI, view);
        assert!(b.keeps(UI, LOGOS) && !b.keeps(UI, other) && !b.keeps(BOOT, LOGOS));
        let overlay = ErpOp::Overlay {
            erps: vec![UI.into()],
            source: Source::addon("pack.erp"),
        };
        b.apply_for(&overlay, "pack", &resolve).unwrap();
        let put = ErpOp::PutResource {
            erp: UI.into(),
            resource: LOGOS.into(),
            source: Source::addon("pack.erp"),
            rename: None,
        };
        b.apply_for(&put, "later", &resolve).unwrap();
        let written: Vec<(String, Vec<u8>, String)> = b
            .resources()
            .map(|(_, r, _, o)| (r.name.clone(), slices_in(r), o.to_owned()))
            .collect();
        assert_eq!(written, [(other.to_owned(), vec![9], "pack".to_owned())]);
        let taken = b.take_held();
        let shown: Vec<(&str, &str, &str)> = taken
            .iter()
            .map(|h| (h.erp.as_str(), h.resource.as_str(), h.origin.as_str()))
            .collect();
        assert_eq!(
            shown,
            [(UI, LOGOS, "pack"), (UI, view, "pack"), (UI, LOGOS, "later")]
        );
        assert_eq!(b.origins(), ["pack"]);
        assert!(b.take_held().is_empty());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn an_array_the_game_lacks_goes_in_whole() {
        let root = texture_game("missing");
        let (game, addon) = (root.join("game"), root.join("addon"));
        std::fs::create_dir_all(&addon).unwrap();
        let other = "eaid://ui_package/art/texture_arrays/new.texarray.image";
        std::fs::write(
            addon.join("a.erp"),
            texture_erp(vec![Resource {
                name: other.into(),
                ..array(2, &[[1; 4], [2; 4]])
            }]),
        )
        .unwrap();
        let original = |rel: &str| game.join(rel);
        let mut b = ErpBuild::new(&original);
        let op = ErpOp::MergeSlices {
            erp: BOOT.into(),
            resource: other.into(),
            source: Source::addon("a.erp"),
        };
        b.apply_for(&op, "a", &|s: &Source| Ok(s.resolve(&addon)?))
            .unwrap();
        let [m] = b.take_merges().try_into().unwrap();
        assert_eq!(
            (m.whole, m.slices, m.notes),
            (
                true,
                0,
                vec![format!(
                    "the game has no {other}; it replaces the whole resource in {BOOT}"
                )]
            )
        );
        let added: Vec<Vec<u8>> = b.resources().map(|(_, r, _, _)| slices_in(r)).collect();
        assert_eq!(added, [vec![1, 2]]);
        let missing = merge_op(BOOT, "none.erp");
        assert!(
            b.apply_for(&missing, "a", &|s: &Source| Ok(s.resolve(&addon)?))
                .is_err()
        );
        std::fs::remove_dir_all(&root).ok();
    }

    // A screen `page` with a node `logo`, ids fixed; `edit` changes it after.
    fn page(edit: &dyn Fn(&mut Node)) -> Vec<u8> {
        use crate::ui::node::{NodeExt, arr, fix_ids, node, ui_bytes};
        let mut scr = node("page", "UISC", vec![], vec![], &[], &[]);
        scr.a_mut().unwrap()[crate::ui::node::CHILDREN] =
            arr(vec![node("logo", "UIGP", vec![], vec![], &[], &[])]);
        fix_ids(&mut scr, None);
        edit(&mut scr);
        ui_bytes(vec![scr]).unwrap()
    }

    fn check_page_edit(name: &str, edited: Vec<u8>) -> Result<Vec<String>> {
        let root = std::env::temp_dir().join(format!("swofty-checkui-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (game, addon) = (root.join("game"), root.join("addon"));
        let (erp_rel, ui) = (
            "2025_asset_groups/ui_package/t.erp",
            "eaid://ui_package/screens/t.ui",
        );
        std::fs::create_dir_all(game.join("2025_asset_groups/ui_package")).unwrap();
        std::fs::create_dir_all(&addon).unwrap();
        std::fs::write(game.join(erp_rel), typed_erp("UIDF", &[(ui, page(&|_| {}))])).unwrap();
        std::fs::write(addon.join("t.ui"), edited).unwrap();
        let original = |rel: &str| game.join(rel);
        let resolve = |s: &Source| Ok(s.resolve(&addon)?);
        let mut b = ErpBuild::new(&original);
        let op = &erp_ops(&format!(
            r#"{{"schema_version":1,"ops":[{{"op":"replace-resource","erp":"{erp_rel}","resource":"{ui}","source":"t.ui"}}]}}"#
        ))[0];
        b.apply_for(op, "t.mod", &resolve).unwrap();
        let got = b.check_ui(&|origin| format!("Mod <{origin}>"));
        std::fs::remove_dir_all(&root).ok();
        got
    }

    #[test]
    fn a_renamed_node_that_keeps_its_hashes_warns_and_names_the_addon() {
        use crate::ui::node::NodeExt;
        let renamed = page(&|scr| {
            let logo = &mut scr.slot_mut(crate::ui::node::CHILDREN).unwrap()[0];
            crate::ui::node::rename(logo, "logo2").unwrap();
        });
        let warnings = check_page_edit("renamed", renamed).unwrap();
        let stale: Vec<&String> = warnings.iter().filter(|w| w.contains("page.logo2")).collect();
        assert_eq!(stale.len(), 2, "{warnings:?}");
        for w in stale {
            assert!(
                w.starts_with(
                    "Mod <t.mod>: 2025_asset_groups/ui_package/t.erp: eaid://ui_package/screens/t.ui: "
                ),
                "{w}"
            );
            assert!(w.contains("the game still finds it by the hash"), "{w}");
        }
    }

    #[test]
    fn a_signal_to_a_missing_node_fails_the_build_and_names_the_addon() {
        use crate::ui::node::{NodeExt, add_events, event, fix_ids, sig};
        let broken = page(&|scr| {
            let logo = &mut scr.slot_mut(crate::ui::node::CHILDREN).unwrap()[0];
            add_events(
                logo,
                vec![event("pressed", vec![sig("page.gone", "focus", "pressed")])],
            )
            .unwrap();
            fix_ids(scr, None);
        });
        let err = check_page_edit("missing", broken).unwrap_err().to_string();
        assert!(
            err.contains("UI edit(s) break rules the game's UI loader enforces"),
            "{err}"
        );
        assert!(
            err.contains("\n  Mod <t.mod>: 2025_asset_groups/ui_package/t.erp: "),
            "{err}"
        );
        assert!(err.contains("-> missing page.gone.focus"), "{err}");
    }
}
