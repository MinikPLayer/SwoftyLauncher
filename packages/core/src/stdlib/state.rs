//! The folder the core keeps its state files in (docs/std/launcher.md "Where the core writes"):
//! the launcher's data folder, so one stat-id store serves the install whatever the game folder is
//! (a restored game folder, or an OverlayFS session whose game-folder writes go to a profile
//! layer). The launcher names it in `<game>/swofty/state.json` at each install, as the path the
//! game sees: a drive path on Windows, a `Z:\` path under Wine and Proton. The core reads
//! `SWOFTY_STATE_DIR` first. A core that finds the folder unwritable keeps its files in
//! `<game>/swofty/registry/` and the launcher reads both places. The install places `state.json`
//! with the loader's files, so a rollback removes it with them.

use std::path::{Path, PathBuf};

use crate::{Result, io_err};

pub const FILE: &str = "swofty/state.json";

/// The text of `state.json` for the data folder `data`.
pub fn text(data: &Path) -> String {
    let mut text = serde_json::to_string_pretty(&serde_json::json!({
        "version": 1,
        "state_dir": crate::paths::game_folder_view(data),
    }))
    .expect("serialisable");
    text.push('\n');
    text
}

/// Write the file an install places as [`FILE`], in the data folder's `state/`, and return its path.
/// A file that is already right is left alone, so its time stays and an OverlayFS layer built from
/// it stays current.
pub fn stage(data: &Path) -> Result<PathBuf> {
    let file = data.join("state").join("core-state.json");
    let text = text(data);
    if std::fs::read_to_string(&file).is_ok_and(|t| t == text) {
        return Ok(file);
    }
    let dir = file.parent().expect("has parent");
    std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    std::fs::write(&file, text).map_err(io_err(&file))?;
    Ok(file)
}

/// Move what an earlier core left in the game folder into the data folder. Returns one line for
/// each thing moved.
pub fn migrate(data: &Path, game: &Path) -> Result<Vec<String>> {
    Ok(super::careers::migrate(data, game)?
        .into_iter()
        .map(|moved| {
            format!(
                "registry: moved {moved} from the game folder to {}",
                data.display()
            )
        })
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_the_data_folder_as_the_game_sees_it() {
        let v: serde_json::Value =
            serde_json::from_str(&text(Path::new("/home/jos\u{e9}/\u{30b9}/swofty"))).unwrap();
        let want = if cfg!(windows) {
            "/home/jos\u{e9}/\u{30b9}/swofty".to_owned()
        } else {
            "Z:\\home\\jos\u{e9}\\\u{30b9}\\swofty".to_owned()
        };
        assert_eq!(v["state_dir"], want);
        assert_eq!(v["version"], 1);
    }

    #[test]
    fn staging_the_state_file_twice_leaves_its_time_alone() {
        let root = std::env::temp_dir().join(format!("swofty-state-stage-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let file = stage(&root).unwrap();
        assert_eq!(std::fs::read_to_string(&file).unwrap(), text(&root));
        let first = std::fs::metadata(&file).unwrap().modified().unwrap();
        std::thread::sleep(std::time::Duration::from_millis(20));
        assert_eq!(stage(&root).unwrap(), file);
        assert_eq!(std::fs::metadata(&file).unwrap().modified().unwrap(), first);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn migrate_moves_the_old_files_and_reports_them() {
        let root = std::env::temp_dir().join(format!("swofty-state-migrate-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let (data, game) = (root.join("data"), root.join("game"));
        let old = crate::stdlib::reports::legacy_path(&game, crate::stdlib::reports::IDS);
        std::fs::create_dir_all(old.parent().unwrap()).unwrap();
        std::fs::write(&old, "{\"track\":{}}").unwrap();
        let first = migrate(&data, &game).unwrap();
        assert_eq!(first.len(), 1, "{first:?}");
        assert_eq!(
            std::fs::read_to_string(crate::stdlib::careers::kept_ids(&data)).unwrap(),
            "{\"track\":{}}"
        );
        assert!(!old.exists());
        assert!(migrate(&data, &game).unwrap().is_empty());
        std::fs::remove_dir_all(&root).ok();
    }
}
