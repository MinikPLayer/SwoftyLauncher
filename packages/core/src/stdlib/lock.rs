//! `registry.lock.json`, which `swofty dev pack` writes into an addon: what
//! its `SWOFTY_REGISTER` registers (ids and labels by registry), the
//! registries it has a dynamic provider on, and the references its code uses.
//! The launcher only reads it; nothing is applied from it. Unknown fields are
//! kept in `extra` and ignored.

use std::collections::BTreeMap;
use std::path::Path;

use serde_json::Value;

pub const FILE: &str = "registry.lock.json";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Provided {
    pub id: String,
    pub label: Option<String>,
    /// What the record stands for, such as a track's `venue/sochi`.
    pub provides: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LockNeed {
    pub record: String,
    /// Fallbacks, as `<registry>:<id>`: the reference is met when any of `record` and these is.
    pub or: Vec<String>,
    pub required: bool,
}

impl LockNeed {
    /// `record`, then each fallback in order.
    pub fn alternatives(&self) -> impl Iterator<Item = &str> {
        std::iter::once(self.record.as_str()).chain(self.or.iter().map(String::as_str))
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Lock {
    pub generated_by: Option<String>,
    pub provides: BTreeMap<String, Vec<Provided>>,
    pub providers: Vec<String>,
    /// Registries the addon defines, with their `major.minor`, when the lock lists them.
    pub registries: BTreeMap<String, String>,
    pub needs: Vec<LockNeed>,
    /// The files in the addon's folder its code names through `file()`, relative to the folder.
    pub files: Vec<String>,
    pub extra: BTreeMap<String, Value>,
}

impl Lock {
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
        let Value::Object(map) =
            serde_json::from_str::<Value>(text).map_err(|e| format!("{FILE} is not valid JSON ({e})"))?
        else {
            return Err(format!("{FILE} is not a JSON object"));
        };
        let mut out = Self::default();
        for (key, v) in map {
            match key.as_str() {
                "generated_by" => out.generated_by = v.as_str().map(str::to_owned),
                "provides" => {
                    for (registry, entries) in v.as_object().into_iter().flatten() {
                        let list = entries
                            .as_array()
                            .into_iter()
                            .flatten()
                            .filter_map(provided)
                            .collect();
                        out.provides.insert(registry.clone(), list);
                    }
                }
                "providers" => {
                    out.providers = v
                        .as_array()
                        .into_iter()
                        .flatten()
                        .filter_map(|p| p.as_str().or_else(|| p.get("registry").and_then(Value::as_str)))
                        .map(str::to_owned)
                        .collect();
                }
                "registries" => {
                    for (name, version) in v.as_object().into_iter().flatten() {
                        let version = version
                            .as_str()
                            .or_else(|| version.get("version").and_then(Value::as_str))
                            .unwrap_or("");
                        out.registries.insert(name.clone(), version.to_owned());
                    }
                }
                "needs" => {
                    out.needs = v
                        .as_array()
                        .into_iter()
                        .flatten()
                        .filter_map(|n| {
                            let record = n.get("record").and_then(Value::as_str)?;
                            let policy = n.get("policy").and_then(Value::as_str).unwrap_or("required");
                            let or = n
                                .get("or")
                                .and_then(Value::as_array)
                                .into_iter()
                                .flatten()
                                .filter_map(Value::as_str)
                                .map(str::to_owned)
                                .collect();
                            Some(LockNeed {
                                record: record.to_owned(),
                                or,
                                required: policy == "required",
                            })
                        })
                        .collect();
                }
                "files" | "paths" => {
                    out.files.extend(
                        v.as_array()
                            .into_iter()
                            .flatten()
                            .filter_map(|f| f.as_str().or_else(|| f.get("path").and_then(Value::as_str)))
                            .map(str::to_owned),
                    );
                }
                _ => {
                    out.extra.insert(key, v);
                }
            }
        }
        Ok(out)
    }

    pub fn ids(&self, registry: &str) -> impl Iterator<Item = &Provided> {
        self.provides.get(registry).into_iter().flatten()
    }

    /// Whether the addon provides record `id` of `registry`: by its id, as what one of its records
    /// stands for (a track's venue), or under a sub-list such as `track.venue`.
    pub fn provides_record(&self, registry: &str, id: &str) -> bool {
        let sub = format!("{registry}.");
        self.ids(registry)
            .any(|p| p.id == id || p.provides.iter().any(|v| v == id))
            || self
                .provides
                .iter()
                .filter(|(name, _)| name.starts_with(&sub))
                .any(|(_, list)| list.iter().any(|p| p.id == id))
    }

    /// Whether the addon registers into or defines `registry`.
    pub fn knows_registry(&self, registry: &str) -> bool {
        self.registries.contains_key(registry)
            || self.provides.contains_key(registry)
            || self.providers.iter().any(|p| p == registry)
    }
}

fn provided(v: &Value) -> Option<Provided> {
    if let Some(id) = v.as_str() {
        return Some(Provided {
            id: id.to_owned(),
            label: None,
            provides: Vec::new(),
        });
    }
    let id = v.get("id").and_then(Value::as_str)?;
    Some(Provided {
        id: id.to_owned(),
        label: v.get("label").and_then(Value::as_str).map(str::to_owned),
        provides: v
            .get("provides")
            .and_then(Value::as_array)
            .into_iter()
            .flatten()
            .filter_map(Value::as_str)
            .map(str::to_owned)
            .collect(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_the_lock_files_the_docs_show() {
        let l = Lock::parse(
            r#"{
              "generated_by": "swofty dev pack 1.6.0",
              "provides": {
                "career.mode": [ { "id": "swofty.progressive/progressive", "label": "lng_progressive_title" } ],
                "career.season": [ "swofty.progressive/2016", "swofty.progressive/2017" ],
                "track": [ { "id": "mod.sochi/sochi", "label": "Sochi Autodrom" } ],
                "track.venue": [ "venue/sochi" ]
              },
              "providers": [ "career.driver_ratings" ],
              "needs": [ { "record": "track:venue/sochi", "policy": "optional" }, { "record": "vehicle:game/ferrari_2025" },
                         { "record": "vehicle:mod.x/a", "or": ["vehicle:mod.y/b", "vehicle:game/ferrari_2025"], "policy": "optional" } ]
            }"#,
        )
        .unwrap();
        assert_eq!(l.generated_by.as_deref(), Some("swofty dev pack 1.6.0"));
        assert_eq!(l.ids("career.season").count(), 2);
        assert_eq!(
            l.ids("career.mode").next().unwrap().label.as_deref(),
            Some("lng_progressive_title")
        );
        assert!(l.provides_record("track", "mod.sochi/sochi"));
        assert!(l.provides_record("track", "venue/sochi"));
        assert!(!l.provides_record("track", "venue/hockenheimring"));
        assert!(l.knows_registry("career.driver_ratings"));
        assert!(l.knows_registry("career.season"));
        assert!(!l.knows_registry("vehicle"));
        assert_eq!(
            l.needs,
            [
                LockNeed {
                    record: "track:venue/sochi".into(),
                    or: vec![],
                    required: false
                },
                LockNeed {
                    record: "vehicle:game/ferrari_2025".into(),
                    or: vec![],
                    required: true
                },
                LockNeed {
                    record: "vehicle:mod.x/a".into(),
                    or: vec!["vehicle:mod.y/b".into(), "vehicle:game/ferrari_2025".into()],
                    required: false
                },
            ]
        );
        assert_eq!(
            l.needs[2].alternatives().collect::<Vec<_>>(),
            ["vehicle:mod.x/a", "vehicle:mod.y/b", "vehicle:game/ferrari_2025"]
        );
    }

    #[test]
    fn a_record_can_say_what_it_stands_for() {
        let l = Lock::parse(
            r#"{ "provides": { "track": [ { "id": "mod.bahrain24/sepang", "provides": ["venue/sepang"] } ] },
                 "registries": { "tails.whatif.presets": "1.0" }, "later": true,
                 "files": [ "sounds/v10.bdl", { "path": "cars/ferrari/" } ] }"#,
        )
        .unwrap();
        assert_eq!(l.files, ["sounds/v10.bdl", "cars/ferrari/"]);
        assert!(l.provides_record("track", "venue/sepang"));
        assert_eq!(l.registries["tails.whatif.presets"], "1.0");
        assert_eq!(l.extra["later"], true);
    }

    #[test]
    fn a_broken_lock_is_an_error() {
        assert!(Lock::parse("[1]").is_err());
        assert!(Lock::parse("nope").is_err());
    }
}
