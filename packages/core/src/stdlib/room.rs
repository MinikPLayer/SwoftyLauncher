//! The safety net for the game database's room (W7-SIZING.md 5.4,
//! docs/std/build.md "Database room"). The core reports how full the value
//! map was after each boot in `room.json`. When it couldn't size the map
//! before the load (`presize: unavailable`) and the older addons' new keys
//! plus the 1.6 records asked for don't fit the room the stock-sized map
//! leaves, the next build puts the older addons' database edits on the file
//! route (1.5.11) and pads the file with inert records past 838,860, so the
//! game sizes its map for the file (2^21 slots) before it loads it.
//!
//! The decision counts the older edits' new keys the build itself makes, not
//! the report's, so it stays the same on the boot after the fallback (when
//! the core has no older edits to count). A report without registry records
//! never triggers it, so a profile of older addons builds as on 1.5.

use swofty_formats::gvdb::{Gvdb, Record};

use super::reports::Room;
use crate::runtime::{RUNTIME_RECORD_MARGIN, runtime_record_room};

/// The records a stock-sized map (2^20 slots) holds before it grows.
pub const STOCK_GROWS_AT: usize = 838_860;
/// The smallest file the game sizes 2^21 slots for.
pub const PAD_2_21: usize = STOCK_GROWS_AT + 1;
/// The smallest file the game sizes 2^22 slots for. Unproven in game (W7's T129).
pub const PAD_2_22: usize = 1_677_722;
/// Whether the net may pad past 2^21 slots; off until a 2^22 reservation is proven to boot.
pub const ALLOW_2_22: bool = false;

const PAD_PATH: &str = "swofty_room_pad";

/// The 1.6 records the report asks room for, when it says the core couldn't size the map.
pub fn asked(room: Option<&Room>, profile: &str, order: &[String]) -> Option<usize> {
    let r = room?;
    (r.presize_unavailable() && r.registry.asked_keys > 0 && r.is_for(profile, order))
        .then_some(r.registry.asked_keys as usize)
}

/// What the build does with the database.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Route {
    /// The older addons' edits go to the core at runtime, as on 1.5.
    Runtime,
    /// The file route, padded to `pad_to` records; `short` is how many 1.6 records still don't fit.
    File { pad_to: usize, short: usize },
}

/// The room a file of `records` leaves for records added after the load.
pub fn room_after(records: usize) -> usize {
    runtime_record_room(records)
}

/// Where a build whose runtime route loads `loaded` records, whose older edits add `older_new`
/// keys, puts them when the core's last report asked room for `asked` 1.6 records.
pub fn route(loaded: usize, older_new: usize, asked: Option<usize>) -> Route {
    let Some(asked) = asked else {
        return Route::Runtime;
    };
    if older_new + asked <= runtime_record_room(loaded) {
        return Route::Runtime;
    }
    file_route(loaded + older_new, asked)
}

/// The padding for a database file of `records` records that has to leave room for `asked` more.
pub fn file_route(records: usize, asked: usize) -> Route {
    if asked <= runtime_record_room(records) {
        return Route::File {
            pad_to: records,
            short: 0,
        };
    }
    let mut pad_to = records.max(PAD_2_21);
    if asked > runtime_record_room(pad_to) && ALLOW_2_22 {
        pad_to = records.max(PAD_2_22);
    }
    Route::File {
        pad_to,
        short: asked.saturating_sub(runtime_record_room(pad_to)),
    }
}

/// Add inert records to `db` until it holds `to`: int records under keys no game code reads,
/// skipping any key it already holds. Returns how many it added.
pub fn pad(db: &mut Gvdb, to: usize) -> usize {
    if db.records.len() >= to {
        return 0;
    }
    let mut taken: std::collections::HashSet<u64> = db.records.iter().map(|r| r.key).collect();
    let mut added = 0;
    let mut i = 0u64;
    while db.records.len() < to {
        let r = Record::new(&format!("{PAD_PATH}._pad_{i}_"), 0, "0");
        i += 1;
        if taken.insert(r.key) {
            db.records.push(r);
            added += 1;
        }
    }
    added
}

/// The build line for a padded file.
pub fn line(pad_to: usize, added: usize, asked: usize, short: usize) -> String {
    let room = runtime_record_room(pad_to);
    let mut out = format!(
        "database: the last boot couldn't size the game database before it loaded, and its 1.6 records ({asked}) \
         don't fit next to the older addons' edits; the edits go into the game file, padded with {added} inert \
         record(s) to {pad_to}, so the game sizes it for {} records ({room} left for 1.6 records, {} kept spare)",
        room + pad_to + RUNTIME_RECORD_MARGIN,
        RUNTIME_RECORD_MARGIN
    );
    if short > 0 {
        out.push_str(&format!(
            "; {short} 1.6 record(s) still don't fit and the core refuses them"
        ));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::stdlib::reports::Room;

    const STOCK: usize = 742_857;

    fn report(presize: &str, asked: u64) -> Room {
        Room::parse(&format!(
            r#"{{ "presize": "{presize}", "registry": {{ "asked_keys": {asked} }} }}"#
        ))
        .unwrap()
    }

    #[test]
    fn only_an_unsized_boot_with_registry_records_asks() {
        assert_eq!(asked(None, "p", &[]), None);
        assert_eq!(asked(Some(&report("applied", 50_000)), "p", &[]), None);
        assert_eq!(asked(Some(&report("not_needed", 50_000)), "p", &[]), None);
        assert_eq!(asked(Some(&report("unavailable", 0)), "p", &[]), None);
        assert_eq!(
            asked(Some(&report("unavailable", 50_000)), "p", &[]),
            Some(50_000)
        );
        let mut other = report("unavailable", 50_000);
        other.profile = Some("q".into());
        assert_eq!(asked(Some(&other), "p", &[]), None);
    }

    #[test]
    fn what_fits_stays_on_the_runtime_route() {
        assert_eq!(route(STOCK, 80_000, None), Route::Runtime);
        let room = runtime_record_room(STOCK);
        assert_eq!(room, 838_860 - STOCK - 4096);
        assert_eq!(route(STOCK, 80_000, Some(room - 80_000)), Route::Runtime);
    }

    #[test]
    fn too_much_moves_the_older_edits_to_a_padded_file() {
        let room = runtime_record_room(STOCK);
        assert_eq!(
            route(STOCK, 80_000, Some(room - 80_000 + 1)),
            Route::File {
                pad_to: PAD_2_21,
                short: 0
            }
        );
        assert_eq!(
            route(STOCK, 84_112, Some(15_020)),
            Route::File {
                pad_to: PAD_2_21,
                short: 0
            }
        );
        assert_eq!(runtime_record_room(PAD_2_21), 1_677_721 - PAD_2_21 - 4096);
    }

    #[test]
    fn a_file_already_past_the_stock_size_isnt_padded_further() {
        assert_eq!(
            file_route(900_000, 10_000),
            Route::File {
                pad_to: 900_000,
                short: 0
            }
        );
    }

    #[test]
    fn records_past_2_21_are_short_until_2_22_is_proven() {
        let too_many = runtime_record_room(PAD_2_21) + 5;
        assert_eq!(
            route(STOCK, 1, Some(too_many)),
            Route::File {
                pad_to: PAD_2_21,
                short: 5
            }
        );
    }

    #[test]
    fn padding_adds_unique_int_records_up_to_the_target() {
        let mut db = Gvdb {
            records: vec![
                Record::new("a.b", 0, "1"),
                Record::new(&format!("{PAD_PATH}._pad_0_"), 0, "7"),
            ],
        };
        assert_eq!(pad(&mut db, 6), 4);
        assert_eq!(db.records.len(), 6);
        let keys: std::collections::HashSet<u64> = db.records.iter().map(|r| r.key).collect();
        assert_eq!(keys.len(), 6);
        assert!(db.records[2..].iter().all(|r| r.tag == 0 && r.value == b"0"));
        assert_eq!(pad(&mut db, 3), 0);
        let again = Gvdb::parse(&db.to_bytes()).unwrap();
        assert_eq!(again.records.len(), 6);
    }

    #[test]
    fn the_line_says_what_the_game_sizes_for() {
        let l = line(PAD_2_21, 95_000, 15_020, 0);
        assert!(l.contains("padded with 95000 inert record(s) to 838861"), "{l}");
        assert!(l.contains("sizes it for 1677721 records"), "{l}");
        assert!(
            line(PAD_2_21, 1, 2, 3).ends_with("3 1.6 record(s) still don't fit and the core refuses them")
        );
    }
}
