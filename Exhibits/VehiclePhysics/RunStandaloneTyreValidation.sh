#!/usr/bin/env bash
# 🧩 Compare the standalone Engine tyre models and surface projection with retained demo references.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
if command -v cmake >/dev/null 2>&1; then CMAKE="$(command -v cmake)"; else CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if command -v ctest >/dev/null 2>&1; then CTEST="$(command -v ctest)"; else CTEST="$ROOT/_ClaudeScratch/venv/bin/ctest"; fi
PYTHON="${PYTHON:-$ROOT/_ClaudeScratch/venv/bin/python}"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
CXX="${CXX:-c++}"
LABEL=release
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release)
HISTORICAL_FLAGS=(-O2)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    LABEL=sanitized
    CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Debug -DFRONTIER_TYRE_SANITIZERS=ON)
    HISTORICAL_FLAGS=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
fi
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/tyre-standalone-$LABEL}"
RESULTS="${RESULTS:-$ROOT/Exhibits/VehiclePhysics/Results}"
mkdir -p "$BUILD" "$RESULTS"
export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export WGPU_BACKEND_TYPE=Vulkan
{
    printf 'Standalone Engine tyre reference validation; %s\n' "$LABEL"
    printf 'Base %s, plus working changes if any; source hashes below.\n' "$(git rev-parse --short HEAD)"
    uname -srm
    "$CXX" --version | head -1
    node --version
    "$PYTHON" -c 'import wgpu, numpy; print("wgpu", wgpu.__version__, "numpy", numpy.__version__)'
    [[ "$(git hash-object Experimental/VehicleHmiReference/Source/TyreCarcassProjection.js)" == 8d2719e5c9959f88c7b8f4fda8c81d1cbd1ab26e ]]
    [[ "$(git hash-object Experimental/VehicleHmiReference/Source/TyreContinuumElasticity.js)" == 0d62f516175845d2b842b73ff9ff491419451208 ]]
    sha256sum Frontier/Engine/Physics/{TyreContinuum.h,TyreContinuum.cpp,TyreSurfaceProjection.h} \
        Exhibits/VehiclePhysics/Code/{TyreContinuumReference.mjs,TyreProjectionReference.mjs,TyreSurfaceShaderValidation.py} \
        Experimental/VehicleHmiReference/Source/{TyreCarcassProjection.js,TyreContinuumElasticity.js} \
        Experimental/VehicleHmiReference/Shader/VehicleVisual.wgsl
    "$CMAKE" -S Exhibits/VehiclePhysics -B "$BUILD" "${CMAKE_ARGS[@]}" >/dev/null
    "$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" >/dev/null
    "$CTEST" --test-dir "$BUILD" --output-on-failure -V
    DEPENDENCIES="$("$CXX" -std=c++17 -I Frontier -MM Exhibits/VehiclePhysics/Code/TyreSurfaceProjectionProbe.cpp)"
    if grep -Eq 'VehicleSolver|Jolt' <<<"$DEPENDENCIES"; then
        printf 'Standalone projection unexpectedly includes vehicle/Jolt dependencies\n' >&2
        exit 1
    fi
    if nm -u "$BUILD/tyre-engine/libFrontierTyreContinuum.a" | c++filt | grep -Eq 'Frontier::Vehicle|JPH::'; then
        printf 'Standalone tyre library unexpectedly links vehicle/Jolt symbols\n' >&2
        exit 1
    fi
    printf 'STANDALONE_DEPENDENCIES vehicle_header=0 jolt_header=0 vehicle_symbols=0 jolt_symbols=0\n'
    git show 9388c96:Frontier/Engine/Physics/TyreDeformation.h > "$BUILD/PriorTyreDeformation.h"
    sed 's@Engine/Physics/TyreSurfaceProjection.h@PriorTyreDeformation.h@' \
        Exhibits/VehiclePhysics/Code/TyreSurfaceProjectionProbe.cpp > "$BUILD/PriorTyreSurfaceProjectionProbe.cpp"
    "$CXX" -std=c++17 "${HISTORICAL_FLAGS[@]}" -Wall -Wextra -Werror -I Frontier -I "$BUILD" \
        "$BUILD/PriorTyreSurfaceProjectionProbe.cpp" -o "$BUILD/PriorTyreSurfaceProjectionProbe"
    "$PYTHON" Exhibits/VehiclePhysics/Code/TyreSurfaceShaderValidation.py \
        "$BUILD/TyreSurfaceProjectionProbe" "$BUILD/projection-fixtures" "$BUILD/PriorTyreSurfaceProjectionProbe"
} 2>&1 | tee "$RESULTS/StandaloneTyre-$LABEL.txt"
