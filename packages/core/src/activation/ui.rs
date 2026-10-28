use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::path::{Path, PathBuf};

use swofty_formats::erp::{Entry, Erp, ErpFile, Resource, STORED, ZSTD, compress_at};
use swofty_formats::pkg::{Node, Obj, Pkg, Val, Value};

use super::place::{ASSET_ROOT, SLOT_DIR, slot_asset_group, slot_file};
use crate::map::TrackPackage;
use crate::{Error, Result, io_err};

pub const PICTURE_SLOT: &str = "invitational_image_2";
const UI: &str = "2025_asset_groups/ui_package";
const OVERRIDES: &str = "eaid://ui_package/databases/overrides.uidb";
const MINIMAP: &str = "eaid://ui_package/databases/minimap.uidb";
const UIDTA: &str = "eaid://ui_package/art/texture_arrays/ui_dynamic_texture_loading.uidta";
const ARRAY_LEVEL: i32 = 3;

pub const UI_ERPS: &[&str] = &[
    "databases.erp",
    "persistent.erp",
    "flow_race.erp",
    "common_front_end.erp",
    "flow_f1_world.erp",
    "flow_career_hub.erp",
    "flow_career_season_setup.erp",
    "flow_car_monitor.erp",
    "flow_frontend.erp",
    "screens/challenge_career_episode.erp",
];

pub const ARRAY_SLOTS: &[(&str, &str)] = &[
    ("tracks.flags_circle", "flag_circle"),
    ("tracks.flags_rectangle", "flag_rectangle"),
    ("tracks.flags_rectangle_small", "flag_rectangle_small"),
    ("fe_world.tracks", "preview_small"),
    ("fe_world.tracks_26", "preview_small"),
    ("fe_world.tracks_bg", "preview_background"),
    ("fe_world.tracks_26_bg", "preview_background"),
    ("fe_world.tracks_spline", "spline"),
    ("fe_world.tracks_spline_large", "spline_large"),
];

const DYNAMIC: &[(&str, &[&str])] = &[
    (
        "fe_world.track_select",
        &["preview_scene_1", "preview_scene_2", "preview_scene_3"],
    ),
    (
        "fe_world.track_select_26",
        &["preview_scene_1", "preview_scene_2", "preview_scene_3"],
    ),
    (
        "fe_world.track_select_f2",
        &["preview_scene_1", "preview_scene_2", "preview_scene_3"],
    ),
    (
        "fe_world.track_select_26_f2",
        &["preview_scene_1", "preview_scene_2", "preview_scene_3"],
    ),
    ("session_manager.track_images", &["session_image"]),
    ("session_manager.track_images_26", &["session_image"]),
];

fn ui_file(rel: &str) -> String {
    format!("{UI}/{rel}")
}

fn texarray(list: &str) -> String {
    format!("eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.{list}.texarray")
}

fn shared_picture(code: &str, key: &str) -> String {
    format!("eaid://ui_package/art/textures/track_installer/shared/{code}/{key}.tif")
}

pub struct MapUi<'a> {
    pub pkg: &'a TrackPackage,
    pub dir: &'a Path,
    pub asset_group: String,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct UiBuild {
    pub dir: PathBuf,
    pub replaced: BTreeMap<String, PathBuf>,
    pub lists: BTreeMap<String, BTreeSet<String>>,
    pub kept: Kept,
    pub notes: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq)]
struct Surface {
    format: u32,
    width: u32,
    height: u32,
    mips: u32,
    slices: u32,
    extra: u32,
}

fn u32_at(d: &[u8], at: usize) -> Result<u32> {
    d.get(at..at + 4)
        .map(|b| u32::from_le_bytes(b.try_into().expect("4 bytes")))
        .ok_or_else(|| Error::Map("surface header is too short".into()))
}

fn put_u32(d: &mut [u8], at: usize, v: u32) {
    d[at..at + 4].copy_from_slice(&v.to_le_bytes());
}

fn surface(head: &[u8]) -> Result<Surface> {
    Ok(Surface {
        format: u32_at(head, 8)?,
        width: u32_at(head, 12)?,
        height: u32_at(head, 16)?,
        mips: u32_at(head, 24)?,
        slices: u32_at(head, 28)?,
        extra: u32_at(head, 36)?,
    })
}

fn set_fragment(res: &mut Resource, index: usize, data: &[u8], level: i32) -> Result<()> {
    let f = res
        .fragments
        .get_mut(index)
        .ok_or_else(|| Error::Map(format!("{}: no fragment {index}", res.name)))?;
    let zstd =
        f.compression == ZSTD || (f.compression != STORED && f.raw.starts_with(&[0x28, 0xb5, 0x2f, 0xfd]));
    f.raw = if zstd {
        compress_at(data, level)?
    } else {
        data.to_vec()
    };
    f.size = data.len() as u64;
    f.packed = f.raw.len() as u64;
    res.hash = res.content_hash()?;
    Ok(())
}

struct Map<'a> {
    ui: &'a MapUi<'a>,
    images: BTreeMap<String, String>,
    minimap: Option<serde_json::Value>,
    res: HashMap<String, Resource>,
}

impl<'a> Map<'a> {
    fn load(ui: &'a MapUi<'a>) -> Result<Self> {
        let files = ui.pkg.ui.as_ref();
        let read = |rel: Option<&String>| -> Result<Option<serde_json::Value>> {
            match rel {
                Some(rel) => crate::read_json(&ui.dir.join(rel)).map(Some),
                None => Ok(None),
            }
        };
        let images: BTreeMap<String, String> = read(files.and_then(|u| u.images_file.as_ref()))?
            .and_then(|v| v.get("resources").cloned())
            .and_then(|v| serde_json::from_value(v).ok())
            .unwrap_or_default();
        let minimap = read(files.and_then(|u| u.minimap_file.as_ref()))?;
        let want: BTreeSet<String> = images
            .values()
            .flat_map(|e| [e.clone(), format!("{e}.image")])
            .collect();
        let res = Erp::read_matching(&ui.dir.join(&ui.pkg.erp), |n| want.contains(n))?
            .into_iter()
            .map(|r| (r.name.clone(), r))
            .collect();
        Ok(Self {
            ui,
            images,
            minimap,
            res,
        })
    }

    fn code(&self) -> &str {
        &self.ui.pkg.track_code
    }

    fn image(&self, key: &str) -> Result<Option<(&Resource, &Resource)>> {
        let Some(eaid) = self.images.get(key) else {
            return Ok(None);
        };
        let view = self.res.get(eaid);
        let surf = self.res.get(&format!("{eaid}.image"));
        match (view, surf) {
            (Some(v), Some(s)) => Ok(Some((v, s))),
            _ => Err(Error::Map(format!(
                "{}: images.json names {eaid}, which the archive lacks",
                self.code()
            ))),
        }
    }
}

fn obj_at<'n>(node: &'n mut Node, path: &[&str]) -> Result<&'n mut Node> {
    let mut cur = node;
    for p in path {
        cur = cur
            .as_obj_mut()
            .and_then(|o| o.get_mut(p))
            .ok_or_else(|| Error::Map(format!("UI database has no {}", path.join("."))))?;
    }
    Ok(cur)
}

fn name_of(n: &Node) -> Option<&str> {
    n.as_obj()?.get("Name")?.as_val()?.as_str()
}

fn set_str(node: &mut Node, key: &str, value: &str) -> Result<()> {
    let o = node
        .as_obj_mut()
        .ok_or_else(|| Error::Map(format!("UI entry is not an object at {key}")))?;
    match o.get_mut(key).and_then(Node::as_val_mut) {
        Some(v) if v.as_str().is_some() => v.set_str(value),
        Some(_) => return Err(Error::Map(format!("UI entry {key} is not a string"))),
        None => o.set(key, Node::Val(Val::str(value))),
    }
    Ok(())
}

fn set_value_str(entry: &mut Node, field: &str, value: &str) -> Result<()> {
    let f = obj_at(entry, &["Contents", field])?;
    set_str(f, "Value", value)
}

fn set_f32(node: &mut Node, value: f32) -> Result<()> {
    let v = node
        .as_obj_mut()
        .and_then(|o| o.get_mut("Value"))
        .and_then(Node::as_val_mut)
        .ok_or_else(|| Error::Map("UI value is missing".into()))?;
    v.value = Value::Raw(value.to_le_bytes().to_vec());
    Ok(())
}

fn upsert(items: &mut Vec<Node>, name: &str, entry: Node) -> usize {
    match items.iter().position(|n| name_of(n) == Some(name)) {
        Some(i) => {
            items[i] = entry;
            i
        }
        None => {
            items.push(entry);
            items.len() - 1
        }
    }
}

fn put_slice(data: &mut Vec<u8>, slices: &mut u32, index: usize, pixels: &[u8], blank: Option<usize>) {
    let size = pixels.len();
    let count = *slices as usize;
    if index > count {
        let fill = blank
            .filter(|b| *b < count)
            .and_then(|b| data.get(b * size..(b + 1) * size))
            .map(<[u8]>::to_vec)
            .unwrap_or_else(|| vec![0; size]);
        for _ in count..index {
            data.extend_from_slice(&fill);
        }
        *slices = index as u32;
    }
    if index < *slices as usize {
        data[index * size..(index + 1) * size].copy_from_slice(pixels);
    } else {
        data.extend_from_slice(pixels);
        *slices += 1;
    }
}

fn edit_pkg(res: &mut Resource, edit: impl FnOnce(&mut Pkg) -> Result<()>) -> Result<()> {
    let mut pkg = Pkg::parse(&res.fragments[0].data()?)?;
    edit(&mut pkg)?;
    set_fragment(res, 0, &pkg.to_bytes()?, 19)
}

fn f(v: &serde_json::Value) -> Result<f32> {
    v.as_f64()
        .map(|x| x as f32)
        .ok_or_else(|| Error::Map(format!("minimap.json: {v} is not a number")))
}

fn minimap_entry(
    template: &Node,
    code: &str,
    index: usize,
    asset_group: &str,
    mm: &serde_json::Value,
) -> Result<Node> {
    let mut e = template.clone();
    set_str(&mut e, "Name", code)?;
    for (key, default) in [
        ("rotation", 0.0),
        ("padding", 0.0),
        ("zoom_factor", 1.0),
        ("dimensions", 1024.0),
    ] {
        if let Ok(node) = obj_at(&mut e, &["Contents", key]) {
            set_f32(node, mm.get(key).map(f).transpose()?.unwrap_or(default))?;
        }
    }
    set_value_str(&mut e, "asset_group", asset_group)?;
    set_value_str(
        &mut e,
        "current_texture_dict",
        &format!("hud.minimap.tracks[{index}].textures"),
    )?;
    for dict in ["textures", "textures_om_xm"] {
        let Some(src) = mm.get(dict) else { continue };
        for (corner, xy) in src
            .get("corners")
            .and_then(|c| c.as_object())
            .into_iter()
            .flatten()
        {
            let arr = obj_at(&mut e, &["Contents", dict, "Contents", corner, "Contents"])?
                .as_arr_mut()
                .ok_or_else(|| Error::Map(format!("minimap {dict}.{corner} is not a list")))?;
            for (i, node) in arr.items.iter_mut().take(2).enumerate() {
                set_f32(node, f(&xy[i])?)?;
            }
        }
        for (key, id) in src
            .get("resources")
            .and_then(|c| c.as_object())
            .into_iter()
            .flatten()
        {
            let id = id.as_str().unwrap_or_default();
            let node = obj_at(&mut e, &["Contents", dict, "Contents", key])?;
            set_str(node, "Value", id.strip_prefix("eaid://").unwrap_or(id))?;
        }
    }
    Ok(e)
}

fn patch_minimap(res: &mut Resource, maps: &[Map]) -> Result<()> {
    edit_pkg(res, |pkg| {
        let list = obj_at(
            &mut pkg.root,
            &["hud", "Contents", "minimap", "Contents", "tracks", "Contents"],
        )?
        .as_arr_mut()
        .ok_or_else(|| Error::Map("minimap tracks is not a list".into()))?;
        let template = list
            .items
            .iter()
            .find(|n| {
                n.as_obj()
                    .and_then(|o| o.get("Contents"))
                    .and_then(Node::as_obj)
                    .and_then(|c| c.get("dimensions"))
                    .is_some()
            })
            .or_else(|| list.items.first())
            .cloned()
            .ok_or_else(|| Error::Map("minimap has no tracks".into()))?;
        for m in maps {
            let Some(mm) = &m.minimap else { continue };
            let index = list
                .items
                .iter()
                .position(|n| name_of(n) == Some(m.code()))
                .unwrap_or(list.items.len());
            let entry = minimap_entry(&template, m.code(), index, &m.ui.asset_group, mm)?;
            upsert(&mut list.items, m.code(), entry);
        }
        Ok(())
    })
}

fn rewrite(original: &Path, dst: &Path, mut replace: HashMap<String, Resource>) -> Result<()> {
    let file = ErpFile::open(original)?;
    let entries = file
        .erp
        .resources
        .iter()
        .map(|r| match replace.remove(&r.name) {
            Some(new) => Entry::Owned(new),
            None => Entry::Copy(r),
        })
        .collect();
    if let Some(name) = replace.keys().next() {
        return Err(Error::Map(format!("{} has no {name}", original.display())));
    }
    let dir = dst.parent().expect("has parent");
    std::fs::create_dir_all(dir).map_err(io_err(dir))?;
    file.write(entries, dst)?;
    Ok(())
}

fn load_one(path: &Path, name: &str) -> Result<Resource> {
    Erp::read_matching(path, |n| n == name)?
        .pop()
        .ok_or_else(|| Error::Map(format!("{} has no {name}", path.display())))
}

pub fn stamp(maps: &[MapUi], original: &dyn Fn(&str) -> PathBuf) -> Result<String> {
    let mut h = <sha2::Sha256 as sha2::Digest>::new();
    for m in maps {
        sha2::Digest::update(&mut h, serde_json::to_vec(m.pkg).expect("serialisable"));
        sha2::Digest::update(&mut h, &m.asset_group);
        for rel in [
            m.pkg.ui.as_ref().and_then(|u| u.images_file.clone()),
            m.pkg.ui.as_ref().and_then(|u| u.minimap_file.clone()),
        ]
        .into_iter()
        .flatten()
        {
            let p = m.dir.join(rel);
            sha2::Digest::update(&mut h, std::fs::read(&p).map_err(io_err(&p))?);
        }
        let erp = m.dir.join(&m.pkg.erp);
        let meta = std::fs::metadata(&erp).map_err(io_err(&erp))?;
        sha2::Digest::update(&mut h, format!("{}|{:?}", meta.len(), meta.modified().ok()));
    }
    for rel in UI_ERPS
        .iter()
        .map(|r| ui_file(r))
        .chain([slot_file(PICTURE_SLOT)])
    {
        let p = original(&rel);
        let meta = std::fs::metadata(&p).map_err(io_err(&p))?;
        sha2::Digest::update(&mut h, format!("{rel}|{}|{:?}", meta.len(), meta.modified().ok()));
    }
    Ok(crate::baseline::hex(&sha2::Digest::finalize(h))[..16].to_owned())
}

pub type Lists = BTreeMap<String, BTreeSet<String>>;

pub type Kept = BTreeMap<(String, String), BTreeSet<String>>;

fn kept(maps: &[MapUi], lists: &Lists, array_erps: &BTreeMap<String, Vec<&str>>) -> Kept {
    let mut kept = Kept::new();
    let pictured: BTreeSet<String> = lists
        .iter()
        .filter(|(_, s)| !s.is_empty())
        .map(|(c, _)| c.clone())
        .collect();
    if !pictured.is_empty() {
        kept.insert((ui_file("databases.erp"), OVERRIDES.into()), pictured);
    }
    let mapped: BTreeSet<String> = maps
        .iter()
        .filter(|m| m.pkg.ui.as_ref().is_some_and(|u| u.minimap_file.is_some()))
        .map(|m| m.pkg.track_code.clone())
        .collect();
    if !mapped.is_empty() {
        for rel in ["databases.erp", "flow_race.erp"] {
            kept.insert((ui_file(rel), MINIMAP.into()), mapped.clone());
        }
    }
    for (rel, ls) in array_erps {
        for list in ls {
            let codes: BTreeSet<String> = lists
                .iter()
                .filter(|(_, s)| s.contains(*list))
                .map(|(c, _)| c.clone())
                .collect();
            for name in [texarray(list), format!("{}.image", texarray(list))] {
                kept.insert((rel.clone(), name), codes.clone());
            }
        }
    }
    kept
}

pub fn plan_lists(maps: &[MapUi], original: &dyn Fn(&str) -> PathBuf) -> Result<(Lists, Vec<String>)> {
    let mut lists = BTreeMap::new();
    let mut notes = Vec::new();
    let mut stock: HashMap<&str, Surface> = HashMap::new();
    for (list, _) in ARRAY_SLOTS {
        let name = format!("{}.image", texarray(list));
        for rel in UI_ERPS {
            let p = original(&ui_file(rel));
            if let Some(r) = Erp::read_matching(&p, |n| n == name)?.pop() {
                stock.insert(list, surface(&r.fragments[0].data()?)?);
                break;
            }
        }
    }
    for ui in maps {
        let m = Map::load(ui)?;
        let mut set = BTreeSet::new();
        for (list, key) in ARRAY_SLOTS {
            let Some((_, surf)) = m.image(key)? else { continue };
            let s = surface(&surf.fragments[0].data()?)?;
            let Some(t) = stock.get(list) else {
                notes.push(format!("{}: the game has no {list} texture array", m.code()));
                continue;
            };
            let fits = (s.format, s.width, s.height, s.mips, s.extra)
                == (t.format, t.width, t.height, t.mips, t.extra);
            if fits {
                set.insert((*list).to_owned());
            } else {
                notes.push(format!(
                    "{}: {key} is {}x{} format {:#x}, {list} wants {}x{} format {:#x}; left out",
                    m.code(),
                    s.width,
                    s.height,
                    s.format,
                    t.width,
                    t.height,
                    t.format
                ));
            }
        }
        lists.insert(m.code().to_owned(), set);
    }
    Ok((lists, notes))
}

pub fn build_ui(maps: &[MapUi], original: &dyn Fn(&str) -> PathBuf, cache: &Path) -> Result<UiBuild> {
    let out = cache.join(stamp(maps, original)?);
    let (lists, notes) = plan_lists(maps, original)?;
    let mut build = UiBuild {
        dir: out.clone(),
        lists: lists.clone(),
        notes,
        ..UiBuild::default()
    };
    let mut targets: Vec<String> = [
        ui_file("databases.erp"),
        ui_file("flow_race.erp"),
        ui_file("persistent.erp"),
        slot_file(PICTURE_SLOT),
    ]
    .into();
    let mut array_erps: BTreeMap<String, Vec<&str>> = BTreeMap::new();
    for (list, _) in ARRAY_SLOTS {
        if !lists.values().any(|s| s.contains(*list)) {
            continue;
        }
        let name = format!("{}.image", texarray(list));
        for rel in UI_ERPS {
            let p = original(&ui_file(rel));
            if ErpFile::open(&p)?.erp.resources.iter().any(|r| r.name == name) {
                array_erps.entry(ui_file(rel)).or_default().push(list);
            }
        }
    }
    build.kept = kept(maps, &lists, &array_erps);
    targets.extend(array_erps.keys().cloned());
    targets.sort();
    targets.dedup();
    let done = out.join(".done");
    if !done.is_file() {
        let loaded: Vec<Map> = maps.iter().map(Map::load).collect::<Result<_>>()?;
        let mut edits: BTreeMap<String, HashMap<String, Resource>> = BTreeMap::new();

        let mut overrides = load_one(&original(&ui_file("databases.erp")), OVERRIDES)?;
        let mut order: BTreeMap<&str, Vec<(usize, &Map)>> = BTreeMap::new();
        let mut listed: BTreeMap<&str, (usize, Option<usize>)> = BTreeMap::new();
        edit_pkg(&mut overrides, |pkg| {
            for m in &loaded {
                for (list, key) in ARRAY_SLOTS {
                    if !lists[m.code()].contains(*list) {
                        continue;
                    }
                    let (a, b) = list.split_once('.').expect("dotted");
                    let items = &mut obj_at(
                        &mut pkg.root,
                        &["overrides", "Contents", a, "Contents", b, "Contents"],
                    )?
                    .as_arr_mut()
                    .ok_or_else(|| Error::Map(format!("overrides {list} is not a list")))?
                    .items;
                    listed.entry(list).or_insert_with(|| {
                        (
                            items.len(),
                            items.iter().position(|n| name_of(n) == Some("blank")),
                        )
                    });
                    let mut entry = items
                        .first()
                        .cloned()
                        .ok_or_else(|| Error::Map(format!("overrides {list} is empty")))?;
                    set_str(&mut entry, "Name", m.code())?;
                    let eaid = &m.images[*key];
                    set_str(&mut entry, "Value", eaid.strip_prefix("eaid://").unwrap_or(eaid))?;
                    let index = upsert(items, m.code(), entry);
                    order.entry(list).or_default().push((index, m));
                }
            }
            Ok(())
        })?;
        edits
            .entry(ui_file("databases.erp"))
            .or_default()
            .insert(OVERRIDES.into(), overrides);
        for rel in ["databases.erp", "flow_race.erp"] {
            let mut mm = load_one(&original(&ui_file(rel)), MINIMAP)?;
            patch_minimap(&mut mm, &loaded)?;
            edits.entry(ui_file(rel)).or_default().insert(MINIMAP.into(), mm);
        }

        let mut arrays: HashMap<&str, (Resource, Resource)> = HashMap::new();
        let mut pads = Vec::new();
        for (rel, ls) in &array_erps {
            for list in ls {
                if !arrays.contains_key(list) {
                    let p = original(rel);
                    let mut surf = load_one(&p, &format!("{}.image", texarray(list)))?;
                    let mut view = load_one(&p, &texarray(list))?;
                    let mut head = surf.fragments[0].data()?;
                    let mut data = surf.fragments[1].data()?;
                    let mut slices = surface(&head)?.slices;
                    let size = data.len() / slices.max(1) as usize;
                    let (entries, blank) = listed[*list];
                    let had = slices as usize;
                    for (index, m) in &order[*list] {
                        let key = ARRAY_SLOTS.iter().find(|(l, _)| l == list).expect("known").1;
                        let (_, img) = m.image(key)?.expect("planned");
                        let pixels = img.fragments[1].data()?;
                        if pixels.len() != size {
                            return Err(Error::Map(format!(
                                "{}: {key} is {} bytes, a {list} slice is {size}",
                                m.code(),
                                pixels.len()
                            )));
                        }
                        put_slice(&mut data, &mut slices, *index, &pixels, blank);
                    }
                    let blanks = (had..slices as usize)
                        .filter(|i| !order[*list].iter().any(|(at, _)| at == i))
                        .count();
                    if blanks > 0 {
                        pads.push(format!(
                            "{list}: the game's list has {entries} entries but only {had} pictures; \
                             {blanks} blank picture(s) fill the gap. Repair the game files if a menu shows \
                             the wrong picture"
                        ));
                    }
                    put_u32(&mut head, 28, slices);
                    set_fragment(&mut surf, 0, &head, 19)?;
                    set_fragment(&mut surf, 1, &data, ARRAY_LEVEL)?;
                    let mut v = view.fragments[0].data()?;
                    put_u32(&mut v, 0x14, slices);
                    set_fragment(&mut view, 0, &v, 19)?;
                    arrays.insert(list, (surf, view));
                }
                let (surf, view) = &arrays[list];
                let e = edits.entry(rel.clone()).or_default();
                e.insert(surf.name.clone(), surf.clone());
                e.insert(view.name.clone(), view.clone());
            }
        }

        let pers = original(&ui_file("persistent.erp"));
        let mut uidta = load_one(&pers, UIDTA)?;
        let mut pictures = Vec::new();
        edit_pkg(&mut uidta, |pkg| {
            let groups = pkg
                .root
                .as_obj_mut()
                .and_then(|o| o.get_mut("TextureArrays"))
                .and_then(Node::as_arr_mut)
                .ok_or_else(|| Error::Map("the dynamic texture list is missing".into()))?;
            for m in &loaded {
                let mut copied = BTreeSet::new();
                for (group, keys) in DYNAMIC {
                    let Some(g) = groups.items.iter_mut().find(|g| name_of(g) == Some(group)) else {
                        continue;
                    };
                    let entries = &mut g
                        .as_obj_mut()
                        .and_then(|o| o.get_mut("TextureArray"))
                        .and_then(Node::as_arr_mut)
                        .ok_or_else(|| Error::Map(format!("{group} has no textures")))?
                        .items;
                    for (n, key) in keys.iter().enumerate() {
                        if m.image(key)?.is_none() {
                            continue;
                        }
                        let name = if n == 0 {
                            m.code().to_owned()
                        } else {
                            format!("{}_{}", m.code(), n + 1)
                        };
                        let rid = shared_picture(m.code(), key);
                        let mut entry = entries
                            .first()
                            .cloned()
                            .unwrap_or_else(|| Node::Obj(Obj::new(Vec::new())));
                        set_str(&mut entry, "Name", &name)?;
                        set_str(&mut entry, "AssetGroupPath", &slot_asset_group(PICTURE_SLOT))?;
                        set_str(&mut entry, "ResourceId", &rid)?;
                        upsert(entries, &name, entry);
                        if copied.insert(*key) {
                            let (view, surf) = m.image(key)?.expect("checked");
                            let mut s = surf.clone();
                            s.name = format!("{rid}.image");
                            let mut v = view.clone();
                            let data = v.fragments[0].data()?;
                            let mut head = data
                                .get(..24)
                                .ok_or_else(|| Error::Map("short view".into()))?
                                .to_vec();
                            head.extend_from_slice(s.name.as_bytes());
                            head.push(0);
                            set_fragment(&mut v, 0, &head, 19)?;
                            v.name = rid.clone();
                            pictures.push(s);
                            pictures.push(v);
                        }
                    }
                }
            }
            Ok(())
        })?;
        edits
            .entry(ui_file("persistent.erp"))
            .or_default()
            .insert(UIDTA.into(), uidta);

        for (rel, replace) in edits {
            rewrite(&original(&rel), &out.join(&rel), replace)?;
        }
        let slot_in = original(&slot_file(PICTURE_SLOT));
        let file = ErpFile::open(&slot_in)?;
        let names: BTreeSet<&str> = pictures.iter().map(|r| r.name.as_str()).collect();
        let mut entries: Vec<Entry> = file
            .erp
            .resources
            .iter()
            .filter(|r| !names.contains(r.name.as_str()))
            .map(Entry::Copy)
            .collect();
        entries.extend(pictures.into_iter().map(Entry::Owned));
        let dst = out.join(slot_file(PICTURE_SLOT));
        let dir = dst.parent().expect("has parent");
        std::fs::create_dir_all(dir).map_err(io_err(dir))?;
        file.write(entries, &dst)?;
        std::fs::write(&done, pads.join("\n")).map_err(io_err(&done))?;
    }
    let pads = std::fs::read_to_string(&done).map_err(io_err(&done))?;
    build.notes.extend(pads.lines().map(str::to_owned));
    for rel in targets {
        build.replaced.insert(rel.clone(), out.join(&rel));
    }
    let _ = (ASSET_ROOT, SLOT_DIR);
    Ok(build)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn array(slices: u8, size: usize) -> Vec<u8> {
        (0..slices).flat_map(|i| vec![i; size]).collect()
    }

    fn slice(data: &[u8], index: usize, size: usize) -> &[u8] {
        &data[index * size..(index + 1) * size]
    }

    #[test]
    fn stock_arrays_overwrite_and_append_in_place() {
        let mut data = array(32, 4);
        let mut slices = 32;
        put_slice(&mut data, &mut slices, 15, &[0xaa; 4], Some(2));
        put_slice(&mut data, &mut slices, 32, &[0xbb; 4], Some(2));
        put_slice(&mut data, &mut slices, 33, &[0xcc; 4], Some(2));
        assert_eq!(slices, 34);
        assert_eq!(data.len(), 34 * 4);
        assert_eq!(slice(&data, 14, 4), [14; 4]);
        assert_eq!(slice(&data, 15, 4), [0xaa; 4]);
        assert_eq!(slice(&data, 32, 4), [0xbb; 4]);
        assert_eq!(slice(&data, 33, 4), [0xcc; 4]);
    }

    #[test]
    fn a_list_longer_than_its_array_pads_with_the_blank_picture() {
        let mut data = array(34, 4);
        let mut slices = 34;
        put_slice(&mut data, &mut slices, 15, &[0xdd; 4], Some(2));
        put_slice(&mut data, &mut slices, 35, &[0xaa; 4], Some(2));
        put_slice(&mut data, &mut slices, 36, &[0xbb; 4], Some(2));
        assert_eq!(slices, 37);
        assert_eq!(data.len(), 37 * 4);
        assert_eq!(slice(&data, 15, 4), [0xdd; 4]);
        assert_eq!(slice(&data, 33, 4), [33; 4]);
        assert_eq!(slice(&data, 34, 4), [2; 4]);
        assert_eq!(slice(&data, 35, 4), [0xaa; 4]);
        assert_eq!(slice(&data, 36, 4), [0xbb; 4]);
        put_slice(&mut data, &mut slices, 34, &[0xcc; 4], Some(2));
        assert_eq!(slices, 37);
        assert_eq!(slice(&data, 34, 4), [0xcc; 4]);
    }

    #[test]
    fn padding_without_a_blank_entry_is_zeroed() {
        let mut data = array(4, 2);
        let mut slices = 4;
        put_slice(&mut data, &mut slices, 7, &[9; 2], None);
        assert_eq!(slices, 8);
        assert_eq!(&data[8..], [0, 0, 0, 0, 0, 0, 9, 9]);
        let mut data = array(4, 2);
        let mut slices = 4;
        put_slice(&mut data, &mut slices, 6, &[9; 2], Some(10));
        assert_eq!(&data[8..], [0, 0, 0, 0, 9, 9]);
    }
}
