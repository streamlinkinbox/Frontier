#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
# Reuse the pinned compiler, native ControlVehicle preparation and thirteen Slang entries.
BUILD_ONLY=1 bash Exhibits/LightCompute/RunFog.sh
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build/light-compute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
"$Build/LightComputeValidation" "$Driver" "$Build" _ClaudeScratch/primary-motion/VehicleMotion0.bin _ClaudeScratch/light-compute --fog-sampling | tee Exhibits/LightCompute/Results/FogSampling.txt
python3 Exhibits/LightCompute/Code/FogSamplingImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
