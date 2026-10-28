//! Folders on the Addons page: Built In, one for each category and the
//! player's own. A folder's addons sit together in the load order, so moving
//! a folder moves every addon in it.

use std::collections::{BTreeMap, BTreeSet, HashMap};

use serde::{Deserialize, Serialize};
use swofty_addon::manifest::Category;

use crate::builtin::CORE_ID;
use crate::profile::{Profile, ProfileAddon};

pub const FILE: &str = "folders.json";

const NONE: &str = "none";

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Key {
    BuiltIn,
    Category(Category),
    Own(String),
}

impl Key {
    pub fn parse(s: &str) -> Option<Self> {
        if s == "built-in" {
            return Some(Self::BuiltIn);
        }
        if let Some(id) = s.strip_prefix("folder:") {
            return Some(Self::Own(id.to_owned()));
        }
        let name = s.strip_prefix("category:")?;
        serde_json::from_value(serde_json::Value::String(name.to_owned()))
            .ok()
            .map(Self::Category)
    }

    pub fn text(&self) -> String {
        match self {
            Self::BuiltIn => "built-in".to_owned(),
            Self::Category(c) => match serde_json::to_value(c) {
                Ok(serde_json::Value::String(name)) => format!("category:{name}"),
                _ => format!("category:{c:?}"),
            },
            Self::Own(id) => format!("folder:{id}"),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Folder {
    pub id: String,
    pub name: String,
    #[serde(flatten, default, skip_serializing_if = "BTreeMap::is_empty")]
    pub unknown: BTreeMap<String, serde_json::Value>,
}

/// The player's folders, where they put addons and which folders are closed.
/// An addon they never moved is in its category's folder, or in none.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
pub struct Folders {
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub folders: Vec<Folder>,
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub placed: BTreeMap<String, String>,
    #[serde(default, skip_serializing_if = "BTreeSet::is_empty")]
    pub collapsed: BTreeSet<String>,
    #[serde(flatten, default, skip_serializing_if = "BTreeMap::is_empty")]
    pub unknown: BTreeMap<String, serde_json::Value>,
}

/// The folder an addon is in when the player hasn't moved it.
pub fn default_place(builtin: bool, category: Option<Category>) -> Option<Key> {
    match builtin {
        true => Some(Key::BuiltIn),
        false => category.map(Key::Category),
    }
}

impl Folders {
    pub fn place(&self, id: &str, builtin: bool, category: Option<Category>) -> Option<Key> {
        let default = default_place(builtin, category);
        if builtin || id == CORE_ID {
            return Some(Key::BuiltIn);
        }
        match self.placed.get(id).map(String::as_str) {
            Some(NONE) => None,
            Some(s) => match Key::parse(s) {
                Some(Key::Own(f)) if self.folder(&f).is_some() => Some(Key::Own(f)),
                Some(k @ Key::Category(_)) => Some(k),
                _ => default,
            },
            None => default,
        }
    }

    /// Puts `id` in `place`, remembering it only when it isn't the addon's default.
    pub fn set_place(&mut self, id: &str, place: Option<&Key>, default: Option<&Key>) {
        if place == default {
            self.placed.remove(id);
        } else {
            let text = place.map_or_else(|| NONE.to_owned(), Key::text);
            self.placed.insert(id.to_owned(), text);
        }
    }

    pub fn folder(&self, id: &str) -> Option<&Folder> {
        self.folders.iter().find(|f| f.id == id)
    }

    pub fn name(&self, key: &Key) -> String {
        match key {
            Key::BuiltIn => "Built In".to_owned(),
            Key::Category(c) => c.label().to_owned(),
            Key::Own(id) => self.folder(id).map_or_else(|| id.clone(), |f| f.name.clone()),
        }
    }

    /// Makes a folder named `name` and returns its key.
    pub fn create(&mut self, name: &str) -> Key {
        let next = self
            .folders
            .iter()
            .filter_map(|f| f.id.strip_prefix('f')?.parse::<u32>().ok())
            .max()
            .map_or(1, |n| n + 1);
        let id = format!("f{next}");
        self.folders.push(Folder {
            id: id.clone(),
            name: name.to_owned(),
            unknown: BTreeMap::new(),
        });
        Key::Own(id)
    }

    pub fn rename(&mut self, id: &str, name: &str) -> bool {
        match self.folders.iter_mut().find(|f| f.id == id) {
            Some(f) => {
                f.name = name.to_owned();
                true
            }
            None => false,
        }
    }

    /// Removes the folder `id`; its addons go back to their own folders.
    pub fn remove(&mut self, id: &str) {
        let key = Key::Own(id.to_owned()).text();
        self.folders.retain(|f| f.id != id);
        self.placed.retain(|_, k| *k != key);
        self.collapsed.remove(&key);
    }

    pub fn is_collapsed(&self, key: &Key) -> bool {
        self.collapsed.contains(&key.text())
    }

    pub fn set_collapsed(&mut self, key: &Key, collapsed: bool) {
        match collapsed {
            true => self.collapsed.insert(key.text()),
            false => self.collapsed.remove(&key.text()),
        };
    }

    /// Checks a name for a folder of the player's own, other than `except`.
    pub fn check_name(&self, name: &str, except: Option<&str>) -> Result<(), String> {
        let name = name.trim();
        if name.is_empty() {
            return Err("Give the folder a name".to_owned());
        }
        if name.chars().count() > 40 {
            return Err("Folder names are at most 40 characters".to_owned());
        }
        let taken = self
            .folders
            .iter()
            .filter(|f| Some(f.id.as_str()) != except)
            .map(|f| f.name.as_str())
            .chain(["Built In"])
            .chain(Category::ALL.iter().map(|c| c.label()))
            .any(|n| n.eq_ignore_ascii_case(name));
        match taken {
            true => Err(format!("There is already a folder named {name}")),
            false => Ok(()),
        }
    }
}

/// A top-level item of the list: an addon in no folder, or a folder and its addons.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Entry {
    Addon(String),
    Folder(Key, Vec<String>),
}

impl Entry {
    pub fn folder(&self) -> Option<&Key> {
        match self {
            Self::Folder(k, _) => Some(k),
            Self::Addon(_) => None,
        }
    }
}

/// Where a moved addon or folder goes, by index into the list before the move.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Spot {
    Top(usize),
    In(Key, usize),
}

/// Groups `order` into folders: each folder sits where its first addon was,
/// and Built In comes first.
pub fn group(order: impl IntoIterator<Item = String>, place: impl Fn(&str) -> Option<Key>) -> Vec<Entry> {
    let mut entries: Vec<Entry> = Vec::new();
    let mut at: HashMap<Key, usize> = HashMap::new();
    for id in order {
        let key = match id == CORE_ID {
            true => Some(Key::BuiltIn),
            false => place(&id),
        };
        match key {
            None => entries.push(Entry::Addon(id)),
            Some(k) => match at.get(&k) {
                Some(&i) => {
                    if let Entry::Folder(_, members) = &mut entries[i] {
                        members.push(id);
                    }
                }
                None => {
                    at.insert(k.clone(), entries.len());
                    entries.push(Entry::Folder(k, vec![id]));
                }
            },
        }
    }
    if let Some(i) = at.get(&Key::BuiltIn).copied().filter(|&i| i > 0) {
        let built_in = entries.remove(i);
        entries.insert(0, built_in);
    }
    entries
}

pub fn flatten(entries: &[Entry]) -> Vec<String> {
    entries
        .iter()
        .flat_map(|e| match e {
            Entry::Addon(id) => std::slice::from_ref(id),
            Entry::Folder(_, members) => members.as_slice(),
        })
        .cloned()
        .collect()
}

/// The first top-level index anything can move to: below Built In.
pub fn floor(entries: &[Entry]) -> usize {
    usize::from(matches!(entries.first(), Some(Entry::Folder(Key::BuiltIn, _))))
}

/// Where `id` is: its folder (or none) and its index there.
pub fn locate(entries: &[Entry], id: &str) -> Option<(Option<Key>, usize)> {
    entries.iter().enumerate().find_map(|(i, e)| match e {
        Entry::Addon(a) if a == id => Some((None, i)),
        Entry::Folder(k, members) => members.iter().position(|m| m == id).map(|j| (Some(k.clone()), j)),
        _ => None,
    })
}

/// Moves the addon `id` to `spot` and returns the folder it is now in. Addons
/// in Built In don't move, and nothing moves into Built In or above it.
pub fn move_addon(entries: &mut Vec<Entry>, id: &str, spot: &Spot) -> Option<Option<Key>> {
    let (from, j) = locate(entries, id)?;
    if from == Some(Key::BuiltIn) {
        return None;
    }
    let spot = match spot {
        Spot::In(Key::BuiltIn, _) => Spot::Top(floor(entries)),
        Spot::Top(i) => Spot::Top((*i).max(floor(entries))),
        s => s.clone(),
    };
    match &spot {
        Spot::Top(i) => {
            let mut i = *i;
            match &from {
                None => {
                    entries.remove(j);
                    if j < i {
                        i -= 1;
                    }
                }
                Some(k) => remove_member(entries, k, j),
            }
            entries.insert(i.min(entries.len()), Entry::Addon(id.to_owned()));
            Some(None)
        }
        Spot::In(key, i) => {
            let mut i = *i;
            match &from {
                None => {
                    entries.remove(j);
                }
                Some(k) => {
                    remove_member(entries, k, j);
                    if k == key && j < i {
                        i -= 1;
                    }
                }
            }
            let members = match entries.iter_mut().find(|e| e.folder() == Some(key)) {
                Some(Entry::Folder(_, members)) => members,
                _ => {
                    entries.push(Entry::Folder(key.clone(), Vec::new()));
                    match entries.last_mut() {
                        Some(Entry::Folder(_, members)) => members,
                        _ => unreachable!(),
                    }
                }
            };
            members.insert(i.min(members.len()), id.to_owned());
            Some(Some(key.clone()))
        }
    }
}

fn remove_member(entries: &mut [Entry], key: &Key, j: usize) {
    if let Some(Entry::Folder(_, members)) = entries.iter_mut().find(|e| e.folder() == Some(key)) {
        members.remove(j);
    }
}

/// Moves the folder `key` to the top-level index `to`, taken before the move.
pub fn move_folder(entries: &mut Vec<Entry>, key: &Key, to: usize) -> bool {
    if *key == Key::BuiltIn {
        return false;
    }
    let Some(from) = entries.iter().position(|e| e.folder() == Some(key)) else {
        return false;
    };
    let mut to = to.max(floor(entries));
    let folder = entries.remove(from);
    if from < to {
        to -= 1;
    }
    entries.insert(to.min(entries.len()), folder);
    from != to
}

/// Where the arrows in an addon's panel move it: one place up or down among
/// what shares its folder, or the list's top level for one in none.
pub fn step(entries: &[Entry], id: &str, up: bool) -> Option<Spot> {
    let (folder, j) = locate(entries, id)?;
    match folder {
        Some(Key::BuiltIn) => None,
        Some(k) => {
            let len = entries
                .iter()
                .find_map(|e| match e {
                    Entry::Folder(f, m) if *f == k => Some(m.len()),
                    _ => None,
                })
                .unwrap_or(0);
            match up {
                true => (j > 0).then(|| Spot::In(k, j - 1)),
                false => (j + 1 < len).then(|| Spot::In(k, j + 2)),
            }
        }
        None => match up {
            true => (j > floor(entries)).then(|| Spot::Top(j - 1)),
            false => (j + 1 < entries.len()).then(|| Spot::Top(j + 2)),
        },
    }
}

/// Puts the profile's addons in `order`. Installed addons in `add` that the
/// profile doesn't list yet join it, off; addons `order` leaves out keep
/// their order after it.
pub fn reorder(p: &mut Profile, order: &[String], add: &dyn Fn(&str) -> bool) {
    let mut rest: Vec<Option<ProfileAddon>> = std::mem::take(&mut p.addons).into_iter().map(Some).collect();
    let at: HashMap<String, usize> = rest
        .iter()
        .enumerate()
        .filter_map(|(i, a)| a.as_ref().map(|a| (a.id.clone(), i)))
        .collect();
    for id in order {
        match at.get(id) {
            Some(&i) => p.addons.extend(rest[i].take()),
            None if add(id) => p.addons.push(ProfileAddon {
                id: id.clone(),
                enabled: false,
                unknown: BTreeMap::new(),
            }),
            None => {}
        }
    }
    p.addons.extend(rest.into_iter().flatten());
}

/// Puts the profile's addons in folder order; true when that moved any.
pub fn arrange(p: &mut Profile, place: impl Fn(&str) -> Option<Key>) -> bool {
    if p.is_vanilla() {
        return false;
    }
    let order = flatten(&group(p.addons.iter().map(|a| a.id.clone()), place));
    if p.addons.iter().map(|a| &a.id).eq(order.iter()) {
        return false;
    }
    reorder(p, &order, &|_| false);
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ids(list: &[&str]) -> Vec<String> {
        list.iter().map(|s| (*s).to_owned()).collect()
    }

    fn place(id: &str) -> Option<Key> {
        match id.split_once('.').map(|(k, _)| k) {
            Some("s") => Some(Key::Category(Category::Seasons)),
            Some("ai") => Some(Key::Category(Category::Ai)),
            Some("mine") => Some(Key::Own("f1".into())),
            Some("tsm") => Some(Key::BuiltIn),
            _ => None,
        }
    }

    fn seasons() -> Key {
        Key::Category(Category::Seasons)
    }

    #[test]
    fn keys_read_back() {
        for k in [
            Key::BuiltIn,
            Key::Category(Category::Ai),
            Key::Category(Category::Seasons),
            Key::Own("f3".into()),
        ] {
            assert_eq!(Key::parse(&k.text()), Some(k));
        }
        assert_eq!(Key::Category(Category::Ai).text(), "category:ai");
        assert_eq!(Key::parse("category:hovercraft"), None);
    }

    #[test]
    fn folders_sit_where_their_first_addon_was_and_built_in_leads() {
        let order = ids(&[
            "swofty.core",
            "x.a",
            "s.one",
            "ai.one",
            "x.b",
            "s.two",
            "tsm.mods",
        ]);
        let entries = group(order, place);
        assert_eq!(
            entries,
            vec![
                Entry::Folder(Key::BuiltIn, ids(&["swofty.core", "tsm.mods"])),
                Entry::Addon("x.a".into()),
                Entry::Folder(seasons(), ids(&["s.one", "s.two"])),
                Entry::Folder(Key::Category(Category::Ai), ids(&["ai.one"])),
                Entry::Addon("x.b".into()),
            ]
        );
        assert_eq!(
            flatten(&entries),
            ids(&[
                "swofty.core",
                "tsm.mods",
                "x.a",
                "s.one",
                "s.two",
                "ai.one",
                "x.b"
            ])
        );
    }

    #[test]
    fn an_addon_dropped_in_a_folder_keeps_the_others_order() {
        let mut entries = group(ids(&["swofty.core", "s.one", "s.two", "x.a"]), place);
        assert_eq!(
            move_addon(&mut entries, "x.a", &Spot::In(seasons(), 1)),
            Some(Some(seasons()))
        );
        assert_eq!(flatten(&entries), ids(&["swofty.core", "s.one", "x.a", "s.two"]));
        assert_eq!(
            move_addon(&mut entries, "s.one", &Spot::In(seasons(), 3)),
            Some(Some(seasons()))
        );
        assert_eq!(flatten(&entries), ids(&["swofty.core", "x.a", "s.two", "s.one"]));
        assert_eq!(move_addon(&mut entries, "s.two", &Spot::Top(0)), Some(None));
        assert_eq!(
            entries,
            vec![
                Entry::Folder(Key::BuiltIn, ids(&["swofty.core"])),
                Entry::Addon("s.two".into()),
                Entry::Folder(seasons(), ids(&["x.a", "s.one"])),
            ],
            "nothing goes above Built In"
        );
    }

    #[test]
    fn moving_a_folder_moves_its_addons() {
        let mut entries = group(ids(&["swofty.core", "s.one", "s.two", "x.a", "ai.one"]), place);
        assert!(move_folder(&mut entries, &seasons(), 4));
        assert_eq!(
            flatten(&entries),
            ids(&["swofty.core", "x.a", "ai.one", "s.one", "s.two"])
        );
        assert!(move_folder(&mut entries, &seasons(), 0));
        assert_eq!(
            flatten(&entries),
            ids(&["swofty.core", "s.one", "s.two", "x.a", "ai.one"])
        );
        assert!(!move_folder(&mut entries, &Key::BuiltIn, 3));
    }

    #[test]
    fn built_in_addons_stay_put() {
        let mut entries = group(ids(&["swofty.core", "tsm.mods", "x.a"]), place);
        assert_eq!(move_addon(&mut entries, "tsm.mods", &Spot::Top(2)), None);
        assert_eq!(
            move_addon(&mut entries, "x.a", &Spot::In(Key::BuiltIn, 0)),
            Some(None)
        );
        assert_eq!(flatten(&entries), ids(&["swofty.core", "tsm.mods", "x.a"]));
    }

    #[test]
    fn arrows_step_within_a_folder_or_the_top_level() {
        let entries = group(ids(&["swofty.core", "x.a", "s.one", "s.two", "x.b"]), place);
        assert_eq!(step(&entries, "x.a", true), None);
        assert_eq!(step(&entries, "x.a", false), Some(Spot::Top(3)));
        assert_eq!(step(&entries, "s.one", true), None);
        assert_eq!(step(&entries, "s.one", false), Some(Spot::In(seasons(), 2)));
        assert_eq!(step(&entries, "s.two", false), None);
        assert_eq!(step(&entries, "x.b", false), None);
        let mut moved = entries.clone();
        move_addon(&mut moved, "s.one", &Spot::In(seasons(), 2));
        assert_eq!(
            flatten(&moved),
            ids(&["swofty.core", "x.a", "s.two", "s.one", "x.b"])
        );
        let mut moved = entries.clone();
        move_addon(&mut moved, "x.a", &Spot::Top(3));
        assert_eq!(
            flatten(&moved),
            ids(&["swofty.core", "s.one", "s.two", "x.a", "x.b"])
        );
    }

    #[test]
    fn arrange_gathers_a_profile_by_folder_and_keeps_switches() {
        let mut p = Profile::new("t");
        for (id, on) in [("x.a", true), ("s.one", false), ("x.b", true), ("s.two", true)] {
            p.set_enabled(id, on).unwrap();
        }
        assert!(arrange(&mut p, place));
        let order: Vec<(&str, bool)> = p.addons.iter().map(|a| (a.id.as_str(), a.enabled)).collect();
        assert_eq!(
            order,
            [
                ("swofty.core", true),
                ("x.a", true),
                ("s.one", false),
                ("s.two", true),
                ("x.b", true)
            ]
        );
        assert!(!arrange(&mut p, place));
    }

    #[test]
    fn reorder_adds_unlisted_addons_off_and_keeps_strays() {
        let mut p = Profile::new("t");
        p.set_enabled("x.a", true).unwrap();
        p.set_enabled("gone.c", true).unwrap();
        reorder(&mut p, &ids(&["swofty.core", "x.new", "x.a"]), &|id| {
            id == "x.new"
        });
        let order: Vec<(&str, bool)> = p.addons.iter().map(|a| (a.id.as_str(), a.enabled)).collect();
        assert_eq!(
            order,
            [
                ("swofty.core", true),
                ("x.new", false),
                ("x.a", true),
                ("gone.c", true)
            ]
        );
    }

    #[test]
    fn places_fall_back_to_the_category_and_forget_removed_folders() {
        let mut f = Folders::default();
        assert_eq!(
            f.place("a", false, Some(Category::Ai)),
            Some(Key::Category(Category::Ai))
        );
        assert_eq!(f.place("a", false, None), None);
        let mine = f.create("Liveries");
        assert_eq!(mine, Key::Own("f1".into()));
        f.set_place("a", Some(&mine), Some(&Key::Category(Category::Ai)));
        f.set_place("b", None, Some(&Key::Category(Category::Cars)));
        f.set_place(
            "c",
            Some(&Key::Category(Category::Cars)),
            Some(&Key::Category(Category::Cars)),
        );
        assert_eq!(f.place("a", false, Some(Category::Ai)), Some(mine.clone()));
        assert_eq!(f.place("b", false, Some(Category::Cars)), None);
        assert!(!f.placed.contains_key("c"), "a default place isn't stored");
        assert_eq!(f.place("x", true, Some(Category::Ai)), Some(Key::BuiltIn));
        f.set_collapsed(&mine, true);
        assert_eq!(f.create("Other"), Key::Own("f2".into()));
        f.remove("f1");
        assert_eq!(
            f.place("a", false, Some(Category::Ai)),
            Some(Key::Category(Category::Ai))
        );
        assert!(f.collapsed.is_empty());
        let json = serde_json::to_string(&f).unwrap();
        assert_eq!(serde_json::from_str::<Folders>(&json).unwrap(), f);
    }

    #[test]
    fn folder_names_are_unique_and_short() {
        let mut f = Folders::default();
        f.create("Liveries");
        assert!(f.check_name("liveries", None).is_err());
        assert!(f.check_name("liveries", Some("f1")).is_ok());
        assert!(f.check_name("Seasons", None).is_err());
        assert!(f.check_name("built in", None).is_err());
        assert!(f.check_name("  ", None).is_err());
        assert!(f.check_name(&"x".repeat(41), None).is_err());
        assert!(f.check_name("2026 grid", None).is_ok());
    }
}
