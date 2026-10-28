//! What the OverlayFS fallback dialog is asked and what it remembers; the
//! window itself is `overlayfs_dialog`.

use std::path::PathBuf;

use swofty_core::overlayfs::Kind;
use swofty_core::store::Store;

pub(crate) const FLAG: &str = "--overlayfs-fallback-dialog";
/// The dialog's exit codes, which the wrapper reads.
pub(crate) const CONTINUE: i32 = 0;
pub(crate) const EXIT: i32 = 1;

/// `--overlayfs-fallback-dialog <kind> <reason> [--data-dir <dir>]`, if that
/// is what the launcher was started for.
pub(crate) fn requested(args: &[String]) -> Option<(Kind, String, Option<PathBuf>)> {
    let at = args.iter().position(|a| a == FLAG)?;
    let kind = Kind::from_arg(args.get(at + 1)?)?;
    let reason = args.get(at + 2)?.clone();
    let data = args
        .iter()
        .position(|a| a == "--data-dir")
        .and_then(|i| args.get(i + 1))
        .map(PathBuf::from);
    Some((kind, reason, data))
}

/// Save "Don't show again" for `kind`. Only Continue remembers it: hiding
/// the dialog must never mean "always cancel". Each kind has its own key, so
/// hiding one never hides the other.
pub(crate) fn remember(store: &Store, kind: Kind, hide: bool, code: i32) {
    if code == CONTINUE && hide {
        let mut config = store.config();
        if config.news_seen.insert(kind.key().into()) {
            store.save_config(&config).ok();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(list: &[&str]) -> Vec<String> {
        list.iter().map(|s| (*s).to_owned()).collect()
    }

    #[test]
    fn only_continue_remembers_and_only_its_own_kind() {
        let root = std::env::temp_dir().join(format!("swofty-dialog-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        std::fs::create_dir_all(&root).unwrap();
        let store = Store::new(root.clone());
        let seen = |k: Kind| store.config().news_seen.contains(k.key());
        remember(&store, Kind::MountFailed, true, EXIT);
        assert!(!seen(Kind::MountFailed), "Exit with the box ticked saves nothing");
        remember(&store, Kind::MountFailed, false, CONTINUE);
        assert!(!seen(Kind::MountFailed), "Continue without the box saves nothing");
        remember(&store, Kind::MountFailed, true, CONTINUE);
        assert!(seen(Kind::MountFailed) && !seen(Kind::Unsupported));
        remember(&store, Kind::Unsupported, true, CONTINUE);
        assert!(seen(Kind::Unsupported));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn reads_the_dialog_request() {
        let got = requested(&args(&[
            "swofty-launcher",
            FLAG,
            "mount-failed",
            "mount: Operation not permitted",
            "--data-dir",
            "/d",
        ]));
        assert_eq!(
            got,
            Some((
                Kind::MountFailed,
                "mount: Operation not permitted".to_owned(),
                Some(PathBuf::from("/d"))
            ))
        );
        assert_eq!(requested(&args(&["swofty-launcher"])), None);
        assert_eq!(requested(&args(&["swofty-launcher", FLAG, "nope", "x"])), None);
    }
}
