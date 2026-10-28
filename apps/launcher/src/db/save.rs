use std::cell::Cell;
use std::path::PathBuf;
use std::rc::Rc;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariant, ButtonVariants};
use gpui_kit::component::dialog::DialogFooter;
use gpui_kit::component::input::{Input, InputState};
use gpui_kit::component::menu::{DropdownMenu, PopupMenuItem};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::{Disableable, Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::dbedit::draft::Details;
use swofty_core::dbedit::package::{self, Package};
use swofty_core::manifest::Category;
use swofty_core::store::Store;

use super::{Then, guarded, text};
use crate::library::plural;
use crate::shell::Shell;
use crate::theme::{Palette, palette};

/// The author last typed, else one from an installed DB Editor addon; never
/// the OS user name.
pub(crate) fn default_author(store: &Store, last: Option<&str>) -> String {
    if let Some(a) = last.filter(|a| !a.trim().is_empty()) {
        return a.to_owned();
    }
    package::list(store)
        .unwrap_or_default()
        .into_iter()
        .map(|p| p.author)
        .find(|a| a != package::DEFAULT_AUTHOR && !a.trim().is_empty())
        .unwrap_or_default()
}

fn field(title: &'static str, input: &Entity<InputState>, p: Palette) -> Div {
    v_flex()
        .gap(px(6.))
        .child(caption(title, p))
        .child(Input::new(input).small())
}

fn caption(title: &'static str, p: Palette) -> Div {
    div()
        .text_size(px(12.))
        .font_weight(FontWeight::MEDIUM)
        .text_color(p.text_muted)
        .child(title)
}

fn category_label(c: Option<Category>) -> &'static str {
    c.map_or("Uncategorised", Category::label)
}

fn category_picker(chosen: Rc<Cell<Option<Category>>>, p: Palette) -> Div {
    let current = chosen.get();
    v_flex().gap(px(6.)).child(caption("CATEGORY", p)).child(
        Button::new("db-save-category")
            .outline()
            .small()
            .w_full()
            .label(category_label(current))
            .dropdown_caret(true)
            .dropdown_menu_with_anchor(Anchor::TopLeft, move |mut menu, _, _| {
                menu = menu.min_w(px(220.)).max_h(px(360.)).scrollable(true);
                let packages = Category::ALL.into_iter().filter(|c| *c != Category::Shaders);
                for c in packages.map(Some).chain([None]) {
                    let chosen = chosen.clone();
                    menu = menu.item(
                        PopupMenuItem::new(category_label(c))
                            .checked(c == current)
                            .on_click(move |_, window, _| {
                                chosen.set(c);
                                window.refresh();
                            }),
                    );
                }
                menu
            }),
    )
}

#[derive(Clone)]
struct Form {
    name: Entity<InputState>,
    version: Option<Entity<InputState>>,
    author: Entity<InputState>,
    description: Entity<InputState>,
    category: Rc<Cell<Option<Category>>>,
}

impl Form {
    fn read(&self, cx: &App) -> [String; 4] {
        let v = |i: &Entity<InputState>| i.read(cx).value().trim().to_owned();
        [
            v(&self.name),
            self.version.as_ref().map(v).unwrap_or_default(),
            v(&self.author),
            v(&self.description),
        ]
    }
}

impl Shell {
    fn db_form(&self, existing: Option<&Package>, window: &mut Window, cx: &mut Context<Self>) -> Form {
        let mut input = |value: String, placeholder: &'static str, cx: &mut Context<Self>| {
            cx.new(|cx| {
                InputState::new(window, cx)
                    .placeholder(placeholder)
                    .default_value(value)
            })
        };
        let m = existing.map(|p| &p.manifest);
        let name = m.map_or_else(
            || self.db.draft.name.clone().unwrap_or_default(),
            |m| m.name.clone(),
        );
        let author = m.map_or_else(
            || default_author(&self.store, self.db.draft.author.as_deref()),
            |m| m.author.clone(),
        );
        Form {
            name: input(name, "e.g. Faster pit stops", cx),
            version: m.map(|m| input(m.version.to_string(), "1.0.0", cx)),
            author: input(author, "Your name", cx),
            description: input(
                m.and_then(|m| m.description.clone()).unwrap_or_default(),
                "What the addon changes",
                cx,
            ),
            category: Rc::new(Cell::new(m.and_then(|m| m.category))),
        }
    }

    /// The header's Save (an addon) or Save as Addon (a new draft).
    pub(crate) fn db_save(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.db.draft.addon.is_some() {
            self.db_save_bound(None, window, cx);
        } else {
            self.db_save_as(None, window, cx);
        }
    }

    pub(crate) fn db_save_as(&mut self, then: Option<Then>, window: &mut Window, cx: &mut Context<Self>) {
        if self.db.draft.edits.is_empty() {
            window.push_notification(
                Notification::info("Change a value first, then save it as an addon."),
                cx,
            );
            return;
        }
        let form = self.db_form(None, window, cx);
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        let focus = Cell::new(true);
        let n = self.db.draft.edits.len();
        let note = if self.vanilla() {
            "Vanilla stays stock: turn the addon on in another profile to use it.".to_owned()
        } else {
            format!(
                "{} {} an addon, turned on in {}.",
                plural(n, "change", "changes"),
                if n == 1 { "becomes" } else { "become" },
                self.library.profile.name
            )
        };
        window.open_alert_dialog(cx, move |alert, window, cx| {
            if focus.replace(false) {
                form.name.update(cx, |s, cx| s.focus(window, cx));
            }
            let (shell, fields, then) = (shell.clone(), form.clone(), then.clone());
            alert
                .width(px(520.))
                .title("Save as Addon")
                .child(
                    v_flex()
                        .gap(px(14.))
                        .child(
                            div()
                                .text_size(px(13.))
                                .text_color(p.text_muted)
                                .child(note.clone()),
                        )
                        .child(field("NAME", &form.name, p))
                        .child(
                            h_flex()
                                .gap(px(12.))
                                .child(div().flex_1().child(field("AUTHOR", &form.author, p)))
                                .child(div().w(px(190.)).child(category_picker(form.category.clone(), p))),
                        )
                        .child(field("DESCRIPTION", &form.description, p)),
                )
                .ok_text("Save as Addon")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    let [name, _, author, description] = fields.read(cx);
                    if name.is_empty() {
                        window.push_notification(Notification::error("Give the addon a name."), cx);
                        return false;
                    }
                    let details = Details {
                        name,
                        author,
                        category: fields.category.get(),
                        description,
                    };
                    shell
                        .update(cx, |s, cx| s.db_save_new(&details, then.clone(), window, cx))
                        .unwrap_or(false)
                })
        });
    }

    fn db_save_new(
        &mut self,
        details: &Details,
        then: Option<Then>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) -> bool {
        let Some(catalog) = self.db.catalog().cloned() else {
            return false;
        };
        let store = self.store.clone();
        let draft = &mut self.db.draft;
        let pkg = match guarded(|| draft.save_as_addon(&store, &catalog, details).map_err(text)) {
            Ok(p) => p,
            Err(e) => {
                crate::app_log::action("db editor: save the addon").failed(&e);
                window.push_notification(Notification::error(e).title("Could not save the addon"), cx);
                return false;
            }
        };
        let profile = self.library.profile.name.clone();
        let on = !self.vanilla() && self.db_turn_on(&pkg.id).is_ok();
        let name = &details.name;
        window.push_notification(
            Notification::success(if on {
                format!("Saved {name} and turned it on in {profile}")
            } else {
                format!("Saved {name}")
            }),
            cx,
        );
        self.db_after_save(pkg.id, then, cx);
        true
    }

    fn db_save_bound(&mut self, then: Option<Then>, window: &mut Window, cx: &mut Context<Self>) -> bool {
        let Some(catalog) = self.db.catalog().cloned() else {
            return false;
        };
        if let Some(e) = self.db.addon_error.clone() {
            crate::app_log::action("db editor: save").failed(&e);
            window.push_notification(Notification::error(e).title("Could not save"), cx);
            return false;
        }
        let name = self.db_draft_name();
        let store = self.store.clone();
        let draft = &mut self.db.draft;
        match guarded(|| draft.save_addon(&store, &catalog).map_err(text)) {
            Ok(pkg) => {
                window.push_notification(Notification::success(format!("Saved {name}")), cx);
                self.db_after_save(pkg.id, then, cx);
                true
            }
            Err(e) => {
                crate::app_log::action("db editor: save").failed(&e);
                window.push_notification(Notification::error(e).title(format!("Could not save {name}")), cx);
                false
            }
        }
    }

    // A saved draft is cleared; the addon shows on Addons unless the save was on the way elsewhere.
    fn db_after_save(&mut self, id: String, then: Option<Then>, cx: &mut Context<Self>) {
        self.db.addon_error = None;
        self.db.seen = None;
        self.db_persist(cx);
        self.reload(cx);
        match then {
            Some(t) => self.db_then(t, cx),
            None => {
                self.db_changed(None, false, cx);
                self.show_addon(&id, cx);
            }
        }
    }

    pub(crate) fn db_discard(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let n = self.db.draft.unsaved();
        if n == 0 {
            return;
        }
        let shell = cx.entity().downgrade();
        let body = match self.db_bound() {
            Some(info) => format!("This puts {} back to how it was last saved.", info.name),
            None => "The new addon goes back to no changes.".to_owned(),
        };
        window.open_alert_dialog(cx, move |alert, _, _| {
            let shell = shell.clone();
            alert
                .title(format!(
                    "Discard {}?",
                    plural(n, "unsaved change", "unsaved changes")
                ))
                .description(format!("{body} This can't be undone."))
                .ok_text("Discard changes")
                .ok_variant(ButtonVariant::Danger)
                .show_cancel(true)
                .on_ok(move |_, _, cx| {
                    shell
                        .update(cx, |s, cx| {
                            s.db.draft.discard();
                            s.db.errors.clear();
                            s.db.slots = super::Slots::default();
                            s.db_changed(None, true, cx);
                        })
                        .ok();
                    true
                })
        });
    }

    /// Runs `then`, first asking Save / Discard / Cancel when the draft has unsaved changes.
    pub(crate) fn db_leave(&mut self, then: Then, window: &mut Window, cx: &mut Context<Self>) {
        let n = self.db.draft.unsaved();
        if n == 0 {
            self.db_then(then, cx);
            return;
        }
        let name = self.db_draft_name();
        let bound = self.db.draft.addon.is_some();
        let shell = cx.entity().downgrade();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (s1, s2, t1, t2) = (shell.clone(), shell.clone(), then.clone(), then.clone());
            let footer = DialogFooter::new()
                .child(
                    Button::new("db-leave-cancel")
                        .outline()
                        .label("Cancel")
                        .on_click(|_, window, cx| window.close_dialog(cx)),
                )
                .child(
                    Button::new("db-leave-discard")
                        .outline()
                        .label("Discard")
                        .on_click(move |_, window, cx| {
                            window.close_dialog(cx);
                            s1.update(cx, |s, cx| {
                                s.db.draft.discard();
                                s.db_then(t1.clone(), cx);
                            })
                            .ok();
                        }),
                )
                .child(
                    Button::new("db-leave-save")
                        .primary()
                        .label(if bound { "Save" } else { "Save as Addon…" })
                        .on_click(move |_, window, cx| {
                            window.close_dialog(cx);
                            s2.update(cx, |s, cx| {
                                if bound {
                                    s.db_save_bound(Some(t2.clone()), window, cx);
                                } else {
                                    s.db_save_as(Some(t2.clone()), window, cx);
                                }
                            })
                            .ok();
                        }),
                );
            alert
                .width(px(480.))
                .title(format!("Save changes to {name}?"))
                .description(format!(
                    "You have {}.",
                    plural(n, "unsaved change", "unsaved changes")
                ))
                .footer(footer)
        });
    }

    pub(crate) fn db_new_draft(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.db_leave(Then::New, window, cx);
    }

    /// Name, version, author, category and description of the draft's addon.
    pub(crate) fn db_details(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(id) = self.db.draft.addon.clone() else {
            return;
        };
        let pkg = match guarded(|| Package::open(&self.store, &id).map_err(text)) {
            Ok(p) => p,
            Err(e) => {
                crate::app_log::action("db editor: read the addon").failed(&e);
                window.push_notification(Notification::error(e).title("Could not read the addon"), cx);
                return;
            }
        };
        let form = self.db_form(Some(&pkg), window, cx);
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        let title = format!("{} details", pkg.manifest.name);
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (shell, fields, id) = (shell.clone(), form.clone(), id.clone());
            alert
                .width(px(560.))
                .title(title.clone())
                .child(
                    v_flex()
                        .gap(px(14.))
                        .child(field("NAME", &form.name, p))
                        .child(
                            h_flex()
                                .gap(px(12.))
                                .child(
                                    div()
                                        .w(px(110.))
                                        .children(form.version.as_ref().map(|v| field("VERSION", v, p))),
                                )
                                .child(div().flex_1().child(field("AUTHOR", &form.author, p)))
                                .child(div().w(px(170.)).child(category_picker(form.category.clone(), p))),
                        )
                        .child(field("DESCRIPTION", &form.description, p))
                        .child(
                            div()
                                .text_size(px(12.))
                                .text_color(p.text_placeholder)
                                .child(format!("ID {id} · versions look like 1.2.0")),
                        ),
                )
                .ok_text("Save details")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    let [name, version, author, description] = fields.read(cx);
                    let category = fields.category.get();
                    let result = shell
                        .update(cx, |s, cx| {
                            let store = s.store.clone();
                            let r = guarded(|| {
                                let mut pkg = Package::open(&store, &id).map_err(text)?;
                                pkg.set_details(&name, &version, &author, &description)
                                    .map_err(text)?;
                                pkg.set_category(category);
                                pkg.write(&store).map_err(text)
                            });
                            if r.is_ok() {
                                if !author.is_empty() {
                                    s.db.draft.author = Some(author.clone());
                                }
                                s.db_quiet_reload(cx);
                                s.reload(cx);
                            }
                            r
                        })
                        .unwrap_or_else(|_| Err("The launcher is closing".into()));
                    match result {
                        Ok(()) => {
                            window.push_notification(Notification::success(format!("Saved {name}")), cx);
                            true
                        }
                        Err(e) => {
                            crate::app_log::action("db editor: save the details").failed(&e);
                            window.push_notification(
                                Notification::error(e).title("Could not save the details"),
                                cx,
                            );
                            false
                        }
                    }
                })
        });
    }

    /// "Share addon (.zip)" and "Export game files (.zip)".
    pub(crate) fn db_export_menu(&self, cx: &mut Context<Self>) -> impl IntoElement {
        let shell = cx.entity().downgrade();
        let share = self.db.draft.addon.is_some() && !self.db_locked();
        let game = !self.db.draft.edits.is_empty() || !self.db_stack().is_empty();
        Button::new("db-export")
            .outline()
            .icon(IconName::FileUp)
            .label("Export")
            .dropdown_caret(true)
            .disabled(!(share || game) || self.db.catalog().is_none())
            .dropdown_menu_with_anchor(Anchor::TopRight, move |menu, _, _| {
                let (s1, s2) = (shell.clone(), shell.clone());
                menu.min_w(px(240.))
                    .item(
                        PopupMenuItem::new("Share addon (.zip)…")
                            .icon(IconName::Package)
                            .disabled(!share)
                            .on_click(move |_, window, cx| {
                                s1.update(cx, |s, cx| s.db_export_addon(window, cx)).ok();
                            }),
                    )
                    .item(
                        PopupMenuItem::new("Export game files (.zip)…")
                            .icon(IconName::FileArchive)
                            .disabled(!game)
                            .on_click(move |_, window, cx| {
                                s2.update(cx, |s, cx| s.db_game_export(window, cx)).ok();
                            }),
                    )
            })
    }

    pub(crate) fn db_export_addon(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(id) = self.db.draft.addon.clone() else {
            return;
        };
        let name = self.db_draft_name();
        let unsaved = self.db.draft.unsaved();
        let file = format!("{}.zip", id.trim_start_matches(package::ID_PREFIX));
        let store = self.store.clone();
        let export = move |path: PathBuf, window: &mut Window, cx: &mut App| {
            let (store, id) = (store.clone(), id.clone());
            let target = path.clone();
            let task = cx
                .background_executor()
                .spawn(async move { guarded(|| Package::export_zip(&store, &id, &target).map_err(text)) });
            let name = name.clone();
            window
                .spawn(cx, async move |cx| {
                    let result = task.await;
                    cx.update(|window, cx| match result {
                        Ok(()) => {
                            let mut body = format!("Saved to {}", path.display());
                            if unsaved > 0 {
                                body.push_str(&format!(
                                    "\n{} aren't in it; save, then export again to include them.",
                                    plural(unsaved, "unsaved change", "unsaved changes")
                                ));
                            }
                            window.push_notification(
                                Notification::success(body).title(format!("Exported {name}")),
                                cx,
                            );
                        }
                        Err(e) => {
                            crate::app_log::action("db editor: export").failed(&e);
                            window.push_notification(Notification::error(e).title("Export failed"), cx)
                        }
                    })
                    .ok();
                })
                .detach();
        };
        if let Some(dir) = super::debug_path("SWOFTY_DEBUG_EXPORT") {
            export(dir.join(&file), window, cx);
            return;
        }
        let rx = cx.prompt_for_new_path(&super::home(&self.store), Some(&file));
        cx.spawn_in(window, async move |_, cx| {
            if let Ok(Ok(Some(path))) = rx.await {
                cx.update(|window, cx| export(path, window, cx)).ok();
            }
        })
        .detach();
    }
}

const GAME_FILES_NOTE: &str = "Players extract this .zip into the F1 25 folder. Steam's Verify integrity of game files \
                               undoes it, and Swofty Launcher won't start modded sessions while that file is \
                               replaced. Players who use Swofty Launcher should get the addon .zip instead.";

fn choice_row(
    id: &'static str,
    on: bool,
    title: String,
    detail: String,
    enabled: bool,
    p: Palette,
) -> Stateful<Div> {
    h_flex()
        .id(id)
        .gap(px(10.))
        .px(px(12.))
        .py(px(9.))
        .rounded(px(6.))
        .border_1()
        .border_color(if on { p.accent } else { p.border_variant })
        .when(on, |d| d.bg(p.accent.opacity(0.08)))
        .when(enabled, |d| d.cursor_pointer())
        .when(!enabled, |d| d.opacity(0.5))
        .child(
            Icon::new(if on {
                IconName::CircleCheck
            } else {
                IconName::Circle
            })
            .size(px(15.))
            .text_color(if on { p.accent } else { p.text_placeholder }),
        )
        .child(
            v_flex()
                .flex_1()
                .min_w_0()
                .child(
                    div()
                        .text_size(px(13.))
                        .font_weight(FontWeight::MEDIUM)
                        .child(title),
                )
                .child(div().text_size(px(12.)).text_color(p.text_muted).child(detail)),
        )
}

/// What a game-files export applies.
#[derive(Clone)]
enum Pick {
    Draft(Box<Package>),
    Addons(Vec<String>),
}

impl Shell {
    /// Builds a game database with the draft or the profile's DB Editor addons
    /// applied and saves it as a .zip of game files.
    pub(crate) fn db_game_export(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let name = self
            .db_bound()
            .map_or_else(|| "DB Editor changes".to_owned(), |i| i.name.clone());
        let n = self.db.draft.edits.len();
        let draft = (n > 0)
            .then(|| guarded(|| Package::unsaved(&name, self.db.draft.edits.clone()).map_err(text)).ok())
            .flatten();
        let profile = self.library.profile.name.clone();
        let stack = guarded(|| package::enabled_packages(&self.store, &profile).map_err(text))
            .unwrap_or_else(|_| self.db_stack());
        let every = Rc::new(Cell::new(draft.is_none()));
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        let file_stem = self
            .db
            .draft
            .addon
            .as_deref()
            .map_or("db-editor", |id| id.trim_start_matches(package::ID_PREFIX))
            .to_owned();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let all = every.get();
            let (shell, stack2, draft2, every2) =
                (shell.clone(), stack.clone(), draft.clone(), every.clone());
            let single = draft.as_ref().map(|_| {
                let every = every.clone();
                choice_row(
                    "db-ge-one",
                    !all,
                    "The changes in the DB Editor".to_owned(),
                    format!("{}, saved or not", plural(n, "change", "changes")),
                    true,
                    p,
                )
                .on_click(move |_, window, _| {
                    every.set(false);
                    window.refresh();
                })
            });
            let stacked = {
                let every = every.clone();
                let enabled = !stack.is_empty();
                choice_row(
                    "db-ge-all",
                    all,
                    format!("Every DB Editor addon on in {profile}"),
                    if enabled {
                        format!("{} in load order", plural(stack.len(), "addon", "addons"))
                    } else {
                        format!("No DB Editor addons are on in {profile}")
                    },
                    enabled,
                    p,
                )
                .when(enabled, |d| {
                    d.on_click(move |_, window, _| {
                        every.set(true);
                        window.refresh();
                    })
                })
            };
            let ready = if all { !stack.is_empty() } else { draft.is_some() };
            let stem = file_stem.clone();
            alert
                .width(px(540.))
                .title("Export game files")
                .child(
                    v_flex()
                        .gap(px(10.))
                        .child(div().text_size(px(13.)).text_color(p.text_muted).child(
                            "Saves a .zip with the game's database file, changed by the addons you pick.",
                        ))
                        .children(single)
                        .child(stacked)
                        .child(
                            h_flex()
                                .mt(px(4.))
                                .gap(px(8.))
                                .items_start()
                                .px(px(10.))
                                .py(px(8.))
                                .rounded(px(6.))
                                .bg(p.warning.opacity(0.1))
                                .text_size(px(12.))
                                .line_height(px(18.))
                                .child(
                                    Icon::new(IconName::TriangleAlert)
                                        .size(px(13.))
                                        .mt(px(2.))
                                        .text_color(p.warning),
                                )
                                .child(div().flex_1().min_w_0().child(GAME_FILES_NOTE)),
                        ),
                )
                .ok_text("Export…")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    if !ready {
                        return false;
                    }
                    let (pick, file) = match (every2.get(), &draft2) {
                        (false, Some(d)) => {
                            (Pick::Draft(Box::new(d.clone())), format!("{stem}-game-files.zip"))
                        }
                        _ => (
                            Pick::Addons(stack2.clone()),
                            "db-editor-addons-game-files.zip".to_owned(),
                        ),
                    };
                    shell
                        .update(cx, |s, cx| s.db_game_export_to(pick, file, window, cx))
                        .ok();
                    true
                })
        });
    }

    fn db_game_export_to(&mut self, pick: Pick, file: String, window: &mut Window, cx: &mut Context<Self>) {
        let store = self.store.clone();
        let Some(game) = crate::sections::game(&store, self.install.as_ref()) else {
            window.push_notification(
                Notification::error("F1 25 was not found.").title("Export failed"),
                cx,
            );
            return;
        };
        let run = move |out: PathBuf, window: &mut Window, cx: &mut Context<Self>| {
            window.push_notification(Notification::info("Building the game files…"), cx);
            let (store, game, pick) = (store.clone(), game.clone(), pick.clone());
            cx.spawn_in(window, async move |_, cx| {
                let target = out.clone();
                let result = cx
                    .background_executor()
                    .spawn(async move {
                        guarded(|| {
                            match pick {
                                Pick::Draft(p) => package::export_packages(&store, &game, &[*p], &target),
                                Pick::Addons(ids) => package::export_game_files(&store, &game, &ids, &target),
                            }
                            .map_err(text)
                        })
                    })
                    .await;
                cx.update(|window, cx| match result {
                    Ok(done) => {
                        let mut body = format!(
                            "{} · {:.1} MB · saved to {}",
                            plural(done.changes, "change", "changes"),
                            done.bytes as f64 / 1_048_576.,
                            out.display()
                        );
                        if !done.skipped.is_empty() {
                            body.push_str(&format!("\n{} skipped:", done.skipped.len()));
                            for line in done.skipped.iter().take(4) {
                                body.push_str(&format!("\n{line}"));
                            }
                        }
                        for line in &done.warnings {
                            body.push_str(&format!("\n{line}"));
                        }
                        let note = match done.warnings.is_empty() {
                            true => Notification::success(body),
                            false => Notification::warning(body).autohide(false),
                        };
                        window.push_notification(note.title("Exported game files"), cx);
                    }
                    Err(e) => {
                        crate::app_log::action("db editor: export").failed(&e);
                        window.push_notification(Notification::error(e).title("Export failed"), cx)
                    }
                })
                .ok();
            })
            .detach();
        };
        if let Some(dir) = super::debug_path("SWOFTY_DEBUG_EXPORT") {
            run(dir.join(&file), window, cx);
            return;
        }
        let rx = cx.prompt_for_new_path(&super::home(&self.store), Some(&file));
        cx.spawn_in(window, async move |this, cx| {
            if let Ok(Ok(Some(path))) = rx.await {
                this.update_in(cx, |_, window, cx| run(path, window, cx)).ok();
            }
        })
        .detach();
    }
}
