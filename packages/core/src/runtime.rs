use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::path::{Path, PathBuf};

use serde::Serialize;
use swofty_formats::erp::{Erp, ErpFile, Resource};
use swofty_formats::gvdb::Gvdb;
use swofty_formats::lng::Lng;

use crate::asset::{AssetOp, FileOp, GroupOp, Kind, LangOp, Op, Source};
use crate::manifest::relative_inside;
use crate::{Error, Result, io_err};

pub const ASSETS_FILE: &str = "swofty/assets.json";
const SWOFTY: &str = "swofty";
const RESOURCES_DIR: &str = "assets/resources";
const LANG_DIR: &str = "assets/lang";
const OVERLAY_DIR: &str = "overlay";
/// The active shader look's packed LUT, under `swofty/`.
pub const SHADER_LUT: &str = "shaders/look.lut";
/// The compiled post-process passes, under `swofty/`.
pub const POSTFX_DIR: &str = "postfx";
const NO_VALUE_TAGS: [u8; 3] = [10, 15, 16];

#[derive(Debug, Clone, Default, PartialEq, Serialize)]
pub struct Assets {
    #[serde(skip_serializing_if = "std::ops::Not::not")]
    pub early_stage: bool,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub features: Vec<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub asset_groups: Vec<Group>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub resource_erps: Vec<ResErp>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub db_edits: Vec<DbEdit>,
    #[serde(skip_serializing_if = "BTreeMap::is_empty")]
    pub lang: BTreeMap<String, LangTable>,
    #[serde(skip_serializing_if = "std::ops::Not::not")]
    pub overlay: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub shader: Option<ShaderLook>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub postfx: Vec<PostFxPass>,
}

/// A post-process pass the in-game core runs, in the order listed.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct PostFxPass {
    /// The DXIL container, relative to `swofty/`.
    pub shader: String,
    pub name: String,
    pub addon: String,
    /// The values the shader reads with `option(i)`.
    pub options: Vec<f64>,
    pub budget_ms: f64,
}

/// The look the in-game core grades the picture with.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct ShaderLook {
    /// The packed LUT, relative to `swofty/`.
    pub lut: String,
    /// From 0 (the game's own colours) to 1 (the look in full).
    pub strength: f64,
    /// The addon's name, for the core's log.
    pub addon: String,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Group {
    pub group: String,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub erps: Vec<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub deps: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub template: Option<String>,
    pub origin: String,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct ResErp {
    pub file: String,
    pub erps: Vec<String>,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub origin: String,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct DbEdit {
    pub hash: String,
    #[serde(rename = "type")]
    pub tag: u8,
    pub value: String,
    #[serde(skip_serializing_if = "std::ops::Not::not")]
    pub retype: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct LangTable {
    pub table: String,
}

// A ready overlay archive from an erp.overlay op, loaded as it is.
#[derive(Debug, Clone)]
pub struct Prebuilt {
    pub origin: String,
    pub path: PathBuf,
    pub erps: Vec<String>,
    pub resources: usize,
}

// Whether an overlay archive loads as it is over each target: no resource in it may
// change a target's stock layout. Ok with its resource count, or why not.
pub fn overlay_fits(
    path: &Path,
    targets: &[String],
    base: &dyn Fn(&str) -> PathBuf,
) -> Result<std::result::Result<usize, String>> {
    let open = |p: &Path| ErpFile::open(p).map_err(|e| Error::Other(format!("{}: {e}", p.display())));
    let overlay = open(path)?;
    for target in targets {
        let stock = open(&base(target))?;
        for res in &overlay.erp.resources {
            if let Some(why) = layout_change(res, &stock.erp) {
                return Ok(Err(format!("{} in {target}: {why}", res.name)));
            }
        }
    }
    Ok(Ok(overlay.erp.resources.len()))
}

#[derive(Debug, Default)]
pub struct Runtime {
    pub assets: Assets,
    // An addon turned on an experimental feature of the core, which needs the
    // file even with no assets.
    enabled: bool,
    resources: usize,
    pub files: BTreeMap<String, PathBuf>,
    tables: BTreeMap<String, Lng>,
    defaults: BTreeMap<String, BTreeMap<String, String>>,
    look: Option<Vec<u8>>,
    passes: Vec<Vec<u8>>,
}

fn argument_len(erp: &Erp) -> usize {
    if erp.version >= 4 { 6 } else { 4 }
}

pub(crate) fn foreign_argument(res: &Resource, erp: &Erp) -> bool {
    res.unk.len() != argument_len(erp)
}

pub(crate) fn layout_change(res: &Resource, erp: &Erp) -> Option<String> {
    let arg = argument_len(erp);
    if res.unk.len() != arg {
        return Some(format!(
            "it has {} argument byte(s), a version {} archive uses {arg}",
            res.unk.len(),
            erp.version
        ));
    }
    let stock = erp.resources.iter().find(|s| s.name == res.name)?;
    if stock.kind != res.kind {
        return Some(format!(
            "type {} differs from the stock {}",
            res.kind(),
            stock.kind()
        ));
    }
    if stock.unk != res.unk {
        return Some("its create argument differs from the stock one".into());
    }
    if stock.fragments.len() != res.fragments.len() {
        return Some(format!(
            "it has {} fragment(s), the stock entry {}",
            res.fragments.len(),
            stock.fragments.len()
        ));
    }
    for (i, (f, s)) in res.fragments.iter().zip(&stock.fragments).enumerate() {
        if f.name != s.name || f.flags != s.flags {
            return Some(format!(
                "fragment {i} is '{}' flags {:#x}, the stock one '{}' flags {:#x}",
                f.name(),
                f.flags,
                s.name(),
                s.flags
            ));
        }
        if f.size != s.size {
            return Some(format!(
                "fragment {i} is {} bytes, the stock one {}; stock archives that bind to the replacement read it with the stock layout",
                f.size, s.size
            ));
        }
    }
    None
}

fn lang_file(language: &str) -> String {
    match language {
        "*" => format!("{LANG_DIR}/all.lng"),
        code => format!("{LANG_DIR}/{code}.lng"),
    }
}

/// Why `swofty_core.dll` ignores an edit of `value` as a record of `tag`, by the
/// rule of its `check_text`; `Ok` when it applies it. Empty text is the game's 0,
/// 0.0 or false for the scalar tags, as in the 7,007 stock colours that hold it.
pub fn runtime_text(tag: u8, value: &str) -> std::result::Result<(), String> {
    if value.is_empty() && matches!(tag, 0 | 1 | 5 | 11 | 12) {
        return Ok(());
    }
    let integer = || {
        let digits = value.strip_prefix(['-', '+']).unwrap_or(value);
        !digits.is_empty() && digits.bytes().all(|b| b.is_ascii_digit())
    };
    match tag {
        0 | 11 if !integer() => Err(format!(
            "an int needs integer text (the game parses it with atoi), got {value:?}"
        )),
        0 | 11 if value.parse::<i32>().is_err() => Err(format!(
            "{value:?} is outside the 32-bit int range the game stores"
        )),
        12 if !integer() => Err(format!(
            "a t12 value needs integer text (the game parses it with strtoull), got {value:?}"
        )),
        12 if value.trim_start_matches(['-', '+']).parse::<u64>().is_err() => {
            Err(format!("{value:?} is outside the unsigned 64-bit range"))
        }
        1 if !value
            .parse::<f64>()
            .is_ok_and(|v| v.is_finite() && v.abs() <= f32::MAX as f64) =>
        {
            Err(format!("{value:?} is not a finite 32-bit float"))
        }
        5 if !value.eq_ignore_ascii_case("true") && !value.eq_ignore_ascii_case("false") => {
            Err(format!("a bool needs true or false, got {value:?}"))
        }
        10 | 15 | 16 => Err(format!("tag {tag} stores nothing")),
        19.. => Err(format!("tag {tag} is not a type the core knows")),
        _ => Ok(()),
    }
}

/// Records the core may add at runtime on top of a game database file of `loaded` records. F1 25 sizes the
/// database's robin_hood map while it loads the file, to the smallest power-of-two slot count whose 80% holds
/// the records, and grows it when an insert finds it 80% full. Growing it after the load leaves the game's
/// pointers into the old slots dangling, and the game crashes at boot (Gdb_Init_TableCursor under locale
/// init), so the core's inserts have to stay under that 80%. The margin covers the core's class probe
/// records and inserts the game makes later.
pub fn runtime_record_room(loaded: usize) -> usize {
    let max_load = |slots: usize| slots / 100 * 80 + slots % 100 * 80 / 100;
    let mut slots = 8usize;
    while max_load(slots) < loaded {
        slots *= 2;
    }
    max_load(slots).saturating_sub(loaded + RUNTIME_RECORD_MARGIN)
}

pub const RUNTIME_RECORD_MARGIN: usize = 4096;

pub fn db_edits(base: &Gvdb, built: &Gvdb) -> std::result::Result<Vec<DbEdit>, String> {
    let stock: HashMap<u64, _> = base.records.iter().map(|r| (r.key, r)).collect();
    let kept: std::collections::HashSet<u64> = built.records.iter().map(|r| r.key).collect();
    let removed = stock.keys().filter(|k| !kept.contains(k)).count();
    if removed > 0 {
        return Err(format!(
            "{removed} record(s) are removed, which runtime database edits cannot do"
        ));
    }
    let mut edits = Vec::new();
    for r in &built.records {
        let before = stock.get(&r.key);
        if before.is_some_and(|b| *b == r) {
            continue;
        }
        if NO_VALUE_TAGS.contains(&r.tag) {
            return Err(format!(
                "record {:#018x} has tag {}, which stores nothing",
                r.key, r.tag
            ));
        }
        let value = String::from_utf8(r.value.clone())
            .map_err(|_| format!("record {:#018x} does not hold text", r.key))?;
        runtime_text(r.tag, &value).map_err(|why| {
            format!(
                "swofty_core.dll would ignore record {:#018x} (tag {}): {why}",
                r.key, r.tag
            )
        })?;
        edits.push(DbEdit {
            hash: format!("{:016x}", r.key),
            tag: r.tag,
            value,
            retype: before.is_some_and(|b| b.tag != r.tag),
        });
    }
    Ok(edits)
}

impl Runtime {
    pub fn new(features: Vec<Kind>) -> Self {
        Self {
            assets: Assets {
                features: features.iter().map(Kind::to_string).collect(),
                ..Assets::default()
            },
            ..Self::default()
        }
    }

    /// Turns on an experimental feature of the core, such as `overlay`.
    pub fn enable(&mut self, feature: &str) {
        if !self.assets.features.iter().any(|f| f == feature) {
            self.assets.features.push(feature.to_owned());
        }
        self.enabled = true;
    }

    /// Grades the picture with a packed LUT at `strength`, from 0 to 1.
    pub fn shader(&mut self, packed: Vec<u8>, strength: f64, addon: &str) {
        self.assets.shader = Some(ShaderLook {
            lut: SHADER_LUT.into(),
            strength,
            addon: addon.into(),
        });
        self.look = Some(packed);
    }

    /// Adds a compiled post-process pass after the ones added before it.
    pub fn postfx(&mut self, dxil: Vec<u8>, name: &str, addon: &str, options: Vec<f64>, budget_ms: f64) {
        self.assets.postfx.push(PostFxPass {
            shader: format!("{POSTFX_DIR}/{:02}.dxil", self.passes.len()),
            name: name.into(),
            addon: addon.into(),
            options,
            budget_ms,
        });
        self.passes.push(dxil);
    }

    fn place(&mut self, rel: String, src: PathBuf) -> Result<()> {
        let rel = format!("{SWOFTY}/{rel}");
        if self.files.insert(rel.clone(), src).is_some() {
            return Err(Error::Other(format!("{rel} is placed twice")));
        }
        Ok(())
    }

    pub fn push(&mut self, op: Op, resolve: &dyn Fn(&Op, &Source) -> Result<PathBuf>) -> Result<()> {
        match &op.op {
            AssetOp::Group(GroupOp::Register {
                group,
                erp,
                erps,
                deps,
                template,
            }) => self.assets.asset_groups.push(Group {
                group: group.clone(),
                erps: erp.iter().chain(erps).cloned().collect(),
                deps: deps.clone(),
                template: template.clone(),
                origin: op.origin.to_string(),
            }),
            AssetOp::Lang(LangOp::Set { language, key, value }) => {
                self.tables
                    .entry(language.clone())
                    .or_default()
                    .set(key, value.clone());
            }
            AssetOp::Lang(LangOp::Default { language, key, value }) => {
                self.defaults
                    .entry(language.clone())
                    .or_default()
                    .insert(key.clone(), value.clone());
            }
            AssetOp::Lang(LangOp::Table { language, source }) => {
                let path = resolve(&op, source)?;
                let bytes = std::fs::read(&path).map_err(io_err(&path))?;
                let read =
                    Lng::parse(&bytes).map_err(|e| Error::Other(format!("{}: {e}", path.display())))?;
                let table = self.tables.entry(language.clone()).or_default();
                for e in read.entries {
                    table.set(&e.key, e.value);
                }
            }
            AssetOp::File(FileOp::Put { source, target }) => {
                if !relative_inside(target) {
                    return Err(Error::Resolve(format!(
                        "{target} must be a relative path inside the game folder"
                    )));
                }
                let src = resolve(&op, source)?;
                let rel = format!("{OVERLAY_DIR}/{target}");
                self.files.remove(&format!("{SWOFTY}/{rel}"));
                self.place(rel, src)?;
                self.assets.overlay = true;
            }
            AssetOp::Erp(_)
            | AssetOp::Db(_)
            | AssetOp::Track(_)
            | AssetOp::Item(_)
            | AssetOp::Warn { .. } => {}
        }
        Ok(())
    }

    // One overlay archive for each origin and set of target archives, so that each
    // addon or mod has its own, beside the ready ones in `prebuilt`. The game reads a
    // group's overlays in the listed order and the first that has a resource wins, so
    // they are listed from the last origin in `origins` to the first.
    pub fn resources<'r>(
        &mut self,
        resources: impl IntoIterator<Item = (&'r str, &'r Resource, &'r Erp, &'r str)>,
        origins: &[String],
        prebuilt: &[Prebuilt],
        out: &Path,
    ) -> Result<Vec<String>> {
        let stale = out.join(SWOFTY).join(RESOURCES_DIR);
        if stale.is_dir() {
            std::fs::remove_dir_all(&stale).map_err(io_err(&stale))?;
        }
        let mut warnings = Vec::new();
        let rank = |origin: &str| origins.iter().position(|o| o == origin).unwrap_or(origins.len());
        type Written<'w> = (&'w Resource, &'w Erp, BTreeSet<&'w str>);
        let mut by_uri: BTreeMap<(usize, &str, &str), Written> = BTreeMap::new();
        for (erp, res, stock, origin) in resources {
            if let Some(why) = layout_change(res, stock) {
                warnings.push(format!("runtime: {} in {erp} is left out: {why}", res.name));
                continue;
            }
            let e = by_uri
                .entry((rank(origin), origin, &res.name))
                .or_insert((res, stock, BTreeSet::new()));
            e.0 = res;
            e.1 = stock;
            e.2.insert(erp);
        }
        // An origin's resources share one archive, which lists every target the
        // origin writes; only a second archive version splits it. Resources
        // without an origin keep one archive per set of targets.
        type Set<'s> = (Vec<Resource>, &'s Erp, BTreeSet<&'s str>);
        let mut sets: BTreeMap<(usize, &str, u32, Vec<&str>), Set> = BTreeMap::new();
        for ((n, origin, _), (res, stock, erps)) in by_uri {
            let split = match origin.is_empty() {
                true => erps.iter().copied().collect(),
                false => Vec::new(),
            };
            let set =
                sets.entry((n, origin, stock.version, split))
                    .or_insert((Vec::new(), stock, BTreeSet::new()));
            set.0.push(res.clone());
            set.2.extend(erps);
        }
        let mut listed: Vec<(usize, ResErp)> = Vec::new();
        for (n, ((rank, origin, _, _), (resources, stock, targets))) in sets.into_iter().enumerate() {
            let rel = format!("{RESOURCES_DIR}/{n}.erp");
            let path = out.join(SWOFTY).join(&rel);
            let dir = path.parent().expect("has parent");
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
            self.resources += resources.len();
            let mut erp = Erp {
                header: stock.header,
                version: stock.version,
                resources,
            };
            std::fs::write(&path, erp.to_bytes()).map_err(io_err(&path))?;
            self.place(rel.clone(), path)?;
            listed.push((
                rank,
                ResErp {
                    file: rel,
                    erps: targets.into_iter().map(str::to_owned).collect(),
                    origin: origin.to_owned(),
                },
            ));
        }
        for (n, p) in prebuilt.iter().enumerate() {
            let rel = format!("{RESOURCES_DIR}/p{n}.erp");
            self.resources += p.resources;
            self.place(rel.clone(), p.path.clone())?;
            listed.push((
                rank(&p.origin),
                ResErp {
                    file: rel,
                    erps: p.erps.clone(),
                    origin: p.origin.clone(),
                },
            ));
        }
        listed.sort_by_key(|e| std::cmp::Reverse(e.0));
        self.assets
            .resource_erps
            .extend(listed.into_iter().map(|(_, e)| e));
        Ok(warnings)
    }

    pub fn db(&mut self, edits: Vec<DbEdit>) {
        self.assets.early_stage |= !edits.is_empty();
        self.assets.db_edits.extend(edits);
    }

    pub fn summary(&self) -> String {
        let a = &self.assets;
        let overlay = self
            .files
            .keys()
            .filter(|k| k.starts_with(&format!("{SWOFTY}/{OVERLAY_DIR}/")))
            .count();
        format!(
            "runtime: {ASSETS_FILE} with {} asset group(s), {} resource(s), {} database edit(s), {} language table(s), {overlay} overlay file(s)",
            a.asset_groups.len(),
            self.resources,
            a.db_edits.len(),
            a.lang.len(),
        )
    }

    pub fn finish(mut self, out: &Path) -> Result<Option<Self>> {
        for (language, defaults) in std::mem::take(&mut self.defaults) {
            let table = self.tables.entry(language).or_default();
            for (key, value) in defaults {
                if table.get(&key).is_none() {
                    table.set(&key, value);
                }
            }
        }
        for (language, table) in std::mem::take(&mut self.tables) {
            let rel = lang_file(&language);
            let path = out.join(SWOFTY).join(&rel);
            let dir = path.parent().expect("has parent");
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
            std::fs::write(&path, table.to_bytes()).map_err(io_err(&path))?;
            self.place(rel.clone(), path)?;
            self.assets.lang.insert(language, LangTable { table: rel });
        }
        if let Some(packed) = self.look.take() {
            let path = out.join(SWOFTY).join(SHADER_LUT);
            let dir = path.parent().expect("has parent");
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
            std::fs::write(&path, packed).map_err(io_err(&path))?;
            self.place(SHADER_LUT.into(), path)?;
        }
        for (i, dxil) in std::mem::take(&mut self.passes).into_iter().enumerate() {
            let rel = self.assets.postfx[i].shader.clone();
            let path = out.join(SWOFTY).join(&rel);
            let dir = path.parent().expect("has parent");
            std::fs::create_dir_all(dir).map_err(io_err(dir))?;
            std::fs::write(&path, dxil).map_err(io_err(&path))?;
            self.place(rel, path)?;
        }
        let empty = Assets {
            features: self.assets.features.clone(),
            ..Assets::default()
        };
        if self.assets == empty && !self.enabled {
            return Ok(None);
        }
        let path = out.join(ASSETS_FILE);
        crate::write_json(&path, &self.assets)?;
        self.files.insert(ASSETS_FILE.into(), path);
        Ok(Some(self))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::asset::Origin;
    use swofty_formats::erp::{Fragment as ErpFragment, STORED};
    use swofty_formats::gvdb::Record;

    fn op(addon: &str, json: &str) -> Op {
        Op {
            op: serde_json::from_str(json).unwrap(),
            origin: Origin {
                addon: addon.into(),
                item: None,
            },
        }
    }

    fn fragment(name: &[u8; 4], flags: u32, data: &[u8]) -> ErpFragment {
        ErpFragment {
            name: *name,
            offset: 0,
            size: data.len() as u64,
            flags,
            compression: STORED,
            packed: data.len() as u64,
            raw: data.to_vec(),
        }
    }

    #[test]
    fn writes_assets_json_in_the_core_schema() {
        let dir = std::env::temp_dir().join(format!("swofty-runtime-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        let addon = dir.join("addon");
        std::fs::create_dir_all(&addon).unwrap();
        let mut table = Lng::default();
        table.set("lng_a", "from table");
        table.set("lng_b", "kept");
        std::fs::write(addon.join("eng.lng"), table.to_bytes()).unwrap();
        std::fs::write(addon.join("one.dds"), b"one").unwrap();
        std::fs::write(addon.join("two.dds"), b"two").unwrap();
        let resolve = |_: &Op, s: &Source| Ok(s.resolve(&addon)?);
        let mut rt = Runtime::new(vec![Kind::File]);
        for (id, json) in [
            (
                "a",
                r#"{"op":"group.register","group":"asset_groups/x/y.assetgroup","erp":"2025_asset_groups/x/y.erp","deps":["asset_groups/z.assetgroup"]}"#,
            ),
            ("a", r#"{"op":"lang.table","language":"eng","source":"eng.lng"}"#),
            (
                "b",
                r#"{"op":"lang.set","language":"eng","key":"lng_a","value":"from set"}"#,
            ),
            (
                "b",
                r#"{"op":"lang.set","language":"*","key":"lng_c","value":"every language"}"#,
            ),
            (
                "a",
                r#"{"op":"file.put","source":"one.dds","target":"livery/s.dds"}"#,
            ),
            (
                "b",
                r#"{"op":"file.put","source":"two.dds","target":"livery/s.dds"}"#,
            ),
        ] {
            rt.push(op(id, json), &resolve).unwrap();
        }
        let mut kind = [0; 16];
        kind[..13].copy_from_slice(b"GfxSurfaceRes");
        let res = |name: &str, fragments: Vec<ErpFragment>| Resource {
            name: name.into(),
            kind,
            unk: 1u32.to_le_bytes().to_vec(),
            fragments,
            hash: [0; 16],
        };
        let x = "eaid://ui_package/x.tif.image";
        let y = "eaid://ui_package/y.tif.image";
        let z = "eaid://ui_package/z.tif.image";
        let mut header = [0; 0x30];
        header[..4].copy_from_slice(b"ERPK");
        header[4] = 3;
        let stock = Erp {
            header,
            version: 3,
            resources: vec![
                res(
                    x,
                    vec![fragment(b"temp", 16, b"HEAD"), fragment(b"pixl", 0, b"STOCK!")],
                ),
                res(y, vec![fragment(b"temp", 16, b"ABCD")]),
            ],
        };
        let replaced = res(
            x,
            vec![fragment(b"temp", 16, b"HEAD"), fragment(b"pixl", 0, b"PIXELS")],
        );
        let resized = res(y, vec![fragment(b"temp", 16, b"ABCDE")]);
        let added = res(z, vec![fragment(b"temp", 16, b"NEW")]);
        let boot = "2025_asset_groups/ui_package/boot.erp";
        let other = "2025_asset_groups/ui_package/other.erp";
        let out = dir.join("overlay");
        let stale = out.join("swofty/assets/resources/0.0.bin");
        std::fs::create_dir_all(stale.parent().unwrap()).unwrap();
        std::fs::write(&stale, b"old").unwrap();
        let warnings = rt
            .resources(
                [
                    (boot, &replaced, &stock, ""),
                    (other, &replaced, &stock, ""),
                    (boot, &resized, &stock, ""),
                    (boot, &added, &stock, ""),
                ],
                &[],
                &[],
                &out,
            )
            .unwrap();
        assert_eq!(
            warnings,
            [format!(
                "runtime: {y} in {boot} is left out: fragment 0 is 5 bytes, the stock one 4; stock archives that bind to the replacement read it with the stock layout"
            )]
        );
        let mut base = Gvdb::default();
        base.records.push(Record::new("a.int", 0, "1"));
        let mut built = base.clone();
        built.set("a.int", "2", None).unwrap();
        built.set("a.new", "hello", Some(2)).unwrap();
        rt.db(db_edits(&base, &built).unwrap());
        let rt = rt.finish(&out).unwrap().unwrap();
        let written: serde_json::Value =
            serde_json::from_slice(&std::fs::read(out.join(ASSETS_FILE)).unwrap()).unwrap();
        let hash = |p: &str| format!("{:016x}", swofty_formats::hash::fnv1a64(p));
        assert_eq!(
            written,
            serde_json::json!({
                "early_stage": true,
                "features": ["file"],
                "asset_groups": [{
                    "group": "asset_groups/x/y.assetgroup",
                    "erps": ["2025_asset_groups/x/y.erp"],
                    "deps": ["asset_groups/z.assetgroup"],
                    "origin": "a",
                }],
                "resource_erps": [
                    { "file": "assets/resources/0.erp", "erps": [boot] },
                    { "file": "assets/resources/1.erp", "erps": [boot, other] },
                ],
                "db_edits": [
                    { "hash": hash("a.int"), "type": 0, "value": "2" },
                    { "hash": hash("a.new"), "type": 2, "value": "hello" },
                ],
                "lang": {
                    "*": { "table": "assets/lang/all.lng" },
                    "eng": { "table": "assets/lang/eng.lng" },
                },
                "overlay": true,
            })
        );
        let eng = Lng::parse(&std::fs::read(out.join("swofty/assets/lang/eng.lng")).unwrap()).unwrap();
        assert_eq!(eng.get("lng_a"), Some("from set"));
        assert_eq!(eng.get("lng_b"), Some("kept"));
        let names = |n: u32| {
            let erp =
                Erp::parse(&std::fs::read(out.join(format!("swofty/assets/resources/{n}.erp"))).unwrap())
                    .unwrap();
            assert_eq!((&erp.header[..8], erp.version), (&header[..8], 3));
            erp.resources
                .iter()
                .map(|r| {
                    (
                        r.name.clone(),
                        r.fragments.iter().map(|f| f.data().unwrap()).collect::<Vec<_>>(),
                    )
                })
                .collect::<Vec<_>>()
        };
        assert_eq!(names(0), [(z.to_owned(), vec![b"NEW".to_vec()])]);
        assert_eq!(
            names(1),
            [(x.to_owned(), vec![b"HEAD".to_vec(), b"PIXELS".to_vec()])]
        );
        assert!(rt.summary().contains("2 resource(s)"));
        assert!(!stale.exists());
        let placed: Vec<&str> = rt.files.keys().map(String::as_str).collect();
        assert_eq!(
            placed,
            [
                "swofty/assets.json",
                "swofty/assets/lang/all.lng",
                "swofty/assets/lang/eng.lng",
                "swofty/assets/resources/0.erp",
                "swofty/assets/resources/1.erp",
                "swofty/overlay/livery/s.dds",
            ]
        );
        assert_eq!(rt.files["swofty/overlay/livery/s.dds"], addon.join("two.dds"));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn a_default_applies_before_every_set_and_never_conflicts() {
        use crate::planner::{Features, Planner};
        let out = std::env::temp_dir().join(format!("swofty-runtime-default-{}", std::process::id()));
        std::fs::remove_dir_all(&out).ok();
        let features = Features {
            stable: BTreeSet::from([Kind::Lang]),
            ..Features::default()
        };
        let opt_in = BTreeSet::new();
        let mut planner = Planner::new(&features, &opt_in);
        for (id, json) in [
            (
                "a",
                r#"{"op":"lang.set","language":"eng","key":"lng_a","value":"set by a"}"#,
            ),
            (
                "b",
                r#"{"op":"lang.default","language":"eng","key":"lng_a","value":"default from b"}"#,
            ),
            (
                "b",
                r#"{"op":"lang.default","language":"eng","key":"lng_b","value":"default from b"}"#,
            ),
            (
                "c",
                r#"{"op":"lang.default","language":"eng","key":"lng_b","value":"default from c"}"#,
            ),
            (
                "c",
                r#"{"op":"lang.default","language":"*","key":"lng_c","value":"every language"}"#,
            ),
            (
                "c",
                r#"{"op":"lang.default","language":"eng","key":"lng_d","value":"default from c"}"#,
            ),
            (
                "d",
                r#"{"op":"lang.set","language":"eng","key":"lng_d","value":"set by d"}"#,
            ),
        ] {
            planner.push(op(id, json));
        }
        let planned = planner.finish();
        assert!(planned.conflicts.is_empty(), "{:?}", planned.conflicts);
        let mut rt = Runtime::new(Vec::new());
        for op in planned.runtime {
            rt.push(op, &|_: &Op, s: &Source| Ok(PathBuf::from(&s.path)))
                .unwrap();
        }
        rt.finish(&out).unwrap().unwrap();
        let table = |name: &str| {
            Lng::parse(&std::fs::read(out.join("swofty/assets/lang").join(name)).unwrap()).unwrap()
        };
        let eng = table("eng.lng");
        assert_eq!(
            ["lng_a", "lng_b", "lng_d"].map(|k| eng.get(k)),
            [Some("set by a"), Some("default from c"), Some("set by d")]
        );
        assert_eq!(table("all.lng").get("lng_c"), Some("every language"));
        std::fs::remove_dir_all(&out).ok();
    }

    #[test]
    fn the_runtime_record_room_stops_before_the_game_database_grows() {
        assert_eq!(
            runtime_record_room(742_857),
            838_860 - 742_857 - RUNTIME_RECORD_MARGIN
        );
        assert_eq!(
            runtime_record_room(758_755),
            838_860 - 758_755 - RUNTIME_RECORD_MARGIN
        );
        assert_eq!(runtime_record_room(838_860), 0);
        assert_eq!(
            runtime_record_room(838_861),
            1_677_721 - 838_861 - RUNTIME_RECORD_MARGIN
        );
        assert_eq!(runtime_record_room(0), 0);
    }

    #[test]
    fn database_edits_retype_and_refuse_removals() {
        let mut base = Gvdb::default();
        base.records.push(Record::new("a.x", 0, "1"));
        base.records.push(Record::new("a.y", 0, "1"));
        let mut built = base.clone();
        built.set("a.x", "1.5", Some(1)).unwrap();
        let edits = db_edits(&base, &built).unwrap();
        assert_eq!(edits.len(), 1);
        assert!(edits[0].retype);
        built.remove("a.y");
        assert!(
            db_edits(&base, &built)
                .unwrap_err()
                .contains("1 record(s) are removed")
        );
        let mut empty = Runtime::new(vec![Kind::File]);
        empty.db(Vec::new());
        assert!(empty.finish(Path::new("/nonexistent")).unwrap().is_none());
    }

    #[test]
    fn runtime_text_takes_what_the_core_applies() {
        for (tag, value) in [
            (11, ""),
            (11, "-1710619"),
            (0, ""),
            (0, "+5"),
            (1, ""),
            (1, "0.1"),
            (5, ""),
            (5, "True"),
            (12, ""),
            (12, "-1"),
            (2, ""),
            (13, ""),
        ] {
            assert_eq!(runtime_text(tag, value), Ok(()), "tag {tag} {value:?}");
        }
        for (tag, value) in [
            (11, "abc"),
            (0, "1.5E+07"),
            (0, "3000000000"),
            (0, "-"),
            (12, "-x"),
            (12, "18446744073709551616"),
            (1, "Level_0|DevelopmentLevel"),
            (1, "inf"),
            (5, "yes"),
            (16, ""),
        ] {
            assert!(runtime_text(tag, value).is_err(), "tag {tag} {value:?}");
        }
    }

    #[test]
    fn database_edits_keep_empty_colours_and_refuse_text_the_core_ignores() {
        let mut base = Gvdb::default();
        base.records
            .push(Record::new("gloves.Gloves_0.colour_1_default", 11, ""));
        base.records.push(Record::new("modes.mode_0.prize", 0, "1.5E+07"));
        let mut built = base.clone();
        built
            .records
            .push(Record::new("gloves.Gloves_1.colour_1_default", 11, ""));
        let edits = db_edits(&base, &built).unwrap();
        assert_eq!(edits.len(), 1);
        assert_eq!((edits[0].tag, edits[0].value.as_str()), (11, ""));
        built
            .records
            .push(Record::new("modes.mode_1.prize", 0, "1.5E+07"));
        let why = db_edits(&base, &built).unwrap_err();
        assert!(
            why.contains("swofty_core.dll would ignore record") && why.contains("\"1.5E+07\""),
            "{why}"
        );
    }
}
