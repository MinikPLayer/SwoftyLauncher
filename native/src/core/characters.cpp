#include "career_ui.hpp"
#include "registry/older.hpp"

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// Per-career character overrides (SwoftyCharacters). Keyed by career data group; each entry acts
// only while the loaded career's data group is its group, so 2025 and 2026 careers stay stock.
//
// Character refs (Game_EntrantsUi_BuildCharacterRef, Game_EntrantsUi_OnCharacterListLoaded). The UI
// keeps one VM node per entrants.characters row under character_list. The GameCharacterListLoaded
// handler clears that node and rebuilds it from the game's character vector (message +0x10; begin
// +0x08, end +0x10, 0x38-byte rows: stat_id byte +0x00, name string object +0x08 with its text at
// +0x10), calling the ref builder once per row. The builder wrap hands a swapped character a
// stand-in record: the replacement row with the character's stat_id, so names, job and the
// message_<name> textures follow the replacement while lookups by id still find the ref. The
// handler wrap keeps the vector; a career load or unload that changes the active group, or
// refresh(), runs the handler again with it so the list is rebuilt for the new group.
//
// Voice (Game_SpeechSystem_PostLine). The speaker name of a line is the speech system's speaker
// record name (*(speech +0x728) +0x60 + line[+0x80] * 0x88, name at +0x18, below the count at
// speech +0x7cc). A muted speaker's line gets its emitter's (line +0x28) vol_speech RTPC set to 0
// through the emitter's setter (vtable +0x60), then posts as usual, so subtitles and the
// end-of-line callbacks still run.
//
// Strings. Each group's strings are runtime string overrides (lang_override.cpp), set when the
// group becomes active and cleared when it stops being active.
//
// The refs and the voices are core mixin sets of their own (manager.cpp), offered only once an addon
// needs them: the refs after a swap, the voices after a mute, at the next mixin commit for a
// registration from an addon's init and right after the ON_LOAD handlers for one made there. A game
// where no addon swaps or mutes runs none of them, so an addon that wraps these functions itself
// keeps its wraps. A binding that does not resolve drops only that part.

namespace {

namespace cu = runtime::career_ui;
namespace mem = swofty::mem;

constexpr const char *REF_MIXIN = "swofty_core.dll (character refs)";
constexpr const char *VOICE_MIXIN = "swofty_core.dll (character voices)";
constexpr size_t ROW = 0x38, MAX_ROWS = 4096, RTPC_SLOT = 0x60;
constexpr uintptr_t SPEAKERS = 0x728, SPEAKER_RECORDS = 0x60, SPEAKER_STRIDE = 0x88, SPEAKER_NAME = 0x18,
                    SPEAKER_COUNT = 0x7cc, LINE_SPEAKER = 0x80, LINE_EMITTER = 0x28;
constexpr char SPEECH_VOLUME[] = "vol_speech";

using BuildRef = void *(void *out, const uint8_t *character, void *node);
using ListLoaded = int64_t(void *ctx, const void *message);
using PostLine = void(void *speech, void *line, uint8_t retrigger);
using SetRtpc = void(void *emitter, const char *name, float value, int a, float interpolation, int b);

struct Swap {
    std::string group, character, replacement;
    HMODULE owner;
    std::string addon;
};

struct Mute {
    std::string group, speaker;
    HMODULE owner;
    std::string addon;
};

struct Text {
    std::string group, language, key, text;
    HMODULE owner;
    std::string addon;
};

BuildRef *orig_build;
ListLoaded *orig_loaded;
PostLine *orig_post;

std::mutex lock;
std::vector<Swap> swaps, eswaps;
std::vector<Mute> mutes, emutes;
std::vector<Text> texts, etexts;
std::string active;
std::vector<std::pair<std::string, std::string>> applied;
std::set<void *> muted_emitters;
std::atomic<const void *> vector_seen{nullptr};

namespace older = swofty_core::registry::older;

void rebuild_effective()
{
    auto &st = runtime::older_store();
    std::vector<Swap> sw;
    std::vector<Mute> mu;
    std::vector<Text> tx;
    for (auto &id : older::in_load_order(st, older::CHARACTER)) {
        older::CharacterState c = older::character_from(older::effective(st, older::CHARACTER, id));
        if (c.group.empty())
            continue;
        std::string addon = older::addon_of(id);
        for (auto &[character, with] : c.swaps)
            sw.push_back({c.group, character, with, nullptr, addon});
        for (auto &m : c.mutes)
            mu.push_back({c.group, m, nullptr, addon});
        for (auto &[language, kt] : c.strings)
            tx.push_back({c.group, language, kt.first, kt.second, nullptr, addon});
    }
    eswaps = std::move(sw);
    emutes = std::move(mu);
    etexts = std::move(tx);
}

void store_record(const std::string &addon, const std::string &group)
{
    older::CharacterState c;
    c.group = group;
    for (auto &w : swaps)
        if (w.addon == addon && w.group == group)
            c.swaps.push_back({w.character, w.replacement});
    for (auto &m : mutes)
        if (m.addon == addon && m.group == group)
            c.mutes.push_back(m.speaker);
    for (auto &t : texts)
        if (t.addon == addon && t.group == group)
            c.strings.push_back({t.language, {t.key, t.text}});
    auto &st = runtime::older_store();
    if (c.swaps.empty() && c.mutes.empty() && c.strings.empty())
        older::drop(st, addon, older::CHARACTER, group);
    else
        older::put(st, addon, older::CHARACTER, group, older::character_record(c), "characters");
    rebuild_effective();
}
std::atomic<bool> any_swap{false}, any_mute{false};
std::atomic<uint32_t> builds_logged, stand_ins, missing_logged, muted_lines, shared_logged;
struct Part {
    const char *name, *label;
    std::atomic<bool> wanted{false}, offered{false}, settled{false}, live{false}, late_logged{false};
};

Part ref{REF_MIXIN, "ref"}, voice{VOICE_MIXIN, "voice"};
bool subscribed;

std::string name_of(const uint8_t *row)
{
    uintptr_t s = 0;
    std::string out;
    if (row && mem::read(reinterpret_cast<uintptr_t>(row) + 0x08, s) && s)
        if (uintptr_t text = 0; mem::read(s + 0x10, text))
            mem::c_string(text, out, 128);
    return out;
}

size_t rows(const void *vector, const uint8_t **begin)
{
    uintptr_t b = 0, e = 0;
    if (!vector || !mem::read(reinterpret_cast<uintptr_t>(vector) + 0x08, b) ||
        !mem::read(reinterpret_cast<uintptr_t>(vector) + 0x10, e) || !b || e <= b || (e - b) % ROW ||
        (e - b) / ROW > MAX_ROWS)
        return 0;
    if (begin)
        *begin = reinterpret_cast<const uint8_t *>(b);
    return (e - b) / ROW;
}

const uint8_t *find_row(const std::string &name)
{
    const uint8_t *b = nullptr;
    size_t n = rows(vector_seen.load(), &b);
    for (size_t i = 0; i < n; i++)
        if (name_of(b + i * ROW) == name)
            return b + i * ROW;
    return nullptr;
}

std::string replacement_for(const std::string &character)
{
    std::lock_guard<std::mutex> g(lock);
    if (active.empty())
        return {};
    for (auto it = eswaps.rbegin(); it != eswaps.rend(); ++it)
        if (it->group == active && it->character == character)
            return it->replacement;
    return {};
}

bool speaker_muted(const char *speaker)
{
    std::lock_guard<std::mutex> g(lock);
    if (active.empty() || !speaker)
        return false;
    for (auto &m : emutes)
        if (m.group == active && m.speaker == speaker)
            return true;
    return false;
}

void *build_ref(void *out, const uint8_t *character, void *node)
{
    if (any_swap.load() && character) {
        std::string want = replacement_for(name_of(character));
        if (!want.empty()) {
            if (const uint8_t *rep = find_row(want)) {
                uint8_t stand_in[ROW];
                memcpy(stand_in, rep, ROW);
                stand_in[0] = character[0];
                stand_ins++;
                return orig_build(out, stand_in, node);
            }
            if (missing_logged++ < 4)
                swofty::log("[characters] no '%s' row in the character list; '%s' keeps its own ref", want.c_str(),
                            name_of(character).c_str());
        }
    }
    return orig_build(out, character, node);
}

int64_t list_loaded(void *ctx, const void *message)
{
    uintptr_t vector = 0;
    if (message && mem::read(reinterpret_cast<uintptr_t>(message) + 0x10, vector) && vector)
        vector_seen = reinterpret_cast<const void *>(vector);
    if (builds_logged++ < 8) {
        std::string group;
        {
            std::lock_guard<std::mutex> g(lock);
            group = active;
        }
        swofty::log("[characters] character list built from %p (%zu rows, group '%s')", (void *)vector,
                    rows(reinterpret_cast<const void *>(vector), nullptr), group.c_str());
    }
    return orig_loaded(ctx, message);
}

const char *speaker_of(const void *speech, const void *line)
{
    uint64_t index = 0;
    uint32_t count = 0;
    uintptr_t table = 0, record = 0, name = 0;
    auto s = reinterpret_cast<uintptr_t>(speech), l = reinterpret_cast<uintptr_t>(line);
    if (!mem::read(l + LINE_SPEAKER, index) || !mem::read(s + SPEAKER_COUNT, count) || index >= count ||
        !mem::read(s + SPEAKERS, table) || !table ||
        !mem::read(table + SPEAKER_RECORDS + index * SPEAKER_STRIDE, record) || !record ||
        !mem::read(record + SPEAKER_NAME, name) || !mem::readable(name, 1))
        return nullptr;
    return reinterpret_cast<const char *>(name);
}

void post_line(void *speech, void *line, uint8_t retrigger)
{
    if (any_mute.load() && speech && line) {
        const char *speaker = speaker_of(speech, line);
        uintptr_t emitter = 0, vt = 0, fn = 0;
        mem::read(reinterpret_cast<uintptr_t>(line) + LINE_EMITTER, emitter);
        if (emitter && speaker_muted(speaker) && mem::read(emitter, vt) && mem::in_image(vt) &&
            mem::read(vt + RTPC_SLOT, fn) && mem::in_image(fn)) {
            reinterpret_cast<SetRtpc *>(fn)(reinterpret_cast<void *>(emitter), SPEECH_VOLUME, 0.f, 0, 0.f, 0);
            bool fresh;
            {
                std::lock_guard<std::mutex> g(lock);
                fresh = muted_emitters.insert(reinterpret_cast<void *>(emitter)).second;
            }
            uint32_t n = ++muted_lines;
            if (fresh || n <= 8)
                swofty::log("[characters] %s line muted (emitter %p, %u so far)", speaker, (void *)emitter, n);
        } else if (emitter) {
            bool shared;
            {
                std::lock_guard<std::mutex> g(lock);
                shared = muted_emitters.count(reinterpret_cast<void *>(emitter)) != 0;
            }
            if (shared && shared_logged++ < 8)
                swofty::log("[characters] emitter %p, muted earlier, also speaks %s; that line is silent too",
                            (void *)emitter, speaker ? speaker : "(unknown)");
        }
    }
    orig_post(speech, line, retrigger);
}

void apply_strings(const std::string &group)
{
    const SwoftyLang *lg = runtime::lang_table();
    std::vector<std::pair<std::string, std::string>> drop;
    std::vector<Text> set;
    {
        std::lock_guard<std::mutex> g(lock);
        drop.swap(applied);
        for (auto &t : etexts)
            if (t.group == group)
                set.push_back(t);
        if (lg && lg->available())
            for (auto &t : set)
                applied.push_back({t.language, t.key});
    }
    if (drop.empty() && set.empty())
        return;
    if (!lg || !lg->available()) {
        if (!set.empty())
            swofty::log("[characters] %zu string(s) for '%s' stay stock: the core's string overrides are unavailable",
                        set.size(), group.c_str());
        return;
    }
    size_t failed = 0;
    for (auto &[language, key] : drop)
        failed += lg->override_clear(language.c_str(), key.c_str()) != SWOFTY_ASSET_OK;
    for (auto &t : set)
        failed += lg->override_set(t.language.c_str(), t.key.c_str(), t.text.c_str()) != SWOFTY_ASSET_OK;
    swofty::log("[characters] strings: cleared %zu, set %zu for '%s' (%zu failed)", drop.size(), set.size(),
                group.c_str(), failed);
}

bool rebuild(const char *why)
{
    const void *vector = vector_seen.load();
    if (!ref.live || !rows(vector, nullptr)) {
        swofty::log("[characters] %s; the character list is not rebuilt (%s)", why,
                    ref.live ? "the game has not built it yet" : "the ref hooks are not installed");
        return false;
    }
    uint8_t message[0x18] = {};
    memcpy(message + 0x10, &vector, sizeof vector);
    auto handler = reinterpret_cast<ListLoaded *>(swofty::api->resolve("Game_EntrantsUi_OnCharacterListLoaded"));
    if (!handler)
        return false;
    uint32_t before = stand_ins.load();
    handler(nullptr, message);
    swofty::log("[characters] %s; character list rebuilt (%u stand-in ref(s))", why, stand_ins.load() - before);
    return true;
}

bool group_used(const std::string &group)
{
    for (auto &s : eswaps)
        if (s.group == group)
            return true;
    return false;
}

void set_active(std::string now, const char *why)
{
    bool refs;
    {
        std::lock_guard<std::mutex> g(lock);
        if (now == active)
            return;
        bool before = group_used(active);
        rebuild_effective();
        refs = before || group_used(now);
        active = now;
        muted_emitters.clear();
    }
    swofty::log("[characters] %s: active group '%s'", why, now.c_str());
    apply_strings(now);
    if (refs)
        rebuild(why);
}

void on_career(const SwoftyEvent *ev, void *)
{
    bool loaded = !strcmp(ev->name, SWOFTY_EVENT_CAREER_LOADED);
    set_active(loaded ? cu::loaded_group() : std::string(), loaded ? "career loaded" : "career unloaded");
}

void subscribe()
{
    if (subscribed)
        return;
    subscribed = true;
    const SwoftyEvents *ev = runtime::events_table();
    uint32_t token = 0;
    for (const char *name : {SWOFTY_EVENT_CAREER_LOADED, SWOFTY_EVENT_CAREER_UNLOADED})
        if (!ev || ev->subscribe(name, on_career, nullptr, &token) != SWOFTY_ASSET_OK)
            swofty::log("[characters] subscribe %s failed; entries apply after refresh() only", name);
}

void changed()
{
    bool s = false, m = false;
    {
        std::lock_guard<std::mutex> g(lock);
        s = !swaps.empty();
        m = !mutes.empty();
    }
    any_swap = s;
    any_mute = m;
}

int need(Part &p, bool want, const char *what)
{
    if (!runtime::enabled("career", nullptr))
        return SWOFTY_ASSET_UNAVAILABLE;
    if (want)
        p.wanted = true;
    if (p.live || (!p.settled && !runtime::core_mixins_closed()))
        return SWOFTY_ASSET_OK;
    if (want && !p.settled && !p.late_logged.exchange(true))
        swofty::log("[characters] %s registered after the last mixin commit; the %s hooks are not installed", what,
                    p.label);
    return SWOFTY_ASSET_UNAVAILABLE;
}

int api_available()
{
    return runtime::enabled("career", nullptr) && (ref.live || voice.live || !(ref.settled || voice.settled));
}

int api_career_group(char *out, size_t cap)
{
    std::lock_guard<std::mutex> g(lock);
    return cu::copy_out(active, out, cap);
}

int api_swap(const char *group, const char *character, const char *replacement)
{
    if (!cu::valid_name(group) || !cu::valid_name(character) || (replacement && !cu::valid_name(replacement)))
        return SWOFTY_ASSET_INVALID;
    HMODULE owner = cu::caller(__builtin_return_address(0));
    std::string addon = runtime::older_addon(__builtin_return_address(0));
    bool now;
    {
        std::lock_guard<std::mutex> g(lock);
        std::erase_if(swaps, [&](const Swap &s) { return s.group == group && s.character == character && s.owner == owner; });
        for (auto &s : swaps)
            if (s.group == group && s.character == character)
                swofty::log("[characters] '%s' in '%s' is also swapped to '%s' by another addon; the last one wins",
                            character, group, s.replacement.c_str());
        if (replacement)
            swaps.push_back({group, character, replacement, owner, addon});
        store_record(addon, group);
        now = active == group;
    }
    changed();
    subscribe();
    swofty::log("[characters] %s '%s' in careers of '%s'%s%s", replacement ? "swap" : "restore", character, group,
                replacement ? " to " : "", replacement ? replacement : "");
    int r = need(ref, replacement != nullptr, "a swap");
    return r == SWOFTY_ASSET_OK && now ? SWOFTY_ASSET_LATE : r;
}

int api_mute(const char *group, const char *speaker, int on)
{
    if (!cu::valid_name(group) || !cu::valid_name(speaker))
        return SWOFTY_ASSET_INVALID;
    HMODULE owner = cu::caller(__builtin_return_address(0));
    std::string addon = runtime::older_addon(__builtin_return_address(0));
    {
        std::lock_guard<std::mutex> g(lock);
        std::erase_if(mutes, [&](const Mute &m) { return m.group == group && m.speaker == speaker && m.owner == owner; });
        if (on)
            mutes.push_back({group, speaker, owner, addon});
        store_record(addon, group);
    }
    changed();
    subscribe();
    swofty::log("[characters] %s %s lines in careers of '%s'", on ? "mute" : "unmute", speaker, group);
    return need(voice, on != 0, "a mute");
}

bool add_text(const std::string &group, const std::string &language, const std::string &key, const char *text,
              HMODULE owner, const std::string &addon)
{
    std::erase_if(texts, [&](const Text &t) {
        return t.group == group && t.language == language && t.key == key && t.owner == owner;
    });
    if (text)
        texts.push_back({group, language, key, text, owner, addon});
    return active == group;
}

std::string language_of(const char *language)
{
    return !language || !*language ? "*" : language;
}

int api_string_set(const char *group, const char *language, const char *key, const char *text)
{
    if (!cu::valid_name(group) || !cu::valid_name(key))
        return SWOFTY_ASSET_INVALID;
    HMODULE owner = cu::caller(__builtin_return_address(0));
    std::string addon = runtime::older_addon(__builtin_return_address(0));
    bool now;
    {
        std::lock_guard<std::mutex> g(lock);
        now = add_text(group, language_of(language), key, text, owner, addon);
        store_record(addon, group);
    }
    subscribe();
    if (now)
        apply_strings(group);
    return SWOFTY_ASSET_OK;
}

int api_strings_json(const char *group, const char *text, size_t len)
{
    if (!cu::valid_name(group) || !text)
        return SWOFTY_ASSET_INVALID;
    json::Value doc;
    if (!json::parse(std::string(text, len), doc) || doc.type != json::Value::Obj)
        return SWOFTY_ASSET_INVALID;
    for (auto &[language, keys] : doc.o)
        if (keys.type != json::Value::Obj)
            return SWOFTY_ASSET_INVALID;
    HMODULE owner = cu::caller(__builtin_return_address(0));
    std::string addon = runtime::older_addon(__builtin_return_address(0));
    size_t n = 0;
    bool now = false;
    {
        std::lock_guard<std::mutex> g(lock);
        for (auto &[language, keys] : doc.o)
            for (auto &[key, value] : keys.o)
                if (value.type == json::Value::Str && cu::valid_name(key.c_str())) {
                    now = add_text(group, language_of(language.c_str()), key, value.s.c_str(), owner, addon);
                    n++;
                }
        store_record(addon, group);
    }
    subscribe();
    swofty::log("[characters] %zu string(s) in %zu language(s) for careers of '%s'", n, doc.o.size(), group);
    if (now)
        apply_strings(group);
    return SWOFTY_ASSET_OK;
}

int api_clear(const char *group)
{
    if (!cu::valid_name(group))
        return SWOFTY_ASSET_INVALID;
    HMODULE owner = cu::caller(__builtin_return_address(0));
    std::string addon = runtime::older_addon(__builtin_return_address(0));
    bool now;
    {
        std::lock_guard<std::mutex> g(lock);
        std::erase_if(swaps, [&](const Swap &s) { return s.group == group && s.owner == owner; });
        std::erase_if(mutes, [&](const Mute &m) { return m.group == group && m.owner == owner; });
        std::erase_if(texts, [&](const Text &t) { return t.group == group && t.owner == owner; });
        store_record(addon, group);
        now = active == group;
    }
    changed();
    if (now)
        apply_strings(group);
    return SWOFTY_ASSET_OK;
}

int api_refresh()
{
    std::string now = cu::loaded_group();
    {
        std::lock_guard<std::mutex> g(lock);
        if (now != active) {
            active.swap(now);
            muted_emitters.clear();
            now = active;
        }
    }
    apply_strings(now);
    return rebuild("refresh") ? SWOFTY_ASSET_OK : SWOFTY_ASSET_LATE;
}

const SwoftyCharacters table = {api_available, api_career_group, api_swap,  api_mute,
                                api_string_set, api_strings_json, api_clear, api_refresh};

}

namespace runtime {

const SwoftyCharacters *characters_table()
{
    return &table;
}

std::vector<CoreMixins> characters_mixins()
{
    std::vector<CoreMixins> out;
    if (!enabled("career", nullptr))
        return out;
    if (ref.wanted && !ref.offered.exchange(true))
        out.push_back(
            {REF_MIXIN,
             {{"Game_EntrantsUi_BuildCharacterRef", SWOFTY_MIXIN_WRAP, 100, (void *)&build_ref, (void **)&orig_build},
              {"Game_EntrantsUi_OnCharacterListLoaded", SWOFTY_MIXIN_WRAP, 100, (void *)&list_loaded,
               (void **)&orig_loaded}}});
    if (voice.wanted && !voice.offered.exchange(true))
        out.push_back(
            {VOICE_MIXIN, {{"Game_SpeechSystem_PostLine", SWOFTY_MIXIN_WRAP, 100, (void *)&post_line, (void **)&orig_post}}});
    return out;
}

void characters_commit()
{
    bool any = false;
    for (Part *p : {&ref, &voice})
        if (p->offered && !p->settled) {
            p->live = core_mixins_active(p->name);
            p->settled = any = true;
        }
    auto state = [](const Part &p) { return p.live ? "armed" : p.settled ? "off" : "not used"; };
    if (any)
        swofty::log("[characters] setup: character refs %s, voices %s", state(ref), state(voice));
}

}
