#!/usr/bin/env bash
# Follow the Plasma XR log (plus Java crashes and native fatal signals). Extra args go to logcat.
set -euo pipefail
source "$(dirname "$0")/env.sh"

exec "${ADB[@]}" logcat -s PlasmaXR:V AndroidRuntime:E DEBUG:F "$@"
