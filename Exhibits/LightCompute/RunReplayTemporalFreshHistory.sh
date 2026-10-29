#!/usr/bin/env bash
#==========================================================================================================================================
#                                         RUNREPLAYTEMPORALFRESHHISTORY.SH
#==========================================================================================================================================
# 🧩 Serial instrumented original/raw temporal-history comparison; failures remain nonzero.
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
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-temporal-history-release}"
BASE="$BUILD/baseline"
RAW="$BUILD/raw"
SOURCE="$BUILD/test-only-source"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayTemporalFreshHistory-reproduced.txt}"
PRIOR="$ROOT/Exhibits/LightCompute/Results/ReplayFreshDonorMotion-paired.txt"
mkdir -p "$BASE" "$RAW" "$SOURCE" "$(dirname "$RESULTS")"
rm -f "$BASE"/*.spv "$RAW"/*.spv
{
    echo 'Test-only diagnostic and raw-seed temporal history; SwiftShader and shaders are not sanitizer-instrumented.'
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    echo "MALLOC_ARENA_MAX=$MALLOC_ARENA_MAX VIRTUAL_MEMORY_KB=$(ulimit -v)"
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    python3 Exhibits/LightCompute/Code/ReplayTemporalFreshHistoryMath.py
    python3 Exhibits/LightCompute/Code/ReplayTemporalFreshHistorySummaryAudit.py
    python3 Exhibits/LightCompute/Code/ReplayTemporalFreshHistoryCandidate.py "$SOURCE/Diagnostic.slang" "$SOURCE/Raw.slang"
    "$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -I Frontier/Engine/Rendering/Shaders \
        -entry TracePrimarySurfaces -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/Primary.spv"
    "$SLANG" "$SOURCE/Diagnostic.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry GenerateReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/ReplayGenerate.spv"
    "$SLANG" "$SOURCE/Diagnostic.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BASE/ReplayResolve.spv"
    "$SLANG" "$SOURCE/Raw.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$RAW/ReplayResolve.spv"
    cp "$BASE/Primary.spv" "$BASE/ReplayGenerate.spv" "$RAW/"
    "${CMAKE:-cmake}" -S Exhibits/LightCompute -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
    "${CMAKE:-cmake}" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
    sha256sum Frontier/Engine/Rendering/{PathReplayDomain.cpp,PathReplayVulkan.cpp,PathReplaySubmissionSequence.cpp} \
        Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering,MaterialRayTransport,SolarOptics}.slang \
        Engine/Render/Shader/{ScatteringEvaluation,AutomotivePaint}.slang \
        Exhibits/LightCompute/Code/{ReplayMaterialReuseValidation.h,ReplayMaterialFloorValidation.h,ReplayMixedMaterialValidation.h,ReplayFreshDonorMotionValidation.h,ReplayTemporalFreshHistoryValidation.h,ReplayTemporalFreshHistoryCandidate.py,ReplayTemporalFreshHistoryMath.py,ReplayTemporalFreshHistorySummary.py,ReplayTemporalFreshHistorySummaryAudit.py,LightComputeValidation.cpp} \
        Exhibits/LightCompute/{ReplayTemporalFreshHistory.md,RunReplayTemporalFreshHistory.sh} "$PRIOR" \
        "$SOURCE"/{Diagnostic,Raw}.slang "$BASE"/{Primary,ReplayGenerate,ReplayResolve}.spv \
        "$RAW"/{Primary,ReplayGenerate,ReplayResolve}.spv
    "${CTEST:-ctest}" --test-dir "$BUILD" --output-on-failure
    if "$BUILD/LightComputeValidation" "$DRIVER" "$BASE" --replay-temporal-history-baseline; then
        echo 'REPLAY_TEMPORAL_ARM_EXIT arm=baseline status=0'
    else
        STATUS=$?
        echo "REPLAY_TEMPORAL_ARM_EXIT arm=baseline status=$STATUS"
    fi
    if "$BUILD/LightComputeValidation" "$DRIVER" "$RAW" --replay-temporal-history-raw; then
        echo 'REPLAY_TEMPORAL_ARM_EXIT arm=raw status=0'
    else
        STATUS=$?
        echo "REPLAY_TEMPORAL_ARM_EXIT arm=raw status=$STATUS"
    fi
} 2>&1 | tee "$RESULTS"
python3 Exhibits/LightCompute/Code/ReplayTemporalFreshHistorySummary.py "$RESULTS" "$PRIOR" | tee -a "$RESULTS"
