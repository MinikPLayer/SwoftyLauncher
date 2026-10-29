#!/usr/bin/env bash
# The repository ships no sound bundle. This puts a stand-in at sounds/v10_screamer.bdl: a copy of a stock
# bundle from your own game folder (the 2025 Mercedes by default), or the bundle you name.
#   sounds/standin.sh                          the stock mercedes_2025.bdl
#   sounds/standin.sh ~/sounds/my_v10.bdl      your own bundle
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
GAME=${GAME_DIR:-$HOME/.steam/steam/steamapps/common/F1 25}
SRC=${1:-$GAME/audio/2025/bundles/mercedes_2025.bdl}
cp "$SRC" v10_screamer.bdl
echo "sounds/v10_screamer.bdl <- $SRC"
