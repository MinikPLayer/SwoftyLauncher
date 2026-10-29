use std::path::{Path, PathBuf};
use std::process::{Command, Output, Stdio};

use swofty_core::save_backup;

fn data_dir(name: &str) -> PathBuf {
    let root = std::env::temp_dir().join(format!("swofty-cli-save-backups-{name}-{}", std::process::id()));
    std::fs::remove_dir_all(&root).ok();
    std::fs::create_dir_all(&root).unwrap();
    root
}

fn swofty(root: &Path, args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_swofty"))
        .args(args)
        .env("SWOFTY_DATA_DIR", root.join("data"))
        .stdin(Stdio::null())
        .output()
        .unwrap()
}

#[test]
fn lists_backups_and_refuses_an_unknown_one() {
    let root = data_dir("list");
    let out = swofty(&root, &["saves", "list"]);
    assert!(out.status.success());
    assert!(String::from_utf8_lossy(&out.stdout).starts_with("no profile save backups in "));

    let remote = root.join("remote");
    std::fs::create_dir_all(&remote).unwrap();
    std::fs::write(remote.join("savegame@profile#NXDSMWW.BWW"), [1u8; 900]).unwrap();
    std::fs::write(remote.join("savegame@profile#QKRHMYXE"), [7u8; 416]).unwrap();
    let b = save_backup::take(
        &root.join("data"),
        &remote,
        "default",
        &["swofty.progressive".into()],
        "20261027-010203",
        "session",
    )
    .unwrap()
    .unwrap();
    let out = swofty(&root, &["saves", "list"]);
    assert!(out.status.success());
    assert_eq!(
        String::from_utf8_lossy(&out.stdout),
        format!(
            "{}  profile default, session, without swofty.progressive, 2 file(s), 1316 bytes\n",
            b.name
        )
    );

    let out = swofty(&root, &["saves", "restore", "nope"]);
    assert!(!out.status.success());
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(
        err.contains("no save backup named nope")
            || err.contains("F1 25 is running")
            || err.contains("Steam is running"),
        "{err}"
    );
    assert_eq!(
        std::fs::read(remote.join("savegame@profile#NXDSMWW.BWW")).unwrap(),
        [1u8; 900]
    );
}
