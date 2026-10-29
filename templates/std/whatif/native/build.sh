#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
SDK=${SWOFTY_SDK_INCLUDE:-$(readlink -f ../../../../packages/sdk/include)}
[ -d include ] && SDK=include
export ZIG_GLOBAL_CACHE_DIR=$PWD/build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/build/zig-cache
mkdir -p build
zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -Wall -Wextra -Wno-nullability-completeness -I"$SDK" -shared -s \
    -o whatif.dll src/whatif.cpp
rm -f whatif.lib
echo "built $(pwd)/whatif.dll"
