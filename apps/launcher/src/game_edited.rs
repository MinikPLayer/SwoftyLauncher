use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::dialog::DialogFooter;
use gpui_kit::component::notification::Notification;
use gpui_kit::component::{Icon, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::install::Source;

use swofty_core::recovery::Blocked;
use swofty_core::stock;

use crate::discord::{BLURPLE, INVITE, LOGO};
use crate::pages::{game_art, label};
use crate::shell::{Page, Shell};
use crate::theme::{Palette, palette};

const TITLE: &str = "Your game files have been edited";
const MISSING_TITLE: &str = "Some game files are missing";

/// A blocked launch the player can fix: files that aren't Steam's copy, or
/// files Swofty did not put in the game folder.
#[derive(Clone)]
pub(crate) enum Edited {
    NotStock(stock::Report),
    Leftovers(Blocked),
}

impl Edited {
    fn files(&self) -> Vec<String> {
        match self {
            Self::NotStock(r) => r.files.clone(),
            Self::Leftovers(b) => b.files.iter().map(|f| f.path.clone()).collect(),
        }
    }

    fn leftovers(&self) -> bool {
        matches!(self, Self::Leftovers(_))
    }

    fn missing(&self) -> Option<stock::Fix> {
        match self {
            Self::NotStock(r) if r.missing => Some(r.fix()),
            _ => None,
        }
    }

    fn why(&self, repair: &Repair) -> String {
        match self {
            Self::NotStock(r) if r.missing => {
                let (gone, them) = match r.files.len() {
                    1 => ("1 game file is".to_owned(), "it"),
                    n => (format!("{n} game files are"), "them"),
                };
                let puts = match (r.fix(), r.version.as_deref()) {
                    (stock::Fix::Update, Some(v)) => format!(
                        "Your F1 25 is {v}, older than {}. Updating F1 25 puts {them} back",
                        swofty_core::known_problems::LATEST_GAME_VERSION
                    ),
                    (stock::Fix::Verify, _) => format!("{} puts {them} back", repair.restores),
                    _ => {
                        let mut then = repair.restores.chars();
                        let then = then
                            .next()
                            .map(|c| c.to_lowercase().chain(then).collect::<String>())
                            .unwrap_or_default();
                        format!("Updating F1 25, then {then}, puts {them} back")
                    }
                };
                format!(
                    "{gone} missing from the F1 25 folder, so Swofty stopped before changing anything. \
                     {puts}, and then Play works."
                )
            }
            Self::NotStock(r) => {
                let (changed, them) = match r.files.len() {
                    1 => ("1 game file was".to_owned(), "it"),
                    n => (format!("{n} game files were"), "them"),
                };
                format!(
                    "{changed} changed outside Swofty, so it stopped before changing anything. \
                     This usually happens when a mod is copied into the game folder by hand. {} \
                     restores {them}, and then Play works.",
                    repair.restores
                )
            }
            Self::Leftovers(_) => "Swofty keeps F1 25 fully restorable: it only changes files it owns and \
                                   undoes them when you quit. It found files in the F1 25 folder that it \
                                   didn't put there, so it stopped before touching anything. Files edited \
                                   by another tool or copied in by hand break that and can crash the game."
                .to_owned(),
        }
    }

    fn open(&self, repair: &Repair) -> Option<Open> {
        repair
            .open
            .filter(|_| !self.leftovers() && self.missing() != Some(stock::Fix::Update))
    }

    fn title(&self) -> &'static str {
        match self.missing() {
            Some(_) => MISSING_TITLE,
            None => TITLE,
        }
    }
}

/// The launcher's one-line notice for a launch the game files stopped.
pub(crate) fn notice(r: &stock::Report, source: Option<Source>) -> String {
    let ea = source == Some(Source::EaApp);
    match (r.missing, r.fix(), ea) {
        (true, stock::Fix::Update, _) => {
            "F1 25 is missing game files; update it in Steam or the EA app".to_owned()
        }
        (true, _, true) => "F1 25 is missing game files; repair them in the EA app".to_owned(),
        (true, _, false) => "F1 25 is missing game files; verify them in Steam".to_owned(),
        (false, _, true) => {
            "your game files aren't the ones the EA app installed; repair them in the EA app".to_owned()
        }
        (false, _, false) => {
            "your game files aren't the ones Steam installed; verify them in Steam".to_owned()
        }
    }
}

/// How the copy's store puts its own files back: Steam opens its check from
/// a link, the EA app only by hand.
#[derive(Debug, PartialEq)]
pub(crate) struct Repair {
    pub(crate) title: &'static str,
    pub(crate) how: &'static str,
    pub(crate) restores: &'static str,
    pub(crate) open: Option<Open>,
}

/// The button that starts the repair, and the step text that goes with it.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(crate) struct Open {
    pub(crate) label: &'static str,
    pub(crate) url: &'static str,
    pub(crate) how: &'static str,
}

pub(crate) fn repair(source: Source) -> Repair {
    match source {
        Source::EaApp => Repair {
            title: "Repair the game in the EA app",
            how: "EA app > Library > F1 25 > Manage > Repair, then press Play again.",
            restores: "Repairing the game in the EA app",
            open: None,
        },
        _ => Repair {
            title: "Verify the game files in Steam",
            how: "Steam > right-click F1 25 > Properties > Installed Files > Verify integrity of game \
                  files, then press Play again.",
            restores: "Verifying the game files in Steam",
            open: (source == Source::Steam).then_some(Open {
                label: "Verify game files in Steam",
                url: stock::STEAM_VERIFY_URL,
                how: "Select Verify game files in Steam below, then press Play again when Steam has \
                      finished. It's also in Steam > right-click F1 25 > Properties > Installed Files.",
            }),
        },
    }
}

fn illustration(p: Palette) -> impl IntoElement {
    let badge = div()
        .absolute()
        .bottom(px(-5.))
        .right(px(-5.))
        .size(px(26.))
        .flex()
        .items_center()
        .justify_center()
        .rounded_full()
        .bg(p.warning)
        .border_2()
        .border_color(p.surface)
        .child(Icon::new(IconName::Pencil).size(px(12.)).text_color(p.chrome));
    h_flex()
        .w_full()
        .justify_center()
        .py(px(7.))
        .rounded(px(10.))
        .bg(p.surface)
        .border_1()
        .border_color(p.border_variant)
        .child(div().relative().child(game_art(48., 48.)).child(badge))
}

fn heading(edited: &Edited, repair: &Repair, p: Palette) -> impl IntoElement {
    let why = edited.why(repair);
    v_flex()
        .items_center()
        .gap(px(6.))
        .px(px(8.))
        .child(
            div()
                .text_center()
                .text_size(px(18.))
                .line_height(px(23.))
                .font_weight(FontWeight::BOLD)
                .text_color(p.text)
                .child(edited.title()),
        )
        .child(
            div()
                .text_center()
                .text_size(px(13.))
                .line_height(px(19.))
                .text_color(p.text_muted)
                .child(why),
        )
}

fn files_box(edited: &Edited, p: Palette) -> impl IntoElement {
    let files = edited.files();
    let shown = files.len().min(6);
    v_flex()
        .gap(px(6.))
        .child(label(
            match edited.missing() {
                Some(_) => "THE MISSING FILES",
                None => "THE FILES SWOFTY FOUND",
            },
            p,
        ))
        .child(
            v_flex()
                .gap(px(2.))
                .p(px(9.))
                .rounded(px(6.))
                .bg(p.elevated)
                .border_1()
                .border_color(p.border)
                .text_size(px(12.))
                .children(files[..shown].iter().map(|f| {
                    h_flex()
                        .gap(px(7.))
                        .items_center()
                        .child(
                            Icon::new(IconName::FilePen)
                                .size(px(13.))
                                .flex_shrink_0()
                                .text_color(p.warning),
                        )
                        .child(div().min_w_0().truncate().child(f.clone()))
                }))
                .when(files.len() > shown, |d| {
                    d.child(
                        div()
                            .pl(px(21.))
                            .text_color(p.text_muted)
                            .child(format!("and {} more", files.len() - shown)),
                    )
                }),
        )
}

fn step(n: u32, title: &'static str, body: &'static str, p: Palette) -> impl IntoElement {
    h_flex()
        .gap(px(12.))
        .items_start()
        .child(
            div()
                .size(px(20.))
                .flex_shrink_0()
                .flex()
                .items_center()
                .justify_center()
                .rounded_full()
                .bg(p.selected)
                .text_size(px(12.))
                .font_weight(FontWeight::SEMIBOLD)
                .text_color(p.accent)
                .child(n.to_string()),
        )
        .child(
            v_flex()
                .flex_1()
                .min_w_0()
                .gap(px(1.))
                .child(
                    div()
                        .text_size(px(13.))
                        .font_weight(FontWeight::MEDIUM)
                        .text_color(p.text)
                        .child(title),
                )
                .child(
                    div()
                        .text_size(px(12.))
                        .line_height(px(18.))
                        .text_color(p.text_muted)
                        .child(body),
                ),
        )
}

fn steps(edited: &Edited, repair: &Repair, p: Palette) -> AnyElement {
    if let Some(fix) = edited.missing() {
        let update = step(
            1,
            "Update F1 25",
            "In Steam or the EA app, install the latest F1 25 update, then press Play again.",
            p,
        );
        let verify = |n| {
            step(
                n,
                repair.title,
                edited.open(repair).map_or(repair.how, |o| o.how),
                p,
            )
        };
        return v_flex()
            .gap(px(9.))
            .child(label("WHAT TO DO", p))
            .map(|d| match fix {
                stock::Fix::Update => d.child(update),
                stock::Fix::Verify => d.child(verify(1)),
                stock::Fix::Both => d.child(update).child(verify(2)),
            })
            .into_any_element();
    }
    let leftovers = edited.leftovers();
    let first = if leftovers { 2 } else { 1 };
    v_flex()
        .gap(px(9.))
        .child(label("WHAT TO DO", p))
        .when(leftovers, |d| {
            d.child(step(
                1,
                "A mod was copied into the game folder",
                "If you put mod files in the F1 25 folder by hand, take them back out.",
                p,
            ))
        })
        .child(step(
            first,
            repair.title,
            edited.open(repair).map_or(repair.how, |o| o.how),
            p,
        ))
        .child(step(
            first + 1,
            "Add mods the safe way",
            "Use the Simplified Importer below to add a mod as an addon Swofty installs and removes cleanly.",
            p,
        ))
        .when(leftovers, |d| {
            d.child(step(
                4,
                "Or restore the game folder",
                "Restore game folder below moves the files aside and puts the game's own files back.",
                p,
            ))
        })
        .into_any_element()
}

pub(crate) fn discord_link(id: &'static str, p: Palette) -> Stateful<Div> {
    h_flex()
        .id(id)
        .h(px(36.))
        .px(px(10.))
        .gap(px(8.))
        .items_center()
        .rounded(px(8.))
        .cursor_pointer()
        .text_size(px(13.))
        .text_color(p.text_muted)
        .hover(|s| s.bg(p.hover).text_color(p.text))
        .child(svg().data(LOGO).size(px(15.)).text_color(rgb(BLURPLE)))
        .child("Get help on Discord")
        .on_click(|_, _, cx| cx.open_url(INVITE))
}

fn footer(
    shell: WeakEntity<Shell>,
    leftovers: bool,
    importer_shown: bool,
    open: Option<Open>,
    p: Palette,
) -> DialogFooter {
    let importer = shell.clone();
    let restore = shell.clone();
    DialogFooter::new()
        .w_full()
        .justify_between()
        .child(discord_link("game-edited-discord", p))
        .child(
            h_flex()
                .gap(px(8.))
                .when(importer_shown, |d| {
                    d.child(
                        Button::new("game-edited-importer")
                            .outline()
                            .h(px(36.))
                            .px(px(14.))
                            .label("Go to Simplified Importer")
                            .on_click(move |_, window, cx| {
                                window.close_dialog(cx);
                                importer.update(cx, |s, cx| s.open_page(Page::Importer, cx)).ok();
                            }),
                    )
                })
                .when(leftovers, |d| {
                    d.child(
                        Button::new("game-edited-restore")
                            .primary()
                            .h(px(36.))
                            .px(px(14.))
                            .icon(IconName::RotateCcw)
                            .label("Restore game folder")
                            .on_click(move |_, window, cx| {
                                window.close_dialog(cx);
                                restore.update(cx, |s, cx| s.restore_game_folder(window, cx)).ok();
                            }),
                    )
                })
                .when_some(open, |d, open| {
                    d.child(
                        Button::new("game-edited-repair")
                            .primary()
                            .h(px(36.))
                            .px(px(14.))
                            .icon(IconName::ShieldCheck)
                            .label(open.label)
                            .on_click(move |_, window, cx| {
                                cx.open_url(open.url);
                                window.close_dialog(cx);
                                window.push_notification(
                                    Notification::info("Press Play again when Steam has finished.")
                                        .title("Steam is verifying F1 25"),
                                    cx,
                                );
                            }),
                    )
                })
                .child(
                    Button::new("game-edited-close")
                        .map(|b| {
                            if leftovers || open.is_some() {
                                b.ghost()
                            } else {
                                b.primary()
                            }
                        })
                        .h(px(36.))
                        .px(px(14.))
                        .label("Close")
                        .on_click(|_, window, cx| window.close_dialog(cx)),
                ),
        )
}

impl Shell {
    /// The modal for a launch stopped because the game files were edited: files
    /// that aren't Steam's copy, or files Swofty did not put in the folder. It
    /// lists the files and what to do; the NotStock case on a Steam copy also
    /// offers Verify game files in Steam, the Leftovers case Restore game
    /// folder. The play bar reopens it from [`Shell::game_edited`].
    pub(crate) fn show_game_edited(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(edited) = self.game_edited.clone() else {
            return;
        };
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        let repair = repair(self.install.as_ref().map_or(Source::Steam, |i| i.source));
        window.open_alert_dialog(cx, move |alert, window, _| {
            let (shell, edited) = (shell.clone(), edited.clone());
            let vh = f32::from(window.viewport_size().height);
            let top = ((vh - 660.) / 2.).max(12.);
            alert
                .width(px(680.))
                .margin_top(px(top))
                .child(
                    v_flex()
                        .gap(px(12.))
                        .pt(px(2.))
                        .child(illustration(p))
                        .child(heading(&edited, &repair, p))
                        .child(files_box(&edited, p))
                        .child(steps(&edited, &repair, p)),
                )
                .footer(footer(
                    shell,
                    edited.leftovers(),
                    edited.missing().is_none(),
                    edited.open(&repair),
                    p,
                ))
        });
    }
}

#[cfg(test)]
mod tests {
    use super::{Edited, notice, repair};
    use swofty_core::install::Source;
    use swofty_core::recovery::Blocked;
    use swofty_core::stock;

    fn not_stock(n: usize) -> Edited {
        let files = (0..n).map(|i| format!("2025_asset_groups/f{i}.erp")).collect();
        Edited::NotStock(stock::Report::new(files, std::path::Path::new("/games/F1 25")))
    }

    fn missing(version: Option<&str>) -> Edited {
        let files = (0..3)
            .map(|i| format!("character_package/heads/male_driver/nico_rosberg/textures/h{i}.mipmaps"))
            .collect();
        Edited::NotStock(stock::Report::missing(
            files,
            std::path::Path::new("/games/F1 25"),
            version.map(str::to_owned),
        ))
    }

    #[test]
    fn missing_files_on_the_latest_game_are_verified() {
        let r = repair(Source::Steam);
        let m = missing(Some("1.0.141.7865"));
        assert_eq!(m.title(), "Some game files are missing");
        assert_eq!(m.open(&r).unwrap().url, "steam://validate/3059520");
        let why = m.why(&r);
        assert!(
            why.starts_with("3 game files are missing from the F1 25 folder"),
            "{why}"
        );
        assert!(
            why.contains("Verifying the game files in Steam puts them back"),
            "{why}"
        );
    }

    #[test]
    fn missing_files_on_an_older_game_are_updated_first() {
        let r = repair(Source::Steam);
        let old = missing(Some("1.0.138.5601"));
        assert_eq!(old.open(&r), None);
        let why = old.why(&r);
        assert!(
            why.contains("Your F1 25 is 1.0.138.5601, older than 1.0.141.7865"),
            "{why}"
        );
        assert!(why.contains("Updating F1 25 puts them back"), "{why}");
        let unknown = missing(None);
        assert!(unknown.open(&r).is_some());
        let why = unknown.why(&r);
        assert!(
            why.contains("Updating F1 25, then verifying the game files in Steam, puts them back"),
            "{why}"
        );
        let Edited::NotStock(report) = old else {
            unreachable!()
        };
        assert_eq!(
            notice(&report, Some(Source::Steam)),
            "F1 25 is missing game files; update it in Steam or the EA app"
        );
    }

    fn leftovers() -> Edited {
        Edited::Leftovers(Blocked {
            game: "/games/F1 25".into(),
            files: Vec::new(),
        })
    }

    #[test]
    fn a_steam_copy_opens_steams_verify() {
        let r = repair(Source::Steam);
        let open = not_stock(52).open(&r).unwrap();
        assert_eq!(open.url, "steam://validate/3059520");
        assert_eq!(open.label, "Verify game files in Steam");
    }

    #[test]
    fn an_ea_app_copy_gets_the_repair_steps_and_no_button() {
        let r = repair(Source::EaApp);
        assert_eq!(r.open, None);
        assert_eq!(not_stock(52).open(&r), None);
        assert!(r.how.starts_with("EA app > Library > F1 25 > Manage > Repair"));
        assert!(
            not_stock(52)
                .why(&r)
                .contains("Repairing the game in the EA app restores them")
        );
    }

    #[test]
    fn a_folder_outside_steam_gets_no_button() {
        assert_eq!(repair(Source::Unknown).open, None);
    }

    #[test]
    fn files_in_the_way_offer_restore_not_verify() {
        assert_eq!(leftovers().open(&repair(Source::Steam)), None);
    }

    #[test]
    fn the_message_counts_the_files_and_names_the_cause() {
        let r = repair(Source::Steam);
        let why = not_stock(52).why(&r);
        assert!(
            why.starts_with("52 game files were changed outside Swofty"),
            "{why}"
        );
        assert!(
            why.contains("a mod is copied into the game folder by hand"),
            "{why}"
        );
        assert!(
            why.contains("Verifying the game files in Steam restores them, and then Play works."),
            "{why}"
        );
        let one = not_stock(1).why(&r);
        assert!(one.starts_with("1 game file was changed"), "{one}");
        assert!(one.contains("restores it,"), "{one}");
    }
}
