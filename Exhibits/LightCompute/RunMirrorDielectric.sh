#!/usr/bin/env bash
#============================================================================================================================================
#                                                        RUNMIRRORDIELECTRIC.SH
#============================================================================================================================================
# 🧩 Verify mirror-to-volume transport and rerun primary glass, boundary arithmetic and opaque reflection acceptance.
set -euo pipefail
cd "$(dirname "$0")/../.."
SOLID_BUILD="${MIRROR_BUILD:-1}" bash Exhibits/LightCompute/RunSolidDielectric.sh
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build/light-compute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
"$Build/LightComputeValidation" "$Driver" "$Build" --mirror-dielectric "$PWD/_ClaudeScratch/light-compute" \
 | tee Exhibits/LightCompute/Results/MirrorDielectric.txt
Original="$PWD/_ClaudeScratch/build/MirrorDielectricOriginalGpu"
mkdir -p "$Original" "$PWD/_ClaudeScratch/tmp"
cp "$Build/"*.spv "$Build/MaterialSceneMaps.bin" "$Original/"
python3 - <<'PYTHON'
from pathlib import Path
import subprocess
Folder = Path('_ClaudeScratch/tmp/MirrorDielectricOriginal')
Folder.mkdir(parents=True,exist_ok=True)
for Name in ('LightInfluence.slang','MaterialRayTransport.slang','SolidDielectricTransport.slang'):
    Source = subprocess.check_output(['git','show','8a1f38f:Frontier/Engine/Rendering/Shaders/'+Name],text=True)
    Source = Source.replace('../../../../Engine/',str(Path('Engine').resolve())+'/')
    (Folder/Name).write_text(Source)
PYTHON
"${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}" _ClaudeScratch/tmp/MirrorDielectricOriginal/LightInfluence.slang \
 -I Frontier/Engine/Rendering/Shaders -entry TraceSurfaceTransport -target spirv -profile spirv_1_3 \
 -emit-spirv-directly -O0 -o "$Original/Transport.spv"
"$Build/LightComputeValidation" "$Driver" "$Original" --mirror-dielectric-original "$PWD/_ClaudeScratch/light-compute" \
 | tee Exhibits/LightCompute/Results/MirrorDielectricOriginal.txt
python3 Exhibits/LightCompute/Code/MirrorDielectricImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
