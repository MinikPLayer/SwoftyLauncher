//! `swofty overlay-exec` against a real unprivileged mount on a temp folder,
//! entered through `/proc` the way the launcher reaches the game's view.
//! Skipped where the system doesn't allow user namespaces or OverlayFS.
#![cfg(target_os = "linux")]

/// A real unprivileged mount on a temp folder, entered through `/proc`
/// the way the launcher reaches the game's view. Skipped where the
/// system doesn't allow user namespaces.
#[test]
fn mounts_and_exposes_the_game_view_through_proc() {
    use std::io::{BufRead, BufReader};
    use std::process::{Command, Stdio};

    let root = std::env::temp_dir().join(format!("swofty-ofs-exec-{}", std::process::id()));
    std::fs::remove_dir_all(&root).ok();
    let (game, common, upper, work, target) = (
        root.join("game"),
        root.join("common"),
        root.join("upper"),
        root.join("work"),
        root.join("target"),
    );
    std::fs::create_dir_all(game.join("data")).unwrap();
    std::fs::create_dir_all(common.join("swofty")).unwrap();
    std::fs::write(game.join("data/stock.bin"), "stock").unwrap();
    std::fs::write(game.join("data/doomed.bin"), "doomed").unwrap();
    std::fs::write(common.join("version.dll"), "loader").unwrap();
    let exe = env!("CARGO_BIN_EXE_swofty");
    let mut child = Command::new(exe)
        .arg("overlay-exec")
        .args(["--lower".as_ref(), common.as_os_str()])
        .args(["--lower".as_ref(), game.as_os_str()])
        .args(["--upper".as_ref(), upper.as_os_str()])
        .args(["--work".as_ref(), work.as_os_str()])
        .args(["--target".as_ref(), target.as_os_str()])
        .args(["--", "sleep", "30"])
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let mut line = String::new();
    BufReader::new(child.stdout.take().unwrap())
        .read_line(&mut line)
        .unwrap();
    if line.trim() != "mounted" {
        let mut err = String::new();
        std::io::Read::read_to_string(child.stderr.as_mut().unwrap(), &mut err).ok();
        child.wait().ok();
        eprintln!("skipped: no unprivileged OverlayFS here: {err}");
        swofty_core::overlayfs::unlock(&root);
        std::fs::remove_dir_all(&root).ok();
        return;
    }
    let view = swofty_core::overlayfs::mount_view(child.id(), &target);
    // The view is the merged tree.
    assert_eq!(
        std::fs::read_to_string(view.join("data/stock.bin")).unwrap(),
        "stock"
    );
    assert_eq!(
        std::fs::read_to_string(view.join("version.dll")).unwrap(),
        "loader"
    );
    // Create, rename onto, and delete a lower file, all through the overlay.
    std::fs::create_dir_all(view.join("swofty/addons/x")).unwrap();
    std::fs::write(view.join("swofty/addons/x/a.tmp"), "new").unwrap();
    std::fs::rename(
        view.join("swofty/addons/x/a.tmp"),
        view.join("swofty/addons/x/a.json"),
    )
    .unwrap();
    std::fs::remove_file(view.join("data/doomed.bin")).unwrap();
    assert!(!view.join("data/doomed.bin").exists());
    assert_eq!(
        std::fs::read_to_string(upper.join("swofty/addons/x/a.json")).unwrap(),
        "new"
    );
    // The delete is a whiteout in the upper dir; the lower layer is intact.
    assert!(upper.join("data/doomed.bin").symlink_metadata().is_ok());
    assert!(game.join("data/doomed.bin").is_file());
    assert!(!game.join("swofty").exists());
    // The host sees no mount, only the empty folder.
    assert_eq!(std::fs::read_dir(&target).unwrap().count(), 0);
    child.kill().ok();
    child.wait().ok();
    swofty_core::overlayfs::unlock(&root);
    std::fs::remove_dir_all(&root).ok();
}
