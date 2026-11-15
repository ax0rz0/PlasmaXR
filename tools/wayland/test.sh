#!/usr/bin/env bash
# Runs the compositor (built for Linux) against real Wayland clients in a container and collects
# screenshots. Needs Docker (Colima is fine).
#
#   ./tools/wayland/test.sh <scenario>        scenarios live in tools/wayland/scenarios/
#   ./tools/wayland/test.sh                   runs all of them
#
# Output: build/wayland-test/<scenario>/*.png and log.txt
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/wayland-test"
IMAGE=${IMAGE:-plasmaxr-wayland-test}
mkdir -p "$OUT"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build --platform linux/arm64 -t "$IMAGE" -f "$ROOT/tools/wayland/Dockerfile.test" "$ROOT/tools/wayland"
fi

scenarios=("$@")
if [ ${#scenarios[@]} -eq 0 ]; then
    scenarios=($(cd "$ROOT/tools/wayland/scenarios" && ls *.sh | sed 's/\.sh$//'))
fi

for s in "${scenarios[@]}"; do
    echo "=== $s"
    rm -rf "$OUT/$s" && mkdir -p "$OUT/$s"
    docker run --rm --platform linux/arm64 \
        -v "$ROOT:/src:ro" -v "$OUT/$s:/out" \
        "$IMAGE" bash -c '
            set -e
            mkdir -p /run/wl /tmp/b && chmod 700 /run/wl
            g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
                -I /src/app/src/main/cpp /src/app/src/main/cpp/wayland/*.cpp \
                /src/tools/wayland/host_test.cpp -lz -o /tmp/b/host_test
            export HARNESS=/tmp/b/host_test
            bash /src/tools/wayland/scenarios/'"$s"'.sh
        ' 2>&1 | tee "$OUT/$s/log.txt" | tail -${TAIL:-40}
done
