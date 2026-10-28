//! SWOFTY_PIPELINE: a hash of the sources that decide what a profile build
//! writes, so a build made by one launcher release is reused by the next
//! when neither changed how builds are made. It covers this crate's sources
//! except the modules that never shape a build, the formats and addon API
//! crates, the data files they embed, and the versions of every dependency
//! outside this workspace.

use std::path::{Path, PathBuf};

/// Modules of this crate that don't decide what a build writes.
const UNRELATED: &[&str] = &[
    "addon_storage.rs",
    "app_control.rs",
    "catalog",
    "compat.rs",
    "crash.rs",
    "disk.rs",
    "ea_app.rs",
    "install.rs",
    "journal.rs",
    "known_problems.rs",
    "launch.rs",
    "loader_check.rs",
    "mpauth.rs",
    "multiplayer",
    "offline.rs",
    "overlay_guard.rs",
    "process.rs",
    "quarantine.rs",
    "recovery.rs",
    "save_backup.rs",
    "saves.rs",
    "send_logs",
    "session.rs",
    "setup.rs",
    "steam_client.rs",
    "stock.rs",
    "update.rs",
    "updater.rs",
    "usage.rs",
];

/// Files outside the source trees that the build embeds.
const DATA: &[&str] = &[
    "../../addons/swoftymod/swofty.json",
    "../../addons/swoftymod/db/core.json",
    "../../tools/gamedb/catalog/keys.tsv.gz",
    "../../tools/gamedb/catalog/tables.tsv",
    "../../tools/uicatalog/catalog.json",
    "../../re/db/ui/loader_rules.json",
];

fn files(dir: &Path, skip: &[&str], out: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for e in entries.flatten() {
        let path = e.path();
        let name = e.file_name().to_string_lossy().into_owned();
        if skip.contains(&name.as_str()) {
            continue;
        }
        if path.is_dir() {
            files(&path, &[], out);
        } else {
            out.push(path);
        }
    }
}

fn fnv(hash: &mut u64, bytes: &[u8]) {
    for b in bytes {
        *hash ^= u64::from(*b);
        *hash = hash.wrapping_mul(0x100_0000_01b3);
    }
}

fn main() {
    let root = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("set by cargo"));
    let mut paths = Vec::new();
    files(&root.join("src"), UNRELATED, &mut paths);
    for krate in ["../formats/src", "../addon-api/src"] {
        files(&root.join(krate), &[], &mut paths);
    }
    paths.extend(DATA.iter().map(|d| root.join(d)));
    paths.sort();
    let mut hash = 0xcbf2_9ce4_8422_2325_u64;
    for path in &paths {
        let rel = path
            .strip_prefix(&root)
            .unwrap_or(path)
            .to_string_lossy()
            .replace('\\', "/");
        fnv(&mut hash, rel.as_bytes());
        let text = std::fs::read(path).unwrap_or_default();
        let text = String::from_utf8(text.clone())
            .map(|t| t.replace("\r\n", "\n").into_bytes())
            .unwrap_or(text);
        fnv(&mut hash, &text);
    }
    let lock = std::fs::read_to_string(root.join("../../Cargo.lock")).unwrap_or_default();
    for block in lock.split("\n\n").filter(|b| !b.contains("name = \"swofty")) {
        fnv(&mut hash, block.as_bytes());
    }
    println!("cargo:rustc-env=SWOFTY_PIPELINE={hash:016x}");
    for dir in ["src", "../formats/src", "../addon-api/src", "../../Cargo.lock"] {
        println!("cargo:rerun-if-changed={}", root.join(dir).display());
    }
    for d in DATA {
        println!("cargo:rerun-if-changed={}", root.join(d).display());
    }
}
