use std::collections::{BTreeSet, HashMap};
use std::sync::OnceLock;

use serde::Deserialize;

use super::catalog::plain;
use super::driver_manager::{self, Driver};
use super::{Catalog, Edits, NodeId, rows};
use crate::heads::ProfileHead;
use crate::ops::first_segment;
use crate::tracks::PlanTrack;
use crate::{Error, Result};

const MODULES_JSON: &str = include_str!("modules.json");

/// A friendly screen over one table. Specs live in `modules.json`.
#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Module {
    pub id: String,
    /// Sidebar group: `Drivers`, `Teams & Cars`, `Tracks & Calendar`, `Rules & Modes`.
    pub group: String,
    pub title: String,
    /// Lucide icon name.
    pub icon: String,
    pub summary: String,
    /// Pick a dataset, season or list first; its label fills `{scope}` in `rows`.
    #[serde(default)]
    pub scope: Option<Scope>,
    /// The row table in selector form without the row number: `entrants.drivers.driver`,
    /// `track_data.game_setup.TrackDataSet[_metadata_={scope}].TrackData`.
    pub rows: String,
    /// Keep only rows whose fields match every filter.
    #[serde(default)]
    pub filter: Vec<Filter>,
    #[serde(default = "metadata")]
    pub label: String,
    #[serde(default)]
    pub name: Option<NameSpec>,
    /// Field ids shown as table columns.
    pub columns: Vec<String>,
    pub fields: Vec<FieldSpec>,
    #[serde(default)]
    pub links: Vec<LinkSpec>,
    /// Notices shown above the table.
    #[serde(default)]
    pub notes: Vec<String>,
}

fn metadata() -> String {
    "_metadata_".into()
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Scope {
    pub title: String,
    pub rows: String,
    #[serde(default = "metadata")]
    pub label: String,
    #[serde(default)]
    pub default: Option<String>,
    #[serde(default)]
    pub name: Option<NameSpec>,
    #[serde(default)]
    pub filter: Vec<Filter>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Filter {
    pub field: String,
    /// Keep when the value is one of these.
    #[serde(default)]
    pub any: Vec<String>,
    /// Drop when the value is one of these.
    #[serde(default)]
    pub not: Vec<String>,
    #[serde(default)]
    pub prefix: Option<String>,
}

/// How a row is named: a field (optionally a `lng_*` key shown as English text), or
/// a template over fields such as `{first_name} {last_name}`.
#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NameSpec {
    #[serde(default)]
    pub field: Option<String>,
    #[serde(default)]
    pub template: Option<String>,
    #[serde(default)]
    pub lng: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Kind {
    Int,
    Float,
    Text,
    Bool,
    /// One of `options`.
    Choice,
    /// A label of the table `refs` names.
    Ref,
    /// Comma-joined labels of the table `refs` names, in order.
    RefList,
    List,
    /// ARGB.
    Color,
    Curve,
    /// `HH,MM`.
    Time,
    Vector,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FieldSpec {
    pub id: String,
    pub label: String,
    /// Relative to the row (`race_number`, `ai.pace`) or an absolute selector path with
    /// `{label}` and `{row.<field>}` placeholders.
    pub path: String,
    #[serde(default)]
    pub section: Option<String>,
    /// Defaults from the record's type tag.
    #[serde(default)]
    pub kind: Option<Kind>,
    #[serde(default)]
    pub min: Option<f64>,
    #[serde(default)]
    pub max: Option<f64>,
    #[serde(default)]
    pub step: Option<f64>,
    #[serde(default)]
    pub unit: Option<String>,
    #[serde(default)]
    pub options: Vec<OptionSpec>,
    #[serde(default)]
    pub refs: Option<RefSpec>,
    #[serde(default)]
    pub help: Option<String>,
    /// The value is a `lng_*` key; show its English text next to it.
    #[serde(default)]
    pub lng: bool,
    #[serde(default)]
    pub readonly: bool,
    /// Rows without this key show an empty cell instead of failing the module.
    #[serde(default)]
    pub optional: bool,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct OptionSpec {
    pub value: String,
    pub label: String,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RefSpec {
    pub rows: String,
    #[serde(default = "metadata")]
    pub label: String,
    #[serde(default)]
    pub name: Option<NameSpec>,
    #[serde(default)]
    pub filter: Vec<Filter>,
}

/// A link out of a module: to an addon's launcher page (and tab), or a URL when the
/// addon is not installed.
#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LinkSpec {
    pub label: String,
    pub text: String,
    #[serde(default)]
    pub addon: Option<String>,
    #[serde(default)]
    pub section: Option<String>,
    #[serde(default)]
    pub tab: Option<String>,
    #[serde(default)]
    pub url: Option<String>,
}

#[derive(Debug, Clone)]
pub struct Column {
    /// Index into `Module::fields`.
    pub field: usize,
    pub label: String,
}

#[derive(Debug, Clone)]
pub struct Cell {
    pub key: Option<u64>,
    pub tag: u8,
    pub stock: String,
    pub value: String,
    pub edited: bool,
    /// English text for `lng` fields and the display name for `ref` fields.
    pub text: Option<String>,
}

#[derive(Debug, Clone)]
pub struct ModuleRow {
    pub label: String,
    pub name: String,
    pub node: NodeId,
    /// One per `Module::fields`, in order.
    pub cells: Vec<Cell>,
    /// One per [`Module::row_specs`], in order.
    pub backing: Vec<Backing>,
}

/// A table a page's fields pick a row of by the page row's label, through a
/// `[<field>={label}]` selector.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RowSpec {
    /// `driver_statistics.driver_statistics.DriverStatisticsHolder`; may hold `{scope}`.
    pub table: String,
    pub field: String,
}

/// Where a page row's row of a [`RowSpec`] table comes from.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Backing {
    /// The stock database has it.
    Stock,
    /// The draft copies it from the row whose field is this label, or from the game default row.
    Copied(Option<String>),
    Missing,
}

#[derive(Debug, Clone)]
pub struct ModuleView {
    /// (label, display name) of each scope choice.
    pub scopes: Vec<(String, String)>,
    pub scope: Option<String>,
    pub columns: Vec<Column>,
    pub rows: Vec<ModuleRow>,
    /// Paths of non-optional fields some row lacks.
    pub missing: Vec<String>,
}

#[derive(Deserialize)]
struct File {
    modules: Vec<Module>,
}

pub fn modules() -> &'static [Module] {
    static MODULES: OnceLock<Vec<Module>> = OnceLock::new();
    MODULES.get_or_init(|| {
        serde_json::from_str::<File>(MODULES_JSON)
            .expect("modules.json matches the module schema")
            .modules
    })
}

pub fn module(id: &str) -> Option<&'static Module> {
    modules().iter().find(|m| m.id == id)
}

/// The value of `rel` under `row`, read through `edits` when given.
fn under<'a>(cat: &'a Catalog, edits: Option<&'a Edits>, row: u64, rel: &str) -> Option<&'a str> {
    let key = cat.state(Some(row), rel).ok()?;
    match edits {
        Some(e) => e.value(cat, key),
        None => cat.value_of(key),
    }
}

fn keep(cat: &Catalog, edits: Option<&Edits>, row: u64, filters: &[Filter]) -> bool {
    filters.iter().all(|f| {
        let v = under(cat, edits, row, &f.field);
        (f.any.is_empty() || v.is_some_and(|v| f.any.iter().any(|a| a == v)))
            && !v.is_some_and(|v| f.not.iter().any(|n| n == v))
            && f.prefix
                .as_ref()
                .is_none_or(|p| v.unwrap_or("").starts_with(p.as_str()))
    })
}

fn table_rows(
    cat: &Catalog,
    edits: Option<&Edits>,
    rows: &str,
    filters: &[Filter],
    scope: Option<&str>,
) -> std::result::Result<Vec<u64>, String> {
    let rows = match scope {
        Some(s) => rows.replace("{scope}", s),
        None => rows.to_owned(),
    };
    let (container, word) = rows
        .rsplit_once('.')
        .filter(|(_, w)| !w.contains(['[', ']']))
        .ok_or_else(|| format!("{rows}: rows must end in .<word>"))?;
    let table = cat.state(None, container)?;
    Ok(cat
        .rows_of(table, word)
        .into_iter()
        .filter(|&r| keep(cat, edits, r, filters))
        .collect())
}

fn english(cat: &Catalog, v: &str) -> Option<String> {
    cat.text(v).map(plain).filter(|t| !t.is_empty())
}

fn name_of(cat: &Catalog, row: u64, spec: Option<&NameSpec>, label: &str) -> String {
    name_edited(cat, None, row, spec, label)
}

/// The row's name, read through `edits` when given.
fn name_edited(
    cat: &Catalog,
    edits: Option<&Edits>,
    row: u64,
    spec: Option<&NameSpec>,
    label: &str,
) -> String {
    let lng = spec.is_some_and(|s| s.lng);
    let value = |field: &str| {
        let key = cat.state(Some(row), field).ok();
        let edited = key.and_then(|k| edits?.get(k)).and_then(|e| e.value.as_deref());
        let v = edited.or_else(|| key.and_then(|k| cat.value_of(k))).unwrap_or("");
        match lng {
            true => english(cat, v),
            false => Some(v.to_owned()),
        }
    };
    let text = match spec {
        None => None,
        Some(NameSpec {
            template: Some(t), ..
        }) => fill(t, value).ok(),
        Some(NameSpec { field: Some(f), .. }) => value(f),
        Some(_) => None,
    };
    match text.as_deref().map(str::trim) {
        Some(t) if !t.is_empty() => t.to_owned(),
        _ if lng => title_case(label),
        _ => label.to_owned(),
    }
}

/// The name a head an addon puts shows: `Rio Haryanto (Rio Haryanto facemod)`.
fn head_title(h: &ProfileHead) -> String {
    let name = title_case(&h.label);
    match &h.addon_name {
        Some(addon) => format!("{name} ({addon})"),
        None => name,
    }
}

fn title_case(label: &str) -> String {
    label
        .split(['_', '-'])
        .filter(|w| !w.is_empty())
        .map(|w| {
            let mut c = w.chars();
            c.next()
                .map(|f| f.to_uppercase().chain(c).collect::<String>())
                .unwrap_or_default()
        })
        .collect::<Vec<_>>()
        .join(" ")
}

/// Replaces each `{name}` through `sub`; `{<16 hex>}` record placeholders stay.
fn fill(text: &str, mut sub: impl FnMut(&str) -> Option<String>) -> std::result::Result<String, String> {
    let mut out = String::with_capacity(text.len());
    let mut rest = text;
    while let Some(i) = rest.find('{') {
        out.push_str(&rest[..i]);
        let Some(j) = rest[i..].find('}') else {
            break;
        };
        let k = &rest[i + 1..i + j];
        match k.len() == 16 && k.bytes().all(|b| b.is_ascii_hexdigit()) {
            true => out.push_str(&rest[i..=i + j]),
            false => out.push_str(&sub(k).ok_or_else(|| format!("{text}: no value for {{{k}}}"))?),
        }
        rest = &rest[i + j + 1..];
    }
    out.push_str(rest);
    Ok(out)
}

fn ref_names(cat: &Catalog, spec: &RefSpec, scope: Option<&str>) -> Vec<(String, String)> {
    let Ok(rows) = table_rows(cat, None, &spec.rows, &spec.filter, scope) else {
        return Vec::new();
    };
    rows.into_iter()
        .filter_map(|r| {
            let label = under(cat, None, r, &spec.label)?.to_owned();
            Some((name_of(cat, r, spec.name.as_ref(), &label), label))
        })
        .map(|(name, label)| (label, name))
        .collect()
}

/// The table and field of the first `[<field>={label}]` selector in `path`.
fn label_table(path: &str) -> Option<RowSpec> {
    let mut rest = Some(path);
    while let Some(p) = rest {
        let (seg, tail) = first_segment(p).ok()?;
        if let Some((field, "{label}")) = seg.select {
            let start = path.len() - p.len();
            return Some(RowSpec {
                table: format!("{}{}", &path[..start], seg.name),
                field: field.to_owned(),
            });
        }
        rest = tail;
    }
    None
}

impl Module {
    /// The tables this page's fields pick a row of by the row's label, in field order.
    pub fn row_specs(&self) -> Vec<RowSpec> {
        let mut out: Vec<RowSpec> = Vec::new();
        for spec in self.fields.iter().filter_map(|f| label_table(&f.path)) {
            if !out.contains(&spec) {
                out.push(spec);
            }
        }
        out
    }

    /// The index in [`Module::row_specs`] of the table field `field` reads.
    pub fn field_row(&self, field: usize) -> Option<usize> {
        self.field_rows().get(field).copied().flatten()
    }

    /// [`Module::field_row`] of every field, in order.
    pub fn field_rows(&self) -> Vec<Option<usize>> {
        let specs = self.row_specs();
        self.fields
            .iter()
            .map(|f| {
                let spec = label_table(&f.path)?;
                specs.iter().position(|s| *s == spec)
            })
            .collect()
    }

    /// The table of a row spec with `{scope}` filled.
    pub fn row_table(&self, spec: &RowSpec, scope: Option<&str>) -> String {
        match scope {
            Some(s) => spec.table.replace("{scope}", s),
            None => spec.table.clone(),
        }
    }

    /// The values of `field` of the stock rows a copy can start from, and
    /// whether the table has a game default row (the one whose field is empty).
    pub fn row_sources(&self, catalog: &Catalog, spec: usize, scope: Option<&str>) -> (Vec<String>, bool) {
        let Some(spec) = self.row_specs().into_iter().nth(spec) else {
            return (Vec::new(), false);
        };
        let table = self.row_table(&spec, scope);
        let mut labels = Vec::new();
        let mut defaults = 0;
        for r in rows::rows(catalog, &table) {
            match rows::value_in(catalog, r, &spec.field) {
                Some("") => defaults += 1,
                Some(v) if !labels.iter().any(|l| l == v) => labels.push(v.to_owned()),
                _ => {}
            }
        }
        (labels, defaults == 1)
    }

    fn backing(
        &self,
        cat: &Catalog,
        edits: &Edits,
        specs: &[RowSpec],
        label: &str,
        scope: Option<&str>,
    ) -> Vec<Backing> {
        specs
            .iter()
            .map(|spec| {
                let table = self.row_table(spec, scope);
                if !rows::matching(cat, &table, &spec.field, label).is_empty() {
                    return Backing::Stock;
                }
                match edits.row(&table, &spec.field, label) {
                    Some(r) => Backing::Copied(r.from.clone()),
                    None => Backing::Missing,
                }
            })
            .collect()
    }

    fn field_key(
        &self,
        cat: &Catalog,
        edits: &Edits,
        row: u64,
        label: &str,
        scope: Option<&str>,
        path: &str,
    ) -> Option<u64> {
        if !path.contains('{') {
            return cat.state(Some(row), path).ok();
        }
        let path = fill(path, |k| match k {
            "label" => Some(label.to_owned()),
            "scope" => scope.map(str::to_owned),
            k => k
                .strip_prefix("row.")
                .and_then(|f| under(cat, Some(edits), row, f))
                .map(str::to_owned),
        })
        .ok()?;
        cat.state(None, &path)
            .ok()
            .or_else(|| edits.copied_path(cat, &path).map(|c| c.key))
    }

    fn scopes(&self, cat: &Catalog) -> Result<Vec<(String, String)>> {
        let Some(sc) = &self.scope else {
            return Ok(Vec::new());
        };
        let rows = table_rows(cat, None, &sc.rows, &sc.filter, None).map_err(Error::Resolve)?;
        Ok(rows
            .into_iter()
            .filter_map(|r| {
                let label = under(cat, None, r, &sc.label)?.to_owned();
                Some((label.clone(), name_of(cat, r, sc.name.as_ref(), &label)))
            })
            .collect())
    }

    fn pick_scope(&self, scopes: &[(String, String)], want: Option<&str>) -> Result<Option<String>> {
        let Some(sc) = &self.scope else {
            return Ok(None);
        };
        let has = |s: &str| scopes.iter().any(|(l, _)| l == s);
        if let Some(w) = want {
            return match has(w) {
                true => Ok(Some(w.to_owned())),
                false => Err(Error::Resolve(format!(
                    "{}: no {} named {w}",
                    self.title, sc.title
                ))),
            };
        }
        Ok(sc
            .default
            .clone()
            .filter(|d| has(d))
            .or_else(|| scopes.first().map(|(l, _)| l.clone())))
    }

    pub fn view(&self, catalog: &Catalog, edits: &Edits, scope: Option<&str>) -> Result<ModuleView> {
        let cat = catalog;
        let scopes = self.scopes(cat)?;
        let scope = self.pick_scope(&scopes, scope)?;
        if self.scope.is_some() && scope.is_none() {
            return Ok(ModuleView {
                scopes,
                scope,
                columns: self.columns(),
                rows: Vec::new(),
                missing: Vec::new(),
            });
        }
        let rows = table_rows(cat, Some(edits), &self.rows, &self.filter, scope.as_deref())
            .map_err(Error::Resolve)?;
        let mut refs: HashMap<usize, HashMap<String, String>> = HashMap::new();
        let mut missing = BTreeSet::new();
        let specs = self.row_specs();
        let mut out = Vec::with_capacity(rows.len());
        for row in rows {
            let label = under(cat, None, row, &self.label).unwrap_or_default().to_owned();
            let name = name_edited(cat, Some(edits), row, self.name.as_ref(), &label);
            let mut cells = Vec::with_capacity(self.fields.len());
            for (i, f) in self.fields.iter().enumerate() {
                let key = self.field_key(cat, edits, row, &label, scope.as_deref(), &f.path);
                let rec = key.and_then(|k| {
                    cat.record_by_key(k)
                        .or_else(|| cat.record_by_key(edits.copied(cat, k)?.source))
                });
                let edit = key.and_then(|k| edits.get(k));
                let (Some(k), true) = (key, rec.is_some() || edit.is_some()) else {
                    if !f.optional {
                        missing.insert(f.path.clone());
                    }
                    cells.push(Cell {
                        key: None,
                        tag: 0,
                        stock: String::new(),
                        value: String::new(),
                        edited: false,
                        text: None,
                    });
                    continue;
                };
                let value = edits.value(cat, k).unwrap_or_default().to_owned();
                let text = match (f.lng, f.kind, &f.refs) {
                    (true, _, _) => english(cat, &value),
                    (_, Some(Kind::Ref | Kind::RefList), Some(spec)) => {
                        let names = refs
                            .entry(i)
                            .or_insert_with(|| ref_names(cat, spec, scope.as_deref()).into_iter().collect());
                        let shown: Vec<&str> = value
                            .split(',')
                            .filter(|p| !p.is_empty())
                            .map(|p| names.get(p).map_or(p, String::as_str))
                            .collect();
                        Some(shown.join(", ")).filter(|t| !t.is_empty() && *t != value)
                    }
                    (_, Some(Kind::Choice), _) => f
                        .options
                        .iter()
                        .find(|o| o.value == value)
                        .map(|o| o.label.clone()),
                    _ => None,
                };
                cells.push(Cell {
                    key: Some(k),
                    tag: rec.map(|r| r.tag).or(edit.map(|e| e.tag)).unwrap_or(0),
                    stock: rec.map(|r| r.value.to_owned()).unwrap_or_default(),
                    edited: edit.is_some(),
                    value,
                    text,
                });
            }
            out.push(ModuleRow {
                node: cat.lookup(row).unwrap_or(0),
                backing: self.backing(cat, edits, &specs, &label, scope.as_deref()),
                label,
                name,
                cells,
            });
        }
        Ok(ModuleView {
            scopes,
            scope,
            columns: self.columns(),
            rows: out,
            missing: missing.into_iter().collect(),
        })
    }

    fn columns(&self) -> Vec<Column> {
        self.columns
            .iter()
            .filter_map(|id| {
                let field = self.fields.iter().position(|f| &f.id == id)?;
                Some(Column {
                    field,
                    label: self.fields[field].label.clone(),
                })
            })
            .collect()
    }

    /// (value, display name) choices for a `ref`/`ref-list` field.
    pub fn choices(&self, catalog: &Catalog, field: usize) -> Vec<(String, String)> {
        let scope = self
            .scopes(catalog)
            .ok()
            .and_then(|s| self.pick_scope(&s, None).ok().flatten());
        self.choices_in(catalog, field, scope.as_deref())
    }

    /// Like `choices`, for the rows of one scope.
    pub fn choices_in(&self, catalog: &Catalog, field: usize, scope: Option<&str>) -> Vec<(String, String)> {
        self.choices_with(catalog, field, scope, &[], &[], &[])
    }

    /// Like `choices_in`, where the field picks a track also offering the
    /// tracks track addons add, and naming a stock slot after the addon that fills it;
    /// where it picks a driver or entrant, also offering the drivers Driver Manager adds;
    /// where it picks a head, also offering the heads addons put.
    pub fn choices_with(
        &self,
        catalog: &Catalog,
        field: usize,
        scope: Option<&str>,
        tracks: &[PlanTrack],
        drivers: &[Driver],
        heads: &[ProfileHead],
    ) -> Vec<(String, String)> {
        let Some(f) = self.fields.get(field) else {
            return Vec::new();
        };
        match &f.refs {
            Some(spec) => {
                let mut names = ref_names(catalog, spec, scope);
                if spec.rows == TRACK_ROWS {
                    add_tracks(&mut names, tracks);
                }
                if driver_manager::TABLES.contains(&spec.rows.as_str()) {
                    for d in drivers {
                        if !names.iter().any(|(l, _)| *l == d.label) {
                            names.push((d.label.clone(), d.title()));
                        }
                    }
                }
                if spec.rows == crate::heads::ROWS {
                    for h in heads {
                        if !names.iter().any(|(l, _)| *l == h.label) {
                            names.push((h.label.clone(), head_title(h)));
                        }
                    }
                }
                distinct(names)
            }
            None => f
                .options
                .iter()
                .map(|o| (o.value.clone(), o.label.clone()))
                .collect(),
        }
    }

    /// Whether `field` takes one value the page can set on many rows at once: not
    /// read-only, not the row's label or a `lng_*` key, and not a list, curve or vector.
    pub fn sets_all(&self, field: usize, tag: u8) -> bool {
        let Some(f) = self.fields.get(field) else {
            return false;
        };
        let many = match f.kind {
            Some(k) => matches!(k, Kind::List | Kind::RefList | Kind::Curve | Kind::Vector),
            None => matches!(tag, 9 | 13 | 14),
        };
        !f.readonly && !f.lng && f.path != self.label && !many
    }

    /// `value` held inside `field`'s range. Values that aren't numbers stay as they are.
    pub fn clamp(&self, field: usize, value: &str) -> String {
        let Some(f) = self.fields.get(field) else {
            return value.to_owned();
        };
        let Ok(n) = value.trim().parse::<f64>() else {
            return value.to_owned();
        };
        let held = match (f.min, f.max) {
            (Some(min), _) if n < min => min,
            (_, Some(max)) if n > max => max,
            _ => return value.to_owned(),
        };
        format!("{held}")
    }

    /// The keys `field` reads in `rows` of `view`, each once, skipping rows without one.
    pub fn keys_of(&self, view: &ModuleView, rows: &[usize], field: usize) -> Vec<u64> {
        let mut keys: Vec<u64> = Vec::new();
        for k in rows
            .iter()
            .filter_map(|&r| view.rows.get(r)?.cells.get(field)?.key)
        {
            if !keys.contains(&k) {
                keys.push(k);
            }
        }
        keys
    }

    /// Sets `field` to `value`, held inside its range, in `rows` of `view` as one change:
    /// every value or, on an error, none. Returns how many values changed.
    pub fn set_all(
        &self,
        catalog: &Catalog,
        edits: &mut Edits,
        view: &ModuleView,
        rows: &[usize],
        field: usize,
        value: &str,
    ) -> std::result::Result<usize, String> {
        let Some(f) = self.fields.get(field) else {
            return Err(format!("{}: no field {field}", self.title));
        };
        let value = self.clamp(field, value);
        let keys = self.keys_of(view, rows, field);
        let mut next = edits.clone();
        let mut changed = 0;
        for k in keys {
            let tag = catalog.record_by_key(k).map_or(2, |r| r.tag);
            if !self.sets_all(field, tag) {
                return Err(format!("{} can't be set on every row", f.label));
            }
            if next.value(catalog, k) == Some(value.as_str()) {
                continue;
            }
            next.set(catalog, k, &value)?;
            changed += 1;
        }
        *edits = next;
        Ok(changed)
    }
}

const TRACK_ROWS: &str = "tracks.Track";

fn add_tracks(names: &mut Vec<(String, String)>, tracks: &[PlanTrack]) {
    for t in tracks {
        match names.iter_mut().find(|(label, _)| *label == t.code) {
            Some((_, have)) => {
                if let Some(name) = &t.name {
                    *have = name.clone();
                }
            }
            None => names.push((t.code.clone(), t.name.clone().unwrap_or_else(|| t.code.clone()))),
        }
    }
}

/// A name more than one choice has gets each choice's label after it.
fn distinct(names: Vec<(String, String)>) -> Vec<(String, String)> {
    let mut count: HashMap<&str, usize> = HashMap::new();
    for (_, name) in &names {
        *count.entry(name.as_str()).or_default() += 1;
    }
    let shared: BTreeSet<String> = count
        .into_iter()
        .filter(|(_, n)| *n > 1)
        .map(|(name, _)| name.to_owned())
        .collect();
    names
        .into_iter()
        .map(|(label, name)| {
            let name = match shared.contains(&name) && name != label {
                true => format!("{name} ({label})"),
                false => name,
            };
            (label, name)
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_module_spec_parses_and_names_its_columns() {
        assert!(modules().len() >= 2);
        for m in modules() {
            assert_eq!(m.columns().len(), m.columns.len(), "{}: unknown column", m.id);
        }
        assert_eq!(
            fill("a.{label}.{0043bb1eadb80344}", |_| Some("x".into())).unwrap(),
            "a.x.{0043bb1eadb80344}"
        );
    }

    #[test]
    fn track_choices_offer_added_tracks_and_slots_filled_by_addons() {
        use swofty_formats::gvdb::{Gvdb, Record};
        let rec = |path: &str, value: &str| Record::new(path, 2, value);
        let mut records = vec![
            Record::new("tracks._Track_instancecount_", 0, "3"),
            Record::new(
                "schedule.activities._activity_race_weekend_instancecount_",
                0,
                "1",
            ),
            rec(
                "schedule.activities.activity_race_weekend_0._metadata_",
                "singapore",
            ),
            rec("schedule.activities.activity_race_weekend_0.track", "singapore"),
        ];
        for (i, code) in ["singapore", "bahrain", "bahrain24"].into_iter().enumerate() {
            records.push(rec(&format!("tracks.Track_{i}._metadata_"), code));
            records.push(rec(&format!("tracks.Track_{i}.gp_short_name"), "lng_x"));
        }
        let cat = Catalog::load(&Gvdb { records }.to_bytes()).unwrap();
        let weekends = module("race-weekends").unwrap();
        let track = weekends.fields.iter().position(|f| f.id == "track").unwrap();
        let view = weekends.view(&cat, &Edits::default(), None).unwrap();
        assert_eq!(view.rows.len(), 1);
        assert_eq!(view.rows[0].name, "Singapore");
        assert_eq!(view.rows[0].cells[track].value, "singapore");
        let stock = weekends.choices_in(&cat, track, None);
        assert_eq!(
            stock.iter().map(|(v, _)| v.as_str()).collect::<Vec<_>>(),
            ["singapore", "bahrain", "bahrain24"]
        );
        let added = [
            PlanTrack {
                code: "bahrain24".into(),
                name: Some("Sepang".into()),
                addon: Some("mod.bahrain24".into()),
            },
            PlanTrack {
                code: "okayama".into(),
                name: None,
                addon: Some("mod.okayama".into()),
            },
        ];
        let with = weekends.choices_with(&cat, track, None, &added, &[], &[]);
        let named = |v: &str| with.iter().find(|(c, _)| c == v).map(|(_, n)| n.as_str());
        assert_eq!(named("bahrain24"), Some("Sepang"));
        assert_eq!(named("okayama"), Some("okayama"));
        assert_eq!(with.len(), 4);
        let calendar = module("calendar").unwrap();
        let event = calendar.fields.iter().position(|f| f.id == "event_name").unwrap();
        assert!(
            calendar
                .choices_with(&cat, event, None, &added, &[], &[])
                .is_empty()
        );
    }

    #[test]
    fn driver_choices_offer_the_drivers_driver_manager_adds() {
        use crate::dbedit::driver_manager::{self, tests};
        let cat = Catalog::from_gvdb(tests::database(), String::new());
        let dir = tests::temp("choices");
        tests::fixture(&dir);
        let drivers = driver_manager::drivers_in(&dir, &cat);
        let lineups = module("lineups").unwrap();
        let at = |m: &Module, id: &str| m.fields.iter().position(|f| f.id == id).unwrap();
        let seat = at(lineups, "entrant_reference");
        assert_eq!(
            lineups.choices_with(&cat, seat, Some("f1_2026"), &[], &drivers, &[]),
            [
                ("max_verstappen".to_owned(), "max_verstappen".to_owned()),
                ("dm_208".to_owned(), "Jacob Swofty (Driver Manager)".to_owned()),
                (
                    "dm_209".to_owned(),
                    "Ret Ired (Driver Manager, retired)".to_owned()
                ),
            ]
        );
        assert_eq!(lineups.choices_in(&cat, seat, Some("f1_2026")).len(), 1);
        for (m, field) in [
            ("lineups", "driver"),
            ("lineups", "entrant_driver"),
            ("entrants", "driver"),
            ("driver-looks", "linked_driver_stat_id"),
            ("retirements-2025", "driver"),
        ] {
            let m = module(m).unwrap();
            let choices = m.choices_with(&cat, at(m, field), None, &[], &drivers, &[]);
            assert!(
                choices.iter().any(|(v, _)| v == "dm_208"),
                "{}.{field}: {choices:?}",
                m.id
            );
        }
        for (m, field) in [("drivers", "nationality"), ("race-weekends", "track")] {
            let m = module(m).unwrap();
            let choices = m.choices_with(&cat, at(m, field), None, &[], &drivers, &[]);
            assert!(
                !choices.iter().any(|(v, _)| v.starts_with("dm_")),
                "{}.{field}",
                m.id
            );
        }

        let view = lineups.view(&cat, &Edits::default(), None).unwrap();
        let key = view.rows[0].cells[seat].key.unwrap();
        let mut edits = Edits::default();
        edits.set(&cat, key, "dm_208").unwrap();
        let after = lineups.view(&cat, &edits, None).unwrap();
        assert_eq!(after.rows[0].cells[seat].value, "dm_208");
        assert_eq!(driver_manager::picks(&edits), ["dm_208"]);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn head_choices_offer_the_heads_addons_put_beside_the_stock_ones() {
        use crate::dbedit::driver_manager::tests;
        let cat = Catalog::from_gvdb(tests::database(), String::new());
        let head = |label: &str, addon: &str| ProfileHead {
            label: label.into(),
            addon: Some(format!("mod.{label}")),
            addon_name: Some(addon.into()),
            female: false,
        };
        let heads = [
            head("rio_haryanto", "Rio Haryanto facemod"),
            head("male_ls144", "Face pack"),
        ];
        let drivers = module("drivers").unwrap();
        let field = drivers.fields.iter().position(|f| f.id == "head").unwrap();
        let choices = drivers.choices_with(&cat, field, None, &[], &[], &heads);
        assert_eq!(
            choices.iter().filter(|(v, _)| v == "male_ls144").count(),
            1,
            "{choices:?}"
        );
        assert!(
            choices.contains(&(
                "rio_haryanto".to_owned(),
                "Rio Haryanto (Rio Haryanto facemod)".to_owned()
            )),
            "{choices:?}"
        );
        let nationality = drivers.fields.iter().position(|f| f.id == "nationality").unwrap();
        let other = drivers.choices_with(&cat, nationality, None, &[], &[], &heads);
        assert!(!other.iter().any(|(v, _)| v == "rio_haryanto"));
    }

    #[test]
    fn sets_one_value_on_every_listed_row_held_in_range() {
        use swofty_formats::gvdb::{Gvdb, Record};
        let mut records = vec![Record::new("entrants.drivers._driver_instancecount_", 0, "4")];
        for (i, (label, mistakes)) in [("a", "0.5"), ("b", "0.2"), ("c", "1"), ("d", "0.4")]
            .into_iter()
            .enumerate()
        {
            let row = format!("entrants.drivers.driver_{i}");
            records.push(Record::new(&format!("{row}._metadata_"), 2, label));
            records.push(Record::new(&format!("{row}.full_name"), 2, "lng_x"));
            records.push(Record::new(&format!("{row}.car_number"), 0, "7"));
            if label != "d" {
                records.push(Record::new(
                    &format!("{row}.driver_characteristics.mistakes"),
                    1,
                    mistakes,
                ));
            }
        }
        let cat = Catalog::load(&Gvdb { records }.to_bytes()).unwrap();
        let drivers = module("drivers").unwrap();
        let at = |id: &str| drivers.fields.iter().position(|f| f.id == id).unwrap();
        let (mistakes, number, name) = (at("trait_mistakes"), at("car_number"), at("full_name"));
        let view = drivers.view(&cat, &Edits::default(), None).unwrap();
        let key = |row: usize| view.rows[row].cells[mistakes].key.unwrap();

        assert!(drivers.sets_all(mistakes, 1) && drivers.sets_all(number, 0));
        assert!(!drivers.sets_all(name, 2), "a lng_* key");
        assert_eq!(drivers.clamp(mistakes, "1.5"), "1");
        assert_eq!(drivers.clamp(mistakes, "-2"), "0");
        assert_eq!(drivers.clamp(mistakes, "0.75"), "0.75");
        assert_eq!(drivers.clamp(mistakes, "high"), "high");
        assert_eq!(
            drivers.keys_of(&view, &[0, 2, 3], mistakes).len(),
            2,
            "d has no value"
        );

        let mut edits = Edits::default();
        let n = drivers
            .set_all(&cat, &mut edits, &view, &[0, 2, 3], mistakes, "9")
            .unwrap();
        assert_eq!(n, 1, "c already holds the max");
        assert_eq!(edits.value(&cat, key(0)), Some("1"));
        assert_eq!(edits.value(&cat, key(1)), Some("0.2"), "b is not listed");
        assert_eq!(edits.len(), 1);

        let n = drivers
            .set_all(&cat, &mut edits, &view, &[0, 1, 2], mistakes, "0.3")
            .unwrap();
        assert_eq!(n, 3);
        assert!(
            [0, 1, 2]
                .iter()
                .all(|&r| edits.value(&cat, key(r)) == Some("0.3"))
        );

        let before = edits.clone();
        assert!(
            drivers
                .set_all(&cat, &mut edits, &view, &[0, 1], number, "seven")
                .is_err()
        );
        assert_eq!(edits, before, "a failed change leaves every value as it was");
        assert!(
            drivers
                .set_all(&cat, &mut edits, &view, &[0, 1], name, "lng_y")
                .is_err()
        );
    }

    #[test]
    fn shared_choice_names_show_their_labels() {
        let names = |v: &[(&str, &str)]| -> Vec<(String, String)> {
            v.iter()
                .map(|(a, b)| ((*a).to_owned(), (*b).to_owned()))
                .collect()
        };
        assert_eq!(
            distinct(names(&[
                ("bahrain", "Bahrain"),
                ("bahrain24", "Bahrain"),
                ("suzuka", "Japan"),
                ("x", "x"),
                ("y", "x"),
            ])),
            names(&[
                ("bahrain", "Bahrain (bahrain)"),
                ("bahrain24", "Bahrain (bahrain24)"),
                ("suzuka", "Japan"),
                ("x", "x"),
                ("y", "x (y)"),
            ])
        );
        let mut picked = names(&[("bahrain24", "Bahrain")]);
        add_tracks(
            &mut picked,
            &[PlanTrack {
                code: "bahrain24".into(),
                name: None,
                addon: None,
            }],
        );
        assert_eq!(picked, names(&[("bahrain24", "Bahrain")]));
    }

    #[test]
    fn every_module_resolves_on_the_real_database() {
        let Some(cat) = crate::dbedit::catalog::tests::real() else {
            eprintln!("F1_25_DIR is not set; skipping");
            return;
        };
        let edits = Edits::default();
        let mut failed = Vec::new();
        for m in modules() {
            let t = std::time::Instant::now();
            let scopes = m.scopes(&cat).unwrap();
            let picks: Vec<Option<&str>> = match m.scope {
                Some(_) => scopes.iter().map(|(l, _)| Some(l.as_str())).collect(),
                None => vec![None],
            };
            let mut rows = 0;
            for scope in picks {
                match m.view(&cat, &edits, scope) {
                    Ok(v) => {
                        rows += v.rows.len();
                        for p in v.missing {
                            failed.push(format!("{} ({scope:?}): {p}", m.id));
                        }
                    }
                    Err(e) => failed.push(format!("{} ({scope:?}): {e}", m.id)),
                }
            }
            eprintln!(
                "{}: {rows} rows over {} scopes in {:?}",
                m.id,
                scopes.len().max(1),
                t.elapsed()
            );
            if rows == 0 {
                failed.push(format!("{}: no rows", m.id));
            }
        }
        assert!(failed.is_empty(), "{}", failed.join("\n"));
        let drivers = module("drivers").unwrap().view(&cat, &edits, None).unwrap();
        let max = drivers.rows.iter().find(|r| r.label == "max_verstappen").unwrap();
        eprintln!("max_verstappen: {}", max.name);
        assert!(
            cat.path(max.node).starts_with("entrants.drivers.driver_"),
            "{}",
            cat.path(max.node)
        );

        let lineups = module("lineups").unwrap();
        let col = |id: &str| lineups.fields.iter().position(|f| f.id == id).unwrap();
        let (reference, driver) = (col("entrant_reference"), col("entrant_driver"));
        let view = lineups.view(&cat, &edits, None).unwrap();
        let seat = &view.rows[0];
        let other = view
            .rows
            .iter()
            .map(|r| r.cells[reference].value.clone())
            .find(|v| *v != seat.cells[reference].value)
            .unwrap();
        let mut moved = Edits::default();
        moved
            .set(&cat, seat.cells[reference].key.unwrap(), &other)
            .unwrap();
        let after = lineups.view(&cat, &moved, view.scope.as_deref()).unwrap();
        let want = cat
            .resolve(&format!(
                "entrants.entrant_lists.entrants.entrant[_metadata_={other}].driver"
            ))
            .unwrap();
        assert_eq!(after.rows[0].cells[driver].key, Some(want));
        assert_ne!(after.rows[0].cells[driver].key, seat.cells[driver].key);

        let calendar = module("calendar").unwrap();
        let before = calendar.view(&cat, &edits, None).unwrap();
        let (a, b) = (&before.rows[0], &before.rows[1]);
        let event = calendar.fields.iter().position(|f| f.id == "event_name").unwrap();
        let mut swapped = Edits::default();
        swapped
            .set(&cat, a.cells[event].key.unwrap(), &b.cells[event].value)
            .unwrap();
        let after = calendar.view(&cat, &swapped, before.scope.as_deref()).unwrap();
        assert_eq!(after.rows[0].cells[event].value, b.cells[event].value);
        assert_ne!(after.rows[0].name, a.name, "the name follows the edited event");

        let weekends = module("race-weekends").unwrap();
        let track = weekends.fields.iter().position(|f| f.id == "track").unwrap();
        let choices = weekends.choices_in(&cat, track, None);
        eprintln!("race weekend tracks: {choices:?}");
        assert!(choices.iter().any(|(v, _)| v == "bahrain24"), "{choices:?}");
        let names: BTreeSet<&str> = choices.iter().map(|(_, n)| n.as_str()).collect();
        assert_eq!(names.len(), choices.len(), "{choices:?}");
    }
}
