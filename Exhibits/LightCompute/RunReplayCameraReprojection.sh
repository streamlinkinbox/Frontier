#!/usr/bin/env bash
# 🧩 Verify exact diffuse camera reprojection, disocclusion and scene-revision resets.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-camera-reprojection-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayCameraReprojection-$MODE.txt}"
OUTPUT="${OUTPUT:-$ROOT/_ClaudeScratch/replay-camera-capture-$MODE}"
IMAGES="${IMAGES:-$OUTPUT/png}"
if [[ -z "${PYTHON:-}" ]]; then
    if [[ -x "$ROOT/_ClaudeScratch/venv/bin/python" ]]; then PYTHON="$ROOT/_ClaudeScratch/venv/bin/python"; else PYTHON=python3; fi
fi
mkdir -p "$BUILD" "$(dirname "$RESULTS")"
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
for PAIR in 'GatherReplayReconstruction ReplayGatherReconstruction' 'FinalizeReplayReconstruction ReplayFinalizeReconstruction'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReconstruction.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" \
    --target LightComputeValidation PathReplayDomainValidation PathReplayVulkanValidation >/dev/null
{
    echo "Strict diffuse camera reprojection; host $MODE; external Vulkan driver and shaders are not sanitizer-instrumented"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,PathReplayReconstruction,MaterialRayScattering}.slang \
        Frontier/Engine/Rendering/{LightComputeFootprint.h,PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp,PathReplayVulkan.h,PathReplayVulkan.cpp} \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayDomainValidation.cpp,PathReplayVulkanValidation.cpp,ReplayCameraReprojectionValidation.h,ReplayCandidateValidation.h,ReplayReconstructionValidation.h,ReplayRoomReference.h,PathReplayGpuValidation.h,PathReplayCapture.h} \
        Exhibits/LightCompute/RunReplayCameraReprojection.sh
    "$BUILD/PathReplayDomainValidation"
    "$BUILD/PathReplayVulkanValidation"
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-camera-reprojection
    if [[ "${CAPTURE:-0}" == 1 ]]; then
        mkdir -p "$OUTPUT" "$IMAGES"
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --capture-replay-camera "$OUTPUT"
        "$PYTHON" - "$OUTPUT" "$IMAGES" <<'PY'
import hashlib
from pathlib import Path
import sys
from PIL import Image
source, destination = map(Path, sys.argv[1:])
for name in ('ReplayCameraFresh', 'ReplayCameraReprojected', 'ReplayCameraAcceptance'):
    with Image.open(source / (name + '.ppm')) as image:
        rgb = image.convert('RGB')
        assert rgb.size == (64, 64)
        pixels = rgb.tobytes()
        rgb.save(destination / (name + '.png'), optimize=True)
    with Image.open(destination / (name + '.png')) as decoded:
        assert decoded.convert('RGB').tobytes() == pixels
    print(f'{name}.png 64x64 RGB_SHA256={hashlib.sha256(pixels).hexdigest()}')
PY
    fi
} 2>&1 | tee "$RESULTS"
