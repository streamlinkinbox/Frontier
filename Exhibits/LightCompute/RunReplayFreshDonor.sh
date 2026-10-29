#!/usr/bin/env bash
#==========================================================================================================================================
#                                                 RUNREPLAYFRESHDONOR.SH
#==========================================================================================================================================
# 🧩 Compare a test-only fresh-neighbor candidate with unchanged replay on independent held-out seeds.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvk_swiftshader.so}"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-fresh-donor-release}"
BASE="$BUILD/baseline"
FRESH="$BUILD/fresh"
CANDIDATE="$BUILD/test-only-source"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayFreshDonor-reproduced.txt}"
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
    echo "Test-only fresh-neighbor holdout; SwiftShader and shaders are not sanitizer-instrumented."
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.cpp,PathReplayVulkan.cpp,PathReplaySubmissionSequence.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering,MaterialRayTransport,SolarOptics}.slang \
        Engine/Render/Shader/{ScatteringEvaluation,AutomotivePaint}.slang \
        Exhibits/LightCompute/Code/{ReplayPbrReflectionGpuValidation.h,ReplayMaterialSamplingValidation.h,ReplayMaterialPathValidation.h,ReplayMaterialReuseValidation.h,ReplayMaterialFloorValidation.h,ReplayMixedMaterialValidation.h,ReplayMaterialGuardValidation.h,ReplayMaterialGuardLinks.slang,ReplayFreshDonorCandidate.py,ReplayFreshDonorCandidateLean.py,ReplayFreshDonorValidation.h,ReplayFreshDonorSummary.py,LightComputeValidation.cpp} \
        "$CANDIDATE/PathReplayReservoir.slang" "$BASE"/{Primary,ReplayGenerate,ReplayResolve,ReplayGuardLinks}.spv \
        "$FRESH"/{Primary,ReplayGenerate,ReplayResolve,ReplayGuardLinks}.spv
    "${CTEST:-ctest}" --test-dir "$BUILD" --output-on-failure
    if "$BUILD/LightComputeValidation" "$DRIVER" "$BASE" --replay-fresh-donor-baseline; then
        echo 'REPLAY_FRESH_ARM_EXIT arm=baseline status=0'
    else
        STATUS=$?
        echo "REPLAY_FRESH_ARM_EXIT arm=baseline status=$STATUS"
    fi
    if "$BUILD/LightComputeValidation" "$DRIVER" "$FRESH" --replay-fresh-donor-candidate; then
        echo 'REPLAY_FRESH_ARM_EXIT arm=fresh status=0'
    else
        STATUS=$?
        echo "REPLAY_FRESH_ARM_EXIT arm=fresh status=$STATUS"
    fi
} 2>&1 | tee "$RESULTS"
# Failed correctness or control gates are reported and keep the script's exit status nonzero.
python3 Exhibits/LightCompute/Code/ReplayFreshDonorSummary.py "$RESULTS" | tee -a "$RESULTS"
