#!/usr/bin/env bash
# 📝 Compare guarded rough opaque replay against independent hemispherical quadrature.
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
fi
BUILD="$ROOT/_ClaudeScratch/build/rough-replay-$MODE"
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
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/RoughReplay-$MODE.txt}"
mkdir -p "$(dirname "$RESULTS")"
{
    printf 'Host build: %s; external Vulkan driver is not sanitizer-instrumented\n' "$MODE"
    "$SLANG" -version
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir}.slang \
              Frontier/Engine/Rendering/{PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp} \
              Frontier/Engine/Rendering/CMakeLists.txt \
              Exhibits/LightCompute/Code/{LightComputeValidation.cpp,ReplayRoomReference.h,RoughReplayValidation.h}
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --rough-replay
} | tee "$RESULTS"
