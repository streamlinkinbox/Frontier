#!/usr/bin/env bash
#==========================================================================================================================================
#                                                 RUNREPLAYTRANSMISSION.SH
#==========================================================================================================================================
# 🧩 Compare replayed pane transport, equal-seed proposals, and bounded BVH fallback against independent checks.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
CMAKE="${CMAKE:-cmake}"
CTEST="${CTEST:-ctest}"
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvk_swiftshader.so}"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MODE=release
ENTRYPOINT=--replay-transmission
SHADER_SOURCE="$ROOT/Frontier/Engine/Rendering/Shaders"
if [[ "${BASELINE:-0}" == 1 ]]; then MODE=baseline; ENTRYPOINT=--replay-transmission-baseline; fi
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-transmission-$MODE}"
SHADERS="$BUILD/shaders"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayTransmission-$MODE.txt}"
mkdir -p "$SHADERS" "$(dirname "$RESULTS")"
rm -f "$SHADERS"/*.spv
if [[ "$MODE" == baseline ]]; then
    BASELINE_REVISION="${BASELINE_REVISION:-d9cbe17}"
    SNAPSHOT="$BUILD/baseline-source"
    mkdir -p "$SNAPSHOT"
    git archive "$BASELINE_REVISION" Frontier/Engine/Rendering/Shaders Engine/Render/Shader | tar -x -C "$SNAPSHOT"
    SHADER_SOURCE="$SNAPSHOT/Frontier/Engine/Rendering/Shaders"
fi

for SPEC in 'LightInfluence TracePrimarySurfaces Primary' \
            'PathReplayReservoir GenerateReplayReservoirs ReplayGenerate' \
            'PathReplayReservoir ResolveReplayReservoirs ReplayResolve' \
            'PathReplaySampleHistory GatherReplaySampleHistory ReplayGatherSampleHistory' \
            'PathReplaySampleHistory FinalizeReplaySampleHistory ReplayFinalizeSampleHistory'; do
    read -r SOURCE ENTRY STEM <<< "$SPEC"
    "$SLANG" "$SHADER_SOURCE/$SOURCE.slang" -I "$SHADER_SOURCE" \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$SHADERS/$STEM.spv"
done
if [[ "$MODE" != baseline ]]; then
    for SPEC in 'InspectReplaySegment ReplaySegmentValidation' 'InspectReplayMisLimits ReplayMisLimits'; do
        read -r ENTRY STEM <<< "$SPEC"
        "$SLANG" Exhibits/LightCompute/Code/ReplaySegmentValidation.slang -I "$SHADER_SOURCE" \
            -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$SHADERS/$STEM.spv"
    done
fi
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
{
    echo "Replay pane transport and reservoir safety; shaders and SwiftShader are not sanitizer-instrumented."
    echo "Host base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    if [[ "$MODE" == baseline ]]; then echo "Historical shader $BASELINE_REVISION; a nonzero numerical result is expected."; fi
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum "$SHADER_SOURCE"/{LightInfluence,PathReplayReservoir,PathReplaySampleHistory,MaterialRayScattering}.slang \
        Frontier/Engine/Rendering/{PathReplayDomain.cpp,PathReplayVulkan.cpp} \
        Exhibits/LightCompute/Code/{ReplayTransmissionValidation.h,ReplaySegmentValidation.h,ReplaySegmentValidation.slang,LightComputeValidation.cpp} \
        "$SHADERS"/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherSampleHistory,ReplayFinalizeSampleHistory}.spv
    if [[ "$MODE" != baseline ]]; then sha256sum "$SHADERS"/{ReplaySegmentValidation,ReplayMisLimits}.spv; fi
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/LightComputeValidation" "$DRIVER" "$SHADERS" "$ENTRYPOINT"
} 2>&1 | tee "$RESULTS"
