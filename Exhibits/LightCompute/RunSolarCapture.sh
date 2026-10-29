#!/usr/bin/env bash
# 📝 Capture a disclosed procedural scene with the actual Vulkan solar passes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
BUILD="$ROOT/_ClaudeScratch/build/solar-capture"
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null && [[ -x "$ROOT/_ClaudeScratch/venv/bin/cmake" ]]; then CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
PYTHON="${PYTHON:-python3}"
if ! "$PYTHON" -c 'import PIL' >/dev/null 2>&1 && [[ -x "$ROOT/_ClaudeScratch/venv/bin/python" ]]; then PYTHON="$ROOT/_ClaudeScratch/venv/bin/python"; fi
if [[ "${PREPARE:-0}" == 1 ]]; then python3 Exhibits/LightCompute/Code/Prepare.py; fi
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
mkdir -p "$BUILD" Exhibits/LightCompute/{Images,Results}
for PAIR in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'TracePrimarySurfaces Primary' \
            'ResolveSolarVisibility SolarVisibility' 'CompositeSolarOptics SolarOptics'; do
    read -r ENTRY STEM <<< "$PAIR"
    "$SLANG" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry "$ENTRY" -target spirv \
        -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/$STEM.spv"
done
"$CMAKE" -S Exhibits/LightCompute -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
"$CMAKE" --build "$BUILD" --parallel 2 --target LightComputeValidation >/dev/null
{
    "$SLANG" -version
    sha256sum Frontier/Engine/Rendering/Shaders/{LightInfluence,SolarOptics}.slang \
              Exhibits/LightCompute/Code/{LightComputeValidation.cpp,SolarCaptureGpu.h}
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD" --solar-capture "$BUILD/SolarCapture.ppm"
    "$PYTHON" - "$BUILD/SolarCapture.ppm" Exhibits/LightCompute/Images/SolarCaptureVulkan.png <<'PY'
from PIL import Image
import hashlib, sys
source = Image.open(sys.argv[1]).convert('RGB')
source.save(sys.argv[2], optimize=True)
with Image.open(sys.argv[2]) as check:
    check.load()
    assert check.size == (640, 360) and check.convert('RGB').tobytes() == source.tobytes()
print(f'PNG verified decoded RGB SHA256={hashlib.sha256(source.tobytes()).hexdigest()}')
PY
} | tee Exhibits/LightCompute/Results/SolarCapture-vulkan.txt
