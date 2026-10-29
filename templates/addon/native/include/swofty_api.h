/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWOFTY_API_VERSION 9

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

/* Standard library 1.6 (API v9): the registry, hook points and services. Each table is appended to
   SwoftyApi and also returned by a core export (SwoftyCoreRegistry, SwoftyCoreHooks, SwoftyCoreServices),
   so an addon built against this header also loads on an older core and reads the three libraries as
   unavailable there. A table's version is the number of the last layout the core fills; fields are only
   ever appended. Results are the SWOFTY_ASSET_* codes plus SWOFTY_TOO_SMALL: *len then holds the size
   needed, and the caller retries with a bigger buffer. The core copies every string and JSON text it is
   given; values come back in the caller's buffer. A refusal leaves its reason in last_error for the
   calling thread. Records, edits and selectors cross the ABI as JSON text: a record is its struct's
   SWOFTY_RECORD fields by name, a Ref is the record's full id ("game/ferrari_2025") or null, a keyed
   list is an array matched by its x-key fields, and a pointer names a keyed element as "slot=1"
   ("/teams/slot=1/seats/0"). Selectors (layer filters, handler scopes) are a JSON array whose entries
   must all match: {"content": id}, {"content_exact": id}, {"owner": addon}, {"data_group": name},
   {"mode": "time_trial"}, {"session_type": ["race"]}, {"track": id}, {"year": [from, to]},
   {"setup_mode": id}, {"vehicle_package": id}, {"any": [selector, ...]}, {"not": selector}. */
#define SWOFTY_REGISTRY_VERSION 1
#define SWOFTY_HOOKS_VERSION 1
#define SWOFTY_SERVICES_VERSION 1

enum { SWOFTY_TOO_SMALL = 4 };

enum { SWOFTY_SCOPE_GLOBAL = 0, SWOFTY_SCOPE_PROFILE = 1, SWOFTY_SCOPE_CAREER = 2 };
enum { SWOFTY_APPLIES_BOOT = 0, SWOFTY_APPLIES_CONTENT = 1, SWOFTY_APPLIES_SESSION = 2, SWOFTY_APPLIES_READ = 3 };
enum {
    SWOFTY_STATUS_OK = 0,
    SWOFTY_STATUS_NO_RECORD = 1,
    SWOFTY_STATUS_NO_REGISTRY = 2,
    SWOFTY_STATUS_INVALID = 3,
    SWOFTY_STATUS_LATE = 4,
};
enum { SWOFTY_PHASE_BEFORE = 0, SWOFTY_PHASE_REPLACE = 1, SWOFTY_PHASE_AFTER = 2 };
enum { SWOFTY_POINT_NOTIFY = 0, SWOFTY_POINT_MODIFY = 1, SWOFTY_POINT_CANCEL = 2 };

typedef struct SwoftyText {
    const char *data;
    size_t size;
} SwoftyText;

typedef int (*SwoftyProvideFn)(void *user, const char *registry, const char *id, char *out, size_t cap, size_t *len);
typedef void (*SwoftyListFn)(void *user, const char *registry, void (*add)(void *ctx, const char *id), void *ctx);
typedef void (*SwoftyRecordFn)(void *user, const char *registry, const char *id);
typedef int (*SwoftyConvertFn)(void *user, SwoftyText in, char *out, size_t cap, size_t *len);
typedef void (*SwoftyCheckFn)(void *user, SwoftyText value, void (*error)(void *ctx, const char *path, const char *msg),
                              void *ctx);
typedef int (*SwoftyEditFn)(void *user, SwoftyText in, char *out, size_t cap, size_t *len);
typedef int (*SwoftyMatchFn)(void *user, SwoftyText base);

/* One layer, as layer_ex takes it. id names one record; otherwise the layer is a selector over every
   record that passes all of match (run on the base value), prefix, tag (with tag_field, the JSON pointer
   of a Ref field whose target must be in the tag; NULL for the record itself) and is not in except.
   The change is edit (code) or patch (a JSON merge patch with keyed arrays and $delete). in is a
   selector array (LayerHandle::in), empty for everywhere. */
typedef struct SwoftyLayerDesc {
    uint32_t size;
    uint32_t scope;
    const char *registry;
    const char *id;
    SwoftyMatchFn match;
    void *match_user;
    const char *prefix;
    const char *const *except;
    size_t except_count;
    const char *tag;
    const char *tag_field;
    SwoftyText in;
    SwoftyEditFn edit;
    void *user;
    SwoftyText patch;
} SwoftyLayerDesc;

/* Registry table, version 1. declare: a registry's schema (JSON Schema subset with x-registry, x-version,
   x-applies, x-snapshot, x-snapshot-budget, x-key, x-ref); idempotent, the core keeps the highest minor of
   each major and its own schema for the registries it owns. add, variant, provide: records under the
   calling addon's own keys, the id comes back as "<addon id>/<key>". layer, layer_fn, layer_select,
   layer_ex: changes to any record, run on its effective value in load order. get takes
   "<name>@<major>" or "<name>". status returns a SWOFTY_STATUS_* value (not a result code). first and
   next list ids under a prefix in id order. option is the calling addon's option; called inside an
   edit it also records that the edit re-runs when the option changes. layer_ex returns the layer's
   handle; layer_in narrows it to where its selector array matches (refused on an Applies::boot
   registry). protect names fields of an own record that other addons' selectors skip. tag_* work on
   shared tags ("c/f1_cars"); tag_members writes a JSON array of ids. variant_patch is variant with a
   JSON merge patch. stat_id is the record's allocated stat id. */
typedef struct SwoftyRegistry {
    uint32_t version;
    int (*available)(void);
    int (*declare)(const char *registry, SwoftyText schema_json);
    int (*check)(const char *registry, SwoftyCheckFn fn, void *user);
    int (*adapter)(const char *registry, uint32_t from_major, uint32_t to_major, SwoftyConvertFn up,
                   SwoftyConvertFn down, void *user);
    int (*provide)(const char *registry, const char *id, SwoftyText value_json);
    int (*provide_fn)(const char *registry, SwoftyProvideFn fn, SwoftyListFn list, void *user);
    int (*invalidate)(const char *registry, const char *id);
    int (*layer)(const char *registry, const char *id, SwoftyText patch_json, uint32_t scope);
    int (*clear_layer)(const char *registry, const char *id, uint32_t scope);
    int (*get)(const char *registry_at_major, const char *id, char *out, size_t cap, size_t *len);
    int (*status)(const char *registry, const char *id);
    int (*origin)(const char *registry, const char *id, const char *pointer, char *addon, size_t cap);
    int (*first)(const char *registry, const char *prefix, char *id, size_t cap);
    int (*next)(const char *registry, const char *prefix, const char *after, char *id, size_t cap);
    int (*watch)(const char *registry, SwoftyRecordFn fn, void *user, uint32_t *token);
    int (*unwatch)(uint32_t token);
    int (*last_error)(char *out, size_t cap);
    int (*add)(const char *registry, const char *key, SwoftyText value_json, char *id, size_t cap);
    int (*variant)(const char *registry, const char *key, const char *base_id, SwoftyEditFn edit, void *user,
                   char *id, size_t cap);
    int (*layer_fn)(const char *registry, const char *id, SwoftyEditFn edit, void *user, uint32_t scope);
    int (*layer_select)(const char *registry, SwoftyMatchFn match, const char *prefix, const char *const *except,
                        size_t except_count, SwoftyEditFn edit, void *user, uint32_t scope);
    int (*option)(const char *id, uint32_t type, SwoftySettingValue *out);
    int (*migrate)(const char *registry, uint32_t from_major, uint32_t to_major, SwoftyConvertFn fn, void *user);
    int (*layer_ex)(const SwoftyLayerDesc *desc, uint32_t *layer);
    int (*layer_in)(uint32_t layer, SwoftyText selectors_json);
    int (*protect)(const char *registry, const char *id, const char *const *fields, size_t count);
    int (*tag_add)(const char *registry, const char *tag, const char *id);
    int (*tag_contains)(const char *registry, const char *tag, const char *id, int *out);
    int (*tag_members)(const char *registry, const char *tag, char *out, size_t cap, size_t *len);
    int (*variant_patch)(const char *registry, const char *key, const char *base_id, SwoftyText patch_json, char *id,
                         size_t cap);
    int (*stat_id)(const char *registry, const char *id, int64_t *out);
} SwoftyRegistry;

/* One call of a hook point or event, as a SwoftyHookFn sees it. payload is the point's struct, which
   starts with uint32_t size and uint32_t payload_version. replaced is set by the core after the replace
   phase. context is the moment's context, for the hooks table's context and context_in. */
typedef struct SwoftyHookCall {
    uint32_t size;
    uint32_t phase;
    const char *point;
    void *payload;
    int replaced;
    const void *context;
} SwoftyHookCall;

typedef int (*SwoftyHookFn)(void *user, SwoftyHookCall *call);

/* Hooks table, version 1. Events are hook points: attach a handler to a phase (SWOFTY_PHASE_*) with a
   priority (higher first); in the replace phase a non-zero return means handled. attach_in also takes a
   selector array: the core calls the handler only where it matches. define declares an addon point
   under the addon's own id (define_in with its kind, SWOFTY_POINT_*, and the scope it fires in). fire
   runs the chain on payload; *replaced is non-zero when a replace handler handled it. live is OK when
   the point is installed and runs its subscribers. context writes one part of a call's context as JSON:
   "mode", "content", "career", "weekend", "session", "player", "participants", or "record:<registry>";
   context_in sets *out to 1 when the context matches the selector array. */
typedef struct SwoftyHooks {
    uint32_t version;
    int (*available)(void);
    int (*live)(const char *point, uint32_t *payload_version);
    int (*attach)(const char *point, uint32_t phase, int32_t priority, SwoftyHookFn fn, void *user, uint32_t *token);
    int (*detach)(uint32_t token);
    int (*define)(const char *point, uint32_t payload_version);
    int (*fire)(const char *point, void *payload, int *replaced);
    int (*attach_in)(const char *point, uint32_t phase, int32_t priority, SwoftyText selectors_json, SwoftyHookFn fn,
                     void *user, uint32_t *token);
    int (*define_in)(const char *point, uint32_t payload_version, uint32_t kind, SwoftyText scope_json);
    int (*context)(const void *context, const char *part, char *out, size_t cap, size_t *len);
    int (*context_in)(const void *context, SwoftyText selectors_json, int *out);
} SwoftyHooks;

typedef void (*SwoftyServiceFn)(void *user, const void *table, size_t size, uint32_t version);

/* Services table, version 1. publish: a table of function pointers under a name and version (the table
   starts with uint32_t size and stays valid while the addon is loaded). when: fn runs on the flow thread
   once a table of that name, of at least min_version and min_size, is published; at once when it
   already is. */
typedef struct SwoftyServices {
    uint32_t version;
    int (*available)(void);
    int (*publish)(const char *name, uint32_t version, const void *table, size_t size);
    int (*when)(const char *name, uint32_t min_version, size_t min_size, SwoftyServiceFn fn, void *user);
} SwoftyServices;

/* Career operations (standard library 1.6), from the core export SwoftyCoreCareerOps; not a SwoftyApi
   field. Table version 1. Every call runs on the game's flow thread (an events, hook or on_frame callback;
   SWOFTY_ASSET_LATE from any other thread), reports SWOFTY_ASSET_UNAVAILABLE and logs the binding when one
   it needs does not resolve on the running build, and writes one [career-ops] log line.
   driver_stat: a driver reference ("game/<label>", "person/<label>") to the stat id of the drivers table row
   with that label. set_slots: a data group's career team slots, slot N (1-based) racing as team stat
   team_stats[N-1]; apply_lineup reads it for a season record. apply_lineup takes a JSON request, either
   {"season": <a career.season record>} (the core plans the seats) or {"lineup": {...}} (the seats, releases,
   numbers, cars and looks in stat ids, run as given), and writes a JSON report to out (SWOFTY_TOO_SMALL:
   *len holds the size needed). set_driver_ratings: one driver's base values of count statistics by game
   stat id (experience 0, racecraft 1, awareness 3, pace 4, focus 5). swap_drivers: two seated drivers
   exchange seats. Stat ids are the upper 16 bits of the game's keys.
   Table version 2 adds report_season: the addon that owns the active career's data group (its career
   content or career.mode record) says the season at the Championship's season_index is year; the core
   then fires career.season.start once per career and year, activates the season's records and freezes
   its snapshot. Any other caller gets SWOFTY_REFUSED; no career loaded, SWOFTY_ASSET_LATE.
   Table version 3 adds apply_teams: the request {"season": <a career.season record>}; each listed slot
   (1-12) races as its team (the data group's slot table, the Championship's participants), and a team
   with no career rows gets them, built from its donor's (contracts, R&D, standings, initial cash, renown
   and the side data found by team label); an empty team leaves the slot out of the Championship. The
   report has apply_lineup's shape, counted per slot. A core that has not implemented it leaves it NULL. The
   request {"start": {"data_group": "<group>", "year": <start season>}} dresses the slot rows of that season's
   added teams (a slot naming a team.identity an addon made) for the new-career wizard, and year 0 takes the dress
   off; the season start then dresses the same rows the same way. The report counts "dressed" and "undressed".
   Table version 4 adds set_seat, set_car_number and set_track_fuel. set_seat takes {"driver": <stat>,
   "team": <stat> or "slot": <1-based slot of the loaded career's data group>, "seat": 0 or 1, "car": <stat,
   optional>}: the driver takes that race seat in Driver Career; the seat's driver takes his old seat, or
   leaves the grid when he had none. The report is apply_lineup's with "holder": "swapped", "released" or
   "none". set_car_number: the driver's racing number (1-99) on his career row and the current weekend's
   copy; a number another row holds is SWOFTY_REFUSED unless flags has SWOFTY_NUMBER_SWAP, which gives that
   row the driver's old number. set_track_fuel takes {"track": "<code>" or "*", "set": "<data set>",
   "from": "<data set>"} (the fuel of the same track in another data set: burn per lap, race fuel rows and
   qualifying fuel), {"track", "set", "burn_per_lap": <kg>}, or {"copy": [{"set", "from"}, ...], "track"}
   (every pair checked before anything is written), or {"restore": true}; "why" names the reason in the
   log. The data sets are f1_2025, sprint_f1_2025, f1_2026 and sprint_f1_2026. The core puts the bytes
   back on career.unloaded and on restore. Table version 5 adds set_logo(team_id, logo_id): the team.identity
   record's logo for the rest of the session is the team.logo record (both ids as the registry spells them);
   SWOFTY_REFUSED when either record does not exist.
   Table version 6 adds set_look: the driver (a driver reference: "game/<label>", "person/<key>" or an addon's
   driver.identity id) wears the look (a look reference: "game/<label>" or an addon's driver.look id) in Driver
   Career: his entrant row's look, which every place that shows his body reads (grid presentation, portraits,
   cutscenes, the garage). The call refuses a look the game did not load (SWOFTY_ASSET_INVALID) and a driver the
   career does not seat. The report is one [career-ops] log line. */
#define SWOFTY_CAREER_OPS_VERSION 6
enum { SWOFTY_REFUSED = 5 };
enum { SWOFTY_NUMBER_SWAP = 1 };

typedef struct SwoftyCareerOps {
    uint32_t version;
    int (*available)(void);
    int (*driver_stat)(const char *driver_ref, uint32_t *out);
    int (*set_slots)(const char *data_group, const uint32_t *team_stats, size_t count);
    int (*apply_lineup)(SwoftyText request_json, char *report, size_t cap, size_t *len);
    int (*set_driver_ratings)(uint32_t driver_stat, const uint16_t *stat_ids, const float *values, size_t count);
    int (*swap_drivers)(uint32_t a_stat, uint32_t b_stat);
    int (*report_season)(int32_t year, int32_t season_index);
    int (*apply_teams)(SwoftyText request_json, char *report, size_t cap, size_t *len);
    int (*set_seat)(SwoftyText request_json, char *report, size_t cap, size_t *len);
    int (*set_car_number)(uint32_t driver_stat, int32_t number, uint32_t flags);
    int (*set_track_fuel)(SwoftyText request_json, char *report, size_t cap, size_t *len);
    int (*set_logo)(const char *team_id, const char *logo_id);
    int (*set_look)(const char *driver_ref, const char *look_ref);
} SwoftyCareerOps;

/* Session operations (standard library 1.6), from the core export SwoftyCoreSessionOps; not a SwoftyApi
   field. Table version 1. Every call runs on the game's flow thread and reports SWOFTY_ASSET_UNAVAILABLE
   when a game function it needs does not resolve on the running build. control: the mode (0 human, 1 AI)
   and the game's controller flag bits (PIT_LANE is bit 0) of the car with that index, read fresh from its
   controller set; SWOFTY_ASSET_LATE while the sets are not built (before the session's first build, or
   between a destroy and the next build). set_control: mode 0 or 1 for the car; the core queues it to the
   next frame when called from inside the controller swap, never applies it between the sets' destroy and
   build, and returns SWOFTY_REFUSED when a flag the core did not set keeps the car on AI. It reaches the
   game through Game_Set_VehicleController, so session.player.control fires for it. The spectator calls
   work on the session's spectator director: spectator_set_active turns the director and its HUD on or
   off; spectator_follow moves it to the car; spectator_set_camera takes 1 (TV pod) or 2 (broadcast);
   spectator_current reads the car (index, or -1 for none) and the camera (-1 for none) while the director
   is active, else SWOFTY_ASSET_LATE. */
#define SWOFTY_SESSION_OPS_VERSION 1

typedef struct SwoftySessionOps {
    uint32_t version;
    int (*available)(void);
    int (*control)(uint32_t car, uint32_t *mode, uint32_t *flags);
    int (*set_control)(uint32_t car, uint32_t mode);
    int (*spectator_set_active)(int on);
    int (*spectator_follow)(uint32_t car);
    int (*spectator_set_camera)(int32_t camera);
    int (*spectator_current)(int32_t *car, int32_t *camera);
} SwoftySessionOps;

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
    const SwoftyRegistry *registry;
    const SwoftyHooks *hooks;
    const SwoftyServices *services;
} SwoftyApi;

typedef int (*SwoftyAddonInitFn)(const SwoftyApi *api);
typedef SwoftyAddonInitFn SwoftyModInitFn;

typedef int (*SwoftyCoreAttachFn)(SwoftyApi *api);
typedef int (*SwoftyCoreInitFn)(const wchar_t *name, SwoftyAddonInitFn init);
typedef int (*SwoftyCoreCommitFn)(void);
typedef int (*SwoftyCoreStageFn)(int stage);
typedef int (*SwoftyAddonRegisterFn)(const SwoftyApi *api);
typedef const SwoftyRegistry *(*SwoftyCoreRegistryFn)(void);
typedef const SwoftyHooks *(*SwoftyCoreHooksFn)(void);
typedef const SwoftyServices *(*SwoftyCoreServicesFn)(void);
typedef const SwoftyCareerOps *(*SwoftyCoreCareerOpsFn)(void);
typedef const SwoftySessionOps *(*SwoftyCoreSessionOpsFn)(void);

#define SWOFTY_STAGE_WINDOW 1

#if defined(_WIN32)
#define SWOFTY_EXPORT __declspec(dllexport)
#else
#define SWOFTY_EXPORT __attribute__((visibility("default")))
#endif

/* Core exports for feature detection (API v9), and the addon export SWOFTY_ADDON() emits when the file
   that expands it includes swofty/registry.hpp: the loader calls SwoftyAddonRegister at the early stage,
   in load order, and SwoftyAddonInit at the stage the manifest asks for. */
SWOFTY_EXPORT const SwoftyRegistry *SwoftyCoreRegistry(void);
SWOFTY_EXPORT const SwoftyHooks *SwoftyCoreHooks(void);
SWOFTY_EXPORT const SwoftyServices *SwoftyCoreServices(void);
SWOFTY_EXPORT const SwoftyCareerOps *SwoftyCoreCareerOps(void);
SWOFTY_EXPORT const SwoftySessionOps *SwoftyCoreSessionOps(void);

#ifdef __cplusplus
}
#endif
