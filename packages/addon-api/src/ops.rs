use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};

use serde::{Deserialize, Serialize};
use swofty_formats::gvdb::{Gvdb, Record, instance_count_path};
use swofty_formats::hash::{fnv1a64, fnv1a64_from};

use crate::asset::Source;
use crate::{Error, Result};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DbOps {
    pub schema_version: u32,
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub fields: BTreeMap<String, Vec<String>>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub strict: bool,
    #[serde(default, skip_serializing_if = "SideData::is_empty")]
    pub side_data: SideData,
    pub ops: Vec<DbOp>,
}

/// The file's settings for the build's side-data check of its `add-row` copies.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct SideData {
    /// Tables and fields whose side-data findings were reviewed: dotted paths
    /// with or without row numbers, `*` matching any run of characters.
    #[serde(default, deserialize_with = "patterns", skip_serializing_if = "Vec::is_empty")]
    pub ignore: Vec<String>,
}

impl SideData {
    pub fn is_empty(&self) -> bool {
        self.ignore.is_empty()
    }
}

fn patterns<'de, D: serde::Deserializer<'de>>(d: D) -> std::result::Result<Vec<String>, D::Error> {
    let v = Vec::<String>::deserialize(d)?;
    for p in &v {
        let ok = !p.is_empty()
            && p.bytes()
                .all(|b| b.is_ascii_alphanumeric() || b"_.*{}[]=-".contains(&b));
        if !ok {
            return Err(serde::de::Error::custom(format!(
                "side_data.ignore: {p:?} is not a table or field path"
            )));
        }
    }
    Ok(v)
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum CopyMode {
    All,
    Listed,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum StatIdMode {
    Fresh,
    Keep,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct DbReport {
    pub lines: Vec<String>,
    pub warnings: Vec<String>,
    /// `remove-record` ops left out because their key is a stock record.
    pub stock_removals: usize,
    /// `remove-record` ops left out because their key is a record a track added.
    pub track_removals: usize,
    /// Each `add-row` applied, in order.
    pub added_rows: Vec<AddedRow>,
}

/// An `add-row` applied: the source row's path and the new row's path.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AddedRow {
    pub from: String,
    pub row: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "op", rename_all = "kebab-case", deny_unknown_fields)]
pub enum DbOp {
    Set {
        select: String,
        field: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        value: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        value_from: Option<String>,
        #[serde(default, skip_serializing_if = "Vec::is_empty")]
        datasets: Vec<String>,
        #[serde(default)]
        create: bool,
        #[serde(default, rename = "type", skip_serializing_if = "Option::is_none")]
        tag: Option<u8>,
    },
    AddRow {
        table: String,
        from: String,
        #[serde(default)]
        set: BTreeMap<String, String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        copy: Option<CopyMode>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        stat_id: Option<StatIdMode>,
    },
    RemoveRow {
        select: String,
    },
    ListAppend {
        select: String,
        field: String,
        value: String,
    },
    ListRemove {
        select: String,
        field: String,
        value: String,
    },
    PutRecord {
        key: String,
        #[serde(rename = "type")]
        tag: u8,
        value: String,
    },
    /// Removing a key already absent is a no-op. A stock record stays unless
    /// `allow_stock_removal` is set, and a record a track added always stays;
    /// see [`FieldNames::stock`] and [`FieldNames::track`].
    RemoveRecord {
        key: String,
        #[serde(default, skip_serializing_if = "std::ops::Not::not")]
        allow_stock_removal: bool,
    },
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "op", rename_all = "kebab-case", deny_unknown_fields)]
pub enum ErpOp {
    ReplaceResource {
        erp: String,
        resource: String,
        #[serde(default)]
        fragment: usize,
        source: Source,
    },
    AddResource {
        erp: String,
        resource: String,
        #[serde(rename = "type")]
        kind: String,
        sources: Vec<Source>,
    },
    CopyResource {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        from_erp: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        source: Option<Source>,
        erp: String,
        resource: String,
        #[serde(default, rename = "as", skip_serializing_if = "Option::is_none")]
        rename: Option<String>,
    },
    PutResource {
        erp: String,
        resource: String,
        source: Source,
        #[serde(default, rename = "as", skip_serializing_if = "Option::is_none")]
        rename: Option<String>,
    },
    /// Removing a resource the archive does not have is a no-op.
    RemoveResource {
        erp: String,
        resource: String,
    },
    AddMipmaps {
        source: Source,
        target: String,
    },
    PatchPkg {
        erp: String,
        resource: String,
        #[serde(default)]
        fragment: usize,
        edits: Vec<PkgEdit>,
    },
    // A ready overlay archive: each resource in it goes into every one of `erps`, as a
    // put-resource from it would. On the runtime route the archive is loaded as it is.
    Overlay {
        erps: Vec<String>,
        source: Source,
    },
    // Each slice of the texture array `resource` in `source` that differs from the
    // game's stock slice goes over the array already in `erp`.
    MergeSlices {
        erp: String,
        resource: String,
        source: Source,
    },
    /// Sets constants of a `.material` resource in place: a number for a scalar, a list for a
    /// vector, written as the material declares them.
    SetMaterial {
        erp: String,
        resource: String,
        params: BTreeMap<String, MaterialValue>,
    },
}

/// A material constant's value: a number, a list, or text from a `{{option}}` placeholder holding
/// one or more numbers separated by spaces or commas (`true` and `false` read as 1 and 0).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum MaterialValue {
    Scalar(f64),
    Vector(Vec<MaterialNumber>),
    Text(String),
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum MaterialNumber {
    Number(f64),
    Text(String),
}

fn material_number(text: &str) -> std::result::Result<f64, String> {
    match text.trim() {
        "true" => Ok(1.0),
        "false" => Ok(0.0),
        t => t
            .parse::<f64>()
            .ok()
            .filter(|v| v.is_finite())
            .ok_or_else(|| format!("{t:?} is not a number")),
    }
}

impl MaterialValue {
    pub fn values(&self) -> std::result::Result<Vec<f64>, String> {
        match self {
            Self::Scalar(v) => Ok(vec![*v]),
            Self::Vector(v) => v
                .iter()
                .map(|n| match n {
                    MaterialNumber::Number(v) => Ok(*v),
                    MaterialNumber::Text(t) => material_number(t),
                })
                .collect(),
            Self::Text(t) => t
                .split(|c: char| c == ',' || c.is_whitespace())
                .filter(|s| !s.is_empty())
                .map(material_number)
                .collect(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct PkgPatch {
    pub schema_version: u32,
    pub targets: Vec<PkgTarget>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub root: Option<String>,
    pub edits: Vec<PkgEdit>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct PkgTarget {
    pub erp: String,
    pub resource: String,
    #[serde(default)]
    pub fragment: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Signal {
    pub to: String,
    pub pin: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub from: Option<String>,
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub attrs: BTreeMap<String, String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct EventReplacement {
    pub event: String,
    pub signals: Vec<Signal>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "op", rename_all = "kebab-case", deny_unknown_fields)]
pub enum PkgEdit {
    CloneNode {
        src: String,
        #[serde(rename = "as")]
        name: String,
        #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
        set: BTreeMap<String, serde_json::Value>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        replace_event: Option<EventReplacement>,
    },
    SetText {
        node: String,
        value: String,
        #[serde(default)]
        localise: bool,
    },
    SetProps {
        node: String,
        set: BTreeMap<String, serde_json::Value>,
    },
    AddSignal {
        node: String,
        event: String,
        signal: Signal,
    },
    RemoveSignal {
        node: String,
        event: String,
        to: String,
    },
    CloneEntry {
        at: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        from: Option<PkgTarget>,
        src: String,
        #[serde(rename = "as")]
        name: String,
        #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
        set: BTreeMap<String, serde_json::Value>,
    },
    SetValue {
        path: String,
        value: serde_json::Value,
    },
}

pub trait FieldNames {
    fn fields(&self, table: &str, row: &str) -> Option<Vec<String>>;

    fn row_keys(&self, shape: &str) -> Option<&[String]> {
        let _ = shape;
        None
    }

    /// Whether `key` is a record of the game's stock database.
    fn stock(&self, key: u64) -> bool {
        let _ = key;
        false
    }

    /// Whether `key` is a record a track's activation added to the database.
    fn track(&self, key: u64) -> bool {
        let _ = key;
        false
    }
}

/// Stock record keys and no field names.
impl FieldNames for HashSet<u64> {
    fn fields(&self, _: &str, _: &str) -> Option<Vec<String>> {
        None
    }

    fn stock(&self, key: u64) -> bool {
        self.contains(&key)
    }
}

pub const ANY_SHAPE: &str = "*";

pub fn row_shape(path: &str) -> String {
    path.split('.')
        .map(|seg| match seg.rsplit_once('_') {
            Some((word, idx)) if !idx.is_empty() && idx.bytes().all(|b| b.is_ascii_digit()) => {
                format!("{word}_N")
            }
            _ => seg.to_owned(),
        })
        .collect::<Vec<_>>()
        .join(".")
}

impl FieldNames for BTreeMap<String, Vec<String>> {
    fn fields(&self, table: &str, row: &str) -> Option<Vec<String>> {
        self.get(&format!("{table}.{row}")).cloned()
    }
}

struct Schema<'a> {
    file: &'a BTreeMap<String, Vec<String>>,
    names: &'a dyn FieldNames,
}

impl FieldNames for Schema<'_> {
    fn fields(&self, table: &str, row: &str) -> Option<Vec<String>> {
        self.file
            .fields(table, row)
            .or_else(|| self.names.fields(table, row))
    }

    fn row_keys(&self, shape: &str) -> Option<&[String]> {
        self.names.row_keys(shape)
    }

    fn stock(&self, key: u64) -> bool {
        self.names.stock(key)
    }

    fn track(&self, key: u64) -> bool {
        self.names.track(key)
    }
}

#[derive(Debug, Clone, PartialEq)]
enum Base {
    Path(String),
    Hash(u64),
}

impl Base {
    fn key(&self, suffix: &str) -> u64 {
        match self {
            Base::Path(p) => fnv1a64(format!("{p}{suffix}")),
            Base::Hash(h) => fnv1a64_from(*h, suffix),
        }
    }

    fn child(&self, name: &str) -> Base {
        match self {
            Base::Path(p) => Base::Path(format!("{p}.{name}")),
            Base::Hash(h) => Base::Hash(fnv1a64_from(*h, format!(".{name}"))),
        }
    }
}

impl std::fmt::Display for Base {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Base::Path(p) => f.write_str(p),
            Base::Hash(h) => write!(f, "{{{h:016x}}}"),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
struct Row {
    base: Base,
}

impl Row {
    fn field(&self, field: &str) -> u64 {
        self.base.key(&format!(".{field}"))
    }
}

struct Db<'a> {
    gvdb: &'a mut Gvdb,
    index: HashMap<u64, usize>,
    removed: Vec<usize>,
    added_rows: HashSet<String>,
}

impl<'a> Db<'a> {
    fn new(gvdb: &'a mut Gvdb) -> Self {
        let mut index = HashMap::with_capacity(gvdb.records.len());
        for (i, r) in gvdb.records.iter().enumerate() {
            index.entry(r.key).or_insert(i);
        }
        Self {
            gvdb,
            index,
            removed: Vec::new(),
            added_rows: HashSet::new(),
        }
    }

    fn remove(&mut self, key: u64) -> bool {
        match self.index.remove(&key) {
            Some(i) => {
                self.removed.push(i);
                true
            }
            None => false,
        }
    }

    fn finish(self) {
        if self.removed.is_empty() {
            return;
        }
        let removed: HashSet<usize> = self.removed.into_iter().collect();
        let mut i = 0;
        self.gvdb.records.retain(|_| {
            i += 1;
            !removed.contains(&(i - 1))
        });
    }

    fn get(&self, key: u64) -> Option<&Record> {
        self.index.get(&key).map(|&i| &self.gvdb.records[i])
    }

    fn get_mut(&mut self, key: u64) -> Option<&mut Record> {
        self.index.get(&key).map(|&i| &mut self.gvdb.records[i])
    }

    fn text(&self, key: u64) -> Option<String> {
        self.get(key).map(|r| r.text().into_owned())
    }

    fn push(&mut self, record: Record) {
        self.index.insert(record.key, self.gvdb.records.len());
        self.gvdb.records.push(record);
    }

    fn instance_count(&self, table: &str, row: &str) -> Option<usize> {
        self.text(fnv1a64(instance_count_path(table, row)))?
            .trim()
            .parse()
            .ok()
    }
}

fn parse_base(s: &str) -> Result<Base> {
    if let Some(hex) = s.strip_prefix('{') {
        let (hex, rest) = hex
            .split_once('}')
            .ok_or_else(|| Error::Resolve(format!("bad placeholder {s}")))?;
        let h = u64::from_str_radix(hex, 16).map_err(|_| Error::Resolve(format!("bad placeholder {s}")))?;
        return Ok(if rest.is_empty() {
            Base::Hash(h)
        } else {
            Base::Hash(fnv1a64_from(h, rest))
        });
    }
    Ok(Base::Path(s.to_owned()))
}

fn row_name(base: &Base, idx: usize, row: &str) -> Base {
    match base {
        Base::Path(p) if row.is_empty() => Base::Path(format!("{p}{idx}")),
        Base::Path(p) => Base::Path(format!("{p}.{row}_{idx}")),
        Base::Hash(h) if row.is_empty() => Base::Hash(fnv1a64_from(*h, idx.to_string())),
        Base::Hash(h) => Base::Hash(fnv1a64_from(*h, format!(".{row}_{idx}"))),
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Segment<'a> {
    pub name: &'a str,
    pub select: Option<(&'a str, &'a str)>,
}

pub fn first_segment(path: &str) -> Result<(Segment<'_>, Option<&str>)> {
    let end = path.find(['.', '[']).unwrap_or(path.len());
    let (name, rest) = path.split_at(end);
    let Some(inner) = rest.strip_prefix('[') else {
        return Ok((Segment { name, select: None }, rest.strip_prefix('.')));
    };
    let close = inner
        .match_indices(']')
        .map(|(i, _)| i)
        .find(|&i| matches!(inner.as_bytes().get(i + 1), None | Some(b'.')))
        .ok_or_else(|| Error::Resolve(format!("{path}: [ needs a ] at the end of its segment")))?;
    let sel = &inner[..close];
    let (field, want) = sel
        .split_once('=')
        .ok_or_else(|| Error::Resolve(format!("{path}: bad selector [{sel}]")))?;
    let segment = Segment {
        name,
        select: Some((field.trim(), want)),
    };
    Ok((segment, inner[close + 1..].strip_prefix('.')))
}

fn select_row(db: &Db, table: &Base, row: &str, field: &str, want: &str) -> std::result::Result<Base, usize> {
    let count = match table {
        Base::Path(t) if !row.is_empty() => db.instance_count(t, row),
        _ => None,
    };
    let mut hits = Vec::new();
    for idx in 0..count.unwrap_or(4096) {
        let candidate = Row {
            base: row_name(table, idx, row),
        };
        match db.text(candidate.field("_metadata_")) {
            Some(_) if db.text(candidate.field(field)).as_deref() == Some(want) => hits.push(candidate.base),
            Some(_) => {}
            None if count.is_none() => break,
            None => {}
        }
    }
    match hits.len() {
        1 => Ok(hits.remove(0)),
        n => Err(n),
    }
}

fn resolve_path(db: &Db, path: &str) -> Result<Base> {
    if !path.contains('[') {
        return parse_base(path);
    }
    let mut base: Option<Base> = None;
    let mut nested = false;
    let mut rest = Some(path);
    while let Some(p) = rest {
        let (seg, tail) = first_segment(p)?;
        if seg.name.is_empty() {
            return Err(Error::Resolve(format!("{path}: empty segment")));
        }
        base = Some(match (base, seg.select) {
            (None, None) => parse_base(seg.name)?,
            (Some(b), None) => b.child(seg.name),
            (prefix, Some((field, want))) => {
                let (table, row) = match prefix {
                    Some(b) => (b, seg.name),
                    None => (parse_base(seg.name)?, ""),
                };
                match select_row(db, &table, row, field, want) {
                    Ok(b) => {
                        nested = true;
                        b
                    }
                    Err(n) => {
                        let upto = &path[..path.len() - tail.map_or(0, |t| t.len() + 1)];
                        let what = match n {
                            0 => "no row matches".to_owned(),
                            n => format!("{n} rows match"),
                        };
                        let scope = match (nested, row) {
                            (false, _) => String::new(),
                            (true, "") => format!(" in {table}"),
                            (true, row) => format!(" in {table}.{row}"),
                        };
                        return Err(Error::Resolve(format!("{upto}: {what}{scope}")));
                    }
                }
            }
        });
        rest = tail;
    }
    base.ok_or_else(|| Error::Resolve(format!("bad path {path}")))
}

fn resolve_row(db: &Db, select: &str) -> Result<Row> {
    Ok(Row {
        base: resolve_path(db, select)?,
    })
}

fn resolve_value(db: &Db, path: &str) -> Result<String> {
    if !path.contains(['.', ']']) {
        return Err(Error::Resolve(format!("bad path {path}")));
    }
    let key = resolve_path(db, path)?.key("");
    db.text(key)
        .ok_or_else(|| Error::Resolve(format!("{path}: no value")))
}

fn resolve_key(db: &Db, key: &str) -> Result<u64> {
    match key.starts_with("0x") || !key.contains('[') {
        true => record_key(key),
        false => Ok(resolve_path(db, key)?.key("")),
    }
}

pub fn apply_db_ops(db: &mut Gvdb, ops: &DbOps, names: &dyn FieldNames) -> Result<Vec<u64>> {
    apply_db_ops_report(db, ops, names, &mut DbReport::default())
}

pub fn apply_db_ops_report(
    db: &mut Gvdb,
    ops: &DbOps,
    names: &dyn FieldNames,
    report: &mut DbReport,
) -> Result<Vec<u64>> {
    let schema = Schema {
        file: &ops.fields,
        names,
    };
    let mut db = Db::new(db);
    let mut cx = Cx {
        strict: ops.strict,
        report,
    };
    let mut replaced = Vec::new();
    for op in &ops.ops {
        replaced.extend(apply(&mut db, op, &schema, &mut cx)?);
    }
    db.finish();
    Ok(replaced)
}

/// Like [`apply_db_ops`], but an op that fails is skipped and returned with
/// its error instead of stopping the rest. A failing op changes nothing.
pub fn apply_db_ops_lenient(
    db: &mut Gvdb,
    ops: &DbOps,
    names: &dyn FieldNames,
    report: &mut DbReport,
) -> (Vec<u64>, Vec<(usize, Error)>) {
    let schema = Schema {
        file: &ops.fields,
        names,
    };
    let mut db = Db::new(db);
    let mut cx = Cx {
        strict: ops.strict,
        report,
    };
    let mut replaced = Vec::new();
    let mut failed = Vec::new();
    for (i, op) in ops.ops.iter().enumerate() {
        match apply(&mut db, op, &schema, &mut cx) {
            Ok(key) => replaced.extend(key),
            Err(e) => failed.push((i, e)),
        }
    }
    db.finish();
    (replaced, failed)
}

pub fn apply_db_op(db: &mut Gvdb, op: &DbOp, names: &dyn FieldNames) -> Result<Option<u64>> {
    let mut db = Db::new(db);
    let mut report = DbReport::default();
    let mut cx = Cx {
        strict: false,
        report: &mut report,
    };
    let replaced = apply(&mut db, op, names, &mut cx)?;
    db.finish();
    Ok(replaced)
}

/// The keys of each row in `rows` that `db` holds, relative to the row, as `add-row` copies them.
pub fn row_keys(db: &mut Gvdb, names: &dyn FieldNames, rows: &[String]) -> Vec<BTreeSet<String>> {
    let db = Db::new(db);
    rows.iter()
        .map(|row| {
            let mut out = Walk::default();
            walk(&db, names, row, "", &mut out);
            out.keys
        })
        .collect()
}

struct Cx<'r> {
    strict: bool,
    report: &'r mut DbReport,
}

impl Cx<'_> {
    fn warn(&mut self, what: String) -> Result<()> {
        if self.strict {
            return Err(Error::Resolve(format!("{what} (strict)")));
        }
        self.report.warnings.push(what);
        Ok(())
    }
}

pub fn record_key(key: &str) -> Result<u64> {
    match key.strip_prefix("0x") {
        Some(hex) if hex.len() == 16 => {
            u64::from_str_radix(hex, 16).map_err(|_| Error::Resolve(format!("bad record key {key}")))
        }
        Some(_) => Err(Error::Resolve(format!("bad record key {key}"))),
        None => Ok(fnv1a64(key)),
    }
}

fn hex_bytes(key: &str, value: &str) -> Result<Vec<u8>> {
    let bad = || Error::Resolve(format!("{key}: value must be hex bytes"));
    if !value.is_ascii() || !value.len().is_multiple_of(2) {
        return Err(bad());
    }
    (0..value.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&value[i..i + 2], 16).map_err(|_| bad()))
        .collect()
}

pub fn add_row_name<'a>(table: &str, from: &'a str) -> Option<&'a str> {
    from.strip_prefix(table)
        .and_then(|r| r.strip_prefix('.'))
        .map(|r| r.split(['[', '.']).next().unwrap_or(r))
        .map(|r| {
            r.rsplit_once('_')
                .filter(|(_, n)| n.parse::<u32>().is_ok())
                .map_or(r, |(n, _)| n)
        })
}

fn list_items(value: &str) -> impl Iterator<Item = &str> {
    value.split(',').map(str::trim).filter(|v| !v.is_empty())
}

fn edit_list(db: &mut Db, select: &str, field: &str, edit: impl FnOnce(&mut Vec<String>)) -> Result<()> {
    let key = match field.contains('[') {
        true => resolve_path(db, &format!("{select}.{field}"))?.key(""),
        false => resolve_row(db, select)?.field(field),
    };
    let r = db
        .get_mut(key)
        .ok_or_else(|| Error::Resolve(format!("{select}.{field}: no such key")))?;
    let mut items: Vec<String> = list_items(&r.text()).map(String::from).collect();
    edit(&mut items);
    r.value = items.join(",").into_bytes();
    Ok(())
}

fn apply(db: &mut Db, op: &DbOp, names: &dyn FieldNames, cx: &mut Cx) -> Result<Option<u64>> {
    match op {
        DbOp::Set {
            select,
            field,
            value,
            value_from,
            datasets,
            create,
            tag,
        } => {
            if !datasets.is_empty() {
                return Err(Error::Resolve("dataset-scoped set is not implemented yet".into()));
            }
            let value = match (value, value_from) {
                (Some(v), None) => v.clone(),
                (None, Some(from)) => resolve_value(db, from)?,
                _ => {
                    return Err(Error::Resolve(format!(
                        "{select}.{field}: set needs value or value_from"
                    )));
                }
            };
            let key = match field.contains('[') {
                true => resolve_path(db, &format!("{select}.{field}"))?.key(""),
                false => resolve_row(db, select)?.field(field),
            };
            match db.get_mut(key) {
                Some(r) => r.value = value.into_bytes(),
                None if *create => {
                    let tag =
                        tag.ok_or_else(|| Error::Resolve(format!("{select}.{field}: create needs type")))?;
                    db.push(Record {
                        key,
                        tag,
                        value: value.into_bytes(),
                    });
                }
                None => return Err(Error::Resolve(format!("{select}.{field}: no such key"))),
            }
            Ok(Some(key))
        }
        DbOp::AddRow {
            table,
            from,
            set,
            copy,
            stat_id,
        } => {
            add_row(
                db,
                names,
                cx,
                AddRow {
                    table,
                    from,
                    set,
                    copy: *copy,
                    stat_id: *stat_id,
                },
            )?;
            Ok(None)
        }
        DbOp::RemoveRow { .. } => Err(Error::Resolve("remove-row is not implemented yet".into())),
        DbOp::ListAppend { select, field, value } => {
            edit_list(db, select, field, |items| {
                for v in list_items(value) {
                    if !items.iter().any(|i| i == v) {
                        items.push(v.to_string());
                    }
                }
            })?;
            Ok(None)
        }
        DbOp::ListRemove { select, field, value } => {
            let drop: Vec<&str> = list_items(value).collect();
            edit_list(db, select, field, |items| {
                items.retain(|i| !drop.contains(&i.as_str()))
            })?;
            Ok(None)
        }
        DbOp::PutRecord { key, tag, value } => {
            let value = hex_bytes(key, value)?;
            let k = resolve_key(db, key)?;
            match db.get_mut(k) {
                Some(r) => {
                    r.tag = *tag;
                    r.value = value;
                }
                None => db.push(Record {
                    key: k,
                    tag: *tag,
                    value,
                }),
            }
            Ok(Some(k))
        }
        DbOp::RemoveRecord {
            key,
            allow_stock_removal,
        } => {
            let k = resolve_key(db, key)?;
            if db.get(k).is_none() {
                cx.report.lines.push(format!("{key}: already removed"));
                return Ok(None);
            }
            // The game reads every stock key and crashes at the missing-key sentinel.
            if !allow_stock_removal && names.stock(k) {
                cx.report.stock_removals += 1;
                return Ok(None);
            }
            // A track's new rows count in their tables, and the game reads them like stock rows.
            if names.track(k) {
                cx.report.track_removals += 1;
                return Ok(None);
            }
            db.remove(k);
            Ok(Some(k))
        }
    }
}

struct AddRow<'a> {
    table: &'a str,
    from: &'a str,
    set: &'a BTreeMap<String, String>,
    copy: Option<CopyMode>,
    stat_id: Option<StatIdMode>,
}

#[derive(Default)]
struct Walk {
    keys: BTreeSet<String>,
    uncatalogued: BTreeSet<String>,
}

fn walk(db: &Db, names: &dyn FieldNames, path: &str, rel: &str, out: &mut Walk) {
    let shape = row_shape(path);
    let entries = match names.row_keys(&shape) {
        Some(e) => e,
        None => {
            out.uncatalogued.insert(shape);
            match names.row_keys(ANY_SHAPE) {
                Some(e) => e,
                None => return,
            }
        }
    };
    for entry in entries {
        let Some(sub) = entry.strip_prefix('+') else {
            if db.get(fnv1a64(format!("{path}.{entry}"))).is_some() {
                out.keys.insert(format!("{rel}{entry}"));
            }
            continue;
        };
        let (head, word) = match sub.rsplit_once('.') {
            Some((h, w)) => (format!("{h}."), w),
            None => (String::new(), sub),
        };
        let container = format!("{path}.{}", head.trim_end_matches('.'));
        let container = container.trim_end_matches('.');
        let count_key = format!("{rel}{head}_{word}_instancecount_");
        let rows = match db.instance_count(container, word) {
            Some(n) => {
                out.keys.insert(count_key);
                n
            }
            None => (0..4096)
                .take_while(|i| {
                    db.get(fnv1a64(format!("{container}.{word}_{i}._metadata_")))
                        .is_some()
                })
                .count(),
        };
        for i in 0..rows {
            walk(
                db,
                names,
                &format!("{container}.{word}_{i}"),
                &format!("{rel}{head}{word}_{i}."),
                out,
            );
        }
    }
}

fn cut_by_count(key: &str, set: &BTreeMap<String, String>) -> bool {
    set.iter().any(|(k, v)| {
        let Some(stem) = k.strip_suffix("_instancecount_") else {
            return false;
        };
        let Ok(count) = v.trim().parse::<usize>() else {
            return false;
        };
        let (head, word) = match stem.rsplit_once('.') {
            Some((h, w)) => (format!("{h}."), w),
            None => (String::new(), stem),
        };
        let Some(word) = word.strip_prefix('_') else {
            return false;
        };
        key.strip_prefix(&format!("{head}{word}_"))
            .and_then(|r| r.split_once('.'))
            .and_then(|(i, _)| i.parse::<usize>().ok())
            .is_some_and(|i| i >= count)
    })
}

fn preview(keys: &[&String]) -> String {
    let shown: Vec<&str> = keys.iter().take(12).map(|k| k.as_str()).collect();
    match keys.len() > shown.len() {
        true => format!("{} and {} more", shown.join(", "), keys.len() - shown.len()),
        false => shown.join(", "),
    }
}

fn stat_ids(db: &Db, table: &str, row: &str, rows: usize, skip: &HashSet<String>) -> Vec<i64> {
    (0..rows)
        .map(|i| format!("{table}.{row}_{i}"))
        .filter(|r| !skip.contains(r))
        .filter_map(|r| db.text(fnv1a64(format!("{r}.stat_id"))))
        .filter_map(|v| v.trim().parse::<i64>().ok())
        .collect()
}

fn add_row(db: &mut Db, names: &dyn FieldNames, cx: &mut Cx, op: AddRow) -> Result<()> {
    let AddRow {
        table,
        from,
        set,
        copy,
        stat_id,
    } = op;
    let src = resolve_row(db, from)?;
    let Base::Path(path) = resolve_path(db, table)? else {
        return Err(Error::Resolve(format!(
            "{table}: add-row needs the table's real path, not a {{hash}}"
        )));
    };
    let Base::Path(src_path) = &src.base else {
        return Err(Error::Resolve(format!("{from} is not a row of {table}")));
    };
    let row = add_row_name(&path, src_path)
        .ok_or_else(|| Error::Resolve(format!("{from} is not a row of {table}")))?;
    let count_path = instance_count_path(&path, row);
    let n = db.instance_count(&path, row).ok_or_else(|| {
        Error::Resolve(format!(
            "{count_path}: missing; add-row needs the table's real path"
        ))
    })?;
    let listed = names.fields(table, row).or_else(|| names.fields(&path, row));
    let mode = copy.unwrap_or(CopyMode::All);
    let mut found = Walk::default();
    walk(db, names, src_path, "", &mut found);
    let dst_path = format!("{path}.{row}_{n}");
    let fields: Vec<String> = match (mode, &listed) {
        (CopyMode::Listed, None) => {
            return Err(Error::Resolve(format!(
                "{table}.{row}: copy listed needs a fields list"
            )));
        }
        (CopyMode::Listed, Some(l)) => l.clone(),
        (CopyMode::All, l) => {
            let mut all = found.keys.clone();
            all.extend(l.iter().flatten().cloned());
            all.into_iter().collect()
        }
    };
    if fields.is_empty() {
        return Err(Error::Resolve(format!("{table}.{row}: field names unknown")));
    }
    if let Some(f) = set.keys().find(|f| !fields.contains(f)) {
        return Err(Error::Resolve(format!("{table}.{row}: unknown field {f}")));
    }
    let what = format!("add-row {dst_path} (from {from})");
    if !found.uncatalogued.is_empty() {
        let shapes: Vec<&String> = found.uncatalogued.iter().collect();
        cx.warn(format!(
            "{what}: the key catalogue has no entry for {}; keys only the exe builds at run time may be missing",
            preview(&shapes)
        ))?;
    }
    if let Some(l) = &listed {
        let gaps: Vec<&String> = found
            .keys
            .iter()
            .filter(|k| !l.contains(k) && !cut_by_count(k, set))
            .collect();
        match (mode, gaps.is_empty()) {
            (_, true) => {}
            (CopyMode::Listed, false) => cx.warn(format!(
                "{what}: the fields list for {table}.{row} misses {} key(s) of the source row, not copied: {}",
                gaps.len(),
                preview(&gaps)
            ))?,
            (CopyMode::All, false) => cx.report.lines.push(format!(
                "{what}: copied {} key(s) the fields list for {table}.{row} doesn't name: {}",
                gaps.len(),
                preview(&gaps)
            )),
        }
    }
    let dst = Row {
        base: Base::Path(dst_path.clone()),
    };
    let mut rows = Vec::with_capacity(fields.len());
    let mut reused = 0;
    for field in &fields {
        if let Some(r) = db.get(dst.field(field)) {
            // A stock row a mod left above the count it lowered is never read.
            if !names.stock(r.key) {
                return Err(Error::Resolve(format!("{dst_path}.{field} already exists")));
            }
            reused += 1;
        }
        let s = db
            .get(src.field(field))
            .ok_or_else(|| Error::Resolve(format!("{from}: has no {field}")))?;
        let value = set
            .get(field)
            .map(|v| v.clone().into_bytes())
            .unwrap_or_else(|| s.value.clone());
        rows.push(Record {
            key: dst.field(field),
            tag: s.tag,
            value,
        });
    }
    let stat_key = dst.field("stat_id");
    let indexed = || {
        let ids = stat_ids(db, &path, row, n, &db.added_rows);
        let unique: HashSet<i64> = ids.iter().copied().collect();
        !ids.is_empty() && unique.len() == ids.len()
    };
    let top_level = row_shape(&dst_path).matches("_N").count() == 1;
    let fresh = match (stat_id, set.contains_key("stat_id")) {
        (_, true) | (Some(StatIdMode::Keep), _) => false,
        (Some(StatIdMode::Fresh), false) => true,
        (None, false) => {
            let indexed = indexed();
            if indexed
                && !top_level
                && let Some(r) = rows.iter().find(|r| r.key == stat_key)
            {
                cx.report.lines.push(format!(
                    "{what}: keeps the source's stat_id {} in a nested table; set stat_id or \"stat_id\": \"fresh\" for its own",
                    String::from_utf8_lossy(&r.value)
                ));
            }
            indexed && top_level
        }
    };
    if fresh && let Some(r) = rows.iter_mut().find(|r| r.key == stat_key) {
        let old = String::from_utf8_lossy(&r.value).into_owned();
        let next = stat_ids(db, &path, row, n, &HashSet::new())
            .into_iter()
            .max()
            .map_or(0, |m| m + 1);
        r.value = next.to_string().into_bytes();
        cx.report
            .lines
            .push(format!("{what}: stat_id {next} (the source's is {old})"));
    }
    cx.report
        .lines
        .push(format!("{what}: copied {} key(s)", rows.len()));
    if reused > 0 {
        cx.report.lines.push(format!(
            "{what}: reuses {reused} stock key(s) of a row above the table's count"
        ));
    }
    for r in rows {
        match db.get_mut(r.key) {
            Some(old) => *old = r,
            None => db.push(r),
        }
    }
    cx.report.added_rows.push(AddedRow {
        from: src_path.clone(),
        row: dst_path.clone(),
    });
    db.added_rows.insert(dst_path);
    let count = db
        .get_mut(fnv1a64(&count_path))
        .expect("instance_count read it above");
    count.value = (n + 1).to_string().into_bytes();
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::asset::{FileKind, OpFile};

    fn db() -> Gvdb {
        let mut records = vec![Record::new("entrants.teams._team_instancecount_", 0, "2")];
        for (i, label) in ["mclaren_26", "gm_cadillac_26"].iter().enumerate() {
            records.push(Record::new(
                &format!("entrants.teams.team_{i}._metadata_"),
                4,
                *label,
            ));
            records.push(Record::new(
                &format!("entrants.teams.team_{i}.stat_id"),
                0,
                (100 + i).to_string(),
            ));
            records.push(Record::new(
                &format!("entrants.teams.team_{i}.display_name_code"),
                2,
                "XXX",
            ));
        }
        let world = fnv1a64("hidden.world_table_");
        for (i, (label, eri)) in [("f1_26", "_f1_26.wep"), ("myteam_26", "_f1_26_myteam.wep")]
            .iter()
            .enumerate()
        {
            for (f, v) in [("_metadata_", *label), ("worldEri", *eri)] {
                let key = fnv1a64_from(world, format!("{i}.{f}"));
                records.push(Record {
                    key,
                    tag: 2,
                    value: v.as_bytes().to_vec(),
                });
            }
        }
        Gvdb { records }
    }

    fn names() -> BTreeMap<String, Vec<String>> {
        let fields = ["_metadata_", "stat_id", "display_name_code"]
            .map(String::from)
            .to_vec();
        BTreeMap::from([("entrants.teams.team".to_owned(), fields)])
    }

    fn ops(json: &str) -> DbOps {
        serde_json::from_str(json).unwrap()
    }

    #[test]
    fn side_data_lists_ignore_patterns_and_rejects_the_rest() {
        let file = ops(
            r#"{"schema_version":1,"side_data":{"ignore":["bank.*","accolades.AccoladeDataGroup_N.driver_data.AccoladeDriver_N"]},"ops":[]}"#,
        );
        assert_eq!(file.side_data.ignore.len(), 2);
        let parse = |json: &str| serde_json::from_str::<DbOps>(json).unwrap_err().to_string();
        assert!(
            parse(r#"{"schema_version":1,"side_data":{"ignored":[]},"ops":[]}"#)
                .contains("unknown field `ignored`")
        );
        assert!(
            parse(r#"{"schema_version":1,"side_data":{"ignore":["bank initial"]},"ops":[]}"#)
                .contains("is not a table or field path")
        );
        assert!(
            parse(r#"{"schema_version":1,"side_data":{"ignore":[""]},"ops":[]}"#)
                .contains("side_data.ignore")
        );
        let roundtrip = serde_json::to_string(&ops(r#"{"schema_version":1,"ops":[]}"#)).unwrap();
        assert!(!roundtrip.contains("side_data"), "{roundtrip}");
    }

    #[test]
    fn add_row_appends_and_bumps_count() {
        let mut db = db();
        let ops = ops(
            r#"{"schema_version":1,"ops":[{"op":"add-row","table":"entrants.teams",
            "from":"entrants.teams.team[_metadata_=gm_cadillac_26]",
            "set":{"_metadata_":"team12_26","display_name_code":"T12"}}]}"#,
        );
        apply_db_ops(&mut db, &ops, &names()).unwrap();
        assert_eq!(db.instance_count("entrants.teams", "team"), Some(3));
        assert_eq!(
            db.get_text("entrants.teams.team_2._metadata_").as_deref(),
            Some("team12_26")
        );
        assert_eq!(
            db.get_text("entrants.teams.team_2.stat_id").as_deref(),
            Some("102")
        );
        assert_eq!(
            db.get_text("entrants.teams.team_2.display_name_code").as_deref(),
            Some("T12")
        );
        let bytes = db.to_bytes();
        assert_eq!(Gvdb::parse(&bytes).unwrap().to_bytes(), bytes);
    }

    #[test]
    fn add_row_reads_field_names_from_the_file() {
        let mut db = db();
        let ops = ops(
            r#"{"schema_version":1,"fields":{"entrants.teams.team":["_metadata_","stat_id"]},"ops":[
            {"op":"add-row","table":"entrants.teams","from":"entrants.teams.team[_metadata_=mclaren_26]",
            "stat_id":"keep","set":{"_metadata_":"team12_26"}}]}"#,
        );
        apply_db_ops(&mut db, &ops, &BTreeMap::new()).unwrap();
        assert_eq!(db.instance_count("entrants.teams", "team"), Some(3));
        assert_eq!(
            db.get_text("entrants.teams.team_2.stat_id").as_deref(),
            Some("100")
        );
        assert!(db.get("entrants.teams.team_2.display_name_code").is_none());
    }

    #[test]
    fn add_row_needs_every_field_on_the_source_and_a_free_row() {
        let mut db = db();
        let missing = ops(
            r#"{"schema_version":1,"fields":{"entrants.teams.team":["_metadata_","nope"]},"ops":[
            {"op":"add-row","table":"entrants.teams","from":"entrants.teams.team_0"}]}"#,
        );
        let err = apply_db_ops(&mut db, &missing, &BTreeMap::new()).unwrap_err();
        assert!(err.to_string().contains("has no nope"), "{err}");
        assert_eq!(db.instance_count("entrants.teams", "team"), Some(2));
        db.records
            .push(Record::new("entrants.teams.team_2._metadata_", 4, "stale"));
        let taken = ops(
            r#"{"schema_version":1,"ops":[{"op":"add-row","table":"entrants.teams","from":"entrants.teams.team_0"}]}"#,
        );
        let err = apply_db_ops(&mut db, &taken, &names()).unwrap_err();
        assert!(err.to_string().contains("already exists"), "{err}");
    }

    #[test]
    fn add_row_reuses_a_stock_row_above_a_lowered_count() {
        let mut db = db();
        for (f, v) in [
            ("_metadata_", "stale"),
            ("stat_id", "7"),
            ("display_name_code", "OLD"),
        ] {
            db.records
                .push(Record::new(&format!("entrants.teams.team_2.{f}"), 2, v));
        }
        let stock: HashSet<u64> = db.records.iter().map(|r| r.key).collect();
        let add = ops(
            r#"{"schema_version":1,"fields":{"entrants.teams.team":["_metadata_","stat_id","display_name_code"]},
            "ops":[{"op":"add-row","table":"entrants.teams","from":"entrants.teams.team_0",
            "set":{"_metadata_":"new_26"}}]}"#,
        );
        let mut report = DbReport::default();
        apply_db_ops_report(&mut db, &add, &stock, &mut report).unwrap();
        let key = fnv1a64("entrants.teams.team_2._metadata_");
        let found: Vec<&Record> = db.records.iter().filter(|r| r.key == key).collect();
        assert_eq!(found.len(), 1);
        assert_eq!(found[0].text(), "new_26");
        assert_eq!(found[0].tag, 4);
        let code = fnv1a64("entrants.teams.team_2.display_name_code");
        assert_eq!(db.records.iter().find(|r| r.key == code).unwrap().text(), "XXX");
        assert_eq!(db.instance_count("entrants.teams", "team"), Some(3));
        assert!(
            report.lines.iter().any(|l| l.contains("reuses 3 stock key(s)")),
            "{:?}",
            report.lines
        );
    }

    #[test]
    fn erp_ops_parse_patch_pkg_and_warn() {
        let file = |json: &str| OpFile::from_value(serde_json::from_str(json).unwrap(), FileKind::Erp);
        let ops = file(
            r#"{"schema_version":1,"ops":[
            {"op":"patch-pkg","erp":"a.erp","resource":"eaid://x.layer","edits":[
              {"op":"clone-entry","at":"instances","from":{"erp":"b.erp","resource":"eaid://y.layer"},
               "src":"[name=y$A]","as":"x$B","set":{"overrides.VehPlacement.Id":25}}]},
            {"op":"warn","message":"Imola has room for 26 cars"}]}"#,
        )
        .unwrap();
        assert_eq!(ops.ops.len(), 2);
        assert!(file(r#"{"schema_version":1,"ops":[{"op":"warn"}]}"#).is_err());
    }

    #[test]
    fn set_with_selector_and_value_from_hash_placeholder() {
        let mut db = db();
        let h = format!("{{{:016x}}}", fnv1a64("hidden.world_table_"));
        let json = format!(
            r#"{{"schema_version":1,"ops":[{{"op":"set","select":"{h}[_metadata_=f1_26]","field":"worldEri",
            "value_from":"{h}[_metadata_=myteam_26].worldEri"}}]}}"#
        );
        apply_db_ops(&mut db, &ops(&json), &names()).unwrap();
        let key = fnv1a64_from(fnv1a64("hidden.world_table_"), "0.worldEri");
        assert_eq!(db.get_key(key).unwrap().text(), "_f1_26_myteam.wep");
    }

    #[test]
    fn put_and_remove_records_by_path_and_hash() {
        let mut db = db();
        let count = db.records.len();
        let code = fnv1a64("entrants.teams.team_0.display_name_code");
        let ops = ops(&format!(
            r#"{{"schema_version":1,"ops":[
            {{"op":"put-record","key":"{code:#018x}","type":4,"value":"4d434c"}},
            {{"op":"put-record","key":"entrants.teams.team_0.new_field","type":0,"value":"3132"}},
            {{"op":"remove-record","key":"entrants.teams.team_1.stat_id","allow_stock_removal":true}}]}}"#
        ));
        let stock: HashSet<u64> = db.records.iter().map(|r| r.key).collect();
        let replaced = apply_db_ops(&mut db, &ops, &stock).unwrap();
        assert_eq!(replaced[0], code);
        assert_eq!(replaced.len(), 3);
        let r = db.get("entrants.teams.team_0.display_name_code").unwrap();
        assert_eq!((r.tag, r.text().as_ref()), (4, "MCL"));
        assert_eq!(
            db.get_text("entrants.teams.team_0.new_field").as_deref(),
            Some("12")
        );
        assert!(db.get("entrants.teams.team_1.stat_id").is_none());
        assert_eq!(db.records.len(), count);
        for bad in [
            r#"{"op":"put-record","key":"0x12","type":0,"value":"00"}"#,
            r#"{"op":"put-record","key":"a.b","type":0,"value":"0"}"#,
        ] {
            let op: DbOp = serde_json::from_str(bad).unwrap();
            assert!(apply_db_op(&mut db, &op, &names()).is_err(), "{bad}");
        }
    }

    #[test]
    fn remove_record_twice_is_idempotent() {
        let key = "entrants.teams.team_1.stat_id";
        let json = format!(
            r#"{{"schema_version":1,"ops":[
            {{"op":"remove-record","key":"{key}"}},
            {{"op":"remove-record","key":"{key}"}}]}}"#
        );
        let mut lenient = db();
        let mut report = DbReport::default();
        let replaced = apply_db_ops_report(&mut lenient, &ops(&json), &names(), &mut report).unwrap();
        assert_eq!(replaced, [fnv1a64(key)]);
        assert!(lenient.get(key).is_none());
        assert_eq!(report.lines, [format!("{key}: already removed")]);

        let strict = format!(
            r#"{{"schema_version":1,"strict":true,"ops":[
            {{"op":"remove-record","key":"{key}"}},
            {{"op":"remove-record","key":"{key}"}}]}}"#
        );
        apply_db_ops(&mut db(), &ops(&strict), &names()).unwrap();
    }

    #[test]
    fn remove_record_keeps_stock_records_unless_the_op_allows_it() {
        let mut db = db();
        let stock: HashSet<u64> = db.records.iter().map(|r| r.key).collect();
        let json = r#"{"schema_version":1,"ops":[
            {"op":"put-record","key":"entrants.teams.team_0.extra","type":0,"value":"31"},
            {"op":"put-record","key":"entrants.teams.team_1.extra","type":0,"value":"32"},
            {"op":"remove-record","key":"entrants.teams.team_0.stat_id"},
            {"op":"remove-record","key":"entrants.teams.team[_metadata_=gm_cadillac_26].stat_id"},
            {"op":"remove-record","key":"entrants.teams.team_0.extra"},
            {"op":"remove-record","key":"entrants.teams.team_1._metadata_","allow_stock_removal":true}]}"#;
        let mut report = DbReport::default();
        let replaced = apply_db_ops_report(&mut db, &ops(json), &stock, &mut report).unwrap();
        assert_eq!(report.stock_removals, 2);
        assert!(report.warnings.is_empty(), "{report:?}");
        let extra = fnv1a64("entrants.teams.team_0.extra");
        assert_eq!(
            replaced,
            [
                extra,
                fnv1a64("entrants.teams.team_1.extra"),
                extra,
                fnv1a64("entrants.teams.team_1._metadata_")
            ]
        );
        for kept in ["entrants.teams.team_0.stat_id", "entrants.teams.team_1.stat_id"] {
            assert!(db.get(kept).is_some(), "{kept}");
        }
        assert!(db.get("entrants.teams.team_0.extra").is_none());
        assert_eq!(db.get_text("entrants.teams.team_1.extra").as_deref(), Some("2"));
        assert!(db.get("entrants.teams.team_1._metadata_").is_none());

        let mut unguarded = self::db();
        apply_db_ops(&mut unguarded, &ops(json), &names()).unwrap();
        assert!(unguarded.get("entrants.teams.team_0.stat_id").is_none());
        let op: DbOp = serde_json::from_str(r#"{"op":"remove-record","key":"a.b"}"#).unwrap();
        assert_eq!(
            serde_json::to_string(&op).unwrap(),
            r#"{"op":"remove-record","key":"a.b"}"#
        );
    }

    const SETS: &str = "track_data.game_setup";

    fn track_db() -> Gvdb {
        let mut records = vec![Record::new(&instance_count_path(SETS, "TrackDataSet"), 0, "2")];
        for (i, (label, tracks)) in [
            ("f1", &["melbourne", "okayama", "okayama"][..]),
            ("f1_2026", &["melbourne", "suzuka"][..]),
        ]
        .into_iter()
        .enumerate()
        {
            let set = format!("{SETS}.TrackDataSet_{i}");
            records.push(Record::new(&format!("{set}._metadata_"), 4, label));
            records.push(Record::new(
                &instance_count_path(&set, "TrackData"),
                0,
                tracks.len().to_string(),
            ));
            for (j, track) in tracks.iter().enumerate() {
                let row = format!("{set}.TrackData_{j}");
                records.push(Record::new(&format!("{row}._metadata_"), 4, *track));
                records.push(Record::new(&format!("{row}.track"), 2, *track));
                records.push(Record::new(
                    &format!("{row}._TrackNameData_instancecount_"),
                    0,
                    "1",
                ));
                records.push(Record::new(
                    &format!("{row}.TrackNameData_0._metadata_"),
                    4,
                    "names",
                ));
                records.push(Record::new(
                    &format!("{row}.TrackNameData_0.title"),
                    4,
                    format!("lng_{label}_{track}"),
                ));
            }
        }
        Gvdb { records }
    }

    #[test]
    fn parses_segments_like_pkg_paths() {
        let (seg, rest) = first_segment("TrackData[name=a.b$c].TrackNameData_0.title").unwrap();
        assert_eq!(
            (seg.name, seg.select, rest),
            (
                "TrackData",
                Some(("name", "a.b$c")),
                Some("TrackNameData_0.title")
            )
        );
        let (seg, rest) = first_segment("t[ k =x[1]").unwrap();
        assert_eq!((seg.select, rest), (Some(("k", "x[1")), None));
        let (seg, _) = first_segment("t[k=x]y]").unwrap();
        assert_eq!(seg.select, Some(("k", "x]y")));
        let (seg, rest) = first_segment("{446668bc25ba571c}[k=v].a").unwrap();
        assert_eq!((seg.name, rest), ("{446668bc25ba571c}", Some("a")));
        for bad in ["t[k=v", "t[kv].a", "t[k=v]a"] {
            assert!(first_segment(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn selects_rows_on_every_segment() {
        let mut db = track_db();
        let ops = ops(r#"{"schema_version":1,"ops":[
            {"op":"set","select":"track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=suzuka]",
             "field":"TrackNameData_0.title","value":"lng_a"},
            {"op":"set","select":"track_data.game_setup.TrackDataSet_1.TrackData[_metadata_=melbourne]",
             "field":"TrackNameData_0.title","value":"lng_b"},
            {"op":"set","select":"track_data.game_setup.TrackDataSet[_metadata_=f1].TrackData_0",
             "field":"TrackNameData[_metadata_=names].title","value":"lng_c"}]}"#);
        let replaced = apply_db_ops(&mut db, &ops, &names()).unwrap();
        let key = |p: &str| fnv1a64(format!("{SETS}.{p}.TrackNameData_0.title"));
        assert_eq!(
            replaced,
            [
                key("TrackDataSet_1.TrackData_1"),
                key("TrackDataSet_1.TrackData_0"),
                key("TrackDataSet_0.TrackData_0")
            ]
        );
        for (row, want) in [
            ("TrackDataSet_1.TrackData_1", "lng_a"),
            ("TrackDataSet_1.TrackData_0", "lng_b"),
            ("TrackDataSet_0.TrackData_0", "lng_c"),
        ] {
            let got = db.get_text(&format!("{SETS}.{row}.TrackNameData_0.title"));
            assert_eq!(got.as_deref(), Some(want), "{row}");
        }
    }

    #[test]
    fn nested_selector_errors_name_the_segment() {
        let set = |select: &str| {
            let op = DbOp::Set {
                select: select.to_owned(),
                field: "track".into(),
                value: Some("x".into()),
                value_from: None,
                datasets: Vec::new(),
                create: false,
                tag: None,
            };
            apply_db_op(&mut track_db(), &op, &names())
                .unwrap_err()
                .to_string()
        };
        assert_eq!(
            set("track_data.game_setup.TrackDataSet[_metadata_=f9].TrackData[_metadata_=suzuka]"),
            "track_data.game_setup.TrackDataSet[_metadata_=f9]: no row matches"
        );
        assert_eq!(
            set("track_data.game_setup.TrackDataSet[_metadata_=f1].TrackData[_metadata_=okayama]"),
            "track_data.game_setup.TrackDataSet[_metadata_=f1].TrackData[_metadata_=okayama]: \
             2 rows match in track_data.game_setup.TrackDataSet_0.TrackData"
        );
        assert_eq!(
            set("track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=okayama]"),
            "track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=okayama]: \
             no row matches in track_data.game_setup.TrackDataSet_1.TrackData"
        );
        assert!(
            set("track_data.game_setup.TrackDataSet[_metadata_=f1].TrackData[track]")
                .contains("bad selector")
        );
        assert!(set("track_data.game_setup..TrackDataSet[_metadata_=f1]").contains("empty segment"));
    }

    #[test]
    fn add_row_into_a_nested_table() {
        let mut db = track_db();
        let first = ops(r#"{"schema_version":1,
            "fields":{"track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData":
              ["_metadata_","track","_TrackNameData_instancecount_","TrackNameData_0._metadata_","TrackNameData_0.title"]},
            "ops":[
            {"op":"add-row","table":"track_data.game_setup.TrackDataSet[_metadata_=f1_2026]",
             "from":"track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=melbourne]",
             "set":{"_metadata_":"okayama","track":"okayama"}},
            {"op":"set","select":"track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=okayama]",
             "field":"TrackNameData_0.title","value":"lng_okayama"}]}"#);
        apply_db_ops(&mut db, &first, &BTreeMap::new()).unwrap();
        let set = format!("{SETS}.TrackDataSet_1");
        assert_eq!(db.instance_count(&set, "TrackData"), Some(3));
        assert_eq!(
            db.instance_count(&format!("{SETS}.TrackDataSet_0"), "TrackData"),
            Some(3)
        );
        for (field, want) in [
            ("_metadata_", "okayama"),
            ("track", "okayama"),
            ("TrackNameData_0.title", "lng_okayama"),
        ] {
            let got = db.get_text(&format!("{set}.TrackData_2.{field}"));
            assert_eq!(got.as_deref(), Some(want), "{field}");
        }
        let resolved_fields = first.fields.values().next().unwrap().clone();
        let again: DbOps = serde_json::from_value(serde_json::json!({
            "schema_version": 1,
            "fields": { format!("{set}.TrackData"): resolved_fields },
            "ops": [{ "op": "add-row", "table": set,
                "from": "track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=okayama]",
                "set": { "_metadata_": "sepang", "track": "sepang" } }]
        }))
        .unwrap();
        apply_db_ops(&mut db, &again, &BTreeMap::new()).unwrap();
        assert_eq!(db.instance_count(&set, "TrackData"), Some(4));
        assert_eq!(
            db.get_text(&format!("{set}.TrackData_3.TrackNameData_0.title"))
                .as_deref(),
            Some("lng_okayama")
        );
        let wrong_table = ops(
            r#"{"schema_version":1,"ops":[{"op":"add-row","table":"track_data.game_setup.TrackDataSet[_metadata_=f1]",
            "from":"track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=melbourne]"}]}"#,
        );
        let err = apply_db_ops(&mut db, &wrong_table, &BTreeMap::new()).unwrap_err();
        assert!(err.to_string().contains("is not a row of"), "{err}");
    }

    #[test]
    fn value_from_and_record_keys_take_nested_selectors() {
        let mut db = track_db();
        let ops = ops(r#"{"schema_version":1,"ops":[
            {"op":"set","select":"track_data.game_setup.TrackDataSet[_metadata_=f1].TrackData[_metadata_=melbourne]",
             "field":"TrackNameData_0.title",
             "value_from":"track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=suzuka].TrackNameData_0.title"},
            {"op":"put-record","key":"track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=suzuka].laps","type":0,"value":"3533"},
            {"op":"remove-record","key":"track_data.game_setup.TrackDataSet[_metadata_=f1_2026].TrackData[_metadata_=melbourne].track",
             "allow_stock_removal":true}]}"#);
        let stock: HashSet<u64> = db.records.iter().map(|r| r.key).collect();
        apply_db_ops(&mut db, &ops, &stock).unwrap();
        let get = |p: &str| db.get_text(&format!("{SETS}.{p}"));
        assert_eq!(
            get("TrackDataSet_0.TrackData_0.TrackNameData_0.title").as_deref(),
            Some("lng_f1_2026_suzuka")
        );
        assert_eq!(get("TrackDataSet_1.TrackData_1.laps").as_deref(), Some("53"));
        assert_eq!(get("TrackDataSet_1.TrackData_0.track"), None);
        assert_eq!(
            get("TrackDataSet_0.TrackData_0.track").as_deref(),
            Some("melbourne")
        );
    }

    #[test]
    fn hash_placeholder_takes_nested_selectors() {
        let mut db = db();
        let world = fnv1a64("hidden.world_table_");
        for (i, (label, n)) in [("a", "6"), ("b", "7")].iter().enumerate() {
            for (f, v) in [("_metadata_", *label), ("n", *n)] {
                db.records.push(Record {
                    key: fnv1a64_from(world, format!("1.slot_{i}.{f}")),
                    tag: 0,
                    value: v.as_bytes().to_vec(),
                });
            }
        }
        let h = format!("{{{world:016x}}}");
        let json = format!(
            r#"{{"schema_version":1,"ops":[{{"op":"set","select":"{h}[_metadata_=f1_26]","field":"worldEri",
            "value_from":"{h}[_metadata_=myteam_26].slot[_metadata_=b].n"}}]}}"#
        );
        apply_db_ops(&mut db, &ops(&json), &names()).unwrap();
        assert_eq!(db.get_key(fnv1a64_from(world, "0.worldEri")).unwrap().text(), "7");
        let json = format!(
            r#"{{"schema_version":1,"ops":[{{"op":"set","select":"{h}[_metadata_=myteam_26].slot[_metadata_=c]","field":"n","value":"1"}}]}}"#
        );
        let err = apply_db_ops(&mut db, &ops(&json), &names())
            .unwrap_err()
            .to_string();
        let row = format!("{{{:016x}}}", fnv1a64_from(world, "1"));
        assert!(
            err.ends_with(&format!("[_metadata_=c]: no row matches in {row}.slot")),
            "{err}"
        );
    }

    #[test]
    fn set_missing_key_fails_without_create() {
        let mut db = db();
        let ops = ops(
            r#"{"schema_version":1,"ops":[{"op":"set","select":"entrants.teams.team_0","field":"nope","value":"1"}]}"#,
        );
        assert!(apply_db_ops(&mut db, &ops, &names()).is_err());
    }

    #[test]
    fn list_append_adds_missing_values_once_and_list_remove_takes_them_out() {
        let mut db = db();
        let append = |v: &str| {
            ops(&format!(
                r#"{{"schema_version":1,"ops":[{{"op":"list-append","select":"entrants.teams.team[_metadata_=mclaren_26]","field":"display_name_code","value":"{v}"}}]}}"#
            ))
        };
        let key = fnv1a64("entrants.teams.team_0.display_name_code");
        assert!(
            apply_db_ops(&mut db, &append("YYY"), &names())
                .unwrap()
                .is_empty()
        );
        apply_db_ops(&mut db, &append("ZZZ, XXX"), &names()).unwrap();
        apply_db_ops(&mut db, &append("YYY"), &names()).unwrap();
        assert_eq!(db.get_key(key).unwrap().text(), "XXX,YYY,ZZZ");
        let remove = ops(
            r#"{"schema_version":1,"ops":[{"op":"list-remove","select":"entrants.teams.team_0","field":"display_name_code","value":"XXX,ZZZ"}]}"#,
        );
        assert!(apply_db_ops(&mut db, &remove, &names()).unwrap().is_empty());
        assert_eq!(db.get_key(key).unwrap().text(), "YYY");
    }

    #[test]
    fn list_ops_on_a_missing_key_fail() {
        let mut db = db();
        let ops = ops(
            r#"{"schema_version":1,"ops":[{"op":"list-append","select":"entrants.teams.team_0","field":"nope","value":"a"}]}"#,
        );
        assert!(apply_db_ops(&mut db, &ops, &names()).is_err());
    }

    #[test]
    fn pkg_patch_parses_design_example() {
        let p: PkgPatch = serde_json::from_str(
            r#"{"schema_version":1,"targets":[{"erp":"2025_asset_groups/ui_package/flow_frontend.erp",
            "resource":"eaid://ui_package/screens/fe_home.ui"}],"root":"Screens[0]:fe_home","edits":[
            {"op":"clone-node","src":"settings","as":"mod_menu_listener","set":{"action_name":"Menu Right Stick Click"},
             "replace_event":{"event":"pressed","signals":[{"to":"fe_home.Action","pin":"route","attrs":{"key":"mod_menu"}}]}},
            {"op":"set-text","node":"contextual_panels.offline_contextual_panel.subtitle","value":"x","localise":false}]}"#,
        )
        .unwrap();
        assert_eq!(p.edits.len(), 2);
    }

    struct Catalogue {
        fields: BTreeMap<String, Vec<String>>,
        shapes: HashMap<String, Vec<String>>,
    }

    impl FieldNames for Catalogue {
        fn fields(&self, table: &str, row: &str) -> Option<Vec<String>> {
            self.fields.fields(table, row)
        }

        fn row_keys(&self, shape: &str) -> Option<&[String]> {
            self.shapes.get(shape).map(Vec::as_slice)
        }
    }

    fn catalogue(fields: &[&str]) -> Catalogue {
        let shape = |keys: &[&str]| keys.iter().map(|k| k.to_string()).collect::<Vec<_>>();
        Catalogue {
            fields: match fields.is_empty() {
                true => BTreeMap::new(),
                false => BTreeMap::from([("modes.mode".to_owned(), shape(fields))]),
            },
            shapes: HashMap::from([
                (
                    "modes.mode_N".to_owned(),
                    shape(&[
                        "_metadata_",
                        "stat_id",
                        "player.driver.preloaded",
                        "_Com_instancecount_",
                        "+Com",
                        "+Loose",
                        "+gone",
                    ]),
                ),
                (
                    "modes.mode_N.Com_N".to_owned(),
                    shape(&["_metadata_", "save_data.group", "_Inner_instancecount_", "+Inner"]),
                ),
                ("modes.mode_N.Com_N.Inner_N".to_owned(), shape(&["value"])),
                ("modes.mode_N.Loose_N".to_owned(), shape(&["_metadata_"])),
            ]),
        }
    }

    fn modes(ids: [&str; 2]) -> Gvdb {
        let mut records = vec![Record::new("modes._mode_instancecount_", 0, "2")];
        for (i, (label, id)) in ["f1", "classic"].iter().zip(ids).enumerate() {
            let r = format!("modes.mode_{i}");
            records.push(Record::new(&format!("{r}._metadata_"), 4, *label));
            records.push(Record::new(&format!("{r}.stat_id"), 0, id));
            records.push(Record::new(&format!("{r}.player.driver.preloaded"), 5, "True"));
        }
        for (k, tag, v) in [
            ("modes.mode_0._Com_instancecount_", 0, "1"),
            ("modes.mode_0.Com_0._metadata_", 4, "ComCareer"),
            ("modes.mode_0.Com_0.save_data.group", 2, "career"),
            ("modes.mode_0.Com_0._Inner_instancecount_", 0, "2"),
            ("modes.mode_0.Com_0.Inner_0.value", 1, "0.5"),
            ("modes.mode_0.Com_0.Inner_1.value", 1, "0.75"),
            ("modes.mode_0.Loose_0._metadata_", 4, "a"),
            ("modes.mode_0.Loose_1._metadata_", 4, "b"),
        ] {
            records.push(Record::new(k, tag, v));
        }
        Gvdb { records }
    }

    fn add_mode(extra: &str) -> DbOps {
        ops(&format!(
            r#"{{"schema_version":1{extra},"ops":[{{"op":"add-row","table":"modes",
            "from":"modes.mode[_metadata_=f1]","set":{{"_metadata_":"f1_1990"}}}}]}}"#
        ))
    }

    #[test]
    fn add_row_copies_every_key_with_nested_rows_and_counts() {
        let mut db = modes(["3", "7"]);
        let mut report = DbReport::default();
        apply_db_ops_report(&mut db, &add_mode(""), &catalogue(&[]), &mut report).unwrap();
        let get = |k: &str| db.get_text(&format!("modes.mode_2.{k}"));
        assert_eq!(db.instance_count("modes", "mode"), Some(3));
        assert_eq!(get("_metadata_").as_deref(), Some("f1_1990"));
        assert_eq!(get("player.driver.preloaded").as_deref(), Some("True"));
        assert_eq!(get("_Com_instancecount_").as_deref(), Some("1"));
        assert_eq!(get("Com_0.save_data.group").as_deref(), Some("career"));
        assert_eq!(get("Com_0._Inner_instancecount_").as_deref(), Some("2"));
        assert_eq!(get("Com_0.Inner_1.value").as_deref(), Some("0.75"));
        assert_eq!(get("Loose_1._metadata_").as_deref(), Some("b"));
        assert!(get("Loose_2._metadata_").is_none());
        assert_eq!(db.get("modes.mode_2.Com_0.Inner_0.value").unwrap().tag, 1);
        assert!(report.warnings.is_empty(), "{:?}", report.warnings);
        assert!(
            report.lines.iter().any(|l| l.ends_with("copied 11 key(s)")),
            "{:?}",
            report.lines
        );
        let bytes = db.to_bytes();
        assert_eq!(Gvdb::parse(&bytes).unwrap().to_bytes(), bytes);
    }

    #[test]
    fn add_row_gives_a_clone_the_next_free_stat_id_and_reports_it() {
        let mut db = modes(["3", "7"]);
        let mut report = DbReport::default();
        apply_db_ops_report(&mut db, &add_mode(""), &catalogue(&[]), &mut report).unwrap();
        apply_db_ops_report(&mut db, &add_mode(""), &catalogue(&[]), &mut report).unwrap();
        assert_eq!(db.get_text("modes.mode_2.stat_id").as_deref(), Some("8"));
        assert_eq!(db.get_text("modes.mode_3.stat_id").as_deref(), Some("9"));
        assert!(
            report.lines.iter().any(|l| l
                == "add-row modes.mode_2 (from modes.mode[_metadata_=f1]): stat_id 8 (the source's is 3)"),
            "{:?}",
            report.lines
        );
        let rows: Vec<(&str, &str)> = report
            .added_rows
            .iter()
            .map(|r| (r.from.as_str(), r.row.as_str()))
            .collect();
        assert_eq!(
            rows,
            [("modes.mode_0", "modes.mode_2"), ("modes.mode_0", "modes.mode_3")]
        );
    }

    #[test]
    fn add_row_keeps_shared_stat_ids_and_explicit_ones() {
        let mut db = modes(["3", "3"]);
        apply_db_ops_report(&mut db, &add_mode(""), &catalogue(&[]), &mut DbReport::default()).unwrap();
        assert_eq!(db.get_text("modes.mode_2.stat_id").as_deref(), Some("3"));

        let mut db = modes(["3", "7"]);
        let keep = ops(
            r#"{"schema_version":1,"ops":[{"op":"add-row","table":"modes","from":"modes.mode_0",
            "stat_id":"keep","set":{"_metadata_":"x"}},{"op":"add-row","table":"modes","from":"modes.mode_0",
            "set":{"_metadata_":"y","stat_id":"40"}}]}"#,
        );
        apply_db_ops_report(&mut db, &keep, &catalogue(&[]), &mut DbReport::default()).unwrap();
        assert_eq!(db.get_text("modes.mode_2.stat_id").as_deref(), Some("3"));
        assert_eq!(db.get_text("modes.mode_3.stat_id").as_deref(), Some("40"));

        let mut db = modes(["3", "3"]);
        let fresh = ops(
            r#"{"schema_version":1,"ops":[{"op":"add-row","table":"modes","from":"modes.mode_0",
            "stat_id":"fresh","set":{"_metadata_":"x"}}]}"#,
        );
        apply_db_ops_report(&mut db, &fresh, &catalogue(&[]), &mut DbReport::default()).unwrap();
        assert_eq!(db.get_text("modes.mode_2.stat_id").as_deref(), Some("4"));
    }

    #[test]
    fn a_fields_list_names_extra_keys_and_the_build_reports_what_it_misses() {
        let listed = ["_metadata_", "stat_id"];
        let mut db = modes(["3", "7"]);
        let mut report = DbReport::default();
        apply_db_ops_report(&mut db, &add_mode(""), &catalogue(&listed), &mut report).unwrap();
        assert_eq!(
            db.get_text("modes.mode_2.Com_0.Inner_1.value").as_deref(),
            Some("0.75")
        );
        assert!(
            report
                .lines
                .iter()
                .any(|l| l.contains("copied 9 key(s) the fields list for modes.mode doesn't name")),
            "{:?}",
            report.lines
        );

        let mut db = modes(["3", "7"]);
        let mut report = DbReport::default();
        let legacy = ops(
            r#"{"schema_version":1,"ops":[{"op":"add-row","table":"modes","from":"modes.mode_0",
            "copy":"listed","set":{"_metadata_":"x"}}]}"#,
        );
        apply_db_ops_report(&mut db, &legacy, &catalogue(&listed), &mut report).unwrap();
        assert!(db.get("modes.mode_2.player.driver.preloaded").is_none());
        assert_eq!(report.warnings.len(), 1, "{:?}", report.warnings);
        assert!(
            report.warnings[0].contains(
                "misses 9 key(s) of the source row, not copied: Com_0.Inner_0.value, Com_0.Inner_1.value"
            ),
            "{}",
            report.warnings[0]
        );

        let strict = ops(
            r#"{"schema_version":1,"strict":true,"ops":[{"op":"add-row","table":"modes","from":"modes.mode_0",
            "copy":"listed","set":{"_metadata_":"x"}}]}"#,
        );
        let err = apply_db_ops(&mut modes(["3", "7"]), &strict, &catalogue(&listed)).unwrap_err();
        assert!(err.to_string().ends_with("(strict)"), "{err}");
    }

    #[test]
    fn a_lowered_row_count_in_set_drops_those_rows_from_the_gap_check() {
        let listed = [
            "_metadata_",
            "stat_id",
            "player.driver.preloaded",
            "_Com_instancecount_",
        ];
        let lowered = ops(
            r#"{"schema_version":1,"ops":[{"op":"add-row","table":"modes","from":"modes.mode_0",
            "copy":"listed","set":{"_metadata_":"x","_Com_instancecount_":"0"}}]}"#,
        );
        let mut report = DbReport::default();
        let mut db = modes(["3", "7"]);
        apply_db_ops_report(&mut db, &lowered, &catalogue(&listed), &mut report).unwrap();
        assert_eq!(report.warnings.len(), 1, "{:?}", report.warnings);
        assert!(
            report.warnings[0].contains("misses 2 key(s)")
                && report.warnings[0].ends_with("Loose_0._metadata_, Loose_1._metadata_"),
            "{}",
            report.warnings[0]
        );
    }

    #[test]
    fn a_row_shape_missing_from_the_catalogue_warns_or_fails_when_strict() {
        let mut names = catalogue(&["_metadata_", "stat_id"]);
        names.shapes.remove("modes.mode_N.Com_N");
        let mut report = DbReport::default();
        apply_db_ops_report(&mut modes(["3", "7"]), &add_mode(""), &names, &mut report).unwrap();
        assert_eq!(report.warnings.len(), 1, "{:?}", report.warnings);
        assert!(
            report.warnings[0].contains("the key catalogue has no entry for modes.mode_N.Com_N"),
            "{}",
            report.warnings[0]
        );
        let err = apply_db_ops(&mut modes(["3", "7"]), &add_mode(r#","strict":true"#), &names).unwrap_err();
        assert!(
            err.to_string().contains("no entry for modes.mode_N.Com_N"),
            "{err}"
        );
    }

    #[test]
    fn row_shapes_number_every_row_segment() {
        assert_eq!(row_shape("a.b_3.c.d_12"), "a.b_N.c.d_N");
        assert_eq!(
            row_shape("canned_events.Per-race DNFs.Per-race DNF_5"),
            "canned_events.Per-race DNFs.Per-race DNF_N"
        );
        assert_eq!(row_shape("x_.y"), "x_.y");
    }
}
