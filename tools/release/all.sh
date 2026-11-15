#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
#
# All release builds on a Mac with Docker: macOS and the Android APK here; Linux x86_64, Linux ARM64 and Windows
# natively in ARM64 containers on Apple silicon (tools/release/x86_64/Dockerfile and tools/release/Dockerfile; on an
# x86_64 host, Linux x86_64 and Windows in tools/release/Dockerfile). Writes build/release/ with the five downloads,
# SHA256SUMS.txt, and each system's update-<system>.txt (what the launcher updates itself from).
# Needs the translated code of a build of each game with its ROM set (make GAME=<id>, make GAME=<id> explore).
#
#   TARGETS="macos android" tools/release/all.sh   # subset of targets (split across two Macs)
#   TARGETS="sums" tools/release/all.sh            # write SHA256SUMS.txt and check manifests once all five exist
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

TARGETS="${TARGETS:-macos linux-x86_64 linux-arm64 windows android}"
HOST_ARCH="$(uname -m)"
ALL_START="$(date +%s)"

fmt_dur() {
    s=$1
    if [ "$s" -ge 60 ]; then
        printf '%dm%02ds' $((s / 60)) $((s % 60))
    else
        printf '%ds' "$s"
    fi
}
has_target() {
    case " $TARGETS " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}
step() {
    label=$1; shift
    t0=$(date +%s)
    printf '[%s] %s...\n' "$(date '+%H:%M:%S')" "$label"
    "$@"
    t1=$(date +%s)
    printf '[%s] %s done (%s)\n' "$(date '+%H:%M:%S')" "$label" "$(fmt_dur $((t1 - t0)))"
}

[ "${NO_AUDIT:-}" = 1 ] || step "audit" tools/audit.sh

DOCKER_ARGS="-e GAMES=${GAMES:-} -e NO_GAME_CODE=${NO_GAME_CODE:-} -e NO_CCACHE=${NO_CCACHE:-} -e CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-}"
if [ -n "${CCACHE_BASE:-}" ]; then
    mkdir -p "$CCACHE_BASE"
    DOCKER_ARGS="$DOCKER_ARGS -v $CCACHE_BASE:/ccache -e CCACHE_BASE=/ccache"
fi

# Build only the Docker images needed for the selected targets.
need_x86=0 need_arm=0 need_amd64=0
if [ "$HOST_ARCH" = "arm64" ] || [ "$HOST_ARCH" = "aarch64" ]; then
    has_target linux-x86_64 && need_x86=1
    { has_target linux-arm64 || has_target windows; } && need_arm=1
else
    { has_target linux-x86_64 || has_target windows; } && need_amd64=1
    has_target linux-arm64 && need_arm=1
fi
[ "$need_x86" = 1 ] && step "docker build (linux-x86_64 cross)" docker build -q -t cps2recomp-build-x86_64 tools/release/x86_64 >/dev/null
[ "$need_arm" = 1 ] && step "docker build (arm64)" docker build --platform linux/arm64 -q -t cps2recomp-build-arm64 tools/release >/dev/null
[ "$need_amd64" = 1 ] && step "docker build (amd64)" docker build --platform linux/amd64 -q -t cps2recomp-build tools/release >/dev/null

rm -rf build/release/update-files

if has_target macos; then
    rm -f build/release/update-macos.txt
    step "macos" tools/release/macos.sh
fi
if has_target linux-x86_64; then
    rm -f build/release/update-linux-x86_64.txt
    if [ "$HOST_ARCH" = "arm64" ] || [ "$HOST_ARCH" = "aarch64" ]; then
        step "linux-x86_64 (native cross)" docker run --rm $DOCKER_ARGS -v "$ROOT":/src -w /src cps2recomp-build-x86_64 tools/release/linux.sh
    else
        step "linux-x86_64" docker run --rm --platform linux/amd64 $DOCKER_ARGS -v "$ROOT":/src -w /src cps2recomp-build tools/release/linux.sh
    fi
fi
if has_target linux-arm64; then
    rm -f build/release/update-linux-arm64.txt
    step "linux-arm64" docker run --rm --platform linux/arm64 $DOCKER_ARGS -v "$ROOT":/src -w /src cps2recomp-build-arm64 tools/release/linux.sh
fi
if has_target windows; then
    rm -f build/release/update-windows-x64.txt
    if [ "$HOST_ARCH" = "arm64" ] || [ "$HOST_ARCH" = "aarch64" ]; then
        step "windows-x64 (native cross)" docker run --rm --platform linux/arm64 $DOCKER_ARGS -v "$ROOT":/src -w /src cps2recomp-build-arm64 tools/release/windows.sh
    else
        step "windows-x64" docker run --rm --platform linux/amd64 $DOCKER_ARGS -v "$ROOT":/src -w /src cps2recomp-build tools/release/windows.sh
    fi
fi
if has_target android; then
    # the Android app, signed with the release key (~/.config/cps2recomp/android-signing.env; the script refuses without it)
    step "android" env RELEASE=1 tools/release/android.sh
fi

ARCHIVES="CPS2Recomp-macOS.zip CPS2Recomp-linux-x86_64.tar.gz CPS2Recomp-linux-arm64.tar.gz CPS2Recomp-windows-x64.zip CPS2Recomp-android.apk"
all_present=1
for a in $ARCHIVES; do
    [ -f "build/release/$a" ] || all_present=0
done

if [ "$all_present" = 1 ] && [ "${NO_SUMS:-}" != 1 ]; then
    cd build/release
    shasum -a 256 $ARCHIVES > SHA256SUMS.txt
    cat SHA256SUMS.txt
    # (each manifest names its system's archive as it is)
    for manifest in update-*.txt; do
        set -- $(sed -n 's/^archive //p' "$manifest")
        if [ "$(shasum -a 256 "$3" | cut -d' ' -f1)" != "$1" ]; then
            echo "error: $manifest does not name $3 as it is" >&2
            exit 1
        fi
    done
    echo "update manifests: $(ls update-*.txt | tr '\n' ' ')"
fi

ALL_END="$(date +%s)"
printf '[%s] version %s: release files in %s/build/release (total %s)\n' \
    "$(date '+%H:%M:%S')" "$(cat "$ROOT/VERSION")" "$ROOT" "$(fmt_dur $((ALL_END - ALL_START)))"
