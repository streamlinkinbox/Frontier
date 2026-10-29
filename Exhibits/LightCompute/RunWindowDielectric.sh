#!/usr/bin/env bash
#============================================================================================================================================
#                                                        RUNWINDOWDIELECTRIC.SH
#============================================================================================================================================
# 🧩 Validate thin-window/closed-volume composition, retained sheet energy and the pinned blocked transmission route.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROUGH_DIELECTRIC_BUILD="${WINDOW_BUILD:-1}" bash Exhibits/LightCompute/RunRoughDielectric.sh
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build/light-compute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
"$Build/LightComputeValidation" "$Driver" "$Build" --window-dielectric "$PWD/_ClaudeScratch/light-compute" \
 | tee Exhibits/LightCompute/Results/WindowDielectric.txt
Original="$PWD/_ClaudeScratch/build/WindowDielectricOriginalGpu"
mkdir -p "$Original" _ClaudeScratch/tmp/WindowDielectricOriginal
cp "$Build/"*.spv "$Build/MaterialSceneMaps.bin" "$Original/"
python3 - <<'PYTHON'
from pathlib import Path
import subprocess
for Name in ('LightInfluence.slang','MaterialRayTransport.slang','SolidDielectricTransport.slang'):
    Source = subprocess.check_output(['git','show','abed7c6:Frontier/Engine/Rendering/Shaders/'+Name],text=True)
    Source = Source.replace('../../../../Engine/',str(Path('Engine').resolve())+'/')
    Path('_ClaudeScratch/tmp/WindowDielectricOriginal',Name).write_text(Source)
PYTHON
"${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}" _ClaudeScratch/tmp/WindowDielectricOriginal/LightInfluence.slang \
 -I Frontier/Engine/Rendering/Shaders -entry TraceSurfaceTransport -target spirv -profile spirv_1_3 \
 -emit-spirv-directly -O0 -o "$Original/Transport.spv"
"$Build/LightComputeValidation" "$Driver" "$Original" --window-dielectric-original "$PWD/_ClaudeScratch/light-compute" \
 | tee Exhibits/LightCompute/Results/WindowDielectricOriginal.txt
python3 Exhibits/LightCompute/Code/WindowDielectricImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
