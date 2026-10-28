//! Exercises offline staging against a real installed extension.
//!
//! Skipped unless SWOFTY_OFFLINE_TEST_DIR points at an extension directory
//! (CI has no extension to run). Everything works on throwaway folders.

use std::path::PathBuf;

use swofty_core::offline::{self, Artifacts};

fn artifacts() -> Option<Artifacts> {
    let dir = PathBuf::from(std::env::var("SWOFTY_OFFLINE_TEST_DIR").ok()?);
    let (prelaunch, probe) = if cfg!(windows) {
        ("f1op-prelaunch.exe", "f1op-probe.exe")
    } else {
        ("f1op-prelaunch-linux-x86_64", "f1op-probe-linux-x86_64")
    };
    Some(Artifacts {
        hook: dir.join("f1op_hook.dll"),
        stub: dir.join("f1op_stub.exe"),
        prelaunch: dir.join(prelaunch),
        probe: dir.join(probe),
        dir,
    })
}

#[test]
fn stage_creates_the_journal_directory() {
    let Some(a) = artifacts() else {
        eprintln!("skipping: set SWOFTY_OFFLINE_TEST_DIR to an installed extension");
        return;
    };
    let tmp = std::env::temp_dir().join(format!("swofty-stage-{}", std::process::id()));
    std::fs::remove_dir_all(&tmp).ok();
    let game = tmp.join("game");
    std::fs::create_dir_all(&game).unwrap();
    std::fs::write(game.join("EAAntiCheat.GameServiceLauncher.exe"), "original").unwrap();

    // The parent directory does not exist yet: this is the regression for the
    // first real run failing with "cannot write journal".
    let journal = tmp.join("state/offline/staging.jsonl");
    let notes = offline::stage(&game, &a, &journal).expect("staging with a nested journal path");
    assert!(notes.iter().any(|n| n.contains("staged offline play")));
    assert!(journal.is_file());

    // And the session unrolls back to vanilla.
    offline::restore(&journal, true).unwrap();
    assert_eq!(
        std::fs::read_to_string(game.join("EAAntiCheat.GameServiceLauncher.exe")).unwrap(),
        "original"
    );
    assert!(!game.join("EAAntiCheat.GameServiceLauncher.exe.bak").exists());
    assert!(!game.join("swofty/mods/core/f1op_hook.dll").exists());
    std::fs::remove_dir_all(&tmp).ok();
}

#[test]
fn stage_and_restore_under_a_non_ascii_data_folder() {
    let Some(a) = artifacts() else {
        eprintln!("skipping: set SWOFTY_OFFLINE_TEST_DIR to an installed extension");
        return;
    };
    // A data folder under a Windows user name such as Naël: the extension,
    // the journal and the game all sit below it.
    let tmp = std::env::temp_dir().join(format!("swofty-stage-unicode-{}", std::process::id()));
    std::fs::remove_dir_all(&tmp).ok();
    let data = tmp.join("Naël Müller 测试");
    let ext = offline::extension_dir(&data);
    std::fs::create_dir_all(&ext).unwrap();
    let copy = |from: &PathBuf| {
        let to = ext.join(from.file_name().unwrap());
        std::fs::copy(from, &to).unwrap();
        to
    };
    let a = Artifacts {
        hook: copy(&a.hook),
        stub: copy(&a.stub),
        prelaunch: copy(&a.prelaunch),
        probe: copy(&a.probe),
        dir: ext.clone(),
    };
    let game = data.join("game");
    std::fs::create_dir_all(&game).unwrap();
    std::fs::write(game.join("EAAntiCheat.GameServiceLauncher.exe"), "original").unwrap();

    let journal = offline::journal_path(&data);
    offline::stage(&game, &a, &journal).expect("staging under a non-ASCII data folder");
    let text = std::fs::read_to_string(&journal).expect("the journal is UTF-8");
    assert!(text.contains("Naël Müller 测试"), "{text}");

    offline::restore(&journal, false).unwrap();
    assert_eq!(
        std::fs::read_to_string(game.join("EAAntiCheat.GameServiceLauncher.exe")).unwrap(),
        "original"
    );
    assert!(!game.join("EAAntiCheat.GameServiceLauncher.exe.bak").exists());
    assert!(!game.join("swofty/mods/core/f1op_hook.dll").exists());
    std::fs::remove_dir_all(&tmp).ok();
}

fn ea_program(body: &str) -> Vec<u8> {
    let mut pe = vec![0u8; 0x200];
    pe[..2].copy_from_slice(b"MZ");
    pe[0x3c..0x40].copy_from_slice(&0x80u32.to_le_bytes());
    pe[0x80..0x84].copy_from_slice(b"PE\0\0");
    pe[0x98..0x9a].copy_from_slice(&0x20bu16.to_le_bytes());
    pe.extend_from_slice(body.as_bytes());
    let table = b"\0\0\0\0certificate O=Electronic Arts, Inc.";
    let at = pe.len() as u32;
    pe[0x128..0x12c].copy_from_slice(&at.to_le_bytes());
    pe[0x12c..0x130].copy_from_slice(&(table.len() as u32).to_le_bytes());
    pe.extend_from_slice(table);
    pe
}

#[test]
fn an_ea_app_repair_mid_session_is_staged_over_and_rolled_back_to_eas_file() {
    let Some(a) = artifacts() else {
        eprintln!("skipping: set SWOFTY_OFFLINE_TEST_DIR to an installed extension");
        return;
    };
    let tmp = std::env::temp_dir().join(format!("swofty-stage-ea-repair-{}", std::process::id()));
    std::fs::remove_dir_all(&tmp).ok();
    let game = tmp.join("game");
    std::fs::create_dir_all(&game).unwrap();
    let launcher = game.join("EAAntiCheat.GameServiceLauncher.exe");
    std::fs::write(&launcher, ea_program("the launcher the EA app installed")).unwrap();
    let journal = tmp.join("state/offline/staging.jsonl");
    offline::stage(&game, &a, &journal).unwrap();
    assert_eq!(offline::slot(&journal).unwrap(), Some(offline::Slot::Staged));

    let updated = ea_program("the launcher EA's updater wrote");
    std::fs::write(&launcher, &updated).unwrap();
    assert_eq!(
        offline::restage(&a, &journal).unwrap(),
        Some(offline::Slot::Replaced)
    );
    assert_eq!(std::fs::read(&launcher).unwrap(), std::fs::read(&a.stub).unwrap());
    assert_eq!(offline::slot(&journal).unwrap(), Some(offline::Slot::Staged));

    let done = offline::restore(&journal, false).unwrap();
    assert_eq!(std::fs::read(&launcher).unwrap(), updated, "{done:?}");
    assert!(!game.join("EAAntiCheat.GameServiceLauncher.exe.bak").exists());
    assert!(!game.join("swofty/mods/core/f1op_hook.dll").exists());
    std::fs::remove_dir_all(&tmp).ok();
}
