#!/usr/bin/env bash
# 📝 Check the optional GGX proposal against independent integrals and a bounded multibounce reference.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if [[ "${PREPARE:-0}" == 1 ]]; then python3 Exhibits/LightCompute/Code/Prepare.py; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
CHECK="${CHECK:---replay-importance}"
case "$CHECK" in
    --replay-importance) RESULT_STEM=ReplayImportance ;;
    --replay-importance-motion) RESULT_STEM=ReplayImportanceMotion ;;
    --replay-importance-parity) RESULT_STEM=ReplayImportanceParity ;;
    *) echo "Unknown replay check" >&2; exit 1 ;;
esac
MODE=release
CMAKE_OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    CMAKE_OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
           '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-importance-$MODE"
mkdir -p "$BUILD" Exhibits/LightCompute/Results
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
if [[ "$CHECK" == --replay-importance-parity ]]; then
    PREVIOUS="$BUILD/previous"
    mkdir -p "$PREVIOUS"
    git show 3c8a286:Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang > "$PREVIOUS/PathReplayReservoir.slang"
    for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" "$PREVIOUS/PathReplayReservoir.slang" -I Frontier/Engine/Rendering/Shaders \
            -entry "$ENTRY" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$PREVIOUS/$STEM.spv"
    done
    cp "$BUILD/Primary.spv" "$PREVIOUS/Primary.spv"
fi
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${CMAKE_OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target LightComputeValidation >/dev/null
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/$RESULT_STEM-$MODE.txt}"
mkdir -p "$(dirname "$RESULTS")"
{
    printf 'Host build: %s; external Vulkan driver is not sanitizer-instrumented\n' "$MODE"
    printf 'Platform: '; uname -sm
    COMPILER="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$BUILD/CMakeCache.txt")"
    printf 'C++ compiler: '; "$COMPILER" --version 2>&1 | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering}.slang \
              Frontier/Engine/Rendering/{LightComputeFootprint.h,PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp,PathReplayVulkan.h,PathReplayVulkan.cpp,CMakeLists.txt} \
              Engine/Render/Shader/ScatteringEvaluation.slang \
              Exhibits/LightCompute/Code/{LightComputeValidation.cpp,ReplayImportanceValidation.h,ReplayImportanceMotionValidation.h,ReplayImportanceParityValidation.h,ReplayRoomReference.h,RoughReplayValidation.h,RoughTextureReplayValidation.h,RoughTextureMotionValidation.h,ReplayTextureValidation.h,ReplayTextureMotionValidation.h}
    if [[ "$CHECK" == --replay-importance-parity ]]; then
        printf 'Previous checkpoint: 3c8a286\n'
        sha256sum "$PREVIOUS/PathReplayReservoir.slang"
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" "$CHECK" "$PREVIOUS"
    else
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" "$CHECK"
    fi
} | tee "$RESULTS"
