// SPDX-License-Identifier: MIT
#include "../native/src/whatif.cpp"

#include "whatif_test.hpp"

static void without_progressive()
{
    Run run("test/storage/", false);
    check(run.core.layers("career.season").size() == 1, "the layer is still made without Progressive");
    if (run.core.rules())
        check(!run.core.effective("career.season", "swofty.progressive/2017"),
              "but there's no season for it to change");
}

int main()
{
    without_progressive();
    return failures ? 1 : 0;
}
