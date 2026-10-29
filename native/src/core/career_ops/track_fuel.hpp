#pragma once
#include <string>

#include "swofty_api.h"

namespace career_ops {

int set_track_fuel(const std::string &who, SwoftyText request, char *out, size_t cap, size_t *len);
void track_fuel_setup();

}
