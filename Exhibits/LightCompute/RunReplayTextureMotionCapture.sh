#!/usr/bin/env bash
# 📝 Capture settled and first-transition moving textured replay renders from Vulkan readback.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if [[ -z "${PYTHON:-}" ]]; then
    if [[ -x "$ROOT/_ClaudeScratch/venv/bin/python" ]]; then PYTHON="$ROOT/_ClaudeScratch/venv/bin/python"; else PYTHON=python3; fi
fi
if [[ "${PREPARE:-0}" == 1 ]]; then python3 Exhibits/LightCompute/Code/Prepare.py; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
BUILD="$ROOT/_ClaudeScratch/build/replay-texture-motion-capture"
OUTPUT="$ROOT/_ClaudeScratch/texture-motion-room"
mkdir -p "$BUILD" "$OUTPUT" Exhibits/LightCompute/{Images,Results}
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target LightComputeValidation >/dev/null
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayTextureMotion-capture.txt}"
mkdir -p "$(dirname "$RESULTS")"
{
    printf 'Host build: release; external Vulkan driver is not sanitizer-instrumented\n'
    printf 'Platform: '; uname -sm
    COMPILER="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$BUILD/CMakeCache.txt")"
    printf 'C++ compiler: '; "$COMPILER" --version 2>&1 | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir}.slang \
              Frontier/Engine/Rendering/{PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp} \
              Frontier/Engine/Rendering/CMakeLists.txt \
              Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayCapture.h,ReplayMotionValidation.h,ReplayMotionCapture.h,ReplayTextureValidation.h,ReplayTextureMotionValidation.h,ReplayTextureMotionCapture.h}
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --capture-texture-motion "$OUTPUT"
    "$PYTHON" - "$OUTPUT" "$ROOT/Exhibits/LightCompute/Images" <<'PY'
import hashlib
import pathlib
import sys
from PIL import Image
source, destination = map(pathlib.Path, sys.argv[1:])
hashes = set()
for stem in ('TexturedMotionBefore', 'TexturedMotionMoved', 'TexturedMotionBlocked', 'TexturedMotionReopened',
             'TexturedMotionMovedFirst', 'TexturedMotionBlockedFirst', 'TexturedMotionReopenedFirst'):
    for suffix in ('', 'Preview'):
        name = stem + suffix
        with Image.open(source / (name + '.ppm')) as image:
            rgb = image.convert('RGB')
            assert rgb.size == (256, 160)
            pixels = rgb.tobytes()
            digest = hashlib.sha256(pixels).hexdigest()
            assert digest not in hashes
            hashes.add(digest)
            path = destination / (name + '.png')
            rgb.save(path, optimize=True)
        with Image.open(path) as check:
            assert check.convert('RGB').tobytes() == pixels
        print(f'{name}.png 256x160 RGB_SHA256={digest}')
PY
} | tee "$RESULTS"
