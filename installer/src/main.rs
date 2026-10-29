#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]

mod data;
mod existing;
mod running;
mod shortcut;
#[cfg(windows)]
mod win32;

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use eframe::egui::{self, Color32, CornerRadius, Margin, RichText, Stroke};
use swofty_core::app_control::Block;
use swofty_core::launcher::Progress;
use swofty_core::session::COMPAT_LAYER;
use swofty_core::update::{self, Install};

const FOLDER_NAME: &str = "SwoftyLauncher";
const FINDING: &str = "Finding the latest release";

const BACKGROUND: Color32 = Color32::from_rgb(0x18, 0x1b, 0x22);
const SURFACE: Color32 = Color32::from_rgb(0x1f, 0x23, 0x2d);
const BORDER: Color32 = Color32::from_rgb(0x3a, 0x3f, 0x4d);
const TEXT: Color32 = Color32::from_rgb(0xe9, 0xec, 0xf3);
const MUTED: Color32 = Color32::from_rgb(0xa3, 0xa8, 0xb7);
const ACCENT: Color32 = Color32::from_rgb(0x8b, 0x7f, 0xd6);
const ACCENT_HOVER: Color32 = Color32::from_rgb(0xa1, 0x96, 0xe6);
const ON_ACCENT: Color32 = Color32::from_rgb(0xf8, 0xf6, 0xff);
const ERROR: Color32 = Color32::from_rgb(0xf0, 0x6d, 0x78);
const HEIGHT: f32 = 340.0;

enum Stage {
    Ready,
    Open(PathBuf, String),
    Working { done: f32, text: String },
    Failed(String),
    Launched,
}

struct Reporter {
    stage: Arc<Mutex<Stage>>,
    ctx: egui::Context,
}

impl Reporter {
    fn set(&self, stage: Stage) {
        *self.stage.lock().unwrap() = stage;
        self.ctx.request_repaint();
    }
}

impl Progress for Reporter {
    fn report(&self, done: f32, text: &str) {
        self.set(Stage::Working {
            done,
            text: text.to_owned(),
        });
    }
}

struct Installer {
    folder: String,
    desktop: bool,
    existing: existing::Existing,
    data: data::Data,
    data_folder: String,
    height: f32,
    stage: Arc<Mutex<Stage>>,
}

impl Installer {
    fn new(cc: &eframe::CreationContext) -> Self {
        cc.egui_ctx.set_theme(egui::Theme::Dark);
        cc.egui_ctx.set_visuals_of(egui::Theme::Dark, visuals());
        let existing = existing::Existing::find();
        let folder = existing
            .root()
            .map_or_else(existing::default_root, Path::to_path_buf);
        let data = data::Data::find();
        Self {
            folder: folder.display().to_string(),
            desktop: true,
            existing,
            data_folder: data.current().display().to_string(),
            data,
            height: HEIGHT,
            stage: Arc::new(Mutex::new(Stage::Ready)),
        }
    }

    fn browse(&mut self) {
        if let Some(picked) = pick_folder("Install the Swofty Launcher in", &self.folder) {
            self.folder = install_folder(picked).display().to_string();
        }
    }

    fn browse_data(&mut self) {
        if let Some(picked) = pick_folder("Keep the Swofty data folder in", &self.data_folder) {
            self.data_folder = data::pick(picked, &self.data).display().to_string();
        }
    }

    fn data_move(&self) -> Result<Option<data::DataMove>, String> {
        if !self.data.movable() {
            return Ok(None);
        }
        resolve(&self.data_folder).and_then(|to| data::plan(&self.data, &to))
    }

    fn start(&mut self, ctx: &egui::Context) {
        let root = match resolve(&self.folder) {
            Ok(root) => root,
            Err(e) => {
                *self.stage.lock().unwrap() = Stage::Failed(e);
                return;
            }
        };
        if let Err(e) = self.existing.planned(&root) {
            *self.stage.lock().unwrap() = Stage::Failed(e);
            return;
        }
        self.folder = root.display().to_string();
        if let Some(why) = running::game_block() {
            *self.stage.lock().unwrap() = Stage::Failed(why.into());
            return;
        }
        let open = running::from(&root);
        match self.data_move() {
            Err(e) => {
                *self.stage.lock().unwrap() = Stage::Failed(e);
                return;
            }
            Ok(Some(_)) => {
                let elsewhere: Vec<running::Running> = running::anywhere()
                    .into_iter()
                    .filter(|r| !open.contains(r))
                    .collect();
                if let Some(why) = running::holding(&elsewhere, false) {
                    *self.stage.lock().unwrap() = Stage::Failed(why);
                    return;
                }
            }
            Ok(None) => {}
        }
        if !open.is_empty() {
            *self.stage.lock().unwrap() = Stage::Open(root, running::describe(&open));
            return;
        }
        self.run(root, false, ctx);
    }

    fn run(&mut self, root: PathBuf, close: bool, ctx: &egui::Context) {
        let moving = match self.existing.planned(&root) {
            Ok(moving) => moving,
            Err(e) => {
                *self.stage.lock().unwrap() = Stage::Failed(e);
                return;
            }
        };
        let data_move = match self.data_move() {
            Ok(data_move) => data_move,
            Err(e) => {
                *self.stage.lock().unwrap() = Stage::Failed(e);
                return;
            }
        };
        let reporter = Reporter {
            stage: self.stage.clone(),
            ctx: ctx.clone(),
        };
        reporter.report(0.0, if close { running::CLOSING } else { FINDING });
        let desktop = self.desktop;
        std::thread::spawn(move || {
            let data_env = data_move.as_ref().and_then(|m| m.env()).map(Path::to_path_buf);
            let stage = match running::clear(&root, close, &reporter)
                .and_then(|()| match &data_move {
                    Some(m) => m.run(&data::Ops::system(), &reporter),
                    None => Ok(()),
                })
                .and_then(|()| install(&root, desktop, moving.as_ref(), &reporter))
                .and_then(|exe| launch(&exe, data_env.as_deref()))
            {
                Ok(()) => Stage::Launched,
                Err(e) => Stage::Failed(e),
            };
            reporter.set(stage);
        });
    }
}

impl eframe::App for Installer {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        let (working, progress, error, open) = match &*self.stage.lock().unwrap() {
            Stage::Ready => (false, None, None, None),
            Stage::Open(root, why) => (true, None, None, Some((root.clone(), why.clone()))),
            Stage::Working { done, text } => (true, Some((*done, text.clone())), None, None),
            Stage::Failed(e) => (false, None, Some(e.clone()), None),
            Stage::Launched => {
                ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                (true, None, None, None)
            }
        };
        let bottom = egui::CentralPanel::default_margins()
            .frame(egui::Frame::new().fill(BACKGROUND).inner_margin(Margin::same(24)))
            .show(ui, |ui| {
                ui.label(RichText::new("Swofty Launcher").size(22.0).strong().color(TEXT));
                ui.label(RichText::new("Installs the latest release for F1 25 and opens it.").color(MUTED));
                ui.add_space(18.0);
                ui.add_enabled_ui(!working, |ui| {
                    self.existing.list(ui, &mut self.folder);
                    ui.label(RichText::new("Install to").color(MUTED));
                    ui.horizontal(|ui| {
                        let browse = 84.0;
                        let width = ui.available_width() - browse - ui.spacing().item_spacing.x;
                        ui.add_sized(
                            [width, 30.0],
                            egui::TextEdit::singleline(&mut self.folder).align(egui::Align2::LEFT_CENTER),
                        );
                        if ui
                            .add_sized([browse, 30.0], egui::Button::new("Browse…"))
                            .clicked()
                        {
                            self.browse();
                        }
                    });
                    ui.add_space(4.0);
                    ui.label(RichText::new(running::placement(&self.folder)).color(MUTED));
                    ui.add_space(6.0);
                    ui.checkbox(&mut self.desktop, "Add a desktop shortcut");
                    self.existing.status(ui, &self.folder);
                    ui.add_space(14.0);
                    ui.label(RichText::new("Keep addons, profiles and backups in").color(MUTED));
                    ui.add_enabled_ui(self.data.movable(), |ui| {
                        ui.horizontal(|ui| {
                            let browse = 84.0;
                            let width = ui.available_width() - browse - ui.spacing().item_spacing.x;
                            ui.add_sized(
                                [width, 30.0],
                                egui::TextEdit::singleline(&mut self.data_folder)
                                    .align(egui::Align2::LEFT_CENTER),
                            );
                            if ui
                                .add_sized([browse, 30.0], egui::Button::new("Browse…"))
                                .clicked()
                            {
                                self.browse_data();
                            }
                        });
                    });
                    ui.add_space(4.0);
                    match resolve(&self.data_folder).and_then(|to| self.data.describe(&to)) {
                        Ok(text) => ui.label(RichText::new(text).color(MUTED)),
                        Err(e) => ui.label(RichText::new(e).color(ERROR)),
                    };
                });
                ui.add_space(18.0);
                if let Some((root, why)) = open {
                    ui.label(RichText::new(why).color(TEXT));
                    ui.add_space(8.0);
                    ui.horizontal(|ui| {
                        let cancel = 84.0;
                        let width = ui.available_width() - cancel - ui.spacing().item_spacing.x;
                        let close = egui::Button::new(
                            RichText::new("Close Swofty Launcher and continue")
                                .size(15.0)
                                .color(ON_ACCENT),
                        )
                        .fill(ACCENT);
                        if ui.add_sized([width, 34.0], close).clicked() {
                            self.run(root, true, &ctx);
                        }
                        if ui
                            .add_sized([cancel, 34.0], egui::Button::new("Cancel"))
                            .clicked()
                        {
                            *self.stage.lock().unwrap() = Stage::Ready;
                        }
                    });
                } else if let Some((done, text)) = progress {
                    ui.add(
                        egui::ProgressBar::new(done)
                            .desired_height(30.0)
                            .corner_radius(6)
                            .fill(ACCENT)
                            .text(RichText::new(text).color(ON_ACCENT)),
                    );
                } else if !working {
                    let label = if error.is_some() { "Try again" } else { "Install" };
                    let button =
                        egui::Button::new(RichText::new(label).size(15.0).color(ON_ACCENT)).fill(ACCENT);
                    if ui.add_sized([ui.available_width(), 34.0], button).clicked() {
                        self.start(&ctx);
                    }
                }
                if let Some(error) = error {
                    ui.add_space(10.0);
                    ui.label(RichText::new(error).color(ERROR));
                }
                ui.min_rect().bottom()
            })
            .inner;
        let height = (bottom + 24.0).max(HEIGHT).round();
        if height != self.height {
            self.height = height;
            ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(egui::vec2(540.0, height)));
        }
    }
}

fn visuals() -> egui::Visuals {
    let mut v = egui::Visuals::dark();
    v.panel_fill = BACKGROUND;
    v.window_fill = BACKGROUND;
    v.extreme_bg_color = SURFACE;
    v.override_text_color = Some(TEXT);
    v.selection.bg_fill = ACCENT;
    v.selection.stroke = Stroke::new(1.0, ON_ACCENT);
    v.text_edit_bg_color = Some(SURFACE);
    for w in [
        &mut v.widgets.inactive,
        &mut v.widgets.hovered,
        &mut v.widgets.active,
        &mut v.widgets.open,
    ] {
        w.corner_radius = CornerRadius::same(6);
    }
    v.widgets.inactive.weak_bg_fill = SURFACE;
    v.widgets.inactive.bg_stroke = Stroke::new(1.0, BORDER);
    v.widgets.hovered.bg_stroke = Stroke::new(1.0, ACCENT_HOVER);
    v.widgets.active.bg_stroke = Stroke::new(1.0, ACCENT);
    v
}

fn pick_folder(title: &str, current: &str) -> Option<PathBuf> {
    let start = PathBuf::from(current)
        .ancestors()
        .find(|p| p.is_dir())
        .map(Path::to_path_buf);
    let mut dialog = rfd::FileDialog::new().set_title(title);
    if let Some(start) = start {
        dialog = dialog.set_directory(start);
    }
    dialog.pick_folder()
}

fn install_folder(picked: PathBuf) -> PathBuf {
    let empty = std::fs::read_dir(&picked).is_ok_and(|mut d| d.next().is_none());
    let installed = picked.join("bin").join(update::exe("swofty-launcher")).is_file();
    let named = picked
        .file_name()
        .is_some_and(|n| n.eq_ignore_ascii_case(FOLDER_NAME));
    if empty || installed || named {
        picked
    } else {
        picked.join(FOLDER_NAME)
    }
}

fn resolve(folder: &str) -> Result<PathBuf, String> {
    let folder = folder.trim();
    if folder.is_empty() {
        return Err("Choose a folder to install in.".into());
    }
    let path = match folder.strip_prefix("~") {
        Some(rest) if rest.is_empty() || rest.starts_with(['/', '\\']) => dirs::home_dir()
            .ok_or("Could not find your home folder.")?
            .join(rest.trim_start_matches(['/', '\\'])),
        _ => PathBuf::from(folder),
    };
    std::path::absolute(&path).map_err(|e| format!("{}: {e}", path.display()))
}

fn install(
    root: &Path,
    desktop: bool,
    moving: Option<&existing::Move>,
    progress: &Reporter,
) -> Result<PathBuf, String> {
    let release = update::fetch_latest(Duration::from_secs(30))
        .map_err(|e| format!("Could not find the latest release: {e}"))?;
    if let Some(moving) = moving {
        progress.report(0.0, "Moving your install");
        moving.run(desktop)?;
    }
    std::fs::create_dir_all(root).map_err(|e| format!("Could not create {}: {e}", root.display()))?;
    let install = Install {
        root: root.to_path_buf(),
    };
    install.update(&release, progress).map_err(|e| e.to_string())?;
    let exe = install.launcher();
    progress.report(1.0, "Adding shortcuts");
    shortcut::create(root, &exe, desktop);
    Ok(exe)
}

/// Opens the installed launcher as the signed-in user. An elevated installer
/// hands it to the desktop's shell, so the launcher doesn't inherit the
/// administrator rights; started directly, it gets none of the installer's
/// compatibility layers.
fn launch(exe: &Path, data_env: Option<&Path>) -> Result<(), String> {
    #[cfg(windows)]
    if let Some(root) = exe.parent().and_then(Path::parent)
        && swofty_core::compat::elevated()
        && win32::shell_open(exe, root).is_ok()
    {
        return Ok(());
    }
    command(exe, data_env)
        .spawn()
        .map(drop)
        .map_err(|e| match Block::of(exe, &e) {
            Some(b) => format!("Installed, but the launcher would not start. {}", b.text()),
            None => format!(
                "Installed, but the launcher would not start ({}): {e}",
                exe.display()
            ),
        })
}

fn command(exe: &Path, data_env: Option<&Path>) -> Command {
    let mut command = Command::new(exe);
    if let Some(root) = exe.parent().and_then(Path::parent) {
        command.current_dir(root);
    }
    if let Some(data) = data_env {
        command.env(data::ENV, data);
    }
    command
        .env_remove(COMPAT_LAYER)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    command
}

fn main() -> eframe::Result {
    let icon = eframe::icon_data::from_png_bytes(include_bytes!("../../apps/launcher/assets/icon-256.png"))
        .expect("the bundled icon is a png");
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("Swofty Launcher Installer")
            .with_app_id("net.swofty.ModLauncher.Installer")
            .with_inner_size([540.0, HEIGHT])
            .with_resizable(false)
            .with_icon(icon),
        centered: true,
        ..Default::default()
    };
    eframe::run_native(
        "Swofty Launcher Installer",
        options,
        Box::new(|cc| Ok(Box::new(Installer::new(cc)))),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn starts_the_launcher_in_its_folder_without_the_compatibility_layers() {
        let root = Path::new("SwoftyLauncher");
        let c = command(&root.join("bin").join(update::exe("swofty-launcher")), None);
        assert_eq!(c.get_current_dir(), Some(root));
        assert!(c.get_envs().any(|(k, v)| k == COMPAT_LAYER && v.is_none()));
    }

    #[cfg(windows)]
    #[test]
    fn asks_windows_to_run_it_with_the_rights_it_was_started_with() {
        let manifest = win32::manifest().expect("an embedded application manifest");
        assert!(manifest.contains(r#"level="asInvoker""#), "{manifest}");
    }
}
