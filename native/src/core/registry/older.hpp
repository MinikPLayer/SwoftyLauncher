#pragma once

#include "store.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The 1.5 libraries underneath registries. Each older call stores what it was given as a record in the
// calling addon's name, and the library's behaviour reads the effective record, so other addons can
// read and layer it. With no layer the effective record is what the call gave, field for field.
//
// - career.content: "<addon>/<content id>", added by career::content::add.
// - hub.tab: "<addon>/<tab name>", added from the swofty.json hub_tab field.
// - career.wizard: "<addon>/<data group>", the addon's whole wizard state for that group, rewritten by
//   every call.
// - career.character: "<addon>/<data group>", the addon's swaps, mutes and strings for that group.
// - lang.string: "game/<lng key>", provided by the core for every key; lang::set writes the calling
//   addon's global layer of it.
// - addon.option: "<addon>/<option id>", added from settings.json's options; the player's choice is its
//   value.

namespace swofty_core::registry::older {

inline constexpr const char *CONTENT = "career.content";
inline constexpr const char *HUB_TAB = "hub.tab";
inline constexpr const char *WIZARD = "career.wizard";
inline constexpr const char *CHARACTER = "career.character";
inline constexpr const char *STRING = "lang.string";
inline constexpr const char *OPTION = "addon.option";

void declare(Store &st);
int put(Store &st, const std::string &addon, const char *registry, const std::string &key, const Value &value,
        const std::string &source);
int drop(Store &st, const std::string &addon, const char *registry, const std::string &key);
Value effective(Store &st, const char *registry, const std::string &id);
std::vector<std::string> in_load_order(Store &st, const char *registry);
std::string addon_of(const std::string &id);
std::string key_of(const std::string &id);

struct ContentDesc {
    std::string id, title, description, data_group, suffix, mode, entitlement;
    uint32_t model = 1;
    int32_t car_group = -1;
    uint32_t badge = 0;
    uint32_t careers = 1;
};
Value content_record(const ContentDesc &d);
void content_shown(const Value &rec, ContentDesc &d);

struct HubTab {
    std::string addon, name, title, group;
    std::vector<std::string> options;
};
Value hub_record(const HubTab &t);
HubTab hub_from(const std::string &id, const Value &rec);

enum WizardKind { W_START, W_TILE_ROUTE, W_STEP, W_OPTION, W_CONFIRM_ROUTE, W_TAB };
struct WizardEntry {
    WizardKind kind;
    std::string group, from, to;
    uint32_t a = 0, b = 0;
};
Value wizard_record(const std::string &group, const std::vector<WizardEntry> &given);
std::vector<WizardEntry> wizard_entries(const Value &rec);

struct CharacterState {
    std::string group;
    std::vector<std::pair<std::string, std::string>> swaps;
    std::vector<std::string> mutes;
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> strings;
};
Value character_record(const CharacterState &s);
CharacterState character_from(const Value &rec);

int string_layer(Store &st, const std::string &addon, const std::string &key, const Value &text);
std::vector<std::pair<std::string, std::string>> string_texts(const Value &rec);
std::vector<std::string> string_ids(Store &st);

struct OptionState {
    std::string text;
    Value value;
};
Value option_record(const std::string &type, const std::vector<OptionState> &states, std::size_t current);
std::size_t option_current(const Value &rec, const std::vector<OptionState> &states, std::size_t given);

}
