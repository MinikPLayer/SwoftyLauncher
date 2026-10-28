//! `swofty overlay-exec` against a real unprivileged mount on a temp folder,
//! entered through `/proc` the way the launcher reaches the game's view.
//! Skipped where the system doesn't allow user namespaces or OverlayFS.
#![cfg(target_os = "linux")]

use std::path::PathBuf;
use std::process::{Child, Command, Stdio};

/// A mounted `overlay-exec` anchor and the folders it was built from.
struct Mounted {
    child: Child,
    root: PathBuf,
    game: PathBuf,
    upper: PathBuf,
    target: PathBuf,
}

impl Mounted {
    /// The folder a mount called `name` is built in.
    fn root_of(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!("swofty-ofs-{name}-{}", std::process::id()))
    }

    /// Mount a game folder with a loader layer over it and run `command` in
    /// the namespace; `None` where the system doesn't allow it.
    fn start(name: &str, command: &[&str]) -> Option<Self> {
        use std::io::{BufRead, BufReader};

        let root = Self::root_of(name);
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
            .arg("--")
            .args(command)
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
            return None;
        }
        Some(Self {
            child,
            root,
            game,
            upper,
            target,
        })
    }

    fn view(&self) -> PathBuf {
        swofty_core::overlayfs::mount_view(self.child.id(), &self.target)
    }
}

impl Drop for Mounted {
    fn drop(&mut self) {
        self.child.kill().ok();
        self.child.wait().ok();
        swofty_core::overlayfs::unlock(&self.root);
        std::fs::remove_dir_all(&self.root).ok();
    }
}

/// A real unprivileged mount on a temp folder, entered through `/proc`
/// the way the launcher reaches the game's view. Skipped where the
/// system doesn't allow user namespaces.
#[test]
fn mounts_and_exposes_the_game_view_through_proc() {
    let Some(m) = Mounted::start("exec", &["sleep", "30"]) else {
        return;
    };
    let (view, game, upper, target) = (m.view(), &m.game, &m.upper, &m.target);
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
    assert_eq!(std::fs::read_dir(target).unwrap().count(), 0);
}

/// The game's side of the netplay mailbox, run inside the namespace: answer
/// the first command in `swofty/netplay/inbox` and delete it, as the game does,
/// then keep running like the game, so the namespace stays.
const GAME: &str = r#"
cd "$1" || exit 1
i=0
while [ $i -lt 200 ]; do
  for f in swofty/netplay/inbox/*.json; do
    [ -f "$f" ] || continue
    n=$(basename "$f" .json)
    mkdir -p swofty/netplay/outbox
    printf '{"seq":%s,"op":"status","ok":true,"error":null,"status":{"screen":"main_menu","in_session":false,"role":"none"},"t":0}' "$n" > "swofty/netplay/outbox/$n.json.tmp"
    mv "swofty/netplay/outbox/$n.json.tmp" "swofty/netplay/outbox/$n.json"
    rm "$f"
    exec sleep 30
  done
  sleep 0.05
  i=$((i+1))
done
exit 1
"#;

/// The netplay mailbox between the launcher and a game in a live mount, the
/// launcher finding the mount from `state/overlay/session.json` alone.
#[test]
fn a_netplay_command_round_trips_with_the_game_inside_the_mount() {
    use swofty_core::multiplayer::game::{self, Screen};
    use swofty_core::overlayfs::{Anchor, HostProc, LiveSession, Proc};

    let target = Mounted::root_of("netplay").join("target");
    let Some(m) = Mounted::start("netplay", &["sh", "-c", GAME, "game", &target.to_string_lossy()]) else {
        return;
    };
    assert_eq!(m.target, target);
    // The data folder the launcher keeps, with the live session's record.
    let data = m.root.join("data");
    let pid = m.child.id();
    LiveSession {
        schema: 1,
        game: m.game.clone(),
        profile: "default".into(),
        target: m.target.clone(),
        upper: m.upper.clone(),
        anchor: Anchor {
            pid,
            start_time: HostProc.start_time(pid).unwrap(),
            mnt_ns: HostProc.mnt_ns(pid).unwrap(),
        },
    }
    .write(&data)
    .unwrap();
    // The launcher finds the game's folder from the record, not the real folder.
    let dir = game::view(&data, &m.game).unwrap();
    assert_eq!(dir, m.view());
    let reply = game::send(&dir, &game::Command::Status, std::time::Duration::from_secs(10)).unwrap();
    assert_eq!(reply.status.screen, Screen::MainMenu);
    // Each side deleted the other's file through the overlay: both folders are empty
    // as the game sees them, and the real game folder never gained a `swofty/`.
    let left = |sub: &str| {
        std::fs::read_dir(dir.join("swofty/netplay").join(sub))
            .unwrap()
            .count()
    };
    assert_eq!((left("inbox"), left("outbox")), (0, 0));
    assert!(m.upper.join("swofty/netplay/inbox").is_dir());
    assert!(!m.game.join("swofty").exists());
    // The game folder as the game sees it, with the real one still intact.
    assert_eq!(
        std::fs::read_to_string(dir.join("data/stock.bin")).unwrap(),
        "stock"
    );
}
