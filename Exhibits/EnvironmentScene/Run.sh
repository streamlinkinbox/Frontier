#!/usr/bin/env bash
# 🧩 Validate environment records, editor descriptions and the Sun/sky/cloud/fog scene render.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-cmake}"
CTEST="${CTEST:-ctest}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if ! command -v "$CTEST" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/ctest" ]]; then CTEST="$ROOT/_ClaudeScratch/venv/bin/ctest"; fi
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/environment-scene-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/EnvironmentScene/Results/EnvironmentScene-$MODE.txt}"
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
"$CMAKE" -S Exhibits/EnvironmentScene -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" >/dev/null
{
    echo "Environment records, editor descriptions and scene render; host $MODE"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any"
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$CTEST" --test-dir "$BUILD" --output-on-failure
} 2>&1 | tee "$RESULTS"
