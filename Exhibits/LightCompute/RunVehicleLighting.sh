#!/usr/bin/env bash
# 📝 Render retained ControlVehicle inputs through existing exhibit passes;no production wiring.
set -euo pipefail
cd "$(dirname "$0")/../.."
Python="${PYTHON:-$PWD/_ClaudeScratch/venv/bin/python}"
CMake="${CMAKE:-$PWD/_ClaudeScratch/venv/bin/cmake}"
Slang="${SLANGC:-$PWD/ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc}"
Driver="${VULKAN_LIBRARY:-$PWD/ExternalPackages/VulkanCompute/SwiftShader/libvk_swiftshader.so}"
Build="$PWD/_ClaudeScratch/build/vehicle-lighting"
Input="$PWD/_ClaudeScratch/vehicle-lighting-inputs"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$PWD/ExternalPackages/VulkanCompute/SwiftShader/vk_swiftshader_icd.json}"
"$Python" Exhibits/LightCompute/Code/VehicleLightingInputs.py
"$CMake" -S Exhibits/LightCompute/VehicleLighting -B "$Build" -DCMAKE_BUILD_TYPE=Release \
    -DVulkanHeaders="$PWD/ExternalPackages/Vulkan-Headers-1.4.321/include"
"$CMake" --build "$Build" --target VehicleLightingCapture VehicleLightingArchive -j2
"$Build/VehicleLightingArchive" "$Input" --primary-motion > Exhibits/LightCompute/Results/VehicleLighting-archive.txt
cp _ClaudeScratch/build/light-compute/MaterialSceneMaps.bin "$Build/"
for Pair in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'ReuseLightNeighbours Reuse' \
 'ReconstructLightHistory Reconstruct' 'FilterLightNeighbours Filter' 'FinalizeLightHistory Finalize' \
 'TracePrimarySurfaces Primary' 'TraceSurfaceTransport Transport' 'ReprojectSurfaceTransport TransportTemporal' \
 'FilterSurfaceTransport TransportSpatial' 'ResolveSurfaceTransport TransportResolve' 'BakeCloudRadiance Clouds' \
 'IntegrateFogScattering Fog' 'CondenseExitancePyramid ExitancePyramid' \
 'ResolveSolarVisibility SolarVisibility' 'CompositeSolarOptics SolarOptics'; do
    read -r Entry Stem <<< "$Pair"
    "$Slang" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry "$Entry" -target spirv \
        -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Build/$Stem.spv"
done
{
    git rev-parse HEAD
    "$Slang" -version
    sha256sum Exhibits/LightCompute/Code/VehicleLightingCapture.cpp "$Build"/*.spv "$Input/VehicleMotion0.bin"
    "$Build/VehicleLightingCapture" "$Driver" "$Build" "$Input/VehicleMotion0.bin" "$Build/captures"
} 2>&1 | tee Exhibits/LightCompute/Results/VehicleLighting-render.txt
"$Python" Exhibits/LightCompute/Code/VehicleLightingImages.py
