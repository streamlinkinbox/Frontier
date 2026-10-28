#!/usr/bin/env bash
#============================================================================================================================================
#                                                        RUNSAVEDDIELECTRIC.SH
#============================================================================================================================================
# 🧩 Verify native TOML authoring and compiled coefficients with actual Vulkan slab captures and vehicle edge checks.
set -euo pipefail
cd "$(dirname "$0")/../.."
Build="$PWD/_ClaudeScratch/build/saved-dielectric"
Capture="$PWD/_ClaudeScratch/saved-dielectric"
mkdir -p "$Build" "$Capture"
Sources=(Exhibits/LightCompute/Code/SavedDielectricAuthoring.cpp Engine/Render/SurfaceMaterial.cpp
 Engine/ContentInterchange/MaterialAuthoring.cpp Engine/Render/MaterialProceduralBaking.cpp
 Engine/Render/StbImageImplementation.cpp)
Options=(-std=c++20 -O1 -ffunction-sections -fdata-sections -Wl,--gc-sections
 -Wall -Wextra -Werror -Wno-missing-field-initializers -I. -IEngine/Render
 -IExternalPackages/tomlpp/include -IExternalPackages/stb)
"${CXX:-c++}" "${Options[@]}" "${Sources[@]}" -o "$Build/SavedDielectricAuthoring"
"$Build/SavedDielectricAuthoring" "$PWD" "$Capture" | tee Exhibits/LightCompute/Results/SavedDielectricAuthoring.txt
"${CXX:-c++}" "${Options[@]}" -g -fsanitize=address,undefined -fno-omit-frame-pointer \
 "${Sources[@]}" -o "$Build/SavedDielectricSanitized"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$Build/SavedDielectricSanitized" "$PWD" "$Capture" \
 | tee Exhibits/LightCompute/Results/SavedDielectricSanitized.txt
# The old native decoder must reject the new volume table, not silently display a material as transmitted glass.
git show cec27e4:Engine/Render/SurfaceMaterial.cpp > "$Build/PreviousSurfaceMaterial.cpp"
Sources[1]="$Build/PreviousSurfaceMaterial.cpp"
"${CXX:-c++}" "${Options[@]}" "${Sources[@]}" -o "$Build/PreviousDielectricAuthoring"
if "$Build/PreviousDielectricAuthoring" "$PWD" "$Capture" > "$Capture/Previous.txt" 2>&1; then
 echo "Previous decoder unexpectedly accepted the new volume table" >&2; exit 1
fi
grep -Fx 'Unsupported material field: DielectricVolume' "$Capture/Previous.txt"
printf 'SAVED_DIELECTRIC_PREVIOUS revision=cec27e4 rejected_volume_table=1\n' \
 | tee Exhibits/LightCompute/Results/SavedDielectricPrevious.txt
python3 Exhibits/LightCompute/Code/VehicleGlassEdges.py _ClaudeScratch/primary-motion/VehicleMotion0.bin \
 Exhibits/LightCompute/Results/VehicleGlassEdges.json
CMake="${CMAKE:-$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake}"
"$CMake" --build _ClaudeScratch/build/light-compute -j2
Ctest="${CTEST:-$PWD/ExternalPackages/BuildTools/cmake/data/bin/ctest}"
"$Ctest" --test-dir _ClaudeScratch/build/vehicle-reference-cmake --output-on-failure \
 | tee Exhibits/LightCompute/Results/SavedDielectricNative.txt
Tools="$PWD/ExternalPackages/VulkanCompute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
_ClaudeScratch/build/light-compute/LightComputeValidation "$Driver" "$PWD/_ClaudeScratch/build/light-compute" \
 --saved-dielectric "$Capture" "$Capture/SavedDielectric.bin" \
 | tee Exhibits/LightCompute/Results/SavedDielectricGpu.txt
python3 Exhibits/LightCompute/Code/SavedDielectricImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
