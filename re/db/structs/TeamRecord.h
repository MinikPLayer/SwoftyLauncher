// struct: TeamRecord
// subsystem: teams
// size: 0x768  L2 decomp:re/decomp/140cdbd40.c;xref:0x1424697ec (Team_Insert_RecordByStatId allocates 0x768 per team, copies the u32 key then the body from record+8)
// notes: one loaded entrants.teams row. Sess_Load_Teams (0x142469440) builds it on the stack at rsp+0xfe0 (the fields
// below are written at rsp+0x468+off and copied up by 0xb78) and Team_Insert_RecordByStatId stores a heap copy in the
// entrants system's team slots: system+0x2c0+set*0x10 = {allocator, slots*}, slots = {allocator, begin, end, cap} of
// 0x10-byte {allocator, TeamRecord*} entries indexed by stat id (set 0 = the database teams; Ent_EntrantsSystem_AddTeam
// adds to set 1 at system+0x2d8; RemoveTeam refuses set 0, "preloaded"). Every text field is a fixed string holder of
// 0x98 bytes {vtable &UNK_145613670 +0, capacity 0x80 +8, char *ptr +0x10, char buf[0x80] +0x18}; the rows below
// name the ptr of each holder (holder = row offset - 0x10). Colours are the database's signed ARGB int32 read without a
// null check (a missing colour key crashes the loader). Layout write-up: kb:subsystems/teams.md#team-record.
struct TeamRecord {
    /* 0x000 */ uint32_t key;           // L2 decomp:re/decomp/142469440.c;decomp:re/decomp/140cdbd40.c;xref:0x14246c499,0x140cdbd53 stat_id << 16 (low byte = set, 0 here); Team_Insert_RecordByStatId indexes its slots by the high word
    /* 0x018 */ char *label;            // L2 decomp:re/decomp/142469440.c;xref:0x142469c56,0x1416a3ade,0x141879897 the row's _metadata_ label (Gdb_Get_RowName); the logo/badge key format sites read this pointer through the stat-id slots (mov rdi,[slots+id*0x10+8]; mov r8,[rdi+0x18] at 0x1416a3ad9/0x1416a3ade and 0x141879892/0x141879897)
    /* 0x0b0 */ char *display_name;     // L2 decomp:re/decomp/142469440.c;xref:0x142469ed6 display_name (lang key, default "" when absent)
    /* 0x148 */ char *display_name_code; // L2 decomp:re/decomp/142469440.c;xref:0x14246a146 display_name_code (3 letters)
    /* 0x1e0 */ char *display_name_short; // L2 decomp:re/decomp/142469440.c;xref:0x14246a3b6 display_name_short
    /* 0x278 */ char *display_name_super_short; // L2 decomp:re/decomp/142469440.c;xref:0x14246a626 display_name_super_short
    /* 0x310 */ char *livery_name;      // L2 decomp:re/decomp/142469440.c;xref:0x14246ab06 livery_name
    /* 0x3a8 */ char *team_base;        // L2 decomp:re/decomp/142469440.c;xref:0x14246ad76 team_base (lang key)
    /* 0x440 */ char *paddock_marker;   // L2 decomp:re/decomp/142469440.c;xref:0x14246c463 optional key <row path>.paddockMarker read by Gdb_Lookup_Key; no stock row has it, so ""
    /* 0x4d8 */ char *timing_tower_texture_path; // L2 decomp:re/decomp/142469440.c;xref:0x14246a896 timing_tower_texture_path
    /* 0x560 */ uint8_t animation_set;  // L2 decomp:re/decomp/142469440.c;xref:0x1424699a4,0x14246bf5c stat_id of the animation_set label's row in entrants.animation.driver_animation_sets (0xff when empty)
    /* 0x561 */ uint8_t audio_id;       // L2 decomp:re/decomp/142469440.c;xref:0x14246ada9 audio_id (0 when absent)
    /* 0x564 */ int32_t primary_colour; // L2 decomp:re/decomp/142469440.c;xref:0x14246adcc signed ARGB, no null check
    /* 0x568 */ int32_t secondary_colour; // L2 decomp:re/decomp/142469440.c;xref:0x14246ae0a signed ARGB, no null check
    /* 0x56c */ int32_t tertiary_colour; // L2 decomp:re/decomp/142469440.c;xref:0x14246ae48 signed ARGB, no null check
    /* 0x570 */ int32_t team_wear_primary_colour; // L2 decomp:re/decomp/142469440.c;xref:0x14246adeb signed ARGB, no null check
    /* 0x574 */ int32_t team_wear_secondary_colour; // L2 decomp:re/decomp/142469440.c;xref:0x14246ae29 signed ARGB, no null check
    /* 0x578 */ int32_t team_wear_tertiary_colour; // L2 decomp:re/decomp/142469440.c;xref:0x14246ae67 signed ARGB, no null check
    /* 0x57c */ int32_t ui_team_colour; // L2 decomp:re/decomp/142469440.c;xref:0x14246ae86 signed ARGB, no null check
    /* 0x580 */ uint8_t nationality;    // L2 decomp:re/decomp/142469440.c;xref:0x14246b0da stat_id of the nationality label's row in entrants.nationality (0 when unresolved)
    /* 0x581 */ uint8_t team_boss;      // L2 decomp:re/decomp/142469440.c;xref:0x14246b44f stat_id of the team_boss label's row in entrants.characters (0xff when empty)
    /* 0x582 */ uint16_t expression_label; // L2 decomp:re/decomp/142469440.c;xref:0x14246b29a stat_id of the expression_label label's row in entrants.expression_label (0 when empty)
    /* 0x584 */ int32_t prestige;       // L2 decomp:re/decomp/142469440.c;xref:0x14246b489 prestige; stored with +0x588 only when >= 0 or absent (absent = 0)
    /* 0x588 */ bool has_prestige;      // L2 decomp:re/decomp/142469440.c;xref:0x14246b489 optional flag of prestige
    /* 0x58c */ bool show_team_badge;   // L2 decomp:re/decomp/142469440.c;xref:0x14246aeaf show_team_badge
    /* 0x58d */ bool show_custom_badge_in_ui; // L2 decomp:re/decomp/142469440.c;xref:0x14246aee3 show_custom_badge_in_UI
    /* 0x5a0 */ char *story_hashtag;    // L1 decomp:re/decomp/142469440.c;xref:0x14246b825 story_info_N.hashtag (holder +0x590; written in the story_info walk)
    /* 0x638 */ char *story_description; // L1 decomp:re/decomp/142469440.c;xref:0x14246b825 story_info_N.description (holder +0x628)
    /* 0x6c0 */ bool has_story;         // L2 decomp:re/decomp/142469440.c;xref:0x1424699c0,0x14246b825 set when the row has a story_info row
    /* 0x6d8 */ char *row_path;         // L2 decomp:re/decomp/142469440.c;xref:0x14246af05 "%s.%s" of the table path and the row name (entrants.teams.team_N), written by Ne_Format (0x1401009f0)
    /* 0x760 */ bool team_colour_valid; // L2 decomp:re/decomp/142469440.c;xref:0x1424699ec,0x14246af2e,0x1424697e2 team_colour_valid (default 1 before the read)
};
