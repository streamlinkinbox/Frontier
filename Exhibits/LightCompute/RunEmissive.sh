#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
Cmake="${CMAKE:-cmake}"
if ! command -v "$Cmake" >/dev/null && [[ -x ExternalPackages/BuildTools/cmake/data/bin/cmake ]]; then
 Cmake="$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake"
fi
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build/light-compute"
if [[ "${PREPARE:-0}" == 1 ]]; then python3 Exhibits/LightCompute/Code/Prepare.py; fi
Slang="${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
mkdir -p "$Build" _ClaudeScratch/light-compute
for Pair in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'ReuseLightNeighbours Reuse' 'ReconstructLightHistory Reconstruct' 'FilterLightNeighbours Filter' 'FinalizeLightHistory Finalize' 'TracePrimarySurfaces Primary' 'TraceSurfaceTransport Transport' 'ReprojectSurfaceTransport TransportTemporal' 'FilterSurfaceTransport TransportSpatial' 'ResolveSurfaceTransport TransportResolve' 'BakeCloudRadiance Clouds' 'IntegrateFogScattering Fog'; do
 read -r Entry Stem <<< "$Pair"
 "$Slang" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry "$Entry" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Build/$Stem.spv"
done
node Exhibits/VehicleReference/Code/Materials.mjs
"$Cmake" -S Exhibits/VehicleReference -B _ClaudeScratch/build/vehicle-reference-cmake -DCMAKE_BUILD_TYPE=Release
"$Cmake" --build _ClaudeScratch/build/vehicle-reference-cmake --parallel 2 --target VehicleCapture
_ClaudeScratch/build/vehicle-reference-cmake/VehicleCapture "$PWD/_ClaudeScratch/vehicle-primary" --primary-input
_ClaudeScratch/build/vehicle-reference-cmake/VehicleCapture "$PWD/_ClaudeScratch/primary-motion" --primary-motion
"$Cmake" -S Exhibits/LightCompute -B "$Build" -DCMAKE_BUILD_TYPE=Release
"$Cmake" --build "$Build" --parallel 2
"$Build/LightComputeValidation" "$Driver" "$Build" _ClaudeScratch/vehicle-primary/VehiclePrimary.bin _ClaudeScratch/light-compute --emissive-only | tee Exhibits/LightCompute/Results/EmissiveArea.txt
"$Build/LightComputeValidation" "$Driver" "$Build" _ClaudeScratch/primary-motion/VehicleMotion0.bin _ClaudeScratch/light-compute --emissive-motion _ClaudeScratch/primary-motion | tee Exhibits/LightCompute/Results/EmissiveMotion.txt
python3 Exhibits/LightCompute/Code/EmissiveImages.py
bash Exhibits/LightCompute/RunTransportReconstruction.sh
