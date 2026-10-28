#!/usr/bin/env bash
# 📝 Capture settled and first-transition textured replay renders from Vulkan readback.
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
BUILD="$ROOT/_ClaudeScratch/build/replay-texture-capture"
OUTPUT="$ROOT/_ClaudeScratch/texture-room"
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
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplayTexture-capture.txt}"
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
              Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayCapture.h,ReplayTextureValidation.h,ReplayTextureCapture.h}
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --capture-replay-texture "$OUTPUT"
    "$PYTHON" - "$OUTPUT" "$ROOT/Exhibits/LightCompute/Images" <<'PY'
import hashlib
import pathlib
import sys
from PIL import Image
source, destination = map(pathlib.Path, sys.argv[1:])
hashes = set()
for name in ('TextureRoomInitial', 'TextureRoomChanged', 'TextureRoomBlocked', 'TextureRoomReopened',
             'TextureRoomChangedFirst', 'TextureRoomBlockedFirst', 'TextureRoomReopenedFirst',
             'TextureRoomInitialPreview', 'TextureRoomChangedPreview', 'TextureRoomBlockedPreview',
             'TextureRoomReopenedPreview', 'TextureRoomChangedFirstPreview', 'TextureRoomBlockedFirstPreview',
             'TextureRoomReopenedFirstPreview'):
    with Image.open(source / (name + '.ppm')) as image:
        rgb = image.convert('RGB')
        assert rgb.size == (256, 160)
        digest = hashlib.sha256(rgb.tobytes()).hexdigest()
        assert digest not in hashes
        hashes.add(digest)
        rgb.save(destination / (name + '.png'), optimize=True)
        print(f'{name}.png {rgb.width}x{rgb.height} RGB_SHA256={digest}')
PY
} | tee "$RESULTS"
