use std::cell::Cell;
use std::ops::Range;
use std::sync::Arc;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::input::{Input, InputState};
use gpui_kit::component::menu::{DropdownMenu, PopupMenuItem};
use gpui_kit::component::{Disableable, Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::dbedit::{Catalog, Edit, NodeId, NodeKind, Origin, RowCopy};

use super::{ADDABLE, Nav, hex, type_name};
use crate::library::plural;
use crate::pages::label;
use crate::shell::Shell;
use crate::theme::{Palette, palette, scrollbar_x};

const LIST_H: f32 = 44.;
const GRID_COL: f32 = 150.;
const GRID_LABEL: f32 = 200.;
const GRID_HEADER: f32 = 32.;

fn kind_icon(kind: &NodeKind) -> IconName {
    match kind {
        NodeKind::Root | NodeKind::Container => IconName::Folder,
        NodeKind::Table { .. } => IconName::Table2,
        NodeKind::Row { .. } => IconName::Rows3,
        NodeKind::Key => IconName::KeyRound,
        NodeKind::Unnamed => IconName::Binary,
    }
}

fn icon_tile(icon: IconName, p: Palette) -> Div {
    div()
        .size(px(26.))
        .flex_shrink_0()
        .rounded(px(5.))
        .bg(p.elevated)
        .flex()
        .items_center()
        .justify_center()
        .child(Icon::new(icon).size(px(13.)).text_color(p.text_muted))
}

// The row a key belongs to and the key's path under it, for a friendly name.
fn friendly(catalog: &Catalog, node: NodeId) -> (String, Option<NodeId>) {
    let mut parts = vec![catalog.node(node).name.clone()];
    let mut at = catalog.node(node).parent;
    while let Some(id) = at {
        let n = catalog.node(id);
        if matches!(n.kind, NodeKind::Row { .. }) {
            let row = n
                .display
                .clone()
                .or_else(|| n.label.clone())
                .unwrap_or_else(|| n.name.clone());
            parts.reverse();
            return (format!("{row} › {}", parts.join(".")), Some(id));
        }
        if matches!(n.kind, NodeKind::Root | NodeKind::Unnamed) {
            break;
        }
        parts.push(n.name.clone());
        at = n.parent;
    }
    parts.reverse();
    (parts.join("."), None)
}

fn table_of(catalog: &Catalog, node: NodeId) -> Option<NodeId> {
    let mut at = catalog.node(node).parent;
    while let Some(id) = at {
        if matches!(catalog.node(id).kind, NodeKind::Table { .. }) {
            return Some(id);
        }
        at = catalog.node(id).parent;
    }
    None
}

fn shape(path: &str) -> String {
    let mut out = String::new();
    let mut depth = 0;
    for c in path.chars() {
        match c {
            '[' => depth += 1,
            ']' => depth -= 1,
            _ if depth == 0 => out.push(c),
            _ => {}
        }
    }
    out.split('.')
        .map(|seg| match seg.rsplit_once('_') {
            Some((head, n)) if !n.is_empty() && n.bytes().all(|b| b.is_ascii_digit()) => head,
            _ => seg,
        })
        .collect::<Vec<_>>()
        .join(".")
}

fn title_case(word: &str) -> String {
    word.split(['_', '-'])
        .filter(|w| !w.is_empty())
        .map(|w| {
            let mut c = w.chars();
            c.next()
                .map(|f| f.to_uppercase().chain(c).collect::<String>())
                .unwrap_or_default()
        })
        .collect::<Vec<_>>()
        .join(" ")
}

const COPIED: &str = "Copied rows";

pub(crate) struct ChangeItem {
    pub group: Option<String>,
    pub edit: Option<Edit>,
    pub row: Option<RowCopy>,
    pub name: String,
    pub path: Option<String>,
    pub node: Option<NodeId>,
}

impl Shell {
    fn db_crumbs(&self, node: NodeId, p: Palette, cx: &mut Context<Self>) -> impl IntoElement {
        let Some(catalog) = self.db.catalog().cloned() else {
            return h_flex();
        };
        let mut chain = vec![node];
        let mut at = catalog.node(node).parent;
        while let Some(id) = at {
            chain.push(id);
            at = catalog.node(id).parent;
        }
        chain.reverse();
        let last = chain.len() - 1;
        h_flex()
            .flex_1()
            .min_w_0()
            .gap(px(4.))
            .flex_wrap()
            .children(chain.into_iter().enumerate().map(|(i, id)| {
                let name = if id == catalog.root() {
                    "All tables".to_owned()
                } else {
                    catalog.node(id).name.clone()
                };
                h_flex()
                    .gap(px(4.))
                    .when(i > 0, |d| {
                        d.child(
                            Icon::new(IconName::ChevronRight)
                                .size(px(12.))
                                .text_color(p.text_placeholder),
                        )
                    })
                    .child(
                        div()
                            .id(SharedString::from(format!("db-crumb-{i}")))
                            .px(px(6.))
                            .py(px(2.))
                            .rounded(px(4.))
                            .text_size(px(13.))
                            .map(|d| {
                                if i == last {
                                    d.font_weight(FontWeight::SEMIBOLD).text_color(p.text)
                                } else {
                                    d.cursor_pointer()
                                        .text_color(p.text_muted)
                                        .hover(move |s| s.bg(p.hover).text_color(p.text))
                                }
                            })
                            .child(name)
                            .on_click(cx.listener(move |this, _, _, cx| this.db_go(Nav::Raw(id), cx))),
                    )
            }))
    }

    pub(crate) fn db_raw(&mut self, node: NodeId, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let Some(catalog) = self.db.catalog().cloned() else {
            return div().into_any_element();
        };
        let n = catalog.node(node).clone();
        let kids = catalog.children(node).len();
        let rows = self.db_raw_rows(node, &catalog);
        let filtered = rows.len() != kids;
        let summary = match &n.kind {
            NodeKind::Root => format!(
                "{} records · {} named · {}",
                catalog.records(),
                catalog.named(),
                plural(kids, "top-level part", "top-level parts")
            ),
            NodeKind::Table { rows, .. } => {
                let fields = catalog.fields(node).len();
                let meaning = catalog.table_meaning(node).unwrap_or_default();
                let mut s = format!(
                    "Table · {} × {}",
                    plural(*rows as usize, "row", "rows"),
                    plural(fields, "field", "fields")
                );
                if !meaning.is_empty() {
                    s.push_str(&format!(" · {meaning}"));
                }
                s
            }
            NodeKind::Row { index } => {
                let mut s = format!("Row {index}");
                if let Some(l) = &n.label {
                    s.push_str(&format!(" · {l}"));
                }
                if let Some(d) = &n.display {
                    s.push_str(&format!(" · {d}"));
                }
                format!("{s} · {}", plural(kids, "entry", "entries"))
            }
            _ => plural(kids, "entry", "entries"),
        };
        let add = Button::new("db-add-key")
            .outline()
            .small()
            .icon(IconName::Plus)
            .label("Add key…")
            .disabled(self.db_locked())
            .on_click(cx.listener(move |this, _, window, cx| this.db_add_dialog(node, window, cx)));
        let content = match n.kind {
            _ if rows.is_empty() && filtered => self.empty(
                IconName::Search,
                "No matches",
                "Nothing at this level matches the filter.",
                div(),
                p,
            ),
            NodeKind::Table { .. } => self.db_grid(node, rows.clone(), &catalog, p, cx),
            _ if kids == 0 => self.empty(
                IconName::Folder,
                "Empty",
                "Nothing is stored under this part.",
                div(),
                p,
            ),
            _ => {
                let shown = rows.clone();
                let list = uniform_list(
                    "db-children",
                    rows.len(),
                    cx.processor(move |this, range: Range<usize>, _, cx| this.db_children(&shown, range, cx)),
                )
                .flex_1()
                .track_scroll(&self.db.list_scroll);
                v_flex()
                    .size_full()
                    .rounded(px(6.))
                    .border_1()
                    .border_color(p.border_variant)
                    .overflow_hidden()
                    .child(self.db.list_motion.wrap(list, &self.db.list_scroll, cx))
                    .into_any_element()
            }
        };
        let detail = self.db.key.map(|k| self.db_key_detail(k, p, cx));
        v_flex()
            .size_full()
            .gap(px(12.))
            .child(
                h_flex()
                    .gap(px(12.))
                    .flex_shrink_0()
                    .child(self.db_crumbs(node, p, cx))
                    .children(self.db.raw_filter.as_ref().map(|d| {
                        div().w(px(240.)).flex_shrink_0().child(
                            Input::new(&d.input).small().cleanable(true).prefix(
                                Icon::new(IconName::ListFilter)
                                    .size(px(14.))
                                    .text_color(p.text_placeholder),
                            ),
                        )
                    }))
                    .child(add),
            )
            .child(
                div()
                    .flex_shrink_0()
                    .text_size(px(12.))
                    .text_color(p.text_muted)
                    .truncate()
                    .child(summary),
            )
            .child(
                h_flex()
                    .flex_1()
                    .min_h_0()
                    .gap(px(16.))
                    .items_start()
                    .child(div().flex_1().min_w_0().h_full().flex().flex_col().child(content))
                    .children(detail),
            )
            .into_any_element()
    }

    fn db_raw_rows(&mut self, node: NodeId, catalog: &Arc<Catalog>) -> Arc<Vec<NodeId>> {
        let query = self
            .db
            .raw_filter
            .as_ref()
            .map(|d| d.query.trim().to_lowercase())
            .unwrap_or_default();
        if let Some((n, q, rows)) = &self.db.raw_rows
            && *n == node
            && *q == query
        {
            return rows.clone();
        }
        let rows: Vec<NodeId> = catalog
            .children(node)
            .iter()
            .copied()
            .filter(|&c| {
                let n = catalog.node(c);
                query.is_empty()
                    || n.name.to_lowercase().contains(&query)
                    || n.label
                        .as_ref()
                        .is_some_and(|l| l.to_lowercase().contains(&query))
                    || n.display
                        .as_ref()
                        .is_some_and(|d| d.to_lowercase().contains(&query))
            })
            .collect();
        let rows = Arc::new(rows);
        self.db.raw_rows = Some((node, query, rows.clone()));
        rows
    }

    fn db_children(
        &mut self,
        kids: &[NodeId],
        range: Range<usize>,
        cx: &mut Context<Self>,
    ) -> Vec<AnyElement> {
        let p = palette(cx);
        let Some(catalog) = self.db.catalog().cloned() else {
            return Vec::new();
        };
        range
            .filter_map(|i| kids.get(i).map(|&c| (i, c)))
            .map(|(i, child)| self.db_entry(&catalog, i, child, true, p, cx))
            .collect()
    }

    fn db_entry(
        &self,
        catalog: &Arc<Catalog>,
        i: usize,
        child: NodeId,
        named: bool,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let n = catalog.node(child);
        let selected = self.db.key == Some(child);
        let mut value = None;
        let mut edited = false;
        let detail = match &n.kind {
            NodeKind::Table { rows, .. } => {
                let mut s = format!("Table · {}", plural(*rows as usize, "row", "rows"));
                if let Some(m) = catalog.table_meaning(child).filter(|m| !m.is_empty()) {
                    s.push_str(&format!(" · {m}"));
                }
                s
            }
            NodeKind::Row { .. } => match (&n.display, &n.label) {
                (Some(d), Some(l)) => format!("{d} · {l}"),
                (Some(d), None) => d.clone(),
                (None, Some(l)) => l.clone(),
                (None, None) => plural(catalog.children(child).len(), "entry", "entries"),
            },
            NodeKind::Key => {
                let r = n.record.map(|r| catalog.record(r));
                let key = r.map(|r| r.key).unwrap_or_default();
                edited = self.db.edit(key).is_some();
                value = Some(self.db.current(key));
                let meaning = catalog.meta(key).map(|m| m.meaning.clone()).unwrap_or_default();
                let tag = r.map_or(String::new(), |r| type_name(r.tag));
                if meaning.is_empty() {
                    tag
                } else {
                    format!("{tag} · {meaning}")
                }
            }
            NodeKind::Unnamed => "Records no known name reaches".into(),
            _ => plural(catalog.children(child).len(), "entry", "entries"),
        };
        let title = if named {
            n.name.clone()
        } else {
            n.record
                .map_or_else(|| n.name.clone(), |r| hex(catalog.record(r).key))
        };
        let is_key = n.kind == NodeKind::Key;
        h_flex()
            .id(SharedString::from(format!("db-entry-{child}")))
            .w_full()
            .h(px(LIST_H))
            .px(px(12.))
            .gap(px(12.))
            .when(i > 0, |d| d.border_t_1().border_color(p.border_variant))
            .cursor_pointer()
            .map(|d| {
                if selected {
                    d.bg(p.selected)
                } else {
                    d.hover(move |s| s.bg(p.hover))
                }
            })
            .child(icon_tile(kind_icon(&n.kind), p))
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .child(
                        div()
                            .text_size(px(13.))
                            .font_weight(FontWeight::MEDIUM)
                            .truncate()
                            .child(title),
                    )
                    .child(
                        div()
                            .text_size(px(11.))
                            .text_color(p.text_muted)
                            .truncate()
                            .child(detail),
                    ),
            )
            .children(value.map(|v| {
                div()
                    .w(px(240.))
                    .flex_shrink_0()
                    .flex()
                    .justify_end()
                    .text_size(px(13.))
                    .text_color(if edited { p.accent } else { p.text })
                    .child(
                        div()
                            .truncate()
                            .child(if v.is_empty() { "empty".to_owned() } else { v }),
                    )
            }))
            .when(!is_key, |d| {
                d.child(
                    Icon::new(IconName::ChevronRight)
                        .size(px(14.))
                        .text_color(p.text_placeholder),
                )
            })
            .on_click(cx.listener(move |this, _, _, cx| {
                if is_key {
                    this.db.key = (this.db.key != Some(child)).then_some(child);
                    cx.notify();
                } else {
                    this.db_show_node(child, cx);
                }
            }))
            .into_any_element()
    }

    fn db_grid(
        &self,
        table: NodeId,
        shown: Arc<Vec<NodeId>>,
        catalog: &Arc<Catalog>,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let fields = Arc::new(catalog.fields(table));
        let rows = shown.len();
        let width = GRID_LABEL + GRID_COL * fields.len() as f32 + 24.;
        let header = h_flex()
            .h(px(GRID_HEADER))
            .px(px(12.))
            .flex_shrink_0()
            .border_b_1()
            .border_color(p.border_variant)
            .text_size(px(11.))
            .font_weight(FontWeight::MEDIUM)
            .text_color(p.text_placeholder)
            .child(div().w(px(GRID_LABEL)).flex_shrink_0().child("ROW"))
            .children(fields.iter().map(|f| {
                div()
                    .w(px(GRID_COL))
                    .flex_shrink_0()
                    .pr(px(10.))
                    .truncate()
                    .child(f.clone())
            }));
        let cols = fields.clone();
        let list = uniform_list(
            "db-grid",
            rows,
            cx.processor(move |this, range: Range<usize>, _, cx| this.db_grid_rows(&shown, &cols, range, cx)),
        )
        .flex_1()
        .track_scroll(&self.db.list_scroll);
        div()
            .relative()
            .size_full()
            .rounded(px(6.))
            .border_1()
            .border_color(p.border_variant)
            .overflow_hidden()
            .child(
                div()
                    .id("db-grid-scroll")
                    .size_full()
                    .flex()
                    .overflow_x_scroll()
                    .restrict_scroll_to_axis()
                    .track_scroll(&self.db.grid_scroll)
                    .child(
                        v_flex()
                            .flex_none()
                            .w(px(width))
                            .h_full()
                            .child(header)
                            .child(list),
                    ),
            )
            .child(
                div().absolute().inset_0().child(
                    scrollbar_x(&self.db.grid_scroll)
                        .id("db-grid-scrollbar")
                        .viewport_from_layout(),
                ),
            )
            .child(
                div()
                    .absolute()
                    .top(px(GRID_HEADER))
                    .bottom_0()
                    .left_0()
                    .right_0()
                    .child(self.db.list_motion.overlay(&self.db.list_scroll, cx)),
            )
            .into_any_element()
    }

    fn db_grid_rows(
        &mut self,
        rows: &[NodeId],
        fields: &[String],
        range: Range<usize>,
        cx: &mut Context<Self>,
    ) -> Vec<AnyElement> {
        let p = palette(cx);
        let Some(catalog) = self.db.catalog().cloned() else {
            return Vec::new();
        };
        range
            .filter_map(|i| rows.get(i).map(|&r| (i, r)))
            .map(|(i, row)| {
                let n = catalog.node(row);
                let title = n
                    .display
                    .clone()
                    .or_else(|| n.label.clone())
                    .unwrap_or_else(|| n.name.clone());
                h_flex()
                    .id(SharedString::from(format!("db-grid-row-{row}")))
                    .w_full()
                    .h(px(36.))
                    .px(px(12.))
                    .when(i > 0, |d| d.border_t_1().border_color(p.border_variant))
                    .hover(move |s| s.bg(p.hover))
                    .child(
                        div()
                            .id(SharedString::from(format!("db-grid-open-{row}")))
                            .w(px(GRID_LABEL))
                            .flex_shrink_0()
                            .pr(px(10.))
                            .cursor_pointer()
                            .text_size(px(13.))
                            .font_weight(FontWeight::MEDIUM)
                            .truncate()
                            .child(title)
                            .on_click(cx.listener(move |this, _, _, cx| this.db_go(Nav::Raw(row), cx))),
                    )
                    .children(fields.iter().map(|f| {
                        let cell = catalog.find_in(row, f);
                        let key = cell
                            .and_then(|c| catalog.node(c).record)
                            .map(|r| catalog.record(r).key);
                        let (text, color) = match key {
                            Some(k) => (
                                self.db.current(k),
                                if self.db.errors.contains_key(&k) {
                                    p.error
                                } else if self.db.edit(k).is_some() {
                                    p.accent
                                } else {
                                    p.text
                                },
                            ),
                            None => ("—".to_owned(), p.text_placeholder),
                        };
                        let selected = cell.is_some() && self.db.key == cell;
                        div()
                            .id(SharedString::from(format!("db-grid-{row}-{f}")))
                            .w(px(GRID_COL))
                            .flex_shrink_0()
                            .h_full()
                            .flex()
                            .items_center()
                            .pr(px(10.))
                            .text_size(px(13.))
                            .text_color(color)
                            .when(selected, |d| d.bg(p.selected))
                            .when(cell.is_some(), |d| d.cursor_pointer())
                            .child(div().truncate().child(text))
                            .on_click(cx.listener(move |this, _, _, cx| {
                                if let Some(c) = cell {
                                    this.db.key = (this.db.key != Some(c)).then_some(c);
                                    cx.notify();
                                }
                            }))
                    }))
                    .into_any_element()
            })
            .collect()
    }

    pub(crate) fn db_unnamed_view(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let rows = self.db.unnamed_rows.clone();
        let total = self
            .db
            .catalog()
            .map_or(0, |c| c.records().saturating_sub(c.named()));
        let filtered = self
            .db
            .unnamed
            .as_ref()
            .is_some_and(|d| !d.query.trim().is_empty());
        let count = if filtered {
            format!("{} of {total}", rows.len())
        } else {
            plural(total, "record", "records")
        };
        let list = if rows.is_empty() {
            self.empty(
                IconName::Search,
                "No matching keys",
                "No unnamed record has that key or value.",
                div(),
                p,
            )
        } else {
            let shown = rows.clone();
            let list = uniform_list(
                "db-unnamed",
                rows.len(),
                cx.processor(move |this, range: Range<usize>, _, cx| {
                    let p = palette(cx);
                    let Some(catalog) = this.db.catalog().cloned() else {
                        return Vec::new();
                    };
                    range
                        .filter_map(|i| shown.get(i).map(|&n| (i, n)))
                        .map(|(i, n)| this.db_entry(&catalog, i, n, false, p, cx))
                        .collect::<Vec<_>>()
                }),
            )
            .flex_1()
            .track_scroll(&self.db.list_scroll);
            v_flex()
                .size_full()
                .rounded(px(6.))
                .border_1()
                .border_color(p.border_variant)
                .overflow_hidden()
                .child(self.db.list_motion.wrap(list, &self.db.list_scroll, cx))
                .into_any_element()
        };
        let detail = self.db.key.map(|k| self.db_key_detail(k, p, cx));
        v_flex()
            .size_full()
            .gap(px(14.))
            .child(
                h_flex()
                    .gap(px(12.))
                    .flex_shrink_0()
                    .child(super::page::section_head(
                        IconName::Binary,
                        "Unnamed keys",
                        "Records whose names aren't known yet. Their values still edit.",
                        p,
                    )),
            )
            .child(
                h_flex()
                    .gap(px(16.))
                    .flex_shrink_0()
                    .children(self.db.unnamed.as_ref().map(|d| {
                        div().w(px(280.)).child(
                            Input::new(&d.input).small().cleanable(true).prefix(
                                Icon::new(IconName::ListFilter)
                                    .size(px(14.))
                                    .text_color(p.text_placeholder),
                            ),
                        )
                    }))
                    .child(div().flex_1())
                    .child(
                        div()
                            .text_size(px(12.))
                            .text_color(p.text_placeholder)
                            .child(count),
                    ),
            )
            .child(
                h_flex()
                    .flex_1()
                    .min_h_0()
                    .gap(px(16.))
                    .items_start()
                    .child(
                        div()
                            .flex_1()
                            .min_w_0()
                            .h_full()
                            .flex()
                            .flex_col()
                            .child(self.db.list_motion.wrap(list, &self.db.list_scroll, cx)),
                    )
                    .children(detail),
            )
            .into_any_element()
    }

    fn db_change_items(&self) -> Vec<ChangeItem> {
        let Some(catalog) = self.db.catalog().cloned() else {
            return Vec::new();
        };
        let mut groups: Vec<(String, Vec<ChangeItem>)> = Vec::new();
        let rows: Vec<ChangeItem> = self
            .db
            .draft
            .edits
            .rows
            .iter()
            .map(|r| ChangeItem {
                group: None,
                edit: None,
                row: Some(r.clone()),
                name: format!("{} › Own row", self.db_row_name(&r.to)),
                path: Some(r.select()),
                node: None,
            })
            .collect();
        if !rows.is_empty() {
            groups.push((COPIED.to_owned(), rows));
        }
        for e in &self.db.draft.edits.edits {
            let node = catalog.node_of_key(e.key);
            let mut path = Some(e.path.clone().unwrap_or_else(|| hex(e.key)));
            let (name, group) = match node {
                Some(n) if e.stock.is_some() => {
                    let (mut name, row) = friendly(&catalog, n);
                    let group = match table_of(&catalog, n) {
                        Some(t) => {
                            let table = catalog.path(t);
                            let shaped = shape(&table);
                            let module = self.db.modules.iter().find(|m| shape(&m.rows) == shaped);
                            if let (Some(m), Some(row)) = (module, row) {
                                let r = catalog.node(row);
                                let rel = name.split_once(" › ").map_or("", |(_, rel)| rel);
                                let field = m.fields.iter().find(|f| f.path == rel);
                                let label = r.label.clone().unwrap_or_else(|| r.name.clone());
                                let shown = self
                                    .db
                                    .view
                                    .as_ref()
                                    .filter(|v| v.module == m.id)
                                    .and_then(|v| v.view.rows.iter().find(|x| x.label == label))
                                    .map(|x| x.name.clone())
                                    .filter(|n| !n.is_empty())
                                    .or_else(|| r.display.clone())
                                    .unwrap_or_else(|| title_case(&label));
                                if let Some(f) = field {
                                    name = format!("{shown} › {}", f.label);
                                    path = None;
                                }
                            }
                            module.map_or(table, |m| m.title.clone())
                        }
                        None if catalog.node(catalog.node(n).parent.unwrap_or(0)).kind
                            == NodeKind::Unnamed =>
                        {
                            "Unnamed keys".to_owned()
                        }
                        None => catalog.path(catalog.node(n).parent.unwrap_or(0)),
                    };
                    (name, group)
                }
                _ if e.is_copied() => (
                    self.db_cell_name(e.key)
                        .unwrap_or_else(|| e.path.clone().unwrap_or_else(|| hex(e.key))),
                    COPIED.to_owned(),
                ),
                _ => (
                    e.path.clone().unwrap_or_else(|| hex(e.key)),
                    "Added keys".to_owned(),
                ),
            };
            let item = ChangeItem {
                group: None,
                edit: Some(e.clone()),
                row: None,
                name,
                path,
                node,
            };
            match groups.iter_mut().find(|(g, _)| *g == group) {
                Some((_, list)) => list.push(item),
                None => groups.push((group, vec![item])),
            }
        }
        let mut out = Vec::new();
        for (g, items) in groups {
            out.push(ChangeItem {
                group: Some(format!("{g} · {}", items.len())),
                edit: None,
                row: None,
                name: String::new(),
                path: None,
                node: None,
            });
            out.extend(items);
        }
        out
    }

    pub(crate) fn db_changes(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let n = self.db.draft.edits.len();
        let actions = h_flex().gap(px(8.)).child(
            Button::new("db-revert-all")
                .outline()
                .small()
                .icon(IconName::Undo2)
                .label("Revert all changes")
                .disabled(n == 0 || self.db_locked())
                .on_click(cx.listener(|this, _, window, cx| this.db_revert_all(window, cx))),
        );
        let head = h_flex()
            .gap(px(12.))
            .flex_shrink_0()
            .child(super::page::section_head(
                IconName::ListChecks,
                "Changes",
                &match self.db.draft.unsaved() {
                    0 => format!("{} in {}", plural(n, "change", "changes"), self.db_draft_name()),
                    k => format!(
                        "{} in {} · {k} not saved yet",
                        plural(n, "change", "changes"),
                        self.db_draft_name()
                    ),
                },
                p,
            ))
            .child(actions);
        if n == 0 {
            return v_flex()
                .size_full()
                .gap(px(14.))
                .child(head)
                .child(self.empty(
                    IconName::ListChecks,
                    "No changes yet",
                    "Values you change in the modules or the table browser show up here. Save them as an addon to use them in the game.",
                    div(),
                    p,
                ))
                .into_any_element();
        }
        let items = Arc::new(self.db_change_items());
        let shown = items.clone();
        v_flex()
            .size_full()
            .gap(px(14.))
            .child(head)
            .child(
                v_flex()
                    .flex_1()
                    .min_h_0()
                    .rounded(px(6.))
                    .border_1()
                    .border_color(p.border_variant)
                    .overflow_hidden()
                    .child(
                        self.db.list_motion.wrap(
                            uniform_list(
                                "db-changes",
                                items.len(),
                                cx.processor(move |this, range: Range<usize>, _, cx| {
                                    let p = palette(cx);
                                    range
                                        .filter_map(|i| shown.get(i).map(|it| (i, it)))
                                        .map(|(i, it)| this.db_change_row(i, it, p, cx))
                                        .collect::<Vec<_>>()
                                }),
                            )
                            .flex_1()
                            .track_scroll(&self.db.list_scroll),
                            &self.db.list_scroll,
                            cx,
                        ),
                    ),
            )
            .into_any_element()
    }

    fn db_change_row(&self, i: usize, it: &ChangeItem, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        if let Some(g) = &it.group {
            return h_flex()
                .w_full()
                .h(px(LIST_H))
                .px(px(12.))
                .items_end()
                .pb(px(8.))
                .when(i > 0, |d| d.border_t_1().border_color(p.border_variant))
                .bg(p.surface)
                .child(label(g.to_uppercase(), p))
                .into_any_element();
        }
        if let Some(r) = &it.row {
            return self.db_copy_change_row(r, it, p, cx);
        }
        let Some(e) = &it.edit else {
            return div().into_any_element();
        };
        let key = e.key;
        let node = it.node;
        let copied = self.db.catalog().filter(|_| e.is_copied()).and_then(|c| {
            let source = self.db.draft.edits.copied(c, e.key)?.source;
            c.record_by_key(source).map(|r| r.value.to_owned())
        });
        let stock = match e.stock.clone().or(copied) {
            Some(s) if s.is_empty() => "empty".to_owned(),
            Some(s) => s,
            None => "added".to_owned(),
        };
        let new = match &e.value {
            Some(v) if v.is_empty() => "empty".to_owned(),
            Some(v) => v.clone(),
            None => "removed".to_owned(),
        };
        let origin = match &e.origin {
            Origin::Typed => None,
            Origin::Imported(f) => Some(format!("Imported from {f}")),
        };
        h_flex()
            .id(SharedString::from(format!("db-change-{key:x}")))
            .w_full()
            .h(px(LIST_H))
            .px(px(12.))
            .gap(px(14.))
            .border_t_1()
            .border_color(p.border_variant)
            .hover(move |s| s.bg(p.hover))
            .when(node.is_some(), |d| d.cursor_pointer())
            .child(
                v_flex()
                    .flex_1()
                    .min_w(px(160.))
                    .child(
                        div()
                            .text_size(px(13.))
                            .font_weight(FontWeight::MEDIUM)
                            .truncate()
                            .child(it.name.clone()),
                    )
                    .children(it.path.clone().map(|path| {
                        div()
                            .text_size(px(11.))
                            .text_color(p.text_placeholder)
                            .truncate()
                            .child(path)
                    })),
            )
            .child(
                h_flex()
                    .w(px(220.))
                    .flex_shrink_0()
                    .gap(px(8.))
                    .text_size(px(13.))
                    .child(
                        div()
                            .max_w(px(100.))
                            .truncate()
                            .text_color(p.text_muted)
                            .child(stock),
                    )
                    .child(
                        Icon::new(IconName::ArrowRight)
                            .size(px(12.))
                            .text_color(p.text_placeholder),
                    )
                    .child(
                        div()
                            .flex_1()
                            .min_w_0()
                            .truncate()
                            .text_color(p.accent)
                            .child(new),
                    ),
            )
            .when_some(origin.filter(|_| self.content_width >= 1100.), |d, origin| {
                d.child(
                    div()
                        .w(px(170.))
                        .flex_shrink_0()
                        .text_size(px(12.))
                        .text_color(p.text_muted)
                        .truncate()
                        .child(origin),
                )
            })
            .child(
                Button::new(SharedString::from(format!("db-change-revert-{key:x}")))
                    .ghost()
                    .xsmall()
                    .icon(IconName::Undo2)
                    .tooltip("Revert")
                    .disabled(self.db_locked())
                    .on_click(cx.listener(move |this, _, _, cx| {
                        cx.stop_propagation();
                        this.db_revert(key, cx);
                    })),
            )
            .on_click(cx.listener(move |this, _, _, cx| {
                if let Some(n) = node {
                    this.db_show_node(n, cx);
                }
            }))
            .into_any_element()
    }

    /// `Row › Field` of the open page's cell holding `key`.
    fn db_cell_name(&self, key: u64) -> Option<String> {
        let slot = self.db.view.as_ref()?;
        let module = self.db.module(&slot.module)?;
        slot.view.rows.iter().find_map(|r| {
            let i = r.cells.iter().position(|c| c.key == Some(key))?;
            let name = if r.name.is_empty() { &r.label } else { &r.name };
            Some(format!("{name} › {}", module.fields.get(i)?.label))
        })
    }

    /// The name a page gives the row labelled `label`, else the label in title case.
    fn db_row_name(&self, label: &str) -> String {
        self.db
            .view
            .as_ref()
            .and_then(|v| v.view.rows.iter().find(|r| r.label == label))
            .map(|r| r.name.clone())
            .filter(|n| !n.is_empty())
            .unwrap_or_else(|| title_case(label))
    }

    fn db_copy_change_row(
        &self,
        r: &RowCopy,
        it: &ChangeItem,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let from = match &r.from {
            Some(l) => format!("copy of {}", self.db_row_name(l)),
            None => "copy of the game default".to_owned(),
        };
        let (table, field, to) = (r.table.clone(), r.field.clone(), r.to.clone());
        h_flex()
            .id(SharedString::from(format!("db-change-row-{}", r.select())))
            .w_full()
            .h(px(LIST_H))
            .px(px(12.))
            .gap(px(14.))
            .border_t_1()
            .border_color(p.border_variant)
            .hover(move |s| s.bg(p.hover))
            .child(
                v_flex()
                    .flex_1()
                    .min_w(px(160.))
                    .child(
                        div()
                            .text_size(px(13.))
                            .font_weight(FontWeight::MEDIUM)
                            .truncate()
                            .child(it.name.clone()),
                    )
                    .children(it.path.clone().map(|path| {
                        div()
                            .text_size(px(11.))
                            .text_color(p.text_placeholder)
                            .truncate()
                            .child(path)
                    })),
            )
            .child(
                h_flex()
                    .w(px(220.))
                    .flex_shrink_0()
                    .gap(px(8.))
                    .text_size(px(13.))
                    .child(div().text_color(p.text_muted).child("none"))
                    .child(
                        Icon::new(IconName::ArrowRight)
                            .size(px(12.))
                            .text_color(p.text_placeholder),
                    )
                    .child(
                        div()
                            .flex_1()
                            .min_w_0()
                            .truncate()
                            .text_color(p.accent)
                            .child(from),
                    ),
            )
            .child(
                Button::new(SharedString::from(format!("db-change-unrow-{}", r.select())))
                    .ghost()
                    .xsmall()
                    .icon(IconName::Undo2)
                    .tooltip("Remove own row")
                    .disabled(self.db_locked())
                    .on_click(cx.listener(move |this, _, _, cx| {
                        cx.stop_propagation();
                        this.db_remove_copy(&table, &field, &to, cx);
                    })),
            )
            .into_any_element()
    }

    pub(crate) fn db_add_dialog(&mut self, at: NodeId, window: &mut Window, cx: &mut Context<Self>) {
        let Some(catalog) = self.db.catalog().cloned() else {
            return;
        };
        let base = if at == catalog.root() {
            String::new()
        } else {
            format!("{}.", catalog.path(at))
        };
        let path = cx.new(|cx| {
            InputState::new(window, cx)
                .placeholder("tracks.Track_3.my_new_key")
                .default_value(base)
        });
        let value = cx.new(|cx| InputState::new(window, cx).placeholder("Value"));
        let tag = std::rc::Rc::new(Cell::new(0u8));
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        let focus = Cell::new(true);
        window.open_alert_dialog(cx, move |alert, window, cx| {
            if focus.replace(false) {
                path.update(cx, |s, cx| s.focus(window, cx));
            }
            let (path, value, shell, tag) = (path.clone(), value.clone(), shell.clone(), tag.clone());
            let current = tag.get();
            let picker = {
                let tag = tag.clone();
                Button::new("db-add-type")
                    .outline()
                    .small()
                    .min_w(px(120.))
                    .label(type_name(current))
                    .dropdown_caret(true)
                    .dropdown_menu(move |mut menu, _, _| {
                        for (t, name) in ADDABLE {
                            let tag = tag.clone();
                            menu = menu.item(PopupMenuItem::new(name).checked(t == current).on_click(move |_, window, _| {
                                tag.set(t);
                                window.refresh();
                            }));
                        }
                        menu
                    })
            };
            let field = |title: &'static str, el: AnyElement| {
                v_flex()
                    .gap(px(6.))
                    .child(div().text_size(px(12.)).font_weight(FontWeight::MEDIUM).text_color(p.text_muted).child(title))
                    .child(el)
            };
            alert
                .width(px(560.))
                .title("Add key")
                .child(
                    v_flex()
                        .gap(px(14.))
                        .child(div().text_size(px(13.)).text_color(p.text_muted).child(
                            "Adds a key the stock database doesn't have. The game only reads keys it knows, so \
                             this is for paths you've confirmed.",
                        ))
                        .child(field("PATH", Input::new(&path).small().into_any_element()))
                        .child(
                            h_flex()
                                .gap(px(12.))
                                .items_start()
                                .child(field("TYPE", picker.into_any_element()))
                                .child(div().flex_1().child(field("VALUE", Input::new(&value).small().into_any_element()))),
                        ),
                )
                .ok_text("Add key")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    let p = path.read(cx).value().trim().to_owned();
                    let v = value.read(cx).value().trim().to_owned();
                    let result = shell
                        .update(cx, |s, cx| s.db_add_key(&p, tag.get(), &v, cx))
                        .unwrap_or_else(|_| Err("The launcher is closing".into()));
                    match result {
                        Ok(()) => {
                            window.push_notification(
                                gpui_kit::component::notification::Notification::success(format!("Added {p}")),
                                cx,
                            );
                            true
                        }
                        Err(e) => {
                            crate::app_log::action("db editor: add a key").failed(&e);
                            window.push_notification(
                                gpui_kit::component::notification::Notification::error(e).title("Could not add the key"),
                                cx,
                            );
                            false
                        }
                    }
                })
        });
    }
}
