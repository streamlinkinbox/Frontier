#!/usr/bin/env bash
# 🧩 Build and exercise the opt-in C++17 area-reservoir primitive without Vulkan or renderer activation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-$ROOT/_ClaudeScratch/venv/bin/cmake}"
CTEST="${CTEST:-$ROOT/_ClaudeScratch/venv/bin/ctest}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie'
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie')
    export ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
fi
BUILD="$ROOT/_ClaudeScratch/build/facet-area-port-$MODE"
mkdir -p "$BUILD"
{
    printf 'FACET_AREA_PORT_BUILD mode=%s parent=%s working_changes=1\n' "$MODE" "$(git rev-parse HEAD)"
    "${CXX:-c++}" --version
    sha256sum Frontier/Engine/Rendering/FacetAreaReservoir.{h,cpp} Frontier/Engine/Rendering/CMakeLists.txt \
        Exhibits/LightCompute/{FacetAreaPort/CMakeLists.txt,Code/FacetAreaPortValidation.cpp,RunFacetAreaPort.sh}
    "$CMAKE" -S Exhibits/LightCompute/FacetAreaPort -B "$BUILD" "${OPTIONS[@]}"
    "$CMAKE" --build "$BUILD" --target FacetAreaPortValidation --parallel 1
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/FacetAreaPortValidation" --unit
} 2>&1 | tee "Exhibits/LightCompute/Results/FacetAreaPort-$MODE.txt"
