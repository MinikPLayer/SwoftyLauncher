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

"${ZIG[@]}" c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness \
    -I"$GAME" -I$SDK -D__USE_MINGW_ANSI_STDIO=1 -shared -s -o any_team.dll src/any_team.cpp

rm -f ./*.lib ./*.pdb
echo "built $(pwd)/any_team.dll"
