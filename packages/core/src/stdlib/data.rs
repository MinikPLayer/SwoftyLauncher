//! An addon's data files: the `registry/*.json` files in its folder, which
//! the core applies in game. Each maps registry names to records: a key
//! without `/` is a record the addon adds under its own id
//! (`<addon>/<key>`), and a key with one is a layer on another record.

use std::path::{Path, PathBuf};

use serde_json::Value;

pub const DIR: &str = "registry";

/// The addon folder's data files, as paths relative to it (`registry/sounds.json`), sorted.
pub fn files(dir: &Path) -> Vec<String> {
    let root = dir.join(DIR);
    let mut out = Vec::new();
    let mut todo: Vec<(PathBuf, String)> = vec![(root, format!("{DIR}/"))];
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
                Ok(t) if t.is_file() && name.to_ascii_lowercase().ends_with(".json") => {
                    out.push(format!("{prefix}{name}"))
                }
                _ => {}
            }
        }
    }
    out.sort();
    out
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Record {
    pub registry: String,
    pub id: String,
    pub provides: Vec<String>,
}

/// The records an addon's data file adds, under the addon's own ids.
pub fn records(addon: &str, text: &str) -> Vec<Record> {
    let Ok(Value::Object(map)) = serde_json::from_str::<Value>(text) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for (registry, records) in map.iter().filter(|(k, _)| !k.starts_with('$')) {
        for (key, value) in records.as_object().into_iter().flatten() {
            if key.contains('/') || key.starts_with('$') {
                continue;
            }
            let provides = value
                .get("provides")
                .and_then(Value::as_array)
                .into_iter()
                .flatten()
                .filter_map(Value::as_str)
                .map(str::to_owned)
                .collect();
            out.push(Record {
                registry: registry.clone(),
                id: format!("{addon}/{key}"),
                provides,
            });
        }
    }
    out.sort_by(|a, b| (&a.registry, &a.id).cmp(&(&b.registry, &b.id)));
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lists_json_files_under_registry_sorted() {
        let dir = std::env::temp_dir().join(format!("swofty-datafiles-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("registry/more")).unwrap();
        std::fs::write(dir.join("registry/sounds.json"), "{}").unwrap();
        std::fs::write(dir.join("registry/layers.json"), "{}").unwrap();
        std::fs::write(dir.join("registry/more/cars.json"), "{}").unwrap();
        std::fs::write(dir.join("registry/notes.txt"), "").unwrap();
        std::fs::write(dir.join("registry/.hidden.json"), "{}").unwrap();
        assert_eq!(
            files(&dir),
            [
                "registry/layers.json",
                "registry/more/cars.json",
                "registry/sounds.json"
            ]
        );
        assert!(files(&dir.join("nothing")).is_empty());
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn records_are_the_keys_without_a_slash() {
        let r = records(
            "mod.sochi",
            r#"{ "track": { "sochi": { "code": "sochi", "provides": ["venue/sochi"] },
                            "game/catalunya": { "name": "x" } },
                 "engine_sound": { "v10": {} } }"#,
        );
        assert_eq!(
            r,
            [
                Record {
                    registry: "engine_sound".into(),
                    id: "mod.sochi/v10".into(),
                    provides: vec![]
                },
                Record {
                    registry: "track".into(),
                    id: "mod.sochi/sochi".into(),
                    provides: vec!["venue/sochi".into()]
                },
            ]
        );
        assert!(records("a.b", "not json").is_empty());
    }
}
