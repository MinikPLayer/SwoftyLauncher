use std::path::{Path, PathBuf};

use swofty_core::activation::place::track_group;
use swofty_core::activation::ui::{MapUi, UiBuild, build_ui};
use swofty_core::map::TrackPackage;
use swofty_formats::erp::{Entry, Erp, ErpFile, Resource};
use swofty_formats::pkg::{Node, Pkg, Val};

const DB: &str = "2025_asset_groups/ui_package/databases.erp";
const FRONT: &str = "2025_asset_groups/ui_package/common_front_end.erp";
const OVERRIDES: &str = "eaid://ui_package/databases/overrides.uidb";
const ARRAY: &str =
    "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.tracks.flags_circle.texarray";
const FLAG: &str = "eaid://ui_package/art/textures/track_installer/flagtest/flag_circle.tif";

fn flags(path: &Path) -> (u32, Vec<u8>) {
    let surf = Erp::read_matching(path, |n| n == format!("{ARRAY}.image"))
        .unwrap()
        .pop()
        .unwrap();
    let head = surf.fragments[0].data().unwrap();
    (
        u32::from_le_bytes(head[28..32].try_into().unwrap()),
        surf.fragments[1].data().unwrap(),
    )
}

fn track(dir: &Path, front: &ErpFile, pixels: &[u8]) -> TrackPackage {
    let find = |name: &str| front.load(front.erp.find(name).unwrap()).unwrap();
    let mut view = find(ARRAY);
    let mut surf = find(&format!("{ARRAY}.image"));
    let mut head = surf.fragments[0].data().unwrap();
    head[28..32].copy_from_slice(&1u32.to_le_bytes());
    surf.set_fragment(0, &head).unwrap();
    surf.set_fragment(1, pixels).unwrap();
    view.name = FLAG.into();
    surf.name = format!("{FLAG}.image");
    std::fs::create_dir_all(dir).unwrap();
    front
        .write(
            vec![Entry::Owned(view), Entry::Owned(surf)],
            &dir.join("flagtest.erp"),
        )
        .unwrap();
    let images = serde_json::json!({ "resources": { "flag_circle": FLAG } });
    std::fs::write(dir.join("images.json"), images.to_string()).unwrap();
    let manifest = serde_json::json!({
        "track_code": "flagtest",
        "erp": "flagtest.erp",
        "activation": { "copy_from_track": "bahrain" },
        "ui": { "images_file": "images.json" },
    });
    TrackPackage::parse(manifest.to_string().as_bytes()).unwrap()
}

fn longer_list(stock: &Path, dst: &Path, extra: usize) {
    let file = ErpFile::open(stock).unwrap();
    let mut res: Resource = file.load(file.erp.find(OVERRIDES).unwrap()).unwrap();
    let mut pkg = Pkg::parse(&res.fragments[0].data().unwrap()).unwrap();
    let mut cur = &mut pkg.root;
    for p in [
        "overrides",
        "Contents",
        "tracks",
        "Contents",
        "flags_circle",
        "Contents",
    ] {
        cur = cur.as_obj_mut().unwrap().get_mut(p).unwrap();
    }
    let items = &mut cur.as_arr_mut().unwrap().items;
    for n in 0..extra {
        let mut e = items[0].clone();
        e.as_obj_mut()
            .unwrap()
            .set("Name", Node::Val(Val::str(format!("old_track_{n}"))));
        items.push(e);
    }
    res.set_fragment(0, &pkg.to_bytes().unwrap()).unwrap();
    let entries = file
        .erp
        .resources
        .iter()
        .map(|r| match r.name == OVERRIDES {
            true => Entry::Owned(res.clone()),
            false => Entry::Copy(r),
        })
        .collect();
    std::fs::create_dir_all(dst.parent().unwrap()).unwrap();
    file.write(entries, dst).unwrap();
}

fn build(game: &Path, pkg: &TrackPackage, dir: &Path, db: Option<&Path>, cache: &Path) -> UiBuild {
    let resolve = |rel: &str| match (rel == DB, db) {
        (true, Some(db)) => db.to_path_buf(),
        _ => game.join(rel),
    };
    let maps = [MapUi {
        pkg,
        dir,
        asset_group: track_group("flagtest", "flagtest"),
    }];
    build_ui(&maps, &resolve, cache).unwrap()
}

#[test]
fn a_flag_list_longer_than_its_array_pads_the_array() {
    let Some(game) = std::env::var_os("F1_25_DIR").map(PathBuf::from) else {
        eprintln!("F1_25_DIR is not set; skipping");
        return;
    };
    let out = std::env::temp_dir().join(format!("swofty-track-flags-{}", std::process::id()));
    std::fs::remove_dir_all(&out).ok();
    let (slices, stock) = flags(&game.join(FRONT));
    let size = stock.len() / slices as usize;
    let pixels = vec![0x5a; size];
    let front = ErpFile::open(&game.join(FRONT)).unwrap();
    let pkg = track(&out.join("track"), &front, &pixels);
    let slice = |d: &[u8], i: usize| d[i * size..(i + 1) * size].to_vec();

    let plain = build(&game, &pkg, &out.join("track"), None, &out.join("plain"));
    assert!(plain.notes.is_empty(), "{:?}", plain.notes);
    let (n, data) = flags(&plain.replaced[FRONT]);
    assert_eq!(n, slices + 1);
    assert_eq!(data[..stock.len()], stock[..]);
    assert_eq!(slice(&data, slices as usize), pixels);

    let entries = slices as usize + 3;
    let db = out.join("originals").join(DB);
    longer_list(&game.join(DB), &db, 3);
    for _ in 0..2 {
        let padded = build(&game, &pkg, &out.join("track"), Some(&db), &out.join("padded"));
        assert_eq!(
            padded.notes,
            [format!(
                "tracks.flags_circle: the game's list has {entries} entries but only {slices} pictures; \
                 3 blank picture(s) fill the gap. Repair the game files if a menu shows the wrong picture"
            )]
        );
        let (n, data) = flags(&padded.replaced[FRONT]);
        assert_eq!(n as usize, entries + 1);
        assert_eq!(data[..stock.len()], stock[..]);
        for i in slices as usize..entries {
            assert_eq!(slice(&data, i), slice(&stock, 2), "slice {i} is the blank flag");
        }
        assert_eq!(slice(&data, entries), pixels);
    }
    std::fs::remove_dir_all(&out).ok();
}

#[test]
fn an_addon_with_its_own_track_pictures_leaves_the_built_menus() {
    use swofty_core::asset::Source;
    use swofty_core::erpbuild::ErpBuild;
    use swofty_core::ops::ErpOp;
    let Some(game) = std::env::var_os("F1_25_DIR").map(PathBuf::from) else {
        eprintln!("F1_25_DIR is not set; skipping");
        return;
    };
    let out = std::env::temp_dir().join(format!("swofty-track-kept-{}", std::process::id()));
    std::fs::remove_dir_all(&out).ok();
    let ui = |rel: &str| format!("2025_asset_groups/ui_package/{rel}");
    let array =
        |list: &str| format!("eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.{list}.texarray");
    let minimap = "eaid://ui_package/databases/minimap.uidb";
    let pack = [
        (ui("common_front_end.erp"), array("fe_world.tracks")),
        (ui("flow_career_hub.erp"), array("tracks.flags_circle")),
        (ui("flow_career_season_setup.erp"), array("fe_world.tracks_bg")),
        (ui("flow_race.erp"), minimap.to_owned()),
    ];
    let dir = out.join("track");
    std::fs::create_dir_all(&dir).unwrap();
    let mut pictures = Vec::new();
    let mut images = serde_json::Map::new();
    for (rel, list, key) in [
        (&pack[0].0, "fe_world.tracks", "preview_small"),
        (&pack[1].0, "tracks.flags_circle", "flag_circle"),
        (&pack[2].0, "fe_world.tracks_bg", "preview_background"),
    ] {
        let file = ErpFile::open(&game.join(rel)).unwrap();
        let find = |name: &str| file.load(file.erp.find(name).unwrap()).unwrap();
        let mut view = find(&array(list));
        let mut surf = find(&format!("{}.image", array(list)));
        let mut head = surf.fragments[0].data().unwrap();
        let slices = u32::from_le_bytes(head[28..32].try_into().unwrap()) as usize;
        let data = surf.fragments[1].data().unwrap();
        head[28..32].copy_from_slice(&1u32.to_le_bytes());
        surf.set_fragment(0, &head).unwrap();
        surf.set_fragment(1, &vec![0x5a; data.len() / slices]).unwrap();
        let name = format!("eaid://ui_package/art/textures/track_installer/kepttest/{key}.tif");
        view.name = name.clone();
        surf.name = format!("{name}.image");
        images.insert(key.into(), name.into());
        pictures.push(view);
        pictures.push(surf);
    }
    let front = ErpFile::open(&game.join(&pack[0].0)).unwrap();
    front
        .write(
            pictures.into_iter().map(Entry::Owned).collect(),
            &dir.join("kepttest.erp"),
        )
        .unwrap();
    std::fs::write(
        dir.join("images.json"),
        serde_json::json!({ "resources": images }).to_string(),
    )
    .unwrap();
    std::fs::write(dir.join("minimap.json"), "{}").unwrap();
    let manifest = serde_json::json!({
        "track_code": "kepttest",
        "erp": "kepttest.erp",
        "activation": { "copy_from_track": "bahrain" },
        "ui": { "images_file": "images.json", "minimap_file": "minimap.json" },
    });
    let pkg = TrackPackage::parse(manifest.to_string().as_bytes()).unwrap();
    let maps = [MapUi {
        pkg: &pkg,
        dir: &dir,
        asset_group: track_group("kepttest", "kepttest"),
    }];
    let resolve = |rel: &str| game.join(rel);
    let built = build_ui(&maps, &resolve, &out.join("cache")).unwrap();

    let addon = out.join("addon");
    std::fs::create_dir_all(&addon).unwrap();
    let mut overlays = Vec::new();
    let mut want = Vec::new();
    for (n, (rel, name)) in pack.iter().enumerate() {
        let file = ErpFile::open(&game.join(rel)).unwrap();
        let names = [name.clone(), format!("{name}.image")];
        let stock: Vec<Resource> = file
            .erp
            .resources
            .iter()
            .filter(|r| names.contains(&r.name))
            .map(|r| file.load(r).unwrap())
            .collect();
        for r in &stock {
            assert!(
                built.kept.contains_key(&(rel.clone(), r.name.clone())),
                "{rel}: {} is not kept",
                r.name
            );
            want.push((rel.clone(), r.name.clone()));
        }
        let src = format!("pack{n}.erp");
        file.write(stock.into_iter().map(Entry::Owned).collect(), &addon.join(&src))
            .unwrap();
        overlays.push(ErpOp::Overlay {
            erps: vec![rel.clone()],
            source: Source::addon(&src),
        });
    }
    let base = |rel: &str| built.replaced.get(rel).cloned().unwrap_or_else(|| game.join(rel));
    let mut b = ErpBuild::new(&base);
    for (erp, resource) in built.kept.keys() {
        b.keep(erp, resource);
    }
    for op in &overlays {
        b.apply_for(op, "pack", &|s: &Source| Ok(s.resolve(&addon)?))
            .unwrap();
    }
    assert_eq!(b.resources().count(), 0);
    let held: Vec<(String, String)> = b.take_held().into_iter().map(|h| (h.erp, h.resource)).collect();
    assert_eq!(held, want);
    std::fs::remove_dir_all(&out).ok();
}
