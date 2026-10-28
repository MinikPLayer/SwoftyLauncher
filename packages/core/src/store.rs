use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, MutexGuard};

use swofty_addon::launcher::Builtin;

use crate::builtin::{CORE_ID, core_manifest};
use crate::helper::Helpers;
use crate::manifest::{AddonManifest, MANIFEST_FILE};
use crate::profile::{self, Profile};
use crate::session::Config;
use crate::settings::{self, Merge, Requests, SessionSettings};
use crate::{Error, Result, io_err};

pub const DEFAULT_PROFILE: &str = "default";
pub const DATA_VERSION: u32 = 3;
const LEGACY_ADDONS_DIR: &str = "mods";

static PROFILE_WRITE: Mutex<()> = Mutex::new(());

fn profile_write() -> MutexGuard<'static, ()> {
    PROFILE_WRITE.lock().unwrap_or_else(|e| e.into_inner())
}

#[derive(Debug, Clone)]
pub struct Store {
    pub root: PathBuf,
    builtins: Arc<[Builtin]>,
    helpers: Arc<Helpers>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ProfileDeletion {
    pub name: String,
    pub switch_to: Option<String>,
    pub saves: bool,
    pub paths: Vec<PathBuf>,
}

impl Store {
    pub fn new(root: impl Into<PathBuf>) -> Self {
        Self {
            root: root.into(),
            builtins: Arc::new([]),
            helpers: Arc::default(),
        }
    }

    pub fn with_builtins(mut self, builtins: Vec<Builtin>) -> Self {
        self.builtins = builtins.into();
        self
    }

    pub fn builtins(&self) -> &[Builtin] {
        &self.builtins
    }

    pub fn helpers(&self) -> &Helpers {
        &self.helpers
    }

    pub fn registry(&self) -> Result<crate::registry::Registry> {
        crate::registry::Registry::load(self)
    }

    pub fn open_default() -> Self {
        Self::new(crate::paths::data_dir())
    }

    pub fn addons_dir(&self) -> PathBuf {
        self.root.join("addons")
    }

    pub fn addon_data_dir(&self, id: &str) -> PathBuf {
        self.root.join("addon-data").join(id)
    }

    pub fn addon_cache_dir(&self, id: &str) -> PathBuf {
        self.root.join("cache/addons").join(id)
    }

    pub fn profiles_dir(&self) -> PathBuf {
        self.root.join("profiles")
    }

    pub fn overlays_dir(&self, profile: &str) -> PathBuf {
        self.root.join("cache/overlay").join(profile)
    }

    pub fn overlay_dir(&self, profile: &str, buildhash: &str) -> PathBuf {
        self.overlays_dir(profile).join(buildhash)
    }

    /// Everything an OverlayFS session keeps for one profile: its mod files
    /// layer, the upper dir the game writes into, and the work dir.
    pub fn profile_dir(&self, profile: &str) -> PathBuf {
        self.profiles_dir().join(profile)
    }

    pub fn profile_layer_dir(&self, profile: &str) -> PathBuf {
        self.profile_dir(profile).join("overlay")
    }

    pub fn overlay_upper_dir(&self, profile: &str) -> PathBuf {
        self.profile_dir(profile).join("writes")
    }

    pub fn overlay_work_dir(&self, profile: &str) -> PathBuf {
        self.profile_dir(profile).join("workdir")
    }

    /// The layer every profile shares: the offline stub and hook, and with
    /// the loader, `version.dll` and the loader folder. A session without
    /// the loader has a layer of its own, so switching to a profile that
    /// plays without addons doesn't rebuild the other.
    pub fn overlay_common_dir(&self, loader: bool) -> PathBuf {
        self.root
            .join("overlay")
            .join(if loader { "common" } else { "common-stub" })
    }

    pub fn overlay_state_dir(&self) -> PathBuf {
        self.root.join("state/overlay")
    }

    /// Where the OverlayFS session mounts the game: an empty folder on the
    /// host, never the game folder, and free of spaces like `state/game-link`.
    pub fn overlay_target(&self) -> PathBuf {
        self.overlay_state_dir().join("game")
    }

    /// The game's logs, collected from the upper dir after an OverlayFS session.
    pub fn logs_dir(&self) -> PathBuf {
        self.root.join("logs")
    }

    pub fn saves_dir(&self, profile: &str) -> PathBuf {
        self.root.join("saves").join(profile)
    }

    pub fn baseline_dir(&self, build: &str) -> PathBuf {
        self.root.join("baseline").join(build)
    }

    pub fn journal_dir(&self) -> PathBuf {
        self.root.join("state/journal")
    }

    pub fn requests_dir(&self) -> PathBuf {
        self.root.join("state/requests")
    }

    pub fn launch_request_path(&self) -> PathBuf {
        self.root.join("state/launch.json")
    }

    pub fn sessions_dir(&self) -> PathBuf {
        self.root.join("sessions")
    }

    pub fn config_path(&self) -> PathBuf {
        self.root.join("config.json")
    }

    pub fn bin_dir(&self) -> PathBuf {
        self.root.join("bin")
    }

    pub fn migrate(&self) -> Result<Vec<String>> {
        let mut notes = Vec::new();
        if !self.root.is_dir() {
            return Ok(notes);
        }
        let config: Result<Config> = match crate::load_json(&self.config_path()) {
            Ok(crate::Loaded::Found(c)) => Ok(c),
            Ok(crate::Loaded::Missing | crate::Loaded::Damaged(_)) => Ok(Config::default()),
            Err(e) => Err(e),
        };
        if config.as_ref().is_ok_and(|c| c.data_version >= DATA_VERSION) {
            return Ok(notes);
        }
        let old = self.root.join(LEGACY_ADDONS_DIR);
        let new = self.addons_dir();
        if old.is_dir() && !new.exists() {
            std::fs::rename(&old, &new).map_err(io_err(&old))?;
            notes.push(format!("moved {} to {}", old.display(), new.display()));
        } else if old.is_dir() {
            let taken: BTreeSet<String> = subdirs(&new)?.iter().filter_map(|d| manifest_id(d)).collect();
            for dir in subdirs(&old)? {
                let to = new.join(dir.file_name().expect("has a name"));
                match manifest_id(&dir) {
                    Some(id) if !taken.contains(&id) && !to.exists() => {
                        std::fs::rename(&dir, &to).map_err(io_err(&dir))?;
                        notes.push(format!("moved {} to {}", dir.display(), to.display()));
                    }
                    Some(id) => notes.push(format!(
                        "kept {}: {} already has {id}",
                        dir.display(),
                        new.display()
                    )),
                    None => notes.push(format!(
                        "kept {}: no addon id in its {MANIFEST_FILE}",
                        dir.display()
                    )),
                }
            }
            if std::fs::remove_dir(&old).is_ok() {
                notes.push(format!("removed the empty {}", old.display()));
            }
        }
        let from = config.as_ref().map_or(0, |c| c.data_version);
        for b in self.builtins.iter() {
            let id = &b.addon.manifest().id;
            match b.addon.migrate(&self.root, from) {
                Ok(n) => notes.extend(n.into_iter().map(|n| format!("{id}: {n}"))),
                Err(e) => return Err(Error::Other(format!("migrating {id}: {e}"))),
            }
        }
        match config {
            Ok(mut config) => {
                config.data_version = DATA_VERSION;
                self.save_config(&config)?;
            }
            Err(e) => notes.push(format!("data_version not recorded: {e}")),
        }
        Ok(notes)
    }

    /// The installed addons this launcher can read. One it can't is left out with a warning;
    /// [`unreadable_addons`](Self::unreadable_addons) lists them.
    pub fn addons(&self) -> Result<BTreeMap<String, AddonManifest>> {
        Ok(self.scan_addons()?.0)
    }

    /// Installed addons this launcher can't read: made for a newer launcher, or damaged.
    pub fn unreadable_addons(&self) -> Vec<Unreadable> {
        self.scan_addons().map(|(_, bad)| bad).unwrap_or_default()
    }

    fn scan_addons(&self) -> Result<(BTreeMap<String, AddonManifest>, Vec<Unreadable>)> {
        let mut out = BTreeMap::from([(CORE_ID.to_owned(), core_manifest())]);
        for b in self.builtins.iter() {
            let m = b.addon.manifest();
            out.entry(m.id.clone()).or_insert_with(|| m.clone());
        }
        let mut bad = Vec::new();
        for dir in subdirs(&self.addons_dir())? {
            if !dir.join(MANIFEST_FILE).is_file() {
                continue;
            }
            let m = match AddonManifest::load(&dir) {
                Ok(m) => m,
                Err(e) => {
                    let u = Unreadable::of(&dir, &e, &crate::update::current_version());
                    warn_once(format!("skipped addon {}: it {}", u.id, u.reason));
                    bad.push(u);
                    continue;
                }
            };
            if self.is_builtin(&m.id) {
                continue;
            }
            out.insert(m.id.clone(), m);
        }
        Ok((out, bad))
    }

    pub fn is_builtin(&self, id: &str) -> bool {
        id == CORE_ID || self.builtins.iter().any(|b| b.addon.manifest().id == id)
    }

    fn adopt_builtins(&self, p: &mut Profile) {
        if p.is_vanilla() {
            return;
        }
        for b in self.builtins.iter() {
            let id = &b.addon.manifest().id;
            let listed = p.addons.iter().any(|a| &a.id == id);
            if b.locked || (b.default_on && !listed) {
                p.set_enabled(id, true).expect("not vanilla");
            }
        }
        if self
            .addons_dir()
            .join(crate::managed::ID)
            .join(MANIFEST_FILE)
            .is_file()
        {
            p.set_enabled(crate::managed::ID, true).expect("not vanilla");
        }
    }

    pub fn install_addon(&self, src: &Path) -> Result<AddonManifest> {
        self.install_addon_for(src, &crate::update::current_version())
    }

    /// Every addon `src` holds: a zip or rar may carry several addon folders
    /// and packages.
    pub fn install_addons(&self, src: &Path) -> Vec<Result<AddonManifest>> {
        let launcher = crate::update::current_version();
        if !src.is_file() {
            return vec![self.install_addon_for(src, &launcher)];
        }
        let scratch = self.scratch_dir();
        let installed = match self.unpacked_addons(src, &scratch) {
            Ok(found) => found
                .iter()
                .map(|addon| self.install_addon_for(addon, &launcher))
                .collect(),
            Err(e) => vec![Err(e)],
        };
        std::fs::remove_dir_all(&scratch).ok();
        installed
    }

    pub fn install_addon_for(&self, src: &Path, launcher: &semver::Version) -> Result<AddonManifest> {
        if src.is_file() {
            let scratch = self.scratch_dir();
            let result = self
                .unpacked_addons(src, &scratch)
                .and_then(|found| match found.as_slice() {
                    [one] => self.install_addon_for(one, launcher),
                    many => Err(Error::Other(format!(
                        "{} holds {} addons; drop it on the launcher to install them all",
                        src.display(),
                        many.len()
                    ))),
                });
            std::fs::remove_dir_all(&scratch).ok();
            return result;
        }
        let src = &crate::import::addon_root(src).unwrap_or_else(|| src.to_path_buf());
        if src.join(crate::dbedit::package::MARKER).is_file() {
            return crate::dbedit::package::install(self, src).map(|(p, _)| p.manifest);
        }
        let m = AddonManifest::load(src).map_err(|e| {
            let u = Unreadable::of(src, &e, launcher);
            if u.newer {
                Error::Other(u.line())
            } else {
                Error::from(e)
            }
        })?;
        if self.is_builtin(&m.id) {
            return Err(Error::Other(format!("{} is built in", m.id)));
        }
        let prefix = crate::dbedit::package::ID_PREFIX;
        if crate::dbedit::package::is_package(self, &m.id) {
            return Err(Error::Other(format!(
                "{} is a DB Editor addon you have; installing over it would delete its edits",
                m.id
            )));
        }
        if m.id.starts_with(prefix) {
            return Err(Error::Other(format!(
                "{}: the {prefix} prefix is kept for addons the DB Editor saves",
                m.id
            )));
        }
        if let Some(why) = crate::resolver::launcher_refusal(&m, launcher) {
            return Err(Error::Other(why));
        }
        let dest = self.addons_dir().join(&m.id);
        self.helpers.forget(&m.id);
        if dest.exists() {
            std::fs::remove_dir_all(&dest).map_err(io_err(&dest))?;
        }
        copy_dir(src, &dest)?;
        Ok(m)
    }

    fn scratch_dir(&self) -> PathBuf {
        static NEXT: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
        let n = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        self.addons_dir().join(format!(".importing-{n}"))
    }

    // A package is one addon; any other archive holds whatever addons it has.
    fn unpacked_addons(&self, archive: &Path, scratch: &Path) -> Result<Vec<PathBuf>> {
        if scratch.exists() {
            std::fs::remove_dir_all(scratch).map_err(io_err(scratch))?;
        }
        crate::import::unpack(archive, scratch)?;
        let found = if crate::import::is_package(archive) {
            crate::import::addon_root(scratch).into_iter().collect()
        } else {
            crate::import::found_addons(scratch)
        };
        if found.is_empty() {
            return Err(Error::Other(format!(
                "{} has no {MANIFEST_FILE}",
                archive.display()
            )));
        }
        Ok(found)
    }

    pub fn remove_addon(&self, id: &str) -> Result<()> {
        if self.is_builtin(id) {
            return Err(Error::Other(format!("{id} is built in and cannot be removed")));
        }
        if crate::managed::is_managed(id) {
            return Err(Error::Other(format!(
                "{id} is managed by the launcher and cannot be removed"
            )));
        }
        let dir = self.addons_dir().join(id);
        if !dir.exists() {
            return Err(Error::Other(format!("no addon named {id} is installed")));
        }
        let _write = profile_write();
        std::fs::remove_dir_all(&dir).map_err(io_err(&dir))?;
        self.helpers.forget(id);
        for name in self.profile_names()? {
            let path = self.profiles_dir().join(format!("{name}.json"));
            if name == profile::VANILLA || !path.is_file() {
                continue;
            }
            let Some(mut p) = load_profile(&path)? else {
                continue;
            };
            let listed = p.addons.iter().any(|a| a.id == id);
            let had_options = p.options.contains_key(id) || p.applied.contains_key(id);
            if listed || had_options {
                p.addons.retain(|a| a.id != id);
                p.options.remove(id);
                p.applied.remove(id);
                p.normalise();
                self.save_profile(&p)?;
            }
        }
        Ok(())
    }

    pub fn profile(&self, name: &str) -> Result<Profile> {
        if name == crate::profile::VANILLA {
            return Ok(Profile::vanilla());
        }
        let path = self.profiles_dir().join(format!("{name}.json"));
        let mut p = load_profile(&path)?.unwrap_or_else(|| Profile::new(name));
        let unknown = p.unknown_fields();
        if !unknown.is_empty() {
            warn_once(format!(
                "profile {name} has settings from a newer Swofty Launcher ({}); they are kept but not used",
                unknown.join(", ")
            ));
        }
        p.normalise();
        self.adopt_builtins(&mut p);
        Ok(p)
    }

    pub fn profile_names(&self) -> Result<Vec<String>> {
        let mut names = vec![profile::VANILLA.to_owned()];
        let rd = match std::fs::read_dir(self.profiles_dir()) {
            Ok(rd) => rd,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                names.push(DEFAULT_PROFILE.into());
                return Ok(names);
            }
            Err(e) => return Err(io_err(self.profiles_dir())(e)),
        };
        let mut found: Vec<String> = rd
            .flatten()
            .filter_map(|e| {
                let name = e.file_name().to_str()?.strip_suffix(".json")?.to_owned();
                profile::valid_name(&name).then_some(name)
            })
            .collect();
        if found.is_empty() {
            found.push(DEFAULT_PROFILE.into());
        }
        found.sort();
        names.extend(found);
        Ok(names)
    }

    pub fn create_profile(&self, name: &str, from: Option<&str>) -> Result<Profile> {
        if !profile::valid_name(name) {
            return Err(Error::Other(format!(
                "{name:?} is not a valid profile name (letters, digits, - and _)"
            )));
        }
        if self.profile_names()?.iter().any(|n| n == name) {
            return Err(Error::Other(format!("a profile named {name} already exists")));
        }
        let mut p = match from {
            Some(src) => self.profile(src)?,
            None => Profile::new(name),
        };
        p.name = name.into();
        p.normalise();
        self.adopt_builtins(&mut p);
        self.save_profile(&p)?;
        Ok(p)
    }

    pub fn rename_profile(&self, from: &str, to: &str) -> Result<Profile> {
        if from == profile::VANILLA {
            return Err(Error::Other("the vanilla profile cannot be renamed".into()));
        }
        let p = self.create_profile(to, Some(from))?;
        let old = self.profiles_dir().join(format!("{from}.json"));
        if old.is_file() {
            std::fs::remove_file(&old).map_err(io_err(&old))?;
        }
        let saves = self.saves_dir(from);
        if saves.is_dir() {
            std::fs::rename(&saves, self.saves_dir(to)).map_err(io_err(&saves))?;
        }
        let overlay = self.profile_dir(from);
        if overlay.is_dir() {
            std::fs::rename(&overlay, self.profile_dir(to)).map_err(io_err(&overlay))?;
        }
        if self.config().active_profile.as_deref().unwrap_or(DEFAULT_PROFILE) == from {
            self.set_active_profile(to)?;
        }
        let mut config = self.config();
        if config.vr_profiles.remove(from) {
            config.vr_profiles.insert(to.to_owned());
            self.save_config(&config)?;
        }
        Ok(p)
    }

    pub fn plan_profile_deletion(&self, name: &str) -> Result<ProfileDeletion> {
        if name == profile::VANILLA {
            return Err(Error::Other(
                "the vanilla profile is built in and cannot be deleted".into(),
            ));
        }
        let names = self.profile_names()?;
        if !names.iter().any(|n| n == name) {
            return Err(Error::Other(format!("no profile named {name}")));
        }
        let others: Vec<&String> = names
            .iter()
            .filter(|n| *n != profile::VANILLA && *n != name)
            .collect();
        let Some(first) = others.first() else {
            return Err(Error::Other(format!(
                "{name} is the only profile; create another profile before deleting it"
            )));
        };
        if let Some(j) = crate::journal::Journal::open(&self.root)?
            .into_iter()
            .find(|j| j.profile == name)
        {
            return Err(Error::Other(format!(
                "{name} is installed in the game folder (journal {}); close F1 25 or run \
                 swofty restore, then delete it",
                j.id
            )));
        }
        let switch_to = (self.active_profile() == name).then(|| {
            others
                .iter()
                .find(|n| n.as_str() == DEFAULT_PROFILE)
                .unwrap_or(first)
                .to_string()
        });
        let saves = self.saves_dir(name);
        let has_saves = std::fs::read_dir(&saves).is_ok_and(|mut rd| rd.next().is_some());
        let paths = [
            self.overlays_dir(name),
            self.profile_dir(name),
            self.requests_path(name),
            saves,
            self.profiles_dir().join(format!("{name}.json")),
        ]
        .into_iter()
        .filter(|p| p.symlink_metadata().is_ok())
        .collect();
        Ok(ProfileDeletion {
            name: name.into(),
            switch_to,
            saves: has_saves,
            paths,
        })
    }

    pub fn delete_profile(&self, name: &str) -> Result<ProfileDeletion> {
        let _write = profile_write();
        let plan = self.plan_profile_deletion(name)?;
        if let Some(to) = &plan.switch_to {
            self.set_active_profile(to)?;
        }
        self.forget_launch_profile(name)?;
        let mut config = self.config();
        if config.vr_profiles.remove(name) {
            self.save_config(&config)?;
        }
        for path in &plan.paths {
            crate::overlayfs::unlock(path);
            let removed = if path.is_dir() && !path.is_symlink() {
                std::fs::remove_dir_all(path)
            } else {
                std::fs::remove_file(path)
            };
            if let Err(e) = removed
                && e.kind() != std::io::ErrorKind::NotFound
            {
                return Err(io_err(path)(e));
            }
        }
        Ok(plan)
    }

    fn forget_launch_profile(&self, name: &str) -> Result<()> {
        let path = self.launch_request_path();
        let Ok(mut request) = crate::read_json::<serde_json::Value>(&path) else {
            return Ok(());
        };
        if request.get("profile").and_then(|p| p.as_str()) != Some(name) {
            return Ok(());
        }
        if let Some(fields) = request.as_object_mut() {
            fields.remove("profile");
        }
        crate::write_json(&path, &request)
    }

    pub fn config(&self) -> Config {
        let mut config = match crate::load_json(&self.config_path()) {
            Ok(crate::Loaded::Found(c)) => c,
            _ => Config::default(),
        };
        config.migrate_games();
        config
    }

    pub fn save_config(&self, config: &Config) -> Result<()> {
        crate::write_json(&self.config_path(), config)
    }

    pub fn active_profile(&self) -> String {
        let names = self.profile_names().unwrap_or_default();
        self.config()
            .active_profile
            .filter(|p| names.contains(p))
            .or_else(|| names.get(1).cloned())
            .unwrap_or_else(|| DEFAULT_PROFILE.into())
    }

    pub fn set_active_profile(&self, name: &str) -> Result<()> {
        let mut config = self.config();
        config.active_profile = Some(name.into());
        self.save_config(&config)
    }

    pub fn latest_session(&self) -> Option<PathBuf> {
        let dir = std::fs::read_to_string(self.sessions_dir().join("latest.txt")).ok()?;
        Some(PathBuf::from(dir.trim()))
    }

    pub fn requests_path(&self, profile: &str) -> PathBuf {
        self.requests_dir().join(format!("{profile}.json"))
    }

    pub fn session_settings(&self, profile: &str) -> Result<SessionSettings> {
        let requests = crate::paths::game_folder_view(&self.requests_path(profile));
        let mut s = SessionSettings::build(&self.profile(profile)?, &self.addons()?, Some(requests))?;
        s.addons
            .retain(|a| a.id == CORE_ID || !self.is_builtin(&a.id) || !a.options.is_empty());
        Ok(s)
    }

    pub fn merge_requests(&self, profile: &str, consume: bool) -> Result<Option<Merge>> {
        let path = self.requests_path(profile);
        if !path.is_file() {
            return Ok(None);
        }
        let req: Requests = crate::read_json(&path)?;
        let _write = profile_write();
        let merged = if req.profile.is_empty() || req.profile == profile {
            let mut p = self.profile(profile)?;
            let out = settings::merge(&mut p, &self.addons()?, &req);
            if !out.changed.is_empty() {
                self.save_profile(&p)?;
            }
            Some(out)
        } else {
            None
        };
        if consume {
            std::fs::remove_file(&path).map_err(io_err(&path))?;
        }
        Ok(merged)
    }

    pub fn update_profile<T>(&self, name: &str, f: impl FnOnce(&mut Profile) -> Result<T>) -> Result<T> {
        let _write = profile_write();
        let mut p = self.profile(name)?;
        let out = f(&mut p)?;
        self.save_profile(&p)?;
        Ok(out)
    }

    pub fn save_profile(&self, p: &Profile) -> Result<()> {
        if p.is_vanilla() {
            return Err(Error::Other(
                "the vanilla profile is built in and cannot be changed".into(),
            ));
        }
        crate::write_json(&self.profiles_dir().join(format!("{}.json", p.name)), p)
    }
}

/// A saved profile. One that is damaged (not JSON at all) was set aside by
/// [`crate::load_json`] and starts over empty, as a missing one does, so it
/// cannot block a launch; the set-aside copy is kept for the player.
/// An installed addon whose `swofty.json` this launcher can't read.
#[derive(Debug, Clone, PartialEq)]
pub struct Unreadable {
    /// The manifest's id when it has one, else the folder name.
    pub id: String,
    pub name: String,
    pub version: Option<String>,
    pub dir: PathBuf,
    /// Why, after "it": "needs a newer Swofty Launcher (unknown field `hud`)".
    pub reason: String,
    /// Made for a newer launcher rather than damaged.
    pub newer: bool,
}

impl Unreadable {
    pub fn of(dir: &Path, err: &swofty_addon::Error, launcher: &semver::Version) -> Self {
        let doc: Option<serde_json::Value> = std::fs::read(dir.join(MANIFEST_FILE))
            .ok()
            .and_then(|b| serde_json::from_slice(&b).ok());
        let text = |k: &str| {
            doc.as_ref()
                .and_then(|d| d.get(k))
                .and_then(|v| v.as_str())
                .map(str::to_owned)
                .filter(|s| !s.trim().is_empty())
        };
        let folder = dir
            .file_name()
            .map_or_else(String::new, |n| n.to_string_lossy().into_owned());
        let id = text("id")
            .filter(|i| crate::manifest::valid_id(i))
            .unwrap_or(folder);
        let name = text("name").unwrap_or_else(|| id.clone());
        let req = doc
            .as_ref()
            .and_then(|d| d.pointer("/compat/launcher"))
            .and_then(|v| v.as_str())
            .and_then(|r| semver::VersionReq::parse(r).ok())
            .filter(|r| !r.matches(launcher));
        let schema = doc
            .as_ref()
            .and_then(|d| d.get("schema_version"))
            .and_then(|v| v.as_u64())
            .filter(|v| *v > u64::from(crate::manifest::SCHEMA_VERSION));
        let message = match err {
            swofty_addon::Error::Json { source, .. } => source.to_string(),
            other => other.to_string(),
        };
        let unknown = unknown_part(&message);
        let reason = match (&req, &unknown) {
            (Some(req), Some(u)) => format!("needs Swofty Launcher {req}; this is {launcher} ({u})"),
            (Some(req), None) => format!("needs Swofty Launcher {req}; this is {launcher}"),
            (None, Some(u)) => format!("needs a newer Swofty Launcher ({u})"),
            (None, None) => match schema {
                Some(v) => format!("needs a newer Swofty Launcher (schema_version {v})"),
                None => format!("can't be read: {message}"),
            },
        };
        Self {
            newer: req.is_some() || unknown.is_some() || schema.is_some(),
            version: text("version"),
            dir: dir.to_path_buf(),
            id,
            name,
            reason,
        }
    }

    /// "Race HUD (w3.hud) needs a newer Swofty Launcher (unknown field `hud`)".
    pub fn line(&self) -> String {
        if self.name == self.id {
            format!("{} {}", self.id, self.reason)
        } else {
            format!("{} ({}) {}", self.name, self.id, self.reason)
        }
    }
}

/// "unknown field `hud`" or "unknown value `postfx`" from a serde message.
fn unknown_part(message: &str) -> Option<String> {
    for (marker, word) in [("unknown field `", "field"), ("unknown variant `", "value")] {
        if let Some(at) = message.find(marker) {
            let rest = &message[at + marker.len()..];
            let name = &rest[..rest.find('`')?];
            return Some(format!("unknown {word} `{name}`"));
        }
    }
    None
}

/// Prints a warning to stderr once per process.
fn warn_once(text: String) {
    static SEEN: Mutex<BTreeSet<String>> = Mutex::new(BTreeSet::new());
    let mut seen = SEEN.lock().unwrap_or_else(|e| e.into_inner());
    if seen.insert(text.clone()) {
        eprintln!("swofty: warning: {text}");
    }
}

fn load_profile(path: &Path) -> Result<Option<Profile>> {
    Ok(match crate::load_json(path)? {
        crate::Loaded::Found(p) => Some(p),
        crate::Loaded::Missing | crate::Loaded::Damaged(_) => None,
    })
}

fn manifest_id(dir: &Path) -> Option<String> {
    let doc: serde_json::Value = crate::read_json(&dir.join(MANIFEST_FILE)).ok()?;
    Some(doc.get("id")?.as_str()?.to_owned())
}

fn subdirs(dir: &Path) -> Result<Vec<PathBuf>> {
    let rd = match std::fs::read_dir(dir) {
        Ok(rd) => rd,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(e) => return Err(io_err(dir)(e)),
    };
    let mut out: Vec<PathBuf> = rd
        .flatten()
        .map(|e| e.path())
        .filter(|p| {
            p.is_dir()
                && !p
                    .file_name()
                    .is_some_and(|n| n.to_string_lossy().starts_with('.'))
        })
        .collect();
    out.sort();
    Ok(out)
}

fn copy_dir(src: &Path, dest: &Path) -> Result<()> {
    std::fs::create_dir_all(dest).map_err(io_err(dest))?;
    for entry in std::fs::read_dir(src).map_err(io_err(src))? {
        let entry = entry.map_err(io_err(src))?;
        let to = dest.join(entry.file_name());
        if entry.path().is_dir() {
            copy_dir(&entry.path(), &to)?;
        } else {
            std::fs::copy(entry.path(), &to).map_err(io_err(&to))?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("swofty-store-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&d).ok();
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn installs_template_addon_and_keeps_core() {
        let root = tmp("addons");
        let store = Store::new(&root);
        let template = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../templates/addon");
        let m = store.install_addon(&template).unwrap();
        assert!(store.addons_dir().join(&m.id).join(MANIFEST_FILE).is_file());
        let addons = store.addons().unwrap();
        assert!(addons.contains_key(CORE_ID));
        assert!(addons.contains_key(&m.id));
        let mut p = store.profile(DEFAULT_PROFILE).unwrap();
        p.set_enabled(&m.id, true).unwrap();
        store.save_profile(&p).unwrap();
        let plan = crate::resolver::resolve(&store.profile(DEFAULT_PROFILE).unwrap(), &addons).unwrap();
        assert_eq!(plan.order, [CORE_ID, m.id.as_str()]);
        std::fs::remove_dir_all(&root).ok();
    }

    fn put_addon(store: &Store, folder: &str, json: &str) -> PathBuf {
        let dir = store.addons_dir().join(folder);
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join(MANIFEST_FILE), json).unwrap();
        dir
    }

    #[test]
    fn an_addon_for_a_newer_launcher_is_skipped_and_named() {
        let root = tmp("future-addon");
        let store = Store::new(&root);
        put_addon(&store, "t.good", &manifest_json("t.good"));
        put_addon(
            &store,
            "w3.hud",
            r#"{"schema_version":1,"id":"w3.hud","name":"Race HUD","version":"0.1.0","author":"t",
                "compat":{"game":"f1_25"},"hud":{"widgets":[]}}"#,
        );
        put_addon(
            &store,
            "t.look",
            r#"{"schema_version":1,"id":"t.look","name":"Look","version":"1.0.0","author":"t",
                "compat":{"game":"f1_25","launcher":">=9.0.0"},"experimental_features":["holograms"]}"#,
        );
        put_addon(&store, "t.broken", "{\"id\": 5}");
        let addons = store.addons().unwrap();
        assert!(addons.contains_key("t.good") && addons.contains_key(CORE_ID));
        assert!(!addons.contains_key("w3.hud") && !addons.contains_key("t.look"));
        let bad = store.unreadable_addons();
        let lines: Vec<String> = bad.iter().map(Unreadable::line).collect();
        let launcher = crate::update::current_version();
        assert_eq!(
            lines,
            [
                "t.broken can't be read: invalid type: integer `5`, expected a string at line 1 column 8"
                    .to_owned(),
                format!(
                    "Look (t.look) needs Swofty Launcher >=9.0.0; this is {launcher} (unknown value `holograms`)"
                ),
                "Race HUD (w3.hud) needs a newer Swofty Launcher (unknown field `hud`)".to_owned(),
            ]
        );
        assert_eq!(
            bad.iter().map(|u| u.newer).collect::<Vec<_>>(),
            [false, true, true]
        );
        assert_eq!(bad[2].version.as_deref(), Some("0.1.0"));

        store
            .update_profile(DEFAULT_PROFILE, |p| {
                p.set_enabled("t.good", true)?;
                p.set_enabled("w3.hud", true)
            })
            .unwrap();
        let registry = store.registry().unwrap();
        assert!(registry.get("t.good").is_some());
        let err = store
            .install_addon(&store.addons_dir().join("w3.hud"))
            .unwrap_err();
        assert_eq!(
            err.to_string(),
            "Race HUD (w3.hud) needs a newer Swofty Launcher (unknown field `hud`)"
        );
        store.remove_addon("w3.hud").unwrap();
        assert!(store.unreadable_addons().iter().all(|u| u.id != "w3.hud"));
        assert!(
            !store
                .profile(DEFAULT_PROFILE)
                .unwrap()
                .addons
                .iter()
                .any(|a| a.id == "w3.hud")
        );
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_profile_with_fields_from_a_newer_launcher_loads_and_keeps_them() {
        let root = tmp("future-profile");
        let store = Store::new(&root);
        put_addon(&store, "t.good", &manifest_json("t.good"));
        std::fs::create_dir_all(store.profiles_dir()).unwrap();
        std::fs::write(
            store.profiles_dir().join("default.json"),
            r#"{"schema_version":2,"name":"default","addons":[{"id":"t.good","enabled":true,"pinned":true}],
                "options":{},"overlay_off":["t.good"]}"#,
        )
        .unwrap();
        let p = store.profile(DEFAULT_PROFILE).unwrap();
        assert!(p.enabled().any(|id| id == "t.good"));
        assert_eq!(p.unknown_fields(), ["overlay_off", "addons[t.good].pinned"]);
        store
            .update_profile(DEFAULT_PROFILE, |p| p.set_enabled("t.good", false))
            .unwrap();
        let saved: serde_json::Value =
            serde_json::from_slice(&std::fs::read(store.profiles_dir().join("default.json")).unwrap())
                .unwrap();
        assert_eq!(saved["overlay_off"], serde_json::json!(["t.good"]));
        let good = saved["addons"]
            .as_array()
            .unwrap()
            .iter()
            .find(|a| a["id"] == "t.good")
            .unwrap();
        assert_eq!(good["pinned"], serde_json::json!(true));
        assert_eq!(good["enabled"], serde_json::json!(false));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn installs_addon_zips() {
        let root = tmp("addon-zips");
        let store = Store::new(root.join("data"));
        let manifest = r#"{"schema_version":1,"id":"t.zipped","name":"Zipped","version":"1.0.0","author":"t","compat":{"game":"f1_25"}}"#;
        for (file, prefix) in [("flat.swoftymod", ""), ("wrapped.zip", "zipped/")] {
            let path = root.join(file);
            let mut z = zip::ZipWriter::new(std::fs::File::create(&path).unwrap());
            let opts = zip::write::SimpleFileOptions::default();
            z.start_file(format!("{prefix}{MANIFEST_FILE}"), opts).unwrap();
            std::io::Write::write_all(&mut z, manifest.as_bytes()).unwrap();
            z.start_file(format!("{prefix}plugins/a.dll"), opts).unwrap();
            z.finish().unwrap();
            assert_eq!(store.install_addon(&path).unwrap().id, "t.zipped");
            assert!(store.addons_dir().join("t.zipped/plugins/a.dll").is_file());
            assert_eq!(scratch_dirs(&store), 0);
        }
        let bare = root.join("bare.zip");
        zip::ZipWriter::new(std::fs::File::create(&bare).unwrap())
            .finish()
            .unwrap();
        assert!(store.install_addon(&bare).is_err());
        std::fs::remove_dir_all(&root).ok();
    }

    fn scratch_dirs(store: &Store) -> usize {
        std::fs::read_dir(store.addons_dir())
            .unwrap()
            .flatten()
            .filter(|e| e.file_name().to_string_lossy().starts_with(".importing"))
            .count()
    }

    fn manifest_json(id: &str) -> String {
        format!(
            r#"{{"schema_version":1,"id":"{id}","name":"{id}","version":"1.0.0","author":"t","compat":{{"game":"f1_25"}}}}"#
        )
    }

    fn package(id: &str) -> Vec<u8> {
        let mut z = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
        z.start_file(MANIFEST_FILE, zip::write::SimpleFileOptions::default())
            .unwrap();
        std::io::Write::write_all(&mut z, manifest_json(id).as_bytes()).unwrap();
        z.finish().unwrap().into_inner()
    }

    #[test]
    fn installs_every_addon_a_rar_holds() {
        let root = tmp("addon-rars");
        let store = Store::new(root.join("data"));
        for (name, version) in [
            ("rar5.rar", rars::ArchiveVersion::Rar50),
            ("rar29.rar", rars::ArchiveVersion::Rar29),
        ] {
            let mut b = rars::Builder::new(version);
            let mut add = |path: &str, data: Vec<u8>| {
                b.add_bytes(path.as_bytes().to_vec(), data, None, None).unwrap();
            };
            add("Pack/t.packed.swoftyaddon", package("t.packed"));
            add(
                &format!("Pack/folder/{MANIFEST_FILE}"),
                manifest_json("t.folder").into_bytes(),
            );
            add("Pack/folder/plugins/a.dll", b"dll".to_vec());
            add("Pack/readme.txt", b"hi".to_vec());
            let rar = root.join(name);
            std::fs::write(&rar, b.to_bytes().unwrap()).unwrap();
            assert_eq!(
                crate::import::addon_claim(&rar),
                swofty_addon::launcher::Claim::Yes
            );
            let ids: Vec<String> = store
                .install_addons(&rar)
                .into_iter()
                .map(|m| m.unwrap().id)
                .collect();
            assert_eq!(ids, ["t.folder", "t.packed"], "{name}");
            assert!(store.addons_dir().join("t.folder/plugins/a.dll").is_file());
            assert!(
                store
                    .install_addon(&rar)
                    .unwrap_err()
                    .to_string()
                    .contains("holds 2 addons")
            );
            assert_eq!(scratch_dirs(&store), 0);
        }
        let mut b = rars::Builder::new(rars::ArchiveVersion::Rar50);
        b.add_bytes(b"One/t.one.swoftyaddon".to_vec(), package("t.one"), None, None)
            .unwrap();
        let one = root.join("one.rar");
        std::fs::write(&one, b.to_bytes().unwrap()).unwrap();
        assert_eq!(store.install_addon(&one).unwrap().id, "t.one");
        let mut b = rars::Builder::new(rars::ArchiveVersion::Rar50);
        b.add_bytes(
            b"mod/2025_asset_groups/a.erp".to_vec(),
            b"erp".to_vec(),
            None,
            None,
        )
        .unwrap();
        let game_mod = root.join("mod.rar");
        std::fs::write(&game_mod, b.to_bytes().unwrap()).unwrap();
        assert_eq!(
            crate::import::addon_claim(&game_mod),
            swofty_addon::launcher::Claim::No
        );
        let err = store.install_addons(&game_mod).remove(0).unwrap_err();
        assert!(err.to_string().contains("has no swofty.json"), "{err}");
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn manages_the_team_simplified_addon() {
        let root = tmp("managed-addon");
        let store = Store::new(root.join("data"));
        let manifest = r#"{"schema_version":1,"id":"teamsimplified.mods","name":"Simplified for Swofty","version":"0.5.0","author":"Team Simplified","compat":{"game":"f1_25"},"launcher":{"program":{"linux":"bin/helper","windows":"bin/helper.exe"}}}"#;
        let pkg = root.join("teamsimplified.mods.swoftyaddon");
        let mut z = zip::ZipWriter::new(std::fs::File::create(&pkg).unwrap());
        let opts = zip::write::SimpleFileOptions::default();
        z.start_file(MANIFEST_FILE, opts).unwrap();
        std::io::Write::write_all(&mut z, manifest.as_bytes()).unwrap();
        z.start_file("bin/helper", opts).unwrap();
        z.finish().unwrap();

        let m = store.install_addon(&pkg).unwrap();
        assert_eq!(m.id, crate::managed::ID);
        assert!(store.addons().unwrap().contains_key(crate::managed::ID));

        let err = store.remove_addon(crate::managed::ID).unwrap_err();
        assert!(err.to_string().contains("managed"), "{err}");
        assert!(
            store
                .addons_dir()
                .join(crate::managed::ID)
                .join(MANIFEST_FILE)
                .is_file()
        );

        let reg = store.registry().unwrap();
        assert!(reg.get(crate::managed::ID).unwrap().locked);

        let p = store.profile(DEFAULT_PROFILE).unwrap();
        assert!(p.enabled().any(|id| id == crate::managed::ID));
        std::fs::remove_dir_all(&root).ok();
    }

    fn write_addon(dir: &Path, id: &str) {
        std::fs::create_dir_all(dir).unwrap();
        std::fs::write(
            dir.join(MANIFEST_FILE),
            format!(
                r#"{{"schema_version":1,"id":"{id}","name":"{id}","version":"1.0.0","author":"t","compat":{{"game":"f1_25"}}}}"#
            ),
        )
        .unwrap();
    }

    #[test]
    fn migrate_renames_the_mods_dir() {
        let root = tmp("migrate-rename");
        let store = Store::new(&root);
        write_addon(&root.join("mods/a.one"), "a.one");
        std::fs::write(store.config_path(), r#"{"active_profile":"grid"}"#).unwrap();
        assert_eq!(store.migrate().unwrap().len(), 1);
        assert!(!root.join("mods").exists());
        assert!(store.addons().unwrap().contains_key("a.one"));
        let config = store.config();
        assert_eq!(config.data_version, DATA_VERSION);
        assert_eq!(config.active_profile.as_deref(), Some("grid"));
        write_addon(&root.join("mods/a.late"), "a.late");
        assert!(store.migrate().unwrap().is_empty());
        assert!(root.join("mods/a.late").is_dir());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn migrate_merges_into_an_existing_addons_dir() {
        let root = tmp("migrate-merge");
        let store = Store::new(&root);
        write_addon(&root.join("addons/a.one"), "a.one");
        write_addon(&root.join("mods/one-copy"), "a.one");
        write_addon(&root.join("mods/a.two"), "a.two");
        std::fs::create_dir_all(root.join("mods/stray")).unwrap();
        std::fs::write(store.config_path(), r#"{"data_version":"from a newer launcher"}"#).unwrap();
        let notes = store.migrate().unwrap();
        assert_eq!(notes.len(), 4, "{notes:?}");
        assert!(notes[3].starts_with("data_version not recorded"));
        assert!(root.join("addons/a.two").join(MANIFEST_FILE).is_file());
        assert!(root.join("mods/one-copy").is_dir() && root.join("mods/stray").is_dir());
        assert_eq!(
            store.addons().unwrap().keys().collect::<Vec<_>>(),
            ["a.one", "a.two", CORE_ID]
        );

        std::fs::remove_dir_all(root.join("mods/one-copy")).unwrap();
        std::fs::remove_dir_all(root.join("mods/stray")).unwrap();
        write_addon(&root.join("mods/a.three"), "a.three");
        std::fs::remove_file(store.config_path()).unwrap();
        assert_eq!(store.migrate().unwrap().len(), 2);
        assert!(!root.join("mods").exists());
        assert!(root.join("addons/a.three").is_dir());
        assert_eq!(store.config().data_version, DATA_VERSION);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn migrate_leaves_a_missing_data_dir_alone() {
        let root = tmp("migrate-missing").join("data");
        assert!(Store::new(&root).migrate().unwrap().is_empty());
        assert!(!root.exists());
    }

    #[test]
    fn reads_profiles_that_list_mods_and_writes_addons() {
        let root = tmp("old-profile");
        let store = Store::new(&root);
        let path = store.profiles_dir().join("grid.json");
        std::fs::create_dir_all(store.profiles_dir()).unwrap();
        std::fs::write(
            &path,
            r#"{"schema_version":1,"name":"grid","mods":[{"id":"swofty.core"},{"id":"a.b","enabled":false},{"id":"c.d"}],"options":{"c.d":{"x":1}}}"#,
        )
        .unwrap();
        let p = store.profile("grid").unwrap();
        assert_eq!(p.enabled().collect::<Vec<_>>(), [CORE_ID, "c.d"]);
        store.save_profile(&p).unwrap();
        let saved: serde_json::Value = crate::read_json(&path).unwrap();
        assert_eq!(saved["schema_version"], profile::SCHEMA_VERSION);
        assert_eq!(
            saved["addons"][1],
            serde_json::json!({"id": "a.b", "enabled": false})
        );
        assert!(saved.get("mods").is_none());
        assert_eq!(saved["options"]["c.d"]["x"], 1);
        assert_eq!(store.profile("grid").unwrap(), p);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn merges_in_game_requests_into_the_profile() {
        let root = tmp("requests");
        let store = Store::new(&root);
        let path = store.requests_path(DEFAULT_PROFILE);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(
            &path,
            r#"{"schema":1,"profile":"default","seq":1,"options":{"swofty.core":{"show_launcher_panel":false}}}"#,
        )
        .unwrap();
        let out = store.merge_requests(DEFAULT_PROFILE, false).unwrap().unwrap();
        assert_eq!(out.changed, ["swofty.core.show_launcher_panel = Off"]);
        assert!(path.is_file());
        let core = crate::builtin::core_manifest();
        let p = store.profile(DEFAULT_PROFILE).unwrap();
        assert_eq!(p.option(&core, &core.options[0]), false);
        let again = store.merge_requests(DEFAULT_PROFILE, true).unwrap().unwrap();
        assert!(again.changed.is_empty());
        assert!(!path.exists());
        assert_eq!(store.merge_requests(DEFAULT_PROFILE, true).unwrap(), None);
        let s = store.session_settings(DEFAULT_PROFILE).unwrap();
        assert_eq!(s.addons[0].options[0].current, 0);
        assert!(s.requests.unwrap().ends_with("default.json"));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn leaves_builtins_without_options_out_of_addon_settings() {
        let root = tmp("builtin-settings");
        let parse = |json: &str| AddonManifest::parse(json).unwrap();
        let builtin = |m| Builtin {
            addon: Arc::new(crate::registry::DeclaredAddon::new(m)),
            locked: false,
            default_on: true,
        };
        let tuned = || {
            builtin(parse(
                r#"{"schema_version":1,"id":"t.tuned","name":"Tuned","version":"1.0.0","author":"t","compat":{"game":"f1_25"},
                    "options":[{"id":"on","type":"toggle","label":"On","default":true,"binds":"native"}]}"#,
            ))
        };
        let quiet = || {
            vec![
                builtin(parse(
                    r#"{"schema_version":1,"id":"t.library","name":"Library","version":"1.0.0","author":"t","compat":{"game":"f1_25"}}"#,
                )),
                builtin(parse(
                    r#"{"schema_version":1,"id":"t.native","name":"Native","version":"1.0.0","author":"t","compat":{"game":"f1_25"},
                        "operations":{"native":[{"dll":"native/n.dll"}]}}"#,
                )),
                builtin(parse(
                    r#"{"schema_version":1,"id":"t.pages","name":"Pages","version":"1.0.0","author":"t","compat":{"game":"f1_25"},
                        "operations":{"screens":[{"name":"pages_screen","file":"ui/p.ui"}]}}"#,
                )),
            ]
        };
        write_addon(&Store::new(&root).addons_dir().join("a.user"), "a.user");
        let plain = Store::new(&root).with_builtins(vec![tuned()]);
        let expected = SessionSettings::build(
            &plain.profile(DEFAULT_PROFILE).unwrap(),
            &plain.addons().unwrap(),
            Some(crate::paths::game_folder_view(
                &plain.requests_path(DEFAULT_PROFILE),
            )),
        )
        .unwrap();
        let plain_json = serde_json::to_vec(&plain.session_settings(DEFAULT_PROFILE).unwrap()).unwrap();
        assert_eq!(plain_json, serde_json::to_vec(&expected).unwrap());

        let mut all = quiet();
        all.push(tuned());
        let store = Store::new(&root).with_builtins(all);
        let p = store.profile(DEFAULT_PROFILE).unwrap();
        assert_eq!(
            p.enabled().collect::<Vec<_>>(),
            [CORE_ID, "t.library", "t.native", "t.pages", "t.tuned"]
        );
        let s = store.session_settings(DEFAULT_PROFILE).unwrap();
        let ids: Vec<&str> = s.addons.iter().map(|a| a.id.as_str()).collect();
        assert_eq!(ids, [CORE_ID, "t.tuned", "a.user"]);
        let screens: Vec<&str> =
            s.ui.as_ref()
                .unwrap()
                .screens
                .iter()
                .map(|x| x.name.as_str())
                .collect();
        assert_eq!(screens, ["pages_screen"]);
        let mut without_pages = s.clone();
        without_pages.ui = None;
        assert_eq!(serde_json::to_vec(&without_pages).unwrap(), plain_json);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn manages_profiles() {
        let root = tmp("profiles");
        let store = Store::new(&root);
        assert_eq!(store.profile_names().unwrap(), ["vanilla", "default"]);
        assert_eq!(store.active_profile(), "default");
        let mut p = store.profile(DEFAULT_PROFILE).unwrap();
        p.set_enabled("a.b", true).unwrap();
        store.save_profile(&p).unwrap();
        store.set_active_profile(DEFAULT_PROFILE).unwrap();
        store.create_profile("grid", Some(DEFAULT_PROFILE)).unwrap();
        assert!(store.create_profile("grid", None).is_err());
        assert!(store.create_profile("vanilla", None).is_err());
        store.rename_profile(DEFAULT_PROFILE, "main").unwrap();
        assert_eq!(store.profile_names().unwrap(), ["vanilla", "grid", "main"]);
        assert_eq!(store.active_profile(), "main");
        let grid = store.profile("grid").unwrap();
        assert_eq!(grid.enabled().collect::<Vec<_>>(), [CORE_ID, "a.b"]);
        std::fs::remove_dir_all(&root).ok();
    }

    fn touch(path: &Path) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, "x").unwrap();
    }

    #[test]
    fn deletes_a_profile_with_its_own_files_only() {
        let root = tmp("delete-profile");
        let store = Store::new(&root);
        write_addon(&store.addons_dir().join("a.user"), "a.user");
        store.create_profile("grid", None).unwrap();
        store.create_profile("main", None).unwrap();
        store.set_active_profile("main").unwrap();
        let own = [
            store.overlay_dir("grid", "h1").join(crate::build::PLAN_FILE),
            store.requests_path("grid"),
            store.saves_dir("grid").join("career.sav"),
        ];
        let kept = [
            store.overlay_dir("main", "h2").join(crate::build::PLAN_FILE),
            store.requests_path("main"),
            store.saves_dir("main").join("career.sav"),
            root.join("cache/maps/okayama/a1/.done"),
            store.addon_data_dir("a.user").join("library.json"),
        ];
        own.iter().chain(&kept).for_each(|f| touch(f));

        let plan = store.plan_profile_deletion("grid").unwrap();
        assert_eq!(plan.switch_to, None);
        assert!(plan.saves);
        assert_eq!(
            plan.paths,
            [
                store.overlays_dir("grid"),
                store.requests_path("grid"),
                store.saves_dir("grid"),
                store.profiles_dir().join("grid.json"),
            ]
        );
        assert!(own.iter().all(|f| f.is_file()));

        assert_eq!(store.delete_profile("grid").unwrap(), plan);
        assert_eq!(store.profile_names().unwrap(), ["vanilla", "main"]);
        assert!(plan.paths.iter().all(|p| !p.exists()));
        for f in &kept {
            assert!(f.is_file(), "{} was removed", f.display());
        }
        assert!(store.addons().unwrap().contains_key("a.user"));
        assert_eq!(store.active_profile(), "main");
        let err = store.delete_profile("grid").unwrap_err();
        assert!(err.to_string().contains("no profile named grid"), "{err}");
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn the_vr_choice_follows_its_profile() {
        use crate::session::LaunchMode;
        let root = tmp("vr-profile");
        let store = Store::new(&root);
        store.create_profile("grid", None).unwrap();
        store.create_profile("main", None).unwrap();
        let mut config = store.config();
        config.set_launch_mode("grid", LaunchMode::Vr);
        store.save_config(&config).unwrap();
        store.rename_profile("grid", "vrgrid").unwrap();
        let config = store.config();
        assert_eq!(config.launch_mode("vrgrid"), LaunchMode::Vr);
        assert_eq!(config.launch_mode("grid"), LaunchMode::Normal);
        store.delete_profile("vrgrid").unwrap();
        assert!(store.config().vr_profiles.is_empty());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_profile_takes_its_overlay_folder_along() {
        let root = tmp("overlay-folder");
        let store = Store::new(&root);
        store.create_profile("grid", None).unwrap();
        store.create_profile("main", None).unwrap();
        let work = store.overlay_work_dir("grid").join("work");
        std::fs::create_dir_all(&work).unwrap();
        std::fs::create_dir_all(store.overlay_upper_dir("grid")).unwrap();
        std::fs::write(store.overlay_upper_dir("grid").join("loader.log"), "log").unwrap();
        // The kernel makes the work dir's `work` folder without any permission.
        #[cfg(unix)]
        std::fs::set_permissions(&work, std::os::unix::fs::PermissionsExt::from_mode(0o0)).unwrap();
        assert!(
            store
                .plan_profile_deletion("grid")
                .unwrap()
                .paths
                .contains(&store.profile_dir("grid"))
        );
        store.rename_profile("grid", "vrgrid").unwrap();
        assert!(!store.profile_dir("grid").exists());
        assert_eq!(
            std::fs::read_to_string(store.overlay_upper_dir("vrgrid").join("loader.log")).unwrap(),
            "log"
        );
        store.delete_profile("vrgrid").unwrap();
        assert!(!store.profile_dir("vrgrid").exists() && !store.profiles_dir().join("vrgrid.json").exists());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn keeps_vanilla_and_the_last_profile() {
        let root = tmp("delete-last");
        let store = Store::new(&root);
        let err = store.delete_profile(DEFAULT_PROFILE).unwrap_err();
        assert!(err.to_string().contains("only profile"), "{err}");
        store.create_profile("grid", None).unwrap();
        let err = store.delete_profile("grid").unwrap_err();
        assert!(err.to_string().contains("only profile"), "{err}");
        let err = store.delete_profile(profile::VANILLA).unwrap_err();
        assert!(err.to_string().contains("built in"), "{err}");
        assert!(store.delete_profile("../config").is_err());
        assert!(store.profiles_dir().join("grid.json").is_file());
        store.create_profile("main", None).unwrap();
        let plan = store.plan_profile_deletion("grid").unwrap();
        assert!(!plan.saves);
        assert_eq!(plan.paths, [store.profiles_dir().join("grid.json")]);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn deleting_the_active_profile_switches_first() {
        let root = tmp("delete-active");
        let store = Store::new(&root);
        store
            .save_profile(&store.profile(DEFAULT_PROFILE).unwrap())
            .unwrap();
        store.create_profile("grid", None).unwrap();
        store.create_profile("zeta", None).unwrap();
        store.set_active_profile("grid").unwrap();
        crate::write_json(
            &store.launch_request_path(),
            &serde_json::json!({"id": "r1", "profile": "grid", "apply": true}),
        )
        .unwrap();

        let plan = store.delete_profile("grid").unwrap();
        assert_eq!(plan.switch_to.as_deref(), Some(DEFAULT_PROFILE));
        assert_eq!(store.config().active_profile.as_deref(), Some(DEFAULT_PROFILE));
        assert_eq!(store.active_profile(), DEFAULT_PROFILE);
        let request: serde_json::Value = crate::read_json(&store.launch_request_path()).unwrap();
        assert_eq!(request, serde_json::json!({"id": "r1", "apply": true}));

        store.create_profile("alpha", None).unwrap();
        crate::write_json(
            &store.launch_request_path(),
            &serde_json::json!({"id": "r2", "profile": "zeta"}),
        )
        .unwrap();
        let plan = store.delete_profile(DEFAULT_PROFILE).unwrap();
        assert_eq!(plan.switch_to.as_deref(), Some("alpha"));
        assert_eq!(store.active_profile(), "alpha");
        assert_eq!(store.profile_names().unwrap(), ["vanilla", "alpha", "zeta"]);
        let request: serde_json::Value = crate::read_json(&store.launch_request_path()).unwrap();
        assert_eq!(request["profile"], "zeta");

        assert_eq!(store.delete_profile("zeta").unwrap().switch_to, None);
        assert_eq!(store.active_profile(), "alpha");
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn refuses_a_profile_installed_in_the_game_folder() {
        let root = tmp("delete-applied");
        let store = Store::new(&root);
        store.create_profile("grid", None).unwrap();
        store.create_profile("main", None).unwrap();
        touch(&store.overlay_dir("grid", "h1").join(crate::build::PLAN_FILE));
        let j = crate::journal::Journal::create(&root, "grid", "build-1", false).unwrap();

        let err = store.delete_profile("grid").unwrap_err();
        assert!(err.to_string().contains("installed in the game folder"), "{err}");
        assert!(err.to_string().contains(&j.id), "{err}");
        assert!(store.profiles_dir().join("grid.json").is_file());
        assert!(store.overlays_dir("grid").is_dir());
        assert_eq!(store.plan_profile_deletion("main").unwrap().switch_to, None);

        j.close().unwrap();
        store.delete_profile("grid").unwrap();
        assert!(!store.overlays_dir("grid").exists());
        std::fs::remove_dir_all(&root).ok();
    }
}
