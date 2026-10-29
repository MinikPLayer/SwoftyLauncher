#pragma once

#include "erp_file.hpp"
#include "logo_array.hpp"
#include "uidb.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// The archives a set of team logos changes. Each logo is a slice for one stock texture array (an encoded image, or a
// copy of the slice a stock key already has) under a key in overrides.uidb. The result names, for every archive in
// the game's ui_package folder that carries a changed array and for databases.erp, the fragments to replace.

namespace swofty_core::apply::content::logo_build {

inline constexpr const char *UI_FOLDER = "2025_asset_groups/ui_package/";
inline constexpr const char *DATABASES = "2025_asset_groups/ui_package/databases.erp";
inline constexpr const char *OVERRIDES = "eaid://ui_package/databases/overrides.uidb";
inline constexpr const char *ARRAY_URI = "eaid://ui_package/art/texture_arrays/overrides.uidb/overrides.";
inline constexpr const char *TEXTURE_PATH = "ui_package/art/textures/swofty/team_logos/";

struct Want {
    std::string array, key, donor;
    std::string rgba;
    uint32_t width = 0, height = 0;
    bool fallback = false;
};

struct Env {
    std::function<std::vector<std::string>()> list;
    std::function<std::shared_ptr<erpfile::Source>(const std::string &rel)> open;
};

struct Result {
    std::map<std::string, erpfile::Changes> changes;
    std::vector<std::string> lines;
    int built = 0, refused = 0;
};

inline std::string list_path(const std::string &array)
{
    std::string path = "overrides.Contents.", rest = array;
    size_t dot;
    while ((dot = rest.find('.')) != std::string::npos) {
        path += rest.substr(0, dot) + ".Contents.";
        rest = rest.substr(dot + 1);
    }
    return path + rest + ".Contents";
}

inline bool build(const Env &env, const std::vector<Want> &wants, Result &out)
{
    using erpfile::Archive;
    struct Open {
        std::shared_ptr<erpfile::Source> src;
        Archive arc;
    };
    std::map<std::string, Open> opened;
    std::string why;
    auto get = [&](const std::string &rel) -> Open * {
        auto it = opened.find(rel);
        if (it != opened.end())
            return &it->second;
        auto src = env.open(rel);
        Open o;
        if (!src || !erpfile::open(*src, o.arc, why))
            return nullptr;
        o.src = src;
        return &opened.emplace(rel, std::move(o)).first->second;
    };
    Open *db = get(DATABASES);
    const erpfile::Entry *ue = db ? db->arc.find(OVERRIDES) : nullptr;
    std::string uidb_bytes;
    if (!ue || ue->frags.empty() || !erpfile::fragment(*db->src, db->arc, ue->frags[0], uidb_bytes, why)) {
        out.lines.push_back("team.logo: overrides.uidb is not in " + std::string(DATABASES) + (why.empty() ? "" : " (" + why + ")"));
        out.refused += int(wants.size());
        return false;
    }
    uidb::Pkg pkg;
    if (!pkg.read(uidb_bytes)) {
        out.lines.push_back("team.logo: overrides.uidb does not read: " + pkg.why);
        out.refused += int(wants.size());
        return false;
    }
    std::vector<std::string> files = env.list();
    std::map<std::string, std::vector<const Want *>> by_array;
    for (auto &w : wants)
        by_array[w.array].push_back(&w);
    bool uidb_changed = false;
    for (auto &[array, list] : by_array) {
        uidb::Node *node = pkg.find(list_path(array));
        if (!node || node->kind != uidb::Node::array) {
            out.lines.push_back("team.logo: overrides.uidb has no list " + array);
            out.refused += int(list.size());
            continue;
        }
        std::vector<std::string> names;
        for (auto &e : node->list)
            names.push_back(uidb::Pkg::text_of(e, "Name"));
        std::string image = ARRAY_URI + array + ".texarray.image", view = ARRAY_URI + array + ".texarray";
        std::vector<std::string> carriers;
        logo_array::Stock stock;
        bool have = false, mismatch = false;
        for (auto &rel : files) {
            Open *o = get(rel);
            if (!o)
                continue;
            const erpfile::Entry *ei = o->arc.find(image), *ev = o->arc.find(view);
            if (!ei && !ev)
                continue;
            logo_array::Stock got;
            if (!ei || !ev || ei->frags.size() != 2 || ev->frags.size() != 1 ||
                !erpfile::fragment(*o->src, o->arc, ei->frags[0], got.head, why) ||
                !erpfile::fragment(*o->src, o->arc, ei->frags[1], got.data, why) ||
                !erpfile::fragment(*o->src, o->arc, ev->frags[0], got.view, why)) {
                out.lines.push_back("team.logo: " + rel + " carries " + array + " in a layout this build does not read");
                mismatch = true;
                break;
            }
            if (have && (got.head != stock.head || got.data != stock.data || got.view != stock.view)) {
                out.lines.push_back("team.logo: " + rel + " carries a different " + array + " than " + carriers[0]);
                mismatch = true;
                break;
            }
            stock = std::move(got);
            have = true;
            carriers.push_back(rel);
        }
        stock.names = names;
        logo_array::Shape shape;
        if (mismatch || !have || !logo_array::shape_of(stock, shape, why)) {
            if (!mismatch)
                out.lines.push_back("team.logo: " + array + (have ? ": " + why : " is in no archive of the ui_package folder"));
            out.refused += int(list.size());
            continue;
        }
        std::vector<logo_array::Slice> add;
        std::set<std::string> seen(names.begin(), names.end());
        std::vector<const Want *> used;
        for (auto *w : list) {
            if (!seen.insert(w->key).second) {
                out.lines.push_back("team.logo: " + array + " lists " + w->key + " already; its slice is kept");
                continue;
            }
            logo_array::Slice s{w->key, {}};
            if (!w->donor.empty()) {
                auto at = std::find(names.begin(), names.end(), w->donor);
                if (at == names.end()) {
                    out.lines.push_back("team.logo: " + array + " has no stock key " + w->donor + " for " + w->key +
                                        (w->fallback ? "; the donor fallback is left out" : ""));
                    if (!w->fallback)
                        out.refused++;
                    seen.erase(w->key);
                    continue;
                }
                s.bc7 = stock.data.substr(size_t(at - names.begin()) * shape.slice_bytes(), shape.slice_bytes());
            } else {
                bool scaled = false;
                if (!logo_array::slice_from_rgba(shape, reinterpret_cast<const uint8_t *>(w->rgba.data()), w->width, w->height, s.bc7,
                                                 why, &scaled)) {
                    out.lines.push_back("team.logo: " + w->key + " in " + array + ": " + why);
                    out.refused++;
                    seen.erase(w->key);
                    continue;
                }
                if (scaled)
                    out.lines.push_back("team.logo: " + w->key + " scaled " + std::to_string(w->width) + "x" + std::to_string(w->height) +
                                        " -> " + std::to_string(shape.width) + "x" + std::to_string(shape.height) + " for " + array);
            }
            add.push_back(std::move(s));
            used.push_back(w);
        }
        if (add.empty())
            continue;
        logo_array::Stock grown;
        if (!logo_array::grow(stock, add, grown, why)) {
            out.lines.push_back("team.logo: " + array + ": " + why);
            out.refused += int(add.size());
            continue;
        }
        for (auto &rel : carriers) {
            auto &c = out.changes[rel];
            c[image] = {grown.head, grown.data};
            c[view] = {grown.view};
        }
        for (auto &s : add) {
            std::string e;
            if (!pkg.append_entry(*node, s.key, std::string(TEXTURE_PATH) + s.key + ".tif", e)) {
                out.lines.push_back("team.logo: " + array + ": " + e);
                out.refused++;
            }
        }
        uidb_changed = true;
        out.built += int(add.size());
        out.lines.push_back("team.logo: " + array + " grows by " + std::to_string(add.size()) + " slice(s) in " +
                            std::to_string(carriers.size()) + " archive(s)");
    }
    if (uidb_changed)
        out.changes[DATABASES][OVERRIDES] = {pkg.write()};
    return true;
}

}
