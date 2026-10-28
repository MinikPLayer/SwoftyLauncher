#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
OUT=${CE_TEST_DIR:-${TMPDIR:-/tmp}}
mkdir -p "$OUT"
CXX=(g++ -std=c++23 -O1 -Wall -Wextra -Wno-misleading-indentation -include unistd.h -I../src -I../../../../packages/sdk/include)
"${CXX[@]}" -o "$OUT/ce_race_numbers_test" race_numbers_test.cpp ../src/race_numbers.cpp
"$OUT/ce_race_numbers_test"
"${CXX[@]}" -o "$OUT/ce_bridge_harness" bridge_harness.cpp ../src/bridge.cpp
"${CXX[@]}" -o "$OUT/ce_weather_plan_test" weather_plan_test.cpp ../src/weather_plan.cpp
"${CXX[@]}" -o "$OUT/ce_lineup_test" lineup_test.cpp ../src/lineup.cpp
"${CXX[@]}" -o "$OUT/ce_rating_solve_test" rating_solve_test.cpp ../src/rating_solve.cpp
"${CXX[@]}" -o "$OUT/ce_calendar_rules_test" calendar_rules_test.cpp ../src/calendar_rules.cpp
CE_TEST_DIR="$OUT" "$OUT/ce_bridge_harness"
"$OUT/ce_weather_plan_test"
"$OUT/ce_lineup_test"
"$OUT/ce_rating_solve_test"
"$OUT/ce_calendar_rules_test"
