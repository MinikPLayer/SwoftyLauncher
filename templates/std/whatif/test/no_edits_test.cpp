// SPDX-License-Identifier: MIT
#include "../native/src/whatif.cpp"

#include "whatif_test.hpp"

static void nothing_edited()
{
    Run run("test/no-storage/");
    check(run.core.all_layers().empty(), "a profile with no edits adds no layer");
}

int main()
{
    nothing_edited();
    return failures ? 1 : 0;
}
