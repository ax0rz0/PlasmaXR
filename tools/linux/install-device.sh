#!/usr/bin/env bash
# Installs the Linux environment into the Plasma XR app on a connected headset:
#   proot (from Termux), its libraries, and the Debian rootfs built by build-rootfs.sh.
#
#   ./tools/linux/fetch-proot.sh      # once: downloads proot into build/linux/proot
#   ./tools/linux/build-rootfs.sh     # once: builds build/linux/rootfs.tar.gz
#   ./tools/linux/install-device.sh   # pushes both and unpacks them as the app's user
#
# The app must be the debug build (run-as only works for debuggable apps). Re-running replaces the
# rootfs, so anything installed inside it is lost; pass --keep-rootfs to only update proot.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PKG=org.plasmaxr.launcher
STAGE=/data/local/tmp/plasma-linux
KEEP=0
[ "${1:-}" = "--keep-rootfs" ] && KEEP=1

[ -d "$ROOT/build/linux/proot" ] || { echo "run tools/linux/fetch-proot.sh first" >&2; exit 1; }
if [ $KEEP -eq 0 ]; then
    [ -f "$ROOT/build/linux/rootfs.tar.gz" ] || { echo "run tools/linux/build-rootfs.sh first" >&2; exit 1; }
fi
adb shell "pm path $PKG" >/dev/null || { echo "$PKG is not installed on the device" >&2; exit 1; }

echo "Pushing files..."
adb shell "mkdir -p $STAGE"
adb push "$ROOT/build/linux/proot/." "$STAGE/" >/dev/null
[ $KEEP -eq 0 ] && adb push "$ROOT/build/linux/rootfs.tar.gz" "$STAGE/rootfs.tar.gz"
adb shell "chmod -R a+rX $STAGE; chmod 755 $STAGE/bin/proot $STAGE/libexec/loader"

echo "Unpacking as the app's user..."
adb shell "run-as $PKG sh -c '
    set -e
    mkdir -p files/linux/run files/linux/tmp files/linux/shm
    rm -rf files/linux/bin files/linux/lib files/linux/libexec
    cp -r $STAGE/bin $STAGE/lib $STAGE/libexec files/linux/
    chmod 755 files/linux/bin/proot files/linux/libexec/loader
'"
if [ $KEEP -eq 0 ]; then
    adb shell "run-as $PKG sh -c '
        set -e
        rm -rf files/linux/rootfs
        mkdir -p files/linux/rootfs
        toybox tar xzf $STAGE/rootfs.tar.gz -C files/linux/rootfs
    '"
fi
adb shell "run-as $PKG sh -c 'du -sh files/linux/rootfs 2>/dev/null; ls files/linux'"
echo "Done."
