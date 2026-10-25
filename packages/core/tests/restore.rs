use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use swofty_core::baseline::{Identity, sha256_bytes};
use swofty_core::install::{Drift, Game, Installer, OVERRIDE_HEADER, Plan};
use swofty_core::journal::{Journal, Op, State};

const GVDB: &str = "game_package/gamedatabases/gamedatabases.2025.bin";
const UI: &str = "2025_asset_groups/ui_package/flow_frontend.erp";
const CORE: &str = "swofty/addons/core/swofty_core.dll";
const SCREEN: &str = "swofty/ui/swofty_addon_0.ui";
const STOCK_TRACK: &str = "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_common.erp";
const MAP_FILES: &[&str] = &[
    "2025_asset_groups/environment_package/tracks/okayama/wep/okayama_common.erp",
    "2025_asset_groups/environment_package/tracks/okayama/wep/okayama_f1_26.erp",
    "2025_asset_groups/environment_package/tracks/okayama/ai/f1_2026_vehicle_package/haas/haas.erp",
    "2025_asset_groups/ui_package/textures/track_maps/okayama.erp",
    "asset_groups/environment_package/tracks/okayama/mipmaps/3b5d3c7d207e_bisect_placeholder.tif.2025.mipmaps",
];
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
        let root = std::env::temp_dir().join(format!("swofty-restore-{name}-{}", std::process::id()));
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
            ("readme.txt", "r-v1"),
            (STOCK_TRACK, "track-v1"),
        ] {
            write(&f.game.join(rel), body);
        }
        write(&f.reg, REG);
        for (name, body) in [
            ("dbA", "db-A"),
            ("dbB", "db-B"),
            ("uiB", "ui-B"),
            ("proxy", "proxy"),
            ("core", "core"),
            ("screen", "screen"),
        ] {
            write(&f.src.join(name), body);
        }
        for (i, _) in MAP_FILES.iter().enumerate() {
            write(&f.src.join(format!("map{i}")), &format!("map-{i}"));
        }
        f
    }

    fn maps_plan(&self) -> Plan {
        let mut p = self.plan("a");
        p.profile = "maps".into();
        p.data.insert(UI.into(), self.src.join("uiB"));
        p.added = MAP_FILES
            .iter()
            .enumerate()
            .map(|(i, rel)| ((*rel).to_owned(), self.src.join(format!("map{i}"))))
            .collect();
        p
    }

    fn game(&self, build: &str) -> Game {
        Game {
            dir: self.game.clone(),
            user_reg: Some(self.reg.clone()),
            identity: Identity::detect(&self.game, Some(build.parse().unwrap())).unwrap(),
        }
    }

    fn plan(&self, profile: &str) -> Plan {
        let mut data = BTreeMap::from([(
            GVDB.to_owned(),
            self.src.join(if profile == "a" { "dbA" } else { "dbB" }),
        )]);
        let mut loader = BTreeMap::from([
            ("version.dll".to_owned(), self.src.join("proxy")),
            (CORE.to_owned(), self.src.join("core")),
        ]);
        if profile == "b" {
            data.insert(UI.into(), self.src.join("uiB"));
            loader.insert(SCREEN.into(), self.src.join("screen"));
        }
        Plan {
            profile: profile.into(),
            data,
            added: BTreeMap::new(),
            loader,
            registry_override: true,
        }
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
}

impl Drop for Fixture {
    fn drop(&mut self) {
        std::fs::remove_dir_all(&self.root).ok();
    }
}

fn write(path: &Path, body: &str) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    // Replace rather than write through: a placed file may be a read-only link.
    std::fs::remove_file(path).ok();
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
fn profile_switches_return_to_the_baseline() {
    let f = Fixture::new("switch");
    let original = f.snapshot();
    let game = f.game("1");
    let inst = Installer::new(&f.data, &game);

    inst.apply(&f.plan("a")).unwrap();
    assert_eq!(f.read(GVDB), "db-A");
    assert_eq!(f.read("version.dll"), "proxy");
    assert!(std::fs::read_to_string(&f.reg).unwrap().contains(OVERRIDE_HEADER));

    inst.apply(&f.plan("b")).unwrap();
    assert_eq!(f.read(GVDB), "db-B");
    assert_eq!(f.read(UI), "ui-B");
    assert_eq!(f.read(SCREEN), "screen");
    assert_eq!(Journal::open(&f.data).unwrap().len(), 1);

    inst.apply(&f.plan("a")).unwrap();
    assert!(!f.game.join(SCREEN).exists());
    assert_eq!(f.read(CORE), "core");

    inst.apply(&Plan::vanilla()).unwrap();
    assert_eq!(f.snapshot(), original);
    assert!(Journal::open(&f.data).unwrap().is_empty());
    assert_eq!(inst.verify().unwrap(), []);
    assert_eq!(inst.restore().unwrap(), []);
    assert_eq!(f.snapshot(), original);
}

#[test]
fn a_kill_between_any_two_steps_rolls_back_to_the_baseline() {
    let total = 7;
    for k in 0..=total {
        let f = Fixture::new(&format!("kill{k}"));
        let original = f.snapshot();
        let game = f.game("1");
        let mut inst = Installer::new(&f.data, &game);
        inst.stop_after = Some(k);
        let result = inst.apply(&f.plan("a"));
        assert_eq!(result.is_ok(), k == total, "k={k}");

        let open = Journal::open(&f.data).unwrap();
        assert_eq!(open.len(), 1, "k={k}");
        if let Some(e) = open[0].entries.get(k).filter(|e| e.state == State::Pending) {
            match e.op {
                Op::ReplaceFile | Op::CreateFile => write(&f.game.join(&e.path), "partial"),
                Op::CreateDir | Op::LinkDir => std::fs::create_dir_all(f.game.join(&e.path)).unwrap(),
                Op::RegistryOverride => {
                    let mut t = std::fs::read_to_string(&f.reg).unwrap();
                    t.push_str(&format!("\n{OVERRIDE_HEADER}\n"));
                    std::fs::write(&f.reg, t).unwrap();
                }
            }
        }

        let restarted = Installer::new(&f.data, &game);
        assert_eq!(restarted.recover().unwrap(), [], "k={k}");
        assert_eq!(f.snapshot(), original, "k={k}");
        assert_eq!(restarted.verify().unwrap(), [], "k={k}");
    }
}

#[test]
fn an_install_the_free_space_cannot_hold_changes_nothing() {
    let f = Fixture::new("space");
    let original = f.snapshot();
    let game = f.game("1");
    let short = Installer {
        free_space: |_| Some(1),
        ..Installer::new(&f.data, &game)
    };
    let err = short.apply(&f.maps_plan()).unwrap_err();
    assert!(
        matches!(err, swofty_core::Error::NoSpace { free: 1, .. }),
        "{err}"
    );
    assert!(err.to_string().starts_with("not enough free space on "), "{err}");
    assert!(err.to_string().contains(" GB, 0.0 GB free"), "{err}");
    assert_eq!(f.snapshot(), original);
    assert!(Journal::open(&f.data).unwrap().is_empty());
    assert!(
        !f.data.join("baseline/objects").exists(),
        "no original was backed up"
    );

    let roomy = Installer {
        free_space: |_| Some(u64::MAX),
        ..Installer::new(&f.data, &game)
    };
    roomy.apply(&f.maps_plan()).unwrap();
    assert_eq!(f.read(UI), "ui-B");
}

#[test]
fn an_install_of_thousands_of_files_writes_the_journal_once() {
    let f = Fixture::new("many");
    let original = f.snapshot();
    let game = f.game("1");
    let mut plan = f.plan("a");
    for i in 0..10_000 {
        let src = f.src.join(format!("many/{i}"));
        write(&src, &i.to_string());
        plan.added
            .insert(format!("2025_asset_groups/many/{}/{i}.erp", i % 50), src);
    }
    let started = std::time::Instant::now();
    Installer::new(&f.data, &game).apply(&plan).unwrap();
    assert!(
        started.elapsed() < std::time::Duration::from_secs(60),
        "{:?}",
        started.elapsed()
    );
    let open = Journal::open(&f.data).unwrap();
    assert!(open[0].committed && open[0].entries.iter().all(|e| e.state == State::Done));
    assert_eq!(open[0].entries.len(), 7 + 10_000 + 51);
    assert_eq!(f.read("2025_asset_groups/many/7/9957.erp"), "9957");

    let mut stopped = Installer::new(&f.data, &game);
    stopped.stop_after = Some(5_000);
    assert!(stopped.apply(&plan).is_err());
    assert!(
        Journal::open(&f.data).unwrap()[0]
            .entries
            .iter()
            .all(|e| e.state == State::Pending)
    );
    assert_eq!(Installer::new(&f.data, &game).recover().unwrap(), []);
    assert_eq!(f.snapshot(), original);
}

#[test]
fn a_game_update_in_between_never_restores_old_originals() {
    let f = Fixture::new("update");
    let game1 = f.game("1");
    Installer::new(&f.data, &game1).apply(&f.plan("a")).unwrap();
    assert_eq!(f.read(GVDB), "db-A");

    for (rel, body) in [("F1_25.exe", "exe-v2"), (GVDB, "db-v2"), ("readme.txt", "r-v2")] {
        write(&f.game.join(rel), body);
    }
    let game2 = f.game("2");
    let inst = Installer::new(&f.data, &game2);
    let report = inst.restore().unwrap();
    assert!(report.contains(&Drift::LeftInPlace(GVDB.into())), "{report:?}");
    assert_eq!(f.read(GVDB), "db-v2");
    assert!(!f.game.join("version.dll").exists());
    assert!(!f.game.join("swofty").exists());

    let updated = f.snapshot();
    assert_eq!(updated["<user.reg>"], sha256_bytes(REG.as_bytes()));

    inst.apply(&f.plan("a")).unwrap();
    assert_eq!(f.read(GVDB), "db-A");
    assert_eq!(
        inst.baseline().unwrap().files[GVDB].sha256,
        sha256_bytes(b"db-v2")
    );
    inst.apply(&Plan::vanilla()).unwrap();
    assert_eq!(f.snapshot(), updated);
    assert_eq!(inst.verify().unwrap(), []);
}

#[test]
fn drift_from_outside_is_reported_and_restored() {
    let f = Fixture::new("drift");
    let original = f.snapshot();
    let game = f.game("1");
    let inst = Installer::new(&f.data, &game);
    inst.apply(&f.plan("a")).unwrap();
    inst.apply(&Plan::vanilla()).unwrap();

    write(&f.game.join(GVDB), "edited by another tool");
    write(&f.game.join("version.dll"), "someone else's proxy");
    let drift = inst.verify().unwrap();
    assert!(drift.contains(&Drift::Changed(GVDB.into())), "{drift:?}");
    assert!(
        drift.contains(&Drift::Addition("version.dll".into())),
        "{drift:?}"
    );
    assert!(inst.apply(&f.plan("a")).is_err());

    assert_eq!(inst.restore().unwrap(), []);
    assert_eq!(f.snapshot(), original);
}

#[test]
fn map_files_in_new_folders_are_removed_by_switches_and_restore() {
    let f = Fixture::new("maps");
    let original = f.snapshot();
    let game = f.game("1");
    let inst = Installer::new(&f.data, &game);

    inst.apply(&f.maps_plan()).unwrap();
    for (i, rel) in MAP_FILES.iter().enumerate() {
        assert_eq!(f.read(rel), format!("map-{i}"));
    }
    assert_eq!(f.read(STOCK_TRACK), "track-v1");

    inst.apply(&f.plan("a")).unwrap();
    assert!(!f.game.join("asset_groups").exists());
    assert!(
        !f.game
            .join("2025_asset_groups/environment_package/tracks/okayama")
            .exists()
    );
    assert!(!f.game.join("2025_asset_groups/ui_package/textures").exists());
    assert!(f.game.join(STOCK_TRACK).is_file());

    inst.apply(&f.maps_plan()).unwrap();
    assert_eq!(inst.restore().unwrap(), []);
    assert_eq!(f.snapshot(), original);

    inst.apply(&f.maps_plan()).unwrap();
    inst.apply(&Plan::vanilla()).unwrap();
    assert_eq!(f.snapshot(), original);
    assert_eq!(inst.verify().unwrap(), []);
}

#[test]
fn a_kill_while_installing_maps_rolls_back_to_the_baseline() {
    let total = {
        let f = Fixture::new("mapcount");
        let game = f.game("1");
        Installer::new(&f.data, &game).apply(&f.maps_plan()).unwrap();
        Journal::open(&f.data).unwrap()[0].entries.len()
    };
    for k in 0..=total {
        let f = Fixture::new(&format!("mapkill{k}"));
        let original = f.snapshot();
        let game = f.game("1");
        let mut inst = Installer::new(&f.data, &game);
        inst.stop_after = Some(k);
        assert_eq!(inst.apply(&f.maps_plan()).is_ok(), k == total, "k={k}");
        let open = Journal::open(&f.data).unwrap();
        if let Some(e) = open[0].entries.get(k).filter(|e| e.state == State::Pending) {
            match e.op {
                Op::ReplaceFile | Op::CreateFile => write(&f.game.join(&e.path), "partial"),
                Op::CreateDir | Op::LinkDir => std::fs::create_dir_all(f.game.join(&e.path)).unwrap(),
                Op::RegistryOverride => {}
            }
        }
        let restarted = Installer::new(&f.data, &game);
        assert_eq!(restarted.recover().unwrap(), [], "k={k}");
        assert_eq!(f.snapshot(), original, "k={k}");
        assert_eq!(restarted.verify().unwrap(), [], "k={k}");
    }
}

/// A stock file moved aside for a link waits at this hidden name.
#[cfg(target_os = "linux")]
fn aside(f: &Fixture, rel: &str) -> PathBuf {
    let path = f.game.join(rel);
    path.with_file_name(format!(
        ".{}.swofty-orig",
        path.file_name().unwrap().to_string_lossy()
    ))
}

#[cfg(target_os = "linux")]
fn inode(path: &Path) -> u64 {
    use std::os::unix::fs::MetadataExt;
    std::fs::symlink_metadata(path).unwrap().ino()
}

/// No hidden original anywhere in the game folder.
#[cfg(target_os = "linux")]
fn no_asides(f: &Fixture) {
    for rel in f.snapshot().keys() {
        assert!(!rel.contains(".swofty-orig"), "{rel} was left behind");
    }
}

#[cfg(target_os = "linux")]
#[test]
fn a_replaced_file_waits_beside_its_link_and_is_renamed_back() {
    let f = Fixture::new("aside-normal");
    let original = f.snapshot();
    let game = f.game("1");
    let inst = Installer::new(&f.data, &game);
    let ino = inode(&f.game.join(GVDB));
    inst.apply(&f.plan("a")).unwrap();
    assert!(
        std::fs::symlink_metadata(f.game.join(GVDB))
            .unwrap()
            .file_type()
            .is_symlink()
    );
    assert_eq!(f.read(GVDB), "db-A");
    assert_eq!(std::fs::read_to_string(aside(&f, GVDB)).unwrap(), "db-v1");
    assert_eq!(inode(&aside(&f, GVDB)), ino, "moved, not copied");

    let done = inst.roll_back(false).unwrap();
    assert_eq!(done.len(), 1);
    assert_eq!(done[0].drift, []);
    assert_eq!(f.snapshot(), original);
    assert_eq!(inode(&f.game.join(GVDB)), ino, "renamed back, not copied");
    no_asides(&f);
    assert_eq!(inst.verify().unwrap(), []);
}

#[cfg(target_os = "linux")]
#[test]
fn a_crash_after_the_original_moved_aside_puts_it_back() {
    for linked in [false, true] {
        let f = Fixture::new(&format!("aside-crash{linked}"));
        let original = f.snapshot();
        let game = f.game("1");
        let ino = inode(&f.game.join(GVDB));
        let mut inst = Installer::new(&f.data, &game);
        // The entries are journalled; the first one is the replaced file.
        inst.stop_after = Some(usize::from(linked));
        assert!(inst.apply(&f.plan("a")).is_err());
        if !linked {
            // Killed between the two renames: only the hidden name holds it.
            // Nothing vouches for it (the hash record was never saved), so the
            // backup is copied back instead.
            std::fs::rename(f.game.join(GVDB), aside(&f, GVDB)).unwrap();
            assert!(!f.game.join(GVDB).exists());
        }
        let open = Journal::open(&f.data).unwrap();
        assert_eq!(open[0].entries[0].state, State::Pending);

        let restarted = Installer::new(&f.data, &game);
        assert_eq!(restarted.recover().unwrap(), [], "linked={linked}");
        assert_eq!(f.snapshot(), original, "linked={linked}");
        if linked {
            assert_eq!(inode(&f.game.join(GVDB)), ino, "renamed back");
        }
        no_asides(&f);
        assert_eq!(restarted.verify().unwrap(), []);
    }
}

#[cfg(target_os = "linux")]
#[test]
fn a_hidden_original_deleted_by_hand_is_restored_from_the_backup() {
    let f = Fixture::new("aside-deleted");
    let original = f.snapshot();
    let game = f.game("1");
    let inst = Installer::new(&f.data, &game);
    inst.apply(&f.plan("a")).unwrap();
    std::fs::remove_file(aside(&f, GVDB)).unwrap();
    assert_eq!(inst.roll_back(false).unwrap()[0].drift, []);
    assert_eq!(f.snapshot(), original);
    no_asides(&f);
    assert_eq!(inst.verify().unwrap(), []);
}

#[cfg(target_os = "linux")]
#[test]
fn a_hidden_original_that_was_changed_is_not_trusted() {
    // A different size, and the same size with other bytes.
    for body in ["not the original at all", "db-v2"] {
        let f = Fixture::new(&format!("aside-changed{}", body.len()));
        let original = f.snapshot();
        let game = f.game("1");
        let inst = Installer::new(&f.data, &game);
        inst.apply(&f.plan("a")).unwrap();
        std::fs::write(aside(&f, GVDB), body).unwrap();
        assert_eq!(inst.roll_back(false).unwrap()[0].drift, [], "{body}");
        assert_eq!(f.read(GVDB), "db-v1", "{body}");
        assert_eq!(f.snapshot(), original, "{body}");
        no_asides(&f);
        assert_eq!(inst.verify().unwrap(), []);
    }
}

#[cfg(target_os = "linux")]
#[test]
fn an_install_right_after_a_rollback_moves_the_original_aside_again() {
    let f = Fixture::new("aside-twice");
    let original = f.snapshot();
    let game = f.game("1");
    let inst = Installer::new(&f.data, &game);
    let ino = inode(&f.game.join(GVDB));
    for _ in 0..3 {
        inst.apply(&f.plan("a")).unwrap();
        assert_eq!(f.read(GVDB), "db-A");
        assert_eq!(inode(&aside(&f, GVDB)), ino);
        inst.roll_back(false).unwrap();
        assert_eq!(f.snapshot(), original);
        no_asides(&f);
    }
    // A different profile over a rolled-back one, and a restore at the end.
    inst.apply(&f.plan("b")).unwrap();
    inst.apply(&f.plan("a")).unwrap();
    assert_eq!(std::fs::read_to_string(aside(&f, GVDB)).unwrap(), "db-v1");
    assert!(!aside(&f, UI).exists());
    inst.restore().unwrap();
    assert_eq!(f.snapshot(), original);
    no_asides(&f);
}

#[cfg(target_os = "linux")]
#[test]
fn originals_left_aside_without_a_journal_are_settled() {
    let f = Fixture::new("aside-nojournal");
    let original = f.snapshot();
    let game = f.game("1");
    let inst = Installer::new(&f.data, &game);
    inst.apply(&f.plan("a")).unwrap();
    // The journal is lost; the link and the hidden original remain.
    for j in Journal::open(&f.data).unwrap() {
        j.close().unwrap();
    }
    assert!(
        inst.verify()
            .unwrap()
            .iter()
            .any(|d| matches!(d, Drift::Addition(p) if p.ends_with(".swofty-orig")))
    );
    inst.tidy(&f.data.join("logs")).unwrap();
    assert_eq!(f.read(GVDB), "db-v1");
    no_asides(&f);
    inst.restore().unwrap();
    assert_eq!(f.snapshot(), original);
}
