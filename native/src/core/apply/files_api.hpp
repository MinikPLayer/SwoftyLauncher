#pragma once

#include "files.hpp"

#include <string>
#include <vector>

// The appliers' file route (files.cpp): redirect a game path to a file in an addon's folder for a scope, put a
// scope's redirects back, and add the file conflicts to conflicts.json. Every call is logged.

namespace swofty_core::apply::files {

Outcome redirect(const std::string &game_path, const std::string &addon, const std::string &file,
                 const std::string &registry, const std::string &id, const std::string &scope);
std::vector<std::string> put_back(const std::string &scope);
std::string winner(const std::string &game_path);
void report(Value &conflicts);

}
