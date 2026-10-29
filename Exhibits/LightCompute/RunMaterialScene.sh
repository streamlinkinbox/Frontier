#!/usr/bin/env bash
#============================================================================================================================================
#                                                         RUNMATERIALSCENE.SH
#============================================================================================================================================
# 🧩 Build and render the native vehicle/material specimens through the shared Slang triangle-BVH renderer.
set -euo pipefail
cd "$(dirname "$0")/../.."
# Prerequisites: existing pinned Vulkan/Slang tools and Pillow 12.0.0 in _ClaudeScratch/tmp/MaterialTools.
python3 Exhibits/LightCompute/Code/PrepareMaterialScene.py
BUILD_ONLY=1 bash Exhibits/LightCompute/RunFog.sh
Tools="$PWD/ExternalPackages/VulkanCompute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
_ClaudeScratch/build/light-compute/LightComputeValidation "$Driver" _ClaudeScratch/build/light-compute \
 _ClaudeScratch/primary-motion/VehicleMotion0.bin _ClaudeScratch/light-compute --material-scene \
 | tee Exhibits/LightCompute/Results/MaterialScene.txt
python3 Exhibits/LightCompute/Code/MaterialSceneImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
