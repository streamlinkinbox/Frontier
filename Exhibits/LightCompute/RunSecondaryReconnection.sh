#!/usr/bin/env bash
# 🧩 Verify restricted secondary-vertex measures, proposal support and production reservoir normalization.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
MODE=release
OPTIONS=(-std=c++20 -O2 -Wall -Wextra -Werror -I Frontier)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/secondary-reconnection-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/SecondaryReconnection-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
{
    printf 'SECONDARY_RECONNECTION platform=%s mode=%s revision=%s working_changes=1\n' "$(uname -s)" "$MODE" "$(git rev-parse HEAD)"
    "${CXX:-c++}" --version
    printf 'OPTIONS %s\n' "${OPTIONS[*]}"
    sha256sum Exhibits/LightCompute/Code/SecondaryReconnectionValidation.cpp \
        Exhibits/LightCompute/RunSecondaryReconnection.sh \
        Frontier/Engine/Rendering/ReservoirSampling.{h,cpp}
    "${CXX:-c++}" "${OPTIONS[@]}" Exhibits/LightCompute/Code/SecondaryReconnectionValidation.cpp \
        Frontier/Engine/Rendering/ReservoirSampling.cpp -o "$BUILD/SecondaryReconnectionValidation"
    "$BUILD/SecondaryReconnectionValidation"
} 2>&1 | tee "$RESULTS"
