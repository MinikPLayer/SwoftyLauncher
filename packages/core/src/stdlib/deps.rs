//! The dependency check before Play and at install, from each addon's
//! `registry.json` and `registry.lock.json` (docs/std/dependencies.md):
//!
//! - an unmet `requires` (an addon, a registry or a record) and a `breaks` on
//!   an addon that is on stop Play, with a message naming the fix;
//! - a `conflicts` entry, and a required reference in a lock file that
//!   nothing provides, warn;
//! - an unmet `recommends`, and an optional reference in a lock file, are
//!   hints.
//!
//! `service` and `hook` entries are checked by the core in game. An addon
//! without a `registry.json` or a lock file adds nothing here, so a profile
//! of addons made for 1.5 checks exactly as before.

use std::collections::BTreeMap;
use std::path::Path;

use semver::Version;

use super::data;
use super::lock::Lock;
use super::manifest::{Clash, Need, RegistryManifest, Target};
use super::range::version;
use crate::manifest::AddonManifest;

/// The CLI's exit code when the dependency check stops Play.
pub const STOP_EXIT: u8 = 3;

/// The registries this launcher's core has, with their current `major.minor`
/// (`packages/sdk/std/registries/<name>/<major>.<minor>.json`).
pub const CORE_REGISTRIES: &[(&str, &str)] = &[
    ("addon.option", "1.0"),
    ("career.calendar", "1.0"),
    ("career.character", "1.0"),
    ("career.content", "1.0"),
    ("career.driver_ratings", "1.0"),
    ("career.mode", "1.0"),
    ("career.season", "1.0"),
    ("career.wizard", "1.0"),
    ("content.package", "1.0"),
    ("driver.head", "1.0"),
    ("driver.identity", "1.0"),
    ("driver.look", "1.0"),
    ("driver.person", "1.0"),
    ("driver.stats", "1.0"),
    ("engine_sound", "1.0"),
    ("era.box", "1.0"),
    ("gloves", "1.0"),
    ("helmet", "1.0"),
    ("hub.tab", "1.0"),
    ("lang.string", "1.0"),
    ("livery", "1.0"),
    ("suit", "1.0"),
    ("team.constructor", "1.0"),
    ("team.identity", "1.0"),
    ("team.logo", "1.0"),
    ("team.pit_times", "1.0"),
    ("vehicle", "1.0"),
    ("vehicle.handling", "1.0"),
    ("vehicle.package", "1.0"),
    ("wearable.season_set", "1.0"),
];

/// What the launcher knows about one addon's 1.6 files.
#[derive(Debug, Clone)]
pub struct Std {
    pub id: String,
    pub name: String,
    pub version: Version,
    pub registry: Option<RegistryManifest>,
    pub lock: Option<Lock>,
    pub data_files: Vec<String>,
    pub records: Vec<data::Record>,
    /// The files its data files and lock name through `Path` fields, for the build to place.
    pub files: super::paths::Named,
    /// Why its `registry.json` or lock file couldn't be read.
    pub unreadable: Vec<String>,
}

impl Std {
    pub fn read(dir: &Path, m: &AddonManifest) -> Self {
        let mut unreadable = Vec::new();
        let registry = RegistryManifest::load(dir).unwrap_or_else(|e| {
            unreadable.push(e);
            None
        });
        let lock = Lock::load(dir).unwrap_or_else(|e| {
            unreadable.push(e);
            None
        });
        let data_files = data::files(dir);
        let mut records: Vec<data::Record> = data_files
            .iter()
            .filter_map(|rel| std::fs::read_to_string(dir.join(rel)).ok())
            .flat_map(|text| data::records(&m.id, &text))
            .collect();
        records.sort_by(|a, b| (&a.registry, &a.id).cmp(&(&b.registry, &b.id)));
        let lock_files = lock.as_ref().map(|l| l.files.as_slice()).unwrap_or_default();
        let files = match data_files.is_empty() && lock_files.is_empty() {
            true => Default::default(),
            false => super::paths::named(dir, &data_files, lock_files),
        };
        Self {
            id: m.id.clone(),
            name: m.name.clone(),
            version: m.version.clone(),
            registry,
            lock,
            data_files,
            records,
            files,
            unreadable,
        }
    }

    pub fn bare(m: &AddonManifest) -> Self {
        Self {
            id: m.id.clone(),
            name: m.name.clone(),
            version: m.version.clone(),
            registry: None,
            lock: None,
            data_files: Vec::new(),
            records: Vec::new(),
            files: Default::default(),
            unreadable: Vec::new(),
        }
    }

    /// Whether the addon has anything of the 1.6 standard library: a `registry.json`, a lock file
    /// or data files.
    pub fn is_std(&self) -> bool {
        self.registry.is_some()
            || self.lock.is_some()
            || !self.data_files.is_empty()
            || !self.unreadable.is_empty()
    }

    pub fn aliases(&self) -> &[String] {
        self.registry.as_ref().map_or(&[], |r| r.aliases.as_slice())
    }

    pub fn answers_to(&self, id: &str) -> bool {
        self.id == id || self.aliases().iter().any(|a| a == id)
    }

    fn provides_record(&self, registry: &str, id: &str) -> bool {
        self.lock
            .as_ref()
            .is_some_and(|l| l.provides_record(registry, id))
            || self
                .records
                .iter()
                .any(|r| r.registry == registry && (r.id == id || r.provides.iter().any(|p| p == id)))
    }

    fn defines_registry(&self, name: &str) -> Option<Option<Version>> {
        if let Some(l) = &self.lock {
            if let Some(v) = l.registries.get(name) {
                return Some(version(v));
            }
            if l.knows_registry(name) {
                return Some(None);
            }
        }
        if self.records.iter().any(|r| r.registry == name) || name.starts_with(&format!("{}.", self.id)) {
            return Some(None);
        }
        None
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, serde::Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Level {
    Stop,
    Warn,
    Hint,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct Finding {
    pub level: Level,
    pub addon: String,
    /// The other addon of a `breaks` or `conflicts`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub against: Option<String>,
    pub headline: String,
    pub detail: Vec<String>,
}

impl Finding {
    pub fn line(&self) -> String {
        match self.detail.is_empty() {
            true => self.headline.clone(),
            false => format!("{}. {}", self.headline, self.detail.join(" ")),
        }
    }

    pub fn block(&self) -> String {
        let mut out = self.headline.clone();
        for d in &self.detail {
            out.push_str("\n  ");
            out.push_str(d);
        }
        out
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, serde::Serialize)]
pub struct Report {
    pub findings: Vec<Finding>,
    /// Entries a `registry.json` holds that this launcher ignores, for the build log.
    pub notes: Vec<String>,
}

impl Report {
    pub fn of(&self, level: Level) -> impl Iterator<Item = &Finding> {
        self.findings.iter().filter(move |f| f.level == level)
    }

    pub fn stops(&self) -> bool {
        self.of(Level::Stop).next().is_some()
    }

    /// The error that stops Play: the first stop, and how many more there are.
    pub fn stop_text(&self) -> Option<String> {
        let stops: Vec<&Finding> = self.of(Level::Stop).collect();
        let first = stops.first()?;
        Some(match stops.len() {
            1 => first.line(),
            n => format!("{} (and {} more: run swofty registry check)", first.line(), n - 1),
        })
    }

    pub fn is_empty(&self) -> bool {
        self.findings.is_empty()
    }
}

/// What the checks see: the addons on in the profile in load order, every installed addon, and the
/// 1.6 files of each.
pub struct Scene<'a> {
    /// `None` at install, where every installed addon counts as on.
    pub profile: Option<&'a str>,
    pub on: &'a [Std],
    pub installed: &'a BTreeMap<String, AddonManifest>,
    /// The 1.6 files of installed addons that are off, to name one that would fix a requirement.
    pub off: &'a [Std],
}

impl Scene<'_> {
    fn nobody(&self) -> String {
        match self.profile {
            Some(p) => format!("no addon on in profile {p} provides"),
            None => "no installed addon provides".into(),
        }
    }

    fn on_addon(&self, id: &str) -> Option<&Std> {
        self.on.iter().find(|a| a.answers_to(id))
    }

    fn installed_addon(&self, id: &str) -> Option<&AddonManifest> {
        self.installed.get(id).or_else(|| {
            self.off
                .iter()
                .find(|a| a.answers_to(id))
                .and_then(|a| self.installed.get(&a.id))
        })
    }
}

pub fn check(scene: &Scene) -> Report {
    let mut out = Report::default();
    for a in scene.on {
        for why in &a.unreadable {
            out.findings.push(Finding {
                level: Level::Warn,
                addon: a.id.clone(),
                against: None,
                headline: format!("{}: {why}", a.name),
                detail: vec!["Its requires, breaks and conflicts aren't checked until it's fixed.".into()],
            });
        }
        if let Some(r) = &a.registry {
            out.notes.extend(
                r.skipped
                    .iter()
                    .map(|s| format!("{}: registry.json {s}; ignored", a.name)),
            );
            for n in &r.requires {
                if let Some(f) = need(scene, a, n, Level::Stop) {
                    out.findings.push(f);
                }
            }
            for n in &r.recommends {
                if let Some(f) = need(scene, a, n, Level::Hint) {
                    out.findings.push(f);
                }
            }
            for c in &r.breaks {
                for b in clashing(scene, a, c) {
                    out.findings.push(breaks(a, b, c));
                }
            }
            for c in &r.conflicts {
                for b in clashing(scene, a, c) {
                    out.findings.push(conflicts(a, b, c));
                }
            }
        }
        if let Some(l) = &a.lock {
            for n in &l.needs {
                let Some(Target::Record { registry, id }) = Target::record(&n.record) else {
                    continue;
                };
                let met = n.alternatives().any(|alt| match Target::record(alt) {
                    Some(Target::Record { registry, id }) => {
                        id.starts_with("game/") || record_met(scene, &registry, &id).is_some()
                    }
                    _ => false,
                });
                if met {
                    continue;
                }
                let level = if n.required { Level::Warn } else { Level::Hint };
                let detail = match n.required {
                    true => "The game stops what needs it with a message naming the fix.",
                    false => "What needs it is left out.",
                };
                out.findings.push(Finding {
                    level,
                    addon: a.id.clone(),
                    against: None,
                    headline: format!("{} uses {registry} {id}, which {}", a.name, scene.nobody()),
                    detail: vec![detail.into()],
                });
            }
        }
    }
    out.findings.sort_by_key(|f| f.level);
    out
}

fn sentence(text: &str) -> String {
    let t = text.trim();
    let mut c = t.chars();
    let mut out = match c.next() {
        Some(first) => first.to_uppercase().chain(c).collect::<String>(),
        None => return String::new(),
    };
    if !out.ends_with(['.', '!', '?']) {
        out.push('.');
    }
    out
}

fn for_text(n: &Need) -> String {
    n.for_.as_deref().map(|f| format!(" for {f}")).unwrap_or_default()
}

fn need(scene: &Scene, a: &Std, n: &Need, level: Level) -> Option<Finding> {
    let verb = match level {
        Level::Stop => "needs",
        _ => "recommends",
    };
    let or_off = match level {
        Level::Stop => format!(", or turn {} off", a.name),
        _ => String::new(),
    };
    let finding = |headline: String, fix: String| Finding {
        level,
        addon: a.id.clone(),
        against: None,
        headline,
        detail: vec![fix],
    };
    match &n.target {
        Target::Service(_) | Target::Hook(_) => None,
        Target::Addon(id) => {
            if let Some(dep) = scene.on_addon(id) {
                if n.version.matches(&dep.version) {
                    return None;
                }
                return Some(finding(
                    format!(
                        "{} {verb} {} {}{}, and {} is installed",
                        a.name,
                        dep.name,
                        n.version,
                        for_text(n),
                        dep.version
                    ),
                    format!("Update {} in the Addon Browser{or_off}.", dep.name),
                ));
            }
            Some(match scene.installed_addon(id) {
                Some(dep) => finding(
                    format!(
                        "{} {verb} {}{}, which is off in profile {}",
                        a.name,
                        dep.name,
                        for_text(n),
                        scene.profile.unwrap_or("any")
                    ),
                    format!("Turn {} on on the Addons page{or_off}.", dep.name),
                ),
                None => finding(
                    format!("{} {verb} {id}{}, which isn't installed", a.name, for_text(n)),
                    format!("Install it in the Addon Browser{or_off}."),
                ),
            })
        }
        Target::Registry(name) => {
            if let Some((_, v)) = CORE_REGISTRIES.iter().find(|(r, _)| r == name) {
                let have = version(v).expect("a core registry version");
                return (!n.version.matches(&have)).then(|| {
                    finding(
                        format!(
                            "{} {verb} the {name} registry {}{}, and this launcher has {v}",
                            a.name,
                            n.version,
                            for_text(n)
                        ),
                        format!("Update Swofty Launcher{or_off}."),
                    )
                });
            }
            let mut unknown_version = false;
            for p in scene.on {
                match p.defines_registry(name) {
                    Some(Some(v)) if n.version.matches(&v) => return None,
                    Some(Some(v)) => {
                        return Some(finding(
                            format!(
                                "{} {verb} the {name} registry {}{}, and {} provides {v}",
                                a.name,
                                n.version,
                                for_text(n),
                                p.name
                            ),
                            format!("Update {} in the Addon Browser{or_off}.", p.name),
                        ));
                    }
                    Some(None) => unknown_version = true,
                    None => {}
                }
            }
            if unknown_version {
                return None;
            }
            let headline = format!(
                "{} {verb} the {name} registry{}, which {}",
                a.name,
                for_text(n),
                scene.nobody()
            );
            Some(
                match scene.off.iter().find(|p| p.defines_registry(name).is_some()) {
                    Some(p) => finding(
                        headline,
                        format!("Turn {} on on the Addons page{or_off}.", p.name),
                    ),
                    None => finding(
                        headline,
                        format!("Install the addon that provides it, or update Swofty Launcher{or_off}."),
                    ),
                },
            )
        }
        Target::Record { registry, id } => {
            if id.starts_with("game/") {
                return None;
            }
            if let Some(p) = record_met(scene, registry, id) {
                if n.version.matches(&p.version) {
                    return None;
                }
                return Some(finding(
                    format!(
                        "{} {verb} {registry} {id} from {} {}{}, and {} is installed",
                        a.name,
                        p.name,
                        n.version,
                        for_text(n),
                        p.version
                    ),
                    format!("Update {} in the Addon Browser{or_off}.", p.name),
                ));
            }
            let headline = format!(
                "{} {verb} {registry} {id}{}, which {}",
                a.name,
                for_text(n),
                scene.nobody()
            );
            Some(match scene.off.iter().find(|p| p.provides_record(registry, id)) {
                Some(p) => finding(
                    headline,
                    format!("Turn {} on on the Addons page{or_off}.", p.name),
                ),
                None => finding(
                    headline,
                    format!("Install an addon that provides it in the Addon Browser{or_off}."),
                ),
            })
        }
    }
}

/// The last addon on in load order that provides the record, as a venue resolves.
fn record_met<'a>(scene: &'a Scene, registry: &str, id: &str) -> Option<&'a Std> {
    scene.on.iter().rev().find(|p| p.provides_record(registry, id))
}

fn glob(pattern: &str, id: &str) -> bool {
    match pattern.strip_suffix('*') {
        Some(prefix) => id.starts_with(prefix),
        None => pattern == id,
    }
}

fn clashing<'a>(scene: &'a Scene, a: &Std, c: &Clash) -> Vec<&'a Std> {
    scene
        .on
        .iter()
        .filter(|b| b.id != a.id)
        .filter(|b| glob(&c.id, &b.id) || b.aliases().iter().any(|al| glob(&c.id, al)))
        .filter(|b| c.version.matches(&b.version))
        .collect()
}

fn breaks(a: &Std, b: &Std, c: &Clash) -> Finding {
    let mut detail: Vec<String> = c.why.iter().map(|w| sentence(w)).collect();
    detail.push(match c.version.update_escapes() {
        true => format!("Update {} in the Addon Browser, or turn one of them off.", b.name),
        false => "Turn one of them off.".into(),
    });
    Finding {
        level: Level::Stop,
        addon: a.id.clone(),
        against: Some(b.id.clone()),
        headline: format!(
            "{} {} doesn't work with {} {}",
            a.name, a.version, b.name, b.version
        ),
        detail,
    }
}

fn conflicts(a: &Std, b: &Std, c: &Clash) -> Finding {
    let mut detail: Vec<String> = c.why.iter().map(|w| sentence(w)).collect();
    detail.push("Play goes on; if something looks wrong in game, turn one of them off.".into());
    Finding {
        level: Level::Warn,
        addon: a.id.clone(),
        against: Some(b.id.clone()),
        headline: format!("{} {} conflicts with {} {}", a.name, a.version, b.name, b.version),
        detail,
    }
}

/// The check for an addon just installed, against every installed addon as if all were on: what it
/// needs that isn't installed, what it recommends, and the installed addons it breaks, conflicts
/// with, or that break it. Nothing here refuses the install: a `breaks` is a warning that Play
/// stops while both are on.
pub fn at_install(new: &Std, installed: &BTreeMap<String, AddonManifest>, all: &[Std]) -> Report {
    let mut on = vec![new.clone()];
    on.extend(all.iter().filter(|s| s.id != new.id).cloned());
    let mut report = check(&Scene {
        profile: None,
        on: &on,
        installed,
        off: &[],
    });
    report
        .findings
        .retain(|f| f.addon == new.id || f.against.as_deref() == Some(new.id.as_str()));
    for f in report.findings.iter_mut().filter(|f| f.level == Level::Stop) {
        f.level = Level::Warn;
        f.detail.push(match f.against {
            Some(_) => "Play stops while both are on.".into(),
            None => "Play stops until it's there.".into(),
        });
    }
    report
}

/// The 1.6 files of every installed addon, by id; addons built in have none.
pub fn read_all(
    store: &crate::store::Store,
    installed: &BTreeMap<String, AddonManifest>,
) -> BTreeMap<String, Std> {
    installed
        .iter()
        .map(|(id, m)| {
            let s = match store.is_builtin(id) {
                true => Std::bare(m),
                false => Std::read(&store.addons_dir().join(id), m),
            };
            (id.clone(), s)
        })
        .collect()
}

/// The profile's check: the addons in `order` are on.
pub fn for_order(
    profile: &str,
    order: &[String],
    installed: &BTreeMap<String, AddonManifest>,
    all: &BTreeMap<String, Std>,
) -> Report {
    let on: Vec<Std> = order.iter().filter_map(|id| all.get(id).cloned()).collect();
    let off: Vec<Std> = all.values().filter(|s| !order.contains(&s.id)).cloned().collect();
    check(&Scene {
        profile: Some(profile),
        on: &on,
        installed,
        off: &off,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::stdlib::lock::Lock;

    fn manifest(id: &str, name: &str, version: &str) -> AddonManifest {
        serde_json::from_str(&format!(
            r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "{version}", "author": "a",
                 "compat": {{ "game": "f1_25" }} }}"#
        ))
        .unwrap()
    }

    struct World {
        installed: BTreeMap<String, AddonManifest>,
        std: BTreeMap<String, Std>,
    }

    impl World {
        fn new() -> Self {
            Self {
                installed: BTreeMap::new(),
                std: BTreeMap::new(),
            }
        }

        fn add(
            mut self,
            id: &str,
            name: &str,
            version: &str,
            registry: Option<&str>,
            lock: Option<&str>,
        ) -> Self {
            let m = manifest(id, name, version);
            let mut s = Std::bare(&m);
            s.registry = registry.map(|r| RegistryManifest::parse(r).unwrap());
            s.lock = lock.map(|l| Lock::parse(l).unwrap());
            self.installed.insert(id.into(), m);
            self.std.insert(id.into(), s);
            self
        }

        fn check(&self, on: &[&str]) -> Report {
            let order: Vec<String> = on.iter().map(|s| s.to_string()).collect();
            for_order("main", &order, &self.installed, &self.std)
        }
    }

    fn lines(r: &Report, level: Level) -> Vec<String> {
        r.of(level).map(Finding::line).collect()
    }

    #[test]
    fn addons_without_std_files_check_nothing() {
        let w = World::new()
            .add("a.old", "Old", "1.0.0", None, None)
            .add("b.old", "Older", "0.1.0", None, None);
        let r = w.check(&["a.old", "b.old"]);
        assert!(r.is_empty());
        assert!(!r.stops());
        assert_eq!(r.stop_text(), None);
    }

    #[test]
    fn an_addon_requirement_stops_play_when_off_missing_or_too_old() {
        let w = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(r#"{ "requires": [ { "addon": "b.y", "version": "^1.3.0" } ] }"#),
                None,
            )
            .add("b.y", "Y", "1.2.0", None, None);
        assert_eq!(
            lines(&w.check(&["a.x", "b.y"]), Level::Stop),
            ["X needs Y ^1.3.0, and 1.2.0 is installed. Update Y in the Addon Browser, or turn X off."]
        );
        assert_eq!(
            lines(&w.check(&["a.x"]), Level::Stop),
            ["X needs Y, which is off in profile main. Turn Y on on the Addons page, or turn X off."]
        );
        let w = World::new().add(
            "a.x",
            "X",
            "1.0.0",
            Some(r#"{ "requires": [ { "id": "c.z" } ] }"#),
            None,
        );
        let r = w.check(&["a.x"]);
        assert_eq!(
            r.stop_text().unwrap(),
            "X needs c.z, which isn't installed. Install it in the Addon Browser, or turn X off."
        );
        let w = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(r#"{ "requires": [ { "addon": "b.y", "version": "^1.3.0" } ] }"#),
                None,
            )
            .add("b.y", "Y", "1.4.0", None, None);
        assert!(w.check(&["a.x", "b.y"]).is_empty());
    }

    #[test]
    fn an_alias_answers_for_the_old_id() {
        let w = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(r#"{ "requires": [ { "addon": "mod.bahrain24" } ] }"#),
                None,
            )
            .add(
                "mod.sepang",
                "Sepang",
                "2.0.0",
                Some(r#"{ "aliases": [ { "id": "mod.bahrain24" } ] }"#),
                None,
            );
        assert!(w.check(&["a.x", "mod.sepang"]).is_empty());
        assert_eq!(
            lines(&w.check(&["a.x"]), Level::Stop),
            [
                "X needs Sepang, which is off in profile main. Turn Sepang on on the Addons page, or turn X off."
            ]
        );
    }

    #[test]
    fn a_registry_requirement_checks_the_core_and_the_addons_on() {
        let w = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(
                    r#"{ "requires": [ { "registry": "career.season", "version": "^1" },
                                         { "registry": "career.season", "version": "^2" },
                                         { "registry": "track", "version": "^1" },
                                         { "registry": "tails.whatif.presets", "version": "^1" },
                                         { "service": "progressive.history", "version": ">=1" },
                                         { "hook": "career.calendar.add_activity" } ] }"#,
                ),
                None,
            )
            .add(
                "tails.whatif",
                "What If",
                "1.0.0",
                None,
                Some(r#"{ "registries": { "tails.whatif.presets": "1.2" } }"#),
            );
        assert_eq!(
            lines(&w.check(&["a.x", "tails.whatif"]), Level::Stop),
            [
                "X needs the career.season registry ^2, and this launcher has 1.0. Update Swofty Launcher, or turn X off.",
                "X needs the track registry, which no addon on in profile main provides. Install the addon that \
                 provides it, or update Swofty Launcher, or turn X off."
            ]
        );
        assert!(
            lines(&w.check(&["a.x"]), Level::Stop).contains(
                &"X needs the tails.whatif.presets registry, which no addon on in profile main provides. Turn What \
                  If on on the Addons page, or turn X off."
                    .to_owned()
            )
        );
    }

    #[test]
    fn a_record_requirement_needs_an_addon_on_that_provides_it() {
        let lock = r#"{ "provides": { "track": [ { "id": "mod.sochi/sochi", "label": "Sochi Autodrom" } ],
                                      "track.venue": [ "venue/sochi" ] } }"#;
        let w = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(
                    r#"{ "requires": [ { "record": "track:venue/sochi", "version": ">=1.1.0" },
                                         { "record": "vehicle:game/ferrari_2025" } ] }"#,
                ),
                None,
            )
            .add("mod.sochi", "Sochi Autodrom", "1.0.0", None, Some(lock));
        assert_eq!(
            lines(&w.check(&["a.x", "mod.sochi"]), Level::Stop),
            [
                "X needs track venue/sochi from Sochi Autodrom >=1.1.0, and 1.0.0 is installed. Update Sochi Autodrom \
              in the Addon Browser, or turn X off."
            ]
        );
        assert_eq!(
            lines(&w.check(&["a.x"]), Level::Stop),
            [
                "X needs track venue/sochi, which no addon on in profile main provides. Turn Sochi Autodrom on on the \
              Addons page, or turn X off."
            ]
        );
        let w = World::new().add(
            "a.x",
            "X",
            "1.0.0",
            Some(r#"{ "requires": [ { "record": "track:venue/imola" } ] }"#),
            None,
        );
        assert_eq!(
            lines(&w.check(&["a.x"]), Level::Stop),
            [
                "X needs track venue/imola, which no addon on in profile main provides. Install an addon that provides \
              it in the Addon Browser, or turn X off."
            ]
        );
    }

    #[test]
    fn a_data_file_record_provides_too() {
        let mut w = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(r#"{ "requires": [ { "record": "track:venue/sochi" } ] }"#),
                None,
            )
            .add("mod.sochi", "Sochi", "1.0.0", None, None);
        w.std.get_mut("mod.sochi").unwrap().records = data::records(
            "mod.sochi",
            r#"{ "track": { "sochi": { "slot": "game/sochi", "provides": ["venue/sochi"] } } }"#,
        );
        assert!(w.check(&["a.x", "mod.sochi"]).is_empty());
    }

    #[test]
    fn breaks_stops_play_and_names_the_fix() {
        let w = World::new()
            .add(
                "swofty.progressive",
                "Progressive Career",
                "1.3.0",
                Some(
                    r#"{ "breaks": [ { "id": "swofty.career-editor", "version": "<0.5.0",
                                       "why": "its contract overwrite switches off Progressive's contract hook" } ] }"#,
                ),
                None,
            )
            .add("swofty.career-editor", "Career Editor", "0.4.2", None, None);
        let r = w.check(&["swofty.progressive", "swofty.career-editor"]);
        let stop = r.of(Level::Stop).next().unwrap();
        assert_eq!(
            stop.block(),
            "Progressive Career 1.3.0 doesn't work with Career Editor 0.4.2\n  Its contract overwrite switches off \
             Progressive's contract hook.\n  Update Career Editor in the Addon Browser, or turn one of them off."
        );
        assert_eq!(stop.against.as_deref(), Some("swofty.career-editor"));
        assert!(w.check(&["swofty.progressive"]).is_empty());
        let fixed = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(r#"{ "breaks": [ { "id": "b.y", "version": "<0.5.0" } ] }"#),
                None,
            )
            .add("b.y", "Y", "0.5.0", None, None);
        assert!(fixed.check(&["a.x", "b.y"]).is_empty());
    }

    #[test]
    fn breaks_on_every_version_can_only_be_turned_off_and_globs_match() {
        let w = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(r#"{ "breaks": [ { "id": "someone.*", "version": "*" } ] }"#),
                None,
            )
            .add("someone.one", "One", "2.0.0", None, None)
            .add("someone.two", "Two", "3.0.0", None, None);
        assert_eq!(
            lines(&w.check(&["a.x", "someone.one", "someone.two"]), Level::Stop),
            [
                "X 1.0.0 doesn't work with One 2.0.0. Turn one of them off.",
                "X 1.0.0 doesn't work with Two 3.0.0. Turn one of them off."
            ]
        );
        let r = w.check(&["a.x", "someone.one", "someone.two"]);
        assert_eq!(
            r.stop_text().unwrap(),
            "X 1.0.0 doesn't work with One 2.0.0. Turn one of them off. (and 1 more: run swofty registry check)"
        );
    }

    #[test]
    fn conflicts_only_warns() {
        let w = World::new()
            .add(
                "a.x",
                "X",
                "1.3.0",
                Some(r#"{ "conflicts": [ { "id": "someone.lineups", "version": "*", "why": "both rewrite the season's line-ups" } ] }"#),
                None,
            )
            .add("someone.lineups", "Lineups", "1.0.0", None, None);
        let r = w.check(&["a.x", "someone.lineups"]);
        assert!(!r.stops());
        assert_eq!(
            lines(&r, Level::Warn),
            [
                "X 1.3.0 conflicts with Lineups 1.0.0. Both rewrite the season's line-ups. Play goes on; if something \
              looks wrong in game, turn one of them off."
            ]
        );
    }

    #[test]
    fn recommends_is_a_hint() {
        let w = World::new().add(
            "cs.2016",
            "F1 2016 GP/TT Addon",
            "0.6.0",
            Some(
                r#"{ "recommends": [ { "record": "track:venue/sochi", "for": "the 2016 Russian Grand Prix" },
                                       { "addon": "x.extra", "for": "more liveries" } ] }"#,
            ),
            None,
        );
        let r = w.check(&["cs.2016"]);
        assert!(!r.stops());
        assert_eq!(
            lines(&r, Level::Hint),
            [
                "F1 2016 GP/TT Addon recommends track venue/sochi for the 2016 Russian Grand Prix, which no addon on \
                 in profile main provides. Install an addon that provides it in the Addon Browser.",
                "F1 2016 GP/TT Addon recommends x.extra for more liveries, which isn't installed. Install it in the \
                 Addon Browser."
            ]
        );
    }

    #[test]
    fn lock_references_warn_or_hint() {
        let w = World::new().add(
            "mod.esp",
            "ESP",
            "1.0.0",
            None,
            Some(r#"{ "needs": [ { "record": "vehicle:swofty.classic-seasons-2017/ferrari", "policy": "required" },
                                  { "record": "track:venue/sochi", "policy": "optional" },
                                  { "record": "vehicle:game/ferrari_2025" } ] }"#),
        );
        let r = w.check(&["mod.esp"]);
        assert!(!r.stops());
        assert_eq!(
            lines(&r, Level::Warn),
            [
                "ESP uses vehicle swofty.classic-seasons-2017/ferrari, which no addon on in profile main provides. The \
              game stops what needs it with a message naming the fix."
            ]
        );
        assert_eq!(
            lines(&r, Level::Hint),
            [
                "ESP uses track venue/sochi, which no addon on in profile main provides. What needs it is left out."
            ]
        );
    }

    #[test]
    fn a_lock_reference_with_fallbacks_is_met_by_any_of_them() {
        let lock = r#"{ "needs": [
            { "record": "vehicle:mod.x/a", "or": ["vehicle:mod.y/b"], "policy": "required" },
            { "record": "vehicle:mod.x/c", "or": ["vehicle:game/ferrari_2025"], "policy": "required" },
            { "record": "vehicle:mod.x/d", "or": ["vehicle:mod.z/e"], "policy": "optional" } ] }"#;
        let w = World::new().add("mod.esp", "ESP", "1.0.0", None, Some(lock)).add(
            "mod.y",
            "Y",
            "1.0.0",
            None,
            Some(r#"{ "provides": { "vehicle": [ "mod.y/b" ] } }"#),
        );
        let r = w.check(&["mod.esp", "mod.y"]);
        assert!(lines(&r, Level::Warn).is_empty(), "{r:?}");
        assert_eq!(
            lines(&r, Level::Hint),
            [
                "ESP uses vehicle mod.x/d, which no addon on in profile main provides. What needs it is left out."
            ]
        );
        assert_eq!(
            lines(&w.check(&["mod.esp"]), Level::Warn),
            [
                "ESP uses vehicle mod.x/a, which no addon on in profile main provides. The game stops what needs it \
              with a message naming the fix."
            ]
        );
    }

    #[test]
    fn a_broken_registry_json_warns_and_unknown_entries_are_notes() {
        let mut w = World::new().add(
            "a.x",
            "X",
            "1.0.0",
            Some(r#"{ "requires": [ { "gadget": 1 } ], "future": {} }"#),
            None,
        );
        let r = w.check(&["a.x"]);
        assert!(r.is_empty());
        assert_eq!(
            r.notes,
            ["X: registry.json requires[0]: not an addon, registry, service, hook or record entry; ignored"]
        );
        w.std
            .get_mut("a.x")
            .unwrap()
            .unreadable
            .push("registry.json is not valid JSON (eof)".into());
        assert_eq!(
            lines(&w.check(&["a.x"]), Level::Warn),
            [
                "X: registry.json is not valid JSON (eof). Its requires, breaks and conflicts aren't checked until it's \
              fixed."
            ]
        );
    }

    #[test]
    fn stops_come_first() {
        let w = World::new()
            .add(
                "a.x",
                "X",
                "1.0.0",
                Some(
                    r#"{ "recommends": [ { "addon": "q.q" } ], "conflicts": [ { "id": "b.y" } ],
                         "requires": [ { "addon": "z.z" } ] }"#,
                ),
                None,
            )
            .add("b.y", "Y", "1.0.0", None, None);
        let levels: Vec<Level> = w
            .check(&["a.x", "b.y"])
            .findings
            .iter()
            .map(|f| f.level)
            .collect();
        assert_eq!(levels, [Level::Stop, Level::Warn, Level::Hint]);
    }

    #[test]
    fn at_install_names_what_the_new_addon_needs_and_breaks_without_refusing() {
        let w = World::new()
            .add("old.editor", "Career Editor", "0.4.2", None, None)
            .add("other.thing", "Other", "1.0.0", Some(r#"{ "requires": [ { "addon": "nope.nope" } ] }"#), None)
            .add(
                "new.prog",
                "Progressive",
                "1.3.0",
                Some(r#"{ "requires": [ { "addon": "base.cs" } ],
                         "breaks": [ { "id": "old.editor", "version": "<0.5.0", "why": "its overwrite" } ] }"#),
                None,
            );
        let all: Vec<Std> = w.std.values().cloned().collect();
        let r = at_install(&w.std["new.prog"], &w.installed, &all);
        assert!(!r.stops());
        assert_eq!(
            lines(&r, Level::Warn),
            [
                "Progressive needs base.cs, which isn't installed. Install it in the Addon Browser, or turn \
                 Progressive off. Play stops until it's there.",
                "Progressive 1.3.0 doesn't work with Career Editor 0.4.2. Its overwrite. Update Career Editor in the \
                 Addon Browser, or turn one of them off. Play stops while both are on."
            ]
        );
    }

    #[test]
    fn the_core_registry_table_matches_the_sdk() {
        let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("../sdk/std/registries");
        let mut found: Vec<(String, String)> = Vec::new();
        for e in std::fs::read_dir(&dir)
            .unwrap()
            .flatten()
            .filter(|e| e.path().is_dir())
        {
            let mut versions: Vec<Version> = std::fs::read_dir(e.path())
                .unwrap()
                .flatten()
                .filter_map(|f| version(f.file_name().to_str()?.strip_suffix(".json")?))
                .collect();
            versions.sort();
            let last = versions.last().expect("a schema");
            found.push((
                e.file_name().to_string_lossy().into_owned(),
                format!("{}.{}", last.major, last.minor),
            ));
        }
        found.sort();
        let table: Vec<(String, String)> = CORE_REGISTRIES
            .iter()
            .map(|(n, v)| (n.to_string(), v.to_string()))
            .collect();
        assert_eq!(table, found);
    }
}
