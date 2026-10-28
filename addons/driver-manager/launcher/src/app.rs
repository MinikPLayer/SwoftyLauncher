use std::path::PathBuf;

use serde_json::{Value, json};

use crate::assets::Files;
use crate::game::{self, Choice, GameData};
use crate::host::Host;
use crate::loader::{self, Loading};
use crate::model::{Driver, Roster};
use crate::preview::{self, Building, Previews};

pub const PROFILE_HEADS: &str = "profile_heads";

pub struct Edit {
    pub draft: Driver,
    pub form: String,
    pub tried: bool,
}

pub struct App {
    pub data_dir: PathBuf,
    pub cache_dir: PathBuf,
    pub vanilla: bool,
    pub enabled: bool,
    pub game: Option<GameData>,
    pub game_error: Option<String>,
    pub loading: Option<Loading>,
    pub edit: Option<Edit>,
    pub bulk: Option<String>,
    pub forms: u32,
    pub game_dir: Option<PathBuf>,
    pub previews: Option<Previews>,
    pub building: Option<Building>,
    pub previews_tried: bool,
    pub files: Files,
    pub stock: Option<crate::stock::Editing>,
    pub host_calls: Vec<String>,
    pub added_heads: Vec<Choice>,
}

impl App {
    pub fn new(params: &Value) -> App {
        let path = |k: &str| PathBuf::from(params[k].as_str().unwrap_or_default());
        App {
            data_dir: path("data_dir"),
            cache_dir: path("cache_dir"),
            vanilla: params["profile"]["vanilla"].as_bool().unwrap_or(false),
            enabled: params["profile"]["enabled"].as_bool().unwrap_or(true),
            game: None,
            game_error: None,
            loading: None,
            edit: None,
            bulk: None,
            forms: 0,
            game_dir: None,
            previews: None,
            building: None,
            previews_tried: false,
            files: Files::default(),
            stock: None,
            host_calls: params["host_calls"]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(|c| c.as_str().map(str::to_owned))
                .collect(),
            added_heads: Vec::new(),
        }
    }

    pub fn roster(&self) -> Result<Roster, String> {
        Roster::load(&self.data_dir)
    }

    pub fn save(&self, roster: &Roster) -> Result<(), String> {
        roster.save(&self.data_dir)
    }

    pub fn next_form(&mut self, base: &str) -> String {
        self.forms += 1;
        format!("{base}_{}", self.forms)
    }

    fn string(host: &mut Host, method: &str, params: Value) -> Result<Option<String>, String> {
        host.call(method, params).map(|v| v.as_str().map(str::to_owned))
    }

    pub fn ensure_game(&mut self, host: &mut Host) {
        self.poll();
        if self.game.is_some() || self.loading.is_some() || self.game_error.is_some() {
            return;
        }
        let key = match Self::string(host, "game_build_key", Value::Null) {
            Ok(Some(k)) => k,
            Ok(None) => {
                self.game_error =
                    Some("The launcher hasn't found F1 25. Locate the game on the Library page.".into());
                return;
            }
            Err(e) => {
                self.game_error = Some(e);
                return;
            }
        };
        let cache = loader::cache_path(&self.cache_dir);
        if let Some(data) = GameData::load_cached(&cache, &key) {
            self.game = Some(data);
            return;
        }
        let db = match Self::string(host, "game_original", json!({ "rel": game::DB_REL })) {
            Ok(Some(p)) => PathBuf::from(p),
            Ok(None) => {
                self.game_error = Some("The launcher returned no game database.".into());
                return;
            }
            Err(e) => {
                self.game_error = Some(format!("The game database isn't available: {e}"));
                return;
            }
        };
        let dir = Self::string(host, "game_dir", Value::Null)
            .ok()
            .flatten()
            .map(PathBuf::from);
        self.game_dir.clone_from(&dir);
        self.loading = Some(Loading::start(db, dir, key, cache));
    }

    pub fn refresh_files(&mut self, host: &mut Host) {
        if self.game_dir.is_none() {
            self.game_dir = Self::string(host, "game_dir", Value::Null)
                .ok()
                .flatten()
                .map(PathBuf::from);
        }
        let addons = host.call("addons", Value::Null).unwrap_or(Value::Null);
        self.files = Files::new(self.game_dir.clone(), &addons);
        self.added_heads = self.profile_heads(host);
        self.apply_files();
    }

    fn profile_heads(&self, host: &mut Host) -> Vec<Choice> {
        if !self.host_calls.iter().any(|c| c == PROFILE_HEADS) {
            return Vec::new();
        }
        let Ok(reply) = host.call(PROFILE_HEADS, Value::Null) else {
            return Vec::new();
        };
        heads_from(&reply)
    }

    fn apply_files(&mut self) {
        if let Some(game) = self.game.as_mut() {
            game.add_heads(&self.added_heads);
            preview::mark_missing(game, &self.files);
        }
    }

    pub fn poll(&mut self) -> bool {
        let mut changed = false;
        if let Some(done) = self.building.as_ref().and_then(Building::poll) {
            self.building = None;
            if let Some(p) = &done {
                eprintln!(
                    "previews: {} built, {} without art, {} ms, {} KiB",
                    p.index.ready.len(),
                    p.index.failed.len(),
                    p.index.millis,
                    p.index.bytes / 1024
                );
            }
            self.previews = done;
            changed = true;
        }
        let Some(result) = self.loading.as_ref().and_then(Loading::poll) else {
            return changed;
        };
        self.loading = None;
        match result {
            Ok(g) => self.game = Some(g),
            Err(e) => self.game_error = Some(e),
        }
        self.apply_files();
        true
    }

    pub fn busy(&self) -> bool {
        self.loading.is_some() || self.building.is_some()
    }

    pub fn ensure_previews(&mut self, host: &mut Host) {
        if self.game.is_none() || self.building.is_some() {
            return;
        }
        if self.game_dir.is_none() {
            self.refresh_files(host);
        }
        let Some(game) = &self.game else {
            return;
        };
        if self.game_dir.is_none() {
            return;
        }
        let root = self.cache_dir.join("previews");
        if !self.previews_tried {
            self.previews_tried = true;
            self.previews = Previews::load(&root, &game.key);
            if self.previews.is_none() {
                let jobs = preview::jobs(game);
                self.building = Some(Building::start(
                    self.files.clone(),
                    root,
                    game.key.clone(),
                    jobs,
                    None,
                ));
                return;
            }
        }
        let Some(p) = &self.previews else {
            return;
        };
        let jobs = preview::missing_jobs(game, &p.index);
        if !jobs.is_empty() {
            let before = Some(p.index.clone());
            self.building = Some(Building::start(
                self.files.clone(),
                root,
                game.key.clone(),
                jobs,
                before,
            ));
        }
    }

    pub fn thumb(&self, list: game::List, value: &str) -> Option<String> {
        self.previews
            .as_ref()?
            .path(list, value)
            .map(|p| p.to_string_lossy().into_owned())
    }

    pub fn large(&mut self, list: game::List, value: &str) -> Option<String> {
        let (Some(previews), Some(game)) = (self.previews.as_mut(), &self.game) else {
            return None;
        };
        previews
            .large(&self.files, game, list, value)
            .map(|p| p.to_string_lossy().into_owned())
    }

    pub fn wait_game(&mut self, host: &mut Host) {
        self.ensure_game(host);
        if let Some(l) = self.loading.take() {
            match l.wait() {
                Ok(g) => self.game = Some(g),
                Err(e) => self.game_error = Some(e),
            }
        }
    }

    pub fn retry_game(&mut self) {
        if self.loading.is_none() {
            self.game_error = None;
        }
        if self.game.is_none() {
            self.previews_tried = false;
        }
    }
}

pub fn heads_from(reply: &Value) -> Vec<Choice> {
    reply["heads"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|h| {
            let value = h["label"].as_str().filter(|l| !l.is_empty())?.to_owned();
            let label = h["name"]
                .as_str()
                .filter(|n| !n.trim().is_empty())
                .map_or_else(|| game::title_case(&value), str::to_owned);
            Some(Choice { value, label })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn choice(value: &str) -> Choice {
        Choice {
            value: value.into(),
            label: value.into(),
        }
    }

    #[test]
    fn heads_the_profile_adds_join_the_list_once_and_go_when_they_leave() {
        let reply = json!({ "heads": [
            { "label": "rio_haryanto", "addon": "mod.rio" },
            { "label": "lando_norris", "name": "Lando" },
            { "label": "" },
        ]});
        let added = heads_from(&reply);
        assert_eq!(added[0].label, "Rio Haryanto");
        assert_eq!(added.len(), 2);

        let mut g = GameData {
            heads: vec![choice("male_ls1"), choice("lando_norris")],
            ..GameData::default()
        };
        g.add_heads(&added);
        g.add_heads(&added);
        let values: Vec<&str> = g.heads.iter().map(|c| c.value.as_str()).collect();
        assert_eq!(values, ["male_ls1", "lando_norris", "rio_haryanto"]);
        assert!(g.has(game::List::Head, "rio_haryanto"));

        g.add_heads(&[]);
        assert!(!g.has(game::List::Head, "rio_haryanto"));
        assert!(g.has(game::List::Head, "lando_norris"));
    }

    #[test]
    fn asks_for_profile_heads_only_when_the_launcher_answers_it() {
        let old = App::new(&json!({ "data_dir": "/d", "cache_dir": "/c", "host_calls": ["tasks"] }));
        assert!(!old.host_calls.iter().any(|c| c == PROFILE_HEADS));
        let new = App::new(&json!({ "host_calls": ["tasks", PROFILE_HEADS] }));
        assert!(new.host_calls.iter().any(|c| c == PROFILE_HEADS));
    }
}
