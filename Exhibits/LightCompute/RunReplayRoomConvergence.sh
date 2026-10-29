#!/usr/bin/env bash
# 🧩 Audit room control convergence using independent CPU sampling and unchanged paired GPU transport.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
CMAKE="${CMAKE:-cmake}"
CTEST="${CTEST:-ctest}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if ! command -v "$CTEST" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/ctest" ]]; then CTEST="$ROOT/_ClaudeScratch/venv/bin/ctest"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' '-DCMAKE_CXX_FLAGS_DEBUG=-O1 -g')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-room-convergence-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayRoomConvergence-$MODE.txt}"
OUTPUT="${OUTPUT:-$ROOT/_ClaudeScratch/replay-room-convergence-capture-$MODE}"
IMAGES="${IMAGES:-$OUTPUT/png}"
if [[ -z "${PYTHON:-}" ]]; then
    if [[ -x "$ROOT/_ClaudeScratch/venv/bin/python" ]]; then PYTHON="$ROOT/_ClaudeScratch/venv/bin/python"; else PYTHON=python3; fi
fi
mkdir -p "$BUILD" "$BUILD/whole" "$BUILD/uncached" "$BUILD/plain" "$(dirname "$RESULTS")"
python3 Exhibits/LightCompute/Code/CompileReplayMISPower.py "$BUILD"
for METHOD in balance power; do
    SOURCE="$BUILD/balance/ReplayResetBudget.slang"
    if [[ "$METHOD" == power ]]; then SOURCE="$BUILD/power/ReplayMISPower.slang"; fi
    for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" "$SOURCE" -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv \
            -profile spirv_1_3 -emit-spirv-directly -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/$METHOD/$STEM.spv"
    done
done
for PAIR in 'GatherReplaySampleHistory ReplayGatherSampleHistory' 'FinalizeReplaySampleHistory ReplayFinalizeSampleHistory'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplaySampleHistory.slang -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly \
        -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
"$SLANG" Exhibits/LightCompute/Code/ReplayFreshBudget.slang -I Frontier/Engine/Rendering/Shaders \
    -entry GenerateFreshBudget -target spirv -profile spirv_1_3 -emit-spirv-directly \
    -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/ReplayFreshBudget.spv"
for METHOD in balance power; do
    for STEM in Primary ReplayGatherSampleHistory ReplayFinalizeSampleHistory ReplayFreshBudget; do
        cp "$BUILD/$STEM.spv" "$BUILD/$METHOD/$STEM.spv"
    done
done
# The FreshBudget pipeline replaces Generate; compile that evaluator with power weights too.
"$SLANG" "$BUILD/power/ReplayFreshPower.slang" -I Frontier/Engine/Rendering/Shaders \
    -entry GenerateFreshBudget -target spirv -profile spirv_1_3 -emit-spirv-directly \
    -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/power/ReplayFreshBudget.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" >/dev/null
{
    echo "Fixed beta=2 MIS experiment; host $MODE; external driver/shaders not sanitizer-instrumented"
    echo "Base $(git rev-parse --short HEAD) plus working changes; source hashes below"
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Exhibits/LightCompute/RunReplayRoomConvergence.sh \
        Exhibits/LightCompute/Code/{ReplayRoomConvergenceValidation.h,CompileReplayMISPower.py,ReplayMISPowerValidation.h,LightComputeValidation.cpp,ReplayFirstHopMISValidation.h,ReplayRoomReference.h,CompileReplayResetBudget.py,CompileReplayBoundedTemporal.py} \
        Frontier/Engine/Rendering/Shaders/{PathReplayReservoir,PathReplaySampleHistory,LightInfluence}.slang \
        "$BUILD/balance/ReplayResetBudget.slang" "$BUILD/power/ReplayMISPower.slang" "$BUILD/power/ReplayFreshPower.slang" \
        "$BUILD"/{balance,power}/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherSampleHistory,ReplayFinalizeSampleHistory,ReplayFreshBudget}.spv \
        "$TOOLS/SwiftShader/libvulkan.so.1" "$BUILD/LightComputeValidation"
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-room-convergence "$OUTPUT"
} 2>&1 | tee "$RESULTS"
