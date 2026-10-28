use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use swofty_formats::bxml::{Document, Element};
use swofty_formats::erp::{Entry, ErpFile, Resource, STORED, ZSTD, compress};

use crate::{Error, Result, io_err};

pub const TEAMS: &str = "2025_asset_groups/f1_2026_vehicle_package/teams";
const ERS: &[u8] = b"EnergyRecoverySystem";
const SETTINGS: &[u8] = b"TrackSettings";
const COUNT: &[u8] = b"numTrackSettingsTypes";
pub const FIELDS: &[&str] = &[
    "Track",
    "MediumDeployFactor",
    "MediumDerateFactor",
    "HotLapDeployFactor",
    "HotLapDerateFactor",
    "HarvestFactorMGUH",
    "OverrideDetectionLapProgress",
    "OverrideActivationLapProgress",
    "HarvestLimitMGUK",
    "HarvestLimitQualifyingMGUK",
    "HarvestTorqueFactor",
];

pub struct ErsMap<'a> {
    pub code: &'a str,
    pub base: &'a str,
    pub values: &'a BTreeMap<String, serde_json::Value>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct ErsBuild {
    pub dir: Option<PathBuf>,
    pub replaced: BTreeMap<String, PathBuf>,
    pub handling_files: usize,
    pub notes: Vec<String>,
}

pub fn format_value(v: &serde_json::Value) -> Result<String> {
    match v {
        serde_json::Value::Number(n) if n.is_i64() || n.is_u64() => Ok(n.to_string()),
        serde_json::Value::Number(n) => {
            let f = n.as_f64().expect("number");
            Ok(if f.fract() == 0.0 && f.abs() >= 1e6 {
                format!("{}", f as i64)
            } else {
                format!("{f}")
            })
        }
        serde_json::Value::String(s) => Ok(s.clone()),
        other => Err(Error::Map(format!("ers value {other} is not a number"))),
    }
}

fn ers_node(root: &mut Element) -> Option<&mut Element> {
    root.children
        .iter_mut()
        .find(|c| c.name == ERS && c.child(SETTINGS).is_some())
}

fn track_of(e: &Element) -> Option<&[u8]> {
    e.child(b"Track")?.attr(b"value")
}

pub fn add_track(root: &mut Element, map: &ErsMap) -> Result<bool> {
    let Some(ers) = ers_node(root) else {
        return Ok(false);
    };
    let code = map.code.as_bytes();
    let count = ers.children_named(SETTINGS).count();
    let at = match ers
        .children
        .iter()
        .position(|c| c.name == SETTINGS && track_of(c) == Some(code))
    {
        Some(i) => i,
        None => {
            let Some(base) = ers
                .children
                .iter()
                .find(|c| c.name == SETTINGS && track_of(c) == Some(map.base.as_bytes()))
                .cloned()
            else {
                return Ok(false);
            };
            let mut new = base;
            new.set_attr(b"name", count.to_string().as_bytes());
            if let Some(t) = new.child_mut(b"Track") {
                t.set_attr(b"value", code);
            }
            let last = ers
                .children
                .iter()
                .rposition(|c| c.name == SETTINGS)
                .expect("has settings");
            ers.children.insert(last + 1, new);
            ers.set_attr(COUNT, (count + 1).to_string().as_bytes());
            last + 1
        }
    };
    let template = ers.children[at]
        .children
        .iter()
        .find(|c| c.name != b"Track")
        .or_else(|| ers.children[at].children.first())
        .cloned()
        .ok_or_else(|| Error::Map("an ERS TrackSettings entry has no fields".into()))?;
    let entry = &mut ers.children[at];
    for (key, value) in map.values {
        let order = FIELDS
            .iter()
            .position(|f| f == key)
            .filter(|&i| i > 0)
            .ok_or_else(|| Error::Map(format!("unknown ers key {key}")))?;
        let value = format_value(value)?;
        if let Some(f) = entry.child_mut(key.as_bytes()) {
            f.set_attr(b"value", value.as_bytes());
            continue;
        }
        let mut leaf = template.clone();
        leaf.name = key.as_bytes().to_vec();
        leaf.attrs = vec![(b"value".to_vec(), value.into_bytes())];
        leaf.children.clear();
        let pos = entry
            .children
            .iter()
            .position(|c| {
                FIELDS
                    .iter()
                    .position(|f| f.as_bytes() == c.name.as_slice())
                    .is_some_and(|i| i > order)
            })
            .unwrap_or(entry.children.len());
        entry.children.insert(pos, leaf);
    }
    Ok(true)
}

fn set_data(res: &mut Resource, index: usize, data: &[u8]) -> Result<()> {
    let f = res
        .fragments
        .get_mut(index)
        .ok_or_else(|| Error::Map(format!("{}: no fragment {index}", res.name)))?;
    let zstd =
        f.compression == ZSTD || (f.compression != STORED && f.raw.starts_with(&[0x28, 0xb5, 0x2f, 0xfd]));
    f.raw = if zstd { compress(data)? } else { data.to_vec() };
    f.size = data.len() as u64;
    f.packed = f.raw.len() as u64;
    res.hash = res.content_hash()?;
    Ok(())
}

pub fn edit_vtf(res: &Resource, maps: &[ErsMap]) -> Result<Option<Resource>> {
    let data = res.fragments[0].data()?;
    let mut doc = Document::parse(&data)?;
    let mut changed = false;
    for m in maps {
        changed |= add_track(&mut doc.root, m)?;
    }
    let bytes = doc.to_bytes();
    if !changed || bytes == data {
        return Ok(None);
    }
    let mut out = res.clone();
    set_data(&mut out, 0, &bytes)?;
    if let Some(stats) = out.fragments.get(1) {
        let old = stats.data()?;
        let version = old
            .get(4..8)
            .map_or(1, |b| u32::from_le_bytes(b.try_into().expect("4 bytes")));
        set_data(&mut out, 1, &doc.string_stats(version))?;
    }
    Ok(Some(out))
}

/// Whether `rel` is a team archive of a vehicle package, whose handling files take the tracks' entries.
pub fn is_car_archive(rel: &str) -> bool {
    rel.starts_with("2025_asset_groups/") && rel.contains("_vehicle_package/teams/") && rel.ends_with(".erp")
}

/// Writes to `out` a copy of the archive at `src` with the tracks' entries in its handling files, and returns how
/// many handling files changed; with none it writes nothing.
pub fn edit_archive(src: &Path, maps: &[ErsMap], out: &Path) -> Result<usize> {
    let file = ErpFile::open(src)?;
    let mut replace = BTreeMap::new();
    for (i, r) in file.erp.resources.iter().enumerate() {
        if r.kind() == "VTF"
            && let Some(new) = edit_vtf(&file.load(r)?, maps)?
        {
            replace.insert(i, new);
        }
    }
    let changed = replace.len();
    if changed == 0 {
        return Ok(0);
    }
    let entries = file
        .erp
        .resources
        .iter()
        .enumerate()
        .map(|(i, r)| match replace.remove(&i) {
            Some(new) => Entry::Owned(new),
            None => Entry::Copy(r),
        })
        .collect();
    let dir = out.parent().expect("has parent");
    std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    file.write(entries, out)?;
    Ok(changed)
}

fn walk(dir: &Path, base: &Path, out: &mut Vec<String>) -> Result<()> {
    let Ok(rd) = std::fs::read_dir(dir) else {
        return Ok(());
    };
    for e in rd {
        let p = e.map_err(io_err(dir))?.path();
        if p.is_dir() {
            walk(&p, base, out)?;
        } else if p.extension().is_some_and(|x| x == "erp") {
            out.push(
                p.strip_prefix(base)
                    .expect("under base")
                    .to_string_lossy()
                    .replace('\\', "/"),
            );
        }
    }
    Ok(())
}

pub fn car_archives(game: &Path) -> Result<Vec<String>> {
    let mut out = Vec::new();
    walk(&game.join(TEAMS), game, &mut out)?;
    out.sort();
    Ok(out)
}

pub fn stamp(maps: &[ErsMap], archives: &[String], original: &dyn Fn(&str) -> PathBuf) -> Result<String> {
    let mut h = <sha2::Sha256 as sha2::Digest>::new();
    for m in maps {
        sha2::Digest::update(
            &mut h,
            format!(
                "{}|{}|{}",
                m.code,
                m.base,
                serde_json::to_string(m.values).expect("json")
            ),
        );
    }
    for rel in archives {
        let p = original(rel);
        let meta = std::fs::metadata(&p).map_err(io_err(&p))?;
        sha2::Digest::update(&mut h, format!("{rel}|{}|{:?}", meta.len(), meta.modified().ok()));
    }
    Ok(crate::baseline::hex(&sha2::Digest::finalize(h))[..16].to_owned())
}

pub fn build_ers(
    maps: &[ErsMap],
    game: &Path,
    original: &dyn Fn(&str) -> PathBuf,
    cache: &Path,
) -> Result<ErsBuild> {
    let mut build = ErsBuild::default();
    if maps.is_empty() {
        return Ok(build);
    }
    let archives = car_archives(game)?;
    let out = cache.join(stamp(maps, &archives, original)?);
    build.dir = Some(out.clone());
    let done = out.join(".done");
    if !done.is_file() {
        let mut count = 0;
        for rel in &archives {
            count += edit_archive(&original(rel), maps, &out.join(rel))?;
        }
        std::fs::create_dir_all(&out).map_err(io_err(&out))?;
        std::fs::write(&done, count.to_string()).map_err(io_err(&done))?;
    }
    for rel in &archives {
        let dst = out.join(rel);
        if dst.is_file() {
            build.replaced.insert(rel.clone(), dst);
        }
    }
    build.handling_files = std::fs::read_to_string(&done)
        .ok()
        .and_then(|s| s.trim().parse().ok())
        .unwrap_or(0);
    for m in maps {
        if !m.values.is_empty() && build.handling_files == 0 {
            build.notes.push(format!(
                "{}: no car has ERS settings for {} to copy",
                m.code, m.base
            ));
        }
    }
    Ok(build)
}

#[cfg(test)]
mod tests {
    use super::*;
    use swofty_formats::bxml::Kind;

    fn leaf(name: &str, v: &str) -> Element {
        Element {
            kind: Kind::Element,
            pad: 0,
            name: name.as_bytes().to_vec(),
            attrs: vec![(b"value".to_vec(), v.as_bytes().to_vec())],
            children: Vec::new(),
            end_pad: 0,
        }
    }

    fn el(name: &str, attrs: &[(&str, &str)], children: Vec<Element>) -> Element {
        Element {
            kind: Kind::Element,
            pad: 0,
            name: name.as_bytes().to_vec(),
            attrs: attrs
                .iter()
                .map(|(k, v)| (k.as_bytes().to_vec(), v.as_bytes().to_vec()))
                .collect(),
            children,
            end_pad: 0,
        }
    }

    #[test]
    fn adds_a_track_cloned_from_its_base() {
        let mut root = el(
            "VTF",
            &[],
            vec![el(
                "EnergyRecoverySystem",
                &[("numTrackSettingsTypes", "1")],
                vec![el(
                    "TrackSettings",
                    &[("name", "0")],
                    vec![
                        leaf("Track", "bahrain"),
                        leaf("MediumDeployFactor", "0.8"),
                        leaf("HarvestLimitMGUK", "7400000"),
                    ],
                )],
            )],
        );
        let values = BTreeMap::from([
            ("HarvestLimitMGUK".to_owned(), serde_json::json!(9000000)),
            ("HotLapDerateFactor".to_owned(), serde_json::json!(0.1)),
        ]);
        let m = ErsMap {
            code: "bahrain24",
            base: "bahrain",
            values: &values,
        };
        assert!(add_track(&mut root, &m).unwrap());
        let ers = &root.children[0];
        assert_eq!(ers.attr(b"numTrackSettingsTypes"), Some(&b"2"[..]));
        let new = &ers.children[1];
        assert_eq!(new.attr(b"name"), Some(&b"1"[..]));
        let names: Vec<&[u8]> = new.children.iter().map(|c| c.name.as_slice()).collect();
        assert_eq!(
            names,
            [
                &b"Track"[..],
                b"MediumDeployFactor",
                b"HotLapDerateFactor",
                b"HarvestLimitMGUK"
            ]
        );
        assert_eq!(
            new.child(b"Track").unwrap().attr(b"value"),
            Some(&b"bahrain24"[..])
        );
        assert_eq!(
            new.child(b"HarvestLimitMGUK").unwrap().attr(b"value"),
            Some(&b"9000000"[..])
        );
        assert_eq!(
            new.child(b"HotLapDerateFactor").unwrap().attr(b"value"),
            Some(&b"0.1"[..])
        );
        assert!(add_track(&mut root, &m).unwrap());
        assert_eq!(root.children[0].children.len(), 2);
    }

    #[test]
    fn number_formatting_matches_the_handling_files() {
        assert_eq!(format_value(&serde_json::json!(9000000)).unwrap(), "9000000");
        assert_eq!(format_value(&serde_json::json!(0.953)).unwrap(), "0.953");
        assert_eq!(format_value(&serde_json::json!(9000000.0)).unwrap(), "9000000");
    }
}
