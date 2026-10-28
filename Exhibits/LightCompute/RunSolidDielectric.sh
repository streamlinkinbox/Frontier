#!/usr/bin/env bash
#============================================================================================================================================
#                                                        RUNSOLIDDIELECTRIC.SH
#============================================================================================================================================
# 🧩 Rebuild production compute shaders, compare Snell boundaries and render closed-volume slab acceptance.
set -euo pipefail
cd "$(dirname "$0")/../.."
if [[ "${SOLID_BUILD:-1}" == 1 ]]; then
 python3 Exhibits/LightCompute/Code/PrepareMaterialScene.py
 BUILD_ONLY=1 bash Exhibits/LightCompute/RunFog.sh
fi
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build/light-compute"
Numeric="$PWD/_ClaudeScratch/build/DielectricBoundaryGpu"
Capture="$PWD/_ClaudeScratch/light-compute"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
mkdir -p "$Numeric" "$Capture"
cp "$Build/"*.spv "$Numeric/"
"${SLANGC:-$Tools/Slang/slangtorch/bin/slangc}" Exhibits/LightCompute/Code/DielectricBoundary.slang \
 -entry EvaluateDielectricBoundary -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$Numeric/Receivers.spv"
"$Build/LightComputeValidation" "$Driver" "$Numeric" --dielectric-boundaries \
 | tee Exhibits/LightCompute/Results/DielectricBoundary.txt
"$Build/LightComputeValidation" "$Driver" "$Build" --solid-dielectric "$Capture" \
 | tee Exhibits/LightCompute/Results/SolidDielectric.txt
"$Build/LightComputeValidation" "$Driver" "$Build" --specular-history "$Capture" \
 | tee Exhibits/LightCompute/Results/SolidDielectricOpaqueRegression.txt
Cmake="${CMAKE:-cmake}"
if ! command -v "$Cmake" >/dev/null; then Cmake="$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake"; fi
"$Cmake" --build _ClaudeScratch/build/vehicle-reference-cmake --parallel 2
Ctest="${CTEST:-ctest}"
if ! command -v "$Ctest" >/dev/null; then Ctest="$PWD/ExternalPackages/BuildTools/cmake/data/bin/ctest"; fi
"$Ctest" --test-dir _ClaudeScratch/build/vehicle-reference-cmake --output-on-failure \
 | tee Exhibits/LightCompute/Results/SolidDielectricNative.txt
python3 Exhibits/LightCompute/Code/SolidDielectricImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
