#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"

if ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig -c "$SELF" "$@"
fi
export ZIG_GLOBAL_CACHE_DIR=$PWD/build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/build/zig-cache

VARIANT=${VARIANT:-}
case $VARIANT in
'') DEFS=() ;;
nomodsettings) DEFS=(-DSWOFTY_NO_MOD_SETTINGS) ;;
hooknoop) DEFS=(-DSWOFTY_MOD_SETTINGS_NOOP) ;;
*)
    echo "build.sh: unknown VARIANT '$VARIANT' (known: nomodsettings, hooknoop)" >&2
    exit 2
    ;;
esac
DIST=dist${VARIANT:+-$VARIANT}
OUT=build${VARIANT:+/$VARIANT}

SDK=../packages/sdk/include
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
CC=(zig cc -target x86_64-windows-gnu)
CXX=(zig c++ -target x86_64-windows-gnu -std=c++23)
SH=third_party/safetyhook
STATIC=(-s)
mkdir -p build "$OUT"
rm -rf "$DIST"
mkdir -p "$DIST/swofty/addons/core"
# bindgen writes bindings.json for the KB build and bindings.<build>.json for every other overlay in
# re/db/builds; core picks the set whose build.pe_timestamp matches the running exe.
python3 ../re/tools/bindgen.py --json "$DIST/swofty/addons/core/bindings.json" --symbols "$DIST/swofty/addons/core/symbols.tsv"
# With a game image at hand, resolve the emitted sets against it the way core does; a binding that fails stops the build.
if [[ -n ${SWOFTY_STEAM_EXE:-} ]]; then
    python3 ../re/tools/bindverify.py --set "$DIST/swofty/addons/core/bindings.json" --image "$SWOFTY_STEAM_EXE"
fi
if [[ -n ${SWOFTY_EA_DUMP:-} ]]; then
    python3 ../re/tools/bindverify.py --set "$DIST/swofty/addons/core/bindings.ea.json" --image "$SWOFTY_EA_DUMP" \
        ${SWOFTY_STEAM_EXE:+--compare "$SWOFTY_STEAM_EXE"}
fi
python3 - src/core/stage.cpp "$DIST/swofty/addons/core/features.json" <<'EOF'
import json, re, sys
table = open(sys.argv[1]).read().split('FEATURES[] = ', 1)[1].split(';', 1)[0]
features = {name: 'stable' if stable == 'true' else 'experimental' for name, stable in re.findall(r'\{"(\w+)", (true|false)\}', table)}
json.dump({'schema': 1, 'features': features}, open(sys.argv[2], 'w'))
EOF

"${PIN[@]}" "${CC[@]}" -O2 -Wall -Wextra -I$SDK -Iinclude -D__USE_MINGW_ANSI_STDIO=1 -shared \
    -o "$OUT/version.dll" src/loader/version.c src/loader/version.def "${STATIC[@]}" &

[[ build/Zydis.o -nt $SH/Zydis.c ]] || "${PIN[@]}" "${CC[@]}" -O2 -I$SH -c $SH/Zydis.c -o build/Zydis.o &
ZS=third_party/zstd
ZOBJ=()
mkdir -p build/zstd
for c in $ZS/common/*.c $ZS/compress/*.c $ZS/decompress/*.c; do
    o=build/zstd/$(basename "${c%.c}").o
    ZOBJ+=("$o")
    [[ $o -nt $c ]] || "${PIN[@]}" "${CC[@]}" -O2 -DXXH_NAMESPACE=ZSTD_ -DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM -DDEBUGLEVEL=0 -I$ZS -c "$c" -o "$o" &
done
[[ build/safetyhook.o -nt $SH/safetyhook.cpp ]] || "${PIN[@]}" "${CXX[@]}" -O2 -I$SH -c $SH/safetyhook.cpp -o build/safetyhook.o &
wait

# GAME_INCLUDE points the core at other generated game headers (bindgen --dev output) ahead of the
# shipped ones in include/. The career library and the gamedb string decode compile out, and report
# unavailable at runtime, when the headers lack the KB structs they read (src/core/layout.hpp).
GAME=()
[[ -n ${GAME_INCLUDE:-} ]] && GAME=(-I"$GAME_INCLUDE")
# The 1.6 handler guard is the one file that uses SEH (__try/__except), which clang only parses with -fms-extensions.
"${PIN[@]}" "${CXX[@]}" -O2 -Wall -Wextra -fms-extensions -I$SDK -c src/core/hooks/guard.cpp -o build/hooks_guard.o
"${PIN[@]}" "${CXX[@]}" -O2 -Wall -Wextra "${DEFS[@]}" -I$SDK "${GAME[@]}" -Iinclude -I$SH -I$ZS -shared -o "$OUT/swofty_core.dll" \
    src/core/manager.cpp src/core/crash.cpp src/core/settings.cpp src/core/swofty_core.cpp src/core/ui.cpp src/core/ui_vm.cpp src/core/items.cpp \
    src/core/stage.cpp src/core/netplay.cpp src/core/netplay_drive.cpp src/core/netplay_bridge.cpp src/core/net_trace.cpp src/core/assets.cpp src/core/resources.cpp src/core/gamedb.cpp src/core/overlay.cpp src/core/events.cpp src/core/hud.cpp src/core/focus.cpp src/core/d3d12_hooks.cpp src/core/grade.cpp src/core/postfx.cpp src/core/telemetry.cpp src/core/dbread.cpp src/core/storage.cpp src/core/career.cpp src/core/career_content.cpp src/core/career_start.cpp src/core/popup.cpp src/core/sdk.cpp src/core/perf.cpp src/core/lang_override.cpp src/core/characters.cpp src/core/career_wizard.cpp src/core/era_picker.cpp src/core/car_category.cpp src/core/save_splice.cpp src/core/ers_newlap.cpp src/core/hub_tab.cpp src/core/registry/schema.cpp src/core/hooks/hooks.cpp src/core/hooks/core_events.cpp src/core/services/services.cpp src/core/career_ops/career_ops.cpp src/core/career_ops/stats.cpp build/hooks_guard.o src/core/registry/store.cpp src/core/registry/table.cpp src/core/registry/validate.cpp src/core/registry/core.cpp src/core/snapshots/snapshots.cpp src/core/snapshots/runtime.cpp src/core/track_acclim.cpp src/core/apply/scheduler.cpp src/core/apply/ids.cpp src/core/apply/moments.cpp src/core/apply/content/appliers.cpp src/core/testagent/agent.cpp src/core/mfd.cpp src/core/apply/files.cpp src/core/save_sandbox.cpp src/core/career_ops/track_fuel.cpp src/core/hooks/game_points.cpp src/core/hooks/session_points.cpp src/core/registry/older.cpp src/core/apply/content/logos.cpp src/core/team_slots.cpp build/safetyhook.o \
    build/Zydis.o "${ZOBJ[@]}" "${STATIC[@]}"

"${PIN[@]}" "${CXX[@]}" -O2 -Wall -Wextra -I$SDK -Iinclude -shared -o "$OUT/swofty_gamelog.dll" src/gamelog/gamelog.cpp "${STATIC[@]}"

cp "$OUT/version.dll" "$DIST/version.dll"
cp "$OUT/swofty_core.dll" "$DIST/swofty/addons/core/swofty_core.dll"
cp "$OUT/swofty_gamelog.dll" "$DIST/swofty/addons/core/swofty_gamelog.dll"
cp src/gamelog/log_channels.tsv "$DIST/swofty/addons/core/log_channels.tsv"

for f in "$DIST/version.dll" "$DIST/swofty/addons/core/swofty_core.dll" "$DIST/swofty/addons/core/swofty_gamelog.dll"; do
    echo "== $f"
    objdump -p "$f" | grep -E '^(Magic|Characteristics)|file format|DLL Name|^\s+\[ *[0-9]+\] \+base' || true
    objdump -f "$f" | grep -E 'architecture'
    md5sum "$f"
done
md5sum "$DIST"/swofty/addons/core/bindings*.json "$DIST/swofty/addons/core/symbols.tsv" "$DIST/swofty/addons/core/log_channels.tsv" \
    "$DIST/swofty/addons/core/features.json"

# Bundled addons ship next to the loader in a release (addons/ beside loader/) and the launcher installs them
# switched off. They are packaged into bundled/, never into dist/, because dist/ is copied into the game folder.
if [[ -z $VARIANT ]]; then
    rm -rf bundled
    mkdir -p bundled
    for addon in ../addons/re-optimization; do
        if [[ ! -f $addon/native/build.sh || ! -f $addon/swofty.json ]]; then
            echo "build.sh: $addon has no native/build.sh or swofty.json; not bundled" >&2
            continue
        fi
        "${PIN[@]}" bash "$addon/native/build.sh"
        python3 ../tools/pack_addon.py "$addon" bundled
    done
    md5sum bundled/*.swoftyaddon 2>/dev/null || true
fi
