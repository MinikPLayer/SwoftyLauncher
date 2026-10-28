#include "runtime.hpp"

#include <windows.h>

#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// ERS new-lap guard (re/db/functions.tsv Game_ErsSystem_HandleVehicleNewLap).
//
// The ERS system subscribes Game_ErsSystem_HandleVehicleNewLap to vehicle_newlap, which the per-vehicle
// progress update posts synchronously when a car's lap changes. The handler walks the system's participants
// (self+0x30..+0x38) for the one whose vehicle component (entry+0x28) carries the message's vehicle id at
// +0xe8, then resets that car's lap energy counters. Game_ErsSystem_BindVehicles leaves entry+0x28 at 0 for a
// participant whose entity has no vehicle component, and the ERS update skips those entries, but the new-lap
// handler reads through them: in a Time Trial with such a participant ahead of the player's car, the first
// new lap faults (ACCESS_VIOLATION reading 0xe8 at F1_25.exe+0x25180f7).
//
// site_Game_ErsNewLap_VehicleLookup is the load of entry+0x28 (r9 = the entry). When the entry or its vehicle
// component is null, or the component's vehicle id can't be read, the guard continues at
// site_Game_ErsNewLap_NextVehicle, the handler's own path for an entry that does not match. Every other entry
// runs the game's code unchanged.
//
// SWOFTY_ERS_GUARD=0 turns it off.

namespace {

namespace mem = swofty::mem;

constexpr const char *MIXIN = "swofty_core.dll (ers new lap guard)";
constexpr size_t SELF_BEGIN = 0x30;
constexpr size_t ENTRY_VEHICLE = 0x28;
constexpr size_t VEHICLE_ID = 0xe8;
constexpr int LOG_FIRST = 8;

uintptr_t next_vehicle = 0;
bool offered = false, settled = false;
std::atomic<int> skipped{0};

void lookup_entry(SwoftyContext *c)
{
    uintptr_t vehicle = 0;
    if (c->r9 && mem::read(c->r9 + ENTRY_VEHICLE, vehicle) && vehicle && mem::readable(vehicle + VEHICLE_ID, 1))
        return;
    c->rip = next_vehicle;
    int n = ++skipped;
    if (n > LOG_FIRST)
        return;
    uintptr_t begin = 0;
    mem::read(c->rcx + SELF_BEGIN, begin);
    swofty::log("[ers] vehicle_newlap for vehicle %u: participant %lld of %lld has no vehicle (entry 0x%llx, vehicle "
                "0x%llx); skipped%s",
                unsigned(c->rdx & 0xff), begin ? (long long)(c->rax - begin) / 8 : -1LL,
                begin ? (long long)(c->r8 - begin) / 8 : -1LL, (unsigned long long)c->r9, (unsigned long long)vehicle,
                n == LOG_FIRST ? " (further skips are not logged)" : "");
}

}

namespace runtime {

std::vector<CoreMixins> ers_newlap_mixins()
{
    std::vector<CoreMixins> out;
    if (offered || !code_ready())
        return out;
    offered = true;
    char v[16] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_ERS_GUARD", v, sizeof v);
    if (n && n < sizeof v && (!strcmp(v, "0") || !strcmp(v, "off"))) {
        swofty::log("[ers] new-lap guard off (SWOFTY_ERS_GUARD=%s)", v);
        return out;
    }
    next_vehicle = swofty::api->resolve("site_Game_ErsNewLap_NextVehicle");
    if (!next_vehicle || !swofty::api->resolve("site_Game_ErsNewLap_VehicleLookup")) {
        next_vehicle = 0;
        swofty::log("[ers] new-lap guard off: needs site_Game_ErsNewLap_VehicleLookup and "
                    "site_Game_ErsNewLap_NextVehicle, which this build does not resolve");
        return out;
    }
    out.push_back(
        {MIXIN, {{"site_Game_ErsNewLap_VehicleLookup", SWOFTY_MIXIN_SITE, 100, (void *)&lookup_entry, nullptr}}});
    return out;
}

void ers_newlap_commit()
{
    if (!offered || settled || !next_vehicle)
        return;
    settled = true;
    swofty::log("[ers] new-lap guard %s",
                core_mixins_active(MIXIN)
                    ? "armed: a vehicle_newlap reaching a participant without a vehicle skips it instead of faulting"
                    : "did not install (see the [core] lines)");
}

}
