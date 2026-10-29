#!/usr/bin/env bash
#==========================================================================================================================================
#                                         RUNREPLAYFRESHDONORMOTION.SH
#==========================================================================================================================================
# 🧩 Measure supplied-receiver motion and candidate counts; preserve failed correctness gates.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvk_swiftshader.so}"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MALLOC_ARENA_MAX="${MALLOC_ARENA_MAX:-2}"
ulimit -v "${VIRTUAL_MEMORY_KB:-3900000}"
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-fresh-motion-release}"
BASE="$BUILD/baseline"
FRESH="$BUILD/fresh"
CANDIDATE="$BUILD/test-only-source"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayFreshDonorMotion-reproduced.txt}"
mkdir -p "$BASE" "$FRESH" "$CANDIDATE" "$(dirname "$RESULTS")"
rm -f "$BASE"/*.spv "$FRESH"/*.spv
python3 Exhibits/LightCompute/Code/ReplayFreshDonorCandidateLean.py "$CANDIDATE/PathReplayReservoir.slang"
for SPEC in 'LightInfluence TracePrimarySurfaces Primary' \
            'PathReplayReservoir GenerateReplayReservoirs ReplayGenerate'; do
    read -r SOURCE ENTRY STEM <<< "$SPEC"
    "$SLANG" "Frontier/Engine/Rendering/Shaders/$SOURCE.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/$STEM.spv"
    cp "$BASE/$STEM.spv" "$FRESH/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang -I Frontier/Engine/Rendering/Shaders \
    -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/ReplayResolve.spv"
"$SLANG" "$CANDIDATE/PathReplayReservoir.slang" -I Frontier/Engine/Rendering/Shaders \
    -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$FRESH/ReplayResolve.spv"
for POLICY in baseline fresh; do
    INCLUDES=(-I Frontier/Engine/Rendering/Shaders)
    if [[ "$POLICY" == fresh ]]; then INCLUDES=(-I "$CANDIDATE" -I Frontier/Engine/Rendering/Shaders); fi
    "$SLANG" Exhibits/LightCompute/Code/ReplayMaterialGuardLinks.slang "${INCLUDES[@]}" \
        -entry InspectReplayMaterialGuardLinks -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
        -o "$BUILD/$POLICY/ReplayGuardLinks.spv"
done
"${CMAKE:-cmake}" -S Exhibits/LightCompute -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
"${CMAKE:-cmake}" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
{
    echo "Test-only moving-receiver holdout; SwiftShader and shaders are not sanitizer-instrumented."
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    echo "MALLOC_ARENA_MAX=$MALLOC_ARENA_MAX VIRTUAL_MEMORY_KB=$(ulimit -v)"
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.cpp,PathReplayVulkan.cpp,PathReplaySubmissionSequence.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering,MaterialRayTransport,SolarOptics}.slang \
        Engine/Render/Shader/{ScatteringEvaluation,AutomotivePaint}.slang \
        Exhibits/LightCompute/Code/{ReplayPbrReflectionGpuValidation.h,ReplayMaterialSamplingValidation.h,ReplayMaterialPathValidation.h,ReplayMaterialReuseValidation.h,ReplayMaterialFloorValidation.h,ReplayMixedMaterialValidation.h,ReplayMaterialGuardValidation.h,ReplayMaterialGuardLinks.slang,ReplayFreshDonorCandidate.py,ReplayFreshDonorCandidateLean.py,ReplayFreshDonorMotionValidation.h,ReplayFreshDonorMotionSummary.py,LightComputeValidation.cpp} \
        Exhibits/LightCompute/RunReplayFreshDonorMotion.sh \
        "$CANDIDATE/PathReplayReservoir.slang" "$BASE"/{Primary,ReplayGenerate,ReplayResolve,ReplayGuardLinks}.spv \
        "$FRESH"/{Primary,ReplayGenerate,ReplayResolve,ReplayGuardLinks}.spv
    "${CTEST:-ctest}" --test-dir "$BUILD" --output-on-failure
    if "$BUILD/LightComputeValidation" "$DRIVER" "$BASE" --replay-fresh-motion-baseline; then
        echo 'REPLAY_FRESH_MOTION_ARM_EXIT arm=baseline status=0'
    else
        STATUS=$?
        echo "REPLAY_FRESH_MOTION_ARM_EXIT arm=baseline status=$STATUS"
    fi
    if "$BUILD/LightComputeValidation" "$DRIVER" "$FRESH" --replay-fresh-motion-candidate; then
        echo 'REPLAY_FRESH_MOTION_ARM_EXIT arm=fresh status=0'
    else
        STATUS=$?
        echo "REPLAY_FRESH_MOTION_ARM_EXIT arm=fresh status=$STATUS"
    fi
} 2>&1 | tee "$RESULTS"
# A failed mean, a failed arm, or a mismatched negative control remains a nonzero script exit.
python3 Exhibits/LightCompute/Code/ReplayFreshDonorMotionSummary.py "$RESULTS" | tee -a "$RESULTS"
