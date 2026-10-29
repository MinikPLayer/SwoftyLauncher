#include "../../src/core/registry/table.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace swofty_core::registry;

int main(int argc, char **argv)
{
    int n = argc > 1 ? std::atoi(argv[1]) : 10000;
    Store &st = store();
    st.declare("test.many", swofty::json::parse(R"({"x-registry":"test.many","type":"object"})"));
    for (int i = 0; i < n; i++) {
        char key[32];
        std::snprintf(key, sizeof key, "k%06d", i);
        st.add("a.addon", "test.many", key, swofty::json::parse(R"({"n":1})"), nullptr);
    }
    const SwoftyRegistry *t = table();
    char id[256];
    int count = 0;
    auto t0 = std::chrono::steady_clock::now();
    if (t->first("test.many", "", id, sizeof id) == SWOFTY_ASSET_OK) {
        std::string after = id;
        for (count = 1; t->next("test.many", "", after.c_str(), id, sizeof id) == SWOFTY_ASSET_OK; count++)
            after = id;
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%d of %d record(s) listed through first/next in %.1f ms\n", count, n, ms);
    if (count != n || ms > 1000) {
        std::printf("FAIL: listing a registry of %d records should take well under a second; next() re-lists the whole "
                    "registry on every call\n",
                    n);
        return 1;
    }
    return 0;
}
