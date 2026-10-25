//! A crash or power loss while the launcher writes a state file can leave it
//! empty or zero-filled. None of them may brick a launch: a damaged file is
//! set aside (never deleted), and the baseline of original game files is only
//! rebuilt from game files that are provably originals. Everything runs on
//! throwaway folders.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use swofty_core::baseline::{Baseline, DAMAGED, Identity, sha256_bytes};
use swofty_core::install::{Drift, Game, Installer, Plan};
use swofty_core::journal::Journal;
use swofty_core::placed::Placed;
use swofty_core::recovery::{self, Scope, SessionLock};
use swofty_core::store::Store;

const GVDB: &str = "game_package/gamedatabases/gamedatabases.2025.bin";
const UI: &str = "2025_asset_groups/ui_package/flow_frontend.erp";
const CORE: &str = "swofty/addons/core/swofty_core.dll";

struct Fixture {
    root: PathBuf,
    game: PathBuf,
    data: PathBuf,
    src: PathBuf,
}

impl Fixture {
    fn new(name: &str) -> Self {
        let root = std::env::temp_dir().join(format!("swofty-damaged-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let f = Self {
            game: root.join("game"),
            data: root.join("data"),
            src: root.join("src"),
            root,
        };
        for (rel, body) in [("F1_25.exe", "exe-v1"), (GVDB, "db-v1"), (UI, "ui-v1")] {
            write(&f.game.join(rel), body);
        }
        for (name, body) in [
            ("db", "db-modded"),
            ("ui", "ui-modded"),
            ("proxy", "proxy"),
            ("core", "core"),
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

    fn plan(&self, data: &[&str]) -> Plan {
        let src = |rel: &str| self.src.join(if rel == GVDB { "db" } else { "ui" });
        Plan {
            profile: "MODDED".into(),
            data: data.iter().map(|rel| ((*rel).to_owned(), src(rel))).collect(),
            added: BTreeMap::new(),
            loader: BTreeMap::from([
                ("version.dll".to_owned(), self.src.join("proxy")),
                (CORE.to_owned(), self.src.join("core")),
            ]),
            registry_override: false,
        }
    }

    fn installer<'a>(&'a self, game: &'a Game) -> Installer<'a> {
        Installer {
            session: true,
            ..Installer::new(&self.data, game)
        }
    }

    fn baseline_path(&self, game: &Game) -> PathBuf {
        Baseline::path(&self.data, &game.identity)
    }

    fn set_aside(&self, path: &Path) -> Vec<PathBuf> {
        let name = format!("{}.corrupt-", path.file_name().unwrap().to_string_lossy());
        std::fs::read_dir(path.parent().unwrap())
            .unwrap()
            .flatten()
            .map(|e| e.path())
            .filter(|p| p.file_name().unwrap().to_string_lossy().starts_with(&name))
            .collect()
    }

    fn read(&self, rel: &str) -> String {
        std::fs::read_to_string(self.game.join(rel)).unwrap()
    }

    fn vanilla(&self) {
        assert_eq!(self.read(GVDB), "db-v1");
        assert_eq!(self.read(UI), "ui-v1");
        assert!(!self.game.join("version.dll").exists());
        assert!(!self.game.join("swofty").exists());
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

/// What NTFS leaves when power is lost after a rename whose data was never
/// flushed: the file's length survives, its bytes do not.
fn zero_fill(path: &Path) {
    let len = std::fs::metadata(path).unwrap().len() as usize;
    std::fs::write(path, vec![0u8; len]).unwrap();
}

fn one_session(f: &Fixture, game: &Game, data: &[&str]) {
    let inst = f.installer(game);
    inst.apply(&f.plan(data)).unwrap();
    assert_eq!(f.read(GVDB), "db-modded");
    inst.roll_back(false).unwrap();
}

#[test]
fn an_empty_or_zero_filled_baseline_is_set_aside_and_rebuilt_from_backed_up_originals() {
    for (name, damage) in [
        ("empty", (|p: &Path| std::fs::write(p, b"").unwrap()) as fn(&Path)),
        ("zeros", zero_fill),
        ("garbage", |p: &Path| {
            std::fs::write(p, b"{\"schema_version\":1,\"bui").unwrap()
        }),
    ] {
        let f = Fixture::new(name);
        let game = f.game();
        one_session(&f, &game, &[GVDB, UI]);
        let path = f.baseline_path(&game);
        let before = std::fs::read(&path).unwrap();
        damage(&path);
        let damaged = std::fs::read(&path).unwrap();

        let inst = f.installer(&game);
        inst.apply(&f.plan(&[GVDB, UI])).unwrap();
        assert_eq!(f.read(GVDB), "db-modded", "{name}");
        let aside = f.set_aside(&path);
        assert_eq!(aside.len(), 1, "{name}: {aside:?}");
        assert_eq!(
            std::fs::read(&aside[0]).unwrap(),
            damaged,
            "{name}: kept byte for byte"
        );
        let rebuilt = Baseline::load_or_new(&f.data, &game.identity).unwrap();
        let rebuild = rebuilt.rebuild.clone().expect("marked as rebuilt");
        assert_eq!(rebuild.damaged, aside[0].file_name().unwrap().to_string_lossy());
        assert!(
            !rebuild.told_to_verify,
            "{name}: every file was a backed-up original"
        );
        let old: Baseline = serde_json::from_slice(&before).unwrap();
        assert_eq!(rebuilt.files, old.files, "{name}");
        assert_eq!(rebuilt.files[GVDB].sha256, sha256_bytes(b"db-v1"));

        inst.roll_back(false).unwrap();
        f.vanilla();
        assert_eq!(swofty_core::damaged_state(&f.data), aside, "{name}");
    }
}

#[test]
fn a_session_start_recovers_with_a_damaged_baseline() {
    let f = Fixture::new("start");
    let game = f.game();
    let inst = f.installer(&game);
    inst.apply(&f.plan(&[GVDB, UI])).unwrap();
    zero_fill(&f.baseline_path(&game));

    // The wrapper died with the game: its journal is still open. Rolling it
    // back needs only the journal and the object store, not the baseline.
    let lock = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    let r = recovery::recover(&lock, &f.data, &game, Scope::All).unwrap();
    assert!(r.recovered());
    f.vanilla();
    drop(lock);
    inst.apply(&f.plan(&[GVDB, UI])).unwrap();
    inst.roll_back(false).unwrap();
    f.vanilla();
}

#[test]
fn a_file_with_no_backed_up_original_is_refused_once_then_recorded_after_verifying() {
    let f = Fixture::new("unknown");
    let game = f.game();
    one_session(&f, &game, &[GVDB]);
    let path = f.baseline_path(&game);
    zero_fill(&path);

    // UI was never backed up, so nothing proves the bytes there are original.
    let inst = f.installer(&game);
    let err = inst.apply(&f.plan(&[GVDB, UI])).unwrap_err().to_string();
    assert!(err.starts_with(DAMAGED), "{err}");
    assert!(err.contains(UI) && !err.contains(GVDB), "{err}");
    f.vanilla();
    assert!(Journal::open(&f.data).unwrap().is_empty());
    let b = Baseline::load_or_new(&f.data, &game.identity).unwrap();
    assert!(b.rebuild.unwrap().told_to_verify);
    assert_eq!(
        b.files.keys().collect::<Vec<_>>(),
        [GVDB],
        "the proven file is kept"
    );

    // The player verified the game files in Steam and pressed Play again.
    inst.apply(&f.plan(&[GVDB, UI])).unwrap();
    assert_eq!(f.read(UI), "ui-modded");
    inst.roll_back(false).unwrap();
    f.vanilla();
    assert_eq!(f.set_aside(&path).len(), 1);
}

#[test]
fn bytes_the_launcher_wrote_are_never_recorded_as_the_original() {
    let f = Fixture::new("ours");
    let game = f.game();
    let inst = f.installer(&game);
    inst.apply(&f.plan(&[GVDB, UI])).unwrap();
    // Power is lost mid-session: the journal and the baseline are both gone,
    // and only the placed-files record says what the launcher wrote.
    for j in Journal::open(&f.data).unwrap() {
        zero_fill(&j.file);
    }
    zero_fill(&f.baseline_path(&game));

    let lock = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    let r = recovery::recover(&lock, &f.data, &game, Scope::All).unwrap();
    assert!(
        r.swept
            .iter()
            .any(|l| l.contains(GVDB) && l.contains("left in place")),
        "{:?}",
        r.swept
    );
    drop(lock);
    assert_eq!(f.read(GVDB), "db-modded");
    assert!(
        !f.game.join("version.dll").exists(),
        "added files are still proven ours"
    );
    let drift = inst.verify().unwrap();
    assert!(drift.contains(&Drift::NoOriginal(GVDB.into())), "{drift:?}");
    for attempt in 0..2 {
        let err = inst.apply(&f.plan(&[GVDB, UI])).unwrap_err().to_string();
        assert!(err.starts_with(DAMAGED), "attempt {attempt}: {err}");
        assert_eq!(f.read(GVDB), "db-modded", "attempt {attempt}: nothing touched");
        let b = Baseline::load_or_new(&f.data, &game.identity).unwrap();
        assert!(!b.files.contains_key(GVDB), "attempt {attempt}: {:?}", b.files);
    }

    // Steam's verify puts the originals back; they match backed-up objects.
    write(&f.game.join(GVDB), "db-v1");
    write(&f.game.join(UI), "ui-v1");
    let lock = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    recovery::recover(&lock, &f.data, &game, Scope::All).unwrap();
    drop(lock);
    inst.apply(&f.plan(&[GVDB, UI])).unwrap();
    inst.roll_back(false).unwrap();
    f.vanilla();
    let b = Baseline::load_or_new(&f.data, &game.identity).unwrap();
    assert_eq!(b.files[GVDB].sha256, sha256_bytes(b"db-v1"));
}

#[test]
fn a_missing_baseline_next_to_a_damaged_copy_stays_in_rebuild_mode() {
    let f = Fixture::new("missing");
    let game = f.game();
    one_session(&f, &game, &[GVDB]);
    let path = f.baseline_path(&game);
    zero_fill(&path);
    Baseline::load_or_new(&f.data, &game.identity).unwrap();
    std::fs::remove_file(&path).unwrap();
    let b = Baseline::load_or_new(&f.data, &game.identity).unwrap();
    assert!(b.checking());
    let err = f
        .installer(&game)
        .apply(&f.plan(&[GVDB, UI]))
        .unwrap_err()
        .to_string();
    assert!(err.starts_with(DAMAGED), "{err}");
}

#[test]
fn a_baseline_in_a_shape_this_build_cannot_read_is_left_alone() {
    let f = Fixture::new("shape");
    let game = f.game();
    one_session(&f, &game, &[GVDB]);
    let path = f.baseline_path(&game);
    std::fs::write(&path, r#"{"schema_version":"two"}"#).unwrap();
    let err = f
        .installer(&game)
        .apply(&f.plan(&[GVDB]))
        .unwrap_err()
        .to_string();
    assert!(err.contains("baseline.json"), "{err}");
    assert!(f.set_aside(&path).is_empty());
    assert_eq!(
        std::fs::read_to_string(&path).unwrap(),
        r#"{"schema_version":"two"}"#
    );
}

#[test]
fn a_damaged_journal_or_placed_record_is_set_aside_and_the_leftovers_still_come_out() {
    let f = Fixture::new("journal");
    let game = f.game();
    let inst = f.installer(&game);
    inst.apply(&f.plan(&[GVDB, UI])).unwrap();
    let journal = Journal::open(&f.data).unwrap().remove(0).file;
    zero_fill(&journal);

    // The placed-files record still proves what the launcher wrote, and the
    // baseline holds the originals: the sweep puts everything back.
    let lock = SessionLock::try_acquire(&f.data).unwrap().unwrap();
    let r = recovery::recover(&lock, &f.data, &game, Scope::All).unwrap();
    drop(lock);
    assert!(r.journals.is_empty());
    assert_eq!(f.set_aside(&journal).len(), 1);
    f.vanilla();

    inst.apply(&f.plan(&[GVDB, UI])).unwrap();
    let placed = Placed::path(&f.data);
    std::fs::write(&placed, b"").unwrap();
    assert!(Placed::load(&f.data).unwrap().files.is_empty());
    assert_eq!(f.set_aside(&placed).len(), 1);
    inst.roll_back(false).unwrap();
    f.vanilla();
}

#[test]
fn a_damaged_profile_or_config_starts_over_and_keeps_the_copy() {
    let f = Fixture::new("store");
    let store = Store::new(&f.data);
    let mut config = store.config();
    config.active_profile = Some("grid".into());
    store.save_config(&config).unwrap();
    store.create_profile("grid", None).unwrap();
    let profile = store.profiles_dir().join("grid.json");
    zero_fill(&profile);
    zero_fill(&store.config_path());

    assert_eq!(store.profile("grid").unwrap().name, "grid");
    assert_eq!(f.set_aside(&profile).len(), 1);
    assert!(!profile.exists());
    assert_eq!(store.config().active_profile, None);
    assert_eq!(f.set_aside(&store.config_path()).len(), 1);
    assert_eq!(swofty_core::damaged_state(&f.data).len(), 2);
}
