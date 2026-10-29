#!/usr/bin/env bash
# 🧩 Compile the Engine Hex8 solid to SPIR-V and execute it unchanged through Vulkan.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
if command -v cmake >/dev/null 2>&1; then CMAKE="$(command -v cmake)"; else CMAKE="$ROOT/_ClaudeScratch/venv/bin/cmake"; fi
if command -v ctest >/dev/null 2>&1; then CTEST="$(command -v ctest)"; else CTEST="$ROOT/_ClaudeScratch/venv/bin/ctest"; fi
PYTHON="${PYTHON:-$ROOT/_ClaudeScratch/venv/bin/python}"
TOOLS="${VULKAN_TOOLS:-$ROOT/ExternalPackages/VulkanCompute}"
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
LABEL=release
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    LABEL=sanitized
    CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Debug -DFRONTIER_TYRE_SANITIZERS=ON)
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
fi
BUILD="${BUILD:-$ROOT/_ClaudeScratch/build/tyre-solid-$LABEL}"
RESULTS="${RESULTS:-$ROOT/Exhibits/VehiclePhysics/Results}"
mkdir -p "$BUILD" "$RESULTS"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$TOOLS/SwiftShader/vk_swiftshader_icd.json}"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
{
    printf 'Engine Hex8 tyre solid direct Vulkan validation; %s\n' "$LABEL"
    printf 'Base %s, plus working changes if any; source hashes below.\n' "$(git rev-parse --short HEAD)"
    uname -srm
    "${CXX:-c++}" --version | head -1
    node --version
    "$PYTHON" -c 'import numpy; print("numpy", numpy.__version__)'
    [[ "$(git hash-object Experimental/VehicleHmiReference/Source/TyreContinuumElasticity.js)" == 0d62f516175845d2b842b73ff9ff491419451208 ]]
    sha256sum Frontier/Engine/Physics/{TyreContinuum.h,TyreContinuum.cpp,TyreProjectionDevice.h,TyreProjectionDevice.cpp,TyreProjectionVulkan.h,TyreProjectionVulkan.cpp,TyreSolidDevice.h,TyreSolidDevice.cpp,TyreSurfaceProjection.h,Shaders/TyreProjectionRecords.slang,Shaders/TyreSolidDevice.slang,Shaders/TyreSolidConstitutive.slang,Shaders/TyreSurfaceDevice.slang} \
        Exhibits/VehiclePhysics/Code/{TyreContinuumReference.mjs,TyreSolidDeviceReference.py,TyreSolidVulkanValidation.cpp,TyreSolidConstitutiveProbe.slang,TyreSurfaceDeviceProbe.slang} \
        Experimental/VehicleHmiReference/Source/TyreContinuumElasticity.js \
        "$TOOLS"/SwiftShader/{libvulkan.so.1,libvk_swiftshader.so,vk_swiftshader_icd.json}
    "$CMAKE" -S Exhibits/VehiclePhysics -B "$BUILD" -DFRONTIER_TYRE_DEVICE_VALIDATION=ON "${CMAKE_ARGS[@]}" >/dev/null
    "$CMAKE" --build "$BUILD" --target TyreSolidVulkanValidation TyreContinuumValidation --parallel "${BUILD_JOBS:-2}" >/dev/null
    "$CTEST" --test-dir "$BUILD" --output-on-failure -V
    if [[ "$LABEL" == sanitized ]]; then
        ldd "$BUILD/TyreSolidVulkanValidation" | grep -E 'libasan|libubsan'
    fi
    DEPENDENCIES="$("${CXX:-c++}" -std=c++17 -I Frontier -I "$TOOLS/Vulkan-Headers-1.4.321/include" -MM \
        Frontier/Engine/Physics/TyreSolidDevice.cpp Frontier/Engine/Physics/TyreProjectionVulkan.cpp)"
    if grep -Eq 'VehicleSolver|Jolt' <<<"$DEPENDENCIES"; then
        printf 'GPU tyre solid unexpectedly includes vehicle/Jolt dependencies\n' >&2
        exit 1
    fi
    printf 'TYRE_SOLID_DEPENDENCIES vehicle_header=0 jolt_header=0\n'
    for ENTRY in ResetTyreSolid AdvanceTyreSolid; do
        "$SLANG" Frontier/Engine/Physics/Shaders/TyreSolidDevice.slang -entry "$ENTRY" \
            -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/$ENTRY.spv"
    done
    "$SLANG" Exhibits/VehiclePhysics/Code/TyreSurfaceDeviceProbe.slang -I Frontier/Engine/Physics/Shaders \
        -entry CompareTyreSurface -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/TyreSurface.spv"
    "$SLANG" Exhibits/VehiclePhysics/Code/TyreSolidConstitutiveProbe.slang -I Frontier/Engine/Physics/Shaders \
        -entry EvaluateSolidStressProbes -target spirv -profile spirv_1_3 -emit-spirv-directly -O0 -o "$BUILD/TyreStress.spv"
    sha256sum "$BUILD/ResetTyreSolid.spv" "$BUILD/AdvanceTyreSolid.spv" "$BUILD/TyreSurface.spv" "$BUILD/TyreStress.spv"
    "$PYTHON" Exhibits/VehiclePhysics/Code/TyreSolidDeviceReference.py \
        "$BUILD/TyreSolidVulkanValidation" "$BUILD/ResetTyreSolid.spv" \
        "$BUILD/AdvanceTyreSolid.spv" "$BUILD/TyreSurface.spv" "$BUILD/TyreStress.spv" "$BUILD/fixtures"
} 2>&1 | tee "$RESULTS/TyreSolidDevice-$LABEL.txt"
