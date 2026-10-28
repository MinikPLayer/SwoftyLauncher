//! What an addon writes through the storage API under swofty/addons/<id>/
//! must outlive the session that wrote it: profile switches, rollbacks,
//! restores, the first-play setup and addon updates. Everything runs on
//! throwaway folders.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use swofty_core::addon_storage;
use swofty_core::baseline::{Identity, sha256_bytes};
use swofty_core::install::{Game, Installer, Plan};
use swofty_core::journal::Journal;
use swofty_core::recovery::{self, Scope, SessionLock};

const GVDB: &str = "game_package/gamedatabases/gamedatabases.2025.bin";
const CORE: &str = "swofty/addons/core/swofty_core.dll";
const SETTINGS: &str = "swofty/settings.json";
const DLL: &str = "swofty/addons/t.store/native/store.dll";
const YEARS: &str = "swofty/addons/t.store/data/years.json";
const CAREERS: &str = "swofty/addons/t.store/careers.json";
const SLOT: &str = "swofty/addons/t.store/slots/1/state.json";
const OTHER: &str = "swofty/addons/t.other/native/other.dll";

struct Fixture {
    root: PathBuf,
    game: PathBuf,
    data: PathBuf,
    src: PathBuf,
}

impl Fixture {
    fn new(name: &str) -> Self {
        let root = std::env::temp_dir().join(format!("swofty-storage-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let f = Self {
            game: root.join("game"),
            data: root.join("data"),
            src: root.join("src"),
            root,
        };
        write(&f.game.join("F1_25.exe"), "exe-v1");
        write(&f.game.join(GVDB), "db-v1");
        for (name, body) in [
            ("db", "db-modded"),
            ("proxy", "proxy"),
            ("core", "core"),
            ("settings", "{}"),
            ("dll", "store-v1"),
            ("years", "years-v1"),
            ("other", "other-v1"),
        ] {
            write(&f.src.join(name), body);
        }
        f
    }

    fn game(&self) -> Game {
        Game {
            dir: self.game.clone(),
            user_reg: None,
            identity: Identity::detect(&self.game, Some(1)).unwrap(),
        }
    }

    fn base(&self, profile: &str) -> Plan {
        Plan {
            profile: profile.into(),
            data: BTreeMap::from([(GVDB.to_owned(), self.src.join("db"))]),
            added: BTreeMap::new(),
            loader: BTreeMap::from([
                ("version.dll".to_owned(), self.src.join("proxy")),
                (CORE.to_owned(), self.src.join("core")),
                (SETTINGS.to_owned(), self.src.join("settings")),
            ]),
            registry_override: false,
        }
    }

    fn with_addon(&self) -> Plan {
        let mut p = self.base("with");
        p.loader.insert(DLL.to_owned(), self.src.join("dll"));
        p.loader.insert(YEARS.to_owned(), self.src.join("years"));
        p
    }

    fn without_addon(&self) -> Plan {
        let mut p = self.base("without");
        p.loader.insert(OTHER.to_owned(), self.src.join("other"));
        p
    }

    fn start(&self, data: &Path, plan: &Plan) -> SessionLock {
        let lock = SessionLock::try_acquire(data).unwrap().unwrap();
        let game = self.game();
        recovery::recover(&lock, data, &game, Scope::All).unwrap();
        Installer {
            session: true,
            ..Installer::new(data, &game)
        }
        .apply(plan)
        .unwrap();
        lock
    }

    fn end(&self, data: &Path, lock: SessionLock) {
        let game = self.game();
        let inst = Installer::new(data, &game);
        inst.roll_back(false).unwrap();
        inst.tidy(&data.join("sessions/logs")).unwrap();
        drop(lock);
    }

    fn session(&self, plan: &Plan, play: impl FnOnce(&Self)) {
        let lock = self.start(&self.data, plan);
        play(self);
        self.end(&self.data, lock);
    }

    fn put(&self, rel: &str, body: &str) {
        write(&self.game.join(rel), body);
    }

    fn read(&self, rel: &str) -> Option<String> {
        std::fs::read_to_string(self.game.join(rel)).ok()
    }

    fn snapshot(&self) -> BTreeMap<String, String> {
        let mut out = BTreeMap::new();
        walk(&self.game, &self.game, &mut out);
        out
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        std::fs::remove_dir_all(&self.root).ok();
    }
}

fn write(path: &Path, body: &str) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, body).unwrap();
}

fn walk(base: &Path, dir: &Path, out: &mut BTreeMap<String, String>) {
    for e in std::fs::read_dir(dir).unwrap().flatten() {
        let rel = e
            .path()
            .strip_prefix(base)
            .unwrap()
            .to_string_lossy()
            .replace('\\', "/");
        if e.path().is_dir() {
            out.insert(format!("{rel}/"), "dir".into());
            walk(base, &e.path(), out);
        } else {
            out.insert(rel, sha256_bytes(&std::fs::read(e.path()).unwrap()));
        }
    }
}

fn age(path: &Path) {
    let then = SystemTime::now() - Duration::from_secs(3600);
    std::fs::File::options()
        .write(true)
        .open(path)
        .unwrap()
        .set_modified(then)
        .unwrap();
}

fn plays(f: &Fixture) {
    f.put(CAREERS, "3 careers");
    f.put(SLOT, "slot one");
}

#[test]
fn a_setup_reset_from_another_data_folder_keeps_what_the_addon_wrote() {
    let f = Fixture::new("reset");
    f.session(&f.with_addon(), plays);

    // Another launcher data folder on the same game folder runs the
    // first-play setup, which puts the game folder back to its own record.
    let other = f.root.join("other-data");
    swofty_core::setup::reset(&other, &f.game(), false).unwrap();
    swofty_core::setup::finish(&other).unwrap();
    let lock = f.start(&other, &f.without_addon());
    f.end(&other, lock);

    let lock = f.start(&f.data, &f.with_addon());
    assert_eq!(f.read(CAREERS).as_deref(), Some("3 careers"));
    assert_eq!(f.read(SLOT).as_deref(), Some("slot one"));
    f.end(&f.data, lock);
}

#[test]
fn a_session_end_moves_storage_out_and_leaves_the_game_folder_vanilla() {
    let f = Fixture::new("vanilla");
    let original = f.snapshot();
    f.session(&f.with_addon(), plays);
    assert_eq!(f.snapshot(), original);
    let kept = addon_storage::addon_dir(&f.data, "t.store");
    assert_eq!(
        std::fs::read_to_string(kept.join("careers.json")).unwrap(),
        "3 careers"
    );
    assert_eq!(
        std::fs::read_to_string(kept.join("slots/1/state.json")).unwrap(),
        "slot one"
    );
    assert!(!kept.join("native").exists());
    assert!(!kept.join("data").exists());
}

#[test]
fn a_profile_without_the_addon_and_back_keeps_its_storage() {
    let f = Fixture::new("switch");
    f.session(&f.with_addon(), plays);

    f.session(&f.without_addon(), |f| {
        assert_eq!(f.read(CAREERS), None);
        assert!(!f.game.join("swofty/addons/t.store").exists());
    });

    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("3 careers"));
        f.put(CAREERS, "4 careers");
        std::fs::remove_file(f.game.join(SLOT)).unwrap();
    });
    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("4 careers"));
        assert_eq!(f.read(SLOT), None);
    });
    let kept = addon_storage::addon_dir(&f.data, "t.store");
    assert_eq!(
        std::fs::read_to_string(kept.join("careers.json")).unwrap(),
        "4 careers"
    );
    assert!(!kept.join("slots").exists());
}

#[test]
fn an_addon_update_keeps_its_storage_and_ships_its_new_files() {
    let f = Fixture::new("update");
    f.session(&f.with_addon(), plays);
    write(&f.src.join("dll"), "store-v2");
    write(&f.src.join("years"), "years-v2");
    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(DLL).as_deref(), Some("store-v2"));
        assert_eq!(f.read(YEARS).as_deref(), Some("years-v2"));
        assert_eq!(f.read(CAREERS).as_deref(), Some("3 careers"));
    });
}

#[test]
fn a_restore_after_a_crashed_session_keeps_what_the_addon_wrote() {
    let f = Fixture::new("crash");
    let original = f.snapshot();
    let lock = f.start(&f.data, &f.with_addon());
    plays(&f);
    // The wrapper dies with the game before its rollback.
    drop(lock);

    recovery::restore(&f.data, &f.game()).unwrap();
    assert_eq!(f.snapshot(), original);
    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("3 careers"));
        assert_eq!(f.read(SLOT).as_deref(), Some("slot one"));
    });
}

#[test]
fn a_crashed_session_whose_journal_is_lost_keeps_the_newest_storage() {
    let f = Fixture::new("lost");
    f.session(&f.with_addon(), plays);
    let lock = f.start(&f.data, &f.with_addon());
    f.put(CAREERS, "4 careers");
    drop(lock);
    std::fs::remove_dir_all(Journal::dir(&f.data)).unwrap();

    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("4 careers"));
    });
    let kept = addon_storage::addon_dir(&f.data, "t.store");
    assert_eq!(
        std::fs::read_to_string(kept.join("careers.json")).unwrap(),
        "4 careers"
    );
}

#[test]
fn storage_left_in_the_game_folder_by_an_older_launcher_is_taken_in() {
    let f = Fixture::new("upgrade");
    f.put(CAREERS, "from 1.5.12");
    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("from 1.5.12"));
    });
    assert_eq!(f.read(CAREERS), None);
    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("from 1.5.12"));
    });
}

#[test]
fn a_shipped_file_wins_over_a_stored_one_at_its_path() {
    let f = Fixture::new("shipped");
    let kept = addon_storage::addon_dir(&f.data, "t.store");
    write(&kept.join("data/years.json"), "written by the addon");
    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(YEARS).as_deref(), Some("years-v1"));
    });
    assert_eq!(
        std::fs::read_to_string(kept.join("data/years.json")).unwrap(),
        "written by the addon"
    );
}

#[test]
fn a_session_without_the_addon_leaves_its_storage_where_it_is() {
    let f = Fixture::new("untouched");
    f.put(CAREERS, "another launcher's");
    f.session(&f.without_addon(), |_| {});
    assert_eq!(f.read(CAREERS).as_deref(), Some("another launcher's"));
    assert!(!addon_storage::addon_dir(&f.data, "t.store").exists());
}

#[test]
fn the_first_play_setup_keeps_storage_an_older_launcher_left_in_the_game_folder() {
    let f = Fixture::new("setup");
    let original = f.snapshot();
    f.put(CAREERS, "from 1.5.12");
    f.put(SLOT, "slot one");
    swofty_core::setup::reset(&f.data, &f.game(), false).unwrap();
    swofty_core::setup::finish(&f.data).unwrap();
    assert_eq!(f.snapshot(), original);
    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("from 1.5.12"));
        assert_eq!(f.read(SLOT).as_deref(), Some("slot one"));
    });
}

#[test]
fn a_session_end_reports_what_it_kept() {
    let f = Fixture::new("report");
    let lock = f.start(&f.data, &f.with_addon());
    plays(&f);
    let game = f.game();
    let done = Installer::new(&f.data, &game).roll_back(false).unwrap();
    drop(lock);
    assert_eq!(done.len(), 1);
    assert_eq!(done[0].kept, [CAREERS, SLOT]);
    assert_eq!(done[0].drift, []);
}

#[test]
fn a_game_that_cant_write_its_folder_keeps_what_it_wrote_in_the_data_folder() {
    let f = Fixture::new("readonly");
    let original = f.snapshot();
    f.session(&f.with_addon(), plays);
    let kept = addon_storage::addon_dir(&f.data, "t.store");

    // The core finds swofty/addons/t.store/ read-only to the game and writes
    // to the addon's storage in the data folder; the copies put back into the
    // game folder at the session start stay as they were.
    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("3 careers"));
        assert_eq!(
            addon_storage::live_dir(&f.data, &f.game, "t.store"),
            f.game.join("swofty/addons/t.store")
        );
        write(
            &kept.join(addon_storage::MARK),
            "the game can't create files there",
        );
        assert_eq!(addon_storage::live_dir(&f.data, &f.game, "t.store"), kept);
        age(&f.game.join(CAREERS));
        write(&kept.join("careers.json"), "4 careers");
        write(&kept.join("seasons.json"), "written this session");
    });
    assert_eq!(f.snapshot(), original);
    assert_eq!(addon_storage::live_dir(&f.data, &f.game, "t.store"), kept);
    assert_eq!(
        std::fs::read_to_string(kept.join("careers.json")).unwrap(),
        "4 careers"
    );
    assert_eq!(
        std::fs::read_to_string(kept.join("slots/1/state.json")).unwrap(),
        "slot one"
    );

    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("4 careers"));
        assert_eq!(
            f.read("swofty/addons/t.store/seasons.json").as_deref(),
            Some("written this session")
        );
        assert_eq!(f.read(SLOT).as_deref(), Some("slot one"));
        assert!(!kept.join(addon_storage::MARK).exists());
        assert_eq!(
            f.read(&format!("swofty/addons/t.store/{}", addon_storage::MARK)),
            None
        );
        assert_eq!(
            addon_storage::live_dir(&f.data, &f.game, "t.store"),
            f.game.join("swofty/addons/t.store")
        );
    });
}

#[test]
fn a_restore_after_a_crash_keeps_what_the_game_wrote_in_the_data_folder() {
    let f = Fixture::new("readonly-crash");
    let original = f.snapshot();
    f.session(&f.with_addon(), plays);
    let kept = addon_storage::addon_dir(&f.data, "t.store");
    let lock = f.start(&f.data, &f.with_addon());
    age(&f.game.join(CAREERS));
    write(&kept.join("careers.json"), "4 careers");
    drop(lock);

    recovery::restore(&f.data, &f.game()).unwrap();
    assert_eq!(f.snapshot(), original);
    assert_eq!(
        std::fs::read_to_string(kept.join("careers.json")).unwrap(),
        "4 careers"
    );
}

#[test]
fn a_stored_copy_newer_than_a_leftover_in_the_game_folder_replaces_it() {
    let f = Fixture::new("newer-stored");
    f.session(&f.with_addon(), plays);
    let kept = addon_storage::addon_dir(&f.data, "t.store");
    let lock = f.start(&f.data, &f.with_addon());
    age(&f.game.join(CAREERS));
    write(&kept.join("careers.json"), "4 careers");
    drop(lock);
    std::fs::remove_dir_all(Journal::dir(&f.data)).unwrap();

    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("4 careers"));
        assert!(!kept.join("careers.json").exists());
    });
}

#[test]
fn an_older_stored_copy_gives_way_to_a_newer_leftover_in_the_game_folder() {
    let f = Fixture::new("older-stored");
    f.session(&f.with_addon(), plays);
    let kept = addon_storage::addon_dir(&f.data, "t.store");
    let lock = f.start(&f.data, &f.with_addon());
    f.put(CAREERS, "4 careers");
    write(&kept.join("careers.json"), "stale");
    age(&kept.join("careers.json"));
    drop(lock);
    std::fs::remove_dir_all(Journal::dir(&f.data)).unwrap();

    f.session(&f.with_addon(), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("4 careers"));
        assert!(!kept.join("careers.json").exists());
    });
    assert_eq!(
        std::fs::read_to_string(kept.join("careers.json")).unwrap(),
        "4 careers"
    );
}
