#!/usr/bin/env bash
# 🧩 Audit fresh sensitive Slang/Vulkan fixtures without changing historical evidence or production defaults.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
CMAKE="${CMAKE:-$ROOT/_ClaudeScratch/venv/bin/cmake}"
CTEST="${CTEST:-$ROOT/_ClaudeScratch/venv/bin/ctest}"
SLANG="$TOOLS/Slang/slangtorch/bin/slangc"
DRIVER="$TOOLS/SwiftShader/libvulkan.so.1"
export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
MODE=release
OPTIONS=(-DCMAKE_BUILD_TYPE=Release)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    MODE=sanitized
    OPTIONS=(-DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie'
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie')
    export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
fi
BUILD="$ROOT/_ClaudeScratch/build/facet-area-occlusion-gpu-$MODE"
mkdir -p "$BUILD"
{
    printf 'OCCLUSION_GPU_BUILD mode=%s parent=%s working_changes=1 driver_sanitized=0\n' "$MODE" "$(git rev-parse HEAD)"
    "${CXX:-c++}" --version
    python Automation/PrepareReplayTools.py --verify
    "$SLANG" -version
    sha256sum Automation/{PrepareReplayTools.py,ReplayToolDependencies.json} "$DRIVER" "$SLANG" \
        Exhibits/LightCompute/Code/FacetAreaOcclusionGpu* Exhibits/LightCompute/FacetAreaOcclusionGpu/CMakeLists.txt \
        Exhibits/LightCompute/RunFacetAreaOcclusionGpu.sh \
        Exhibits/LightCompute/Code/{LightComputeValidation.cpp,LightComputeFormat.h,RectangleDomainValidation.h,FacetReservoirDescriptor.h,GeometricAreaReservoirGpu.slang,RgbTraceGpu.slang,FacetAreaOcclusionValidation.cpp} \
        Frontier/Engine/Rendering/*.{h,cpp} Frontier/Engine/Rendering/Shaders/*.slang \
        Engine/Render/{MaterialPreviewGeometry,ScatteringSequence,PaneThickness,DielectricRefraction}.h
    for PAIR in 'GenerateOcclusionPayload DiffuseGenerate' 'ResolveOcclusionPayload DiffuseResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" Exhibits/LightCompute/Code/FacetAreaOcclusionGpu.slang \
            -I Frontier/Engine/Rendering/Shaders -I Exhibits/LightCompute/Code -entry "$ENTRY" \
            -target spirv -profile spirv_1_3 -emit-spirv-directly -fp-mode precise -O0 -o "$BUILD/$STEM.spv"
    done
    sha256sum "$BUILD"/*.spv
    "$CMAKE" -S Exhibits/LightCompute/FacetAreaOcclusionGpu -B "$BUILD" "${OPTIONS[@]}"
    "$CMAKE" --build "$BUILD" --target FacetAreaOcclusionGpuValidation FacetAreaOcclusionValidation \
        FacetAreaProposalValidation FacetAreaPortValidation FacetAreaScoreValidation FacetAreaTransportValidation --parallel 1
    "$CTEST" --test-dir "$BUILD" -V --output-on-failure
    "$BUILD/FacetAreaOcclusionGpuValidation" "$DRIVER" "$BUILD" "$BUILD/captures"
    sha256sum "$BUILD/FacetAreaOcclusionGpuSubmission.h" "$BUILD"/captures/*.f32
} 2>&1 | tee "Exhibits/LightCompute/Results/FacetAreaOcclusionGpu-$MODE.txt"
