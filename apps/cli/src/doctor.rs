use std::process::ExitCode;

use anyhow::Result;
use swofty_core::copies;
use swofty_core::ea_app;
use swofty_core::install::{Game, Installer, Source};
use swofty_core::store::Store;
use swofty_core::{STEAM_APP_ID, steam};

pub fn doctor(store: &Store) -> Result<ExitCode> {
    let (lines, ok) = report(store, None);
    for l in lines {
        println!("{l}");
    }
    Ok(if ok { ExitCode::SUCCESS } else { ExitCode::FAILURE })
}

/// The doctor's rows. With `session`, the game folder checked is that
/// session's, not the one Steam reports.
pub fn report(store: &Store, session: Option<&Game>) -> (Vec<String>, bool) {
    let mut lines = Vec::new();
    let mut row = |k: &str, v: String| lines.push(format!("{k:18} {v}"));
    row("data dir", store.root.display().to_string());
    row(
        "addons",
        format!(
            "{} installed (including swofty.core)",
            store.addons().map(|m| m.len()).unwrap_or(0)
        ),
    );
    let config = store.config();
    let found = copies::discover(&config);
    let active = copies::active(&found, &config).cloned();
    if found.is_empty() {
        row("game copies", "none found (swofty game add <folder>)".into());
    }
    for c in &found {
        let key = if active.as_ref().is_some_and(|a| a.dir == c.dir) {
            "copy (active)"
        } else {
            "copy"
        };
        let version = c.version().unwrap_or_else(|| "unknown version".into());
        row(
            key,
            format!(
                "{} · {version} · {}",
                copies::store_name(c.source),
                c.dir.display()
            ),
        );
    }
    if active.as_ref().is_some_and(|a| a.source == Source::EaApp) {
        let app = if ea_app::running() { "running" } else { "closed" };
        let handoff = ea_app::handoff(config.launch.ea_launch_command.as_deref())
            .map_or_else(|| "not found".to_owned(), |h| h.to_string());
        row("ea app", app.into());
        row("ea app hand-off", handoff);
        if let Some(p) = &config.launch.ea_prefix {
            row("ea app prefix", p.display().to_string());
            let reg = ea_app::prefix_user_reg(p).and_then(|r| std::fs::read_to_string(r).ok());
            row(
                "ea app dll override",
                match reg {
                    Some(t) if swofty_core::install::has_override(&t) => "present".into(),
                    Some(_) => "absent".into(),
                    None => "no user.reg in the prefix".into(),
                },
            );
        }
    }
    if let Some(g) = session {
        row("game dir", g.dir.display().to_string());
        row("build id", g.identity.build_id.clone());
        match Installer::new(&store.root, g).verify() {
            Ok(d) if d.is_empty() => row("baseline", "no drift".into()),
            Ok(d) => d.iter().for_each(|d| row("drift", d.to_string())),
            Err(e) => row("baseline", format!("not checked: {e}")),
        }
    }
    for p in swofty_core::damaged_state(&store.root) {
        row(
            "damaged state",
            format!(
                "{} was unreadable (a crash or power loss while it was written) and was set aside; \
                 swofty carried on without it, and the copy is safe to delete",
                p.display()
            ),
        );
    }
    let roots = steam::steam_roots();
    if roots.is_empty() {
        row("steam", "not found".into());
        return (lines, session.is_some());
    }
    for r in &roots {
        row("steam root", r.display().to_string());
        for lib in steam::libraries(r) {
            row("  library", lib.display().to_string());
        }
    }
    if session.is_none() {
        row("steam client", swofty_core::steam_client::status().to_string());
    }
    let Some(game) = roots.iter().find_map(|r| steam::find_game_in(r)) else {
        row(
            "F1 25",
            format!("app {STEAM_APP_ID} not installed in any library"),
        );
        return (lines, session.is_some());
    };
    if session.is_none() {
        row("game dir", game.dir.display().to_string());
        row(
            "build id",
            game.build_id.map_or("unknown".into(), |b| b.to_string()),
        );
        row(
            "exe",
            if game.exe().is_file() {
                "present".into()
            } else {
                "MISSING".into()
            },
        );
        match crate::build::game(store, Some(game.dir.clone()))
            .and_then(|g| Ok(Installer::new(&store.root, &g).verify()?))
        {
            Ok(d) if d.is_empty() => row("baseline", "no drift".into()),
            Ok(d) => d.iter().for_each(|d| row("drift", d.to_string())),
            Err(e) => row("baseline", format!("not checked: {e:#}")),
        }
    }
    if cfg!(windows) {
        let dir = session.map_or(&game.dir, |g| &g.dir);
        let found = swofty_core::compat::session_admin_layers(dir);
        if found.is_empty() {
            row("run as admin", "not set".into());
        }
        for l in &found {
            row(
                "run as admin",
                format!(
                    "{} is set to run as administrator {}: modded sessions start only when the launcher \
                     runs as administrator; untick it on the exe's Properties > Compatibility tab",
                    l.name,
                    l.scope()
                ),
            );
        }
    }
    if !cfg!(windows) {
        row(
            "compat tool",
            steam::compat_tool(&game.steam_root).unwrap_or_else(|| "default".into()),
        );
        match game.prefix() {
            Some(pfx) => {
                let reg = std::fs::read_to_string(pfx.join("user.reg")).unwrap_or_default();
                row("prefix", pfx.display().to_string());
                row(
                    "dll override",
                    if swofty_core::install::has_override(&reg) {
                        "present".into()
                    } else {
                        "absent".into()
                    },
                );
            }
            None => row("prefix", "not created yet (launch the game once)".into()),
        }
        let opts = steam::launch_options(&game.steam_root);
        if opts.is_empty() {
            row("launch option", "none".into());
        }
        for (user, o) in opts {
            let state = if steam::wrapper_installed(&o) {
                "wrapper"
            } else {
                "no wrapper"
            };
            row("launch option", format!("user {user} ({state}): {o}"));
            if cfg!(target_os = "linux") && steam::wrapper_installed(&o) {
                row(
                    "overlayfs option",
                    if o.contains("--use-overlayfs") {
                        "--use-overlayfs is in the launch option".into()
                    } else {
                        "off (add --use-overlayfs after `run` to use it)".into()
                    },
                );
            }
        }
    }
    if cfg!(target_os = "linux") {
        row(
            "overlayfs mount",
            match crate::overlay::doctor_check(store) {
                Ok(()) => "works: an unprivileged OverlayFS mount succeeded".into(),
                Err(why) => format!("fails ({why}); --use-overlayfs would fall back to copying"),
            },
        );
    }
    (lines, true)
}
