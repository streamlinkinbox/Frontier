#!/usr/bin/env bash
#============================================================================================================================================
#                                                        RUNVEHICLEGLAZING.SH
#============================================================================================================================================
# 🧩 Verify finite vehicle panes, native saved coefficients and actual Vulkan readbacks.
set -euo pipefail
cd "$(dirname "$0")/../.."
Build="$PWD/_ClaudeScratch/build/vehicle-glazing"
Capture="$PWD/_ClaudeScratch/vehicle-glazing"
mkdir -p "$Build" "$Capture"
Cmake="${CMAKE:-$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake}"
Ctest="${CTEST:-$PWD/ExternalPackages/BuildTools/cmake/data/bin/ctest}"
if [[ "${GLAZING_BUILD:-1}" == 1 ]]; then
 PYTHONPATH="$PWD/_ClaudeScratch/tmp/MaterialTools${PYTHONPATH:+:$PYTHONPATH}" python3 Exhibits/LightCompute/Code/PrepareMaterialScene.py
 BUILD_ONLY=1 CMAKE="$Cmake" bash Exhibits/LightCompute/RunFog.sh
fi
Options=(-std=c++20 -Wall -Wextra -Werror -I. -IFrontier)
"${CXX:-c++}" "${Options[@]}" -O2 Exhibits/LightCompute/Code/PaneThicknessValidation.cpp -o "$Build/PaneThicknessValidation"
"$Build/PaneThicknessValidation" _ClaudeScratch/primary-motion/VehicleMotion0.bin \
 | tee Exhibits/LightCompute/Results/PaneThickness.txt
"${CXX:-c++}" "${Options[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
 Exhibits/LightCompute/Code/PaneThicknessValidation.cpp -o "$Build/PaneThicknessSanitized"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$Build/PaneThicknessSanitized" \
 _ClaudeScratch/primary-motion/VehicleMotion0.bin | tee Exhibits/LightCompute/Results/PaneThicknessSanitized.txt
"${CXX:-c++}" "${Options[@]}" -O1 -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -Wno-missing-field-initializers -IExternalPackages/tomlpp/include -IExternalPackages/stb \
 Exhibits/LightCompute/Code/SavedDielectricAuthoring.cpp Engine/Render/SurfaceMaterial.cpp \
 Engine/ContentInterchange/MaterialAuthoring.cpp Engine/Render/MaterialProceduralBaking.cpp \
 Engine/Render/StbImageImplementation.cpp -o "$Build/SavedDielectricAuthoring"
"$Build/SavedDielectricAuthoring" "$PWD" "$Capture" "$PWD/Exhibits/LightCompute/Materials/VehicleGlazing.material" \
 | tee Exhibits/LightCompute/Results/VehicleGlazingAuthoring.txt
"$Cmake" --build _ClaudeScratch/build/light-compute -j2
"$Cmake" --build _ClaudeScratch/build/vehicle-reference-cmake -j2
"$Ctest" --test-dir _ClaudeScratch/build/vehicle-reference-cmake --output-on-failure \
 | tee Exhibits/LightCompute/Results/VehicleGlazingNative.txt
Tools="$PWD/ExternalPackages/VulkanCompute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
_ClaudeScratch/build/light-compute/LightComputeValidation "$Driver" "$PWD/_ClaudeScratch/build/light-compute" \
 --vehicle-glazing "$PWD/_ClaudeScratch/primary-motion/VehicleMotion0.bin" "$Capture" "$Capture/SavedDielectric.bin" \
 | tee Exhibits/LightCompute/Results/VehicleGlazingGpu.txt
PYTHONPATH="$PWD/_ClaudeScratch/tmp/MaterialTools${PYTHONPATH:+:$PYTHONPATH}" python3 Exhibits/LightCompute/Code/VehicleGlazingImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
