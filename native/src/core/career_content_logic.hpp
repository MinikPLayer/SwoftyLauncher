#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <swofty_api.h>

// Pure logic of the career content library (career_content.cpp): the descriptor check, the save-slot
// data group walk, the car group pick and the database requirements of a content. No game calls:
// the game reads come in through the Access and Db callbacks, so the unit test drives them with
// fake memory and a fake database.

namespace runtime::content {

constexpr size_t MAX_CONTENTS = 8;
constexpr size_t ID_MAX = 48;
constexpr size_t SUFFIX_MAX = 8;
constexpr size_t TEXT_MAX = 96;
constexpr uint32_t MODEL_2026 = 0;
constexpr uint32_t MODEL_2025 = 1;
constexpr uint32_t CAREER_DRIVER = 1;
constexpr uint32_t CAREER_MY_TEAM = 2;
constexpr uint32_t CAREER_ALL = CAREER_DRIVER | CAREER_MY_TEAM;

// The career content select's Addons row (fe_career_dlc_select): the content tiles' view model nodes go
// in the list addon_buttons next to the stock buttons list, and the row raises its own press and focus
// commands, which run the stock select_pressed and focus_changed paths on the row's part of the tile list.
constexpr const char *ADDON_BUTTONS = "addon_buttons";
constexpr const char *ADDON_BUTTONS_VISIBLE = "addon_buttons_visible";
constexpr const char *ADDON_TEMPLATE = "addon_tile_button_default";
constexpr uint32_t BUTTONS = 0x94eb0e14;
constexpr uint32_t ADDON_BUTTONS_HASH = 0xc2ec2239;
constexpr uint32_t SELECT_PRESSED = 0x2b0357a;
constexpr uint32_t FOCUS_CHANGED = 0x70d928ee;
constexpr uint32_t ADDON_SELECT_PRESSED = 0x8fb68dff;
constexpr uint32_t ADDON_FOCUS_CHANGED = 0x088404d3;
constexpr size_t LIST_BUTTONS = 0x38;
constexpr size_t LIST_ITEMS_BEGIN = 0x78;
constexpr size_t LIST_ITEMS_END = 0x80;
constexpr size_t VM_ROOT = 0x10;
constexpr size_t VM_TILE_LIST = 0x2c8;
constexpr size_t STACK_SCAN = 0x1000;

constexpr uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    while (*s)
        h = h * 33 + uint8_t(*s++);
    return h;
}

static_assert(djb2("buttons") == BUTTONS && djb2(ADDON_BUTTONS) == ADDON_BUTTONS_HASH &&
              djb2("select_pressed") == SELECT_PRESSED && djb2("focus_changed") == FOCUS_CHANGED &&
              djb2("addon_select_pressed") == ADDON_SELECT_PRESSED && djb2("addon_focus_changed") == ADDON_FOCUS_CHANGED);

// The career a content select belongs to, from the pick route that opened it (career_dlc_select_my_team
// or career_dlc_select_driver, career_start.hpp); Driver Career before any pick. The stock tiles carry
// no route (+0x178 is unset on both screens).
inline uint32_t career_of(uint32_t picked)
{
    return picked == SWOFTY_CAREER_MY_TEAM ? CAREER_MY_TEAM : CAREER_DRIVER;
}

inline const char *career_name(uint32_t career)
{
    return career == CAREER_MY_TEAM ? "My Team" : "Driver Career";
}

// The stock command an Addons row command stands for, or 0 for any other command.
inline uint32_t stock_command(uint32_t hash)
{
    return hash == ADDON_SELECT_PRESSED ? SELECT_PRESSED : hash == ADDON_FOCUS_CHANGED ? FOCUS_CHANGED : 0;
}

// FeTile_Build_List's showroom case (menu kind 4, fe_showroom) ends through the career content case's
// closing jmp, so site_FeTile_CareerContentEnd runs for both. A list is the content select's when its view
// model has the content select vtable or it holds a stock career tile (item ids of FeTile_Build_List case 6).
constexpr const char *ITEM_ID_2025 = "career_default_content";
constexpr const char *ITEM_ID_2026 = "career_dlc_content";
constexpr size_t SELECT_SCAN_TILES = 16;

inline bool career_tile_id(const char *id)
{
    return id && (!std::strcmp(id, ITEM_ID_2025) || !std::strcmp(id, ITEM_ID_2026));
}

inline bool content_select(uintptr_t owner_vtable, uintptr_t select_vtable, const char *const *ids, size_t n)
{
    if (owner_vtable && owner_vtable == select_vtable)
        return true;
    for (size_t i = 0; i < n; i++)
        if (career_tile_id(ids[i]))
            return true;
    return false;
}

// A career (the championship entity, or the career a save-slot predicate is asked about) lists its
// championships at +0x1040..+0x1048 (16-byte entries, championship at +8). Each championship's
// CareerSetupComponent (registry at championship +0x208) answers its data-group index from vtable
// +0x268 (a byte); the names are the entrant lists at (component +0x290) +0x560..+0x568 (16-byte
// entries, list at +8, name at *(list +0x28) +0x10), indexed by entrant list stat_id. The save-slot
// predicates 0x1438fb390 / 0x1438fb5d0 compare that name with the tile's data group.
constexpr size_t CAREER_CHAMPS_BEGIN = 0x1040;
constexpr size_t CAREER_CHAMPS_END = 0x1048;
constexpr size_t CHAMP_STRIDE = 0x10;
constexpr size_t CHAMP_AT = 0x08;
constexpr size_t CHAMP_REGISTRY = 0x208;
constexpr size_t SETUP_SOURCE = 0x290;
constexpr size_t GROUPS_BEGIN = 0x560;
constexpr size_t GROUPS_END = 0x568;
constexpr size_t GROUP_STRIDE = 0x10;
constexpr size_t GROUP_AT = 0x08;
constexpr size_t GROUP_NAMED = 0x28;
constexpr size_t NAMED_CHARS = 0x10;
constexpr size_t SETUP_INDEX_SLOT = 0x268;
constexpr size_t MAX_CHAMPS = 64;
constexpr size_t MAX_GROUPS = 1024;

// The mode championship's statId (game_modes.ModeChampionship.statId), read by the gate
// Game_Career_IsDriverCareerMode's callers through the same object.
constexpr size_t MODE_STAT_ID = 0x2a8;

// Career content item (FeTile_Construct_CareerItem): +0x150..+0x178 menu systems the init slot
// resolves, +0x178 route (career kind: driver_career / my_team_career), +0x180 data group, +0x188
// suffix, byte +0x190 is_dlc_content, and for the 2026 item +0x1b8..+0x1c8 the entitlement
// (+0x1c0 "f1_2026"). The 2025 item is 0x1b8 bytes, the 2026 one 0x1c8.
constexpr size_t ITEM_SIZE_2026 = 0x1c8;
constexpr size_t ITEM_SIZE_2025 = 0x1b8;
constexpr size_t ITEM_SYSTEMS = 0x150;
constexpr size_t ITEM_SYSTEMS_SIZE = 0x28;
constexpr size_t ITEM_SYSTEM_PROBE = 0x158;
constexpr size_t ITEM_ROUTE = 0x178;
constexpr size_t ITEM_DATA_GROUP = 0x180;
constexpr size_t ITEM_SUFFIX = 0x188;
constexpr size_t ITEM_IS_DLC = 0x190;
constexpr size_t ITEM_ENTITLEMENT = 0x1b8;
constexpr size_t ITEM_ENTITLEMENT_NAME = 0x1c0;
constexpr size_t ITEM_VTABLE_SLOTS = 22;
constexpr size_t ITEM_POPULATE_SLOT = 4;
constexpr size_t ITEM_PRESS_SLOT = 5;
constexpr size_t ITEM_NOTIFICATION_SLOT = 0x78 / 8;
constexpr size_t NESTR_CHARS = 9;
constexpr size_t TILE_ALLOCATOR = 0xc8;
constexpr size_t LIST_ALLOCATOR = 0x08;

// Career content entries (customisation car groups, g_FeTile_CustomisationSystem +0x230..+0x238,
// 0x60 bytes: +0x04 packed id with the group's stat_id in the high word, +0x58 dlc_content).
constexpr size_t SYSTEM_ENTRIES_BEGIN = 0x230;
constexpr size_t SYSTEM_ENTRIES_END = 0x238;
constexpr size_t ENTRY_SIZE = 0x60;
constexpr size_t ENTRY_ID = 0x04;
constexpr size_t ENTRY_DLC = 0x58;
constexpr uint32_t NO_CONTENT = 0x3fff0000;

template <class T> T at(const void *p, size_t off)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + off, sizeof v);
    return v;
}

struct Desc {
    std::string id, title, description, data_group, suffix, mode, entitlement;
    uint32_t model = MODEL_2026;
    int32_t car_group = -1;
    uint32_t badge = 0;
    uint32_t careers = CAREER_DRIVER;
};

inline bool token(const std::string &s, size_t max, bool upper_ok)
{
    if (s.empty() || s.size() > max)
        return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || (upper_ok && c >= 'A' && c <= 'Z')))
            return false;
    return true;
}

inline bool stock_group(const std::string &g)
{
    return g == "f1_career_2025" || g == "f1_career_2026";
}

// Why a descriptor is refused, or empty when it is fine. taken(field, value) says whether an
// already registered content uses that id or data group.
template <class Taken> std::string check(const Desc &d, Taken taken)
{
    if (!token(d.id, ID_MAX, false))
        return "id must be 1 to 48 characters of a-z, 0-9, _ or -";
    if (!token(d.data_group, ID_MAX, false))
        return "data_group must be 1 to 48 characters of a-z, 0-9, _ or -";
    if (stock_group(d.data_group))
        return "data_group " + d.data_group + " is a stock career's";
    if (!token(d.suffix, SUFFIX_MAX, true))
        return "suffix must be 1 to 8 characters of A-Z, a-z, 0-9, _ or -";
    if (d.suffix == "25" || d.suffix == "26")
        return "suffix " + d.suffix + " is a stock career's";
    if (!token(d.mode, ID_MAX, false))
        return "mode must name the game_modes.ModeChampionship save (1 to 48 characters of a-z, 0-9, _ or -)";
    if (d.title.empty() || d.title.size() > TEXT_MAX || d.description.empty() || d.description.size() > TEXT_MAX)
        return "title and description must be lng_* keys of 1 to 96 characters";
    if (d.model != MODEL_2026 && d.model != MODEL_2025)
        return "model must be SWOFTY_CAREER_CONTENT_MODEL_2026 or _2025";
    if (d.entitlement.size() > ID_MAX)
        return "entitlement is longer than 48 characters";
    if (d.car_group < -1 || d.car_group > 0xffff)
        return "car_group must be -1 or a customisation car group stat_id (0..65535)";
    if (d.badge > 0xff)
        return "badge must be 0 (none) or a notification value below 256";
    if (!d.careers || (d.careers & ~CAREER_ALL))
        return "careers must be SWOFTY_CAREER_CONTENT_DRIVER, SWOFTY_CAREER_CONTENT_MY_TEAM or both";
    if (taken("id", d.id))
        return "another content already has id " + d.id;
    if (taken("data_group", d.data_group))
        return "another content already has data group " + d.data_group;
    if (taken("suffix", d.suffix))
        return "another content already has suffix " + d.suffix;
    return {};
}

struct Access {
    void *(*setup_component)(void *championship, void *user);
    int (*group_index)(void *component, void *user);
    bool (*readable)(const void *p, size_t n, void *user);
    void *user;
};

struct GroupSeen {
    int index = -1;
    size_t count = 0;
    const char *name = nullptr;
    bool component = false;
};

inline const char *group_name(const void *component, int index, size_t *count, const Access &a)
{
    *count = 0;
    if (!a.readable(component, SETUP_SOURCE + 8, a.user))
        return nullptr;
    auto *src = at<const uint8_t *>(component, SETUP_SOURCE);
    if (!src || !a.readable(src, GROUPS_END + 8, a.user))
        return nullptr;
    auto *lb = at<const uint8_t *>(src, GROUPS_BEGIN), *le = at<const uint8_t *>(src, GROUPS_END);
    if (!lb || le < lb || size_t(le - lb) % GROUP_STRIDE || size_t(le - lb) / GROUP_STRIDE > MAX_GROUPS)
        return nullptr;
    *count = size_t(le - lb) / GROUP_STRIDE;
    if (index < 0 || size_t(index) >= *count || !a.readable(lb + size_t(index) * GROUP_STRIDE, GROUP_STRIDE, a.user))
        return nullptr;
    auto *group = at<const uint8_t *>(lb + size_t(index) * GROUP_STRIDE, GROUP_AT);
    if (!group || !a.readable(group, GROUP_NAMED + 8, a.user))
        return nullptr;
    auto *named = at<const uint8_t *>(group, GROUP_NAMED);
    if (!named || !a.readable(named, NAMED_CHARS + 8, a.user))
        return nullptr;
    const char *name = at<const char *>(named, NAMED_CHARS);
    return name && a.readable(name, 1, a.user) ? name : nullptr;
}

template <class Each> void each_setup(const void *career, const Access &a, Each each)
{
    if (!career || !a.readable(career, CAREER_CHAMPS_END + 8, a.user))
        return;
    auto *b = at<const uint8_t *>(career, CAREER_CHAMPS_BEGIN), *e = at<const uint8_t *>(career, CAREER_CHAMPS_END);
    if (!b || e < b || size_t(e - b) % CHAMP_STRIDE || size_t(e - b) / CHAMP_STRIDE > MAX_CHAMPS ||
        (e != b && !a.readable(b, size_t(e - b), a.user)))
        return;
    for (; b < e; b += CHAMP_STRIDE) {
        void *champ = at<void *>(b, CHAMP_AT);
        void *comp = champ ? a.setup_component(champ, a.user) : nullptr;
        if (comp && each(comp))
            return;
    }
}

// What the predicate reads for the first championship with a CareerSetupComponent.
inline GroupSeen data_group(const void *career, const Access &a)
{
    GroupSeen seen;
    each_setup(career, a, [&](void *comp) {
        seen.component = true;
        seen.index = a.group_index(comp, a.user);
        seen.name = group_name(comp, seen.index, &seen.count, a);
        return true;
    });
    return seen;
}

// The predicate's own test: some championship of the career has data group want.
inline bool data_group_is(const void *career, const char *want, const Access &a)
{
    bool hit = false;
    if (!want)
        return false;
    each_setup(career, a, [&](void *comp) {
        size_t count = 0;
        const char *name = group_name(comp, a.group_index(comp, a.user), &count, a);
        hit = name && !std::strcmp(name, want);
        return hit;
    });
    return hit;
}

struct Pick {
    enum Kind { none, exact, fallback } kind = none;
    uint32_t id = NO_CONTENT;
};

// The car group a content tile selects: the entry whose stat_id is car_group, else what the model
// tile's own press takes (2026: the first dlc_content entry, 2025: the first other one).
inline Pick pick(const uint8_t *begin, const uint8_t *end, int32_t car_group, uint32_t model)
{
    Pick fallback;
    for (const uint8_t *e = begin; e && e + ENTRY_SIZE <= end; e += ENTRY_SIZE) {
        uint32_t id = at<uint32_t>(e, ENTRY_ID);
        if (car_group >= 0 && (id >> 16) == uint32_t(car_group))
            return {Pick::exact, id};
        bool dlc = e[ENTRY_DLC] != 0;
        if (fallback.kind == Pick::none && dlc == (model == MODEL_2026))
            fallback = {Pick::fallback, id};
    }
    return fallback;
}

struct Db {
    bool (*text)(const std::string &key, std::string &out, void *user);
    bool (*integer)(const std::string &key, int64_t &out, void *user);
    bool (*list_stat)(const std::string &label, int64_t &out, void *user);
    void *user;
};

// A content whose requirements fail is checked again, at most every RETRY_MS, until they hold: rows
// that runtime database operations add can land after the first check. A reason is logged once,
// and again only when it changes.
constexpr uint64_t RETRY_MS = 1000;

struct Retry {
    uint64_t next = 0;
    uint32_t tries = 0;
    std::string why;

    bool due(uint64_t now) const
    {
        return now >= next;
    }

    bool failed(uint64_t now, const std::string &reason)
    {
        tries++;
        next = now + RETRY_MS;
        if (reason == why)
            return false;
        why = reason;
        return true;
    }
};

struct Requirements {
    bool ok = false;
    int64_t mode = -1;
    int64_t group = -1;
    bool f1 = false, f2 = false, icons = false;
    std::string why, warn;
};

inline bool count_of(const Db &db, const std::string &key, int64_t &n)
{
    return db.integer(key, n, db.user) && n > 0 && n <= 4096;
}

inline bool text_is(const Db &db, const std::string &key, const std::string &want)
{
    std::string v;
    return db.text(key, v, db.user) && v == want;
}

// The statId of the game_modes.ModeChampionship row that saves to save (save_data.name or
// save_data_0.name), or -1.
inline int64_t mode_stat_id(const Db &db, const std::string &save)
{
    int64_t n = 0;
    if (!count_of(db, "game_modes._ModeChampionship_instancecount_", n))
        return -1;
    for (int64_t i = 0; i < n; i++) {
        std::string row = "game_modes.ModeChampionship_" + std::to_string(i);
        int64_t stat = -1;
        if ((text_is(db, row + ".save_data.name", save) || text_is(db, row + ".save_data_0.name", save)) &&
            db.integer(row + ".statId", stat, db.user))
            return stat;
    }
    return -1;
}

// Some content set lists an official entrant list with this name.
inline bool official_list(const Db &db, const std::string &name)
{
    int64_t sets = 0;
    if (!count_of(db, "content_sets._content_set_instancecount_", sets))
        return false;
    for (int64_t i = 0; i < sets; i++) {
        std::string set = "content_sets.content_set_" + std::to_string(i);
        int64_t lists = 0;
        if (!count_of(db, set + "._official_entrant_list_instancecount_", lists))
            continue;
        for (int64_t j = 0; j < lists; j++)
            if (text_is(db, set + ".official_entrant_list_" + std::to_string(j) + ".name", name))
                return true;
    }
    return false;
}

inline Requirements requirements(const Desc &d, const Db &db)
{
    Requirements r;
    r.mode = mode_stat_id(db, d.mode);
    if (!db.list_stat(d.data_group, r.group, db.user))
        r.group = -1;
    r.f1 = official_list(db, "F1CAREER_" + d.suffix);
    r.f2 = official_list(db, "F2CAREER_" + d.suffix);
    r.icons = official_list(db, "ICONS_" + d.suffix);
    if (r.mode < 0)
        r.why = "no game_modes.ModeChampionship row saves to " + d.mode + " (save_data.name)";
    else if (r.group < 0)
        r.why = "no entrants.entrant_lists row is named " + d.data_group +
                " (the championship's ComCareerSetup.entrant_list; its stat_id is the save-slot data group index)";
    else if (r.group > 255)
        r.why = "entrant list " + d.data_group + " has stat_id " + std::to_string(r.group) +
                "; the data group index is a byte, so it must be below 256";
    else if (!r.f1)
        r.why = "no content set lists the official entrant list F1CAREER_" + d.suffix +
                " (the driver select formats F1CAREER_%s with the tile's suffix)";
    if (!r.f2)
        r.warn += "no official entrant list F2CAREER_" + d.suffix + " (the F2 tab of the driver select stays empty)";
    if (!r.icons)
        r.warn += std::string(r.warn.empty() ? "" : "; ") + "no official entrant list ICONS_" + d.suffix +
                  " (the Icons tab of the driver select stays empty)";
    r.ok = r.why.empty();
    return r;
}

}
