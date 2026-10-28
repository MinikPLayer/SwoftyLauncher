#pragma once
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "json.hpp"
#include "swofty_api.h"

namespace runtime {

enum Stage { STAGE_WINDOW = 1 };

struct Sig {
    const char *what, *pattern;
    uintptr_t rva;
    enum Kind { addr, rel32, riprel } kind = addr;
    uintptr_t insn = 0, anchor = 0, len = 0;
};

struct OverlayErp {
    std::string path;
    uint64_t size = 0;
    size_t resources = 0;
    std::vector<std::string> targets;
};

struct Overlay {
    std::vector<OverlayErp> erps;
    size_t replaced = 0, added = 0;
};

const json::Value &config();
const std::string &config_source();
const std::wstring &game_root();
const std::wstring &swofty_root();
bool enabled(const char *feature, const char *what);
std::string load_stage();
bool same_build();

uintptr_t find(const Sig &s, const char *tag, const char *channel = "assets");
bool readable(const void *p, size_t n);
std::string read_file(const std::wstring &path, size_t limit = size_t(512) << 20);
bool write_file(const std::wstring &path, const std::string &data);
bool file_size(const std::wstring &path, uint64_t *size);
std::wstring wide(const std::string &s);
std::string narrow(const std::wstring &w);
std::wstring under_swofty(const std::string &rel);
std::string lower(std::string s);
uint64_t fnv64(const char *s, size_t n);
inline uint64_t fnv64(const std::string &s) { return fnv64(s.data(), s.size()); }
uint32_t fnv32(const char *s);
void md5(const void *data, size_t n, uint8_t out[16]);
std::string hex16(uint64_t v);

void on_stage(void (*fn)(int stage));
void when_code_ready(const char *what, void (*fn)());
bool code_ready();

const Overlay *resource_overlay(DWORD wait_ms);
void overlay_registered(const std::vector<uint64_t> &erp_hashes, size_t groups);
void overlay_dropped(const std::string &why);
std::string erp_name(uint64_t hash);

const SwoftyAssets *assets_table();
const SwoftyEvents *events_table();
const SwoftyOverlay *overlay_table();
const SwoftyTelemetry *telemetry_table();
const SwoftyDb *db_table();
const SwoftyStorage *storage_table();
const SwoftyCareer *career_table();
const SwoftyPopup *popup_table();
const SwoftyLang *lang_table();
const SwoftyCharacters *characters_table();
const SwoftyCareerWizard *wizard_table();
void lang_override_setup();
const json::Value &sdk();
void ready();
void assets_setup();
void resources_setup();
void gamedb_setup();
void dbread_setup();
void overlay_setup();
void events_setup();
void events_drop_module(void *module);
size_t events_fire(const char *event, int64_t a, int64_t b, const void *data);

struct CoreMixins {
    const char *name;
    std::vector<SwoftyMixin> mixins;
};

enum class TileRoute { pending, live, off };

std::vector<CoreMixins> career_mixins();
std::vector<CoreMixins> characters_mixins();
std::vector<CoreMixins> wizard_mixins();
std::vector<CoreMixins> tile_route_mixins();
void characters_commit();
void wizard_commit();
void tile_route_commit();
void tile_route_want();
TileRoute tile_route_state();
void wizard_tile_routed(const std::string &group, void *out);
bool wizard_route_running(const std::string &route, void *name);
bool core_mixins_active(const char *name);
bool core_mixins_closed();
void career_setup();
void career_tick();
int career_block(const void *from, uint32_t mode, const char *title, const char *body);
int career_unblock(const void *from, uint32_t mode);
void career_start_setup();
void career_start_drop_module(void *module);
bool career_start_live();
uint32_t career_start_picked();
void popup_setup();
void popup_tick();
void mfd_setup();
void mfd_tick();
int popup_show(const std::string &title, const std::string &body, const std::string &by);
int hud_toast(const std::string &text, const std::string &detail, float seconds, const std::string &by);
const SwoftyCareerContent *career_content_table();
std::vector<CoreMixins> career_content_mixins();
void career_content_commit();
void career_content_tick();
void hub_tick();
const SwoftyEra *era_table();
std::vector<CoreMixins> era_mixins();
void era_commit();
std::vector<CoreMixins> car_category_mixins();
void car_category_commit();
std::vector<CoreMixins> save_splice_mixins();
void save_splice_commit();
std::vector<CoreMixins> ers_newlap_mixins();
void ers_newlap_commit();
std::vector<CoreMixins> track_acclim_mixins();
void track_acclim_commit();
void hud_setup();
void focus_setup();
void hud_fatal();
void grade_setup();
void postfx_setup();
void telemetry_setup();
void netplay_setup();
void netplay_bridge_setup();
void netplay_bridge_status_changed();
void netplay_exit();
void netplay_drive_setup();
void netplay_flow(uint32_t hash, uint32_t to);
void netplay_screen(const char *name);
bool netplay_popup(const char *id, void *fn, void *ctx);
void netplay_home_vm(void *vm);
void netplay_tick();
void net_trace_setup();
int group_add(const json::Value &entry);
int db_add(const json::Value &entry);
int lang_add(const json::Value &entry);
int file_add(const json::Value &entry);

struct Robin {
    uint8_t *s;

    template <class T> T at(size_t off) const { return *reinterpret_cast<const T *>(s + off); }
    uint64_t mult() const { return at<uint64_t>(0x10); }
    uint64_t *slots() const { return at<uint64_t *>(0x18); }
    uint8_t *info() const { return at<uint8_t *>(0x20); }
    uint64_t count() const { return at<uint64_t>(0x28); }
    uint64_t mask() const { return at<uint64_t>(0x30); }
    uint32_t inc() const { return at<uint32_t>(0x40); }
    uint32_t shift() const { return at<uint32_t>(0x44); }

    size_t buffer() const
    {
        uint64_t n = mask() + 1, a = n < 0x28f5c28f5c28f5dull ? n * 80 / 100 : (n / 100) * 80;
        return size_t(n + std::min<uint64_t>(a, 0xff));
    }

    bool sane() const
    {
        if (!readable(s, 0x48))
            return false;
        uint64_t n = mask() + 1;
        return n >= 8 && !(n & (n - 1)) && inc() >= 1 && inc() <= 0x20 && slots() && info() &&
               readable(info(), 1) && readable(info() + buffer(), 1) && readable(slots(), 16) &&
               readable(reinterpret_cast<uint8_t *>(slots()) + buffer() * 16 - 16, 16);
    }

    uint64_t *find(uint64_t key) const
    {
        uint64_t h = key * mult();
        h ^= h >> 33;
        uint32_t inf = inc() + ((uint32_t(h) & 0x1f) >> (shift() & 0x3f));
        size_t idx = size_t((h >> 5) & mask());
        const uint8_t *in = info();
        uint64_t *sl = slots();
        do {
            if (inf == in[idx] && sl[idx * 2] == key)
                return &sl[idx * 2];
            ++idx;
            inf += inc();
            if (inf == in[idx] && sl[idx * 2] == key)
                return &sl[idx * 2];
            ++idx;
            inf += inc();
        } while (inf <= in[idx]);
        return nullptr;
    }
};

}
