#!/usr/bin/env bash
# Capture what the headset is displaying (composited stereo output) into debug-out/.
set -euo pipefail
source "$(dirname "$0")/env.sh"

mkdir -p "$ROOT/debug-out"
out="$ROOT/debug-out/screenshot-$(date +%Y%m%d-%H%M%S).png"
"${ADB[@]}" exec-out screencap -p > "$out"
echo "$out"
