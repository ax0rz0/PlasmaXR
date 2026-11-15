#!/usr/bin/env bash
# Sourced by the other scripts. Finds a JDK Gradle can use and adb, and exports:
#   ROOT   repository root
#   ADB    adb command as an array (honours SERIAL=<serial or host:port> when several devices exist)
#   PKG    application id

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG=org.plasmaxr.launcher

jdk_major() { "$1/bin/java" -version 2>&1 | sed -n '1s/.*version "\([0-9]*\).*/\1/p'; }

# AGP needs JDK 17+, and very new JDKs can lag behind it, so prefer 21 then 17.
pick_jdk() {
    local c major
    for c in "${JAVA_HOME:-}" \
             /opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
             /opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home \
             /usr/local/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
             /usr/lib/jvm/java-21-openjdk* /usr/lib/jvm/java-17-openjdk* \
             /Library/Java/JavaVirtualMachines/*/Contents/Home; do
        [[ -n "$c" && -x "$c/bin/java" ]] || continue
        major="$(jdk_major "$c")"
        if [[ "$major" == 17 || "$major" == 21 ]]; then
            export JAVA_HOME="$c"
            export PATH="$JAVA_HOME/bin:$PATH"
            return 0
        fi
    done
    echo "warning: no JDK 17 or 21 found; using whatever 'java' is on PATH" >&2
}
pick_jdk

if ! command -v adb >/dev/null 2>&1; then
    for d in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
        if [[ -n "$d" && -x "$d/platform-tools/adb" ]]; then
            export PATH="$d/platform-tools:$PATH"
            break
        fi
    done
fi

ADB=(adb)
if [[ -n "${SERIAL:-}" ]]; then ADB=(adb -s "$SERIAL"); fi
