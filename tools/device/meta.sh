#!/usr/bin/env bash
# Reversible control of Meta's system packages on the headset (adb shell privileges, no root).
# Every package this script disables is recorded, and `restore` re-enables exactly those.
#
#   ./tools/device/meta.sh status                 what is disabled, and the free RAM
#   ./tools/device/meta.sh list                   the groups in meta-groups.txt
#   ./tools/device/meta.sh disable optional       disable a group (or: disable com.some.package ...)
#   ./tools/device/meta.sh restore                re-enable everything this script disabled
#   ./tools/device/meta.sh restore optional       ...or one group / package
#
# The record lives in debug-out/meta-disabled.txt (git-ignored: it describes one device).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../../scripts/env.sh"

GROUPS_FILE="$ROOT/tools/device/meta-groups.txt"
STATE="$ROOT/debug-out/meta-disabled.txt"
mkdir -p "$(dirname "$STATE")"
touch "$STATE"

sh_() { "${ADB[@]}" shell "$@" | tr -d '\r'; }

installed() { sh_ "pm list packages $1" | grep -qx "package:$1"; }

group_packages() {  # prints the packages of a group
    awk -F'|' -v g="$1" '/^[a-z]/ { gsub(/ /,"",$1); gsub(/ /,"",$2); if ($1==g) print $2 }' "$GROUPS_FILE"
}

expand() {  # group names and/or package names -> package names
    local a
    for a in "$@"; do
        if grep -qE "^$a[[:space:]]*\|" "$GROUPS_FILE"; then group_packages "$a"; else echo "$a"; fi
    done
}

free_ram() { sh_ "dumpsys meminfo" | grep -E "^ *Free RAM" | head -1 | sed 's/^ *//'; }

cmd="${1:-status}"; shift || true
case "$cmd" in
    status)
        echo "Disabled by this tool:"
        if [ -s "$STATE" ]; then sed 's/^/  /' "$STATE"; else echo "  (nothing)"; fi
        echo "Disabled on the device (all):"
        sh_ "pm list packages -d" | sed 's/package:/  /'
        echo "$(free_ram)"
        ;;
    list)
        awk -F'|' '/^[a-z]/ { gsub(/^ +| +$/,"",$1); gsub(/^ +| +$/,"",$2); gsub(/^ +| +$/,"",$3); printf "%-9s %-44s %s\n",$1,$2,$3 }' "$GROUPS_FILE"
        ;;
    disable)
        [ $# -gt 0 ] || { echo "usage: meta.sh disable <group|package>..." >&2; exit 1; }
        before="$(free_ram)"
        for pkg in $(expand "$@"); do
            if ! installed "$pkg"; then echo "  skip     $pkg (not installed)"; continue; fi
            out="$(sh_ "pm disable-user --user 0 $pkg" 2>&1 || true)"
            if echo "$out" | grep -q "new state: disabled-user"; then
                grep -qx "$pkg" "$STATE" || echo "$pkg" >> "$STATE"
                echo "  disabled $pkg"
            elif echo "$out" | grep -q "new state: disabled"; then
                echo "  already  $pkg"
            else
                echo "  FAILED   $pkg: $out"
            fi
        done
        sleep 2
        echo "RAM before: $before"
        echo "RAM after:  $(free_ram)"
        ;;
    restore)
        targets=()
        if [ $# -gt 0 ]; then targets=($(expand "$@")); else targets=($(cat "$STATE")); fi
        for pkg in "${targets[@]:-}"; do
            [ -n "$pkg" ] || continue
            if sh_ "pm enable $pkg" 2>&1 | grep -q "new state: enabled"; then
                echo "  enabled  $pkg"
                grep -vx "$pkg" "$STATE" > "$STATE.tmp" || true
                mv "$STATE.tmp" "$STATE"
            else
                echo "  could not enable $pkg"
            fi
        done
        echo "$(free_ram)"
        ;;
    *)
        sed -n '2,12p' "$0"
        ;;
esac
