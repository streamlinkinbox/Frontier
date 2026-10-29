#!/usr/bin/env bash
# 🧩 Recompile the preceding default policy and verify its unchanged input buffers with the new validator.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TOOLS="$ROOT/ExternalPackages/VulkanCompute"
SLANG="${SLANGC:-$TOOLS/Slang/slangtorch/bin/slangc}"
DRIVER="${VULKAN_LIBRARY:-$TOOLS/SwiftShader/libvulkan.so.1}"
if [[ -z "${VULKAN_LIBRARY:-}" ]]; then export VK_ICD_FILENAMES="$TOOLS/SwiftShader/vk_swiftshader_icd.json"; fi
export LD_LIBRARY_PATH="$TOOLS/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
BUILD="$ROOT/_ClaudeScratch/build/query-calibration-release"
OUTPUT="$ROOT/_ClaudeScratch/query-calibration-regression"
mkdir -p "$BUILD/regression" "$OUTPUT"
{
    printf 'Default-policy regression revision=%s working_changes=1\n' "$(git rev-parse HEAD)"
    sha256sum Exhibits/LightCompute/Code/{QueryElisionGpu.slang,TracedQuartetGpu.slang,TracedQuartetValidation.h,LightComputeValidation.cpp} \
        Exhibits/LightCompute/RunQueryCalibrationRegression.sh
    for PAIR in 'GenerateVertexPayload DiffuseGenerate' 'EvaluateVertexPayload DiffuseResolve'; do
        read -r ENTRY STEM <<< "$PAIR"
        "$SLANG" Exhibits/LightCompute/Code/QueryElisionGpu.slang \
            -I Frontier/Engine/Rendering/Shaders -I Exhibits/LightCompute/Code -entry "$ENTRY" -target spirv -profile spirv_1_3 \
            -emit-spirv-directly -O0 -o "$BUILD/regression/$STEM.spv"
    done
    sha256sum "$BUILD/regression"/*.spv
    "$BUILD/LightComputeValidation" "$DRIVER" "$BUILD/regression" --query-elision "$OUTPUT"
    sha256sum "$OUTPUT"/*.f32
} 2>&1 | tee Exhibits/LightCompute/Results/QueryCalibration-regression-release.txt
