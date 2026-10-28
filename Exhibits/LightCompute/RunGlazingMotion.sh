#!/usr/bin/env bash
#============================================================================================================================================
#                                                         RUNGLAZINGMOTION.SH
#============================================================================================================================================
# 🧩 Rebuild static acceptance, then render six genuinely posed closed-glass vehicles with history/fresh references.
set -euo pipefail
cd "$(dirname "$0")/../.."
if [[ "${MOTION_BUILD:-1}" == 1 ]]; then
 GLAZING_BUILD=1 bash Exhibits/LightCompute/RunVehicleGlazing.sh
fi
Capture="$PWD/_ClaudeScratch/glazing-motion"
mkdir -p "$Capture"
cp Exhibits/LightCompute/Results/VehicleGlazingAuthoring.txt Exhibits/LightCompute/Results/GlazingMotionAuthoring.txt
Coefficients="${GLAZING_SPECTRA:-$PWD/_ClaudeScratch/vehicle-glazing/SavedDielectric.bin}"
Build="$PWD/_ClaudeScratch/build/light-compute"
Cmake="${CMAKE:-$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake}"
"$Cmake" --build "$Build" -j2
"$Build/LightComputeValidation" unused "$Build" --glazing-motion-native "$PWD/_ClaudeScratch/primary-motion" \
 "$Capture" "$Coefficients" | tee Exhibits/LightCompute/Results/GlazingMotionNative.txt
if [[ "${MOTION_SANITIZE:-1}" == 1 ]]; then
 "$Cmake" -S Exhibits/LightCompute -B _ClaudeScratch/build/glazing-motion-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g1'
 "$Cmake" --build _ClaudeScratch/build/glazing-motion-sanitized -j1
 ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  _ClaudeScratch/build/glazing-motion-sanitized/LightComputeValidation unused "$Build" --glazing-motion-native \
  "$PWD/_ClaudeScratch/primary-motion" "$Capture" "$Coefficients" \
  | tee Exhibits/LightCompute/Results/GlazingMotionSanitized.txt
fi
Tools="$PWD/ExternalPackages/VulkanCompute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
"$Build/LightComputeValidation" "$Driver" "$Build" --glazing-motion "$PWD/_ClaudeScratch/primary-motion" \
 "$Capture" "$Coefficients" | tee Exhibits/LightCompute/Results/GlazingMotionGpu.txt
bash Exhibits/LightCompute/RunFaintPath.sh
python3 Exhibits/LightCompute/Code/GlazingMotionImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
