use std::path::PathBuf;
use std::process::Command;

use swofty_formats::erp::Erp;
use swofty_formats::gvdb::Gvdb;
use swofty_formats::lng::Lng;
use swofty_formats::nefs::Archive;
use swofty_formats::pkg::{Pkg, is_pkg};

const GVDB: &str = "game_package/gamedatabases/gamedatabases.2025.bin";
const ERPS: &[&str] = &[
    "2025_asset_groups/ui_package/flow_frontend.erp",
    "2025_asset_groups/ui_package/persistent.erp",
    "2025_asset_groups/ui_package/databases.erp",
    "2025_asset_groups/ui_package/screens/fe_home.erp",
    "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_f1_26.erp",
    "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_f1_26_myteam.erp",
    "2025_asset_groups/environment_package/tracks/bahrain/wep/bahrain_common.erp",
];
const PKG_ERPS: &[&str] = &[
    "2025_asset_groups/ui_package/flow_frontend.erp",
    "2025_asset_groups/ui_package/persistent.erp",
    "2025_asset_groups/ui_package/databases.erp",
    "2025_asset_groups/ui_package/screens/fe_home.erp",
];

fn game_dir() -> Option<PathBuf> {
    match std::env::var_os("F1_25_DIR") {
        Some(d) => Some(PathBuf::from(d)),
        None => {
            eprintln!("F1_25_DIR is not set; skipping");
            None
        }
    }
}

fn read(dir: &std::path::Path, rel: &str) -> Option<Vec<u8>> {
    let path = dir.join(rel);
    match std::fs::read(&path) {
        Ok(d) => Some(d),
        Err(e) => {
            eprintln!("skipping {}: {e}", path.display());
            None
        }
    }
}

fn oracle() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tools")
}

#[test]
fn gvdb_round_trip_is_byte_identical() {
    let Some(dir) = game_dir() else { return };
    let Some(data) = read(&dir, GVDB) else { return };
    let db = Gvdb::parse(&data).unwrap();
    assert!(db.to_bytes() == data, "GVDB round trip differs");
    assert_eq!(
        db.get_text("tracks.Track_3.instanceName").as_deref(),
        Some("bahrain")
    );
    assert_eq!(db.instance_count("entrants.teams", "team"), Some(358));
    assert_eq!(db.instance_count("entrants.vehicles", "vehicle"), Some(413));
}

#[test]
fn gvdb_edit_matches_python_oracle() {
    let Some(dir) = game_dir() else { return };
    let Some(data) = read(&dir, GVDB) else { return };
    let out = std::env::temp_dir().join(format!("swofty-gvdb-oracle-{}.bin", std::process::id()));
    let status = Command::new("python3")
        .arg(oracle().join("gamedb.py"))
        .args([
            "set",
            dir.join(GVDB).to_str().unwrap(),
            "tracks.Track_3.numberOfCorners",
            "16",
            "-o",
        ])
        .arg(&out)
        .status();
    let Ok(status) = status else {
        eprintln!("python3 not available; skipping");
        return;
    };
    assert!(status.success());
    let expected = std::fs::read(&out).unwrap();
    std::fs::remove_file(&out).ok();
    let mut db = Gvdb::parse(&data).unwrap();
    db.set("tracks.Track_3.numberOfCorners", "16", None).unwrap();
    assert!(
        db.to_bytes() == expected,
        "Rust edit differs from tools/gamedb.py"
    );
}

#[test]
fn language_files_come_out_of_the_exe() {
    let Some(dir) = game_dir() else { return };
    let Ok(mut exe) = std::fs::File::open(dir.join("F1_25.exe")) else {
        eprintln!("no F1_25.exe under F1_25_DIR; skipping");
        return;
    };
    let version = swofty_formats::pe::file_version(&mut exe).unwrap().unwrap();
    assert_eq!(version.split('.').count(), 4, "{version}");
    let archive = Archive::open_exe(&mut exe).unwrap();
    let mut data = std::fs::File::open(dir.join(&archive.volume)).unwrap();
    let langs: Vec<_> = archive
        .entries
        .iter()
        .filter(|e| e.path.starts_with("localisation/2025_release/language_") && e.path.ends_with(".lng"))
        .collect();
    assert!(langs.len() >= 11, "{} language files", langs.len());
    for entry in langs {
        let bytes = archive.read(entry, &mut data).unwrap();
        let (lng, warnings) =
            Lng::parse_with_warnings(&bytes).unwrap_or_else(|e| panic!("{}: {e}", entry.path));
        assert!(warnings.is_empty(), "{}: {warnings:?}", entry.path);
        assert!(lng.entries.len() > 50_000, "{}", entry.path);
        assert_eq!(Lng::parse(&lng.to_bytes()).unwrap(), lng, "{}", entry.path);
    }
}

#[test]
fn erp_round_trip_is_byte_identical() {
    let Some(dir) = game_dir() else { return };
    for rel in ERPS {
        let Some(data) = read(&dir, rel) else { continue };
        let mut erp = Erp::parse(&data).unwrap();
        assert!(erp.to_bytes() == data, "{rel}: ERP round trip differs");
        for res in erp.resources.iter().take(50) {
            assert_eq!(res.content_hash().unwrap(), res.hash, "{rel}: {} hash", res.name);
        }
    }
}

#[test]
fn pkg_round_trip_is_byte_identical() {
    let Some(dir) = game_dir() else { return };
    let mut checked = 0;
    for rel in PKG_ERPS {
        let Some(data) = read(&dir, rel) else { continue };
        let erp = Erp::parse(&data).unwrap();
        for res in &erp.resources {
            for (i, frag) in res.fragments.iter().enumerate() {
                let body = frag.data().unwrap();
                if !is_pkg(&body) {
                    continue;
                }
                let pkg = Pkg::parse(&body).unwrap_or_else(|e| panic!("{rel}: {} [{i}]: {e}", res.name));
                let again = pkg.to_bytes().unwrap();
                assert!(
                    again == body,
                    "{rel}: {} [{i}]: !pkg round trip differs",
                    res.name
                );
                checked += 1;
            }
        }
    }
    eprintln!("{checked} !pkg fragments round-tripped");
}

#[test]
fn track_layers_round_trip_byte_identically() {
    let Some(dir) = game_dir() else { return };
    let tracks = dir.join("2025_asset_groups/environment_package/tracks");
    let Ok(entries) = std::fs::read_dir(&tracks) else {
        eprintln!("no tracks under F1_25_DIR; skipping");
        return;
    };
    let mut checked = 0;
    for entry in entries.flatten() {
        let track = entry.file_name().to_string_lossy().into_owned();
        let path = entry.path().join(format!("wep/{track}_f1_26_myteam.erp"));
        if !path.is_file() {
            continue;
        }
        let layers = Erp::read_matching(&path, |n| n.ends_with(".layer")).unwrap();
        for res in &layers {
            let body = res.fragments[0].data().unwrap();
            let pkg = Pkg::parse(&body).unwrap_or_else(|e| panic!("{track}: {}: {e}", res.name));
            assert!(
                pkg.to_bytes().unwrap() == body,
                "{track}: {}: !pkg round trip differs",
                res.name
            );
            checked += 1;
        }
    }
    eprintln!("{checked} track layers round-tripped");
}

#[test]
fn zstd_recompression_matches_the_cli() {
    let Some(dir) = game_dir() else { return };
    let rel = "2025_asset_groups/ui_package/screens/fe_home.erp";
    let Some(data) = read(&dir, rel) else { return };
    let erp = Erp::parse(&data).unwrap();
    let Some(frag) = erp
        .resources
        .iter()
        .flat_map(|r| &r.fragments)
        .find(|f| f.compression == swofty_formats::erp::ZSTD)
    else {
        return;
    };
    let body = frag.data().unwrap();
    let Ok(mut child) = Command::new("zstd")
        .args(["-c", "-19", "-q"])
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .spawn()
    else {
        eprintln!("zstd not available; skipping");
        return;
    };
    std::io::Write::write_all(&mut child.stdin.take().unwrap(), &body).unwrap();
    let cli = child.wait_with_output().unwrap().stdout;
    assert!(
        swofty_formats::erp::compress(&body).unwrap() == cli,
        "zstd output differs from the CLI"
    );
}
