use std::path::{Path, PathBuf};
use std::process::ExitCode;

use anyhow::{Context, Result, bail};
use swofty_core::baseline::DepotHashes;
use swofty_core::build::GVDB;
use swofty_core::install::{EA_APP_EXPERIMENTAL, Game, Installer, Source};
use swofty_core::recovery::{Blocked, Scope, SessionLock};
use swofty_core::settings::SETTINGS_FILE;
use swofty_core::stock::{self, Depots};
use swofty_core::store::Store;
use swofty_core::{GAME_EXE, steam};
use swofty_formats::erp::Erp;
use swofty_formats::gvdb::Gvdb;
pub const UI_ERPS: &[&str] = &[
    "2025_asset_groups/ui_package/flow_frontend.erp",
    "2025_asset_groups/ui_package/persistent.erp",
    "2025_asset_groups/ui_package/databases.erp",
    "2025_asset_groups/ui_package/screens/fe_home.erp",
];
pub fn game(store: &Store, explicit: Option<PathBuf>) -> Result<Game> {
    Ok(Game::locate(&store.root, game_dir(store, explicit)?)?)
}

pub fn supported(g: &Game) -> Result<()> {
    if Source::detect(&g.dir) == Source::EaApp {
        eprintln!("swofty: {}: {EA_APP_EXPERIMENTAL}", g.dir.display());
    }
    Ok(())
}

/// `--game`, else the active copy (`swofty game use`, the launcher's game
/// switcher), else Steam's.
fn game_dir(store: &Store, explicit: Option<PathBuf>) -> Result<PathBuf> {
    explicit
        .or_else(|| swofty_core::copies::find_active(&store.config()).map(|c| c.dir))
        .context("F1 25 not found; pass --game, add the folder with swofty game add, or run swofty doctor")
}

pub fn build(store: &Store, profile: &str, game: Option<PathBuf>, side_data: bool) -> Result<ExitCode> {
    let g = self::game(store, game)?;
    let built = build_profile(store, profile, &g)?;
    report(profile, &built);
    if side_data {
        println!(
            "side data of add-row copies ({} finding(s)):",
            built.side_data.len()
        );
        for f in &built.side_data {
            println!("  {f}");
        }
    }
    Ok(ExitCode::SUCCESS)
}

pub(crate) fn build_profile(store: &Store, profile: &str, g: &Game) -> Result<swofty_core::build::Built> {
    let addons = store.addons()?;
    let mut p = store.profile(profile)?;
    let mut built = swofty_core::build::build(store, profile, g)?;
    if p.is_vanilla() {
        return Ok(built);
    }
    let settings = built.overlay.join(SETTINGS_FILE);
    std::fs::create_dir_all(settings.parent().expect("has parent"))?;
    let session = match built.settings.take() {
        Some(s) => s,
        None => store.session_settings(profile)?,
    };
    // Left alone when it's unchanged, so its time doesn't make an OverlayFS
    // layer built from it look stale.
    let text = serde_json::to_vec_pretty(&session)?;
    if std::fs::read(&settings).ok().as_deref() != Some(text.as_slice()) {
        std::fs::write(&settings, text)?;
    }
    if !built.plan.loader.is_empty() {
        built.plan.loader.insert(SETTINGS_FILE.into(), settings);
    }
    p.mark_built(&addons);
    store.update_profile(profile, |fresh| {
        fresh.applied = p.applied;
        Ok(())
    })?;
    Ok(built)
}

pub(crate) fn report(profile: &str, built: &swofty_core::build::Built) {
    if built.plan.is_vanilla() {
        println!("{profile} plays without addons; nothing to build");
        return;
    }
    println!("profile {profile}: {}", built.order.join(" -> "));
    for l in &built.lines {
        println!("{l}");
    }
    for w in &built.warnings {
        println!("warning: {w}");
    }
    println!("overlay {}: {}", built.buildhash, built.overlay.display());
    for rel in built.plan.data.keys() {
        println!("  replace {rel}");
    }
    for rel in built.plan.loader.keys() {
        println!("  place {rel}");
    }
    let added = &built.plan.added;
    if !added.is_empty() {
        println!(
            "  add {} file(s) under {}",
            added.len(),
            common_root(added.keys())
        );
    }
}

pub fn conflicts(store: &Store, profile: &str, check: bool, game: Option<PathBuf>) -> Result<ExitCode> {
    let report = match check {
        true => swofty_core::conflicts::check(store, profile, &self::game(store, game)?)?,
        false => match swofty_core::conflicts::last(store, profile) {
            Some(report) => report,
            None => {
                println!("{profile} has not been built yet; run swofty conflicts --check");
                return Ok(ExitCode::SUCCESS);
            }
        },
    };
    let names: std::collections::BTreeMap<String, String> = store
        .registry()?
        .manifests()
        .into_iter()
        .map(|(id, m)| (id, m.name))
        .collect();
    print!("{}", conflicts_text(&report, &names));
    Ok(ExitCode::SUCCESS)
}

fn conflicts_text(
    report: &swofty_core::conflicts::Report,
    names: &std::collections::BTreeMap<String, String>,
) -> String {
    if report.pairs.is_empty() {
        return format!("{}: no addon overrides another\n", report.profile);
    }
    let name = |id: &str| names.get(id).cloned().unwrap_or_else(|| id.to_owned());
    let mut out = String::new();
    for pair in &report.pairs {
        out.push_str(&pair.line(&name));
        out.push('\n');
        for item in &pair.items {
            out.push_str(&format!("  {item}\n"));
        }
        let more =
            (pair.database + pair.resources + pair.files + pair.strings).saturating_sub(pair.items.len());
        if more > 0 && pair.items.len() >= swofty_core::conflicts::MAX_ITEMS {
            out.push_str(&format!("  and {more} more\n"));
        }
    }
    out
}

fn common_root<'a>(paths: impl Iterator<Item = &'a String>) -> String {
    let mut roots: Vec<&str> = paths.filter_map(|p| p.split('/').next()).collect();
    roots.sort();
    roots.dedup();
    roots.join(", ")
}

/// Build the profile and install it. Files an unfinished session left in the
/// way that are provably ours are cleared and the install is tried once
/// more; anything else stops it untouched, with what a restore would do.
pub fn install(store: &Store, profile: &str, g: &Game, session: bool, out: &dyn Fn(&str)) -> Result<()> {
    if swofty_core::compat::write_blocker(&g.dir).is_some() {
        bail!("{}", swofty_core::compat::write_blocked_text(&g.dir));
    }
    install_checked(store, profile, g, session, out).map_err(|e| denied(e, &g.dir))
}

/// Names the game folder and the fix when Windows refused a write in it.
fn denied(e: anyhow::Error, game: &Path) -> anyhow::Error {
    let refused = e.chain().any(|c| match c.downcast_ref::<swofty_core::Error>() {
        Some(swofty_core::Error::Io { path, source }) => {
            source.kind() == std::io::ErrorKind::PermissionDenied && path.starts_with(game)
        }
        _ => false,
    });
    if !refused {
        return e;
    }
    anyhow::anyhow!(
        "{e}. Windows refused to let Swofty change files in {}; run Swofty Launcher as administrator, \
         or move F1 25 to a folder outside Program Files",
        game.display()
    )
}

fn install_checked(store: &Store, profile: &str, g: &Game, session: bool, out: &dyn Fn(&str)) -> Result<()> {
    crate::setup::gate(store)?;
    Blocked::clear(&store.root).ok();
    stock::Report::clear(&store.root).ok();
    let depots = check_stock(store, g, out)?;
    let inst = Installer {
        session,
        depot: depots.as_ref().map_or(&() as &dyn DepotHashes, |d| d),
        out,
        ..Installer::new(&store.root, g)
    };
    let built = build_profile(store, profile, g)?;
    report(profile, &built);
    let found = match inst.apply(&built.plan) {
        Err(swofty_core::Error::Leftovers(found)) => found,
        r => return Ok(not_stock(store, g, r)?),
    };
    if found.iter().any(|c| !c.owned) {
        return Err(crate::recover::blocked(store, g, &found));
    }
    let cleared = inst.clear(&found)?;
    crate::recover::announce(store, g, &cleared, out);
    crate::recover::record_cleared(store, &cleared);
    let built = build_profile(store, profile, g)?;
    report(profile, &built);
    match inst.apply(&built.plan) {
        Err(swofty_core::Error::Leftovers(found)) => Err(crate::recover::blocked(store, g, &found)),
        r => Ok(not_stock(store, g, r)?),
    }
}

/// Check the game against Steam's manifests before anything is built from
/// it. Steam's list stays with the install, which checks every original it
/// backs up for the first time. A game folder outside Steam is not checked;
/// a Steam install whose list can't be read says why.
fn check_stock(store: &Store, g: &Game, out: &dyn Fn(&str)) -> Result<Option<Depots>> {
    let Some(found) = steam::find_game().filter(|i| i.dir == g.dir) else {
        return Ok(None);
    };
    let depots = match Depots::load(&found) {
        Ok(d) => d,
        Err(why) => {
            out(&format!(
                "[stock] {why}; the game files are not checked against Steam"
            ));
            heal_launcher(store, &g.dir, None, out)?;
            return Ok(None);
        }
    };
    heal_launcher(store, &g.dir, Some(&depots as &dyn DepotHashes), out)?;
    let checked = stock::check(&store.root, g, &depots)?;
    if checked.proven > 0 {
        out(&format!(
            "[stock] {} original(s) on record match Steam's copy",
            checked.proven
        ));
    }
    for rel in &checked.rerecorded {
        out(&format!(
            "[stock] {rel} matches Steam's copy again; backed up as the original"
        ));
    }
    for d in &checked.eac {
        out(&format!(
            "[stock] {} differs from Steam's copy (size {}, Steam {}); not used by modded sessions, continuing",
            d.rel,
            d.size.map_or("missing".to_owned(), |n| n.to_string()),
            d.steam
        ));
    }
    if let Some(n) = checked.swept {
        out(&format!("[stock] {n} game file(s) checked against Steam's list"));
    }
    if !checked.foreign.is_empty() {
        for f in &checked.detail {
            out(&format!(
                "[stock] {} is not Steam's copy (measured {}, size {}, Steam {}, modified {})",
                f.rel,
                f.measured,
                f.size.map_or("missing".to_owned(), |n| n.to_string()),
                f.steam,
                f.modified.map_or("unknown".to_owned(), |t| format!("{t:.0}"))
            ));
        }
        stock::Report::new(checked.foreign.clone(), &g.dir)
            .with_detail(checked.detail.clone())
            .write(&store.root)?;
        return Err(swofty_core::Error::NotStock(checked.foreign).into());
    }
    Ok(Some(depots))
}

/// Clear a stale EA AntiCheat backup an interrupted session left before the
/// stock check runs, logging what it did. Does nothing while a session is
/// pending.
fn heal_launcher(
    store: &Store,
    game: &Path,
    depot: Option<&dyn DepotHashes>,
    out: &dyn Fn(&str),
) -> Result<()> {
    for note in swofty_core::offline::heal_before_check(&store.root, game, depot)? {
        out(&format!("[stock] {note}"));
    }
    Ok(())
}

/// An original that turned out not to be Steam's while the install backed it
/// up stops the launch the same way as the check before the build.
fn not_stock(store: &Store, g: &Game, r: swofty_core::Result<()>) -> swofty_core::Result<()> {
    if let Err(swofty_core::Error::NotStock(files)) = &r {
        stock::Report::new(files.clone(), &g.dir).write(&store.root).ok();
    }
    r
}

pub fn apply(store: &Store, profile: &str, game: Option<PathBuf>) -> Result<ExitCode> {
    let g = self::game(store, game)?;
    supported(&g)?;
    let out = |l: &str| println!("{l}");
    let Some(lock) = SessionLock::try_acquire(&store.root)? else {
        bail!("a modded session is running or rolling back; close F1 25 and try again");
    };
    crate::recover::stale(store, &lock, &g, Scope::All, &out)?;
    install(store, profile, &g, false, &out)?;
    println!(
        "applied {profile} to {}; swofty restore puts the game back",
        g.dir.display()
    );
    Ok(ExitCode::SUCCESS)
}

pub fn verify(store: &Store, game: Option<PathBuf>) -> Result<ExitCode> {
    let g = self::game(store, game)?;
    let drift = Installer::new(&store.root, &g).verify()?;
    let game = g.dir;
    let mut ok = drift.is_empty();
    for d in &drift {
        println!("DRIFT {d}");
    }
    let mut check = |name: &str, result: Result<bool>| match result {
        Ok(true) => println!("ok    {name}"),
        Ok(false) => {
            ok = false;
            println!("FAIL  {name}: round trip differs");
        }
        Err(e) => {
            ok = false;
            println!("FAIL  {name}: {e:#}");
        }
    };
    check(
        GVDB,
        read(&game, GVDB).and_then(|d| Ok(Gvdb::parse(&d)?.to_bytes() == d)),
    );
    for rel in UI_ERPS {
        check(
            rel,
            read(&game, rel).and_then(|d| Ok(Erp::parse(&d)?.to_bytes() == d)),
        );
    }
    if !game.join(GAME_EXE).is_file() {
        ok = false;
        println!("FAIL  {GAME_EXE} not found in {}", game.display());
    }
    Ok(if ok { ExitCode::SUCCESS } else { ExitCode::FAILURE })
}

fn read(game: &Path, rel: &str) -> Result<Vec<u8>> {
    std::fs::read(game.join(rel)).with_context(|| rel.to_owned())
}

pub fn restore(store: &Store, game: Option<PathBuf>) -> Result<ExitCode> {
    let g = self::game(store, game)?;
    let done = swofty_core::recovery::restore(&store.root, &g)?;
    for n in &done.notes {
        println!("restored: {n}");
    }
    for p in &done.set_aside {
        println!("set aside: {}", p.display());
    }
    let left = done.left;
    if left.is_empty() {
        println!(
            "restored: the game folder matches the baseline for build {}",
            g.identity.build_id
        );
        return Ok(ExitCode::SUCCESS);
    }
    for d in &left {
        println!("not restored: {d}");
    }
    Ok(ExitCode::FAILURE)
}

#[cfg(test)]
mod tests {
    use std::cell::RefCell;
    use std::collections::BTreeMap;
    use std::sync::Arc;

    use swofty_core::asset::{AssetBatch, AssetOp, FileOp, Op, Origin, Root, Source};
    use swofty_core::baseline::Identity;
    use swofty_core::builtin::loader_dir;
    use swofty_core::launcher::{Addon, BuildCx, Builtin};
    use swofty_core::manifest::AddonManifest;

    use super::*;

    #[test]
    fn names_the_game_folder_when_windows_refuses_a_write() {
        let game = Path::new("/games/F1 25");
        let refused = swofty_core::Error::Io {
            path: game.join("2025_asset_groups/ui_package/databases.erp"),
            source: std::io::Error::from(std::io::ErrorKind::PermissionDenied),
        };
        let text = denied(anyhow::Error::from(refused), game).to_string();
        assert!(
            text.ends_with(
                "Windows refused to let Swofty change files in /games/F1 25; run Swofty Launcher as \
                 administrator, or move F1 25 to a folder outside Program Files"
            ),
            "{text}"
        );
        let elsewhere = swofty_core::Error::Io {
            path: "/data/cache/x".into(),
            source: std::io::Error::from(std::io::ErrorKind::PermissionDenied),
        };
        assert!(
            !denied(anyhow::Error::from(elsewhere), game)
                .to_string()
                .contains("administrator")
        );
    }

    struct Writer(AddonManifest);

    impl Addon for Writer {
        fn manifest(&self) -> &AddonManifest {
            &self.0
        }

        fn build(&self, cx: &BuildCx) -> swofty_addon::Result<AssetBatch> {
            cx.cx.set_state(&serde_json::json!({"built": true}))?;
            Ok(AssetBatch::default())
        }
    }

    #[test]
    fn keeps_profile_changes_made_during_the_build() {
        if loader_dir().is_some() {
            return;
        }
        let root = std::env::temp_dir().join(format!("swofty-cli-build-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let dir = root.join("game");
        std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
        std::fs::write(dir.join(GAME_EXE), "exe").unwrap();
        std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
        let game = Game {
            identity: Identity::detect(&dir, Some(1)).unwrap(),
            dir,
            user_reg: None,
        };
        let manifest = AddonManifest::parse(
            r#"{"schema_version":1,"id":"t.writer","name":"Writer","version":"1.0.0","author":"t","compat":{"game":"f1_25"}}"#,
        )
        .unwrap();
        let store = Store::new(root.join("data")).with_builtins(vec![Builtin {
            addon: Arc::new(Writer(manifest)),
            locked: false,
            default_on: true,
        }]);
        build_profile(&store, "default", &game).unwrap();
        let p = store.profile("default").unwrap();
        assert_eq!(p.state["t.writer"], serde_json::json!({"built": true}));
        std::fs::remove_dir_all(&root).ok();
    }

    const MIP: &str =
        "environment_package/tracks/suzuka24/mipmaps/f55b20d9a14b_pit_perch_ferrari_b_n.tif.2025.mipmaps";

    struct Putter(AddonManifest);

    impl Addon for Putter {
        fn manifest(&self) -> &AddonManifest {
            &self.0
        }

        fn build(&self, _: &BuildCx) -> swofty_addon::Result<AssetBatch> {
            Ok(AssetBatch {
                ops: vec![Op {
                    op: AssetOp::File(FileOp::Put {
                        source: Source {
                            root: Root::Data,
                            path: "mip".into(),
                        },
                        target: MIP.into(),
                    }),
                    origin: Origin {
                        addon: "t.putter".into(),
                        item: None,
                    },
                }],
                ..AssetBatch::default()
            })
        }
    }

    struct Harness {
        root: PathBuf,
        store: Store,
        game: Game,
    }

    impl Harness {
        fn new(name: &str) -> Self {
            let root = std::env::temp_dir().join(format!("swofty-cli-{name}-{}", std::process::id()));
            std::fs::remove_dir_all(&root).ok();
            let dir = root.join("game");
            std::fs::create_dir_all(dir.join("game_package/gamedatabases")).unwrap();
            std::fs::write(dir.join(GAME_EXE), "exe").unwrap();
            std::fs::write(dir.join(GVDB), Gvdb { records: Vec::new() }.to_bytes()).unwrap();
            let manifest = AddonManifest::parse(
                r#"{"schema_version":1,"id":"t.putter","name":"Putter","version":"1.0.0","author":"t","compat":{"game":"f1_25"}}"#,
            )
            .unwrap();
            let store = Store::new(root.join("data")).with_builtins(vec![Builtin {
                addon: Arc::new(Putter(manifest)),
                locked: false,
                default_on: true,
            }]);
            let data = store.addon_data_dir("t.putter");
            std::fs::create_dir_all(&data).unwrap();
            std::fs::write(data.join("mip"), "mip-ferrari").unwrap();
            Self {
                game: Game {
                    identity: Identity::detect(&dir, Some(1)).unwrap(),
                    dir,
                    user_reg: None,
                },
                store,
                root,
            }
        }

        fn install(&self, log: &RefCell<Vec<String>>) -> Result<()> {
            if !swofty_core::setup::done(&self.store.root) {
                swofty_core::setup::reset(&self.store.root, &self.game, false)?;
                swofty_core::setup::finish(&self.store.root)?;
            }
            install(&self.store, "default", &self.game, true, &|l| {
                log.borrow_mut().push(l.to_owned())
            })
        }

        fn mip(&self) -> String {
            std::fs::read_to_string(self.game.dir.join(MIP)).unwrap()
        }
    }

    impl Drop for Harness {
        fn drop(&mut self) {
            std::fs::remove_dir_all(&self.root).ok();
        }
    }

    #[test]
    fn a_leftover_from_an_interrupted_session_is_recovered_and_the_launch_proceeds() {
        if loader_dir().is_some() {
            return;
        }
        let h = Harness::new("leftover");
        let log = RefCell::new(Vec::new());
        h.install(&log).unwrap();
        assert_eq!(h.mip(), "mip-ferrari");
        assert!(log.borrow().is_empty());
        // The session was interrupted and its journal is gone; the mip file
        // it placed is still in the game folder.
        std::fs::remove_dir_all(h.store.root.join("state/journal")).unwrap();

        h.install(&log).unwrap();
        let log = log.into_inner();
        assert_eq!(log[0], swofty_core::recovery::HEADLINE);
        assert_eq!(log[1], format!("  removed {MIP}"));
        assert!(log.iter().any(|l| l.starts_with("doctor: data dir")), "{log:?}");
        assert!(log.iter().any(|l| l.starts_with("doctor: baseline")), "{log:?}");
        assert_eq!(h.mip(), "mip-ferrari");
        let open = swofty_core::journal::Journal::open(&h.store.root).unwrap();
        assert_eq!(open.len(), 1);
        assert!(open[0].session);
        assert_eq!(open[0].entries.last().unwrap().path, MIP);
        let notice = swofty_core::recovery::Notice::read(&h.store.root).unwrap();
        assert_eq!(notice.lines, [format!("removed {MIP}")]);

        let inst = Installer::new(&h.store.root, &h.game);
        inst.roll_back(true).unwrap();
        assert!(!h.game.dir.join("environment_package").exists());
        assert_eq!(inst.verify().unwrap(), []);
    }

    #[test]
    fn a_hand_copied_file_is_backed_up_and_put_back_never_deleted() {
        if loader_dir().is_some() {
            return;
        }
        let h = Harness::new("by-hand");
        std::fs::create_dir_all(h.game.dir.join(MIP).parent().unwrap()).unwrap();
        std::fs::write(h.game.dir.join(MIP), "copied in by hand").unwrap();
        let log = RefCell::new(Vec::new());
        h.install(&log).unwrap();
        assert_eq!(h.mip(), "mip-ferrari");
        Installer::new(&h.store.root, &h.game).roll_back(true).unwrap();
        assert_eq!(h.mip(), "copied in by hand");
    }

    #[test]
    fn a_file_of_unknown_provenance_stops_the_launch_untouched_with_what_restore_does() {
        if loader_dir().is_some() {
            return;
        }
        let h = Harness::new("unknown");
        let built = build_profile(&h.store, "default", &h.game).unwrap();
        assert!(built.plan.added.contains_key(MIP));
        // A copy that turns up after the build is only found when the files
        // are placed, as with track mip files, which always go in as
        // additions.
        std::fs::create_dir_all(h.game.dir.join(MIP).parent().unwrap()).unwrap();
        std::fs::write(h.game.dir.join(MIP), "copied in by hand").unwrap();
        let err = Installer::new(&h.store.root, &h.game)
            .apply(&built.plan)
            .map_err(|e| match e {
                swofty_core::Error::Leftovers(found) => crate::recover::blocked(&h.store, &h.game, &found),
                e => e.into(),
            })
            .unwrap_err()
            .to_string();
        assert!(
            err.starts_with(&format!(
                "{MIP} is already in the game folder, and swofty did not put it there; it was left untouched in case it is a mod copied in by hand. Restore game folder (swofty restore) rolls back swofty's own changes and moves this file to "
            )),
            "{err}"
        );
        assert!(err.ends_with(", where you can get it back"), "{err}");
        assert!(!err.contains('\n'));
        assert_eq!(h.mip(), "copied in by hand");
        assert!(
            swofty_core::journal::Journal::open(&h.store.root)
                .unwrap()
                .is_empty()
        );
        let blocked = swofty_core::recovery::Blocked::read(&h.store.root).unwrap();
        assert_eq!(blocked.files[0].path, MIP);

        let done = swofty_core::recovery::restore(&h.store.root, &h.game).unwrap();
        assert_eq!(
            std::fs::read_to_string(&done.set_aside[0]).unwrap(),
            "copied in by hand"
        );
        assert!(!h.game.dir.join("environment_package").exists());
        let log = RefCell::new(Vec::new());
        h.install(&log).unwrap();
        assert_eq!(h.mip(), "mip-ferrari");
    }

    #[test]
    fn prints_each_pair_then_its_items() {
        use swofty_core::conflicts::{Pair, Report};
        let mut report = Report {
            profile: "default".into(),
            built_at: 1.0,
            ..Report::default()
        };
        let names = BTreeMap::from([("t.b".to_owned(), "Second".to_owned())]);
        assert_eq!(
            conflicts_text(&report, &names),
            "default: no addon overrides another\n"
        );
        report.pairs.push(Pair {
            winner: "t.b".into(),
            loser: "t.a".into(),
            database: 1,
            resources: 0,
            files: 1,
            strings: 0,
            items: vec!["rules.laps".into(), "x/a.dds".into()],
        });
        assert_eq!(
            conflicts_text(&report, &names),
            "Second overrides t.a on 1 database value and 1 file\n  rules.laps\n  x/a.dds\n"
        );
        let max = swofty_core::conflicts::MAX_ITEMS;
        report.pairs[0] = Pair {
            database: max,
            strings: 2,
            items: vec!["rules.laps".into(); max],
            ..report.pairs[0].clone()
        };
        assert!(conflicts_text(&report, &names).ends_with("  and 3 more\n"));
    }
}
