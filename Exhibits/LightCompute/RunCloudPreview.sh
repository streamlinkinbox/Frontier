#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
BUILD_ONLY=1 bash Exhibits/LightCompute/RunFog.sh
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build/light-compute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
Mode=--cloud-preview
Stem=CloudPreview
Options=()
if [[ "${COMPARE:-0}" == 1 ]]; then Mode=--cloud-budget; Stem=CloudBudget; Options=(--compare); fi
"$Build/LightComputeValidation" "$Driver" "$Build" _ClaudeScratch/primary-motion/VehicleMotion0.bin \
 _ClaudeScratch/light-compute "$Mode" | tee "Exhibits/LightCompute/Results/$Stem.txt"
python3 Exhibits/LightCompute/Code/CloudBudgetImages.py "${Options[@]}"
python3 Exhibits/LightCompute/Code/Evidence.py
