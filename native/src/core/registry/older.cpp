#include "older.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace swofty_core::registry::older {

namespace {

const char *const CAREERS[] = {"", "driver", "my_team", "both"};

Value schema_of(const char *name, const char *applies, const char *required)
{
    Value s = Value::object();
    s["$schema"] = "https://json-schema.org/draft/2020-12/schema";
    s["x-registry"] = name;
    s["x-version"] = "1.0";
    s["x-applies"] = applies;
    s["x-snapshot"] = false;
    s["type"] = "object";
    s["additionalProperties"] = true;
    Value req = Value::array();
    if (required && *required)
        req.push_back(required);
    s["required"] = std::move(req);
    return s;
}

std::set<std::string> &string_keys_seen()
{
    static std::set<std::string> keys;
    return keys;
}

int provide_string(void *, const char *, const char *, char *out, size_t cap, size_t *len)
{
    static const char body[] = "{\"text\":{}}";
    if (len)
        *len = sizeof body - 1;
    if (!out || cap < sizeof body)
        return SWOFTY_TOO_SMALL;
    std::memcpy(out, body, sizeof body);
    return SWOFTY_ASSET_OK;
}

std::string str(const Value &v, const char *field)
{
    return std::string(v[field].as_string());
}

Value text_of(const std::string &s)
{
    return Value(s);
}

std::string text_from(const Value &v, const std::string &fallback)
{
    if (v.is_string())
        return std::string(v.as_string());
    if (v.is_object() && v["literal"].is_string())
        return std::string(v["literal"].as_string());
    return fallback;
}

std::string model_ref(uint32_t model)
{
    return model == 2 ? "game/f1_career_2025" : "game/f1_career_2026";
}

}

void declare(Store &st)
{
    struct Reg {
        const char *name, *applies, *required;
    };
    static const Reg regs[] = {{CONTENT, "boot", "data_group"}, {HUB_TAB, "read", "data_group"},
                               {WIZARD, "content", "data_group"}, {CHARACTER, "content", "data_group"},
                               {STRING, "read", nullptr},        {OPTION, "read", "value"}};
    for (auto &r : regs)
        if (!st.declared(r.name))
            st.declare(r.name, schema_of(r.name, r.applies, r.required), true);
    static bool provided = false;
    if (!provided) {
        provided = true;
        st.provide_fn("game", STRING, &provide_string, nullptr, nullptr);
    }
}

int put(Store &st, const std::string &addon, const char *registry, const std::string &key, const Value &value,
        const std::string &source)
{
    return st.put_older(addon, registry, key, &value, source);
}

int drop(Store &st, const std::string &addon, const char *registry, const std::string &key)
{
    return st.put_older(addon, registry, key, nullptr, {});
}

Value effective(Store &st, const char *registry, const std::string &id)
{
    Record r = st.get(registry, id);
    return r.status == Status::ok ? r.value : Value();
}

std::string addon_of(const std::string &id)
{
    auto at = id.find('/');
    return at == std::string::npos ? std::string() : id.substr(0, at);
}

std::string key_of(const std::string &id)
{
    auto at = id.find('/');
    return at == std::string::npos ? id : id.substr(at + 1);
}

std::vector<std::string> in_load_order(Store &st, const char *registry)
{
    std::vector<std::string> ids = st.list(registry, "");
    std::vector<std::string> order = st.order();
    auto rank = [&](const std::string &id) {
        std::string a = addon_of(id);
        if (a == "game")
            return -1;
        auto it = std::find(order.begin(), order.end(), a);
        return it == order.end() ? int(order.size()) : int(it - order.begin());
    };
    std::stable_sort(ids.begin(), ids.end(), [&](const std::string &a, const std::string &b) { return rank(a) < rank(b); });
    return ids;
}

Value content_record(const ContentDesc &d)
{
    Value v = Value::object();
    v["mode"] = nullptr;
    v["tile"] = nullptr;
    v["model"] = model_ref(d.model);
    v["calendar"] = nullptr;
    v["after_last"] = "market";
    v["title"] = text_of(d.title);
    v["description"] = text_of(d.description);
    v["data_group"] = d.data_group;
    v["suffix"] = d.suffix;
    v["save_container"] = d.mode;
    v["car_group"] = int64_t(d.car_group);
    v["badge"] = int64_t(d.badge);
    v["entitlement"] = d.entitlement;
    v["careers"] = CAREERS[d.careers & 3];
    return v;
}

void content_shown(const Value &rec, ContentDesc &d)
{
    if (!rec.is_object())
        return;
    d.title = text_from(rec["title"], d.title);
    d.description = text_from(rec["description"], d.description);
    if (rec["badge"].is_int() && rec["badge"].as_int() >= 0 && rec["badge"].as_int() < 256)
        d.badge = uint32_t(rec["badge"].as_int());
    if (rec["car_group"].is_int() && rec["car_group"].as_int() >= -1)
        d.car_group = int32_t(rec["car_group"].as_int());
    if (rec["entitlement"].is_string() && rec["entitlement"].as_string().size() <= 48)
        d.entitlement = std::string(rec["entitlement"].as_string());
    std::string_view c = rec["careers"].as_string();
    for (uint32_t i = 1; i < 4; i++)
        if (c == CAREERS[i])
            d.careers = i;
}

Value hub_record(const HubTab &t)
{
    Value v = Value::object();
    v["name"] = t.name;
    v["title"] = t.title;
    v["data_group"] = t.group;
    Value opts = Value::array();
    for (auto &o : t.options)
        opts.push_back(o);
    v["options"] = std::move(opts);
    return v;
}

HubTab hub_from(const std::string &id, const Value &rec)
{
    HubTab t;
    t.addon = addon_of(id);
    t.name = str(rec, "name");
    if (t.name.empty())
        t.name = key_of(id);
    t.title = text_from(rec["title"], t.addon);
    t.group = str(rec, "data_group");
    if (auto *a = rec["options"].if_array())
        for (auto &o : *a)
            if (o.is_string())
                t.options.emplace_back(o.as_string());
    return t;
}

Value wizard_record(const std::string &group, const std::vector<WizardEntry> &given)
{
    Value v = Value::object();
    v["content"] = nullptr;
    v["steps"] = Value::array();
    v["data_group"] = group;
    bool start = false;
    Value tiles = Value::array(), steps = Value::array(), options = Value::array(), confirms = Value::array(),
          tabs = Value::array();
    for (auto &e : given) {
        if (e.group != group)
            continue;
        switch (e.kind) {
        case W_START:
            start = true;
            break;
        case W_TILE_ROUTE:
            tiles.push_back(Value::object({{"from", e.from}, {"to", e.to}}));
            break;
        case W_STEP:
            steps.push_back(Value::object({{"at", int64_t(e.a)}, {"with", int64_t(e.b)}}));
            break;
        case W_OPTION:
            options.push_back(Value::object({{"at", int64_t(e.a)}, {"with", int64_t(e.b)}}));
            break;
        case W_CONFIRM_ROUTE:
            confirms.push_back(Value::object({{"from", e.from}, {"to", e.to}}));
            break;
        case W_TAB:
            tabs.push_back(e.from);
            break;
        }
    }
    v["starts_in_f1"] = start;
    v["tile_routes"] = std::move(tiles);
    v["replace_steps"] = std::move(steps);
    v["force_options"] = std::move(options);
    v["confirm_routes"] = std::move(confirms);
    v["hide_driver_tabs"] = std::move(tabs);
    return v;
}

std::vector<WizardEntry> wizard_entries(const Value &rec)
{
    std::vector<WizardEntry> out;
    if (!rec.is_object())
        return out;
    std::string group = str(rec, "data_group");
    if (group.empty())
        return out;
    if (rec["starts_in_f1"].as_bool(false))
        out.push_back({W_START, group, "", "", 0, 0});
    auto routes = [&](const char *field, WizardKind kind) {
        if (auto *a = rec[field].if_array())
            for (auto &r : *a)
                if (r.is_object() && r["to"].is_string() && !r["to"].as_string().empty())
                    out.push_back({kind, group, str(r, "from"), str(r, "to"), 0, 0});
    };
    auto swaps = [&](const char *field, WizardKind kind) {
        if (auto *a = rec[field].if_array())
            for (auto &r : *a) {
                int64_t at = r["at"].as_int(-1), with = r["with"].as_int(-1);
                if (at >= 0 && at <= 0xff && with >= 0 && with <= 0xff && at != with)
                    out.push_back({kind, group, "", "", uint32_t(at), uint32_t(with)});
            }
    };
    routes("tile_routes", W_TILE_ROUTE);
    swaps("replace_steps", W_STEP);
    swaps("force_options", W_OPTION);
    routes("confirm_routes", W_CONFIRM_ROUTE);
    if (auto *a = rec["hide_driver_tabs"].if_array())
        for (auto &t : *a)
            if (t.is_string() && !t.as_string().empty())
                out.push_back({W_TAB, group, std::string(t.as_string()), "", 0, 0});
    return out;
}

Value character_record(const CharacterState &s)
{
    Value v = Value::object();
    v["replaces"] = nullptr;
    v["first_name"] = nullptr;
    v["last_name"] = nullptr;
    v["portrait"] = nullptr;
    Value mute = Value::array();
    for (auto &m : s.mutes)
        mute.push_back(m);
    v["mute"] = std::move(mute);
    v["data_group"] = s.group;
    Value swaps = Value::array();
    for (auto &[c, w] : s.swaps)
        swaps.push_back(Value::object({{"character", c}, {"with", w}}));
    v["swaps"] = std::move(swaps);
    Value strings = Value::object();
    for (auto &[language, kt] : s.strings)
        strings[language][kt.first] = kt.second;
    v["strings"] = std::move(strings);
    return v;
}

CharacterState character_from(const Value &rec)
{
    CharacterState s;
    if (!rec.is_object())
        return s;
    s.group = str(rec, "data_group");
    if (auto *a = rec["swaps"].if_array())
        for (auto &w : *a)
            if (w.is_object() && !w["character"].as_string().empty() && !w["with"].as_string().empty())
                s.swaps.push_back({str(w, "character"), str(w, "with")});
    if (auto *a = rec["mute"].if_array())
        for (auto &m : *a)
            if (m.is_string() && !m.as_string().empty())
                s.mutes.emplace_back(m.as_string());
    if (auto *o = rec["strings"].if_object())
        for (auto &[language, keys] : *o)
            if (auto *ko = keys.if_object())
                for (auto &[key, text] : *ko)
                    if (text.is_string())
                        s.strings.push_back({language, {key, std::string(text.as_string())}});
    return s;
}

int string_layer(Store &st, const std::string &addon, const std::string &key, const Value &text)
{
    string_keys_seen().insert(key);
    std::string id = "game/" + key;
    if (!text.is_object() || !text.size())
        return st.clear_layer(addon, STRING, id, Scope::global);
    LayerSpec spec;
    spec.addon = addon;
    spec.registry = STRING;
    spec.id = id;
    spec.scope = Scope::global;
    spec.change.is_patch = true;
    spec.change.patch = Value::object({{"text", text}});
    spec.source = "lang::set";
    uint32_t handle = 0;
    return st.layer(spec, &handle);
}

std::vector<std::pair<std::string, std::string>> string_texts(const Value &rec)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (auto *o = rec["text"].if_object())
        for (auto &[language, text] : *o)
            if (text.is_string())
                out.push_back({language, std::string(text.as_string())});
    return out;
}

std::vector<std::string> string_ids(Store &st)
{
    std::set<std::string> game;
    for (auto &k : string_keys_seen())
        game.insert("game/" + k);
    for (auto &id : st.layer_targets(STRING))
        if (addon_of(id) == "game")
            game.insert(id);
    std::vector<std::string> out(game.begin(), game.end());
    for (auto &id : in_load_order(st, STRING))
        if (addon_of(id) != "game")
            out.push_back(id);
    return out;
}

Value option_record(const std::string &type, const std::vector<OptionState> &states, std::size_t current)
{
    Value v = Value::object();
    v["type"] = type;
    Value list = Value::array();
    for (auto &s : states)
        list.push_back(Value::object({{"text", s.text}, {"value", s.value}}));
    v["states"] = std::move(list);
    v["value"] = current < states.size() ? states[current].value : Value();
    return v;
}

std::size_t option_current(const Value &rec, const std::vector<OptionState> &states, std::size_t given)
{
    if (!rec.is_object())
        return given;
    const Value &want = rec["value"];
    if (given < states.size() && states[given].value == want)
        return given;
    for (std::size_t i = 0; i < states.size(); i++)
        if (states[i].value == want)
            return i;
    return given;
}

}
