#!/usr/bin/env bash
#============================================================================================================================================
#                                                         RUNGLAZINGBOUNDS.SH
#============================================================================================================================================
# 🧩 Reproduce pinned previous triangle/budget failure and isolated guided-path budget comparisons.
set -euo pipefail
cd "$(dirname "$0")/../.."
Tools="$PWD/ExternalPackages/VulkanCompute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
for Mode in ${GLAZING_BOUNDS:-Previous 32 128 512 2048}; do
 Build="$PWD/_ClaudeScratch/build/GlazingBound$Mode"
 Source="$PWD/_ClaudeScratch/tmp/GlazingBound$Mode"
 mkdir -p "$Build" "$Source"
 cp _ClaudeScratch/build/light-compute/*.spv _ClaudeScratch/build/light-compute/MaterialSceneMaps.bin "$Build/"
 cp Frontier/Engine/Rendering/Shaders/*.slang "$Source/"
 python3 - "$Source" "$Mode" <<'PY'
from pathlib import Path
import subprocess
import sys
Destination = Path(sys.argv[1])
Mode = sys.argv[2]
if Mode == 'Previous':
    for Name in ('LightInfluence.slang', 'MaterialRayTransport.slang', 'SolidDielectricTransport.slang'):
        (Destination / Name).write_bytes(subprocess.check_output(['git', 'show', '250616f:Frontier/Engine/Rendering/Shaders/' + Name]))
else:
    assert Mode in ('32', '128', '512', '2048')
    Target = Destination / 'SolidDielectricTransport.slang'
    Target.write_text(Target.read_text().replace('Interaction < 2048', 'Interaction < ' + Mode))
for Source in Destination.glob('*.slang'):
    Source.write_text(Source.read_text().replace('../../../../Engine/', str(Path('Engine').resolve()) + '/'))
PY
 for Pair in 'TracePrimarySurfaces Primary' 'TraceSurfaceTransport Transport'; do
  read -r Entry Stem <<< "$Pair"
  "${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}" "$Source/LightInfluence.slang" -entry "$Entry" \
   -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Build/$Stem.spv"
 done
 printf 'GLAZING_BOUND source_override=%s (device line describes production source, not this override)\n' "$Mode" \
  | tee "Exhibits/LightCompute/Results/VehicleGlazingBound$Mode.txt"
 _ClaudeScratch/build/light-compute/LightComputeValidation "$Driver" "$Build" --vehicle-glazing-probe \
  "$PWD/_ClaudeScratch/primary-motion/VehicleMotion0.bin" "$PWD/_ClaudeScratch/vehicle-glazing" \
  "$PWD/_ClaudeScratch/vehicle-glazing/SavedDielectric.bin" \
  | tee -a "Exhibits/LightCompute/Results/VehicleGlazingBound$Mode.txt"
done
