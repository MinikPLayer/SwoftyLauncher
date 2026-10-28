use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::path::Path;

use serde::{Deserialize, Serialize};
use swofty_formats::gvdb::Gvdb;
use swofty_formats::hash::fnv1a64;
use swofty_formats::lng::Lng;

pub const DB_REL: &str = "game_package/gamedatabases/gamedatabases.2025.bin";
const CACHE_SCHEMA: u32 = 3;

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Choice {
    pub value: String,
    pub label: String,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct StockLook {
    pub folder: String,
    pub label: String,
    pub helmet: String,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct StockDriver {
    pub label: String,
    pub name: String,
    pub code: String,
    pub number: u32,
    pub stat_id: u32,
    #[serde(default)]
    pub head: String,
    #[serde(default)]
    pub looks: Vec<StockLook>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Art {
    pub source: String,
    pub colours: u32,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct GameData {
    pub schema: u32,
    pub key: String,
    pub named: bool,
    pub nationalities: Vec<Choice>,
    pub heads: Vec<Choice>,
    pub helmets: Vec<Choice>,
    pub suits: Vec<Choice>,
    pub gloves: Vec<Choice>,
    pub poses: Vec<Choice>,
    pub emotes: Vec<Choice>,
    pub voices: Vec<Choice>,
    pub stock: Vec<StockDriver>,
    pub driver_ids: BTreeSet<u32>,
    pub preset_ids: BTreeSet<u32>,
    pub labels: BTreeSet<String>,
    #[serde(default)]
    pub art: BTreeMap<String, Art>,
    #[serde(skip)]
    pub missing: BTreeSet<String>,
    #[serde(skip)]
    pub supplied: BTreeMap<String, String>,
    #[serde(skip)]
    pub added_heads: BTreeSet<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum List {
    Nationality,
    Head,
    Helmet,
    Suit,
    Gloves,
    Pose,
    Emote,
    Voice,
}

impl List {
    pub const LOOKS: [List; 6] = [
        List::Head,
        List::Helmet,
        List::Suit,
        List::Gloves,
        List::Pose,
        List::Emote,
    ];

    pub fn slug(self) -> &'static str {
        match self {
            List::Nationality => "nationality",
            List::Head => "head",
            List::Helmet => "helmet",
            List::Suit => "suit",
            List::Gloves => "gloves",
            List::Pose => "pose",
            List::Emote => "emote",
            List::Voice => "voice",
        }
    }

    pub fn key(self, value: &str) -> String {
        format!("{}/{value}", self.slug())
    }
}

impl GameData {
    pub fn art(&self, list: List, value: &str) -> Option<&Art> {
        self.art.get(&list.key(value))
    }

    pub fn installed(&self, list: List, value: &str) -> bool {
        !self.missing.contains(&list.key(value))
    }

    pub fn available(&self, list: List, value: &str) -> bool {
        self.has(list, value) && self.installed(list, value)
    }

    pub fn choices(&self, list: List) -> impl Iterator<Item = &Choice> {
        self.list(list)
            .iter()
            .filter(move |c| self.installed(list, &c.value))
    }

    pub fn list(&self, list: List) -> &[Choice] {
        match list {
            List::Nationality => &self.nationalities,
            List::Head => &self.heads,
            List::Helmet => &self.helmets,
            List::Suit => &self.suits,
            List::Gloves => &self.gloves,
            List::Pose => &self.poses,
            List::Emote => &self.emotes,
            List::Voice => &self.voices,
        }
    }

    pub fn has(&self, list: List, value: &str) -> bool {
        self.list(list).iter().any(|c| c.value == value)
    }

    pub fn label(&self, list: List, value: &str) -> String {
        let label = self
            .list(list)
            .iter()
            .find(|c| c.value == value)
            .map_or_else(|| value.to_owned(), |c| c.label.clone());
        match self.supplied.get(&list.key(value)) {
            Some(addon) => format!("{label} ({addon})"),
            None => label,
        }
    }

    pub fn add_heads(&mut self, heads: &[Choice]) {
        let before = std::mem::take(&mut self.added_heads);
        self.heads.retain(|c| !before.contains(&c.value));
        let mut seen = BTreeSet::new();
        let mut added: Vec<Choice> = heads
            .iter()
            .filter(|h| !self.has(List::Head, &h.value) && seen.insert(h.value.clone()))
            .cloned()
            .collect();
        added.sort_by_key(|c| c.label.to_lowercase());
        self.added_heads = seen;
        self.heads.extend(added);
    }

    pub fn stock_driver(&self, label: &str) -> Option<&StockDriver> {
        self.stock.iter().find(|s| s.label == label)
    }

    pub fn load_cached(path: &Path, key: &str) -> Option<GameData> {
        let data: GameData = serde_json::from_slice(&std::fs::read(path).ok()?).ok()?;
        (data.schema == CACHE_SCHEMA && data.key == key).then_some(data)
    }

    pub fn save_cached(&self, path: &Path) {
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir).ok();
        }
        if let Ok(bytes) = serde_json::to_vec(self) {
            let tmp = path.with_extension("tmp");
            if std::fs::write(&tmp, bytes).is_ok() {
                std::fs::rename(&tmp, path).ok();
            }
        }
    }
}

pub struct Db<'a> {
    db: &'a Gvdb,
    index: HashMap<u64, usize>,
}

impl<'a> Db<'a> {
    pub fn new(db: &'a Gvdb) -> Self {
        let index = db.records.iter().enumerate().map(|(i, r)| (r.key, i)).collect();
        Db { db, index }
    }

    pub fn text(&self, path: &str) -> Option<String> {
        let i = *self.index.get(&fnv1a64(path))?;
        Some(self.db.records[i].text().into_owned())
    }
}

fn rows<'a>(db: &'a Db, container: &'a str, row: &'a str) -> impl Iterator<Item = String> + 'a {
    let n = db
        .text(&format!("{container}._{row}_instancecount_"))
        .and_then(|t| t.trim().parse::<usize>().ok())
        .unwrap_or(0);
    (0..n).map(move |i| format!("{container}.{row}_{i}"))
}

fn field(db: &Db, row: &str, name: &str) -> String {
    db.text(&format!("{row}.{name}")).unwrap_or_default()
}

fn int(db: &Db, row: &str, name: &str) -> Option<u32> {
    field(db, row, name).trim().parse::<f64>().ok().map(|v| v as u32)
}

pub fn title_case(s: &str) -> String {
    s.split(['_', ' ', '-'])
        .filter(|w| !w.is_empty())
        .map(|w| {
            let mut c = w.chars();
            match c.next() {
                Some(f) => f.to_uppercase().collect::<String>() + c.as_str(),
                None => String::new(),
            }
        })
        .collect::<Vec<_>>()
        .join(" ")
}

struct Names<'a>(Option<&'a Lng>);

impl Names<'_> {
    fn get(&self, key: &str) -> Option<String> {
        let text = self.0?.get(key)?.trim();
        (!text.is_empty()).then(|| clean(text))
    }
}

fn clean(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut tag = false;
    for c in text.chars() {
        match c {
            '<' => tag = true,
            '>' if tag => tag = false,
            _ if !tag => out.push(c),
            _ => {}
        }
    }
    out.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn disambiguate(mut list: Vec<Choice>) -> Vec<Choice> {
    let mut seen: BTreeMap<String, usize> = BTreeMap::new();
    for c in &list {
        *seen.entry(c.label.to_lowercase()).or_default() += 1;
    }
    for c in &mut list {
        if seen[&c.label.to_lowercase()] > 1 {
            c.label = format!("{} · {}", c.label, c.value);
        }
    }
    list.sort_by_key(|c| c.label.to_lowercase());
    list
}

fn kit(
    db: &Db,
    names: &Names,
    art: &mut BTreeMap<String, Art>,
    list: List,
    table: &str,
    row: &str,
) -> Vec<Choice> {
    let source_field = match list {
        List::Pose | List::Emote => "texture_name",
        _ => "asset_group",
    };
    let choices = rows(db, table, row)
        .filter_map(|r| {
            let value = field(db, &r, "_metadata_");
            if value.is_empty() {
                return None;
            }
            art.insert(
                list.key(&value),
                Art {
                    source: field(db, &r, source_field),
                    colours: int(db, &r, "nb_colours").unwrap_or(0),
                },
            );
            let label = match names.get(&field(db, &r, "name_loc_tag")) {
                Some(text) if text.chars().count() > 2 => text,
                Some(text) => format!("{text} ({value})"),
                None => title_case(&value),
            };
            Some(Choice { value, label })
        })
        .collect();
    disambiguate(choices)
}

pub fn extract(db: &Gvdb, lng: Option<&Lng>, key: &str) -> GameData {
    let db = &Db::new(db);
    let names = Names(lng);
    let mut data = GameData {
        schema: CACHE_SCHEMA,
        key: key.to_owned(),
        named: lng.is_some(),
        ..GameData::default()
    };

    let mut driver_names: BTreeMap<String, String> = BTreeMap::new();
    for r in rows(db, "entrants.drivers", "driver") {
        let label = field(db, &r, "_metadata_");
        let name = names
            .get(&field(db, &r, "full_name"))
            .unwrap_or_else(|| title_case(&label));
        if let Some(id) = int(db, &r, "stat_id") {
            data.driver_ids.insert(id);
            data.stock.push(StockDriver {
                label: label.clone(),
                name: name.clone(),
                code: field(db, &r, "initials"),
                number: int(db, &r, "car_number").unwrap_or(0),
                stat_id: id,
                head: field(db, &r, "head"),
                looks: Vec::new(),
            });
        }
        data.labels.insert(label.clone());
        driver_names.insert(label, name);
    }

    data.nationalities = disambiguate(
        rows(db, "entrants.nationality", "nationality")
            .filter_map(|r| {
                let value = field(db, &r, "_metadata_");
                (!value.is_empty() && value != "default").then(|| Choice {
                    label: names
                        .get(&field(db, &r, "language_string"))
                        .unwrap_or_else(|| title_case(&value)),
                    value,
                })
            })
            .collect(),
    );

    let mut generic = Vec::new();
    let mut looks = Vec::new();
    for r in rows(db, "entrants.animation.driver_heads", "driver_head") {
        let value = field(db, &r, "_metadata_");
        if value.is_empty() {
            continue;
        }
        let female = field(db, &r, "body_type").contains("female");
        let generic_num = value
            .strip_prefix("male_ls")
            .or_else(|| value.strip_prefix("female_ls"))
            .map(str::to_owned);
        if let Some(num) = generic_num {
            let kind = if female { "female" } else { "male" };
            generic.push(Choice {
                label: format!("Generic {kind} {num}"),
                value,
            });
            continue;
        }
        let who = driver_names
            .get(&value)
            .cloned()
            .or_else(|| names.get(&field(db, &r, "esports_name")))
            .unwrap_or_else(|| title_case(&value));
        looks.push(Choice {
            label: format!("Looks like {who}"),
            value,
        });
    }
    generic.sort_by_key(|c| natural(&c.label));
    let looks = disambiguate(looks);
    data.heads = generic.into_iter().chain(looks).collect();

    let art = &mut data.art;
    data.helmets = kit(
        db,
        &names,
        art,
        List::Helmet,
        "customisation.custom_avatar.helmets",
        "Helmet",
    );
    data.suits = kit(
        db,
        &names,
        art,
        List::Suit,
        "customisation.custom_avatar.suits",
        "Suit",
    );
    data.gloves = kit(
        db,
        &names,
        art,
        List::Gloves,
        "customisation.custom_avatar.gloves",
        "Gloves",
    );
    let mut poses = kit(
        db,
        &names,
        art,
        List::Pose,
        "customisation.custom_avatar.poses",
        "Pose",
    );
    poses.sort_by_key(|c| natural(&c.value));
    data.poses = poses;
    let mut emotes = kit(
        db,
        &names,
        art,
        List::Emote,
        "customisation.custom_avatar.podium_emotes",
        "PodiumEmote",
    );
    emotes.sort_by_key(|c| natural(&c.value));
    data.emotes = emotes;

    let mut voices = Vec::new();
    for r in rows(db, "entrants.audio_names", "audio_name") {
        let value = field(db, &r, "_metadata_");
        let generic = field(db, &r, "selection_type") == "custom_standard"
            && !value.to_lowercase().contains("disabled");
        if value == "DefaultName" {
            voices.push(Choice {
                value,
                label: "Default (no surname)".into(),
            });
        } else if generic {
            let label = names
                .get(&field(db, &r, "language_string"))
                .unwrap_or_else(|| value.clone());
            voices.push(Choice { value, label });
        }
    }
    let (default, rest): (Vec<Choice>, Vec<Choice>) =
        voices.into_iter().partition(|c| c.value == "DefaultName");
    data.voices = default.into_iter().chain(disambiguate(rest)).collect();

    let at: HashMap<String, usize> = data
        .stock
        .iter()
        .enumerate()
        .map(|(i, s)| (s.label.clone(), i))
        .collect();
    for folder in rows(db, "entrants.driver_customisations", "folder") {
        let folder_label = field(db, &folder, "_metadata_");
        for r in rows(db, &folder, "preloaded_driver_customisation_properties") {
            if let Some(id) = int(db, &r, "stat_id") {
                data.preset_ids.insert(id);
            }
            if let Some(&i) = at.get(&field(db, &r, "linked_driver_stat_id")) {
                data.stock[i].looks.push(StockLook {
                    folder: folder_label.clone(),
                    label: field(db, &r, "_metadata_"),
                    helmet: field(db, &r, "helmet"),
                });
            }
        }
    }
    data
}

fn natural(s: &str) -> (String, u64) {
    let digits: String = s.chars().rev().take_while(char::is_ascii_digit).collect();
    let n = digits.chars().rev().collect::<String>().parse().unwrap_or(0);
    (s[..s.len() - digits.len()].to_lowercase(), n)
}
