use std::path::{Path, PathBuf};

use swofty_core::baseline::Identity;
use swofty_core::build::{GVDB, build};
use swofty_core::install::Game;
use swofty_core::store::Store;
use swofty_formats::gvdb::{Gvdb, Record};

fn write(path: &Path, body: &str) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, body).unwrap();
}

struct Fixture {
    root: PathBuf,
    store: Store,
    game: Game,
    _pin: swofty_core::builtin::LoaderPin,
}

impl Fixture {
    fn new(name: &str) -> Self {
        let root = std::env::temp_dir().join(format!("swofty-std16-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        write(&dir.join("F1_25.exe"), "exe");
        let records = vec![
            Record::new("tracks.Track._portimao_instancecount_", 0, "1"),
            Record::new("tracks.Track_0._metadata_", 6, "portimao"),
            Record::new("tracks.Track_0.laps", 0, "60"),
        ];
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let store = Store::new(root.join("data"));
        Self {
            root,
            store,
            game,
            _pin: swofty_core::builtin::LoaderPin::none(),
        }
    }

    fn addon(&self, id: &str, name: &str, version: &str, ops: &str, registry: Option<&str>) {
        let src = self.root.join("src").join(id);
        write(
            &src.join("swofty.json"),
            &format!(
                r#"{{ "schema_version": 1, "id": "{id}", "name": "{name}", "version": "{version}", "author": "t",
                     "compat": {{ "game": "f1_25" }}, "operations": {{ "assets": ["ops.json"] }} }}"#
            ),
        );
        write(
            &src.join("ops.json"),
            &format!(r#"{{ "schema_version": 1, "ops": [{ops}] }}"#),
        );
        if let Some(r) = registry {
            write(&src.join("registry.json"), r);
        }
        self.store.install_addon(&src).unwrap();
        self.store
            .update_profile("default", |p| p.set_enabled(id, true))
            .unwrap();
    }

    fn room(&self, body: &str) {
        write(&self.store.root.join("registry/room.json"), body);
    }

    fn legacy_room(&self, body: &str) {
        write(&self.game.dir.join("swofty/registry/room.json"), body);
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        std::fs::remove_dir_all(&self.root).ok();
    }
}

const LAPS: &str =
    r#"{"op":"db.set","select":"tracks.Track[_metadata_=portimao]","field":"laps","value":"66"}"#;

#[test]
fn a_breaks_on_an_addon_that_is_on_stops_the_build_with_the_fix() {
    let f = Fixture::new("breaks");
    f.addon(
        "swofty.progressive",
        "Progressive Career",
        "1.3.0",
        LAPS,
        Some(r#"{ "breaks": [ { "id": "swofty.career-editor", "version": "<0.5.0", "why": "its contract overwrite" } ] }"#),
    );
    f.addon("swofty.career-editor", "Career Editor", "0.4.2", LAPS, None);
    let err = build(&f.store, "default", &f.game).unwrap_err();
    assert!(matches!(err, swofty_core::Error::Dependencies(_)), "{err}");
    assert_eq!(
        err.to_string(),
        "Progressive Career 1.3.0 doesn't work with Career Editor 0.4.2. Its contract overwrite. Update Career \
         Editor in the Addon Browser, or turn one of them off."
    );
    f.store
        .update_profile("default", |p| p.set_enabled("swofty.career-editor", false))
        .unwrap();
    assert!(build(&f.store, "default", &f.game).is_ok());
}

#[test]
fn a_conflicts_entry_warns_and_the_build_goes_on() {
    let f = Fixture::new("conflicts");
    f.addon(
        "a.x",
        "X",
        "1.0.0",
        LAPS,
        Some(r#"{ "conflicts": [ { "id": "b.y", "why": "both set the laps" } ], "unknown": 1 }"#),
    );
    f.addon("b.y", "Y", "1.0.0", LAPS, None);
    let built = build(&f.store, "default", &f.game).unwrap();
    assert!(
        built
            .warnings
            .iter()
            .any(|w| w.starts_with("X 1.0.0 conflicts with Y 1.0.0. Both set the laps.")),
        "{:?}",
        built.warnings
    );
}

#[test]
fn a_room_report_without_registry_records_changes_nothing() {
    let f = Fixture::new("room-same");
    f.addon("a.x", "X", "1.0.0", LAPS, None);
    let before = build(&f.store, "default", &f.game).unwrap();
    f.room(r#"{ "version": 1, "presize": "unavailable", "reason": "signature", "registry": { "asked_keys": 0 } }"#);
    let after = build(&f.store, "default", &f.game).unwrap();
    assert_eq!(before.buildhash, after.buildhash);
    f.room(r#"{ "version": 1, "presize": "unavailable", "registry": { "asked_keys": 500000 } }"#);
    let asked = build(&f.store, "default", &f.game).unwrap();
    assert_eq!(
        before.buildhash, asked.buildhash,
        "a profile of older addons is never padded"
    );
    assert_eq!(
        std::fs::read(&before.plan.data[GVDB]).unwrap(),
        std::fs::read(&after.plan.data[GVDB]).unwrap()
    );
}

#[test]
fn an_unsized_boot_that_ran_out_of_room_pads_the_next_database_file() {
    let f = Fixture::new("room-pad");
    f.addon("a.x", "X", "1.0.0", LAPS, Some("{}"));
    f.room(
        r#"{ "version": 1, "presize": "unavailable", "reason": "signature", "file_records": 3,
             "registry": { "asked_keys": 200000, "addons": { "a.x": { "asked": 200000, "applied": 90000 } } } }"#,
    );
    let built = build(&f.store, "default", &f.game).unwrap();
    let db = Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap()).unwrap();
    assert_eq!(db.records.len(), swofty_core::stdlib::room::PAD_2_21);
    assert_eq!(db.get_text("tracks.Track_0.laps").as_deref(), Some("66"));
    assert!(
        built
            .lines
            .iter()
            .any(|l| l.contains("padded with 838858 inert record(s) to 838861")),
        "{:?}",
        built.lines
    );
    f.room(r#"{ "version": 1, "presize": "applied", "registry": { "asked_keys": 200000 } }"#);
    let sized = build(&f.store, "default", &f.game).unwrap();
    let db = Gvdb::parse(&std::fs::read(&sized.plan.data[GVDB]).unwrap()).unwrap();
    assert_eq!(db.records.len(), 3);
}

#[test]
fn the_room_report_is_read_from_the_data_folder_first_and_the_game_folder_after() {
    let f = Fixture::new("room-places");
    f.addon("a.x", "X", "1.0.0", LAPS, Some("{}"));
    let ran_out = r#"{ "version": 1, "presize": "unavailable", "registry": { "asked_keys": 200000,
                       "addons": { "a.x": { "asked": 200000, "applied": 90000 } } } }"#;
    let fine = r#"{ "version": 1, "presize": "applied", "registry": { "asked_keys": 200000 } }"#;
    let records = |f: &Fixture| {
        let built = build(&f.store, "default", &f.game).unwrap();
        Gvdb::parse(&std::fs::read(&built.plan.data[GVDB]).unwrap())
            .unwrap()
            .records
            .len()
    };
    f.legacy_room(ran_out);
    assert_eq!(
        records(&f),
        swofty_core::stdlib::room::PAD_2_21,
        "an old core's report counts"
    );
    f.room(fine);
    assert_eq!(
        records(&f),
        3,
        "the data folder's report wins over the game folder's"
    );
}
