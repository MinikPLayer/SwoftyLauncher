#include "runtime.hpp"

#include <windows.h>

#include <swofty/game.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "layout.hpp"
#include "save_splice_logic.hpp"

// Save splice: keeps the save containers of absent addons through a profile save
// (re/db/subsystems/game.md#save-containers, #save-splice).
//
// The profile save is one tree of save-data nodes under the root save_game. Every game mode with
// register_save_container hangs its container, named by save_data.name, under the root, and that
// container holds every career of the mode. A career content addon with its own mode container
// (driver_career_progressive, driver_career_1990) is a top-level child of the root, so in a session
// without the addon nobody claims it: the load drops it and the next profile save writes the root
// without it.
//
// Load (site_Game_SaveData_UnclaimedChild, inside Game_SaveData_DeserialiseContainer): for every child
// bag of the root that matched no registered child, the core writes the bag with the game's own bag
// writer (Game_SaveData_WriteBag) into memory the core owns, first through a counting stream for the
// size, then through a memory write stream, the way the game stores a career in a slot. Nothing goes
// into the game's SAVE_DATA heap. The wrap of Game_SaveData_DeserialiseContainer empties the store
// when a root load starts.
//
// Save (Game_SaveData_SerialiseContainer_2, the inline profile save): site_Game_SaveData_ContainersCount
// adds the kept containers to the root's containers count before it is written, and
// site_Game_SaveData_ContainersChildren writes each kept container right after it: its name (stream
// write_length and write, as the string writer does), the bag type id the block uses and the captured
// bytes. The reader reads them back as the same bags, so a mode that comes back with its addon loads
// its careers again. A failed write stores the error in the result and continues at
// site_Game_SaveData_ContainersDone, the game's own path for a failed write in that block.
//
// SWOFTY_SAVE_SPLICE=0 turns it off.

namespace {

namespace mem = swofty::mem;
namespace g = swofty::game;
namespace sp = runtime::save_splice;
namespace layout = runtime::layout;

constexpr const char *MIXIN = "swofty_core.dll (save splice)";
constexpr const char *SLOT_MIXIN = "swofty_core.dll (save slot)";
constexpr size_t CHILD_SLOT = 0x88;
constexpr size_t FRAME_SELF = 0x30;
constexpr size_t FRAME_CHILDREN = 0x38;
constexpr size_t MAX_CHILDREN = 4096;
constexpr size_t FRAME_STREAM = 0x50;
constexpr size_t FRAME_BAG_TYPE = 0x58;
constexpr size_t FRAME_COUNT = 0x90;
constexpr int WRITE_FAILED = 0x58;

struct Bindings {
    uintptr_t root_vt = 0, write_bag = 0, lookup = 0, codec_vt = 0, counting_vt = 0, memory_vt = 0, table = 0,
              bag_type = 0, done = 0;
} b;

bool offered = false, settled = false, live = false;
std::mutex lock;
sp::Store store;
sp::Owed owed;
uint32_t loads = 0;

// ---- W4 career snapshots (native/src/core/snapshots/): the core's own swofty_registry entry rides in
// this store like any unclaimed container. The game never claims it, so a load keeps it and a save
// writes it back; the hooks hand its body to the snapshots at each root load and take a new one at
// each profile save. ----
runtime::save_splice::EntryHooks entry_hooks;
bool entry_put = false;
// ---- end W4 ----

using WriteBag_t = int (*)(void *codec, void *bag);
using Lookup_t = const char *(*)(void *table, uint64_t hash);
using Get_t = void *(*)(void *self);
using Id_t = int (*)(void *self);
using Offset_t = size_t (*)(void *self);
using Write_t = int (*)(void *stream, const void *data, uint64_t size);
using WriteLength_t = int (*)(void *stream, uint32_t length);
using Deserialise_t = int *(*)(void *self, int *result, void *bag, uint8_t reset_on_error, uint64_t groups);

Deserialise_t deserialise_orig = nullptr;

void *method(const void *obj, size_t offset)
{
    uintptr_t vt = 0, fn = 0;
    if (!obj || !mem::read(reinterpret_cast<uintptr_t>(obj), vt) || !mem::in_image(vt) || !mem::read(vt + offset, fn) ||
        !mem::in_image(fn))
        return nullptr;
    return reinterpret_cast<void *>(fn);
}

bool is_root(uintptr_t self)
{
    uintptr_t vt = 0;
    return b.root_vt && self && mem::read(self, vt) && vt == b.root_vt;
}

template <class PV = g::NePropertyVtbl, class TV = g::NePropertyTypeVtbl>
bool read_property(void *prop, uint64_t &hash, void *&bag, std::string &why)
{
    if constexpr (layout::property<PV, TV>) {
        auto type_of = reinterpret_cast<Get_t>(method(prop, offsetof(PV, type)));
        auto value_of = reinterpret_cast<Get_t>(method(prop, offsetof(PV, value)));
        if (!type_of || !value_of)
            return why = "its property has no readable vtable", false;
        void *type = type_of(prop);
        auto key_offset = reinterpret_cast<Offset_t>(method(type, offsetof(TV, key_offset)));
        auto id = reinterpret_cast<Id_t>(method(type, offsetof(TV, id)));
        auto bag_id = reinterpret_cast<Id_t>(method(reinterpret_cast<void *>(b.bag_type), offsetof(TV, id)));
        if (!key_offset || !id || !bag_id)
            return why = "its value type has no readable vtable", false;
        size_t at = key_offset(type);
        if (at > 0x1000 || !mem::read(reinterpret_cast<uintptr_t>(prop) + at, hash))
            return why = "its key hash is not readable", false;
        if (id(type) != bag_id(reinterpret_cast<void *>(b.bag_type)))
            return why = "it is not a bag", false;
        bag = value_of(prop);
        return bag ? true : (why = "it has no bag", false);
    } else {
        (void)prop, (void)hash, (void)bag;
        return why = "this core lacks the property layout", false;
    }
}

template <class C = g::NeBagCodec, class CS = g::NeCountingStream, class MS = g::NeMemWriteStream,
          class SV = g::NeStreamVtbl>
bool encode(void *bag, std::vector<uint8_t> &out, std::string &why)
{
    if constexpr (layout::save_streams<C, CS, MS, SV>) {
        static_assert(sizeof(CS) <= 0x30 && sizeof(MS) <= 0x28 && sizeof(C) <= 0x10);
        auto write_bag = reinterpret_cast<WriteBag_t>(b.write_bag);
        alignas(16) std::array<uint8_t, 0x30> counting{};
        alignas(16) std::array<uint8_t, 0x10> codec{};
        auto *cs = reinterpret_cast<CS *>(counting.data());
        auto *c = reinterpret_cast<C *>(codec.data());
        cs->vtable = reinterpret_cast<void **>(b.counting_vt);
        c->vtable = reinterpret_cast<void **>(b.codec_vt);
        c->stream = cs;
        if (int r = write_bag(c, bag))
            return why = "the size pass failed with code " + std::to_string(r), false;
        uint64_t size = cs->count;
        if (size < 16 || size > sp::MAX_BODY)
            return why = "its size " + std::to_string(size) + " is out of range", false;
        out.assign(size_t(size), 0);
        alignas(16) std::array<uint8_t, 0x28> memory{};
        auto *ms = reinterpret_cast<MS *>(memory.data());
        ms->vtable = reinterpret_cast<void **>(b.memory_vt);
        ms->data = out.data();
        ms->capacity = size;
        c->stream = ms;
        if (int r = write_bag(c, bag))
            return why = "the write pass failed with code " + std::to_string(r), false;
        if (ms->position != size)
            return why = "the write pass wrote " + std::to_string(ms->position) + " of " + std::to_string(size) +
                         " bytes",
                   false;
        return true;
    } else {
        (void)bag, (void)out;
        return why = "this core lacks the stream layout", false;
    }
}

std::string name_of(uint64_t hash)
{
    uintptr_t table = 0;
    if (!mem::read(b.table, table) || !table)
        return {};
    const char *s = reinterpret_cast<Lookup_t>(b.lookup)(reinterpret_cast<void *>(table), hash);
    std::string out;
    return s && mem::c_string(s, out, sp::NAME_LIMIT + 1) ? out : std::string();
}

int *deserialise_entry(void *self, int *result, void *bag, uint8_t reset_on_error, uint64_t groups)
{
    bool root = is_root(reinterpret_cast<uintptr_t>(self));
    if (root) {
        std::lock_guard<std::mutex> g(lock);
        store.begin_load();
        ++loads;
    }
    int *r = deserialise_orig(self, result, bag, reset_on_error, groups);
    if (root) {
        // W4: the swofty_registry body this load kept, for the career snapshots.
        std::vector<uint8_t> entry;
        bool has_entry = false;
        {
            std::lock_guard<std::mutex> g(lock);
            if (store.size())
                swofty::log("[saves] profile load %u: kept %zu save container(s) no loaded game mode claims (%llu "
                            "bytes); the next profile save writes them back",
                            loads, store.size(), (unsigned long long)store.total());
            for (auto &k : store.kept())
                if (k.name == sp::ENTRY_NAME)
                    entry = k.body, has_entry = true;
        }
        if (live && entry_hooks.loaded)
            entry_hooks.loaded(has_entry ? &entry : nullptr);
    }
    return r;
}

void unclaimed_entry(SwoftyContext *c)
{
    if (!live || !is_root(c->rdi))
        return;
    uintptr_t slot = 0;
    void *prop = nullptr;
    std::string why, name;
    uint64_t hash = 0;
    void *bag = nullptr;
    std::vector<uint8_t> bytes;
    bool ok = mem::read(c->rsp + CHILD_SLOT, slot) && mem::read(slot, prop) && prop;
    if (!ok)
        why = "the current child is not readable";
    ok = ok && read_property(prop, hash, bag, why);
    if (ok) {
        name = name_of(hash);
        if (!sp::name_ok(name)) {
            why = "its name does not resolve";
            ok = false;
        }
    }
    ok = ok && encode(bag, bytes, why);
    std::lock_guard<std::mutex> g(lock);
    if (!ok) {
        swofty::log("[saves] profile load %u: could not keep the unclaimed save container %s (hash %016llx): %s; the "
                    "game drops it",
                    loads, name.empty() ? "?" : name.c_str(), (unsigned long long)hash, why.c_str());
        return;
    }
    size_t size = bytes.size();
    switch (store.add(name, std::move(bytes))) {
    case sp::Add::kept:
    case sp::Add::replaced:
        swofty::log("[saves] profile load %u: kept %s (%zu bytes), which no loaded game mode claims", loads, name.c_str(),
                    size);
        break;
    default:
        swofty::log("[saves] profile load %u: could not keep %s (%zu bytes): the store is full; the game drops it", loads,
                    name.c_str(), size);
    }
}

template <class S = g::SaveDataContainer> bool child_names(uintptr_t begin, uintptr_t end, std::vector<std::string> &out)
{
    if constexpr (layout::named_container<S>) {
        if (end < begin || (end - begin) % 8 || (end - begin) / 8 > MAX_CHILDREN)
            return false;
        for (uintptr_t at = begin; at < end; at += 8) {
            uintptr_t child = 0, name = 0;
            std::string n;
            if (!mem::read(at, child) || !mem::read(child + offsetof(S, name), name) ||
                !mem::c_string(name, n, 256))
                return false;
            out.push_back(n);
        }
        return true;
    } else {
        (void)begin, (void)end, (void)out;
        return false;
    }
}

// W4b: Game_SaveSlotManager_SelectSlot stored the slot it loads at manager+0x280; rbx = that SaveSlotInfo
// (slot_id +0x0, slot_guid +0x8), rsi = the manager (re/db/structs/SaveSlotInfo.h, SaveSlotManager.h).
void slot_selected(SwoftyContext *c)
{
    uint64_t id = 0, guid = 0;
    if (c->rbx && mem::read(c->rbx, id) && mem::read(c->rbx + 8, guid))
        runtime::snapshots_slot_selected(c->rsi, id, guid);
}

void count_entry(SwoftyContext *c)
{
    uintptr_t self = 0;
    if (!live || !mem::read(c->rsp + FRAME_SELF, self) || !is_root(self))
        return;
    // W4: the career snapshots' new swofty_registry body, when they changed since the last write.
    std::vector<uint8_t> entry;
    bool has_entry = entry_hooks.saving && entry_hooks.saving(entry);
    std::lock_guard<std::mutex> g(lock);
    owed.take(nullptr);
    if (entry_put && entry_hooks.written)
        entry_hooks.written(false);
    entry_put = false;
    if (has_entry) {
        size_t size = entry.size();
        sp::Add a = store.add(sp::ENTRY_NAME, std::move(entry));
        entry_put = a == sp::Add::kept || a == sp::Add::replaced;
        if (!entry_put) {
            swofty::log("[saves] profile save: the career snapshots' %s entry (%zu bytes) does not fit the save "
                        "splice's store; not written this time",
                        sp::ENTRY_NAME, size);
            if (entry_hooks.written)
                entry_hooks.written(false);
        }
    }
    uint64_t count = 0;
    uintptr_t begin = 0;
    if (!store.size() || !mem::read(c->rsp + FRAME_COUNT, count) || !mem::read(c->rsp + FRAME_CHILDREN, begin))
        return;
    std::vector<std::string> registered;
    if (!child_names(begin, c->r15, registered) || registered.size() != count) {
        swofty::log("[saves] profile save: the root's children are not readable; the kept save containers are not "
                    "written back this time");
        return;
    }
    std::vector<size_t> picked = sp::pick(store.kept(), registered);
    if (picked.size() != store.size())
        swofty::log("[saves] profile save: %zu kept save container(s) now have a registered container of the same name; "
                    "those are not written back",
                    store.size() - picked.size());
    if (picked.empty())
        return;
    *reinterpret_cast<uint64_t *>(c->rsp + FRAME_COUNT) =
        owed.bump(reinterpret_cast<const void *>(c->r12), count, std::move(picked));
}

template <class SV = g::NeStreamVtbl>
int write_kept(void *stream, uint32_t bag_type, const std::vector<size_t> &picked, std::string &names)
{
    if constexpr (layout::save_streams<g::NeBagCodec, g::NeCountingStream, g::NeMemWriteStream, SV>) {
        auto write = reinterpret_cast<Write_t>(method(stream, offsetof(SV, write)));
        auto write_length = reinterpret_cast<WriteLength_t>(method(stream, offsetof(SV, write_length)));
        if (!write || !write_length)
            return WRITE_FAILED;
        for (size_t i : picked) {
            if (i >= store.size())
                return WRITE_FAILED;
            const sp::Kept &k = store.kept()[i];
            int r = write_length(stream, uint32_t(k.name.size()));
            if (!r)
                r = write(stream, k.name.data(), k.name.size());
            if (!r)
                r = write(stream, &bag_type, sizeof bag_type);
            if (!r)
                r = write(stream, k.body.data(), k.body.size());
            if (r)
                return r;
            names += (names.empty() ? "" : ", ") + k.name;
        }
        return 0;
    } else {
        (void)stream, (void)bag_type, (void)picked, (void)names;
        return WRITE_FAILED;
    }
}

void children_entry(SwoftyContext *c)
{
    uintptr_t self = 0;
    if (!live || !mem::read(c->rsp + FRAME_SELF, self) || !is_root(self))
        return;
    std::lock_guard<std::mutex> g(lock);
    std::vector<size_t> picked = owed.take(reinterpret_cast<const void *>(c->r12));
    if (picked.empty())
        return;
    void *stream = nullptr;
    uint32_t bag_type = 0;
    mem::read(c->rsp + FRAME_STREAM, stream);
    mem::read(c->rsp + FRAME_BAG_TYPE, bag_type);
    std::string names;
    int r = write_kept(stream, bag_type, picked, names);
    // W4: whether the career snapshots' entry went out with this save.
    bool entry_written = false;
    if (entry_put) {
        for (size_t i : picked)
            entry_written |= i < store.size() && store.kept()[i].name == sp::ENTRY_NAME;
        entry_put = false;
        if (entry_hooks.written)
            entry_hooks.written(!r && entry_written);
    }
    if (r) {
        *reinterpret_cast<int *>(c->r12) = r;
        c->rip = b.done;
        swofty::log("[saves] profile save: writing back the kept save containers failed with code %d; the save fails "
                    "as on any write error",
                    r);
        return;
    }
    swofty::log("[saves] profile save: wrote back %zu save container(s) no loaded game mode claims: %s", picked.size(),
                names.c_str());
}

uintptr_t need(const char *id, std::string &missing)
{
    uintptr_t a = swofty::api->resolve(id);
    if (!a)
        missing += std::string(missing.empty() ? "" : ", ") + id;
    return a;
}

}

namespace runtime {

std::vector<CoreMixins> save_splice_mixins()
{
    std::vector<CoreMixins> out;
    if (offered || !code_ready())
        return out;
    offered = true;
    // W4b: the career id's save slot, whether or not the splice runs.
    out.push_back({SLOT_MIXIN, {{"site_Game_SaveSlot_Selected", SWOFTY_MIXIN_SITE, 100, (void *)&slot_selected, nullptr}}});
    char v[16] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_SAVE_SPLICE", v, sizeof v);
    if (n && n < sizeof v && (!strcmp(v, "0") || !strcmp(v, "off"))) {
        swofty::log("[saves] save splice off (SWOFTY_SAVE_SPLICE=%s)", v);
        return out;
    }
    if (!layout::SAVE_SPLICE) {
        swofty::log("[saves] save splice off: this core was built without the save stream and property layouts");
        return out;
    }
    std::string missing;
    b.root_vt = need("vt_Game_SaveDataRootContainer", missing);
    b.write_bag = need("Game_SaveData_WriteBag", missing);
    b.lookup = need("Ne_StringTable_Lookup", missing);
    b.codec_vt = need("k_Ne_BagCodecVtbl", missing);
    b.counting_vt = need("k_Ne_CountingStreamVtbl", missing);
    b.memory_vt = need("k_Ne_MemWriteStreamVtbl", missing);
    b.table = need("g_Ne_StringTable", missing);
    b.bag_type = need("k_Ne_PropertyType_Bag", missing);
    b.done = need("site_Game_SaveData_ContainersDone", missing);
    if (!missing.empty()) {
        swofty::log("[saves] save splice off: needs %s, which this build does not resolve", missing.c_str());
        return out;
    }
    out.push_back({MIXIN,
                   {{"Game_SaveData_DeserialiseContainer", SWOFTY_MIXIN_WRAP, 100, (void *)&deserialise_entry,
                     (void **)&deserialise_orig},
                    {"site_Game_SaveData_UnclaimedChild", SWOFTY_MIXIN_SITE, 100, (void *)&unclaimed_entry, nullptr},
                    {"site_Game_SaveData_ContainersCount", SWOFTY_MIXIN_SITE, 100, (void *)&count_entry, nullptr},
                    {"site_Game_SaveData_ContainersChildren", SWOFTY_MIXIN_SITE, 100, (void *)&children_entry,
                     nullptr}}});
    return out;
}

void save_splice_commit()
{
    if (!offered || settled || !b.done) {
        snapshots_start(false);
        return;
    }
    settled = true;
    live = core_mixins_active(MIXIN);
    swofty::log("[saves] save splice %s",
                live ? "armed: save containers no loaded game mode claims are kept through each profile load and save"
                     : "did not install (see the [core] lines)");
    snapshots_start(live);
}

// ---- W4 career snapshots ----
namespace save_splice {

void set_entry_hooks(const EntryHooks &h) { entry_hooks = h; }

std::string entry_blocked()
{
    if (!live)
        return "the save splice is not armed";
    if constexpr (layout::SAVE_SPLICE) {
        auto bag_id = reinterpret_cast<Id_t>(
            method(reinterpret_cast<void *>(b.bag_type), offsetof(g::NePropertyTypeVtbl, id)));
        uint32_t id = bag_id ? uint32_t(bag_id(reinterpret_cast<void *>(b.bag_type))) : 0;
        if (id != ENTRY_BAG_TYPE) {
            char why[96];
            snprintf(why, sizeof why, "the game's bag type id is %08x, not 'pbag' (%08x)", id, ENTRY_BAG_TYPE);
            return why;
        }
        return {};
    } else {
        return "this core lacks the property layout";
    }
}

}
// ---- end W4 ----

}
