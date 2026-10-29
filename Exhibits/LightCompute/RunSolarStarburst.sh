#!/usr/bin/env bash
# 📝 Check isolated aperture spikes and mixed flare readbacks on Vulkan.
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
MODE=release
FLAGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    FLAGS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
           '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/solar-starburst-$MODE"
OUTPUT="$ROOT/_ClaudeScratch/solar-starburst-$MODE"
mkdir -p "$BUILD" "$OUTPUT" Exhibits/LightCompute/{Images,Results}
for PAIR in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'TracePrimarySurfaces Primary' \
            'ResolveSolarVisibility SolarVisibility' 'CompositeSolarOptics SolarOptics'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry "$ENTRY" -target spirv \
        -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${FLAGS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target LightComputeValidation >/dev/null
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/SolarStarburst-$MODE.txt}"
mkdir -p "$(dirname "$RESULTS")"
{
    printf 'Host build: %s; external Vulkan driver is not sanitizer-instrumented\n' "$MODE"
    printf 'Platform: '; uname -sm
    COMPILER="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$BUILD/CMakeCache.txt")"
    printf 'C++ compiler: '; "$COMPILER" --version 2>&1 | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,SolarOptics}.slang \
              Frontier/Engine/Rendering/{SolarOpticsPayload,LensFlareModel}.h \
              Exhibits/LightCompute/Code/{LightComputeValidation.cpp,SolarOpticsValidation.h,SolarStarburstValidation.h}
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --solar-starburst "$OUTPUT"
    if [[ "$MODE" == release ]]; then
        "$PYTHON" - "$OUTPUT" "$ROOT/Exhibits/LightCompute/Images" <<'PY'
import hashlib
import pathlib
import sys
from PIL import Image
source, destination = map(pathlib.Path, sys.argv[1:])
hashes = set()
for name in ('StarburstSix', 'StarburstFive', 'StarburstMixed'):
    with Image.open(source / (name + '.ppm')) as image:
        rgb = image.convert('RGB')
        assert rgb.size == (256, 256)
        pixels = rgb.tobytes()
        digest = hashlib.sha256(pixels).hexdigest()
        assert digest not in hashes
        hashes.add(digest)
        path = destination / (name + '.png')
        rgb.save(path, optimize=True)
    with Image.open(path) as check:
        assert check.convert('RGB').tobytes() == pixels
    print(f'{name}.png 256x256 RGB_SHA256={digest}')
PY
    elif [[ -f "$ROOT/_ClaudeScratch/solar-starburst-release/StarburstSix.ppm" ]]; then
        for NAME in StarburstSix StarburstFive StarburstMixed; do
            cmp "$OUTPUT/$NAME.ppm" "$ROOT/_ClaudeScratch/solar-starburst-release/$NAME.ppm"
            printf '%s sanitized/release PPM byte-identical\n' "$NAME"
        done
    fi
} | tee "$RESULTS"
