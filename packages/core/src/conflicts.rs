//! Which addon overrides which in a profile's build: kept from the last
//! build and checked on request without installing anything.

use std::collections::BTreeMap;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

use crate::Result;
use crate::install::Game;
use crate::planner::{Conflict, Target};
use crate::store::Store;

pub const FILE: &str = "conflicts.json";
/// Items kept per pair; the counts stay exact.
pub const MAX_ITEMS: usize = 1000;
const TEXT: &str = "text ";

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Report {
    pub profile: String,
    /// Seconds since the Unix epoch.
    pub built_at: f64,
    pub pairs: Vec<Pair>,
    /// The profile's enabled addons in load order when it was built.
    #[serde(default)]
    pub order: Vec<String>,
    /// Per addon id, the removals of stock database records the build skipped.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub stock_removals: BTreeMap<String, usize>,
    /// Per addon id, the game's entrant lists it renamed or removed that the build kept.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub kept_lists: BTreeMap<String, Vec<String>>,
}

impl Report {
    /// Whether the profile still turns on the same addons in the same order.
    pub fn is_current(&self, profile: &crate::profile::Profile) -> bool {
        self.order.iter().map(String::as_str).eq(profile.enabled())
    }
}

/// `winner` loads after `loser` and replaces what both write.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Pair {
    pub winner: String,
    pub loser: String,
    pub database: usize,
    pub resources: usize,
    pub files: usize,
    #[serde(default)]
    pub strings: usize,
    /// Readable names of the overridden items, such as `tracks.Track_3.laps`.
    pub items: Vec<String>,
}

/// What one addon writes in a build.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Changes {
    pub database: usize,
    pub resources: usize,
    pub files: usize,
    pub strings: usize,
    pub tracks: usize,
}

impl Changes {
    pub(crate) fn count(&mut self, target: &Target) {
        match target {
            Target::Record(_) => self.database += 1,
            Target::Resource { .. } | Target::Slice { .. } => self.resources += 1,
            Target::File(_) => self.files += 1,
            Target::String { .. } => self.strings += 1,
            Target::Group(_) => {}
        }
    }

    fn parts(&self) -> Vec<String> {
        [
            (self.database, "database value"),
            (self.resources, "archive resource"),
            (self.files, "file"),
            (self.strings, "text string"),
            (self.tracks, "track"),
        ]
        .into_iter()
        .filter(|(n, _)| *n > 0)
        .map(|(n, what)| format!("{n} {what}{}", if n == 1 { "" } else { "s" }))
        .collect()
    }

    pub fn is_empty(&self) -> bool {
        *self == Self::default()
    }
}

/// `412 database values, 3 archive resources, 2 files`.
impl std::fmt::Display for Changes {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.parts().join(", "))
    }
}

fn and_list(parts: &[String]) -> String {
    match parts {
        [] => String::new(),
        [one] => one.clone(),
        [rest @ .., last] => format!("{} and {last}", rest.join(", ")),
    }
}

impl Pair {
    /// `<winner> overrides <loser> on 12 database values and 1 file`, naming each addon with `name`.
    pub fn line(&self, name: &dyn Fn(&str) -> String) -> String {
        let changes = Changes {
            database: self.database,
            resources: self.resources,
            files: self.files,
            strings: self.strings,
            tracks: 0,
        };
        format!(
            "{} overrides {} on {}",
            name(&self.winner),
            name(&self.loser),
            and_list(&changes.parts())
        )
    }
}

fn item(target: &Target, record: &dyn Fn(u64) -> Option<String>) -> String {
    match target {
        Target::Record(key) => record(*key).unwrap_or_else(|| format!("{key:#018x}")),
        Target::Resource { erp, uri } => format!("{erp}:{uri}"),
        Target::Slice { erp, uri, slice } => format!("{erp}:{uri} slice {slice}"),
        Target::File(rel) => rel.clone(),
        Target::String { language, key } if language == "*" => format!("{TEXT}{key}"),
        Target::String { language, key } => format!("{TEXT}{key} ({language})"),
        Target::Group(group) => format!("asset group {group}"),
    }
}

/// The addon pairs of a build's conflicts, naming database records with
/// `record`, and lines for the conflicts between items of one addon.
pub(crate) fn summarise(
    profile: &str,
    conflicts: &[Conflict],
    record: &dyn Fn(u64) -> Option<String>,
) -> (Report, Vec<String>) {
    let mut pairs: BTreeMap<(&str, &str), Pair> = BTreeMap::new();
    let mut within = Vec::new();
    for c in conflicts {
        let mut inner = Vec::new();
        let mut name = None;
        for loser in &c.overridden {
            if loser.addon == c.winner.addon {
                inner.push(loser.to_string());
                continue;
            }
            let pair = pairs
                .entry((&c.winner.addon, &loser.addon))
                .or_insert_with(|| Pair {
                    winner: c.winner.addon.clone(),
                    loser: loser.addon.clone(),
                    ..Pair::default()
                });
            match c.target {
                Target::Record(_) => pair.database += 1,
                Target::Resource { .. } | Target::Slice { .. } => pair.resources += 1,
                Target::File(_) => pair.files += 1,
                Target::String { .. } => pair.strings += 1,
                Target::Group(_) => {}
            }
            if pair.items.len() < MAX_ITEMS {
                pair.items
                    .push(name.get_or_insert_with(|| item(&c.target, record)).clone());
            }
        }
        if !inner.is_empty() {
            within.push(format!(
                "conflict: {}: {} overrides {}",
                c.target,
                c.winner,
                inner.join(", ")
            ));
        }
    }
    let report = Report {
        profile: profile.to_owned(),
        built_at: now(),
        pairs: pairs.into_values().collect(),
        order: Vec::new(),
        stock_removals: BTreeMap::new(),
        kept_lists: BTreeMap::new(),
    };
    (report, within)
}

/// A record's label selector path, else its dotted path in the catalogue.
pub(crate) fn record_name(cat: &crate::dbedit::Catalog, key: u64) -> Option<String> {
    if let Some((select, field)) = cat.selector(key) {
        return Some(format!("{select}.{field}"));
    }
    Some(cat.path(cat.node_of_key(key)?)).filter(|p| !p.is_empty())
}

pub(crate) fn now() -> f64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0.0, |d| d.as_secs_f64())
}

pub fn path(store: &Store, profile: &str) -> PathBuf {
    store.overlays_dir(profile).join(FILE)
}

/// The conflicts of the profile's last build.
pub fn last(store: &Store, profile: &str) -> Option<Report> {
    crate::read_json(&path(store, profile)).ok()
}

/// Plans the profile's build without installing it and returns its conflicts.
pub fn check(store: &Store, profile: &str, game: &Game) -> Result<Report> {
    Ok(crate::build::build_as(store, profile, game, true)?.report)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::asset::Origin;

    fn origin(addon: &str, item: Option<&str>) -> Origin {
        Origin {
            addon: addon.into(),
            item: item.map(Into::into),
        }
    }

    fn conflict(target: Target, winner: Origin, overridden: Vec<Origin>) -> Conflict {
        Conflict {
            target,
            winner,
            overridden,
        }
    }

    #[test]
    fn pairs_count_each_kind_and_name_their_items() {
        let (a, b, c) = (origin("a", None), origin("b", None), origin("c", None));
        let conflicts = [
            conflict(Target::Record(7), b.clone(), vec![a.clone()]),
            conflict(Target::Record(8), b.clone(), vec![a.clone()]),
            conflict(
                Target::Resource {
                    erp: "x.erp".into(),
                    uri: "eaid://r".into(),
                },
                c,
                vec![a.clone(), b.clone()],
            ),
            conflict(Target::File("f.dds".into()), b.clone(), vec![a.clone()]),
            conflict(
                Target::String {
                    language: "*".into(),
                    key: "lng_x".into(),
                },
                b,
                vec![a.clone()],
            ),
            conflict(
                Target::File("g.dds".into()),
                origin("m", Some("two")),
                vec![origin("m", Some("one")), a],
            ),
        ];
        let named = |key: u64| (key == 7).then(|| "rules.laps".to_owned());
        let (report, within) = summarise("default", &conflicts, &named);
        assert_eq!(report.profile, "default");
        assert!(report.built_at > 0.0);
        let shown: Vec<(&str, &str, usize, usize, usize)> = report
            .pairs
            .iter()
            .map(|p| {
                (
                    p.winner.as_str(),
                    p.loser.as_str(),
                    p.database,
                    p.resources,
                    p.files,
                )
            })
            .collect();
        assert_eq!(
            shown,
            [
                ("b", "a", 2, 0, 1),
                ("c", "a", 0, 1, 0),
                ("c", "b", 0, 1, 0),
                ("m", "a", 0, 0, 1)
            ]
        );
        assert_eq!(
            report.pairs[0].items,
            ["rules.laps", "0x0000000000000008", "f.dds", "text lng_x"]
        );
        assert_eq!(report.pairs[1].items, ["x.erp:eaid://r"]);
        assert_eq!(within, ["conflict: g.dds: m (two) overrides m (one)"]);
        let name = |id: &str| id.to_uppercase();
        assert_eq!(
            report.pairs[0].line(&name),
            "B overrides A on 2 database values, 1 file and 1 text string"
        );
        assert_eq!(report.pairs[1].line(&name), "C overrides A on 1 archive resource");
        let c = Changes {
            database: 412,
            resources: 3,
            files: 2,
            strings: 5,
            tracks: 1,
        };
        assert_eq!(
            c.to_string(),
            "412 database values, 3 archive resources, 2 files, 5 text strings, 1 track"
        );
        assert!(Changes::default().is_empty());
    }

    #[test]
    fn a_report_goes_stale_when_the_load_order_or_switches_change() {
        let old: Report = serde_json::from_str(r#"{"profile":"p","built_at":1.0,"pairs":[]}"#).unwrap();
        let mut p = crate::profile::Profile::new("p");
        for id in ["t.a", "t.b"] {
            p.set_enabled(id, true).unwrap();
        }
        assert!(!old.is_current(&p));
        let report = Report {
            order: p.enabled().map(str::to_owned).collect(),
            ..old
        };
        assert!(report.is_current(&p));
        p.move_addon("t.b", -1).unwrap();
        assert!(!report.is_current(&p));
        p.move_addon("t.b", 1).unwrap();
        assert!(report.is_current(&p));
        p.set_enabled("t.a", false).unwrap();
        assert!(!report.is_current(&p));
    }

    #[test]
    fn keeps_at_most_max_items_per_pair() {
        let conflicts: Vec<Conflict> = (0..MAX_ITEMS as u64 + 5)
            .map(|k| conflict(Target::Record(k), origin("b", None), vec![origin("a", None)]))
            .collect();
        let (report, _) = summarise("p", &conflicts, &|_| None);
        assert_eq!(report.pairs[0].database, MAX_ITEMS + 5);
        assert_eq!(report.pairs[0].items.len(), MAX_ITEMS);
    }
}
