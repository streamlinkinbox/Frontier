#!/usr/bin/env bash
# 📝 Build and exercise the authoritative engine replay domain without the exhibit archive or a GPU.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then
    CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"
fi
CTEST="${CTEST:-ctest}"
if ! command -v "$CTEST" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/ctest" ]]; then
    CTEST="$ROOT/_ClaudeScratch/venv/bin/ctest"
fi
MODE=release
FLAGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    FLAGS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
           '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-engine-$MODE"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${FLAGS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target PathReplayDomainValidation PathReplayArchiveValidation >/dev/null
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/PathReplayEngine-$MODE.txt}"
mkdir -p "$(dirname "$RESULTS")"
{
    printf 'Host build: %s\n' "$MODE"
    printf 'Platform: '; uname -sm
    COMPILER="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$BUILD/CMakeCache.txt")"
    printf 'C++ compiler: '; "$COMPILER" --version 2>&1 | sed -n '1p'
    sha256sum Frontier/Engine/Rendering/{PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp} \
              Frontier/Engine/Rendering/CMakeLists.txt \
              Exhibits/LightCompute/Code/{PathReplayDomainValidation.cpp,PathReplayArchiveValidation.cpp} \
              Exhibits/LightCompute/Code/LightComputeFormat.h Exhibits/LightCompute/CMakeLists.txt
    "$BUILD/PathReplayDomainValidation"
    "$BUILD/PathReplayArchiveValidation" "$BUILD/ReplayArchiveFixture.bin"
    "$CTEST" --test-dir "$BUILD" --output-on-failure -R '^PathReplay(Domain|Archive)Validation$'
} | tee "$RESULTS"
