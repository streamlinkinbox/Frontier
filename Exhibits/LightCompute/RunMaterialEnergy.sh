#!/usr/bin/env bash
#============================================================================================================================================
#                                                         RUNMATERIALENERGY.SH
#============================================================================================================================================
# 🧩 Exercise native quadrature, actual shared Slang and the pinned pre-correction shader.
set -euo pipefail
cd "$(dirname "$0")/../.."
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build"
Scratch="$PWD/_ClaudeScratch/tmp/MaterialEnergy"
Results="$PWD/Exhibits/TextureStudio/Results"
mkdir -p "$Build" "$Scratch" "$Results"
# Restore the pinned tools with Code/Prepare.py first if absent. Reuse an already completed build only when explicitly requested.
if [[ "${ENERGY_BUILD:-1}" == 1 ]]; then BUILD_ONLY=1 bash Exhibits/LightCompute/RunFog.sh; fi
c++ -std=c++20 -O2 -Wall -Wextra -Werror -I. Exhibits/TextureStudio/Code/MaterialEnergyValidation.cpp -o "$Build/MaterialEnergyValidation"
"$Build/MaterialEnergyValidation" "$Results/MaterialEnergyAcceptance.txt"
c++ -std=c++20 -O2 -Wall -Wextra -Werror -I. Exhibits/TextureStudio/Code/MaterialEnergyBaseline.cpp -o "$Build/MaterialEnergyCurrent"
"$Build/MaterialEnergyCurrent" > "$Results/MaterialEnergyCurrent.txt"
git show b6bc49e:Engine/Render/Shader/ScatteringEvaluation.slang > "$Scratch/ScatteringEnergyOriginal.slang"
sed "s|../../../Engine/Render/Shader/ScatteringEvaluation.slang|$Scratch/ScatteringEnergyOriginal.slang|" \
 Exhibits/LightCompute/Code/MaterialEnergy.slang > "$Scratch/MaterialEnergyOriginal.slang"
Slang="${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}"
for Variant in Current Original; do
 Directory="$Build/MaterialEnergy${Variant}Gpu"
 mkdir -p "$Directory"
 cp "$Build/light-compute/"*.spv "$Directory/"
 Source="$PWD/Exhibits/LightCompute/Code/MaterialEnergy.slang"
 if [[ "$Variant" == Original ]]; then Source="$Scratch/MaterialEnergyOriginal.slang"; fi
 "$Slang" "$Source" -entry IntegrateMaterialEnergy -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Directory/Receivers.spv"
done
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
"$Build/light-compute/LightComputeValidation" "$Driver" "$Build/MaterialEnergyOriginalGpu" --material-energy-original \
 | tee "$Results/MaterialEnergyGpuOriginal.txt"
"$Build/light-compute/LightComputeValidation" "$Driver" "$Build/MaterialEnergyCurrentGpu" --material-energy \
 | tee "$Results/MaterialEnergyGpuCurrent.txt"
