#!/usr/bin/env bash
# ========================================================================================================================================
#                           RUNREPLAYTEMPORALVISIBILITY.SH
# ========================================================================================================================================
# 🧩 Per-pixel test-only attribution of temporal replay visibility and reservoir weights.
# Set ENDPOINT_PAIR_ONLY=1 for exact-emitter-endpoint retraces; estimate flags isolate motion/occluder controls.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$ROOT/_ClaudeScratch/pytools/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$ROOT/_ClaudeScratch/build/swiftshader-unity-fast/Linux/libvk_swiftshader.so}"
export VULKAN_SDK="${VULKAN_SDK:-$ROOT/_ClaudeScratch/deps/Vulkan-Headers}"
export LD_LIBRARY_PATH="$(dirname "$DRIVER")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MALLOC_ARENA_MAX="${MALLOC_ARENA_MAX:-2}"
ulimit -v "${VIRTUAL_MEMORY_KB:-3900000}"
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-provenance-release}"
SOURCE="$BUILD/visibility-only-source"
OUTPUT="$BUILD/visibility-baseline"
DIRECT_ONLY="${DIRECT_ONLY:-0}"
ESTIMATE_ONLY="${ESTIMATE_ONLY:-0}"
STATIONARY_ESTIMATE_ONLY="${STATIONARY_ESTIMATE_ONLY:-0}"
CLEAR_MOTION_ESTIMATE_ONLY="${CLEAR_MOTION_ESTIMATE_ONLY:-0}"
OCCLUDER_ABLATION_ONLY="${OCCLUDER_ABLATION_ONLY:-0}"
VISIBILITY_ABLATION_ONLY="${VISIBILITY_ABLATION_ONLY:-0}"
ENDPOINT_PAIR_ONLY="${ENDPOINT_PAIR_ONLY:-0}"
if [[ "$DIRECT_ONLY" == 1 && ( "$ESTIMATE_ONLY" == 1 || "$STATIONARY_ESTIMATE_ONLY" == 1 || "$CLEAR_MOTION_ESTIMATE_ONLY" == 1 || "$OCCLUDER_ABLATION_ONLY" == 1 || "$VISIBILITY_ABLATION_ONLY" == 1 || "$ENDPOINT_PAIR_ONLY" == 1 ) ]] ||
   [[ "$ESTIMATE_ONLY" == 1 && ( "$STATIONARY_ESTIMATE_ONLY" == 1 || "$CLEAR_MOTION_ESTIMATE_ONLY" == 1 || "$OCCLUDER_ABLATION_ONLY" == 1 || "$VISIBILITY_ABLATION_ONLY" == 1 || "$ENDPOINT_PAIR_ONLY" == 1 ) ]] ||
   [[ "$STATIONARY_ESTIMATE_ONLY" == 1 && ( "$CLEAR_MOTION_ESTIMATE_ONLY" == 1 || "$OCCLUDER_ABLATION_ONLY" == 1 || "$VISIBILITY_ABLATION_ONLY" == 1 || "$ENDPOINT_PAIR_ONLY" == 1 ) ]] ||
   [[ "$CLEAR_MOTION_ESTIMATE_ONLY" == 1 && ( "$OCCLUDER_ABLATION_ONLY" == 1 || "$VISIBILITY_ABLATION_ONLY" == 1 || "$ENDPOINT_PAIR_ONLY" == 1 ) ]] ||
   [[ "$OCCLUDER_ABLATION_ONLY" == 1 && ( "$VISIBILITY_ABLATION_ONLY" == 1 || "$ENDPOINT_PAIR_ONLY" == 1 ) ]] ||
   [[ "$VISIBILITY_ABLATION_ONLY" == 1 && "$ENDPOINT_PAIR_ONLY" == 1 ]]; then
    echo "Choose only one visibility/estimate run mode" >&2
    exit 2
elif [[ "$ENDPOINT_PAIR_ONLY" == 1 ]]; then
    RESULTS="${RESULTS:-$ROOT/_ClaudeScratch/provenance/direct-endpoint-pair.log}"
    AUDIT_FLAG="--replay-temporal-direct-visibility-occluder-ablation"
elif [[ "$VISIBILITY_ABLATION_ONLY" == 1 ]]; then
    RESULTS="${RESULTS:-$ROOT/_ClaudeScratch/provenance/direct-visibility-occluder-ablation.log}"
    AUDIT_FLAG="--replay-temporal-direct-visibility-occluder-ablation"
elif [[ "$OCCLUDER_ABLATION_ONLY" == 1 ]]; then
    RESULTS="${RESULTS:-$ROOT/_ClaudeScratch/provenance/direct-estimate-occluder-ablation.log}"
    AUDIT_FLAG="--replay-temporal-direct-estimate-occluder-ablation"
elif [[ "$CLEAR_MOTION_ESTIMATE_ONLY" == 1 ]]; then
    RESULTS="${RESULTS:-$ROOT/_ClaudeScratch/provenance/direct-estimate-clear-motion.log}"
    AUDIT_FLAG="--replay-temporal-direct-estimate-clear-motion"
elif [[ "$STATIONARY_ESTIMATE_ONLY" == 1 ]]; then
    RESULTS="${RESULTS:-$ROOT/_ClaudeScratch/provenance/direct-estimate-stationary.log}"
    AUDIT_FLAG="--replay-temporal-direct-estimate-stationary"
elif [[ "$ESTIMATE_ONLY" == 1 ]]; then
    RESULTS="${RESULTS:-$ROOT/_ClaudeScratch/provenance/direct-estimate.log}"
    AUDIT_FLAG="--replay-temporal-direct-estimate"
elif [[ "$DIRECT_ONLY" == 1 ]]; then
    RESULTS="${RESULTS:-$ROOT/_ClaudeScratch/provenance/direct-visibility.log}"
    AUDIT_FLAG="--replay-temporal-direct-visibility"
else
    RESULTS="${RESULTS:-$ROOT/_ClaudeScratch/provenance/visibility.log}"
    AUDIT_FLAG="--replay-temporal-visibility"
fi
if [[ "$DIRECT_ONLY" != 0 && "$DIRECT_ONLY" != 1 ]]; then
    echo "DIRECT_ONLY must be 0 or 1 (got $DIRECT_ONLY)" >&2
    exit 2
fi
if [[ "$ESTIMATE_ONLY" != 0 && "$ESTIMATE_ONLY" != 1 ]]; then
    echo "ESTIMATE_ONLY must be 0 or 1 (got $ESTIMATE_ONLY)" >&2
    exit 2
fi
if [[ "$STATIONARY_ESTIMATE_ONLY" != 0 && "$STATIONARY_ESTIMATE_ONLY" != 1 ]]; then
    echo "STATIONARY_ESTIMATE_ONLY must be 0 or 1 (got $STATIONARY_ESTIMATE_ONLY)" >&2
    exit 2
fi
if [[ "$CLEAR_MOTION_ESTIMATE_ONLY" != 0 && "$CLEAR_MOTION_ESTIMATE_ONLY" != 1 ]]; then
    echo "CLEAR_MOTION_ESTIMATE_ONLY must be 0 or 1 (got $CLEAR_MOTION_ESTIMATE_ONLY)" >&2
    exit 2
fi
if [[ "$OCCLUDER_ABLATION_ONLY" != 0 && "$OCCLUDER_ABLATION_ONLY" != 1 ]]; then
    echo "OCCLUDER_ABLATION_ONLY must be 0 or 1 (got $OCCLUDER_ABLATION_ONLY)" >&2
    exit 2
fi
if [[ "$VISIBILITY_ABLATION_ONLY" != 0 && "$VISIBILITY_ABLATION_ONLY" != 1 ]]; then
    echo "VISIBILITY_ABLATION_ONLY must be 0 or 1 (got $VISIBILITY_ABLATION_ONLY)" >&2
    exit 2
fi
if [[ "$ENDPOINT_PAIR_ONLY" != 0 && "$ENDPOINT_PAIR_ONLY" != 1 ]]; then
    echo "ENDPOINT_PAIR_ONLY must be 0 or 1 (got $ENDPOINT_PAIR_ONLY)" >&2
    exit 2
fi
mkdir -p "$SOURCE" "$OUTPUT" "$(dirname "$RESULTS")"
rm -f "$OUTPUT"/*.spv
python3 Exhibits/LightCompute/Code/ReplayTemporalVisibilityCandidate.py "$SOURCE/Visibility.slang"
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -I Frontier/Engine/Rendering/Shaders \
    -entry TracePrimarySurfaces -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$OUTPUT/Primary.spv"
"$SLANG" "$SOURCE/Visibility.slang" -I Frontier/Engine/Rendering/Shaders \
    -entry GenerateReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$OUTPUT/ReplayGenerate.spv"
"$SLANG" "$SOURCE/Visibility.slang" -I Frontier/Engine/Rendering/Shaders \
    -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$OUTPUT/ReplayResolve.spv"
CMAKE="${CMAKE:-$ROOT/_ClaudeScratch/pytools/cmake/data/bin/cmake}"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
CTEST="${CTEST:-$ROOT/_ClaudeScratch/pytools/cmake/data/bin/ctest}"
"$CTEST" --test-dir "$BUILD" --output-on-failure
python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEstimateSummaryTest.py
python3 Exhibits/LightCompute/Code/ReplayTemporalDirectVisibilityAblationSummaryTest.py
python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEndpointPairSummaryTest.py
{
    echo 'Test-only per-pixel temporal visibility/contribution readback; no estimator or production shader changes.'
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    echo "MALLOC_ARENA_MAX=$MALLOC_ARENA_MAX VIRTUAL_MEMORY_KB=$(ulimit -v)"
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir}.slang \
        Exhibits/LightCompute/Code/{ReplayTemporalVisibilityCandidate.py,ReplayTemporalVisibilityValidation.h,ReplayTemporalDirectVisibilitySummary.py,ReplayTemporalDirectVisibilityAblationSummary.py,ReplayTemporalDirectVisibilityAblationSummaryTest.py,ReplayTemporalDirectEndpointPairSummary.py,ReplayTemporalDirectEndpointPairSummaryTest.py,ReplayTemporalDirectEstimateSummary.py,ReplayTemporalDirectEstimateSummaryTest.py,ReplayMaterialFloorValidation.h,LightComputeValidation.cpp} \
        "$SOURCE/Visibility.slang" "$OUTPUT"/{Primary,ReplayGenerate,ReplayResolve}.spv
    if "$BUILD/LightComputeValidation" "$DRIVER" "$OUTPUT" "$AUDIT_FLAG"; then
        :
    else
        STATUS=$?
        echo "REPLAY_TEMPORAL_VISIBILITY_EXIT status=$STATUS"
        exit "$STATUS"
    fi
} 2>&1 | tee "$RESULTS"
if [[ "$VISIBILITY_ABLATION_ONLY" == 1 ]]; then
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectVisibilityAblationSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectVisibilityAblation.svg | tee -a "$RESULTS"
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEndpointPairSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEndpointPair.svg | tee -a "$RESULTS"
elif [[ "$ENDPOINT_PAIR_ONLY" == 1 ]]; then
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEndpointPairSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEndpointPair.svg | tee -a "$RESULTS"
elif [[ "$OCCLUDER_ABLATION_ONLY" == 1 ]]; then
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEstimateSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEstimateBaseOnly.svg base_only | tee -a "$RESULTS"
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEstimateSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEstimateAddedOnly.svg added_only | tee -a "$RESULTS"
elif [[ "$CLEAR_MOTION_ESTIMATE_ONLY" == 1 ]]; then
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEstimateSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEstimateClearMotion.svg | tee -a "$RESULTS"
elif [[ "$STATIONARY_ESTIMATE_ONLY" == 1 ]]; then
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEstimateSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEstimateStationary.svg | tee -a "$RESULTS"
elif [[ "$ESTIMATE_ONLY" == 1 ]]; then
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEstimateSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEstimate.svg | tee -a "$RESULTS"
elif [[ "$DIRECT_ONLY" == 1 ]]; then
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectVisibilitySummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectVisibility.svg | tee -a "$RESULTS"
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEstimateSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEstimate.svg | tee -a "$RESULTS"
else
    python3 Exhibits/LightCompute/Code/ReplayTemporalVisibilitySummary.py "$RESULTS" \
        Exhibits/LightCompute/Results/ReplayTemporalPathStream.txt \
        Exhibits/LightCompute/Results/ReplayTemporalSelectionStream.txt \
        Exhibits/LightCompute/ReplayTemporalVisibility.svg | tee -a "$RESULTS"
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectVisibilitySummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectVisibility.svg | tee -a "$RESULTS"
    python3 Exhibits/LightCompute/Code/ReplayTemporalDirectEstimateSummary.py "$RESULTS" \
        Exhibits/LightCompute/ReplayTemporalDirectEstimate.svg | tee -a "$RESULTS"
fi
