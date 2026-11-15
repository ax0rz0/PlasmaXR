#!/usr/bin/env bash
# Render every panel offscreen on the headset and pull the PNGs into debug-out/snap/.
set -euo pipefail
source "$(dirname "$0")/env.sh"

"$(dirname "$0")/debug.sh" snap >/dev/null
sleep 2
mkdir -p "$ROOT/debug-out/snap"
"${ADB[@]}" pull "/sdcard/Android/data/$PKG/files/snap/." "$ROOT/debug-out/snap/"
echo "snapshots in $ROOT/debug-out/snap"
