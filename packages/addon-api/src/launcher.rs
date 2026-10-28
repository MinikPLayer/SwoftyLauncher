use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};

use crate::asset::{AssetBatch, Features};
use crate::manifest::AddonManifest;
use crate::view::{Effect, Event, SectionSpec, View};
use crate::{Error, Result};

pub trait Addon: Send + Sync {
    fn manifest(&self) -> &AddonManifest;

    fn icon(&self) -> Option<&'static [u8]> {
        None
    }

    fn sections(&self) -> Vec<SectionSpec> {
        Vec::new()
    }

    fn view(&self, cx: &Cx, section: &str) -> Result<View> {
        let _ = cx;
        Err(Error::NoSection(section.into()))
    }

    fn event(&self, cx: &Cx, section: &str, event: Event) -> Result<Effect> {
        let _ = (cx, section, event);
        Ok(Effect::None)
    }

    fn tick(&self, cx: &Cx, section: &str) -> Result<bool> {
        let _ = (cx, section);
        Ok(false)
    }

    fn importer(&self) -> Option<&dyn Importer> {
        None
    }

    fn build(&self, cx: &BuildCx) -> Result<AssetBatch> {
        let _ = cx;
        Ok(AssetBatch::default())
    }

    fn commands(&self) -> Vec<CommandSpec> {
        Vec::new()
    }

    fn command(&self, cx: &Cx, path: &[String], args: &[String]) -> Result<String> {
        let _ = (cx, args);
        Err(Error::NoCommand(path.join(" ")))
    }

    fn summary(&self, cx: &Cx) -> Option<Summary> {
        let _ = cx;
        None
    }

    fn migrate(&self, data: &Path, version: u32) -> Result<Vec<String>> {
        let _ = (data, version);
        Ok(Vec::new())
    }
}

pub trait Importer: Send + Sync {
    fn claims(&self, path: &Path) -> Claim;
    fn import(&self, cx: &Cx, paths: &[PathBuf], progress: &dyn Progress) -> Result<Vec<String>>;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Claim {
    No,
    Maybe,
    Yes,
}

pub trait Progress: Send + Sync {
    fn report(&self, done: f32, text: &str);
}

// A live checklist the launcher shows in a dialog while an event runs; each
// snapshot replaces the last.
pub trait Tasks: Send + Sync {
    fn show(&self, list: &TaskList);
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct TaskList {
    pub title: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub subtitle: String,
    #[serde(default)]
    pub items: Vec<TaskItem>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct TaskItem {
    pub id: String,
    pub label: String,
    pub state: TaskState,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub note: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TaskState {
    Waiting,
    Running,
    Done,
    Failed,
    Skipped,
}

pub trait StateStore: Send + Sync {
    fn get(&self, addon: &str) -> Result<Option<serde_json::Value>>;
    fn set(&self, addon: &str, value: serde_json::Value) -> Result<()>;
}

pub trait GameFiles: Send + Sync {
    fn build_key(&self) -> String;
    fn exe_version(&self) -> Option<String>;
    fn is_stock(&self, rel: &str) -> bool;
    fn original(&self, rel: &str) -> Result<PathBuf>;
    fn original_lang(&self, code: &str) -> Result<PathBuf>;

    fn dir(&self) -> Option<PathBuf> {
        None
    }

    /// The folder of addon `addon` in the game, as the game sees it. That is
    /// `dir()/swofty/addons/<addon>` unless the game runs from a mount (an
    /// OverlayFS session on Linux), where the folder is in the mount's view
    /// while the game runs and in the profile's own data otherwise, and an
    /// error says why a live one can't be reached. Ask again for every use:
    /// the answer changes between sessions.
    fn addon_dir(&self, addon: &str) -> Result<Option<PathBuf>> {
        Ok(self.dir().map(|d| d.join("swofty").join("addons").join(addon)))
    }

    fn status(&self) -> GameStatus {
        GameStatus::default()
    }

    fn steam_save_dir(&self) -> Option<PathBuf> {
        None
    }

    fn profile_tracks(&self, profile: &str) -> Result<Option<ProfileTracks>> {
        let _ = profile;
        Ok(None)
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct ProfileTracks {
    pub built: bool,
    #[serde(default)]
    pub buildhash: Option<String>,
    #[serde(default)]
    pub tracks: Vec<ProfileTrack>,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct ProfileTrack {
    pub code: String,
    pub stat_id: u32,
    #[serde(default)]
    pub events: Vec<String>,
    #[serde(default)]
    pub playable: bool,
    #[serde(default)]
    pub modded: bool,
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default)]
    pub addon: Option<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct GameStatus {
    pub running: bool,
    #[serde(default)]
    pub pid: Option<u32>,
    #[serde(default)]
    pub since: Option<f64>,
    #[serde(default)]
    pub session: Option<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct AddonInfo {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub dir: PathBuf,
    pub enabled: bool,
    /// The addon's index in the active profile's addon list; `None` from launchers before 1.3.0.
    #[serde(default)]
    pub position: Option<usize>,
}

// Installed addons and their switch in the active profile, for a plugin that
// manages other addons, such as one that lists the data addons adding a track.
pub trait AddonStore: Send + Sync {
    fn list(&self) -> Result<Vec<AddonInfo>>;
    fn install(&self, path: &Path) -> Result<AddonInfo>;
    fn set_enabled(&self, id: &str, on: bool) -> Result<()>;
    fn remove(&self, id: &str) -> Result<()>;
    /// Moves an addon `delta` places in the active profile's load order; later loads win.
    fn move_addon(&self, _id: &str, _delta: isize) -> Result<()> {
        Err(Error::Other(
            "this launcher can't reorder addons; it needs 1.3.0".into(),
        ))
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ProfileInfo {
    pub name: String,
    pub vanilla: bool,
    pub enabled: bool,
}

pub struct Cx<'a> {
    pub addon: &'a str,
    pub profile: &'a ProfileInfo,
    pub data_dir: &'a Path,
    pub cache_dir: &'a Path,
    pub game: Option<&'a dyn GameFiles>,
    state: &'a dyn StateStore,
    addons: Option<&'a dyn AddonStore>,
    tasks: Option<&'a dyn Tasks>,
}

impl<'a> Cx<'a> {
    pub fn new(
        addon: &'a str,
        profile: &'a ProfileInfo,
        data_dir: &'a Path,
        cache_dir: &'a Path,
        game: Option<&'a dyn GameFiles>,
        state: &'a dyn StateStore,
    ) -> Self {
        Self {
            addon,
            profile,
            data_dir,
            cache_dir,
            game,
            state,
            addons: None,
            tasks: None,
        }
    }

    pub fn with_addons(mut self, addons: &'a dyn AddonStore) -> Self {
        self.addons = Some(addons);
        self
    }

    // None when the launcher doesn't answer the addon host calls (ADDON_CALLS).
    pub fn addons(&self) -> Option<&'a dyn AddonStore> {
        self.addons
    }

    pub fn with_tasks(mut self, tasks: &'a dyn Tasks) -> Self {
        self.tasks = Some(tasks);
        self
    }

    // False when nothing shows task lists here, such as a launcher before 1.3.0.
    pub fn tasks(&self, list: &TaskList) -> bool {
        self.tasks.inspect(|t| t.show(list)).is_some()
    }

    pub fn state<T: DeserializeOwned + Default>(&self) -> Result<T> {
        match self.state.get(self.addon)? {
            Some(value) => {
                serde_json::from_value(value).map_err(|e| Error::Other(format!("{} state: {e}", self.addon)))
            }
            None => Ok(T::default()),
        }
    }

    pub fn set_state<T: Serialize>(&self, value: &T) -> Result<()> {
        let value =
            serde_json::to_value(value).map_err(|e| Error::Other(format!("{} state: {e}", self.addon)))?;
        self.state.set(self.addon, value)
    }
}

pub struct BuildCx<'a> {
    pub cx: Cx<'a>,
    pub options: &'a BTreeMap<String, serde_json::Value>,
    pub features: &'a Features,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Summary {
    pub label: String,
    pub items: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CommandSpec {
    pub name: String,
    pub about: String,
    #[serde(default)]
    pub args: Vec<ArgSpec>,
    #[serde(default)]
    pub subcommands: Vec<CommandSpec>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ArgSpec {
    pub name: String,
    pub help: String,
    pub required: bool,
    pub many: bool,
}

#[derive(Clone)]
pub struct Builtin {
    pub addon: Arc<dyn Addon>,
    pub locked: bool,
    pub default_on: bool,
}

impl std::fmt::Debug for Builtin {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Builtin")
            .field("id", &self.addon.manifest().id)
            .field("locked", &self.locked)
            .field("default_on", &self.default_on)
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Mutex;

    use super::*;

    #[derive(Default)]
    struct Memory(Mutex<BTreeMap<String, serde_json::Value>>);

    impl StateStore for Memory {
        fn get(&self, addon: &str) -> Result<Option<serde_json::Value>> {
            Ok(self.0.lock().unwrap().get(addon).cloned())
        }

        fn set(&self, addon: &str, value: serde_json::Value) -> Result<()> {
            self.0.lock().unwrap().insert(addon.into(), value);
            Ok(())
        }
    }

    #[derive(Debug, Default, PartialEq, Serialize, Deserialize)]
    struct Order {
        mods: Vec<String>,
    }

    #[test]
    fn state_defaults_until_set() {
        let store = Memory::default();
        let profile = ProfileInfo {
            name: "default".into(),
            vanilla: false,
            enabled: true,
        };
        let cx = Cx::new("a.b", &profile, Path::new("/d"), Path::new("/c"), None, &store);
        assert_eq!(cx.state::<Order>().unwrap(), Order::default());
        let order = Order {
            mods: vec!["x".into()],
        };
        cx.set_state(&order).unwrap();
        assert_eq!(cx.state::<Order>().unwrap(), order);
        store.set("a.b", serde_json::json!({ "mods": 3 })).unwrap();
        assert!(cx.state::<Order>().is_err());
    }

    #[derive(Default)]
    struct Shown(Mutex<Vec<TaskList>>);

    impl Tasks for Shown {
        fn show(&self, list: &TaskList) {
            self.0.lock().unwrap().push(list.clone());
        }
    }

    #[test]
    fn tasks_reach_the_launcher_only_when_it_shows_them() {
        let (store, shown) = (Memory::default(), Shown::default());
        let profile = ProfileInfo {
            name: "default".into(),
            vanilla: false,
            enabled: true,
        };
        let list = TaskList {
            title: "Migrating".into(),
            subtitle: String::new(),
            items: vec![TaskItem {
                id: "a".into(),
                label: "A".into(),
                state: TaskState::Running,
                note: None,
            }],
        };
        let cx = Cx::new("a.b", &profile, Path::new("/d"), Path::new("/c"), None, &store);
        assert!(!cx.tasks(&list));
        assert!(cx.with_tasks(&shown).tasks(&list));
        assert_eq!(*shown.0.lock().unwrap(), [list]);
    }

    #[test]
    fn addon_info_reads_launchers_without_a_position() {
        let old = serde_json::json!({"id": "a.b", "name": "B", "version": "1.0.0", "author": "t", "dir": "/a/b", "enabled": true});
        let info: AddonInfo = serde_json::from_value(old).unwrap();
        assert_eq!(
            (info.id.as_str(), info.enabled, info.position),
            ("a.b", true, None)
        );
        let placed = AddonInfo {
            position: Some(2),
            ..info
        };
        let json = serde_json::to_value(&placed).unwrap();
        assert_eq!(json["position"], 2);
        assert_eq!(serde_json::from_value::<AddonInfo>(json).unwrap(), placed);
        let off = serde_json::to_value(AddonInfo::default()).unwrap();
        assert!(off["position"].is_null());
    }
}
