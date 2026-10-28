use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};

use swofty_formats::gvdb::{Gvdb, Record, instance_count_path};
use swofty_formats::hash::fnv1a64;

use crate::ops::{DbOp, DbOps, record_key, row_keys};
use crate::rowkeys::{Names, RowKeys};

const TABLE: &str = "entrants.entrant_lists";
const ROW: &str = "entrant_list";
const SCOPES: [&str; 3] = [TABLE, "entrants.entrant_setup_modes", "content_sets"];
const MAX_ROWS: usize = 256;

struct List {
    name: String,
    keys: Vec<(String, u8, Vec<u8>)>,
}

/// The game's entrant lists and the fields that name them, from the stock database.
pub struct StockLists {
    lists: Vec<List>,
    refs: Vec<String>,
    watched: HashSet<u64>,
}

/// The entrant lists before one addon's database ops.
pub struct Before {
    count: usize,
    names: HashSet<String>,
    refs: Vec<Option<Vec<String>>>,
}

fn index(db: &Gvdb) -> HashMap<u64, usize> {
    let mut at = HashMap::with_capacity(db.records.len());
    for (i, r) in db.records.iter().enumerate() {
        at.entry(r.key).or_insert(i);
    }
    at
}

fn text(db: &Gvdb, at: &HashMap<u64, usize>, path: &str) -> Option<String> {
    at.get(&fnv1a64(path)).map(|&i| db.records[i].text().into_owned())
}

fn count(db: &Gvdb, at: &HashMap<u64, usize>, container: &str, row: &str) -> usize {
    text(db, at, &instance_count_path(container, row))
        .and_then(|t| t.trim().parse().ok())
        .unwrap_or(0)
}

fn name(db: &Gvdb, at: &HashMap<u64, usize>, i: usize) -> Option<String> {
    text(db, at, &format!("{TABLE}.{ROW}_{i}._metadata_"))
}

fn items(value: &str) -> Vec<String> {
    value
        .split(',')
        .map(str::trim)
        .filter(|v| !v.is_empty())
        .map(String::from)
        .collect()
}

fn put(db: &mut Gvdb, at: &mut HashMap<u64, usize>, key: u64, tag: u8, value: Vec<u8>) {
    match at.get(&key) {
        Some(&i) => {
            db.records[i].tag = tag;
            db.records[i].value = value;
        }
        None => {
            at.insert(key, db.records.len());
            db.records.push(Record { key, tag, value });
        }
    }
}

fn in_scope(path: &str) -> bool {
    SCOPES.iter().any(|s| path.starts_with(s))
}

impl StockLists {
    pub fn new(stock: &Gvdb) -> Self {
        let at = index(stock);
        let rows: Vec<String> = (0..count(stock, &at, TABLE, ROW))
            .map(|i| format!("{TABLE}.{ROW}_{i}"))
            .collect();
        let fields = BTreeMap::new();
        let names = Names {
            fields: &fields,
            keys: RowKeys::builtin(),
            stock: None,
            tracks: None,
        };
        let keys = row_keys(&mut stock.clone(), &names, &rows);
        let lists = rows
            .iter()
            .zip(keys)
            .filter_map(|(row, keys)| {
                let name = text(stock, &at, &format!("{row}._metadata_"))?;
                let keys = keys
                    .into_iter()
                    .filter_map(|rel| {
                        let r = &stock.records[*at.get(&fnv1a64(format!("{row}.{rel}")))?];
                        Some((rel, r.tag, r.value.clone()))
                    })
                    .collect();
                Some(List { name, keys })
            })
            .collect();
        let mut refs = Vec::new();
        for i in 0..count(stock, &at, "content_sets", "content_set") {
            let set = format!("content_sets.content_set_{i}");
            for j in 0..count(stock, &at, &set, "available_entrant_lists") {
                refs.push(format!("{set}.available_entrant_lists_{j}.entrant_lists"));
            }
        }
        for i in 0..count(stock, &at, "entrants.entrant_setup_modes", "entrant_setup_mode") {
            for field in ["entrant_lists", "selectable_entrant_lists"] {
                refs.push(format!(
                    "entrants.entrant_setup_modes.entrant_setup_mode_{i}.{field}"
                ));
            }
        }
        refs.retain(|p| at.contains_key(&fnv1a64(p)));
        let mut watched: HashSet<u64> = refs.iter().map(fnv1a64).collect();
        watched.insert(fnv1a64(instance_count_path(TABLE, ROW)));
        for i in 0..MAX_ROWS {
            for field in ["_metadata_", "stat_id"] {
                watched.insert(fnv1a64(format!("{TABLE}.{ROW}_{i}.{field}")));
            }
        }
        Self { lists, refs, watched }
    }

    /// Whether `ops` can rename, remove or stop naming an entrant list.
    pub fn touched(&self, ops: &DbOps) -> bool {
        let key = |k: &str| match k.contains('[') {
            true => in_scope(k),
            false => record_key(k).is_ok_and(|k| self.watched.contains(&k)),
        };
        ops.ops.iter().any(|op| match op {
            DbOp::PutRecord { key: k, .. } | DbOp::RemoveRecord { key: k, .. } => key(k),
            DbOp::Set { select, .. } | DbOp::ListAppend { select, .. } | DbOp::ListRemove { select, .. } => {
                in_scope(select)
            }
            DbOp::AddRow { .. } | DbOp::RemoveRow { .. } => false,
        })
    }

    pub fn before(&self, db: &Gvdb) -> Before {
        let at = index(db);
        let count = count(db, &at, TABLE, ROW);
        Before {
            count,
            names: (0..count).filter_map(|i| name(db, &at, i)).collect(),
            refs: self
                .refs
                .iter()
                .map(|p| text(db, &at, p).map(|v| items(&v)))
                .collect(),
        }
    }

    /// Puts back the game's entrant lists one addon's ops renamed or removed,
    /// the rows a count it lowered hides and the list names it took out of
    /// content sets and entrant setup modes, and returns those lists.
    pub fn keep(&self, db: &mut Gvdb, before: &Before) -> Vec<String> {
        let mut at = index(db);
        let had = count(db, &at, TABLE, ROW);
        let shown: HashSet<String> = (0..had).filter_map(|i| name(db, &at, i)).collect();
        let mut n = had.max(before.count);
        let mut names: HashSet<String> = (0..n).filter_map(|i| name(db, &at, i)).collect();
        for list in &self.lists {
            if !before.names.contains(&list.name) || names.contains(&list.name) {
                continue;
            }
            for (rel, tag, value) in &list.keys {
                put(
                    db,
                    &mut at,
                    fnv1a64(format!("{TABLE}.{ROW}_{n}.{rel}")),
                    *tag,
                    value.clone(),
                );
            }
            n += 1;
            names.insert(list.name.clone());
        }
        if n != had
            && let Some(&i) = at.get(&fnv1a64(instance_count_path(TABLE, ROW)))
        {
            db.records[i].value = n.to_string().into_bytes();
        }
        let mut kept: BTreeSet<String> = before
            .names
            .iter()
            .filter(|l| !shown.contains(*l) && names.contains(*l))
            .cloned()
            .collect();
        for (path, was) in self.refs.iter().zip(&before.refs) {
            let (Some(was), Some(&i)) = (was, at.get(&fnv1a64(path))) else {
                continue;
            };
            let mut now = items(&db.records[i].text());
            let back: Vec<String> = was
                .iter()
                .filter(|l| !now.contains(l) && names.contains(*l))
                .cloned()
                .collect();
            if back.is_empty() {
                continue;
            }
            now.extend(back.iter().cloned());
            db.records[i].value = now.join(",").into_bytes();
            kept.extend(back);
        }
        kept.into_iter().collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rec(path: &str, tag: u8, value: &str) -> Record {
        Record::new(path, tag, value)
    }

    fn stock() -> Gvdb {
        let mut records = vec![
            rec("entrants.entrant_lists._entrant_list_instancecount_", 0, "3"),
            rec("content_sets._content_set_instancecount_", 0, "1"),
            rec(
                "content_sets.content_set_0._available_entrant_lists_instancecount_",
                0,
                "1",
            ),
            rec(
                "content_sets.content_set_0.available_entrant_lists_0.entrant_lists",
                13,
                "f1_2026,f2_2025,f2_2026",
            ),
            rec(
                "entrants.entrant_setup_modes._entrant_setup_mode_instancecount_",
                0,
                "1",
            ),
            rec(
                "entrants.entrant_setup_modes.entrant_setup_mode_0.entrant_lists",
                13,
                "f2_2025,f2_2026",
            ),
        ];
        for (i, (name, id)) in [("f1_2026", "67"), ("f2_2026", "104"), ("f2_2025", "95")]
            .into_iter()
            .enumerate()
        {
            let row = format!("entrants.entrant_lists.entrant_list_{i}");
            records.push(rec(&format!("{row}._metadata_"), 4, name));
            records.push(rec(&format!("{row}.stat_id"), 0, id));
            records.push(rec(&format!("{row}.available"), 4, "False"));
        }
        Gvdb { records }
    }

    fn get(db: &Gvdb, path: &str) -> Option<String> {
        db.get_text(path)
    }

    #[test]
    fn puts_back_a_list_an_older_database_renamed_and_drops_from_names() {
        let stock = stock();
        let lists = StockLists::new(&stock);
        let mut db = stock.clone();
        let before = lists.before(&db);
        db.set(
            "entrants.entrant_lists.entrant_list_1._metadata_",
            "f2_2025",
            None,
        )
        .unwrap();
        db.set("entrants.entrant_lists.entrant_list_1.stat_id", "95", None)
            .unwrap();
        db.set("entrants.entrant_lists._entrant_list_instancecount_", "2", None)
            .unwrap();
        db.set(
            "content_sets.content_set_0.available_entrant_lists_0.entrant_lists",
            "f1_2026,f2_2025",
            None,
        )
        .unwrap();
        db.set(
            "entrants.entrant_setup_modes.entrant_setup_mode_0.entrant_lists",
            "f2_2025",
            None,
        )
        .unwrap();
        assert_eq!(lists.keep(&mut db, &before), ["f2_2026"]);
        assert_eq!(
            get(&db, "entrants.entrant_lists._entrant_list_instancecount_").as_deref(),
            Some("4")
        );
        assert_eq!(
            get(&db, "entrants.entrant_lists.entrant_list_2._metadata_").as_deref(),
            Some("f2_2025")
        );
        assert_eq!(
            get(&db, "entrants.entrant_lists.entrant_list_3._metadata_").as_deref(),
            Some("f2_2026")
        );
        assert_eq!(
            get(&db, "entrants.entrant_lists.entrant_list_3.stat_id").as_deref(),
            Some("104")
        );
        assert_eq!(
            get(
                &db,
                "content_sets.content_set_0.available_entrant_lists_0.entrant_lists"
            )
            .as_deref(),
            Some("f1_2026,f2_2025,f2_2026")
        );
        assert_eq!(
            get(
                &db,
                "entrants.entrant_setup_modes.entrant_setup_mode_0.entrant_lists"
            )
            .as_deref(),
            Some("f2_2025,f2_2026")
        );
    }

    #[test]
    fn leaves_changes_that_keep_every_list() {
        let stock = stock();
        let lists = StockLists::new(&stock);
        let mut db = stock.clone();
        let before = lists.before(&db);
        db.set("entrants.entrant_lists.entrant_list_1.available", "True", None)
            .unwrap();
        db.set(
            "entrants.entrant_setup_modes.entrant_setup_mode_0.entrant_lists",
            "f2_2026,f2_2025,f1_2026",
            None,
        )
        .unwrap();
        let want = db.clone();
        assert!(lists.keep(&mut db, &before).is_empty());
        assert_eq!(db.to_bytes(), want.to_bytes());
    }
}
