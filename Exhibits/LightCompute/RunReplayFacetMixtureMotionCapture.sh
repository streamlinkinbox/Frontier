#!/usr/bin/env bash
# 🧩 Capture moving radiant triangles and check the capture helper's default arguments.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then
    CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"
fi
if [[ -z "${PYTHON:-}" ]]; then
    if [[ -x "$ROOT/_ClaudeScratch/venv/bin/python" ]]; then PYTHON="$ROOT/_ClaudeScratch/venv/bin/python"; else PYTHON=python3; fi
fi
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
CHECK="${CHECK:---capture-replay-mixture-motion}"
case "$CHECK" in
    --capture-replay-mixture-motion) RESULT_STEM=ReplayFacetMixtureMotionCapture ;;
    --capture-replay-default-parity) RESULT_STEM=ReplayFacetMixtureMotionCaptureDefaultParity ;;
    *) printf 'Unknown moving-radiant capture check\n' >&2; exit 1 ;;
esac
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-facet-mixture-motion-$MODE"
OUTPUT="${OUTPUT:-$ROOT/_ClaudeScratch/replay-facet-mixture-motion-capture-$MODE}"
if [[ "$MODE" == release ]]; then IMAGES="${IMAGES:-$ROOT/Exhibits/LightCompute/Images}";
else IMAGES="${IMAGES:-$OUTPUT/png}"; fi
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/$RESULT_STEM-$MODE.txt}"
mkdir -p "$BUILD" "$OUTPUT" "$IMAGES" "$(dirname "$RESULTS")"
for PAIR in 'GenerateReplayReservoirs ReplayGenerate' 'ResolveReplayReservoirs ReplayResolve'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" --target LightComputeValidation >/dev/null
{
    echo "Moving-radiant replay capture $CHECK; host $MODE; external driver and shaders are not sanitizer-instrumented"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    uname -srm
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,MaterialRayScattering}.slang \
        Frontier/Engine/Rendering/{LightComputeFootprint.h,PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp,PathReplayVulkan.h,PathReplayVulkan.cpp,RadiantFacetDistribution.h,RadiantFacetDistribution.cpp} \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayVulkanValidation.cpp,PathReplayCapture.h,ReplayMotionValidation.h,ReplayTextureValidation.h,ReplayTextureMotionValidation.h,RoughTextureReplayValidation.h,ReplayFacetMixtureCapture.h,ReplayFacetMixtureMotionValidation.h,ReplayFacetMixtureMotionCapture.h} \
        Exhibits/LightCompute/RunReplayFacetMixtureMotionCapture.sh
    if [[ "$CHECK" == --capture-replay-default-parity ]]; then
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" "$CHECK"
    else
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" "$CHECK" "$OUTPUT"
        "$PYTHON" - "$OUTPUT" "$IMAGES" <<'PY'
import hashlib
import pathlib
import sys
from PIL import Image
source, destination = map(pathlib.Path, sys.argv[1:])
hashes = set()
for stem in ('ReplayRadiantMotionBefore', 'ReplayRadiantMotionMoved', 'ReplayRadiantMotionBlocked',
             'ReplayRadiantMotionReopened', 'ReplayRadiantMotionMovedFirst', 'ReplayRadiantMotionBlockedFirst',
             'ReplayRadiantMotionReopenedFirst'):
    for suffix in ('', 'Preview'):
        name = stem + suffix
        with Image.open(source / (name + '.ppm')) as image:
            rgb = image.convert('RGB')
            assert rgb.size == (128, 160)
            pixels = rgb.tobytes()
            digest = hashlib.sha256(pixels).hexdigest()
            assert digest not in hashes
            hashes.add(digest)
            path = destination / (name + '.png')
            rgb.save(path, optimize=True)
        with Image.open(path) as check:
            assert check.convert('RGB').tobytes() == pixels
        print(f'{name}.png 128x160 RGB_SHA256={digest}')
PY
    fi
} 2>&1 | tee "$RESULTS"
