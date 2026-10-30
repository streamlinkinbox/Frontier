#!/usr/bin/env bash
# 🧩 Validate selected-proposal Vulkan deformed causal parity and all previous native units.
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
BUILD="$ROOT/_ClaudeScratch/build/facet-causal-gpu-$MODE"
mkdir -p "$BUILD"
{
    printf 'CAUSAL_GPU_BUILD mode=%s parent=%s working_changes=1\n' "$MODE" "$(git rev-parse HEAD)"
    "${CXX:-c++}" --version
    sha256sum Frontier/Engine/Rendering/FacetArea{Reservoir,Proposals,Score,Transport}.{h,cpp} Frontier/Engine/Rendering/CMakeLists.txt \
        Exhibits/LightCompute/{FacetAreaTransport/CMakeLists.txt,Code/FacetAreaProposal{Validation.cpp,Scenes.py},Code/FacetAreaPortValidation.cpp,RunFacetAreaTransport.sh,Code/FacetAreaScoreValidation.cpp,Code/FacetAreaTransportValidation.cpp,Code/FacetAreaTransportScenes.py}
    sha256sum Exhibits/LightCompute/Code/{TriangleReconnection,RgbTrace,GeometricAreaReservoir,DeformedAreaReservoir}Validation.h \
        Exhibits/LightCompute/Code/{LightComputeFormat.h,PerspectiveAreaReservoirValidation.h} Frontier/Engine/Rendering/{TriangleVisibilitySpace,ClusterNesting,PathReplayFootprint}.h
    sha256sum Exhibits/LightCompute/Code/FacetAreaOcclusionValidation.cpp \
        Exhibits/LightCompute/FacetAreaOcclusion/CMakeLists.txt Exhibits/LightCompute/RunFacetAreaOcclusion.sh
    sha256sum Frontier/Engine/Rendering/*.{h,cpp} \
        ExternalPackages/Vulkan-Headers-1.4.321/include/vulkan/{vulkan.h,vulkan_core.h,vk_platform.h}
    python3 Automation/PrepareReplayTools.py --verify
    "$SLANG" -version
    sha256sum Automation/{PrepareReplayTools.py,ReplayToolDependencies.json} "$SLANG" "$DRIVER" \
        Exhibits/LightCompute/Code/{FacetCausalGpu.slang,FacetCausalGpuValidation.cpp,FacetAreaOcclusionGpuSubmission.py,LightComputeValidation.cpp,GeometricAreaReservoirGpu.slang,RgbTraceGpu.slang} \
        Exhibits/LightCompute/FacetCausalGpu/CMakeLists.txt Exhibits/LightCompute/RunFacetCausalGpu.sh \
        Frontier/Engine/Rendering/Shaders/*.slang Engine/Render/{MaterialPreviewGeometry,ScatteringSequence,PaneThickness,DielectricRefraction}.h
    for PAIR in 'GenerateCausalPayload DiffuseGenerate' 'ResolveCausalPayload DiffuseResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" Exhibits/LightCompute/Code/FacetCausalGpu.slang -I Frontier/Engine/Rendering/Shaders \
            -I Exhibits/LightCompute/Code -entry "$ENTRY" -target spirv -profile spirv_1_3 \
            -emit-spirv-directly -fp-mode precise -O0 -o "$BUILD/$STEM.spv"
    done
    sha256sum "$BUILD"/*.spv
    "$CMAKE" -S Exhibits/LightCompute/FacetCausalGpu -B "$BUILD" "${OPTIONS[@]}"
    sha256sum Frontier/Engine/Rendering/FacetAreaPacket.h Exhibits/LightCompute/Code/FacetPacketValidation.cpp \
        Exhibits/LightCompute/FacetPacket/CMakeLists.txt Exhibits/LightCompute/RunFacetPacket.sh
    sha256sum Exhibits/LightCompute/Code/FacetMixedValidation.cpp Exhibits/LightCompute/FacetMixed/CMakeLists.txt \
        Exhibits/LightCompute/RunFacetMixed.sh
    sha256sum Exhibits/LightCompute/Code/FacetNorm{Scenes.py,Validation.cpp} Exhibits/LightCompute/FacetNorm/CMakeLists.txt Exhibits/LightCompute/RunFacetNorm.sh
    sha256sum Exhibits/LightCompute/Code/FacetCausal{Scenes.py,Validation.cpp} Exhibits/LightCompute/FacetCausal/CMakeLists.txt Exhibits/LightCompute/RunFacetCausal.sh
    "$CMAKE" --build "$BUILD" --target FacetAreaProposalValidation FacetAreaPortValidation FacetAreaScoreValidation FacetAreaTransportValidation FacetAreaOcclusionValidation FacetPacketValidation FacetMixedValidation FacetNormValidation FacetCausalValidation FacetCausalGpuValidation --parallel 1
    "$BUILD/FacetCausalGpuValidation" --unit
    "$CTEST" --test-dir "$BUILD" --output-on-failure
    "$BUILD/native/norm/mixed/packet/native/baseline/FacetAreaScoreValidation" --unit
    "$BUILD/native/norm/mixed/packet/native/baseline/FacetAreaProposalValidation" --unit
    "$BUILD/native/norm/mixed/packet/native/baseline/FacetAreaPortValidation" --unit
    "$BUILD/native/norm/mixed/packet/native/baseline/FacetAreaTransportValidation" --unit
    "$BUILD/native/norm/mixed/packet/native/FacetAreaOcclusionValidation" --unit
    "$BUILD/native/norm/mixed/packet/FacetPacketValidation" --unit
    "$BUILD/native/norm/mixed/FacetMixedValidation" --unit
    "$BUILD/native/norm/FacetNormValidation" --unit
    "$BUILD/native/FacetCausalValidation" --unit
    "$BUILD/native/FacetCausalValidation" "$BUILD/captures"
    "$BUILD/FacetCausalGpuValidation" "$DRIVER" "$BUILD" "$BUILD/captures" "$BUILD/gpu-captures"
    sha256sum "$BUILD/FacetCausalGpuSubmission.h" "$BUILD"/gpu-captures/*.f32
    sha256sum "$BUILD/native/norm/mixed/packet/native/baseline/FacetAreaProposalScenes.h" "$BUILD/native/norm/FacetNormScenes.h" "$BUILD/native/FacetCausalReplay.h" "$BUILD"/captures/*
} 2>&1 | tee "Exhibits/LightCompute/Results/FacetCausalGpu-$MODE.txt"
