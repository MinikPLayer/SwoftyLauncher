use std::collections::{BTreeMap, BTreeSet};

use serde::Serialize;
use serde_json::Value;

pub const LOCK_FILE: &str = "registry.lock.json";

pub fn generated_by() -> String {
    format!("swofty dev pack {}", env!("CARGO_PKG_VERSION"))
}

#[derive(Debug, Serialize, PartialEq)]
pub struct Lock {
    pub generated_by: String,
    pub provides: BTreeMap<String, Vec<Provided>>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub providers: Vec<String>,
    pub needs: Vec<Need>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub files: Vec<String>,
}

#[derive(Debug, Serialize, PartialEq, Clone)]
#[serde(untagged)]
pub enum Provided {
    Id(String),
    Labelled { id: String, label: String },
}

impl Provided {
    fn id(&self) -> &str {
        match self {
            Provided::Id(id) | Provided::Labelled { id, .. } => id,
        }
    }
}

#[derive(Debug, Serialize, PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
#[serde(rename_all = "lowercase")]
pub enum Policy {
    Required,
    Optional,
}

#[derive(Debug, Serialize, PartialEq, Clone)]
pub struct Need {
    pub record: String,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub or: Vec<String>,
    pub policy: Policy,
}

impl Lock {
    pub fn from_recordings(recordings: &[Value], generated_by: &str) -> Lock {
        let mut provides: BTreeMap<String, Vec<Provided>> = BTreeMap::new();
        let mut providers: Vec<String> = Vec::new();
        let mut needs: BTreeMap<(String, Vec<String>), Policy> = BTreeMap::new();
        let mut own: BTreeSet<String> = BTreeSet::new();
        let mut refs: Vec<(String, String, Policy)> = Vec::new();
        for rec in recordings {
            if let Some(addon) = rec["addon"].as_str() {
                own.insert(format!("{addon}/"));
            }
            let schemas = &rec["registries"];
            for r in rec["records"].as_array().into_iter().flatten() {
                let (Some(registry), Some(id)) = (r["registry"].as_str(), r["id"].as_str()) else {
                    continue;
                };
                let entry = match label(&r["value"]) {
                    Some(label) => Provided::Labelled {
                        id: id.to_owned(),
                        label,
                    },
                    None => Provided::Id(id.to_owned()),
                };
                let list = provides.entry(registry.to_owned()).or_default();
                match list.iter_mut().find(|p| p.id() == id) {
                    Some(p) => *p = entry,
                    None => list.push(entry),
                }
                let schema = &schemas[registry];
                let mut found = Vec::new();
                walk(schema, schema, &r["value"], &mut found, 0);
                refs.extend(found.into_iter().map(|(reg, raw)| {
                    let policy = if raw.contains('?') {
                        Policy::Optional
                    } else {
                        Policy::Required
                    };
                    (reg, raw, policy)
                }));
                if let Some(base) = r["base"].as_str() {
                    refs.push((registry.to_owned(), base.to_owned(), Policy::Required));
                }
            }
            for p in rec["providers"].as_array().into_iter().flatten() {
                if let Some(reg) = p["registry"].as_str()
                    && !providers.iter().any(|x| x == reg)
                {
                    providers.push(reg.to_owned());
                }
            }
            for l in rec["layers"].as_array().into_iter().flatten() {
                let Some(reg) = l["registry"].as_str() else {
                    continue;
                };
                if let Some(id) = l["id"].as_str() {
                    refs.push((reg.to_owned(), id.to_owned(), Policy::Optional));
                }
                let schema = &schemas[reg];
                let mut found = Vec::new();
                walk(schema, schema, &l["result"], &mut found, 0);
                walk(schema, schema, &l["patch"], &mut found, 0);
                refs.extend(found.into_iter().map(|(r, raw)| {
                    let policy = if raw.contains('?') {
                        Policy::Optional
                    } else {
                        Policy::Required
                    };
                    (r, raw, policy)
                }));
            }
        }
        for (registry, raw, policy) in refs {
            let alts: Vec<&str> = raw
                .split('|')
                .map(|a| a.trim_end_matches('?'))
                .filter(|a| !a.is_empty())
                .collect();
            if alts.is_empty()
                || alts
                    .iter()
                    .any(|a| a.starts_with("game/") || own.iter().any(|o| a.starts_with(o.as_str())))
            {
                continue;
            }
            let mut names = alts.iter().map(|a| format!("{registry}:{a}"));
            let first = names.next().expect("one alternative");
            let key = (first, names.collect::<Vec<_>>());
            let p = needs.entry(key).or_insert(policy);
            *p = (*p).min(policy);
        }
        Lock {
            generated_by: generated_by.to_owned(),
            provides,
            providers,
            needs: needs
                .into_iter()
                .map(|((record, or), policy)| Need { record, or, policy })
                .collect(),
            files: Vec::new(),
        }
    }

    pub fn to_json(&self) -> String {
        let mut s = serde_json::to_string_pretty(self).expect("the lock serialises");
        s.push('\n');
        s
    }
}

fn label(value: &Value) -> Option<String> {
    ["label", "title"].iter().find_map(|k| {
        let v = &value[*k];
        v.as_str()
            .or_else(|| v["literal"].as_str())
            .filter(|s| !s.is_empty())
            .map(str::to_owned)
    })
}

fn resolve<'a>(root: &'a Value, mut node: &'a Value) -> &'a Value {
    for _ in 0..16 {
        let Some(r) = node["$ref"].as_str() else {
            break;
        };
        let Some(path) = r.strip_prefix('#') else {
            break;
        };
        match root.pointer(path) {
            Some(n) => node = n,
            None => break,
        }
    }
    node
}

fn walk(root: &Value, node: &Value, value: &Value, out: &mut Vec<(String, String)>, depth: usize) {
    if depth > 64 || node.is_null() {
        return;
    }
    let node = resolve(root, node);
    if let (Some(reg), Some(s)) = (node["x-ref"].as_str(), value.as_str()) {
        out.push((reg.to_owned(), s.to_owned()));
    }
    for key in ["oneOf", "anyOf", "allOf"] {
        for alt in node[key].as_array().into_iter().flatten() {
            walk(root, alt, value, out, depth + 1);
        }
    }
    match value {
        Value::Object(obj) => {
            for (k, v) in obj {
                let sub = match &node["properties"][k] {
                    Value::Null => &node["additionalProperties"],
                    p => p,
                };
                if sub.is_object() {
                    walk(root, sub, v, out, depth + 1);
                }
            }
        }
        Value::Array(items) => {
            for (i, v) in items.iter().enumerate() {
                let sub = match &node["prefixItems"][i] {
                    Value::Null => &node["items"],
                    p => p,
                };
                if sub.is_object() {
                    walk(root, sub, v, out, depth + 1);
                }
            }
        }
        _ => {}
    }
    out.dedup();
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn progressive() -> Value {
        let seasons: Vec<Value> = (2016..=2025)
            .map(|y| {
                json!({ "op": "add", "registry": "career.season", "key": y.to_string(),
                        "id": format!("swofty.progressive/{y}"),
                        "value": { "year": y, "data_group": "f1_career_progressive",
                                   "box": if y == 2016 { json!("venue/sochi?") } else { json!(null) },
                                   "car": if y == 2016 { json!("swofty.classic-seasons-2016/ferrari|mod.f1-2016/ferrari") } else { json!(null) } } })
            })
            .collect();
        let mut records = vec![json!({
            "op": "variant", "registry": "career.mode", "key": "progressive",
            "id": "swofty.progressive/progressive", "base": "game/f1_career_2026",
            "value": { "data_group": "f1_career_progressive", "save_container": "driver_career_progressive",
                       "title": "lng_progressive_title" }
        })];
        records.extend(seasons);
        json!({
            "recorder": 1,
            "addon": "swofty.progressive",
            "registries": {
                "career.mode": { "x-registry": "career.mode", "properties": { "title": { "type": "string" } } },
                "career.season": {
                    "x-registry": "career.season",
                    "properties": { "box": { "$ref": "#/$defs/track" }, "year": { "type": "integer" },
                                    "car": { "type": ["string", "null"], "x-ref": "vehicle" } },
                    "$defs": { "track": { "type": ["string", "null"], "x-ref": "track" } }
                }
            },
            "records": records,
            "providers": [ { "registry": "career.driver_ratings", "list": true } ],
            "layers": [],
            "status": "ok"
        })
    }

    #[test]
    fn writes_the_manifest_pages_example() {
        let lock = Lock::from_recordings(&[progressive()], "swofty dev pack 1.6.0");
        let doc: Value = serde_json::from_str(&lock.to_json()).unwrap();
        let seasons: Vec<Value> = (2016..=2025)
            .map(|y| json!(format!("swofty.progressive/{y}")))
            .collect();
        let expected = json!({
          "generated_by": "swofty dev pack 1.6.0",
          "provides": {
            "career.mode": [ { "id": "swofty.progressive/progressive", "label": "lng_progressive_title" } ],
            "career.season": seasons
          },
          "providers": [ "career.driver_ratings" ],
          "needs": [
            { "record": "track:venue/sochi", "policy": "optional" },
            { "record": "vehicle:swofty.classic-seasons-2016/ferrari", "or": [ "vehicle:mod.f1-2016/ferrari" ], "policy": "required" }
          ]
        });
        assert_eq!(doc, expected);
        let text = lock.to_json();
        let keys: Vec<&str> = text
            .lines()
            .filter(|l| l.starts_with("  \""))
            .map(|l| l.trim().split('"').nth(1).unwrap())
            .collect();
        assert_eq!(keys, ["generated_by", "provides", "providers", "needs"]);
    }

    #[test]
    fn needs_skip_stock_and_own_records_and_keep_the_strictest_policy() {
        let rec = json!({
            "addon": "mod.esp",
            "registries": { "vehicle": { "properties": {
                "donor": { "type": ["string", "null"], "x-ref": "vehicle" },
                "engine_sound": { "type": ["string", "null"], "x-ref": "engine_sound" },
                "parts": { "type": "array", "items": { "type": "object", "properties": {
                    "livery": { "oneOf": [ { "type": "string", "x-ref": "livery" }, { "type": "null" } ] } } } }
            } } },
            "records": [
                { "registry": "vehicle", "id": "mod.esp/a", "value": {
                    "donor": "game/ferrari_2025", "engine_sound": "mod.esp/v10", "label": "",
                    "parts": [ { "livery": "mod.l/red?" }, { "livery": "mod.l/red" } ] } },
                { "registry": "vehicle", "id": "mod.esp/b", "value": { "donor": "mod.x/a|mod.y/b?" } },
                { "registry": "vehicle", "id": "mod.esp/c", "value": { "donor": "mod.x/a|game/b" } }
            ],
            "layers": [ { "registry": "vehicle", "id": "swofty.classic-seasons-2017/ferrari", "scope": "global" },
                        { "registry": "vehicle", "select": { "prefix": "game/" },
                          "result": { "engine_sound": "mod.v12/sound", "donor": null } },
                        { "registry": "vehicle", "id": "game/x", "patch": { "engine_sound": "mod.v8/sound?" } } ]
        });
        let lock = Lock::from_recordings(&[rec], "x");
        assert_eq!(
            lock.provides["vehicle"],
            [
                Provided::Id("mod.esp/a".into()),
                Provided::Id("mod.esp/b".into()),
                Provided::Id("mod.esp/c".into())
            ]
        );
        assert!(lock.providers.is_empty());
        assert_eq!(
            serde_json::to_value(&lock.needs).unwrap(),
            json!([
                { "record": "engine_sound:mod.v12/sound", "policy": "required" },
                { "record": "engine_sound:mod.v8/sound", "policy": "optional" },
                { "record": "livery:mod.l/red", "policy": "required" },
                { "record": "vehicle:mod.x/a", "or": ["vehicle:mod.y/b"], "policy": "optional" },
                { "record": "vehicle:swofty.classic-seasons-2017/ferrari", "policy": "optional" }
            ])
        );
        assert!(!lock.to_json().contains("providers"));
    }

    #[test]
    fn reads_the_recorder_tests_recording() {
        let rec: Value = serde_json::from_str(include_str!(
            "../../../../packages/sdk/recorder/test/expected/t_register.json"
        ))
        .unwrap();
        let lock = Lock::from_recordings(&[rec], "swofty dev pack 1.6.0");
        assert_eq!(
            serde_json::to_value(&lock).unwrap(),
            json!({
                "generated_by": "swofty dev pack 1.6.0",
                "provides": {
                    "career.mode": [ { "id": "t.recorder/v10_career", "label": "lng_v10_title" } ],
                    "engine_sound": [ "t.recorder/v10" ],
                    "vehicle": [ { "id": "t.recorder/ferrari_v10", "label": "ferrari_v10" } ]
                },
                "needs": [
                    { "record": "vehicle:swofty.classic-seasons-2017/ferrari", "policy": "optional" }
                ]
            })
        );
    }
}
