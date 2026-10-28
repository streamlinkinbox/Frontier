#!/usr/bin/env bash
#============================================================================================================================================
#                                                      RUNPANETRANSMITTANCE.SH
#============================================================================================================================================
# 🧩 Verify thin-pane area-emitter quadrature, eight-pane BSDF continuation, multi-stratum mixtures and volumetric fog shafts.
set -euo pipefail
cd "$(dirname "$0")/../.."
Cmake="${CMAKE:-$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake}"
Ctest="${CTEST:-$PWD/ExternalPackages/BuildTools/cmake/data/bin/ctest}"
Tools="$PWD/ExternalPackages/VulkanCompute"
Slang="${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}"
Build="$PWD/_ClaudeScratch/build/light-compute"
Sanitized="$PWD/_ClaudeScratch/build/pane-transmittance-sanitized"
Capture="$PWD/_ClaudeScratch/pane-transmittance"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
mkdir -p "$Build" "$Capture"
if [[ "${PANE_BUILD:-1}" == 1 ]]; then
 PYTHONPATH="$PWD/_ClaudeScratch/tmp/MaterialTools${PYTHONPATH:+:$PYTHONPATH}" python3 Exhibits/LightCompute/Code/PrepareMaterialScene.py
 for Pair in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'ReuseLightNeighbours Reuse' 'ReconstructLightHistory Reconstruct' 'FilterLightNeighbours Filter' 'FinalizeLightHistory Finalize' 'TracePrimarySurfaces Primary' 'TraceSurfaceTransport Transport' 'ReprojectSurfaceTransport TransportTemporal' 'FilterSurfaceTransport TransportSpatial' 'ResolveSurfaceTransport TransportResolve' 'BakeCloudRadiance Clouds' 'IntegrateFogScattering Fog' 'CondenseExitancePyramid ExitancePyramid'; do
  read -r Entry Stem <<< "$Pair"
  "$Slang" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry "$Entry" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Build/$Stem.spv"
 done
 "$Cmake" -S Exhibits/LightCompute -B "$Build" -DCMAKE_BUILD_TYPE=Release
 "$Cmake" --build "$Build" -j2
 "$Cmake" -S Exhibits/LightCompute -B "$Sanitized" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
 "$Cmake" --build "$Sanitized" -j2
fi
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
 "$Sanitized/LightComputeValidation" "$Driver" "$Build" --pane-transmittance-native \
 "$Capture" "$PWD/_ClaudeScratch/glazing-motion/SavedDielectric.bin" \
 | tee Exhibits/LightCompute/Results/PaneTransmittanceSanitized.txt
"$Ctest" --test-dir _ClaudeScratch/build/vehicle-reference-cmake --output-on-failure \
 | tee Exhibits/LightCompute/Results/PaneTransmittanceCtest.txt
"$Build/LightComputeValidation" "$Driver" "$Build" --pane-transmittance \
 "$Capture" "$PWD/_ClaudeScratch/glazing-motion/SavedDielectric.bin" \
 | tee Exhibits/LightCompute/Results/PaneTransmittance.txt
Original="$PWD/_ClaudeScratch/build/PaneTransmittanceOriginalGpu"
mkdir -p "$Original" _ClaudeScratch/tmp/PaneTransmittanceOriginal
cp "$Build/"*.spv "$Build/MaterialSceneMaps.bin" "$Original/"
python3 - <<'PYTHON'
from pathlib import Path
import subprocess
for Name in ('LightInfluence.slang','MaterialRayTransport.slang','SolidDielectricTransport.slang'):
    Source = subprocess.check_output(['git','show','f5d99d4:Frontier/Engine/Rendering/Shaders/'+Name],text=True)
    Source = Source.replace('../../../../Engine/',str(Path('Engine').resolve())+'/')
    Path('_ClaudeScratch/tmp/PaneTransmittanceOriginal',Name).write_text(Source)
PYTHON
for Pair in 'TraceSurfaceTransport Transport' 'IntegrateFogScattering Fog'; do
 read -r Entry Stem <<< "$Pair"
 "$Slang" _ClaudeScratch/tmp/PaneTransmittanceOriginal/LightInfluence.slang \
  -I Frontier/Engine/Rendering/Shaders -entry "$Entry" -target spirv -profile spirv_1_3 \
  -emit-spirv-directly -O0 -o "$Original/$Stem.spv"
done
"$Build/LightComputeValidation" "$Driver" "$Original" --pane-transmittance-original \
 "$Capture" "$PWD/_ClaudeScratch/glazing-motion/SavedDielectric.bin" \
 | tee Exhibits/LightCompute/Results/PaneTransmittanceOriginal.txt
{
 "$Build/LightComputeValidation" "$Driver" "$Build" --window-dielectric "$PWD/_ClaudeScratch/light-compute"
 "$Build/LightComputeValidation" "$Driver" "$Build" --diffuse-dielectric "$PWD/_ClaudeScratch/light-compute"
 "$Build/LightComputeValidation" "$Driver" "$Build" _ClaudeScratch/primary-motion/VehicleMotion0.bin "$PWD/_ClaudeScratch/light-compute" --emissive-only
 "$Build/LightComputeValidation" "$Driver" "$Build" _ClaudeScratch/primary-motion/VehicleMotion0.bin "$PWD/_ClaudeScratch/light-compute" --fog-facet-checks
 "$Build/LightComputeValidation" "$Driver" "$Build" _ClaudeScratch/primary-motion/VehicleMotion0.bin "$PWD/_ClaudeScratch/light-compute" --fog-checks
 "$Build/LightComputeValidation" "$Driver" "$Build" --vehicle-glazing _ClaudeScratch/primary-motion/VehicleMotion0.bin "$PWD/_ClaudeScratch/vehicle-glazing" "$PWD/_ClaudeScratch/glazing-motion/SavedDielectric.bin"
} | tee Exhibits/LightCompute/Results/PaneTransmittanceRegression.txt
python3 Exhibits/LightCompute/Code/PaneTransmittanceImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
