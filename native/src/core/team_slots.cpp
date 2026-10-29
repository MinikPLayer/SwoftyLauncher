#include "runtime.hpp"

#include <windows.h>

#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>

#include "team_slots_logic.hpp"

// The loaded team records (re/db/subsystems/teams.md#team-record). Sess_Load_Teams stores each available
// entrants.teams row as a 0x768-byte TeamRecord in a stat-id indexed slot vector through
// Team_Insert_RecordByStatId. Sess_Load_Teams (0x142469440) only walks the entrants.teams rows when its set argument
// (bpl) is 0, the database set: for any other set it builds an empty slot vector and posts its message
// (0x1424695af test bpl,bpl / jne 0x14246ca8c), so the insert call fires for the database set alone and the record
// key, stat_id << 16, needs no set byte. site_Team_LoadInsertCall sees the vector (rcx) and the stack record
// (rdx); site_Team_LoadInserted sees the stored record (rax). The core keeps the latest vector and reads a slot's
// record through it when asked, so a reloaded set is never read through a stale pointer, and tells the team applier
// about every record the loader stores so a team it has dressed is dressed again. Nothing the game computes is
// changed here.
//
// SWOFTY_TEAM_SLOTS=0 turns it off.

namespace {

namespace mem = swofty::mem;

constexpr const char *MIXIN = "swofty_core.dll (team record slots)";
constexpr size_t RECORD_SIZE = 0x768;

std::atomic<uintptr_t> database_set{0};
std::atomic<uint32_t> last_key{0};
std::atomic<int> stored{0};
bool offered = false, settled = false, wanted = false;

std::function<void(uint16_t, uint8_t *)> &inserted_hook()
{
    static std::function<void(uint16_t, uint8_t *)> h;
    return h;
}

std::mutex &hook_mu()
{
    static std::mutex m;
    return m;
}

void insert_call(SwoftyContext *c)
{
    uint32_t key = 0;
    if (!c->rdx || !mem::read(c->rdx, key)) {
        last_key = 0xffffffffu;
        return;
    }
    last_key = key;
    if (team_slots::database_key(key))
        database_set = c->rcx;
}

void inserted(SwoftyContext *c)
{
    uint32_t key = last_key;
    if (key == 0xffffffffu || !team_slots::database_key(key) || !c->rax)
        return;
    int n = ++stored;
    if (n == 1)
        swofty::log("[teams] the game stored its first database team record (stat id %u); loaded team records can be reached",
                    unsigned(team_slots::stat_of(key)));
    if (!mem::readable(c->rax, RECORD_SIZE))
        return;
    std::function<void(uint16_t, uint8_t *)> h;
    {
        std::lock_guard g(hook_mu());
        h = inserted_hook();
    }
    if (h)
        h(team_slots::stat_of(key), reinterpret_cast<uint8_t *>(c->rax));
}

}

namespace runtime {

uint8_t *team_record_of(uint16_t stat)
{
    uintptr_t vector = database_set;
    auto read = [](uintptr_t at, uintptr_t &out) { return mem::read(at, out); };
    uintptr_t record = team_slots::record_in(vector, stat, read);
    return record && mem::readable(record, RECORD_SIZE) ? reinterpret_cast<uint8_t *>(record) : nullptr;
}

void team_record_inserted(std::function<void(uint16_t, uint8_t *)> fn)
{
    std::lock_guard g(hook_mu());
    inserted_hook() = std::move(fn);
}

std::vector<CoreMixins> team_slots_mixins()
{
    std::vector<CoreMixins> out;
    if (offered || !code_ready())
        return out;
    offered = true;
    char v[16] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_TEAM_SLOTS", v, sizeof v);
    if (n && n < sizeof v && (!strcmp(v, "0") || !strcmp(v, "off"))) {
        swofty::log("[teams] team record slots off (SWOFTY_TEAM_SLOTS=%s)", v);
        return out;
    }
    if (!swofty::api->resolve("site_Team_LoadInsertCall") || !swofty::api->resolve("site_Team_LoadInserted")) {
        swofty::log("[teams] team record slots off: needs site_Team_LoadInsertCall and site_Team_LoadInserted, which this "
                    "build does not resolve");
        return out;
    }
    wanted = true;
    out.push_back({MIXIN,
                   {{"site_Team_LoadInsertCall", SWOFTY_MIXIN_SITE, 100, (void *)&insert_call, nullptr},
                    {"site_Team_LoadInserted", SWOFTY_MIXIN_SITE, 100, (void *)&inserted, nullptr}}});
    return out;
}

void team_slots_commit()
{
    if (!wanted || settled)
        return;
    settled = true;
    swofty::log("[teams] team record slots %s",
                core_mixins_active(MIXIN) ? "armed: loaded team records are reachable for dressing"
                                          : "did not install (see the [core] lines)");
}

}
