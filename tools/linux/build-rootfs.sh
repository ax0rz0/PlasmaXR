#!/usr/bin/env bash
# Builds the Debian arm64 rootfs used by Plasma XR's Linux apps into build/linux/rootfs.tar.gz.
# Needs Docker (Colima is fine). An arm64 host builds natively; others need qemu-user.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/linux"
TAG=plasmaxr-rootfs
mkdir -p "$OUT"

docker build --platform linux/arm64 --build-arg "EXTRA=${EXTRA:-}" -t "$TAG" "$ROOT/tools/linux"

cid="$(docker create --platform linux/arm64 "$TAG" /bin/true)"
trap 'docker rm -f "$cid" >/dev/null 2>&1 || true' EXIT

# Export the filesystem, dropping pseudo-filesystems and files the container runtime adds.
docker export "$cid" | gzip -6 > "$OUT/rootfs.tar.gz"
ls -lh "$OUT/rootfs.tar.gz"
