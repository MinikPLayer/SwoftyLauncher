#pragma once
#include <swofty/game/base.hpp>

namespace swofty::game {

struct NeAllocator {
    NeAllocatorVtbl *vtable; // L2 experimental
};
static_assert(offsetof(NeAllocator, vtable) == 0x0);

struct NeAllocatorVtbl {
    uint8_t _pad_0x0[0x18];
    void *alloc; // L2 experimental
    uint8_t _pad_0x20[0x18];
    void *free; // L2 experimental
};
static_assert(offsetof(NeAllocatorVtbl, alloc) == 0x18);
static_assert(offsetof(NeAllocatorVtbl, free) == 0x38);

struct NeBagCodec {
    void **vtable; // L3 stable
    void *stream; // L3 stable
};
static_assert(offsetof(NeBagCodec, vtable) == 0x0);
static_assert(offsetof(NeBagCodec, stream) == 0x8);

struct NeCountingStream {
    void **vtable; // L3 stable
    uint8_t _pad_0x8[0x20];
    uint64_t count; // L3 stable
};
static_assert(offsetof(NeCountingStream, vtable) == 0x0);
static_assert(offsetof(NeCountingStream, count) == 0x28);

struct NeLogGate {
    uint32_t channel_hashes[373]; // L2 experimental
    uint8_t enabled; // L2 experimental
};
static_assert(offsetof(NeLogGate, channel_hashes) == 0x0);
static_assert(offsetof(NeLogGate, enabled) == 0x5d4);

struct NeMemWriteStream {
    void **vtable; // L3 stable
    uint8_t *data; // L3 stable
    uint64_t capacity; // L3 stable
    uint64_t position; // L3 stable
    uint64_t size; // L2 experimental
};
static_assert(offsetof(NeMemWriteStream, vtable) == 0x0);
static_assert(offsetof(NeMemWriteStream, data) == 0x8);
static_assert(offsetof(NeMemWriteStream, capacity) == 0x10);
static_assert(offsetof(NeMemWriteStream, position) == 0x18);
static_assert(offsetof(NeMemWriteStream, size) == 0x20);

struct NeMessageBus {
    uint8_t _pad_0x0[0x68];
    uint8_t active; // L2 experimental
};
static_assert(offsetof(NeMessageBus, active) == 0x68);

struct NeOptionalVec {
    NeAllocator *alloc; // L2 experimental
    void *begin; // L2 experimental
    void *end; // L2 experimental
    uint64_t count; // L2 experimental
    uint8_t engaged; // L2 experimental
    uint8_t _pad_0x21[0x7];
};
static_assert(offsetof(NeOptionalVec, alloc) == 0x0);
static_assert(offsetof(NeOptionalVec, begin) == 0x8);
static_assert(offsetof(NeOptionalVec, end) == 0x10);
static_assert(offsetof(NeOptionalVec, count) == 0x18);
static_assert(offsetof(NeOptionalVec, engaged) == 0x20);
static_assert(sizeof(NeOptionalVec) == 0x28);

struct NePropertyTypeVtbl {
    uint8_t _pad_0x0[0x10];
    void *id; // L3 stable
    uint8_t _pad_0x18[0x8];
    void *key_offset; // L3 stable
};
static_assert(offsetof(NePropertyTypeVtbl, id) == 0x10);
static_assert(offsetof(NePropertyTypeVtbl, key_offset) == 0x20);

struct NePropertyVtbl {
    uint8_t _pad_0x0[0x8];
    void *type; // L3 stable
    uint8_t _pad_0x10[0x28];
    void *value; // L3 stable
};
static_assert(offsetof(NePropertyVtbl, type) == 0x8);
static_assert(offsetof(NePropertyVtbl, value) == 0x38);

struct NeSharedCtrl {
    void **vtable; // L2 experimental
    int32_t strong; // L2 experimental
    int32_t weak; // L2 experimental
};
static_assert(offsetof(NeSharedCtrl, vtable) == 0x0);
static_assert(offsetof(NeSharedCtrl, strong) == 0x8);
static_assert(offsetof(NeSharedCtrl, weak) == 0xc);

struct NeStreamVtbl {
    uint8_t _pad_0x0[0x48];
    void *write; // L3 stable
    void *write_length; // L3 stable
};
static_assert(offsetof(NeStreamVtbl, write) == 0x48);
static_assert(offsetof(NeStreamVtbl, write_length) == 0x50);

struct SaveDataContainer {
    void **vtable; // L3 stable
    uint8_t _pad_0x8[0x78];
    const char *name; // L3 stable
    uint8_t _pad_0x88[0x78];
    uint8_t retain_unknown; // L2 experimental
    uint8_t _pad_0x101[0x7];
    uint64_t retain_unknown_key; // L2 experimental
    uint8_t _pad_0x110[0xb8];
    uint64_t retained_count; // L2 experimental
};
static_assert(offsetof(SaveDataContainer, vtable) == 0x0);
static_assert(offsetof(SaveDataContainer, name) == 0x80);
static_assert(offsetof(SaveDataContainer, retain_unknown) == 0x100);
static_assert(offsetof(SaveDataContainer, retain_unknown_key) == 0x108);
static_assert(offsetof(SaveDataContainer, retained_count) == 0x1c8);

using Ne_Compare_StringNoCase_t = int32_t(const char *a, const char *b);
inline constexpr fn<"Ne_Compare_StringNoCase", Ne_Compare_StringNoCase_t> Ne_Compare_StringNoCase{}; // L2 experimental, _stricmp
using Ne_Detach_ChildNode_t = int(void *parent, void *child);
inline constexpr fn<"Ne_Detach_ChildNode", Ne_Detach_ChildNode_t> Ne_Detach_ChildNode{}; // L2 experimental
using Ne_Entity_FindComponent_t = void *(void *entity, const void *type);
inline constexpr fn<"Ne_Entity_FindComponent", Ne_Entity_FindComponent_t> Ne_Entity_FindComponent{}; // L2 experimental
using Ne_Format_String_t = int64_t(NeStr **out, const char *fmt, ...);
inline constexpr fn<"Ne_Format_String", Ne_Format_String_t> Ne_Format_String{}; // L2 experimental
using Ne_Get_SettingBool_t = uint64_t(void *node, const char *name, uint8_t *out, uint8_t def);
inline constexpr fn<"Ne_Get_SettingBool", Ne_Get_SettingBool_t> Ne_Get_SettingBool{}; // L2 experimental
using Ne_Get_SettingU32_t = uint64_t(void *node, const char *name, uint32_t *out, uint32_t def);
inline constexpr fn<"Ne_Get_SettingU32", Ne_Get_SettingU32_t> Ne_Get_SettingU32{}; // L2 experimental
using Ne_Log_Thunk_t = void(const char *channel, const char *fmt, ...);
inline constexpr fn<"Ne_Log_Thunk", Ne_Log_Thunk_t> Ne_Log_Thunk{}; // L2 experimental
using Ne_Make_StrFromCStr_t = uint32_t(NeStr **out, const char *s, uint32_t flags, uintptr_t len);
inline constexpr fn<"Ne_Make_StrFromCStr", Ne_Make_StrFromCStr_t> Ne_Make_StrFromCStr{}; // L3 stable
using Ne_Parse_Long_t = int32_t(const char *text, char **end, int32_t base);
inline constexpr fn<"Ne_Parse_Long", Ne_Parse_Long_t> Ne_Parse_Long{}; // L2 experimental, strtol
using Ne_Play_FullscreenVideo_t = void(void *feature_system, NeStr **path, const void *on_end, const void *frame_callbacks, int32_t layer);
inline constexpr fn<"Ne_Play_FullscreenVideo", Ne_Play_FullscreenVideo_t> Ne_Play_FullscreenVideo{}; // L2 experimental, neon::NeUIGenericFeatureSystem::playFullscreenVideo
using Ne_Post_BusMessage_t = uint64_t(void *bus, void *msg);
inline constexpr fn<"Ne_Post_BusMessage", Ne_Post_BusMessage_t> Ne_Post_BusMessage{}; // L2 experimental
using Ne_StringTable_Lookup_t = const char *(void *table, uint64_t hash);
inline constexpr fn<"Ne_StringTable_Lookup", Ne_StringTable_Lookup_t> Ne_StringTable_Lookup{}; // L3 stable
inline constexpr data<"g_Ne_DefaultAllocator", void *> g_Ne_DefaultAllocator{}; // L2 experimental
inline constexpr data<"g_Ne_LogGate", NeLogGate> g_Ne_LogGate{}; // L2 experimental
inline constexpr data<"g_Ne_StringTable", void *> g_Ne_StringTable{}; // L3 stable
inline constexpr vtable<"k_Ne_BagCodecVtbl", 4> k_Ne_BagCodecVtbl{}; // L3 stable
inline constexpr vtable<"k_Ne_CountingStreamVtbl", 24> k_Ne_CountingStreamVtbl{}; // L3 stable
inline constexpr vtable<"k_Ne_MemWriteStreamVtbl", 33> k_Ne_MemWriteStreamVtbl{}; // L3 stable
inline constexpr data<"k_Ne_PropertyType_Bag", void *> k_Ne_PropertyType_Bag{}; // L3 stable
inline constexpr data<"k_Ne_TypeChainEnd", void *> k_Ne_TypeChainEnd{}; // L2 experimental
inline constexpr data<"k_Ne_TypeRoot", void *> k_Ne_TypeRoot{}; // L2 experimental, NoOtiInfo.
inline constexpr site<"site_Ne_Format_String_Entry"> site_Ne_Format_String_Entry{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_CrowdInShadows"> site_Ne_HwBool_CrowdInShadows{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_GodRaysPost"> site_Ne_HwBool_GodRaysPost{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_GrassFlattening"> site_Ne_HwBool_GrassFlattening{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_GroundCoverEnabled"> site_Ne_HwBool_GroundCoverEnabled{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_GroundCoverMeshes"> site_Ne_HwBool_GroundCoverMeshes{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_LensStreak"> site_Ne_HwBool_LensStreak{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_MotionBlurV2"> site_Ne_HwBool_MotionBlurV2{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_NightShadowCharCast"> site_Ne_HwBool_NightShadowCharCast{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_RainBeads"> site_Ne_HwBool_RainBeads{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_RainHigh"> site_Ne_HwBool_RainHigh{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_RainSheets"> site_Ne_HwBool_RainSheets{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_RainSplashes"> site_Ne_HwBool_RainSplashes{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_SkyShadowLowLod"> site_Ne_HwBool_SkyShadowLowLod{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_SkyShadowMap32Bits"> site_Ne_HwBool_SkyShadowMap32Bits{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_TrackHeatHaze"> site_Ne_HwBool_TrackHeatHaze{}; // L2 experimental
inline constexpr call<"site_Ne_HwBool_VolumetricFog"> site_Ne_HwBool_VolumetricFog{}; // L2 experimental
inline constexpr call<"site_Ne_HwConfigParseCall"> site_Ne_HwConfigParseCall{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_CrowdBillboardDistance"> site_Ne_HwInt_CrowdBillboardDistance{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_NightShadowMapSize"> site_Ne_HwInt_NightShadowMapSize{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_NightShadowSourceCount"> site_Ne_HwInt_NightShadowSourceCount{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_ShadowSampling"> site_Ne_HwInt_ShadowSampling{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_SkyShadowCascadeCount"> site_Ne_HwInt_SkyShadowCascadeCount{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_SkyShadowMapSize"> site_Ne_HwInt_SkyShadowMapSize{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_SpotShadowMapSize"> site_Ne_HwInt_SpotShadowMapSize{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_SpotShadowSampling"> site_Ne_HwInt_SpotShadowSampling{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_TexStreamSizeInMiB"> site_Ne_HwInt_TexStreamSizeInMiB{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_VolFogMapHeight"> site_Ne_HwInt_VolFogMapHeight{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_VolFogMapWidth"> site_Ne_HwInt_VolFogMapWidth{}; // L2 experimental
inline constexpr call<"site_Ne_HwInt_WorldShadowMapSize"> site_Ne_HwInt_WorldShadowMapSize{}; // L2 experimental
inline constexpr call<"site_Ne_HwXmlParseCall"> site_Ne_HwXmlParseCall{}; // L2 experimental
inline constexpr call<"site_Ne_HwXmlWriteCall"> site_Ne_HwXmlWriteCall{}; // L2 experimental
inline constexpr site<"site_Ne_ReplayAfterRecord"> site_Ne_ReplayAfterRecord{}; // L2 experimental

}
