use serde_json::{Value, json};

use crate::store::Entry;

pub enum Undo {
    Op(Value),
    Ops(Vec<Value>),
    BackupOnly,
    None(&'static str),
}

fn set_op(op: &str, value: &Value, expect: &Value) -> Undo {
    if value.is_null() {
        return Undo::None("the before value is missing");
    }
    let mut o = json!({ "op": op, "args": { "value": value } });
    if !expect.is_null() {
        o["expect"] = expect.clone();
    }
    Undo::Op(o)
}

// The game names a Driver Career's rounds by number ("0", "1", ...); such a name only comes back
// with the track id the plugin recorded beside it.
fn with_track(args: &mut Value, before: &Value, idx: usize) -> Result<(), &'static str> {
    if let Some(id) = before["tracks"][idx].as_u64().filter(|&id| id < 255) {
        args["verified"] = json!(true);
        args["track_stat_id"] = json!(id);
        return Ok(());
    }
    if args["weekend"].as_str().is_some_and(crate::calendar::index_name) {
        return Err("this round's track wasn't recorded; add it again from Add a round");
    }
    Ok(())
}

pub fn inverse(e: &Entry) -> Undo {
    if !e.ok {
        return Undo::None("the edit failed");
    }
    let b = &e.before;
    let a = &e.after;
    match e.op.as_str() {
        "economy.cash.set" | "economy.cash.add" => set_op("economy.cash.set", &b["cash"], &a["cash"]),
        "economy.rp.set" | "economy.rp.add" => set_op("economy.rp.set", &b["rp"], &a["rp"]),
        // An experimental My Team free agent signing undoes as one too; each driver's team after the
        // swap is found by id, since a replace lists the free agent first.
        "drivers.swap" if e.args["experimentalFreeAgent"] == true => {
            let (Some(x), Some(y)) = (e.args["a"].as_i64(), e.args["b"].as_i64()) else {
                return Undo::None("the drivers are missing");
            };
            let team = |id: i64| {
                [&a["a"], &a["b"]]
                    .into_iter()
                    .find(|d| d["driverStatId"].as_i64() == Some(id))
                    .map(|d| d["teamStatId"].clone())
            };
            let (Some(tx), Some(ty)) = (team(x), team(y)) else {
                return Undo::None("the seats after the swap are missing");
            };
            Undo::Op(
                json!({ "op": "drivers.swap", "args": { "a": x, "b": y, "experimentalFreeAgent": true,
                "expect": { "aTeamStatId": tx, "bTeamStatId": ty } } }),
            )
        }
        "drivers.ratings.set" => {
            let Some(id) = b["driverStatId"]
                .as_i64()
                .or_else(|| e.args["driverStatId"].as_i64())
            else {
                return Undo::None("the driver is missing");
            };
            let Some(before) = b["ratings"].as_object() else {
                return Undo::None("the before ratings are missing");
            };
            let mut ratings = serde_json::Map::new();
            let mut expect = serde_json::Map::new();
            for (k, v) in before {
                if let Some(val) = v["value"].as_f64() {
                    ratings.insert(k.clone(), json!(val));
                }
                if let Some(now) = a["ratings"][k]["value"].as_f64() {
                    expect.insert(k.clone(), json!(now));
                }
            }
            if ratings.is_empty() {
                return Undo::None("the before ratings are missing");
            }
            Undo::Op(json!({ "op": "drivers.ratings.set",
                              "args": { "driverStatId": id, "ratings": ratings, "mode": "restore", "expect": expect } }))
        }
        "drivers.number.set" => {
            let (Some(id), Some(now)) = (
                e.args["driverStatId"].as_i64(),
                a["driver"]["raceNumber"].as_i64(),
            ) else {
                return Undo::None("the number after the edit is missing");
            };
            let Some(old) = b["driver"]["raceNumber"].as_i64() else {
                return Undo::None("the driver had no number before");
            };
            let swapped = !a["swappedWith"].is_null();
            Undo::Op(json!({ "op": "drivers.number.set",
                "args": { "driverStatId": id, "number": old, "swap": swapped, "expect": now } }))
        }
        "rnd.fit" => {
            let fitted = e.args["fitted"].as_bool().unwrap_or(true);
            let mut args = e.args.clone();
            args["fitted"] = json!(!fitted);
            Undo::Op(json!({ "op": "rnd.fit", "args": args }))
        }
        "season.calendar.reorder" => {
            let n = e.args["order"].as_array().map_or(0, Vec::len);
            let Some(rounds) = b["rounds"].as_array() else {
                return Undo::None("the before calendar is missing");
            };
            if n == 0 || n > rounds.len() {
                return Undo::None("the before calendar is missing");
            }
            Undo::Op(
                json!({ "op": "season.calendar.reorder", "args": { "order": rounds[rounds.len() - n..] } }),
            )
        }
        "weather.plan.set" | "weather.plan.clear" => {
            let mut sessions = b["sessions"].clone();
            if !sessions.is_object() {
                return Undo::None("the before plan is missing");
            }
            if let Some(m) = sessions.as_object_mut() {
                for (k, v) in m.iter_mut() {
                    if v.is_object()
                        && let Some(o) = v.as_object_mut()
                    {
                        o.retain(|key, _| {
                            matches!(
                                key.as_str(),
                                "laps" | "minutes" | "start" | "timeline" | "air_temp" | "track_temp"
                            )
                        });
                    }
                    let _ = k;
                }
            }
            Undo::Op(
                json!({ "op": "weather.plan.set", "args": { "weekend": b["weekend"], "sessions": sessions } }),
            )
        }
        "weather.seed.lock" | "weather.seed.reroll" | "weather.seed.unlock" => {
            if b["locked"].as_bool() == Some(true) {
                Undo::Op(json!({ "op": "weather.seed.lock", "args": { "seed": b["seed"] } }))
            } else {
                Undo::Op(json!({ "op": "weather.seed.unlock", "args": {} }))
            }
        }
        "economy.renown.set" | "economy.renown.add" => {
            set_op("economy.renown.set", &b["renown"], &a["renown"])
        }
        "drivers.swap" => {
            let (Some(x), Some(y)) = (e.args["a"].as_i64(), e.args["b"].as_i64()) else {
                return Undo::None("the drivers are missing");
            };
            // Up to 0.2.5 a Driver Career replace listed the free agent as `a` whatever the args said,
            // so each driver's team after the swap is found by id.
            let team_after = |id: i64| {
                [&a["a"], &a["b"]]
                    .into_iter()
                    .find(|d| d["driverStatId"].as_i64() == Some(id))
                    .map(|d| d["teamStatId"].clone())
            };
            let (Some(tx), Some(ty)) = (team_after(x), team_after(y)) else {
                return Undo::None("the seats after the swap are missing");
            };
            let mut args = json!({ "a": x, "b": y, "expect": { "aTeamStatId": tx, "bTeamStatId": ty } });
            // A free agent the swap gave his first number goes back to none when the undo releases him.
            let side = |v: &Value, id: i64| {
                [&v["a"], &v["b"]]
                    .into_iter()
                    .find(|d| d["driverStatId"].as_i64() == Some(id))
                    .cloned()
                    .unwrap_or(Value::Null)
            };
            let clear: Vec<i64> = [x, y]
                .into_iter()
                .filter(|&id| {
                    let (was, now) = (side(b, id), side(a, id));
                    was["teamStatId"].is_null() && was["raceNumber"].is_null() && now["raceNumber"].is_i64()
                })
                .collect();
            if !clear.is_empty() {
                args["clearNumbers"] = json!(clear);
            }
            Undo::Op(json!({ "op": "drivers.swap", "args": args }))
        }
        "drivers.contract.set" => {
            let mut args = json!({ "driverStatId": e.args["driverStatId"], "list": e.args["list"].as_str().unwrap_or("decided") });
            if !e.args["newTeamStatId"].is_null() {
                args["newTeamStatId"] = e.args["newTeamStatId"].clone();
            }
            let mut expect = serde_json::Map::new();
            for f in ["salary", "signingBonus", "teamPerfMoney", "teamPerfRaceValue"] {
                if e.args.get(f).is_some() && !b[f].is_null() {
                    args[f] = b[f].clone();
                    expect.insert(f.to_owned(), a[f].clone());
                }
            }
            if expect.is_empty() {
                return Undo::None("the before contract is missing");
            }
            args["expect"] = Value::Object(expect);
            Undo::Op(json!({ "op": "drivers.contract.set", "args": args }))
        }
        "season.points.adjust" => match (e.args["driver"].as_i64(), e.args["delta"].as_i64()) {
            (Some(d), Some(delta)) => {
                Undo::Op(json!({ "op": "season.points.adjust", "args": { "driver": d, "delta": -delta } }))
            }
            _ => Undo::None("the adjustment is missing"),
        },
        "season.points.clear" => {
            let ops: Vec<Value> = b["adjustments"]
                .as_array()
                .into_iter()
                .flatten()
                .filter(|x| x["points"].as_i64().unwrap_or(0) != 0)
                .map(|x| json!({ "op": "season.points.adjust", "args": { "driver": x["driver_stat_id"], "delta": x["points"] } }))
                .collect();
            if ops.is_empty() {
                Undo::None("nothing was cleared")
            } else {
                Undo::Ops(ops)
            }
        }
        "season.calendar.set_round" => {
            let r = e.args["round"].as_u64().unwrap_or(0) as usize;
            match b["rounds"].get(r.wrapping_sub(1)) {
                Some(w) if r > 0 => {
                    let mut args = json!({ "round": r, "weekend": w });
                    match with_track(&mut args, b, r - 1) {
                        Ok(()) => Undo::Op(json!({ "op": "season.calendar.set_round", "args": args })),
                        Err(why) => Undo::None(why),
                    }
                }
                _ => Undo::None("the before calendar is missing"),
            }
        }
        "season.calendar.add_round" => {
            let n = a["rounds"].as_array().map_or(0, Vec::len);
            let at = e.args["at"].as_u64().map_or(n, |v| v as usize);
            if at == 0 {
                return Undo::None("the new round is unknown");
            }
            Undo::Op(json!({ "op": "season.calendar.remove_round", "args": { "round": at } }))
        }
        "season.calendar.remove_round" => {
            let r = e.args["round"].as_u64().unwrap_or(0) as usize;
            match b["rounds"].get(r.wrapping_sub(1)) {
                Some(w) if r > 0 => {
                    let mut args = json!({ "weekend": w, "at": r });
                    match with_track(&mut args, b, r - 1) {
                        Ok(()) => Undo::Op(json!({ "op": "season.calendar.add_round", "args": args })),
                        Err(why) => Undo::None(why),
                    }
                }
                _ => Undo::None("the before calendar is missing"),
            }
        }
        "rnd.complete"
        | "rnd.unlock"
        | "rnd.reset"
        | "rnd.refund"
        | "facilities.complete"
        | "facilities.set_level"
        | "economy.sponsor.loyalty.add"
        | "economy.costcap.refund"
        | "drivers.contracts.repair"
        | "season.calendar.repair"
        | "drivers.freeAgent.add" => Undo::BackupOnly,
        "bridge.ping" => Undo::None("nothing to undo"),
        _ => Undo::None("no undo is defined for this edit"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_experimental_free_agent_signing_undoes_as_one() {
        let mut e = entry(
            "drivers.swap",
            json!({ "a": 150, "b": 174, "experimentalFreeAgent": true, "expect": { "aTeamStatId": 2, "bTeamStatId": null } }),
            Value::Null,
        );
        e.after = json!({ "a": { "driverStatId": 174, "teamStatId": 2 }, "b": { "driverStatId": 150, "teamStatId": null } });
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert_eq!(
            op["args"],
            json!({ "a": 150, "b": 174, "experimentalFreeAgent": true, "expect": { "aTeamStatId": null, "bTeamStatId": 2 } })
        );
    }

    fn entry(op: &str, args: Value, before: Value) -> Entry {
        Entry {
            seq: 1,
            i: 0,
            t: 0,
            label: String::new(),
            op: op.into(),
            args,
            ok: true,
            error: None,
            before,
            after: Value::Null,
            undo_of: None,
            career: Some("dc-00d00000".into()),
            mode: "driver_career".into(),
        }
    }

    #[test]
    fn a_driver_career_replace_undoes_with_each_drivers_team() {
        // dar1us: Tsolov (174, free agent) into Hadjar's (149) VCARB seat; 0.2.5 reported the free agent as `a`
        let mut e = entry(
            "drivers.swap",
            json!({ "a": 149, "b": 174, "expect": { "aTeamStatId": 482, "bTeamStatId": null } }),
            Value::Null,
        );
        e.after = json!({ "a": { "driverStatId": 174, "teamStatId": 482 }, "b": { "driverStatId": 149, "teamStatId": null }, "appliedNow": true });
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert_eq!(
            op["args"],
            json!({ "a": 149, "b": 174, "expect": { "aTeamStatId": null, "bTeamStatId": 482 } })
        );
        e.after = json!({ "a": { "driverStatId": 149, "teamStatId": null }, "b": { "driverStatId": 174, "teamStatId": 482 }, "appliedNow": true });
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert_eq!(
            op["args"]["expect"],
            json!({ "aTeamStatId": null, "bTeamStatId": 482 })
        );
        e.after = Value::Null;
        assert!(matches!(inverse(&e), Undo::None(_)));
        assert!(matches!(
            inverse(&entry("drivers.contracts.repair", json!({}), Value::Null)),
            Undo::BackupOnly
        ));
        assert!(matches!(
            inverse(&entry("season.calendar.repair", json!({}), Value::Null)),
            Undo::BackupOnly
        ));
    }

    #[test]
    fn undoing_a_replace_takes_back_the_free_agents_new_number() {
        // free agent Martins (146, no number) into Hadjar's (149, #6) VCARB seat; the game gave Martins #12
        let mut e = entry(
            "drivers.swap",
            json!({ "a": 146, "b": 149, "expect": { "aTeamStatId": null, "bTeamStatId": 482 } }),
            json!({ "a": { "driverStatId": 146, "teamStatId": null, "raceNumber": null },
                    "b": { "driverStatId": 149, "teamStatId": 482, "raceNumber": 6 }, "appliedNow": true }),
        );
        e.after = json!({ "a": { "driverStatId": 146, "teamStatId": 482, "raceNumber": 12 },
                          "b": { "driverStatId": 149, "teamStatId": null, "raceNumber": 6 }, "appliedNow": true });
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert_eq!(
            op["args"],
            json!({ "a": 146, "b": 149, "expect": { "aTeamStatId": 482, "bTeamStatId": null }, "clearNumbers": [146] })
        );
        // a free agent who had a number keeps it, and so does a race driver numbered in a seat trade
        e.before["a"]["raceNumber"] = json!(41);
        e.after["a"]["raceNumber"] = json!(41);
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert!(op["args"].get("clearNumbers").is_none());
        e.before["a"] = json!({ "driverStatId": 146, "teamStatId": 480, "raceNumber": null });
        e.after["a"]["raceNumber"] = json!(12);
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert!(op["args"].get("clearNumbers").is_none());
        // a 0.3.0 result without race numbers undoes as before
        e.before = Value::Null;
        e.after = json!({ "a": { "driverStatId": 146, "teamStatId": 482 }, "b": { "driverStatId": 149, "teamStatId": null } });
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert!(op["args"].get("clearNumbers").is_none());
    }

    #[test]
    fn a_numbered_round_comes_back_with_its_track_id() {
        let before = json!({ "rounds": ["0", "1", "2"], "tracks": [0, 2, 13] });
        let Undo::Op(op) = inverse(&entry(
            "season.calendar.remove_round",
            json!({ "round": 2 }),
            before.clone(),
        )) else {
            panic!("no undo");
        };
        assert_eq!(
            op,
            json!({ "op": "season.calendar.add_round", "args": { "weekend": "1", "at": 2, "verified": true, "track_stat_id": 2 } })
        );
        let Undo::Op(op) = inverse(&entry(
            "season.calendar.set_round",
            json!({ "round": 3, "weekend": "rw_monaco" }),
            before,
        )) else {
            panic!("no undo");
        };
        assert_eq!(
            op["args"],
            json!({ "round": 3, "weekend": "2", "verified": true, "track_stat_id": 13 })
        );
        let old = json!({ "rounds": ["0", "1", "2"] });
        assert!(matches!(
            inverse(&entry("season.calendar.remove_round", json!({ "round": 2 }), old)),
            Undo::None(_)
        ));
        let named = json!({ "rounds": ["rw_melbourne_26", "rw_suzuka_26"] });
        let Undo::Op(op) = inverse(&entry(
            "season.calendar.remove_round",
            json!({ "round": 2 }),
            named,
        )) else {
            panic!("no undo");
        };
        assert_eq!(op["args"], json!({ "weekend": "rw_suzuka_26", "at": 2 }));
    }

    #[test]
    fn a_race_number_goes_back_and_a_swap_swaps_back() {
        let mut e = entry(
            "drivers.number.set",
            json!({ "driverStatId": 9, "number": 33 }),
            json!({ "driver": { "driverStatId": 9, "raceNumber": 1 } }),
        );
        e.after = json!({ "driver": { "driverStatId": 9, "raceNumber": 33 } });
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert_eq!(
            op["args"],
            json!({ "driverStatId": 9, "number": 1, "swap": false, "expect": 33 })
        );
        e.before["swappedWith"] = json!({ "driverStatId": 12, "raceNumber": 33 });
        e.after["swappedWith"] = json!({ "driverStatId": 12, "raceNumber": 1 });
        let Undo::Op(op) = inverse(&e) else {
            panic!("no undo")
        };
        assert_eq!(op["args"]["swap"], true);
        e.before = json!({ "driver": { "driverStatId": 9, "raceNumber": null } });
        assert!(matches!(inverse(&e), Undo::None(_)));
    }
}
