#!/usr/bin/env bash
#============================================================================================================================================
#                                                       RUNROUGHREFLECTIONS.SH
#============================================================================================================================================
# 🧩 Build actual rough-lobe screen/BVH comparisons, a pinned sharp negative control and retained image evidence.
set -euo pipefail
cd "$(dirname "$0")/../.."
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build"
Scratch="$PWD/_ClaudeScratch/tmp/SharpReflections"
Capture="$PWD/_ClaudeScratch/light-compute"
mkdir -p "$Scratch" "$Build/SharpReflections" "$Capture"
if [[ "${REFLECTION_BUILD:-1}" == 1 ]]; then
 python3 Exhibits/LightCompute/Code/PrepareMaterialScene.py
 BUILD_ONLY=1 bash Exhibits/LightCompute/RunFog.sh
fi
cp "$Build/light-compute/"*.spv "$Build/SharpReflections/"
cp "$Build/light-compute/MaterialSceneMaps.bin" "$Build/SharpReflections/"
git show 05e4090:Frontier/Engine/Rendering/Shaders/MaterialRayTransport.slang > "$Scratch/MaterialRayTransport.slang"
git show 05e4090:Frontier/Engine/Rendering/Shaders/LightInfluence.slang > "$Scratch/LightInfluence.slang"
python3 - "$Scratch" <<'PY'
from pathlib import Path
import sys
Directory = Path(sys.argv[1])
Source = Directory / 'LightInfluence.slang'
Source.write_text(Source.read_text().replace('#include "MaterialRayTransport.slang"',
                  '#include "' + str(Directory / 'MaterialRayTransport.slang') + '"'))
PY
"${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}" "$Scratch/LightInfluence.slang" -I Frontier/Engine/Rendering/Shaders \
 -entry TraceSurfaceTransport -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Build/SharpReflections/Transport.spv"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
"$Build/light-compute/LightComputeValidation" "$Driver" "$Build/light-compute" --rough-reflections "$Capture" \
 | tee Exhibits/LightCompute/Results/RoughReflection.txt
"$Build/light-compute/LightComputeValidation" "$Driver" "$Build/SharpReflections" --rough-reflections-sharp "$Capture" \
 | tee Exhibits/LightCompute/Results/RoughReflectionSharp.txt
python3 Exhibits/LightCompute/Code/RoughReflectionImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
