#!/usr/bin/env bash
# Send a command to a running debug build. See docs/DEBUGGING.md for the command list.
#   ./scripts/debug.sh sim taskbar 0.5 0.5 1
set -euo pipefail
source "$(dirname "$0")/env.sh"

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <command> [args...]   (see docs/DEBUGGING.md)" >&2
    exit 2
fi
# The command line is re-parsed by the shell on the headset, so keep it as one quoted string.
"${ADB[@]}" shell am broadcast -n "$PKG/.DebugReceiver" --es cmd "'$*'"
