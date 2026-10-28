//! The Simplified Importer: the managed Simplified for Swofty addon's section, shown
//! as a big section of its own.

use std::time::{Duration, Instant};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::{Disableable, WindowExt, v_flex};
use gpui_kit::*;
use swofty_core::managed;
use swofty_core::profile::VANILLA;

use crate::sections::{Reporter, Shared};
use crate::shell::{Page, Shell};
use crate::theme::Palette;

const FETCH_TIMEOUT: Duration = Duration::from_secs(20);

impl Shell {
    /// The index of the Simplified Importer's section, when its addon is on.
    pub(crate) fn importer_section(&self) -> Option<usize> {
        self.library.sections.iter().position(|s| s.is_importer())
    }

    /// The section the current page shows: an addon page or the Simplified Importer.
    pub(crate) fn open_section(&self) -> Option<usize> {
        match self.page {
            Page::Section(i) => Some(i),
            Page::Importer => self.importer_section(),
            _ => None,
        }
    }

    pub(crate) fn importer_page(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        if let Some(i) = self.importer_section() {
            return self.section_page(i, p, cx).into_any_element();
        }
        let installed = self.library.addons.iter().any(|a| a.manifest.id == managed::ID);
        if installed && self.vanilla() {
            return self.importer_in_vanilla(p, cx);
        }
        let adding = self.updates.adding;
        let (title, body, button) = if !installed {
            (
                format!("Install {}", managed::NAME),
                format!(
                    "{} is an addon by Team Simplified. Its Simplified Importer turns mods made for the game \
                     folder into Swofty addons. The launcher downloads it from GitHub.",
                    managed::NAME
                ),
                Button::new("importer-install")
                    .primary()
                    .icon(IconName::Download)
                    .label(if adding { "Installing…" } else { "Install" })
                    .loading(adding)
                    .disabled(adding)
                    .on_click(cx.listener(|this, _, window, cx| this.install_importer(window, cx))),
            )
        } else {
            (
                format!("Turn on {}", managed::NAME),
                format!("{} is off in {}.", managed::NAME, self.library.profile.name),
                Button::new("importer-on")
                    .primary()
                    .icon(IconName::Power)
                    .label("Turn on")
                    .on_click(cx.listener(|this, _, window, cx| {
                        this.set_addon_enabled(managed::ID, true, window, cx)
                    })),
            )
        };
        v_flex()
            .size_full()
            .child(self.header(Page::Importer.label(), "Team Simplified".to_owned(), div(), p))
            .child(self.empty(IconName::FolderInput, title, body, button, p))
            .into_any_element()
    }

    fn importer_in_vanilla(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let modded = self
            .library
            .profiles
            .iter()
            .find(|p| !p.vanilla())
            .map(|p| p.name.clone());
        let button = match modded {
            Some(name) => Button::new("importer-use-profile")
                .primary()
                .icon(IconName::Layers)
                .label(format!("Use {name}"))
                .on_click(cx.listener(move |this, _, window, cx| this.switch_profile(&name, window, cx))),
            None => Button::new("importer-profiles")
                .outline()
                .icon(IconName::Layers)
                .label("Profiles")
                .on_click(cx.listener(|this, _, _, cx| this.go(Page::Profiles, cx))),
        };
        let action = v_flex().items_center().gap(px(12.)).child(button).child(
            div()
                .text_size(px(12.))
                .text_color(p.text_placeholder)
                .child("Imported mods are listed here once a modded profile is active."),
        );
        v_flex()
            .size_full()
            .child(self.header(Page::Importer.label(), "Team Simplified".to_owned(), div(), p))
            .child(self.empty(
                IconName::ShieldCheck,
                "Vanilla stays stock",
                format!(
                    "The {VANILLA} profile plays F1 25 as it shipped, without addons, so mods aren't \
                     imported or turned on here."
                ),
                action,
                p,
            ))
            .into_any_element()
    }

    fn install_importer(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.updates.adding {
            return;
        }
        self.updates.adding = true;
        let store = self.store.clone();
        let offered = self.updates.missing_addon().cloned();
        cx.spawn_in(window, async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move {
                    let release = match offered {
                        Some(r) => r,
                        None => managed::fetch_latest(FETCH_TIMEOUT).map_err(|e| e.to_string())?,
                    };
                    let launcher = swofty_core::update::current_version();
                    managed::install_release(&store, &release, &launcher, &Reporter(Shared::default()))
                        .map_err(|e| e.to_string())
                })
                .await;
            this.update_in(cx, |s, window, cx| {
                s.updates.adding = false;
                match result {
                    Ok(version) => {
                        s.updates.addon_installed = Some(version.clone());
                        s.reload(cx);
                        if s.importer_section().is_none() && !s.vanilla() {
                            s.set_addon_enabled(managed::ID, true, window, cx);
                        }
                        window.push_notification(
                            Notification::success(format!("Installed {} {version}", managed::NAME)),
                            cx,
                        );
                    }
                    Err(e) => {
                        crate::app_log::action("install addon")
                            .addon(managed::ID)
                            .failed(&e);
                        window.push_notification(
                            Notification::error(e).title(format!("Could not install {}", managed::NAME)),
                            cx,
                        )
                    }
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    /// Switches to Addons and highlights `id` there.
    pub(crate) fn show_addon(&mut self, id: &str, cx: &mut Context<Self>) {
        self.highlight = Some((id.to_owned(), Instant::now()));
        self.go(Page::Addons, cx);
    }
}
