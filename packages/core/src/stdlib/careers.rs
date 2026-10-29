//! The career index (docs/std/dependencies.md "Careers remember what they
//! were made with"). The core gives each career an id and a dependency
//! record, saved in its save entry and mirrored to
//! `$DATA/careers/index.json`, the launcher's data folder, which the launcher
//! names to the core in `<game>/swofty/state.json`, with the launcher profile
//! that started it. Before 1.6.0 the core wrote `<game>/swofty/careers/index.json`;
//! the launcher merges that file into the data folder's by career id and
//! removes it, so the list survives a restore of the game folder, and checks
//! each career's addons against a profile.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use serde_json::Value;

use super::range::Range;
use crate::manifest::AddonManifest;

pub const GAME_PATH: &str = "swofty/careers/index.json";
pub const DATA_PATH: &str = "careers/index.json";

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct CareerAddon {
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub version: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub need: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub why: Option<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Career {
    pub career: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub profile: Option<String>,
    #[serde(default)]
    pub label: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub key: Option<String>,
    #[serde(default)]
    pub addons: Vec<CareerAddon>,
    #[serde(flatten)]
    pub extra: BTreeMap<String, Value>,
}

/// The careers a career index holds: `{ "careers": [...] }`, a list, or an object of records by
/// save key.
pub fn parse(text: &str) -> Vec<Career> {
    let Ok(v) = serde_json::from_str::<Value>(text) else {
        return Vec::new();
    };
    let one = |key: Option<&str>, v: &Value| -> Option<Career> {
        let mut c: Career = serde_json::from_value(v.clone()).ok()?;
        if c.key.is_none() {
            c.key = key.map(str::to_owned);
        }
        (!c.career.is_empty()).then_some(c)
    };
    match &v {
        Value::Array(list) => list.iter().filter_map(|c| one(None, c)).collect(),
        Value::Object(map) => match map.get("careers") {
            Some(Value::Array(list)) => list.iter().filter_map(|c| one(None, c)).collect(),
            Some(Value::Object(by_key)) => by_key.iter().filter_map(|(k, c)| one(Some(k), c)).collect(),
            _ => map
                .iter()
                .filter(|(_, c)| c.is_object())
                .filter_map(|(k, c)| one(Some(k), c))
                .collect(),
        },
        _ => Vec::new(),
    }
}

pub fn read(path: &Path) -> Vec<Career> {
    std::fs::read_to_string(path)
        .map(|t| parse(&t))
        .unwrap_or_default()
}

pub fn data_path(data: &Path) -> PathBuf {
    data.join(DATA_PATH)
}

/// Merge the game folder's career index, written before the core used the data folder, into the
/// launcher's copy, newest record per career id first, and return the merged list. A career the
/// game no longer lists stays in the copy. A career the core wrote without a profile gets `stamp`,
/// the profile of the session that just ran.
pub fn sync(data: &Path, game: Option<&Path>, stamp: Option<&str>) -> crate::Result<Vec<Career>> {
    let mut kept = read(&data_path(data));
    let mut changed = false;
    let fresh = game.map(|g| read(&g.join(GAME_PATH))).unwrap_or_default();
    for mut c in fresh {
        changed = true;
        match kept.iter_mut().find(|k| k.career == c.career) {
            Some(k) => {
                c.profile = c.profile.or_else(|| k.profile.clone());
                *k = c;
            }
            None => kept.push(c),
        }
    }
    if let Some(stamp) = stamp {
        for c in kept.iter_mut().filter(|c| c.profile.is_none()) {
            c.profile = Some(stamp.to_owned());
            changed = true;
        }
    }
    if changed {
        crate::write_json_shared(
            &data_path(data),
            &serde_json::json!({ "version": 1, "careers": kept }),
        )?;
    }
    Ok(kept)
}

/// Where the launcher keeps the core's stat-id store: the one store of the install.
pub fn kept_ids(data: &Path) -> PathBuf {
    super::reports::path(data, super::reports::IDS)
}

/// Move what the core wrote in the game folder before it used the data folder: the career index
/// (merged), the stat-id store (the newer of the two wins) and the reports (when the data folder
/// has none). Returns one line for each thing moved.
pub fn migrate(data: &Path, game: &Path) -> crate::Result<Vec<String>> {
    use super::reports;
    let mut out = Vec::new();
    let old_index = game.join(GAME_PATH);
    if old_index.is_file() {
        sync(data, Some(game), None)?;
        std::fs::remove_file(&old_index).ok();
        out.push("career index".to_owned());
    }
    let modified = |p: &Path| std::fs::metadata(p).and_then(|m| m.modified()).ok();
    for name in reports::SENT.iter().copied().chain([reports::IDS]) {
        let old = reports::legacy_path(game, name);
        if !old.is_file() {
            continue;
        }
        let new = reports::path(data, name);
        let take = !new.is_file() || (name == reports::IDS && modified(&old) > modified(&new));
        if take {
            let dir = new.parent().expect("has parent");
            std::fs::create_dir_all(dir).map_err(crate::io_err(dir))?;
            std::fs::copy(&old, &new).map_err(crate::io_err(&new))?;
            out.push(name.to_owned());
        }
        std::fs::remove_file(&old).ok();
    }
    Ok(out)
}

/// Before the game folder's `swofty/` goes (Restore game files): move anything the core left there
/// into the data folder, so nothing is lost. The data folder is not touched by the restore.
pub fn keep(data: &Path, game: &Path) -> crate::Result<()> {
    migrate(data, game).map(|_| ())
}

/// What a career needs that profile `order` doesn't have on: one line per addon, empty when it
/// loads in that profile.
pub fn missing(c: &Career, order: &[String], installed: &BTreeMap<String, AddonManifest>) -> Vec<String> {
    let mut out = Vec::new();
    for a in &c.addons {
        let name = installed
            .get(&a.id)
            .map_or(a.id.clone(), |m| format!("{} ({})", m.name, a.id));
        let range = a
            .need
            .as_deref()
            .and_then(|n| Range::parse(n).ok())
            .unwrap_or_else(Range::any);
        match installed.get(&a.id) {
            None => out.push(format!("needs {name}, which isn't installed")),
            Some(_) if !order.contains(&a.id) => out.push(format!("needs {name}, which is off")),
            Some(m) if !range.matches(&m.version) => {
                out.push(format!("needs {name} {range}, and {} is installed", m.version))
            }
            Some(_) => {}
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const RECORD: &str = r#"{
      "career": "6f3e9a",
      "profile": "progressive",
      "label": "Progressive Career: Max Verstappen, 2016, season 1",
      "match": { "driver": 4103, "team": 2, "season_index": 0 },
      "made_with": { "launcher": "1.6.0", "core": "0.3.0", "game_build": 25251167 },
      "addons": [
        { "id": "swofty.progressive", "version": "1.3.0", "need": "^1.3.0", "why": "career content f1_career_progressive" },
        { "id": "mod.sochi", "version": "1.1.0", "need": "^1.1.0", "why": "calendar round 4: track mod.sochi/sochi" }
      ],
      "records": [ { "ref": "track:venue/sochi", "resolved": "mod.sochi/sochi", "stat_id": 18 } ]
    }"#;

    fn manifest(id: &str, version: &str) -> AddonManifest {
        serde_json::from_str(&format!(
            r#"{{ "schema_version": 1, "id": "{id}", "name": "N {id}", "version": "{version}", "author": "a",
                 "compat": {{ "game": "f1_25" }} }}"#
        ))
        .unwrap()
    }

    #[test]
    fn reads_every_shape_of_the_index() {
        let by_key = format!(r#"{{ "f1_career_progressive/pg-1849223-max_verstappen": {RECORD} }}"#);
        let c = parse(&by_key);
        assert_eq!(c.len(), 1);
        assert_eq!(c[0].career, "6f3e9a");
        assert_eq!(c[0].profile.as_deref(), Some("progressive"));
        assert_eq!(
            c[0].key.as_deref(),
            Some("f1_career_progressive/pg-1849223-max_verstappen")
        );
        assert_eq!(c[0].addons[1].need.as_deref(), Some("^1.1.0"));
        assert!(c[0].extra.contains_key("made_with"));
        assert_eq!(parse(&format!(r#"{{ "careers": [{RECORD}] }}"#)).len(), 1);
        assert_eq!(parse(&format!("[{RECORD}]")).len(), 1);
        assert!(parse("garbage").is_empty());
        assert!(parse(r#"{ "version": 1 }"#).is_empty());
    }

    #[test]
    fn names_what_a_career_needs_in_a_profile() {
        let c = &parse(&format!("[{RECORD}]"))[0];
        let installed: BTreeMap<String, AddonManifest> = [
            ("swofty.progressive", manifest("swofty.progressive", "1.3.2")),
            ("mod.sochi", manifest("mod.sochi", "1.0.0")),
        ]
        .into_iter()
        .map(|(k, v)| (k.to_owned(), v))
        .collect();
        let both: Vec<String> = vec!["swofty.progressive".into(), "mod.sochi".into()];
        assert_eq!(
            missing(c, &both, &installed),
            ["needs N mod.sochi (mod.sochi) ^1.1.0, and 1.0.0 is installed"]
        );
        assert_eq!(
            missing(c, &both[..1], &installed),
            ["needs N mod.sochi (mod.sochi), which is off"]
        );
        assert_eq!(
            missing(c, &both, &BTreeMap::new()),
            [
                "needs swofty.progressive, which isn't installed",
                "needs mod.sochi, which isn't installed"
            ]
        );
    }

    #[test]
    fn the_launchers_copy_keeps_careers_and_their_profile() {
        let root = std::env::temp_dir().join(format!("swofty-careers-{}", std::process::id()));
        let (data, game) = (root.join("data"), root.join("game"));
        std::fs::create_dir_all(game.join("swofty/careers")).unwrap();
        std::fs::write(game.join(GAME_PATH), format!("[{RECORD}]")).unwrap();
        let first = sync(&data, Some(&game), Some("progressive")).unwrap();
        assert_eq!(first.len(), 1);
        let later = RECORD
            .replace("\"profile\": \"progressive\",", "")
            .replace("season 1", "season 2");
        let other = RECORD
            .replace("6f3e9a", "77aa01")
            .replace("progressive\",", "vanilla-ish\",");
        std::fs::write(game.join(GAME_PATH), format!("[{later}, {other}]")).unwrap();
        let merged = sync(&data, Some(&game), Some("other")).unwrap();
        assert_eq!(merged.len(), 2);
        assert_eq!(merged[0].profile.as_deref(), Some("progressive"));
        assert!(merged[0].label.ends_with("season 2"));
        std::fs::remove_file(game.join(GAME_PATH)).unwrap();
        assert_eq!(sync(&data, Some(&game), None).unwrap().len(), 2);
        assert_eq!(sync(&data, None, None).unwrap().len(), 2);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_career_without_a_profile_gets_the_sessions_and_the_ids_survive_a_restore() {
        let root = std::env::temp_dir().join(format!("swofty-careers-keep-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (data, game) = (root.join("data"), root.join("game"));
        let unstamped = RECORD.replace("\"profile\": \"progressive\",", "");
        std::fs::create_dir_all(game.join("swofty/careers")).unwrap();
        std::fs::write(game.join(GAME_PATH), format!("[{unstamped}]")).unwrap();
        let synced = sync(&data, Some(&game), Some("tails")).unwrap();
        assert_eq!(synced[0].profile.as_deref(), Some("tails"));
        let ids = crate::stdlib::reports::legacy_path(&game, crate::stdlib::reports::IDS);
        std::fs::create_dir_all(ids.parent().unwrap()).unwrap();
        std::fs::write(&ids, "{\"track\":{\"mod.sochi/sochi\":18}}").unwrap();
        keep(&data, &game).unwrap();
        std::fs::remove_dir_all(game.join("swofty")).unwrap();
        assert_eq!(read(&data_path(&data)).len(), 1);
        assert_eq!(
            std::fs::read_to_string(kept_ids(&data)).unwrap(),
            "{\"track\":{\"mod.sochi/sochi\":18}}"
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn migrating_moves_the_old_files_once_and_the_newer_ids_win() {
        let root = std::env::temp_dir().join(format!("swofty-careers-migrate-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (data, game) = (root.join("d\u{e4}ta \u{30b9}"), root.join("game"));
        let put = |p: &Path, t: &str| {
            std::fs::create_dir_all(p.parent().unwrap()).unwrap();
            std::fs::write(p, t).unwrap();
        };
        use crate::stdlib::reports::{IDS, ROOM, legacy_path, path};
        put(&legacy_path(&game, IDS), "{\"old\":1}");
        put(&legacy_path(&game, ROOM), "{\"presize\":\"applied\"}");
        put(&game.join(GAME_PATH), &format!("[{RECORD}]"));
        put(&path(&data, ROOM), "{\"presize\":\"kept\"}");
        let moved = migrate(&data, &game).unwrap();
        assert_eq!(moved, ["career index", "ids.json"]);
        assert_eq!(std::fs::read_to_string(path(&data, IDS)).unwrap(), "{\"old\":1}");
        assert_eq!(
            std::fs::read_to_string(path(&data, ROOM)).unwrap(),
            "{\"presize\":\"kept\"}"
        );
        assert_eq!(read(&data_path(&data)).len(), 1);
        for gone in [
            legacy_path(&game, IDS),
            legacy_path(&game, ROOM),
            game.join(GAME_PATH),
        ] {
            assert!(!gone.exists(), "{}", gone.display());
        }
        assert!(migrate(&data, &game).unwrap().is_empty());
        put(&legacy_path(&game, IDS), "{\"stale\":1}");
        std::fs::File::options()
            .write(true)
            .open(legacy_path(&game, IDS))
            .unwrap()
            .set_modified(std::time::SystemTime::UNIX_EPOCH)
            .unwrap();
        assert!(migrate(&data, &game).unwrap().is_empty());
        assert_eq!(std::fs::read_to_string(path(&data, IDS)).unwrap(), "{\"old\":1}");
        assert!(!legacy_path(&game, IDS).exists());
        std::fs::remove_dir_all(&root).ok();
    }
}
