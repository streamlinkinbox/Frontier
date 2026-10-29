#!/usr/bin/env bash
#==========================================================================================================================================
#                                                  RUNREPLAYMATERIALPATH.SH
#==========================================================================================================================================
# 🧩 Exercise Engine replay recording against independent material and diffuse-bounce integrals.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvk_swiftshader.so}"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-material-path-release}"
SHADERS="$BUILD/shaders"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayMaterialPath-release.txt}"
mkdir -p "$SHADERS" "$(dirname "$RESULTS")"
rm -f "$SHADERS"/*.spv
for SPEC in 'LightInfluence TracePrimarySurfaces Primary' \
            'PathReplayReservoir GenerateReplayReservoirs ReplayGenerate' \
            'PathReplayReservoir ResolveReplayReservoirs ReplayResolve'; do
    read -r SOURCE ENTRY STEM <<< "$SPEC"
    "$SLANG" "Frontier/Engine/Rendering/Shaders/$SOURCE.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$SHADERS/$STEM.spv"
done
"${CMAKE:-cmake}" -S Exhibits/LightCompute -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
"${CMAKE:-cmake}" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
{
    echo "Recorded material and diffuse-bounce replay; SwiftShader and shaders are not sanitizer-instrumented."
    echo "Host base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.cpp,PathReplayVulkan.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering}.slang \
        Exhibits/LightCompute/Code/{ReplayMaterialSamplingValidation.h,ReplayMaterialPathValidation.h,LightComputeValidation.cpp} \
        "$SHADERS"/{Primary,ReplayGenerate,ReplayResolve}.spv
    "${CTEST:-ctest}" --test-dir "$BUILD" --output-on-failure
    "$BUILD/LightComputeValidation" "$DRIVER" "$SHADERS" --replay-material-path
} 2>&1 | tee "$RESULTS"
