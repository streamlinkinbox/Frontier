#!/usr/bin/env bash
# ========================================================================================================================================
#                           RUNREPLAYTEMPORALPROVENANCE.SH
# ========================================================================================================================================
# 🧩 Test-only provenance readback on matched moving-visibility lower-floor temporal cases.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvk_swiftshader.so}"
export LD_LIBRARY_PATH="$(dirname "$DRIVER")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MALLOC_ARENA_MAX="${MALLOC_ARENA_MAX:-2}"
ulimit -v "${VIRTUAL_MEMORY_KB:-3900000}"
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/replay-temporal-provenance-release}"
SOURCE="$BUILD/test-only-source"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayTemporalProvenance.txt}"
SELECTION="$ROOT/Exhibits/LightCompute/Results/ReplayTemporalSelectionStream.txt"
PATH_LOG="$ROOT/Exhibits/LightCompute/Results/ReplayTemporalPathStream.txt"
mkdir -p "$SOURCE" "$BUILD/provenance-baseline" "$BUILD/provenance-path" "$BUILD/provenance-selection" "$(dirname "$RESULTS")"
rm -f "$BUILD"/provenance-{baseline,path,selection}/*.spv
{
    echo 'Test-only temporal reservoir provenance slabs; no production shader or reservoir default changes.'
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes follow."
    echo "MALLOC_ARENA_MAX=$MALLOC_ARENA_MAX VIRTUAL_MEMORY_KB=$(ulimit -v)"
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    python3 Exhibits/LightCompute/Code/ReplayTemporalProvenanceCandidate.py \
        "$SOURCE/Baseline.slang" "$SOURCE/PathHash.slang" "$SOURCE/SelectionHash.slang"
    "$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -I Frontier/Engine/Rendering/Shaders \
        -entry TracePrimarySurfaces -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
        -o "$BUILD/provenance-baseline/Primary.spv"
    for ARM in baseline path selection; do
        case "$ARM" in
            baseline) SHADER="$SOURCE/Baseline.slang" ;;
            path) SHADER="$SOURCE/PathHash.slang" ;;
            selection) SHADER="$SOURCE/SelectionHash.slang" ;;
        esac
        "$SLANG" "$SHADER" -I Frontier/Engine/Rendering/Shaders \
            -entry GenerateReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
            -o "$BUILD/provenance-$ARM/ReplayGenerate.spv"
    done
    "$SLANG" "$SOURCE/Baseline.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 \
        -o "$BUILD/provenance-baseline/ReplayResolve.spv"
    for ARM in path selection; do
        cp "$BUILD/provenance-baseline/Primary.spv" "$BUILD/provenance-$ARM/Primary.spv"
        cp "$BUILD/provenance-baseline/ReplayResolve.spv" "$BUILD/provenance-$ARM/ReplayResolve.spv"
    done
    "${CMAKE:-cmake}" -S Exhibits/LightCompute -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
    "${CMAKE:-cmake}" --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir}.slang \
        Exhibits/LightCompute/Code/{ReplayTemporalProvenanceCandidate.py,ReplayTemporalProvenanceValidation.h,LightComputeValidation.cpp} \
        Exhibits/LightCompute/RunReplayTemporalProvenance.sh "$SOURCE"/{Baseline,PathHash,SelectionHash}.slang \
        "$BUILD"/provenance-{baseline,path,selection}/{Primary,ReplayGenerate,ReplayResolve}.spv
    "${CTEST:-ctest}" --test-dir "$BUILD" --output-on-failure
    for ARM in baseline path selection; do
        case "$ARM" in
            baseline) FLAG=baseline ;;
            path) FLAG=path ;;
            selection) FLAG=selection ;;
        esac
        if "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD/provenance-$ARM" \
             "--replay-temporal-provenance-$FLAG"; then
            echo "REPLAY_TEMPORAL_PROVENANCE_ARM_EXIT arm=$ARM status=0"
        else
            STATUS=$?
            echo "REPLAY_TEMPORAL_PROVENANCE_ARM_EXIT arm=$ARM status=$STATUS"
        fi
    done
} 2>&1 | tee "$RESULTS"
python3 Exhibits/LightCompute/Code/ReplayTemporalProvenanceSummary.py \
    "$RESULTS" "$SELECTION" "$PATH_LOG" | tee -a "$RESULTS"
