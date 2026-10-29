#pragma once

#include "lower.hpp"

#include <string>
#include <vector>

namespace swofty_core::apply::content {

struct LogoReport {
    std::vector<std::string> lines;
    int built = 0, refused = 0;
};

void deliver_logos(const std::vector<LogoImage> &logos, LogoReport &report);

}
