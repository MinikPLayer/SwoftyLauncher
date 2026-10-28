//! A modded session's game files must come back out of the game folder
//! however the session ends, and the next start must finish what an
//! unfinished session left. Everything runs on throwaway folders.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use swofty_core::baseline::{Identity, sha256_bytes};
use swofty_core::busy;
use swofty_core::install::{Drift, Game, Installer, OVERRIDE_HEADER, Plan};
use swofty_core::journal::Journal;
use swofty_core::placed::Placed;
use swofty_core::recovery::{self, Blocked, Notice, Scope, SessionLock};

const GVDB: &str = "game_package/gamedatabases/gamedatabases.2025.bin";
const UI: &str = "2025_asset_groups/ui_package/flow_frontend.erp";
const CORE: &str = "swofty/addons/core/swofty_core.dll";
const SETTINGS: &str = "swofty/settings.json";
const MIP: &str =
    "environment_package/tracks/suzuka24/mipmaps/f55b20d9a14b_pit_perch_ferrari_b_n.tif.2025.mipmaps";
const ARCHIVE: &str = "2025_asset_groups/environment_package/tracks/okayama/wep/okayama_common.erp";
const LAUNCHER: &str = "EAAntiCheat.GameServiceLauncher.exe";
const REG: &str = "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\n\n[Software\\\\Wine] 1700000000\n\"Version\"=\"win10\"\n";

struct Fixture {
    root: PathBuf,
    game: PathBuf,
    data: PathBuf,
    reg: PathBuf,
    src: PathBuf,
}

impl Fixture {
    fn new(name: &str) -> Self {
        let root = std::env::temp_dir().join(format!("swofty-session-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let f = Self {
            game: root.join("game"),
            data: root.join("data"),
            reg: root.join("pfx/user.reg"),
            src: root.join("src"),
            root,
        };
        for (rel, body) in [
            ("F1_25.exe", "exe-v1"),
            (GVDB, "db-v1"),
            (UI, "ui-v1"),
            (LAUNCHER, "anticheat"),
            ("environment_package/tracks/suzuka/suzuka.erp", "stock-suzuka"),
        ] {
            write(&f.game.join(rel), body);
        }
        write(&f.reg, REG);
        for (name, body) in [
            ("db", "db-modded"),
            ("ui", "ui-modded"),
            ("proxy", "proxy"),
            ("core", "core"),
            ("settings", "{}"),
            ("mip", "mip-ferrari"),
            ("archive", "okayama"),
            ("stub", "session-launcher"),
        ] {
            write(&f.src.join(name), body);
        }
        f
    }

    fn game(&self) -> Game {
        Game {
            dir: self.game.clone(),
            user_reg: Some(self.reg.clone()),
            identity: Identity::detect(&self.game, Some(1)).unwrap(),
        }
    }

    fn plan(&self) -> Plan {
        Plan {
            profile: "MODDED".into(),
            data: BTreeMap::from([
                (GVDB.to_owned(), self.src.join("db")),
                (UI.to_owned(), self.src.join("ui")),
            ]),
            added: BTreeMap::from([
                (MIP.to_owned(), self.src.join("mip")),
                (ARCHIVE.to_owned(), self.src.join("archive")),
            ]),
            loader: BTreeMap::from([
                ("version.dll".to_owned(), self.src.join("proxy")),
                (CORE.to_owned(), self.src.join("core")),
                (SETTINGS.to_owned(), self.src.join("settings")),
            ]),
            registry_override: true,
        }
    }

    fn session(&self, game: &Game) {
        let inst = Installer {
            session: true,
            ..Installer::new(&self.data, game)
        };
        inst.apply(&self.plan()).unwrap();
    }

    /// What the game and the offline play extension leave while a session
    /// runs: the loader's logs, a diagnostic dump, a crash report with its
    /// minidump, and the staged anti-cheat launcher with its journal.
    fn play(&self) {
        write(&self.game.join("swofty/loader.log"), "loader attached\n");
        write(&self.game.join("swofty/offline.log"), "[offline] launched\n");
        write(&self.game.join("swofty/ea-rdata.dump"), "dump");
        write(
            &self.game.join("swofty/crash-20261021-120000.txt"),
            "Swofty crash report\n",
        );
        write(&self.game.join("swofty/crash-20261021-120000.dmp"), "MDMP");
        std::fs::create_dir_all(self.game.join("swofty/mods/core")).unwrap();
        let launcher = self.game.join(LAUNCHER);
        let parked = self.game.join(format!("{LAUNCHER}.bak"));
        std::fs::rename(&launcher, &parked).unwrap();
        std::fs::copy(self.src.join("stub"), &launcher).unwrap();
        let journal = swofty_core::offline::journal_path(&self.data);
        let lines = [
            serde_json::json!({"op": "rename", "from": launcher, "to": parked}),
            serde_json::json!({"op": "copy", "from": self.src.join("stub"), "to": launcher,
                "sha256": sha256_bytes(b"session-launcher")}),
        ];
        let text: String = lines.iter().map(|l| format!("{l}\n")).collect();
        write(&journal, &text);
    }

    fn snapshot(&self) -> BTreeMap<String, String> {
        let mut out = BTreeMap::new();
        walk(&self.game, &self.game, &mut out);
        out.insert(
            "<user.reg>".into(),
            sha256_bytes(&std::fs::read(&self.reg).unwrap()),
        );
        out
    }

    fn read(&self, rel: &str) -> String {
        std::fs::read_to_string(self.game.join(rel)).unwrap()
    }

    fn lose_journals(&self) {
        std::fs::remove_dir_all(Journal::dir(&self.data)).unwrap();
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

#[test]
fn a_session_that_crashed_is_rolled_back_at_the_next_start() {
    let f = Fixture::new("crash");
    let original = f.snapshot();
    let game = f.game();
    let wrapper = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    f.session(&game);
    f.play();
    assert_eq!(f.read(MIP), "mip-ferrari");
    assert!(std::fs::read_to_string(&f.reg).unwrap().contains(OVERRIDE_HEADER));

    // The wrapper dies with the game (killed before its rollback): nothing is
    // undone, and its lock goes with it.
    drop(wrapper);
    let open = Journal::open(&f.data).unwrap();
    assert_eq!(open.len(), 1);
    assert!(open[0].session);
    let drift = Installer::new(&f.data, &game).verify().unwrap();
    assert!(
        drift.contains(&Drift::OpenJournal(open[0].id.clone())),
        "{drift:?}"
    );

    let lock = SessionLock::try_acquire(&f.data)
        .unwrap()
        .expect("no session is live");
    let r = recovery::recover(&lock, &f.data, &game, Scope::Sessions).unwrap();
    assert!(r.recovered());
    let lines = r.lines();
    assert!(
        lines.contains(&"rolled back the offline play staging".to_owned()),
        "{lines:?}"
    );
    assert!(
        lines.contains(&format!(
            "rolled back journal {} (17 change(s) for profile MODDED) that a session left open",
            open[0].id
        )),
        "{lines:?}"
    );
    assert_eq!(r.logs, 5);

    assert_eq!(f.snapshot(), original);
    assert_eq!(Installer::new(&f.data, &game).verify().unwrap(), []);
    assert!(Journal::open(&f.data).unwrap().is_empty());
    assert!(!Placed::path(&f.data).exists());
    let orphaned = f.data.join("sessions/orphaned-logs");
    assert_eq!(
        std::fs::read_to_string(orphaned.join("loader.log")).unwrap(),
        "loader attached\n"
    );
    assert_eq!(Notice::read(&f.data).unwrap().lines, lines);

    let again = recovery::recover(&lock, &f.data, &game, Scope::Sessions).unwrap();
    assert!(!again.recovered());
    assert!(again.lines().is_empty());
}

#[test]
fn a_session_rolled_back_when_the_game_exits_leaves_the_folder_byte_identical() {
    let f = Fixture::new("exit");
    let original = f.snapshot();
    let game = f.game();
    for _ in 0..2 {
        f.session(&game);
        f.play();
        swofty_core::offline::restore_pending(&f.data).unwrap();
        let inst = Installer::new(&f.data, &game);
        let done = inst.roll_back(false).unwrap();
        assert_eq!(done.len(), 1);
        assert!(done[0].drift.is_empty());
        let logs = f.data.join("sessions/one/game-logs");
        assert_eq!(inst.tidy(&logs).unwrap(), 5);
        assert!(logs.join("crash-20261021-120000.txt").is_file());
        assert!(logs.join("crash-20261021-120000.dmp").is_file());
        assert_eq!(f.snapshot(), original);
        assert_eq!(inst.verify().unwrap(), []);
    }
}

#[test]
fn a_live_session_is_never_rolled_back_underneath_it() {
    let f = Fixture::new("live");
    let game = f.game();
    let wrapper = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    f.session(&game);
    assert!(SessionLock::held_elsewhere(&f.data));
    assert!(SessionLock::try_acquire(&f.data).unwrap().is_none());
    assert!(
        recovery::restore(&f.data, &game)
            .unwrap_err()
            .to_string()
            .contains("still running")
    );
    assert_eq!(f.read(GVDB), "db-modded");
    drop(wrapper);
    assert!(!SessionLock::held_elsewhere(&f.data));
}

#[test]
fn launcher_start_keeps_a_swofty_apply_install() {
    let f = Fixture::new("apply");
    let original = f.snapshot();
    let game = f.game();
    Installer::new(&f.data, &game).apply(&f.plan()).unwrap();
    let lock = SessionLock::try_acquire(&f.data).unwrap().unwrap();

    let r = recovery::recover(&lock, &f.data, &game, Scope::Sessions).unwrap();
    assert!(!r.recovered());
    assert_eq!(f.read(GVDB), "db-modded");
    assert_eq!(f.read(MIP), "mip-ferrari");

    let r = recovery::recover(&lock, &f.data, &game, Scope::All).unwrap();
    assert_eq!(r.journals.len(), 1);
    assert!(!r.journals[0].session);
    assert_eq!(f.snapshot(), original);
}

#[test]
fn leftovers_whose_journal_was_lost_are_proven_ours_and_cleared() {
    let f = Fixture::new("lost");
    let original = f.snapshot();
    let game = f.game();
    f.session(&game);
    // A session from before the placed-files record, whose journal is gone:
    // only the bytes on disk are left to go by.
    f.lose_journals();
    std::fs::remove_file(Placed::path(&f.data)).unwrap();

    let inst = Installer {
        session: true,
        ..Installer::new(&f.data, &game)
    };
    let Err(swofty_core::Error::Leftovers(found)) = inst.apply(&f.plan()) else {
        panic!("the leftovers must stop the install");
    };
    assert!(found.iter().all(|c| c.owned), "{found:?}");
    let paths: Vec<&str> = found.iter().map(|c| c.path.as_str()).collect();
    assert_eq!(paths, [UI, GVDB, ARCHIVE, MIP, CORE, SETTINGS, "version.dll"]);
    assert!(found[0].replaced && !found[2].replaced);
    assert!(Journal::open(&f.data).unwrap().is_empty(), "nothing is written");

    let cleared = inst.clear(&found).unwrap();
    assert!(cleared.contains(&format!("removed {MIP}")), "{cleared:?}");
    assert!(
        cleared.contains(&format!("put the original {GVDB} back")),
        "{cleared:?}"
    );
    assert_eq!(f.read(GVDB), "db-v1");
    assert!(!f.game.join("environment_package/tracks/suzuka24").exists());
    assert!(f.game.join("environment_package/tracks/suzuka").is_dir());

    inst.apply(&f.plan()).unwrap();
    assert_eq!(f.read(MIP), "mip-ferrari");
    inst.roll_back(true).unwrap();
    std::fs::write(&f.reg, REG).unwrap();
    assert_eq!(f.snapshot(), original);
    assert_eq!(inst.verify().unwrap(), []);
}

#[test]
fn the_placed_record_proves_leftovers_after_an_addon_update() {
    let f = Fixture::new("record");
    let original = f.snapshot();
    let game = f.game();
    f.session(&game);
    f.lose_journals();
    // The addon was updated since: the leftovers no longer match what the
    // profile places, but they match what the launcher recorded writing.
    write(&f.src.join("mip"), "mip-ferrari-v2");
    write(&f.src.join("db"), "db-modded-v2");

    let inst = Installer::new(&f.data, &game);
    let Err(swofty_core::Error::Leftovers(found)) = inst.apply(&f.plan()) else {
        panic!("the leftovers must stop the install");
    };
    assert!(found.iter().all(|c| c.owned), "{found:?}");
    inst.clear(&found).unwrap();
    inst.apply(&f.plan()).unwrap();
    assert_eq!(f.read(MIP), "mip-ferrari-v2");
    assert_eq!(f.read(GVDB), "db-modded-v2");
    inst.roll_back(false).unwrap();
    assert_eq!(f.snapshot(), original);
    assert_eq!(inst.verify().unwrap(), []);
}

#[test]
fn the_next_start_sweeps_recorded_leftovers_before_anything_is_built() {
    let f = Fixture::new("sweep");
    let original = f.snapshot();
    let game = f.game();
    f.session(&game);
    f.lose_journals();

    let lock = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    let r = recovery::recover(&lock, &f.data, &game, Scope::Sessions).unwrap();
    assert!(r.recovered());
    assert!(r.swept.contains(&format!("removed {MIP}")), "{:?}", r.swept);
    assert!(
        r.swept.contains(&format!("put the original {UI} back")),
        "{:?}",
        r.swept
    );
    assert!(
        r.swept
            .contains(&"removed the version.dll override from the Proton prefix".to_owned())
    );
    assert_eq!(f.snapshot(), original);
    assert_eq!(Installer::new(&f.data, &game).verify().unwrap(), []);
}

#[test]
fn a_hand_copied_file_is_never_deleted_or_overwritten() {
    let f = Fixture::new("hand");
    let original = f.snapshot();
    let game = f.game();
    let inst = Installer::new(&f.data, &game);
    f.session(&game);
    inst.roll_back(false).unwrap();
    assert_eq!(f.snapshot(), original);

    write(&f.game.join(MIP), "a mod copied in by hand");
    write(&f.game.join(GVDB), "edited by another tool");
    let Err(swofty_core::Error::Leftovers(found)) = inst.apply(&f.plan()) else {
        panic!("files of unknown provenance must stop the install");
    };
    let paths: Vec<(&str, bool)> = found.iter().map(|c| (c.path.as_str(), c.owned)).collect();
    assert_eq!(paths, [(GVDB, false), (MIP, false)]);
    assert_eq!(
        swofty_core::Error::Leftovers(found.clone()).to_string(),
        format!("{GVDB} is not the original, and swofty did not write it (and 1 more)")
    );
    assert!(inst.clear(&found).unwrap().is_empty());
    assert_eq!(f.read(MIP), "a mod copied in by hand");
    assert_eq!(f.read(GVDB), "edited by another tool");
    assert!(Journal::open(&f.data).unwrap().is_empty());

    let blocked = Blocked::record(&f.data, &f.game, &found).unwrap();
    let explain = blocked.explain(&f.data);
    assert!(
        explain.starts_with(
            "Restore game folder (swofty restore) rolls back swofty's own changes and moves these files to"
        ),
        "{explain}"
    );
    assert!(
        explain.ends_with("the game's original files go back in their place"),
        "{explain}"
    );
    let done = recovery::restore(&f.data, &game).unwrap();
    assert_eq!(done.left, []);
    let kept: Vec<String> = done
        .set_aside
        .iter()
        .map(|p| std::fs::read_to_string(p).unwrap())
        .collect();
    assert_eq!(kept, ["edited by another tool", "a mod copied in by hand"]);
    assert!(done.set_aside[1].ends_with(MIP));
    assert!(Blocked::read(&f.data).is_none());
    assert_eq!(f.snapshot(), original);

    inst.apply(&f.plan()).unwrap();
    assert_eq!(f.read(MIP), "mip-ferrari");
}

#[test]
fn placed_paths_compare_like_the_game_folder() {
    let mut p = Placed::default();
    p.insert("Swofty\\Addons/core/x.dll", "ab".into(), false);
    assert!(p.wrote("Swofty/Addons/core/x.dll", "ab"));
    assert_eq!(p.wrote("swofty/addons/core/x.dll", "ab"), cfg!(windows));
}

#[test]
fn a_build_cancelled_mid_copy_leaves_the_game_folder_as_it_was() {
    let f = Fixture::new("cancel");
    let original = f.snapshot();
    let game = f.game();
    let wrapper = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    let seen = std::cell::RefCell::new(Vec::new());
    let progress = |done: usize, total: usize| {
        seen.borrow_mut().push((done, total));
        if done == 2 {
            busy::request_cancel(&f.data).unwrap();
        }
    };
    let inst = Installer {
        session: true,
        progress: &progress,
        ..Installer::new(&f.data, &game)
    };
    let Err(swofty_core::Error::Cancelled) = inst.apply(&f.plan()) else {
        panic!("the cancel must stop the copy");
    };
    let seen = seen.into_inner();
    assert_eq!(seen.last(), Some(&(2, 7)), "{seen:?}");
    assert_eq!(f.read(GVDB), "db-modded", "the copy stopped between files");
    assert_ne!(f.snapshot(), original);

    let done = Installer::new(&f.data, &game).roll_back(false).unwrap();
    assert_eq!(done.len(), 1);
    drop(wrapper);
    assert_eq!(f.snapshot(), original);
    assert!(Journal::open(&f.data).unwrap().is_empty());
    assert!(!SessionLock::held_elsewhere(&f.data));
}

#[test]
fn a_build_whose_process_died_is_cleaned_up_by_the_next_session() {
    let f = Fixture::new("died");
    let original = f.snapshot();
    let game = f.game();
    let wrapper = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    let mut b = busy::Busy::new("MODDED", vec!["mod.a".into()]);
    b.pid = b.pid.wrapping_add(1);
    b.step = busy::Step::Copying;
    b.done = 3;
    b.total = 7;
    b.since = 1_793_154_000.5;
    b.write(&f.data).unwrap();
    let inst = Installer {
        session: true,
        stop_after: Some(4),
        ..Installer::new(&f.data, &game)
    };
    inst.apply(&f.plan()).unwrap_err();
    assert_eq!(
        busy::Busy::live(&f.data).as_ref(),
        Some(&b),
        "busy while its process holds the lock"
    );
    drop(wrapper);
    assert_eq!(busy::Busy::live(&f.data), None, "its process is gone");
    busy::request_cancel(&f.data).unwrap();

    let lock = SessionLock::try_acquire(&f.data)
        .unwrap()
        .expect("no session is live");
    let r = recovery::recover(&lock, &f.data, &game, Scope::Sessions).unwrap();
    assert!(r.recovered());
    assert!(
        r.lines().contains(
            &"the last Play stopped while copying the mods into the game folder (3 of 7 files); Play starts the build again"
                .to_owned()
        ),
        "{:?}",
        r.lines()
    );
    assert_eq!(f.snapshot(), original);
    assert_eq!(busy::Busy::read(&f.data), None);
    assert!(
        !busy::cancel_requested(&f.data),
        "an old cancel never stops the next build"
    );
}
