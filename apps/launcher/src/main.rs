#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]

mod addons;
mod admin_needed;
mod app_log;
mod blocks;
mod browser;
mod colour;
mod console;
mod crash_modal;
mod db;
mod discord;
mod ea_app;
mod game;
mod game_edited;
mod importer;
mod library;
mod multiplayer;
mod news;
mod overlayfs_answer;
mod overlayfs_dialog;
mod pages;
mod play_bar;
mod reveal;
mod scroll;
mod sections;
mod send_logs;
mod setup;
mod shell;
mod tasks;
mod theme;
mod tools;
mod update;

use gpui_kit::component::{TitleBar, v_flex};
use gpui_kit::*;

fn main() {
    // The wrapper's fallback question is its own small window, so it works
    // while the launcher is open.
    let args: Vec<String> = std::env::args().collect();
    if let Some((kind, reason, data)) = overlayfs_answer::requested(&args) {
        overlayfs_dialog::run(kind, reason, data);
    }
    app_log::start();
    let app = gpui_kit::application().with_assets(gpui_kit::assets::AllAssets);
    let prepared = app.background_executor().spawn(async { shell::Prepared::load() });
    app.run(move |cx| {
        gpui_kit::init(cx);
        theme::init(cx);
        let options = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds::centered(
                None,
                size(px(1440.), px(900.)),
                cx,
            ))),
            window_min_size: Some(size(px(1024.), px(680.))),
            app_id: Some("net.swofty.ModLauncher".into()),
            titlebar: Some(TitlebarOptions {
                title: Some("Swofty Launcher for F1 25".into()),
                ..TitleBar::title_bar_options()
            }),
            ..TitleBar::window_options()
        };
        let (main, _) = gpui_kit::open_window(options, cx, |window, cx| {
            cx.new(|cx| Launcher::new(prepared, window, cx))
        })
        .expect("failed to open window");
        cx.on_window_closed(move |cx, id| {
            if id == main.window_id() {
                cx.quit();
            }
        })
        .detach();
        cx.activate(true);
    });
}

struct Launcher {
    shell: Option<Entity<shell::Shell>>,
}

impl Launcher {
    fn new(prepared: Task<shell::Prepared>, window: &mut Window, cx: &mut Context<Self>) -> Self {
        cx.spawn_in(window, async move |this, cx| {
            let prepared = prepared.await;
            this.update_in(cx, |this, window, cx| {
                let shell = cx.new(|cx| shell::Shell::new(prepared, window, cx));
                shell.update(cx, |s, cx| {
                    s.restore_console(cx);
                    s.run_startup(window, cx);
                });
                this.shell = Some(shell);
                cx.notify();
            })
            .ok();
        })
        .detach();
        Self { shell: None }
    }
}

impl Render for Launcher {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        if let Some(shell) = &self.shell {
            return shell.clone().into_any_element();
        }
        let p = theme::palette(cx);
        v_flex()
            .size_full()
            .bg(p.background)
            .text_color(p.text_muted)
            .child(TitleBar::new().bg(p.chrome))
            .child(
                v_flex()
                    .flex_1()
                    .items_center()
                    .justify_center()
                    .gap(px(20.))
                    .child(pages::wordmark(220.))
                    .child("Loading your launcher…"),
            )
            .into_any_element()
    }
}
