#!/usr/bin/env bash
# 🧩 Validate subpixel sample-seed retracing and conservative history intake.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
CMAKE="${CMAKE:-cmake}"
CTEST="${CTEST:-ctest}"
if ! command -v "$CMAKE" >/dev/null; then
    for CANDIDATE in "$ROOT/_ClaudeScratch/venv/bin/cmake" "$ROOT/_ClaudeScratch/pytools/bin/cmake"; do
        if [[ -x "$CANDIDATE" ]]; then CMAKE="$CANDIDATE"; break; fi
    done
fi
if ! command -v "$CTEST" >/dev/null; then
    for CANDIDATE in "$ROOT/_ClaudeScratch/venv/bin/ctest" "$ROOT/_ClaudeScratch/pytools/bin/ctest"; do
        if [[ -x "$CANDIDATE" ]]; then CTEST="$CANDIDATE"; break; fi
    done
fi
if [[ -d "$ROOT/_ClaudeScratch/pytools/bin" ]]; then export PATH="$ROOT/_ClaudeScratch/pytools/bin:$PATH"; fi
if [[ -d "$ROOT/_ClaudeScratch/pytools" ]]; then export PYTHONPATH="$ROOT/_ClaudeScratch/pytools${PYTHONPATH:+:$PYTHONPATH}"; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
fi
BUILD="$ROOT/_ClaudeScratch/build/replay-sample-history-$MODE"
RESULTS="${RESULTS:-$ROOT/Exhibits/LightCompute/Results/ReplaySampleHistory-$MODE.txt}"
OUTPUT="${OUTPUT:-$ROOT/_ClaudeScratch/replay-sample-capture-$MODE}"
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
for PAIR in 'GatherReplaySampleHistory ReplayGatherSampleHistory' 'FinalizeReplaySampleHistory ReplayFinalizeSampleHistory'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/PathReplaySampleHistory.slang \
        -I Frontier/Engine/Rendering/Shaders -entry "$ENTRY" -target spirv -profile spirv_1_3 \
        -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry TracePrimarySurfaces -target spirv \
    -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/Primary.spv"
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" "${OPTIONS[@]}" >/dev/null
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-2}" >/dev/null
{
    echo "Subpixel seed retracing; host $MODE; external Vulkan driver and shaders are not sanitizer-instrumented"
    echo "Base $(git rev-parse --short HEAD), plus working changes if any; source hashes below."
    uname -srm
    "${CXX:-c++}" --version | sed -n '1p'
    "$SLANG" -version 2>&1
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,PathReplayReservoir,PathReplaySampleHistory,MaterialRayScattering}.slang \
        Frontier/Engine/Rendering/{LightComputeFootprint.h,PathReplayFootprint.h,PathReplayDomain.h,PathReplayDomain.cpp,PathReplayVulkan.h,PathReplayVulkan.cpp} \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,PathReplayDomainValidation.cpp,PathReplayVulkanValidation.cpp,ReplaySampleHistoryValidation.h,ReplayCameraReprojectionValidation.h,ReplayRoomReference.h,ReplayCandidateValidation.h,ReplayReconstructionValidation.h,PathReplayGpuValidation.h} \
        Exhibits/LightCompute/RunReplaySampleHistory.sh \
        "$TOOLS"/SwiftShader/{libvulkan.so.1,libvk_swiftshader.so,vk_swiftshader_icd.json} \
        "$BUILD"/{Primary,ReplayGenerate,ReplayResolve,ReplayGatherSampleHistory,ReplayFinalizeSampleHistory}.spv
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --replay-sample-history
    if [[ "${CAPTURE:-0}" == 1 ]]; then
        mkdir -p "$OUTPUT" "$IMAGES"
        "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --capture-replay-sample-history "$OUTPUT"
        "$PYTHON" - "$OUTPUT" "$IMAGES" <<'PY'
import hashlib
from pathlib import Path
import sys
from PIL import Image
source, destination = map(Path, sys.argv[1:])
for name in ('ReplaySampleFresh', 'ReplaySampleFourFresh', 'ReplaySampleRetraced', 'ReplaySampleAcceptance'):
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
