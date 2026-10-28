// SPDX-License-Identifier: AGPL-3.0-or-later

#include "weekend_logic.hpp"

#include <cstdio>
#include <cstdlib>

using namespace progressive;

namespace {

int failures = 0;

void expect(bool ok, const char *what, int line)
{
    if (!ok) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define EXPECT(x) expect((x), #x, __LINE__)

weekend::State fresh()
{
    weekend::State s;
    s.sessions = 5;
    s.first_rows = 21;
    return s;
}

void only_an_untouched_weekend_is_rebuilt()
{
    EXPECT(weekend::not_rebuildable(fresh()) == nullptr);
    weekend::State s = fresh();
    s.progress = 1;
    EXPECT(weekend::not_rebuildable(s) != nullptr);
    s = fresh();
    s.current = 1;
    EXPECT(weekend::not_rebuildable(s) != nullptr);
    s = fresh();
    s.first_completion = 1;
    EXPECT(weekend::not_rebuildable(s) != nullptr);
    s = fresh();
    s.sessions = 0;
    EXPECT(weekend::not_rebuildable(s) != nullptr);
    s = fresh();
    s.first_rows = 0;
    EXPECT(weekend::not_rebuildable(s) != nullptr);
}

void entrants_compare_as_a_multiset()
{
    EXPECT(weekend::same_entrants({1, 2, 3}, {3, 1, 2}));
    EXPECT(!weekend::same_entrants({1, 2, 3}, {1, 2, 3, 4}));
    EXPECT(!weekend::same_entrants({1, 2, 3, 4}, {1, 2, 3}));
    EXPECT(!weekend::same_entrants({1, 2, 5}, {1, 2, 3}));
    EXPECT(!weekend::same_entrants({1, 1, 2}, {1, 2, 2}));
    EXPECT(weekend::same_entrants({}, {}));
    std::vector<uint32_t> creation, seated;
    for (uint32_t d = 0; d < 21; ++d)
        creation.push_back(d);
    seated = creation;
    seated.push_back(99);
    EXPECT(!weekend::same_entrants(creation, seated));
    seated.resize(20);
    EXPECT(!weekend::same_entrants(creation, seated));
}

} // namespace

int main()
{
    only_an_untouched_weekend_is_rebuilt();
    entrants_compare_as_a_multiset();
    if (failures) {
        std::printf("weekend_logic_test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("weekend_logic_test: ok\n");
    return EXIT_SUCCESS;
}
