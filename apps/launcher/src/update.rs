use std::path::Path;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use gpui_kit::assets::IconName;
use gpui_kit::component::notification::Notification;
use gpui_kit::component::spinner::Spinner;
use gpui_kit::component::tooltip::Tooltip;
use gpui_kit::component::{Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use semver::Version;
use swofty_core::app_control::Block;
use swofty_core::managed;
use swofty_core::update::{self, Install, Release};
use swofty_core::updater::{self, Catalog, Gate, Part, Phase, Plan, Ran, Track};

use crate::play_bar::link;
use crate::sections::{Reporter, Shared};
use crate::shell::{PlayState, Shell};
use crate::theme::Palette;

const TICK: Duration = Duration::from_secs(5);
const RESTART_DELAY: Duration = Duration::from_millis(1500);
pub(crate) const BUILDING: &str =
    "Swofty is still building your mods. Update when it finishes, or cancel the build.";
pub const BUTTON_WIDTH: f32 = 204.;

pub struct Updates {
    pub copy: Result<Install, String>,
    pub launcher: Track,
    pub addon: Catalog,
    pub addon_installed: Option<Version>,
    pub phase: Phase,
    pub checking: bool,
    pub adding: bool,
    pub progress: Option<(f32, String)>,
    pub restarting: Vec<(Part, Version)>,
    pub restarts: bool,
    pub expected: Option<String>,
    pub held: Option<String>,
    pub followed_up: bool,
}

impl Updates {
    pub fn new() -> Self {
        let copy = Install::current();
        if let Ok(i) = &copy
            && i.staging().exists()
        {
            let i = i.clone();
            std::thread::spawn(move || {
                for _ in 0..3 {
                    if i.cleanup() {
                        break;
                    }
                    std::thread::sleep(Duration::from_secs(5));
                }
            });
        }
        Self {
            copy,
            launcher: Track::default(),
            addon: Catalog::default(),
            addon_installed: None,
            phase: Phase::Idle,
            checking: false,
            adding: false,
            progress: None,
            restarting: Vec::new(),
            restarts: true,
            expected: std::env::var(updater::UPDATED_ENV).ok().filter(|v| !v.is_empty()),
            held: None,
            followed_up: false,
        }
    }

    fn launcher_offer(&self) -> Option<&Release> {
        self.copy
            .is_ok()
            .then(|| self.launcher.offer(Some(&update::current_version())))
            .flatten()
    }

    pub fn plan(&self) -> Plan {
        updater::plan(
            &self.addon,
            self.addon_installed.as_ref(),
            self.launcher_offer(),
            &update::current_version(),
            self.held.as_deref(),
        )
    }

    pub fn missing_addon(&self) -> Option<&Release> {
        self.addon.choose(&update::current_version(), None)
    }

    fn waiting_notice(&self) -> Option<String> {
        let target = self
            .launcher_offer()
            .and_then(Release::version)
            .unwrap_or_else(update::current_version);
        let c = self.addon.waiting(&target, self.addon_installed.as_ref())?;
        Some(format!(
            "{} {} needs Swofty Launcher {} and installs once that launcher is out. You have {}.",
            managed::NAME,
            c.version,
            c.needs.as_ref()?,
            update::current_version()
        ))
    }

    fn blocked_notice(&self) -> Option<(Version, String, &str)> {
        let reason = self.copy.as_ref().err()?;
        let version = self.launcher.version()?;
        (version > update::current_version()).then(|| {
            (
                version,
                self.launcher.tag.clone().unwrap_or_default(),
                reason.as_str(),
            )
        })
    }
}

fn log_change(part: Part, before: &Option<String>, after: &Option<String>) {
    if before != after
        && let Some(e) = after
    {
        eprintln!("swofty-launcher: {} update check: {e}", part.name());
    }
}

pub(crate) fn sentence(text: &str) -> String {
    let mut chars = text.chars();
    match chars.next() {
        Some(c) => c.to_uppercase().chain(chars).collect(),
        None => String::new(),
    }
}

impl Shell {
    pub(crate) fn watch_updates(cx: &mut Context<Self>) -> Task<()> {
        cx.spawn(async move |this, cx| {
            loop {
                if this.update(cx, |s, cx| s.tick_updates(cx)).is_err() {
                    break;
                }
                cx.background_executor().timer(TICK).await;
            }
        })
    }

    fn tick_updates(&mut self, cx: &mut Context<Self>) {
        let now = update::now_secs();
        let check_launcher = self.store.config().updates.check;
        let u = &self.updates;
        let addon_due = u.addon.probe_due(now);
        let launcher_due = check_launcher && u.launcher.probe_due(now);
        if u.checking
            || matches!(u.phase, Phase::Installing | Phase::Restarting)
            || !(addon_due || launcher_due)
        {
            return;
        }
        self.updates.checking = true;
        let (mut addon, mut launcher) = (self.updates.addon.clone(), self.updates.launcher.clone());
        let can_install = self.updates.copy.is_ok();
        let store = self.store.clone();
        cx.spawn(async move |this, cx| {
            let (addon, launcher, installed) = cx
                .background_executor()
                .spawn(async move {
                    let installed = managed::installed(&store);
                    let current = update::current_version();
                    if launcher_due {
                        updater::check(Part::Launcher, &mut launcher, Some(&current), can_install, now);
                    }
                    if addon_due {
                        updater::check_importer(&store, &mut addon, installed.as_ref(), &current, now);
                    }
                    (addon, launcher, installed)
                })
                .await;
            this.update(cx, |s, cx| s.checked(addon, launcher, installed, cx))
                .ok();
        })
        .detach();
    }

    fn checked(
        &mut self,
        addon: Catalog,
        launcher: Track,
        installed: Option<Version>,
        cx: &mut Context<Self>,
    ) {
        let u = &mut self.updates;
        log_change(Part::Addon, &u.addon.latest.error, &addon.latest.error);
        log_change(Part::Addon, &u.addon.error, &addon.error);
        log_change(Part::Launcher, &u.launcher.error, &launcher.error);
        (u.addon, u.launcher, u.addon_installed, u.checking) = (addon, launcher, installed, false);
        if u.phase == Phase::Idle
            && let Some(offer) = u.launcher_offer()
            && u.expected.as_deref() == Some(offer.tag_name.as_str())
            && let Some(text) = updater::did_not_take(u.expected.as_deref(), &update::current_version())
        {
            u.phase = Phase::Failed(text);
        }
        if u.addon_installed.is_none()
            && !u.adding
            && u.phase == Phase::Idle
            && let Some(release) = u.missing_addon().cloned()
        {
            self.add_missing_addon(release, cx);
        }
        let plan = self.updates.plan();
        if !self.updates.followed_up
            && updater::follow_up(self.updates.expected.is_some(), &plan)
            && self.update_gate(&self.play_state()) == Gate::Update
        {
            self.updates.followed_up = true;
            let task = self.run_plan(plan, false, cx);
            cx.spawn(async move |this, cx| {
                let ran = task.await;
                this.update(cx, |s, cx| s.finish_updates(ran, cx)).ok();
            })
            .detach();
        }
        cx.notify();
    }

    fn add_missing_addon(&mut self, release: Release, cx: &mut Context<Self>) {
        self.updates.adding = true;
        let store = self.store.clone();
        cx.spawn(async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move {
                    let launcher = update::current_version();
                    managed::install_release(&store, &release, &launcher, &Reporter(Shared::default()))
                })
                .await;
            this.update(cx, |s, cx| {
                s.updates.adding = false;
                match result {
                    Ok(version) => {
                        eprintln!("swofty-launcher: installed {} {version}", managed::ID);
                        s.updates.addon_installed = Some(version);
                        s.reload(cx);
                    }
                    Err(e) => eprintln!("swofty-launcher: could not install {}: {e}", managed::ID),
                }
            })
            .ok();
        })
        .detach();
    }

    pub(crate) fn update_gate(&self, state: &PlayState) -> Gate {
        let busy = matches!(
            state,
            PlayState::Running
                | PlayState::Elsewhere(_)
                | PlayState::Launching
                | PlayState::Restoring
                | PlayState::Detecting
                | PlayState::Checking
                | PlayState::Building
        );
        updater::gate(&self.updates.phase, !self.updates.plan().is_empty(), busy)
    }

    fn start_updates(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if matches!(self.play_state(), PlayState::Building) {
            window.push_notification(Notification::error(BUILDING), cx);
            return;
        }
        if matches!(
            self.play_state(),
            PlayState::Running | PlayState::Elsewhere(_) | PlayState::Launching
        ) {
            window.push_notification(Notification::error("Close F1 25 before updating."), cx);
            return;
        }
        let plan = self.updates.plan();
        if plan.is_empty() {
            return;
        }
        let task = self.run_plan(plan, true, cx);
        cx.spawn_in(window, async move |this, cx| {
            let ran = task.await;
            this.update_in(cx, |s, window, cx| {
                if let Some(e) = s.finish_updates(ran, cx) {
                    crate::app_log::action("update").failed(&e);
                    window.push_notification(Notification::error(e).title("Update failed"), cx);
                }
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    fn run_plan(&mut self, plan: Plan, restarts: bool, cx: &mut Context<Self>) -> Task<Ran> {
        self.updates.phase = Phase::Installing;
        self.updates.restarts = restarts || plan.launcher.is_some();
        let shared: Shared = Arc::new(Mutex::new(Some((0., "Starting the update".into()))));
        let reporter = Reporter(shared.clone());
        self.follow_update_progress(shared, cx);
        let store = self.store.clone();
        let copy = self.updates.copy.clone().ok();
        cx.background_executor().spawn(async move {
            let current = update::current_version();
            let ran = updater::run(&plan, &current, &reporter, |part, release, launcher, progress| {
                updater::install(&store, copy.as_ref(), part, release, launcher, progress)
            });
            if let Ok(mut p) = reporter.0.lock() {
                *p = None;
            }
            ran
        })
    }

    fn finish_updates(&mut self, ran: Ran, cx: &mut Context<Self>) -> Option<String> {
        let u = &mut self.updates;
        u.progress = None;
        if let Some(version) = ran.addon() {
            u.addon_installed = Some(version.clone());
        }
        if let Some(tag) = ran.failed_tag(Part::Addon) {
            u.held = Some(tag.to_owned());
        }
        let error = ran.error();
        if ran.launcher().is_some() || (u.restarts && ran.restart()) {
            u.phase = Phase::Restarting;
            u.restarting = ran.done.clone();
            let launcher = ran.launcher().map(|v| format!("v{v}"));
            cx.spawn(async move |this, cx| {
                cx.background_executor().timer(RESTART_DELAY).await;
                this.update(cx, |s, cx| s.restart(launcher, cx)).ok();
            })
            .detach();
            cx.notify();
            return None;
        }
        u.phase = match &error {
            Some(e) => Phase::Failed(format!("Update failed: {e}")),
            None => Phase::Idle,
        };
        if ran.addon().is_some() {
            self.reload(cx);
        }
        cx.notify();
        error
    }

    fn retry_addon(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.updates.held = None;
        self.updates.phase = Phase::Idle;
        self.start_updates(window, cx);
    }

    fn follow_update_progress(&mut self, shared: Shared, cx: &mut Context<Self>) {
        self.updates.progress = shared.lock().ok().and_then(|p| p.clone());
        cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(Duration::from_millis(100)).await;
                let now = shared.lock().ok().and_then(|p| p.clone());
                let done = now.is_none();
                if this
                    .update(cx, |s, cx| {
                        if now.is_some() {
                            s.updates.progress = now;
                        }
                        cx.notify();
                    })
                    .is_err()
                    || done
                {
                    break;
                }
            }
        })
        .detach();
    }

    fn restart(&mut self, launcher: Option<String>, cx: &mut Context<Self>) {
        let exe = match &self.updates.copy {
            Ok(install) => Ok(install.launcher()),
            Err(_) => std::env::current_exe(),
        };
        let mut command = match exe {
            Ok(exe) => std::process::Command::new(exe),
            Err(e) => {
                self.updates.phase = Phase::Failed(format!(
                    "Updated, but could not restart: {e}. Close and reopen the launcher."
                ));
                cx.notify();
                return;
            }
        };
        command.env_remove(update::PRETEND_ENV);
        match &launcher {
            Some(tag) => command.env(updater::UPDATED_ENV, tag),
            None => command.env_remove(updater::UPDATED_ENV),
        };
        match command.spawn() {
            Ok(_) => cx.quit(),
            Err(e) => {
                self.updates.phase = Phase::Failed(match Block::of(Path::new(command.get_program()), &e) {
                    Some(b) => format!("Updated, but could not restart: {}", b.text()),
                    None => format!("Updated, but could not restart: {e}. Close and reopen the launcher."),
                });
                cx.notify();
            }
        }
    }

    pub(crate) fn update_status(
        &self,
        gate: Gate,
        row: Div,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> Option<Div> {
        let dot = |c: Hsla| div().size(px(7.)).rounded_full().bg(c).flex_shrink_0();
        let text = |t: String| div().min_w_0().truncate().child(t);
        let u = &self.updates;
        match gate {
            Gate::Update => {
                let plan = u.plan();
                let changes: Vec<String> = plan
                    .versions()
                    .iter()
                    .map(|(part, v)| {
                        let from = match part {
                            Part::Addon => u.addon_installed.as_ref().map(ToString::to_string),
                            Part::Launcher => Some(update::current_version().to_string()),
                        };
                        match from {
                            Some(from) => format!("{} {from} → {v}", part.name()),
                            None => format!("{} {v}", part.name()),
                        }
                    })
                    .collect();
                let notes = plan.steps().first().map(|(_, r)| r.notes_url());
                Some(
                    row.child(dot(p.accent))
                        .child(text(format!(
                            "Update required: {}. The launcher restarts afterwards.",
                            changes.join(" and ")
                        )))
                        .when_some(notes, |d, notes| {
                            d.child(
                                link("update-notes", "Release notes", p)
                                    .flex_shrink_0()
                                    .on_click(move |_, _, cx| cx.open_url(&notes)),
                            )
                        }),
                )
            }
            Gate::Installing => Some(row.child(dot(p.warning)).child(text(match u.restarts {
                true => format!(
                    "Updating {}. Keep the launcher open; it restarts when it's done.",
                    updater::describe(&u.plan().versions())
                ),
                false => format!("Updating {}.", updater::describe(&u.plan().versions())),
            }))),
            Gate::Restarting => Some(row.child(dot(p.success)).child(text(format!(
                "Updated {}. Restarting the launcher…",
                updater::describe(&u.restarting)
            )))),
            Gate::Retry | Gate::Play => {
                let Phase::Failed(error) = &u.phase else {
                    return None;
                };
                if gate == Gate::Play && !u.plan().is_empty() {
                    return None;
                }
                let tip = SharedString::from(error.clone());
                Some(
                    row.text_color(p.error)
                        .child(Icon::new(IconName::CircleAlert).size(px(16.)))
                        .child(
                            div()
                                .id("update-failed")
                                .min_w_0()
                                .truncate()
                                .child(error.clone())
                                .tooltip(move |window, cx| {
                                    let tip = tip.clone();
                                    Tooltip::element(move |_, _| div().max_w(px(480.)).child(tip.clone()))
                                        .build(window, cx)
                                }),
                        )
                        .when(gate == Gate::Retry, |d| {
                            d.child(
                                link("play-anyway", "Play without updating", p)
                                    .flex_shrink_0()
                                    .on_click(cx.listener(|this, _, window, cx| this.play(window, cx))),
                            )
                        })
                        .when(gate == Gate::Play && u.held.is_some(), |d| {
                            d.child(
                                link("update-addon-again", "Try again", p)
                                    .flex_shrink_0()
                                    .on_click(
                                        cx.listener(|this, _, window, cx| this.retry_addon(window, cx)),
                                    ),
                            )
                        }),
                )
            }
        }
    }

    pub(crate) fn update_button(&self, gate: Gate, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let u = &self.updates;
        let summary = updater::summary(&u.plan().versions());
        let (progress, detail) = match &u.progress {
            Some((done, text)) => (Some(*done), text.clone()),
            None => (None, summary.clone()),
        };
        let (icon, label, sub, bg, fg, active) = match gate {
            Gate::Installing => (
                IconName::LoaderCircle,
                match progress {
                    Some(done) => format!("Updating {:.0}%", done * 100.),
                    None => "Updating…".into(),
                },
                detail,
                p.elevated,
                p.text,
                false,
            ),
            Gate::Restarting => (
                IconName::LoaderCircle,
                "Restarting…".into(),
                updater::summary(&u.restarting),
                p.elevated,
                p.text,
                false,
            ),
            Gate::Retry => (
                IconName::RotateCw,
                "Retry update".into(),
                summary,
                p.accent,
                p.on_accent,
                true,
            ),
            Gate::Update | Gate::Play => (
                IconName::CircleArrowUp,
                "Update".into(),
                summary,
                p.accent,
                p.on_accent,
                true,
            ),
        };
        div()
            .id("play")
            .relative()
            .overflow_hidden()
            .h(px(52.))
            .w(px(BUTTON_WIDTH))
            .flex_shrink_0()
            .rounded(px(9.))
            .bg(bg)
            .text_color(fg)
            .flex()
            .items_center()
            .gap(px(10.))
            .px(px(14.))
            .when(!active, |d| d.border_1().border_color(p.border))
            .when(active, |d| {
                d.cursor_pointer()
                    .hover(move |s| s.bg(p.accent_hover))
                    .on_click(cx.listener(|this, _, window, cx| this.start_updates(window, cx)))
            })
            .map(|d| match icon {
                IconName::LoaderCircle => d.child(Spinner::new().icon(icon).with_size(px(18.))),
                _ => d.child(Icon::new(icon).size(px(18.))),
            })
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .child(
                        div()
                            .text_size(px(15.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child(label),
                    )
                    .child(
                        div()
                            .text_size(px(11.))
                            .truncate()
                            .text_color(if active { p.on_accent } else { p.text_muted })
                            .when(active, |d| d.opacity(0.85))
                            .child(sub),
                    ),
            )
            .when_some(progress.filter(|_| gate == Gate::Installing), |d, done| {
                d.child(
                    div()
                        .absolute()
                        .left_0()
                        .bottom_0()
                        .h(px(4.))
                        .w(relative(done.clamp(0., 1.)))
                        .bg(p.accent),
                )
            })
            .into_any_element()
    }

    pub(crate) fn update_bar(&self, p: Palette) -> Option<impl IntoElement> {
        if !self.update_check {
            return None;
        }
        let bar = h_flex()
            .id("update-bar")
            .flex_shrink_0()
            .px(px(16.))
            .py(px(8.))
            .gap(px(12.))
            .bg(p.surface)
            .border_t_1()
            .border_color(p.border_variant)
            .text_size(px(13.));
        let Some((version, tag, reason)) = self.updates.blocked_notice() else {
            let notice = self.updates.waiting_notice()?;
            return Some(
                bar.text_color(p.text_muted)
                    .child(Icon::new(IconName::Info).size(px(16.)))
                    .child(div().flex_1().min_w_0().truncate().child(notice)),
            );
        };
        let notes = update::notes_url(&tag);
        Some(
            bar.child(
                Icon::new(IconName::CircleArrowUp)
                    .size(px(16.))
                    .text_color(p.accent),
            )
            .child(div().flex_1().min_w_0().truncate().child(format!(
                "Swofty Launcher {version} is available. {}. You have {}.",
                sentence(reason),
                update::current_version()
            )))
            .child(
                link("update-release", "Release page", p)
                    .flex_shrink_0()
                    .on_click(move |_, _, cx| cx.open_url(&notes)),
            ),
        )
    }
}
