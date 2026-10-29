#pragma once

#include "lower.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Lowers the liveries cars wear (a vehicle record whose livery names a livery record) and the special liveries to
// what the game needs, from the rows the game loaded and the rows the content records wrote (a Read over the row
// engine's view): the car's wep archive path comes from its entrants.vehicles row (vehicle_package, asset_filename)
// and its package's resource_path. A livery's archive is routed to the addon's file; a car whose archive is not in
// the game folder gets its asset group, listing its package's teams/common.erp (or the donor package's) first. Its
// textures folder is routed file by file over the car's textures<suffix>/ folder. A special livery adds an se_livery
// row and an unlock row, copied from the first stock special livery of the same car (or the table's first), with the
// se_livery id and the unlock statId from their pools.

namespace swofty_core::apply::content {

using Read = std::function<std::optional<std::string>(const std::string &path)>;

inline constexpr const char *SE_LIVERIES = "customisation.custom_se_liveries.se_liveries";
inline constexpr const char *UNLOCK_IDS = "livery.unlock";
inline constexpr const char *UNLOCK_FIELDS[] = {
    "_metadata_", "customisation_item", "is_new_on_boot", "rarity", "statId", "type_loctag",
    "_ItemLockServerGeneric_instancecount_", "ItemLockServerGeneric_0._metadata_",
    "ItemLockServerGeneric_0.ceremony_image", "ItemLockServerGeneric_0.hideWhenLocked",
    "ItemLockServerGeneric_0.hideWhenUnlocked", "ItemLockServerGeneric_0.message_icon",
    "ItemLockServerGeneric_0.message_name_loc"};
inline constexpr const char *SE_LIVERY_FIELDS[] = {"_metadata_", "compatible_vehicle", "desc_loc_tag", "id",
                                                   "name_loc_tag", "team_customisation", "ui_priority", "wep_override"};

struct CarPaths {
    std::string label, rp, wep, group, team_dir, common, why;
    bool ok() const { return why.empty(); }
};

inline std::string replace_all(std::string s, const std::string &from, const std::string &to)
{
    for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size())
        s.replace(at, from.size(), to);
    return s;
}

inline CarPaths car_paths(const std::string &label, const Read &read)
{
    CarPaths c;
    c.label = label;
    std::string row = std::string(VEHICLES) + "[_metadata_=" + label + "]";
    auto pkg = read(row + ".vehicle_package");
    if (!pkg || pkg->empty())
        return c.why = "the game database has no entrants.vehicles row " + label, c;
    auto rp = read(std::string(PACKAGES) + "[_metadata_=" + *pkg + "].resource_path");
    if (!rp || rp->empty())
        return c.why = "vehicle package " + *pkg + " has no resource_path", c;
    std::string asset = read(row + ".asset_filename").value_or("");
    if (rp->find("[FILENAME]") != std::string::npos && asset.empty())
        return c.why = label + " has no asset_filename for " + *rp, c;
    c.rp = replace_all(*rp, "[FILENAME]", asset);
    c.wep = "2025_asset_groups/" + c.rp + ".erp";
    c.group = "asset_groups/" + c.rp + ".assetgroup";
    auto w = c.rp.rfind("/wep/");
    c.team_dir = w == std::string::npos ? c.rp.substr(0, c.rp.rfind('/') + 1) : c.rp.substr(0, w + 1);
    c.common = "2025_asset_groups/" + c.rp.substr(0, c.rp.find('/')) + "/teams/common.erp";
    return c;
}

inline std::string path_file(const Value &p)
{
    return p.is_string() ? std::string(p.as_string()) : std::string();
}

inline std::string auto_suffix(int n)
{
    char b[16];
    std::snprintf(b, sizeof b, "_lx%02d", n);
    return b;
}

// The archive and textures a livery puts on one car, with the suffix of its wep slot ("" for the car's own).
inline void livery_files(const std::string &id, const Value &rec, const CarPaths &car, const CarPaths *donor,
                         const std::string &suffix, Plan &plan)
{
    std::string addon = addon_of(id);
    std::string archive = path_file(rec["archive"]), textures = path_file(rec["textures"]);
    if (!archive.empty()) {
        std::string wep = "2025_asset_groups/" + car.rp + suffix + ".erp";
        Group g;
        g.group = "asset_groups/" + car.rp + suffix + ".assetgroup";
        g.tmpl = suffix.empty() ? (donor && donor->ok() ? donor->group : std::string()) : car.group;
        g.erp = wep;
        g.file = archive;
        g.addon = addon;
        g.id = id;
        g.common = {car.common};
        if (donor && donor->ok() && donor->common != car.common)
            g.common.push_back(donor->common);
        plan.groups.push_back(std::move(g));
    }
    if (!textures.empty()) {
        if (textures.back() != '/')
            textures += '/';
        plan.folders.push_back({car.team_dir + "textures" + suffix + "/", textures, addon, id});
    }
}

inline void lower_livery(const std::string &id, const Value &rec, const std::vector<std::string> &wearers,
                         const Lookup &lookup, const Read &read, Plan &plan, const StatIds &stat = {},
                         int *next_slot = nullptr)
{
    plan.owner = addon_of(id);
    auto label_of = [&](const std::string &vid) {
        const Value *v = lookup("vehicle", vid);
        return v && (*v)["label"].is_string() ? std::string((*v)["label"].as_string()) : key_of(vid);
    };
    auto donor_of = [&](const std::string &vid) -> std::optional<CarPaths> {
        const Value *v = lookup("vehicle", vid);
        if (!v || !(*v)["donor"].is_string())
            return std::nullopt;
        return car_paths(key_of((*v)["donor"].as_string()), read);
    };
    if (path_file(rec["archive"]).empty() && path_file(rec["textures"]).empty()) {
        plan.lines.push_back("livery " + id + ": no archive or textures folder in the addon; nothing applied");
        plan.refused++;
        return;
    }
    std::size_t worn = 0;
    for (auto &vid : wearers) {
        CarPaths car = car_paths(label_of(vid), read);
        if (!car.ok()) {
            plan.lines.push_back("livery " + id + " on " + vid + ": not applied: " + car.why);
            plan.refused++;
            continue;
        }
        auto donor = donor_of(vid);
        livery_files(id, rec, car, donor ? &*donor : nullptr, "", plan);
        plan.lines.push_back("livery " + id + ": worn by " + vid + " (" + car.wep + ")");
        worn++;
    }
    if (!rec["special"].is_bool() || !rec["special"].as_bool()) {
        if (!worn)
            plan.lines.push_back("livery " + id + ": no car wears it; nothing applied");
        return;
    }
    if (!rec["vehicle"].is_string()) {
        plan.lines.push_back("livery " + id + ": a special livery needs its vehicle; nothing applied");
        plan.refused++;
        return;
    }
    std::string vid(rec["vehicle"].as_string());
    CarPaths car = car_paths(label_of(vid), read);
    if (!car.ok()) {
        plan.lines.push_back("livery " + id + ": special livery not applied: " + car.why);
        plan.refused++;
        return;
    }
    std::string suffix = rec["suffix"].is_string() ? std::string(rec["suffix"].as_string()) : "auto";
    if (suffix == "auto") {
        int n = next_slot ? ++*next_slot : 1;
        suffix = auto_suffix(n);
    }
    auto sid = stat_of(stat, "livery", id, pin_of(rec));
    auto unlock = stat_of(stat, UNLOCK_IDS, id, -1);
    if (!sid || !unlock) {
        plan.lines.push_back("livery " + id + ": no free se_livery id or unlock statId; nothing applied");
        plan.refused++;
        return;
    }
    std::string row_label = car.label + "_" + key_of(id);
    std::string from = std::string(SE_LIVERIES) + ".se_livery_0";
    auto count = read(std::string(SE_LIVERIES) + "._se_livery_instancecount_");
    int64_t rows = count ? std::strtoll(count->c_str(), nullptr, 10) : 0;
    for (int64_t i = 0; i < rows; i++) {
        std::string at = std::string(SE_LIVERIES) + ".se_livery_" + std::to_string(i);
        if (read(at + ".compatible_vehicle") == car.label) {
            from = at;
            break;
        }
    }
    auto from_label = read(from + "._metadata_");
    std::optional<std::string> folder;
    for (int l = 0; l < 8 && !folder; l++)
        for (int f = 0; f < 64 && !folder; f++) {
            std::string at = "unlockable_content.unlockable_items.unlockable_item_list_" + std::to_string(l) + ".folder_" +
                             std::to_string(f);
            auto n = read(at + "._unlockable_se_livery_instancecount_");
            if (n && std::strtoll(n->c_str(), nullptr, 10) > 0)
                folder = at;
        }
    if (!from_label || !folder) {
        plan.lines.push_back("livery " + id + ": the game database has no special livery row to copy; nothing applied");
        plan.refused++;
        return;
    }
    std::string picker = rec["picker_name"].is_string() ? std::string(rec["picker_name"].as_string()) : std::string();
    Value se = Value::object({{"_metadata_", row_label},
                              {"compatible_vehicle", car.label},
                              {"id", std::to_string(*sid)},
                              {"wep_override", suffix},
                              {"ui_priority", std::to_string(*sid)}});
    if (!picker.empty()) {
        se["name_loc_tag"] = Value(picker);
        se["desc_loc_tag"] = Value(picker);
    }
    push_row(plan, SE_LIVERIES, std::string(SE_LIVERIES) + ".se_livery[_metadata_=" + *from_label + "]", std::move(se));
    add_fields(plan, (std::string(SE_LIVERIES) + ".se_livery").c_str(), SE_LIVERY_FIELDS, std::size(SE_LIVERY_FIELDS));
    std::string unlock_from = *folder + ".unlockable_se_livery_0";
    auto unlock_label = read(unlock_from + "._metadata_");
    push_row(plan, *folder, *folder + ".unlockable_se_livery[_metadata_=" + unlock_label.value_or("") + "]",
             Value::object({{"_metadata_", row_label},
                            {"customisation_item", row_label},
                            {"statId", std::to_string(*unlock)}}));
    add_fields(plan, (*folder + ".unlockable_se_livery").c_str(), UNLOCK_FIELDS, std::size(UNLOCK_FIELDS));
    auto donor = donor_of(vid);
    livery_files(id, rec, car, donor ? &*donor : nullptr, suffix, plan);
    std::string unlock_kind = rec["unlock"].is_string() ? std::string(rec["unlock"].as_string()) : "owned";
    plan.owned.push_back({row_label, id, unlock_kind == "owned"});
    plan.lines.push_back("livery " + id + ": special, wep_override " + suffix + ", stat id " + std::to_string(*sid) +
                         " (se_livery), unlock statId " + std::to_string(*unlock) + ", " + unlock_kind);
}

// The se_livery id pool and the unlock statId pool: the ids above every row the tables hold when the game loads.
inline std::vector<PoolScan> scan_livery_pools(const std::function<std::optional<std::string>(const std::string &)> &text)
{
    std::vector<PoolScan> out;
    PoolScan se{"livery", SE_LIVERIES, 0, 255, {}};
    auto n = text(std::string(SE_LIVERIES) + "._se_livery_instancecount_");
    int64_t top = 0;
    for (int64_t i = 0, rows = n ? std::strtoll(n->c_str(), nullptr, 10) : 0; i < rows; i++)
        if (auto v = text(std::string(SE_LIVERIES) + ".se_livery_" + std::to_string(i) + ".id"))
            top = std::max<int64_t>(top, std::strtoll(v->c_str(), nullptr, 10));
    se.lo = top + 1;
    out.push_back(se);
    static constexpr const char *KINDS[] = {
        "consumable_xp_booster", "currency_reward", "unlockable_badge_bg", "unlockable_badge_emblem",
        "unlockable_casual_footwear", "unlockable_casual_headwear", "unlockable_decal_set", "unlockable_driver",
        "unlockable_driver_number", "unlockable_eyewear", "unlockable_gloves", "unlockable_head", "unlockable_helmet",
        "unlockable_item_pack", "unlockable_livery", "unlockable_personal_sticker", "unlockable_podium_emote",
        "unlockable_pose", "unlockable_se_livery", "unlockable_suit", "unlockable_tops", "unlockable_trousers",
        "unlockable_victory_vo"};
    PoolScan un{UNLOCK_IDS, "unlockable_content.unlockable_items", 0, 65535, {}};
    int64_t utop = 0;
    for (int l = 0; l < 8; l++) {
        std::string list = "unlockable_content.unlockable_items.unlockable_item_list_" + std::to_string(l);
        auto folders = text(list + "._folder_instancecount_");
        for (int64_t f = 0, nf = folders ? std::strtoll(folders->c_str(), nullptr, 10) : 0; f < nf; f++) {
            std::string folder = list + ".folder_" + std::to_string(f);
            for (const char *kind : KINDS) {
                auto c = text(folder + "._" + kind + "_instancecount_");
                for (int64_t i = 0, rows = c ? std::strtoll(c->c_str(), nullptr, 10) : 0; i < rows; i++)
                    if (auto v = text(folder + "." + kind + "_" + std::to_string(i) + ".statId"))
                        utop = std::max<int64_t>(utop, std::strtoll(v->c_str(), nullptr, 10));
            }
        }
    }
    un.lo = utop + 1;
    out.push_back(un);
    return out;
}

}
