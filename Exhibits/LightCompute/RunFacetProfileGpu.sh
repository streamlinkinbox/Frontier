#!/usr/bin/env bash
# 🧩 Validate batched GPU-generated full perspective images without changing production allocations.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE:-$ROOT/_ClaudeScratch/venv/bin/cmake}"
CTEST="${CTEST:-$ROOT/_ClaudeScratch/venv/bin/ctest}"
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
SLANG="$TOOLS/Slang/slangtorch/bin/slangc"
DRIVER="$TOOLS/SwiftShader/libvulkan.so.1"
export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie'
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie')
    export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
fi
BUILD="$ROOT/_ClaudeScratch/build/facet-profile-gpu-$MODE"
mkdir -p "$BUILD"
{
    printf 'PROFILE_GPU_BUILD mode=%s parent=%s working_changes=1\n' "$MODE" "$(git rev-parse HEAD)"
    "${CXX:-c++}" --version
    python3 Automation/PrepareReplayTools.py --verify
    "$SLANG" -version
    sha256sum "$SLANG" "$DRIVER" Automation/{PrepareReplayTools.py,ReplayToolDependencies.json} \
        ExternalPackages/Vulkan-Headers-1.4.321/include/vulkan/{vulkan.h,vulkan_core.h,vk_platform.h} \
        Exhibits/LightCompute/Code/FacetProfileGpu{Sources.py,Validation.cpp} \
        Exhibits/LightCompute/FacetProfileGpu/CMakeLists.txt Exhibits/LightCompute/RunFacetProfileGpu.sh \
        Exhibits/LightCompute/Code/{AreaReconnectionValidation.h,FacetImageGpuSources.py,FacetPacketGpu.slang,FacetPacketGpuValidation.cpp,FacetPacketGpuSubmission.py,FacetCausalGpuValidation.cpp,LightComputeValidation.cpp,GeometricAreaReservoirGpu.slang,RgbTraceGpu.slang} \
        Frontier/Engine/Rendering/*.{h,cpp} Frontier/Engine/Rendering/Shaders/*.slang
    "$CMAKE" -S Exhibits/LightCompute/FacetProfileGpu -B "$BUILD" "${OPTIONS[@]}"
    for PAIR in 'GenerateFacetProfile DiffuseGenerate' 'ResolveFacetProfile DiffuseResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" "$BUILD/FacetProfileGpu.slang" -I Frontier/Engine/Rendering/Shaders \
            -I Exhibits/LightCompute/Code -entry "$ENTRY" -target spirv -profile spirv_1_3 \
            -emit-spirv-directly -fp-mode precise -O0 -o "$BUILD/$STEM.spv"
    done
    sha256sum "$BUILD"/*.spv "$BUILD"/*.h "$BUILD/FacetProfileGpu.slang"
    "$CMAKE" --build "$BUILD" --parallel 2
    "$CTEST" --test-dir "$BUILD" --verbose
    STATUS=0
    "$BUILD/FacetProfileGpuValidation" "$DRIVER" "$BUILD" "$BUILD/captures" || STATUS=$?
    sha256sum "$BUILD"/captures/*
    printf 'PROFILE_GPU_EXIT code=%s\n' "$STATUS"
    exit "$STATUS"
} 2>&1 | tee "Exhibits/LightCompute/Results/FacetProfileGpu-$MODE.txt"
