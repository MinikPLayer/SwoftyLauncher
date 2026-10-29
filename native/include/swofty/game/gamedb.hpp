#pragma once
#include <swofty/game/base.hpp>

namespace swofty::game {

struct GdbDatabase {
    uint8_t _pad_0x0[0xf0];
    uint8_t loaded; // L2 experimental
};
static_assert(offsetof(GdbDatabase, loaded) == 0xf0);

struct GdbStringRecord {
    uint8_t _pad_0x0[0x10];
    uint64_t enc_size; // L2 experimental
    uint8_t *enc; // L2 experimental
};
static_assert(offsetof(GdbStringRecord, enc_size) == 0x10);
static_assert(offsetof(GdbStringRecord, enc) == 0x18);

struct GdbValueMap {
    uint8_t _pad_0x0[0x28];
    uint64_t num_elements; // L2 experimental
    uint64_t mask; // L2 experimental
    uint64_t max_num_elements_allowed; // L2 experimental
    uint8_t _pad_0x40[0x8];
};
static_assert(offsetof(GdbValueMap, num_elements) == 0x28);
static_assert(offsetof(GdbValueMap, mask) == 0x30);
static_assert(offsetof(GdbValueMap, max_num_elements_allowed) == 0x38);
static_assert(sizeof(GdbValueMap) == 0x48);

using Gdb_HashMap_Rehash_t = void(GdbValueMap *map, uint64_t num_buckets);
inline constexpr fn<"Gdb_HashMap_Rehash", Gdb_HashMap_Rehash_t> Gdb_HashMap_Rehash{}; // L2 experimental
using Gdb_Load_GameDatabase_t = GdbDatabase *(GdbDatabase *db, void *cfg);
inline constexpr fn<"Gdb_Load_GameDatabase", Gdb_Load_GameDatabase_t> Gdb_Load_GameDatabase{}; // L2 experimental
using Gdb_Lookup_Key_t = void **(const char *path);
inline constexpr fn<"Gdb_Lookup_Key", Gdb_Lookup_Key_t> Gdb_Lookup_Key{}; // L2 experimental
inline constexpr data<"g_Gdb_Database", GdbDatabase *> g_Gdb_Database{}; // L2 experimental
inline constexpr data<"g_Gdb_RecordAllocator", void *> g_Gdb_RecordAllocator{}; // L2 experimental
inline constexpr data<"k_Gdb_StringXorKey", uint8_t[4]> k_Gdb_StringXorKey{}; // L2 experimental
inline constexpr site<"site_Gdb_DeptCountStore"> site_Gdb_DeptCountStore{}; // L2 experimental
inline constexpr call<"site_Gdb_InsertGrowCall"> site_Gdb_InsertGrowCall{}; // L2 experimental
inline constexpr site<"site_Gdb_ReserveTarget"> site_Gdb_ReserveTarget{}; // L2 experimental

}
