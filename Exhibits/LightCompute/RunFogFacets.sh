#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
# Restore the native vehicle and compile all thirteen shared Slang entries without replacing archived captures.
BUILD_ONLY=1 bash Exhibits/LightCompute/RunFog.sh
Cmake="${CMAKE:-cmake}"
if ! command -v "$Cmake" >/dev/null && [[ -x ExternalPackages/BuildTools/cmake/data/bin/cmake ]]; then
 Cmake="$PWD/ExternalPackages/BuildTools/cmake/data/bin/cmake"
fi
Tools="$PWD/ExternalPackages/VulkanCompute"
Build="$PWD/_ClaudeScratch/build/light-compute"
Sanitizers="$PWD/_ClaudeScratch/build/light-compute-asan"
Driver="${VULKAN_LIBRARY:-$Tools/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$Tools/SwiftShader/vk_swiftshader_icd.json"; fi
"$Cmake" -S Exhibits/LightCompute -B "$Sanitizers" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
 '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
 '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
"$Cmake" --build "$Sanitizers" --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$Sanitizers/LightComputeValidation" \
 "$Driver" "$Build" _ClaudeScratch/primary-motion/VehicleMotion0.bin _ClaudeScratch/light-compute \
 --fog-facet-checks | tee Exhibits/LightCompute/Results/FogFacetSanitizers.txt
"$Build/LightComputeValidation" "$Driver" "$Build" _ClaudeScratch/primary-motion/VehicleMotion0.bin \
 _ClaudeScratch/light-compute --fog-facets | tee Exhibits/LightCompute/Results/FogFacets.txt
python3 Exhibits/LightCompute/Code/FogFacetImages.py
python3 Exhibits/LightCompute/Code/Evidence.py
