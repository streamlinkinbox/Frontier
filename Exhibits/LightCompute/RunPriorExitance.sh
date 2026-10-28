#!/usr/bin/env bash
#============================================================================================================================================
#                                                       RUNPRIOREXITANCE.SH
#============================================================================================================================================
# 🧩 Verify prior-screen exitance pyramid, rigid authored motion, DistanceFieldSpace clearance and six-pose unified vehicle captures.
set -euo pipefail
cd "$(dirname "$0")/../.."
CMake="${CMAKE:-$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake}"
Slang="${SLANGC:-$PWD/ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc}"
Driver="${VULKAN_LIBRARY:-$PWD/ExternalPackages/VulkanCompute/SwiftShader/libvk_swiftshader.so}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$PWD/ExternalPackages/VulkanCompute/SwiftShader/vk_swiftshader_icd.json"; fi
Build="$PWD/_ClaudeScratch/build/light-compute"
Sanitized="$PWD/_ClaudeScratch/build/light-compute-sanitized"
Pinned="$PWD/_ClaudeScratch/build/light-compute-3e8259c"
Motion="$PWD/_ClaudeScratch/primary-motion"
Saved="$PWD/_ClaudeScratch/vehicle-glazing/SavedDielectric.bin"
Out="$PWD/_ClaudeScratch/unified-exitance"
Results="$PWD/Exhibits/LightCompute/Results"
mkdir -p "$Build" "$Sanitized" "$Pinned/Shaders" "$Out"
# Prerequisites: Prepare.py, PrepareMaterialScene.py and the vehicle capture chain of Run.sh must already have produced $Motion and $Saved.
for Pair in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'ReuseLightNeighbours Reuse' 'ReconstructLightHistory Reconstruct' 'FilterLightNeighbours Filter' 'FinalizeLightHistory Finalize' 'TracePrimarySurfaces Primary' 'TraceSurfaceTransport Transport' 'ReprojectSurfaceTransport TransportTemporal' 'FilterSurfaceTransport TransportSpatial' 'ResolveSurfaceTransport TransportResolve' 'BakeCloudRadiance Clouds' 'IntegrateFogScattering Fog' 'CondenseExitancePyramid ExitancePyramid'; do
  read -r Entry Stem <<< "$Pair"
  "$Slang" Frontier/Engine/Rendering/Shaders/LightInfluence.slang -entry "$Entry" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Build/$Stem.spv"
done
"$CMake" -S Exhibits/LightCompute -B "$Build" -DCMAKE_BUILD_TYPE=Release
"$CMake" --build "$Build" -j2
"$Build/LightComputeValidation" "$Driver" "$Build" --prior-exitance-native "$Motion/VehicleMotion0.bin" "$Out" | tee "$Results/PriorExitanceNative.txt"
# Pinned negative control: 3e8259c shaders (no ExitancePyramid; cold reset on authored motion).
for File in LightInfluence.slang MaterialRayScattering.slang MaterialRayTransport.slang SolidDielectricTransport.slang; do
  git show "3e8259c:Frontier/Engine/Rendering/Shaders/$File" > "$Pinned/Shaders/$File"
done
cp "$Build/MaterialSceneMaps.bin" "$Pinned/"
for Pair in 'AssignLightCells Cells' 'EvaluateLightReceivers Receivers' 'ReuseLightNeighbours Reuse' 'ReconstructLightHistory Reconstruct' 'FilterLightNeighbours Filter' 'FinalizeLightHistory Finalize' 'TracePrimarySurfaces Primary' 'TraceSurfaceTransport Transport' 'ReprojectSurfaceTransport TransportTemporal' 'FilterSurfaceTransport TransportSpatial' 'ResolveSurfaceTransport TransportResolve' 'BakeCloudRadiance Clouds' 'IntegrateFogScattering Fog'; do
  read -r Entry Stem <<< "$Pair"
  "$Slang" "$Pinned/Shaders/LightInfluence.slang" -entry "$Entry" -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Pinned/$Stem.spv"
done
"$Build/LightComputeValidation" "$Driver" "$Pinned" --prior-exitance-original "$Motion/VehicleMotion0.bin" "$Out" | tee "$Results/PriorExitanceOriginal.txt"
# Sanitized native checks.
"$CMake" -S Exhibits/LightCompute -B "$Sanitized" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
"$CMake" --build "$Sanitized" -j2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$Sanitized/LightComputeValidation" "$Driver" "$Build" --prior-exitance-native "$Motion/VehicleMotion0.bin" "$Out" | tee "$Results/PriorExitanceSanitized.txt"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$Sanitized/LightComputeValidation" "$Driver" "$Build" --unified-scene-native "$Motion" "$Out" "$Saved" | tee -a "$Results/PriorExitanceSanitized.txt"
# GPU acceptance (SwiftShader CPU Vulkan, type 4). Run serially; each pass takes several minutes.
"$Build/LightComputeValidation" "$Driver" "$Build" --prior-exitance "$Motion/VehicleMotion0.bin" "$Out" > "$Results/PriorExitance.txt" 2>&1
"$Build/LightComputeValidation" "$Driver" "$Build" --unified-scene "$Motion" "$Out" "$Saved" > "$Results/UnifiedScene.txt" 2>&1
cp "$Out/PriorExitanceMetrics.json" "$Results/PriorExitanceMetrics.json"
cp "$Out/UnifiedSceneMetrics.json" "$Results/UnifiedSceneMetrics.json"
python3 Exhibits/LightCompute/Code/PriorExitanceImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
