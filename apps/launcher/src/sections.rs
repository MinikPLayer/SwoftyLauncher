use std::collections::{BTreeMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariant, ButtonVariants};
use gpui_kit::component::menu::{ContextMenuExt, DropdownMenu, PopupMenuItem};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::switch::Switch;
use gpui_kit::component::{Disableable, Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::baseline::Identity;
use swofty_core::import::{self, Route};
use swofty_core::install::Game;
use swofty_core::launcher::Progress;
use swofty_core::profile::Profile;
use swofty_core::registry::{Host, Registry};
use swofty_core::store::Store;
use swofty_core::view::{self, Art, Block, Body, ButtonKind, Control, Effect, Event, Row, Style, Tone, View};

use crate::blocks::{At, search_id};
use crate::game::Install;
use crate::library::{AddonIcon, SectionItem, plural};
use crate::pages::{GUTTER, badge, chevron, fact, label, monogram, names, stat, tag, tile};
use crate::scroll::SmoothScroll;
use crate::shell::{Page, Shell};
use crate::theme::Palette;

pub(crate) type Shared = Arc<Mutex<Option<(f32, String)>>>;

pub(crate) struct Reporter(pub(crate) Shared);

impl Progress for Reporter {
    fn report(&self, done: f32, text: &str) {
        if let Ok(mut p) = self.0.lock() {
            *p = Some((done, text.to_owned()));
        }
    }
}

pub(crate) fn game(store: &Store, install: Option<&Install>) -> Option<Game> {
    let install = install?;
    let identity = Identity::detect_cached(&store.root, &install.dir, install.build_id).ok()?;
    Some(Game {
        dir: install.dir.clone(),
        user_reg: None,
        identity,
    })
}

fn host(store: &Store, addon: &str, profile: &Profile, game: Option<&Game>) -> Result<Host, String> {
    Host::new(store, addon, profile, game).map_err(|e| e.to_string())
}

pub(crate) fn icon(i: view::Icon) -> IconName {
    use view::Icon as I;
    match i {
        I::Activity => IconName::Activity,
        I::ArrowRightLeft => IconName::ArrowRightLeft,
        I::ChevronDown => IconName::ChevronDown,
        I::ChevronUp => IconName::ChevronUp,
        I::CircleAlert => IconName::CircleAlert,
        I::CircleCheck => IconName::CircleCheck,
        I::Copy => IconName::Copy,
        I::Eraser => IconName::Eraser,
        I::FolderOpen => IconName::FolderOpen,
        I::Gamepad => IconName::Gamepad2,
        I::Layers => IconName::Layers,
        I::Lock => IconName::Lock,
        I::Map => IconName::Map,
        I::Package => IconName::Package,
        I::Pencil => IconName::Pencil,
        I::Play => IconName::Play,
        I::Plus => IconName::Plus,
        I::Puzzle => IconName::Puzzle,
        I::Settings => IconName::Settings,
        I::ShieldCheck => IconName::ShieldCheck,
        I::Trash => IconName::Trash,
        I::Upload => IconName::Upload,
        I::Calendar => IconName::Calendar,
        I::Car => IconName::Car,
        I::Clock => IconName::Clock,
        I::CloudRain => IconName::CloudRain,
        I::Flag => IconName::Flag,
        I::Gauge => IconName::Gauge,
        I::History => IconName::RotateCcw,
        I::Info => IconName::Info,
        I::Refresh => IconName::RefreshCw,
        I::Save => IconName::Save,
        I::Search => IconName::Search,
        I::Timer => IconName::Timer,
        I::TrendingDown => IconName::TrendingDown,
        I::TrendingUp => IconName::TrendingUp,
        I::TriangleAlert => IconName::TriangleAlert,
        I::Trophy => IconName::Trophy,
        I::Undo => IconName::Undo2,
        I::User => IconName::User,
        I::Users => IconName::Users,
        I::Wallet => IconName::Wallet,
        I::Wrench => IconName::Wrench,
        I::Zap => IconName::Zap,
    }
}

pub(crate) fn tone(t: Tone, p: Palette) -> Hsla {
    match t {
        Tone::Neutral => p.text,
        Tone::Muted => p.text_muted,
        Tone::Accent => p.accent,
        Tone::Good => p.success,
        Tone::Warn => p.warning,
        Tone::Bad => p.error,
    }
}

pub(crate) fn art(a: &Art, size: f32, p: Palette) -> AnyElement {
    match a {
        Art::None => badge(IconName::Package, size, p).into_any_element(),
        Art::Monogram(text) => monogram(text, size, p).into_any_element(),
        Art::Image(path) => div()
            .size(px(size))
            .flex_shrink_0()
            .rounded(px(5.))
            .overflow_hidden()
            .border_1()
            .border_color(p.border_variant)
            .child(img(path.clone()).size_full().object_fit(ObjectFit::Cover))
            .into_any_element(),
    }
}

pub(crate) fn section_key(s: &SectionItem) -> String {
    format!("{}/{}", s.addon, s.spec.id)
}

fn file_name(path: &Path) -> String {
    path.file_name().map_or_else(
        || path.display().to_string(),
        |n| n.to_string_lossy().into_owned(),
    )
}

impl Shell {
    pub(crate) fn refresh_views(&mut self, cx: &mut Context<Self>) {
        self.fetch_views(None, cx);
    }

    pub(crate) fn fetch_views(&mut self, only: Option<usize>, cx: &mut Context<Self>) {
        let store = self.store.clone();
        let library = &self.library;
        let registry = library.registry.clone();
        let profile = library.profile.clone();
        let install = self.panel_install();
        let open = self.open_section();
        let keys: HashSet<String> = library.sections.iter().map(section_key).collect();
        if self.views_profile != profile.name {
            self.views_profile = profile.name.clone();
            self.views.clear();
        }
        self.views.retain(|k, _| keys.contains(k));
        let prefetch = self.detected;
        let sections: Vec<(String, String, bool)> = library
            .sections
            .iter()
            .enumerate()
            .filter(|(i, _)| only.is_none_or(|o| o == *i))
            .filter(|(i, s)| {
                open == Some(*i) || prefetch || !registry.get(&s.addon).is_some_and(|e| e.helper)
            })
            .map(|(_, s)| {
                let key = section_key(s);
                let cached = self.views.contains_key(&key);
                (s.addon.clone(), s.spec.id.clone(), cached)
            })
            .collect();
        if sections.is_empty() {
            return;
        }
        self.view_gen += 1;
        let generation = self.view_gen;
        cx.spawn(async move |this, cx| {
            let views: Vec<(String, Result<View, String>)> = cx
                .background_executor()
                .spawn(async move {
                    let game = game(&store, install.as_ref());
                    sections
                        .into_iter()
                        .filter_map(|(addon, section, cached)| {
                            let view = registry
                                .get(&addon)
                                .ok_or_else(|| format!("{addon} is gone"))
                                .and_then(|e| {
                                    let host = host(&store, &addon, &profile, game.as_ref())?;
                                    let view = match (&e.plugin, cached) {
                                        (Some(plugin), true) => {
                                            let view = plugin.try_view(&host.cx(), &section);
                                            if view.is_none() {
                                                plugin.mark_stale(Some(&section));
                                            }
                                            view
                                        }
                                        _ => Some(e.addon.view(&host.cx(), &section)),
                                    };
                                    Ok(view.map(|v| v.map_err(|e| e.to_string())))
                                });
                            match view {
                                Ok(None) => None,
                                Ok(Some(view)) => Some((format!("{addon}/{section}"), view)),
                                Err(e) => Some((format!("{addon}/{section}"), Err(e))),
                            }
                        })
                        .collect()
                })
                .await;
            this.update(cx, |s, cx| {
                for (key, view) in views {
                    if s.view_gens.get(&key).is_some_and(|g| *g > generation) {
                        continue;
                    }
                    s.view_gens.insert(key.clone(), generation);
                    s.views.insert(key, view.map(Arc::new));
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    pub(crate) fn live_poll(&mut self, cx: &mut Context<Self>) {
        self.live_n += 1;
        self.refresh_stale(cx);
        if self.live_n.is_multiple_of(4) {
            self.tick(cx);
        }
    }

    fn refresh_stale(&mut self, cx: &mut Context<Self>) {
        let open = self.open_section();
        let mut due = Vec::new();
        let mut seen = HashSet::new();
        for s in &self.library.sections {
            if !seen.insert(s.addon.clone()) {
                continue;
            }
            let Some(plugin) = self.library.registry.get(&s.addon).and_then(|e| e.plugin.clone()) else {
                continue;
            };
            if plugin.busy() {
                continue;
            }
            let Some(stale) = plugin.take_stale() else {
                continue;
            };
            for (i, t) in self.library.sections.iter().enumerate() {
                let shown = open == Some(i) || self.views.contains_key(&section_key(t));
                if t.addon == s.addon && shown && stale.covers(&t.spec.id) {
                    due.push(i);
                }
            }
        }
        for i in due {
            self.fetch_views(Some(i), cx);
        }
    }

    fn tick(&mut self, cx: &mut Context<Self>) {
        let Some(i) = self.open_section() else {
            return;
        };
        let Some(s) = self.library.sections.get(i) else {
            return;
        };
        let ticks = matches!(self.views.get(&section_key(s)), Some(Ok(v)) if v.tick);
        let Some(plugin) = self.library.registry.get(&s.addon).and_then(|e| e.plugin.clone()) else {
            return;
        };
        if !ticks || self.ticking || !plugin.running() {
            return;
        }
        self.ticking = true;
        let store = self.store.clone();
        let profile = self.library.profile.clone();
        let install = self.panel_install();
        let (addon, section) = (s.addon.clone(), s.spec.id.clone());
        cx.spawn(async move |this, cx| {
            let failed = cx
                .background_executor()
                .spawn(async move {
                    let game = game(&store, install.as_ref());
                    let Ok(host) = host(&store, &addon, &profile, game.as_ref()) else {
                        return false;
                    };
                    if let Some(Err(e)) = plugin.try_tick(&host.cx(), &section) {
                        eprintln!("swofty-launcher: {addon} tick: {e}");
                    }
                    plugin.failure().is_some()
                })
                .await;
            this.update(cx, |s, cx| {
                s.ticking = false;
                if failed {
                    s.fetch_views(Some(i), cx);
                }
                s.refresh_stale(cx);
            })
            .ok();
        })
        .detach();
    }

    /// Sends `startup` to the first section of each addon that asks for it.
    pub(crate) fn run_startup(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let mut seen = std::collections::BTreeSet::new();
        let targets: Vec<(String, String)> = self
            .library
            .sections
            .iter()
            .filter(|s| seen.insert(s.addon.clone()))
            .filter(|s| {
                self.library.addons.iter().any(|a| {
                    a.manifest.id == s.addon && a.manifest.launcher.as_ref().is_some_and(|l| l.startup)
                })
            })
            .map(|s| (s.addon.clone(), s.spec.id.clone()))
            .collect();
        for (addon, section) in targets {
            let control = format!("{addon}/{section}/startup");
            self.send(&addon, &section, control, Event::Startup {}, window, cx);
        }
    }

    pub(crate) fn send(
        &mut self,
        addon: &str,
        section: &str,
        control: String,
        event: Event,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let Some(entry) = self.library.registry.get(addon).cloned() else {
            return;
        };
        let store = self.store.clone();
        let profile = self.library.profile.clone();
        let install = self.panel_install();
        let (addon, section) = (addon.to_owned(), section.to_owned());
        let feed = crate::tasks::Feed::default();
        let sink = crate::tasks::Sink(feed.clone());
        self.busy.insert(control.clone());
        self.follow_tasks(control.clone(), feed.clone(), cx);
        cx.notify();
        cx.spawn_in(window, async move |this, cx| {
            let id = addon.clone();
            let effect = cx
                .background_executor()
                .spawn(async move {
                    let game = game(&store, install.as_ref());
                    let host = host(&store, &addon, &profile, game.as_ref())?;
                    entry
                        .addon
                        .event(&host.cx().with_tasks(&sink), &section, event)
                        .map_err(|e| e.to_string())
                })
                .await;
            this.update_in(cx, |s, window, cx| {
                s.busy.remove(&control);
                s.settled(&control, &effect);
                let effect = s.finish_tasks(&control, &feed, effect, cx);
                s.effect(&id, effect, window, cx);
            })
            .ok();
        })
        .detach();
    }

    fn effect(
        &mut self,
        addon: &str,
        effect: Result<Effect, String>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        match effect {
            Ok(Effect::None) => cx.notify(),
            Ok(Effect::Reload) => self.reload(cx),
            Ok(Effect::Notice(text)) => {
                window.push_notification(Notification::info(text), cx);
                self.reload(cx);
            }
            Ok(Effect::SetAddonEnabled(on)) => self.set_addon_enabled(addon, on, window, cx),
            Ok(Effect::Open(path)) => {
                cx.open_with_system(&path);
                cx.notify();
            }
            Ok(Effect::ShowAddon(shown)) => {
                if let Some(text) = shown.notice() {
                    window.push_notification(Notification::info(text.to_owned()), cx);
                }
                self.reload(cx);
                self.show_addon(shown.id(), cx);
            }
            Err(e) => {
                crate::app_log::action("addon action").addon(addon).failed(&e);
                window.push_notification(Notification::error(e), cx);
                cx.notify();
            }
        }
    }

    pub(crate) fn drop_paths(&mut self, paths: Vec<PathBuf>, window: &mut Window, cx: &mut Context<Self>) {
        self.dropping = false;
        if paths.is_empty() {
            return;
        }
        let store = self.store.clone();
        let registry = self.library.registry.clone();
        let profile = self.library.profile.clone();
        let install = self.install.clone();
        let shared: Shared = Arc::new(Mutex::new(Some((
            0.,
            format!("Importing {}", plural(paths.len(), "item", "items")),
        ))));
        let reporter = Reporter(shared.clone());
        self.follow_progress(shared, cx);
        cx.spawn_in(window, async move |this, cx| {
            let (mut done, failed, unclaimed, fresh, left_out) = cx
                .background_executor()
                .spawn(async move {
                    let before = installed(&store);
                    let (done, failed, unclaimed) =
                        import_all(&store, &registry, &profile, install.as_ref(), &paths, &reporter);
                    let fresh = installed_since(&before, &installed(&store));
                    let addons = store.addons().unwrap_or_default();
                    let left_out: Vec<String> = fresh
                        .iter()
                        .filter_map(|id| addons.get(id))
                        .flat_map(|m| swofty_core::tracks::left_out_lines(&store, m))
                        .collect();
                    if let Ok(mut p) = reporter.0.lock() {
                        *p = None;
                    }
                    (done, failed, unclaimed, fresh, left_out)
                })
                .await;
            if fresh.len() > 1 {
                done.insert(
                    0,
                    format!(
                        "Installed {}; Addons highlights the newest",
                        plural(fresh.len(), "addon", "addons")
                    ),
                );
            }
            this.update_in(cx, |s, window, cx| {
                if !done.is_empty() {
                    window.push_notification(Notification::success(done.join("\n")), cx);
                }
                if !failed.is_empty() {
                    window.push_notification(Notification::error(failed.join("\n")), cx);
                }
                if !left_out.is_empty() {
                    window.push_notification(
                        Notification::warning(left_out.join("\n"))
                            .title("Track can't be used")
                            .autohide(false),
                        cx,
                    );
                }
                if !unclaimed.is_empty() {
                    s.show_not_addons(&unclaimed, window, cx);
                }
                s.rediscover = true;
                s.reload(cx);
                if let Some(id) = fresh.first() {
                    s.show_addon(id, cx);
                }
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    fn show_not_addons(&self, paths: &[PathBuf], window: &mut Window, cx: &mut Context<Self>) {
        const SHOWN: usize = 5;
        let one = paths.len() == 1;
        let names: Vec<String> = paths.iter().take(SHOWN).map(|p| file_name(p)).collect();
        let more = paths.len().saturating_sub(SHOWN);
        let shell = cx.entity().downgrade();
        window.open_alert_dialog(cx, move |alert, _, cx| {
            let p = crate::theme::palette(cx);
            let shell = shell.clone();
            let files = v_flex()
                .gap(px(8.))
                .child(if one {
                    "The launcher can't install this file as an addon:"
                } else {
                    "The launcher can't install these files as addons:"
                })
                .child(
                    v_flex()
                        .px(px(12.))
                        .py(px(8.))
                        .gap(px(4.))
                        .rounded(px(8.))
                        .bg(p.background)
                        .border_1()
                        .border_color(p.border_variant)
                        .text_color(p.text)
                        .children(names.iter().map(|n| {
                            h_flex()
                                .gap(px(8.))
                                .items_center()
                                .child(
                                    Icon::new(IconName::File)
                                        .size(px(14.))
                                        .text_color(p.text_placeholder),
                                )
                                .child(div().min_w_0().truncate().child(n.clone()))
                        }))
                        .when(more > 0, |d| {
                            d.child(
                                div()
                                    .text_color(p.text_placeholder)
                                    .child(format!("and {more} more")),
                            )
                        }),
                );
            let mods = if one {
                "Mods like this one go through the Simplified Importer, which turns them into addons."
            } else {
                "Mods like these go through the Simplified Importer, which turns them into addons."
            };
            alert
                .width(px(540.))
                .title(if one {
                    "That isn't a Swofty addon"
                } else {
                    "Those aren't Swofty addons"
                })
                .description(files)
                .child(
                    v_flex()
                        .gap(px(10.))
                        .child(drop_choice(
                            IconName::FolderInput,
                            "Trying to add a mod?",
                            mods,
                            Button::new("not-addon-importer")
                                .primary()
                                .icon(IconName::ArrowRight)
                                .label("Go to Simplified Importer")
                                .on_click(move |_, window, cx| {
                                    window.close_dialog(cx);
                                    shell.update(cx, |s, cx| s.open_page(Page::Importer, cx)).ok();
                                }),
                            p,
                        ))
                        .child(drop_choice(
                            IconName::BookOpen,
                            "Making a Swofty addon?",
                            "An addon is a folder with a swofty.json.",
                            crate::tools::web_link("not-addon-docs", "Read the addon docs", ADDON_DOCS, p),
                            p,
                        )),
                )
                .ok_text("Close")
                .ok_variant(ButtonVariant::Secondary)
        });
    }

    pub(crate) fn follow_progress(&mut self, shared: Shared, cx: &mut Context<Self>) {
        self.progress = shared.lock().ok().and_then(|p| p.clone());
        cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(Duration::from_millis(100)).await;
                let now = shared.lock().ok().and_then(|p| p.clone());
                let done = now.is_none();
                if this
                    .update(cx, |s, cx| {
                        s.progress = now;
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

    pub(crate) fn section_page(&mut self, index: usize, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let Some(section) = self.library.sections.get(index) else {
            return div().into_any_element();
        };
        let (addon, id, title) = (
            section.addon.clone(),
            section.spec.id.clone(),
            section.spec.title.clone(),
        );
        let section_icon = icon(section.spec.icon);
        let view = match self.views.get(&format!("{addon}/{id}")) {
            Some(Ok(v)) => v.clone(),
            Some(Err(e)) => {
                return v_flex()
                    .size_full()
                    .child(self.header(title, String::new(), div(), p))
                    .child(self.empty(section_icon, "This page could not load", e.clone(), div(), p))
                    .into_any_element();
            }
            None => {
                return v_flex()
                    .size_full()
                    .child(self.header(title, "Loading…".into(), div(), p))
                    .into_any_element();
            }
        };
        let mut actions: Vec<AnyElement> = view
            .actions
            .iter()
            .map(|b| self.button(&addon, &id, b, false, cx))
            .collect();
        // Tracks is in the Addons group, whose pages all have Install addon.
        if id == crate::library::TRACKS_SECTION {
            actions.push(self.install_addon_button(cx).into_any_element());
        }
        let notices: Vec<AnyElement> = view
            .notices
            .iter()
            .enumerate()
            .map(|(i, n)| {
                h_flex()
                    .id(SharedString::from(format!("notice-{i}")))
                    .mx(px(GUTTER))
                    .mb(px(10.))
                    .px(px(12.))
                    .py(px(8.))
                    .gap(px(10.))
                    .flex_shrink_0()
                    .rounded(px(6.))
                    .border_1()
                    .border_color(p.border_variant)
                    .bg(p.surface)
                    .text_size(px(13.))
                    .child(
                        Icon::new(IconName::CircleAlert)
                            .size(px(14.))
                            .text_color(tone(n.tone, p)),
                    )
                    .child(div().flex_1().min_w_0().text_color(p.text).child(n.text.clone()))
                    .children(n.action.as_ref().map(|b| self.button(&addon, &id, b, true, cx)))
                    .into_any_element()
            })
            .collect();
        let width = self.content_width;
        let mut search_bar = None;
        let mut footer = view.footer.clone().map(|f| {
            div()
                .flex_shrink_0()
                .py(px(10.))
                .border_t_1()
                .border_color(p.border_variant)
                .text_size(px(12.))
                .text_color(p.text_placeholder)
                .child(f)
        });
        let body = match &view.body {
            Body::List {
                rows,
                reorder,
                search,
            } => {
                let key = search
                    .as_ref()
                    .map(|s| format!("{addon}/{id}/{}", search_id(s, "list")));
                let query = key
                    .as_deref()
                    .filter(|_| search.as_ref().is_some_and(|s| s.filter))
                    .and_then(|k| self.query(k));
                search_bar = key.and_then(|k| self.search_box(&k, p));
                let shown: Vec<(usize, &Row)> = rows
                    .iter()
                    .enumerate()
                    .filter(|(_, r)| {
                        query.is_none_or(|q| {
                            let tags = r.tags.iter().map(|t| t.text.as_str());
                            view::Search::matches(
                                q,
                                [r.title.as_str(), r.subtitle.as_str()].into_iter().chain(tags),
                            )
                        })
                    })
                    .collect();
                let rows: Vec<AnyElement> = shown
                    .iter()
                    .enumerate()
                    .map(|(n, (i, r))| self.view_row(&addon, &id, r, *i, n, rows.len(), *reorder, p, cx))
                    .collect();
                if rows.is_empty() && query.is_some() {
                    div()
                        .px(px(GUTTER))
                        .py(px(12.))
                        .text_size(px(13.))
                        .text_color(p.text_muted)
                        .child("No matches")
                        .into_any_element()
                } else {
                    self.list(SharedString::from(format!("section-{addon}-{id}")), rows, p)
                        .children(footer.take().map(|f| f.mt(px(16.))))
                        .into_any_element()
                }
            }
            Body::Empty { title, text, action } => {
                let action = action
                    .as_ref()
                    .map(|b| self.button(&addon, &id, b, false, cx))
                    .unwrap_or_else(|| div().into_any_element());
                self.empty(section_icon, title.clone(), text.clone(), action, p)
            }
            // The wheel glides, as on the Addons and Addon Browser pages.
            Body::Blocks(blocks) => {
                let (scroll, smooth) = self
                    .section_scrolls
                    .entry(format!("{addon}/{id}"))
                    .or_insert_with(|| (ScrollHandle::new(), SmoothScroll::default()))
                    .clone();
                let body = v_flex()
                    .flex_none()
                    .px(px(GUTTER))
                    .pt(px(4.))
                    .pb(px(24.))
                    .gap(px(16.))
                    .children(blocks.iter().map(|b| {
                        let at = At {
                            addon: &addon,
                            section: &id,
                            width,
                            nested: false,
                            fill: false,
                        };
                        self.block_at(at, b, p, cx)
                    }))
                    .children(footer.take());
                div()
                    .relative()
                    .flex_1()
                    .min_h_0()
                    .w_full()
                    .child(
                        div()
                            .id(SharedString::from(format!("section-{addon}-{id}")))
                            .size_full()
                            .flex()
                            .flex_col()
                            .overflow_y_scroll()
                            .track_scroll(&scroll)
                            .child(body),
                    )
                    .child(smooth.overlay(&scroll, cx))
                    .into_any_element()
            }
        };
        v_flex()
            .size_full()
            .child(self.header(
                title,
                view.subtitle.clone(),
                h_flex().gap(px(8.)).children(actions),
                p,
            ))
            .children(notices)
            .children(search_bar.map(|s| h_flex().px(px(GUTTER)).pb(px(12.)).flex_shrink_0().child(s)))
            .child(body)
            .children(footer.map(|f| f.mx(px(GUTTER))))
            .into_any_element()
    }

    pub(crate) fn section_count(&self, index: usize) -> Option<usize> {
        let s = self.library.sections.get(index)?;
        match &self
            .views
            .get(&format!("{}/{}", s.addon, s.spec.id))?
            .as_ref()
            .ok()?
            .body
        {
            Body::List { rows, .. } => Some(rows.len()),
            Body::Empty { .. } => Some(0),
            Body::Blocks(_) => None,
        }
    }

    // A row without art shows its page's icon: `sections/<id>.svg` when the addon has one,
    // else the page's icon from the launcher's set.
    fn row_art(&self, addon: &str, section: &str, a: &Art, size: f32, p: Palette) -> AnyElement {
        if !a.is_none() {
            return art(a, size, p);
        }
        let s = self
            .library
            .sections
            .iter()
            .find(|s| s.addon == addon && s.spec.id == section);
        match s {
            Some(SectionItem {
                icon: Some(AddonIcon::Glyph(data)),
                ..
            }) => tile(size, p)
                .child(svg().data(data).size(px(size * 0.5)).text_color(p.text_muted))
                .into_any_element(),
            Some(s) => badge(icon(s.spec.icon), size, p).into_any_element(),
            None => art(a, size, p),
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn view_row(
        &self,
        addon: &str,
        section: &str,
        r: &Row,
        index: usize,
        position: usize,
        count: usize,
        reorder: bool,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let key = format!("{addon}/{section}/{}", r.id);
        let open = self.open_row.as_ref() == Some(&key);
        let expandable = !r.detail.is_empty() || reorder;
        let trailing = r.trailing.as_ref().map(|c| self.control(addon, section, c, cx));
        let toggle_key = key.clone();
        let head = self
            .row_head(format!("row-{key}"), open, p)
            .child(self.row_art(addon, section, &r.art, 32., p))
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .gap(px(2.))
                    .child(
                        div()
                            .text_size(px(14.))
                            .font_weight(FontWeight::MEDIUM)
                            .truncate()
                            .child(r.title.clone()),
                    )
                    .child(
                        div()
                            .text_size(px(13.))
                            .text_color(p.text_muted)
                            .truncate()
                            .child(r.subtitle.clone()),
                    ),
            )
            .child(
                h_flex().gap(px(4.)).flex_shrink_0().children(
                    r.tags
                        .iter()
                        .map(|t| tag(t.text.clone(), p).text_color(tone(t.tone, p))),
                ),
            )
            .children(trailing.map(|t| div().w(px(96.)).flex().justify_end().child(t)))
            .when(expandable, |d| d.child(chevron(open, p)))
            .on_click(cx.listener(move |this, _, _, cx| {
                this.open_row = (this.open_row.as_ref() != Some(&toggle_key)).then(|| toggle_key.clone());
                cx.notify();
            }));
        let head = match r.menu.is_empty() {
            true => head.into_any_element(),
            false => {
                let shell = cx.entity().downgrade();
                let (addon, section, items, key) =
                    (addon.to_owned(), section.to_owned(), r.menu.clone(), key.clone());
                head.context_menu(move |mut menu, _, _| {
                    for b in &items {
                        let (shell, addon, section, b) =
                            (shell.clone(), addon.clone(), section.clone(), b.clone());
                        let control = format!("{key}/menu-{}", b.id);
                        let mut item = PopupMenuItem::new(b.label.clone());
                        if let Some(i) = b.icon {
                            item = item.icon(icon(i));
                        }
                        menu = menu.item(item.on_click(move |_, window, cx| {
                            shell
                                .update(cx, |this, cx| {
                                    this.press(&addon, &section, control.clone(), &b, window, cx)
                                })
                                .ok();
                        }));
                    }
                    menu
                })
                .into_any_element()
            }
        };
        let moves = reorder.then(|| {
            let mv = |label: &'static str, ic: IconName, to: usize, enabled: bool, cx: &mut Context<Self>| {
                let (addon, section, row) = (addon.to_owned(), section.to_owned(), r.id.clone());
                let control = format!("{key}/move-{label}");
                Button::new(SharedString::from(control.clone()))
                    .outline()
                    .small()
                    .icon(ic)
                    .label(label)
                    .disabled(!enabled || self.busy.contains(&control))
                    .on_click(cx.listener(move |this, _, window, cx| {
                        let event = Event::Move { row: row.clone(), to };
                        this.send(&addon, &section, control.clone(), event, window, cx)
                    }))
            };
            h_flex()
                .gap(px(6.))
                .child(mv(
                    "Move up",
                    IconName::ArrowUp,
                    index.saturating_sub(1),
                    index > 0,
                    cx,
                ))
                .child(mv(
                    "Move down",
                    IconName::ArrowDown,
                    index + 1,
                    index + 1 < count,
                    cx,
                ))
        });
        self.row(position == 0, open, p)
            .child(head)
            .when(open, |d| {
                d.child(
                    v_flex()
                        .gap(px(16.))
                        .pl(px(58.))
                        .pr(px(20.))
                        .pt(px(14.))
                        .pb(px(18.))
                        .border_t_1()
                        .border_color(p.border_variant)
                        .children(r.detail.iter().map(|b| {
                            let at = At {
                                addon,
                                section,
                                width: (self.content_width - 80.).max(240.),
                                nested: false,
                                fill: false,
                            };
                            self.block_at(at, b, p, cx)
                        }))
                        .children(moves),
                )
            })
            .into_any_element()
    }

    pub(crate) fn block_at(&self, at: At, b: &Block, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        if let Some(rich) = self.rich_block(at, b, p, cx) {
            return rich;
        }
        let (addon, section) = (at.addon, at.section);
        match b {
            Block::Stats(facts) => h_flex()
                .flex_wrap()
                .gap_x(px(32.))
                .gap_y(px(12.))
                .children(
                    facts
                        .iter()
                        .map(|f| stat(f.label.to_uppercase(), f.value.clone(), p.text, p)),
                )
                .into_any_element(),
            Block::Facts(facts) => v_flex()
                .max_w(px(560.))
                .children(facts.iter().map(|f| fact(f.label.clone(), f.value.clone(), p)))
                .into_any_element(),
            Block::Label(text) => label(text.to_uppercase(), p).into_any_element(),
            Block::Text { text, tone: t } => div()
                .max_w(px(720.))
                .text_size(px(13.))
                .line_height(px(20.))
                .text_color(tone(*t, p))
                .child(text.clone())
                .into_any_element(),
            Block::Names { label, items } => names(label.to_uppercase(), items, p).into_any_element(),
            Block::Table(table) => {
                let columns: Vec<&str> = table.columns.iter().map(|c| c.title()).collect();
                let rows: Vec<Vec<String>> = table
                    .rows
                    .iter()
                    .map(|r| r.cells().iter().map(|c| c.text().to_owned()).collect())
                    .collect();
                let cell = |text: String, head: bool| {
                    div()
                        .flex_1()
                        .min_w_0()
                        .truncate()
                        .text_size(px(if head { 11. } else { 13. }))
                        .text_color(if head { p.text_placeholder } else { p.text })
                        .child(text)
                };
                v_flex()
                    .max_w(px(760.))
                    .rounded(px(5.))
                    .border_1()
                    .border_color(p.border_variant)
                    .child(
                        h_flex()
                            .px(px(12.))
                            .py(px(6.))
                            .gap(px(12.))
                            .children(columns.iter().map(|c| cell(c.to_uppercase(), true))),
                    )
                    .children(rows.iter().map(|row| {
                        h_flex()
                            .px(px(12.))
                            .py(px(6.))
                            .gap(px(12.))
                            .border_t_1()
                            .border_color(p.border_variant)
                            .children(row.iter().map(|c| cell(c.clone(), false)))
                    }))
                    .into_any_element()
            }
            Block::Buttons(buttons) => h_flex()
                .gap(px(6.))
                .children(buttons.iter().map(|b| self.button(addon, section, b, true, cx)))
                .into_any_element(),
            _ => div().into_any_element(),
        }
    }

    fn control(&self, addon: &str, section: &str, c: &Control, cx: &mut Context<Self>) -> AnyElement {
        match c {
            Control::Switch { id, on, enabled } => {
                let control = format!("{addon}/{section}/{id}");
                let hold = SharedString::from(format!("hold-{control}"));
                let (addon, section, id) = (addon.to_owned(), section.to_owned(), id.clone());
                let switch = Switch::new(SharedString::from(format!("switch-{control}")))
                    .checked(*on)
                    .small()
                    .disabled(!enabled || self.busy.contains(&control))
                    .on_change(cx.listener(move |this, on: &bool, window, cx| {
                        let event = Event::Toggle {
                            id: id.clone(),
                            on: *on,
                        };
                        this.send(&addon, &section, control.clone(), event, window, cx)
                    }));
                div()
                    .id(hold)
                    .on_click(|_, _, cx| cx.stop_propagation())
                    .child(switch)
                    .into_any_element()
            }
            Control::Choice { id, options, current } => {
                let control = format!("{addon}/{section}/{id}");
                let shell = cx.entity().downgrade();
                let (options, current) = (options.clone(), *current);
                let (addon, section, id) = (addon.to_owned(), section.to_owned(), id.clone());
                Button::new(SharedString::from(format!("choice-{control}")))
                    .outline()
                    .small()
                    .min_w(px(140.))
                    .label(options.get(current).cloned().unwrap_or_default())
                    .dropdown_caret(true)
                    .disabled(self.busy.contains(&control))
                    .dropdown_menu_with_anchor(Anchor::TopRight, move |mut menu, _, _| {
                        for (i, text) in options.iter().enumerate() {
                            let (shell, addon, section, id, control) = (
                                shell.clone(),
                                addon.clone(),
                                section.clone(),
                                id.clone(),
                                control.clone(),
                            );
                            menu =
                                menu.item(PopupMenuItem::new(text.clone()).checked(i == current).on_click(
                                    move |_, window, cx| {
                                        let event = Event::Choose {
                                            id: id.clone(),
                                            index: i,
                                        };
                                        shell
                                            .update(cx, |this, cx| {
                                                this.send(
                                                    &addon,
                                                    &section,
                                                    control.clone(),
                                                    event,
                                                    window,
                                                    cx,
                                                )
                                            })
                                            .ok();
                                    },
                                ));
                        }
                        menu
                    })
                    .into_any_element()
            }
            Control::Button(b) => self.button(addon, section, b, true, cx),
        }
    }

    pub(crate) fn button(
        &self,
        addon: &str,
        section: &str,
        b: &view::Button,
        small: bool,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let control = format!("{addon}/{section}/{}", b.id);
        let mut button = Button::new(SharedString::from(format!("button-{control}")))
            .label(b.label.clone())
            .loading(self.busy.contains(&control))
            .disabled(self.busy.contains(&control));
        button = match b.style {
            Style::Primary => button.primary(),
            Style::Outline => button.outline(),
            Style::Ghost => button.ghost(),
            Style::Danger => button.danger().ghost(),
        };
        button = match b.size {
            view::ButtonSize::Large => button.large().h(px(52.)).px(px(32.)).text_size(px(16.)),
            view::ButtonSize::Normal if small => button.small(),
            view::ButtonSize::Normal => button,
        };
        if let Some(i) = b.icon {
            button = button.icon(icon(i));
        }
        let (addon, section, b) = (addon.to_owned(), section.to_owned(), b.clone());
        button
            .on_click(cx.listener(move |this, _, window, cx| {
                cx.stop_propagation();
                this.press(&addon, &section, control.clone(), &b, window, cx);
            }))
            .into_any_element()
    }

    // A click on a button or a row's menu item: send it, ask first, or pick files.
    fn press(
        &mut self,
        addon: &str,
        section: &str,
        control: String,
        b: &view::Button,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let (addon, section, id) = (addon.to_owned(), section.to_owned(), b.id.clone());
        let click = Event::Click { id: id.clone() };
        match &b.kind {
            ButtonKind::Event => self.send(&addon, &section, control, click, window, cx),
            ButtonKind::Confirm { title, text } => {
                let shell = cx.entity().downgrade();
                let (title, text) = (
                    SharedString::from(title.clone()),
                    SharedString::from(text.clone()),
                );
                window.open_alert_dialog(cx, move |alert, _, _| {
                    let (shell, addon, section, control, id) = (
                        shell.clone(),
                        addon.clone(),
                        section.clone(),
                        control.clone(),
                        id.clone(),
                    );
                    alert
                        .title(title.clone())
                        .description(text.clone())
                        .show_cancel(true)
                        .on_ok(move |_, window, cx| {
                            let click = Event::Click { id: id.clone() };
                            shell
                                .update(cx, |this, cx| {
                                    this.send(&addon, &section, control.clone(), click, window, cx)
                                })
                                .ok();
                            true
                        })
                });
            }
            ButtonKind::Pick {
                title,
                folders,
                multiple,
                ..
            } => {
                let rx = cx.prompt_for_paths(PathPromptOptions {
                    files: true,
                    directories: *folders,
                    multiple: *multiple,
                    prompt: Some(SharedString::from(title.clone())),
                });
                cx.spawn_in(window, async move |this, cx| {
                    if let Ok(Ok(Some(paths))) = rx.await {
                        this.update_in(cx, |s, window, cx| {
                            let event = Event::Picked { id, paths };
                            s.send(&addon, &section, control, event, window, cx)
                        })
                        .ok();
                    }
                })
                .detach();
            }
        }
    }

    pub(crate) fn drop_overlay(&self, p: Palette) -> impl IntoElement {
        div()
            .absolute()
            .inset_0()
            .p(px(16.))
            .bg(p.background.opacity(0.85))
            .child(
                v_flex()
                    .size_full()
                    .items_center()
                    .justify_center()
                    .gap(px(8.))
                    .rounded(px(10.))
                    .border_2()
                    .border_dashed()
                    .border_color(p.accent)
                    .child(badge(IconName::Upload, 44., p).text_color(p.accent))
                    .child(
                        div()
                            .pt(px(8.))
                            .text_size(px(15.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child("Drop to import"),
                    )
                    .child(
                        div()
                            .max_w(px(420.))
                            .text_center()
                            .text_size(px(13.))
                            .line_height(px(19.))
                            .text_color(p.text_muted)
                            .child("Addon folders, .swoftyaddon files, zip and rar archives, track packages and the files your addons import."),
                    ),
            )
    }

    pub(crate) fn progress_card(&self, p: Palette) -> Option<impl IntoElement> {
        let (done, text) = self.progress.clone()?;
        Some(
            v_flex()
                .absolute()
                .right(px(16.))
                .bottom(px(84.))
                .w(px(320.))
                .p(px(12.))
                .gap(px(8.))
                .rounded(px(6.))
                .border_1()
                .border_color(p.border)
                .bg(p.elevated)
                .child(div().text_size(px(13.)).truncate().child(text))
                .child(
                    div()
                        .h(px(4.))
                        .w_full()
                        .rounded_full()
                        .bg(p.border_variant)
                        .child(
                            div()
                                .h_full()
                                .w(relative(done.clamp(0., 1.)))
                                .rounded_full()
                                .bg(p.accent),
                        ),
                ),
        )
    }
}

// When each installed addon's folder last changed, by addon id.
fn installed(store: &Store) -> BTreeMap<String, SystemTime> {
    let Ok(dirs) = std::fs::read_dir(store.addons_dir()) else {
        return BTreeMap::new();
    };
    dirs.flatten()
        .filter(|e| e.path().join(swofty_core::manifest::MANIFEST_FILE).is_file())
        .filter_map(|e| {
            Some((
                e.file_name().into_string().ok()?,
                e.metadata().ok()?.modified().ok()?,
            ))
        })
        .collect()
}

// The addons installed or replaced between two snapshots, newest first.
fn installed_since(
    before: &BTreeMap<String, SystemTime>,
    after: &BTreeMap<String, SystemTime>,
) -> Vec<String> {
    let mut fresh: Vec<(&String, &SystemTime)> = after
        .iter()
        .filter(|(id, at)| before.get(*id) != Some(at))
        .collect();
    fresh.sort_by(|a, b| b.1.cmp(a.1).then_with(|| a.0.cmp(b.0)));
    fresh.into_iter().map(|(id, _)| id.clone()).collect()
}

const ADDON_DOCS: &str = "https://f1.swofty.net/reference/addons/";

fn drop_choice(
    icon: IconName,
    title: &str,
    text: &str,
    action: impl IntoElement,
    p: Palette,
) -> impl IntoElement {
    h_flex()
        .p(px(14.))
        .gap(px(14.))
        .items_start()
        .rounded(px(10.))
        .border_1()
        .border_color(p.border_variant)
        .bg(p.background)
        .child(
            div()
                .size(px(34.))
                .flex_shrink_0()
                .flex()
                .items_center()
                .justify_center()
                .rounded(px(8.))
                .bg(p.selected)
                .child(Icon::new(icon).size(px(17.)).text_color(p.accent_hover)),
        )
        .child(
            v_flex()
                .flex_1()
                .min_w_0()
                .gap(px(4.))
                .child(
                    div()
                        .text_size(px(14.))
                        .font_weight(FontWeight::SEMIBOLD)
                        .text_color(p.text)
                        .child(title.to_owned()),
                )
                .child(
                    div()
                        .text_size(px(13.))
                        .line_height(px(19.))
                        .text_color(p.text_muted)
                        .child(text.to_owned()),
                )
                .child(h_flex().pt(px(6.)).child(action)),
        )
}

fn import_all(
    store: &Store,
    registry: &Registry,
    profile: &Profile,
    install: Option<&Install>,
    paths: &[PathBuf],
    progress: &Reporter,
) -> (Vec<String>, Vec<String>, Vec<PathBuf>) {
    let (batches, unclaimed) = import::route_all(registry, paths);
    let mut done = Vec::new();
    let mut failed = Vec::new();
    let mut game_files = None;
    for (route, paths) in batches {
        match route {
            Route::Addon => {
                for path in paths {
                    for result in store.install_addons(&path) {
                        match result {
                            Ok(m) => done.push(format!("Installed {} {}", m.name, m.version)),
                            Err(e) => {
                                crate::app_log::action("install addon").file(&path).failed(&e);
                                failed.push(format!("{}: {e}", file_name(&path)))
                            }
                        }
                    }
                }
            }
            Route::Importer(id) => {
                let Some(importer) = registry.get(&id).and_then(|e| e.addon.importer()) else {
                    continue;
                };
                let game = game_files.get_or_insert_with(|| game(store, install));
                let result = host(store, &id, profile, game.as_ref()).and_then(|h| {
                    importer
                        .import(&h.cx(), &paths, progress)
                        .map_err(|e| e.to_string())
                });
                match result {
                    Ok(lines) => done.extend(lines),
                    Err(e) => {
                        crate::app_log::action("import")
                            .addon(&id)
                            .files(&paths)
                            .failed(&e);
                        failed.push(e)
                    }
                }
            }
        }
    }
    (done, failed, unclaimed)
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeMap;
    use std::time::{Duration, SystemTime};

    use swofty_core::store::Store;

    use super::{installed, installed_since};

    #[test]
    fn an_import_shows_what_it_installed_or_replaced_newest_first() {
        let t = |s: u64| SystemTime::UNIX_EPOCH + Duration::from_secs(s);
        let snap = |v: &[(&str, u64)]| -> BTreeMap<String, SystemTime> {
            v.iter().map(|(id, s)| ((*id).to_owned(), t(*s))).collect()
        };
        let before = snap(&[("a.kept", 1), ("a.replaced", 2), ("a.removed", 3)]);
        let after = snap(&[("a.kept", 1), ("a.replaced", 9), ("b.new", 8), ("c.new", 9)]);
        assert_eq!(installed_since(&before, &after), ["a.replaced", "c.new", "b.new"]);
        assert!(installed_since(&before, &before).is_empty());
    }

    #[test]
    fn snapshots_list_only_folders_with_a_manifest() {
        let root = std::env::temp_dir().join(format!("swofty-launcher-installed-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let store = Store::new(&root);
        let addon = store.addons_dir().join("t.one");
        std::fs::create_dir_all(&addon).unwrap();
        std::fs::create_dir_all(store.addons_dir().join(".importing")).unwrap();
        std::fs::write(addon.join(swofty_core::manifest::MANIFEST_FILE), "{}").unwrap();
        let before = BTreeMap::new();
        assert_eq!(installed_since(&before, &installed(&store)), ["t.one"]);
        std::fs::remove_dir_all(&root).ok();
    }
}
