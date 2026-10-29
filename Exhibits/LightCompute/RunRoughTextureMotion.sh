#!/usr/bin/env bash
# 📝 Compare moving rough-textured primary rays and multibounce replay with independent CPU paths.
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
MODE=release
FLAGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    FLAGS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
           '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/rough-texture-motion-$MODE"
mkdir -p "$BUILD" Exhibits/LightCompute/Results
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${FLAGS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target LightComputeValidation >/dev/null
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/RoughTextureMotion-$MODE.txt}"
mkdir -p "$(dirname "$RESULTS")"
{
    printf 'Host build: %s; external Vulkan driver is not sanitizer-instrumented\n' "$MODE"
    printf 'Platform: '; uname -sm
    COMPILER="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$BUILD/CMakeCache.txt")"
    printf 'C++ compiler: '; "$COMPILER" --version 2>&1 | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir}.slang \
              Frontier/Engine/Rendering/{PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp} \
              Frontier/Engine/Rendering/CMakeLists.txt \
              Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayCapture.h} \
              Exhibits/LightCompute/Code/{ReplayRoomReference.h,ReplayMotionValidation.h,ReplayTextureValidation.h} \
              Exhibits/LightCompute/Code/{ReplayTextureMotionValidation.h,RoughTextureReplayValidation.h} \
              Exhibits/LightCompute/Code/RoughTextureMotionValidation.h
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --rough-texture-motion
} | tee "$RESULTS"
