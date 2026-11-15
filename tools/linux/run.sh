#!/usr/bin/env bash
# Runs a command line inside the Linux environment on the headset, as the app's user, and prints
# its output. The Plasma XR app does not have to be running (but needs to be installed).
#
#   ./tools/linux/run.sh 'uname -a; ls /usr/bin | head'
#   PROOT_NO_SECCOMP=0 ./tools/linux/run.sh 'foot --version'
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE=/data/local/tmp/plasma-linux
adb push "$HERE/guest.sh" "$STAGE/guest.sh" >/dev/null
adb shell "chmod 755 $STAGE/guest.sh"
# One level of quoting for adb, one for run-as; the command itself goes to /bin/sh -c in the guest.
q=$(printf '%s' "$*" | sed "s/'/'\\\\''/g")
adb shell "run-as org.plasmaxr.launcher env PROOT_NO_SECCOMP=${PROOT_NO_SECCOMP:-1} $STAGE/guest.sh /bin/sh -c '$q'"
