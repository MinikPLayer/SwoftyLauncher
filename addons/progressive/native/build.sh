#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"

if command -v zig >/dev/null; then
    ZIG=(zig)
else
    ZIG=(nix run nixpkgs#zig --)
fi

export ZIG_GLOBAL_CACHE_DIR=$PWD/build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/build/zig-cache
mkdir -p build

REPO=../../..
SDK=$REPO/packages/sdk/include
GAME=${GAME_INCLUDE:-$REPO/native/include}
CXX=("${ZIG[@]}" c++ -target x86_64-windows-gnu -std=c++23)

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive.dll src/plugin.cpp src/report.cpp src/probe.cpp src/season.cpp src/store.cpp src/seats.cpp src/names.cpp src/ratings.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_ui.dll src/ui/tile.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_suffix.dll src/ui/suffix.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_back.dll src/ui/back.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_rnd.dll src/ui/rnd.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_follow.dll src/ui/follow.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_cards.dll src/ui/cards.cpp src/ui/wizard_ratings.cpp src/names.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_logos.dll src/ui/logos.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_numbers.dll src/ui/numbers.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_teamlogos.dll src/ui/teamlogos.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_resume.dll src/ui/resume.cpp

"${CXX[@]}" -O2 -Wall -Wextra -Wno-nullability-completeness -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s \
    -o progressive_fuel.dll src/ui/fuel.cpp

rm -f ./*.lib
echo "built $(pwd)/progressive.dll progressive_ui.dll progressive_suffix.dll progressive_back.dll progressive_rnd.dll progressive_follow.dll progressive_cards.dll progressive_logos.dll progressive_numbers.dll progressive_teamlogos.dll progressive_resume.dll progressive_fuel.dll"
