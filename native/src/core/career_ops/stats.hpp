#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace career_ops::stats {

// One driver's base values of the given statistics (game stat ids: experience 0, racecraft 1, awareness 3,
// pace 4, focus 5), written into the loaded career's driver statistics. text says what changed.
int set(uint32_t driver_stat, const uint16_t *stat_ids, const float *values, size_t count, std::string &text);

}
