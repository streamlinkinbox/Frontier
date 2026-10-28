#!/usr/bin/env bash
# 🧩 Validate fresh direct lighting and indirect-only reservoir sampling with separately counted work.
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
BUILD="$ROOT/_ClaudeScratch/build/replay-indirect-split-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayIndirectSplit-$MODE.txt}"
OUTPUT="${OUTPUT:-$ROOT/_ClaudeScratch/replay-indirect-split-capture-$MODE}"
IMAGES="${IMAGES:-$OUTPUT/png}"
if [[ -z "${PYTHON:-}" ]]; then
    if [[ -x "$ROOT/_ClaudeScratch/venv/bin/python" ]]; then PYTHON="$ROOT/_ClaudeScratch/venv/bin/python"; else PYTHON=python3; fi
fi
mkdir -p "$BUILD" "$BUILD/uncached" "$BUILD/plain" "$(dirname "$RESULTS")"
python3 Exhibits/LightCompute/Code/CompileReplayIndirectSplit.py "$BUILD/ReplayIndirectSplit.slang"
NATIVE=(-std=c++20 -O2 -Wall -Wextra -Werror)
if [[ "$MODE" == sanitized ]]; then NATIVE+=(-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-c++}" "${NATIVE[@]}" Exhibits/LightCompute/Code/ReplayBoundedTemporalMeasure.cpp -o "$BUILD/ReplayBoundedTemporalMeasure"
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" "$BUILD/ReplayIndirectSplit.slang" \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/$STEM.spv"
done
for PAIR in 'GatherReplaySampleHistory ReplayGatherSampleHistory' 'FinalizeReplaySampleHistory ReplayFinalizeSampleHistory'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplaySampleHistory.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
# A plain shader control verifies that path-counting atomics do not alter transport or histories.
for SPEC in 'PathReplayReservoir GenerateReplayReservoirs ReplayGenerate' \
            'PathReplayReservoir ResolveReplayReservoirs ReplayResolve' \
            'PathReplaySampleHistory GatherReplaySampleHistory ReplayGatherSampleHistory' \
            'PathReplaySampleHistory FinalizeReplaySampleHistory ReplayFinalizeSampleHistory'; do
    read -r SOURCE ENTRY STEM <<< "$SPEC"
    "$SLANG" "Frontier/Engine/Rendering/Shaders/$SOURCE.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/plain/$STEM.spv"
done
cp "$BUILD/Primary.spv" "$BUILD/plain/Primary.spv"
"$SLANG" Exhibits/LightCompute/Code/ReplayFreshBudget.slang -I Frontier/Engine/Rendering/Shaders \
    -entry GenerateFreshBudget -target spirv -profile spirv_1_3 -emit-spirv-directly \
    -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/ReplayFreshBudget.spv"
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" "$BUILD/whole/ReplayResetBudget.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly \
        -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/whole/$STEM.spv"
done
for STEM in Primary ReplayGatherSampleHistory ReplayFinalizeSampleHistory ReplayFreshBudget; do
    cp "$BUILD/$STEM.spv" "$BUILD/whole/$STEM.spv"
done
for PAIR in 'GenerateSplitIdentity SplitIdentityGenerate' 'ResolveSplitIdentity SplitIdentityResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" "$BUILD/ReplayIndirectSplit.slang" -I Frontier/Engine/Rendering/Shaders \
        -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly \
        -DFRONTIER_REPLAY_PATH_BUDGET=1 -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" "$BUILD/ReplayIndirectSplit.slang" -I Frontier/Engine/Rendering/Shaders \
    -entry ResolveReplayReservoirs -target spirv -profile spirv_1_3 -emit-spirv-directly \
    -DFRONTIER_REPLAY_PATH_BUDGET=1 -DFRONTIER_SPLIT_DIRECT_ONLY=1 -O0 -o "$BUILD/SplitDirectResolve.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" >/dev/null
{
    echo "Bounded temporal prototype; host $MODE; external Vulkan driver and shaders are not sanitizer-instrumented"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,PathReplaySampleHistory,MaterialRayScattering}.slang \
        Frontier/Engine/Rendering/{LightComputeFootprint.h,PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp,PathReplayVulkan.h,PathReplayVulkan.cpp} \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayDomainValidation.cpp,PathReplayVulkanValidation.cpp,ReplaySampleHistoryValidation.h,ReplaySeedReservoirValidation.h,ReplayCompactStorageValidation.h,ReplayBoundedTemporalValidation.h,ReplayPathBudgetValidation.h,ReplayCameraReprojectionValidation.h,ReplayRoomReference.h,ReplayCandidateValidation.h,ReplayReconstructionValidation.h,PathReplayGpuValidation.h} \
        Exhibits/LightCompute/RunReplayIndirectSplit.sh Exhibits/LightCompute/Code/ReplayFreshBudget.slang Exhibits/LightCompute/Code/ReplayPathBudgetImages.py \
        "$TOOLS"/SwiftShader/{libvulkan.so.1,libvk_swiftshader.so,vk_swiftshader_icd.json} \
        "$BUILD"/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherSampleHistory,ReplayFinalizeSampleHistory,ReplayFreshBudget}.spv \
        "$BUILD/plain"/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherSampleHistory,ReplayFinalizeSampleHistory}.spv
    sha256sum Exhibits/LightCompute/Code/{ReplayIndirectSplitValidation.h,CompileReplayIndirectSplit.py,CompileReplayResetBudget.py,CompileReplayBoundedTemporal.py,ReplayBoundedTemporalMeasure.cpp} \
        "$BUILD/ReplayIndirectSplit.slang" "$BUILD/whole/ReplayResetBudget.slang" \
        "$BUILD/whole"/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherSampleHistory,ReplayFinalizeSampleHistory,ReplayFreshBudget}.spv \
        "$BUILD"/{SplitIdentityGenerate,SplitIdentityResolve,SplitDirectResolve}.spv "$BUILD/LightComputeValidation"
    echo "Host sanitizer configuration uses Debug -O1 -g; external Vulkan/shaders are not instrumented"
    "$BUILD/ReplayBoundedTemporalMeasure"
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-indirect-split "$OUTPUT"
} 2>&1 | tee "$RESULTS"
