//! `registry.json`, next to an addon's `swofty.json`: what the addon needs
//! and recommends, the addon versions it breaks or conflicts with, the ids it
//! was published under before, guards for addons without code, and the build
//! operation files its data files replace. Launchers of 1.5 and earlier never
//! open it. Every field is optional; a field or an entry this launcher doesn't
//! know is kept in `extra` and ignored.

use std::collections::BTreeMap;
use std::path::Path;

use serde_json::Value;

use super::range::Range;

pub const FILE: &str = "registry.json";

/// The capability in the loader's `features.json` of a core that applies the
/// content registries' data-file records as database rows at boot.
pub const ROWS_FEATURE: &str = "registry_rows";

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Target {
    Addon(String),
    Registry(String),
    Service(String),
    Hook(String),
    Record { registry: String, id: String },
}

impl Target {
    pub fn record(text: &str) -> Option<Self> {
        let (registry, id) = text.split_once(':')?;
        (!registry.is_empty() && !id.is_empty()).then(|| Self::Record {
            registry: registry.to_owned(),
            id: id.to_owned(),
        })
    }

    pub fn describe(&self) -> String {
        match self {
            Self::Addon(id) => id.clone(),
            Self::Registry(name) => format!("the {name} registry"),
            Self::Service(name) => format!("the {name} service"),
            Self::Hook(name) => format!("the {name} hook point"),
            Self::Record { registry, id } => format!("{registry} {id}"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Need {
    pub target: Target,
    pub version: Range,
    pub for_: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Clash {
    pub id: String,
    pub version: Range,
    pub why: Option<String>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct RegistryManifest {
    pub requires: Vec<Need>,
    pub recommends: Vec<Need>,
    pub breaks: Vec<Clash>,
    pub conflicts: Vec<Clash>,
    pub aliases: Vec<String>,
    pub saves_careers: Option<String>,
    pub guards: Vec<Value>,
    /// Build operation files (paths as `swofty.json`'s `operations` lists
    /// them) that the addon's data files replace: a launcher whose core
    /// applies those records leaves them out of the build.
    pub replaced_operations: Vec<String>,
    /// Entries this launcher couldn't read, as `<field>[<index>]: <why>`; they are ignored.
    pub skipped: Vec<String>,
    /// Top-level fields this launcher doesn't know, kept as they are.
    pub extra: BTreeMap<String, Value>,
}

impl RegistryManifest {
    /// The addon folder's `registry.json`: `Ok(None)` when it has none, `Err` when it isn't a JSON object.
    pub fn load(dir: &Path) -> Result<Option<Self>, String> {
        let path = dir.join(FILE);
        let text = match std::fs::read_to_string(&path) {
            Ok(t) => t,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(format!("{FILE} can't be read: {e}")),
        };
        Self::parse(&text).map(Some)
    }

    pub fn parse(text: &str) -> Result<Self, String> {
        let value: Value =
            serde_json::from_str(text).map_err(|e| format!("{FILE} is not valid JSON ({e})"))?;
        Self::from_value(value)
    }

    pub fn from_value(value: Value) -> Result<Self, String> {
        let Value::Object(map) = value else {
            return Err(format!("{FILE} is not a JSON object"));
        };
        let mut out = Self::default();
        for (key, v) in map {
            match key.as_str() {
                "requires" => out.requires = needs(&v, "requires", &mut out.skipped),
                "recommends" => out.recommends = needs(&v, "recommends", &mut out.skipped),
                "breaks" => out.breaks = clashes(&v, "breaks", &mut out.skipped),
                "conflicts" => out.conflicts = clashes(&v, "conflicts", &mut out.skipped),
                "aliases" => {
                    for (i, a) in array(&v) {
                        match a.get("id").and_then(Value::as_str).or_else(|| a.as_str()) {
                            Some(id) if !id.is_empty() => out.aliases.push(id.to_owned()),
                            _ => out.skipped.push(format!("aliases[{i}]: no id")),
                        }
                    }
                }
                "saves" => {
                    out.saves_careers = v.get("careers").and_then(Value::as_str).map(str::to_owned);
                }
                "guards" => out.guards = array(&v).map(|(_, g)| g.clone()).collect(),
                "replaced_operations" => {
                    for (i, f) in array(&v) {
                        match f.as_str().map(op_path).filter(|f| !f.is_empty()) {
                            Some(f) => out.replaced_operations.push(f),
                            None => out
                                .skipped
                                .push(format!("replaced_operations[{i}]: not a file path")),
                        }
                    }
                }
                _ => {
                    out.extra.insert(key, v);
                }
            }
        }
        Ok(out)
    }

    pub fn is_empty(&self) -> bool {
        self.requires.is_empty()
            && self.recommends.is_empty()
            && self.breaks.is_empty()
            && self.conflicts.is_empty()
            && self.aliases.is_empty()
            && self.saves_careers.is_none()
            && self.guards.is_empty()
            && self.replaced_operations.is_empty()
    }

    /// Whether `rel`, an operation file of the addon's `swofty.json`, is one
    /// its data files replace.
    pub fn replaces(&self, rel: &str) -> bool {
        let rel = op_path(rel);
        self.replaced_operations.contains(&rel)
    }
}

/// An operation file path as `swofty.json` lists it, compared without a
/// leading `./` and with `/` separators.
pub fn op_path(rel: &str) -> String {
    let rel = rel.trim().replace('\\', "/");
    rel.strip_prefix("./").unwrap_or(&rel).to_owned()
}

/// Whether the core in `loader` (a loader folder) applies the content
/// registries' data-file records itself: its `features.json` lists
/// [`ROWS_FEATURE`].
pub fn core_applies_rows(loader: &Path) -> bool {
    let path = loader.join(crate::planner::FEATURES_FILE);
    let Ok(text) = std::fs::read_to_string(&path) else {
        return false;
    };
    serde_json::from_str::<Value>(&text)
        .ok()
        .and_then(|v| {
            v.get("features")?
                .get(ROWS_FEATURE)?
                .as_str()
                .map(|l| l == "stable")
        })
        .unwrap_or(false)
}

fn array(v: &Value) -> impl Iterator<Item = (usize, &Value)> {
    v.as_array().into_iter().flatten().enumerate()
}

fn text<'a>(v: &'a Value, key: &str) -> Option<&'a str> {
    v.get(key).and_then(Value::as_str).filter(|s| !s.is_empty())
}

fn needs(v: &Value, field: &str, skipped: &mut Vec<String>) -> Vec<Need> {
    let mut out = Vec::new();
    for (i, e) in array(v) {
        let target = if let Some(id) = text(e, "addon").or_else(|| text(e, "id")) {
            Some(Target::Addon(id.to_owned()))
        } else if let Some(name) = text(e, "registry") {
            Some(Target::Registry(name.to_owned()))
        } else if let Some(name) = text(e, "service") {
            Some(Target::Service(name.to_owned()))
        } else if let Some(name) = text(e, "hook") {
            Some(Target::Hook(name.to_owned()))
        } else if let Some(r) = text(e, "record") {
            match Target::record(r) {
                Some(t) => Some(t),
                None => {
                    skipped.push(format!("{field}[{i}]: record {r:?} is not <registry>:<id>"));
                    continue;
                }
            }
        } else {
            None
        };
        let Some(target) = target else {
            skipped.push(format!(
                "{field}[{i}]: not an addon, registry, service, hook or record entry"
            ));
            continue;
        };
        let version = match Range::of(text(e, "version")) {
            Ok(r) => r,
            Err(why) => {
                skipped.push(format!("{field}[{i}]: {why}"));
                continue;
            }
        };
        out.push(Need {
            target,
            version,
            for_: text(e, "for").map(str::to_owned),
        });
    }
    out
}

fn clashes(v: &Value, field: &str, skipped: &mut Vec<String>) -> Vec<Clash> {
    let mut out = Vec::new();
    for (i, e) in array(v) {
        let Some(id) = text(e, "id").or_else(|| text(e, "addon")) else {
            skipped.push(format!("{field}[{i}]: no id"));
            continue;
        };
        let version = match Range::of(text(e, "version")) {
            Ok(r) => r,
            Err(why) => {
                skipped.push(format!("{field}[{i}]: {why}"));
                continue;
            }
        };
        out.push(Clash {
            id: id.to_owned(),
            version,
            why: text(e, "why").map(str::to_owned),
        });
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_every_field_the_docs_give() {
        let m = RegistryManifest::parse(
            r#"{
              "requires": [
                { "registry": "career.season", "version": "^1" },
                { "service": "progressive.history", "version": ">=1" },
                { "hook": "career.calendar.add_activity", "version": ">=1" },
                { "record": "vehicle:swofty.classic-seasons-2017/ferrari", "version": ">=1.0.0" },
                { "addon": "swofty.progressive", "version": "^1.3.0" },
                { "id": "mod.sochi" }
              ],
              "recommends": [ { "record": "track:venue/sochi", "for": "the 2016 Russian Grand Prix" } ],
              "breaks": [ { "id": "swofty.career-editor", "version": "<0.5.0", "why": "its contract overwrite" } ],
              "conflicts": [ { "id": "someone.lineups", "version": "*", "why": "both rewrite the line-ups" } ],
              "aliases": [ { "id": "mod.bahrain24" } ],
              "saves": { "careers": "loaded" },
              "guards": [ { "on": "career.start", "mode": "my_team", "block": { "title": "t", "body": "b" } } ]
            }"#,
        )
        .unwrap();
        assert_eq!(m.requires.len(), 6);
        assert_eq!(m.requires[0].target, Target::Registry("career.season".into()));
        assert_eq!(
            m.requires[1].target,
            Target::Service("progressive.history".into())
        );
        assert_eq!(
            m.requires[2].target,
            Target::Hook("career.calendar.add_activity".into())
        );
        assert_eq!(
            m.requires[3].target,
            Target::Record {
                registry: "vehicle".into(),
                id: "swofty.classic-seasons-2017/ferrari".into()
            }
        );
        assert_eq!(m.requires[4].target, Target::Addon("swofty.progressive".into()));
        assert!(m.requires[5].version.is_any());
        assert_eq!(
            m.recommends[0].for_.as_deref(),
            Some("the 2016 Russian Grand Prix")
        );
        assert_eq!(m.breaks[0].id, "swofty.career-editor");
        assert_eq!(m.breaks[0].version.as_str(), "<0.5.0");
        assert_eq!(m.conflicts[0].why.as_deref(), Some("both rewrite the line-ups"));
        assert_eq!(m.aliases, ["mod.bahrain24"]);
        assert_eq!(m.saves_careers.as_deref(), Some("loaded"));
        assert_eq!(m.guards.len(), 1);
        assert!(m.skipped.is_empty());
        assert!(m.extra.is_empty());
    }

    #[test]
    fn unknown_fields_and_entries_are_kept_and_ignored() {
        let m = RegistryManifest::parse(
            r#"{
              "requires": [ { "widget": "x" }, { "registry": "career.season", "since": "1.7" }, { "record": "nocolon" } ],
              "breaks": [ { "version": "1" }, { "id": "a.b", "version": "!!" } ],
              "future": { "anything": [1, 2] }
            }"#,
        )
        .unwrap();
        assert_eq!(m.requires.len(), 1);
        assert_eq!(m.skipped.len(), 4, "{:?}", m.skipped);
        assert!(m.breaks.is_empty());
        assert_eq!(m.extra["future"]["anything"][1], 2);
    }

    #[test]
    fn an_empty_object_is_empty_and_a_non_object_is_an_error() {
        assert!(RegistryManifest::parse("{}").unwrap().is_empty());
        assert!(
            RegistryManifest::parse("[]")
                .unwrap_err()
                .contains("not a JSON object")
        );
        assert!(
            RegistryManifest::parse("{")
                .unwrap_err()
                .contains("not valid JSON")
        );
    }

    #[test]
    fn replaced_operations_name_files_as_swofty_json_lists_them() {
        let m = RegistryManifest::parse(
            r#"{ "replaced_operations": ["db/records.json", "./db\\more.json", 3, ""] }"#,
        )
        .unwrap();
        assert_eq!(m.replaced_operations, ["db/records.json", "db/more.json"]);
        assert_eq!(m.skipped.len(), 2, "{:?}", m.skipped);
        assert!(m.replaces("db/records.json") && m.replaces("./db/records.json"));
        assert!(m.replaces("db/more.json") && !m.replaces("db/season.json"));
        assert!(!m.is_empty());
    }

    #[test]
    fn only_a_core_that_lists_the_rows_feature_applies_rows() {
        let dir = std::env::temp_dir().join(format!("swofty-rowsfeature-{}", std::process::id()));
        let file = dir.join(crate::planner::FEATURES_FILE);
        std::fs::create_dir_all(file.parent().unwrap()).unwrap();
        assert!(!core_applies_rows(&dir), "no features.json");
        std::fs::write(&file, r#"{ "schema": 1, "features": { "db": "stable" } }"#).unwrap();
        assert!(!core_applies_rows(&dir), "a 1.5 core");
        std::fs::write(
            &file,
            r#"{ "schema": 1, "features": { "db": "stable", "registry_rows": "experimental" } }"#,
        )
        .unwrap();
        assert!(!core_applies_rows(&dir));
        std::fs::write(
            &file,
            r#"{ "schema": 1, "features": { "db": "stable", "registry_rows": "stable" } }"#,
        )
        .unwrap();
        assert!(core_applies_rows(&dir));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn a_folder_without_one_has_none() {
        let dir = std::env::temp_dir().join(format!("swofty-regjson-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        assert_eq!(RegistryManifest::load(&dir), Ok(None));
        std::fs::write(dir.join(FILE), r#"{ "aliases": ["old.id"] }"#).unwrap();
        assert_eq!(RegistryManifest::load(&dir).unwrap().unwrap().aliases, ["old.id"]);
        std::fs::remove_dir_all(&dir).ok();
    }
}
