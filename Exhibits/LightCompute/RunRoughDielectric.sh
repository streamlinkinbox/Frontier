#!/usr/bin/env bash
#============================================================================================================================================
#                                                        RUNROUGHDIELECTRIC.SH
#============================================================================================================================================
# 🧩 Verify sampled rough reflections through closed glass against the pinned blocked route and existing transport regressions.
set -euo pipefail
cd "$(dirname "$0")/../.."
MIRROR_BUILD="${ROUGH_DIELECTRIC_BUILD:-1}" bash Exhibits/LightCompute/RunMirrorDielectric.sh
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build/light-compute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
"$Build/LightComputeValidation" "$Driver" "$Build" --rough-dielectric "$PWD/_ClaudeScratch/light-compute" \
 | tee Exhibits/LightCompute/Results/RoughDielectric.txt
Original="$PWD/_ClaudeScratch/build/RoughDielectricOriginalGpu"
mkdir -p "$Original" _ClaudeScratch/tmp/RoughDielectricOriginal
cp "$Build/"*.spv "$Build/MaterialSceneMaps.bin" "$Original/"
python3 - <<'PYTHON'
from pathlib import Path
import subprocess
for Name in ('LightInfluence.slang','MaterialRayTransport.slang','SolidDielectricTransport.slang'):
    Source = subprocess.check_output(['git','show','213f050:Frontier/Engine/Rendering/Shaders/'+Name],text=True)
    Source = Source.replace('../../../../Engine/',str(Path('Engine').resolve())+'/')
    Path('_ClaudeScratch/tmp/RoughDielectricOriginal',Name).write_text(Source)
PYTHON
"${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}" _ClaudeScratch/tmp/RoughDielectricOriginal/LightInfluence.slang \
 -I Frontier/Engine/Rendering/Shaders -entry TraceSurfaceTransport -target spirv -profile spirv_1_3 \
 -emit-spirv-directly -O0 -o "$Original/Transport.spv"
"$Build/LightComputeValidation" "$Driver" "$Original" --rough-dielectric-original "$PWD/_ClaudeScratch/light-compute" \
 | tee Exhibits/LightCompute/Results/RoughDielectricOriginal.txt
"$Build/LightComputeValidation" "$Driver" "$Build" --rough-reflections "$PWD/_ClaudeScratch/light-compute" \
 | tee Exhibits/LightCompute/Results/RoughDielectricReflectionRegression.txt
python3 Exhibits/LightCompute/Code/RoughDielectricImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
