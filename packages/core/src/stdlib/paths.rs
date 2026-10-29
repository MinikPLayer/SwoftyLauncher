//! The files a 1.6 addon's records name through `Path` fields, which the build places in
//! `<game>/swofty/addons/<id>/` next to its data files, so the core's file redirects find them.
//!
//! - In a data file, a `Path` field is any field whose schema is the registry's `#/$defs/path`
//!   (`packages/sdk/std/registries/<name>/<major>.<minor>.json`): a string names a file, or a
//!   folder when it ends in `/`; `{ "erp", ... }` names an archive; `{ "stock" }` is the game's
//!   own file and is never placed.
//! - An addon with code lists the files its `SWOFTY_REGISTER` names in its lock file's `files`.
//! - A path is relative to the addon's folder, with `/` or `\` between folders. One that is
//!   absolute, names a drive or goes up with `..` is never placed.

use std::collections::BTreeSet;
use std::path::Path;
use std::sync::OnceLock;

use serde_json::Value;

const PATH_DEF: &str = "#/$defs/path";

/// The current schema of each core registry that has one.
const SCHEMAS: &[(&str, &str)] = &[
    (
        "addon.option",
        include_str!("../../../sdk/std/registries/addon.option/1.0.json"),
    ),
    (
        "career.calendar",
        include_str!("../../../sdk/std/registries/career.calendar/1.0.json"),
    ),
    (
        "career.character",
        include_str!("../../../sdk/std/registries/career.character/1.0.json"),
    ),
    (
        "career.content",
        include_str!("../../../sdk/std/registries/career.content/1.0.json"),
    ),
    (
        "career.driver_ratings",
        include_str!("../../../sdk/std/registries/career.driver_ratings/1.0.json"),
    ),
    (
        "career.mode",
        include_str!("../../../sdk/std/registries/career.mode/1.0.json"),
    ),
    (
        "career.season",
        include_str!("../../../sdk/std/registries/career.season/1.0.json"),
    ),
    (
        "career.wizard",
        include_str!("../../../sdk/std/registries/career.wizard/1.0.json"),
    ),
    (
        "content.package",
        include_str!("../../../sdk/std/registries/content.package/1.0.json"),
    ),
    (
        "driver.head",
        include_str!("../../../sdk/std/registries/driver.head/1.0.json"),
    ),
    (
        "driver.identity",
        include_str!("../../../sdk/std/registries/driver.identity/1.0.json"),
    ),
    (
        "driver.look",
        include_str!("../../../sdk/std/registries/driver.look/1.0.json"),
    ),
    (
        "driver.person",
        include_str!("../../../sdk/std/registries/driver.person/1.0.json"),
    ),
    (
        "driver.stats",
        include_str!("../../../sdk/std/registries/driver.stats/1.0.json"),
    ),
    (
        "engine_sound",
        include_str!("../../../sdk/std/registries/engine_sound/1.0.json"),
    ),
    (
        "era.box",
        include_str!("../../../sdk/std/registries/era.box/1.0.json"),
    ),
    (
        "gloves",
        include_str!("../../../sdk/std/registries/gloves/1.0.json"),
    ),
    (
        "helmet",
        include_str!("../../../sdk/std/registries/helmet/1.0.json"),
    ),
    (
        "hub.tab",
        include_str!("../../../sdk/std/registries/hub.tab/1.0.json"),
    ),
    (
        "lang.string",
        include_str!("../../../sdk/std/registries/lang.string/1.0.json"),
    ),
    (
        "livery",
        include_str!("../../../sdk/std/registries/livery/1.0.json"),
    ),
    ("suit", include_str!("../../../sdk/std/registries/suit/1.0.json")),
    (
        "team.constructor",
        include_str!("../../../sdk/std/registries/team.constructor/1.0.json"),
    ),
    (
        "team.identity",
        include_str!("../../../sdk/std/registries/team.identity/1.0.json"),
    ),
    (
        "team.logo",
        include_str!("../../../sdk/std/registries/team.logo/1.0.json"),
    ),
    (
        "team.pit_times",
        include_str!("../../../sdk/std/registries/team.pit_times/1.0.json"),
    ),
    (
        "vehicle",
        include_str!("../../../sdk/std/registries/vehicle/1.0.json"),
    ),
    (
        "vehicle.handling",
        include_str!("../../../sdk/std/registries/vehicle.handling/1.0.json"),
    ),
    (
        "vehicle.package",
        include_str!("../../../sdk/std/registries/vehicle.package/1.0.json"),
    ),
    (
        "wearable.season_set",
        include_str!("../../../sdk/std/registries/wearable.season_set/1.0.json"),
    ),
];

fn schema(registry: &str) -> Option<&'static Value> {
    static PARSED: OnceLock<Vec<(&'static str, Value)>> = OnceLock::new();
    PARSED
        .get_or_init(|| {
            SCHEMAS
                .iter()
                .filter_map(|(name, text)| Some((*name, serde_json::from_str(text).ok()?)))
                .collect()
        })
        .iter()
        .find(|(name, _)| *name == registry)
        .map(|(_, s)| s)
}

/// `rel` as a path inside the addon's folder with `/` between folders, or `None` when it would
/// leave the folder. A trailing `/` (a folder) is kept.
pub fn inside(rel: &str) -> Option<String> {
    let norm = rel.trim().replace('\\', "/");
    if norm.is_empty() || norm.starts_with('/') || Path::new(rel).is_absolute() {
        return None;
    }
    let folder = norm.ends_with('/');
    let mut parts = Vec::new();
    for part in norm.split('/') {
        match part {
            "" | "." => {}
            ".." => return None,
            p if p.contains(':') => return None,
            p => parts.push(p),
        }
    }
    if parts.is_empty() {
        return None;
    }
    let mut out = parts.join("/");
    if folder {
        out.push('/');
    }
    Some(out)
}

/// The addon-folder paths `value` names through the `Path` fields of the schema `node`.
fn walk(root: &Value, node: &Value, value: &Value, out: &mut Vec<String>, depth: usize) {
    if depth > 64 || !node.is_object() {
        return;
    }
    let mut node = node;
    for _ in 0..16 {
        let Some(r) = node.get("$ref").and_then(Value::as_str) else {
            break;
        };
        if r == PATH_DEF {
            match value {
                Value::String(s) => out.push(s.clone()),
                Value::Object(o) => out.extend(o.get("erp").and_then(Value::as_str).map(str::to_owned)),
                _ => {}
            }
            return;
        }
        match r.strip_prefix('#').and_then(|p| root.pointer(p)) {
            Some(n) => node = n,
            None => return,
        }
    }
    for key in ["oneOf", "anyOf", "allOf"] {
        for alt in node.get(key).and_then(Value::as_array).into_iter().flatten() {
            walk(root, alt, value, out, depth + 1);
        }
    }
    match value {
        Value::Object(obj) => {
            for (k, v) in obj {
                let sub = node
                    .get("properties")
                    .and_then(|p| p.get(k))
                    .or_else(|| node.get("additionalProperties"));
                if let Some(sub) = sub {
                    walk(root, sub, v, out, depth + 1);
                }
            }
        }
        Value::Array(items) => {
            for (i, v) in items.iter().enumerate() {
                let sub = node
                    .get("prefixItems")
                    .and_then(|p| p.get(i))
                    .or_else(|| node.get("items"));
                if let Some(sub) = sub {
                    walk(root, sub, v, out, depth + 1);
                }
            }
        }
        _ => {}
    }
}

/// The paths a data file's records and layers name through `Path` fields, for every registry
/// whose schema the launcher has.
pub fn named_in_data(text: &str) -> Vec<String> {
    let Ok(Value::Object(map)) = serde_json::from_str::<Value>(text) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for (registry, records) in &map {
        let Some(root) = schema(registry) else {
            continue;
        };
        for value in records.as_object().into_iter().flat_map(|o| o.values()) {
            walk(root, root, value, &mut out, 0);
        }
    }
    out
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Named {
    /// Files to place, relative to the addon's folder, sorted.
    pub files: Vec<String>,
    /// Paths that aren't in the addon's folder, or that leave it.
    pub missing: Vec<String>,
}

/// The files in `dir` that the addon's data files and its lock's `files` name.
pub fn named(dir: &Path, data_files: &[String], lock_files: &[String]) -> Named {
    let mut wanted: Vec<String> = data_files
        .iter()
        .filter_map(|rel| std::fs::read_to_string(dir.join(rel)).ok())
        .flat_map(|text| named_in_data(&text))
        .collect();
    wanted.extend(lock_files.iter().cloned());
    let (mut files, mut missing) = (BTreeSet::new(), BTreeSet::new());
    for raw in wanted {
        let Some(rel) = inside(&raw) else {
            missing.insert(raw);
            continue;
        };
        let at = dir.join(rel.trim_end_matches('/'));
        if at.is_file() && !rel.ends_with('/') {
            files.insert(rel);
        } else if at.is_dir() {
            let base = rel.trim_end_matches('/').to_owned();
            for f in under(&at) {
                files.insert(format!("{base}/{f}"));
            }
        } else {
            missing.insert(raw);
        }
    }
    Named {
        files: files.into_iter().collect(),
        missing: missing.into_iter().collect(),
    }
}

/// Every file under `dir`, relative to it, without hidden files and folders.
fn under(dir: &Path) -> Vec<String> {
    let mut out = Vec::new();
    let mut todo = vec![(dir.to_path_buf(), String::new())];
    while let Some((at, prefix)) = todo.pop() {
        let Ok(rd) = std::fs::read_dir(&at) else {
            continue;
        };
        for e in rd.flatten() {
            let name = e.file_name().to_string_lossy().into_owned();
            if name.starts_with('.') {
                continue;
            }
            match e.file_type() {
                Ok(t) if t.is_dir() => todo.push((e.path(), format!("{prefix}{name}/"))),
                Ok(t) if t.is_file() => out.push(format!("{prefix}{name}")),
                _ => {}
            }
        }
    }
    out.sort();
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("swofty-paths-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn put(path: &Path, body: &str) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, body).unwrap();
    }

    #[test]
    fn a_path_stays_inside_the_addons_folder() {
        assert_eq!(inside("sounds/v10.bdl").as_deref(), Some("sounds/v10.bdl"));
        assert_eq!(inside("sounds\\v10.bdl").as_deref(), Some("sounds/v10.bdl"));
        assert_eq!(inside("./cars//ferrari/").as_deref(), Some("cars/ferrari/"));
        assert_eq!(inside("klänge/v10 ü.bdl").as_deref(), Some("klänge/v10 ü.bdl"));
        for bad in [
            "",
            "/etc/passwd",
            "../x.bdl",
            "a/../../b",
            "C:/x.bdl",
            "c:x",
            "\\\\server\\x",
            ".",
        ] {
            assert_eq!(inside(bad), None, "{bad}");
        }
    }

    #[test]
    fn finds_the_path_fields_of_records_and_layers_by_their_schema() {
        let named = named_in_data(
            r#"{
              "engine_sound": { "v10_screamer": { "bundle": "sounds/v10_screamer.bdl", "settings_from": "game/ferrari_2025" } },
              "vehicle": {
                "ferrari": { "model": "cars/ferrari/", "engine_sound": "mod.esp/v10_screamer" },
                "swofty.classic-seasons-2017/ferrari": { "model": { "erp": "cars/ferrari.erp", "resources": ["eaid://x"] } },
                "game/mercedes_2025": { "model": { "stock": "2025_asset_groups/x.erp" } }
              },
              "team.logo": { "williams_2017": { "images": { "hud": "logos/williams_hud.png", "hub": { "stock": "ui/hub.png" } } } },
              "helmet": { "hamilton_2017": { "archive": "helmets/hamilton_2017.erp", "colours": null } },
              "driver.head": { "senna": { "archive": "heads/senna/", "rig_from": "game/male_ls222" } },
              "someone.own": { "x": { "file": "not/a/schema/path.bin" } },
              "career.season": { "y": { "name": "sounds/v10_screamer.bdl" } }
            }"#,
        );
        let mut named = named;
        named.sort();
        assert_eq!(
            named,
            [
                "cars/ferrari.erp",
                "cars/ferrari/",
                "heads/senna/",
                "helmets/hamilton_2017.erp",
                "logos/williams_hud.png",
                "sounds/v10_screamer.bdl"
            ]
        );
    }

    #[test]
    fn places_named_files_and_folders_and_reports_the_rest() {
        let dir = tmp("named");
        put(
            &dir.join("registry/sounds.json"),
            r#"{ "engine_sound": { "v10": { "bundle": "sounds/v10.bdl" },
                                                          "v8": { "bundle": "sounds/v8.bdl" },
                                                          "up": { "bundle": "../outside.bdl" } } }"#,
        );
        put(
            &dir.join("registry/cars.json"),
            r#"{ "vehicle": { "car": { "model": "cars/car/" } } }"#,
        );
        put(&dir.join("sounds/v10.bdl"), "bdl");
        put(&dir.join("cars/car/car.erp"), "erp");
        put(&dir.join("cars/car/tex/a.dds"), "dds");
        put(&dir.join("cars/car/.cache/x"), "x");
        put(&dir.join("code/recorded.bin"), "bin");
        let data = vec!["registry/cars.json".to_owned(), "registry/sounds.json".to_owned()];
        let lock = vec!["code\\recorded.bin".to_owned(), "C:/nope".to_owned()];
        let n = named(&dir, &data, &lock);
        assert_eq!(
            n.files,
            [
                "cars/car/car.erp",
                "cars/car/tex/a.dds",
                "code/recorded.bin",
                "sounds/v10.bdl"
            ]
        );
        assert_eq!(n.missing, ["../outside.bdl", "C:/nope", "sounds/v8.bdl"]);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn every_core_registry_schema_is_embedded() {
        let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("../sdk/std/registries");
        for e in std::fs::read_dir(&dir)
            .unwrap()
            .flatten()
            .filter(|e| e.path().is_dir())
        {
            let name = e.file_name().to_string_lossy().into_owned();
            let (_, text) = SCHEMAS.iter().find(|(n, _)| *n == name).expect("embedded");
            let mut versions: Vec<String> = std::fs::read_dir(e.path())
                .unwrap()
                .flatten()
                .map(|f| f.file_name().to_string_lossy().into_owned())
                .filter(|f| f.ends_with(".json") && f.chars().next().is_some_and(|c| c.is_ascii_digit()))
                .collect();
            versions.sort();
            let newest = std::fs::read_to_string(e.path().join(versions.last().unwrap())).unwrap();
            assert_eq!(*text, newest, "{name}");
            assert!(schema(&name).is_some(), "{name}");
        }
    }
}
