//! The reports the core writes in `$DATA/registry/` (the launcher's data folder, which it names in
//! `<game>/swofty/state.json`; docs/std/launcher.md):
//! `conflicts.json` and `effective.json` at each session load, `catalog.json`
//! after each session, and `room.json` after each boot. The launcher shows
//! the last ones, marked "as of your last session", and nothing depends on
//! them. Every field is optional and an unknown one is ignored.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde::Deserialize;
use serde_json::Value;

pub const DIR: &str = "registry";
/// Where the core wrote them before 1.6.0 gave it the data folder, and where it still writes them
/// when it can't use the data folder.
pub const LEGACY_DIR: &str = "swofty/registry";
pub const CONFLICTS: &str = "conflicts.json";
pub const EFFECTIVE: &str = "effective.json";
pub const CATALOG: &str = "catalog.json";
pub const ROOM: &str = "room.json";
/// The core's stat-id store (docs/std/dependencies.md "Tracks have stable identities").
pub const IDS: &str = "ids.json";
/// The reports Send Logs adds, by their name in `$DATA/registry/`.
pub const SENT: [&str; 4] = [EFFECTIVE, CONFLICTS, CATALOG, ROOM];

pub fn dir(data: &Path) -> PathBuf {
    data.join(DIR)
}

pub fn path(data: &Path, name: &str) -> PathBuf {
    dir(data).join(name)
}

pub fn legacy_path(game: &Path, name: &str) -> PathBuf {
    game.join(LEGACY_DIR).join(name)
}

/// The report `name`: the data folder's, else the game folder's, when the core couldn't use the data
/// folder or hasn't written there since the move.
pub fn found(data: &Path, game: Option<&Path>, name: &str) -> Option<PathBuf> {
    let now = path(data, name);
    if now.is_file() {
        return Some(now);
    }
    let old = legacy_path(game?, name);
    old.is_file().then_some(old)
}

fn read_report(data: &Path, game: Option<&Path>, name: &str) -> Option<String> {
    std::fs::read_to_string(found(data, game, name)?).ok()
}

/// One entry of `conflicts.json`'s `registry` list.
#[derive(Debug, Clone, PartialEq)]
pub enum Conflict {
    Field {
        registry: String,
        id: String,
        path: String,
        addons: Vec<String>,
        wins: String,
    },
    InvalidLayer {
        registry: String,
        id: String,
        addon: String,
        error: String,
    },
    /// Two addons redirect the same game path (a record's `Path`); the later in load order wins.
    File {
        registry: String,
        id: String,
        path: String,
        addons: Vec<String>,
        wins: String,
    },
    HookUnavailable {
        point: String,
        holder: String,
        subscribers: Vec<String>,
    },
    HandlerFaulted {
        point: String,
        phase: String,
        addon: String,
    },
    Other {
        kind: String,
        addon: Option<String>,
        raw: Value,
    },
}

fn s(v: &Value, key: &str) -> String {
    v.get(key).and_then(Value::as_str).unwrap_or_default().to_owned()
}

fn list(v: &Value, key: &str) -> Vec<String> {
    v.get(key)
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
        .filter_map(Value::as_str)
        .map(str::to_owned)
        .collect()
}

impl Conflict {
    pub fn of(v: &Value) -> Self {
        match v.get("kind").and_then(Value::as_str).unwrap_or_default() {
            "field" => Self::Field {
                registry: s(v, "registry"),
                id: s(v, "id"),
                path: s(v, "path"),
                addons: list(v, "addons"),
                wins: s(v, "wins"),
            },
            "invalid_layer" => Self::InvalidLayer {
                registry: s(v, "registry"),
                id: s(v, "id"),
                addon: s(v, "addon"),
                error: s(v, "error"),
            },
            "file" => Self::File {
                registry: s(v, "registry"),
                id: s(v, "id"),
                path: s(v, "path"),
                addons: list(v, "addons"),
                wins: s(v, "wins"),
            },
            "hook_unavailable" => Self::HookUnavailable {
                point: s(v, "point"),
                holder: s(v, "holder"),
                subscribers: list(v, "subscribers"),
            },
            "handler_faulted" => Self::HandlerFaulted {
                point: s(v, "point"),
                phase: s(v, "phase"),
                addon: s(v, "addon"),
            },
            kind => Self::Other {
                kind: kind.to_owned(),
                addon: v.get("addon").and_then(Value::as_str).map(str::to_owned),
                raw: v.clone(),
            },
        }
    }

    /// The addons the entry is about.
    pub fn addons(&self) -> Vec<&str> {
        match self {
            Self::Field { addons, .. } | Self::File { addons, .. } => {
                addons.iter().map(String::as_str).collect()
            }
            Self::InvalidLayer { addon, .. } | Self::HandlerFaulted { addon, .. } => vec![addon.as_str()],
            Self::HookUnavailable {
                holder, subscribers, ..
            } => std::iter::once(holder.as_str())
                .chain(subscribers.iter().map(String::as_str))
                .collect(),
            Self::Other { addon, .. } => addon.iter().map(String::as_str).collect(),
        }
    }

    /// One line for the conflicts view, with addon names from `name`.
    pub fn line(&self, name: &dyn Fn(&str) -> String) -> String {
        match self {
            Self::Field {
                registry,
                id,
                path,
                addons,
                wins,
            } => format!(
                "{registry} {id} {path}: set by {}; {} wins (reorder addons to change it)",
                addons.iter().map(|a| name(a)).collect::<Vec<_>>().join(", "),
                name(wins)
            ),
            Self::InvalidLayer {
                registry,
                id,
                addon,
                error,
            } => format!("{registry} {id}: {}'s layer dropped: {error}", name(addon)),
            Self::File {
                registry,
                id,
                path,
                addons,
                wins,
            } => format!(
                "{path}: redirected by {}; {}'s file wins ({registry} {id}; reorder addons to change it)",
                addons.iter().map(|a| name(a)).collect::<Vec<_>>().join(", "),
                name(wins)
            ),
            Self::HookUnavailable {
                point,
                holder,
                subscribers,
            } => format!(
                "hook {point} unavailable: {holder} overwrites it, so {} can't use it",
                subscribers.iter().map(|a| name(a)).collect::<Vec<_>>().join(", ")
            ),
            Self::HandlerFaulted { point, phase, addon } => {
                format!(
                    "{point} ({phase}): {}'s handler faulted and was detached for the session",
                    name(addon)
                )
            }
            Self::Other { kind, raw, .. } => format!("{kind}: {raw}"),
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Conflicts {
    pub registry: Vec<Conflict>,
    /// The core's own status lines by addon, when it writes them (`"status": { "<id>": ["..."] }`).
    pub status: BTreeMap<String, Vec<String>>,
}

impl Conflicts {
    pub fn parse(text: &str) -> Option<Self> {
        let v: Value = serde_json::from_str(text).ok()?;
        let registry = v
            .get("registry")
            .and_then(Value::as_array)
            .into_iter()
            .flatten()
            .map(Conflict::of)
            .collect();
        let status = v
            .get("status")
            .and_then(Value::as_object)
            .into_iter()
            .flatten()
            .map(|(id, lines)| {
                let lines = match lines {
                    Value::String(one) => vec![one.clone()],
                    other => other
                        .as_array()
                        .into_iter()
                        .flatten()
                        .filter_map(Value::as_str)
                        .map(str::to_owned)
                        .collect(),
                };
                (id.clone(), lines)
            })
            .collect();
        Some(Self { registry, status })
    }

    pub fn read(data: &Path, game: Option<&Path>) -> Option<Self> {
        Self::parse(&read_report(data, game, CONFLICTS)?)
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Deserialize)]
#[serde(default)]
pub struct Counted {
    pub asked: u64,
    pub new_keys: u64,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Deserialize)]
#[serde(default)]
pub struct AddonRoom {
    pub asked: u64,
    pub applied: u64,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Deserialize)]
#[serde(default)]
pub struct RegistryRoom {
    pub asked_keys: u64,
    pub new_keys: u64,
    pub refused_keys: u64,
    pub addons: BTreeMap<String, AddonRoom>,
}

/// `room.json`, from W7-SIZING.md 5.3.
#[derive(Debug, Clone, Default, PartialEq, Eq, Deserialize)]
#[serde(default)]
pub struct Room {
    pub version: u32,
    pub pe_timestamp: u64,
    pub presize: String,
    pub reason: Option<String>,
    pub profile: Option<String>,
    pub slots: u64,
    pub grows_at: u64,
    pub file_records: u64,
    pub reserved_for: u64,
    pub older_edits: Counted,
    pub registry: RegistryRoom,
    pub probes: u64,
    pub count: u64,
    pub margin: u64,
    pub room: i64,
    pub info_inc: u32,
}

impl Room {
    pub fn parse(text: &str) -> Option<Self> {
        serde_json::from_str(text).ok()
    }

    pub fn read(data: &Path, game: Option<&Path>) -> Option<Self> {
        Self::parse(&read_report(data, game, ROOM)?)
    }

    pub fn presize_unavailable(&self) -> bool {
        self.presize == "unavailable"
    }

    /// Whether this report is about a boot of `profile` with the addons `order`: its `profile` when
    /// it names one, else every addon it counts registry records for is on.
    pub fn is_for(&self, profile: &str, order: &[String]) -> bool {
        match &self.profile {
            Some(p) => p == profile,
            None => self.registry.addons.keys().all(|id| order.contains(id)),
        }
    }
}

/// The status line of each addon from the last session's reports: what didn't apply.
pub fn status_lines(conflicts: Option<&Conflicts>, room: Option<&Room>) -> BTreeMap<String, Vec<String>> {
    let mut out: BTreeMap<String, Vec<String>> = BTreeMap::new();
    if let Some(c) = conflicts {
        let mut dropped: BTreeMap<&str, Vec<String>> = BTreeMap::new();
        for e in &c.registry {
            match e {
                Conflict::InvalidLayer {
                    registry,
                    id,
                    addon,
                    error,
                } => dropped
                    .entry(addon)
                    .or_default()
                    .push(format!("{registry} {id}: {error}")),
                Conflict::HookUnavailable {
                    point,
                    holder,
                    subscribers,
                } => {
                    for s in subscribers {
                        out.entry(s.clone())
                            .or_default()
                            .push(format!("hook {point} unavailable ({holder} overwrites it)"));
                    }
                }
                Conflict::HandlerFaulted { point, phase, addon } => out
                    .entry(addon.clone())
                    .or_default()
                    .push(format!("its {point} handler ({phase}) faulted and was detached")),
                Conflict::Other {
                    kind,
                    addon: Some(addon),
                    raw,
                } => {
                    let why = raw
                        .get("error")
                        .or_else(|| raw.get("reason"))
                        .and_then(Value::as_str)
                        .map(|w| format!(": {w}"))
                        .unwrap_or_default();
                    out.entry(addon.clone())
                        .or_default()
                        .push(format!("{}{why}", kind.replace('_', " ")))
                }
                _ => {}
            }
        }
        for (addon, layers) in dropped {
            let more = match layers.len() {
                1 => String::new(),
                n => format!(", and {} more", n - 1),
            };
            let noun = if layers.len() == 1 { "layer" } else { "layers" };
            out.entry(addon.to_owned()).or_default().push(format!(
                "{} {noun} dropped ({}{more})",
                layers.len(),
                layers[0]
            ));
        }
        for (addon, lines) in &c.status {
            out.entry(addon.clone())
                .or_default()
                .extend(lines.iter().cloned());
        }
    }
    if let Some(r) = room {
        for (addon, a) in &r.registry.addons {
            if a.applied < a.asked {
                out.entry(addon.clone()).or_default().push(format!(
                    "{} of {} database record(s) refused, past the database room",
                    a.asked - a.applied,
                    a.asked
                ));
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const CONFLICTS_DOC: &str = r#"{
      "registry": [
        { "kind": "field", "registry": "career.season", "id": "swofty.progressive/2017",
          "path": "/teams/slot=1/seats", "addons": ["tails.whatif-history", "someone.realism"], "wins": "someone.realism" },
        { "kind": "invalid_layer", "registry": "career.season", "id": "swofty.progressive/2018",
          "addon": "tails.whatif-history", "error": "/teams/slot=4/seats/1 \"game/kimi_raikkonen\" already holds a seat" },
        { "kind": "hook_unavailable", "point": "career.calendar.add_activity", "holder": "old_addon",
          "subscribers": ["swofty.progressive"] },
        { "kind": "handler_faulted", "point": "session.lap", "phase": "after", "addon": "someone.lapticker" },
        { "kind": "invalid_data", "addon": "mod.esp", "error": "registry/sounds.json: not an object" },
        { "kind": "file", "registry": "engine_sound", "id": "mod.late/v10", "path": "audio/2025/bundles/v10.bdl",
          "addons": ["mod.esp", "mod.late"], "wins": "mod.late" },
        { "kind": "something_new", "x": 1 }
      ],
      "later": true
    }"#;

    #[test]
    fn reads_every_kind_of_conflict_the_docs_show() {
        let c = Conflicts::parse(CONFLICTS_DOC).unwrap();
        assert_eq!(c.registry.len(), 7);
        assert!(matches!(&c.registry[0], Conflict::Field { wins, .. } if wins == "someone.realism"));
        assert!(matches!(&c.registry[3], Conflict::HandlerFaulted { phase, .. } if phase == "after"));
        assert!(
            matches!(&c.registry[6], Conflict::Other { kind, addon: None, .. } if kind == "something_new")
        );
        let name = |id: &str| match id {
            "someone.realism" => "Realism".to_owned(),
            other => other.to_owned(),
        };
        assert_eq!(
            c.registry[0].line(&name),
            "career.season swofty.progressive/2017 /teams/slot=1/seats: set by tails.whatif-history, Realism; \
             Realism wins (reorder addons to change it)"
        );
        assert_eq!(c.registry[5].addons(), vec!["mod.esp", "mod.late"]);
        assert_eq!(
            c.registry[5].line(&name),
            "audio/2025/bundles/v10.bdl: redirected by mod.esp, mod.late; mod.late's file wins \
             (engine_sound mod.late/v10; reorder addons to change it)"
        );
        assert_eq!(
            c.registry[2].line(&name),
            "hook career.calendar.add_activity unavailable: old_addon overwrites it, so swofty.progressive can't use it"
        );
    }

    #[test]
    fn status_lines_say_what_didnt_apply_per_addon() {
        let c = Conflicts::parse(CONFLICTS_DOC).unwrap();
        let room = Room::parse(
            r#"{ "presize": "applied", "registry": { "addons": {
                 "someone.megapack": { "asked": 4200, "applied": 3020 }, "fine.one": { "asked": 3, "applied": 3 } } } }"#,
        )
        .unwrap();
        let lines = status_lines(Some(&c), Some(&room));
        assert_eq!(
            lines["tails.whatif-history"],
            [
                "1 layer dropped (career.season swofty.progressive/2018: /teams/slot=4/seats/1 \"game/kimi_raikkonen\" already holds a seat)"
            ]
        );
        assert_eq!(
            lines["swofty.progressive"],
            ["hook career.calendar.add_activity unavailable (old_addon overwrites it)"]
        );
        assert_eq!(
            lines["someone.lapticker"],
            ["its session.lap handler (after) faulted and was detached"]
        );
        assert_eq!(
            lines["mod.esp"],
            ["invalid data: registry/sounds.json: not an object"]
        );
        assert_eq!(
            lines["someone.megapack"],
            ["1180 of 4200 database record(s) refused, past the database room"]
        );
        assert!(!lines.contains_key("fine.one"));
        assert!(!lines.contains_key("someone.realism"));
    }

    #[test]
    fn several_dropped_layers_make_one_line_and_core_lines_are_kept() {
        let c = Conflicts::parse(
            r#"{ "registry": [
                 { "kind": "invalid_layer", "registry": "vehicle", "id": "a/1", "addon": "x.y", "error": "e1" },
                 { "kind": "invalid_layer", "registry": "vehicle", "id": "a/2", "addon": "x.y", "error": "e2" } ],
                 "status": { "x.y": ["round 12 left out (no track provides Hockenheim)"], "z.w": "one line" } }"#,
        )
        .unwrap();
        let lines = status_lines(Some(&c), None);
        assert_eq!(
            lines["x.y"],
            [
                "2 layers dropped (vehicle a/1: e1, and 1 more)",
                "round 12 left out (no track provides Hockenheim)"
            ]
        );
        assert_eq!(lines["z.w"], ["one line"]);
    }

    #[test]
    fn reads_the_room_report_of_w7() {
        let r = Room::parse(
            r#"{
              "version": 1, "pe_timestamp": 1789118616, "presize": "applied", "slots": 2097152, "grows_at": 1677721,
              "file_records": 742857, "reserved_for": 935081,
              "older_edits": { "asked": 84112, "new_keys": 80120 },
              "registry": { "asked_keys": 103984, "new_keys": 103984, "refused_keys": 0,
                            "addons": { "swofty.f1-1990": { "asked": 15020, "applied": 15020 } } },
              "probes": 7, "count": 926968, "margin": 4096, "room": 746657, "info_inc": 32
            }"#,
        )
        .unwrap();
        assert_eq!(r.older_edits.new_keys, 80120);
        assert_eq!(r.registry.asked_keys, 103984);
        assert_eq!(r.room, 746657);
        assert!(!r.presize_unavailable());
        assert!(r.is_for("any", &["swofty.f1-1990".into()]));
        assert!(!r.is_for("any", &["other".into()]));
        let u =
            Room::parse(r#"{ "presize": "unavailable", "reason": "signature", "profile": "p1" }"#).unwrap();
        assert!(u.presize_unavailable());
        assert!(u.is_for("p1", &[]));
        assert!(!u.is_for("p2", &[]));
        assert!(Room::parse("nope").is_none());
    }
}
