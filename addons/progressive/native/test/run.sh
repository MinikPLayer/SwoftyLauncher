#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"

if command -v zig >/dev/null; then
    ZIG=(zig)
else
    ZIG=(nix run nixpkgs#zig --)
fi

OUT=${PG_TEST_DIR:-${TMPDIR:-/tmp}/progressive-test}
mkdir -p "$OUT"
export ZIG_GLOBAL_CACHE_DIR=$OUT/zig-cache ZIG_LOCAL_CACHE_DIR=$OUT/zig-cache

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/lineup_logic_test" lineup_logic_test.cpp
"$OUT/lineup_logic_test"

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/season_logic_test" season_logic_test.cpp
"$OUT/season_logic_test" ../../data/years.json

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/resume_logic_test" resume_logic_test.cpp
"$OUT/resume_logic_test"

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/cards_logic_test" cards_logic_test.cpp
"$OUT/cards_logic_test"

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/player_logic_test" player_logic_test.cpp
"$OUT/player_logic_test"

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/development_logic_test" development_logic_test.cpp
"$OUT/development_logic_test" ../../data/years.json

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/midseason_logic_test" midseason_logic_test.cpp
"$OUT/midseason_logic_test" ../../data/years.json

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/store_logic_test" store_logic_test.cpp
"$OUT/store_logic_test"

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/meetings_logic_test" meetings_logic_test.cpp
"$OUT/meetings_logic_test"

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/weekend_logic_test" weekend_logic_test.cpp
"$OUT/weekend_logic_test"

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/rnd_model_logic_test" rnd_model_logic_test.cpp
"$OUT/rnd_model_logic_test" ../../data/years.json

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/scene_logic_test" scene_logic_test.cpp
"$OUT/scene_logic_test"

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/seats_logic_test" seats_logic_test.cpp
"$OUT/seats_logic_test" ../../data/years.json

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/titles_logic_test" titles_logic_test.cpp
"$OUT/titles_logic_test" ../../data/titles.json

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/ratings_logic_test" ratings_logic_test.cpp
"$OUT/ratings_logic_test" ../../data/years.json

"${ZIG[@]}" c++ -target x86_64-linux-musl -std=c++23 -O1 -Wall -Wextra -Wno-nullability-completeness -I../src \
    -o "$OUT/fuel_logic_test" fuel_logic_test.cpp
"$OUT/fuel_logic_test"
