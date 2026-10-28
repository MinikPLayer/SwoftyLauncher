use std::cell::RefCell;
use std::collections::HashMap;
use std::path::{Component, Path, PathBuf};
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::input::{Input, InputEvent, InputState, NumberInput};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::select::{Select, SelectDelegate, SelectEvent, SelectItem, SelectState};
use gpui_kit::component::slider::{Slider, SliderEvent, SliderState};
use gpui_kit::component::switch::Switch;
use gpui_kit::component::{Disableable, Icon, IndexPath, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use serde_json::{Map, Value};
use swofty_core::view::{
    Align, Block, Body, Card, Cell, Choice, DropZone, Field, Fit, Form, Grid, Hero, Image as ImageBlock,
    Meter, Pill, Search, SelectField, Table, TableRow, Tabs, Tile, Tone, View, colour_hex, parse_colour,
};

use crate::colour::{ColourEvent, ColourPicker, hsla_of};
use crate::pages::label;
use crate::sections::{art, icon, tone};
use crate::shell::Shell;
use crate::theme::Palette;

const GAP: f32 = 16.;
/// Tables with more rows than this draw only the rows in view.
const LONG_TABLE: usize = 30;
/// A table's detail panel, as wide as the DB Editor's, and the narrowest a column without a
/// fixed width may get beside it before the panel opens under its row instead.
const DETAIL_W: f32 = 380.;
const DETAIL_COLUMN_MIN: f32 = 96.;
const SELECT_MENU_H: f32 = 320.;
const SELECT_HEADING_H: f32 = 26.;
const SELECT_THUMB: f32 = 30.;
const SELECT_THUMB_SMALL: f32 = 18.;
const GRID_COLUMN: f32 = 280.;
const TILE_COLUMN: f32 = 170.;

#[derive(Clone, Copy)]
pub(crate) struct At<'a> {
    pub addon: &'a str,
    pub section: &'a str,
    pub width: f32,
    pub nested: bool,
    pub fill: bool,
}

impl At<'_> {
    pub(crate) fn key(&self, id: &str) -> String {
        format!("{}/{}/{id}", self.addon, self.section)
    }

    fn inner(self, width: f32) -> Self {
        Self {
            width,
            nested: true,
            fill: false,
            ..self
        }
    }
}

pub(crate) enum Reset {
    No,
    Now,
    After(u64),
}

#[derive(Clone, Default)]
struct Shown {
    label: SharedString,
    image: Option<PathBuf>,
}

#[derive(Clone)]
struct Opt {
    value: String,
    label: SharedString,
    image: Option<PathBuf>,
    thumbs: bool,
    shown: Rc<RefCell<Shown>>,
}

fn thumb(image: Option<PathBuf>, size: f32, bg: Option<Hsla>) -> Div {
    div()
        .size(px(size))
        .flex_shrink_0()
        .rounded(px(4.))
        .overflow_hidden()
        .when_some(bg, |d, bg| d.bg(bg))
        .when_some(image, |d, path| {
            d.child(
                img(path)
                    .size(px(size))
                    .aspect_square()
                    .object_fit(ObjectFit::Cover),
            )
        })
}

impl SelectItem for Opt {
    type Value = String;

    fn title(&self) -> SharedString {
        self.label.clone()
    }

    // The closed field keeps showing the committed value while the cursor moves.
    fn display_title(&self) -> Option<AnyElement> {
        let shown = self.shown.borrow().clone();
        Some(match shown.image {
            Some(image) => h_flex()
                .gap(px(8.))
                .min_w_0()
                .child(thumb(Some(image), SELECT_THUMB_SMALL, None))
                .child(div().min_w_0().truncate().child(shown.label))
                .into_any_element(),
            None => shown.label.into_any_element(),
        })
    }

    fn render(&self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        h_flex()
            .gap(px(10.))
            .min_w_0()
            .when(self.thumbs, |d| {
                d.child(thumb(
                    self.image.clone(),
                    SELECT_THUMB,
                    Some(crate::theme::palette(cx).elevated),
                ))
            })
            .child(div().min_w_0().truncate().child(self.label.clone()))
    }

    fn value(&self) -> &String {
        &self.value
    }
}

struct OptGroup {
    title: Option<SharedString>,
    items: Vec<Opt>,
}

struct Opts {
    groups: Vec<OptGroup>,
    matched: Vec<(usize, Vec<usize>)>,
    titled: bool,
}

impl Opts {
    fn new(groups: Vec<OptGroup>) -> Self {
        let titled = groups.iter().any(|g| g.title.is_some());
        let matched = groups
            .iter()
            .enumerate()
            .map(|(g, group)| (g, (0..group.items.len()).collect()))
            .collect();
        Self {
            groups,
            matched,
            titled,
        }
    }
}

impl SelectDelegate for Opts {
    type Item = Opt;

    fn sections_count(&self, _: &App) -> usize {
        self.matched.len()
    }

    fn items_count(&self, section: usize) -> usize {
        self.matched.get(section).map_or(0, |(_, rows)| rows.len())
    }

    fn item(&self, ix: IndexPath) -> Option<&Opt> {
        let (g, rows) = self.matched.get(ix.section)?;
        self.groups[*g].items.get(*rows.get(ix.row)?)
    }

    fn position<V>(&self, value: &V) -> Option<IndexPath>
    where
        Opt: SelectItem<Value = V>,
        V: PartialEq,
    {
        self.matched.iter().enumerate().find_map(|(section, (g, rows))| {
            let items = &self.groups[*g].items;
            let row = rows.iter().position(|&r| items[r].value() == value)?;
            Some(IndexPath::default().section(section).row(row))
        })
    }

    fn perform_search(&mut self, query: &str, _: &mut Window, _: &mut App) -> Task<()> {
        let needle = query.to_lowercase();
        self.matched = self
            .groups
            .iter()
            .enumerate()
            .filter_map(|(g, group)| {
                let whole = group
                    .title
                    .as_ref()
                    .is_some_and(|t| t.to_lowercase().contains(&needle));
                let rows: Vec<usize> = (0..group.items.len())
                    .filter(|&r| whole || group.items[r].matches(query))
                    .collect();
                (!rows.is_empty()).then_some((g, rows))
            })
            .collect();
        Task::ready(())
    }

    fn render_section_header(&self, section: usize, _: &mut Window, cx: &mut App) -> Option<AnyElement> {
        if !self.titled {
            return None;
        }
        let p = crate::theme::palette(cx);
        let title = self
            .matched
            .get(section)
            .and_then(|(g, _)| self.groups[*g].title.clone());
        Some(
            h_flex()
                .h(px(SELECT_HEADING_H))
                .px_2()
                .items_end()
                .pb(px(4.))
                .when(section > 0, |d| d.border_t_1().border_color(p.border_variant))
                .children(title.map(|t| label(t.to_uppercase(), p)))
                .into_any_element(),
        )
    }
}

struct SelectSlot {
    state: Entity<SelectState<Opts>>,
    options: Vec<Choice>,
    shown: Rc<RefCell<Shown>>,
}

struct ImageRoots([PathBuf; 2]);

impl ImageRoots {
    fn allow(&self, path: &Path) -> Option<PathBuf> {
        let plain = path.is_absolute()
            && path.components().all(|c| {
                matches!(
                    c,
                    Component::Prefix(_) | Component::RootDir | Component::Normal(_)
                )
            });
        (plain && self.0.iter().any(|root| path.starts_with(root))).then(|| path.to_path_buf())
    }
}

fn opts(sf: &SelectField, roots: &ImageRoots, shown: &Rc<RefCell<Shown>>) -> Opts {
    let images: Vec<Option<PathBuf>> = sf
        .options
        .iter()
        .map(|o| o.image().and_then(|i| roots.allow(i)))
        .collect();
    let thumbs = images.iter().any(Option::is_some);
    let mut groups = vec![OptGroup {
        title: None,
        items: Vec::new(),
    }];
    for (o, image) in sf.options.iter().zip(images) {
        if o.is_heading() {
            groups.push(OptGroup {
                title: Some(SharedString::from(o.label().to_owned())),
                items: Vec::new(),
            });
        } else if let Some(group) = groups.last_mut() {
            group.items.push(Opt {
                value: o.value().to_owned(),
                label: SharedString::from(o.label().to_owned()),
                image,
                thumbs,
                shown: shown.clone(),
            });
        }
    }
    groups.retain(|g| !g.items.is_empty());
    Opts::new(groups)
}

fn shown_for(sf: &SelectField, roots: &ImageRoots, value: &str) -> Shown {
    match sf.options.iter().find(|o| !o.is_heading() && o.value() == value) {
        Some(o) => Shown {
            label: SharedString::from(o.label().to_owned()),
            image: o.image().and_then(|i| roots.allow(i)),
        },
        None => Shown {
            label: SharedString::from(value.to_owned()),
            image: None,
        },
    }
}

pub(crate) struct FormSlot {
    inputs: HashMap<String, Entity<InputState>>,
    sliders: HashMap<String, Entity<SliderState>>,
    selects: HashMap<String, SelectSlot>,
    picks: HashMap<String, Value>,
    synced: HashMap<String, Value>,
    pub(crate) reset: Reset,
    seen: bool,
    _subs: Vec<Subscription>,
}

pub(crate) struct SearchSlot {
    state: Entity<InputState>,
    pub(crate) query: String,
    generation: u64,
    seen: bool,
    _sub: Subscription,
}

pub(crate) struct CellEdit {
    target: CellTarget,
    field: Field,
    original: Value,
    input: Entity<InputState>,
    _subs: Vec<Subscription>,
}

pub(crate) struct ColourEdit {
    key: String,
    target: ColourTarget,
    picker: Entity<ColourPicker>,
    trigger: Rc<std::cell::Cell<Bounds<Pixels>>>,
    _subs: Vec<Subscription>,
}

#[derive(Clone)]
enum ColourTarget {
    Form {
        key: String,
        field: String,
        addon: String,
        section: String,
        form: String,
    },
    Cell {
        target: CellTarget,
        original: Value,
    },
}

fn swatch(rgb: Option<[u8; 3]>, size: f32, p: Palette) -> Div {
    let dot = div().size(px(size)).flex_shrink_0().rounded_full();
    match rgb {
        Some(c) => dot.bg(hsla_of(c)).border_1().border_color(hsla(0., 0., 1., 0.12)),
        None => dot.border_1().border_color(p.text_placeholder),
    }
}

pub(crate) fn pill(p: &Pill, pal: Palette) -> Div {
    let color = tone(p.tone, pal);
    let (bg, fg) = match p.tone {
        Tone::Neutral => (pal.elevated, pal.text),
        Tone::Muted => (pal.hover, pal.text_muted),
        _ => (color.opacity(0.14), color),
    };
    h_flex()
        .h(px(22.))
        .px(px(9.))
        .gap(px(6.))
        .flex_shrink_0()
        .rounded(px(11.))
        .bg(bg)
        .text_color(fg)
        .text_size(px(11.))
        .font_weight(FontWeight::SEMIBOLD)
        .whitespace_nowrap()
        .when(p.dot, |d| d.child(div().size(px(6.)).rounded_full().bg(fg)))
        .when_some(p.icon.filter(|_| !p.dot), |d, i| {
            d.child(Icon::new(icon(i)).size(px(12.)))
        })
        .child(p.text.clone())
}

fn pills(items: &[Pill], pal: Palette) -> Div {
    h_flex()
        .gap(px(6.))
        .flex_wrap()
        .children(items.iter().map(|p| pill(p, pal)))
}

fn bar(fraction: f32, color: Hsla, height: f32, pal: Palette) -> Div {
    div()
        .h(px(height))
        .w_full()
        .rounded_full()
        .bg(pal.elevated)
        .child(
            div()
                .h_full()
                .w(relative(fraction.clamp(0., 1.)))
                .rounded_full()
                .bg(color),
        )
}

fn fill_tone(t: Option<Tone>, pal: Palette) -> Hsla {
    match t {
        None | Some(Tone::Neutral) => pal.accent,
        Some(t) => tone(t, pal),
    }
}

fn tile_columns(count: usize, width: f32) -> usize {
    let wanted = match count {
        0..=4 => count.max(1),
        5 | 6 => 3,
        _ => 4,
    };
    wanted.min(((width + GAP) / (TILE_COLUMN + GAP)).floor().max(1.) as usize)
}

fn surface(pal: Palette) -> Div {
    v_flex()
        .rounded(px(8.))
        .border_1()
        .border_color(pal.border_variant)
        .bg(pal.surface)
}

fn number_text(text: &str) -> Option<f64> {
    let cleaned: String = text.chars().filter(|c| !matches!(c, ',' | ' ' | '_')).collect();
    cleaned.parse::<f64>().ok().filter(|v| v.is_finite())
}

fn field_text(field: &Field, value: &Value) -> String {
    match (field, value) {
        (Field::Number(f) | Field::Slider(f), Value::Number(n)) => {
            n.as_f64().map(|v| f.text(v)).unwrap_or_default()
        }
        (_, Value::String(s)) => s.clone(),
        (_, Value::Null) => String::new(),
        (_, v) => v.to_string(),
    }
}

fn sorted_rows(t: &Table, sort: Option<&(String, bool)>) -> Vec<usize> {
    let mut order: Vec<usize> = (0..t.rows.len()).collect();
    let Some((column, desc)) = sort else {
        return order;
    };
    let Some(ci) = t.columns.iter().position(|c| c.key() == column) else {
        return order;
    };
    let keys: Vec<_> = t
        .rows
        .iter()
        .map(|r| r.cells().get(ci).map(Cell::sort_key))
        .collect();
    order.sort_by(|a, b| match (&keys[*a], &keys[*b]) {
        (Some(x), Some(y)) => x.compare(y),
        (x, y) => x.is_some().cmp(&y.is_some()).reverse(),
    });
    if *desc {
        order.reverse();
    }
    order
}

/// Which table rows' details to walk into: `open(table id, row id)`.
type OpenRows<'f> = &'f dyn Fn(&str, &str) -> bool;

pub(crate) fn walk<'a>(blocks: &'a [Block], out: &mut Vec<&'a Block>) {
    walk_open(blocks, out, &|_, _| true);
}

fn walk_open<'a>(blocks: &'a [Block], out: &mut Vec<&'a Block>, open: OpenRows) {
    for b in blocks {
        out.push(b);
        match b {
            Block::Card(c) => walk_open(&c.blocks, out, open),
            Block::Grid(g) => walk_open(&g.blocks, out, open),
            Block::Table(t) => {
                for (i, r) in t.rows.iter().enumerate() {
                    if open(t.id(), &r.id(i)) {
                        walk_open(r.detail(), out, open);
                    }
                }
            }
            _ => {}
        }
    }
}

fn view_blocks(view: &View) -> Vec<&Block> {
    view_blocks_open(view, &|_, _| true)
}

/// The view's blocks, walking into the details of the table rows `open` names.
fn view_blocks_open<'a>(view: &'a View, open: OpenRows) -> Vec<&'a Block> {
    let mut out = Vec::new();
    match &view.body {
        Body::Blocks(blocks) => walk_open(blocks, &mut out, open),
        Body::List { rows, .. } => {
            for r in rows {
                walk(&r.detail, &mut out);
            }
        }
        Body::Empty { .. } => {}
    }
    out
}

pub(crate) fn search_id(search: &Search, fallback: &str) -> String {
    search.id.clone().unwrap_or_else(|| fallback.to_owned())
}

impl Shell {
    fn current_view(&self) -> Option<(String, String, Arc<View>, u64)> {
        let i = self.open_section()?;
        let s = self.library.sections.get(i)?;
        let key = format!("{}/{}", s.addon, s.spec.id);
        let view = self.views.get(&key)?.as_ref().ok()?.clone();
        let generation = self.view_gens.get(&key).copied().unwrap_or(0);
        Some((s.addon.clone(), s.spec.id.clone(), view, generation))
    }

    fn image_roots(&self, addon: &str) -> ImageRoots {
        ImageRoots([
            self.store.addon_cache_dir(addon),
            self.store.addon_data_dir(addon),
        ])
    }

    pub(crate) fn find_form(&self, addon: &str, section: &str, id: &str) -> Option<Form> {
        let view = self.views.get(&format!("{addon}/{section}"))?.as_ref().ok()?;
        view_blocks(view).into_iter().find_map(|b| match b {
            Block::Form(f) if f.id == id => Some(f.clone()),
            _ => None,
        })
    }

    pub(crate) fn sync_blocks(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some((addon, section, view, generation)) = self.current_view() else {
            self.forms.clear();
            self.searches.clear();
            self.cell = None;
            self.colour = None;
            return;
        };
        let prefix = format!("{addon}/{section}/");
        for slot in self.forms.values_mut() {
            slot.seen = false;
        }
        for slot in self.searches.values_mut() {
            slot.seen = false;
        }
        if let Body::List { search: Some(s), .. } = &view.body {
            let key = format!("{prefix}{}", search_id(s, "list"));
            self.sync_search(&key, &addon, &section, s, window, cx);
        }
        // A form in a table row's detail is set up only while the row is open: a long table can
        // carry a form in every row.
        let open_rows = self.table_rows.clone();
        let open = |table: &str, row: &str| {
            open_rows
                .get(&format!("{prefix}table:{table}"))
                .is_some_and(|o| o == row)
        };
        for b in view_blocks_open(&view, &open) {
            match b {
                Block::Form(f) => self.sync_form(&addon, &section, f, generation, window, cx),
                Block::Table(t) => {
                    if let Some(s) = &t.search {
                        let key = format!("{prefix}{}", search_id(s, t.id()));
                        self.sync_search(&key, &addon, &section, s, window, cx);
                    }
                }
                _ => {}
            }
        }
        self.forms.retain(|k, s| k.starts_with(&prefix) && s.seen);
        self.searches.retain(|k, s| k.starts_with(&prefix) && s.seen);
        if self
            .cell
            .as_ref()
            .is_some_and(|c| !c.target.key.starts_with(&prefix))
        {
            self.cell = None;
        }
        if self.colour.as_ref().is_some_and(|c| !c.key.starts_with(&prefix)) {
            self.colour = None;
        }
    }

    fn sync_search(
        &mut self,
        key: &str,
        addon: &str,
        section: &str,
        search: &Search,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        if let Some(slot) = self.searches.get_mut(key) {
            slot.seen = true;
            return;
        }
        let placeholder = search.placeholder.clone().unwrap_or_else(|| "Search".into());
        let state = cx.new(|cx| InputState::new(window, cx).placeholder(placeholder));
        let (key_owned, addon, section, id, event) = (
            key.to_owned(),
            addon.to_owned(),
            section.to_owned(),
            search.id.clone(),
            search.event,
        );
        let fallback = key.rsplit('/').next().unwrap_or("list").to_owned();
        let sub = cx.subscribe_in(&state, window, move |this, state, ev: &InputEvent, window, cx| {
            if !matches!(ev, InputEvent::Change) {
                return;
            }
            let query = state.read(cx).value().to_string();
            let Some(slot) = this.searches.get_mut(&key_owned) else {
                return;
            };
            if slot.query == query {
                return;
            }
            slot.query = query.clone();
            slot.generation += 1;
            cx.notify();
            if !event {
                return;
            }
            let (generation, key, addon, section) =
                (slot.generation, key_owned.clone(), addon.clone(), section.clone());
            let id = id.clone().unwrap_or_else(|| fallback.clone());
            cx.spawn_in(window, async move |this, cx| {
                cx.background_executor().timer(Duration::from_millis(300)).await;
                this.update_in(cx, |s, window, cx| {
                    if s.searches
                        .get(&key)
                        .is_some_and(|slot| slot.generation == generation)
                    {
                        let event = swofty_core::view::Event::Search { id, query };
                        s.send(&addon, &section, key.clone(), event, window, cx);
                    }
                })
                .ok();
            })
            .detach();
        });
        self.searches.insert(
            key.to_owned(),
            SearchSlot {
                state,
                query: String::new(),
                generation: 0,
                seen: true,
                _sub: sub,
            },
        );
    }

    pub(crate) fn query(&self, key: &str) -> Option<&str> {
        self.searches
            .get(key)
            .map(|s| s.query.as_str())
            .filter(|q| !q.trim().is_empty())
    }

    pub(crate) fn search_box(&self, key: &str, p: Palette) -> Option<AnyElement> {
        let slot = self.searches.get(key)?;
        Some(
            div()
                .w(px(280.))
                .child(
                    Input::new(&slot.state).small().cleanable(true).prefix(
                        Icon::new(IconName::Search)
                            .size(px(14.))
                            .text_color(p.text_placeholder),
                    ),
                )
                .into_any_element(),
        )
    }

    fn sync_form(
        &mut self,
        addon: &str,
        section: &str,
        form: &Form,
        generation: u64,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let key = format!("{addon}/{section}/{}", form.id);
        let roots = self.image_roots(addon);
        if !self.forms.contains_key(&key) {
            self.forms.insert(
                key.clone(),
                FormSlot {
                    inputs: HashMap::new(),
                    sliders: HashMap::new(),
                    selects: HashMap::new(),
                    picks: HashMap::new(),
                    synced: HashMap::new(),
                    reset: Reset::No,
                    seen: true,
                    _subs: Vec::new(),
                },
            );
        }
        let force = match self.forms[&key].reset {
            Reset::Now => true,
            Reset::After(g) => generation > g,
            Reset::No => false,
        };
        let mut subs = Vec::new();
        for field in &form.fields {
            let id = field.id().to_owned();
            let value = field.value();
            let current = self.field_value(&key, field, cx);
            let slot = self.forms.get_mut(&key).expect("inserted above");
            let synced = slot.synced.get(&id).cloned();
            let edited = synced.as_ref().is_some_and(|s| current.as_ref() != Some(s));
            let apply = force || synced.is_none() || (!edited && synced.as_ref() != Some(&value));
            match field {
                Field::Number(_) | Field::Text(_) if !slot.inputs.contains_key(&id) => {
                    let text = field_text(field, &value);
                    let state = cx.new(|cx| {
                        let mut s = InputState::new(window, cx).default_value(text);
                        if let Field::Number(nf) = field {
                            s = s.step(nf.step());
                            if let Some(min) = nf.min {
                                s = s.min(min);
                            }
                            if let Some(max) = nf.max {
                                s = s.max(max);
                            }
                        }
                        if let Field::Text(tf) = field
                            && let Some(ph) = tf.placeholder.clone()
                        {
                            s = s.placeholder(ph);
                        }
                        s
                    });
                    let (field_id, addon, section, form_id) =
                        (id.clone(), addon.to_owned(), section.to_owned(), form.id.clone());
                    subs.push(cx.subscribe_in(
                        &state,
                        window,
                        move |this, _, ev: &InputEvent, window, cx| {
                            let commit = matches!(ev, InputEvent::PressEnter { .. } | InputEvent::Blur);
                            if matches!(ev, InputEvent::Change) {
                                cx.notify();
                            }
                            if !commit {
                                return;
                            }
                            let Some(form) = this.find_form(&addon, &section, &form_id) else {
                                return;
                            };
                            if form.live {
                                this.submit_form(&addon, &section, &form, Some(&field_id), window, cx);
                            } else if matches!(ev, InputEvent::PressEnter { .. }) {
                                this.submit_form(&addon, &section, &form, None, window, cx);
                            }
                        },
                    ));
                    slot.inputs.insert(id.clone(), state);
                }
                Field::Number(_) | Field::Text(_) => {
                    if apply && let Some(state) = slot.inputs.get(&id).cloned() {
                        let text = field_text(field, &value);
                        state.update(cx, |s, cx| s.set_value(text, window, cx));
                    }
                }
                Field::Slider(nf) => {
                    let at = value.as_f64().unwrap_or(0.) as f32;
                    if let Some(state) = slot.sliders.get(&id).cloned() {
                        if apply {
                            state.update(cx, |s, cx| s.set_value(at, window, cx));
                        }
                    } else {
                        let state = cx.new(|_| {
                            SliderState::new()
                                .min(nf.min.unwrap_or(0.) as f32)
                                .max(nf.max.unwrap_or(100.) as f32)
                                .step(nf.step() as f32)
                                .default_value(at)
                        });
                        let (addon, section, form_id, field_id) =
                            (addon.to_owned(), section.to_owned(), form.id.clone(), id.clone());
                        subs.push(cx.subscribe_in(
                            &state,
                            window,
                            move |this, _, ev: &SliderEvent, window, cx| {
                                cx.notify();
                                if !matches!(ev, SliderEvent::Release(_)) {
                                    return;
                                }
                                if let Some(form) =
                                    this.find_form(&addon, &section, &form_id).filter(|f| f.live)
                                {
                                    this.submit_form(&addon, &section, &form, Some(&field_id), window, cx);
                                }
                            },
                        ));
                        slot.sliders.insert(id.clone(), state);
                    }
                }
                Field::Select(sf) => {
                    if force || slot.picks.get(&id) == Some(&value) {
                        slot.picks.remove(&id);
                    }
                    let current = slot.picks.get(&id).unwrap_or(&value);
                    let current = current.as_str().unwrap_or_default().to_owned();
                    if let Some(select) = slot.selects.get_mut(&id) {
                        let state = select.state.clone();
                        if select.options != sf.options {
                            select.options = sf.options.clone();
                            let items = opts(sf, &roots, &select.shown);
                            state.update(cx, |s, cx| s.set_items(items, window, cx));
                        }
                        if state.read(cx).selected_value() != Some(&current) {
                            state.update(cx, |s, cx| s.set_selected_value(&current, window, cx));
                        }
                    } else {
                        let shown = Rc::new(RefCell::new(shown_for(sf, &roots, &current)));
                        let items = opts(sf, &roots, &shown);
                        let at = items.position(&current);
                        let state = cx.new(|cx| {
                            let mut s = SelectState::new(items, at, window, cx).searchable(sf.search);
                            s.set_selected_index(at, window, cx);
                            s
                        });
                        let (key, field_id, addon, section, form_id) = (
                            key.clone(),
                            id.clone(),
                            addon.to_owned(),
                            section.to_owned(),
                            form.id.clone(),
                        );
                        subs.push(cx.subscribe_in(
                            &state,
                            window,
                            move |this, state, ev: &SelectEvent<Opts>, window, cx| {
                                let SelectEvent::Confirm(Some(v)) = ev else {
                                    return;
                                };
                                state.update(cx, |s, cx| s.set_selected_value(v, window, cx));
                                let live = this
                                    .find_form(&addon, &section, &form_id)
                                    .filter(|f| f.live)
                                    .map(|f| (addon.clone(), section.clone(), f));
                                this.pick(&key, &field_id, Value::String(v.clone()), live, window, cx);
                            },
                        ));
                        slot.selects.insert(
                            id.clone(),
                            SelectSlot {
                                state,
                                options: sf.options.clone(),
                                shown,
                            },
                        );
                    }
                }
                Field::Toggle(_) | Field::Colour(_) => {
                    if force || slot.picks.get(&id) == Some(&value) {
                        slot.picks.remove(&id);
                    }
                }
            }
            let slot = self.forms.get_mut(&key).expect("inserted above");
            if apply {
                slot.synced.insert(id, value);
            }
        }
        let slot = self.forms.get_mut(&key).expect("inserted above");
        if force {
            slot.reset = Reset::No;
        }
        slot.seen = true;
        slot._subs.extend(subs);
    }

    fn field_value(&self, key: &str, field: &Field, cx: &App) -> Option<Value> {
        let slot = self.forms.get(key)?;
        let id = field.id();
        match field {
            Field::Number(nf) => {
                let text = slot.inputs.get(id)?.read(cx).value().to_string();
                if text.trim().is_empty() {
                    return nf.value.is_none().then_some(Value::Null);
                }
                number_text(&text).map(|v| nf.json(nf.clamp(v)))
            }
            Field::Slider(nf) => {
                let v = slot.sliders.get(id)?.read(cx).value().start() as f64;
                Some(nf.json(nf.clamp(v)))
            }
            Field::Text(tf) => {
                let mut text = slot.inputs.get(id)?.read(cx).value().to_string();
                if let Some(max) = tf.max_len {
                    text = text.chars().take(max).collect();
                }
                Some(Value::String(text))
            }
            Field::Select(_) | Field::Toggle(_) | Field::Colour(_) => {
                Some(slot.picks.get(id).cloned().unwrap_or_else(|| field.value()))
            }
        }
    }

    fn form_changes(&self, key: &str, form: &Form, cx: &App) -> (Vec<String>, bool) {
        let mut changed = Vec::new();
        let mut valid = true;
        for f in &form.fields {
            match self.field_value(key, f, cx) {
                Some(v) if v != f.value() => changed.push(f.id().to_owned()),
                Some(_) => {}
                None if self
                    .forms
                    .get(key)
                    .is_some_and(|s| !s.inputs.contains_key(f.id()) && !s.sliders.contains_key(f.id())) => {}
                None => {
                    valid = false;
                    changed.push(f.id().to_owned());
                }
            }
        }
        (changed, valid)
    }

    pub(crate) fn submit_form(
        &mut self,
        addon: &str,
        section: &str,
        form: &Form,
        only: Option<&str>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let key = format!("{addon}/{section}/{}", form.id);
        if self.busy.contains(&key) || form.disabled {
            return;
        }
        let mut values = Map::new();
        let mut changed = Vec::new();
        for f in form.fields.iter().filter(|f| only.is_none_or(|o| o == f.id())) {
            let Some(v) = self.field_value(&key, f, cx) else {
                let name = f.label().unwrap_or(f.id()).to_owned();
                window.push_notification(Notification::error(format!("{name} is not a number")), cx);
                return;
            };
            if v != f.value() {
                changed.push(f.id().to_owned());
            }
            values.insert(f.id().to_owned(), v);
        }
        if changed.is_empty() {
            return;
        }
        let event = swofty_core::view::Event::Submit {
            form: form.id.clone(),
            row: None,
            values,
            changed,
        };
        self.send(addon, section, key, event, window, cx);
    }

    fn reset_form(&mut self, key: &str, cx: &mut Context<Self>) {
        if let Some(slot) = self.forms.get_mut(key) {
            slot.reset = Reset::Now;
        }
        cx.notify();
    }

    pub(crate) fn settled(&mut self, control: &str, effect: &Result<swofty_core::view::Effect, String>) {
        use swofty_core::view::Effect;
        self.pending_cells.remove(control);
        if let Some(slot) = self.forms.get_mut(control) {
            slot.reset = match effect {
                Ok(Effect::Reload | Effect::Notice(_)) => Reset::After(self.view_gen),
                Ok(_) => Reset::Now,
                Err(_) => Reset::No,
            };
        }
    }

    fn pick(
        &mut self,
        key: &str,
        id: &str,
        value: Value,
        live: Option<(String, String, Form)>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        if let Some(slot) = self.forms.get_mut(key) {
            slot.picks.insert(id.to_owned(), value);
        }
        if let Some((addon, section, form)) = live {
            self.submit_form(&addon, &section, &form, Some(id), window, cx);
        }
        cx.notify();
    }

    pub(crate) fn rich_block(
        &self,
        at: At,
        b: &Block,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> Option<AnyElement> {
        Some(match b {
            Block::Hero(h) => self.hero(at, h, p, cx),
            Block::Pills(items) => pills(items, p).into_any_element(),
            Block::Tiles(tiles) => self.tiles(at, tiles, p),
            Block::Meters(meters) => self.meters(meters, p),
            Block::Card(c) => self.card(at, c, p, cx),
            Block::Grid(g) => self.grid(at, g, p, cx),
            Block::Form(f) => self.form(at, f, p, cx),
            Block::Tabs(t) => self.tabs(at, t, p, cx),
            Block::Image(i) => self.image(at, i, p),
            Block::DropZone(d) => self.drop_zone(at, d, p, cx),
            Block::Table(t) if !t.is_plain() => self.table(at, t, p, cx),
            _ => return None,
        })
    }

    fn drop_zone(&self, at: At, d: &DropZone, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        v_flex()
            .w_full()
            .when(at.fill, |x| x.flex_1())
            .min_h(px(280.))
            .items_center()
            .justify_center()
            .gap(px(12.))
            .px(px(32.))
            .py(px(36.))
            .rounded(px(10.))
            .border_1()
            .border_color(p.border)
            .bg(p.surface)
            .when_some(d.icon, |x, i| {
                x.child(
                    div()
                        .size(px(64.))
                        .rounded_full()
                        .bg(p.elevated)
                        .flex()
                        .items_center()
                        .justify_center()
                        .child(Icon::new(icon(i)).size(px(30.)).text_color(p.accent)),
                )
            })
            .child(
                div()
                    .text_size(px(20.))
                    .font_weight(FontWeight::SEMIBOLD)
                    .text_color(p.text)
                    .child(d.title.clone()),
            )
            .when(!d.text.is_empty(), |x| {
                x.child(
                    div()
                        .max_w(px(440.))
                        .text_center()
                        .text_size(px(13.))
                        .text_color(p.text_muted)
                        .child(d.text.clone()),
                )
            })
            .child(
                div()
                    .pt(px(8.))
                    .child(self.button(at.addon, at.section, &d.button, false, cx)),
            )
            .into_any_element()
    }

    fn tabs(&self, at: At, t: &Tabs, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let active = t.active.clone().or_else(|| t.items.first().map(|i| i.id.clone()));
        let items = t.items.iter().map(|item| {
            let on = active.as_deref() == Some(item.id.as_str());
            let control = format!("{}/{}/{}:{}", at.addon, at.section, t.id, item.id);
            let busy = self.busy.contains(&control);
            let fg = if item.disabled {
                p.text_placeholder
            } else if on {
                p.on_accent
            } else {
                p.text_muted
            };
            let mut tab = div()
                .id(SharedString::from(format!("tab-{control}")))
                .h(px(32.))
                .px(px(14.))
                .flex()
                .items_center()
                .gap(px(7.))
                .rounded(px(7.))
                .text_size(px(13.))
                .font_weight(if on {
                    FontWeight::SEMIBOLD
                } else {
                    FontWeight::MEDIUM
                })
                .text_color(fg)
                .whitespace_nowrap()
                .when(on, |d| d.bg(p.accent))
                .when_some(item.icon, |d, i| d.child(Icon::new(icon(i)).size(px(14.))))
                .child(item.label.clone())
                .when_some(item.badge.clone(), |d, badge| {
                    d.child(
                        div()
                            .h(px(18.))
                            .min_w(px(18.))
                            .px(px(5.))
                            .flex()
                            .items_center()
                            .justify_center()
                            .rounded(px(9.))
                            .text_size(px(10.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .bg(if on { p.on_accent.opacity(0.22) } else { p.elevated })
                            .text_color(if on { p.on_accent } else { p.text })
                            .child(badge),
                    )
                })
                .when(busy, |d| d.opacity(0.6));
            if !on && !item.disabled && !busy {
                let (addon, section, id, tab_id) = (
                    at.addon.to_owned(),
                    at.section.to_owned(),
                    t.id.clone(),
                    item.id.clone(),
                );
                tab = tab
                    .cursor_pointer()
                    .hover(|d| d.bg(p.hover).text_color(p.text))
                    .on_click(cx.listener(move |this, _, window, cx| {
                        cx.stop_propagation();
                        let event = swofty_core::view::Event::Tab {
                            id: id.clone(),
                            tab: tab_id.clone(),
                        };
                        this.send(&addon, &section, control.clone(), event, window, cx);
                    }));
            }
            tab
        });
        h_flex()
            .p(px(4.))
            .gap(px(4.))
            .flex_wrap()
            .rounded(px(10.))
            .border_1()
            .border_color(p.border_variant)
            .bg(p.surface)
            .children(items)
            .into_any_element()
    }

    fn hero(&self, at: At, h: &Hero, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let buttons: Vec<AnyElement> = h
            .buttons
            .iter()
            .map(|b| self.button(at.addon, at.section, b, true, cx))
            .collect();
        let facts = (!h.facts.is_empty()).then(|| {
            h_flex()
                .flex_wrap()
                .gap_x(px(28.))
                .gap_y(px(10.))
                .pt(px(14.))
                .border_t_1()
                .border_color(p.border_variant)
                .children(h.facts.iter().map(|f| {
                    v_flex()
                        .gap(px(3.))
                        .child(label(f.label.to_uppercase(), p))
                        .child(div().text_size(px(13.)).text_color(p.text).child(f.value.clone()))
                }))
        });
        surface(p)
            .p(px(20.))
            .gap(px(16.))
            .child(
                h_flex()
                    .gap(px(14.))
                    .items_center()
                    .when(!h.art.is_none(), |d| d.child(art(&h.art, 48., p)))
                    .child(
                        v_flex()
                            .flex_1()
                            .min_w_0()
                            .gap(px(6.))
                            .child(
                                h_flex()
                                    .gap(px(10.))
                                    .flex_wrap()
                                    .child(
                                        div()
                                            .text_size(px(20.))
                                            .font_weight(FontWeight::SEMIBOLD)
                                            .text_color(p.text)
                                            .child(h.title.clone()),
                                    )
                                    .child(pills(&h.pills, p)),
                            )
                            .when_some(h.subtitle.clone(), |d, s| {
                                d.child(div().text_size(px(13.)).text_color(p.text_muted).child(s))
                            }),
                    )
                    .when(!buttons.is_empty(), |d| {
                        d.child(h_flex().gap(px(6.)).children(buttons))
                    }),
            )
            .children(facts)
            .into_any_element()
    }

    fn tiles(&self, at: At, tiles: &[Tile], p: Palette) -> AnyElement {
        let cols = tile_columns(tiles.len(), at.width);
        let tile = |t: &Tile| {
            let value_color = tone(t.tone, p);
            v_flex()
                .flex_1()
                .flex_basis(px(0.))
                .min_w_0()
                .px(px(16.))
                .pt(px(14.))
                .pb(px(if t.progress.is_some() { 12. } else { 14. }))
                .gap(px(6.))
                .rounded(px(8.))
                .border_1()
                .border_color(p.border_variant)
                .bg(p.surface)
                .child(
                    h_flex()
                        .gap(px(8.))
                        .child(label(t.label.to_uppercase(), p).flex_1().min_w_0().truncate())
                        .when_some(t.icon, |d, i| {
                            d.child(Icon::new(icon(i)).size(px(14.)).text_color(p.text_placeholder))
                        }),
                )
                .child(
                    h_flex()
                        .gap(px(8.))
                        .items_baseline()
                        .child(
                            div()
                                .text_size(px(24.))
                                .line_height(px(30.))
                                .font_weight(FontWeight::SEMIBOLD)
                                .text_color(value_color)
                                .truncate()
                                .child(t.value.clone()),
                        )
                        .when_some(t.delta.clone(), |d, delta| {
                            d.child(
                                div()
                                    .text_size(px(12.))
                                    .font_weight(FontWeight::MEDIUM)
                                    .text_color(tone(t.delta_tone(), p))
                                    .whitespace_nowrap()
                                    .child(delta),
                            )
                        }),
                )
                .when_some(t.hint.clone(), |d, h| {
                    d.child(
                        div()
                            .text_size(px(12.))
                            .text_color(p.text_muted)
                            .truncate()
                            .child(h),
                    )
                })
                .when_some(t.progress, |d, f| {
                    let color = fill_tone(Some(t.tone), p);
                    d.child(div().pt(px(4.)).child(bar(f, color, 3., p)))
                })
                .into_any_element()
        };
        v_flex()
            .gap(px(12.))
            .children(tiles.chunks(cols).map(|row| {
                h_flex()
                    .items_stretch()
                    .gap(px(12.))
                    .children(row.iter().map(tile))
                    .children((row.len()..cols).map(|_| div().flex_1().flex_basis(px(0.))))
            }))
            .into_any_element()
    }

    fn image(&self, at: At, i: &ImageBlock, p: Palette) -> AnyElement {
        let width = i.width.clamp(0., at.width.max(0.));
        let height = i.height.max(0.);
        let border = if i.transparent { 0. } else { 2. };
        let inner = ((width - border).max(1.), (height - border).max(1.));
        let fit = match i.fit {
            Fit::Cover => ObjectFit::Cover,
            Fit::Contain => ObjectFit::Contain,
        };
        v_flex()
            .w(px(width))
            .gap(px(6.))
            .child(
                div()
                    .w(px(width))
                    .h(px(height))
                    .rounded(px(6.))
                    .overflow_hidden()
                    .when(!i.transparent, |d| {
                        d.bg(p.surface).border_1().border_color(p.border_variant)
                    })
                    .when_some(self.image_roots(at.addon).allow(&i.path), |d, path| {
                        d.child(
                            img(path)
                                .w(px(inner.0))
                                .h(px(inner.1))
                                .aspect_ratio(inner.0 / inner.1)
                                .object_fit(fit),
                        )
                    }),
            )
            .when_some(i.caption.clone(), |d, c| {
                d.child(div().text_size(px(12.)).text_color(p.text_muted).child(c))
            })
            .into_any_element()
    }

    fn meters(&self, meters: &[Meter], p: Palette) -> AnyElement {
        v_flex()
            .gap(px(14.))
            .children(meters.iter().map(|m| {
                v_flex()
                    .gap(px(7.))
                    .child(
                        h_flex()
                            .gap(px(12.))
                            .child(
                                div()
                                    .flex_1()
                                    .min_w_0()
                                    .truncate()
                                    .text_size(px(13.))
                                    .text_color(p.text)
                                    .child(m.label.clone()),
                            )
                            .child(
                                div()
                                    .text_size(px(12.))
                                    .text_color(p.text_muted)
                                    .whitespace_nowrap()
                                    .child(m.text()),
                            ),
                    )
                    .child(bar(m.fraction(), fill_tone(m.tone, p), 6., p))
            }))
            .into_any_element()
    }

    fn card(&self, at: At, c: &Card, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let inner = at.inner(at.width - 34.);
        let head = c.title.is_some() || c.subtitle.is_some() || !c.pills.is_empty() || !c.buttons.is_empty();
        let buttons: Vec<AnyElement> = c
            .buttons
            .iter()
            .map(|b| self.button(at.addon, at.section, b, true, cx))
            .collect();
        surface(p)
            .when(c.dashed, |d| {
                d.border_2().border_dashed().border_color(p.text_muted)
            })
            .when(at.fill, |d| d.flex_1())
            .p(px(16.))
            .gap(px(14.))
            .when(head, |d| {
                d.child(
                    h_flex()
                        .gap(px(10.))
                        .min_h(px(24.))
                        .child(
                            v_flex()
                                .flex_1()
                                .min_w_0()
                                .gap(px(2.))
                                .when_some(c.title.clone(), |d, t| {
                                    d.child(
                                        div()
                                            .text_size(px(14.))
                                            .font_weight(FontWeight::SEMIBOLD)
                                            .text_color(p.text)
                                            .truncate()
                                            .child(t),
                                    )
                                })
                                .when_some(c.subtitle.clone(), |d, s| {
                                    d.child(div().text_size(px(12.)).text_color(p.text_muted).child(s))
                                }),
                        )
                        .child(pills(&c.pills, p))
                        .children(buttons),
                )
            })
            .children(c.blocks.iter().map(|b| self.block_at(inner, b, p, cx)))
            .into_any_element()
    }

    fn grid(&self, at: At, g: &Grid, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let mut weights = g.columns.weights();
        let fits = ((at.width + GAP) / (GRID_COLUMN + GAP)).floor().max(1.) as usize;
        if weights.len() > fits {
            weights = vec![1.];
        }
        let cols = weights.len();
        let total: f32 = weights.iter().sum();
        let usable = at.width - GAP * (cols as f32 - 1.);
        v_flex()
            .gap(px(GAP))
            .children(g.blocks.chunks(cols).map(|row| {
                h_flex()
                    .items_stretch()
                    .gap(px(GAP))
                    .children(row.iter().zip(&weights).map(|(b, w)| {
                        let cell = At {
                            width: usable * w / total,
                            fill: true,
                            ..at
                        };
                        v_flex()
                            .flex_basis(px(0.))
                            .flex_grow(*w)
                            .min_w_0()
                            .child(self.block_at(cell, b, p, cx))
                    }))
                    .children(
                        weights[row.len()..]
                            .iter()
                            .map(|w| div().flex_basis(px(0.)).flex_grow(*w)),
                    )
            }))
            .into_any_element()
    }

    fn form(&self, at: At, f: &Form, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let key = at.key(&f.id);
        let busy = self.busy.contains(&key);
        let cols = f.columns.unwrap_or(1).clamp(1, 3);
        let cols = cols.min(((at.width + GAP) / (220. + GAP)).floor().max(1.) as usize);
        let (changed, valid) = self.form_changes(&key, f, cx);
        let fields: Vec<AnyElement> = f
            .fields
            .iter()
            .map(|field| {
                let dirty = changed.iter().any(|c| c == field.id());
                self.field(at, &key, f, field, dirty, busy, p, cx)
            })
            .collect();
        let mut rows: Vec<AnyElement> = Vec::new();
        let mut fields = fields.into_iter().peekable();
        while fields.peek().is_some() {
            let row: Vec<AnyElement> = fields.by_ref().take(cols).collect();
            let pad = cols - row.len();
            rows.push(
                h_flex()
                    .items_start()
                    .gap(px(20.))
                    .children(row)
                    .children((0..pad).map(|_| div().flex_1().flex_basis(px(0.))))
                    .into_any_element(),
            );
        }
        let footer = (!f.live).then(|| {
            let (reset_key, apply_form) = (key.clone(), f.clone());
            let (addon, section) = (at.addon.to_owned(), at.section.to_owned());
            let count = changed.len();
            h_flex()
                .gap(px(8.))
                .pt(px(4.))
                .child(
                    div()
                        .flex_1()
                        .text_size(px(12.))
                        .text_color(p.text_muted)
                        .child(match count {
                            0 => String::new(),
                            1 => "1 unsaved change".to_owned(),
                            n => format!("{n} unsaved changes"),
                        }),
                )
                .when(count > 0 && !busy, |d| {
                    d.child(
                        Button::new(SharedString::from(format!("reset-{key}")))
                            .ghost()
                            .small()
                            .label("Reset")
                            .on_click(cx.listener(move |this, _, _, cx| this.reset_form(&reset_key, cx))),
                    )
                })
                .child(
                    Button::new(SharedString::from(format!("apply-{key}")))
                        .primary()
                        .small()
                        .label(f.apply.clone().unwrap_or_else(|| "Apply".into()))
                        .loading(busy)
                        .disabled(busy || f.disabled || count == 0 || !valid)
                        .on_click(cx.listener(move |this, _, window, cx| {
                            this.submit_form(&addon, &section, &apply_form, None, window, cx)
                        })),
                )
        });
        v_flex()
            .gap(px(16.))
            .when(cols == 1, |d| d.max_w(px(520.)))
            .children(rows)
            .children(footer)
            .into_any_element()
    }

    #[allow(clippy::too_many_arguments)]
    fn field(
        &self,
        at: At,
        key: &str,
        form: &Form,
        field: &Field,
        dirty: bool,
        busy: bool,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let id = field.id().to_owned();
        let disabled = form.disabled || field.disabled() || busy;
        let slot = self.forms.get(key);
        let control_id = SharedString::from(format!("field-{key}/{id}"));
        let live = form
            .live
            .then(|| (at.addon.to_owned(), at.section.to_owned(), form.clone()));
        let control: AnyElement = match field {
            Field::Number(nf) => match slot.and_then(|s| s.inputs.get(&id)) {
                Some(state) => NumberInput::new(state)
                    .small()
                    .disabled(disabled)
                    .when_some(nf.unit.clone(), |n, u| {
                        n.suffix(div().text_size(px(12.)).text_color(p.text_placeholder).child(u))
                    })
                    .into_any_element(),
                None => div().into_any_element(),
            },
            Field::Text(_) => match slot.and_then(|s| s.inputs.get(&id)) {
                Some(state) => Input::new(state).small().disabled(disabled).into_any_element(),
                None => div().into_any_element(),
            },
            Field::Slider(nf) => match slot.and_then(|s| s.sliders.get(&id)) {
                Some(state) => {
                    let v = state.read(cx).value().start() as f64;
                    let text = format!(
                        "{}{}",
                        nf.text(nf.clamp(v)),
                        nf.unit.as_ref().map(|u| format!(" {u}")).unwrap_or_default()
                    );
                    h_flex()
                        .h(px(28.))
                        .gap(px(12.))
                        .child(Slider::new(state).disabled(disabled).flex_1())
                        .child(
                            div()
                                .min_w(px(64.))
                                .flex()
                                .justify_end()
                                .text_size(px(13.))
                                .text_color(p.text)
                                .whitespace_nowrap()
                                .child(text),
                        )
                        .into_any_element()
                }
                None => div().into_any_element(),
            },
            Field::Select(sf) => match slot.and_then(|s| s.selects.get(&id)) {
                Some(select) => {
                    let current = slot
                        .and_then(|s| s.picks.get(&id))
                        .cloned()
                        .unwrap_or_else(|| field.value());
                    let committed = shown_for(
                        sf,
                        &self.image_roots(at.addon),
                        current.as_str().unwrap_or_default(),
                    );
                    let label = committed.label.clone();
                    select.shown.replace(committed);
                    Select::new(&select.state)
                        .id(control_id)
                        .small()
                        .w_full()
                        .menu_max_h(px(SELECT_MENU_H))
                        .search_placeholder("Search")
                        .placeholder(label)
                        .disabled(disabled)
                        .into_any_element()
                }
                None => div().into_any_element(),
            },
            Field::Colour(cf) => {
                let colour_key = format!("{key}/{id}");
                let rgb = self.colour_shown(&colour_key, cx).or_else(|| {
                    slot.and_then(|s| s.picks.get(&id))
                        .cloned()
                        .unwrap_or_else(|| field.value())
                        .as_str()
                        .and_then(parse_colour)
                });
                let open = self.colour.as_ref().is_some_and(|c| c.key == colour_key);
                let overlay = self.colour_overlay(&colour_key, cx);
                let target = ColourTarget::Form {
                    key: key.to_owned(),
                    field: id.clone(),
                    addon: at.addon.to_owned(),
                    section: at.section.to_owned(),
                    form: form.id.clone(),
                };
                let start = rgb.unwrap_or([0xFF, 0xFF, 0xFF]);
                h_flex()
                    .id(control_id)
                    .relative()
                    .h(px(24.))
                    .px(px(9.))
                    .gap(px(8.))
                    .rounded(px(5.))
                    .border_1()
                    .border_color(if open { p.accent } else { p.border })
                    .bg(p.background)
                    .child(swatch(rgb, 12., p))
                    .when(cf.hex, |d| {
                        d.child(
                            div()
                                .text_size(px(13.))
                                .text_color(if rgb.is_some() { p.text } else { p.text_placeholder })
                                .child(rgb.map_or_else(|| "None".to_owned(), colour_hex)),
                        )
                    })
                    .map(|d| match disabled {
                        true => d.opacity(0.5),
                        false => d
                            .cursor_pointer()
                            .on_click(cx.listener(move |this, _, window, cx| {
                                this.open_colour(colour_key.clone(), target.clone(), start, window, cx)
                            })),
                    })
                    .children(overlay)
                    .into_any_element()
            }
            Field::Toggle(_) => {
                let on = slot
                    .and_then(|s| s.picks.get(&id))
                    .cloned()
                    .unwrap_or_else(|| field.value())
                    == Value::Bool(true);
                let (key, id) = (key.to_owned(), id.clone());
                h_flex()
                    .h(px(28.))
                    .gap(px(10.))
                    .child(
                        Switch::new(control_id)
                            .checked(on)
                            .small()
                            .disabled(disabled)
                            .on_change(cx.listener(move |this, on: &bool, window, cx| {
                                this.pick(&key, &id, Value::Bool(*on), live.clone(), window, cx)
                            })),
                    )
                    .child(
                        div()
                            .text_size(px(13.))
                            .text_color(if on { p.text } else { p.text_muted })
                            .child(if on { "On" } else { "Off" }),
                    )
                    .into_any_element()
            }
        };
        v_flex()
            .flex_1()
            .flex_basis(px(0.))
            .min_w_0()
            .gap(px(6.))
            .child(
                h_flex()
                    .gap(px(6.))
                    .child(
                        div()
                            .text_size(px(12.))
                            .font_weight(FontWeight::MEDIUM)
                            .text_color(p.text_muted)
                            .child(field.label().unwrap_or(field.id()).to_owned()),
                    )
                    .when(dirty, |d| d.child(div().size(px(6.)).rounded_full().bg(p.accent))),
            )
            .child(control)
            .when_some(field.help().map(str::to_owned), |d, h| {
                d.child(
                    div()
                        .text_size(px(12.))
                        .line_height(px(17.))
                        .text_color(p.text_placeholder)
                        .child(h),
                )
            })
            .into_any_element()
    }

    fn table(&self, at: At, t: &Table, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let table_key = at.key(&format!("table:{}", t.id()));
        let sort = self
            .sorts
            .get(&table_key)
            .cloned()
            .or_else(|| t.sort.as_ref().map(|s| (s.column.clone(), s.desc)));
        let search_key = t.search.as_ref().map(|s| at.key(&search_id(s, t.id())));
        let query = search_key
            .as_deref()
            .filter(|_| t.search.as_ref().is_some_and(|s| s.filter))
            .and_then(|k| self.query(k));
        let order: Vec<usize> = sorted_rows(t, sort.as_ref())
            .into_iter()
            .filter(|i| query.is_none_or(|q| Search::matches(q, t.rows[*i].cells().iter().map(Cell::text))))
            .collect();
        let (base_h, text) = if t.compact { (30., 12.) } else { (36., 13.) };
        let row_h = t.row_height.map_or(base_h, |h| h.max(base_h));
        // The row whose detail is open, while it is still shown.
        let selected = self.table_rows.get(&table_key).and_then(|id| {
            order
                .iter()
                .map(|&i| (i, &t.rows[i]))
                .find(|(i, r)| r.id(*i) == *id && !r.detail().is_empty())
        });
        let selected_id = selected.map(|(i, r)| r.id(i));
        let specs: Vec<_> = t.columns.iter().map(|c| c.spec()).collect();
        let sized = |d: Div, i: usize| -> Div {
            let spec = &specs[i];
            let d = match spec.width {
                Some(w) => d.w(px(w)).flex_shrink_0(),
                None => d.flex_1().flex_basis(px(0.)).min_w_0(),
            };
            match spec.align {
                Align::Left => d.justify_start(),
                Align::Right => d.justify_end(),
                Align::Center => d.justify_center(),
            }
        };
        let header = h_flex()
            .h(px(base_h - 2.))
            .px(px(12.))
            .gap(px(12.))
            .border_b_1()
            .border_color(p.border_variant)
            .children(t.columns.iter().enumerate().map(|(i, c)| {
                let spec = &specs[i];
                let column = c.key().to_owned();
                let active = sort.as_ref().filter(|(k, _)| *k == column).map(|(_, d)| *d);
                // A pencil for cells edited as text; a switch shows it can be flipped.
                let editable = t.rows.iter().any(|r| {
                    r.cells().get(i).is_some_and(|c| {
                        matches!(c, Cell::Spec(s) if s.edit.as_ref().is_some_and(|e| !matches!(e, Field::Toggle(_))))
                    })
                });
                let head = sized(h_flex().gap(px(4.)), i)
                    .id(SharedString::from(format!("{table_key}/h/{i}")))
                    .text_size(px(11.))
                    .font_weight(FontWeight::MEDIUM)
                    .text_color(if active.is_some() {
                        p.text
                    } else {
                        p.text_placeholder
                    })
                    .child(div().truncate().child(spec.title.to_uppercase()))
                    .when(editable, |d| {
                        d.child(
                            Icon::new(IconName::Pencil)
                                .size(px(10.))
                                .text_color(p.text_placeholder),
                        )
                    })
                    .when_some(active, |d, desc| {
                        d.child(
                            Icon::new(if desc {
                                IconName::ChevronDown
                            } else {
                                IconName::ChevronUp
                            })
                            .size(px(12.))
                            .text_color(p.accent),
                        )
                    });
                if spec.sort {
                    let key = table_key.clone();
                    head.cursor_pointer()
                        .hover(move |s| s.text_color(p.text))
                        .on_click(cx.listener(move |this, _, _, cx| {
                            let desc = active.is_some_and(|d| !d);
                            this.sorts.insert(key.clone(), (column.clone(), desc));
                            cx.notify();
                        }))
                        .into_any_element()
                } else {
                    head.into_any_element()
                }
            }));
        // The open row's detail goes beside the table when every column still fits, else under
        // its row, where it was clicked.
        let needed = 24.
            + specs
                .iter()
                .map(|c| c.width.unwrap_or(DETAIL_COLUMN_MIN))
                .sum::<f32>()
            + 12. * specs.len().saturating_sub(1) as f32;
        let beside = at.width - DETAIL_W - GAP >= needed;
        // A long table draws the rows in and near the page's view and leaves room for the rest.
        let shown = self.visible_rows(at, &table_key, order.len(), row_h, selected.is_some() && !beside);
        let (above, below) = (shown.start, order.len() - shown.end);
        let rows: Vec<AnyElement> = order
            .iter()
            .enumerate()
            .skip(shown.start)
            .take(shown.len())
            .flat_map(|(n, &ri)| {
                let row = &t.rows[ri];
                let row_id = row.id(ri);
                let row_tone = row.tone();
                let opens = !row.detail().is_empty();
                let open = selected_id.as_deref() == Some(row_id.as_str());
                let (key, id) = (table_key.clone(), row_id.clone());
                let line = h_flex()
                    .id(SharedString::from(format!("{table_key}/r/{row_id}")))
                    .h(px(row_h))
                    .px(px(12.))
                    .gap(px(12.))
                    .when(n > 0, |d| d.border_t_1().border_color(p.border_variant))
                    .when(open, |d| d.bg(p.selected))
                    .when(opens && !open, |d| {
                        d.cursor_pointer().hover(move |s| s.bg(p.hover))
                    })
                    .when(opens, |d| {
                        // A click opens the row's detail, or closes it when it is open.
                        d.on_click(cx.listener(move |this, _, _, cx| {
                            if this.table_rows.get(&key) == Some(&id) {
                                this.table_rows.remove(&key);
                            } else {
                                this.table_rows.insert(key.clone(), id.clone());
                            }
                            cx.notify();
                        }))
                    })
                    .children(
                        row.cells()
                            .iter()
                            .take(specs.len())
                            .enumerate()
                            .map(|(ci, cell)| {
                                let content = self.cell(
                                    at,
                                    t,
                                    &table_key,
                                    &row_id,
                                    &specs[ci],
                                    t.columns[ci].key(),
                                    cell,
                                    row_tone,
                                    (text, row_h),
                                    p,
                                    cx,
                                );
                                sized(h_flex(), ci).child(content).into_any_element()
                            }),
                    )
                    .into_any_element();
                let under = (open && !beside).then(|| self.row_detail(at, &table_key, row, None, p, cx));
                std::iter::once(line).chain(under)
            })
            .collect();
        let empty = order.is_empty().then(|| {
            div()
                .px(px(12.))
                .py(px(14.))
                .text_size(px(13.))
                .text_color(p.text_muted)
                .child(t.empty.clone().unwrap_or_else(|| "No matches".into()))
        });
        let body = v_flex()
            .w_full()
            .overflow_hidden()
            .when(!at.nested, |d| {
                d.rounded(px(6.)).border_1().border_color(p.border_variant)
            })
            .child(header)
            .child(
                v_flex()
                    .w_full()
                    .relative()
                    .child(self.rows_tracker(at, &table_key))
                    .when(above > 0, |d| {
                        d.child(div().flex_shrink_0().h(px(above as f32 * row_h)))
                    })
                    .children(rows)
                    .when(below > 0, |d| {
                        d.child(div().flex_shrink_0().h(px(below as f32 * row_h)))
                    }),
            )
            .children(empty);
        let detail = selected
            .filter(|_| beside)
            .map(|(_, r)| self.row_detail(at, &table_key, r, Some(DETAIL_W), p, cx));
        let table = match detail {
            Some(detail) => h_flex()
                .items_start()
                .gap(px(GAP))
                .child(div().flex_1().min_w_0().child(body))
                .child(detail)
                .into_any_element(),
            None => body.into_any_element(),
        };
        v_flex()
            .gap(px(10.))
            .children(search_key.and_then(|k| self.search_box(&k, p)))
            .child(table)
            .into_any_element()
    }

    /// The rows of `n` worth drawing: those within the page's view, plus a few either side, for
    /// a long table on a scrolled page whose rows are all `row_h` tall (`varied` when an open
    /// row's detail sits under it). Every row on the first frame, before the rows' place is known.
    fn visible_rows(&self, at: At, key: &str, n: usize, row_h: f32, varied: bool) -> std::ops::Range<usize> {
        const OVERSCAN: usize = 8;
        if varied || n <= LONG_TABLE {
            return 0..n;
        }
        let Some((scroll, _)) = self.section_scrolls.get(&format!("{}/{}", at.addon, at.section)) else {
            return 0..n;
        };
        let Some(top) = self.table_tops.borrow().get(key).copied() else {
            return 0..n;
        };
        let view_h = f32::from(scroll.bounds().size.height);
        if view_h <= 0. {
            return 0..n;
        }
        // The view's top, from the rows' top.
        let from = -f32::from(scroll.offset().y) - top;
        let first = ((from / row_h).floor().max(0.) as usize)
            .saturating_sub(OVERSCAN)
            .min(n);
        let last = (((from + view_h) / row_h).ceil().max(0.) as usize + OVERSCAN).min(n);
        first..last.max(first)
    }

    /// An empty element over a table's rows that notes where they start in the page's scrolled
    /// content, for `visible_rows` on the next frame.
    fn rows_tracker(&self, at: At, key: &str) -> impl IntoElement {
        let tops = self.table_tops.clone();
        let scroll = self
            .section_scrolls
            .get(&format!("{}/{}", at.addon, at.section))
            .map(|(s, _)| s.clone());
        let key = key.to_owned();
        canvas(
            move |bounds, _, _| {
                if let Some(scroll) = scroll {
                    let top = bounds.origin.y - scroll.bounds().origin.y - scroll.offset().y;
                    tops.borrow_mut().insert(key, f32::from(top));
                }
            },
            |_, _, _, _| {},
        )
        .absolute()
        .inset_0()
    }

    /// A table row's detail panel: its title and subtitle, a close button and its blocks. A
    /// `width` puts it beside the table; without one it fills the table under its row.
    fn row_detail(
        &self,
        at: At,
        table_key: &str,
        row: &TableRow,
        width: Option<f32>,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let key = table_key.to_owned();
        let inner = At {
            width: width.unwrap_or(at.width) - 34.,
            nested: false,
            fill: false,
            ..at
        };
        v_flex()
            .map(|d| match width {
                Some(w) => d
                    .w(px(w))
                    .flex_shrink_0()
                    .rounded(px(8.))
                    .border_1()
                    .border_color(p.border_variant),
                None => d.w_full().border_t_1().border_color(p.border_variant),
            })
            .bg(p.surface)
            .overflow_hidden()
            .child(
                h_flex()
                    .px(px(16.))
                    .py(px(12.))
                    .gap(px(10.))
                    .border_b_1()
                    .border_color(p.border_variant)
                    .child(
                        v_flex()
                            .flex_1()
                            .min_w_0()
                            .gap(px(2.))
                            .child(
                                div()
                                    .text_size(px(15.))
                                    .font_weight(FontWeight::SEMIBOLD)
                                    .truncate()
                                    .child(row.title()),
                            )
                            .when_some(row.subtitle().map(str::to_owned), |d, s| {
                                d.child(
                                    div()
                                        .text_size(px(12.))
                                        .text_color(p.text_muted)
                                        .truncate()
                                        .child(s),
                                )
                            }),
                    )
                    .child(
                        Button::new(SharedString::from(format!("{table_key}/close")))
                            .ghost()
                            .small()
                            .icon(IconName::X)
                            .tooltip("Close")
                            .on_click(cx.listener(move |this, _, _, cx| {
                                this.table_rows.remove(&key);
                                cx.notify();
                            })),
                    ),
            )
            .child(
                v_flex()
                    .px(px(16.))
                    .py(px(14.))
                    .gap(px(16.))
                    .children(row.detail().iter().map(|b| self.block_at(inner, b, p, cx))),
            )
            .into_any_element()
    }

    #[allow(clippy::too_many_arguments)]
    fn cell(
        &self,
        at: At,
        t: &Table,
        table_key: &str,
        row_id: &str,
        spec: &swofty_core::view::ColumnSpec,
        column: &str,
        cell: &Cell,
        row_tone: Option<Tone>,
        (size, row_h): (f32, f32),
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let c = cell.spec();
        let key = format!("{table_key}/{row_id}/{column}");
        let color = c.tone.or(row_tone).map_or(p.text, |t| tone(t, p));
        if let Some(edit) = self.cell.as_ref().filter(|e| e.target.key == key) {
            let input = edit.input.clone();
            return div()
                .id(SharedString::from(format!("edit-{key}")))
                .w_full()
                .capture_key_down(cx.listener(|this, ev: &KeyDownEvent, _, cx| {
                    if ev.keystroke.key == "escape" {
                        this.cell = None;
                        cx.notify();
                    }
                }))
                .child(Input::new(&input).xsmall())
                .into_any_element();
        }
        // A switch cell keeps its pending state as "true" or "false", not as its text.
        let toggle = match &c.edit {
            Some(Field::Toggle(tf)) => Some(tf.clone()),
            _ => None,
        };
        let pending = toggle.is_none() && self.pending_cells.contains_key(&key);
        let shown = match pending {
            true => self.pending_cells[&key].clone(),
            false => c.text.clone(),
        };
        let text_el = match c.spans.is_empty() || pending {
            true => div().child(shown.clone()),
            // The spans as one text, each with its weight and colour.
            false => {
                let mut whole = String::new();
                let mut marks = Vec::new();
                for span in &c.spans {
                    let start = whole.len();
                    whole.push_str(&span.text);
                    if span.bold || span.tone.is_some() {
                        marks.push((
                            start..whole.len(),
                            HighlightStyle {
                                font_weight: span.bold.then_some(FontWeight::BOLD),
                                color: span.tone.map(|t| tone(t, p)),
                                ..HighlightStyle::default()
                            },
                        ));
                    }
                }
                div().child(StyledText::new(whole).with_highlights(marks))
            }
        }
        .min_w_0()
        .truncate()
        .text_size(px(size))
        .text_color(if pending { p.text_muted } else { color });
        let mut content = h_flex().gap(px(8.)).min_w_0();
        if spec.align == Align::Right {
            content = content.justify_end();
        }
        // A picture before the text: a square as tall as the row allows, or the cell's size.
        if let Some(image) = &c.image {
            let (w, h) = image.size((row_h - 8.).max(16.));
            let (w, h) = (w.clamp(1., 512.), h.clamp(1., (row_h - 4.).max(1.)));
            let fit = match image.fit() {
                Fit::Cover => ObjectFit::Cover,
                Fit::Contain => ObjectFit::Contain,
            };
            content = content.child(
                div()
                    .w(px(w))
                    .h(px(h))
                    .flex_shrink_0()
                    .rounded(px(if h > 24. { 6. } else { 2. }))
                    .overflow_hidden()
                    .when(!image.transparent(), |d| d.bg(p.elevated))
                    .when_some(self.image_roots(at.addon).allow(image.path()), |d, path| {
                        d.child(img(path).w(px(w)).h(px(h)).object_fit(fit))
                    }),
            );
        }
        if let Some(tf) = toggle {
            let on = self.pending_cells.get(&key).map_or(tf.value, |v| v == "true");
            let busy = self.busy.contains(&key);
            let (addon, section, table, row, column, control) = (
                at.addon.to_owned(),
                at.section.to_owned(),
                t.id().to_owned(),
                row_id.to_owned(),
                column.to_owned(),
                key.clone(),
            );
            let switch = Switch::new(SharedString::from(format!("cell-{key}")))
                .checked(on)
                .small()
                .disabled(busy || tf.disabled)
                .on_change(cx.listener(move |this, on: &bool, window, cx| {
                    this.pending_cells.insert(control.clone(), on.to_string());
                    let mut values = Map::new();
                    values.insert(column.clone(), Value::Bool(*on));
                    let event = swofty_core::view::Event::Submit {
                        form: table.clone(),
                        row: Some(row.clone()),
                        values,
                        changed: vec![column.clone()],
                    };
                    this.send(&addon, &section, control.clone(), event, window, cx);
                }));
            // The switch's click stays in the cell, so it doesn't also open the row.
            content = content.child(
                div()
                    .id(SharedString::from(format!("cell-hold-{key}")))
                    .flex_shrink_0()
                    .on_click(|_, _, cx| cx.stop_propagation())
                    .child(switch),
            );
            if !shown.is_empty() {
                content = content.child(text_el);
            }
            return content.into_any_element();
        }
        if let Some(Field::Colour(cf)) = &c.edit {
            let target = CellTarget {
                key: key.clone(),
                addon: at.addon.to_owned(),
                section: at.section.to_owned(),
                table: t.id().to_owned(),
                row: row_id.to_owned(),
                column: column.to_owned(),
            };
            let original = match cf.rgb().or_else(|| parse_colour(&c.text)) {
                Some(rgb) => Value::String(colour_hex(rgb)),
                None => Value::Null,
            };
            let rgb = self
                .colour_shown(&key, cx)
                .or_else(|| parse_colour(&shown).filter(|_| pending))
                .or_else(|| original.as_str().and_then(parse_colour));
            let (busy, disabled) = (self.busy.contains(&key), cf.disabled);
            let start = rgb.unwrap_or([0xFF, 0xFF, 0xFF]);
            let open_key = key.clone();
            return content
                .id(SharedString::from(format!("cell-{key}")))
                .relative()
                .px(px(6.))
                .mx(px(-6.))
                .h(px(size + 12.))
                .rounded(px(4.))
                .child(swatch(rgb, 14., p))
                .when(cf.hex, |d| d.child(text_el))
                .when(!disabled, |d| d.cursor_pointer())
                .when(!busy && !disabled, |d| d.hover(move |s| s.bg(p.hover)))
                .on_click(cx.listener(move |this, _, window, cx| {
                    cx.stop_propagation();
                    if disabled || this.busy.contains(&open_key) {
                        return;
                    }
                    let target = ColourTarget::Cell {
                        target: target.clone(),
                        original: original.clone(),
                    };
                    this.open_colour(open_key.clone(), target, start, window, cx)
                }))
                .children(self.colour_overlay(&key, cx))
                .into_any_element();
        }
        if let Some(f) = c.bar {
            content = content.child(div().w(px(56.)).flex_shrink_0().child(bar(
                f,
                fill_tone(c.tone, p),
                4.,
                p,
            )));
        }
        content = if c.pill {
            content.child(pill(
                &Pill {
                    text: shown,
                    tone: c.tone.unwrap_or_default(),
                    ..Pill::default()
                },
                p,
            ))
        } else {
            content.child(text_el)
        };
        let Some(field) = c.edit.clone() else {
            return content.into_any_element();
        };
        let (addon, section, table, row, column) = (
            at.addon.to_owned(),
            at.section.to_owned(),
            t.id().to_owned(),
            row_id.to_owned(),
            column.to_owned(),
        );
        let original = match field.value() {
            Value::Null => match &field {
                Field::Number(nf) => number_text(&c.text).map_or(Value::Null, |v| nf.json(v)),
                _ => Value::String(c.text.clone()),
            },
            v => v,
        };
        let busy = self.busy.contains(&key);
        content
            .id(SharedString::from(format!("cell-{key}")))
            .px(px(6.))
            .mx(px(-6.))
            .h(px(size + 12.))
            .rounded(px(4.))
            .cursor_pointer()
            .when(!busy, |d| d.hover(move |s| s.bg(p.hover)))
            .on_click(cx.listener(move |this, _, window, cx| {
                // Editing the cell doesn't also open the row.
                cx.stop_propagation();
                if this.busy.contains(&key) {
                    return;
                }
                this.edit_cell(
                    CellTarget {
                        key: key.clone(),
                        addon: addon.clone(),
                        section: section.clone(),
                        table: table.clone(),
                        row: row.clone(),
                        column: column.clone(),
                    },
                    field.clone(),
                    original.clone(),
                    window,
                    cx,
                )
            }))
            .into_any_element()
    }

    fn edit_cell(
        &mut self,
        target: CellTarget,
        field: Field,
        original: Value,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let text = field_text(&field, &original);
        let input = cx.new(|cx| {
            let mut s = InputState::new(window, cx).default_value(text);
            if let Field::Number(nf) = &field {
                s = s.step(nf.step());
            }
            if let Field::Text(tf) = &field
                && let Some(ph) = tf.placeholder.clone()
            {
                s = s.placeholder(ph);
            }
            s
        });
        input.update(cx, |s, cx| {
            s.focus(window, cx);
            s.select_all(window, cx);
        });
        let key = target.key.clone();
        let sub = cx.subscribe_in(&input, window, move |this, _, ev: &InputEvent, window, cx| {
            if matches!(ev, InputEvent::PressEnter { .. } | InputEvent::Blur)
                && this.cell.as_ref().is_some_and(|c| c.target.key == key)
            {
                this.commit_cell(window, cx);
            }
        });
        self.cell = Some(CellEdit {
            target,
            field,
            original,
            input,
            _subs: vec![sub],
        });
        cx.notify();
    }

    fn commit_cell(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(edit) = self.cell.take() else {
            return;
        };
        let text = edit.input.read(cx).value().to_string();
        let value = match &edit.field {
            Field::Number(nf) => match number_text(&text) {
                Some(v) => nf.json(nf.clamp(v)),
                None => {
                    window.push_notification(Notification::error(format!("{text} is not a number")), cx);
                    cx.notify();
                    return;
                }
            },
            Field::Text(tf) => Value::String(match tf.max_len {
                Some(max) => text.chars().take(max).collect(),
                None => text,
            }),
            _ => Value::Null,
        };
        cx.notify();
        if value == edit.original || value.is_null() {
            return;
        }
        let text = field_text(&edit.field, &value);
        self.submit_cell(edit.target, value, text, window, cx);
    }

    fn submit_cell(
        &mut self,
        target: CellTarget,
        value: Value,
        text: String,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        self.pending_cells.insert(target.key.clone(), text);
        let mut values = Map::new();
        values.insert(target.column.clone(), value);
        let event = swofty_core::view::Event::Submit {
            form: target.table,
            row: Some(target.row),
            values,
            changed: vec![target.column],
        };
        self.send(&target.addon, &target.section, target.key, event, window, cx);
    }

    fn colour_shown(&self, key: &str, cx: &App) -> Option<[u8; 3]> {
        let edit = self.colour.as_ref().filter(|c| c.key == key)?;
        Some(edit.picker.read(cx).rgb())
    }

    fn colour_overlay(&self, key: &str, cx: &mut Context<Self>) -> Vec<AnyElement> {
        let Some(edit) = self.colour.as_ref().filter(|c| c.key == key) else {
            return Vec::new();
        };
        let trigger = edit.trigger.clone();
        let picker = edit.picker.clone();
        vec![
            canvas(move |b, _, _| trigger.set(b), |_, _, _, _| {})
                .absolute()
                .size_full()
                .into_any_element(),
            div()
                .absolute()
                .top_full()
                .left_0()
                .child(
                    deferred(
                        anchored().snap_to_window_with_margin(px(8.)).child(
                            div()
                                .id("colour-overlay")
                                .mt(px(6.))
                                .occlude()
                                .on_mouse_down_out(cx.listener(|this, ev: &MouseDownEvent, window, cx| {
                                    let on_trigger = this
                                        .colour
                                        .as_ref()
                                        .is_some_and(|c| c.trigger.get().contains(&ev.position));
                                    if !on_trigger {
                                        this.close_colour(true, window, cx);
                                    }
                                }))
                                .child(picker),
                        ),
                    )
                    .with_priority(1),
                )
                .into_any_element(),
        ]
    }

    fn open_colour(
        &mut self,
        key: String,
        target: ColourTarget,
        start: [u8; 3],
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        if self.colour.as_ref().is_some_and(|c| c.key == key) {
            self.close_colour(true, window, cx);
            return;
        }
        self.close_colour(true, window, cx);
        let picker = cx.new(|cx| ColourPicker::new(start, window, cx));
        let subs = vec![
            cx.subscribe_in(&picker, window, |this, _, ev: &ColourEvent, window, cx| {
                this.close_colour(matches!(ev, ColourEvent::Commit), window, cx)
            }),
            cx.observe(&picker, |_, _, cx| cx.notify()),
        ];
        self.colour = Some(ColourEdit {
            key,
            target,
            picker,
            trigger: Rc::default(),
            _subs: subs,
        });
        cx.notify();
    }

    fn close_colour(&mut self, commit: bool, window: &mut Window, cx: &mut Context<Self>) {
        let Some(edit) = self.colour.take() else {
            return;
        };
        cx.notify();
        if !commit {
            return;
        }
        let hex = colour_hex(edit.picker.read(cx).rgb());
        match edit.target {
            ColourTarget::Form {
                key,
                field,
                addon,
                section,
                form,
            } => {
                let live = self
                    .find_form(&addon, &section, &form)
                    .filter(|f| f.live)
                    .map(|f| (addon, section, f));
                self.pick(&key, &field, Value::String(hex), live, window, cx);
            }
            ColourTarget::Cell { target, original } => {
                let value = Value::String(hex.clone());
                if value != original {
                    self.submit_cell(target, value, hex, window, cx);
                }
            }
        }
    }
}

#[derive(Clone)]
pub(crate) struct CellTarget {
    key: String,
    addon: String,
    section: String,
    table: String,
    row: String,
    column: String,
}
