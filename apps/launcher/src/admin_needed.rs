use std::path::Path;
use std::time::Duration;

use gpui_kit::component::WindowExt;
use gpui_kit::component::notification::Notification;
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::compat;
use swofty_core::install::Source;

use crate::app_log;
use crate::game::Install;
use crate::shell::Shell;

const TITLE: &str = "F1 25 needs administrator rights";

/// Where the player moves the game, by the store it came from.
fn move_hint(source: Source) -> &'static str {
    match source {
        Source::EaApp => " in the EA app",
        Source::Steam => " in Steam (Properties > Installed Files > Move install folder)",
        Source::Unknown => "",
    }
}

pub(crate) fn text(dir: &Path, source: Source, windows: bool) -> String {
    if !windows {
        return format!("{}.", compat::write_blocked_text_for(dir, false));
    }
    format!(
        "F1 25 is installed in {}, a folder Windows lets only administrators change, so Swofty can't \
         add your mods there.\n\nRestart Swofty Launcher as administrator to play with mods, or move \
         F1 25 to a folder outside Program Files{}.",
        dir.display(),
        move_hint(source)
    )
}

impl Shell {
    /// The game folder is one this launcher can't write in: say so and offer
    /// to restart as administrator, on Windows.
    pub(crate) fn show_admin_needed(
        &mut self,
        install: &Install,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let windows = cfg!(windows);
        let text = text(&install.dir, install.source, windows);
        window.open_alert_dialog(cx, move |alert, _, _| {
            alert
                .width(px(560.))
                .title(TITLE)
                .description(text.clone())
                .map(|a| {
                    if !windows {
                        return a.ok_text("OK");
                    }
                    a.ok_text("Restart as administrator")
                        .show_cancel(true)
                        .on_ok(|_, window, cx| {
                            restart_elevated(window, cx);
                            true
                        })
                })
        });
    }
}

/// How long the administrator copy has to keep running before this one closes.
const ELEVATED_SETTLE: Duration = Duration::from_secs(6);

fn not_restarted(text: String, window: &mut Window, cx: &mut App) {
    window.push_notification(
        Notification::warning(text)
            .title("Not restarted as administrator")
            .autohide(false),
        cx,
    );
}

/// Starts the administrator copy and closes this one once it is still
/// running a few seconds later. One that closes before then is reported.
fn restart_elevated(window: &mut Window, cx: &mut App) {
    app_log::note("restart as administrator: asking Windows");
    let started = cx.background_executor().spawn(async move {
        std::thread::spawn(compat::relaunch_elevated)
            .join()
            .unwrap_or_else(|_| Err("the administrator prompt failed".to_owned()))
    });
    window
        .spawn(cx, async move |cx| {
            let started = started.await;
            cx.update(|window, cx| elevated_started(started, window, cx)).ok();
        })
        .detach();
}

fn elevated_started(started: Result<compat::Elevated, String>, window: &mut Window, cx: &mut App) {
    let elevated = match started {
        Ok(e) => e,
        Err(why) => {
            app_log::note(&format!("restart as administrator: {why}"));
            return not_restarted(
                format!(
                    "Windows didn't restart Swofty Launcher: {why}. It keeps running without administrator \
                     rights."
                ),
                window,
                cx,
            );
        }
    };
    app_log::note(&format!(
        "restart as administrator: started the administrator copy{}",
        elevated
            .pid()
            .map(|pid| format!(", process {pid}"))
            .unwrap_or_default()
    ));
    window.minimize_window();
    let wait = cx
        .background_executor()
        .spawn(async move { elevated.exited_within(ELEVATED_SETTLE) });
    window
        .spawn(cx, async move |cx| {
            let exited = wait.await;
            cx.update(|window, cx| match exited {
                None => {
                    app_log::note("restart as administrator: the administrator copy is running; closing");
                    cx.quit();
                }
                Some(code) => {
                    app_log::note(&format!(
                        "restart as administrator: the administrator copy closed with exit code {code:#x}"
                    ));
                    window.activate_window();
                    not_restarted(
                        format!(
                            "The administrator copy of Swofty Launcher closed as it started (exit code \
                             {code:#x}). This one keeps running without administrator rights. Press {} so \
                             the developers can see why.",
                            crate::send_logs::label()
                        ),
                        window,
                        cx,
                    );
                }
            })
            .ok();
        })
        .detach();
}

#[cfg(test)]
mod tests {
    use super::text;
    use std::path::Path;
    use swofty_core::install::Source;

    #[test]
    fn names_the_folder_and_both_ways_out() {
        let ea = text(Path::new(r"C:\Program Files\EA Games\F1 25"), Source::EaApp, true);
        assert!(
            ea.starts_with(r"F1 25 is installed in C:\Program Files\EA Games\F1 25, a folder"),
            "{ea}"
        );
        assert!(ea.contains("Restart Swofty Launcher as administrator"), "{ea}");
        assert!(ea.ends_with("outside Program Files in the EA app."), "{ea}");
        let steam = text(
            Path::new(r"C:\Program Files\Steam\steamapps\common\F1 25"),
            Source::Steam,
            true,
        );
        assert!(steam.contains("Move install folder"), "{steam}");
        assert!(!steam.contains("EA app"), "{steam}");
        let linux = text(Path::new("/games/F1 25"), Source::Steam, false);
        assert!(
            linux.contains("/games/F1 25") && !linux.contains("administrator"),
            "{linux}"
        );
    }
}
