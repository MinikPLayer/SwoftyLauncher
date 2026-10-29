// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <string>
#include <vector>
#include <swofty/registry.hpp>

// UI and strings: one text key in every language, and the options an addon shows in ADDON SETTINGS.
// The 1.x string library (swofty/lang.hpp) writes the calling addon's global layer of a lang.string
// record; the options of an addon's swofty.json are addon.option records under its id.

namespace swofty::ui {

using Result = swofty::Result;

struct String {
    static constexpr Version version{1, 0};
    std::map<std::string, std::string> text;
    json::Value within;
    json::Value extra;
};
SWOFTY_RECORD(String, text, within)
SWOFTY_RECORD_REGISTRY(String, "lang.string")
inline Registry<String> strings{"lang.string", {.applies = Applies::read}};

struct OptionState {
    std::string text;
    json::Value value;
};
SWOFTY_RECORD(OptionState, text, value)

struct AddonOption {
    static constexpr Version version{1, 0};
    std::string type;
    std::vector<OptionState> states;
    json::Value value;
    json::Value extra;
};
SWOFTY_RECORD(AddonOption, type, states, value)
SWOFTY_RECORD_REGISTRY(AddonOption, "addon.option")
inline Registry<AddonOption> addon_options{"addon.option", {.applies = Applies::read}};

}
