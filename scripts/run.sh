#!/usr/bin/env bash
# Build the debug APK, install it on the connected headset and launch it.
set -euo pipefail
source "$(dirname "$0")/env.sh"

cd "$ROOT"
./gradlew assembleDebug
"${ADB[@]}" install -r app/build/outputs/apk/debug/app-debug.apk
"${ADB[@]}" shell am start -n "$PKG/.PlasmaActivity"
