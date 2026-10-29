#!/usr/bin/env bash
#==========================================================================================================================================
#                                                RUNREPLAYMATERIALGUARD.SH
#==========================================================================================================================================
# 🧩 Compare a test-only material-boundary candidate with the unchanged Engine shader on held-out seeds.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvk_swiftshader.so}"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-material-guard-release}"
BASE="$BUILD/baseline"
GUARD="$BUILD/guard"
CANDIDATE="$BUILD/test-only-source"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayMaterialGuard-release.txt}"
mkdir -p "$BASE" "$GUARD" "$CANDIDATE" "$(dirname "$RESULTS")"
rm -f "$BASE"/*.spv "$GUARD"/*.spv
python3 Exhibits/LightCompute/Code/ReplayMaterialGuardCandidate.py "$CANDIDATE/PathReplayReservoir.slang"
for SPEC in 'LightInfluence TracePrimarySurfaces Primary' \
            'PathReplayReservoir GenerateReplayReservoirs ReplayGenerate'; do
    read -r SOURCE ENTRY STEM <<< "$SPEC"
    "$SLANG" "Frontier/Engine/Rendering/Shaders/$SOURCE.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/$STEM.spv"
    cp "$BASE/$STEM.spv" "$GUARD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang -I Frontier/Engine/Rendering/Shaders \
    -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/ReplayResolve.spv"
"$SLANG" "$CANDIDATE/PathReplayReservoir.slang" -I Frontier/Engine/Rendering/Shaders \
    -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$GUARD/ReplayResolve.spv"
for POLICY in baseline guard; do
    INCLUDES=(-I Frontier/Engine/Rendering/Shaders)
    if [[ "$POLICY" == guard ]]; then INCLUDES=(-I "$CANDIDATE" -I Frontier/Engine/Rendering/Shaders); fi
    "$SLANG" Exhibits/LightCompute/Code/ReplayMaterialGuardLinks.slang "${INCLUDES[@]}" \
        -entry InspectReplayMaterialGuardLinks -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
        -o "$BUILD/$POLICY/ReplayGuardLinks.spv"
done
"${CMAKE:-cmake}" -S Exhibits/LightCompute -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
"${CMAKE:-cmake}" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
{
    echo "Test-only material-boundary guard holdout; SwiftShader and shaders are not sanitizer-instrumented."
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.cpp,PathReplayVulkan.cpp,PathReplaySubmissionSequence.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering,MaterialRayTransport,SolarOptics}.slang \
        Engine/Render/Shader/{ScatteringEvaluation,AutomotivePaint}.slang \
        Exhibits/LightCompute/Code/{ReplayPbrReflectionGpuValidation.h,ReplayMaterialSamplingValidation.h,ReplayMaterialPathValidation.h,ReplayMaterialReuseValidation.h,ReplayMaterialFloorValidation.h,ReplayMixedMaterialValidation.h,ReplayMaterialGuardValidation.h,ReplayMaterialGuardLinks.slang,ReplayMaterialGuardCandidate.py,ReplayMaterialGuardSummary.py,LightComputeValidation.cpp} \
        "$CANDIDATE/PathReplayReservoir.slang" "$BASE"/{Primary,ReplayGenerate,ReplayResolve,ReplayGuardLinks}.spv \
        "$GUARD"/{Primary,ReplayGenerate,ReplayResolve,ReplayGuardLinks}.spv
    "${CTEST:-ctest}" --test-dir "$BUILD" --output-on-failure
    # 📝 A failed baseline must not hide the candidate arm; report both exits and fail the paired summary afterwards.
    if "$BUILD/LightComputeValidation" "$DRIVER" "$BASE" --replay-material-guard-baseline; then
        echo 'REPLAY_GUARD_ARM_EXIT arm=baseline status=0'
    else
        STATUS=$?
        echo "REPLAY_GUARD_ARM_EXIT arm=baseline status=$STATUS"
    fi
    if "$BUILD/LightComputeValidation" "$DRIVER" "$GUARD" --replay-material-guard-candidate; then
        echo 'REPLAY_GUARD_ARM_EXIT arm=guard status=0'
    else
        STATUS=$?
        echo "REPLAY_GUARD_ARM_EXIT arm=guard status=$STATUS"
    fi
} 2>&1 | tee "$RESULTS"
# This summary emits all diagnostics but exits nonzero if either arm or any mean gate failed.
python3 Exhibits/LightCompute/Code/ReplayMaterialGuardFailureAnalysis.py "$RESULTS" | tee -a "$RESULTS"
