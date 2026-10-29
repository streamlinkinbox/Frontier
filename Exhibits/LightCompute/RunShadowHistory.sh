#!/usr/bin/env bash
#============================================================================================================================================
#                                                        RUNSHADOWHISTORY.SH
#============================================================================================================================================
# 🧩 Exercise complete pole silhouettes and rough-reflection history through native C++ and shared Slang.
set -euo pipefail
cd "$(dirname "$0")/../.."
if [[ "${SHADOW_HISTORY_BUILD:-1}" == 1 ]]; then
 python3 Exhibits/LightCompute/Code/PrepareMaterialScene.py
 BUILD_ONLY=1 bash Exhibits/LightCompute/RunFog.sh
fi
Tools="$PWD/ExternalPackages/VulkanCompute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
Build="$PWD/_ClaudeScratch/build/light-compute"
Capture="$PWD/_ClaudeScratch/light-compute"
mkdir -p "$Capture"
"$Build/LightComputeValidation" "$Driver" "$Build" --pole-shadows "$Capture" | tee Exhibits/LightCompute/Results/PoleShadow.txt
"$Build/LightComputeValidation" "$Driver" "$Build" --specular-history-original "$Capture" | tee Exhibits/LightCompute/Results/SpecularHistoryOriginal.txt
"$Build/LightComputeValidation" "$Driver" "$Build" --specular-history "$Capture" | tee Exhibits/LightCompute/Results/SpecularHistory.txt
Cmake="${CMAKE:-cmake}"
if ! command -v "$Cmake" >/dev/null; then Cmake="$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake"; fi
"$Cmake" --build _ClaudeScratch/build/vehicle-reference-cmake --parallel 2
Ctest="${CTEST:-ctest}"
if ! command -v "$Ctest" >/dev/null; then Ctest="$(dirname "$Cmake")/ctest"; fi
"$Ctest" --test-dir _ClaudeScratch/build/vehicle-reference-cmake --output-on-failure \
 | tee Exhibits/LightCompute/Results/ShadowHistoryNative.txt
python3 Exhibits/LightCompute/Code/ShadowHistoryImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
