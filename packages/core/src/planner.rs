use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::path::Path;

use serde::{Deserialize, Serialize};
use swofty_formats::gvdb::Gvdb;

use crate::asset::{AssetOp, FileKind, FileOp, GroupOp, LangOp, Op, OpFile, Origin, Source};
pub use crate::asset::{Features, Kind};
use crate::conflicts::Changes;
use crate::dbedit::deps::Copy;
use crate::entrant_lists::StockLists;
use crate::ops::{DbOps, DbReport, ErpOp, apply_db_ops_lenient, apply_db_ops_report};
use crate::owned::{self, Added};
use crate::rowkeys::{Names, RowKeys};
use crate::{Error, Result};

pub const FEATURES_FILE: &str = "swofty/addons/core/features.json";

const BUILD_DELIVERIES: [Kind; 3] = [Kind::Erp, Kind::Db, Kind::File];
const RUNTIME_DELIVERIES: [Kind; 5] = [Kind::Group, Kind::Erp, Kind::Db, Kind::Lang, Kind::File];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Route {
    Build,
    Runtime,
    Waiting,
}

#[derive(Deserialize)]
#[serde(rename_all = "lowercase")]
enum Level {
    Stable,
    Experimental,
    #[serde(other)]
    Other,
}

#[derive(Deserialize)]
struct FeaturesFile {
    #[serde(default)]
    features: BTreeMap<String, Level>,
}

pub fn load_features(loader: &Path) -> Result<Features> {
    let path = loader.join(FEATURES_FILE);
    if !path.is_file() {
        return Ok(Features::default());
    }
    let file: FeaturesFile = crate::read_json(&path)?;
    let mut out = Features::default();
    for (name, level) in file.features {
        let Ok(kind) = serde_json::from_value::<Kind>(name.into()) else {
            continue;
        };
        match level {
            Level::Stable => out.stable.insert(kind),
            Level::Experimental => out.experimental.insert(kind),
            Level::Other => false,
        };
    }
    Ok(out)
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub enum Target {
    Group(String),
    Resource { erp: String, uri: String },
    Slice { erp: String, uri: String, slice: usize },
    Record(u64),
    String { language: String, key: String },
    File(String),
}

impl std::fmt::Display for Target {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Target::Group(g) => write!(f, "asset group {g}"),
            Target::Resource { erp, uri } => write!(f, "{uri} in {erp}"),
            Target::Slice { erp, uri, slice } => write!(f, "slice {slice} of {uri} in {erp}"),
            Target::Record(key) => write!(f, "database record {key:#018x}"),
            Target::String { language, key } => write!(f, "text {key} ({language})"),
            Target::File(rel) => f.write_str(rel),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Conflict {
    pub target: Target,
    pub winner: Origin,
    pub overridden: Vec<Origin>,
}

impl std::fmt::Display for Conflict {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let lost: Vec<String> = self.overridden.iter().map(Origin::to_string).collect();
        write!(
            f,
            "{}: {} overrides {}",
            self.target,
            self.winner,
            lost.join(", ")
        )
    }
}

/// The build warning for `n` removals of stock records that addon `name` asked for.
pub fn stock_removals_line(name: &str, n: usize) -> String {
    format!(
        "{name}: {n} removal(s) of stock database records skipped; the game crashes when a record it reads is missing"
    )
}

/// The build warning for `n` removals of records the tracks add that addon `name` asked for.
pub fn track_removals_line(name: &str, n: usize) -> String {
    format!(
        "{name}: {n} removal(s) of database records the tracks add skipped; the game crashes when a record it reads is missing"
    )
}

/// The build warning for the entrant `lists` addon `name` renamed or removed, which the build kept.
pub fn kept_lists_line(name: &str, lists: &[String]) -> String {
    let shown: Vec<&str> = lists.iter().take(8).map(String::as_str).collect();
    let more = match lists.len() > shown.len() {
        true => format!(" and {} more", lists.len() - shown.len()),
        false => String::new(),
    };
    format!(
        "{name}: kept the entrant list(s) {}{more} that it renames or removes; the game crashes when a list it looks up by name is missing",
        shown.join(", ")
    )
}

#[derive(Debug, Clone, Default)]
pub struct Planned {
    pub runtime: Vec<Op>,
    pub waiting: Vec<Op>,
    pub conflicts: Vec<Conflict>,
}

pub struct Planner<'a> {
    features: &'a Features,
    opt_in: &'a BTreeSet<Kind>,
    build_only: &'a BTreeSet<Kind>,
    db: Vec<(Origin, DbOps)>,
    erp: Vec<Op>,
    files: Vec<Op>,
    tracks: Vec<Op>,
    items: Vec<Op>,
    planned: Planned,
    writers: BTreeMap<Target, Vec<Origin>>,
    // Origins in the order their first op came, which is load order.
    ranks: Vec<Origin>,
    removals: usize,
    fields: BTreeMap<String, BTreeMap<String, Vec<String>>>,
    skipped: Vec<String>,
    stock_removals: BTreeMap<String, usize>,
    track_removals: BTreeMap<String, usize>,
    lists: Option<StockLists>,
    kept_lists: BTreeMap<String, Vec<String>>,
    copies: Vec<Copy>,
    lenient: BTreeMap<String, String>,
    touched: BTreeMap<String, Touched>,
    overlays: Option<&'a OverlayNames<'a>>,
}

/// The resource names in an `erp.overlay` op's archive.
pub type OverlayNames<'a> = dyn Fn(&Origin, &Source) -> Vec<String> + 'a;

#[derive(Default)]
struct Touched {
    records: HashSet<u64>,
    other: HashSet<Target>,
}

fn erp_target(op: &ErpOp) -> Option<(Target, bool)> {
    let resource = |erp: &str, uri: &str| Target::Resource {
        erp: erp.to_owned(),
        uri: uri.to_owned(),
    };
    match op {
        ErpOp::ReplaceResource { erp, resource: r, .. } | ErpOp::RemoveResource { erp, resource: r } => {
            Some((resource(erp, r), true))
        }
        ErpOp::PutResource {
            erp,
            resource: r,
            rename,
            ..
        } => Some((resource(erp, rename.as_deref().unwrap_or(r)), true)),
        ErpOp::CopyResource {
            erp,
            resource: r,
            rename,
            ..
        } => Some((resource(erp, rename.as_deref().unwrap_or(r)), false)),
        ErpOp::AddResource { erp, resource: r, .. }
        | ErpOp::PatchPkg { erp, resource: r, .. }
        | ErpOp::SetMaterial { erp, resource: r, .. } => Some((resource(erp, r), false)),
        ErpOp::AddMipmaps { target, .. } => {
            Some((Target::File(target.trim_end_matches('/').to_owned()), true))
        }
        // Its resources come from the archive's index, through `Planner::overlays`.
        ErpOp::Overlay { .. } => None,
        // The build writes one target per slice it changes, through `Planner::replaced`.
        ErpOp::MergeSlices { .. } => None,
    }
}

fn target(op: &AssetOp) -> Option<(Target, bool)> {
    match op {
        AssetOp::Group(GroupOp::Register { group, .. }) => Some((Target::Group(group.clone()), false)),
        AssetOp::Erp(op) => erp_target(op),
        AssetOp::Lang(LangOp::Set { language, key, .. }) => Some((
            Target::String {
                language: language.clone(),
                key: key.clone(),
            },
            true,
        )),
        AssetOp::File(FileOp::Put { target, .. }) => Some((Target::File(target.clone()), true)),
        AssetOp::Db(_)
        | AssetOp::Lang(LangOp::Table { .. } | LangOp::Default { .. })
        | AssetOp::Track(_)
        | AssetOp::Item(_)
        | AssetOp::Warn { .. } => None,
    }
}

impl<'a> Planner<'a> {
    pub fn new(features: &'a Features, opt_in: &'a BTreeSet<Kind>) -> Self {
        static NONE: BTreeSet<Kind> = BTreeSet::new();
        Self {
            features,
            opt_in,
            build_only: &NONE,
            db: Vec::new(),
            erp: Vec::new(),
            files: Vec::new(),
            tracks: Vec::new(),
            items: Vec::new(),
            planned: Planned::default(),
            writers: BTreeMap::new(),
            ranks: Vec::new(),
            removals: 0,
            fields: BTreeMap::new(),
            skipped: Vec::new(),
            stock_removals: BTreeMap::new(),
            track_removals: BTreeMap::new(),
            lists: None,
            kept_lists: BTreeMap::new(),
            copies: Vec::new(),
            lenient: BTreeMap::new(),
            touched: BTreeMap::new(),
            overlays: None,
        }
    }

    pub fn overlays(mut self, names: &'a OverlayNames<'a>) -> Self {
        self.overlays = Some(names);
        self
    }

    pub fn build_only(mut self, kinds: &'a BTreeSet<Kind>) -> Self {
        self.build_only = kinds;
        self
    }

    /// Addons (id → name) whose failing database ops are skipped with a
    /// warning instead of failing the build: the DB Editor's packages.
    pub fn lenient(mut self, addons: BTreeMap<String, String>) -> Self {
        self.lenient = addons;
        self
    }

    /// Skips `id`'s failing database ops from now on: an addon whose track a
    /// build leaves out has ops on that track's rows.
    pub fn add_lenient(&mut self, id: &str, name: &str) {
        self.lenient
            .entry(id.to_owned())
            .or_insert_with(|| name.to_owned());
    }

    pub fn route(&self, kind: Kind) -> Route {
        match self.delivery(kind) {
            Route::Runtime if kind == Kind::Erp && self.removals > 0 => Route::Build,
            route => route,
        }
    }

    pub fn route_note(&self, kind: Kind) -> Option<String> {
        (self.route(kind) != self.delivery(kind)).then(|| {
            format!(
                "built into the game files; {} resource removal(s) cannot apply at runtime",
                self.removals
            )
        })
    }

    fn delivery(&self, kind: Kind) -> Route {
        let stable = self.features.stable.contains(&kind);
        let experimental = self.features.experimental.contains(&kind);
        let build = BUILD_DELIVERIES.contains(&kind);
        if build && self.build_only.contains(&kind) {
            return Route::Build;
        }
        let runtime =
            RUNTIME_DELIVERIES.contains(&kind) && (stable || (experimental && self.opt_in.contains(&kind)));
        match (runtime, build) {
            (true, _) => Route::Runtime,
            (false, true) => Route::Build,
            (false, false) => Route::Waiting,
        }
    }

    pub fn push(&mut self, op: Op) {
        self.rank(&op.origin);
        if let Some((target, replaces)) = target(&op.op) {
            self.write(target, &op.origin, replaces);
        }
        if let (AssetOp::Erp(ErpOp::Overlay { erps, source }), Some(names)) = (&op.op, self.overlays) {
            for uri in names(&op.origin, source) {
                for erp in erps {
                    let target = Target::Resource {
                        erp: erp.clone(),
                        uri: uri.clone(),
                    };
                    self.write(target, &op.origin, true);
                }
            }
        }
        if matches!(op.op, AssetOp::Erp(ErpOp::RemoveResource { .. })) {
            self.removals += 1;
        }
        if matches!(op.op, AssetOp::Track(_)) {
            self.tracks.push(op);
            return;
        }
        if matches!(op.op, AssetOp::Item(_)) {
            self.items.push(op);
            return;
        }
        let Some(kind) = op.op.kind() else {
            self.erp.push(op);
            return;
        };
        match self.route(kind) {
            Route::Runtime if !matches!(kind, Kind::Db | Kind::Erp) => self.planned.runtime.push(op),
            Route::Waiting => self.planned.waiting.push(op),
            Route::Build | Route::Runtime => match op.op {
                AssetOp::Db(db) => match self.db.last_mut() {
                    Some((origin, ops)) if *origin == op.origin => ops.ops.push(db),
                    _ => self.db.push((
                        op.origin,
                        DbOps {
                            schema_version: 1,
                            fields: BTreeMap::new(),
                            strict: false,
                            side_data: Default::default(),
                            ops: vec![db],
                        },
                    )),
                },
                AssetOp::Erp(_) => self.erp.push(op),
                _ => self.files.push(op),
            },
        }
    }

    pub fn push_file(&mut self, origin: &Origin, kind: FileKind, file: OpFile) {
        let db = file.ops.iter().any(|op| matches!(op, AssetOp::Db(_)));
        if kind == FileKind::Database || db {
            let ops = DbOps {
                schema_version: file.schema_version,
                fields: file.fields,
                strict: file.strict,
                side_data: file.side_data,
                ops: Vec::new(),
            };
            self.db.push((origin.clone(), ops));
        }
        for op in file.ops {
            self.push(Op {
                op,
                origin: origin.clone(),
            });
        }
    }

    pub fn add_fields(&mut self, addon: &str, fields: BTreeMap<String, Vec<String>>) {
        if !fields.is_empty() {
            self.fields.entry(addon.to_owned()).or_default().extend(fields);
        }
    }

    fn rank(&mut self, origin: &Origin) -> usize {
        if self.ranks.last() == Some(origin) {
            return self.ranks.len() - 1;
        }
        match self.ranks.iter().position(|o| o == origin) {
            Some(at) => at,
            None => {
                self.ranks.push(origin.clone());
                self.ranks.len() - 1
            }
        }
    }

    fn overrides(&mut self, target: &Target, winner: &Origin, lost: Vec<Origin>) {
        if lost.is_empty() {
            return;
        }
        let conflicts = &mut self.planned.conflicts;
        match conflicts.iter_mut().find(|c| c.target == *target) {
            Some(c) => {
                let before = std::mem::replace(&mut c.winner, winner.clone());
                for o in std::iter::once(before).chain(lost) {
                    if o != *winner && !c.overridden.contains(&o) {
                        c.overridden.push(o);
                    }
                }
                c.overridden.retain(|o| o != winner);
            }
            None => conflicts.push(Conflict {
                target: target.clone(),
                winner: winner.clone(),
                overridden: lost,
            }),
        }
    }

    // Splits the other writers of `target` into those before `origin` in load
    // order, which it overrides and which leave the list, and the latest after it.
    fn split(
        &mut self,
        target: &Target,
        origin: &Origin,
        keep_earlier: bool,
    ) -> (Vec<Origin>, Option<Origin>) {
        let mine = self.rank(origin);
        let ws = self.writers.get(target).cloned().unwrap_or_default();
        let (mut earlier, mut later, mut kept) = (Vec::new(), None, Vec::new());
        for w in ws {
            let rank = self.rank(&w);
            if w == *origin || rank > mine {
                if rank > mine && later.as_ref().is_none_or(|(r, _)| rank > *r) {
                    later = Some((rank, w.clone()));
                }
                kept.push(w);
            } else {
                if keep_earlier {
                    kept.push(w.clone());
                }
                earlier.push(w);
            }
        }
        if let Some(ws) = self.writers.get_mut(target) {
            *ws = kept;
        }
        (earlier, later.map(|(_, w)| w))
    }

    // A whole-resource write overrides the earlier slice writes into that
    // resource, and a slice write overrides the earlier whole-resource writes on
    // that slice, while a later write of either kind overrides it.
    fn across(&mut self, target: &Target, origin: &Origin) -> Vec<Origin> {
        let others: Vec<Target> = match target {
            Target::Slice { erp, uri, .. } => vec![Target::Resource {
                erp: erp.clone(),
                uri: uri.clone(),
            }],
            Target::Resource { erp, uri } => {
                let slice = |slice| Target::Slice {
                    erp: erp.clone(),
                    uri: uri.clone(),
                    slice,
                };
                self.writers
                    .range(slice(0)..=slice(usize::MAX))
                    .map(|(t, _)| t.clone())
                    .collect()
            }
            _ => return Vec::new(),
        };
        let mut lost = Vec::new();
        for other in others {
            let whole = matches!(other, Target::Resource { .. });
            let (earlier, later) = self.split(&other, origin, whole);
            if let Some(winner) = later {
                self.overrides(&other, &winner, vec![origin.clone()]);
            }
            lost.extend(earlier);
        }
        lost
    }

    fn write(&mut self, target: Target, origin: &Origin, replaces: bool) {
        if !self.touched.contains_key(&origin.addon) {
            self.touched.insert(origin.addon.clone(), Touched::default());
        }
        let touched = self.touched.get_mut(&origin.addon).expect("inserted above");
        match target {
            Target::Record(key) => touched.records.insert(key),
            Target::Group(_) => false,
            _ => touched.other.insert(target.clone()),
        };
        if replaces {
            let (mut lost, later) = self.split(&target, origin, false);
            for w in self.across(&target, origin) {
                if !lost.contains(&w) {
                    lost.push(w);
                }
            }
            match later {
                Some(winner) => {
                    lost.push(origin.clone());
                    self.overrides(&target, &winner, lost);
                }
                None => self.overrides(&target, origin, lost),
            }
        }
        let writers = self.writers.entry(target).or_default();
        if !writers.contains(origin) {
            writers.push(origin.clone());
        }
    }

    /// What each addon writes so far: distinct records, resources, files and strings.
    pub fn changes(&self) -> BTreeMap<String, Changes> {
        self.touched
            .iter()
            .map(|(addon, t)| {
                let mut c = Changes {
                    database: t.records.len(),
                    ..Changes::default()
                };
                for target in &t.other {
                    c.count(target);
                }
                (addon.clone(), c)
            })
            .collect()
    }

    /// Records that `origin` replaced `target` (a merged texture-array slice, say).
    pub fn replaced(&mut self, target: Target, origin: &Origin) {
        self.write(target, origin, true);
    }

    /// Records that `keeper` keeps `target`, which `lost` replaces later in load order.
    pub fn kept(&mut self, target: Target, keeper: &Origin, lost: &Origin) {
        let writers = self.writers.entry(target.clone()).or_default();
        writers.retain(|w| w != lost);
        if !writers.contains(keeper) {
            writers.push(keeper.clone());
        }
        self.overrides(&target, keeper, vec![lost.clone()]);
    }

    /// Records that `origin` puts the whole archive `erp` with a `file.put`: it
    /// writes every resource other ops write in it, and load order decides.
    pub fn put_archive(&mut self, erp: &str, origin: &Origin) {
        let inside: Vec<Target> = self
            .writers
            .keys()
            .filter(
                |t| matches!(t, Target::Resource { erp: e, .. } | Target::Slice { erp: e, .. } if e == erp),
            )
            .cloned()
            .collect();
        for target in inside {
            self.write(target, origin, true);
        }
    }

    /// The origin that writes any of `targets` last in load order.
    pub fn last_writer(&self, targets: &[Target]) -> Option<&Origin> {
        let rank = |o: &Origin| self.ranks.iter().position(|r| r == o);
        targets
            .iter()
            .filter_map(|t| self.writers.get(t))
            .flatten()
            .max_by_key(|o| rank(o))
    }

    pub fn features(&self) -> Vec<Kind> {
        self.features
            .experimental
            .intersection(self.opt_in)
            .copied()
            .collect()
    }

    pub fn has_db(&self) -> bool {
        !self.db.is_empty()
    }

    /// [`Planner::apply_db_report`] with `db` as the stock database.
    pub fn apply_db(&mut self, db: &mut Gvdb) -> Result<Vec<Added>> {
        let stock = db.records.iter().map(|r| r.key).collect();
        self.apply_db_report(db, &stock, &HashSet::new(), &mut DbReport::default())
    }

    /// Applies every addon's database ops in load order. A `remove-record` of a
    /// key in `stock` is left out unless the op allows it, and one of a key in
    /// `tracks`, the records track activation added, always; see
    /// [`Planner::take_stock_removals`] and [`Planner::take_track_removals`].
    pub fn apply_db_report(
        &mut self,
        db: &mut Gvdb,
        stock: &HashSet<u64>,
        tracks: &HashSet<u64>,
        report: &mut DbReport,
    ) -> Result<Vec<Added>> {
        let fields = std::mem::take(&mut self.fields);
        let none = BTreeMap::new();
        let mut added = Vec::new();
        for (origin, ops) in std::mem::take(&mut self.db) {
            let before = owned::counts(db);
            let names = Names {
                fields: fields.get(&origin.addon).unwrap_or(&none),
                keys: RowKeys::builtin(),
                stock: Some(stock),
                tracks: Some(tracks),
            };
            let mut ours = DbReport::default();
            let lists = self.lists.as_ref().filter(|l| l.touched(&ops));
            let was = lists.map(|l| l.before(db));
            let replaced = if let Some(name) = self.lenient.get(&origin.addon) {
                let (replaced, failed) = apply_db_ops_lenient(db, &ops, &names, &mut ours);
                self.skipped.extend(
                    failed
                        .into_iter()
                        .map(|(_, e)| format!("{name}: change skipped: {e}")),
                );
                replaced
            } else {
                apply_db_ops_report(db, &ops, &names, &mut ours)
                    .map_err(|e| Error::Other(format!("applying {origin}: {e}")))?
            };
            report
                .lines
                .extend(ours.lines.into_iter().map(|l| format!("{}: {l}", origin.addon)));
            report.warnings.extend(
                ours.warnings
                    .into_iter()
                    .map(|w| format!("{}: {w}", origin.addon)),
            );
            if ours.stock_removals > 0 {
                *self.stock_removals.entry(origin.addon.clone()).or_default() += ours.stock_removals;
            }
            if ours.track_removals > 0 {
                *self.track_removals.entry(origin.addon.clone()).or_default() += ours.track_removals;
            }
            if let (Some(lists), Some(was)) = (lists, was) {
                let kept = lists.keep(db, &was);
                if !kept.is_empty() {
                    self.kept_lists
                        .entry(origin.addon.clone())
                        .or_default()
                        .extend(kept);
                }
            }
            self.copies.extend(ours.added_rows.into_iter().map(|r| Copy {
                addon: origin.addon.clone(),
                from: r.from,
                row: r.row,
                ignore: ops.side_data.ignore.clone(),
                quiet: false,
            }));
            for key in replaced {
                self.write(Target::Record(key), &origin, true);
            }
            added.extend(owned::added(db, &origin.addon, &before));
        }
        Ok(added)
    }

    /// The addon whose write of the record `key` wins, the last in load order.
    pub fn record_writer(&self, key: u64) -> Option<&str> {
        self.writers
            .get(&Target::Record(key))?
            .iter()
            .max_by_key(|w| self.ranks.iter().position(|o| o == *w))
            .map(|w| w.addon.as_str())
    }

    /// Lenient addons' changes the last `apply_db` could not apply.
    pub fn take_skipped(&mut self) -> Vec<String> {
        std::mem::take(&mut self.skipped)
    }

    /// Per addon id, the removals of stock records the last `apply_db` left out.
    pub fn take_stock_removals(&mut self) -> BTreeMap<String, usize> {
        std::mem::take(&mut self.stock_removals)
    }

    /// Per addon id, the removals of records the tracks added that the last `apply_db` left out.
    pub fn take_track_removals(&mut self) -> BTreeMap<String, usize> {
        std::mem::take(&mut self.track_removals)
    }

    /// Keeps the game's entrant `lists` when an addon's ops rename or remove one; see [`Planner::take_kept_lists`].
    pub fn keep_entrant_lists(&mut self, lists: StockLists) {
        self.lists = Some(lists);
    }

    /// Per addon id, the entrant lists the last `apply_db` kept that the addon renamed or removed.
    pub fn take_kept_lists(&mut self) -> BTreeMap<String, Vec<String>> {
        std::mem::take(&mut self.kept_lists)
    }

    /// The `add-row` copies the last `apply_db` made, in load order.
    pub fn take_copies(&mut self) -> Vec<Copy> {
        std::mem::take(&mut self.copies)
    }

    pub fn erp_ops(&self) -> &[Op] {
        &self.erp
    }

    pub fn take_erp(&mut self) -> Vec<Op> {
        std::mem::take(&mut self.erp)
    }

    pub fn take_files(&mut self) -> Vec<Op> {
        std::mem::take(&mut self.files)
    }

    pub fn take_tracks(&mut self) -> Vec<Op> {
        std::mem::take(&mut self.tracks)
    }

    pub fn take_items(&mut self) -> Vec<Op> {
        std::mem::take(&mut self.items)
    }

    pub fn finish(self) -> Planned {
        self.planned
    }
}

pub fn waiting_lines(waiting: &[Op], features: &Features) -> Vec<String> {
    let mut counts: BTreeMap<(String, Kind), usize> = BTreeMap::new();
    for op in waiting {
        if let Some(kind) = op.op.kind() {
            *counts.entry((op.origin.to_string(), kind)).or_default() += 1;
        }
    }
    counts
        .into_iter()
        .map(|((origin, kind), n)| match features.experimental.contains(&kind) {
            true => format!(
                "{origin}: {n} {kind} op(s) wait; the runtime {kind} feature is experimental and \"runtime_assets\" in config.json does not turn it on"
            ),
            false => format!("{origin}: {n} {kind} op(s) wait for {kind} support in swofty_core"),
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::asset::Source;

    fn origin(addon: &str) -> Origin {
        Origin {
            addon: addon.into(),
            item: None,
        }
    }

    fn op(addon: &str, json: &str) -> Op {
        Op {
            op: serde_json::from_str(json).unwrap(),
            origin: origin(addon),
        }
    }

    #[test]
    fn reads_features_and_routes_each_kind() {
        let dir = std::env::temp_dir().join(format!("swofty-features-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        assert_eq!(load_features(&dir).unwrap(), Features::default());
        let path = dir.join(FEATURES_FILE);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(
            &path,
            r#"{ "schema": 1, "features": { "group": "stable", "erp": "experimental", "db": "experimental",
                 "lang": "experimental", "file": "experimental", "audio": "stable", "tex": "soon" } }"#,
        )
        .unwrap();
        let features = load_features(&dir).unwrap();
        assert_eq!(features.stable, BTreeSet::from([Kind::Group]));
        assert_eq!(features.experimental.len(), 4);
        let opt_in = BTreeSet::from([Kind::Erp, Kind::Group]);
        let p = Planner::new(&features, &opt_in);
        assert_eq!(p.route(Kind::Erp), Route::Runtime);
        assert_eq!(p.route(Kind::Db), Route::Build);
        assert_eq!(p.route(Kind::File), Route::Build);
        assert_eq!(p.route(Kind::Lang), Route::Waiting);
        assert_eq!(p.route(Kind::Group), Route::Runtime);
        assert_eq!(p.features(), [Kind::Erp]);
        assert_eq!(
            waiting_lines(
                &[op(
                    "a",
                    r#"{"op":"lang.set","language":"*","key":"k","value":"v"}"#
                )],
                &features
            ),
            [
                "a: 1 lang op(s) wait; the runtime lang feature is experimental and \"runtime_assets\" in config.json does not turn it on"
            ]
        );
        std::fs::write(
            &path,
            r#"{ "schema": 1, "features": { "group": "stable", "erp": "stable", "db": "stable", "lang": "stable", "file": "experimental" } }"#,
        )
        .unwrap();
        let shipped = load_features(&dir).unwrap();
        let none = BTreeSet::new();
        let p = Planner::new(&shipped, &none);
        for kind in [Kind::Group, Kind::Erp, Kind::Db, Kind::Lang] {
            assert_eq!(p.route(kind), Route::Runtime);
        }
        assert_eq!(p.route(Kind::File), Route::Build);
        assert!(p.features().is_empty());
        let file = BTreeSet::from([Kind::File]);
        let p = Planner::new(&shipped, &file);
        assert_eq!(p.route(Kind::File), Route::Runtime);
        assert_eq!(p.features(), [Kind::File]);
        let forced = BTreeSet::from([Kind::Erp, Kind::Db, Kind::Lang]);
        let p = Planner::new(&shipped, &none).build_only(&forced);
        assert_eq!(p.route(Kind::Erp), Route::Build);
        assert_eq!(p.route(Kind::Db), Route::Build);
        assert_eq!(p.route(Kind::Lang), Route::Runtime);
        assert_eq!(p.route(Kind::Group), Route::Runtime);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn records_conflicts_for_replacing_writes_only() {
        let features = Features::default();
        let opt_in = BTreeSet::new();
        let mut p = Planner::new(&features, &opt_in);
        let put = r#"{"op":"erp.put-resource","erp":"a.erp","resource":"r","source":"x.erp"}"#;
        let patch = r#"{"op":"erp.patch-pkg","erp":"a.erp","resource":"r","edits":[]}"#;
        p.push(op("a", put));
        p.push(op("b", patch));
        p.push(op("c", put));
        p.push(op("c", r#"{"op":"file.put","source":"f","target":"x/y.dds"}"#));
        p.push(op("d", r#"{"op":"file.put","source":"f","target":"x/y.dds"}"#));
        p.push(op(
            "a",
            r#"{"op":"lang.set","language":"*","key":"k","value":"v"}"#,
        ));
        p.push(op("a", r#"{"op":"warn","message":"m"}"#));
        assert_eq!(p.take_erp().len(), 4);
        let files = p.take_files();
        assert_eq!(files.len(), 2);
        assert!(
            matches!(&files[0].op, AssetOp::File(FileOp::Put { source, .. }) if *source == Source::addon("f"))
        );
        let planned = p.finish();
        assert_eq!(planned.waiting.len(), 1);
        assert_eq!(
            waiting_lines(&planned.waiting, &features),
            ["a: 1 lang op(s) wait for lang support in swofty_core"]
        );
        let shown: Vec<String> = planned.conflicts.iter().map(Conflict::to_string).collect();
        assert_eq!(shown, ["r in a.erp: c overrides a, b", "x/y.dds: d overrides c"]);
    }

    #[test]
    fn the_last_writer_of_a_resource_or_its_archive_comes_last_in_load_order() {
        let features = Features::default();
        let opt_in = BTreeSet::new();
        let put = r#"{"op":"erp.put-resource","erp":"a.erp","resource":"r","source":"x.erp"}"#;
        let file = |erp: &str| format!(r#"{{"op":"file.put","source":"f","target":"{erp}"}}"#);
        let both = |erp: &str| {
            [
                Target::Resource {
                    erp: erp.into(),
                    uri: "r".into(),
                },
                Target::File(erp.into()),
            ]
        };
        let mut p = Planner::new(&features, &opt_in);
        p.push(op("a", put));
        p.push(op("b", &file("b.erp")));
        p.push(op("c", put));
        assert_eq!(p.last_writer(&both("a.erp")), Some(&origin("c")));
        assert_eq!(p.last_writer(&both("b.erp")), Some(&origin("b")));
        assert_eq!(p.last_writer(&both("c.erp")), None);
        p.push(op("d", &file("a.erp")));
        assert_eq!(p.last_writer(&both("a.erp")), Some(&origin("d")));
        p.push(op("a", &file("b.erp")));
        assert_eq!(p.last_writer(&both("b.erp")), Some(&origin("b")));
    }

    #[test]
    fn whole_resource_and_slice_writes_override_each_other_by_load_order() {
        let features = Features::default();
        let opt_in = BTreeSet::new();
        let put = r#"{"op":"erp.put-resource","erp":"a.erp","resource":"r","source":"x.erp"}"#;
        let merge = r#"{"op":"erp.merge-slices","erp":"a.erp","resource":"r","source":"x.erp"}"#;
        let slice = |slice| Target::Slice {
            erp: "a.erp".into(),
            uri: "r".into(),
            slice,
        };
        let shown =
            |p: Planner| -> Vec<String> { p.finish().conflicts.iter().map(Conflict::to_string).collect() };

        let mut p = Planner::new(&features, &opt_in);
        p.push(op("a", merge));
        p.push(op("b", put));
        p.replaced(slice(1), &origin("a"));
        assert_eq!(shown(p), ["r in a.erp: b overrides a"]);

        let mut p = Planner::new(&features, &opt_in);
        p.push(op("a", put));
        p.push(op("b", merge));
        p.replaced(slice(1), &origin("b"));
        p.replaced(slice(2), &origin("b"));
        assert_eq!(
            shown(p),
            [
                "slice 1 of r in a.erp: b overrides a",
                "slice 2 of r in a.erp: b overrides a"
            ]
        );

        let mut p = Planner::new(&features, &opt_in);
        p.push(op("a", merge));
        p.replaced(slice(0), &origin("a"));
        p.push(op("b", put));
        p.push(op("c", merge));
        p.replaced(slice(0), &origin("c"));
        assert_eq!(
            shown(p),
            [
                "r in a.erp: b overrides a",
                "slice 0 of r in a.erp: c overrides b"
            ]
        );
    }

    #[test]
    fn a_resource_removal_puts_every_erp_op_on_the_build_route() {
        let features = Features {
            stable: BTreeSet::from([Kind::Erp, Kind::Db]),
            experimental: BTreeSet::new(),
        };
        let none = BTreeSet::new();
        let mut p = Planner::new(&features, &none);
        let remove = |r: &str| format!(r#"{{"op":"erp.remove-resource","erp":"a.erp","resource":"{r}"}}"#);
        p.push(op(
            "a",
            r#"{"op":"erp.put-resource","erp":"a.erp","resource":"r","source":"x.erp"}"#,
        ));
        p.push(op(
            "a",
            r#"{"op":"erp.add-mipmaps","source":"m","target":"x/r.tif.mipmaps"}"#,
        ));
        assert_eq!(p.route(Kind::Erp), Route::Runtime);
        assert_eq!(p.route_note(Kind::Erp), None);
        p.push(op("b", &remove("r")));
        p.push(op("b", &remove("s")));
        p.push(op(
            "b",
            r#"{"op":"file.put","source":"f","target":"x/r.tif.mipmaps"}"#,
        ));
        assert_eq!(p.route(Kind::Erp), Route::Build);
        assert_eq!(p.route(Kind::Db), Route::Runtime);
        assert_eq!(
            p.route_note(Kind::Erp).as_deref(),
            Some("built into the game files; 2 resource removal(s) cannot apply at runtime")
        );
        assert_eq!(p.route_note(Kind::Db), None);
        assert_eq!(p.take_erp().len(), 4);
        let shown: Vec<String> = p.finish().conflicts.iter().map(Conflict::to_string).collect();
        assert_eq!(
            shown,
            ["r in a.erp: b overrides a", "x/r.tif.mipmaps: b overrides a"]
        );
        let mut p = Planner::new(&features, &none).build_only(&features.stable);
        p.push(op("b", &remove("r")));
        assert_eq!(p.route(Kind::Erp), Route::Build);
        assert_eq!(p.route_note(Kind::Erp), None);
    }

    #[test]
    fn build_fields_apply_to_their_own_addon_only() {
        use swofty_formats::gvdb::Record;
        let features = Features::default();
        let opt_in = BTreeSet::new();
        let add = r#"{"op":"db.add-row","table":"t","from":"t.r_0"}"#;
        let db = || Gvdb {
            records: vec![
                Record::new("t._r_instancecount_", 0, "1"),
                Record::new("t.r_0.a", 0, "7"),
            ],
        };
        let mut p = Planner::new(&features, &opt_in);
        p.add_fields("a", BTreeMap::from([("t.r".to_owned(), vec!["a".to_owned()])]));
        p.push(op("a", add));
        let mut built = db();
        p.apply_db(&mut built).unwrap();
        assert_eq!(built.get_text("t.r_1.a").as_deref(), Some("7"));
        let mut p = Planner::new(&features, &opt_in);
        p.add_fields("a", BTreeMap::from([("t.r".to_owned(), vec!["a".to_owned()])]));
        p.push(op("b", add));
        let e = p.apply_db(&mut db()).unwrap_err();
        assert!(e.to_string().contains("field names unknown"), "{e}");
    }

    #[test]
    fn database_files_keep_their_schema_and_report_replaced_records() {
        let features = Features::default();
        let opt_in = BTreeSet::new();
        let mut p = Planner::new(&features, &opt_in);
        let empty: OpFile = serde_json::from_str(r#"{"schema_version":1,"ops":[]}"#).unwrap();
        p.push_file(&origin("a"), FileKind::Database, empty.clone());
        p.push_file(&origin("a"), FileKind::Assets, empty);
        assert_eq!(p.db.len(), 1);
        let key = "rules.x";
        p.push(op(
            "a",
            &format!(r#"{{"op":"db.put-record","key":"{key}","type":0,"value":"31"}}"#),
        ));
        p.push(op(
            "b",
            &format!(r#"{{"op":"db.put-record","key":"{key}","type":0,"value":"32"}}"#),
        ));
        assert_eq!(p.db.len(), 2);
        let mut db = Gvdb::default();
        p.apply_db(&mut db).unwrap();
        assert_eq!(db.get_text(key).as_deref(), Some("2"));
        let planned = p.finish();
        assert_eq!(planned.conflicts.len(), 1);
        assert_eq!(planned.conflicts[0].winner, origin("b"));
        assert_eq!(planned.conflicts[0].overridden, [origin("a")]);
    }
}
