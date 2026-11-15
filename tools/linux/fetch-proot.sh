#!/usr/bin/env bash
# Downloads Termux's aarch64 build of proot (and the two libraries it needs) into
# build/linux/proot, laid out the way the app expects: bin/ lib/ libexec/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/linux"
BASE=https://packages.termux.dev/apt/termux-main
WORK="$OUT/dl"
rm -rf "$WORK" "$OUT/proot" && mkdir -p "$WORK" "$OUT/proot"/{bin,lib,libexec}

echo "Looking up current package versions..."
curl -fsSL "$BASE/dists/stable/main/binary-aarch64/Packages" -o "$WORK/Packages"
pkg_path() {
    awk -v want="$1" 'BEGIN{RS=""; FS="\n"} {
        name=""; file="";
        for (i=1;i<=NF;i++) { if ($i ~ /^Package: /) name=substr($i,10); if ($i ~ /^Filename: /) file=substr($i,11) }
        if (name==want) { print file; exit }
    }' "$WORK/Packages"
}

for p in proot libtalloc libandroid-shmem; do
    f="$(pkg_path "$p")"
    [ -n "$f" ] || { echo "package $p not found" >&2; exit 1; }
    echo "  $p -> $f"
    curl -fsSL "$BASE/$f" -o "$WORK/$p.deb"
    mkdir -p "$WORK/x/$p"
    # A .deb is an ar archive; bsdtar reads it directly.
    tar -xf "$WORK/$p.deb" -C "$WORK/x/$p"
    (cd "$WORK/x/$p" && tar -xf data.tar.xz)
done

U=data/data/com.termux/files/usr
cp "$WORK/x/proot/$U/bin/proot" "$OUT/proot/bin/proot"
cp "$WORK/x/proot/$U/libexec/proot/loader" "$OUT/proot/libexec/loader"
# proot is linked against libtalloc.so.2 by name.
cp "$WORK/x/libtalloc/$U/lib/libtalloc.so.2.5.0" "$OUT/proot/lib/libtalloc.so.2"
cp "$WORK/x/libandroid-shmem/$U/lib/libandroid-shmem.so" "$OUT/proot/lib/"
rm -rf "$WORK"
echo "proot staged in $OUT/proot"
