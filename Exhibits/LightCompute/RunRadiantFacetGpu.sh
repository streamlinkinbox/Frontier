#!/usr/bin/env bash
# 🧩 Run the Engine radiant-facet distribution through the existing Vulkan fog-area checks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then
    CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"
fi
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/radiant-facet-$MODE"
FIXTURE="$ROOT/_ClaudeScratch/radiant-facet-fixture"
OUTPUT="$ROOT/_ClaudeScratch/radiant-facet-gpu-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/RadiantFacetGpu-$MODE.txt}"
mkdir -p "$BUILD" "$FIXTURE" "$OUTPUT" "$(dirname "$RESULTS")"
for PAIR in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'ReuseLightNeighbours Reuse' \
            'ReconstructLightHistory Reconstruct' 'FilterLightNeighbours Filter' 'FinalizeLightHistory Finalize' \
            'TracePrimarySurfaces Primary' 'TraceSurfaceTransport Transport' \
            'ReprojectSurfaceTransport TransportTemporal' 'FilterSurfaceTransport TransportSpatial' \
            'ResolveSurfaceTransport TransportResolve' 'BakeCloudRadiance Clouds' 'IntegrateFogScattering Fog'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry "$ENTRY" -target spirv \
        -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
node Exhibits/VehicleReference/Code/Materials.mjs
"$CMAKE" -S Exhibits/VehicleReference -B "$ROOT/_ClaudeScratch/build/radiant-facet-vehicle" \
    -DCMAKE_BUILD_TYPE=Release >/dev/null
"$CMAKE" --build "$ROOT/_ClaudeScratch/build/radiant-facet-vehicle" --parallel 2 --target VehicleCapture >/dev/null
"$ROOT/_ClaudeScratch/build/radiant-facet-vehicle/VehicleCapture" "$FIXTURE" --vulkan-input
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target LightComputeValidation >/dev/null
{
    echo "Engine CDF in existing fog/facet checks; host $MODE; external Vulkan driver is not instrumented"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    uname -srm
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/RadiantFacetDistribution.{h,cpp} \
        Frontier/Engine/Rendering/Shaders/LightInfluence.slang \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,EmissiveFacetQuadrature.h,FogFacetValidation.h} \
        "$FIXTURE/VehicleLightInput.bin"
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" "$FIXTURE/VehicleLightInput.bin" "$OUTPUT" \
        --fog-facet-checks
} | tee "$RESULTS"
