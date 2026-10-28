#include "runtime.hpp"

#include <safetyhook.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "perf.hpp"

// Message popups (NeUIMessageSystem). The game raises every popup the same way: construct a message
// by its UIMD id (Ui_Message_Construct, 0x1442d9bf0: system, out handle, fixed-capacity id string,
// response callback, two unused zeros), set its user attributes through the handle
// (Ui_Message_SetAttribute, 0x1406a79c0, takes an NeStr reference), then drop the handle
// (Ui_Message_ReleaseHandle, 0x1442aa980). The message stays queued and the persistent screen's
// multichoice popup draws it. The beta_motd poster (0x1436d2120) does exactly this with a literal
// "message" attribute, so one signature over its body yields the id string vtable, the constructor
// and the attribute setter.
//
// The text comes from two attributes, "title" and "message", which the swofty_message definition
// the launcher adds to onlineservices_messages.uimsg reads through lng_value_tag tokens. When that
// definition is missing (an old UI build), the core falls back to the stock beta_motd message, whose
// body token reads "message"; the title then leads the body.
//
// Every construct passes through the wrap below, which records the system pointer (the game
// constructs messages from boot on, the online-services wait box first). Requests queue from any
// thread and are shown from the flow tick.
// While a launcher netplay command drives the LAN menus, the wrap hands it the LAN Game dialog and
// the Join via IP prompt instead of queueing them (runtime::netplay_popup): the caller gets an empty
// handle, which the handle setter and release accept, and netplay answers the response callback.
//
// Toasts go through the same system as hud_main messages, the queue the race HUD's central
// notification (hud_notifications_dynamic, F1HM bound to #ui_messages.hud_main_queue.hud_main) draws.
// The launcher adds swofty_toast_<seconds> to hud_messages.uimsg: literal (localise false) message and
// sub_message attributes and a fixed msg_duration per id, so a toast picks the id nearest its duration.

std::string settings_addon(const void *from, std::string &dll);
void *ui_vm_message_system();
bool ui_vm_set_path(const std::string &path, int type, const void *value);

namespace {

const runtime::Sig S_MOTD{
    "Ui_Message_PostMotd",
    "48 B9 62 65 74 61 5F 6D 6F 74 48 89 4C 24 58 66 C7 44 24 60 64 00 48 8D 0D ?? ?? ?? ?? 48 89 4C 24 40 C5 F9 57 "
    "C0 C5 F9 29 44 24 30 C5 F9 11 44 24 20 48 8D 9C 24 E0 00 00 00 4C 8D 44 24 40 4C 8D 4C 24 30 48 89 C1 48 89 DA "
    "E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 8B 50 68 48 C7 84 24 D8 00 00 00 00 00 00 00 4C 8D B4 24 D8 00 00 00 4C "
    "89 F1 45 31 C0 45 31 C9 E8 ?? ?? ?? ?? 48 C7 44 24 20 00 00 00 00 48 8D 15 ?? ?? ?? ?? 48 89 D9 4D 89 F0 45 31 "
    "C9 E8 ?? ?? ?? ??",
    0x36d23ca};
const runtime::Sig S_RELEASE{"Ui_Message_ReleaseHandle",
                             "56 57 53 48 83 EC 30 48 8B 79 08 48 85 FF 0F 84 ?? ?? ?? ?? 48 89 CE 48 8B 8F 90 00 00 00 "
                             "48 8B 87 98 00 00 00",
                             0x42aa980};
constexpr uintptr_t MOTD_VT_DISP = 0x19, MOTD_VT_END = 0x1d, MOTD_CONSTRUCT = 0x4b, MOTD_MAKE_STR = 0x78,
                    MOTD_SET_ATTR = 0x96;
constexpr const uint8_t CONSTRUCT_HEAD[] = {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x30};
constexpr const char *OWN_ID = "swofty_message", *FALLBACK_ID = "beta_motd";
constexpr size_t MAX_QUEUED = 8, MAX_TEXT = 4000;

struct FixedId {
    const void *vt;
    uint64_t cap;
    char *data;
    char buf[128];
};

struct Handle {
    void *sys;
    void *msg;
};

struct Callback {
    void *fn;
    void *ctx;
};

using Construct_t = Handle *(void *sys, Handle *out, FixedId *id, Callback *cb, const char *a5, uint64_t a6);
using SetAttr_t = bool(Handle *h, const char *name, void **value, uint32_t flags, const char *a5);
using Release_t = void(Handle *h);
using MakeStr_t = uint32_t(void **out, const char *s, uint32_t flags, uintptr_t len);

struct Request {
    std::string title, body, by;
};

struct Toast {
    std::string text, detail, icon, by;
    unsigned seconds;
};

struct Set {
    std::string path, text, by;
    int type;
    int32_t number;
};

constexpr const char *ICONS[] = {"generic",  "diagonal",           "black",       "checkpoint",
                                 "chequered", "fast_lap",           "formation_lap", "pp_icon",
                                 "red",      "safety_car",         "virtual_safety_car",
                                 "virtual_safety_car_green", "flag_rival_won"};
constexpr const char *SET_PREFIXES[] = {"ui_menu_data.hud_", "view_model_data.hud.", "view_model_data.hud_"};
constexpr size_t MAX_SETS = 64;

constexpr unsigned TOAST_SECONDS[] = {2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 30};
constexpr size_t MAX_TOASTS = 8, MAX_TOAST_TEXT = 200;

uintptr_t a_construct, a_set_attr, a_release, a_make_str;
const void *id_vt;
safetyhook::InlineHook h_construct;
std::atomic<void *> sys;
std::atomic<bool> live, own_missing;
std::mutex queue_lock;
std::deque<Request> queue;
std::atomic<size_t> pending, toasts_pending;
std::deque<Toast> toasts;
std::atomic<bool> toast_missing;
std::deque<Set> sets;
std::atomic<size_t> sets_pending;

uintptr_t rel32(uintptr_t at)
{
    return at + 4 + *reinterpret_cast<const int32_t *>(at);
}

Handle *construct_hook(void *s, Handle *out, FixedId *id, Callback *cb, const char *a5, uint64_t a6)
{
    void *was = sys.exchange(s);
    if (was != s)
        swofty::log("[popup] message system %p%s (first message '%.64s')", s, was ? " replaces an earlier one" : "",
                    id && swofty::mem::readable(id, sizeof(FixedId)) && id->data && swofty::mem::readable(id->data, 1)
                        ? id->data
                        : "?");
    const char *name = id && swofty::mem::readable(id, sizeof(FixedId)) && id->data && swofty::mem::readable(id->data, 1)
                           ? id->data
                           : nullptr;
    if (name && out && cb && swofty::mem::readable(cb, sizeof *cb) && runtime::netplay_popup(name, cb->fn, cb->ctx)) {
        *out = Handle{s, nullptr};
        return out;
    }
    return h_construct.call<Handle *>(s, out, id, cb, a5, a6);
}

bool make_id(FixedId &id, const char *name)
{
    id.vt = id_vt;
    id.cap = sizeof id.buf;
    id.data = id.buf;
    return snprintf(id.buf, sizeof id.buf, "%s", name) > 0;
}

bool set(Handle &h, const char *name, const std::string &text)
{
    void *s = nullptr;
    reinterpret_cast<MakeStr_t *>(a_make_str)(&s, text.c_str(), 0, 0);
    return reinterpret_cast<SetAttr_t *>(a_set_attr)(&h, name, &s, 0, nullptr);
}

Handle open(void *s, const char *name)
{
    Handle h{};
    FixedId id{};
    Callback cb{};
    if (make_id(id, name))
        reinterpret_cast<Construct_t *>(a_construct)(s, &h, &id, &cb, nullptr, 0);
    return h;
}

void show(void *s, const Request &r)
{
    Handle h = own_missing ? Handle{} : open(s, OWN_ID);
    bool own = h.msg != nullptr;
    if (!own) {
        if (!own_missing.exchange(true))
            swofty::log("[popup] the game has no '%s' message (the launcher's UI build adds it); falling back to "
                        "'%s', whose title is fixed, so the title leads the body",
                        OWN_ID, FALLBACK_ID);
        h = open(s, FALLBACK_ID);
    }
    if (!h.msg) {
        swofty::log("[popup] popup from %s dropped: the game constructed no message ('%s' and '%s' both refused)",
                    r.by.c_str(), OWN_ID, FALLBACK_ID);
        return;
    }
    bool ok = own ? set(h, "title", r.title) && set(h, "message", r.body)
                  : set(h, "message", r.title.empty() ? r.body : r.body.empty() ? r.title : r.title + "\n\n" + r.body);
    reinterpret_cast<Release_t *>(a_release)(&h);
    swofty::log("[popup] shown for %s as '%s': \"%.80s\"%s", r.by.c_str(), own ? OWN_ID : FALLBACK_ID, r.title.c_str(),
                ok ? "" : " (an attribute was refused; the popup may show empty text)");
}

unsigned toast_seconds(float want)
{
    unsigned best = TOAST_SECONDS[0];
    for (unsigned n : TOAST_SECONDS)
        if (std::fabs(float(n) - want) < std::fabs(float(best) - want))
            best = n;
    return best;
}

void show_toast(void *s, const Toast &t)
{
    char id[32];
    snprintf(id, sizeof id, "swofty_toast_%u", t.seconds);
    Handle h = open(s, id);
    if (!h.msg) {
        if (!toast_missing.exchange(true))
            swofty::log("[hud] toast from %s dropped: the game has no '%s' message (the launcher's UI build adds the "
                        "swofty_toast_* HUD messages)",
                        t.by.c_str(), id);
        return;
    }
    bool ok = set(h, "message", t.text) && set(h, "sub_message", t.detail);
    if (!t.icon.empty())
        ok = set(h, "flag", "#overrides.osd.race_flags." + t.icon) && ok;
    reinterpret_cast<Release_t *>(a_release)(&h);
    swofty::log("[hud] toast shown for %s as '%s'%s%s: \"%.80s\"%s", t.by.c_str(), id, t.icon.empty() ? "" : " icon ",
                t.icon.c_str(), t.text.c_str(), ok ? "" : " (an attribute was refused; the toast may show empty text)");
}

int api_available()
{
    return live && runtime::enabled("popup", nullptr);
}

std::string who(const void *from)
{
    std::string dll, id = settings_addon(from, dll);
    return id.empty() ? (dll.empty() ? std::string("swofty_core") : dll) : id;
}

int enqueue(std::string t, std::string b, const std::string &by)
{
    if (t.empty() && b.empty()) {
        swofty::log("[popup] show by %s refused: empty title and body", by.c_str());
        return SWOFTY_ASSET_INVALID;
    }
    if (!api_available()) {
        swofty::log("[popup] show by %s: unavailable (%s)", by.c_str(),
                    runtime::enabled("popup", nullptr) ? "the message system did not resolve on this build"
                                                       : "the popup feature is off");
        return SWOFTY_ASSET_UNAVAILABLE;
    }
    if (t.size() > MAX_TEXT)
        t.resize(MAX_TEXT);
    if (b.size() > MAX_TEXT)
        b.resize(MAX_TEXT);
    std::lock_guard<std::mutex> g(queue_lock);
    for (auto &q : queue)
        if (q.title == t && q.body == b)
            return SWOFTY_ASSET_OK;
    if (queue.size() >= MAX_QUEUED) {
        swofty::log("[popup] queue full (%zu); dropping the oldest popup, from %s", queue.size(), queue.front().by.c_str());
        queue.pop_front();
    }
    queue.push_back({std::move(t), std::move(b), by});
    pending = queue.size();
    swofty::log("[popup] queued by %s: \"%.80s\"%s", by.c_str(), queue.back().title.c_str(),
                sys.load() ? "" : " (waiting for the game's first message to find the message system)");
    return SWOFTY_ASSET_OK;
}

int api_show(const char *title, const char *body)
{
    return enqueue(title ? title : "", body ? body : "", who(__builtin_return_address(0)));
}

const SwoftyPopup table = {api_available, api_show};

int toast_enqueue(std::string text, std::string detail, float seconds, std::string icon, const std::string &by)
{
    if (text.empty()) {
        swofty::log("[hud] toast by %s refused: empty text", by.c_str());
        return SWOFTY_ASSET_INVALID;
    }
    if (!icon.empty() && std::none_of(std::begin(ICONS), std::end(ICONS), [&](const char *i) { return icon == i; })) {
        swofty::log("[hud] toast by %s refused: unknown icon '%.40s'", by.c_str(), icon.c_str());
        return SWOFTY_ASSET_INVALID;
    }
    if (!api_available()) {
        swofty::log("[hud] toast by %s: unavailable (%s)", by.c_str(),
                    runtime::enabled("popup", nullptr) ? "the message system did not resolve on this build"
                                                       : "the popup feature is off");
        return SWOFTY_ASSET_UNAVAILABLE;
    }
    if (text.size() > MAX_TOAST_TEXT)
        text.resize(MAX_TOAST_TEXT);
    if (detail.size() > MAX_TOAST_TEXT)
        detail.resize(MAX_TOAST_TEXT);
    unsigned n = toast_seconds(seconds > 0 ? seconds : 4.0f);
    std::lock_guard<std::mutex> g(queue_lock);
    if (toasts.size() >= MAX_TOASTS) {
        swofty::log("[hud] toast queue full (%zu); dropping the oldest toast, from %s", toasts.size(),
                    toasts.front().by.c_str());
        toasts.pop_front();
    }
    toasts.push_back({std::move(text), std::move(detail), std::move(icon), by, n});
    toasts_pending = toasts.size();
    swofty::log("[hud] toast queued by %s for %us: \"%.80s\"", by.c_str(), n, toasts.back().text.c_str());
    return SWOFTY_ASSET_OK;
}

int api_toast(const char *text, const char *detail, float seconds, const char *icon)
{
    return toast_enqueue(text ? text : "", detail ? detail : "", seconds, icon ? icon : "",
                         who(__builtin_return_address(0)));
}

int set_enqueue(std::string path, int type, std::string text, int32_t number, const std::string &by)
{
    if (!path.empty() && path[0] == '#')
        path.erase(0, 1);
    bool listed = std::any_of(std::begin(SET_PREFIXES), std::end(SET_PREFIXES),
                              [&](const char *p) { return path.rfind(p, 0) == 0 && path.size() > strlen(p); });
    if (!listed || path.find_first_of(" []") != std::string::npos || path.back() == '.' || type < 0 || type > 2) {
        swofty::log("[hud] set by %s refused: '%.120s' is not a hud_* leaf (ui_menu_data.hud_*, view_model_data.hud*)",
                    by.c_str(), path.c_str());
        return SWOFTY_ASSET_INVALID;
    }
    if (!api_available())
        return SWOFTY_ASSET_UNAVAILABLE;
    if (text.size() > MAX_TOAST_TEXT)
        text.resize(MAX_TOAST_TEXT);
    std::lock_guard<std::mutex> g(queue_lock);
    for (auto &q : sets)
        if (q.path == path) {
            q = {path, std::move(text), by, type, number};
            return SWOFTY_ASSET_OK;
        }
    if (sets.size() >= MAX_SETS) {
        swofty::log("[hud] set queue full (%zu); dropping the oldest, from %s", sets.size(), sets.front().by.c_str());
        sets.pop_front();
    }
    sets.push_back({std::move(path), std::move(text), by, type, number});
    sets_pending = sets.size();
    return SWOFTY_ASSET_OK;
}

int api_set(const char *path, uint32_t type, const void *value)
{
    std::string by = who(__builtin_return_address(0));
    if (!path || !value)
        return SWOFTY_ASSET_INVALID;
    if (type == 0)
        return set_enqueue(path, 0, static_cast<const char *>(value), 0, by);
    return set_enqueue(path, int(type), "", *static_cast<const int32_t *>(value), by);
}

void apply_set(const Set &q)
{
    bool ok = q.type == 0 ? ui_vm_set_path(q.path, 0, q.text.c_str()) : ui_vm_set_path(q.path, q.type, &q.number);
    static std::atomic<int> told;
    if (!ok || told.fetch_add(1) < 16)
        swofty::log("[hud] set %s by %s: %s = %s%s", ok ? "applied" : "failed", q.by.c_str(), q.path.c_str(),
                    q.type == 0 ? ("\"" + q.text.substr(0, 60) + "\"").c_str() : std::to_string(q.number).c_str(),
                    ok ? "" : " (the parent node did not resolve)");
}

const SwoftyHud hud_table = {SWOFTY_HUD_VERSION, api_available, api_toast, api_set};

unsigned toast_test_every;
uint64_t toast_test_next;
unsigned toast_test_count;

void toast_test_tick()
{
    if (!toast_test_every)
        return;
    uint64_t now = GetTickCount64();
    if (now < toast_test_next)
        return;
    toast_test_next = now + uint64_t(toast_test_every) * 1000;
    char text[64], detail[64];
    snprintf(text, sizeof text, "SWOFTY TOAST %u", ++toast_test_count);
    const char *icon = ICONS[(toast_test_count - 1) % (sizeof ICONS / sizeof *ICONS)];
    snprintf(detail, sizeof detail, "core test, icon %s", icon);
    toast_enqueue(text, detail, 4.0f, icon, "SWOFTY_TOAST_TEST");
}

std::string set_test_text;
uint64_t set_test_next;
std::vector<Set> dev_sets;

void set_test_tick()
{
    if (set_test_text.empty())
        return;
    uint64_t now = GetTickCount64();
    if (now < set_test_next)
        return;
    set_test_next = now + 5000;
    if (set_test_text.size() > 2 && set_test_text[1] == ':') {
        size_t at = 0;
        while (at < set_test_text.size()) {
            size_t end = set_test_text.find(';', at);
            std::string item = set_test_text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? set_test_text.size() : end + 1;
            size_t eq = item.find('=');
            if (item.size() < 3 || item[1] != ':' || eq == std::string::npos)
                continue;
            int type = item[0] == 'b' ? 1 : item[0] == 'i' ? 2 : 0;
            std::string value = item.substr(eq + 1);
            set_enqueue(item.substr(2, eq - 2), type, value, type ? int32_t(atoi(value.c_str())) : 0,
                        "SWOFTY_HUD_SET_TEST");
        }
        return;
    }
    int32_t off = 0;
    set_enqueue("ui_menu_data.hud_lap_number_visible", 1, "", off, "SWOFTY_HUD_SET_TEST");
    set_enqueue("ui_menu_data.hud_lap_description_loc_tag", 1, "", off, "SWOFTY_HUD_SET_TEST");
    set_enqueue("ui_menu_data.hud_lap_description_string", 0, set_test_text, 0, "SWOFTY_HUD_SET_TEST");
    set_enqueue("ui_menu_data.hud_total_lap_count_visible", 1, "", off, "SWOFTY_HUD_SET_TEST");
    set_enqueue("ui_menu_data.hud_top_left_position_visible", 1, "", off, "SWOFTY_HUD_SET_TEST");
}

void dev_set_parse(const std::string &spec)
{
    size_t at = 0;
    while (at < spec.size()) {
        size_t end = spec.find(';', at);
        std::string item = spec.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? spec.size() : end + 1;
        size_t eq = item.find('=');
        if (item.size() < 3 || item[1] != ':' || eq == std::string::npos)
            continue;
        Set q{item.substr(2, eq - 2), item.substr(eq + 1), "SWOFTY_HUD_DEV_SET", 0, 0};
        q.type = item[0] == 'b' ? 1 : item[0] == 'i' ? 2 : 0;
        if (q.type)
            q.number = int32_t(atoi(q.text.c_str()));
        dev_sets.push_back(q);
        swofty::log("[hud] SWOFTY_HUD_DEV_SET: %s = %s every 5 s (dev switch, any path)", q.path.c_str(), q.text.c_str());
    }
}

void dev_set_tick()
{
    static uint64_t next;
    if (dev_sets.empty())
        return;
    uint64_t now = GetTickCount64();
    if (now < next)
        return;
    next = now + 5000;
    for (auto &q : dev_sets)
        apply_set(q);
}

}

extern "C" __declspec(dllexport) const SwoftyHud *SwoftyCoreHud(void)
{
    return &hud_table;
}

namespace runtime {

const SwoftyPopup *popup_table()
{
    return &table;
}

int popup_show(const std::string &title, const std::string &body, const std::string &by)
{
    return enqueue(title, body, by);
}

int hud_toast(const std::string &text, const std::string &detail, float seconds, const std::string &by)
{
    return toast_enqueue(text, detail, seconds, "", by);
}

void popup_setup()
{
    if (!enabled("popup", "popup")) {
        swofty::log("[popup] feature off; popup.show reports unavailable");
        return;
    }
    uintptr_t base = swofty::api->module_base();
    uintptr_t motd = find(S_MOTD, "message system", "popup");
    a_release = find(S_RELEASE, "message system", "popup");
    if (motd) {
        id_vt = reinterpret_cast<const void *>(motd + MOTD_VT_END + *reinterpret_cast<const int32_t *>(motd + MOTD_VT_DISP));
        a_construct = rel32(motd + MOTD_CONSTRUCT);
        a_make_str = rel32(motd + MOTD_MAKE_STR);
        a_set_attr = rel32(motd + MOTD_SET_ATTR);
    }
    if (!motd || !a_release || !swofty::mem::in_image(a_construct) ||
        memcmp(reinterpret_cast<const void *>(a_construct), CONSTRUCT_HEAD, sizeof CONSTRUCT_HEAD)) {
        swofty::log("[popup] the message system did not resolve (poster %s, release %s, constructor %s); popup.show "
                    "reports unavailable",
                    motd ? "found" : "missing", a_release ? "found" : "missing",
                    a_construct ? "shape differs" : "missing");
        return;
    }
    auto r = safetyhook::InlineHook::create(reinterpret_cast<void *>(a_construct), reinterpret_cast<void *>(&construct_hook));
    if (!r) {
        swofty::log("[popup] hook on Ui_Message_Construct at base+0x%llx failed; popup.show reports unavailable",
                    (unsigned long long)(a_construct - base));
        return;
    }
    h_construct = std::move(*r);
    live = true;
    char every[16] = {};
    if (GetEnvironmentVariableA("SWOFTY_TOAST_TEST", every, sizeof every) && atoi(every) > 0) {
        toast_test_every = unsigned(atoi(every));
        toast_test_next = GetTickCount64() + uint64_t(toast_test_every) * 1000;
        swofty::log("[hud] SWOFTY_TOAST_TEST: a test toast every %us", toast_test_every);
    }
    char dev[512] = {};
    if (GetEnvironmentVariableA("SWOFTY_HUD_DEV_SET", dev, sizeof dev) && dev[0])
        dev_set_parse(dev);
    char lap[128] = {};
    if (GetEnvironmentVariableA("SWOFTY_HUD_SET_TEST", lap, sizeof lap) && lap[0]) {
        set_test_text = lap;
        if (lap[1] == ':')
            swofty::log("[hud] SWOFTY_HUD_SET_TEST: writes %s through set every 5 s", lap);
        else
            swofty::log("[hud] SWOFTY_HUD_SET_TEST: the lap counter shows \"%s\", lap totals and the top-left position "
                        "tower hide (set every 5 s)",
                        lap);
    }
    swofty::log("[popup] setup: live; construct base+0x%llx, set attribute base+0x%llx, release base+0x%llx, id vtable "
                "base+0x%llx",
                (unsigned long long)(a_construct - base), (unsigned long long)(a_set_attr - base),
                (unsigned long long)(a_release - base), (unsigned long long)(uintptr_t(id_vt) - base));
}

void popup_tick()
{
    toast_test_tick();
    set_test_tick();
    dev_set_tick();
    if (sets_pending.load(std::memory_order_relaxed)) {
        std::deque<Set> now_sets;
        {
            std::lock_guard<std::mutex> g(queue_lock);
            now_sets.swap(sets);
            sets_pending = 0;
        }
        static const uint32_t set_probe = perf::probe("core", "hud set");
        perf::Scope p(set_probe);
        for (auto &q : now_sets) {
            perf::Pause game;
            apply_set(q);
        }
    }
    if (!pending.load(std::memory_order_relaxed) && !toasts_pending.load(std::memory_order_relaxed))
        return;
    void *s = sys.load();
    if (!s && (s = ui_vm_message_system())) {
        void *was = nullptr;
        if (sys.compare_exchange_strong(was, s))
            swofty::log("[popup] message system %p from the UI system (no message constructed yet)", s);
        else
            s = was;
    }
    if (!s)
        return;
    static const uint32_t probe = perf::probe("core", "popup show");
    perf::Scope p(probe);
    std::deque<Request> now;
    std::deque<Toast> now_toasts;
    {
        std::lock_guard<std::mutex> g(queue_lock);
        now.swap(queue);
        now_toasts.swap(toasts);
        pending = 0;
        toasts_pending = 0;
    }
    for (auto &r : now) {
        perf::Pause game;
        show(s, r);
    }
    for (auto &t : now_toasts) {
        perf::Pause game;
        show_toast(s, t);
    }
}

}
