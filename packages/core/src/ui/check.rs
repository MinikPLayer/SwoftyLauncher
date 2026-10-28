use std::collections::{BTreeMap, BTreeSet};

use swofty_formats::hash::djb2;
use swofty_formats::pkg::{Node, Pkg};

use super::catalog::{Catalog, Engine, EngineRule, NodeType, Scenes, attr_shape, element_type, shape, tag};
use super::idf;
use super::node::*;

const LIST_CLASSES: &[&str] = &["UITG", "UITB", "UILT", "UIL2", "UIEL"];
const SCREEN_CMDS: &[&str] = &["OpenScreen", "CloseScreen", "TransitionScreen"];
/// The code of a finding where a name no longer matches the djb2 hash stored
/// beside it, as after a rename that kept the hash.
pub const STALE_HASH: &str = "stale-hash";

fn str_of<'a>(o: &'a swofty_formats::pkg::Obj, key: &str) -> Option<&'a str> {
    o.get(key)?.as_val()?.as_str()
}

fn walk(n: &Node, path: &str, out: &mut Vec<String>) {
    let name = n.name();
    let here = if path.is_empty() {
        name.to_owned()
    } else {
        format!("{path}.{name}")
    };
    let kind = n.type_code();
    let Ok(props) = n.props() else { return };
    if LIST_CLASSES.contains(&kind.as_str()) {
        let db = props
            .get("data")
            .and_then(Node::as_obj)
            .and_then(|d| str_of(d, "dbentry"));
        match db {
            Some(d) if !d.is_empty() => {}
            _ => out.push(format!(
                "{here} [{kind}]: list controls need data bound to an array (data.dbentry); the game fails to open the screen"
            )),
        }
    }
    if props.get("allowVMBinding").is_some() && !path.is_empty() {
        out.push(format!("{here} [{kind}]: only a screen may set allowVMBinding"));
    }
    if let Ok(lay) = n.slot(LAYOUT) {
        for grp in lay {
            let entries: Vec<&Node> = match grp {
                Node::Arr(a) => a.items.iter().collect(),
                other => vec![other],
            };
            for b in entries.into_iter().filter_map(Node::as_obj) {
                let by_node = b.get("nodeId").is_some();
                let by_comp = b.get("componentId").is_some() && b.get("isComponentId").is_some();
                if !(by_node || by_comp) {
                    out.push(format!(
                        "{here} [{kind}]: a layout entry has neither a nodeId nor a componentId"
                    ));
                }
            }
        }
    }
    if let Ok(kids) = n.slot(CHILDREN) {
        for c in kids.iter().filter(|c| c.is_node()) {
            walk(c, &here, out);
        }
    }
}

fn binds_view_model(scr: &Node) -> bool {
    scr.props()
        .ok()
        .and_then(|p| p.get("allowVMBinding"))
        .and_then(Node::as_obj)
        .and_then(|o| o.get("value"))
        .and_then(Node::as_val)
        .is_some_and(|v| matches!(&v.value, swofty_formats::pkg::Value::Raw(b) if b.first() == Some(&1)))
}

pub fn screens(files: &[(String, Vec<u8>)], view_models: &[String]) -> crate::Result<Vec<String>> {
    let mut out = Vec::new();
    for (name, data) in files {
        let p = Pkg::parse(data)?;
        for scr in p.root.o()?.at("Screens")?.a()? {
            walk(scr, "", &mut out);
            let vm = view_models.contains(name);
            if binds_view_model(scr) && !vm {
                out.push(format!(
                    "{name}: sets allowVMBinding without a view model; the page would stay blank and Back would not work"
                ));
            }
            if vm && !binds_view_model(scr) {
                out.push(format!(
                    "{name}: has a view model but does not set allowVMBinding"
                ));
            }
        }
    }
    Ok(out)
}

struct Flow {
    erp: Option<String>,
    screens: BTreeSet<String>,
}

fn flows(data: &[u8]) -> crate::Result<BTreeMap<String, Flow>> {
    let p = Pkg::parse(data)?;
    let mut out = BTreeMap::new();
    for f in p.root.o()?.at("Flows")?.a()? {
        let erp = f
            .props()?
            .get("asset_group")
            .and_then(Node::as_obj)
            .and_then(|o| str_of(o, "value"))
            .map(|v| {
                let base = v.rsplit('/').next().unwrap_or(v);
                base.rsplit_once('.').map_or(base, |(b, _)| b).to_owned()
            });
        let mut screens = BTreeSet::new();
        for scene in f.slot(CHILDREN)? {
            for e in scene.slot(CHILDREN).map(|v| v.as_slice()).unwrap_or_default() {
                if let Some(s) = e
                    .props()
                    .ok()
                    .and_then(|p| p.get("screen"))
                    .and_then(Node::as_obj)
                    .and_then(|o| str_of(o, "value"))
                {
                    screens.insert(s.to_owned());
                }
            }
        }
        out.insert(f.name().to_owned(), Flow { erp, screens });
    }
    Ok(out)
}

fn attrs(n: &Node) -> BTreeMap<String, String> {
    let mut out = BTreeMap::new();
    for a in n
        .as_obj()
        .and_then(|o| o.get("Attributes"))
        .and_then(Node::as_arr)
        .map(|a| a.items.as_slice())
        .unwrap_or_default()
    {
        if let Some(o) = a.as_obj()
            && let (Some(k), Some(v)) = (str_of(o, "Id"), str_of(o, "Value"))
        {
            out.insert(k.to_owned(), v.to_owned());
        }
    }
    out
}

pub fn routes(
    routes: &[u8],
    flows_ui: &[u8],
    changed: &[&str],
    registered: &BTreeMap<String, String>,
) -> crate::Result<Vec<String>> {
    route_files(&[routes], flows_ui, changed, registered, &BTreeSet::new())
}

/// The flow whose asset group is the archive `erp` (`flow_frontend`), as flows.ui lists it.
pub fn flow_loading(flows_ui: &[u8], erp: &str) -> crate::Result<Option<String>> {
    Ok(flows(flows_ui)?
        .into_iter()
        .find(|(_, f)| f.erp.as_deref() == Some(erp))
        .map(|(name, _)| name))
}

/// [`routes`] over several route files, which the game reads as one set of route groups. `stock` names game
/// screens that routes may open without a flow entry, as the game's own routes open them.
pub fn route_files(
    routes: &[&[u8]],
    flows_ui: &[u8],
    changed: &[&str],
    registered: &BTreeMap<String, String>,
    stock: &BTreeSet<String>,
) -> crate::Result<Vec<String>> {
    let fl = flows(flows_ui)?;
    let parsed = routes
        .iter()
        .map(|r| Pkg::parse(r))
        .collect::<std::result::Result<Vec<_>, _>>()?;
    let mut groups: BTreeMap<String, BTreeMap<String, &Node>> = BTreeMap::new();
    for p in &parsed {
        for g in p.root.o()?.at("RouteGroupings")?.a()? {
            let go = g.o()?;
            let gid = str_of(go, "Id").unwrap_or_default().to_owned();
            for r in go.at("Routes")?.a()? {
                let rid = r.o().ok().and_then(|o| str_of(o, "Id")).unwrap_or_default();
                groups.entry(gid.clone()).or_default().insert(rid.to_owned(), r);
            }
        }
    }
    let known = |t: &str| {
        registered.contains_key(t) || stock.contains(t) || fl.values().any(|f| f.screens.contains(t))
    };
    let mut out = Vec::new();
    for (name, flow) in registered {
        let ok = fl
            .values()
            .any(|f| f.erp.as_deref() == Some(flow) && f.screens.contains(name));
        if !ok {
            out.push(format!(
                "screen {name}: no flow that loads {flow} lists it, so routes to it cannot open it"
            ));
        }
    }
    for (gid, rs) in &groups {
        for (rid, r) in rs {
            let id = format!("{gid}.{rid}");
            if !changed.iter().any(|c| *c == gid || *c == id) {
                continue;
            }
            let mut targets = BTreeSet::new();
            if let Some(d) = attrs(r).get("Destination").filter(|d| !d.is_empty()) {
                targets.insert(d.clone());
            }
            for c in r.o()?.at("Commands")?.a()? {
                let kind = c.o().ok().and_then(|o| str_of(o, "Type")).unwrap_or_default();
                let at = attrs(c);
                if kind == "Migrate" {
                    let f = at.get("Flow").map(String::as_str).unwrap_or_default();
                    if !fl.contains_key(f) {
                        out.push(format!("{id}: Migrate to unknown flow {f:?}"));
                    }
                }
                if kind == "TriggerSubRoute" || SCREEN_CMDS.contains(&kind) {
                    for k in ["Target", "To"] {
                        if let Some(t) = at.get(k).filter(|t| !t.is_empty()) {
                            targets.insert(t.clone());
                        }
                    }
                }
                if kind == "TriggerSubRoute" {
                    let t = at.get("Target").map(String::as_str).unwrap_or_default();
                    let sub = at.get("SubRoute").map(String::as_str).unwrap_or_default();
                    if !groups.get(t).is_some_and(|g| g.contains_key(sub)) {
                        out.push(format!("{id}: sub-route {t}.{sub} does not exist"));
                    }
                }
            }
            for t in targets {
                if !known(&t) {
                    out.push(format!(
                        "{id}: target screen {t:?} is not defined in any flow (fatal in game)"
                    ));
                }
            }
        }
    }
    Ok(out)
}

type Signal = (String, String, String);

fn index<'a>(n: &'a Node, path: &str, out: &mut BTreeMap<String, &'a Node>) {
    let here = if path.is_empty() {
        n.name().to_owned()
    } else {
        format!("{path}.{}", n.name())
    };
    for c in n.slot(CHILDREN).into_iter().flatten().filter(|c| c.is_node()) {
        index(c, &here, out);
    }
    out.insert(here, n);
}

fn tree(p: &Pkg) -> crate::Result<BTreeMap<String, &Node>> {
    let mut out = BTreeMap::new();
    for scr in p.root.o()?.at("Screens")?.a()? {
        index(scr, "", &mut out);
    }
    Ok(out)
}

fn signals(n: &Node) -> Vec<Signal> {
    let mut out = Vec::new();
    for e in [COMMANDS, EVENTS]
        .into_iter()
        .flat_map(|i| n.slot(i).into_iter().flatten())
    {
        let sigs = e.as_obj().and_then(|o| o.get("signals")).and_then(Node::as_arr);
        for s in sigs.into_iter().flat_map(|a| &a.items).filter_map(Node::as_obj) {
            out.push((
                str_of(s, "to_object_name").unwrap_or_default().to_owned(),
                str_of(s, "to_pin_name").unwrap_or_default().to_owned(),
                s.get("attributes").map(canonical).unwrap_or_default(),
            ));
        }
    }
    out
}

fn kids(n: &Node) -> Vec<&str> {
    n.slot(CHILDREN)
        .map(|k| k.iter().filter(|c| c.is_node()).map(|c| c.name()).collect())
        .unwrap_or_default()
}

pub fn stock_kept(stock: &[u8], patched: &[u8], prefix: &str) -> crate::Result<Vec<String>> {
    let (a, b) = (Pkg::parse(stock)?, Pkg::parse(patched)?);
    let (old, new) = (tree(&a)?, tree(&b)?);
    let sent: BTreeSet<Signal> = old.values().flat_map(|n| signals(n)).collect();
    let mut out = Vec::new();
    for (path, n) in &old {
        let Some(m) = new.get(path) else {
            out.push(format!("{path}: removed"));
            continue;
        };
        let slot = |x: &Node, i: usize| x.a().ok().and_then(|a| a.get(i)).map(canonical);
        if (0..8)
            .filter(|i| ![CHILDREN, COMMANDS, EVENTS].contains(i))
            .any(|i| slot(n, i) != slot(m, i))
        {
            out.push(format!("{path}: its properties, layout or behaviours changed"));
        }
        let (k0, k1) = (kids(n), kids(m));
        if !k1.starts_with(&k0) || k1[k0.len()..].iter().any(|k| !k.starts_with(prefix)) {
            out.push(format!("{path}: its stock children changed"));
        }
    }
    for (path, m) in &new {
        for sig in signals(m) {
            if old.contains_key(&sig.0) && !sent.contains(&sig) {
                out.push(format!(
                    "{path}: sends {}.{} {}, which no stock node sends",
                    sig.0, sig.1, sig.2
                ));
            }
        }
    }
    Ok(out)
}

fn integrity_walk<'a>(n: &'a Node, parent: Option<&str>, out: &mut Vec<(String, &'a Node)>) {
    let path = match parent {
        Some(p) => format!("{p}.{}", n.name()),
        None => n.name().to_owned(),
    };
    out.push((path.clone(), n));
    for c in n.slot(CHILDREN).into_iter().flatten().filter(|c| c.is_node()) {
        integrity_walk(c, Some(&path), out);
    }
}

fn u32_at(o: &swofty_formats::pkg::Obj, key: &str) -> Option<u32> {
    o.get(key)?.as_val()?.as_u32()
}

pub fn integrity(data: &[u8]) -> crate::Result<(usize, usize, Vec<String>)> {
    let p = Pkg::parse(data)?;
    let root = p.root.o()?;
    let mut nodes = Vec::new();
    for key in ["Screens", "Templates"] {
        for top in root
            .get(key)
            .and_then(Node::as_arr)
            .map(|a| a.items.as_slice())
            .unwrap_or_default()
        {
            integrity_walk(top, None, &mut nodes);
        }
    }
    let paths: BTreeSet<&str> = nodes.iter().map(|(p, _)| p.as_str()).collect();
    let (mut bad, mut signals) = (Vec::new(), 0);
    for (path, n) in &nodes {
        let props = n.props()?;
        if props.get("id").is_some() && u32_at(props, "id") != Some(djb2(path)) {
            bad.push(format!("{path}: id"));
        }
        if props.get("nameId").is_some() && u32_at(props, "nameId") != Some(djb2(n.name())) {
            bad.push(format!("{path}: nameId"));
        }
        for grp in n.slot(LAYOUT).into_iter().flatten().filter_map(Node::as_arr) {
            for b in grp.items.iter().filter_map(Node::as_obj) {
                if let (Some(debug), Some(id)) = (str_of(b, "debugName"), u32_at(b, "nodeId"))
                    && id != djb2(debug)
                {
                    bad.push(format!("{path}: layout {debug}"));
                }
            }
        }
        for (slot, key, hash) in [
            (EVENTS, "eventName", "event"),
            (COMMANDS, "commandName", "command"),
        ] {
            for e in n.slot(slot).into_iter().flatten().filter_map(Node::as_obj) {
                let Some(name) = str_of(e, key) else { continue };
                if e.get(hash).is_some() && u32_at(e, hash) != Some(djb2(name)) {
                    bad.push(format!("{path}: {key} {name}"));
                }
                for s in e
                    .get("signals")
                    .and_then(Node::as_arr)
                    .map(|a| a.items.as_slice())
                    .unwrap_or_default()
                    .iter()
                    .filter_map(Node::as_obj)
                {
                    signals += 1;
                    let to = str_of(s, "to_object_name").unwrap_or_default();
                    let pin = str_of(s, "to_pin_name").unwrap_or_default();
                    if u32_at(s, "to_object") != Some(djb2(to)) || u32_at(s, "to_pin") != Some(djb2(pin)) {
                        bad.push(format!("{path}: hash {to}.{pin}"));
                    }
                    if !paths.contains(to) {
                        bad.push(format!("{path}: on {name} -> missing {to}.{pin}"));
                    }
                }
            }
        }
    }
    Ok((nodes.len(), signals, bad))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Level {
    Hard,
    Warn,
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct Finding {
    pub level: Level,
    pub code: String,
    pub node_type: String,
    pub name: String,
    pub at: String,
    pub message: String,
    pub source: String,
}

impl Finding {
    pub fn warn(code: &str, node_type: &str, name: &str, at: &str, message: String) -> Self {
        Self {
            level: Level::Warn,
            code: code.to_owned(),
            node_type: node_type.to_owned(),
            name: name.to_owned(),
            at: at.to_owned(),
            message,
            source: "catalogue".into(),
        }
    }

    fn stock_diff(code: &str, node_type: &str, name: &str, at: &str, message: String) -> Self {
        Self {
            source: "stock diff".into(),
            ..Self::warn(code, node_type, name, at, message)
        }
    }

    fn engine(rule: &EngineRule, at: &str, message: String) -> Self {
        Self {
            level: Level::Hard,
            source: rule.cite(),
            ..Self::warn(&rule.id, "", "", at, message)
        }
    }

    fn hard(code: &str, at: &str, message: String, source: &str) -> Self {
        Self {
            level: Level::Hard,
            source: source.to_owned(),
            ..Self::warn(code, "", "", at, message)
        }
    }
}

impl std::fmt::Display for Finding {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}: {} [{}]", self.at, self.message, self.source)
    }
}

#[derive(Debug, Default)]
pub struct Report {
    pub hard: Vec<Finding>,
    pub warnings: Vec<Finding>,
}

impl Report {
    pub fn add(&mut self, f: Finding) {
        match f.level {
            Level::Hard => self.hard.push(f),
            Level::Warn => self.warnings.push(f),
        }
    }

    pub fn extend(&mut self, other: Report) {
        self.hard.extend(other.hard);
        self.warnings.extend(other.warnings);
    }
}

fn integrity_findings(data: &[u8], frag: &str) -> crate::Result<Vec<Finding>> {
    let p = Pkg::parse(data)?;
    let root = p.root.o()?;
    let tops: BTreeSet<&str> = ["Screens", "Templates"]
        .iter()
        .filter_map(|k| root.get(k).and_then(Node::as_arr))
        .flat_map(|a| a.items.iter().filter(|t| t.is_node()).map(|t| t.name()))
        .collect();
    let (_, _, bad) = integrity(data)?;
    Ok(bad
        .into_iter()
        .filter(|b| match b.split_once(" -> missing ") {
            Some((_, to)) => tops.contains(to.split('.').next().unwrap_or_default()),
            None => true,
        })
        .map(|b| {
            let (at, msg) = b.split_once(": ").unwrap_or((&b, ""));
            let code = match msg.contains(" -> missing ") {
                true => "integrity",
                false => STALE_HASH,
            };
            Finding::hard(
                code,
                &format!("{frag}: {at}"),
                msg.to_owned(),
                "format: the loader resolves nodes, events and signals by these djb2 hashes",
            )
        })
        .collect())
}

fn vm_keys(n: &Node, out: &mut BTreeSet<String>) {
    let Ok(props) = n.props() else { return };
    for (k, v) in &props.items {
        let Some(o) = v.as_obj() else { continue };
        let bound = o.get("dbentry").and_then(Node::as_val).and_then(|v| v.as_str());
        let named = (k.as_str() == Some("dbEntry"))
            .then(|| o.get("value").and_then(Node::as_val).and_then(|v| v.as_str()))
            .flatten();
        for key in bound.into_iter().chain(named) {
            if !key.is_empty()
                && !key.starts_with('#')
                && key.chars().next().is_some_and(|c| c.is_ascii_alphabetic())
            {
                out.insert(key.split('.').next().unwrap_or(key).to_owned());
            }
        }
    }
    for c in n.slot(CHILDREN).into_iter().flatten().filter(|c| c.is_node()) {
        vm_keys(c, out);
    }
}

fn roots(data: &[u8]) -> crate::Result<BTreeMap<String, BTreeSet<String>>> {
    let p = Pkg::parse(data)?;
    let root = p.root.o()?;
    let mut out = BTreeMap::new();
    for key in ["Screens", "Templates"] {
        for t in root
            .get(key)
            .and_then(Node::as_arr)
            .map(|a| a.items.as_slice())
            .unwrap_or_default()
            .iter()
            .filter(|t| t.is_node())
        {
            let mut keys = BTreeSet::new();
            vm_keys(t, &mut keys);
            out.insert(t.name().to_owned(), keys);
        }
    }
    Ok(out)
}

fn prop_index(data: &[u8]) -> crate::Result<BTreeMap<String, (String, BTreeSet<String>)>> {
    let p = Pkg::parse(data)?;
    let mut nodes = Vec::new();
    for key in ["Screens", "Templates"] {
        for top in p
            .root
            .o()?
            .get(key)
            .and_then(Node::as_arr)
            .map(|a| a.items.as_slice())
            .unwrap_or_default()
        {
            if top.is_node() {
                integrity_walk(top, None, &mut nodes);
            }
        }
    }
    Ok(nodes
        .into_iter()
        .map(|(path, n)| {
            let keys = n
                .props()
                .map(|p| p.keys().map(str::to_owned).collect())
                .unwrap_or_default();
            (path, (n.type_code(), keys))
        })
        .collect())
}

pub fn dropped_props(
    stock: &[u8],
    edited: &[u8],
    frag: &str,
    engine: &Engine,
) -> crate::Result<Vec<Finding>> {
    let (old, new) = (prop_index(stock)?, prop_index(edited)?);
    let mut out = Vec::new();
    for (path, (class, keys)) in &new {
        let Some((_, before)) = old.get(path) else {
            continue;
        };
        for k in before.difference(keys) {
            let read_by = engine
                .rules
                .iter()
                .find(|r| r.props.iter().any(|p| &p.key == k))
                .map(|r| format!("rule {}", r.id))
                .or_else(|| {
                    engine
                        .classes
                        .get(class)
                        .filter(|c| c.props_read_by_init.contains(k))
                        .map(|c| format!("the {class} init {}", c.init))
                })
                .or_else(|| {
                    engine
                        .element_base_props
                        .contains(k)
                        .then(|| "the element base init".to_owned())
                });
            if let Some(by) = read_by {
                out.push(Finding::stock_diff(
                    "prop-dropped",
                    class,
                    k,
                    &format!("{frag}: {path} [{class}]"),
                    format!("stock sets {k} here and {by} reads it; the edit drops it"),
                ));
            }
        }
    }
    Ok(out)
}

pub fn runtime_bindings(stock: &[u8], edited: &[u8], frag: &str) -> crate::Result<Vec<Finding>> {
    let (old, new) = (roots(stock)?, roots(edited)?);
    let mut out = Vec::new();
    for (name, keys) in &new {
        let Some(before) = old.get(name) else { continue };
        for k in keys.difference(before) {
            out.push(Finding::stock_diff(
                "vm-key-new",
                "",
                k,
                &format!("{frag}: {name}"),
                format!(
                    "binds view-model key {k}, which no stock {name} node binds; stock {name} only binds {}. The binding resolves only if native code has published {k} when the template binds, so the screen depends on that order",
                    before.iter().take(8).cloned().collect::<Vec<_>>().join(", ")
                ),
            ));
        }
    }
    Ok(out)
}

fn u64_of(n: Option<&Node>) -> Option<u64> {
    match &n?.as_val()?.value {
        swofty_formats::pkg::Value::Raw(b) if b.len() >= 8 => {
            Some(u64::from_le_bytes(b[..8].try_into().ok()?))
        }
        _ => None,
    }
}

struct Loader<'a> {
    engine: &'a Engine,
    frag: &'a str,
    out: Vec<Finding>,
}

impl Loader<'_> {
    fn flag(&mut self, id: &str, path: &str, message: String) {
        if let Some(rule) = self.engine.rule(id) {
            self.out
                .push(Finding::engine(rule, &format!("{}: {path}", self.frag), message));
        }
    }

    fn node(&mut self, n: &Node, parent: &str) {
        let Some(items) = n.as_arr().map(|a| &a.items) else {
            self.flag("node.record", parent, "a child is not a node record".into());
            return;
        };
        let props = items.first().and_then(Node::as_obj);
        let name = props.and_then(|p| str_of(p, "name"));
        let ty = props
            .and_then(|p| p.get("type"))
            .and_then(Node::as_val)
            .and_then(|v| v.as_u32());
        let path = match (parent, name) {
            ("", Some(nm)) => nm.to_owned(),
            (_, Some(nm)) => format!("{parent}.{nm}"),
            (_, None) => format!("{parent}.?"),
        };
        let slots_ok = (6..=8).contains(&items.len()) && items[1..].iter().all(|s| s.as_arr().is_some());
        let (Some(_), Some(ty), true) = (name, ty, slots_ok) else {
            self.flag(
                "node.record",
                &path,
                "the node must be [props with name and type, children, commands, events, layout, components(, behaviours, slot7)]".into(),
            );
            return;
        };
        let class = fourcc_str(ty);
        let here = format!("{path} [{class}]");
        let slot = |i: usize| {
            items
                .get(i)
                .and_then(Node::as_arr)
                .map(|a| a.items.as_slice())
                .unwrap_or_default()
        };
        let engine = self.engine;
        let Some(info) = engine.classes.get(&class) else {
            self.flag(
                "node.class_registered",
                &here,
                format!("no node class is registered for {class}"),
            );
            return;
        };
        if info.init_calls_element_base {
            if let Some(first) = slot(4).first() {
                match first.as_arr() {
                    Some(a) if a.items.is_empty() => self.flag(
                        "layout.path_nonempty",
                        &here,
                        "the layout path is an empty list".into(),
                    ),
                    Some(a) => {
                        for step in &a.items {
                            let Some(o) = step.as_obj() else {
                                self.flag("layout.step_keys", &here, "a layout step is not an object".into());
                                continue;
                            };
                            if o.get("isComponentId").is_some() {
                                if u64_of(o.get("componentId")).is_none() {
                                    self.flag(
                                        "layout.step_keys",
                                        &here,
                                        "a component layout step has no u64 componentId".into(),
                                    );
                                }
                            } else if o
                                .get("nodeId")
                                .and_then(Node::as_val)
                                .and_then(|v| v.as_u32())
                                .is_none()
                            {
                                self.flag(
                                    "layout.step_keys",
                                    &here,
                                    "a layout step has neither isComponentId nor a u32 nodeId".into(),
                                );
                            }
                        }
                    }
                    None => self.flag(
                        "layout.step_keys",
                        &here,
                        "the layout path is not a list of steps".into(),
                    ),
                }
            }
            if let Some(c) = slot(5).first() {
                let ok = c
                    .as_obj()
                    .is_some_and(|o| o.get("name").is_some() && u64_of(o.get("componentId")).is_some());
                if !ok {
                    self.flag(
                        "components.keys",
                        &here,
                        "the first component has no name or no u64 componentId".into(),
                    );
                }
            }
            for b in slot(6) {
                if b.as_obj().and_then(|o| o.get("type")).is_none() {
                    self.flag("behaviours.type", &here, "a behaviour has no type".into());
                }
            }
        }
        for (i, key, id) in [
            (COMMANDS, "command", "commands.entry"),
            (EVENTS, "event", "events.entry"),
        ] {
            for e in slot(i) {
                let o = e.as_obj();
                if !o.is_some_and(|o| {
                    o.get(key).is_some() && o.get("signals").and_then(Node::as_arr).is_some()
                }) {
                    let what = o
                        .and_then(|o| {
                            str_of(
                                o,
                                if key == "command" {
                                    "commandName"
                                } else {
                                    "eventName"
                                },
                            )
                        })
                        .unwrap_or("?");
                    self.flag(
                        id,
                        &here,
                        format!("{key} {what} needs a {key} hash and a signals list"),
                    );
                    continue;
                }
                let sigs = o
                    .and_then(|o| o.get("signals"))
                    .and_then(Node::as_arr)
                    .map(|a| a.items.as_slice())
                    .unwrap_or_default();
                for s in sigs {
                    let so = s.as_obj();
                    let ok = so.is_some_and(|s| s.get("to_pin").is_some() && s.get("to_object").is_some());
                    let maps_ok = so
                        .and_then(|s| s.get("mappings"))
                        .and_then(Node::as_arr)
                        .is_none_or(|m| {
                            m.items
                                .iter()
                                .all(|e| e.as_obj().is_some_and(|e| e.get("value").is_some()))
                        });
                    if !ok || !maps_ok {
                        self.flag(
                            "signals.entry",
                            &here,
                            "a signal needs to_pin and to_object, and every mapping a value".into(),
                        );
                    }
                }
            }
        }
        let p = props.expect("checked above");
        let behaviours: Vec<String> = slot(6)
            .iter()
            .filter_map(|b| b.as_obj()?.get("type")?.as_val()?.as_u32())
            .map(fourcc_str)
            .collect();
        for rule in engine.rules.iter().filter(|r| !r.props.is_empty()) {
            if !rule.applies_to.matches(&class, &behaviours) {
                continue;
            }
            for pr in &rule.props {
                if let Some(problem) = pr.problem(p.get(&pr.key)) {
                    let on = if rule.applies_to.behaviours.is_empty() {
                        class.clone()
                    } else {
                        format!("{class} with {}", rule.applies_to.behaviours.join(", "))
                    };
                    self.out.push(Finding::engine(
                        rule,
                        &format!("{}: {here}", self.frag),
                        format!("{on}: {problem}"),
                    ));
                }
            }
        }
        for c in slot(CHILDREN) {
            self.node(c, &path);
        }
    }
}

fn root_names(data: &[u8]) -> crate::Result<Vec<(String, String)>> {
    let p = Pkg::parse(data)?;
    let root = p.root.o()?;
    let mut out = Vec::new();
    for key in ["Screens", "Templates"] {
        for t in root
            .get(key)
            .and_then(Node::as_arr)
            .map(|a| a.items.as_slice())
            .unwrap_or_default()
        {
            if t.is_node() {
                out.push((key.to_owned(), t.name().to_owned()));
            }
        }
    }
    Ok(out)
}

pub fn loader_findings(
    resource: &str,
    frag: &str,
    data: &[u8],
    stock: Option<&[u8]>,
    cat: &Catalog,
    engine: &Engine,
) -> crate::Result<Vec<Finding>> {
    let p = Pkg::parse(data)?;
    let root = p.root.o()?;
    let mut l = Loader {
        engine,
        frag,
        out: Vec::new(),
    };
    for key in ["Screens", "Templates"] {
        for t in root
            .get(key)
            .and_then(Node::as_arr)
            .map(|a| a.items.as_slice())
            .unwrap_or_default()
        {
            l.node(t, "");
        }
    }
    let names = root_names(data)?;
    let before: BTreeSet<(String, String)> = match stock {
        Some(s) => root_names(s)?.into_iter().collect(),
        None => BTreeSet::new(),
    };
    let mut seen = BTreeSet::new();
    for (kind, name) in &names {
        let id = if kind == "Templates" {
            "template.duplicate"
        } else {
            "screen.duplicate"
        };
        let what = if kind == "Templates" { "template" } else { "screen" };
        if !seen.insert((kind.clone(), name.clone())) {
            l.flag(id, name, format!("a second {what} named {name} in this file"));
            continue;
        }
        if before.contains(&(kind.clone(), name.clone())) {
            continue;
        }
        let elsewhere: Vec<&String> = cat
            .root_names
            .get(kind)
            .and_then(|m| m.get(name))
            .map(|v| v.iter().filter(|r| r.as_str() != resource).collect())
            .unwrap_or_default();
        if let Some(other) = elsewhere.first() {
            l.flag(
                id,
                name,
                format!("stock {other} already registers a {what} named {name}"),
            );
        }
    }
    Ok(l.out)
}

#[derive(Clone, Copy)]
pub struct UiInput<'a> {
    pub resource: &'a str,
    pub frag: &'a str,
    pub data: &'a [u8],
    pub stock: Option<&'a [u8]>,
}

pub fn ui_edit(input: &UiInput, scenes: &Scenes, cat: &Catalog, engine: &Engine) -> crate::Result<Report> {
    let mut report = Report::default();
    let frag = input.frag;
    let mut found = integrity_findings(input.data, frag)?;
    found.extend(loader_findings(
        input.resource,
        frag,
        input.data,
        input.stock,
        cat,
        engine,
    )?);
    found.extend(against_stock(input.data, cat, scenes)?.into_iter().map(|mut f| {
        f.at = format!("{frag}: {}", f.at);
        f
    }));
    if let Some(s) = input.stock {
        found.extend(runtime_bindings(s, input.data, frag)?);
        found.extend(dropped_props(s, input.data, frag, engine)?);
    }
    for f in found {
        report.add(f);
    }
    Ok(report)
}

pub fn scene_edit(resource: &str, data: &[u8], cat: &Catalog) -> crate::Result<Report> {
    let mut report = Report::default();
    let p = Pkg::parse(data)?;
    let format = "format: the game finds a scene by its componentId, md5-64 of its eaid path, and an element by its leaf name through nodeidlookup";
    for m in idf::xref_mismatches(&p)? {
        let (at, msg) = m.split_once(": ").unwrap_or((&m, ""));
        report.add(Finding::hard(
            "xref-mismatch",
            at,
            msg.to_owned(),
            "format: the game loads the scene an eXRef's xref id names",
        ));
    }
    if resource.ends_with(".idf") {
        let want = idf::scene_id(resource);
        for entry in idf::scenes(&p)? {
            if idf::scene_name(entry) != Some(want) {
                report.add(Finding::hard(
                    "scene-id",
                    resource,
                    format!("the scene's name is not {want:#018x}, the componentId of its path"),
                    format,
                ));
            }
        }
    }
    for problem in idf::id_problems(&p)? {
        if problem == "nodeidlookup" {
            report.add(Finding::hard(
                "nodeidlookup",
                resource,
                "nodeidlookup is not the sorted djb2 of every element's leaf name, so layout attaches cannot find the missing elements".into(),
                format,
            ));
        } else {
            report.add(Finding::warn(
                "element-id",
                "",
                &problem,
                resource,
                format!("{problem} is not the djb2 of the element's dotted path"),
            ));
        }
    }
    for f in scene_against_stock(data, cat)? {
        report.add(f);
    }
    Ok(report)
}

#[derive(Debug, Clone, Default)]
pub struct ArchiveUi {
    pub archive: String,
    pub resources: BTreeMap<String, (String, Vec<Vec<u8>>)>,
}

impl ArchiveUi {
    pub fn read(archive: &str, path: &std::path::Path) -> crate::Result<Self> {
        let all = swofty_formats::erp::Erp::read_matching(path, |n| {
            n.ends_with(".ui") || (n.ends_with(".idf") && !n.contains('?'))
        })?;
        let mut resources = BTreeMap::new();
        for r in all
            .into_iter()
            .filter(|r| matches!(r.kind().as_str(), "UIDF" | "UILayout"))
        {
            let frags = r
                .fragments
                .iter()
                .map(|f| f.data())
                .collect::<swofty_formats::Result<Vec<_>>>()?;
            resources.insert(r.name.clone(), (r.kind(), frags));
        }
        Ok(Self {
            archive: archive.to_owned(),
            resources,
        })
    }

    fn scenes(&self) -> Scenes {
        let mut s = Scenes::default();
        for (kind, frags) in self.resources.values() {
            if kind == "UILayout"
                && let Some(f) = frags.first()
            {
                s.add(f).ok();
            }
        }
        s
    }
}

type Binding = (String, Option<u64>, Vec<u32>, Vec<String>);
type CopyOf<'a> = (&'a String, &'a Vec<Vec<u8>>);

fn bindings(data: &[u8]) -> crate::Result<Vec<Binding>> {
    fn walk(n: &Node, path: &str, ctx: Option<u64>, out: &mut Vec<Binding>) {
        let here = if path.is_empty() {
            n.name().to_owned()
        } else {
            format!("{path}.{}", n.name())
        };
        let own = node_scene(n);
        if let Some(first) = layout_groups(n).into_iter().next() {
            let ids: Vec<u32> = first
                .iter()
                .filter_map(|b| b.get("nodeId"))
                .filter_map(Node::as_val)
                .filter_map(|v| v.as_u32())
                .collect();
            let names: Vec<String> = first
                .iter()
                .filter_map(|b| str_of(b, "debugName"))
                .map(str::to_owned)
                .collect();
            if !path.is_empty() && !ids.is_empty() && ids.len() == first.len() {
                out.push((here.clone(), ctx, ids, names));
            }
        }
        for c in n.slot(CHILDREN).into_iter().flatten().filter(|c| c.is_node()) {
            walk(c, &here, own.or(ctx), out);
        }
    }
    let p = Pkg::parse(data)?;
    let mut out = Vec::new();
    for key in ["Screens", "Templates"] {
        for t in p
            .root
            .o()?
            .get(key)
            .and_then(Node::as_arr)
            .map(|a| a.items.as_slice())
            .unwrap_or_default()
        {
            if t.is_node() {
                walk(t, "", node_scene(t), &mut out);
            }
        }
    }
    Ok(out)
}

fn roots_scenes(data: &[u8]) -> crate::Result<Vec<(String, u64)>> {
    let p = Pkg::parse(data)?;
    let mut out = Vec::new();
    for key in ["Screens", "Templates"] {
        for t in p
            .root
            .o()?
            .get(key)
            .and_then(Node::as_arr)
            .map(|a| a.items.as_slice())
            .unwrap_or_default()
        {
            if let Some(id) = t.is_node().then(|| node_scene(t)).flatten() {
                out.push((t.name().to_owned(), id));
            }
        }
    }
    Ok(out)
}

fn in_lookup(scenes: &Scenes, ids: &[u32]) -> Option<u32> {
    ids.iter()
        .copied()
        .find(|id| !scenes.scenes.values().any(|s| s.lookup.binary_search(id).is_ok()))
}

pub fn scene_bindings(
    archive: &str,
    input: &UiInput,
    edited: &Scenes,
    before: &Scenes,
    cat: &Catalog,
) -> crate::Result<Vec<Finding>> {
    let UiInput {
        resource,
        frag,
        data,
        stock,
    } = *input;
    let format = "format: the game resolves each layout step's nodeId through the nodeidlookup of the scene it reaches";
    let mut out = Vec::new();
    let at = |p: &str| format!("{archive}: {resource} {frag}: {p}");
    let stock_roots: BTreeSet<(String, u64)> = match stock {
        Some(s) => roots_scenes(s)?.into_iter().collect(),
        None => BTreeSet::new(),
    };
    for (root, id) in roots_scenes(data)? {
        if stock_roots.contains(&(root.clone(), id)) {
            continue;
        }
        if !edited.scenes.contains_key(&id) && !cat.has_scene(id) {
            out.push(Finding::hard(
                "component-missing",
                &at(&root),
                format!("its scene {id:#018x} is in neither this archive nor any stock archive"),
                format,
            ));
        } else if !edited.scenes.contains_key(&id) {
            out.push(Finding::warn("component-elsewhere", "", &format!("{id:016x}"), &at(&root), format!("its scene {id:#018x} is not in this archive; it loads only if another archive has brought it in")));
        }
    }
    let old = match stock {
        Some(s) => bindings(s)?,
        None => Vec::new(),
    };
    let found: BTreeSet<(String, Vec<u32>)> = old
        .iter()
        .filter(|(_, _, ids, _)| in_lookup(before, ids).is_none())
        .map(|(p, _, ids, _)| (p.clone(), ids.clone()))
        .collect();
    let resolved: BTreeSet<(String, Vec<u32>)> = old
        .iter()
        .filter(|(_, ctx, ids, _)| resolves(before, *ctx, ids))
        .map(|(p, _, ids, _)| (p.clone(), ids.clone()))
        .collect();
    let known: BTreeSet<(String, Vec<u32>)> = old.into_iter().map(|(p, _, ids, _)| (p, ids)).collect();
    for (path, ctx, ids, names) in bindings(data)? {
        let key = (path.clone(), ids.clone());
        if let Some(missing) = in_lookup(edited, &ids) {
            let name = names
                .get(ids.iter().position(|i| *i == missing).unwrap_or(0))
                .cloned()
                .unwrap_or_default();
            if found.contains(&key) {
                out.push(Finding::hard(
                    "layout-node-missing",
                    &at(&path),
                    format!("layout step {name} (nodeId {missing}) was in the stock scenes of this archive and is in no edited scene's nodeidlookup"),
                    format,
                ));
            } else if !known.contains(&key) {
                out.push(Finding::warn(
                    "layout-node-unknown",
                    "",
                    &name,
                    &at(&path),
                    format!("layout step {name} (nodeId {missing}) is in no scene's nodeidlookup in this archive; it binds only if another archive's scene or native code provides it"),
                ));
            }
            continue;
        }
        if resolved.contains(&key) && !resolves(edited, ctx, &ids) {
            out.push(Finding::hard(
                "layout-unresolved",
                &at(&path),
                format!(
                    "layout {} resolved against the stock scenes and does not resolve against the edited ones",
                    names.join(">")
                ),
                format,
            ));
        }
    }
    Ok(out)
}

fn resolves(scenes: &Scenes, ctx: Option<u64>, ids: &[u32]) -> bool {
    let Some(sid) = ctx else { return false };
    scenes.resolve_ids(ids, sid)
}

pub fn archives(
    edited: &[ArchiveUi],
    stock: &[ArchiveUi],
    cat: &Catalog,
    engine: &Engine,
) -> crate::Result<Report> {
    let mut report = Report::default();
    let empty = ArchiveUi::default();
    for a in edited {
        let s = stock.iter().find(|s| s.archive == a.archive).unwrap_or(&empty);
        let changed: Vec<(&String, &String, &Vec<Vec<u8>>)> = a
            .resources
            .iter()
            .filter(|(name, (_, frags))| s.resources.get(*name).is_none_or(|(_, f)| f != frags))
            .map(|(name, (kind, frags))| (name, kind, frags))
            .collect();
        if changed.is_empty() {
            continue;
        }
        let (scenes, before) = (a.scenes(), s.scenes());
        for (name, kind, frags) in changed {
            let base = s.resources.get(name).map(|(_, f)| f);
            for (i, data) in frags.iter().enumerate() {
                let old = base.and_then(|f| f.get(i)).map(Vec::as_slice);
                if old == Some(data.as_slice()) || !swofty_formats::pkg::is_pkg(data) {
                    continue;
                }
                let frag = format!("{}#{i}", if kind == "UIDF" { "ui" } else { "idf" });
                let mut r = if kind == "UIDF" {
                    let input = UiInput {
                        resource: name,
                        frag: &frag,
                        data,
                        stock: old,
                    };
                    let mut r = ui_edit(&input, &scenes, cat, engine)?;
                    for f in scene_bindings(&a.archive, &input, &scenes, &before, cat)? {
                        r.add(f);
                    }
                    r
                } else {
                    scene_edit(name, data, cat)?
                };
                for f in r.hard.iter_mut().chain(r.warnings.iter_mut()) {
                    if !f.at.starts_with(&a.archive) {
                        f.at = format!("{}: {name}: {}", a.archive, f.at);
                    }
                }
                report.extend(r);
            }
        }
    }
    let mut copies: BTreeMap<&String, Vec<CopyOf>> = BTreeMap::new();
    for a in edited {
        for (name, (_, frags)) in &a.resources {
            copies.entry(name).or_default().push((&a.archive, frags));
        }
    }
    for (name, list) in copies {
        if let Some((a, first)) = list.first()
            && let Some((b, _)) = list.iter().find(|(_, f)| f != first)
        {
            report.add(Finding::warn(
                "copies-differ",
                "",
                name,
                name,
                format!("{a} and {b} carry different copies; the game uses the one from the archive that loads first"),
            ));
        }
    }
    Ok(report)
}

pub fn archive_files(pairs: &[(&str, &std::path::Path, &std::path::Path)]) -> crate::Result<Report> {
    let mut edited = Vec::new();
    let mut stock = Vec::new();
    for (name, before, after) in pairs {
        stock.push(ArchiveUi::read(name, before)?);
        edited.push(ArchiveUi::read(name, after)?);
    }
    archives(&edited, &stock, super::catalog::stock(), super::catalog::engine())
}

const SKIP_PROPS: &[&str] = &["id", "nameId", "version", "name", "type"];
const COMPONENTS: usize = 5;

struct Stock<'a> {
    cat: &'a Catalog,
    scenes: &'a Scenes,
    types: BTreeMap<String, String>,
    out: Vec<Finding>,
}

fn node_scene(n: &Node) -> Option<u64> {
    let comp = |o: &swofty_formats::pkg::Obj| match &o.get("componentId")?.as_val()?.value {
        swofty_formats::pkg::Value::Raw(b) if b.len() >= 8 => {
            Some(u64::from_le_bytes(b[..8].try_into().ok()?))
        }
        _ => None,
    };
    if let Some(id) = n
        .slot(COMPONENTS)
        .into_iter()
        .flatten()
        .filter_map(Node::as_obj)
        .find_map(comp)
    {
        return Some(id);
    }
    layout_groups(n)
        .into_iter()
        .flatten()
        .filter(|b| b.get("isComponentId").is_some())
        .find_map(comp)
}

fn layout_groups(n: &Node) -> Vec<Vec<&swofty_formats::pkg::Obj>> {
    n.slot(LAYOUT)
        .into_iter()
        .flatten()
        .map(|grp| match grp {
            Node::Arr(a) => a.items.iter().filter_map(Node::as_obj).collect(),
            other => other.as_obj().into_iter().collect(),
        })
        .collect()
}

fn seen<V>(m: &BTreeMap<String, V>) -> String {
    let keys: Vec<&str> = m.keys().map(String::as_str).take(8).collect();
    let more = if m.len() > 8 { ", …" } else { "" };
    format!("{}{more}", keys.join(", "))
}

impl Stock<'_> {
    fn visit(
        &mut self,
        n: &Node,
        path: &str,
        parent: Option<&str>,
        role: &str,
        ctx: Option<u64>,
        scopes: &[u64],
    ) {
        let t = n.type_code();
        let here = format!("{path} [{t}]");
        let own = node_scene(n);
        let kid_ctx = own.or(ctx);
        let mut kid_scopes = scopes.to_vec();
        if let Some(nt) = self.cat.node_types.get(&t) {
            self.node(n, &here, nt, &t, parent, role, ctx, scopes, &mut kid_scopes);
        } else {
            self.out.push(Finding::warn(
                "node-type",
                &t,
                &t,
                &here,
                format!("node type {t} is not in any stock .ui"),
            ));
        }
        for c in n.slot(CHILDREN).into_iter().flatten().filter(|c| c.is_node()) {
            let p = format!("{path}.{}", c.name());
            self.visit(c, &p, Some(&t), "child", kid_ctx, &kid_scopes);
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn node(
        &mut self,
        n: &Node,
        here: &str,
        nt: &NodeType,
        t: &str,
        parent: Option<&str>,
        role: &str,
        ctx: Option<u64>,
        scopes: &[u64],
        kid_scopes: &mut Vec<u64>,
    ) {
        let out = &mut self.out;
        if role != "child" && !nt.roles.contains_key(role) {
            out.push(Finding::warn(
                "root-type",
                t,
                role,
                here,
                format!("stock never uses a {t} as a {role}; it is a {}", seen(&nt.roles)),
            ));
        }
        if let Some(p) = parent
            && self.cat.node_types.contains_key(p)
            && !nt.parents.contains_key(p)
        {
            out.push(Finding::warn(
                "parent-type",
                t,
                p,
                here,
                format!(
                    "stock never puts a {t} under a {p}; its parents are {}",
                    seen(&nt.parents)
                ),
            ));
        }
        if let Ok(props) = n.props() {
            for (k, v) in &props.items {
                let key = k.as_str().unwrap_or_default();
                if SKIP_PROPS.contains(&key) {
                    continue;
                }
                match nt.props.get(key) {
                    None => out.push(Finding::warn(
                        "prop",
                        t,
                        key,
                        here,
                        format!("property {key} is never set on a stock {t}"),
                    )),
                    Some(p) => {
                        let sh = shape(v);
                        if !p.shapes.contains_key(&sh) {
                            out.push(Finding::warn(
                                "prop-shape",
                                t,
                                key,
                                here,
                                format!("property {key} is {sh}; stock {t} uses {}", seen(&p.shapes)),
                            ));
                        }
                    }
                }
            }
        }
        for (slot, key, kind, stock) in [
            (COMMANDS, "commandName", "command", &nt.commands),
            (EVENTS, "eventName", "event", &nt.events),
        ] {
            for e in n.slot(slot).into_iter().flatten().filter_map(Node::as_obj) {
                let name = str_of(e, key).unwrap_or("?");
                let sigs: Vec<&swofty_formats::pkg::Obj> = e
                    .get("signals")
                    .and_then(Node::as_arr)
                    .map(|a| a.items.iter().filter_map(Node::as_obj).collect())
                    .unwrap_or_default();
                match stock.get(name) {
                    None => out.push(Finding::warn(
                        kind,
                        t,
                        name,
                        here,
                        format!("{kind} {name} is never declared on a stock {t}"),
                    )),
                    Some(s) if sigs.is_empty() && s.no_signals == 0 => out.push(Finding::warn(
                        if kind == "event" {
                            "event-no-signals"
                        } else {
                            "command-no-signals"
                        },
                        t,
                        name,
                        here,
                        format!(
                            "{kind} {name} has no signals; all {} stock {t} {name} {kind}s have signals. The loader accepts an empty signals list, and raising the {kind} then does nothing ({}.entry; Vm_Raise_NodeEvent 0x1442468d0)",
                            s.count,
                            if kind == "event" { "events" } else { "commands" }
                        ),
                    )),
                    Some(s) if !sigs.is_empty() && s.no_signals == s.count => out.push(Finding::warn(
                        if kind == "event" {
                            "event-signals"
                        } else {
                            "command-signals"
                        },
                        t,
                        name,
                        here,
                        format!("{kind} {name} has signals; no stock {t} {name} {kind} has any"),
                    )),
                    _ => {}
                }
                for s in sigs {
                    let to = str_of(s, "to_object_name").unwrap_or_default();
                    let pin = str_of(s, "to_pin_name").unwrap_or_default();
                    let from = str_of(s, "from_pin_name").unwrap_or("null");
                    if !nt.pins_out.contains_key(from) {
                        out.push(Finding::warn(
                            "pin-out",
                            t,
                            from,
                            here,
                            format!("on {name} sends from pin {from}, which no stock {t} sends from"),
                        ));
                    }
                    let Some(tt) = self.types.get(to) else { continue };
                    let Some(target) = self.cat.node_types.get(tt) else {
                        continue;
                    };
                    let Some(p) = target.pins_in.get(pin) else {
                        out.push(Finding::warn(
                            "pin-in",
                            tt,
                            pin,
                            here,
                            format!(
                                "on {name} -> {to}.{pin}: no stock signal reaches pin {pin} on a {tt}{}",
                                pin_note(tt, pin)
                            ),
                        ));
                        continue;
                    };
                    if !nt.links.contains_key(&format!("{tt}.{pin}")) {
                        out.push(Finding::warn(
                            "link",
                            t,
                            &format!("{tt}.{pin}"),
                            here,
                            format!("on {name} -> {to}.{pin}: no stock {t} signals a {tt}.{pin}"),
                        ));
                    }
                    let attrs = s.get("attributes").map(attr_shape);
                    let with: u64 = p.attributes.values().sum();
                    match attrs {
                        Some(a) if with == 0 => out.push(Finding::warn("pin-attributes", tt, pin, here, format!(
                            "on {name} -> {to}.{pin}: sends attributes {a}; stock signals to {tt}.{pin} carry none"
                        ))),
                        Some(a) if !p.attributes.contains_key(&a) => out.push(Finding::warn("pin-attributes", tt, pin, here, format!(
                            "on {name} -> {to}.{pin}: attributes {a}; stock signals to {tt}.{pin} carry {}",
                            seen(&p.attributes)
                        ))),
                        None if with == p.count => out.push(Finding::warn("pin-attributes", tt, pin, here, format!(
                            "on {name} -> {to}.{pin}: no attributes; every stock signal to {tt}.{pin} carries {}",
                            seen(&p.attributes)
                        ))),
                        _ => {}
                    }
                }
            }
        }
        for grp in layout_groups(n) {
            let kinds: Vec<&str> = grp
                .iter()
                .map(|b| {
                    if b.get("isComponentId").is_some() {
                        "comp"
                    } else if b.get("nodeId").is_some() {
                        "node"
                    } else {
                        "?"
                    }
                })
                .collect();
            let key = format!("{role}:{}", kinds.join(">"));
            if !nt.layout.contains_key(&key) {
                out.push(Finding::warn(
                    "layout-shape",
                    t,
                    &kinds.join(">"),
                    here,
                    format!(
                        "layout shape {} as a {role} is never used by a stock {t}; it uses {}",
                        kinds.join(">"),
                        seen(&nt.layout)
                    ),
                ));
            }
            let names: Vec<&str> = grp
                .iter()
                .filter(|b| b.get("nodeId").is_some())
                .filter_map(|b| str_of(b, "debugName"))
                .collect();
            if role != "child" || names.is_empty() || names.len() != grp.len() {
                continue;
            }
            let hit = ctx
                .into_iter()
                .chain(scopes.iter().rev().copied())
                .filter(|sid| self.scenes.scenes.contains_key(sid))
                .find_map(|sid| self.scenes.resolve(&names, sid).ok());
            let Some((chain, xref)) = hit else { continue };
            let ch = chain.join(">");
            if chain[..chain.len() - 1].iter().any(|c| c != "eXRef") {
                out.push(Finding::warn("layout-through", t, &ch, here, format!(
                    "layout {} walks {ch}; in stock every element before the last in a layout path is an eXRef",
                    names.join(">")
                )));
            } else if !nt.layout_elements.contains_key(&ch) {
                out.push(Finding::warn(
                    "layout-element",
                    t,
                    &ch,
                    here,
                    format!(
                        "layout {} binds {ch}; stock {t} binds {}",
                        names.join(">"),
                        seen(&nt.layout_elements)
                    ),
                ));
            }
            if let Some(x) = xref {
                kid_scopes.push(x);
            }
        }
    }
}

fn pin_note(class: &str, pin: &str) -> String {
    match super::catalog::engine().classes.get(class) {
        Some(c) if !c.native_pins.iter().any(|p| p == pin) => format!(
            "; the {class} pin handler ({}) has no case for it, so the signal does nothing unless the target declares an event or command named {pin}",
            c.deliver_pin
        ),
        _ => String::new(),
    }
}

fn index_types(n: &Node, path: &str, out: &mut BTreeMap<String, String>) {
    out.insert(path.to_owned(), n.type_code());
    for c in n.slot(CHILDREN).into_iter().flatten().filter(|c| c.is_node()) {
        index_types(c, &format!("{path}.{}", c.name()), out);
    }
}

pub fn against_stock(data: &[u8], cat: &Catalog, scenes: &Scenes) -> crate::Result<Vec<Finding>> {
    let p = Pkg::parse(data)?;
    let root = p.root.o()?;
    let mut out = Vec::new();
    for (key, role) in [("Screens", "screen_root"), ("Templates", "template_root")] {
        let tops: Vec<&Node> = root
            .get(key)
            .and_then(Node::as_arr)
            .map(|a| a.items.iter().filter(|t| t.is_node()).collect())
            .unwrap_or_default();
        let mut types = BTreeMap::new();
        for t in &tops {
            index_types(t, t.name(), &mut types);
        }
        let mut st = Stock {
            cat,
            scenes,
            types,
            out: Vec::new(),
        };
        for t in tops {
            st.visit(t, t.name(), None, role, node_scene(t), &[]);
        }
        out.extend(st.out);
    }
    Ok(out)
}

pub fn scene_against_stock(data: &[u8], cat: &Catalog) -> crate::Result<Vec<Finding>> {
    let p = Pkg::parse(data)?;
    let mut out = Vec::new();
    for entry in idf::scenes(&p)? {
        let Some(root) = entry.as_obj().and_then(|o| o.get("scene")) else {
            continue;
        };
        scene_walk(root, None, "", cat, &mut out);
    }
    Ok(out)
}

fn scene_walk(e: &Node, parent: Option<&str>, path: &str, cat: &Catalog, out: &mut Vec<Finding>) {
    let t = element_type(e);
    let here = format!("{} [{t}]", if path.is_empty() { idf::elem_name(e) } else { path });
    match cat.scene_elements.get(&t) {
        None => out.push(Finding::warn(
            "element-type",
            &t,
            &t,
            &here,
            format!("element type {t} is not in any stock scene"),
        )),
        Some(et) => {
            if let Some(p) = parent
                && !et.parents.contains_key(p)
            {
                out.push(Finding::warn(
                    "element-parent",
                    &t,
                    p,
                    &here,
                    format!(
                        "stock never puts a {t} under a {p}; its parents are {}",
                        seen(&et.parents)
                    ),
                ));
            }
            for prm in e
                .as_obj()
                .and_then(|o| o.get("params"))
                .and_then(Node::as_arr)
                .map(|a| a.items.as_slice())
                .unwrap_or_default()
                .iter()
                .filter_map(Node::as_obj)
            {
                let name = str_of(prm, "name").unwrap_or_default();
                let ty = match prm.get("value") {
                    Some(Node::Val(v)) => tag(v.ty),
                    Some(other) => shape(other),
                    None => "none".into(),
                };
                match et.params.get(name) {
                    None => out.push(Finding::warn(
                        "param",
                        &t,
                        name,
                        &here,
                        format!("param {name} is never set on a stock {t}"),
                    )),
                    Some(sp) if !sp.types.contains_key(&ty) => out.push(Finding::warn(
                        "param-type",
                        &t,
                        name,
                        &here,
                        format!("param {name} is {ty}; stock {t} uses {}", seen(&sp.types)),
                    )),
                    _ => {}
                }
            }
        }
    }
    for c in e
        .as_obj()
        .and_then(|o| o.get("children"))
        .and_then(Node::as_arr)
        .map(|a| a.items.as_slice())
        .unwrap_or_default()
        .iter()
        .filter(|c| c.as_obj().is_some())
    {
        let name = idf::elem_name(c);
        let p = match (parent, path) {
            (None, _) => name.to_owned(),
            (_, "") => name.to_owned(),
            _ => format!("{path}.{name}"),
        };
        scene_walk(c, Some(&t), &p, cat, out);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ui::node::{
        arr, boolv, command, event, f32v, fix_ids, node, obj, prop, s, sig, u32v, ui_bytes,
    };

    fn screen_with(kids: Vec<Node>, vm: bool) -> Vec<u8> {
        let mut props = vec![];
        if vm {
            props.push(("allowVMBinding", prop(boolv(true), None)));
        }
        let mut scr = node("page", "UISC", props, vec![], &[], &[]);
        scr.a_mut().unwrap()[CHILDREN] = arr(kids);
        ui_bytes(vec![scr]).unwrap()
    }

    #[test]
    fn integrity_finds_stale_hashes_and_missing_targets() {
        use crate::ui::node::{event, sig};
        let kid = node(
            "kid",
            "UIGP",
            vec![],
            vec![event("pressed", vec![sig("page.kid", "focus", "pressed")])],
            &["slot"],
            &[],
        );
        let mut scr = node("page", "UISC", vec![], vec![], &[], &[]);
        scr.a_mut().unwrap()[CHILDREN] = arr(vec![kid]);
        let mut bad = scr.clone();
        fix_ids(&mut scr, None);
        let (nodes, signals, problems) = integrity(&ui_bytes(vec![scr]).unwrap()).unwrap();
        assert_eq!((nodes, signals), (2, 1));
        assert!(problems.is_empty(), "{problems:?}");
        let kid = &mut bad.slot_mut(CHILDREN).unwrap()[0];
        let e = &mut kid.slot_mut(EVENTS).unwrap()[0];
        let s = &mut e.o_mut().unwrap().at_mut("signals").unwrap().a_mut().unwrap()[0];
        set_str(s.o_mut().unwrap(), "to_object_name", "page.gone").unwrap();
        let (_, _, problems) = integrity(&ui_bytes(vec![bad]).unwrap()).unwrap();
        assert_eq!(
            problems,
            [
                "page: id",
                "page.kid: id",
                "page.kid: hash page.gone.focus",
                "page.kid: on pressed -> missing page.gone.focus"
            ]
        );
    }

    #[test]
    fn list_controls_need_bound_data() {
        let bound = node(
            "ok",
            "UITG",
            vec![("data", obj(vec![("value", s("")), ("dbentry", s("#a.b"))]))],
            vec![],
            &[],
            &[],
        );
        let literal = node(
            "bad",
            "UITG",
            vec![("data", prop(s("x"), None))],
            vec![],
            &[],
            &[],
        );
        let files = vec![("page".to_owned(), screen_with(vec![bound, literal], false))];
        let out = screens(&files, &[]).unwrap();
        assert_eq!(out.len(), 1, "{out:?}");
        assert!(out[0].starts_with("page.bad [UITG]"));
    }

    #[test]
    fn view_model_binding_needs_a_view_model() {
        let files = vec![("page".to_owned(), screen_with(vec![], true))];
        assert_eq!(screens(&files, &[]).unwrap().len(), 1);
        assert!(screens(&files, &["page".to_owned()]).unwrap().is_empty());
        let plain = vec![("page".to_owned(), screen_with(vec![], false))];
        assert_eq!(screens(&plain, &["page".to_owned()]).unwrap().len(), 1);
    }

    fn cmd(kind: &str, attrs: &[(&str, &str)]) -> Node {
        let a = attrs
            .iter()
            .map(|(k, v)| obj(vec![("Id", s(k)), ("Value", s(v))]))
            .collect();
        obj(vec![("Attributes", arr(a)), ("Type", s(kind))])
    }

    fn route(id: &str, cmds: Vec<Node>) -> Node {
        obj(vec![("Id", s(id)), ("Commands", arr(cmds))])
    }

    fn pkg(root: Node) -> Vec<u8> {
        Pkg {
            name: [0; 4],
            root,
            vca: Vec::new(),
            layout: None,
        }
        .to_bytes()
        .unwrap()
    }

    #[test]
    fn routes_must_reach_screens_and_sub_routes() {
        let entry = node(
            "enter_page",
            "FLEN",
            vec![("screen", prop(s("page"), None))],
            vec![],
            &[],
            &[],
        );
        let mut scene = node("scene", "FLSC", vec![], vec![], &[], &[]);
        scene.a_mut().unwrap()[CHILDREN] = arr(vec![entry]);
        let mut flow = node(
            "options_flow",
            "FLOW",
            vec![(
                "asset_group",
                prop(s("asset_groups/ui_package/flow_options.assetgroup"), None),
            )],
            vec![],
            &[],
            &[],
        );
        flow.a_mut().unwrap()[CHILDREN] = arr(vec![scene]);
        let flows = pkg(obj(vec![("Flows", arr(vec![flow]))]));
        let group = |id: &str, routes: Vec<Node>| obj(vec![("Id", s(id)), ("Routes", arr(routes))]);
        let routes = pkg(obj(vec![(
            "RouteGroupings",
            arr(vec![
                group(
                    "page",
                    vec![route("open", vec![cmd("OpenScreen", &[("Target", "page")])])],
                ),
                group(
                    "home",
                    vec![
                        route(
                            "go",
                            vec![cmd(
                                "TriggerSubRoute",
                                &[("SubRoute", "open"), ("Target", "page")],
                            )],
                        ),
                        route(
                            "bad",
                            vec![
                                cmd("TriggerSubRoute", &[("SubRoute", "shut"), ("Target", "page")]),
                                cmd("OpenScreen", &[("Target", "nowhere")]),
                                cmd("Migrate", &[("Flow", "no_flow")]),
                            ],
                        ),
                    ],
                ),
            ]),
        )]));
        let registered = BTreeMap::from([("page".to_owned(), "flow_options".to_owned())]);
        let out = super::routes(&routes, &flows, &["page", "home.go"], &registered).unwrap();
        assert!(out.is_empty(), "{out:?}");
        let out = super::routes(&routes, &flows, &["home"], &registered).unwrap();
        assert_eq!(out.len(), 3, "{out:?}");
        let elsewhere = BTreeMap::from([("page".to_owned(), "flow_frontend".to_owned())]);
        let out = super::routes(&routes, &flows, &[], &elsewhere).unwrap();
        assert_eq!(out.len(), 1, "{out:?}");
    }

    fn cat() -> Catalog {
        serde_json::from_value(serde_json::json!({
            "node_types": {
                "UIGP": {
                    "roles": { "template_root": 1, "child": 1 },
                    "parents": { "UIGP": 1 },
                    "props": { "visible": { "count": 1, "shapes": { "bool": 1 } } },
                    "events": { "navUp": { "count": 3, "no_signals": 0 }, "anim_complete": { "count": 2, "no_signals": 2 } },
                    "commands": { "focus": { "count": 1, "no_signals": 0 } },
                    "pins_in": { "focus": { "count": 1 }, "raise_command": { "count": 1, "attributes": { "command.name.value:stri": 1 } } },
                    "pins_out": { "navUp": 1, "anim_complete": 1, "focus": 1 },
                    "links": { "UIGP.focus": 1, "UIGP.raise_command": 1 },
                    "layout": { "child:node": 1, "child:node>node": 1 },
                    "layout_elements": { "eXRef": 1, "eXRef>eNull": 1 }
                }
            },
            "scene_elements": {
                "eScene": {},
                "eXRef": { "parents": { "eScene": 1, "eMaskTexture": 1 }, "params": { "fileRef": { "count": 1, "types": { "stri": 1 } } } },
                "eNull": { "parents": { "eScene": 1 } },
                "eMaskTexture": { "parents": { "eScene": 1 } },
                "eMask": { "parents": { "eNull": 1 }, "params": {} }
            }
        }))
        .unwrap()
    }

    fn u64v(v: u64) -> Node {
        Node::Val(swofty_formats::pkg::Val::raw(*b"ui64", v.to_le_bytes()))
    }

    fn el(name: &str, ty: &str, kids: Vec<Node>, xref: Option<u64>) -> Node {
        let mut t = ty.as_bytes().to_vec();
        t.resize(16, 0);
        let mut pairs = vec![
            ("name", s(name)),
            ("typeName", Node::Val(swofty_formats::pkg::Val::raw(*b"shnm", t))),
            ("params", arr(vec![])),
            ("children", arr(kids)),
        ];
        if let Some(x) = xref {
            pairs.push(("xref", u64v(x)));
        }
        obj(pairs)
    }

    fn scene(id: u64, kids: Vec<Node>) -> Vec<u8> {
        let entry = obj(vec![
            ("scene", el("Scene Root", "eScene", kids, None)),
            ("name", u64v(id)),
            ("nodeidlookup", arr(vec![])),
        ]);
        pkg(obj(vec![("scenelist", arr(vec![entry]))]))
    }

    fn template_node(kids: Vec<Node>, scene_id: u64) -> Node {
        let mut root = node("tpl", "UIGP", vec![], vec![], &[], &[]);
        root.a_mut().unwrap()[CHILDREN] = arr(kids);
        root.a_mut().unwrap()[COMPONENTS] = arr(vec![obj(vec![
            ("componentId", u64v(scene_id)),
            ("debugName", s("eaid://ui_package/art/x")),
            ("name", s("x")),
        ])]);
        fix_ids(&mut root, None);
        root
    }

    fn template(kids: Vec<Node>, scene_id: u64) -> Vec<u8> {
        pkg(obj(vec![("Templates", arr(vec![template_node(kids, scene_id)]))]))
    }

    #[test]
    fn catalogue_flags_what_stock_never_does() {
        let mut row = node(
            "row",
            "UIGP",
            vec![
                ("visible", prop(s("yes"), None)),
                ("invented", prop(boolv(true), None)),
            ],
            vec![
                event("navUp", vec![]),
                event("anim_complete", vec![sig("tpl.row", "focus", "anim_complete")]),
                event("mystery", vec![sig("tpl.row", "focus", "mystery")]),
            ],
            &[],
            &[],
        );
        row.a_mut().unwrap()[COMMANDS] =
            arr(vec![command("focus", vec![sig("tpl.row", "explode", "focus")])]);
        let other = node("other", "UICX", vec![], vec![], &[], &[]);
        let out = against_stock(&template(vec![row, other], 1), &cat(), &Scenes::default()).unwrap();
        let has = |s: &str| out.iter().any(|f| f.to_string().contains(s));
        assert!(has("property visible is stri; stock UIGP uses bool"), "{out:#?}");
        assert!(has("property invented is never set"), "{out:#?}");
        assert!(has("event navUp has no signals"), "{out:#?}");
        assert!(has("event anim_complete has signals; no stock"), "{out:#?}");
        assert!(has("event mystery is never declared"), "{out:#?}");
        assert!(has("no stock signal reaches pin explode on a UIGP"), "{out:#?}");
        assert!(has("node type UICX is not in any stock .ui"), "{out:#?}");
        assert_eq!(out.len(), 8, "{out:#?}");
    }

    #[test]
    fn layout_paths_through_a_mask_are_flagged() {
        let mut scenes = Scenes::default();
        let row = el("row", "eXRef", vec![], Some(2));
        scenes
            .add(&scene(1, vec![el("row_mask", "eMaskTexture", vec![row], None)]))
            .unwrap();
        scenes
            .add(&scene(2, vec![el("item_spawner", "eNull", vec![], None)]))
            .unwrap();
        let direct = node("a", "UIGP", vec![], vec![], &["row"], &[]);
        let nested = node("b", "UIGP", vec![], vec![], &["row", "item_spawner"], &[]);
        let masked = node("c", "UIGP", vec![], vec![], &["row_mask", "row"], &[]);
        let out = against_stock(&template(vec![direct, nested, masked], 1), &cat(), &scenes).unwrap();
        assert_eq!(out.len(), 1, "{out:#?}");
        assert!(
            out[0]
                .to_string()
                .starts_with("tpl.c [UIGP]: layout row_mask>row walks eMaskTexture>eXRef"),
            "{out:#?}"
        );
    }

    #[test]
    fn scenes_are_checked_for_parenting_and_params() {
        let mut xref = el("row", "eXRef", vec![], Some(2));
        xref.as_obj_mut().unwrap().set(
            "params",
            arr(vec![obj(vec![("name", s("fileRef")), ("value", u32v(3))])]),
        );
        let data = scene(
            1,
            vec![
                el("m", "eMask", vec![xref], None),
                el("w", "eWiggle", vec![], None),
            ],
        );
        let out = scene_against_stock(&data, &cat()).unwrap();
        let has = |s: &str| out.iter().any(|f| f.to_string().contains(s));
        assert!(
            has("m [eMask]: stock never puts a eMask under a eScene"),
            "{out:#?}"
        );
        assert!(
            has("m.row [eXRef]: stock never puts a eXRef under a eMask"),
            "{out:#?}"
        );
        assert!(has("param fileRef is ui32; stock eXRef uses stri"), "{out:#?}");
        assert!(has("element type eWiggle is not in any stock scene"), "{out:#?}");
    }

    #[test]
    fn the_stock_catalogue_knows_the_row_navigation_pins() {
        let c = super::super::catalog::stock();
        let f1ts = &c.node_types["F1TS"];
        for pin in ["focus", "inputAllow", "inputDisallow", "focus_and_reset"] {
            assert!(f1ts.pins_in.contains_key(pin), "{pin}");
        }
        assert!(c.node_types["UIGP"].commands.contains_key("block_input"));
        assert!(c.scene_elements["eMaskTexture"].children.contains_key("eXRef"));
    }

    fn engine() -> Engine {
        let rule = |id: &str, code: u64| serde_json::json!({ "id": id, "level": "L2", "check": "", "fail": { "code": code, "scope": "node" }, "fn": format!("0x14000{code:04x} {id}") });
        let with = |mut r: serde_json::Value, applies: serde_json::Value, props: serde_json::Value| {
            r["applies_to"] = applies;
            r["props"] = props;
            r
        };
        serde_json::from_value(serde_json::json!({
            "schema": 1,
            "rules": [
                rule("node.record", 5), rule("node.class_registered", 5), rule("layout.path_nonempty", 34),
                rule("layout.step_keys", 3), rule("components.keys", 3), rule("behaviours.type", 3),
                rule("commands.entry", 3), rule("events.entry", 3), rule("signals.entry", 3),
                with(rule("listbase.data_binding", 5), serde_json::json!({ "classes": ["UIL2"] }), serde_json::json!([{ "key": "data", "required": true, "shape": "binding" }])),
                with(rule("svcd.value", 3), serde_json::json!({ "classes": ["SVCD"] }), serde_json::json!([{ "key": "value", "required": true }])),
                with(rule("f1ts.input_type", 87), serde_json::json!({ "classes": ["F1TS"] }), serde_json::json!([{ "key": "inputType", "shape": "value", "values": ["normal", "dpadOnly"], "case_insensitive": true }])),
                with(rule("behaviour.nbvn_visualnav", 3), serde_json::json!({ "behaviours": ["NBVN"] }), serde_json::json!([{ "key": "visualNav", "required": true, "shape": "object" }])),
                rule("template.duplicate", 88), rule("screen.duplicate", 85)
            ],
            "classes": {
                "UIGP": { "init_calls_element_base": true },
                "F1TS": { "init_calls_element_base": true },
                "SVCD": { "init_calls_element_base": true },
                "UIL2": { "init_calls_element_base": true, "init_calls_list_base": true }
            }
        }))
        .unwrap()
    }

    #[test]
    fn a_visual_nav_behaviour_needs_its_visual_nav_object() {
        let root = |props: Vec<(&str, Node)>| {
            let mut r = node("tpl", "UIGP", props, vec![], &[], &["NBEL", "NBVN"]);
            r.a_mut().unwrap()[COMPONENTS] = arr(vec![obj(vec![
                ("componentId", u64v(1)),
                ("debugName", s("eaid://ui_package/art/x")),
                ("name", s("x")),
            ])]);
            fix_ids(&mut r, None);
            pkg(obj(vec![("Templates", arr(vec![r]))]))
        };
        let nav = obj(vec![
            ("xOffset", prop(f32v(0.0), None)),
            ("yOffset", prop(f32v(0.0), None)),
        ]);
        let missing = root(vec![]);
        let report = ui_edit(&input(&missing, None), &Scenes::default(), &cat(), &engine()).unwrap();
        assert_eq!(report.hard.len(), 1, "{report:#?}");
        assert_eq!(report.hard[0].code, "behaviour.nbvn_visualnav");
        assert!(
            report.hard[0]
                .message
                .contains("UIGP with NBVN: visualNav is missing"),
            "{report:#?}"
        );
        let scalar = root(vec![("visualNav", u32v(0))]);
        let report = ui_edit(&input(&scalar, None), &Scenes::default(), &cat(), &engine()).unwrap();
        assert!(
            report
                .hard
                .iter()
                .any(|h| h.message.contains("visualNav is not an object")),
            "{report:#?}"
        );
        let fine = root(vec![("visualNav", nav)]);
        let report = ui_edit(&input(&fine, None), &Scenes::default(), &cat(), &engine()).unwrap();
        assert!(report.hard.is_empty(), "{report:#?}");
    }

    fn input<'a>(data: &'a [u8], stock: Option<&'a [u8]>) -> UiInput<'a> {
        UiInput {
            resource: "eaid://ui_package/screens/x.ui",
            frag: "syst",
            data,
            stock,
        }
    }

    #[test]
    fn engine_rules_reject_what_the_loader_rejects() {
        let mut row = node("row", "UIGP", vec![], vec![], &[], &[]);
        row.a_mut().unwrap()[EVENTS] = arr(vec![obj(vec![
            ("eventName", s("navUp")),
            ("event", u32v(djb2("navUp"))),
        ])]);
        row.a_mut().unwrap()[LAYOUT] = arr(vec![arr(vec![])]);
        let spawner = node(
            "sp",
            "F1TS",
            vec![("inputType", prop(s("stick"), None))],
            vec![],
            &["a"],
            &[],
        );
        let query = node("q", "SVCD", vec![], vec![], &[], &[]);
        let list = node("l", "UIL2", vec![("data", prop(s("x"), None))], vec![], &[], &[]);
        let alien = node("z", "UIZZ", vec![], vec![], &[], &[]);
        let mut bad_step = node("b", "UIGP", vec![], vec![], &[], &[]);
        bad_step.a_mut().unwrap()[LAYOUT] = arr(vec![arr(vec![obj(vec![("debugName", s("b"))])])]);
        let tpl = template_node(vec![row, spawner, query, list, alien, bad_step], 1);
        let mut other = template_node(vec![], 2);
        other.props_mut().unwrap().set("name", s("tile"));
        let mut cat = cat();
        cat.root_names.insert(
            "Templates".into(),
            BTreeMap::from([(
                "tile".to_owned(),
                vec!["eaid://ui_package/screens/y.ui".to_owned()],
            )]),
        );
        let data = pkg(obj(vec![("Templates", arr(vec![tpl.clone(), tpl, other]))]));
        let report = ui_edit(&input(&data, None), &Scenes::default(), &cat, &engine()).unwrap();
        let ids: BTreeSet<&str> = report.hard.iter().map(|h| h.code.as_str()).collect();
        for id in [
            "events.entry",
            "layout.path_nonempty",
            "layout.step_keys",
            "f1ts.input_type",
            "svcd.value",
            "listbase.data_binding",
            "node.class_registered",
            "template.duplicate",
        ] {
            assert!(ids.contains(id), "{id} missing: {report:#?}");
        }
        let dup: Vec<&Finding> = report
            .hard
            .iter()
            .filter(|h| h.code == "template.duplicate")
            .collect();
        assert_eq!(dup.len(), 2, "{dup:#?}");
        assert!(dup.iter().any(|h| h.message.contains("y.ui")), "{dup:#?}");
        let f1ts = report.hard.iter().find(|h| h.code == "f1ts.input_type").unwrap();
        assert!(
            f1ts.source.starts_with("f1ts.input_type L2: rc 0x57"),
            "{f1ts:#?}"
        );
        assert!(f1ts.source.contains("0x140000057"), "{f1ts:#?}");
        let stock = pkg(obj(vec![("Templates", arr(vec![template_node(vec![], 1)]))]));
        let clean = ui_edit(&input(&stock, Some(&stock)), &Scenes::default(), &cat, &engine()).unwrap();
        assert!(clean.hard.is_empty() && clean.warnings.is_empty(), "{clean:#?}");
    }

    #[test]
    fn the_engine_rules_file_loads() {
        let e = super::super::catalog::engine();
        assert!(e.rules.len() >= 18);
        assert_eq!(e.classes.len(), 136);
        for id in [
            "node.record",
            "events.entry",
            "signals.entry",
            "template.duplicate",
            "f1ts.input_type",
        ] {
            assert!(e.rule(id).is_some(), "{id}");
        }
        assert!(e.classes["F1TS"].native_pins.iter().any(|p| p == "inputAllow"));
        for id in [
            "behaviour.nbvn_visualnav",
            "svcd.value",
            "f1ts.input_type",
            "listbase.data_binding",
        ] {
            let r = e.rule(id).unwrap();
            assert!(!r.props.is_empty(), "{id}");
            assert!(
                !r.applies_to.classes.is_empty() || !r.applies_to.behaviours.is_empty(),
                "{id}"
            );
        }
    }

    fn scene_at(path: &str, kids: Vec<Node>) -> Vec<u8> {
        let mut entry = obj(vec![
            ("scene", el("Scene Root", "eScene", kids, None)),
            ("name", u64v(idf::scene_id(path))),
            ("nodeidlookup", arr(vec![])),
        ]);
        idf::relookup(&mut entry).unwrap();
        let mut p = Pkg::parse(&pkg(obj(vec![("scenelist", arr(vec![entry]))]))).unwrap();
        for e in idf::scenes_mut(&mut p).unwrap() {
            let root = e.o_mut().unwrap().at_mut("scene").unwrap();
            for c in idf::children_mut(root).unwrap() {
                let n = idf::elem_name(c).to_owned();
                idf::recompute_ids(c, &n).unwrap();
            }
        }
        p.to_bytes().unwrap()
    }

    #[test]
    fn scene_edits_are_checked_against_the_ui_that_binds_them() {
        let path = "eaid://ui_package/art/x.idf";
        let id = idf::scene_id(path);
        let ui = "eaid://ui_package/screens/x.ui";
        let tpl = |kids: Vec<Node>| pkg(obj(vec![("Templates", arr(vec![template_node(kids, id)]))]));
        let bound = || node("row", "UIGP", vec![], vec![], &["row"], &[]);
        let stock_scene = scene_at(path, vec![el("row", "eNull", vec![], None)]);
        let archive = |name: &str, scene: &[u8], ui_data: &[u8]| ArchiveUi {
            archive: name.into(),
            resources: BTreeMap::from([
                (path.to_owned(), ("UILayout".to_owned(), vec![scene.to_vec()])),
                (ui.to_owned(), ("UIDF".to_owned(), vec![ui_data.to_vec()])),
            ]),
        };
        let stock_ui = tpl(vec![bound()]);
        let stock = [
            archive("a.erp", &stock_scene, &stock_ui),
            archive("b.erp", &stock_scene, &stock_ui),
        ];
        let mut cat = cat();
        cat.scene_ids = vec![format!("{id:016x}")];
        let engine = engine();

        let renamed = scene_at(path, vec![el("row_2", "eNull", vec![], None)]);
        let new_ui = tpl(vec![
            bound(),
            node("extra", "UIGP", vec![], vec![], &["row"], &[]),
        ]);
        let edited = [
            archive("a.erp", &renamed, &new_ui),
            archive("b.erp", &stock_scene, &stock_ui),
        ];
        let report = archives(&edited, &stock, &cat, &engine).unwrap();
        let has = |code: &str, at: &str| {
            report
                .hard
                .iter()
                .chain(&report.warnings)
                .any(|f| f.code == code && f.to_string().contains(at))
        };
        assert!(
            has(
                "layout-node-unknown",
                "a.erp: eaid://ui_package/screens/x.ui ui#0: tpl.extra"
            ),
            "{report:#?}"
        );
        assert!(has("layout-node-missing", "tpl.row"), "{report:#?}");
        assert!(has("copies-differ", "a.erp and b.erp"), "{report:#?}");
        assert!(report.hard.iter().all(|h| h.level == Level::Hard));
        assert!(
            report
                .hard
                .iter()
                .any(|h| h.code == "layout-node-missing" && h.at.contains("tpl.row")),
            "{report:#?}"
        );

        let mut stale = Pkg::parse(&stock_scene).unwrap();
        let entry = &mut idf::scenes_mut(&mut stale).unwrap()[0];
        let root = entry.o_mut().unwrap().at_mut("scene").unwrap();
        idf::children_mut(root)
            .unwrap()
            .push(el("row_3", "eNull", vec![], None));
        entry.o_mut().unwrap().set("name", u64v(7));
        let stale = stale.to_bytes().unwrap();
        let edited = [
            archive("a.erp", &stale, &stock_ui),
            archive("b.erp", &stale, &stock_ui),
        ];
        let report = archives(&edited, &stock, &cat, &engine).unwrap();
        let codes: BTreeSet<&str> = report.hard.iter().map(|h| h.code.as_str()).collect();
        assert!(
            codes.contains("nodeidlookup") && codes.contains("scene-id"),
            "{report:#?}"
        );

        let report = archives(&stock, &stock, &cat, &engine).unwrap();
        assert!(
            report.hard.is_empty() && report.warnings.is_empty(),
            "{report:#?}"
        );
    }

    #[test]
    fn bindings_to_view_model_keys_stock_never_binds_are_flagged() {
        let spawner = |name: &str, key: &str| {
            node(
                name,
                "F1TS",
                vec![("dataContext", prop(s(""), Some(key)))],
                vec![],
                &[],
                &[],
            )
        };
        let stock = template(vec![spawner("a", "tiles")], 1);
        let query = node(
            "has",
            "SVCD",
            vec![("dbEntry", prop(s("addon_tiles_visible"), None))],
            vec![],
            &[],
            &[],
        );
        let global = node(
            "g",
            "UILB",
            vec![("text", prop(s(""), Some("#hud.title")))],
            vec![],
            &[],
            &[],
        );
        let edited = template(
            vec![
                spawner("a", "tiles"),
                spawner("b", "addon_tiles.list"),
                query,
                global,
            ],
            1,
        );
        let found = runtime_bindings(&stock, &edited, "syst").unwrap();
        let keys: Vec<&str> = found.iter().map(|f| f.name.as_str()).collect();
        assert_eq!(keys, ["addon_tiles", "addon_tiles_visible"], "{found:#?}");
        assert!(
            found
                .iter()
                .all(|f| f.level == Level::Warn && f.code == "vm-key-new")
        );
    }

    #[test]
    fn broken_hashes_are_hard_and_signals_leaving_the_file_are_not() {
        let row = node(
            "row",
            "UIGP",
            vec![],
            vec![event("navUp", vec![sig("elsewhere.node", "focus", "navUp")])],
            &[],
            &[],
        );
        let mut root = template_node(vec![row], 1);
        root.find_mut("row")
            .unwrap()
            .props_mut()
            .unwrap()
            .set("nameId", u32v(1));
        let data = pkg(obj(vec![("Templates", arr(vec![root]))]));
        let report = ui_edit(&input(&data, None), &Scenes::default(), &cat(), &engine()).unwrap();
        assert_eq!(report.hard.len(), 1, "{report:#?}");
        assert!(
            report.hard[0].to_string().contains("tpl.row: nameId"),
            "{report:#?}"
        );
    }
}
