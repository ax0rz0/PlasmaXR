#!/usr/bin/env bash
# Runs a command as root on the headset through the Plasma XR app: Magisk has granted root to the app, and
# `run-as` (debug builds only) lets adb act as the app. The adb shell itself is not allowed to use su.
#
#   ./tools/device/root.sh 'id; cmd role get-role-holders android.app.role.HOME'
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../../scripts/env.sh"
q=$(printf '%s' "$*" | sed "s/'/'\\\\''/g")
"${ADB[@]}" shell "run-as $PKG su -c '$q'"
