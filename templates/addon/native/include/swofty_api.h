/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWOFTY_API_VERSION 8

enum {
    SWOFTY_MIXIN_WRAP = 1,
    SWOFTY_MIXIN_SITE = 2,
    SWOFTY_MIXIN_VTABLE = 3,
    SWOFTY_MIXIN_REDIRECT = 4,
    SWOFTY_MIXIN_PATCH = 5,
    SWOFTY_MIXIN_ON_LOAD = 6,
};
#define SWOFTY_MIXIN_OVERWRITE 0x100u
#define SWOFTY_MIXIN_KIND(k) ((k) & 0xffu)
#define SWOFTY_MIXIN_ARG(k) ((k) >> 16)
#define SWOFTY_MIXIN_WITH_ARG(k, arg) ((uint32_t)(k) | (uint32_t)(arg) << 16)

typedef struct SwoftyMixin {
    const char *target;
    uint32_t kind;
    int32_t priority;
    void *handler;
    void **orig;
} SwoftyMixin;

typedef union SwoftyXmm {
    uint8_t u8[16];
    uint32_t u32[4];
    uint64_t u64[2];
    float f32[4];
    double f64[2];
} SwoftyXmm;

typedef struct SwoftyContext {
    SwoftyXmm xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15;
    uintptr_t rflags, r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rdx, rcx, rbx, rax, rbp, rsp, trampoline_rsp, rip;
} SwoftyContext;

enum {
    SWOFTY_SETTING_TOGGLE = 1,
    SWOFTY_SETTING_SLIDER = 2,
    SWOFTY_SETTING_CHOICE = 3,
};

typedef struct SwoftySettingValue {
    uint32_t type;
    int32_t toggle;
    double slider;
    const char *choice;
} SwoftySettingValue;

typedef void (*SwoftySettingFn)(const char *id, const SwoftySettingValue *value, void *user);

enum {
    SWOFTY_ASSET_OK = 0,
    SWOFTY_ASSET_LATE = 1,
    SWOFTY_ASSET_UNAVAILABLE = 2,
    SWOFTY_ASSET_INVALID = 3,
};

typedef struct SwoftyFragment {
    const char *file;
    const char *fourcc;
    uint32_t flags;
} SwoftyFragment;

typedef struct SwoftyAssets {
    int (*available)(const char *feature);
    int (*register_group)(const char *group, const char *const *erps, size_t erp_count, const char *const *deps,
                          size_t dep_count, const char *templ);
    int (*put_resource)(const char *uri, const SwoftyFragment *fragments, size_t fragment_count, const char *const *erps,
                        size_t erp_count);
    int (*db_set)(const char *key, const char *type, const char *value);
    int (*db_set_raw)(uint64_t hash, uint8_t tag, const void *value, size_t size);
    int (*lang_set)(const char *language, const char *key, const char *text);
    int (*lang_table)(const char *language, const char *file);
    int (*overlay_file)(const char *game_path, const char *file);
} SwoftyAssets;

/* The standard-library tables below use the SWOFTY_ASSET_* result codes: ok, late,
   unavailable (older core, or the feature is off), invalid (bad argument). Each table's
   available() returns non-zero only when its feature is on and the core implements it. */

#define SWOFTY_EVENT_SESSION_START "session.start"
#define SWOFTY_EVENT_SESSION_END "session.end"
#define SWOFTY_EVENT_RACE_START "race.start"
#define SWOFTY_EVENT_LAP "lap"
#define SWOFTY_EVENT_FLAG "flag"
#define SWOFTY_EVENT_STATE "state"
#define SWOFTY_EVENT_SCREEN "screen"
#define SWOFTY_EVENT_PAUSE "pause"
#define SWOFTY_EVENT_RESUME "resume"
#define SWOFTY_EVENT_RESULTS "results"
#define SWOFTY_EVENT_FLOW_STATE "flow.state"
#define SWOFTY_EVENT_CAREER_LOADED "career.loaded"
#define SWOFTY_EVENT_CAREER_UNLOADED "career.unloaded"
#define SWOFTY_EVENT_CAREER_HUB "career.hub"
#define SWOFTY_EVENT_CAREER_STARTING "career.starting"

typedef struct SwoftyEvent {
    const char *name;
    uint64_t frame;
    double time;
    int64_t a, b;
    const void *data;
} SwoftyEvent;

typedef void (*SwoftyEventFn)(const SwoftyEvent *ev, void *user);
typedef void (*SwoftyFrameFn)(double dt, void *user);

typedef struct SwoftyEvents {
    int (*available)(void);
    int (*subscribe)(const char *event, SwoftyEventFn fn, void *user, uint32_t *token);
    int (*on_frame)(SwoftyFrameFn fn, void *user, uint32_t *token);
    int (*unsubscribe)(uint32_t token);
} SwoftyEvents;

typedef struct SwoftyPaint SwoftyPaint;
typedef void (*SwoftyPaintFn)(SwoftyPaint *paint, void *user);

/* Overlay v2: the SwoftyOverlay entries after image() exist when the running swofty_core.dll exports
   SwoftyCoreOverlayVersion and it returns 2 or more (swofty/overlay.hpp checks); the v3 entries after
   ui_scale() exist when it returns 3 or more. A text style and an input snapshot are read and written
   up to their size field, so later fields can be added. */
#define SWOFTY_OVERLAY_VERSION 3

enum {
    SWOFTY_TEXT_LEFT = 0x00,
    SWOFTY_TEXT_CENTER = 0x01,
    SWOFTY_TEXT_RIGHT = 0x02,
    SWOFTY_TEXT_TOP = 0x00,
    SWOFTY_TEXT_MIDDLE = 0x10,
    SWOFTY_TEXT_BASELINE = 0x20,
    SWOFTY_TEXT_BOTTOM = 0x30,
};

enum {
    SWOFTY_GRADIENT_VERTICAL = 0,
    SWOFTY_GRADIENT_HORIZONTAL = 1,
};

enum {
    SWOFTY_POLYLINE_CLOSED = 1,
};

enum {
    SWOFTY_MOUSE_LEFT = 1,
    SWOFTY_MOUSE_RIGHT = 2,
    SWOFTY_MOUSE_MIDDLE = 4,
};

/* Gamepad buttons in SwoftyInput.pad_buttons / pad_pressed: the XInput XINPUT_GAMEPAD_* bits. */
enum {
    SWOFTY_PAD_UP = 0x0001,
    SWOFTY_PAD_DOWN = 0x0002,
    SWOFTY_PAD_LEFT = 0x0004,
    SWOFTY_PAD_RIGHT = 0x0008,
    SWOFTY_PAD_START = 0x0010,
    SWOFTY_PAD_BACK = 0x0020,
    SWOFTY_PAD_LTHUMB = 0x0040,
    SWOFTY_PAD_RTHUMB = 0x0080,
    SWOFTY_PAD_LB = 0x0100,
    SWOFTY_PAD_RB = 0x0200,
    SWOFTY_PAD_A = 0x1000,
    SWOFTY_PAD_B = 0x2000,
    SWOFTY_PAD_X = 0x4000,
    SWOFTY_PAD_Y = 0x8000,
};

typedef struct SwoftyInput {
    uint32_t size;
    uint32_t focused;
    float x, y;
    float wheel;
    uint32_t buttons;
    uint32_t pressed;
    uint32_t released;
    uint32_t char_count;
    uint32_t chars[16];
    uint8_t keys_down[32];
    uint8_t keys_pressed[32];
    uint8_t keys_typed[32];
    uint32_t pad_connected;
    uint32_t pad_buttons;
    uint32_t pad_pressed;
    float pad_lx, pad_ly, pad_rx, pad_ry;
    float pad_lt, pad_rt;
} SwoftyInput;

typedef struct SwoftyTextStyle {
    uint32_t size;
    uint32_t align;
    float px;
    uint32_t rgba;
    float tracking;
    float line_height;
    uint32_t outline_rgba;
    float outline_px;
    uint32_t shadow_rgba;
    float shadow_dx, shadow_dy, shadow_blur;
    float max_width;
    uint32_t font;
} SwoftyTextStyle;

typedef struct SwoftyOverlay {
    int (*available)(void);
    int (*on_paint)(SwoftyPaintFn fn, void *user, uint32_t *token);
    int (*remove)(uint32_t token);
    void (*size)(const SwoftyPaint *paint, float *width, float *height);
    void (*rect)(SwoftyPaint *paint, float x, float y, float w, float h, uint32_t rgba);
    void (*rect_outline)(SwoftyPaint *paint, float x, float y, float w, float h, float thickness, uint32_t rgba);
    void (*line)(SwoftyPaint *paint, float x0, float y0, float x1, float y1, float thickness, uint32_t rgba);
    void (*text)(SwoftyPaint *paint, float x, float y, float size, uint32_t rgba, const char *utf8);
    void (*measure)(const SwoftyPaint *paint, float size, const char *utf8, float *width, float *height);
    int (*load_image)(const char *id, const char *file);
    int (*image)(SwoftyPaint *paint, const char *id, float x, float y, float w, float h, uint32_t tint);
    /* v2 */
    void (*rect_rounded)(SwoftyPaint *paint, float x, float y, float w, float h, float radius, uint32_t rgba);
    void (*rect_rounded_outline)(SwoftyPaint *paint, float x, float y, float w, float h, float radius, float thickness,
                                 uint32_t rgba);
    void (*rect_gradient)(SwoftyPaint *paint, float x, float y, float w, float h, float radius, uint32_t from,
                          uint32_t to, uint32_t flags);
    void (*circle)(SwoftyPaint *paint, float cx, float cy, float radius, uint32_t rgba);
    void (*circle_outline)(SwoftyPaint *paint, float cx, float cy, float radius, float thickness, uint32_t rgba);
    void (*arc)(SwoftyPaint *paint, float cx, float cy, float radius, float from, float to, float thickness,
                uint32_t rgba);
    void (*polyline)(SwoftyPaint *paint, const float *xy, uint32_t count, float thickness, uint32_t rgba,
                     uint32_t flags);
    void (*polygon)(SwoftyPaint *paint, const float *xy, uint32_t count, uint32_t rgba);
    void (*text_styled)(SwoftyPaint *paint, float x, float y, const SwoftyTextStyle *style, const char *utf8);
    void (*measure_styled)(const SwoftyPaint *paint, const SwoftyTextStyle *style, const char *utf8, float *width,
                           float *height);
    int (*image_uv)(SwoftyPaint *paint, const char *id, float x, float y, float w, float h, float u0, float v0,
                    float u1, float v1, uint32_t tint);
    int (*image_nine)(SwoftyPaint *paint, const char *id, float x, float y, float w, float h, float left, float top,
                      float right, float bottom, float scale, uint32_t tint);
    int (*image_size)(const char *id, float *width, float *height);
    void (*push_clip)(SwoftyPaint *paint, float x, float y, float w, float h);
    void (*pop_clip)(SwoftyPaint *paint);
    void (*push_transform)(SwoftyPaint *paint, const float *m);
    void (*pop_transform)(SwoftyPaint *paint);
    void (*push_opacity)(SwoftyPaint *paint, float opacity);
    void (*pop_opacity)(SwoftyPaint *paint);
    void (*safe_area)(const SwoftyPaint *paint, float *x, float *y, float *w, float *h);
    float (*ui_scale)(const SwoftyPaint *paint);
    /* v3 */
    int (*input)(const SwoftyPaint *paint, SwoftyInput *out);
    int (*focus)(int on);
} SwoftyOverlay;

#define SWOFTY_TELEMETRY_VERSION 1

typedef struct SwoftyCarState {
    uint32_t version;
    uint32_t flags;
    float speed;
    float rpm;
    float max_rpm;
    int32_t gear;
    float throttle, brake, clutch, steer;
    float tyre_temp[4];
    float tyre_wear[4];
    float brake_temp[4];
    float fuel;
    float fuel_laps;
    int32_t position;
    uint32_t lap;
    uint32_t sector;
    float last_lap_time, current_lap_time, best_lap_time;
    float sector_time[3];
    float lap_distance;
    float world_pos[3];
    float world_vel[3];
    float reserved[8];
} SwoftyCarState;

typedef struct SwoftySessionState {
    uint32_t version;
    uint32_t session_type;
    uint32_t track;
    uint32_t flag;
    uint32_t weather;
    uint32_t total_laps;
    uint32_t participant_count;
    uint32_t player_index;
    float session_time;
    float session_time_left;
    float track_temp, air_temp;
    float reserved[8];
} SwoftySessionState;

typedef struct SwoftyParticipant {
    uint32_t version;
    uint32_t flags;
    char name[48];
    uint32_t team;
    int32_t position;
    uint32_t lap;
    float lap_distance;
    float best_lap_time;
    float reserved[4];
} SwoftyParticipant;

typedef struct SwoftyTelemetryFeed {
    uint32_t version;
    uint32_t size;
    uint64_t seq;
    uint64_t frame;
    SwoftySessionState session;
    SwoftyCarState cars[24];
    SwoftyParticipant participants[24];
} SwoftyTelemetryFeed;

typedef struct SwoftyTelemetry {
    int (*available)(void);
    int (*session)(SwoftySessionState *out);
    int (*car)(uint32_t index, SwoftyCarState *out);
    int (*participant)(uint32_t index, SwoftyParticipant *out);
    int (*shared_memory)(const char **name);
} SwoftyTelemetry;

enum {
    SWOFTY_DB_INT = 0,
    SWOFTY_DB_FLOAT = 1,
    SWOFTY_DB_STRING = 2,
    SWOFTY_DB_ENUM = 3,
    SWOFTY_DB_ID = 4,
    SWOFTY_DB_BOOL = 5,
};

typedef struct SwoftyDbValue {
    uint8_t tag;
    int64_t i;
    double f;
    const char *s;
} SwoftyDbValue;

typedef struct SwoftyDbCursor {
    uint64_t table;
    uint64_t index;
    uint64_t count;
    const void *row;
} SwoftyDbCursor;

typedef struct SwoftyDb {
    int (*available)(void);
    int (*get)(const char *key, SwoftyDbValue *out);
    int (*get_hash)(uint64_t hash, SwoftyDbValue *out);
    int (*int_at)(const char *key, int64_t *out);
    int (*float_at)(const char *key, double *out);
    int (*string_at)(const char *key, const char **out);
    int (*bool_at)(const char *key, int32_t *out);
    int (*open)(const char *table, SwoftyDbCursor *cursor);
    int (*row)(const char *table, const char *label, SwoftyDbCursor *cursor);
    int (*step)(SwoftyDbCursor *cursor);
    int (*cell)(const SwoftyDbCursor *cursor, const char *field, SwoftyDbValue *out);
    const char *(*row_name)(const SwoftyDbCursor *cursor);
} SwoftyDb;

typedef struct SwoftyStorage {
    int (*available)(void);
    int (*read)(const char *path, void *buf, size_t cap, size_t *len);
    int (*write)(const char *path, const void *data, size_t len);
    int (*append)(const char *path, const void *data, size_t len);
    int (*remove)(const char *path);
    int (*size)(const char *path, uint64_t *out);
    int (*list)(const char *dir, char *buf, size_t cap, size_t *len);
} SwoftyStorage;

enum {
    SWOFTY_FLOW_NONE = 0,
    SWOFTY_FLOW_ENTERING = 1,
    SWOFTY_FLOW_UPDATING = 2,
    SWOFTY_FLOW_EXITING = 3,
};

enum {
    SWOFTY_CAREER_NONE = 0,
    SWOFTY_CAREER_MY_TEAM = 1,
    SWOFTY_CAREER_DRIVER = 2,
};

enum {
    SWOFTY_CAREER_COMPONENTS_UNSEEN = 0,
    SWOFTY_CAREER_COMPONENTS_LIVE = 1,
    SWOFTY_CAREER_COMPONENTS_FALLBACK = 2,
};

typedef struct SwoftyFlowState {
    uint32_t hash;
    uint32_t from;
    uint32_t to;
    uint32_t named;
    uint64_t transitions;
    char name[64];
} SwoftyFlowState;

typedef struct SwoftyCareerState {
    uint32_t loaded;
    uint32_t mode;
    uint32_t hub;
    uint32_t humans;
    uint32_t player_driver;
    uint32_t player_team;
    uint32_t has_player;
    uint32_t components;
    uint32_t component_mode;
    uint32_t team_hq_mode;
    uint32_t flow_career;
    uint32_t reserved;
    char key[32];
} SwoftyCareerState;

enum {
    SWOFTY_CAREER_START_PICK = 0,
    SWOFTY_CAREER_START_NEW = 1,
    SWOFTY_CAREER_START_CONTINUE = 2,
};

typedef struct SwoftyCareerStart {
    uint32_t mode;
    uint32_t kind;
    uint32_t blocked;
    uint32_t reserved;
    char route[64];
} SwoftyCareerStart;

typedef struct SwoftyCareer {
    int (*available)(void);
    int (*state)(SwoftyCareerState *out);
    int (*flow)(SwoftyFlowState *out);
    int (*flow_status)(uint32_t hash, uint32_t *status, uint64_t *age_ms);
    void *(*entity)(void);
    void *(*component)(const char *type);
    void *(*component_by_desc)(const void *desc);
    int (*post)(void *msg);
    int (*block)(uint32_t mode, const char *title, const char *body);
    int (*unblock)(uint32_t mode);
} SwoftyCareer;

typedef struct SwoftyPopup {
    int (*available)(void);
    int (*show)(const char *title, const char *body);
} SwoftyPopup;

/* Race HUD (swofty/hud.hpp), table version 1. Not part of SwoftyApi: the table comes from the core's
   SwoftyCoreHud export, so an addon built against this header also loads on an older core. Fields are only
   ever appended; version is the number of the last layout the core fills, so check it before calling a
   field added after version 1.
   Version 1: available; toast shows text (and an optional second line, detail) in the race HUD's central
   notification for about seconds, rounded to 2, 3, 4, 5, 6, 8, 10, 12, 15, 20 or 30, with icon NULL/"" for
   the default or a race_flags name (generic, diagonal, black, checkpoint, chequered, fast_lap,
   formation_lap, pp_icon, red, safety_car, virtual_safety_car, virtual_safety_car_green, flag_rival_won);
   set writes one HUD data leaf under ui_menu_data.hud_* or view_model_data.hud*: type SWOFTY_HUD_TEXT with
   a const char *, SWOFTY_HUD_BOOL or SWOFTY_HUD_INT with a const int32_t *. Strings are UTF-8, copied and
   capped at 200 bytes. */
#define SWOFTY_API_HUD 1
#define SWOFTY_HUD_VERSION 1
enum { SWOFTY_HUD_TEXT = 0, SWOFTY_HUD_BOOL = 1, SWOFTY_HUD_INT = 2 };
typedef struct SwoftyHud {
    uint32_t version;
    int (*available)(void);
    int (*toast)(const char *text, const char *detail, float seconds, const char *icon);
    int (*set)(const char *path, uint32_t type, const void *value);
} SwoftyHud;

/* Team HQ hub tabs (swofty/hub.hpp). Present on cores that export SwoftyCoreHub; the table is read through
   GetProcAddress, so an addon built against this header also loads on an older core and sees it absent.
   An addon's manifest `hub_tab` {name, title, options, data_group} makes the core add a Team HQ laptop tab
   for careers of data_group (after STANDINGS), whose page template is swofty_hub_<name> and whose data the
   core publishes at view_model_data.swofty_hub_<name> (title, count, o<k>.label/value/description).
   Version 1: available; hide_rnd(on) hides the R&D tab of Team HQ while on is set: Team HQ builds it
   hidden, and an open Team HQ hides or shows it on the next frame (cores before the live path leave it out
   from the next Team HQ entry instead); set_option(addon, option, value) picks the option state whose
   value is `value` (a choice value, "true"/"false", or a number) through the same command ADDON SETTINGS
   runs. */
#define SWOFTY_API_HUB 1
#define SWOFTY_HUB_VERSION 1
typedef struct SwoftyHub {
    uint32_t version;
    int (*available)(void);
    int (*hide_rnd)(int on);
    int (*set_option)(const char *addon, const char *option, const char *value);
} SwoftyHub;

/* Runtime string overrides (API v8): the localised text a lng_* key resolves to, changed while the
   game runs. language NULL or "*" means every language; otherwise a language code (eng, fre, ...)
   that applies while it is the game's current language and wins over "*". The text is UTF-8 and is
   copied. A change shows wherever the game looks the key up next; refresh() re-sends the current
   language to the game's language listeners so bound labels read their text again (game thread). */
typedef struct SwoftyLang {
    int (*available)(void);
    int (*override_set)(const char *language, const char *key, const char *text);
    int (*override_clear)(const char *language, const char *key);
    int (*override_clear_all)(void);
    int (*refresh)(void);
} SwoftyLang;

/* Extra career contents (no API version of its own: the table is read only when the core exports
   SwoftyCoreCareerContent, see swofty/career_content.hpp). A content is a tile on the career
   content select after the stock 2025 and 2026 ones, with its own save slots (its data group). The
   core builds the tile, presses it, routes it, passes its mode through the driver-career gate and
   answers the save-slot predicate for it. Strings are copied. */
#define SWOFTY_EVENT_CAREER_CONTENT_PRESSED "career.content.pressed"

enum {
    SWOFTY_CAREER_CONTENT_MODEL_2026 = 0,
    SWOFTY_CAREER_CONTENT_MODEL_2025 = 1,
};

enum {
    SWOFTY_CAREER_CONTENT_DRIVER = 1,
    SWOFTY_CAREER_CONTENT_MY_TEAM = 2,
};

enum {
    SWOFTY_CAREER_CONTENT_QUEUED = 0,
    SWOFTY_CAREER_CONTENT_LIVE = 1,
    SWOFTY_CAREER_CONTENT_OFF = 2,
};

typedef struct SwoftyCareerContentDesc {
    uint32_t size;
    uint32_t model;
    const char *id;
    const char *title;
    const char *description;
    const char *data_group;
    const char *suffix;
    const char *mode;
    const char *entitlement;
    int32_t car_group;
    uint32_t badge;
    uint32_t careers;
} SwoftyCareerContentDesc;

typedef struct SwoftyCareerContentInfo {
    uint32_t index;
    uint32_t state;
    int64_t mode_stat_id;
    int64_t data_group_index;
    uint32_t tiles;
    uint32_t presses;
    char id[64];
    char data_group[64];
    char why[192];
} SwoftyCareerContentInfo;

typedef struct SwoftyCareerContent {
    int (*available)(void);
    int (*add)(const SwoftyCareerContentDesc *desc);
    int (*info)(const char *id, SwoftyCareerContentInfo *out);
    int (*data_group)(const void *career, char *out, size_t cap);
    int (*pressed)(char *out, size_t cap);
} SwoftyCareerContent;

/* Per-career character overrides (swofty/characters.hpp). Present on cores that have them, NULL
   otherwise; SWOFTY_API_CHARACTERS marks a header that declares the table. Every entry is keyed by
   a career data group (the CareerSetupComponent data group, e.g. f1_career_2026) and applies only
   while the loaded career's data group is that one. swap() builds the character's UI ref (portrait,
   names, job) from another entrants.characters row under the character's own stat_id; replacement
   NULL drops the swap. mute() plays a speaker's lines at speech volume 0. string_set() and
   strings_json() hold runtime string overrides (SwoftyLang) that are set while the group's career
   is loaded; text NULL drops one. clear() drops every entry of the group that the calling module
   made. refresh() rebuilds the character list now (game thread). */
#define SWOFTY_API_CHARACTERS 1
typedef struct SwoftyCharacters {
    int (*available)(void);
    int (*career_group)(char *out, size_t cap);
    int (*swap)(const char *group, const char *character, const char *replacement);
    int (*mute)(const char *group, const char *speaker, int on);
    int (*string_set)(const char *group, const char *language, const char *key, const char *text);
    int (*strings_json)(const char *group, const char *json, size_t len);
    int (*clear)(const char *group);
    int (*refresh)(void);
} SwoftyCharacters;

/* Career wizard control (swofty/career_wizard.hpp). Present on cores that have it, NULL otherwise;
   SWOFTY_API_CAREER_WIZARD marks a header that declares the table. Every entry is keyed by the data
   group of the career content tile the wizard was opened from (wizard state +0x128). starts_in_f1()
   answers yes for the group (no F2 choice, no team-icons step). tile_route() makes the tile open
   route `to` instead of `from` (NULL or "" for any route); the route must exist in the front end's
   routes. replace_step() pushes step `with` where the wizard would push `step`; force_option()
   records option `with` where the player picked `option`. confirm_route() makes the confirm step
   open `to` instead of `from`. hide_driver_tab() drops a Choose Driver header tab. Passing the
   stock value back (on 0, with == step, to NULL) drops an entry. clear() drops every entry of the
   group that the calling module made. group() names the data group of the last career tile. */
#define SWOFTY_API_CAREER_WIZARD 1
typedef struct SwoftyCareerWizard {
    int (*available)(void);
    int (*group)(char *out, size_t cap);
    int (*starts_in_f1)(const char *group, int on);
    int (*tile_route)(const char *group, const char *from, const char *to);
    int (*replace_step)(const char *group, uint32_t step, uint32_t with);
    int (*force_option)(const char *group, uint32_t option, uint32_t with);
    int (*confirm_route)(const char *group, const char *from, const char *to);
    int (*hide_driver_tab)(const char *group, const char *tab, int on);
    int (*clear)(const char *group);
} SwoftyCareerWizard;

/* Era picker boxes (swofty/era.hpp). Present on cores that have them, NULL otherwise; the table is read
   only when the core exports SwoftyCoreEra, and SWOFTY_API_ERA marks a header that declares it. A box
   is a box in the Addons row of the Grand Prix / Time Trial era picker that selects entrant_list, which
   must be in the entrant lists of the core's host setup mode swofty_f1; the core skips, with a log
   line, a box whose list the screen's mode does not offer there. title is a lng_* key (NULL or "" for
   lng_<entrant_list>), image a texture path (NULL or "" for
   #overrides.home_screen.tiles_solo_6x6[<entrant_list>]), order sorts the boxes (then registration
   order). Strings are copied. */
#define SWOFTY_API_ERA 1
typedef struct SwoftyEraBox {
    uint32_t size;
    const char *entrant_list;
    const char *title;
    const char *image;
    int32_t order;
} SwoftyEraBox;

typedef struct SwoftyEra {
    int (*available)(void);
    int (*add_box)(const SwoftyEraBox *box);
    int (*count)(void);
} SwoftyEra;

typedef struct SwoftyApi {
    uint32_t version;
    void (*log)(const char *fmt, ...);
    uintptr_t (*module_base)(void);
    int (*scan)(const char *section, const char *pattern, uintptr_t *out);
    uintptr_t (*resolve)(const char *id);
    int (*register_mixins)(const SwoftyMixin *mixins, size_t count);
    void *(*fields)(const void *obj, size_t size);
    int (*setting)(const char *id, uint32_t type, SwoftySettingValue *out);
    int (*watch_setting)(const char *id, SwoftySettingFn fn, void *user);
    const SwoftyAssets *assets;
    const SwoftyEvents *events;
    const SwoftyOverlay *overlay;
    const SwoftyTelemetry *telemetry;
    const SwoftyDb *db;
    const SwoftyStorage *storage;
    const SwoftyCareer *career;
    const SwoftyPopup *popup;
    const SwoftyLang *lang;
    const SwoftyCareerContent *career_content;
    const SwoftyCharacters *characters;
    const SwoftyCareerWizard *career_wizard;
    const SwoftyEra *era;
} SwoftyApi;

typedef int (*SwoftyAddonInitFn)(const SwoftyApi *api);
typedef SwoftyAddonInitFn SwoftyModInitFn;

typedef int (*SwoftyCoreAttachFn)(SwoftyApi *api);
typedef int (*SwoftyCoreInitFn)(const wchar_t *name, SwoftyAddonInitFn init);
typedef int (*SwoftyCoreCommitFn)(void);
typedef int (*SwoftyCoreStageFn)(int stage);

#define SWOFTY_STAGE_WINDOW 1

#if defined(_WIN32)
#define SWOFTY_EXPORT __declspec(dllexport)
#else
#define SWOFTY_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}
#endif
