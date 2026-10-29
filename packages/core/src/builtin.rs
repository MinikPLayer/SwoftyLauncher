use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::asset::{FileKind, OpFile};
use crate::install::PROXY;
use crate::manifest::AddonManifest;
use crate::{Result, io_err};

pub use swofty_addon::CORE_ID;
pub const CORE_MANIFEST: &str = include_str!("../../../addons/swoftymod/swofty.json");
pub const CORE_DB: &str = include_str!("../../../addons/swoftymod/db/core.json");
pub const CORE_MODE: &str = "swofty_f1";
pub const CORE_DLL: &str = "swofty/addons/core/swofty_core.dll";
pub const LOADER_ENV: &str = "SWOFTY_LOADER_DIR";

pub fn core_manifest() -> AddonManifest {
    AddonManifest::parse(CORE_MANIFEST).expect("the built-in manifest is valid")
}

pub fn core_db_ops() -> OpFile {
    let doc = serde_json::from_str(CORE_DB).expect("the built-in database ops are JSON");
    OpFile::from_value(doc, FileKind::Database).expect("the built-in database ops are valid")
}

thread_local! {
    static PINNED: std::cell::RefCell<Option<Option<PathBuf>>> = const { std::cell::RefCell::new(None) };
}

pub struct LoaderPin(Option<Option<PathBuf>>);

impl LoaderPin {
    pub fn none() -> Self {
        Self::set(None)
    }

    pub fn to(dir: PathBuf) -> Self {
        Self::set(Some(dir))
    }

    fn set(dir: Option<PathBuf>) -> Self {
        Self(PINNED.with(|p| p.replace(Some(dir))))
    }
}

impl Drop for LoaderPin {
    fn drop(&mut self) {
        PINNED.with(|p| *p.borrow_mut() = self.0.take());
    }
}

pub fn loader_dir() -> Option<PathBuf> {
    if let Some(pinned) = PINNED.with(|p| p.borrow().clone()) {
        return pinned;
    }
    let bin = std::env::current_exe()
        .ok()
        .and_then(|e| Some(e.parent()?.to_path_buf()));
    [
        std::env::var_os(LOADER_ENV).map(PathBuf::from),
        bin.as_ref().and_then(|b| Some(b.parent()?.join("loader"))),
        bin.map(|b| b.join("loader")),
        Some(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../native/dist")),
    ]
    .into_iter()
    .flatten()
    .find(|d| is_loader(d))
}

pub fn is_loader(dir: &Path) -> bool {
    dir.join(PROXY).is_file() && dir.join(CORE_DLL).is_file()
}

pub fn loader_files(dir: &Path) -> Result<BTreeMap<String, PathBuf>> {
    fn walk(dir: &Path, prefix: &str, out: &mut BTreeMap<String, PathBuf>) -> Result<()> {
        for entry in std::fs::read_dir(dir).map_err(io_err(dir))? {
            let path = entry.map_err(io_err(dir))?.path();
            let name = path.file_name().expect("has a name").to_string_lossy();
            let rel = format!("{prefix}{name}");
            if path.is_dir() {
                walk(&path, &format!("{rel}/"), out)?;
            } else {
                out.insert(rel, path);
            }
        }
        Ok(())
    }
    let mut out = BTreeMap::new();
    walk(dir, "", &mut out)?;
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_manifest_is_valid() {
        let m = core_manifest();
        assert_eq!(m.id, CORE_ID);
        assert!(m.is_builtin());
    }

    #[test]
    fn builtin_database_ops_add_the_swofty_f1_modes() {
        use crate::asset::AssetOp;
        use crate::ops::DbOp;

        let file = core_db_ops();
        let ops: Vec<&DbOp> = file
            .ops
            .iter()
            .filter_map(|op| match op {
                AssetOp::Db(op) => Some(op),
                _ => None,
            })
            .collect();
        assert_eq!(ops.len(), file.ops.len());
        let added: Vec<(&str, &str, Option<&str>)> = ops
            .iter()
            .filter_map(|op| match op {
                DbOp::AddRow { table, set, .. } => Some((
                    table.as_str(),
                    set["_metadata_"].as_str(),
                    set.get("stat_id").map(String::as_str),
                )),
                _ => None,
            })
            .collect();
        assert_eq!(
            added,
            [
                ("entrants.entrant_setup_modes", CORE_MODE, None),
                ("game_setup.game_setup_modes", CORE_MODE, Some("20")),
            ]
        );
        let lists = ops
            .iter()
            .filter(|op| matches!(op, DbOp::ListAppend { value, .. } if value == CORE_MODE))
            .count();
        assert_eq!((lists, ops.len()), (50, 52));
    }

    #[test]
    fn lists_every_file_of_the_loader() {
        let dir = std::env::temp_dir().join(format!("swofty-loader-{}", std::process::id()));
        std::fs::remove_dir_all(&dir).ok();
        let core = dir.join("swofty/addons/core");
        std::fs::create_dir_all(&core).unwrap();
        for f in [
            "swofty_core.dll",
            "swofty_gamelog.dll",
            "bindings.json",
            "symbols.tsv",
            "log_channels.tsv",
        ] {
            std::fs::write(core.join(f), f).unwrap();
        }
        assert!(!is_loader(&dir));
        std::fs::write(dir.join(PROXY), "proxy").unwrap();
        assert!(is_loader(&dir));
        let files = loader_files(&dir).unwrap();
        assert_eq!(
            files.keys().collect::<Vec<_>>(),
            [
                "swofty/addons/core/bindings.json",
                "swofty/addons/core/log_channels.tsv",
                "swofty/addons/core/swofty_core.dll",
                "swofty/addons/core/swofty_gamelog.dll",
                "swofty/addons/core/symbols.tsv",
                "version.dll",
            ]
        );
        assert_eq!(files[CORE_DLL], core.join("swofty_core.dll"));
        std::fs::remove_dir_all(&dir).ok();
    }
}
