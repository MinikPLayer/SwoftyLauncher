// A variant edit that never returns: the recorder must time out and name the edit.
#include <swofty/registry.hpp>
#include <swofty/systems/career.hpp>

using namespace swofty;

static volatile bool forever = true;

SWOFTY_REGISTER
{
    career::modes.variant("stuck", career::modes["game/f1_career_2026"], [](career::ModeRecord &) {
        while (forever) {
        }
    });
}

SWOFTY_ADDON()
