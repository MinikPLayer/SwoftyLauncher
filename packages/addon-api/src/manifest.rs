use std::collections::BTreeMap;
use std::path::Path;

use semver::{Version, VersionReq};
use serde::{Deserialize, Serialize};

use crate::launcher::CommandSpec;
use crate::view::SectionSpec;
use crate::{Error, Result};

pub const MANIFEST_FILE: &str = "swofty.json";
pub const SCHEMA_VERSION: u32 = 1;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct AddonManifest {
    pub schema_version: u32,
    pub id: String,
    pub name: String,
    pub version: Version,
    pub author: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub license: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub homepage: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub kind: Vec<Kind>,
    /// Experimental engines of the core the addon uses; the build turns them on while it is on.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub experimental_features: Vec<ExperimentalFeature>,
    pub compat: Compat,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub requires: Vec<Requirement>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub conflicts: Vec<Conflict>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub load_after: Vec<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub options: Vec<AddonOption>,
    #[serde(default)]
    pub operations: Operations,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub launcher: Option<Launcher>,
    #[serde(default, skip_serializing_if = "Saves::is_unset")]
    pub saves: Saves,
    /// What the addon is for, for grouping and filtering; `kind` says what it touches.
    #[serde(
        default,
        deserialize_with = "Category::read",
        skip_serializing_if = "Option::is_none"
    )]
    pub category: Option<Category>,
    /// The tool that generated the addon and the file it came from.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub origin: Option<Origin>,
    /// A colour look applied to the game's picture, before the HUD draws.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub shader: Option<Shader>,
    /// Post-process passes run on the game's picture after its tonemap and before the HUD.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub postfx: Vec<PostFx>,
    /// A settings page in game between a Driver Career tile and the new-career wizard.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub career_setup: Option<CareerSetup>,
    /// A settings tab in the career hub's laptop (Team HQ) for careers of one data group.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hub_tab: Option<HubTab>,
    /// Whether players in a multiplayer session must match on the addon; see
    /// [`AddonManifest::multiplayer`] for the default.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub multiplayer: Option<Multiplayer>,
    /// What the addon adds to the race HUD.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hud: Option<Hud>,
}

/// What an addon, or one of its options, is to a multiplayer session.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "lowercase")]
pub enum Multiplayer {
    /// Every player needs it at the host's version, with the host's settings.
    Gameplay,
    /// Players may differ on it.
    Cosmetic,
}

/// Most options a hub tab shows: one per cell of the laptop's stat grid.
pub const HUB_TAB_OPTIONS: usize = 6;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct HubTab {
    pub name: String,
    pub title: String,
    pub options: Vec<String>,
    pub data_group: String,
}

impl HubTab {
    pub fn template(&self) -> String {
        format!("swofty_hub_{}", self.name)
    }

    pub fn view_model(&self) -> String {
        format!("view_model_data.hud_swofty_hub_{}", self.name)
    }

    fn validate(&self, options: &[AddonOption]) -> std::result::Result<(), String> {
        if !valid_screen_name(&self.name) || self.name.len() > 40 {
            return Err(format!(
                "name {:?} must be [a-z0-9_], at most 40 characters",
                self.name
            ));
        }
        if self.title.trim().is_empty() {
            return Err("title is empty".into());
        }
        if !valid_screen_name(&self.data_group) {
            return Err(format!("data_group {:?} must be [a-z0-9_]", self.data_group));
        }
        if self.options.is_empty() || self.options.len() > HUB_TAB_OPTIONS {
            return Err(format!("options must name 1 to {HUB_TAB_OPTIONS} options"));
        }
        own_options(&self.options, options)
    }
}

fn own_options(ids: &[String], options: &[AddonOption]) -> std::result::Result<(), String> {
    for (i, id) in ids.iter().enumerate() {
        if !options.iter().any(|o| &o.id == id) {
            return Err(format!("option {id} is not one of the addon's options"));
        }
        if ids[..i].contains(id) {
            return Err(format!("option {id} is listed twice"));
        }
    }
    Ok(())
}

/// The wizard screen a career setup page's CONFIRM opens unless `next` names another.
pub const CAREER_SETUP_NEXT: &str = "career_wizard_season_length_select";
/// The screen a career setup page's Back opens.
pub const CAREER_SETUP_BACK: &str = "fe_career_dlc_select";
/// Longest `career_setup.route`.
pub const CAREER_SETUP_ROUTE_LEN: usize = 48;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct CareerSetup {
    pub route: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    pub options: Vec<String>,
    #[serde(default = "career_setup_next", skip_serializing_if = "is_career_setup_next")]
    pub next: String,
}

fn career_setup_next() -> String {
    CAREER_SETUP_NEXT.into()
}

fn is_career_setup_next(next: &String) -> bool {
    next == CAREER_SETUP_NEXT
}

impl CareerSetup {
    pub fn screen(&self) -> String {
        format!("swofty_setup_{}", self.route)
    }

    pub fn back_route(&self) -> String {
        format!("back_{}", self.route)
    }

    fn validate(&self, options: &[AddonOption]) -> std::result::Result<(), String> {
        if !valid_screen_name(&self.route) || self.route.len() > CAREER_SETUP_ROUTE_LEN {
            return Err(format!(
                "route {:?} must be [a-z0-9_], at most {CAREER_SETUP_ROUTE_LEN} characters",
                self.route
            ));
        }
        if !valid_screen_name(&self.next) {
            return Err(format!("next {:?} must be [a-z0-9_]", self.next));
        }
        if self.title.as_deref().is_some_and(|t| t.trim().is_empty()) {
            return Err("title is empty".into());
        }
        if self.options.is_empty() {
            return Err("options names no option".into());
        }
        own_options(&self.options, options)
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Hud {
    /// Rows on the ADDONS page of the race MFD.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub mfd: Option<HudMfd>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct HudMfd {
    /// The group title the page shows above the addon's rows; the addon name when unset.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    /// Option ids, in page order; each is a toggle or choice option of this addon.
    pub rows: Vec<String>,
}

pub const MFD_ROWS_MAX: usize = 8;
pub const MFD_TEXT_MAX: usize = 32;

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[serde(rename_all = "kebab-case")]
pub enum Category {
    Seasons,
    Tracks,
    #[serde(alias = "liveries")]
    Cars,
    Helmets,
    Shaders,
    Career,
    Ai,
    Performance,
    Utility,
}

impl Category {
    pub const ALL: [Category; 9] = [
        Self::Seasons,
        Self::Tracks,
        Self::Cars,
        Self::Helmets,
        Self::Shaders,
        Self::Career,
        Self::Ai,
        Self::Performance,
        Self::Utility,
    ];

    /// Categories from earlier launchers that read as none.
    pub const RETIRED: [&str; 7] = [
        "drivers-teams",
        "database",
        "gameplay",
        "ui",
        "audio",
        "tools",
        "other",
    ];

    pub fn label(self) -> &'static str {
        match self {
            Self::Seasons => "Seasons",
            Self::Tracks => "Tracks",
            Self::Cars => "Cars",
            Self::Helmets => "Helmets",
            Self::Shaders => "Shaders",
            Self::Career => "Career",
            Self::Ai => "AI",
            Self::Performance => "Performance",
            Self::Utility => "Utility",
        }
    }

    /// Reads an optional category, taking `liveries` as cars and a
    /// [retired](Category::RETIRED) or unknown one as none.
    pub fn read<'de, D: serde::Deserializer<'de>>(d: D) -> std::result::Result<Option<Self>, D::Error> {
        use serde::de::IntoDeserializer;
        Ok(Option::<String>::deserialize(d)?.and_then(|name| {
            Self::deserialize(IntoDeserializer::<serde::de::value::Error>::into_deserializer(
                name,
            ))
            .ok()
        }))
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Origin {
    /// `db-editor`, or the id of the addon that generated it (`teamsimplified.mods`).
    pub tool: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool_version: Option<String>,
    /// The file name the addon was made from.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub file: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sha256: Option<String>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Saves {
    #[serde(default)]
    pub affects: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
}

impl Saves {
    fn is_unset(&self) -> bool {
        *self == Self::default()
    }

    pub fn message(&self) -> Option<&str> {
        self.message.as_deref().map(str::trim).filter(|m| !m.is_empty())
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Launcher {
    pub program: BTreeMap<String, String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub sections: Vec<SectionSpec>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub imports: Vec<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub build: bool,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub commands: Vec<CommandSpec>,
    // Ask the helper which dropped files and folders it takes, by running it as
    // `<program> claims <path>`, instead of matching `imports` against extensions.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub claims: bool,
    // Send the addon's first section a `startup` event once per launcher start.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub startup: bool,
}

impl Launcher {
    pub fn program(&self) -> Option<&str> {
        self.program.get(std::env::consts::OS).map(String::as_str)
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "lowercase")]
pub enum Kind {
    Database,
    Erp,
    Text,
    Ui,
    Files,
    Native,
    /// Merged into the game's shader packages, as the Simplified Suite loads `.serps` mods.
    Simplified,
}

/// An experimental engine of the in-game core, named as its standard library page's
/// Feature row names it, that an addon turns on by listing it.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[serde(rename_all = "lowercase")]
pub enum ExperimentalFeature {
    /// Drawing over the game with `swofty/overlay.hpp`.
    Overlay,
    /// Grading the game's picture with a [`Shader`] look.
    Shaders,
    /// Running [`PostFx`] passes on the game's picture.
    Postfx,
}

impl ExperimentalFeature {
    /// Its name in `swofty/assets.json`.
    pub fn name(self) -> &'static str {
        match self {
            Self::Overlay => "overlay",
            Self::Shaders => "shaders",
            Self::Postfx => "postfx",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Overlay => "Overlay",
            Self::Shaders => "Shaders",
            Self::Postfx => "Post-processing",
        }
    }

    /// What it does in game, in the player's words.
    pub fn effect(self) -> &'static str {
        match self {
            Self::Overlay => "Draws over the game",
            Self::Shaders => "Changes the picture",
            Self::Postfx => "Changes the picture",
        }
    }
}

/// A colour look: a `.cube` 3D LUT graded in display colour, as a screenshot is, applied to
/// the game's picture after its tonemap and before the HUD.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Shader {
    /// The `.cube` file inside the addon. `{{id}}` names a `db` option, so a choice can pick
    /// between looks.
    pub lut: String,
    /// How much of the look applies, from 0 to 100, or `{{id}}` naming a `db` slider over that
    /// range. Defaults to 100.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub strength: Option<Strength>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum Strength {
    Percent(f64),
    Option(String),
}

/// A post-process pass: an HLSL compute shader written against `swofty/postfx.hlsli`, which
/// the launcher compiles to DXIL at build time.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct PostFx {
    /// The `.hlsl` file inside the addon.
    pub shader: String,
    /// The pass's name in the build output and the core's log. Defaults to the file's stem.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    /// Up to 16 values the shader reads with `option(i)`: numbers, or `{{id}}` naming a `db`
    /// option whose value is a number or a toggle.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub options: Vec<PassValue>,
    /// Where the pass runs among every addon's passes: lower runs first, and equal values keep
    /// the load order.
    #[serde(default, skip_serializing_if = "is_zero")]
    pub order: i32,
    /// GPU milliseconds a frame past which the core turns the pass off. Defaults to 1.5.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub budget_ms: Option<f64>,
}

fn is_zero(v: &i32) -> bool {
    *v == 0
}

impl PostFx {
    pub fn label(&self) -> String {
        self.name.clone().unwrap_or_else(|| {
            Path::new(&self.shader)
                .file_stem()
                .map(|s| s.to_string_lossy().into_owned())
                .unwrap_or_else(|| self.shader.clone())
        })
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum PassValue {
    Number(f64),
    Option(String),
}

/// Most values a pass reads with `option(i)`.
pub const POSTFX_OPTIONS: usize = 16;

/// The option ids a `{{id}}` template names, in order.
pub fn placeholders(text: &str) -> Vec<&str> {
    let mut out = Vec::new();
    let mut rest = text;
    while let Some(start) = rest.find("{{") {
        let Some(end) = rest[start..].find("}}") else {
            break;
        };
        out.push(rest[start + 2..start + end].trim());
        rest = &rest[start + end + 2..];
    }
    out
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Compat {
    pub game: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub game_builds: Vec<VersionReq>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub launcher: Option<VersionReq>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub seasons: Vec<String>,
}

impl Compat {
    pub fn accepts_build(&self, build_id: u64) -> bool {
        let v = Version::new(build_id, 0, 0);
        self.game_builds.is_empty() || self.game_builds.iter().any(|r| r.matches(&v))
    }

    pub fn accepts_launcher(&self, version: &Version) -> bool {
        self.launcher.as_ref().is_none_or(|r| r.matches(version))
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Requirement {
    pub id: String,
    #[serde(default = "any_version")]
    pub version: VersionReq,
}

fn any_version() -> VersionReq {
    VersionReq::STAR
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Conflict {
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub touches: Option<String>,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum OptionType {
    Toggle,
    Slider,
    Choice,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum Binds {
    Db,
    Native,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(from = "ChoiceRepr", into = "ChoiceRepr")]
pub struct Choice {
    pub value: String,
    pub label: Option<String>,
}

#[derive(Serialize, Deserialize)]
#[serde(untagged, deny_unknown_fields)]
enum ChoiceRepr {
    Plain(String),
    Labelled { value: String, label: String },
}

impl From<ChoiceRepr> for Choice {
    fn from(r: ChoiceRepr) -> Self {
        match r {
            ChoiceRepr::Plain(value) => Self { value, label: None },
            ChoiceRepr::Labelled { value, label } => Self {
                value,
                label: Some(label),
            },
        }
    }
}

impl From<Choice> for ChoiceRepr {
    fn from(c: Choice) -> Self {
        match c.label {
            None => Self::Plain(c.value),
            Some(label) => Self::Labelled {
                value: c.value,
                label,
            },
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct AddonOption {
    pub id: String,
    #[serde(rename = "type")]
    pub kind: OptionType,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub label: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    pub default: serde_json::Value,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub values: Vec<Choice>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub min: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub step: Option<f64>,
    pub binds: Binds,
    /// Kept off the addon's ADDON SETTINGS page in game, such as a developer option or one only a career setup
    /// page or hub tab shows.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub hidden: bool,
    /// `cosmetic` leaves the option out of the multiplayer match of a gameplay addon.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub multiplayer: Option<Multiplayer>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Operations {
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub database: Vec<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub erp: Vec<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub ui: Vec<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub files: Vec<FileEntry>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub native: Vec<NativePlugin>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub screens: Vec<ScreenOp>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub assets: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct FileEntry {
    pub source: String,
    pub target: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct NativePlugin {
    pub dll: String,
    // Accepted for compatibility; declaring a capability has no effect.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub capabilities: Vec<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub early: bool,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub files: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct ScreenOp {
    pub name: String,
    /// The screen's `.ui`; empty when `page` has the launcher generate it.
    #[serde(default)]
    pub file: String,
    /// `options`: the launcher builds this addon's options page, as ADDON SETTINGS shows it, as
    /// this screen, opened from `anchor` by the route `open_<name>`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub page: Option<String>,
    #[serde(default = "default_flow")]
    pub flow: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub anchor: Option<String>,
    #[serde(default)]
    pub view_model: bool,
}

fn default_flow() -> String {
    "flow_frontend".into()
}

pub fn valid_screen_name(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 64
        && name
            .bytes()
            .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'_')
}

impl AddonManifest {
    pub fn load(dir: &Path) -> Result<Self> {
        let path = dir.join(MANIFEST_FILE);
        let data = std::fs::read(&path).map_err(|source| Error::Io {
            path: path.clone(),
            source,
        })?;
        let m: Self = serde_json::from_slice(&data).map_err(|source| Error::Json { path, source })?;
        m.validate()?;
        Ok(m)
    }

    pub fn parse(json: &str) -> Result<Self> {
        let m: Self = serde_json::from_str(json).map_err(|e| Error::Manifest(e.to_string()))?;
        m.validate()?;
        Ok(m)
    }

    pub fn is_builtin(&self) -> bool {
        self.id == crate::CORE_ID
    }

    /// `multiplayer` when set; otherwise cosmetic for a `helmets`, `shaders` or `performance`
    /// addon and for one that ships only a `shader` look or `postfx` passes, and gameplay for the
    /// rest.
    pub fn multiplayer(&self) -> Multiplayer {
        if let Some(m) = self.multiplayer {
            return m;
        }
        let look_only = (self.shader.is_some() || !self.postfx.is_empty())
            && self.operations == Operations::default()
            && !self.launcher.as_ref().is_some_and(|l| l.build);
        let cosmetic = matches!(
            self.category,
            Some(Category::Helmets | Category::Shaders | Category::Performance)
        );
        if look_only || cosmetic {
            Multiplayer::Cosmetic
        } else {
            Multiplayer::Gameplay
        }
    }

    pub fn validate(&self) -> Result<()> {
        let fail = |m: String| Err(Error::Manifest(format!("{}: {m}", self.id)));
        if self.schema_version != SCHEMA_VERSION {
            return fail(format!("schema_version {} is not supported", self.schema_version));
        }
        if !valid_id(&self.id) {
            return fail("id must be lowercase dotted [a-z0-9._-] with at least two parts".into());
        }
        if self.compat.game != crate::GAME_ID {
            return fail(format!(
                "compat.game {} is not {}",
                self.compat.game,
                crate::GAME_ID
            ));
        }
        let mut seen = BTreeMap::new();
        for o in &self.options {
            if seen.insert(o.id.as_str(), ()).is_some() {
                return fail(format!("option {} is declared twice", o.id));
            }
            if let Err(e) = o.validate() {
                return fail(format!("option {}: {e}", o.id));
            }
        }
        let mut screens = BTreeMap::new();
        for s in &self.operations.screens {
            if !valid_screen_name(&s.name) || !valid_screen_name(&s.flow) {
                return fail(format!("screen {}: name and flow must be [a-z0-9_]", s.name));
            }
            if s.anchor.as_deref().is_some_and(|a| !valid_screen_name(a)) {
                return fail(format!("screen {}: anchor must be [a-z0-9_]", s.name));
            }
            if screens.insert(s.name.as_str(), ()).is_some() {
                return fail(format!("screen {} is declared twice", s.name));
            }
            match s.page.as_deref() {
                None if s.file.is_empty() => {
                    return fail(format!("screen {}: needs a file, or page: options", s.name));
                }
                Some(p) if p != "options" => {
                    return fail(format!("screen {}: page must be options, not {p}", s.name));
                }
                _ => {}
            }
        }
        let paths = self
            .operations
            .database
            .iter()
            .chain(&self.operations.erp)
            .chain(&self.operations.ui)
            .chain(&self.operations.assets);
        let paths = paths
            .chain(self.operations.files.iter().map(|f| &f.source))
            .chain(
                self.operations
                    .native
                    .iter()
                    .flat_map(|n| std::iter::once(&n.dll).chain(&n.files)),
            )
            .chain(
                self.operations
                    .screens
                    .iter()
                    .filter(|s| s.page.is_none())
                    .map(|s| &s.file),
            )
            .chain(self.launcher.iter().flat_map(|l| l.program.values()))
            .chain(&self.icon);
        if let Some(p) = paths.into_iter().find(|p| !relative_inside(p)) {
            return fail(format!("{p} must be a relative path inside the addon"));
        }
        if let Some(c) = &self.career_setup {
            c.validate(&self.options)
                .or_else(|e| fail(format!("career_setup: {e}")))?;
        }
        if let Some(h) = &self.hub_tab {
            h.validate(&self.options)
                .or_else(|e| fail(format!("hub_tab: {e}")))?;
        }
        if let Some(s) = &self.shader {
            self.validate_shader(s)
                .or_else(|e| fail(format!("shader: {e}")))?;
        }
        for (i, p) in self.postfx.iter().enumerate() {
            self.validate_postfx(p)
                .or_else(|e| fail(format!("postfx[{i}]: {e}")))?;
        }
        if let Some(m) = self.hud.as_ref().and_then(|h| h.mfd.as_ref()) {
            self.validate_mfd(m).or_else(|e| fail(format!("hud.mfd: {e}")))?;
        }
        if let Some(l) = &self.launcher {
            if l.program.is_empty() {
                return fail("launcher.program names no program".into());
            }
            if let Some(e) = l.imports.iter().find(|e| e.is_empty() || e.contains('.')) {
                return fail(format!(
                    "launcher.imports: {e:?} must be an extension without the dot"
                ));
            }
        }
        Ok(())
    }

    fn validate_postfx(&self, p: &PostFx) -> std::result::Result<(), String> {
        if !self.experimental_features.contains(&ExperimentalFeature::Postfx) {
            return Err("list \"postfx\" in experimental_features to use it".into());
        }
        if !relative_inside(&p.shader) {
            return Err(format!(
                "shader {} must be a relative path inside the addon",
                p.shader
            ));
        }
        if !p.shader.to_ascii_lowercase().ends_with(".hlsl") {
            return Err(format!("shader {} must be a .hlsl file", p.shader));
        }
        if p.options.len() > POSTFX_OPTIONS {
            return Err(format!(
                "{} options is more than {POSTFX_OPTIONS}",
                p.options.len()
            ));
        }
        if let Some(b) = p.budget_ms
            && !(b > 0.0 && b <= 16.0)
        {
            return Err(format!("budget_ms {b} is not above 0 and at most 16"));
        }
        for v in &p.options {
            let PassValue::Option(t) = v else { continue };
            let [id] = placeholders(t)[..] else {
                return Err(format!("option {t:?} must be a number or one {{{{option}}}}"));
            };
            if !(t.trim().starts_with("{{") && t.trim().ends_with("}}")) {
                return Err(format!("option {t:?} must be a number or one {{{{option}}}}"));
            }
            match self.options.iter().find(|o| o.id == id) {
                None => return Err(format!("{{{{{id}}}}} names no option")),
                Some(o) if o.binds != Binds::Db => return Err(format!("option {id} must bind db")),
                Some(_) => {}
            }
        }
        Ok(())
    }

    fn validate_mfd(&self, m: &HudMfd) -> std::result::Result<(), String> {
        if m.rows.is_empty() || m.rows.len() > MFD_ROWS_MAX {
            return Err(format!("rows must list 1 to {MFD_ROWS_MAX} options"));
        }
        if let Some(t) = &m.title
            && (t.trim().is_empty() || t.chars().count() > MFD_TEXT_MAX)
        {
            return Err(format!("title must be 1 to {MFD_TEXT_MAX} characters"));
        }
        let mut seen = BTreeMap::new();
        for r in &m.rows {
            if seen.insert(r.as_str(), ()).is_some() {
                return Err(format!("row {r} is listed twice"));
            }
            let Some(o) = self.options.iter().find(|o| &o.id == r) else {
                return Err(format!("row {r} names no option of this addon"));
            };
            if o.kind == OptionType::Slider {
                return Err(format!("row {r}: only toggle and choice options can be MFD rows"));
            }
            if o.label.as_deref().unwrap_or(&o.id).chars().count() > MFD_TEXT_MAX {
                return Err(format!(
                    "row {r}: the option label is longer than {MFD_TEXT_MAX} characters"
                ));
            }
        }
        Ok(())
    }

    fn validate_shader(&self, s: &Shader) -> std::result::Result<(), String> {
        if !self.experimental_features.contains(&ExperimentalFeature::Shaders) {
            return Err("list \"shaders\" in experimental_features to use it".into());
        }
        if !relative_inside(&s.lut) {
            return Err(format!("lut {} must be a relative path inside the addon", s.lut));
        }
        if !s.lut.to_ascii_lowercase().ends_with(".cube") {
            return Err(format!("lut {} must be a .cube file", s.lut));
        }
        if s.lut.matches("{{").count() != placeholders(&s.lut).len() {
            return Err(format!("lut {} has an unclosed {{{{", s.lut));
        }
        let db_option = |id: &str, slider: bool| {
            let o = self.options.iter().find(|o| o.id == id);
            match o {
                None => Err(format!("{{{{{id}}}}} names no option")),
                Some(o) if o.binds != Binds::Db => Err(format!("option {id} must bind db")),
                Some(o) if slider && o.kind != OptionType::Slider => {
                    Err(format!("option {id} must be a slider"))
                }
                Some(o) if slider && (o.min.unwrap_or(0.) < 0. || o.max.unwrap_or(0.) > 100.) => {
                    Err(format!("option {id} must stay within 0 to 100"))
                }
                Some(_) => Ok(()),
            }
        };
        for id in placeholders(&s.lut) {
            db_option(id, false)?;
        }
        match &s.strength {
            Some(Strength::Percent(p)) if !(0.0..=100.0).contains(p) => {
                Err(format!("strength {p} is not from 0 to 100"))
            }
            Some(Strength::Option(t)) => match placeholders(t)[..] {
                [id] if t.trim().starts_with("{{") && t.trim().ends_with("}}") => db_option(id, true),
                _ => Err(format!("strength {t:?} must be a number or one {{{{option}}}}")),
            },
            _ => Ok(()),
        }
    }
}

pub fn valid_id(id: &str) -> bool {
    let parts: Vec<&str> = id.split('.').collect();
    parts.len() >= 2
        && parts.iter().all(|p| {
            !p.is_empty()
                && p.bytes()
                    .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-' || b == b'_')
        })
}

pub fn relative_inside(path: &str) -> bool {
    let p = Path::new(path);
    !path.is_empty()
        && !path.contains('\\')
        && p.components()
            .all(|c| matches!(c, std::path::Component::Normal(_) | std::path::Component::CurDir))
}

#[cfg(test)]
mod tests {
    use super::*;

    const GRID24: &str = r#"{
      "schema_version": 1,
      "id": "swofty.f1-2026-grid24",
      "name": "F1 2026 - 24-Car Grid",
      "version": "1.0.0",
      "author": "swofty",
      "kind": ["database"],
      "compat": { "game": "f1_25", "game_builds": [">=25251167"], "launcher": ">=0.1.0", "seasons": ["f1_2026"] },
      "requires": [{ "id": "swofty.core", "version": ">=0.1.0" }],
      "conflicts": [{ "id": "*", "touches": "db:entrants.entrant_lists.entrant_list_34.*" }],
      "load_after": ["swofty.track.*"],
      "options": [{ "id": "team_livery", "type": "choice", "default": "titlesponsor",
                    "values": ["titlesponsor", { "value": "tier01", "label": "Tier 1" }, "replica"], "binds": "db" }],
      "operations": { "database": ["db/grid24.json"] }
    }"#;

    #[test]
    fn hud_mfd_rows_name_toggle_or_choice_options() {
        let with = |hud: &str| GRID24.replace(r#""operations":"#, &format!(r#""hud": {hud}, "operations":"#));
        let m = AddonManifest::parse(&with(
            r#"{ "mfd": { "title": "LIVERY", "rows": ["team_livery"] } }"#,
        ))
        .unwrap();
        let mfd = m.hud.unwrap().mfd.unwrap();
        assert_eq!(mfd.title.as_deref(), Some("LIVERY"));
        assert_eq!(mfd.rows, ["team_livery"]);
        for (bad, why) in [
            (r#"{ "mfd": { "rows": [] } }"#, "rows must list"),
            (r#"{ "mfd": { "rows": ["missing"] } }"#, "names no option"),
            (
                r#"{ "mfd": { "rows": ["team_livery", "team_livery"] } }"#,
                "listed twice",
            ),
            (
                r#"{ "mfd": { "title": "", "rows": ["team_livery"] } }"#,
                "title must be",
            ),
            (
                r#"{ "mfd": { "rows": ["team_livery"], "page": 2 } }"#,
                "unknown field",
            ),
        ] {
            let e = AddonManifest::parse(&with(bad)).unwrap_err().to_string();
            assert!(e.contains(why), "{bad}: {e}");
        }
        let slider = GRID24
            .replace(
                r#""type": "choice", "default": "titlesponsor","#,
                r#""type": "slider", "default": 1, "min": 0, "max": 2, "step": 1,"#,
            )
            .replace(
                r#""values": ["titlesponsor", { "value": "tier01", "label": "Tier 1" }, "replica"], "#,
                "",
            );
        let e = AddonManifest::parse(&slider.replace(
            r#""operations":"#,
            r#""hud": { "mfd": { "rows": ["team_livery"] } }, "operations":"#,
        ))
        .unwrap_err()
        .to_string();
        assert!(e.contains("only toggle and choice"), "{e}");
    }

    #[test]
    fn parses_design_example() {
        let m = AddonManifest::parse(GRID24).unwrap();
        assert_eq!(m.version, Version::new(1, 0, 0));
        assert!(m.compat.accepts_build(25_251_167));
        assert!(!m.compat.accepts_build(25_000_000));
        assert!(m.compat.accepts_launcher(&Version::new(1, 0, 9)));
        let newer = Compat {
            launcher: Some(VersionReq::parse(">=1.0.9").unwrap()),
            ..m.compat.clone()
        };
        assert!(newer.accepts_launcher(&Version::new(1, 0, 9)));
        assert!(!newer.accepts_launcher(&Version::new(1, 0, 8)));
        assert_eq!(m.options[0].binds, Binds::Db);
    }

    #[test]
    fn kind_takes_text_beside_the_older_values() {
        assert_eq!(AddonManifest::parse(GRID24).unwrap().kind, [Kind::Database]);
        let m = AddonManifest::parse(&GRID24.replace(
            r#""kind": ["database"]"#,
            r#""kind": ["database", "erp", "text", "ui", "files", "native", "simplified"]"#,
        ))
        .unwrap();
        assert_eq!(m.kind[2], Kind::Text);
        assert_eq!(m.kind[6], Kind::Simplified);
        assert_eq!(serde_json::to_value(&m).unwrap()["kind"][2], "text");
        assert_eq!(serde_json::to_value(&m).unwrap()["kind"][6], "simplified");
        assert!(
            AddonManifest::parse(&GRID24.replace(r#""kind": ["database"]"#, r#""kind": ["lang"]"#)).is_err()
        );
    }

    #[test]
    fn parses_early_native_plugins() {
        let with = |native: &str| {
            GRID24.replace(
                r#""operations": { "database": ["db/grid24.json"] }"#,
                &format!(r#""operations": {{ "native": [{native}] }}"#),
            )
        };
        let m = AddonManifest::parse(&with(
            r#"{ "dll": "native/a.dll", "early": true }, { "dll": "native/b.dll" }"#,
        ))
        .unwrap();
        let n = &m.operations.native;
        assert!(n[0].early && !n[1].early);
        let json = serde_json::to_value(&m.operations).unwrap();
        assert_eq!(json["native"][0]["early"], true);
        assert!(json["native"][1].get("early").is_none());
        let m = AddonManifest::parse(&with(
            r#"{ "dll": "native/a.dll", "files": ["lang/", "erp/a.bin"] }"#,
        ))
        .unwrap();
        assert_eq!(m.operations.native[0].files, ["lang/", "erp/a.bin"]);
        assert!(json["native"][1].get("files").is_none());
        assert!(AddonManifest::parse(&with(r#"{ "dll": "native/a.dll", "files": ["../x"] }"#)).is_err());
    }

    #[test]
    fn rejects_bad_ids_and_paths() {
        assert!(!valid_id("Grid24"));
        assert!(!valid_id("single"));
        assert!(valid_id("author.mod-name"));
        assert!(!relative_inside("../escape.json"));
        assert!(!relative_inside("/abs.json"));
        assert!(relative_inside("db/ops.json"));
        let bad = GRID24.replace("db/grid24.json", "../grid24.json");
        assert!(AddonManifest::parse(&bad).is_err());
    }

    #[test]
    fn parses_and_checks_screens() {
        let with = |screens: &str| {
            GRID24.replace(
                r#""operations": { "database": ["db/grid24.json"] }"#,
                &format!(r#""operations": {{ "screens": [{screens}] }}"#),
            )
        };
        let m = AddonManifest::parse(&with(
            r#"{ "name": "grid_page", "file": "ui/grid_page.ui", "flow": "flow_options", "anchor": "settings_container", "view_model": true },
               { "name": "grid_info", "file": "ui/grid_info.ui" }"#,
        ))
        .unwrap();
        let s = &m.operations.screens;
        assert_eq!(
            (s[0].flow.as_str(), s[0].anchor.as_deref(), s[0].view_model),
            ("flow_options", Some("settings_container"), true)
        );
        assert_eq!(
            (s[1].flow.as_str(), s[1].anchor.as_deref(), s[1].view_model),
            ("flow_frontend", None, false)
        );
        let m = AddonManifest::parse(&with(
            r#"{ "name": "grid_options", "page": "options", "flow": "flow_options", "anchor": "settings_container" }"#,
        ))
        .unwrap();
        let s = &m.operations.screens[0];
        assert_eq!((s.page.as_deref(), s.file.as_str()), (Some("options"), ""));
        for bad in [
            r#"{ "name": "a" }"#,
            r#"{ "name": "a", "page": "about" }"#,
            r#"{ "name": "a", "file": "../a.ui" }"#,
            r#"{ "name": "Bad", "file": "ui/a.ui" }"#,
            r#"{ "name": "a", "file": "ui/a.ui", "anchor": "x.y" }"#,
            r#"{ "name": "a", "file": "ui/a.ui" }, { "name": "a", "file": "ui/b.ui" }"#,
            r#"{ "name": "a", "file": "ui/a.ui", "extra": 1 }"#,
        ] {
            assert!(AddonManifest::parse(&with(bad)).is_err(), "{bad}");
        }
    }

    #[test]
    fn parses_the_saves_flag() {
        let with = |saves: &str| {
            GRID24.replace(
                r#""author": "swofty","#,
                &format!(r#""author": "swofty", {saves}"#),
            )
        };
        let m = AddonManifest::parse(GRID24).unwrap();
        assert_eq!(m.saves, Saves::default());
        assert!(!m.saves.affects);
        assert!(serde_json::to_value(&m).unwrap().get("saves").is_none());
        let m = AddonManifest::parse(&with(r#""saves": { "affects": true },"#)).unwrap();
        assert!(m.saves.affects);
        assert_eq!(m.saves.message(), None);
        let m = AddonManifest::parse(&with(
            r#""saves": { "affects": true, "message": "Retire drivers instead of removing the addon." },"#,
        ))
        .unwrap();
        assert!(m.saves.affects);
        assert_eq!(
            m.saves.message(),
            Some("Retire drivers instead of removing the addon.")
        );
        let json = serde_json::to_value(&m).unwrap();
        assert_eq!(json["saves"]["affects"], true);
        assert_eq!(AddonManifest::parse(&json.to_string()).unwrap(), m);
        let m = AddonManifest::parse(&with(r#""saves": {},"#)).unwrap();
        assert!(!m.saves.affects);
        assert_eq!(
            AddonManifest::parse(&with(r#""saves": { "affects": true, "message": " " },"#))
                .unwrap()
                .saves
                .message(),
            None
        );
        for bad in [
            r#""saves": { "affects": true, "extra": 1 },"#,
            r#""saves": { "affects": "yes" },"#,
            r#""saves": true,"#,
        ] {
            assert!(AddonManifest::parse(&with(bad)).is_err(), "{bad}");
        }
    }

    #[test]
    fn category_and_origin_are_optional_and_round_trip() {
        let m = AddonManifest::parse(GRID24).unwrap();
        assert_eq!((m.category, &m.origin), (None, &None));
        let json = serde_json::to_value(&m).unwrap();
        assert!(json.get("category").is_none() && json.get("origin").is_none());
        let with = GRID24.replace(
            r#""author": "swofty","#,
            r#""author": "swofty", "category": "helmets",
               "origin": { "tool": "teamsimplified.mods", "tool_version": "2.0.0", "file": "grid.zip", "sha256": "ab" },"#,
        );
        let m = AddonManifest::parse(&with).unwrap();
        assert_eq!(m.category, Some(Category::Helmets));
        assert_eq!(m.category.unwrap().label(), "Helmets");
        let origin = m.origin.as_ref().unwrap();
        assert_eq!(
            (origin.tool.as_str(), origin.file.as_deref()),
            ("teamsimplified.mods", Some("grid.zip"))
        );
        let json = serde_json::to_value(&m).unwrap();
        assert_eq!(json["category"], "helmets");
        assert_eq!(AddonManifest::parse(&json.to_string()).unwrap(), m);
        let bare = with.replace(
            r#", "tool_version": "2.0.0", "file": "grid.zip", "sha256": "ab""#,
            "",
        );
        let json = serde_json::to_value(AddonManifest::parse(&bare).unwrap()).unwrap();
        assert_eq!(
            json["origin"],
            serde_json::json!({ "tool": "teamsimplified.mods" })
        );
        for c in Category::ALL {
            let v = serde_json::to_value(c).unwrap();
            assert_eq!(serde_json::from_value::<Category>(v).unwrap(), c);
        }
        let bad = GRID24.replace(
            r#""author": "swofty","#,
            r#""author": "swofty", "origin": { "tool": "x", "extra": 1 },"#,
        );
        assert!(AddonManifest::parse(&bad).is_err(), "{bad}");
    }

    #[test]
    fn earlier_categories_read_as_the_nearest_or_none() {
        let read = |c: &str| {
            let with = GRID24.replace(
                r#""author": "swofty","#,
                &format!(r#""author": "swofty", "category": {c},"#),
            );
            AddonManifest::parse(&with).unwrap().category
        };
        assert_eq!(read(r#""liveries""#), Some(Category::Cars));
        assert_eq!(read(r#""tracks""#), Some(Category::Tracks));
        assert_eq!(read("null"), None);
        assert_eq!(read(r#""maps""#), None);
        assert_eq!(read(r#""career""#), Some(Category::Career));
        assert_eq!(read(r#""ai""#), Some(Category::Ai));
        assert_eq!(Category::Ai.label(), "AI");
        for c in Category::RETIRED {
            assert_eq!(read(&format!("{c:?}")), None, "{c}");
        }
        let m = AddonManifest::parse(&GRID24.replace(
            r#""author": "swofty","#,
            r#""author": "swofty", "category": "liveries","#,
        ))
        .unwrap();
        assert_eq!(serde_json::to_value(&m).unwrap()["category"], "cars");
    }

    #[test]
    fn experimental_features_name_the_core_engines_an_addon_turns_on() {
        let m = AddonManifest::parse(GRID24).unwrap();
        assert!(m.experimental_features.is_empty());
        assert!(
            serde_json::to_value(&m)
                .unwrap()
                .get("experimental_features")
                .is_none()
        );
        let with = |f: &str| {
            GRID24.replace(
                r#""author": "swofty","#,
                &format!(r#""author": "swofty", "experimental_features": {f},"#),
            )
        };
        let m = AddonManifest::parse(&with(r#"["overlay"]"#)).unwrap();
        assert_eq!(m.experimental_features, [ExperimentalFeature::Overlay]);
        let f = m.experimental_features[0];
        assert_eq!((f.name(), f.label()), ("overlay", "Overlay"));
        assert_eq!(
            serde_json::to_value(&m).unwrap()["experimental_features"][0],
            "overlay"
        );
        assert!(AddonManifest::parse(&with(r#"["hud"]"#)).is_err());
        let m = AddonManifest::parse(&with(r#"["shaders"]"#)).unwrap();
        let f = m.experimental_features[0];
        assert_eq!((f.name(), f.label()), ("shaders", "Shaders"));
    }

    const LOOKS: &str = r#"{
        "schema_version": 1, "id": "author.looks", "name": "Looks", "version": "1.0.0", "author": "a",
        "category": "shaders", "experimental_features": ["shaders"], "compat": { "game": "f1_25" },
        "options": [
            { "id": "look", "type": "choice", "default": "mono", "values": ["mono", "noir"], "binds": "db" },
            { "id": "strength", "type": "slider", "min": 0, "max": 100, "step": 10, "default": 100, "binds": "db" },
            { "id": "live", "type": "toggle", "default": true, "binds": "native" }
        ],
        "shader": SHADER
    }"#;

    fn looks(shader: &str) -> std::result::Result<AddonManifest, String> {
        AddonManifest::parse(&LOOKS.replace("SHADER", shader)).map_err(|e| e.to_string())
    }

    #[test]
    fn a_shader_pack_names_its_lut_and_strength() {
        let m = looks(r#"{ "lut": "looks/{{look}}.cube", "strength": "{{ strength }}" }"#).unwrap();
        assert_eq!(m.category, Some(Category::Shaders));
        assert_eq!(Category::Shaders.label(), "Shaders");
        let s = m.shader.as_ref().unwrap();
        assert_eq!(s.lut, "looks/{{look}}.cube");
        assert_eq!(s.strength, Some(Strength::Option("{{ strength }}".into())));
        assert_eq!(placeholders(&s.lut), ["look"]);
        let m = looks(r#"{ "lut": "looks/mono.CUBE", "strength": 40 }"#).unwrap();
        assert_eq!(m.shader.unwrap().strength, Some(Strength::Percent(40.0)));
        assert_eq!(
            looks(r#"{ "lut": "mono.cube" }"#)
                .unwrap()
                .shader
                .unwrap()
                .strength,
            None
        );
    }

    #[test]
    fn a_postfx_pass_is_checked_before_it_installs() {
        let with = |postfx: &str, features: &str| {
            AddonManifest::parse(&format!(
                r#"{{ "schema_version": 1, "id": "author.fx", "name": "Fx", "version": "1.0.0", "author": "a",
                    "experimental_features": {features}, "compat": {{ "game": "f1_25" }},
                    "options": [{{ "id": "amount", "type": "slider", "min": 0, "max": 100, "step": 1, "default": 50, "binds": "db" }}],
                    "postfx": {postfx} }}"#
            ))
            .map_err(|e| e.to_string())
        };
        let m = with(
            r#"[{ "shader": "fx/grain.hlsl", "options": [0.5, "{{amount}}"], "order": -1 }]"#,
            r#"["postfx"]"#,
        )
        .unwrap();
        assert_eq!(m.postfx[0].label(), "grain");
        assert_eq!(m.postfx[0].order, -1);
        assert_eq!(ExperimentalFeature::Postfx.name(), "postfx");
        let err = |postfx: &str| with(postfx, r#"["postfx"]"#).unwrap_err();
        let at = |msg: &str| format!("invalid manifest: author.fx: postfx[0]: {msg}");
        assert_eq!(
            with(r#"[{ "shader": "fx/grain.hlsl" }]"#, "[]").unwrap_err(),
            at(r#"list "postfx" in experimental_features to use it"#)
        );
        assert_eq!(
            err(r#"[{ "shader": "../grain.hlsl" }]"#),
            at("shader ../grain.hlsl must be a relative path inside the addon")
        );
        assert_eq!(
            err(r#"[{ "shader": "fx/grain.fx" }]"#),
            at("shader fx/grain.fx must be a .hlsl file")
        );
        assert_eq!(
            err(r#"[{ "shader": "fx/g.hlsl", "options": ["{{nope}}"] }]"#),
            at("{{nope}} names no option")
        );
        assert_eq!(
            err(r#"[{ "shader": "fx/g.hlsl", "budget_ms": 0 }]"#),
            at("budget_ms 0 is not above 0 and at most 16")
        );
        assert_eq!(
            err(&format!(
                r#"[{{ "shader": "fx/g.hlsl", "options": [{}] }}]"#,
                vec!["1"; 17].join(",")
            )),
            at("17 options is more than 16")
        );
    }

    #[test]
    fn a_shader_pack_is_checked_before_it_installs() {
        let err = |shader: &str| looks(shader).unwrap_err();
        let at = |msg: &str| format!("invalid manifest: author.looks: shader: {msg}");
        assert_eq!(
            err(r#"{ "lut": "mono.png" }"#),
            at("lut mono.png must be a .cube file")
        );
        assert_eq!(
            err(r#"{ "lut": "../mono.cube" }"#),
            at("lut ../mono.cube must be a relative path inside the addon")
        );
        assert_eq!(
            err(r#"{ "lut": "{{look.cube" }"#),
            at("lut {{look.cube has an unclosed {{")
        );
        assert_eq!(
            err(r#"{ "lut": "{{shade}}.cube" }"#),
            at("{{shade}} names no option")
        );
        assert_eq!(
            err(r#"{ "lut": "{{live}}.cube" }"#),
            at("option live must bind db")
        );
        assert_eq!(
            err(r#"{ "lut": "a.cube", "strength": 150 }"#),
            at("strength 150 is not from 0 to 100")
        );
        assert_eq!(
            err(r#"{ "lut": "a.cube", "strength": "{{look}}" }"#),
            at("option look must be a slider")
        );
        assert_eq!(
            err(r#"{ "lut": "a.cube", "strength": "x{{strength}}" }"#),
            at(r#"strength "x{{strength}}" must be a number or one {{option}}"#)
        );
        let no_feature = LOOKS
            .replace(r#""experimental_features": ["shaders"],"#, "")
            .replace("SHADER", r#"{ "lut": "a.cube" }"#);
        assert_eq!(
            AddonManifest::parse(&no_feature).unwrap_err().to_string(),
            at(r#"list "shaders" in experimental_features to use it"#)
        );
    }

    #[test]
    fn rejects_unknown_fields() {
        let bad = GRID24.replace("\"kind\"", "\"kinds\"");
        assert!(AddonManifest::parse(&bad).is_err());
    }

    #[test]
    fn a_career_setup_names_a_route_and_options_of_its_addon() {
        let with = |setup: &str| {
            GRID24.replace(
                r#""operations": { "database": ["db/grid24.json"] }"#,
                &format!(r#""operations": {{ "database": ["db/grid24.json"] }}, "career_setup": {setup}"#),
            )
        };
        let m = AddonManifest::parse(&with(
            r#"{ "route": "career_wizard_driver_career_grid", "title": "Grid Career", "options": ["team_livery"] }"#,
        ))
        .unwrap();
        let c = m.career_setup.as_ref().unwrap();
        assert_eq!(c.next, CAREER_SETUP_NEXT);
        assert_eq!(c.screen(), "swofty_setup_career_wizard_driver_career_grid");
        assert_eq!(c.back_route(), "back_career_wizard_driver_career_grid");
        let json = serde_json::to_value(&m).unwrap();
        assert!(json["career_setup"].get("next").is_none());
        assert_eq!(json["career_setup"]["options"][0], "team_livery");
        let m = AddonManifest::parse(&with(
            r#"{ "route": "grid", "options": ["team_livery"], "next": "career_wizard_settings_select" }"#,
        ))
        .unwrap();
        let c = m.career_setup.unwrap();
        assert_eq!(
            (c.title, c.next.as_str()),
            (None, "career_wizard_settings_select")
        );

        let at = |m: &str| format!("invalid manifest: swofty.f1-2026-grid24: career_setup: {m}");
        for (setup, why) in [
            (
                r#"{ "route": "Grid", "options": ["team_livery"] }"#,
                r#"route "Grid" must be [a-z0-9_], at most 48 characters"#.to_owned(),
            ),
            (
                &format!(
                    r#"{{ "route": "{}", "options": ["team_livery"] }}"#,
                    "r".repeat(49)
                ),
                format!(
                    r#"route "{}" must be [a-z0-9_], at most 48 characters"#,
                    "r".repeat(49)
                ),
            ),
            (
                r#"{ "route": "grid", "options": [] }"#,
                "options names no option".to_owned(),
            ),
            (
                r#"{ "route": "grid", "options": ["nope"] }"#,
                "option nope is not one of the addon's options".to_owned(),
            ),
            (
                r#"{ "route": "grid", "options": ["team_livery", "team_livery"] }"#,
                "option team_livery is listed twice".to_owned(),
            ),
            (
                r#"{ "route": "grid", "title": " ", "options": ["team_livery"] }"#,
                "title is empty".to_owned(),
            ),
            (
                r#"{ "route": "grid", "options": ["team_livery"], "next": "a-b" }"#,
                r#"next "a-b" must be [a-z0-9_]"#.to_owned(),
            ),
        ] {
            assert_eq!(
                AddonManifest::parse(&with(setup)).unwrap_err().to_string(),
                at(&why),
                "{setup}"
            );
        }
        assert!(
            AddonManifest::parse(&with(
                r#"{ "route": "grid", "options": ["team_livery"], "back": "fe_career" }"#
            ))
            .is_err()
        );
        let hidden = with(r#"{ "route": "grid", "options": ["team_livery"] }"#)
            .replace(r#""binds": "db" }]"#, r#""binds": "db", "hidden": true }]"#);
        let m = AddonManifest::parse(&hidden).unwrap();
        assert!(m.options[0].hidden);
        assert_eq!(m.career_setup.unwrap().options, ["team_livery"]);
    }

    #[test]
    fn a_hub_tab_names_its_template_view_model_and_shown_options() {
        let with = |tab: &str| {
            GRID24.replace(
                r#""operations": { "database": ["db/grid24.json"] }"#,
                &format!(r#""operations": {{ "database": ["db/grid24.json"] }}, "hub_tab": {tab}"#),
            )
        };
        let m = AddonManifest::parse(&with(
            r#"{ "name": "progressive", "title": "Progressive Settings", "options": ["team_livery"],
                 "data_group": "f1_career_progressive" }"#,
        ))
        .unwrap();
        let h = m.hub_tab.as_ref().unwrap();
        assert_eq!(h.template(), "swofty_hub_progressive");
        assert_eq!(h.view_model(), "view_model_data.hud_swofty_hub_progressive");
        assert_eq!(
            serde_json::to_value(&m).unwrap()["hub_tab"]["data_group"],
            "f1_career_progressive"
        );
        let at = |m: &str| format!("invalid manifest: swofty.f1-2026-grid24: hub_tab: {m}");
        let tab = |name: &str, title: &str, options: &str, group: &str| {
            format!(
                r#"{{ "name": "{name}", "title": "{title}", "options": {options}, "data_group": "{group}" }}"#
            )
        };
        for (t, why) in [
            (
                tab("Pro", "T", r#"["team_livery"]"#, "g"),
                r#"name "Pro" must be [a-z0-9_], at most 40 characters"#,
            ),
            (tab("p", " ", r#"["team_livery"]"#, "g"), "title is empty"),
            (
                tab("p", "T", r#"["team_livery"]"#, "a.b"),
                r#"data_group "a.b" must be [a-z0-9_]"#,
            ),
            (tab("p", "T", "[]", "g"), "options must name 1 to 6 options"),
            (
                tab("p", "T", r#"["nope"]"#, "g"),
                "option nope is not one of the addon's options",
            ),
            (
                tab("p", "T", r#"["team_livery", "team_livery"]"#, "g"),
                "option team_livery is listed twice",
            ),
        ] {
            assert_eq!(
                AddonManifest::parse(&with(&t)).unwrap_err().to_string(),
                at(why),
                "{t}"
            );
        }
        let hidden = with(&tab("p", "T", r#"["team_livery"]"#, "g"))
            .replace(r#""binds": "db" }]"#, r#""binds": "db", "hidden": true }]"#);
        let m = AddonManifest::parse(&hidden).unwrap();
        assert!(m.options[0].hidden);
        assert_eq!(m.hub_tab.unwrap().options, ["team_livery"]);
    }

    #[test]
    fn an_option_can_be_hidden_from_addon_settings() {
        let m = AddonManifest::parse(GRID24).unwrap();
        assert!(!m.options[0].hidden);
        assert!(
            serde_json::to_value(&m).unwrap()["options"][0]
                .get("hidden")
                .is_none()
        );
        let m = AddonManifest::parse(
            &GRID24.replace(r#""binds": "db" }]"#, r#""binds": "db", "hidden": true }]"#),
        )
        .unwrap();
        assert!(m.options[0].hidden);
        assert_eq!(serde_json::to_value(&m).unwrap()["options"][0]["hidden"], true);
    }
}
