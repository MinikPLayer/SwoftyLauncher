#pragma once
#include <swofty/game/base.hpp>

namespace swofty::game {

struct TeamRecord {
    uint8_t _pad_0x0[0xb0];
    char *display_name; // L2 experimental
    uint8_t _pad_0xb8[0x90];
    char *display_name_code; // L2 experimental
    uint8_t _pad_0x150[0x90];
    char *display_name_short; // L2 experimental
    uint8_t _pad_0x1e8[0x90];
    char *display_name_super_short; // L2 experimental
    uint8_t _pad_0x280[0x90];
    char *livery_name; // L2 experimental
    uint8_t _pad_0x318[0x90];
    char *team_base; // L2 experimental
    uint8_t _pad_0x3b0[0x128];
    char *timing_tower_texture_path; // L2 experimental
    uint8_t _pad_0x4e0[0x84];
    int32_t primary_colour; // L2 experimental
    int32_t secondary_colour; // L2 experimental
    int32_t tertiary_colour; // L2 experimental
    int32_t team_wear_primary_colour; // L2 experimental
    int32_t team_wear_secondary_colour; // L2 experimental
    int32_t team_wear_tertiary_colour; // L2 experimental
    int32_t ui_team_colour; // L2 experimental
    uint8_t _pad_0x580[0x1e8];
};
static_assert(offsetof(TeamRecord, display_name) == 0xb0);
static_assert(offsetof(TeamRecord, display_name_code) == 0x148);
static_assert(offsetof(TeamRecord, display_name_short) == 0x1e0);
static_assert(offsetof(TeamRecord, display_name_super_short) == 0x278);
static_assert(offsetof(TeamRecord, livery_name) == 0x310);
static_assert(offsetof(TeamRecord, team_base) == 0x3a8);
static_assert(offsetof(TeamRecord, timing_tower_texture_path) == 0x4d8);
static_assert(offsetof(TeamRecord, primary_colour) == 0x564);
static_assert(offsetof(TeamRecord, secondary_colour) == 0x568);
static_assert(offsetof(TeamRecord, tertiary_colour) == 0x56c);
static_assert(offsetof(TeamRecord, team_wear_primary_colour) == 0x570);
static_assert(offsetof(TeamRecord, team_wear_secondary_colour) == 0x574);
static_assert(offsetof(TeamRecord, team_wear_tertiary_colour) == 0x578);
static_assert(offsetof(TeamRecord, ui_team_colour) == 0x57c);
static_assert(sizeof(TeamRecord) == 0x768);

inline constexpr call<"site_Team_LoadInsertCall"> site_Team_LoadInsertCall{}; // L2 experimental
inline constexpr site<"site_Team_LoadInserted"> site_Team_LoadInserted{}; // L2 experimental
inline constexpr site<"site_Team_SessionCarBadgeLabel"> site_Team_SessionCarBadgeLabel{}; // L2 experimental
inline constexpr site<"site_Team_TimingPillBadgeLabel"> site_Team_TimingPillBadgeLabel{}; // L2 experimental

}
