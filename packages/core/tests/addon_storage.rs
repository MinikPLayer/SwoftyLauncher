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

#[test]
fn each_profile_keeps_its_own_copy_of_an_addons_files() {
    let f = Fixture::new("per-profile");
    let profile = |name: &str| {
        let mut p = f.with_addon();
        p.profile = name.into();
        p
    };
    f.session(&profile("real"), |f| f.put(CAREERS, "real history"));
    addon_storage::profile_created(&f.data, "whatif", None).unwrap();
    f.session(&profile("whatif"), |f| {
        assert_eq!(f.read(CAREERS), None);
        f.put(CAREERS, "what if");
    });
    f.session(&profile("real"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("real history"));
    });
    f.session(&profile("whatif"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("what if"));
    });
    addon_storage::profile_created(&f.data, "copy", Some("whatif")).unwrap();
    f.session(&profile("copy"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("what if"));
        f.put(CAREERS, "copied, then changed");
    });
    f.session(&profile("whatif"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("what if"));
    });
    let copies = addon_storage::copies_dir(&f.data, "t.store");
    addon_storage::profile_renamed(&f.data, "copy", "renamed").unwrap();
    assert!(copies.join("renamed/careers.json").is_file());
    addon_storage::profile_deleted(&f.data, "renamed").unwrap();
    assert!(!copies.join("renamed").exists());
    assert!(!f.game.join("swofty/addons/t.store/.profiles").exists());
}

#[test]
fn profiles_from_before_16_start_from_the_15_storage_and_then_keep_their_own() {
    let f = Fixture::new("per-profile-seed");
    let kept = addon_storage::addon_dir(&f.data, "t.store");
    write(&kept.join("careers.json"), "3 careers");
    let profile = |name: &str| {
        let mut p = f.with_addon();
        p.profile = name.into();
        p
    };
    f.session(&profile("one"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("3 careers"));
        f.put(CAREERS, "one's");
    });
    f.session(&profile("two"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("3 careers"));
        f.put(CAREERS, "two's");
    });
    f.session(&profile("one"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("one's"));
    });
    f.session(&profile("two"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("two's"));
    });
    assert!(!kept.join(".profiles").exists());
}

fn as_profile(f: &Fixture, name: &str) -> Plan {
    let mut p = f.with_addon();
    p.profile = name.into();
    p
}

fn forget_profiles(f: &Fixture) {
    std::fs::remove_dir_all(addon_storage::copies_dir(&f.data, "t.store")).unwrap();
}

#[test]
fn w8_review_a_new_profile_made_before_the_first_16_session_keeps_the_old_storage() {
    let f = Fixture::new("w8-new-before-first");
    f.session(&as_profile(&f, "main"), plays);
    forget_profiles(&f);
    let kept = addon_storage::addon_dir(&f.data, "t.store").join("careers.json");
    assert_eq!(
        std::fs::read_to_string(&kept).unwrap(),
        "3 careers",
        "the 1.5 storage as the 1.5 launcher left it"
    );
    addon_storage::profile_created(&f.data, "fresh", None).unwrap();
    f.session(&as_profile(&f, "fresh"), |f| assert_eq!(f.read(CAREERS), None));
    f.session(&as_profile(&f, "main"), |f| {
        assert_eq!(
            f.read(CAREERS).as_deref(),
            Some("3 careers"),
            "main's 1.5 careers are gone"
        );
    });
}

#[test]
fn w8_review_renaming_a_profile_while_its_session_runs_keeps_that_session() {
    let f = Fixture::new("w8-rename-running");
    f.session(&as_profile(&f, "real"), |f| f.put(CAREERS, "season 1"));
    let lock = f.start(&f.data, &as_profile(&f, "real"));
    f.put(CAREERS, "season 2");
    addon_storage::profile_created(&f.data, "renamed", Some("real")).unwrap();
    addon_storage::profile_deleted(&f.data, "real").unwrap();
    f.end(&f.data, lock);
    f.session(&as_profile(&f, "renamed"), |f| {
        assert_eq!(
            f.read(CAREERS).as_deref(),
            Some("season 2"),
            "the running session's writes are lost"
        );
    });
}

#[test]
fn w8_review_a_deleted_profile_name_reused_starts_empty() {
    let f = Fixture::new("w8-delete-reuse");
    f.session(&as_profile(&f, "a"), |f| f.put(CAREERS, "a's"));
    addon_storage::profile_created(&f.data, "b", None).unwrap();
    f.session(&as_profile(&f, "b"), |f| f.put(CAREERS, "b's"));
    addon_storage::profile_deleted(&f.data, "b").unwrap();
    f.session(&as_profile(&f, "a"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("a's"))
    });
    addon_storage::profile_created(&f.data, "b", None).unwrap();
    f.session(&as_profile(&f, "b"), |f| {
        assert_eq!(f.read(CAREERS), None, "the deleted profile b's files came back");
    });
}

#[cfg(unix)]
#[test]
fn w8_review_a_profile_switch_that_fails_half_way_keeps_both_copies() {
    use std::os::unix::fs::PermissionsExt;
    let f = Fixture::new("w8-half-way");
    f.session(&as_profile(&f, "real"), plays);
    addon_storage::profile_created(&f.data, "other", None).unwrap();
    f.session(&as_profile(&f, "other"), |f| f.put(CAREERS, "other's"));
    let stuck = addon_storage::profile_dir(&f.data, "t.store", "real").join("slots/1/state.json");
    std::fs::set_permissions(&stuck, std::fs::Permissions::from_mode(0o000)).unwrap();
    {
        let lock = SessionLock::try_acquire(&f.data).unwrap().unwrap();
        let game = f.game();
        recovery::recover(&lock, &f.data, &game, Scope::All).unwrap();
        let failed = Installer {
            session: true,
            ..Installer::new(&f.data, &game)
        }
        .apply(&as_profile(&f, "real"));
        assert!(failed.is_err(), "the copy back of real's files should fail");
        drop(lock);
    }
    std::fs::set_permissions(&stuck, std::fs::Permissions::from_mode(0o644)).unwrap();
    f.session(&as_profile(&f, "real"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("3 careers"));
        assert_eq!(
            f.read(SLOT).as_deref(),
            Some("slot one"),
            "real's slot file is gone"
        );
    });
    f.session(&as_profile(&f, "other"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("other's"))
    });
}

#[test]
fn w8_review_a_new_empty_profile_doesnt_see_another_profiles_files_of_an_addon_stored_later() {
    let f = Fixture::new("w8-later-addon");
    f.session(&as_profile(&f, "main"), |_| {});
    std::fs::remove_dir_all(addon_storage::addon_dir(&f.data, "t.store")).ok();
    addon_storage::profile_created(&f.data, "fresh", None).unwrap();
    f.session(&as_profile(&f, "main"), |f| f.put(CAREERS, "main's"));
    f.session(&as_profile(&f, "fresh"), |f| {
        assert_eq!(f.read(CAREERS), None, "fresh sees main's files");
    });
}

#[test]
fn a_profile_switch_cut_off_half_way_is_finished_at_the_next_start() {
    let f = Fixture::new("swap-cut");
    let profile = |name: &str| {
        let mut p = f.with_addon();
        p.profile = name.into();
        p
    };
    f.session(&profile("a"), |f| f.put(CAREERS, "a's"));
    addon_storage::profile_created(&f.data, "b", None).unwrap();
    f.session(&profile("b"), |f| f.put(CAREERS, "b's"));
    let copies = addon_storage::copies_dir(&f.data, "t.store");
    let storage = addon_storage::addon_dir(&f.data, "t.store");
    write(&copies.join(".incoming/careers.json"), "a's");
    write(&copies.join(".incoming.ready"), "a");
    std::fs::rename(&storage, copies.join(".outgoing")).unwrap();
    f.session(&profile("a"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("a's"))
    });
    f.session(&profile("b"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("b's"))
    });
    assert!(!copies.join(".incoming").exists());
    assert!(!copies.join(".outgoing").exists());
}

#[test]
fn a_profile_switch_whose_copy_never_finished_leaves_the_storage_as_it_was() {
    let f = Fixture::new("swap-partial");
    let profile = |name: &str| {
        let mut p = f.with_addon();
        p.profile = name.into();
        p
    };
    f.session(&profile("a"), |f| f.put(CAREERS, "a's"));
    addon_storage::profile_created(&f.data, "b", None).unwrap();
    f.session(&profile("b"), |f| f.put(CAREERS, "b's"));
    let copies = addon_storage::copies_dir(&f.data, "t.store");
    write(&copies.join(".incoming/careers.json"), "half");
    f.session(&profile("b"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("b's"))
    });
    f.session(&profile("a"), |f| {
        assert_eq!(f.read(CAREERS).as_deref(), Some("a's"))
    });
}

#[test]
fn restoring_the_game_folder_keeps_the_career_index_and_the_stat_id_store() {
    let f = Fixture::new("restore-registry");
    let original = f.snapshot();
    let career = r#"[{ "career": "6f3e9a", "label": "Progressive", "addons": [ { "id": "t.store" } ] }]"#;
    f.session(&f.with_addon(), |f| {
        f.put("swofty/careers/index.json", career);
        f.put("swofty/registry/ids.json", "{\"track\":{\"mod.sochi/sochi\":18}}");
    });
    recovery::restore(&f.data, &f.game()).unwrap();
    assert_eq!(f.snapshot(), original);
    let kept = swofty_core::stdlib::careers::read(&swofty_core::stdlib::careers::data_path(&f.data));
    assert_eq!(kept.len(), 1);
    assert_eq!(kept[0].profile.as_deref(), Some("with"));
    assert_eq!(
        std::fs::read_to_string(swofty_core::stdlib::careers::kept_ids(&f.data)).unwrap(),
        "{\"track\":{\"mod.sochi/sochi\":18}}"
    );
    f.session(&f.with_addon(), |f| {
        assert!(f.read("swofty/registry/ids.json").is_none());
        let state = f
            .read("swofty/state.json")
            .expect("the install names the state folder");
        assert!(state.contains("\"state_dir\""), "{state}");
    });
}
