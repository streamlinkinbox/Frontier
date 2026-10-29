#!/usr/bin/env bash
# ========================================================================================================================================
#                                  RUNREPLAYTEMPORALSELECTIONSTREAM.SH
# ========================================================================================================================================
# 🧩 Paired SwiftShader holdout: original vs deterministic decorrelated reservoir-selection RNG; no production edits.
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
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-temporal-selection-stream-release}"
BASE="$BUILD/baseline"
DECORRELATED="$BUILD/decorrelated"
SOURCE="$BUILD/test-only-source"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayTemporalSelectionStream.txt}"
mkdir -p "$BASE" "$DECORRELATED" "$SOURCE" "$(dirname "$RESULTS")"
rm -f "$BASE"/*.spv "$DECORRELATED"/*.spv
{
    echo 'Test-only deterministic decorrelated reservoir-selection stream; SwiftShader and shaders are not sanitizer-instrumented.'
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    echo "MALLOC_ARENA_MAX=$MALLOC_ARENA_MAX VIRTUAL_MEMORY_KB=$(ulimit -v)"
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    python3 Exhibits/LightCompute/Code/ReplayTemporalFreshHistoryMath.py
    python3 Exhibits/LightCompute/Code/ReplayTemporalFreshHistorySummaryAudit.py
    python3 Exhibits/LightCompute/Code/ReplayTemporalSelectionStreamCandidate.py "$SOURCE/Baseline.slang" "$SOURCE/Decorrelated.slang"
    "$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -I Frontier/Engine/Rendering/Shaders \
        -entry TracePrimarySurfaces -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/Primary.spv"
    "$SLANG" "$SOURCE/Baseline.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry GenerateReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/ReplayGenerate.spv"
    "$SLANG" "$SOURCE/Baseline.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/ReplayResolve.spv"
    "$SLANG" "$SOURCE/Decorrelated.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry GenerateReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$DECORRELATED/ReplayGenerate.spv"
    "$SLANG" "$SOURCE/Decorrelated.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$DECORRELATED/ReplayResolve.spv"
    cp "$BASE/Primary.spv" "$DECORRELATED/"
    "${CMAKE:-cmake}" -S Exhibits/LightCompute -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
    "${CMAKE:-cmake}" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.cpp,PathReplayVulkan.cpp,PathReplaySubmissionSequence.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering,MaterialRayTransport,SolarOptics}.slang \
        Engine/Render/Shader/{ScatteringEvaluation,AutomotivePaint}.slang \
        Exhibits/LightCompute/Code/{ReplayMaterialReuseValidation.h,ReplayMaterialFloorValidation.h,ReplayMixedMaterialValidation.h,ReplayFreshDonorMotionValidation.h,ReplayTemporalFreshHistoryValidation.h,ReplayTemporalSelectionStreamValidation.h,ReplayTemporalSelectionStreamCandidate.py,LightComputeValidation.cpp} \
        Exhibits/LightCompute/{ReplayTemporalFreshHistory.md,RunReplayTemporalSelectionStream.sh} \
        "$SOURCE"/{Baseline,Decorrelated}.slang "$BASE"/{Primary,ReplayGenerate,ReplayResolve}.spv \
        "$DECORRELATED"/{Primary,ReplayGenerate,ReplayResolve}.spv
    "${CTEST:-ctest}" --test-dir "$BUILD" --output-on-failure
    if "$BUILD/LightComputeValidation" "$DRIVER" "$BASE" --replay-temporal-selection-baseline; then
        echo 'REPLAY_SELECTION_STREAM_ARM_EXIT arm=baseline status=0'
    else
        STATUS=$?
        echo "REPLAY_SELECTION_STREAM_ARM_EXIT arm=baseline status=$STATUS"
    fi
    if "$BUILD/LightComputeValidation" "$DRIVER" "$DECORRELATED" --replay-temporal-selection-decorrelated; then
        echo 'REPLAY_SELECTION_STREAM_ARM_EXIT arm=decorrelated status=0'
    else
        STATUS=$?
        echo "REPLAY_SELECTION_STREAM_ARM_EXIT arm=decorrelated status=$STATUS"
    fi
} 2>&1 | tee "$RESULTS"
python3 Exhibits/LightCompute/Code/ReplayTemporalSelectionStreamSummary.py "$RESULTS" | tee -a "$RESULTS"
