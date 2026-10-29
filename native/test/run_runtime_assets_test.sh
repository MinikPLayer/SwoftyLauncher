#!/usr/bin/env bash
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"
if ! command -v wine >/dev/null || ! command -v xvfb-run >/dev/null || ! command -v zig >/dev/null; then
    exec nix shell nixpkgs#zig nixpkgs#wineWow64Packages.stable nixpkgs#xvfb-run -c "$SELF" "$@"
fi
. ./game_lock.sh
DIST=${DIST:-dist}
OUT=${1:-$PWD/../build/wine-runtime-assets}
export ZIG_GLOBAL_CACHE_DIR=$PWD/../build/zig-cache ZIG_LOCAL_CACHE_DIR=$PWD/../build/zig-cache
PIN=(taskset -c 2-7,10,11,14-31 nice -n 10)
rm -rf "$OUT/game"
mkdir -p "$OUT/game"

echo "==== dbread logic unit test"
"${PIN[@]}" zig c++ -target x86_64-linux-musl -std=c++23 -O2 -Wall -Wextra -I../../packages/sdk/include -o "$OUT/dbread_unit" dbread_unit.cpp
"$OUT/dbread_unit"

ZS=../third_party/zstd
mkdir -p "$OUT/zstd"
ZOBJ=()
for c in $ZS/common/*.c $ZS/decompress/*.c; do
    o=$OUT/zstd/$(basename "${c%.c}").o
    ZOBJ+=("$o")
    [[ $o -nt $c ]] || "${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -DZSTD_DISABLE_ASM -DXXH_NAMESPACE=ZSTD_ -DZSTD_LEGACY_SUPPORT=0 -I$ZS -c "$c" -o "$o" &
done
wait
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -Wno-unused-function -I$ZS -o "$OUT/game/F1_25.exe" fake_runtime.c "${ZOBJ[@]}" -lversion -luser32
cat > "$OUT/slow_mod.c" <<'EOC'
#include <windows.h>
__declspec(dllexport) int SwoftyAddonInit(const void *api)
{
    (void)api;
    Sleep(1500);
    SetEnvironmentVariableA("SWOFTY_SLOW_DONE", "1");
    return 1;
}
EOC
"${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -shared -o "$OUT/t_slow.dll" "$OUT/slow_mod.c"
cat > "$OUT/plain_mod.c" <<'EOC'
__declspec(dllexport) int SwoftyAddonInit(const void *api)
{
    (void)api;
    return 1;
}
EOC
for n in t_early t_window; do
    "${PIN[@]}" zig cc -target x86_64-windows-gnu -O2 -shared -o "$OUT/$n.dll" "$OUT/plain_mod.c"
done
cat > "$OUT/t_files.cpp" <<'EOC'
#include <swofty/registry.hpp>
#include <swofty/systems/sounds.hpp>

using namespace swofty;

SWOFTY_REGISTER
{
#ifdef SECOND
    sounds::engine_sounds.add("t_v10", {.bundle = file("sounds/other/t_v10.bdl"), .settings_from = sounds::engine_sounds["game/ferrari_2025"]});
#else
    for (std::string k : {"t_v10", "t_kept", "t_missing"})
        sounds::engine_sounds.add(k, {.bundle = file("sounds/" + k + ".bdl"), .settings_from = sounds::engine_sounds["game/ferrari_2025"]});
#endif
}

SWOFTY_ADDON()
EOC
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -I../../packages/sdk/include -shared -s -o "$OUT/t_files.dll" "$OUT/t_files.cpp" &
"${PIN[@]}" zig c++ -target x86_64-windows-gnu -std=c++23 -O2 -DSECOND -I../../packages/sdk/include -shared -s -o "$OUT/t_files2.dll" "$OUT/t_files.cpp" &
wait
cp -r "../$DIST/." "$OUT/game/"
python3 - "$OUT/game/swofty/addons/core/bindings.json" <<'EOB'
import json, sys
doc = json.load(open(sys.argv[1]))
doc['bindings'] = [b for b in doc['bindings'] if b['id'] != 'k_Gdb_StringXorKey'] + [
    {'id': 'k_Gdb_StringXorKey', 'kind': 'data', 'status': 'experimental', 'section': '.xtls',
     'pattern': '48 B8 53 57 46 54 58 4B 45 59 4C 8D 25 ?? ?? ?? ??', 'anchor': 13,
     'resolve': {'kind': 'riprel', 'insn': 10, 'len': 7}}]
json.dump(doc, open(sys.argv[1], 'w'), indent=1)
EOB
python3 - "$OUT/game" <<'EOF'
import os, struct, sys
sys.path.insert(0, '../../tools')
import rtassets
g = sys.argv[1]
def mk(p):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p
logo = 'eaid://ui_package/art/textures/press_start/ps_logo.tif.image'
white = 'eaid://ui_package/art/textures/small_white.tif.image'
hdr = struct.pack('<10I', 2, 264, 70, 8, 4, 1, 1, 1, 0, 0)
stock_logo = dict(uri=logo, type='GfxSurfaceRes', arg=1, frags=[dict(data=hdr), dict(data=b'STOCKPIX' + bytes(4096))])
stock_white = dict(uri=white, type='GfxSurfaceRes', arg=1, frags=[dict(data=hdr), dict(data=b'WHITEPIX' + bytes(8))])
t = lambda n: dict(uri='eaid://ui_package/t/%s.tif.image' % n, type='GfxSurfaceRes', arg=1, frags=[dict(data=hdr), dict(data=b'T' * 16)])
ui = os.path.join(g, '2025_asset_groups', 'ui_package')
rtassets.erp_write(mk(os.path.join(ui, 'flow_boot.erp')), [stock_logo, stock_white, t('size'), t('count'), t('fourcc')])
rtassets.erp_write(mk(os.path.join(g, '2025_asset_groups', 'swofty', 'rt_test.erp')), [dict(uri='eaid://swofty/rt_test/thing.bin', type='ACStringTable', arg=1, frags=[dict(data=b'rt_test!')])])
rtassets.erp_write(mk(os.path.join(g, '2025_asset_groups', 'swofty', 'aliased.erp')), [dict(uri='eaid://swofty/aliased/thing.bin', type='ACStringTable', arg=1, frags=[dict(data=b'aliased!')])])
rtassets.erp_write(mk(os.path.join(ui, 'screens', 'splash.erp')), [stock_logo])
pre = lambda n, d: dict(uri='eaid://ui_package/prebuilt/%s.bin' % n, type='ACStringTable', arg=1, frags=[dict(data=d)])
rtassets.erp_write(mk(os.path.join(ui, 'prebuilt.erp')), [pre('stock', b'STOCKPRE'), pre('other', b'OTHERPRE')])
rtassets.erp_write(mk(os.path.join(g, 'swofty', 'assets', 'resources', '0.erp')), [pre('stock', b'PREBUILT'), pre('new', b'PREADDED')])
rtassets.erp_write(mk(os.path.join(ui, 'other.erp')), [dict(uri='eaid://ui_package/other.bin', type='ACStringTable', arg=1, frags=[dict(data=b'other')])])
open(os.path.join(g, 'archive_language_eng.lng'), 'wb').write(rtassets.lngt_build([('lng_press_start', 'PRESS ANY BUTTON'), ('lng_mode_career', 'Career'), ('lng_other', 'Other')], 7))
sw = os.path.join(g, 'swofty')
open(mk(os.path.join(sw, 'res', 'ps_logo.0.temp')), 'wb').write(hdr)
open(os.path.join(sw, 'res', 'ps_logo.1.temp'), 'wb').write(b'SWOFTYPX' + bytes(4096))
open(os.path.join(sw, 'res', 'ps_logo_old.1.temp'), 'wb').write(b'OLDPIXEL' + bytes(4096))
open(os.path.join(sw, 'res', 'w16.temp'), 'wb').write(b'MODWHITE' + bytes(8))
open(os.path.join(sw, 'res', 'w32.temp'), 'wb').write(b'MODSIZE!' + bytes(24))
open(os.path.join(sw, 'res', 'added.0.temp'), 'wb').write(hdr)
open(os.path.join(sw, 'res', 'added.1.temp'), 'wb').write(b'ADDEDPIX' + bytes(8))
open(mk(os.path.join(sw, 'overlay', 'system', 'config.xml')), 'w').write('<swofty overlay config/>')
open(mk(os.path.join(sw, 'lang', 'language_eng.lng')), 'wb').write(rtassets.lngt_build([('lng_press_start', 'REPLACED PRESS'), ('lng_mode_career', 'REPLACED CAREER')]))
open(os.path.join(sw, 'lang', 'good.lngt'), 'wb').write(rtassets.lngt_build([('lng_table_only', 'FROM TABLE')]))
bad = bytearray(rtassets.lngt_build([('lng_a', 'A'), ('lng_b', 'B')]))
i = bad.index(b'HSHT') + 8
bad[i:i + 8] = struct.pack('>II', 1, 9)
open(os.path.join(sw, 'lang', 'bad.lngt'), 'wb').write(bytes(bad))
EOF
assets_json() {
    python3 - "$OUT/game/swofty/assets.json" "$1" <<'EOF'
import json, sys
mode = sys.argv[2]
logo = 'eaid://ui_package/art/textures/press_start/ps_logo.tif.image'
full = {
    'early_stage': mode == 'early',
    'features': ['group', 'file'],
    'resources_trace': True,
    'asset_groups': [
        {'group': 'asset_groups/swofty/rt_test.assetgroup', 'erp': '2025_asset_groups/swofty/rt_test.erp'},
        {'group': 'asset_groups/swofty/aliased.assetgroup', 'erp': '2025_asset_groups/swofty/aliased.erp'},
    ],
    'resources': [
        {'uri': logo, 'fragments': ['res/ps_logo.0.temp', 'res/ps_logo_old.1.temp']},
        {'uri': 'eaid://swofty/runtime_test/added_white.tif.image', 'type': 'GfxSurfaceRes', 'arg': 1,
         'fragments': [{'file': 'res/added.0.temp', 'fourcc': 'temp', 'flags': 16}, {'file': 'res/added.1.temp', 'fourcc': 'temp', 'flags': 16}],
         'erps': ['2025_asset_groups/ui_package/flow_boot.erp']},
        {'uri': 'eaid://ui_package/does/not/exist.image', 'file': 'res/added.1.temp'},
        {'uri': 'eaid://ui_package/art/textures/small_white.tif.image', 'type': 'GfxTextureRes', 'fragments': ['res/ps_logo.0.temp', 'res/w16.temp']},
        {'uri': 'eaid://ui_package/t/size.tif.image', 'fragments': ['res/ps_logo.0.temp', 'res/w32.temp']},
        {'uri': 'eaid://ui_package/t/count.tif.image', 'fragments': ['res/ps_logo.0.temp']},
        {'uri': 'eaid://ui_package/t/fourcc.tif.image', 'fragments': ['res/ps_logo.0.temp', {'file': 'res/w16.temp', 'fourcc': 'pixl'}]},
        {'uri': 'eaid://swofty/runtime_test/bad_type.image', 'type': 'NoSuchType', 'fragments': ['res/added.0.temp', 'res/added.1.temp'],
         'erps': ['2025_asset_groups/ui_package/flow_boot.erp']},
        {'uri': 'eaid://swofty/runtime_test/long_type.image', 'type': 'ThisTypeIsWayTooLong', 'fragments': ['res/added.1.temp'],
         'erps': ['2025_asset_groups/ui_package/flow_boot.erp']},
        {'uri': logo, 'fragments': ['res/ps_logo.0.temp', 'res/ps_logo.1.temp']},
    ],
    'db_edits': [
        {'key': 'ai.general_ai_settings.max_test_time', 'type': 'float', 'value': '301'},
        {'key': 'fake.flag', 'value': 'True'},
        {'key': 'swofty.runtime_test.marker', 'type': 'string', 'value': 'hello from swofty'},
        {'key': 'swofty.runtime_test.answer', 'type': 'int', 'value': 42},
        {'key': 'fake.retype_me', 'type': 'string', 'value': 'five', 'retype': True},
        {'key': 'fake.keep', 'type': 'string', 'value': 'not without retype'},
        {'key': 'swofty.runtime_test.untyped', 'value': '1'},
        {'key': 'swofty.runtime_test.big', 'type': 'int', 'value': 1234567890},
        {'key': 'swofty.runtime_test.toobig', 'type': 'int', 'value': 3000000000},
        {'key': 'swofty.runtime_test.u64', 'type': 't12', 'value': 12345678901234567},
        {'key': 'swofty.runtime_test.u64neg', 'type': 't12', 'value': '-1'},
        {'key': 'swofty.runtime_test.u64bad', 'type': 't12', 'value': '-x'},
        {'key': 'swofty.runtime_test.f', 'type': 'float', 'value': 0.1},
        {'key': 'swofty.runtime_test.badint', 'type': 'int', 'value': 'abc'},
        {'key': 'swofty.runtime_test.colour_empty', 'type': 'colour', 'value': ''},
        {'key': 'swofty.runtime_test.int_empty', 'type': 'int', 'value': ''},
    ],
    'lang': {
        'eng': {'strings': {'lng_press_start': 'SWOFTY STRINGS OK', 'lng_swofty_only': 'only in the mod table', 'lng_number': 5},
                'table': 'lang/good.lngt'},
        'fra': {'strings': {'lng_press_start': 5, 'lng_mode_career': None}},
        'deu': {'table': 'lang/bad.lngt'},
    },
    'db_inspect': [
        {'key': 'fake.keep', 'as': 'int'},
        {'key': 'ai.general_ai_settings.max_test_time', 'as': 'float'},
        {'key': 'fake.flag', 'as': 'bool'},
        {'key': 'fake.name', 'as': 'string'},
        {'key': 'does.not.exist', 'as': 'int'},
    ],
    'db_inspect_tables': [
        {'table': 'fake.items', 'field': 'rank'},
        {'table': 'fake.items', 'field': 'rank', 'label': 'bravo'},
    ],
    'overlay': True,
    'resource_erps': [{'file': 'assets/resources/0.erp', 'erps': ['2025_asset_groups/ui_package/prebuilt.erp']},
                      {'file': 'assets/resources/missing.erp', 'erps': ['2025_asset_groups/ui_package/prebuilt.erp']}],
}
replace = {'lang': {'eng': {'replace': 'lang/language_eng.lng'}}}
if mode in ('early', 'late'):
    doc = full
elif mode in ('earlywait', 'secondcopy'):
    doc = {'early_stage': True}
elif mode == 'gated':
    doc = dict(full, features=[], lang=dict(full['lang'], eng=dict(full['lang']['eng'], **replace['lang']['eng'])))
else:
    doc = dict(replace, features=['file'])
json.dump(doc, open(sys.argv[1], 'w'), indent=1)
EOF
}
export WINEPREFIX="$OUT/prefix" WINEDLLOVERRIDES="version=n,b;mscoree,mshtml=;winemenubuilder.exe,winedbg.exe=d" WINEDEBUG=-all
status=0
LOG=$OUT/game/swofty/loader.log
check() {
    if grep -qE "$2" "$LOG"; then echo "ok  : $1"; else echo "FAIL: $1"; status=1; fi
}
absent() {
    if grep -qE "$2" "$LOG"; then echo "FAIL: $1"; status=1; else echo "ok  : $1"; fi
}
files_setup() {
    cp "$OUT/t_files.dll" "$OUT/t_files2.dll" "$OUT/game/swofty/addons/"
    python3 - "$OUT/game" <<'EOF'
import json, os, sys
sys.path.insert(0, '../../tools')
import gamedb
g = sys.argv[1]
def put(rel, data):
    p = os.path.join(g, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(data)
put('swofty/addons/t.files/sounds/t_v10.bdl', b'T.FILES V10')
put('swofty/addons/t.files2/sounds/other/t_v10.bdl', b'T.FILES2 V10')
put('audio/2025/bundles/t_kept.bdl', b'GAME KEPT')
put('audio/2025/bundles/ferrari_2025.bdl', b'STOCK FERRARI')
row = 'audio.audio_bundle_settings.audio_bundle_settings_0.'
recs = [('audio.audio_bundle_settings._audio_bundle_settings_instancecount_', 0, '1'), (row + '_metadata_', 2, 'ferrari_2025'),
        (row + 'bundle_name', 2, 'ferrari_2025'), (row + 'era', 3, 'modern'), (row + 'engine', 5, 'True'),
        (row + 'exhaust', 5, 'True'), (row + 'cabin', 5, 'True')]
put('game_package/gamedatabases/gamedatabases.2025.bin',
    gamedb.write([gamedb.Record(gamedb.fnv1a64(k), t, v.encode()) for k, t, v in recs]))
json.dump({'schema': 1, 'profile': 't', 'addons': [{'id': 't.files', 'dlls': ['t_files.dll'], 'options': []},
                                                   {'id': 't.files2', 'dlls': ['t_files2.dll'], 'options': []}]},
          open(os.path.join(g, 'swofty', 'settings.json'), 'w'))
os.makedirs(os.path.join(g, 'swofty', 'registry'), exist_ok=True)
json.dump({'version': 1, 'profile': 't', 'order': ['t.files', 't.files2']}, open(os.path.join(g, 'swofty', 'registry', 'index.json'), 'w'))
EOF
}
files_cleanup() {
    rm -rf "$OUT/game/audio" "$OUT/game/game_package/gamedatabases" "$OUT/game/swofty/registry"
}
for mode in none replace gated late early earlywait secondcopy files; do
    echo "==== $mode"
    rm -rf "$OUT/game/swofty/assets.json" "$OUT/game/swofty/settings.json" "$OUT/game/swofty/addons/"t_*.dll "$OUT/game/swofty/addons/"t.*
    rm -rf "$OUT/game/swofty/cache" "$OUT/game/swofty/"crash-*
    [[ $mode == earlywait ]] && cp "$OUT/t_slow.dll" "$OUT/game/swofty/addons/t_slow.dll"
    if [[ $mode == early ]]; then
        for n in early window; do
            mkdir -p "$OUT/game/swofty/addons/t.$n/native"
            cp "$OUT/t_$n.dll" "$OUT/game/swofty/addons/t.$n/native/"
        done
        echo '{ "schema": 1, "profile": "t", "addons": [], "early_plugins": ["t_early.dll"],
  "plugins": ["addons/t.window/native/t_window.dll", "addons/t.early/native/t_early.dll"] }' >"$OUT/game/swofty/settings.json"
    fi
    [[ $mode == none || $mode == files ]] || assets_json $mode
    [[ $mode == files ]] && files_setup
    timeout 120 "${PIN[@]}" xvfb-run -a wine "$OUT/game/F1_25.exe" $mode || status=1
    echo "---- $LOG ($mode)"
    grep -v '  section ' "$LOG" | grep -vE '\] scan \.xtls|\[core\] bind ' || true
    case $mode in
    files)
        check "files: SwoftyAddonRegister turns the early stage on" 'early stage: on for 2 addon dll\(s\) that export SwoftyAddonRegister'
        check "files: the earlier addon's bundle is redirected first" '\[registry\] file: redirect audio/2025/bundles/t_v10\.bdl -> t\.files/sounds/t_v10\.bdl \(engine_sound t\.files/t_v10\)'
        check "files: the later addon's bundle takes the path" '\[registry\] file: redirect audio/2025/bundles/t_v10\.bdl -> t\.files2/sounds/other/t_v10\.bdl \(engine_sound t\.files2/t_v10\)'
        check "files: two addons on one path log a conflict" '\[registry\] file conflict: audio/2025/bundles/t_v10\.bdl is redirected by t\.files, t\.files2; t\.files2 wins'
        check "files: a bundle in the game folder is kept" "\[apply\] engine_sound boot: +t\.files: 1 bundle\(s\) not in swofty/addons/t\.files/; the game folder's copies are used"
        check "files: a bundle in neither folder is refused" '\[registry\] file: engine_sound t\.files/t_missing: swofty/addons/t\.files/sounds/t_missing\.bdl is not installed; audio/2025/bundles/t_missing\.bdl is not redirected'
        check "files: the route is installed without the experimental file feature" '\[assets\] overlay: route audio/2025/bundles/t_v10\.bdl -> /game/swofty/addons/t\.files2/sounds/other/t_v10\.bdl'
        check "files: the boot line counts the redirect by the addon that wins" '\[registry\] applied at boot: engine_sound 4 \(1 bundle path redirected to t\.files2; [0-9]+ database records\)'
        check "files: the game's open goes through the route" '\[assets\] overlay: audio/2025/bundles/t_v10\.bdl -> /game/swofty/addons/t\.files2/sounds/other/t_v10\.bdl \(engine_sound t\.files2/t_v10; resolve #1'
        absent "files: no overlay feature gate message" 'overlay skipped: the runtime file feature is experimental'
        files_cleanup
        ;;
    early)
        check "loader: early stage on from assets.json" 'early stage: on \("early_stage": true in swofty.assets\.json\)'
        check "loader: CreateMutexW slot armed" 'trigger armed: game IAT slot KERNEL32!CreateMutexW at .*, early stage'
        check "loader: another mutex name is skipped" "early trigger skipped: KERNEL32!CreateMutexW from .* name 'Global.SomethingElse'"
        check "loader: early trigger fired from plain code" "trigger: KERNEL32!CreateMutexW from .* \(\.xtls\) name 'Codemasters F1 25' on thread .*loading addons now \(early stage\)"
        check "loader: early slot disarmed" 'trigger disarmed: KERNEL32!CreateMutexW slot restored'
        check "loader: window stage after the early load" 'stage: window \(USER32!RegisterClassExW .*\), addons were loaded at the early stage'
        absent "loader: the game's own mutex call is not a second copy" 'single instance:'
        absent "crash: the exit after the window stage logs no early-exit stack" "before the game's window"
        check "loader: early plugin list read from settings.json" 'early stage: plugin t_early\.dll has "early": true'
        check "loader: a plugin without \"early\" waits for the window" 'addon t_window\.dll: loads at the window stage'
        check "loader: plugin paths read from settings.json" 'addons: settings\.json lists 2 plugin\(s\) in addon folders'
        check "loader: the early plugin loads from its addon folder" 'addon t_early\.dll: loading .*[\]swofty[\]addons[\]t\.early[\]native[\]t_early\.dll'
        check "loader: the early plugin loads at the early stage" 'addon t_early\.dll: loaded at .*, API v1 plugin'
        check "loader: the rest load at the window stage" 'addons: loading the addons without "early": true at the window stage'
        check "loader: the window plugin loads then" 'addon t_window\.dll: loaded at .*, API v1 plugin'
        check "loader: the window plugin loads from its addon folder" 'addon t_window\.dll: loading .*[\]swofty[\]addons[\]t\.window[\]native[\]t_window\.dll'
        check "core: second mixin commit for the window plugins" '\[core\] commit: 0 of 0 mixin addon\(s\) active'
        check "core: stage handlers ran" '\[assets\] stage window: [1-9] handler\(s\); addons were loaded at the early stage'
        check "gamedb: load wrap installed early" '\[assets\] gamedb: Gdb_Load_GameDatabase hooked at base\+0x[0-9a-f]+ \(g_Gdb_Database = 0+, not built yet\)'
        check "gamedb: edits applied inside the load" '\[assets\] gamedb: database at .* \(right after Gdb_Load_GameDatabase returned\) on thread .*loaded=1'
        check "gamedb: float overwritten" '\[assets\] gamedb: overwrote ai\.general_ai_settings\.max_test_time .*tag 1 float\): 300 -> 301'
        check "gamedb: bool inferred and overwritten" '\[assets\] gamedb: overwrote fake\.flag .*tag 5 bool\): false -> true'
        check "gamedb: string created" '\[assets\] gamedb: created swofty\.runtime_test\.marker .*tag 2 string\): string, label fnv32 0x[0-9a-f]+[[:space:]]*$'
        check "gamedb: int created" '\[assets\] gamedb: created swofty\.runtime_test\.answer .*tag 0 int\): 42'
        check "gamedb: retype only with \"retype\": true" '\[assets\] gamedb: retyped fake\.retype_me .*tag 2 string\): 5 -> string'
        check "gamedb: class mismatch without retype refused" '\[assets\] gamedb: fake\.keep skipped: the existing record is int .*the edit says string; set "retype": true'
        check "gamedb: new key without type skipped" '\[assets\] gamedb: swofty\.runtime_test\.untyped skipped: a new key needs "type"'
        check "gamedb: 10-digit int exact" '\[assets\] gamedb: created swofty\.runtime_test\.big .*tag 0 int\): 1234567890'
        check "gamedb: 17-digit t12 exact" '\[assets\] gamedb: created swofty\.runtime_test\.u64 .*tag 12 t12\): 12345678901234567'
        check "gamedb: negative t12 wraps like strtoull" '\[assets\] gamedb: created swofty\.runtime_test\.u64neg .*tag 12 t12\): 18446744073709551615'
        check "gamedb: non-integer t12 refused" "\[assets\] gamedb: edit swofty\.runtime_test\.u64bad ignored: a t12 value needs integer text"
        check "gamedb: float text exact" '\[assets\] gamedb: created swofty\.runtime_test\.f .*tag 1 float\): 0\.1'
        check "gamedb: int out of range refused" "\[assets\] gamedb: edit swofty\.runtime_test\.toobig ignored: '3000000000' is outside the 32-bit int range"
        check "gamedb: non-integer int refused" "\[assets\] gamedb: edit swofty\.runtime_test\.badint ignored: an int needs integer text"
        check "gamedb: empty colour text created as 0 like a stock colour" '\[assets\] gamedb: created swofty\.runtime_test\.colour_empty .*tag 11 colour\): 0'
        check "gamedb: empty int text created as 0" '\[assets\] gamedb: created swofty\.runtime_test\.int_empty .*tag 0 int\): 0'
        check "gamedb: summary" '\[assets\] gamedb: 2 overwritten, 8 created, 1 retyped, 2 skipped'
        check "gamedb: window-stage read-back through Gdb_Lookup_Key" '\[assets\] gamedb: window stage: Gdb_Lookup_Key\("ai\.general_ai_settings\.max_test_time"\) -> 301'
        check "dbread: window stage sees the loaded database" '\[dbread\] window stage: database ready; 5 key\(s\), 2 table\(s\) to inspect'
        check "dbread: int_at reads an int record" '\[dbread\] inspect fake\.keep: int_at ok 7'
        check "dbread: get tags an int record" '\[dbread\] inspect fake\.keep as int: get ok tag int i=7 '
        check "dbread: float_at reads a float record (post-edit)" '\[dbread\] inspect ai\.general_ai_settings\.max_test_time: float_at ok 301'
        check "dbread: bool_at reads a bool record (post-edit)" '\[dbread\] inspect fake\.flag: bool_at ok true'
        if grep -q 'string reads unavailable: this core was built without the GdbStringRecord layout' "$LOG"; then
            TEXT_AT='unavailable "\(null\)"' LEWIS='\(null\)' ALPHA='\(null\)' BRAVO='\(null\)' CHARLIE='\(null\)'
        else
            TEXT_AT='ok "Lewis"' LEWIS=Lewis ALPHA=alpha BRAVO=bravo CHARLIE=charlie
            check "dbread: string records decode with k_Gdb_StringXorKey" 'bind k_Gdb_StringXorKey -> '
        fi
        check "dbread: string_at reads a string record" "\[dbread\] inspect fake\.name: string_at $TEXT_AT"
        check "dbread: get tags a string record with its fnv32 label" "\[dbread\] inspect fake\.name as string: get ok tag string s=\"$LEWIS\" meta=0x[0-9a-f]+"
        check "dbread: a missing key is invalid" '\[dbread\] inspect does\.not\.exist as int: get invalid'
        check "dbread: missing key typed accessor is invalid" '\[dbread\] inspect does\.not\.exist: int_at invalid'
        check "dbread: open finds the row count" '\[dbread\] table fake\.items field rank: open ok, 3 row\(s\)'
        check "dbread: iterate row 0 name and field" "\[dbread\] table fake\.items\[0\] name=\"$ALPHA\" rank ok tag int i=1"
        check "dbread: iterate row 1 name and field" "\[dbread\] table fake\.items\[1\] name=\"$BRAVO\" rank ok tag int i=2"
        check "dbread: iterate row 2 name and field" "\[dbread\] table fake\.items\[2\] name=\"$CHARLIE\" rank ok tag int i=3"
        check "dbread: row-by-label resolves the index" "\[dbread\] table fake\.items row \"bravo\": ok index 1 name=\"$BRAVO\" rank i=2"
        ;;
    late)
        check "loader: early stage off" 'early stage: off \("early_stage": false in swofty.assets\.json\)'
        absent "early off: CreateMutexW slot not armed" 'KERNEL32!CreateMutexW'
        absent "early off: no stage handler call at the late trigger" 'stage: window|\[assets\] stage window|handler\(s\)'
        absent "early off: no wait at the late trigger" 'waited [0-9]+ ms for the addon load'
        check "early off: late trigger loads addons as before" "trigger: USER32!RegisterClassExW from .* class 'EGO_WindowClass' on thread .*loading addons now[[:space:]]*$"
        check "gamedb: early stage off is explained and the load wrap is not installed" '\[assets\] gamedb: early_stage is off, so addons loaded at the window trigger\. The game database is built before that \(Boot_Init_NeApplication 0x1443c0560'
        absent "gamedb: no load wrap without the early stage" 'Gdb_Load_GameDatabase hooked'
        check "gamedb: late fallback refuses a foreign exe" '\[assets\] gamedb: the database was built before addons loaded and this exe is not the analysed build'
        ;;
    earlywait)
        check "earlywait: the watcher thread loaded addons" 'trigger: main window visible \(fallback, IAT trigger never fired\)'
        check "earlywait: CreateMutexW waited for the load on the other thread" 'early trigger: waited [0-9]+ ms for the addon load running on another thread \(done\)'
        check "earlywait: the slow mod finished inside the wait" 'addon t_slow\.dll: SwoftyAddonInit returned 1'
        ;;
    secondcopy)
        check "secondcopy: the loader says another copy holds the mutex" "single instance: the game's mutex 'Codemasters F1 25' already exists, so another copy of F1 25 is running"
        check "secondcopy: the clean exit before the window logs its caller stack" '\[crash\] ExitProcess\(0\) on thread [0-9]+, [0-9]+ s after load, before the game.s window; caller stack:'
        check "secondcopy: the stack names the exe frame" '\[crash\]   #0 .*F1_25\.exe\+0x'
        absent "secondcopy: no crash report for a clean exit" '\[crash\] crash report '
        if compgen -G "$OUT/game/swofty/crash-*.txt" >/dev/null; then
            echo "FAIL: secondcopy: a crash-*.txt was written"
            status=1
        else
            echo "ok  : secondcopy: no crash-*.txt"
        fi
        ;;
    replace)
        check "lang: whole-file replacement registered" "\[assets\] lang: 'eng' language file -> /game/swofty/lang/language_eng\.lng"
        check "overlay: language path redirected (archive probe)" '\[assets\] overlay: localisation/2025_release/language_eng\.lng -> /game/swofty/lang/language_eng\.lng \(lang replace .*resolve #1 .*probe=1'
        check "overlay: language path redirected (loose open)" '\[assets\] overlay: localisation/2025_release/language_eng\.lng -> /game/swofty/lang/language_eng\.lng \(lang replace .*resolve #2 .*probe=0, loose files'
        ;;
    none)
        absent "no runtime-assets logs without assets.json" '\[assets\]'
        absent "no early stage without assets.json" 'KERNEL32!CreateMutexW|stage: window'
        check "loader: early stage off without assets.json" 'early stage: off \(no "early_stage" in swofty.assets\.json\)'
        ;;
    gated)
        check "gated: the file overlay needs the experimental file feature" '\[assets\] overlay skipped: the runtime file feature is experimental'
        check "gated: a whole language file needs the experimental file feature" '\[assets\] lang replace lang/language_eng\.lng skipped: the runtime file feature is experimental'
        check "gated: stable asset groups still apply" '\[assets\] registered asset_groups/swofty/rt_test\.assetgroup -> 2025_asset_groups/swofty/rt_test\.erp'
        absent "gated: no fs overlay redirect" '\[assets\] overlay: (system/config\.xml|localisation/)'
        check "gated: stable resources still apply" '\[assets\] resources: 3 overlay ERP\(s\) \(2 written, 0 unchanged\)'
        check "gated: stable string tables still apply" "\[assets\] lang: table for 'eng': 2 string\(s\) from assets\.json strings"
        ;;
    esac
    if [[ $mode == early || $mode == late ]]; then
        check "resources: duplicate uri, later declaration wins" '\[assets\] resources: eaid://ui_package/art/textures/press_start/ps_logo\.tif\.image is declared again: entry 9 replaces entry 0 \(the later declaration wins\)'
        check "resources: scan found both stock ERPs, zstd used" "\[assets\] resources: replace eaid://ui_package/art/textures/press_start/ps_logo\.tif\.image \(GfxSurfaceRes, arg 1, 2 fragment\(s\), 4144 bytes, [0-9]+ stored zstd\), metadata from 2025_asset_groups/ui_package/(flow_boot|screens/splash)\.erp; stock ERP\(s\): 2025_asset_groups/ui_package/flow_boot\.erp, 2025_asset_groups/ui_package/screens/splash\.erp"
        check "resources: addition accepted (layout seen in stock)" '\[assets\] resources: add eaid://swofty/runtime_test/added_white\.tif\.image \(GfxSurfaceRes, arg 1, 2 fragment\(s\), 56 bytes, 56 stored\); stock ERP\(s\): 2025_asset_groups/ui_package/flow_boot\.erp'
        check "resources: unknown uri without type skipped" '\[assets\] resources: skipped eaid://ui_package/does/not/exist\.image: not found in any scanned stock ERP'
        check "resources: replacement with another type refused" '\[assets\] resources: skipped eaid://ui_package/art/textures/small_white\.tif\.image: "type" GfxTextureRes differs from the stock type GfxSurfaceRes'
        check "resources: fragment size mismatch refused" '\[assets\] resources: skipped eaid://ui_package/t/size\.tif\.image: fragment 1 \(res/w32\.temp\) is 32 bytes, the stock one 16'
        check "resources: fragment count mismatch refused" '\[assets\] resources: skipped eaid://ui_package/t/count\.tif\.image: the stock entry in .* has 2 fragment\(s\), the mod gives 1'
        check "resources: fourcc mismatch refused" "\[assets\] resources: skipped eaid://ui_package/t/fourcc\.tif\.image: fragment 1 fourcc 'pixl' differs from the stock 'temp'"
        check "resources: unknown type refused" '\[assets\] resources: skipped eaid://swofty/runtime_test/bad_type\.image: type NoSuchType is not used by any resource in the scanned stock ERPs'
        check "resources: over-long type refused" '\[assets\] resources: skipped eaid://swofty/runtime_test/long_type\.image: "type" must be 1 to 15 characters'
        check "resources: per-target overlays" '\[assets\] resources: 3 overlay ERP\(s\) \(2 written, 0 unchanged\) for 2 target ERP\(s\); 2 replaced, 2 added'
        check "resources: prebuilt overlay ERP loaded" '\[assets\] resources: overlay ERP swofty/assets/resources/0\.erp prebuilt by the launcher \([0-9]+ bytes, 2 resource\(s\): 1 replaced, 1 added\) for 1 target ERP\(s\): 2025_asset_groups/ui_package/prebuilt\.erp'
        check "resources: missing prebuilt overlay ERP refused loudly" '\[assets\] resources: prebuilt overlay ERP assets/resources/missing\.erp NOT loaded: the file is missing or not an ERP; its 1 target ERP\(s\) load stock data'
        check "resources: prebuilt summary" '\[assets\] resources: 1 of 2 prebuilt overlay ERP\(s\) loaded in [0-9]+ ms'
        check "resources: prebuilt group gets its overlay first" '\[assets\] resources: group 2025_asset_groups/ui_package/prebuilt\.assetgroup \(0x[0-9a-f]+\) now reads swofty/assets/resources/0\.erp first'
        check "resources: flow_boot overlay carries both uris" '\[assets\] resources: overlay ERP swofty/cache/rt_[0-9a-f]{16}\.erp written \([0-9]+ bytes, 2 resource\(s\): .*\) for 1 target ERP\(s\): 2025_asset_groups/ui_package/flow_boot\.erp'
        check "resources: splash overlay carries only ps_logo" '\[assets\] resources: overlay ERP swofty/cache/rt_[0-9a-f]{16}\.erp written \([0-9]+ bytes, 1 resource\(s\): eaid://ui_package/art/textures/press_start/ps_logo\.tif\.image\) for 1 target ERP\(s\): 2025_asset_groups/ui_package/screens/splash\.erp'
        check "resources: no lock held while waiting for the build" '\[assets\] resources: (waited [0-9]+ ms for the overlay build \(no lock held while waiting\)|[0-9]+ of 3 overlay ERP)'
        check "resources: flow_boot group gets its overlay first" '\[assets\] resources: group 2025_asset_groups/ui_package/flow_boot\.assetgroup \(0x[0-9a-f]+\) now reads swofty/cache/rt_[0-9a-f]{16}\.erp first, then its 1 stock ERP\(s\)'
        check "resources: splash group gets its overlay first" '\[assets\] resources: group 2025_asset_groups/ui_package/screens/splash\.assetgroup \(0x[0-9a-f]+\) now reads swofty/cache/rt_[0-9a-f]{16}\.erp first'
        check "resources: both overlays registered" "\[assets\] resources: overlay swofty/cache/rt_[0-9a-f]{16}\.erp registered as erp 0x[0-9a-f]+ \(name read back 'swofty/cache/rt_"
        check "resources: flag-4 group hooks installed" '\[assets\] resources: Asset_Expand_GroupErps and Asset_Release_GroupErps hooked at base\+0x[0-9a-f]+ and base\+0x[0-9a-f]+'
        check "resources: flag-4 group covered" '\[assets\] resources: dynamic group 0x5157f7aa5157f7aa \(handle flag 4, 1 ERP\(s\) in its own list at \+0x18\) lists a target ERP; .* now see 1 overlay ERP\(s\) first'
        check "resources: on-disk size probe" '\[assets\] probe: [1-9][0-9]* on-disk ERP\(s\) checked \(2025_asset_groups/'
        check "asset groups: alias-named ERP refused" '\[assets\] skipped asset_groups/swofty/aliased\.assetgroup .*already has a name in g_Asset_AliasNames'
        check "asset groups: registered group" '\[assets\] registered asset_groups/swofty/rt_test\.assetgroup -> 2025_asset_groups/swofty/rt_test\.erp'
        check "trace: opt-in trace on" '\[assets\] resources: first-reader trace on at base\+0x[0-9a-f]+'
        check "trace: splash overlay created ps_logo" '\[assets\] resource eaid://ui_package/art/textures/press_start/ps_logo\.tif\.image: CREATED from the overlay \(first reader\); erp swofty/cache/rt_[0-9a-f]{16}\.erp .*refs 1 .*md5 match'
        check "trace: splash.erp bound to it" '\[assets\] resource eaid://ui_package/art/textures/press_start/ps_logo\.tif\.image: bound to the existing copy \(the stock bytes are skipped\); erp 2025_asset_groups/ui_package/screens/splash\.erp .*refs 2'
        check "trace: flow_boot.erp bound to it" '\[assets\] resource eaid://ui_package/art/textures/press_start/ps_logo\.tif\.image: bound to the existing copy .*erp 2025_asset_groups/ui_package/flow_boot\.erp .*refs 4'
        check "trace: added uri created" '\[assets\] resource eaid://swofty/runtime_test/added_white\.tif\.image: CREATED from the overlay'
        check "overlay: archive path redirected (probe miss)" '\[assets\] overlay: system/config\.xml -> /game/swofty/overlay/system/config\.xml \(overlay folder; resolve #1 .*probe=1, archive'
        check "overlay: archive path redirected (loose open)" '\[assets\] overlay: system/config\.xml -> /game/swofty/overlay/system/config\.xml \(overlay folder; resolve #2 .*probe=0, loose files'
        check "lang: non-string values skipped" "\[assets\] lang: 'eng': 1 entr\(ies\) in \"strings\" skipped: values must be JSON strings"
        check "lang: an all-non-string table is not built" "\[assets\] lang: 'fra': no usable string in \"strings\"; no table added"
        check "lang: bad LNGT bucket range refused" "\[assets\] lang: table lang/bad\.lngt skipped: not a valid LNGT table \(HSHT bucket 0 covers strings 1\.\.10 of 2\)"
        check "lang: string table built" "\[assets\] lang: table for 'eng': 2 string\(s\) from assets\.json strings"
        check "lang: valid table file accepted" "\[assets\] lang: table for 'eng': 1 string\(s\) from lang/good\.lngt"
        check "lang: table added and searched first" "\[assets\] lang: added 2 string\(s\) from assets\.json strings for 'eng' at priority 0 \(Loc_Add_LangTable returned 0\); lookup \"lng_press_start\" -> \"SWOFTY STRINGS OK\" \(the mod table is searched first\)"
    fi
done
exit $status
