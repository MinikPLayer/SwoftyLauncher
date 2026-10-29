use std::path::{Path, PathBuf};

use crate::vdf::{self, Vdf};
use crate::{GAME_EXE, STEAM_APP_ID};

#[derive(Debug, Clone, PartialEq)]
pub struct GameInstall {
    pub steam_root: PathBuf,
    pub library: PathBuf,
    pub dir: PathBuf,
    pub build_id: Option<u64>,
    pub compatdata: Option<PathBuf>,
}

impl GameInstall {
    pub fn exe(&self) -> PathBuf {
        self.dir.join(GAME_EXE)
    }

    pub fn prefix(&self) -> Option<PathBuf> {
        self.compatdata.as_ref().map(|c| c.join("pfx"))
    }
}

pub fn steam_roots() -> Vec<PathBuf> {
    let mut out: Vec<PathBuf> = Vec::new();
    if let Some(dir) = std::env::var_os("SWOFTY_STEAM_ROOT") {
        out.push(dir.into());
    }
    if cfg!(windows) {
        for var in ["ProgramFiles(x86)", "ProgramFiles"] {
            if let Some(p) = std::env::var_os(var) {
                out.push(PathBuf::from(p).join("Steam"));
            }
        }
    } else if let Some(home) = std::env::var_os("HOME").map(PathBuf::from) {
        out.push(home.join(".steam/steam"));
        out.push(home.join(".local/share/Steam"));
        out.push(home.join(".var/app/com.valvesoftware.Steam/.local/share/Steam"));
        out.push(home.join(".var/app/com.valvesoftware.Steam/data/Steam"));
    }
    let mut seen = Vec::new();
    out.retain(|p| {
        let real = p.canonicalize().unwrap_or_else(|_| p.clone());
        let keep = p.join("steamapps").is_dir() && !seen.contains(&real);
        seen.push(real);
        keep
    });
    out
}

fn read_vdf(path: &Path) -> Option<Vdf> {
    vdf::parse(&std::fs::read_to_string(path).ok()?)
}

pub fn libraries(steam_root: &Path) -> Vec<PathBuf> {
    let mut libs = vec![steam_root.to_path_buf()];
    if let Some(v) = read_vdf(&steam_root.join("steamapps/libraryfolders.vdf")) {
        let entries = v.get("libraryfolders").into_iter().flat_map(Vdf::entries);
        for (_, e) in entries {
            if let Some(p) = e.get("path").and_then(Vdf::str) {
                let p = PathBuf::from(p);
                let real = |x: &Path| x.canonicalize().unwrap_or_else(|_| x.to_path_buf());
                if !libs.iter().any(|l| real(l) == real(&p)) {
                    libs.push(p);
                }
            }
        }
    }
    libs
}

pub fn find_game() -> Option<GameInstall> {
    steam_roots().into_iter().find_map(|root| find_game_in(&root))
}

pub fn find_game_in(steam_root: &Path) -> Option<GameInstall> {
    libraries(steam_root).into_iter().find_map(|lib| {
        let steamapps = lib.join("steamapps");
        let acf = read_vdf(&steamapps.join(format!("appmanifest_{STEAM_APP_ID}.acf")))?;
        let state = acf.get("AppState")?;
        let dir = steamapps.join("common").join(state.get("installdir")?.str()?);
        let build_id = state
            .get("buildid")
            .and_then(Vdf::str)
            .and_then(|b| b.parse().ok());
        let compat = steamapps.join("compatdata").join(STEAM_APP_ID.to_string());
        Some(GameInstall {
            steam_root: steam_root.to_path_buf(),
            library: lib.clone(),
            dir,
            build_id,
            compatdata: compat.is_dir().then_some(compat),
        })
    })
}

const VALIDATING: u64 = 0x2_0000;
// EAppState bits for a running update, uninstall or backup, and the stages of
// a download: Steam is writing the game's files.
const CHANGING: u64 = 0x100
    | 0x400
    | 0x800
    | 0x1000
    | 0x1_0000
    | 0x4_0000
    | 0x8_0000
    | 0x10_0000
    | 0x20_0000
    | 0x40_0000
    | 0x80_0000;

/// What Steam is doing to the game's files: "verifying" while it validates
/// them, "updating" while it changes them any other way, None when it leaves
/// them alone. Steam keeps the `StateFlags` of the app manifest as they were
/// while it verifies, so a running Steam's content log, which records every
/// state change, is read first.
pub fn busy(install: &GameInstall) -> Option<&'static str> {
    let running = crate::steam_client::status().state != crate::steam_client::State::Stopped;
    running
        .then(|| log_busy(&install.steam_root.join("logs/content_log.txt")))
        .flatten()
        .or_else(|| manifest_busy(install))
}

/// The game's last state in Steam's content log: busy while an update runs,
/// and verifying when the update is a check of the installed files.
fn log_busy(log: &Path) -> Option<&'static str> {
    use std::io::{Read, Seek, SeekFrom};
    let mut file = std::fs::File::open(log).ok()?;
    let len = file.metadata().ok()?.len();
    file.seek(SeekFrom::Start(len.saturating_sub(256 * 1024))).ok()?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes).ok()?;
    let text = String::from_utf8_lossy(&bytes);
    let last = |what: &str| {
        let key = format!("AppID {STEAM_APP_ID} {what} : ");
        text.lines()
            .rev()
            .find_map(|l| l.split_once(key.as_str()).map(|(_, state)| state.to_owned()))
    };
    if !last("state changed")?.contains("Update Running") {
        return None;
    }
    Some(
        if last("App update changed").is_some_and(|u| u.contains("Verifying")) {
            "verifying"
        } else {
            "updating"
        },
    )
}

fn manifest_busy(install: &GameInstall) -> Option<&'static str> {
    let acf = read_vdf(
        &install
            .library
            .join("steamapps")
            .join(format!("appmanifest_{STEAM_APP_ID}.acf")),
    )?;
    let flags: u64 = acf.path(&["AppState", "StateFlags"])?.str()?.parse().ok()?;
    if flags & VALIDATING != 0 {
        Some("verifying")
    } else if flags & CHANGING != 0 {
        Some("updating")
    } else {
        None
    }
}

/// True when `dir` is a Steam copy of the game: it sits in a library's
/// `steamapps/common` next to the app's manifest.
pub fn in_steam_library(dir: &Path) -> bool {
    let shaped = |dir: &Path| {
        let named = |p: Option<&Path>, name: &str| {
            p.and_then(Path::file_name)
                .is_some_and(|n| n.to_string_lossy().eq_ignore_ascii_case(name))
        };
        let common = dir.parent();
        let steamapps = common.and_then(Path::parent);
        named(common, "common")
            && named(steamapps, "steamapps")
            && steamapps.is_some_and(|s| s.join(format!("appmanifest_{STEAM_APP_ID}.acf")).is_file())
    };
    shaped(dir) || dir.canonicalize().is_ok_and(|real| shaped(&real))
}

pub const STEAMVR_APP_ID: u32 = 250_820;

pub fn steamvr_installed(steam_root: &Path) -> bool {
    libraries(steam_root).iter().any(|lib| {
        lib.join(format!("steamapps/appmanifest_{STEAMVR_APP_ID}.acf"))
            .is_file()
    })
}

pub fn launch_options(steam_root: &Path) -> Vec<(String, String)> {
    let Ok(users) = std::fs::read_dir(steam_root.join("userdata")) else {
        return Vec::new();
    };
    users
        .flatten()
        .filter_map(|u| {
            let cfg = read_vdf(&u.path().join("config/localconfig.vdf"))?;
            let apps = cfg.path(&["UserLocalConfigStore", "Software", "Valve", "Steam", "apps"])?;
            let opts = apps.get(&STEAM_APP_ID.to_string())?.get("LaunchOptions")?.str()?;
            Some((u.file_name().to_string_lossy().into_owned(), opts.to_owned()))
        })
        .collect()
}

pub fn compat_tool(steam_root: &Path) -> Option<String> {
    let cfg = read_vdf(&steam_root.join("config/config.vdf"))?;
    let map = cfg.path(&[
        "InstallConfigStore",
        "Software",
        "Valve",
        "Steam",
        "CompatToolMapping",
    ])?;
    let name = |key: &str| {
        map.get(key)?
            .get("name")?
            .str()
            .filter(|n| !n.is_empty())
            .map(str::to_owned)
    };
    name(&STEAM_APP_ID.to_string()).or_else(|| name("0"))
}

fn valve_folder_name(tool: &str) -> Option<String> {
    let rest = tool.strip_prefix("proton_")?;
    match rest {
        "experimental" => Some("Proton - Experimental".to_owned()),
        "hotfix" => Some("Proton Hotfix".to_owned()),
        v if !v.is_empty() && v.chars().all(|c| c.is_ascii_digit()) => Some(format!("Proton {v}.0")),
        _ => None,
    }
}

fn manifest_names(dir: &Path) -> Vec<String> {
    let Some(v) = read_vdf(&dir.join("compatibilitytool.vdf")) else {
        return Vec::new();
    };
    let Some(tools) = v.path(&["compatibilitytools", "compat_tools"]) else {
        return Vec::new();
    };
    tools
        .entries()
        .flat_map(|(k, e)| {
            [
                Some(k.clone()),
                e.get("display_name").and_then(Vdf::str).map(str::to_owned),
            ]
        })
        .flatten()
        .collect()
}

pub struct ProtonSearch {
    pub found: Option<PathBuf>,
    pub searched: Vec<PathBuf>,
}

/// The Proton directory (the one holding `proton`) for the compat tool name
/// Steam records: a folder of that name, a tool whose compatibilitytool.vdf
/// declares the name, or Valve's own folder for an internal name such as
/// `proton_9`. Searches compatibilitytools.d and every library's
/// steamapps/common, with `first` searched ahead of the rest.
pub fn find_proton(steam_root: &Path, first: Option<&Path>, tool: &str) -> ProtonSearch {
    let mut homes = vec![steam_root.join("compatibilitytools.d")];
    let mut libs: Vec<PathBuf> = first.into_iter().map(Path::to_path_buf).collect();
    libs.extend(libraries(steam_root));
    for lib in libs {
        let common = lib.join("steamapps/common");
        if !homes.contains(&common) {
            homes.push(common);
        }
    }
    let valve = valve_folder_name(tool);
    let matches = |dir: &Path| {
        let folder = dir.file_name().map(|n| n.to_string_lossy().into_owned());
        let named = |want: &str| folder.as_deref().is_some_and(|f| f.eq_ignore_ascii_case(want));
        named(tool) || valve.as_deref().is_some_and(named) || manifest_names(dir).iter().any(|n| n == tool)
    };
    let mut found = None;
    for home in &homes {
        let Ok(rd) = std::fs::read_dir(home) else {
            continue;
        };
        let mut dirs: Vec<PathBuf> = rd.flatten().map(|e| e.path()).collect();
        dirs.sort();
        if let Some(hit) = dirs
            .into_iter()
            .find(|d| d.join("proton").is_file() && matches(d))
        {
            found = Some(hit);
            break;
        }
    }
    ProtonSearch {
        found,
        searched: homes,
    }
}

const STEAMID64_BASE: u64 = 76_561_197_960_265_728;

/// The game's Steam Cloud save folder, `userdata/<steamid3>/<app>/remote`: for the
/// most recent user in loginusers.vdf, else the user whose folder changed last.
pub fn save_dir(steam_root: &Path) -> Option<PathBuf> {
    let remote = |id3: &str| {
        let dir = steam_root
            .join("userdata")
            .join(id3)
            .join(STEAM_APP_ID.to_string())
            .join("remote");
        dir.is_dir().then_some(dir)
    };
    let recent = read_vdf(&steam_root.join("config/loginusers.vdf")).and_then(|v| {
        let users = v.get("users")?;
        let stamp = |e: &Vdf| {
            e.get("Timestamp")
                .and_then(Vdf::str)
                .and_then(|t| t.parse::<u64>().ok())
        };
        let best = users.entries().max_by_key(|(_, e)| {
            let most_recent = e.get("MostRecent").and_then(Vdf::str) == Some("1");
            (most_recent, stamp(e).unwrap_or(0))
        })?;
        let id64: u64 = best.0.parse().ok()?;
        remote(&id64.checked_sub(STEAMID64_BASE)?.to_string())
    });
    recent.or_else(|| {
        std::fs::read_dir(steam_root.join("userdata"))
            .ok()?
            .flatten()
            .filter_map(|u| {
                let dir = remote(u.file_name().to_str()?)?;
                let changed = std::fs::metadata(&dir).and_then(|m| m.modified()).ok();
                Some((changed, dir))
            })
            .max_by_key(|(changed, _)| *changed)
            .map(|(_, dir)| dir)
    })
}

/// save_dir for the Steam install that holds the game, then for every other one.
pub fn find_save_dir() -> Option<PathBuf> {
    let game_root = find_game().map(|g| g.steam_root);
    game_root
        .into_iter()
        .chain(steam_roots())
        .find_map(|root| save_dir(&root))
}

pub fn wrapper_installed(launch_options: &str) -> bool {
    launch_options.contains("swofty")
        && launch_options.contains("run")
        && launch_options.contains("%command%")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vdf_path(p: &Path) -> String {
        p.display().to_string().replace('\\', "\\\\")
    }

    fn proton_tree(tag: &str) -> PathBuf {
        let root = std::env::temp_dir().join(format!("swofty-proton-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(root.join("steamapps")).unwrap();
        std::fs::create_dir_all(root.join("config")).unwrap();
        root
    }

    fn put_proton(dir: &Path) {
        std::fs::create_dir_all(dir).unwrap();
        std::fs::write(dir.join("proton"), "").unwrap();
    }

    fn mapping(root: &Path, body: &str) {
        std::fs::write(
            root.join("config/config.vdf"),
            format!(
                "\"InstallConfigStore\" {{ \"Software\" {{ \"Valve\" {{ \"Steam\" {{ \"CompatToolMapping\" {{ {body} }} }} }} }} }}"
            ),
        )
        .unwrap();
    }

    #[test]
    fn compat_tool_falls_back_to_global_default() {
        let root = proton_tree("global");
        mapping(&root, "\"0\" { \"name\" \"proton_9\" }");
        assert_eq!(compat_tool(&root).as_deref(), Some("proton_9"));
        mapping(
            &root,
            "\"0\" { \"name\" \"proton_9\" } \"3059520\" { \"name\" \"GE-Proton9-1\" }",
        );
        assert_eq!(compat_tool(&root).as_deref(), Some("GE-Proton9-1"));
        mapping(&root, "\"3059520\" { \"name\" \"\" }");
        assert_eq!(compat_tool(&root), None);
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn finds_valve_proton_by_internal_name_in_main_library() {
        let root = proton_tree("valve");
        put_proton(&root.join("steamapps/common/Proton 9.0"));
        put_proton(&root.join("steamapps/common/Proton - Experimental"));
        let other = root.join("drive2");
        std::fs::create_dir_all(other.join("steamapps/common/F1 25")).unwrap();
        let hit = find_proton(&root, Some(&other), "proton_9");
        assert_eq!(hit.found, Some(root.join("steamapps/common/Proton 9.0")));
        let hit = find_proton(&root, Some(&other), "proton_experimental");
        assert_eq!(
            hit.found,
            Some(root.join("steamapps/common/Proton - Experimental"))
        );
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn finds_proton_in_listed_library_and_by_manifest_name() {
        let root = proton_tree("libs");
        let lib = std::env::temp_dir().join(format!("swofty-proton-libs2-{}", std::process::id()));
        put_proton(&lib.join("steamapps/common/Proton 10.0"));
        std::fs::write(
            root.join("steamapps/libraryfolders.vdf"),
            format!(
                "\"libraryfolders\" {{ \"1\" {{ \"path\" \"{}\" }} }}",
                vdf_path(&lib)
            ),
        )
        .unwrap();
        let custom = root.join("compatibilitytools.d/some-folder");
        put_proton(&custom);
        std::fs::write(
            custom.join("compatibilitytool.vdf"),
            "\"compatibilitytools\" { \"compat_tools\" { \"my-tool\" { \"install_path\" \".\" \"display_name\" \"My Tool\" } } }",
        )
        .unwrap();
        assert_eq!(
            find_proton(&root, None, "proton_10").found,
            Some(lib.join("steamapps/common/Proton 10.0"))
        );
        assert_eq!(find_proton(&root, None, "my-tool").found, Some(custom.clone()));
        assert_eq!(find_proton(&root, None, "My Tool").found, Some(custom));
        let miss = find_proton(&root, None, "nothing");
        assert!(miss.found.is_none());
        assert!(miss.searched.contains(&lib.join("steamapps/common")));
        let _ = std::fs::remove_dir_all(&root);
        let _ = std::fs::remove_dir_all(&lib);
    }

    #[test]
    fn exact_folder_name_still_resolves() {
        let root = proton_tree("exact");
        put_proton(&root.join("compatibilitytools.d/proton-cachyos-10.0-20260425-slr-x86_64"));
        assert_eq!(
            find_proton(&root, None, "proton-cachyos-10.0-20260425-slr-x86_64").found,
            Some(root.join("compatibilitytools.d/proton-cachyos-10.0-20260425-slr-x86_64"))
        );
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn finds_game_in_secondary_library() {
        let root = std::env::temp_dir().join(format!("swofty-steam-{}", std::process::id()));
        let lib = root.join("lib2");
        std::fs::create_dir_all(root.join("steamapps")).unwrap();
        std::fs::create_dir_all(lib.join("steamapps/common/F1 25")).unwrap();
        std::fs::create_dir_all(lib.join("steamapps/compatdata/3059520/pfx")).unwrap();
        std::fs::write(
            root.join("steamapps/libraryfolders.vdf"),
            format!(
                r#""libraryfolders" {{ "0" {{ "path" "{}" }} "1" {{ "path" "{}" }} }}"#,
                vdf_path(&root),
                vdf_path(&lib)
            ),
        )
        .unwrap();
        std::fs::write(
            lib.join("steamapps/appmanifest_3059520.acf"),
            r#""AppState" { "appid" "3059520" "installdir" "F1 25" "buildid" "25251167" }"#,
        )
        .unwrap();
        let g = find_game_in(&root).unwrap();
        assert_eq!(g.dir, lib.join("steamapps/common/F1 25"));
        assert_eq!(g.build_id, Some(25_251_167));
        assert_eq!(busy(&g), None);
        for (flags, state) in [
            ("4", None),
            ("6", None),
            ("131076", Some("verifying")),
            ("1030", Some("updating")),
        ] {
            std::fs::write(
                lib.join("steamapps/appmanifest_3059520.acf"),
                format!(r#""AppState" {{ "appid" "3059520" "installdir" "F1 25" "StateFlags" "{flags}" }}"#),
            )
            .unwrap();
            assert_eq!(busy(&g), state, "{flags}");
        }
        std::fs::write(
            lib.join("steamapps/appmanifest_3059520.acf"),
            r#""AppState" { "appid" "3059520" "installdir" "F1 25" "buildid" "25251167" }"#,
        )
        .unwrap();
        assert!(g.prefix().unwrap().ends_with("compatdata/3059520/pfx"));
        assert!(in_steam_library(&g.dir));
        assert!(!in_steam_library(&root.join("steamapps/common/F1 25")));
        assert!(!in_steam_library(&lib));
        assert!(!steamvr_installed(&root));
        std::fs::write(
            lib.join("steamapps/appmanifest_250820.acf"),
            r#""AppState" { "appid" "250820" "installdir" "SteamVR" }"#,
        )
        .unwrap();
        assert!(steamvr_installed(&root));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn finds_the_most_recent_users_save_folder() {
        let root = std::env::temp_dir().join(format!("swofty-saves-{}", std::process::id()));
        for id3 in ["11", "22"] {
            std::fs::create_dir_all(root.join(format!("userdata/{id3}/3059520/remote"))).unwrap();
        }
        std::fs::create_dir_all(root.join("config")).unwrap();
        std::fs::write(
            root.join("config/loginusers.vdf"),
            format!(
                r#""users" {{ "{}" {{ "AccountName" "a" "MostRecent" "0" "Timestamp" "900" }}
                   "{}" {{ "AccountName" "b" "MostRecent" "1" "Timestamp" "100" }} }}"#,
                STEAMID64_BASE + 11,
                STEAMID64_BASE + 22
            ),
        )
        .unwrap();
        assert_eq!(save_dir(&root), Some(root.join("userdata/22/3059520/remote")));
        std::fs::remove_file(root.join("config/loginusers.vdf")).unwrap();
        assert!(save_dir(&root).is_some_and(|d| d.ends_with("3059520/remote")));
        std::fs::remove_dir_all(root.join("userdata")).unwrap();
        assert_eq!(save_dir(&root), None);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn reads_what_steam_does_to_the_game_from_its_content_log() {
        let dir = std::env::temp_dir().join(format!("swofty-content-log-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let log = dir.join("content_log.txt");
        let at = |lines: &[&str]| {
            std::fs::write(&log, lines.join("\n") + "\n").unwrap();
            log_busy(&log)
        };
        let shader = [
            "[2026-10-24 12:48:43] AppID 3059520 Shader update changed : Running Update,Reconfiguring,",
            "[2026-10-24 12:48:45] AppID 3059520 state changed : Fully Installed,Update Queued,",
            "[2026-10-24 12:48:45] AppID 3059520 state changed : Fully Installed,",
        ];
        assert_eq!(at(&shader), None);
        let verify = [
            "[2026-10-24 15:23:18] Start validating appID 3059520 on BuildID 25251167",
            "[2026-10-24 15:23:18] AppID 3059520 state changed : Fully Installed,Update Queued,Update Running,",
            "[2026-10-24 15:23:18] AppID 228980 state changed : Fully Installed,Update Running,",
            "[2026-10-24 15:23:18] AppID 3059520 App update changed : Running Update,Verifying Installed,",
        ];
        assert_eq!(at(&verify), Some("verifying"));
        let mut done = verify.to_vec();
        done.push("[2026-10-24 15:31:02] AppID 3059520 App update changed : None");
        done.push("[2026-10-24 15:31:02] AppID 3059520 state changed : Fully Installed,");
        assert_eq!(at(&done), None);
        let mut update = done.clone();
        update.push("[2026-10-24 16:00:00] AppID 3059520 state changed : Fully Installed,Update Running,");
        update.push("[2026-10-24 16:00:01] AppID 3059520 App update changed : Running Update,Downloading,");
        assert_eq!(at(&update), Some("updating"));
        assert_eq!(log_busy(&dir.join("missing.txt")), None);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn detects_wrapper() {
        assert!(wrapper_installed(
            "~/.local/share/swofty/bin/swofty run -- %command%"
        ));
        assert!(!wrapper_installed("echo %command%"));
    }
}
