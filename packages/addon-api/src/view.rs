use std::path::PathBuf;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SectionSpec {
    pub id: String,
    pub title: String,
    pub icon: Icon,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct View {
    pub subtitle: String,
    #[serde(default)]
    pub actions: Vec<Button>,
    #[serde(default)]
    pub notices: Vec<Notice>,
    pub body: Body,
    #[serde(default)]
    pub footer: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub tick: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Body {
    List {
        rows: Vec<Row>,
        reorder: bool,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        search: Option<Search>,
    },
    Empty {
        title: String,
        text: String,
        action: Option<Button>,
    },
    Blocks(Vec<Block>),
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Row {
    pub id: String,
    pub title: String,
    pub subtitle: String,
    #[serde(default)]
    pub art: Art,
    #[serde(default)]
    pub tags: Vec<Tag>,
    #[serde(default)]
    pub trailing: Option<Control>,
    #[serde(default)]
    pub detail: Vec<Block>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub menu: Vec<Button>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Block {
    Stats(Vec<Fact>),
    Facts(Vec<Fact>),
    Label(String),
    Text { text: String, tone: Tone },
    Names { label: String, items: Vec<String> },
    Table(Table),
    Buttons(Vec<Button>),
    Hero(Hero),
    Pills(Vec<Pill>),
    Tiles(Vec<Tile>),
    Meters(Vec<Meter>),
    Card(Card),
    Grid(Grid),
    Form(Form),
    Tabs(Tabs),
    Image(Image),
    DropZone(DropZone),
}

/// A large drop target with one main call to action, such as the Simplified Importer's
/// Import a mod: an icon, a title, a line of text and a big button, centred in a
/// tall area. Files dropped on the window still go to the addon that claims them.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DropZone {
    pub title: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub text: String,
    pub button: Button,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon: Option<Icon>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Image {
    pub path: PathBuf,
    pub width: f32,
    pub height: f32,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub caption: Option<String>,
    #[serde(default)]
    pub fit: Fit,
    /// Draws the picture without the box behind it, so its transparent parts show what is
    /// under it.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub transparent: bool,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Fit {
    #[default]
    Cover,
    Contain,
}

/// A row of tabs. Clicking an enabled tab that isn't active sends `Event::Tab`.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Tabs {
    pub id: String,
    pub items: Vec<TabItem>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub active: Option<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct TabItem {
    pub id: String,
    pub label: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon: Option<Icon>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub badge: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub disabled: bool,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Hero {
    pub title: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub subtitle: Option<String>,
    #[serde(default, skip_serializing_if = "Art::is_none")]
    pub art: Art,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub pills: Vec<Pill>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub facts: Vec<Fact>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub buttons: Vec<Button>,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Pill {
    pub text: String,
    #[serde(default)]
    pub tone: Tone,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub dot: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon: Option<Icon>,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Tile {
    pub label: String,
    pub value: String,
    #[serde(default)]
    pub tone: Tone,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub delta: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub delta_tone: Option<Tone>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hint: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon: Option<Icon>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub progress: Option<f32>,
}

impl Tile {
    pub fn delta_tone(&self) -> Tone {
        if let Some(t) = self.delta_tone {
            return t;
        }
        match self.delta.as_deref().and_then(|d| d.trim_start().chars().next()) {
            Some('+') => Tone::Good,
            Some('-' | '\u{2212}') => Tone::Bad,
            _ => Tone::Muted,
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Meter {
    #[serde(default)]
    pub label: String,
    pub value: f64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub min: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub text: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tone: Option<Tone>,
}

impl Meter {
    pub fn fraction(&self) -> f32 {
        let (min, max) = (self.min.unwrap_or(0.), self.max.unwrap_or(100.));
        if max <= min || !self.value.is_finite() {
            return 0.;
        }
        ((self.value - min) / (max - min)).clamp(0., 1.) as f32
    }

    pub fn text(&self) -> String {
        self.text
            .clone()
            .unwrap_or_else(|| format!("{} / {}", number(self.value), number(self.max.unwrap_or(100.))))
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Card {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub subtitle: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub pills: Vec<Pill>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub buttons: Vec<Button>,
    #[serde(default)]
    pub blocks: Vec<Block>,
    /// Draw the card with a dashed outline, for a placeholder or drop target
    /// such as the Simplified Importer's list of imported mods.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub dashed: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Grid {
    #[serde(default)]
    pub columns: Columns,
    #[serde(default)]
    pub blocks: Vec<Block>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum Columns {
    Count(usize),
    Weights(Vec<f32>),
}

impl Default for Columns {
    fn default() -> Self {
        Self::Count(2)
    }
}

impl Columns {
    pub fn weights(&self) -> Vec<f32> {
        match self {
            Self::Count(n) => vec![1.; (*n).clamp(1, 4)],
            Self::Weights(w) if w.is_empty() => vec![1.],
            Self::Weights(w) => w.iter().map(|w| if *w > 0. { *w } else { 1. }).collect(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Search {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub placeholder: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub event: bool,
    #[serde(default = "yes")]
    pub filter: bool,
}

impl Default for Search {
    fn default() -> Self {
        Self {
            id: None,
            placeholder: None,
            event: false,
            filter: true,
        }
    }
}

impl Search {
    pub fn matches<'a>(query: &str, texts: impl IntoIterator<Item = &'a str> + Clone) -> bool {
        query.split_whitespace().all(|word| {
            let word = word.to_lowercase();
            texts
                .clone()
                .into_iter()
                .any(|t| t.to_lowercase().contains(&word))
        })
    }
}

fn yes() -> bool {
    true
}

fn is_yes(v: &bool) -> bool {
    *v
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Table {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    pub columns: Vec<Column>,
    pub rows: Vec<TableRow>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sort: Option<Sort>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub search: Option<Search>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub compact: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub empty: Option<String>,
    /// Taller rows, such as for pictures in cells; never shorter than the default.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub row_height: Option<f32>,
}

impl Table {
    pub fn plain(columns: Vec<String>, rows: Vec<Vec<String>>) -> Self {
        Self {
            id: None,
            columns: columns.into_iter().map(Column::Name).collect(),
            rows: rows
                .into_iter()
                .map(|r| TableRow::Cells(r.into_iter().map(Cell::Text).collect()))
                .collect(),
            sort: None,
            search: None,
            compact: false,
            empty: None,
            row_height: None,
        }
    }

    /// Whether clicking a row opens its detail panel.
    pub fn has_detail(&self) -> bool {
        self.rows.iter().any(|r| !r.detail().is_empty())
    }

    pub fn id(&self) -> &str {
        self.id.as_deref().unwrap_or("table")
    }

    pub fn is_plain(&self) -> bool {
        self.id.is_none()
            && self.sort.is_none()
            && self.search.is_none()
            && !self.compact
            && self.row_height.is_none()
            && self.columns.iter().all(|c| matches!(c, Column::Name(_)))
            && self.rows.iter().all(
                |r| matches!(r, TableRow::Cells(cells) if cells.iter().all(|c| matches!(c, Cell::Text(_)))),
            )
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum Column {
    Name(String),
    Spec(ColumnSpec),
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct ColumnSpec {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub key: Option<String>,
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub align: Align,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub width: Option<f32>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub sort: bool,
}

impl Column {
    pub fn title(&self) -> &str {
        match self {
            Self::Name(t) => t,
            Self::Spec(s) => &s.title,
        }
    }

    pub fn key(&self) -> &str {
        match self {
            Self::Spec(ColumnSpec { key: Some(k), .. }) => k,
            _ => self.title(),
        }
    }

    pub fn spec(&self) -> ColumnSpec {
        match self {
            Self::Name(t) => ColumnSpec {
                title: t.clone(),
                ..ColumnSpec::default()
            },
            Self::Spec(s) => s.clone(),
        }
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Align {
    #[default]
    Left,
    Right,
    Center,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Sort {
    pub column: String,
    #[serde(default)]
    pub desc: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum TableRow {
    Cells(Vec<Cell>),
    Spec(RowSpec),
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct RowSpec {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    #[serde(default)]
    pub cells: Vec<Cell>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tone: Option<Tone>,
    /// The detail panel's title and subtitle; the title defaults to the text of the first cell
    /// that isn't a switch.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub subtitle: Option<String>,
    /// Blocks shown in a panel beside the table while the row is selected; a row with
    /// detail is selected by clicking it.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub detail: Vec<Block>,
}

impl TableRow {
    pub fn cells(&self) -> &[Cell] {
        match self {
            Self::Cells(c) => c,
            Self::Spec(r) => &r.cells,
        }
    }

    pub fn id(&self, index: usize) -> String {
        match self {
            Self::Spec(RowSpec { id: Some(id), .. }) => id.clone(),
            _ => index.to_string(),
        }
    }

    pub fn tone(&self) -> Option<Tone> {
        match self {
            Self::Cells(_) => None,
            Self::Spec(r) => r.tone,
        }
    }

    pub fn detail(&self) -> &[Block] {
        match self {
            Self::Cells(_) => &[],
            Self::Spec(r) => &r.detail,
        }
    }

    /// The detail panel's title: the row's own, else the text of its first cell that isn't a
    /// switch.
    pub fn title(&self) -> String {
        match self {
            Self::Spec(RowSpec { title: Some(t), .. }) => t.clone(),
            _ => self
                .cells()
                .iter()
                .find(|c| !matches!(c, Cell::Spec(s) if matches!(s.edit, Some(Field::Toggle(_)))))
                .map(|c| c.text().to_owned())
                .unwrap_or_default(),
        }
    }

    pub fn subtitle(&self) -> Option<&str> {
        match self {
            Self::Spec(r) => r.subtitle.as_deref(),
            Self::Cells(_) => None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum Cell {
    Text(String),
    Spec(Box<CellSpec>),
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct CellSpec {
    #[serde(default)]
    pub text: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tone: Option<Tone>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sort: Option<SortKey>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub pill: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub bar: Option<f32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub edit: Option<Field>,
    /// A picture before the text, such as a portrait or a flag.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub image: Option<CellImage>,
    /// The text in pieces with their own weight and colour, drawn instead of `text`;
    /// `text` still searches, sorts and shows on launchers without spans.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub spans: Vec<Span>,
}

/// A cell's picture: the absolute path of a PNG or JPEG under the addon's cache or data
/// folder, or that with a size in pixels. Without a size it is a square as tall as the row
/// allows; with one it fills that box, cropped to it unless `fit` is `contain`.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum CellImage {
    Path(PathBuf),
    Sized {
        path: PathBuf,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        width: Option<f32>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        height: Option<f32>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        fit: Option<Fit>,
        #[serde(default, skip_serializing_if = "std::ops::Not::not")]
        transparent: bool,
    },
}

impl CellImage {
    pub fn path(&self) -> &std::path::Path {
        match self {
            Self::Path(p) | Self::Sized { path: p, .. } => p,
        }
    }

    /// The box's width and height, given the side of the default square.
    pub fn size(&self, side: f32) -> (f32, f32) {
        match self {
            Self::Path(_) => (side, side),
            Self::Sized { width, height, .. } => match (width, height) {
                (Some(w), Some(h)) => (*w, *h),
                (Some(w), None) => (*w, *w),
                (None, Some(h)) => (*h, *h),
                (None, None) => (side, side),
            },
        }
    }

    pub fn fit(&self) -> Fit {
        match self {
            Self::Sized { fit: Some(f), .. } => *f,
            _ => Fit::Cover,
        }
    }

    pub fn transparent(&self) -> bool {
        matches!(
            self,
            Self::Sized {
                transparent: true,
                ..
            }
        )
    }
}

/// A piece of a cell's text.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Span {
    pub text: String,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub bold: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tone: Option<Tone>,
}

impl Cell {
    pub fn text(&self) -> &str {
        match self {
            Self::Text(t) => t,
            Self::Spec(c) => &c.text,
        }
    }

    pub fn spec(&self) -> CellSpec {
        match self {
            Self::Text(t) => CellSpec {
                text: t.clone(),
                ..CellSpec::default()
            },
            Self::Spec(c) => (**c).clone(),
        }
    }

    pub fn sort_key(&self) -> SortKey {
        if let Self::Spec(c) = self
            && let Some(k) = &c.sort
        {
            return k.clone();
        }
        let text = self.text().trim();
        match text.replace(',', "").parse::<f64>() {
            Ok(n) if !text.is_empty() => SortKey::Number(n),
            _ => SortKey::Text(text.to_lowercase()),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum SortKey {
    Number(f64),
    Text(String),
}

impl SortKey {
    pub fn compare(&self, other: &Self) -> std::cmp::Ordering {
        use std::cmp::Ordering;
        match (self, other) {
            (Self::Number(a), Self::Number(b)) => a.partial_cmp(b).unwrap_or(Ordering::Equal),
            (Self::Number(_), Self::Text(_)) => Ordering::Less,
            (Self::Text(_), Self::Number(_)) => Ordering::Greater,
            (Self::Text(a), Self::Text(b)) => a.cmp(b),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Form {
    pub id: String,
    pub fields: Vec<Field>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub columns: Option<usize>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub apply: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub live: bool,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub disabled: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Field {
    Number(NumberField),
    Slider(NumberField),
    Text(TextField),
    Select(SelectField),
    Toggle(ToggleField),
    Colour(ColourField),
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct NumberField {
    #[serde(default)]
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub label: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub help: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub disabled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub value: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub min: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub step: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub unit: Option<String>,
}

impl NumberField {
    pub fn step(&self) -> f64 {
        self.step.filter(|s| *s > 0. && s.is_finite()).unwrap_or(1.)
    }

    pub fn clamp(&self, value: f64) -> f64 {
        let step = self.step();
        let base = self.min.unwrap_or(0.);
        let mut v = base + ((value - base) / step).round() * step;
        if let Some(min) = self.min {
            v = v.max(min);
        }
        if let Some(max) = self.max {
            v = v.min(max);
        }
        let digits = decimals(step);
        let scale = 10f64.powi(digits as i32);
        (v * scale).round() / scale
    }

    pub fn json(&self, value: f64) -> serde_json::Value {
        if value.fract() == 0. && self.step().fract() == 0. && value.abs() < 9.0e15 {
            serde_json::Value::from(value as i64)
        } else {
            serde_json::Number::from_f64(value).map_or(serde_json::Value::Null, serde_json::Value::Number)
        }
    }

    pub fn text(&self, value: f64) -> String {
        format!("{value:.*}", decimals(self.step()))
    }
}

fn decimals(step: f64) -> usize {
    let text = format!("{step}");
    text.split_once('.').map_or(0, |(_, f)| f.len().min(6))
}

pub fn number(v: f64) -> String {
    if v.fract() == 0. && v.abs() < 9.0e15 {
        format!("{}", v as i64)
    } else {
        let t = format!("{v:.2}");
        t.trim_end_matches('0').trim_end_matches('.').to_owned()
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct TextField {
    #[serde(default)]
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub label: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub help: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub disabled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub value: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub placeholder: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_len: Option<usize>,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct SelectField {
    #[serde(default)]
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub label: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub help: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub disabled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub value: Option<String>,
    #[serde(default)]
    pub options: Vec<Choice>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub search: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum Choice {
    Text(String),
    Spec {
        value: String,
        label: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        image: Option<PathBuf>,
    },
    Heading {
        heading: String,
    },
}

impl Choice {
    pub fn value(&self) -> &str {
        match self {
            Self::Text(t) => t,
            Self::Spec { value, .. } => value,
            Self::Heading { .. } => "",
        }
    }

    pub fn label(&self) -> &str {
        match self {
            Self::Text(t) => t,
            Self::Spec { label, .. } => label,
            Self::Heading { heading } => heading,
        }
    }

    pub fn is_heading(&self) -> bool {
        matches!(self, Self::Heading { .. })
    }

    pub fn image(&self) -> Option<&std::path::Path> {
        match self {
            Self::Spec { image, .. } => image.as_deref(),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct ToggleField {
    #[serde(default)]
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub label: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub help: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub disabled: bool,
    #[serde(default)]
    pub value: bool,
}

/// A colour, sent as `#RRGGBB`. `value` takes `#RRGGBB` or `#RGB`, with or without `#`, in
/// either case; `hex: false` shows only the colour's dot.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ColourField {
    #[serde(default)]
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub label: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub help: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub disabled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub value: Option<String>,
    #[serde(default = "yes", skip_serializing_if = "is_yes")]
    pub hex: bool,
}

impl ColourField {
    pub fn rgb(&self) -> Option<[u8; 3]> {
        self.value.as_deref().and_then(parse_colour)
    }
}

pub fn parse_colour(text: &str) -> Option<[u8; 3]> {
    let text = text.trim();
    let digits = text.strip_prefix('#').unwrap_or(text);
    if !digits.chars().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    let digit = |i: usize| u8::from_str_radix(&digits[i..i + 1], 16).ok();
    match digits.len() {
        3 => Some([digit(0)? * 17, digit(1)? * 17, digit(2)? * 17]),
        6 => {
            let byte = |i: usize| u8::from_str_radix(&digits[i..i + 2], 16).ok();
            Some([byte(0)?, byte(2)?, byte(4)?])
        }
        _ => None,
    }
}

pub fn colour_hex([r, g, b]: [u8; 3]) -> String {
    format!("#{r:02X}{g:02X}{b:02X}")
}

impl Field {
    pub fn id(&self) -> &str {
        match self {
            Self::Number(f) | Self::Slider(f) => &f.id,
            Self::Text(f) => &f.id,
            Self::Select(f) => &f.id,
            Self::Toggle(f) => &f.id,
            Self::Colour(f) => &f.id,
        }
    }

    pub fn label(&self) -> Option<&str> {
        match self {
            Self::Number(f) | Self::Slider(f) => f.label.as_deref(),
            Self::Text(f) => f.label.as_deref(),
            Self::Select(f) => f.label.as_deref(),
            Self::Toggle(f) => f.label.as_deref(),
            Self::Colour(f) => f.label.as_deref(),
        }
    }

    pub fn help(&self) -> Option<&str> {
        match self {
            Self::Number(f) | Self::Slider(f) => f.help.as_deref(),
            Self::Text(f) => f.help.as_deref(),
            Self::Select(f) => f.help.as_deref(),
            Self::Toggle(f) => f.help.as_deref(),
            Self::Colour(f) => f.help.as_deref(),
        }
    }

    pub fn disabled(&self) -> bool {
        match self {
            Self::Number(f) | Self::Slider(f) => f.disabled,
            Self::Text(f) => f.disabled,
            Self::Select(f) => f.disabled,
            Self::Toggle(f) => f.disabled,
            Self::Colour(f) => f.disabled,
        }
    }

    pub fn value(&self) -> serde_json::Value {
        use serde_json::Value;
        match self {
            Self::Number(f) => f.value.map_or(Value::Null, |v| f.json(v)),
            Self::Slider(f) => f.json(f.value.unwrap_or(f.min.unwrap_or(0.))),
            Self::Text(f) => Value::String(f.value.clone().unwrap_or_default()),
            Self::Select(f) => f
                .value
                .clone()
                .or_else(|| f.options.first().map(|o| o.value().to_owned()))
                .map_or(Value::Null, Value::String),
            Self::Toggle(f) => Value::Bool(f.value),
            Self::Colour(f) => f.rgb().map_or(Value::Null, |c| Value::String(colour_hex(c))),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Control {
    Switch {
        id: String,
        on: bool,
        enabled: bool,
    },
    Choice {
        id: String,
        options: Vec<String>,
        current: usize,
    },
    Button(Button),
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Button {
    pub id: String,
    pub label: String,
    #[serde(default)]
    pub icon: Option<Icon>,
    #[serde(default)]
    pub style: Style,
    #[serde(default)]
    pub kind: ButtonKind,
    #[serde(default)]
    pub size: ButtonSize,
}

impl Button {
    pub fn new(id: impl Into<String>, label: impl Into<String>) -> Self {
        Self {
            id: id.into(),
            label: label.into(),
            icon: None,
            style: Style::default(),
            kind: ButtonKind::default(),
            size: ButtonSize::default(),
        }
    }
}

/// How prominently a button is drawn. `Large` is for a page's main call to
/// action, such as the Simplified Importer's Choose a mod button.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ButtonSize {
    #[default]
    Normal,
    Large,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ButtonKind {
    #[default]
    Event,
    Confirm {
        title: String,
        text: String,
    },
    Pick {
        title: String,
        extensions: Vec<String>,
        folders: bool,
        multiple: bool,
    },
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Event {
    Click {
        id: String,
    },
    Toggle {
        id: String,
        on: bool,
    },
    Choose {
        id: String,
        index: usize,
    },
    Move {
        row: String,
        to: usize,
    },
    Picked {
        id: String,
        paths: Vec<PathBuf>,
    },
    Submit {
        form: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        row: Option<String>,
        values: serde_json::Map<String, serde_json::Value>,
        #[serde(default)]
        changed: Vec<String>,
    },
    Search {
        id: String,
        query: String,
    },
    Tab {
        id: String,
        tab: String,
    },
    /// Sent once per launcher start to a section whose addon declares
    /// `launcher.startup`, for long work such as converting mods.
    Startup {},
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Effect {
    None,
    Reload,
    Notice(String),
    SetAddonEnabled(bool),
    Open(PathBuf),
    /// Switches to the Addons page and highlights this addon.
    ShowAddon(Shown),
}

/// A `show_addon` target: the addon's id, or `{ "id", "notice" }` to also
/// show the notice as a notification.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum Shown {
    Id(String),
    With {
        id: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        notice: Option<String>,
    },
}

impl Shown {
    pub fn new(id: impl Into<String>, notice: impl Into<String>) -> Self {
        Self::With {
            id: id.into(),
            notice: Some(notice.into()),
        }
    }

    pub fn id(&self) -> &str {
        match self {
            Self::Id(id) | Self::With { id, .. } => id,
        }
    }

    pub fn notice(&self) -> Option<&str> {
        match self {
            Self::Id(_) => None,
            Self::With { notice, .. } => notice.as_deref(),
        }
    }
}

impl From<String> for Shown {
    fn from(id: String) -> Self {
        Self::Id(id)
    }
}

impl From<&str> for Shown {
    fn from(id: &str) -> Self {
        Self::Id(id.into())
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Fact {
    pub label: String,
    pub value: String,
}

impl Fact {
    pub fn new(label: impl Into<String>, value: impl Into<String>) -> Self {
        Self {
            label: label.into(),
            value: value.into(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Tag {
    pub text: String,
    #[serde(default)]
    pub tone: Tone,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Notice {
    pub text: String,
    #[serde(default)]
    pub tone: Tone,
    #[serde(default)]
    pub action: Option<Button>,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Tone {
    #[default]
    Neutral,
    Muted,
    Accent,
    Good,
    Warn,
    Bad,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Style {
    Primary,
    #[default]
    Outline,
    Ghost,
    Danger,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Art {
    #[default]
    None,
    Monogram(String),
    Image(PathBuf),
}

impl Art {
    pub fn is_none(&self) -> bool {
        matches!(self, Self::None)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Icon {
    Activity,
    ArrowRightLeft,
    ChevronDown,
    ChevronUp,
    CircleAlert,
    CircleCheck,
    Copy,
    Eraser,
    FolderOpen,
    Gamepad,
    Layers,
    Lock,
    Map,
    Package,
    Pencil,
    Play,
    Plus,
    Puzzle,
    Settings,
    ShieldCheck,
    Trash,
    Upload,
    Calendar,
    Car,
    Clock,
    CloudRain,
    Flag,
    Gauge,
    History,
    Info,
    Refresh,
    Save,
    Search,
    Timer,
    TrendingDown,
    TrendingUp,
    TriangleAlert,
    Trophy,
    Undo,
    User,
    Users,
    Wallet,
    Wrench,
    Zap,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_row_menu_is_optional_in_json() {
        let plain: Row = serde_json::from_str(r#"{"id":"a","title":"A","subtitle":""}"#).unwrap();
        assert!(plain.menu.is_empty());
        assert!(!serde_json::to_string(&plain).unwrap().contains("menu"));
        let with: Row = serde_json::from_str(
            r#"{"id":"a","title":"A","subtitle":"","menu":[
                {"id":"remove:a","label":"Remove","style":"danger",
                 "kind":{"confirm":{"title":"Remove A?","text":"It is deleted."}}}]}"#,
        )
        .unwrap();
        assert_eq!(with.menu.len(), 1);
        assert_eq!(with.menu[0].style, Style::Danger);
        assert!(matches!(with.menu[0].kind, ButtonKind::Confirm { .. }));
    }

    #[test]
    fn views_round_trip_through_json() {
        let view = View {
            subtitle: "1 in library".into(),
            actions: vec![Button {
                kind: ButtonKind::Pick {
                    title: "Import".into(),
                    extensions: vec!["serps".into(), "zip".into()],
                    folders: true,
                    multiple: true,
                },
                icon: Some(Icon::Upload),
                ..Button::new("import", "Import…")
            }],
            notices: vec![Notice {
                text: "Team Simplified is off in this profile.".into(),
                tone: Tone::Warn,
                action: Some(Button::new("enable", "Turn on")),
            }],
            body: Body::List {
                rows: vec![Row {
                    id: "rm.alphatauri".into(),
                    title: "Scuderia AlphaTauri".into(),
                    subtitle: "RM Designs · 1.0".into(),
                    art: Art::Monogram("AT".into()),
                    tags: vec![Tag {
                        text: "Database".into(),
                        tone: Tone::Muted,
                    }],
                    trailing: Some(Control::Switch {
                        id: "rm.alphatauri".into(),
                        on: true,
                        enabled: true,
                    }),
                    detail: vec![Block::Stats(vec![Fact::new("Version", "1.0")])],
                    menu: Vec::new(),
                }],
                reorder: true,
                search: None,
            },
            footer: Some("By Team Simplified.".into()),
            tick: false,
        };
        let json = serde_json::to_value(&view).unwrap();
        assert_eq!(json["body"]["list"]["rows"][0]["trailing"]["switch"]["on"], true);
        assert_eq!(json["actions"][0]["icon"], "upload");
        assert_eq!(serde_json::from_value::<View>(json).unwrap(), view);
        let event: Event = serde_json::from_str(r#"{"move":{"row":"a","to":2}}"#).unwrap();
        assert_eq!(
            event,
            Event::Move {
                row: "a".into(),
                to: 2
            }
        );
        assert_eq!(serde_json::to_value(Effect::Reload).unwrap(), "reload");
        assert_eq!(
            serde_json::from_str::<Effect>(r#"{"show_addon":"you.okayama"}"#).unwrap(),
            Effect::ShowAddon("you.okayama".into())
        );
    }

    #[test]
    fn show_addon_takes_an_id_or_an_id_with_a_notice() {
        let old: Effect = serde_json::from_str(r#"{"show_addon":"you.okayama"}"#).unwrap();
        let Effect::ShowAddon(shown) = &old else {
            panic!("show_addon")
        };
        assert_eq!((shown.id(), shown.notice()), ("you.okayama", None));
        assert_eq!(
            serde_json::to_value(&old).unwrap(),
            serde_json::json!({ "show_addon": "you.okayama" })
        );
        let new: Effect =
            serde_json::from_str(r#"{"show_addon":{"id":"you.okayama","notice":"Imported Okayama"}}"#)
                .unwrap();
        assert_eq!(
            new,
            Effect::ShowAddon(Shown::new("you.okayama", "Imported Okayama"))
        );
        let Effect::ShowAddon(shown) = &new else {
            panic!("show_addon")
        };
        assert_eq!(
            (shown.id(), shown.notice()),
            ("you.okayama", Some("Imported Okayama"))
        );
        assert_eq!(
            serde_json::from_value::<Effect>(serde_json::to_value(&new).unwrap()).unwrap(),
            new
        );
        let bare: Effect = serde_json::from_str(r#"{"show_addon":{"id":"you.okayama"}}"#).unwrap();
        let Effect::ShowAddon(shown) = &bare else {
            panic!("show_addon")
        };
        assert_eq!((shown.id(), shown.notice()), ("you.okayama", None));
    }

    #[test]
    fn image_blocks_parse_inside_cards_and_default_to_cover() {
        let card: Block = serde_json::from_value(serde_json::json!({ "card": { "blocks": [
            { "image": { "path": "/c/helmet-ab12.png", "width": 240, "height": 135, "caption": "Helmet" } },
            { "image": { "path": "/d/suit.png", "width": 96, "height": 96, "fit": "contain" } }
        ] } }))
        .unwrap();
        let Block::Card(card) = &card else { panic!("card") };
        let Block::Image(helmet) = &card.blocks[0] else {
            panic!("image")
        };
        assert_eq!(
            (
                helmet.path.to_str(),
                helmet.width,
                helmet.height,
                helmet.caption.as_deref(),
                helmet.fit
            ),
            (Some("/c/helmet-ab12.png"), 240., 135., Some("Helmet"), Fit::Cover)
        );
        let Block::Image(suit) = &card.blocks[1] else {
            panic!("image")
        };
        assert_eq!((suit.fit, suit.caption.is_none()), (Fit::Contain, true));
        let json = serde_json::to_value(Block::Card(card.clone())).unwrap();
        assert_eq!(json["card"]["blocks"][1]["image"]["fit"], "contain");
        assert!(json["card"]["blocks"][1]["image"].get("caption").is_none());
    }

    #[test]
    fn dashboard_blocks_parse_from_the_documented_json() {
        let view: View = serde_json::from_value(serde_json::json!({
            "subtitle": "s", "tick": true,
            "body": { "blocks": [
                { "hero": { "title": "Scuderia Swofty", "subtitle": "My Team",
                    "art": { "monogram": "S" },
                    "pills": [ { "text": "LIVE", "tone": "good", "dot": true } ],
                    "facts": [ { "label": "Next race", "value": "Monaco" } ],
                    "buttons": [ { "id": "backup", "label": "Back up now", "icon": "save" } ] } },
                { "tiles": [
                    { "label": "Cash", "value": "$12.4M", "delta": "+$2.0M", "hint": "since Bahrain" },
                    { "label": "RP", "value": "340", "progress": 0.68, "tone": "accent" },
                    { "label": "P", "value": "P4", "delta": "-1" },
                    { "label": "Q", "value": "1", "delta": "1", "delta_tone": "warn" } ] },
                { "grid": { "columns": [2, 1], "blocks": [
                    { "card": { "title": "Facilities", "blocks": [
                        { "meters": [ { "label": "Wind tunnel", "value": 3, "max": 4, "text": "Phase 3 of 4", "tone": "good" } ] } ] } },
                    { "card": { "pills": [ { "text": "Offline", "tone": "muted" } ] } } ] } },
                { "grid": { "blocks": [] } },
                { "pills": [ { "text": "In Team HQ", "tone": "accent", "icon": "car" } ] }
            ] }
        }))
        .unwrap();
        assert!(view.tick);
        let Body::Blocks(blocks) = &view.body else {
            panic!("blocks body");
        };
        let Block::Hero(hero) = &blocks[0] else {
            panic!("hero");
        };
        assert_eq!(hero.art, Art::Monogram("S".into()));
        assert!(hero.pills[0].dot);
        assert_eq!(hero.buttons[0].icon, Some(Icon::Save));
        let Block::Tiles(tiles) = &blocks[1] else {
            panic!("tiles");
        };
        let tones: Vec<Tone> = tiles.iter().map(Tile::delta_tone).collect();
        assert_eq!(tones, [Tone::Good, Tone::Muted, Tone::Bad, Tone::Warn]);
        assert_eq!(tiles[1].progress, Some(0.68));
        let Block::Grid(grid) = &blocks[2] else {
            panic!("grid");
        };
        assert_eq!(grid.columns.weights(), [2., 1.]);
        let Block::Card(card) = &grid.blocks[0] else {
            panic!("card");
        };
        let Block::Meters(meters) = &card.blocks[0] else {
            panic!("meters");
        };
        assert_eq!(meters[0].fraction(), 0.75);
        assert_eq!(meters[0].text(), "Phase 3 of 4");
        let Block::Grid(plain) = &blocks[3] else {
            panic!("grid");
        };
        assert_eq!(plain.columns.weights(), [1., 1.]);
        assert_eq!(Columns::Count(9).weights().len(), 4);
        let again: View = serde_json::from_value(serde_json::to_value(&view).unwrap()).unwrap();
        assert_eq!(again, view);
        let bare = serde_json::to_value(View { tick: false, ..view }).unwrap();
        assert!(bare.get("tick").is_none());
    }

    #[test]
    fn meters_default_to_a_hundred_and_clamp() {
        let m: Meter = serde_json::from_str(r#"{"value": 40}"#).unwrap();
        assert_eq!((m.fraction(), m.text()), (0.4, "40 / 100".to_owned()));
        let over: Meter = serde_json::from_str(r#"{"value": 12, "min": 2, "max": 7}"#).unwrap();
        assert_eq!(over.fraction(), 1.0);
        let flat: Meter = serde_json::from_str(r#"{"value": 1, "min": 5, "max": 5}"#).unwrap();
        assert_eq!(flat.fraction(), 0.0);
    }

    #[test]
    fn forms_parse_every_field_kind() {
        let form: Form = serde_json::from_value(serde_json::json!({
            "id": "finance", "columns": 2, "apply": "Apply",
            "fields": [
                { "number": { "id": "cash", "label": "Cash", "value": 12400000, "min": 0, "max": 2147483647, "step": 100000, "unit": "$" } },
                { "slider": { "id": "rp", "value": 340, "min": 0, "max": 500, "step": 10 } },
                { "text": { "id": "name", "value": "Swofty", "placeholder": "Team", "max_len": 32 } },
                { "select": { "id": "weather", "value": "dry", "options": [ { "value": "dry", "label": "Dry" }, "Light rain", { "heading": "Wet" }, { "value": "storm", "label": "Storm", "image": "/c/storm-1.png" } ] } },
                { "toggle": { "id": "lock", "value": true, "disabled": true } }
            ]
        }))
        .unwrap();
        let ids: Vec<&str> = form.fields.iter().map(Field::id).collect();
        assert_eq!(ids, ["cash", "rp", "name", "weather", "lock"]);
        let values: Vec<serde_json::Value> = form.fields.iter().map(Field::value).collect();
        assert_eq!(
            values,
            [
                serde_json::json!(12400000),
                serde_json::json!(340),
                serde_json::json!("Swofty"),
                serde_json::json!("dry"),
                serde_json::json!(true)
            ]
        );
        assert!(values[0].is_i64());
        assert!(form.fields[4].disabled() && !form.fields[0].disabled());
        let Field::Select(select) = &form.fields[3] else {
            panic!("select");
        };
        assert_eq!(select.options[1].value(), "Light rain");
        assert_eq!(select.options[0].label(), "Dry");
        assert!(select.options[2].is_heading() && !select.options[0].is_heading());
        assert_eq!(
            (select.options[2].label(), select.options[2].value()),
            ("Wet", "")
        );
        assert_eq!(
            select.options[3].image(),
            Some(std::path::Path::new("/c/storm-1.png"))
        );
        assert!(select.options[0].image().is_none() && select.options[2].image().is_none());
        assert!(!form.live && !form.disabled);
        let again: Form = serde_json::from_value(serde_json::to_value(&form).unwrap()).unwrap();
        assert_eq!(again, form);
    }

    #[test]
    fn select_fields_can_ask_for_search() {
        let field: Field = serde_json::from_value(serde_json::json!(
            { "select": { "id": "helmet", "value": "h2", "search": true,
                          "options": [ { "value": "h1", "label": "Classic Red" }, { "value": "h2", "label": "Carbon" } ] } }
        ))
        .unwrap();
        let Field::Select(select) = &field else {
            panic!("select");
        };
        assert!(select.search);
        assert_eq!(field.value(), serde_json::json!("h2"));
        assert_eq!(serde_json::to_value(&field).unwrap()["select"]["search"], true);
        let plain: Field = serde_json::from_value(
            serde_json::json!({ "select": { "id": "garage", "options": ["Front", "Back"] } }),
        )
        .unwrap();
        let Field::Select(select) = &plain else {
            panic!("select");
        };
        assert!(!select.search);
        assert!(
            serde_json::to_value(&plain).unwrap()["select"]
                .get("search")
                .is_none()
        );
    }

    #[test]
    fn number_fields_clamp_round_and_send_integers() {
        let cash = NumberField {
            min: Some(0.),
            max: Some(1000.),
            step: Some(10.),
            ..NumberField::default()
        };
        assert_eq!(cash.clamp(123.), 120.);
        assert_eq!(cash.clamp(-5.), 0.);
        assert_eq!(cash.clamp(5000.), 1000.);
        assert!(cash.json(120.).is_i64());
        let rate = NumberField {
            step: Some(0.05),
            ..NumberField::default()
        };
        assert_eq!(rate.clamp(0.333), 0.35);
        assert_eq!(rate.json(0.35), serde_json::json!(0.35));
        assert_eq!(rate.text(0.3), "0.30");
        assert!(NumberField::default().json(3.).is_i64());
        assert_eq!(NumberField::default().json(2.5), serde_json::json!(2.5));
    }

    #[test]
    fn tables_keep_the_old_shape_and_add_sorting_search_and_edits() {
        let old: Block =
            serde_json::from_str(r#"{"table":{"columns":["File","Op"],"rows":[["a.erp","patch"]]}}"#)
                .unwrap();
        let Block::Table(old_table) = &old else {
            panic!("table");
        };
        assert!(old_table.is_plain());
        assert_eq!(
            *old_table,
            Table::plain(
                vec!["File".into(), "Op".into()],
                vec![vec!["a.erp".into(), "patch".into()]]
            )
        );
        assert_eq!(
            serde_json::to_value(&old).unwrap(),
            serde_json::json!({"table":{"columns":["File","Op"],"rows":[["a.erp","patch"]]}})
        );
        let t: Table = serde_json::from_value(serde_json::json!({
            "id": "drivers",
            "columns": [
                "Driver",
                { "key": "team", "title": "Team" },
                { "key": "pace", "title": "Pace", "align": "right", "width": 96, "sort": true }
            ],
            "rows": [
                { "id": "ver", "cells": [
                    "Max Verstappen",
                    { "text": "Red Bull", "tone": "muted" },
                    { "text": "95", "bar": 0.95, "edit": { "number": { "min": 0, "max": 100 } } } ] },
                ["Lando Norris", "McLaren", "1,093"],
                ["Oscar Piastri", "McLaren", { "text": "$12.4M", "sort": 12400000 }]
            ],
            "sort": { "column": "pace", "desc": true },
            "search": { "placeholder": "Search drivers", "event": true },
            "compact": true,
            "empty": "No drivers match"
        }))
        .unwrap();
        assert!(!t.is_plain());
        assert_eq!(t.id(), "drivers");
        let keys: Vec<&str> = t.columns.iter().map(Column::key).collect();
        assert_eq!(keys, ["Driver", "team", "pace"]);
        assert_eq!(t.columns[2].spec().align, Align::Right);
        assert_eq!(t.columns[2].spec().width, Some(96.));
        assert!(t.columns[2].spec().sort && !t.columns[0].spec().sort);
        assert_eq!(t.rows[0].id(0), "ver");
        assert_eq!(t.rows[1].id(1), "1");
        assert_eq!(t.rows[0].cells()[1].spec().tone, Some(Tone::Muted));
        assert!(matches!(t.rows[0].cells()[2].spec().edit, Some(Field::Number(_))));
        assert_eq!(t.rows[1].cells()[2].sort_key(), SortKey::Number(1093.));
        assert_eq!(t.rows[2].cells()[2].sort_key(), SortKey::Number(12_400_000.));
        assert_eq!(
            t.rows[0].cells()[0].sort_key(),
            SortKey::Text("max verstappen".into())
        );
        assert_eq!(
            SortKey::Number(2.).compare(&SortKey::Number(10.)),
            std::cmp::Ordering::Less
        );
        let search = t.search.clone().unwrap();
        assert!(search.event && search.filter && search.id.is_none());
        assert_eq!(
            t.sort,
            Some(Sort {
                column: "pace".into(),
                desc: true
            })
        );
        let again: Table = serde_json::from_value(serde_json::to_value(&t).unwrap()).unwrap();
        assert_eq!(again, t);
    }

    #[test]
    fn table_rows_take_pictures_switches_spans_and_a_detail_panel() {
        let t: Table = serde_json::from_value(serde_json::json!({
            "id": "drivers",
            "columns": ["On", "Driver", "Nationality"],
            "row_height": 72,
            "rows": [
                { "id": "rai", "subtitle": "kimi_raikkonen",
                  "cells": [
                    { "text": "On", "edit": { "toggle": { "value": true } } },
                    { "text": "Kimi RÄIKKÖNEN", "image": "/cache/kimi.png",
                      "spans": [ { "text": "Kimi " }, { "text": "RÄIKKÖNEN", "bold": true } ] },
                    { "text": "Finnish", "image": { "path": "/cache/flags/FI.png", "width": 20, "height": 14 } } ],
                  "detail": [ { "facts": [ { "label": "Car number", "value": "#7" } ] } ] },
                ["Off", "Nico Rosberg", "German"]
            ]
        }))
        .unwrap();
        assert!(!t.is_plain() && t.has_detail());
        assert_eq!(t.row_height, Some(72.));
        let (kimi, nico) = (&t.rows[0], &t.rows[1]);
        assert!(matches!(
            kimi.cells()[0].spec().edit,
            Some(Field::Toggle(ToggleField { value: true, .. }))
        ));
        let name = kimi.cells()[1].spec();
        assert_eq!(
            name.image.as_ref().unwrap().path(),
            std::path::Path::new("/cache/kimi.png")
        );
        assert_eq!(name.image.as_ref().unwrap().size(64.), (64., 64.));
        assert!(!name.spans[0].bold && name.spans[1].bold);
        assert_eq!(kimi.cells()[2].spec().image.unwrap().size(64.), (20., 14.));
        // The text still searches and sorts; the panel is titled after the first cell that
        // isn't a switch.
        assert_eq!(name.text, "Kimi RÄIKKÖNEN");
        assert_eq!(kimi.title(), "Kimi RÄIKKÖNEN");
        assert_eq!(nico.title(), "Off");
        assert_eq!(kimi.subtitle(), Some("kimi_raikkonen"));
        assert_eq!(kimi.detail().len(), 1);
        assert!(nico.detail().is_empty() && nico.subtitle().is_none());
        let again: Table = serde_json::from_value(serde_json::to_value(&t).unwrap()).unwrap();
        assert_eq!(again, t);
        // Rows without the new keys serialize as before.
        assert_eq!(
            serde_json::to_value(Table::plain(vec!["A".into()], vec![vec!["1".into()]])).unwrap(),
            serde_json::json!({ "columns": ["A"], "rows": [["1"]] })
        );
    }

    #[test]
    fn colour_fields_send_upper_case_hex_and_leave_the_default_hex_out() {
        let form: Form = serde_json::from_value(serde_json::json!({ "id": "livery", "fields": [
            { "colour": { "id": "team", "label": "Team colour", "value": "#e10600" } },
            { "colour": { "id": "accent", "value": "0af", "hex": false } },
            { "colour": { "id": "unset" } }
        ] }))
        .unwrap();
        let values: Vec<serde_json::Value> = form.fields.iter().map(Field::value).collect();
        assert_eq!(
            values,
            [
                serde_json::json!("#E10600"),
                serde_json::json!("#00AAFF"),
                serde_json::Value::Null
            ]
        );
        let Field::Colour(team) = &form.fields[0] else {
            panic!("colour")
        };
        assert!(team.hex && team.rgb() == Some([0xE1, 0x06, 0x00]));
        assert_eq!(form.fields[0].label(), Some("Team colour"));
        let json = serde_json::to_value(&form).unwrap();
        assert!(json["fields"][0]["colour"].get("hex").is_none());
        assert_eq!(json["fields"][1]["colour"]["hex"], false);
        let again: Form = serde_json::from_value(json).unwrap();
        assert_eq!(again, form);
    }

    #[test]
    fn parse_colour_takes_six_or_three_digits_and_refuses_the_rest() {
        assert_eq!(parse_colour("#E8002D"), Some([0xE8, 0x00, 0x2D]));
        assert_eq!(parse_colour(" e8002d "), Some([0xE8, 0x00, 0x2D]));
        assert_eq!(parse_colour("#fA0"), Some([0xFF, 0xAA, 0x00]));
        assert_eq!(parse_colour("abc"), Some([0xAA, 0xBB, 0xCC]));
        for bad in [
            "",
            "#",
            "#12",
            "#1234",
            "#12345",
            "#1234567",
            "#GGGGGG",
            "##123456",
            "+12345",
            "red",
            "#12 456",
            "#E8002Dé",
        ] {
            assert_eq!(parse_colour(bad), None, "{bad}");
        }
        assert_eq!(colour_hex([0x00, 0xa1, 0x9c]), "#00A19C");
    }

    #[test]
    fn colour_cells_and_transparent_pictures_parse() {
        let row: TableRow = serde_json::from_value(serde_json::json!({ "id": "ferrari", "cells": [
            { "text": "Ferrari", "image": { "path": "/cache/logos/ferrari.png", "width": 28, "height": 28,
                                            "fit": "contain", "transparent": true } },
            { "text": "#E8002D", "edit": { "colour": {} } },
            { "text": "#E8002D", "edit": { "colour": { "hex": false } } }
        ] }))
        .unwrap();
        let logo = row.cells()[0].spec().image.unwrap();
        assert_eq!(
            (logo.fit(), logo.transparent(), logo.size(32.)),
            (Fit::Contain, true, (28., 28.))
        );
        let Some(Field::Colour(cell)) = row.cells()[1].spec().edit else {
            panic!("colour cell")
        };
        assert!(cell.hex && cell.value.is_none());
        assert!(matches!(
            row.cells()[2].spec().edit,
            Some(Field::Colour(ColourField { hex: false, .. }))
        ));
        let block: Block = serde_json::from_value(serde_json::json!(
            { "image": { "path": "/c/logo.png", "width": 96, "height": 96, "transparent": true } }
        ))
        .unwrap();
        assert!(matches!(&block, Block::Image(i) if i.transparent));
        let again: TableRow = serde_json::from_value(serde_json::to_value(&row).unwrap()).unwrap();
        assert_eq!(again, row);
    }

    #[test]
    fn pictures_without_the_new_keys_serialize_as_before() {
        let sized = serde_json::json!({ "path": "/cache/flags/FI.png", "width": 20.0, "height": 14.0 });
        let image: CellImage = serde_json::from_value(sized.clone()).unwrap();
        assert_eq!((image.fit(), image.transparent()), (Fit::Cover, false));
        assert_eq!(serde_json::to_value(&image).unwrap(), sized);
        let plain: CellImage = serde_json::from_value(serde_json::json!("/cache/kimi.png")).unwrap();
        assert!(!plain.transparent() && plain.fit() == Fit::Cover);
        let block = serde_json::json!({ "image": { "path": "/c/helmet.png", "width": 240.0, "height": 135.0, "fit": "cover" } });
        let parsed: Block = serde_json::from_value(block.clone()).unwrap();
        assert_eq!(serde_json::to_value(&parsed).unwrap(), block);
    }

    #[test]
    fn search_matches_every_word_ignoring_case() {
        let row = ["Max Verstappen", "Red Bull Racing"];
        assert!(Search::matches("max", row));
        assert!(Search::matches("RED max", row));
        assert!(!Search::matches("max ferrari", row));
        assert!(Search::matches("  ", row));
        let list: Body =
            serde_json::from_str(r#"{"list":{"rows":[],"reorder":false,"search":{"filter":false}}}"#)
                .unwrap();
        assert!(matches!(
            list,
            Body::List {
                search: Some(Search { filter: false, .. }),
                ..
            }
        ));
        let plain: Body = serde_json::from_str(r#"{"list":{"rows":[],"reorder":false}}"#).unwrap();
        assert!(!serde_json::to_string(&plain).unwrap().contains("search"));
    }

    #[test]
    fn tabs_block_and_tab_event_match_the_documented_json() {
        let b: Block = serde_json::from_value(serde_json::json!({ "tabs": { "id": "page",
            "items": [ { "id": "overview", "label": "Overview", "icon": "gauge" },
                       { "id": "team", "label": "Team", "badge": "2", "disabled": true } ],
            "active": "overview" } }))
        .unwrap();
        let Block::Tabs(t) = &b else { panic!("tabs") };
        assert_eq!(t.active.as_deref(), Some("overview"));
        assert_eq!(
            (t.items[1].badge.as_deref(), t.items[1].disabled, t.items[0].icon),
            (Some("2"), true, Some(Icon::Gauge))
        );
        assert_eq!(
            serde_json::to_value(Event::Tab {
                id: "page".into(),
                tab: "team".into()
            })
            .unwrap(),
            serde_json::json!({ "tab": { "id": "page", "tab": "team" } })
        );
    }

    #[test]
    fn submit_and_search_events_match_the_documented_json() {
        let submit: Event = serde_json::from_value(serde_json::json!({ "submit": {
            "form": "finance", "values": { "cash": 15000000, "lock": false }, "changed": ["cash"] } }))
        .unwrap();
        let Event::Submit {
            form,
            row,
            values,
            changed,
        } = &submit
        else {
            panic!("submit");
        };
        assert_eq!(
            (form.as_str(), row, changed.as_slice()),
            ("finance", &None, &["cash".to_owned()][..])
        );
        assert_eq!(values["cash"], 15000000);
        assert!(!serde_json::to_string(&submit).unwrap().contains("row"));
        let cell: Event = serde_json::from_str(
            r#"{"submit":{"form":"drivers","row":"ver","values":{"pace":96},"changed":["pace"]}}"#,
        )
        .unwrap();
        assert!(matches!(cell, Event::Submit { row: Some(ref r), .. } if r == "ver"));
        assert_eq!(
            serde_json::to_value(Event::Search {
                id: "drivers".into(),
                query: "ver".into()
            })
            .unwrap(),
            serde_json::json!({ "search": { "id": "drivers", "query": "ver" } })
        );
        let icons: Vec<Icon> =
            serde_json::from_str(r#"["refresh","undo","cloud-rain","trending-up","triangle-alert"]"#)
                .unwrap();
        assert_eq!(
            icons,
            [
                Icon::Refresh,
                Icon::Undo,
                Icon::CloudRain,
                Icon::TrendingUp,
                Icon::TriangleAlert
            ]
        );
    }
}
